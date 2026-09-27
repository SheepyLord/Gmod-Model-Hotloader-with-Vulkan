// Exercise authored sleeve constraints under rapid arm motion, with and without
// the optional guard. Accept an actual cached PMX for a focused offline replay.
#include "runtime.hpp"
#include "secondary.hpp"
#include "rig.hpp"
#include "jobs.hpp"
#include <BulletDynamics/ConstraintSolver/btGeneric6DofConstraint.h>
#include <iostream>
#include <fstream>
using namespace mmd;
int main(int argc,char** argv){try{
 if(argc<2)throw std::runtime_error("Pass a PMX fixture/cache model and optional JSON report");
 auto model=parse(readFile(argv[1]));Json report=Json::array();double before=0,after=0;
 for(bool guard:{false,true}){
  Secondary::setTuning(10,1,1,guard,1);Secondary::setSleepPolicy(false,.5f,.35f,1.5f);
  World host;auto id=host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryBackend","cpu_mt_v2"},{"secondaryCollision",0}});auto& p=host.get(id);
  int arm=-1;for(size_t i=0;i<p.sourceRig->bones.size();i++)if(p.sourceRig->bones[i].name=="ValveBiped.Bip01_L_UpperArm")arm=int(i);
  double maximum=0,sum=0,guardCost=0,maxSpeed=0;unsigned count=0,frames=0;
  for(int frame=0;frame<=360;frame++){
   double t=frame/60.;btVector3 offset(float(std::max(0.,t-1)*240),0,0);std::vector<btTransform> poses;
   btTransform swing(btQuaternion(btVector3(1,0,0),float(std::sin(t*8)*.8)),btVector3(0,0,0));
   if(arm>=0){auto pivot=p.sourceRig->bones[arm].rest.getOrigin();swing.setOrigin(pivot-swing.getBasis()*pivot);}
   for(size_t i=0;i<p.sourceRig->bones.size();i++){auto pose=p.sourceRig->bones[i].rest;int ancestor=int(i);while(ancestor>=0&&ancestor!=arm)ancestor=p.sourceRig->bones[ancestor].parent;if(ancestor==arm&&arm>=0)pose=swing*pose;pose.getOrigin()+=offset;poses.push_back(pose);}
   p.submitPresentationPose(poses,t,frame+1);p.secondary->waitAsyncIdle();p.stepSource();
   if(!p.sourceError.empty())throw std::runtime_error(p.sourceError);
   if(frame<120)continue;
   auto diagnostics=p.secondary->diagnostics(false);guardCost+=diagnostics["stretchGuardMs"].get<double>();++frames;
   for(int i=0;i<p.secondary->dynamics()->getNumCollisionObjects();i++)if(auto rigid=btRigidBody::upcast(p.secondary->dynamics()->getCollisionObjectArray()[i])){
    auto position=rigid->getWorldTransform().getOrigin(),velocity=rigid->getLinearVelocity();
    for(int k=0;k<3;k++)if(!std::isfinite(position[k])||!std::isfinite(velocity[k]))throw std::runtime_error("Non-finite stretch replay state");
    maxSpeed=std::max(maxSpeed,double(velocity.length()));
   }
   for(int i=0;i<p.secondary->dynamics()->getNumConstraints();i++){
    auto constraint=p.secondary->dynamics()->getConstraint(i);
    if(constraint->getConstraintType()!=D6_CONSTRAINT_TYPE&&constraint->getConstraintType()!=D6_SPRING_CONSTRAINT_TYPE)continue;
    auto joint=static_cast<btGeneric6DofConstraint*>(constraint);joint->calculateTransforms();
    auto delta=joint->getCalculatedTransformA().getBasis().transpose()*(joint->getCalculatedTransformB().getOrigin()-joint->getCalculatedTransformA().getOrigin());btVector3 lo,hi;joint->getLinearLowerLimit(lo);joint->getLinearUpperLimit(hi);
    double error=0;for(int k=0;k<3;k++)if(lo[k]<=hi[k])error=std::max({error,double(lo[k]-delta[k]),double(delta[k]-hi[k])});maximum=std::max(maximum,error);sum+=error;++count;
   }
  }
  auto d=p.secondary->diagnostics(false);report.push_back({{"guard",guard},{"maxLimitErrorPmx",maximum},{"meanLimitErrorPmx",sum/std::max(1u,count)},{"meanGuardMs",guardCost/frames},{"maxSpeedPmx",maxSpeed},{"bodies",d["bodies"]},{"joints",d["joints"]},{"corrections",d["stretchCorrections"]}});
  if(guard)after=maximum;else before=maximum;
  auto gravity=p.secondary->dynamics()->getGravity();Secondary::setTuning(18,.5f,.75f,guard,1);
  p.secondary->waitAsyncIdle();p.secondary->step(1./60);
  if(p.secondary->dynamics()->getSolverInfo().m_numIterations!=18||(p.secondary->dynamics()->getGravity()-gravity*.5f).length()>1e-5f)throw std::runtime_error("Live tuning was not applied");
 }
 Secondary::setTuning(10,1,1,false,1);std::cout<<report.dump(2)<<std::endl;if(argc>2)std::ofstream(argv[2])<<report.dump(2);
 shutdownJobs();return after<=std::max(.15,before*.9)?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;shutdownJobs();return 2;}}
