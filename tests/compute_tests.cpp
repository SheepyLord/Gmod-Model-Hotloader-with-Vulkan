#include "runtime.hpp"
#include "scene.hpp"
#include "jobs.hpp"
#include "compute_solver.hpp"
#include "vulkan_solver.hpp"
#include <BulletDynamics/ConstraintSolver/btGeneric6DofSpringConstraint.h>
#include <iostream>
#include <LinearMath/btThreads.h>
#include <vector>
using namespace mmd;
struct Fixture {
 nanoem_physics_world_t* world=nullptr;
 std::vector<std::unique_ptr<btCollisionShape>> shapes;
 std::vector<std::unique_ptr<btDefaultMotionState>> states;
 std::vector<std::unique_ptr<btRigidBody>> bodies;
 std::vector<std::unique_ptr<btTypedConstraint>> joints;
 explicit Fixture(const std::string& backend){nanoem_status_t status=NANOEM_STATUS_SUCCESS;world=nanoemCreateSecondaryWorld({},&status,backend);nanoemPhysicsWorldSetGroundEnabled(world,false);nanoemWorld(world)->setGravity({0,0,0});}
 ~Fixture(){for(auto& j:joints)nanoemWorld(world)->removeConstraint(j.get());for(auto& b:bodies)nanoemWorld(world)->removeRigidBody(b.get());nanoemPhysicsWorldDestroy(world);}
 btRigidBody& add(int kind,float mass,btVector3 position){
  if(kind==0)shapes.push_back(std::make_unique<btSphereShape>(.3f));else if(kind==1)shapes.push_back(std::make_unique<btBoxShape>(btVector3(.3f,.4f,.2f)));else shapes.push_back(std::make_unique<btCapsuleShape>(.2f,.5f));
  btVector3 inertia(0,0,0);if(mass>0)shapes.back()->calculateLocalInertia(mass,inertia);states.push_back(std::make_unique<btDefaultMotionState>(btTransform(btQuaternion::getIdentity(),position)));btRigidBody::btRigidBodyConstructionInfo ci(mass,states.back().get(),shapes.back().get(),inertia);ci.m_linearDamping=.1f;ci.m_angularDamping=.15f;bodies.push_back(std::make_unique<btRigidBody>(ci));bodies.back()->setActivationState(DISABLE_DEACTIVATION);nanoemWorld(world)->addRigidBody(bodies.back().get());return *bodies.back();
 }
 void setup(int test){
  if(test<6){int axis=test%3;bool angular=test>=3;auto& anchor=add(0,0,{0,4,0});auto& body=add(test%3,1,{0,4,0});anchor.setCollisionFlags(anchor.getCollisionFlags()|btCollisionObject::CF_NO_CONTACT_RESPONSE);
   auto joint=std::make_unique<btGeneric6DofSpringConstraint>(anchor,body,btTransform::getIdentity(),btTransform::getIdentity(),true);
   joint->setLinearLowerLimit({0,0,0});joint->setLinearUpperLimit({0,0,0});joint->setAngularLowerLimit({0,0,0});joint->setAngularUpperLimit({0,0,0});btVector3 lower(0,0,0),upper(0,0,0);lower[axis]=-1;upper[axis]=1;if(angular){joint->setAngularLowerLimit(lower);joint->setAngularUpperLimit(upper);}else{joint->setLinearLowerLimit(lower);joint->setLinearUpperLimit(upper);}
   joint->enableSpring(test,true);joint->setStiffness(test,8);joint->setDamping(test,.5f);joint->setEquilibriumPoint();nanoemWorld(world)->addConstraint(joint.get(),true);joints.push_back(std::move(joint));btVector3 impulse(0,0,0);impulse[axis]=angular?.1f:1.f;if(angular)body.applyTorqueImpulse(impulse);else body.applyCentralImpulse(impulse);
  }else if(test==7){
   nanoemWorld(world)->getSolverInfo().m_minimumSolverBatchSize=1;
   auto& anchor=add(0,2,{0,4,0});anchor.setCollisionFlags(anchor.getCollisionFlags()|btCollisionObject::CF_KINEMATIC_OBJECT|btCollisionObject::CF_NO_CONTACT_RESPONSE);
   for(int i=0;i<3;i++){
    auto& body=add(1,1,{float(i+1),4,0});btTransform frame=btTransform::getIdentity();frame.setOrigin({float(i+1),0,0});
    auto joint=std::make_unique<btGeneric6DofSpringConstraint>(anchor,body,frame,btTransform::getIdentity(),true);
    joint->setLinearLowerLimit({0,0,0});joint->setLinearUpperLimit({0,0,0});joint->setAngularLowerLimit({0,0,0});joint->setAngularUpperLimit({0,0,0});
    nanoemWorld(world)->addConstraint(joint.get(),true);joints.push_back(std::move(joint));body.applyCentralImpulse({float(i+1),0,.3f});
   }
  }else{
   nanoemWorld(world)->setGravity({0,-9.8f,0});auto& ground=add(1,0,{0,-1,0});ground.getCollisionShape()->setLocalScaling({30,1,30});nanoemWorld(world)->updateSingleAabb(&ground);
   for(int i=0;i<3;i++){auto& b=add(i,1,{float(i-1)*2,2,0});b.setFriction(.6f);b.setRestitution(.15f);b.setLinearVelocity({.3f,0,.1f});}
  }
 }
};
int main(int argc,char** argv){try{
 btSetTaskScheduler(btGetSequentialTaskScheduler());initializeBulletScheduler();std::string backend=argc>1?argv[1]:"cpu_mt";if(backend=="gpu_opencl"){auto caps=openclCapabilities();std::cout<<caps.dump()<<"\n";if(!caps["available"].get<bool>())throw std::runtime_error("OpenCL capability probe failed");}
 // gpu_vulkan [colored|ordered]: the Vulkan solver with either level layout.
 if(backend=="gpu_vulkan"){setVulkanColoring(!(argc>2&&std::string(argv[2])=="ordered"));auto caps=vulkanCapabilities(false);std::cout<<caps.dump()<<"\n";if(!caps["available"].get<bool>())throw std::runtime_error("Vulkan capability probe failed");}
 setComputeValidation(backend=="gpu_opencl"||backend=="gpu_vulkan");Json results=Json::array();bool passed=true;
 for(int test=0;test<8;test++){Fixture reference("reference"),candidate(backend);reference.setup(test);candidate.setup(test);double maxPosition=0,maxVelocity=0,maxRotation=0;double referenceEnergy=0,candidateEnergy=0,maxRowVelocity=0,maxRowImpulse=0;Json perBody=Json::array();for(size_t i=0;i<reference.bodies.size();i++)perBody.push_back({{"position",0.},{"rotation",0.}});
  for(int frame=0;frame<600;frame++){nanoemStepFixed(reference.world,1.f/60);nanoemStepFixed(candidate.world,1.f/60);auto stats=nanoemBroadphaseInfo(candidate.world);if(stats.contains("compute")){maxRowVelocity=std::max(maxRowVelocity,stats["compute"].value("rowVelocityError",0.));maxRowImpulse=std::max(maxRowImpulse,stats["compute"].value("rowImpulseError",0.));}for(size_t i=0;i<reference.bodies.size();i++){auto& a=*reference.bodies[i];auto& b=*candidate.bodies[i];double p=(a.getWorldTransform().getOrigin()-b.getWorldTransform().getOrigin()).length(),v=(a.getLinearVelocity()-b.getLinearVelocity()).length();double r=a.getWorldTransform().getRotation().angleShortestPath(b.getWorldTransform().getRotation());if(!std::isfinite(p+v+r))throw std::runtime_error("Non-finite result");perBody[i]["position"]=std::max(perBody[i]["position"].get<double>(),p);perBody[i]["rotation"]=std::max(perBody[i]["rotation"].get<double>(),r);maxPosition=std::max(maxPosition,p);maxVelocity=std::max(maxVelocity,v);maxRotation=std::max(maxRotation,r);referenceEnergy+=a.getLinearVelocity().length2()+a.getAngularVelocity().length2();candidateEnergy+=b.getLinearVelocity().length2()+b.getAngularVelocity().length2();}}
  bool ok=maxPosition<(test!=6?.01:.1)&&maxRotation<(test!=6?.02:.25)&&std::abs(candidateEnergy-referenceEnergy)<=std::max(.001,referenceEnergy*.05);passed&=ok;results.push_back({{"test",test},{"perBody",perBody},{"rowVelocityError",maxRowVelocity},{"rowImpulseError",maxRowImpulse},{"maxPosition",maxPosition},{"maxVelocity",maxVelocity},{"maxRotation",maxRotation},{"energyRatio",candidateEnergy/std::max(1e-20,referenceEnergy)},{"passed",ok}});
 }
 std::cout<<Json({{"backend",backend},{"ordering",backend=="gpu_vulkan"?(vulkanColoring()?"colored":"ordered"):"-"},{"passed",passed},{"tests",results}}).dump(2)<<"\n";shutdownCompute();shutdownJobs();return passed?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";shutdownCompute();shutdownJobs();return 1;}}
