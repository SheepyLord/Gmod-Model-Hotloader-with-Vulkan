#include "runtime.hpp"
#include "secondary.hpp"
#include "jobs.hpp"
#include "jiggle_direction.hpp"
#include <iostream>
using namespace mmd;
int main(int argc,char** argv){try{
 if(argc!=2)throw std::runtime_error("Expected fixture path");
 if(jiggleDirectionScale({0,0,-1},{0,0,1})!=0||jiggleDirectionScale({0,0,1},{0,0,1})!=1)throw std::runtime_error("Jiggle front/back motion direction reversed");
 float side=jiggleDirectionScale({1,0,0},{0,0,1});
 if(btFabs(side-std::cos(SIMD_HALF_PI/1.33f))>1e-6f||jiggleDirectionScale({0,0,0},{0,0,1})!=1||jiggleDirectionScale({1,0,0},{0,0,0})!=1)throw std::runtime_error("Jiggle cosine weight or stationary fallback is wrong");
 auto turn=btQuaternion(btVector3(0,1,0),.83f);
 if(btFabs(jiggleDirectionScale(quatRotate(turn,{1,0,0}),quatRotate(turn,{0,0,1}))-side)>1e-6f)throw std::runtime_error("Jiggle filter depends on character facing");
 // The default is capped at the lanes this machine has (CI runners have four).
 for(unsigned t:{1,2,4,8,32})if(automaticWorkerCount(t)!=std::min(t<=4?t:t-2,maximumWorkerCount()))throw std::runtime_error("Worker default mismatch");
 auto model=parse(readFile(argv[1]));model->materials.at(0).alpha=0;World host;
 auto id=host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryBackend","cpu_mt_v2"},{"secondaryCollision",0}});
 auto& p=host.get(id);unsigned frame=0;double time=0;
 auto drive=[&](float offset=0.f,double dt=1./60){std::vector<btTransform> pose;for(auto& b:p.sourceRig->bones){pose.push_back(b.rest);pose.back().getOrigin()+=btVector3(offset,0,0);}time+=dt;++frame;p.submitPresentationPose(pose,time,frame);p.secondary->waitAsyncIdle();p.stepSource();};
 for(int i=0;i<10;i++)drive();
 p.secondary->setQuality(2,false);drive();
 if(p.secondary->frameStats().iterations!=5)throw std::runtime_error("Reduced iteration count not applied");
 p.secondary->setQuality(4,true);auto before=p.secondary->frameStats();
 for(int i=0;i<150;i++)drive();
 auto paused=p.secondary->frameStats();
 if(paused.ticks!=before.ticks||paused.dropped!=before.dropped)throw std::runtime_error("Suspended world simulated or accumulated lost time");
 p.secondary->setQuality(1,false);drive();auto resumed=p.secondary->frameStats();
 if(resumed.iterations!=10||resumed.ticks-before.ticks>1||resumed.dropped!=before.dropped)throw std::runtime_error("Resume accumulated debt or failed to restore quality");
 if(!p.sourceError.empty())throw std::runtime_error(p.sourceError);
 const auto bulletTicks=p.secondary->frameStats().ticks;bool moved=false,rootMoved=false,childInherited=false;
 Secondary::setTuning(0,1,1,false,1);
 for(double rate:{30.,60.,240.})for(int i=0;i<120;i++){
  drive(float(std::sin(time*6)*18),1./rate);
  auto shown=p.global;p.evaluate(false);auto animated=p.global;
  for(size_t b=0;b<p.model->bones.size();b++)if(p.secondary->drivers[b]>=0&&p.sourceControl[b]<0){
   int parent=p.model->bones[b].parent;
   auto rest=p.model->bones[b].position-(parent>=0?p.model->bones[parent].position:btVector3(0,0,0));
   auto pivot=parent>=0?shown[parent]*rest:rest;
   if((shown[b].getOrigin()-pivot).length()>.001f)throw std::runtime_error("Jiggle stretched a bone pivot");
   auto q=shown[b].getRotation();if(!std::isfinite(q.x())||!std::isfinite(shown[b].getOrigin().x()))throw std::runtime_error("Non-finite jiggle output");
   moved|=q.angleShortestPath(animated[b].getRotation())>.001f;
   bool descendant=false;
   for(int a=parent;a>=0&&p.sourceControl[a]<0;a=p.model->bones[a].parent)
    if(p.secondary->drivers[a]>=0){descendant=true;break;}
   if(descendant){
    auto shownLocal=shown[parent].inverse()*shown[b];
    auto animatedLocal=animated[parent].inverse()*animated[b];
    if(shownLocal.getRotation().angleShortestPath(animatedLocal.getRotation())>.001f||
       (shownLocal.getOrigin()-animatedLocal.getOrigin()).length()>.001f)
     throw std::runtime_error("Jiggle independently bent a descendant of the chain root");
    childInherited|=q.angleShortestPath(animated[b].getRotation())>.001f;
   }else rootMoved|=q.angleShortestPath(animated[b].getRotation())>.001f;
  }
  p.evaluate(true);
 }
 if(!moved||p.secondary->frameStats().ticks!=bulletTicks)throw std::runtime_error("Jiggle failed to move or stepped Bullet");
 if(!rootMoved||!childInherited)throw std::runtime_error("Fixture did not exercise root jiggle and inherited child motion");
 for(int i=0;i<240;i++)drive();
 auto settled=p.global;p.evaluate(false);
 for(size_t b=0;b<settled.size();b++)if(settled[b].getRotation().angleShortestPath(p.global[b].getRotation())>.03f)throw std::runtime_error("Stationary jiggle did not settle to authored pose");
 Secondary::setTuning(-1,1,1,false,1);drive(600);
 auto off=p.global;p.evaluate(false);
 for(size_t b=0;b<off.size();b++)if((off[b].getOrigin()-p.global[b].getOrigin()).length()>.0001f||off[b].getRotation().angleShortestPath(p.global[b].getRotation())>.001f)throw std::runtime_error("Disabled physics does not follow animation");
 if(p.secondary->frameStats().ticks!=bulletTicks||p.secondary->frameStats().accumulator!=0)throw std::runtime_error("Disabled physics stepped or accrued debt");
 Secondary::setTuning(10,1,1,false,1);drive(600);drive(600);
 if(p.secondary->frameStats().iterations!=10||!p.sourceError.empty())throw std::runtime_error("Full physics failed to resume after mode switch");
 auto pausedId=host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryBackend","cpu_mt_v2"},{"secondaryCollision",0}});
 auto& pausedActor=host.get(pausedId);pausedActor.secondary->setQuality(1,true);
 std::vector<btTransform> shifted;for(auto& b:pausedActor.sourceRig->bones){shifted.push_back(b.rest);shifted.back().getOrigin()+=btVector3(1200,-700,80);}
 pausedActor.submitPresentationPose(shifted,1,1);pausedActor.stepSource();auto shown=pausedActor.global;pausedActor.evaluate(false);
 for(size_t b=0;b<shown.size();b++)if((shown[b].getOrigin()-pausedActor.global[b].getOrigin()).length()>.001f)throw std::runtime_error("Initially suspended actor left secondary geometry at world origin");
 if(pausedActor.secondary->frameStats().ticks!=0)throw std::runtime_error("Initially suspended actor stepped Bullet");
 pausedActor.secondary->setQuality(1,false);pausedActor.submitPresentationPose(shifted,1+1./60,2);pausedActor.secondary->waitAsyncIdle();pausedActor.stepSource();
 if(pausedActor.secondary->frameStats().steps!=1)throw std::runtime_error("First visible pose did not resume normally");
 if(model->materials.empty())throw std::runtime_error("Material fixture missing");
 std::vector<bool> visible(model->materials.size(),true),opaque(model->materials.size(),false);
 opaque[0]=true;p.setMaterialState(visible,opaque);p.ensureSnapshot();
 if(p.snapshot->materials[0].alpha!=1||model->materials[0].alpha!=0)throw std::runtime_error("Show did not force alpha without mutating authored data");
 opaque[0]=false;p.setMaterialState(visible,opaque);p.ensureSnapshot();
 if(p.snapshot->materials[0].alpha!=0)throw std::runtime_error("Authored alpha not restored");
 for(int i=0;i<4;i++){opaque[0]=i%2==0;p.setMaterialState(visible,opaque);p.ensureSnapshot();if(p.snapshot->materials[0].alpha!=(opaque[0]?1:0))throw std::runtime_error("Reused snapshot retained overridden alpha");}
 std::cout<<"Quality suspension, iteration tiers, worker defaults and material alpha passed\n";
 shutdownJobs();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";shutdownJobs();return 1;}}
