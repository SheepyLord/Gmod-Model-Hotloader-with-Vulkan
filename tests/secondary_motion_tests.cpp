// Production-path regressions for independent attachments and thin map surfaces.
#include "runtime.hpp"
#include "rig.hpp"
#include "secondary.hpp"
#include "scene.hpp"
#include "jobs.hpp"
#include <BulletDynamics/ConstraintSolver/btGeneric6DofConstraint.h>
#include <iostream>
#include <fstream>
using namespace mmd;
namespace {
btVector3 vector(const Json& p){return {p[0],p[1],p[2]};}
std::vector<btTransform> palette(const Instance& p,btVector3 offset={0,0,0}){
 std::vector<btTransform> out;for(auto& b:p.sourceRig->bones){auto t=b.rest;t.getOrigin()+=offset;out.push_back(t);}return out;
}
void submit(Instance& p,const std::vector<btTransform>& pose,double t,uint64_t frame){
 p.submitPresentationPose(pose,t,frame);p.secondary->waitAsyncIdle();p.stepSource();
}
std::shared_ptr<SceneFrame> floorScene(float height){
 auto g=std::make_shared<SceneGeometry>();g->kind=SceneGeometry::Triangles;
 g->minimum={-500,-500,height};g->maximum={500,500,height};
 g->vertices={{-500,-500,height},{500,-500,height},{500,500,height},{-500,-500,height},{500,500,height},{-500,500,height}};
 auto frame=std::make_shared<SceneFrame>();SceneObject o;o.id=1;o.geometry=g;o.isStatic=true;frame->objects.push_back(o);return frame;
}
}
int main(int argc,char** argv){try{
 if(argc<2)throw std::runtime_error("Pass native-chain.pmx and optional report path");
 auto model=parse(readFile(argv[1]));Json report=Json::array();bool passed=true;
 Secondary::setSleepPolicy(false,.5f,.35f,1.5f);
 for(auto backend:{"reference","cpu_mt","cpu_mt_v2"}){
  Json result={{"backend",backend}};
  for(int rate:{60,144})for(float speed:{60.f,240.f,-240.f}){
   World host;auto id=host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",0},{"secondaryBackend",backend}});auto& p=host.get(id);
   double lead=0,displayLead=0,maxError=0,stopError=0;unsigned samples=0;
   for(int f=0;f<=9*rate;f++){
    double t=double(f)/rate;
    submit(p,palette(p,btVector3(float(std::clamp(t-1.,0.,6.)*speed),0,0)),t,f+1);
    if(t>=5&&t<7){auto joint=static_cast<btGeneric6DofConstraint*>(p.secondary->dynamics()->getConstraint(0));joint->calculateTransforms();
     auto error=toSource(joint->getCalculatedTransformB().getOrigin()-joint->getCalculatedTransformA().getOrigin())*p.sourceRig->scale;
     lead+=error.x();maxError=std::max(maxError,double(error.length()));++samples;
     displayLead+=p.secondary->diagnostics()["attachmentJoints"][0]["displayOffsetSource"][0].get<double>();
    }
    if(t>=8)stopError=std::max(stopError,std::abs(p.secondary->diagnostics()["attachmentJoints"][0]["displayOffsetSource"][0].get<double>()));
   }
   result["carry"+std::to_string(int(speed))+"At"+std::to_string(rate)+"Fps"]={{"firstLinkLeadSource",lead/samples},{"displayLeadSource",displayLead/samples},{"firstLinkErrorSource",maxError},{"stoppedErrorSource",stopError}};
   passed&=std::abs(lead/samples)<.25&&std::abs(displayLead/samples)<.25&&stopError<.25;
  }
  {
   World host;auto id=host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",0},{"secondaryBackend",backend}});auto& p=host.get(id);
   auto rest=palette(p);for(int f=0;f<=60;f++)submit(p,rest,f/60.,f+1);
   auto before=p.secondary->diagnostics()["bodyList"];
   auto moved=rest;moved[0].getOrigin()+=btVector3(8,0,0);
   // Same timestamp: inspect presentation reprojection without a new solver step.
   submit(p,moved,1.,62);auto after=p.secondary->diagnostics()["bodyList"];double error=0;
   for(size_t i=0;i<before.size();i++)if(!before[i]["follower"].get<bool>())error=std::max(error,double((vector(before[i]["displayPosition"])-vector(after[i]["displayPosition"])).length()));
   result["pelvisOnlyHairDisplacementPmx"]=error;result["attachmentPassed"]=error<1e-4;passed&=error<1e-4;
   moved[0].setRotation(btQuaternion(btVector3(0,0,1),.4f)*moved[0].getRotation());
   submit(p,moved,1.,63);after=p.secondary->diagnostics()["bodyList"];error=0;
   for(size_t i=0;i<before.size();i++)if(!before[i]["follower"].get<bool>())error=std::max(error,double((vector(before[i]["displayPosition"])-vector(after[i]["displayPosition"])).length()));
   result["pelvisRotationHairDisplacementPmx"]=error;passed&=error<1e-4;
   moved=rest;int head=-1;for(size_t i=0;i<p.sourceRig->bones.size();i++)if(p.sourceRig->bones[i].name=="ValveBiped.Bip01_Head1")head=int(i);
   if(head<0)throw std::runtime_error("Fixture has no head");moved[head].getOrigin()+=btVector3(8,0,0);
   submit(p,moved,1.,64);after=p.secondary->diagnostics()["bodyList"];error=0;
   auto expected=fromSource(btVector3(8,0,0))/p.sourceRig->scale;
   for(size_t i=0;i<before.size();i++)if(!before[i]["follower"].get<bool>())error=std::max(error,double((vector(after[i]["displayPosition"])-vector(before[i]["displayPosition"])-expected).length()));
   result["headMotionCompensationErrorPmx"]=error;passed&=error<1e-4;
  }
  {
   World host;auto id=host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",1},{"secondaryBackend",backend}});auto& p=host.get(id);
   publishScene(floorScene(0));double minimum=1e9,afterLift=1e9,maxError=0;
   // Lower the head follower to 1 PMX unit above a zero-thickness floor, hold
   // the full chain under compression, then lift it back into free space.
   for(int f=0;f<=600;f++){
    float lower=f<180?16.f*f/180:f<300?16.f:f<420?16.f*(420-f)/120:0;
    submit(p,palette(p,btVector3(0,0,-lower*p.sourceRig->scale)),f/60.,f+1);
    auto d=p.secondary->diagnostics();for(auto& b:d["bodyList"])if(!b["follower"].get<bool>()){
     double y=b["position"][1];minimum=std::min(minimum,y);if(f>=540)afterLift=std::min(afterLift,y);
    }
    if(f>=540)maxError=std::max(maxError,d["maxLinearLimitError"].get<double>());
   }
   result["minimumBodyHeightPmx"]=minimum;result["afterLiftMinimumPmx"]=afterLift;result["afterLiftJointErrorPmx"]=maxError;
   result["floorPassed"]=minimum>=-.05&&afterLift>1&&maxError<.5;passed&=result["floorPassed"].get<bool>();
   // Recreate secondary physics while already compressed through the plane,
   // then lift. No sweep history exists for these initially buried bodies.
   submit(p,palette(p,btVector3(0,0,-16*p.sourceRig->scale)),11,602);p.reset();
   for(int f=0;f<240;f++)submit(p,palette(p,btVector3(0,0,-16*p.sourceRig->scale*btMax(0.f,1-f/120.f))),11+f/60.,603+f);
   auto recovered=p.secondary->diagnostics();double recoveredMinimum=1e9;
   for(auto& b:recovered["bodyList"])if(!b["follower"].get<bool>())recoveredMinimum=std::min(recoveredMinimum,b["position"][1].get<double>());
   result["resetWhileBuriedRecoveredMinimumPmx"]=recoveredMinimum;
   result["resetWhileBuriedJointErrorPmx"]=recovered["maxLinearLimitError"];
   passed&=recoveredMinimum>1&&recovered["maxLinearLimitError"].get<double>()<.5;
   publishScene(nullptr);
  }
  report.push_back(result);std::cout<<result.dump(2)<<std::endl;
 }
 if(argc>2)std::ofstream(argv[2])<<report.dump(2);
 shutdownJobs();return passed?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;shutdownJobs();return 2;}}
