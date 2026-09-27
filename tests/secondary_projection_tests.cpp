// Positional stabilization must retain authored body and constraint parameters.
// Exercise translation with near-unity damping and extreme mass ratios.
#include "runtime.hpp"
#include "secondary.hpp"
#include "rig.hpp"
#include "jobs.hpp"
#include "contact_projection.hpp"
#include <BulletDynamics/ConstraintSolver/btGeneric6DofConstraint.h>
#include <iostream>
#include <fstream>
using namespace mmd;
void testContacts(){
 btSphereShape strand(.15f);btBoxShape floor({10,.25f,10}),wall({.25f,10,10});
 btCollisionObject ground,side;ground.setCollisionShape(&floor);side.setCollisionShape(&wall);
 ground.setWorldTransform(btTransform(btQuaternion::getIdentity(),{0,-.25f,0}));
 side.setWorldTransform(btTransform(btQuaternion::getIdentity(),{1.25f,0,0}));
 auto obstacle=[](btCollisionObject& o){ProjectionObstacle p;p.object=&o;o.getCollisionShape()->getAabb(o.getWorldTransform(),p.minimum,p.maximum);return p;};
 std::vector<ProjectionObstacle> objects{obstacle(ground)};
 btTransform start(btQuaternion::getIdentity(),{0,1,0});unsigned contacts=0;
 auto move=contactSafeTranslation(strand,start,{3,-5,0},objects,contacts);
 auto position=start.getOrigin()+move;
 if(!contacts||position.y()<.149f||position.x()<2.9f)throw std::runtime_error("Stretch failed to slide across the floor");
 objects.push_back(obstacle(side));contacts=0;
 position=start.getOrigin()+contactSafeTranslation(strand,start,{3,-5,0},objects,contacts);
 if(contacts<2||position.y()<.149f||position.x()>.851f)throw std::runtime_error("Stretch crossed a floor/wall corner");
 start.setOrigin({0,.15f,0});contacts=0;
 auto escape=contactSafeTranslation(strand,start,{0,3,0},objects,contacts);
 if(escape.y()<2.99f)throw std::runtime_error("A touching strand could not lift from the floor");
 // Inclined and moved props use their current transform, with one-way contact.
 ground.setWorldTransform(btTransform(btQuaternion(btVector3(0,0,1),.4f),{0,-.25f,0}));
 auto untouched=ground.getWorldTransform();objects={obstacle(ground)};contacts=0;start.setOrigin({0,1,0});
 position=start.getOrigin()+contactSafeTranslation(strand,start,{2,-4,0},objects,contacts);
 auto local=ground.getWorldTransform().inverse()*position;
 if(!contacts||local.y()<.399f||ground.getWorldTransform()!=untouched)throw std::runtime_error("Stretch crossed or pushed a sloped prop");
}
int main(int argc,char** argv){try{
 testContacts();
 if(argc<2)throw std::runtime_error("Pass a PMX fixture/cache model and optional JSON report");
 auto model=parse(readFile(argv[1]));Json report=Json::array();double before=0,after=0;
 for(int variant=0;variant<2;variant++){
  
  Secondary::setTuning(10,1,1,variant!=0,1);Secondary::setSleepPolicy(false,.5f,.35f,1.5f);
  World host;auto id=host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryBackend","cpu_mt_v2"},{"secondaryCollision",0}});auto& p=host.get(id);
  int arm=-1;for(size_t i=0;i<p.sourceRig->bones.size();i++)if(p.sourceRig->bones[i].name=="ValveBiped.Bip01_L_UpperArm")arm=int(i);
  double maximum=0,sum=0,guardCost=0,maxSpeed=0,stepCost=0;unsigned count=0,frames=0;
  for(int frame=0;frame<=360;frame++){
   double t=frame/60.;btVector3 offset(float(std::max(0.,t-1)*240),0,0);std::vector<btTransform> poses;
   btTransform swing(btQuaternion(btVector3(1,0,0),float(std::sin(t*8)*.8)),btVector3(0,0,0));
   if(arm>=0){auto pivot=p.sourceRig->bones[arm].rest.getOrigin();swing.setOrigin(pivot-swing.getBasis()*pivot);}
   for(size_t i=0;i<p.sourceRig->bones.size();i++){auto pose=p.sourceRig->bones[i].rest;int ancestor=int(i);while(ancestor>=0&&ancestor!=arm)ancestor=p.sourceRig->bones[ancestor].parent;if(ancestor==arm&&arm>=0)pose=swing*pose;pose.getOrigin()+=offset;poses.push_back(pose);}
   p.submitPresentationPose(poses,t,frame+1);p.secondary->waitAsyncIdle();p.stepSource();
   if(!p.sourceError.empty())throw std::runtime_error(p.sourceError);
   if(frame<120)continue;
   auto diagnostics=p.secondary->diagnostics(false);stepCost+=diagnostics["stepMs"].get<double>();guardCost+=diagnostics["stretchGuardMs"].get<double>();++frames;
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
  auto d=p.secondary->diagnostics(true);report.push_back({{"stretch",variant!=0},{"maxLimitErrorPmx",maximum},{"meanLimitErrorPmx",sum/std::max(1u,count)},{"meanGuardMs",guardCost/frames},{"meanStepMs",stepCost/frames},{"maxSpeedPmx",maxSpeed},{"bodies",d["bodies"]},{"joints",d["joints"]},{"corrections",d["stretchCorrections"]}});
  if(variant)after=maximum;else before=maximum;
  if(variant&&d["stretchCorrections"].get<unsigned>()==0)throw std::runtime_error("Positional projection never ran");
  auto dynamics=p.secondary->dynamics();
  std::vector<btVector3> limits;std::vector<float> masses,angular;
  for(int i=0;i<dynamics->getNumConstraints();i++){auto c=static_cast<btGeneric6DofConstraint*>(dynamics->getConstraint(i));btVector3 lo,hi;c->getLinearLowerLimit(lo);c->getLinearUpperLimit(hi);limits.push_back(lo);limits.push_back(hi);}
  for(int i=0;i<dynamics->getNumCollisionObjects();i++)if(auto b=btRigidBody::upcast(dynamics->getCollisionObjectArray()[i])){masses.push_back(b->getInvMass());angular.push_back(b->getAngularDamping());}
  // Turning the correction on/off must never change masses, damping, ERP
  // or authored translation limits, even for a stretch-prone rig.
  Secondary::setTuning(10,1,1,variant==0,1);p.secondary->step(1./60);p.secondary->waitAsyncIdle();
  for(int i=0;i<dynamics->getNumConstraints();i++){auto c=static_cast<btGeneric6DofConstraint*>(dynamics->getConstraint(i));btVector3 lo,hi;c->getLinearLowerLimit(lo);c->getLinearUpperLimit(hi);if(lo!=limits[i*2]||hi!=limits[i*2+1])throw std::runtime_error("Authored travel changed");auto erp=c->getTranslationalLimitMotor()->m_stopERP;for(int axis=0;axis<3;axis++)if(btFabs(erp[axis]-.2f)>1e-6)throw std::runtime_error("Authored ERP not restored");}
  for(int i=0;i<dynamics->getNumCollisionObjects();i++)if(auto b=btRigidBody::upcast(dynamics->getCollisionObjectArray()[i])){auto source=model->bodies[i];if(b->getInvMass()!=masses[i]||b->getAngularDamping()!=angular[i]||btFabs(b->getLinearDamping()-nanoemModelRigidBodyGetLinearDamping(source))>1e-6)throw std::runtime_error("Authored body properties not restored");}
  auto gravity=p.secondary->dynamics()->getGravity();Secondary::setTuning(18,.5f,.75f,variant!=0,1);
  p.secondary->waitAsyncIdle();p.secondary->step(1./60);
  if(p.secondary->dynamics()->getSolverInfo().m_numIterations!=18||(p.secondary->dynamics()->getGravity()-gravity*.5f).length()>1e-5f)throw std::runtime_error("Live tuning was not applied");
 }
 Secondary::setTuning(10,1,1,false,1);std::cout<<report.dump(2)<<std::endl;if(argc>2)std::ofstream(argv[2])<<report.dump(2);
 shutdownJobs();return after<=std::max(.15,before*.9)?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;shutdownJobs();return 2;}}
