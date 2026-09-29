// Asynchronous secondary scheduling (cpu_mt_v2): equivalence with the
// frame-synchronous multicore world, bounded presentation lag, resets,
// collision-mode changes and teardown while a tick is in flight.
#include "runtime.hpp"
#include "secondary.hpp"
#include "rig.hpp"
#include "jobs.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
using namespace mmd;
namespace {
std::vector<btTransform> palette(const Rig& rig,double t,float offset=0){std::vector<btTransform> out;btTransform drive(btQuaternion(btVector3(0,0,1),float(std::sin(t*1.3)*.2)),btVector3(float(std::sin(t*1.7)*12)+offset,float(std::cos(t*.9)*6),float(std::sin(t*2)*4)));out.reserve(rig.bones.size());for(auto& b:rig.bones)out.push_back(drive*b.rest);return out;}
Json options(const char* backend){return {{"backend","source"},{"presentationDriven",true},{"secondaryCollision",0},{"secondaryBackend",backend}};}
double bodyDifference(const Json& a,const Json& b){double maximum=0;auto& la=a["bodyList"];auto& lb=b["bodyList"];if(la.size()!=lb.size())return 1e9;for(size_t i=0;i<la.size();i++){auto u=la[i]["position"],v=lb[i]["position"];maximum=std::max(maximum,double((btVector3(u[0],u[1],u[2])-btVector3(v[0],v[1],v[2])).length()));}return maximum;}
}
int main(int argc,char** argv){try{
 if(argc!=2)throw std::runtime_error("Pass the rigid chain fixture");auto model=parse(readFile(argv[1]));
 int checks=0;auto check=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);++checks;std::cout<<"PASS "<<name<<"\n";};
 Secondary::setSleepPolicy(false,.15f,.2f,1.f);Secondary::setAsyncWaitBudget(0);
 {
  World syncHost,asyncHost;
  auto s=syncHost.create(model,options("cpu_mt")),a=asyncHost.create(model,options("cpu_mt_v2"));
  auto& ps=syncHost.get(s);auto& pa=asyncHost.get(a);
  check(pa.secondary->asynchronous()&&!ps.secondary->asynchronous()&&pa.secondary->effectiveBackend=="cpu_mt_v2","cpu_mt_v2 schedules asynchronously while cpu_mt stays frame-synchronous");
  double maximum=0,maxLag=0;unsigned presentedBehind=0;
  for(int frame=0;frame<=180;frame++){
   double t=frame/60.;auto pose=palette(*ps.sourceRig,t);
   ps.submitPresentationPose(pose,t,uint64_t(frame)+1);ps.stepSource();
   pa.submitPresentationPose(pose,t,uint64_t(frame)+1);
   if(frame%2){pa.stepSource();maxLag=std::max(maxLag,pa.secondary->lagMs);if(pa.secondary->lagMs>0)presentedBehind++;pa.presentationDirty=false;}
   pa.secondary->waitAsyncIdle();pa.stepSource();
   maximum=std::max(maximum,bodyDifference(ps.secondary->diagnostics(),pa.secondary->diagnostics()));
  }
  check(maximum<1e-3,"asynchronous ticks reproduce the synchronous multicore trajectory");
  check(pa.secondary->ticks==ps.secondary->ticks&&pa.secondary->ticks==181&&pa.secondary->dropped==0&&pa.secondary->resets==ps.secondary->resets,"asynchronous clock keeps every 60 Hz tick without drops or resets");
  check(maxLag<=2*1000./60+1,"presentation lags the input by at most two frames while a tick is in flight");
  check(pa.secondary->lagMs==0,"a completed tick presents without lag");
  Secondary::setAsyncWaitBudget(50);
  for(int frame=181;frame<=190;frame++){double t=frame/60.;pa.submitPresentationPose(palette(*ps.sourceRig,t),t,uint64_t(frame)+1);pa.stepSource();check(pa.secondary->lagMs==0,"a wait budget lets the frame present the tick it submitted");}
  Secondary::setAsyncWaitBudget(0);
  auto resets=pa.secondary->resets;
  pa.submitPresentationPose(palette(*ps.sourceRig,191/60.,256),191/60.,192);pa.secondary->waitAsyncIdle();pa.stepSource();
  check(pa.secondary->resets==resets+1&&pa.secondary->resetReason=="teleport","teleports reset the asynchronous world once");
  pa.secondary->setCollisionMode(2);pa.submitPresentationPose(palette(*ps.sourceRig,192/60.,256),192/60.,193);pa.secondary->setCollisionMode(0);pa.secondary->waitAsyncIdle();pa.stepSource();
  check(pa.secondary->diagnostics(false)["collisionFlags"]==Collide::Character&&pa.sourceError.empty(),"collision mode changes apply between ticks");
  double total=pa.secondary->inputTime;
  for(int k=0;k<24;k++){double t=(193+k)/60.;pa.submitPresentationPose(palette(*ps.sourceRig,t,256),t,uint64_t(194+k));}
  pa.secondary->waitAsyncIdle();pa.stepSource();
  check(std::abs(pa.secondary->inputTime-(total+24./60))<1e-6&&pa.sourceError.empty(),"a bounded input backlog keeps its simulation time");
  setWorkerCount(4);pa.submitPresentationPose(palette(*ps.sourceRig,218/60.,256),218/60.,219);setWorkerCount(0);pa.secondary->waitAsyncIdle();pa.stepSource();
  check(pa.sourceError.empty()&&pa.secondary->asyncError.empty(),"worker pool resizes finish in-flight asynchronous ticks");
  pa.submitPresentationPose(palette(*ps.sourceRig,219/60.,256),219/60.,220);
 }
 check(true,"worlds destroy safely while a tick is queued");
 // A mode requested while the final queued tick is in flight waits for the next
 // tick; a newer mode chosen once the worker is idle must supersede it.
 {
  World host;auto h=host.create(model,options("cpu_mt_v2"));auto& p=host.get(h);int frame=0;
  auto tick=[&]{double t=frame/60.;p.submitPresentationPose(palette(*p.sourceRig,t),t,uint64_t(++frame));};
  // The worker has taken the input: its tick is running (20 ms longer than usual).
  auto inFlight=[&]{Secondary::setAsyncTestStepDelay(20);tick();while(p.secondary->queuedInputs())std::this_thread::yield();};
  auto effective=[&]{p.secondary->waitAsyncIdle();return p.secondary->diagnostics(false)["effectiveCollisionFlags"].get<unsigned>();};
  const unsigned all=collisionFlagsForLevel(2),character=Collide::Character;
  p.secondary->setCollisionMode(2);tick();
  check(effective()==all,"an idle world applies its collision mode at once");
  inFlight();p.secondary->setCollisionMode(0);
  check(effective()==all,"a collision mode requested during a tick waits for the next tick");
  Secondary::setAsyncTestStepDelay(0);tick();
  check(effective()==character&&p.secondary->collisionFlags==character,"the requested collision mode applies at the next tick");
  p.secondary->setCollisionMode(2);tick();
  inFlight();p.secondary->setCollisionMode(0);p.secondary->waitAsyncIdle();
  Secondary::setAsyncTestStepDelay(0);p.secondary->setCollisionMode(2);tick();
  check(effective()==all&&p.secondary->collisionFlags==all&&p.sourceError.empty(),"a collision mode chosen while idle supersedes one left pending by the final in-flight tick");
 }
 // Presentation continuity while a stepping job outlasts a frame: frames paced
 // at 240 Hz in real time present right after submitting (no wait budget) while
 // every 60 Hz step job sleeps 6 ms. The displayed offset of a dynamic body from
 // its anchor must keep moving evenly: no held frames and no catch-up jumps.
 {
  World host;auto h=host.create(model,options("cpu_mt_v2"));auto& p=host.get(h);
  int body=-1,anchorBody=-1;{auto d=p.secondary->diagnostics(true);for(size_t i=0;i<d["bodyList"].size();i++){auto& b=d["bodyList"][i];if(!b["follower"].get<bool>()&&b["presentationAnchorBody"].get<int>()>=0){body=int(i);anchorBody=b["presentationAnchorBody"].get<int>();}}}
  check(body>=0,"the chain fixture has an anchored dynamic body to present");
  Secondary::setAsyncTestStepDelay(6);
  const double frame=1./240;std::vector<double> motion;btVector3 last(0,0,0);double lagPeak=0,delay=0;
  auto start=std::chrono::steady_clock::now();
  for(int f=0;f<240;f++){
   double t=f*frame;
   while(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<t)std::this_thread::yield();
   p.submitPresentationPose(palette(*p.sourceRig,t),t,uint64_t(f)+1);p.stepSource();
   if(p.secondary->lagMs>lagPeak)lagPeak=p.secondary->lagMs;delay=p.secondary->presentationDelayMs();
   auto rel=p.secondary->displayTransform(size_t(anchorBody)).inverse()*p.secondary->displayTransform(size_t(body));
   if(f>=96)motion.push_back((rel.getOrigin()-last).length());last=rel.getOrigin();
  }
  Secondary::setAsyncTestStepDelay(0);
  std::vector<double> sorted(motion);std::sort(sorted.begin(),sorted.end());double median=sorted[sorted.size()/2];
  double jerk=0;unsigned held=0;
  for(size_t i=1;i<motion.size();i++){jerk=std::max(jerk,std::abs(motion[i]-motion[i-1]));if(i+1<motion.size()&&motion[i]<.1*median&&motion[i-1]>.5*median&&motion[i+1]>.5*median)held++;}
  std::cout<<"slow-step presentation: lag peak "<<lagPeak<<" ms, display delay "<<delay<<" ms, per-frame motion median "<<median<<", largest change between frames "<<jerk<<" ("<<jerk/std::max(1e-9,median)<<" of median), held frames "<<held<<"\n";
  check(p.sourceError.empty()&&lagPeak>6&&lagPeak<100,"the slow step job makes the worker run more than one frame behind");
  check(held==0&&jerk<.6*median,"presentation stays continuous while a step job outlasts a frame");
  check(delay>1000./60+6&&delay<1000./60*3,"the display margin settles just above the observed lag");
 }
 // Repeated teardown catches lifetime races hidden by a single fast fixture.
 for(int trial=0;trial<100;trial++){
  World host;auto h=host.create(model,options("cpu_mt_v2"));auto& p=host.get(h);
  for(int frame=0;frame<4;frame++)p.submitPresentationPose(palette(*p.sourceRig,frame/60.),frame/60.,frame+1);
  host.remove(h);
 }
 check(true,"100 queued worlds join before their pose/control buffers are destroyed");
 {
  World host;auto h=host.create(model,options("cpu_mt_v2"));auto& p=host.get(h);setWorkerCount(1);
  for(int frame=0;frame<=30;frame++){double t=frame/60.;p.submitPresentationPose(palette(*p.sourceRig,t),t,uint64_t(frame)+1);p.stepSource();}
  check(p.secondary->ticks==31&&p.sourceError.empty(),"a single-thread pool runs asynchronous ticks inline");
  setWorkerCount(0);
 }
 Secondary::setSleepPolicy(true,.15f,.2f,.5f);
 {
  World host;auto h=host.create(model,options("cpu_mt_v2"));auto& p=host.get(h);auto rest=palette(*p.sourceRig,0);
  unsigned slept=0;
  for(int frame=0;frame<=300;frame++){double t=frame/60.;p.submitPresentationPose(rest,t,uint64_t(frame)+1);p.secondary->waitAsyncIdle();p.stepSource();slept=std::max(slept,p.secondary->sleepingBodies);}
  std::cout<<"sleeping bodies after five resting seconds: "<<p.secondary->sleepingBodies<<" of "<<p.secondary->diagnostics(false)["bodies"]<<"\n";
  auto before=p.secondary->diagnostics();
  for(int frame=301;frame<=360;frame++){double t=frame/60.;p.submitPresentationPose(palette(*p.sourceRig,t),t,uint64_t(frame)+1);p.secondary->waitAsyncIdle();p.stepSource();}
  check(p.sourceError.empty()&&p.secondary->resets==2&&p.secondary->dropped==0,"sleeping worlds keep simulating without errors");
  check(slept==0||bodyDifference(before,p.secondary->diagnostics())>1e-3,"moving followers wake sleeping chains");
  check(slept==0||p.secondary->sleepingBodies<slept,"woken chains report fewer sleeping bodies");
 }
 Secondary::setSleepPolicy(true,.15f,.2f,1.f);
 shutdownJobs();std::cout<<checks<<" asynchronous scheduling checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";shutdownJobs();return 1;}}
