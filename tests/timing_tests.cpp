#include "runtime.hpp"
#include "secondary.hpp"
#include "rig.hpp"
#include "jobs.hpp"
#include "mesh_topology.hpp"
#include "vertex_upload.hpp"
#include <atomic>
#include <iostream>
using namespace mmd;
int main(int argc,char** argv){try{
 if(argc<2||argc>3)throw std::runtime_error("Pass the cloth fixture and optional broadphase");if(argc==3)setSecondaryBroadphaseDefault(argv[2]);auto model=parse(readFile(argv[1]));
 int checks=0;auto check=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);++checks;std::cout<<"PASS "<<name<<"\n";};
 alignas(16) float upload[16]{};DrawVertex v{};v.x=1;v.y=2;v.z=3;v.nx=4;v.ny=5;v.nz=6;v.u=7;v.v=8;v.tx=9;v.ty=10;v.tz=11;v.tw=-1;
 streamSourceVertex(upload,v);_mm_sfence();uint32_t color;std::memcpy(&color,upload+6,4);
 check(upload[0]==1&&upload[3]==4&&upload[4]==5&&upload[5]==6&&color==0xffffffff&&upload[7]==7&&upload[8]==8&&upload[9]==9&&upload[10]==10&&upload[11]==11&&upload[12]==-1&&upload[13]==0&&upload[14]==0&&upload[15]==0,"streamed Source vertex layout preserves positions, normals, UVs, tangents and color");
 {
  // Compressed common vertex: positions, colour and UV bit-exact at their offsets;
  // normal and tangent survive Source's UBYTE4 encode and shader decode closely.
  alignas(16) float compact[8]{};streamCompactVertex(compact,v);_mm_sfence();uint32_t packed,colour;std::memcpy(&packed,compact+3,4);std::memcpy(&colour,compact+4,4);
  check(compact[0]==1&&compact[1]==2&&compact[2]==3&&colour==0xffffffff&&compact[5]==7&&compact[6]==8&&compact[7]==0,"compact Source vertex keeps position, colour and UV at the 32-byte layout offsets");
  uint32_t seed=7;auto next=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/float(1u<<24)*2-1;};
  double worstNormal=0,worstTangent=0;bool signs=true;
  for(int i=0;i<20000;i++){DrawVertex r{};btVector3 n(next(),next(),next()),t(next(),next(),next());if(n.length2()<1e-4f||t.length2()<1e-4f)continue;n.normalize();t.normalize();
   r.nx=n.x();r.ny=n.y();r.nz=n.z();r.tx=t.x();r.ty=t.y();r.tz=t.z();r.tw=i%2?-1.f:1.f;uint32_t bits=packNormalTangent(r);float dn[3],dt[3],sign=0;unpackUbyte4(bits,false,dn);unpackUbyte4(bits,true,dt,&sign);
   worstNormal=std::max(worstNormal,double(btVector3(dn[0],dn[1],dn[2]).angle(n)));worstTangent=std::max(worstTangent,double(btVector3(dt[0],dt[1],dt[2]).angle(t)));signs=signs&&sign==r.tw;}
  std::cout<<"compact normal error max "<<worstNormal*SIMD_DEGS_PER_RAD<<" deg, tangent "<<worstTangent*SIMD_DEGS_PER_RAD<<" deg\n";
  check(worstNormal*SIMD_DEGS_PER_RAD<2.2&&worstTangent*SIMD_DEGS_PER_RAD<2.2&&signs,"compressed normals and tangents decode within 2.2 degrees and keep the binormal sign");
 }
 Model topologyModel;topologyModel.materials.resize(11);
 for(unsigned i=0;i<11;i++){auto& m=topologyModel.materials[i];m.first=unsigned(topologyModel.indices.size());m.count=81003;for(unsigned k=0;k<m.count;k++)topologyModel.indices.push_back((k+i*17999)%190000);}
 auto topology=batchTopology(topologyModel);bool exact=true;
 for(size_t i=0;i<topology.parts.size();i++){unsigned index=topologyModel.materials[i].first;for(auto range:topology.parts[i]){auto& chunk=topology.chunks[range.chunk];for(unsigned k=range.first;k<range.first+range.count;k++)exact=exact&&chunk.vertices[chunk.indices[k]]==topologyModel.indices[index++];}exact=exact&&index==topologyModel.materials[i].first+topologyModel.materials[i].count;}
 check(exact,"batched material ranges preserve every triangle and vertex index in order");
 bool bounded=true;for(auto& chunk:topology.chunks)bounded=bounded&&chunk.vertices.size()<=32760&&chunk.indices.size()<=60000&&chunk.indices.size()%3==0;
 check(bounded,"shared render chunks stay within Source vertex and index limits");
 std::atomic<int> jobs=0;parallelFor(16,1,[&](size_t a,size_t b){for(size_t i=a;i<b;i++)parallelFor(31,1,[&](size_t x,size_t y){jobs+=int(y-x);});});check(jobs==16*31,"nested task groups complete every chunk without lost wakeups");
 bool caught=false;try{parallelFor(50,1,[](size_t a,size_t){if(a==17)throw std::runtime_error("task error");});}catch(...){caught=true;}check(caught,"worker exceptions return across the frame barrier");
 std::vector<btVector3> reference;
 for(int fps:{60,30,120,144,240}){
  World host;auto handle=host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",0}});auto& p=host.get(handle);auto& rig=*p.sourceRig;
  auto submit=[&](double t,uint64_t frame){std::vector<btTransform> palette;btTransform drive(btQuaternion::getIdentity(),btVector3(float(t*12),0,0));for(auto& b:rig.bones)palette.push_back(drive*b.rest);p.submitPresentationPose(palette,t,frame);p.stepSource();};
  submit(0,1);auto initialResets=p.secondary->resets;unsigned between=0,smooth=0;btVector3 previousDisplay(0,0,0);
  for(int frame=1;frame<=fps*2;frame++){
   submit(double(frame)/fps,uint64_t(frame)+1);
   auto state=p.secondary->diagnostics()["bodyList"];for(auto& body:state)if(!body["follower"].get<bool>()){
    auto v=body["displayPosition"];btVector3 display(v[0],v[1],v[2]);
    if(frame>fps&&p.secondary->lastSteps==0){between++;if((display-previousDisplay).length()>1e-6)smooth++;}
    previousDisplay=display;break;
   }
  }
  if(fps>60)check(between>0&&smooth==between,"rendered secondary bodies continue moving between fixed physics ticks");
  check(p.secondary->ticks==121,"60 Hz simulation tick count is independent of render cadence");
  check(p.secondary->resets==initialResets&&p.secondary->dropped==0,"ordinary continuous movement neither resets nor drops simulation");
  double maximum=0;auto bodies=p.secondary->diagnostics()["bodyList"];
  for(size_t i=0;i<bodies.size();i++){auto v=bodies[i]["position"];btVector3 position(v[0],v[1],v[2]);if(fps==60)reference.push_back(position);else maximum=std::max(maximum,double((position-reference[i]).length()));}
  check(maximum<.002,"matched fixed-tick states agree across 30/60/120/144/240 FPS input schedules");
  double before=p.secondary->simulationTime;submit(2,10000);check(p.secondary->simulationTime==before,"repeated timestamps do not integrate or consume physics time");
  auto resetCount=p.secondary->resets;for(auto& t:p.sourcePose)t.getOrigin()+=btVector3(100,0,0);p.reset();check(p.secondary->dropped==0&&p.sourceError.empty()&&p.secondary->resets==resetCount+1&&!p.sourceTeleport&&p.secondary->resetReason=="manual","explicit reset clears history once and does not schedule a second reset");
 }
 {
  World host;auto handle=host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",0}});auto& p=host.get(handle);uint64_t frame=0;double t=0;
  auto submit=[&](double time,float offset=0){std::vector<btTransform> palette;btTransform drive(btQuaternion::getIdentity(),btVector3(float(time*12)+offset,0,0));for(auto& b:p.sourceRig->bones)palette.push_back(drive*b.rest);p.submitPresentationPose(palette,time,++frame);p.stepSource();};
  submit(0);auto resets=p.secondary->resets;
  const double intervals[]={.004,.011,.027,.19,.008,.016,.006};for(int i=0;i<150;i++){t+=intervals[i%7];submit(t);}
  for(int i=0;i<60;i++){t+=1./144;submit(t);}
  check(p.secondary->dropped==0&&p.secondary->resets==resets&&p.secondary->accumulator<1./60,"irregular frames and a short stall recover their complete simulation time");
  submit(t-1);check(p.secondary->resets==resets+1&&p.secondary->resetReason=="clock_rewind","clock rewinds reset the timeline explicitly");
  submit(t-.99,256);check(p.secondary->resets==resets+2&&p.secondary->resetReason=="teleport","teleporting resets secondary history once");
 }
 // Stepped skeletons: a pose that only changes every fourth 240 Hz frame (a
 // 60 Hz networked update) is shown one update behind, interpolated, so the
 // driven pose moves every frame with even steps; a pose that changes every
 // frame passes through without delay; smoothing off keeps the steps.
 for(int variant=0;variant<3;variant++){
  World host;host.poseSmoothing=variant!=2;auto handle=host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",0}});auto& p=host.get(handle);int root=p.sourceRig->bones[0].mmd;
  auto palette=[&](double time){std::vector<btTransform> out;btTransform drive(btQuaternion::getIdentity(),btVector3(float(time*24),0,0));for(auto& b:p.sourceRig->bones)out.push_back(drive*b.rest);return out;};
  uint64_t frame=0;std::vector<double> steps;btVector3 previous(0,0,0);
  for(int f=0;f<=480;f++){double t=f/240.,poseTime=variant==1?t:std::floor(f/4.)*4/240.;p.submitPresentationPose(palette(poseTime),t,++frame);p.stepSource();auto o=p.sourcePose[root].getOrigin();if(f>240)steps.push_back((o-previous).length());previous=o;}
  std::vector<double> sorted(steps);std::sort(sorted.begin(),sorted.end());double median=sorted[sorted.size()/2];unsigned still=0;double worst=0;
  for(double s:steps){if(s<1e-6)still++;worst=std::max(worst,std::abs(s-median));}
  std::cout<<"stepped-pose smoothing variant "<<variant<<": delay "<<p.presentationDelay*1000<<" ms, update interval "<<p.presentationUpdateInterval*1000<<" ms, still frames "<<still<<" of "<<steps.size()<<", worst step deviation "<<worst/std::max(1e-9,median)<<" of median\n";
  if(variant==0)check(still==0&&worst<.3*median&&p.presentationDelay>1./60&&p.presentationDelay<1.5/60,"a skeleton networked at 60 Hz is interpolated one update behind and moves evenly every frame");
  if(variant==1)check(still==0&&p.presentationDelay==0&&p.presentationSamples.size()>=2,"a skeleton that changes every frame passes through without delay");
  if(variant==2)check(still==steps.size()*3/4&&p.presentationDelay==0,"with smoothing off a 60 Hz skeleton keeps its steps");
 }
 // Compare the optimized palette path against the original scalar definitions.
 World host;auto id=host.create(model,{{"backend","source"},{"secondaryCollision",0}});auto& p=host.get(id);p.secondary.reset();
 for(int mode=0;mode<5;mode++){
  for(auto& v:model->vertices){v.type=mode;v.bones={0,1,2,3};v.weights={.1f,.2f,.3f,.4f};if(mode==0)v.weights={1,0,0,0};if(mode==1||mode==3)v.weights={.3f,.7f,0,0};v.c={1,2,3};v.r0={2,0,1};v.r1={0,2,1};}model->invalidateSkinLayout();
  for(size_t i=0;i<p.skin.size();i++)p.skin[i]=btTransform(btQuaternion(btVector3(1,2,3).normalized(),float(i)*.17f),btVector3(float(i),2,-1));
  p.publish(0);double error=0,normalError=0;for(size_t i=0;i<model->vertices.size();i++){auto expected=p.placement*(toSource(skinPosition(model->vertices[i],p.skin))*p.scale)/Inch;auto& d=p.snapshot->vertices[i];error=std::max(error,double((expected-btVector3(d.x,d.y,d.z)).length()));auto normal=p.placement.getBasis()*toSource(skinNormal(model->vertices[i],p.skin));normal.normalize();normalError=std::max(normalError,double((normal-btVector3(d.nx,d.ny,d.nz)).length()));}
  check(error<1e-4&&normalError<1e-4,"optimized BDEF/SDEF/QDEF palette preserves scalar positions and normals");
 }
 shutdownJobs();std::cout<<checks<<" timing/scheduler checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";shutdownJobs();return 1;}}
