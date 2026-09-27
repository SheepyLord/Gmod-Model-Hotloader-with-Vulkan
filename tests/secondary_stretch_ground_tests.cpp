// Contact-aware stretch projection must preserve compression/lift recovery,
// including a reset below a triangle floor and live collision-mode changes.
#include "runtime.hpp"
#include "rig.hpp"
#include "secondary.hpp"
#include "scene.hpp"
#include "jobs.hpp"
#include <fstream>
#include <iostream>
using namespace mmd;
namespace {
std::shared_ptr<SceneFrame> floorScene(){
 auto g=std::make_shared<SceneGeometry>();g->kind=SceneGeometry::Triangles;
 g->minimum={-500,-500,0};g->maximum={500,500,0};
 g->vertices={{-500,-500,0},{500,-500,0},{500,500,0},{-500,-500,0},{500,500,0},{-500,500,0}};
 auto frame=std::make_shared<SceneFrame>();SceneObject o;o.id=1;o.geometry=g;o.isStatic=true;frame->objects.push_back(o);return frame;
}
void submit(Instance& p,btVector3 offset,int frame){
 std::vector<btTransform> pose;for(auto& b:p.sourceRig->bones){auto t=b.rest;t.getOrigin()+=offset;pose.push_back(t);}
 p.submitPresentationPose(pose,frame/60.,frame+1);p.secondary->waitAsyncIdle();p.stepSource();
 if(!p.sourceError.empty())throw std::runtime_error(p.sourceError);
}
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
}
int main(int argc,char** argv){try{
 if(argc<2)throw std::runtime_error("Pass native-chain.pmx and optional JSON report");
 require(!Secondary::tuning()["stretch"].get<bool>(),"Stretch correction must default off");
 auto model=parse(readFile(argv[1]));Json report=Json::array();
 Secondary::setSleepPolicy(false,.5f,.35f,1.5f);
 for(int mode:{1,2}){
  std::vector<btVector3> baseline;double maximumDifference=0;
  for(bool requested:{false,true}){
   Secondary::setTuning(10,1,1,requested,1);
   World host;auto id=host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",mode},{"secondaryBackend","cpu_mt_v2"}});auto& p=host.get(id);
   publishScene(floorScene());double minimum=1e9,afterLift=1e9,maxError=0;size_t sample=0;unsigned maxContacts=0;
   for(int f=0;f<=900;f++){
    if(f==601){submit(p,{0,0,-16*p.sourceRig->scale},660);p.reset();}
    float lower=f<=600?(f<180?16.f*f/180:f<300?16.f:f<420?16.f*(420-f)/120:0):16.f*btMax(0.f,1-(f-601)/120.f);
    bool lifted=(f>=540&&f<=600)||f>=840;
    submit(p,{0,0,-lower*p.sourceRig->scale},f<=600?f:f+60);
    auto d=p.secondary->diagnostics();

    maxContacts=std::max(maxContacts,d["worldContacts"].get<unsigned>());
    for(int i=0;i<p.secondary->dynamics()->getNumCollisionObjects();i++){
     auto body=btRigidBody::upcast(p.secondary->dynamics()->getCollisionObjectArray()[i]);if(!body||body->isStaticOrKinematicObject())continue;
     auto position=body->getWorldTransform().getOrigin(),velocity=body->getLinearVelocity();
     for(int k=0;k<3;k++)require(std::isfinite(position[k])&&std::isfinite(velocity[k]),"Non-finite floor replay state");
     if(f<=600)minimum=std::min(minimum,double(position.y()));if(lifted)afterLift=std::min(afterLift,double(position.y()));
     for(auto value:{position,velocity}){
      if(!requested)baseline.push_back(value);else {require(sample<baseline.size(),"Replay body counts changed");maximumDifference=std::max(maximumDifference,double((value-baseline[sample]).length()));}++sample;
     }
    }
    if(lifted)maxError=std::max(maxError,d["maxLinearLimitError"].get<double>());
   }
   auto d=p.secondary->diagnostics();
   require(minimum>=-.05&&afterLift>1&&maxError<.5,"Compression/lift surface recovery regressed");
   require(maxContacts>0&&d["surfaceRecoveries"].get<uint64_t>()>0,"Replay did not exercise floor contacts and buried-chain recovery");
   if(requested)require(d["stretchCorrections"].get<uint64_t>()>0&&d["stretchContactClamps"].get<uint64_t>()>0,"Contact-aware projection never ran");
   report.push_back({{"collisionMode",mode},{"stretchRequested",requested},{"minimumHeightPmx",minimum},{"afterLiftMinimumPmx",afterLift},{"afterLiftErrorPmx",maxError},{"maxWorldContacts",maxContacts},{"surfaceCorrections",d["surfaceCorrections"]},{"surfaceRecoveries",d["surfaceRecoveries"]},{"stretchCorrections",d["stretchCorrections"]},{"maximumStateDifference",maximumDifference}});
   // The guard works across mode switches at complete worker boundaries.
   if(requested){
    p.secondary->setCollisionMode(0);
    for(int f=961;f<=1080;f++)submit(p,{float(f-960)*4,0,0},f);
    auto corrections=p.secondary->diagnostics(false)["stretchCorrections"].get<uint64_t>();
    require(corrections>0,"Opt-in model-only correction never ran");
    p.secondary->setCollisionMode(mode);
    for(int f=1081;f<=1110;f++)submit(p,{float(f-960)*4,0,0},f);
    require(p.secondary->diagnostics(false)["stretchCorrections"].get<uint64_t>()>corrections,"Mode change disabled stretch correction");
    report.back()["modeSwitchPassed"]=true;
   }
   publishScene(nullptr);
  }
 }
 Secondary::setTuning(10,1,1,false,1);std::cout<<report.dump(2)<<std::endl;if(argc>2)std::ofstream(argv[2])<<report.dump(2);
 shutdownJobs();return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;publishScene(nullptr);shutdownJobs();return 1;}}
