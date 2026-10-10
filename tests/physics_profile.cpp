// Standalone per-step cost breakdown of one character's secondary (nanoem/Bullet) world.
// Usage: mmdhl_profile (--pmx <file> | --cache <root> --asset <id>) [--steps N] [--warmup N]
//        [--iterations N] [--mode 0|1|2] [--workers N] [--backend reference|cpu_mt|cpu_mt_v2]
//        [--no-profile] [--no-deform] [--no-midphase] [--margin <pmx units>]
//        [--broadphase auto|dbvt|dbvt-fast|sap] [--scene contacts] [--static] [--sleep] [--json <file>]
// Configure the build with -DMMDHL_BULLET_PROFILE=ON to get Bullet's phase timing, accumulated over
// every measured step (Bullet itself resets its profile at the start of each step).
// An asynchronous backend (cpu_mt_v2) is measured per tick: each frame waits for the queued tick
// before presenting it, so the reported step cost is the tick job's own work.
#include "runtime.hpp"
#include "secondary.hpp"
#include "rig.hpp"
#include "jobs.hpp"
#include "scene.hpp"
#include "broadphase.hpp"
#include "ordered_dispatcher.hpp"
#include "vulkan_solver.hpp"
#include "contact_fixture.hpp"
#include "crash_report.hpp"
#include <LinearMath/btQuickprof.h>
#include <functional>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <numeric>
#include <string>
#include <vector>
#include "wide_main.hpp"
using namespace mmd;
namespace {
struct Stats {
 std::vector<double> samples;
 void add(double v){samples.push_back(v);}
 double q(double p){if(samples.empty())return 0;auto s=samples;std::sort(s.begin(),s.end());return s[std::min(s.size()-1,size_t(p*s.size()))];}
 double mean(){return samples.empty()?0:std::accumulate(samples.begin(),samples.end(),0.)/samples.size();}
};
double ms(std::chrono::steady_clock::time_point a){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-a).count();}
// Bullet's profile tree holds the last step only; fold it into per-path totals after every step.
struct ProfileTotals {
 struct Entry {double ms=0;uint64_t calls=0;};
 std::map<std::string,Entry> entries;unsigned frames=0;
 // The runtime DLL owns Bullet's profile tree (this executable links its own
 // LinearMath copy), so the snapshot comes through the Secondary API.
 void accumulate(){
  for(auto& node:Secondary::profilerSnapshot()){auto& e=entries[node["path"].get<std::string>()];e.ms+=node["ms"].get<double>();e.calls+=node["calls"].get<uint64_t>();}
  ++frames;
 }
 void print(){
  if(!frames)return;std::cout<<"bullet phases (ms per step, averaged over "<<frames<<" steps):\n";
  for(auto& [path,e]:entries){size_t depth=std::count(path.begin(),path.end(),'/');std::cout<<"  "<<std::string(depth*2-2,' ')<<path.substr(path.rfind('/')+1)<<"  "<<e.ms/frames<<" ms  ("<<double(e.calls)/frames<<" calls)\n";}
 }
};
}
int wmain(int argc,wchar_t** argv){
 test::installCrashReport();
 try{
  std::vector<std::wstring> args(argv+1,argv+argc);
  fs::path cache,pmx;std::string asset;int steps=600,warmup=120,iterations=-1,mode=0,workers=0;std::string broadphase="auto",scene,backend="reference";fs::path jsonPath;bool dumpProfile=true,deformTest=true,staticPose=false,sleepPolicy=false,midphase=true,stretch=false;double margin=-1;
  for(size_t i=0;i<args.size();i++){auto& a=args[i];auto next=[&]{if(i+1>=args.size())throw std::runtime_error("Missing argument value");return args[++i];};
   if(a==L"--cache")cache=next();else if(a==L"--asset")asset=utf8(next());else if(a==L"--pmx")pmx=next();else if(a==L"--steps")steps=std::stoi(next());else if(a==L"--warmup")warmup=std::stoi(next());
   else if(a==L"--iterations")iterations=std::stoi(next());else if(a==L"--mode")mode=std::stoi(next());else if(a==L"--workers")workers=std::stoi(next());else if(a==L"--backend")backend=utf8(next());else if(a==L"--static")staticPose=true;else if(a==L"--sleep")sleepPolicy=true;else if(a==L"--broadphase")broadphase=utf8(next());else if(a==L"--scene")scene=utf8(next());else if(a==L"--json")jsonPath=next();else if(a==L"--no-profile")dumpProfile=false;else if(a==L"--no-deform")deformTest=false;
   else if(a==L"--no-midphase")midphase=false;else if(a==L"--margin")margin=std::stod(next());
   else if(a==L"--stretch")stretch=true;
   else if(a==L"--order"){auto order=utf8(next());if(order!="colored"&&order!="ordered")throw std::runtime_error("--order must be colored or ordered");setVulkanColoring(order=="colored");}
   else throw std::runtime_error("Unknown argument: "+utf8(a));}
  if(steps<=0||warmup<2||workers<0||workers>32||mode<0||mode>2||iterations==0||iterations< -1)throw std::runtime_error("Invalid benchmark options");
  if(!scene.empty()&&scene!="contacts")throw std::runtime_error("Scene must be contacts");
  if(broadphase=="0")broadphase="dbvt";if(broadphase=="2")broadphase="sap";isSapBroadphase(broadphase);
  if((mode>0)!=(scene=="contacts"))throw std::runtime_error("Collision modes 1/2 require --scene contacts; mode 0 uses no scene");
  setMidphaseGate(midphase);if(margin>=0)setSecondaryDbvtMargin(float(margin));
  std::shared_ptr<Model> model;auto loadStart=std::chrono::steady_clock::now();
  if(!pmx.empty())model=parse(readFile(pmx));else if(!cache.empty()&&!asset.empty())model=loadAsset(cache,asset);
  else throw std::runtime_error("Usage: mmdhl_profile (--pmx <file> | --cache <root> --asset <id>) [--steps N] [--warmup N] [--iterations N] [--mode 0|1|2] [--scene contacts] [--broadphase auto|dbvt|dbvt-fast|sap] [--workers N] [--backend name] [--no-profile] [--no-deform] [--no-midphase] [--margin units] [--json <file>]");
  nanoem_rsize_t ikCount=0;nanoemModelGetAllConstraintObjects(model->source,&ikCount);
  std::cout<<"model "<<model->name<<" vertices="<<model->vertices.size()<<" bones="<<model->bones.size()<<" ik="<<ikCount<<" bodies="<<model->bodies.size()<<" joints="<<model->joints.size()<<" soft="<<model->softBodies.size()<<" load="<<ms(loadStart)<<" ms\n";
  setWorkerCount(unsigned(workers));
  const auto measuredWorkers=workerCount();
  auto fitStart=std::chrono::steady_clock::now();
  Secondary::setSleepPolicy(sleepPolicy,.5f,.35f,1.5f);Secondary::setTuning(iterations>0?iterations:10,1,1,stretch,1);
  World host;auto handle=host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",mode},{"secondaryBroadphase",broadphase},{"secondaryBackend",backend}});auto& p=host.get(handle);auto& rig=*p.sourceRig;
  const bool asynchronous=p.secondary->asynchronous();
  std::cout<<"rig fitted in "<<ms(fitStart)<<" ms; workers="<<workerCount()<<" broadphase="<<broadphase<<" backend="<<p.secondary->effectiveBackend<<(asynchronous?" (asynchronous, measured per tick)":"")<<" midphase="<<midphase<<"\n";
  auto world=p.secondary->dynamics();
  if(iterations>0)world->getSolverInfo().m_numIterations=iterations;
  int spheres=0,boxes=0,capsules=0,kinematic=0,other=0;
  for(int i=0;i<world->getNumCollisionObjects();i++){auto o=world->getCollisionObjectArray()[i];
   switch(o->getCollisionShape()->getShapeType()){case SPHERE_SHAPE_PROXYTYPE:spheres++;break;case BOX_SHAPE_PROXYTYPE:boxes++;break;case CAPSULE_SHAPE_PROXYTYPE:capsules++;break;default:other++;break;}
   if(o->isKinematicObject())kinematic++;}
  int rows=0;std::map<int,int> jointTypes;
  for(int i=0;i<world->getNumConstraints();i++){auto c=world->getConstraint(i);jointTypes[c->getConstraintType()]++;btTypedConstraint::btConstraintInfo1 info{};c->getInfo1(&info);rows+=info.m_numConstraintRows;}
  std::cout<<"world objects="<<world->getNumCollisionObjects()<<" (sphere "<<spheres<<", box "<<boxes<<", capsule "<<capsules<<", other "<<other<<", kinematic "<<kinematic<<") constraints="<<world->getNumConstraints()<<" rows="<<rows
   <<" iterations="<<world->getSolverInfo().m_numIterations<<" solverMode="<<world->getSolverInfo().m_solverMode<<" splitImpulse="<<world->getSolverInfo().m_splitImpulse<<"\n";
  for(auto& [type,count]:jointTypes)std::cout<<"  constraint type "<<type<<": "<<count<<"\n";
  // Rigid whole-body motion about the pelvis, similar to tests/game/native-stress.lua.
  btVector3 pivot=rig.bones[0].rest.getOrigin();
  auto submit=[&](int frame){double t=frame/60.,phase=staticPose?0.:t;
   if(mode)publishScene(test::contactScene(t,mode));
   btVector3 offset(float(std::sin(phase*.7)*24),float(std::sin(phase*.9)*16),float(std::sin(phase*1.1)*14));
   btQuaternion q;q.setEulerZYX(float(std::sin(phase*.6)*18*SIMD_RADS_PER_DEG),float(std::sin(phase)*12*SIMD_RADS_PER_DEG),float(std::sin(phase*.8)*10*SIMD_RADS_PER_DEG));
   btTransform drive=btTransform(btQuaternion::getIdentity(),pivot+offset)*btTransform(q,btVector3(0,0,0))*btTransform(btQuaternion::getIdentity(),-pivot);
   std::vector<btTransform> palette;palette.reserve(rig.bones.size());for(auto& b:rig.bones)palette.push_back(drive*b.rest);
   p.submitPresentationPose(palette,t,uint64_t(frame)+1);
   if(asynchronous)p.secondary->waitAsyncIdle();
   p.stepSource();};
  for(int f=0;f<warmup;f++)submit(f);
  p.secondary->waitAsyncIdle();
  bool profiling=Secondary::profilerReset(dumpProfile);auto before=p.secondary->frameStats();
  p.secondary->setPairCounting(true);
  int maxExternalContacts=0;bool finite=true;
  std::cout<<"profileEnabled="<<profiling<<" collisionMode="<<mode<<" scene="<<(mode?scene:"none")<<"\n";
  Stats frameMs,physMs,poseMs,tickMs;unsigned stepCount=0;ProfileTotals totals;uint64_t digest=14695981039346656037ull;
  for(int f=warmup;f<warmup+steps;f++){auto start=std::chrono::steady_clock::now();submit(f);frameMs.add(ms(start));
   auto stats=p.secondary->frameStats();physMs.add(stats.physicsMs);poseMs.add(stats.poseMs);tickMs.add(stats.lastMs);stepCount+=stats.steps;
   if(profiling)totals.accumulate();
   // Trajectory digest (FNV-1a over every transform after every step) to compare exactness across builds/flags.
   for(int i=0;i<world->getNumCollisionObjects();i++){const auto& t=world->getCollisionObjectArray()[i]->getWorldTransform();for(int row=0;row<3;row++){finite&=std::isfinite(t.getOrigin()[row]);for(int col=0;col<3;col++)finite&=std::isfinite(t.getBasis()[row][col]);}
    float values[12]={t.getOrigin().x(),t.getOrigin().y(),t.getOrigin().z(),t.getBasis()[0].x(),t.getBasis()[0].y(),t.getBasis()[0].z(),t.getBasis()[1].x(),t.getBasis()[1].y(),t.getBasis()[1].z(),t.getBasis()[2].x(),t.getBasis()[2].y(),t.getBasis()[2].z()};
    unsigned char bytes[sizeof(values)];std::memcpy(bytes,values,sizeof(values));for(auto byte:bytes){digest^=byte;digest*=1099511628211ull;}}
   if(mode){auto d=p.secondary->diagnostics(false);maxExternalContacts=std::max(maxExternalContacts,d["externalContacts"].get<int>());}}
  auto after=p.secondary->frameStats();
  if(sleepPolicy){p.secondary->waitAsyncIdle();auto d=p.secondary->diagnostics(true);std::map<int,int> activation;int slow=0,total=0;float maxSpeed=0,maxSpin=0;
   for(auto& b:d["bodyList"]){if(!b.contains("activation"))continue;activation[b["activation"].get<int>()]++;total++;float speed=b["speed"],spin=b["spin"];if(speed<.5f&&spin<.35f)slow++;maxSpeed=std::max(maxSpeed,speed);maxSpin=std::max(maxSpin,spin);}
   std::cout<<"sleep: sleeping="<<d["sleepingBodies"]<<" slow="<<slow<<"/"<<total<<" maxSpeed="<<maxSpeed<<" maxSpin="<<maxSpin<<" activation";for(auto& [state,count]:activation)std::cout<<" "<<state<<":"<<count;std::cout<<"\n";}
  auto dispatcher=world->getDispatcher();int manifolds=dispatcher->getNumManifolds(),contacts=0;for(int i=0;i<manifolds;i++)contacts+=dispatcher->getManifoldByIndexInternal(i)->getNumContacts();
  std::cout<<"steps="<<stepCount<<" frames="<<steps<<" resets="<<after.resets<<" dropped="<<after.dropped<<" error="<<p.sourceError<<"\n";
  std::cout<<"frame wall ms (submit, tick, present): mean="<<frameMs.mean()<<" p50="<<frameMs.q(.5)<<" p95="<<frameMs.q(.95)<<" max="<<frameMs.q(1)<<"\n";
  std::cout<<"bullet step ms: mean="<<physMs.mean()<<" p50="<<physMs.q(.5)<<" p95="<<physMs.q(.95)<<" max="<<physMs.q(1)<<"\n";
  std::cout<<"secondary tick ms (scene sync, pose, step, guards, publish): mean="<<tickMs.mean()<<" p50="<<tickMs.q(.5)<<" p95="<<tickMs.q(.95)<<"\n";
  std::cout<<"pose evaluate ms ("<<(asynchronous?"one tick-local evaluation per tick":"three evaluations per frame")<<"): mean="<<poseMs.mean()<<"\n";
  std::cout<<"overlapping pairs="<<world->getPairCache()->getNumOverlappingPairs()<<" manifolds="<<manifolds<<" contacts="<<contacts<<"\n";
  {char hex[32];std::snprintf(hex,sizeof(hex),"%016llx",static_cast<unsigned long long>(digest));std::cout<<"trajectory="<<hex<<"\n";}
  {auto d=p.secondary->diagnostics(false);if(d.contains("compute"))std::cout<<"compute="<<d["compute"].dump()<<"\n";}
  if(stretch){auto d=p.secondary->diagnostics(false);double n=std::max(1u,stepCount);std::cout<<"stretch per step: guardMs="<<(after.guardTotalMs-before.guardTotalMs)/n<<" corrections="<<d["stretchCorrections"].get<double>()/n<<" clamps="<<d["stretchContactClamps"].get<double>()/n<<std::endl;}
  {auto d=p.secondary->diagnostics(false);if(d.contains("midphase")){double n=std::max(1u,stepCount);std::cout<<"midphase per step: dispatched="<<d["midphase"]["dispatched"].get<double>()/n<<" gated="<<d["midphase"]["gated"].get<double>()/n<<" (by capsule bound "<<d["midphase"]["swept"].get<double>()/n<<")\n";}}
  std::cout.flush();
  {
   // Island structure: union-find over dynamic bodies through constraints and touching manifolds.
   auto& objects=world->getCollisionObjectArray();std::map<const btCollisionObject*,int> index;for(int i=0;i<objects.size();i++)index[objects[i]]=i;
   std::vector<int> parent(objects.size());std::iota(parent.begin(),parent.end(),0);
   std::function<int(int)> find=[&](int i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
   auto unite=[&](const btCollisionObject* a,const btCollisionObject* b){if(!a||!b||a->isStaticOrKinematicObject()||b->isStaticOrKinematicObject()||!index.contains(a)||!index.contains(b))return;int x=find(index[a]),y=find(index[b]);if(x!=y)parent[x]=y;};
   for(int i=0;i<world->getNumConstraints();i++){auto c=world->getConstraint(i);unite(&c->getRigidBodyA(),&c->getRigidBodyB());}
   for(int i=0;i<manifolds;i++){auto m=dispatcher->getManifoldByIndexInternal(i);if(m->getNumContacts())unite(m->getBody0(),m->getBody1());}
   std::map<int,std::pair<int,int>> islands;
   for(int i=0;i<objects.size();i++)if(!objects[i]->isStaticOrKinematicObject())islands[find(i)].first++;
   for(int i=0;i<world->getNumConstraints();i++){auto c=world->getConstraint(i);const btCollisionObject* a=&c->getRigidBodyA();const btCollisionObject* b=&c->getRigidBodyB();auto pick=!a->isStaticOrKinematicObject()?a:b;if(pick->isStaticOrKinematicObject()||!index.contains(pick))continue;btTypedConstraint::btConstraintInfo1 info{};c->getInfo1(&info);islands[find(index[pick])].second+=info.m_numConstraintRows;}
   std::vector<std::pair<int,int>> sizes;for(auto& [root,s]:islands)sizes.push_back(s);std::sort(sizes.rbegin(),sizes.rend());
   std::cout<<"islands="<<sizes.size()<<" largest (bodies,rows):";for(size_t i=0;i<std::min<size_t>(10,sizes.size());i++)std::cout<<" ("<<sizes[i].first<<","<<sizes[i].second<<")";std::cout<<"\n";
  }
  std::cout.flush();
  if(profiling)totals.print();Secondary::profilerReset(false);p.secondary->setPairCounting(false);
  if(deformTest){
   auto measure=[&](unsigned n){setWorkerCount(n);Stats d;for(int i=0;i<30;i++){p.poseDirty=true;auto s=std::chrono::steady_clock::now();p.ensureSnapshot();d.add(ms(s));}return d;};
   auto multi=measure(unsigned(workers));unsigned count=workerCount();auto single=measure(1);
   Stats evaluate;for(int i=0;i<30;i++){auto s=std::chrono::steady_clock::now();p.evaluate(false);evaluate.add(ms(s));}
   std::cout<<"deform ms: workers="<<count<<" p50="<<multi.q(.5)<<" ; single-thread p50="<<single.q(.5)<<" ("<<single.q(.5)*1e6/double(std::max<size_t>(1,model->vertices.size()))<<" ns/vertex)\n";
   std::cout<<"evaluate ms (bones+IK, single): p50="<<evaluate.q(.5)<<"\n";
  }
  auto diagnostics=p.secondary->diagnostics(false);
  bool passed=finite&&p.sourceError.empty()&&stepCount==unsigned(steps)&&after.resets==before.resets&&after.dropped==before.dropped&&after.accumulator<=1./60+1e-6&&(!mode||maxExternalContacts>0);
  Json report={{"passed",passed},{"broadphaseRequested",broadphase},{"backend",p.secondary->effectiveBackend},{"asynchronous",asynchronous},{"midphase",midphase},{"profileEnabled",profiling},{"scene",scene},{"workersRequested",workers},{"workersMeasured",measuredWorkers},{"steps",stepCount},{"warmup",warmup},{"finite",finite},{"maxExternalContacts",maxExternalContacts},{"measurementResets",after.resets-before.resets},{"measurementDropped",after.dropped-before.dropped},{"physics",{{"mean",physMs.mean()},{"p50",physMs.q(.5)},{"p95",physMs.q(.95)}}},{"tick",{{"mean",tickMs.mean()},{"p50",tickMs.q(.5)},{"p95",tickMs.q(.95)}}},{"wall",{{"mean",frameMs.mean()},{"p50",frameMs.q(.5)},{"p95",frameMs.q(.95)}}},{"diagnostics",diagnostics}};
  if(profiling){Json phases=Json::object();for(auto& [path,e]:totals.entries)phases[path]={{"ms",e.ms/std::max(1u,totals.frames)},{"calls",double(e.calls)/std::max(1u,totals.frames)}};report["phases"]=phases;}
  if(!jsonPath.empty())writeJson(jsonPath,report);std::cout<<"validation="<<(passed?"PASS":"FAIL")<<" externalContacts="<<maxExternalContacts<<"\n";
  publishScene(nullptr);shutdownJobs();return passed?0:1;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";shutdownJobs();return 1;}
}
