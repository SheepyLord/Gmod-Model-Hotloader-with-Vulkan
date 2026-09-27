#include "compute_solver.hpp"
#include "opencl_solver_kernel.hpp"
#include "compute_rows.hpp"
#include "vulkan_solver.hpp"
#include "jobs.hpp"
#include "ordered_joint_setup.hpp"
#include <BulletCollision/NarrowPhaseCollision/btPersistentManifold.h>
#include <clew/clew.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <array>
#include <cstring>
#include <condition_variable>
#include <future>
#include <atomic>
#include <cstdlib>
#include <unordered_set>
namespace mmd {namespace {
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
F4 pack(const btVector3& v,float w=0){return {v.x(),v.y(),v.z(),w};}
btVector3 unpack(F4 v){return {v.x,v.y,v.z};}
void check(cl_int code,const char* operation){if(code!=CL_SUCCESS)throw std::runtime_error(std::string(operation)+" failed (OpenCL "+std::to_string(code)+")");}
struct Device {
 std::mutex mutex;cl_context context=nullptr;cl_device_id device=nullptr;cl_command_queue queue=nullptr;cl_program program=nullptr;cl_kernel kernel=nullptr;
 std::array<cl_mem,6> memory{};std::array<size_t,6> capacity{};
 bool initialized=false,available=false;std::string name,driver,error;double kernelMs=0,transferMs=0,waitMs=0,wallMs=0;size_t uploadBytes=0,downloadBytes=0,workgroupSize=256;unsigned growths=0;
 // A timed-out driver may still DMA into its host buffers. Quarantine those
 // allocations and the context for the process lifetime, never reuse/free them.
 std::vector<std::shared_ptr<Buffers>>* quarantine=nullptr;
 ~Device(){shutdown();}
 void shutdown(){
  if(quarantine)return;
  for(auto& p:memory)if(p){clReleaseMemObject(p);p=nullptr;}capacity.fill(0);
  if(kernel){clReleaseKernel(kernel);kernel=nullptr;}if(program){clReleaseProgram(program);program=nullptr;}
  if(queue){clReleaseCommandQueue(queue);queue=nullptr;}if(context){clReleaseContext(context);context=nullptr;}
  initialized=available=false;
 }
 void init(){if(initialized)return;initialized=true;try{
  if(clewInit("OpenCL.dll")!=CLEW_SUCCESS)throw std::runtime_error("OpenCL runtime is not installed");
  cl_uint count=0;check(clGetPlatformIDs(0,nullptr,&count),"Enumerate OpenCL platforms");std::vector<cl_platform_id> platforms(count);check(clGetPlatformIDs(count,platforms.data(),nullptr),"Read OpenCL platforms");
  int best=-1;for(auto platform:platforms){cl_uint n=0;if(clGetDeviceIDs(platform,CL_DEVICE_TYPE_GPU,0,nullptr,&n)!=CL_SUCCESS)continue;std::vector<cl_device_id> devices(n);check(clGetDeviceIDs(platform,CL_DEVICE_TYPE_GPU,n,devices.data(),nullptr),"Read OpenCL devices");
   for(auto d:devices){char vendor[512]={};cl_bool compiler=0;cl_uint units=0;clGetDeviceInfo(d,CL_DEVICE_VENDOR,sizeof(vendor),vendor,nullptr);clGetDeviceInfo(d,CL_DEVICE_COMPILER_AVAILABLE,sizeof(compiler),&compiler,nullptr);clGetDeviceInfo(d,CL_DEVICE_MAX_COMPUTE_UNITS,sizeof(units),&units,nullptr);int score=int(units)+(std::strstr(vendor,"NVIDIA")?10000:0);if(compiler&&score>best){device=d;best=score;}}
  }if(!device)throw std::runtime_error("No OpenCL GPU with a compiler is available");
  char text[512]={};check(clGetDeviceInfo(device,CL_DEVICE_NAME,sizeof(text),text,nullptr),"Read GPU name");name=text;check(clGetDeviceInfo(device,CL_DRIVER_VERSION,sizeof(text),text,nullptr),"Read GPU driver");driver=text;
  cl_int e=0;context=clCreateContext(nullptr,1,&device,nullptr,nullptr,&e);check(e,"Create OpenCL context");queue=clCreateCommandQueue(context,device,CL_QUEUE_PROFILING_ENABLE,&e);check(e,"Create OpenCL queue");
  const char* source=SolverKernel;size_t length=std::strlen(source);program=clCreateProgramWithSource(context,1,&source,&length,&e);check(e,"Create Model Hotloader OpenCL program");e=clBuildProgram(program,1,&device,"-cl-std=CL1.2",nullptr,nullptr);if(e!=CL_SUCCESS){size_t n=0;clGetProgramBuildInfo(program,device,CL_PROGRAM_BUILD_LOG,0,nullptr,&n);std::string log(n,'\0');clGetProgramBuildInfo(program,device,CL_PROGRAM_BUILD_LOG,n,log.data(),nullptr);throw std::runtime_error("OpenCL kernel compilation failed: "+log);}
  kernel=clCreateKernel(program,"solve_islands",&e);check(e,"Create solver kernel");
  // Developer benchmark override; this never changes simulation settings.
  if(auto setting=std::getenv("MMDHL_OPENCL_WORKGROUP")){int n=std::atoi(setting);if(n==32||n==64||n==128||n==256)workgroupSize=size_t(n);}
  size_t maximum=0;check(clGetKernelWorkGroupInfo(kernel,device,CL_KERNEL_WORK_GROUP_SIZE,sizeof(maximum),&maximum,nullptr),"Read solver workgroup limit");
  workgroupSize=std::min(workgroupSize,maximum);if(!workgroupSize)throw std::runtime_error("Invalid OpenCL workgroup limit");
  auto probe=clCreateKernel(program,"probe",&e);check(e,"Create capability probe");auto buffer=clCreateBuffer(context,CL_MEM_READ_WRITE,sizeof(F4),nullptr,&e);check(e,"Allocate capability probe");clSetKernelArg(probe,0,sizeof(buffer),&buffer);size_t one=1;check(clEnqueueNDRangeKernel(queue,probe,1,nullptr,&one,&one,0,nullptr,nullptr),"Run capability probe");F4 value;check(clEnqueueReadBuffer(queue,buffer,CL_TRUE,0,sizeof(value),&value,0,nullptr,nullptr),"Read capability probe");clReleaseMemObject(buffer);clReleaseKernel(probe);if(value.x!=1.25f||value.w!=10)throw std::runtime_error("OpenCL capability probe returned invalid data");available=true;
 }catch(const std::exception& e){error=e.what();available=false;}}
 void ensure(int index,size_t bytes){bytes=std::max(size_t(16),bytes);if(bytes<=capacity[index])return;size_t grown=std::max(bytes,capacity[index]+capacity[index]/2);cl_int e=0;auto replacement=clCreateBuffer(context,CL_MEM_READ_WRITE,grown,nullptr,&e);check(e,"Grow OpenCL solver buffer");if(memory[index])clReleaseMemObject(memory[index]);memory[index]=replacement;capacity[index]=grown;growths++;}
 nlohmann::json runBuffers(Buffers& data){auto start=Clock::now();std::unique_lock lock(mutex);waitMs=elapsed(start);init();if(!available)throw std::runtime_error(error);if(data.islands.empty())return nlohmann::json::object();
  std::array<size_t,6> sizes={data.bodies.size()*sizeof(GBody),data.rows.size()*sizeof(GRow),data.impulses.size()*sizeof(F2),data.groups.size()*sizeof(I2),data.levels.size()*sizeof(I2),data.islands.size()*sizeof(GIsland)};
  std::array<void*,6> pointers={data.bodies.data(),data.rows.data(),data.impulses.data(),data.groups.data(),data.levels.data(),data.islands.data()};uploadBytes=0;auto transfer=Clock::now();
  for(int i=0;i<6;i++){ensure(i,sizes[i]);if(sizes[i])check(clEnqueueWriteBuffer(queue,memory[i],CL_FALSE,0,sizes[i],pointers[i],0,nullptr,nullptr),"Upload solver data");uploadBytes+=sizes[i];check(clSetKernelArg(kernel,i,sizeof(cl_mem),&memory[i]),"Bind solver buffer");}
  size_t local=workgroupSize,global=data.islands.size()*local;cl_event event=nullptr;check(clEnqueueNDRangeKernel(queue,kernel,1,nullptr,&global,&local,0,nullptr,&event),"Dispatch constraint solver");
  check(clEnqueueReadBuffer(queue,memory[0],CL_FALSE,0,sizes[0],data.bodies.data(),0,nullptr,nullptr),"Download solved bodies");cl_event done=nullptr;check(clEnqueueReadBuffer(queue,memory[2],CL_FALSE,0,sizes[2],data.impulses.data(),0,nullptr,&done),"Download impulses");clFlush(queue);
  for(;;){cl_int status=0;check(clGetEventInfo(done,CL_EVENT_COMMAND_EXECUTION_STATUS,sizeof(status),&status,nullptr),"Poll OpenCL solver");if(status==CL_COMPLETE)break;if(status<0)throw std::runtime_error("OpenCL solver reported a device execution failure");if(elapsed(transfer)>3000){available=false;error="OpenCL solver timed out";throw std::runtime_error(error);}std::this_thread::yield();}
  cl_ulong begin=0,end=0;clGetEventProfilingInfo(event,CL_PROFILING_COMMAND_START,sizeof(begin),&begin,nullptr);clGetEventProfilingInfo(event,CL_PROFILING_COMMAND_END,sizeof(end),&end,nullptr);kernelMs=double(end-begin)*1e-6;clReleaseEvent(event);clReleaseEvent(done);downloadBytes=sizes[0]+sizes[2];transferMs=std::max(0.,elapsed(transfer)-kernelMs);wallMs=elapsed(start);
  for(const auto& body:data.bodies)for(auto vector:{body.linear,body.angular,body.push,body.turn})if(!std::isfinite(vector.x)||!std::isfinite(vector.y)||!std::isfinite(vector.z))throw std::runtime_error("Non-finite OpenCL velocity");
  for(const auto& impulse:data.impulses)if(!std::isfinite(impulse.x)||!std::isfinite(impulse.y))throw std::runtime_error("Non-finite OpenCL impulse");
  return {{"device",name},{"kernelMs",kernelMs},{"transferMs",transferMs},{"waitMs",waitMs},{"uploadBytes",uploadBytes},{"downloadBytes",downloadBytes},{"bufferGrowths",growths},{"batchIslands",data.islands.size()},{"workgroupSize",workgroupSize}};
 }
 nlohmann::json run(Buffers& data){
  auto staged=std::make_shared<Buffers>(std::move(data));
  try{auto stats=runBuffers(*staged);data=std::move(*staged);return stats;}
  catch(const std::exception& e){std::lock_guard lock(mutex);available=false;error=e.what();if(!quarantine)quarantine=new std::vector<std::shared_ptr<Buffers>>;quarantine->push_back(std::move(staged));throw;}
 }
};
Device& device(){static Device value;return value;}
std::atomic<unsigned> expectedBatch{1};
std::atomic<bool> validateRows{false};
class Coordinator {
 struct Request {Buffers* buffers;std::promise<nlohmann::json> result;};
 std::mutex mutex;std::condition_variable wake;std::vector<std::shared_ptr<Request>> pending;bool stopping=false;std::thread thread;Buffers joined;
 void work(){for(;;){
  std::vector<std::shared_ptr<Request>> batch;{
   std::unique_lock lock(mutex);wake.wait(lock,[&]{return stopping||!pending.empty();});if(stopping&&pending.empty())return;
   if(expectedBatch>1)wake.wait_for(lock,std::chrono::microseconds(250),[&]{return stopping||pending.size()>=expectedBatch.load();});batch.swap(pending);
  }
  try{
   if(batch.size()==1){auto result=device().run(*batch[0]->buffers);result["batchCharacters"]=1;batch[0]->result.set_value(std::move(result));continue;}
   joined.clear();std::vector<std::pair<size_t,size_t>> offsets;
   for(auto& request:batch){auto& source=*request->buffers;int bo=int(joined.bodies.size()),ro=int(joined.rows.size()),go=int(joined.groups.size()),lo=int(joined.levels.size());offsets.emplace_back(bo,ro);
    joined.bodies.insert(joined.bodies.end(),source.bodies.begin(),source.bodies.end());joined.impulses.insert(joined.impulses.end(),source.impulses.begin(),source.impulses.end());
    for(auto row:source.rows){row.ids.x+=bo;row.ids.y+=bo;if(row.ids.z>=0)row.ids.z+=ro;joined.rows.push_back(row);}
    for(auto group:source.groups)joined.groups.push_back({group.x+ro,group.y+ro});for(auto level:source.levels)joined.levels.push_back({level.x+go,level.y+go});
    for(auto island:source.islands){island.rows.x+=ro;island.rows.y+=ro;island.rows.z+=ro;island.rows.w+=bo;island.levels.x+=lo;island.levels.y+=lo;island.levels.z+=lo;island.levels.w+=lo;joined.islands.push_back(island);}
   }
   auto result=device().run(joined);result["batchCharacters"]=batch.size();
   for(size_t i=0;i<batch.size();i++){auto& target=*batch[i]->buffers;auto [bo,ro]=offsets[i];std::copy_n(joined.bodies.begin()+bo,target.bodies.size(),target.bodies.begin());std::copy_n(joined.impulses.begin()+ro,target.impulses.size(),target.impulses.begin());batch[i]->result.set_value(result);}
  }catch(...){auto error=std::current_exception();for(auto& request:batch)request->result.set_exception(error);}
 }}
public:
 Coordinator():thread([this]{work();}){}
 ~Coordinator(){stop();}
 void stop(){{std::lock_guard lock(mutex);stopping=true;}wake.notify_all();if(thread.joinable())thread.join();}
 nlohmann::json run(Buffers& buffers){auto request=std::make_shared<Request>();request->buffers=&buffers;auto result=request->result.get_future();{std::lock_guard lock(mutex);pending.push_back(request);}wake.notify_one();return result.get();}
};
std::unique_ptr<Coordinator>& coordinatorStorage(){static std::unique_ptr<Coordinator> value;return value;}
std::mutex coordinatorMutex;
Coordinator& coordinator(){std::lock_guard lock(coordinatorMutex);(void)device();auto& value=coordinatorStorage();if(!value)value=std::make_unique<Coordinator>();return *value;}
struct Group:OrderedJointSolver {
 std::vector<btCollisionObject*> bodies;std::vector<btPersistentManifold*> manifolds;std::vector<btTypedConstraint*> constraints;btContactSolverInfo info;btIDebugDraw* debug=nullptr;size_t bodyOffset=0,rowOffset=0;
 void setup(btCollisionObject** b,int nb,btPersistentManifold** m,int nm,btTypedConstraint** c,int nc,const btContactSolverInfo& settings,btIDebugDraw* drawer){
  bodies.clear();manifolds.clear();constraints.clear();if(nb)bodies.assign(b,b+nb);if(nm)manifolds.assign(m,m+nm);if(nc)constraints.assign(c,c+nc);info=settings;debug=drawer;
  solveGroupCacheFriendlySetup(b,nb,m,nm,c,nc,info,debug);
 }
 void iterate(){solveGroupCacheFriendlyIterations(bodies.data(),int(bodies.size()),manifolds.data(),int(manifolds.size()),constraints.data(),int(constraints.size()),info,debug);}
 void finish(){solveGroupCacheFriendlyFinish(bodies.data(),int(bodies.size()),info);}
 void append(Buffers& out,bool colored){
  bodyOffset=out.bodies.size();rowOffset=out.rows.size();GIsland island;island.rows.x=int(rowOffset);island.levels.x=int(out.levels.size());island.other={std::max(info.m_numIterations,m_maxOverrideNumSolverIterations),info.m_splitImpulse?1:0,m_tmpSolverBodyPool.size(),info.m_numIterations};island.rows.w=int(bodyOffset);
  for(int i=0;i<m_tmpSolverBodyPool.size();i++){auto& b=m_tmpSolverBodyPool[i];bool writable=b.m_originalBody&&(b.m_invMass.length2()>0||b.m_originalBody->getInvInertiaDiagLocal().length2()>0);out.bodies.push_back({pack(b.m_deltaLinearVelocity),pack(b.m_deltaAngularVelocity),pack(b.m_pushVelocity),pack(b.m_turnVelocity),pack(b.m_invMass,writable?1.f:0.f),pack(b.m_linearFactor),pack(b.m_angularFactor)});}
  auto add=[&](btConstraintArray& pool,int type){
   int first=int(out.rows.size());std::vector<std::vector<I2>> waves;std::vector<int> last(m_tmpSolverBodyPool.size(),-1);std::vector<std::array<uint64_t,2>> used(colored?m_tmpSolverBodyPool.size():0);
   for(int i=0;i<pool.size();){int end=i+1;if(type==0)while(end<pool.size()&&pool[end].m_originalContactPoint==pool[i].m_originalContactPoint)end++;
    int a=pool[i].m_solverBodyIdA,b=pool[i].m_solverBodyIdB;bool wa=out.bodies[bodyOffset+a].invMass.w!=0,wb=out.bodies[bodyOffset+b].invMass.w!=0;
    // Ordered: preserve every shared-body dependency, including springs, so
    // the sweep equals Bullet's. Colored: the lowest color free on both
    // writable bodies, fewer levels but a reordered PGS sweep (visibly softer
    // dense PMX clothing at 10 iterations in the v1 OpenCL experiment).
    int level=std::max(wa?last[a]:-1,wb?last[b]:-1)+1;
    if(colored){
     std::array<uint64_t,2> taken{};for(int k=0;k<2;k++)taken[k]=(wa?used[a][k]:0)|(wb?used[b][k]:0);
     // Past 128 colors a group gets a level of its own after every color.
     level=~taken[0]?std::countr_one(taken[0]):~taken[1]?64+std::countr_one(taken[1]):std::max<int>(128,int(waves.size()));
     if(level<128){if(wa)used[a][level/64]|=1ull<<(level%64);if(wb)used[b][level/64]|=1ull<<(level%64);}
    }
    if(wa)last[a]=std::max(last[a],level);if(wb)last[b]=std::max(last[b],level);if(waves.size()<=size_t(level))waves.resize(level+1);waves[level].push_back({int(out.rows.size()),int(out.rows.size())+end-i});
    for(;i<end;i++){auto& r=pool[i];out.rows.push_back({pack(r.m_contactNormal1),pack(r.m_contactNormal2),pack(r.m_relpos1CrossNormal),pack(r.m_relpos2CrossNormal),pack(r.m_angularComponentA),pack(r.m_angularComponentB),{r.m_rhs,r.m_cfm,r.m_jacDiagABInv,r.m_friction},{r.m_lowerLimit,r.m_upperLimit,r.m_rhsPenetration,float(r.m_overrideNumSolverIterations)},{int(bodyOffset)+a,int(bodyOffset)+b,type>=2?island.rows.y+r.m_frictionIndex:-1,type}});out.impulses.push_back({float(r.m_appliedImpulse),float(r.m_appliedPushImpulse)});
     // normal*invMass exactly as the SIMD row solver forms it; w marks a writable body.
     out.terms.push_back(pack(r.m_contactNormal1*m_tmpSolverBodyPool[r.m_solverBodyIdA].internalGetInvMass(),wa?1.f:0.f));out.terms.push_back(pack(r.m_contactNormal2*m_tmpSolverBodyPool[r.m_solverBodyIdB].internalGetInvMass(),wb?1.f:0.f));}
   }
   for(auto& wave:waves){int begin=int(out.groups.size());out.groups.insert(out.groups.end(),wave.begin(),wave.end());out.levels.push_back({begin,int(out.groups.size())});}return first;
  };
  add(m_tmpSolverNonContactConstraintPool,0);island.levels.y=int(out.levels.size());island.rows.y=add(m_tmpSolverContactConstraintPool,1);island.levels.z=int(out.levels.size());
  add(m_tmpSolverContactFrictionConstraintPool,2);add(m_tmpSolverContactRollingFrictionConstraintPool,3);island.rows.z=int(out.rows.size());island.levels.w=int(out.levels.size());out.islands.push_back(island);
 }
 void restore(const Buffers& data){
  for(int i=0;i<m_tmpSolverBodyPool.size();i++){auto& target=m_tmpSolverBodyPool[i];auto& b=data.bodies[bodyOffset+i];target.m_deltaLinearVelocity=unpack(b.linear);target.m_deltaAngularVelocity=unpack(b.angular);target.m_pushVelocity=unpack(b.push);target.m_turnVelocity=unpack(b.turn);}
  size_t index=rowOffset;for(auto pool:{&m_tmpSolverNonContactConstraintPool,&m_tmpSolverContactConstraintPool,&m_tmpSolverContactFrictionConstraintPool,&m_tmpSolverContactRollingFrictionConstraintPool})for(int i=0;i<pool->size();i++){auto impulse=data.impulses[index++];if(!std::isfinite(impulse.x)||!std::isfinite(impulse.y))throw std::runtime_error("Non-finite OpenCL impulse");(*pool)[i].m_appliedImpulse=impulse.x;(*pool)[i].m_appliedPushImpulse=impulse.y;}
 }
 std::pair<double,double> compare(const Buffers& gpu){
  // Setup and warm-start are identical. Compare only the solve, before any
  // contact detection/integration can amplify tiny floating-point differences.
  iterate();double velocity=0,impulse=0;
  for(int i=0;i<m_tmpSolverBodyPool.size();i++){
   const auto& a=m_tmpSolverBodyPool[i];const auto& b=gpu.bodies[bodyOffset+i];
   for(auto delta:{a.m_deltaLinearVelocity-unpack(b.linear),a.m_deltaAngularVelocity-unpack(b.angular),a.m_pushVelocity-unpack(b.push),a.m_turnVelocity-unpack(b.turn)})velocity=std::max(velocity,double(delta.length()));
  }
  size_t index=rowOffset;
  for(auto pool:{&m_tmpSolverNonContactConstraintPool,&m_tmpSolverContactConstraintPool,&m_tmpSolverContactFrictionConstraintPool,&m_tmpSolverContactRollingFrictionConstraintPool})for(int i=0;i<pool->size();i++){
   const auto& b=gpu.impulses[index++];impulse=std::max({impulse,double(btFabs(float((*pool)[i].m_appliedImpulse)-b.x)),double(btFabs(float((*pool)[i].m_appliedPushImpulse)-b.y))});
  }
  return {velocity,impulse};
 }
};
}
struct ComputeSolver::Impl {ComputeBackend backend;bool gpu,colored=false;std::vector<std::unique_ptr<Group>> groups;size_t count=0,finished=0;std::unordered_set<const btRigidBody*> kinematicPending;Buffers buffers;double setupMs=0,solveMs=0,finishMs=0,packMs=0;std::string failure;nlohmann::json gpuStats=nlohmann::json::object();explicit Impl(ComputeBackend value):backend(value),gpu(value!=ComputeBackend::Cpu){}};
ComputeSolver::ComputeSolver(ComputeBackend backend):impl(std::make_unique<Impl>(backend)){if(backend==ComputeBackend::Vulkan)countVulkanWorld(1);}
ComputeSolver::~ComputeSolver(){if(impl->backend==ComputeBackend::Vulkan)countVulkanWorld(-1);}
void ComputeSolver::prepareSolve(int,int){impl->count=impl->finished=0;impl->kinematicPending.clear();impl->setupMs=impl->solveMs=impl->finishMs=impl->packMs=0;}
btScalar ComputeSolver::solveGroup(btCollisionObject** b,int nb,btPersistentManifold** m,int nm,btTypedConstraint** c,int nc,const btContactSolverInfo& info,btIDebugDraw* debug,btDispatcher*){
 // A PMX follower can remain kinematic with nonzero authored inverse mass.
 // Bullet writes its solved velocity back, so a later island sharing it must
 // see that write before row setup. Flush precisely those dependencies.
 std::unordered_set<const btRigidBody*> incoming;
 auto inspect=[&](const btCollisionObject* body){auto rigid=btRigidBody::upcast(body);if(rigid&&rigid->isKinematicObject()&&(rigid->getInvMass()!=0||rigid->getInvInertiaDiagLocal().length2()!=0))incoming.insert(rigid);};
 // A manifold without points creates no rows, so it neither reads nor writes
 // a solver body; dense rigs keep thousands of such empty manifolds alive.
 for(int i=0;i<nb;i++)inspect(b[i]);for(int i=0;i<nm;i++){if(!m[i]->getNumContacts())continue;inspect(m[i]->getBody0());inspect(m[i]->getBody1());}for(int i=0;i<nc;i++){inspect(&c[i]->getRigidBodyA());inspect(&c[i]->getRigidBodyB());}
 for(auto body:incoming)if(impl->kinematicPending.contains(body)){allSolved(info,debug);break;}
 impl->kinematicPending.insert(incoming.begin(),incoming.end());
 // Randomized row sweeps carry solver RNG state between islands; keep the
 // original solver in that uncommon mode instead of changing its semantics.
 if(info.m_solverMode&SOLVER_RANDMIZE_ORDER){allSolved(info,debug);impl->failure="Randomized solver rows require sequential CPU solving";return btSequentialImpulseConstraintSolver::solveGroup(b,nb,m,nm,c,nc,info,debug,nullptr);}
 if(impl->gpu&&(info.m_solverMode&SOLVER_INTERLEAVE_CONTACT_AND_FRICTION_CONSTRAINTS))impl->failure="Interleaved contact/friction rows require CPU solving";
 auto start=Clock::now();if(impl->count==impl->groups.size())impl->groups.push_back(std::make_unique<Group>());impl->groups[impl->count++]->setup(b,nb,m,nm,c,nc,info,debug);impl->setupMs+=elapsed(start);return 0;
}
void ComputeSolver::allSolved(const btContactSolverInfo&,btIDebugDraw*){
 if(impl->finished==impl->count)return;
 auto start=Clock::now();bool solved=false;
 if(impl->gpu&&impl->failure.empty())try{
  bool vulkan=impl->backend==ComputeBackend::Vulkan;impl->colored=vulkan&&vulkanColoring();
  auto packing=Clock::now();impl->buffers.clear();for(size_t i=impl->finished;i<impl->count;i++)impl->groups[i]->append(impl->buffers,impl->colored);impl->packMs+=elapsed(packing);
  if(!impl->buffers.rows.empty()){
   impl->gpuStats=vulkan?runVulkanSolver(impl->buffers):coordinator().run(impl->buffers);
   if(validateRows){double velocity=0,impulse=0;for(size_t i=impl->finished;i<impl->count;i++){auto errors=impl->groups[i]->compare(impl->buffers);velocity=std::max(velocity,errors.first);impulse=std::max(impulse,errors.second);}impl->gpuStats["rowVelocityError"]=velocity;impl->gpuStats["rowImpulseError"]=impulse;}
   for(size_t i=impl->finished;i<impl->count;i++)impl->groups[i]->restore(impl->buffers);solved=true;
  }
 }catch(const std::exception& e){impl->failure=e.what();}
 if(!solved)parallelForSecondary(impl->count-impl->finished,1,[&](size_t a,size_t b){for(size_t i=a;i<b;i++)impl->groups[impl->finished+i]->iterate();});
 impl->solveMs+=elapsed(start);start=Clock::now();for(size_t i=impl->finished;i<impl->count;i++)impl->groups[i]->finish();impl->finishMs+=elapsed(start);impl->finished=impl->count;impl->kinematicPending.clear();
}
void ComputeSolver::reset(){for(auto& group:impl->groups)group->reset();}
nlohmann::json ComputeSolver::diagnostics()const{
 auto out=nlohmann::json{{"islands",impl->count},{"setupMs",impl->setupMs},{"solveMs",impl->solveMs},{"finishMs",impl->finishMs},{"packMs",impl->packMs},{"rowGroups",impl->buffers.groups.size()},{"levels",impl->buffers.levels.size()},{"failure",impl->failure},{"solver",!impl->gpu||!impl->failure.empty()?"cpu_ordered_islands":impl->backend==ComputeBackend::OpenCl?"opencl_ordered_pgs":impl->colored?"vulkan_colored_pgs":"vulkan_ordered_pgs"}};
 if(impl->gpu)out.update(impl->gpuStats);return out;
}
void setComputeBatchSize(unsigned count){expectedBatch=std::max(1u,count);}
void setComputeValidation(bool enabled){validateRows=enabled;}
void shutdownCompute(){{std::lock_guard lock(coordinatorMutex);coordinatorStorage().reset();auto& d=device();std::lock_guard deviceLock(d.mutex);d.shutdown();}shutdownVulkan();}
nlohmann::json openclCapabilities(bool isolated){
 static const nlohmann::json preflight=isolated?probeOpenClWorker():nlohmann::json{{"available",true}};
 if(isolated&&!preflight.value("available",false))return preflight;
 auto& d=device();std::lock_guard lock(d.mutex);d.init();return {{"available",d.available},{"device",d.name},{"driver",d.driver},{"error",d.error},{"api","OpenCL 1.2"},{"scope","constraint iterations; CPU collision detection and integration"},{"experimental",true},{"validated",false}};}
}
