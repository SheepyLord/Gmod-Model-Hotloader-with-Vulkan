// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2015-2023 hkrn All rights reserved (adapted emapp portions).
// Independent API replay of the pinned nanoem backend. The runtime and replay
// receive the same timestamped follower transforms; no Secondary solver is used here.
#include "runtime.hpp"
#include "rig.hpp"
#include "secondary.hpp"
#include "scene.hpp"
#include "contact_fixture.hpp"
#include "compute_solver.hpp"
#include "jobs.hpp"
#include <ext/physics.h>
#include <iostream>
#include <algorithm>
#include <set>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "wide_main.hpp"
using namespace mmd;
struct Input {nanoem_physics_world_t* world; nanoem_physics_rigid_body_t *a,*b;float aTransform[16],bTransform[16];};
struct Replay {
 nanoem_physics_world_t* world=nullptr;std::vector<nanoem_physics_rigid_body_t*> bodies;std::vector<nanoem_physics_joint_t*> joints;std::vector<nanoem_physics_soft_body_t*> soft;std::vector<bool> followers,active;std::vector<btTransform> initial;
 Replay(const Instance& instance){nanoem_status_t status=NANOEM_STATUS_SUCCESS;world=nanoemPhysicsWorldCreate(nullptr,&status);nanoemPhysicsWorldSetGroundEnabled(world,false);nanoemPhysicsWorldSetPreferredFPS(world,60);nanoemPhysicsWorldSetActive(world,true);
  for(auto source:instance.model->bodies){auto b=nanoemPhysicsRigidBodyCreate(source,nullptr,&status);if(!b)throw std::runtime_error("Reference body creation failed");int bone=boneIndex(nanoemModelRigidBodyGetBoneObject(source));bool follows=nanoemModelRigidBodyGetTransformType(source)==0||(bone>=0&&instance.sourceControl[bone]>=0);if(follows)nanoemPhysicsRigidBodySetKinematic(b,true);nanoemPhysicsWorldAddRigidBody(world,b);bodies.push_back(b);followers.push_back(follows);float m[16];nanoemPhysicsMotionStateGetInitialWorldTransform(nanoemPhysicsRigidBodyGetMotionState(b),m);btTransform t;t.setFromOpenGLMatrix(m);initial.push_back(t);}
  for(size_t ji=0;ji<instance.model->joints.size();ji++){auto source=instance.model->joints[ji];auto ref=instance.model->jointReferences[ji];if(!ref.valid)continue;int a=ref.a,b=ref.b;Input input{world,a<0?nullptr:bodies[a],b<0?nullptr:bodies[b]};btTransform::getIdentity().getOpenGLMatrix(input.aTransform);btTransform::getIdentity().getOpenGLMatrix(input.bTransform);auto j=nanoemPhysicsJointCreate(source,&input,&status);if(!j)throw std::runtime_error("Reference joint creation failed");bool enabled=!((a<0||followers[a])&&(b<0||followers[b]));if(enabled)nanoemPhysicsWorldAddJoint(world,j);joints.push_back(j);active.push_back(enabled);}
  for(size_t i=0;i<bodies.size();i++){int bi=boneIndex(nanoemModelRigidBodyGetBoneObject(instance.model->bodies[i]));auto t=bi<0?initial[i]:instance.skin[bi]*initial[i];float m[16];t.getOpenGLMatrix(m);nanoemPhysicsRigidBodySetWorldTransform(bodies[i],m);nanoemPhysicsRigidBodyResetStates(bodies[i]);}
  for(auto source:instance.model->softBodies){auto s=nanoemPhysicsSoftBodyCreate(source,world,&status);if(!s)throw std::runtime_error("Reference soft body creation failed");nanoemPhysicsWorldAddSoftBody(world,s);soft.push_back(s);}
  nanoemPhysicsWorldReset(world);followSoft(*instance.model,instance.skin,true);
 }
 ~Replay(){for(auto s:soft){nanoemPhysicsWorldRemoveSoftBody(world,s);nanoemPhysicsSoftBodyDestroy(s);}for(size_t i=0;i<joints.size();i++){if(active[i])nanoemPhysicsWorldRemoveJoint(world,joints[i]);nanoemPhysicsJointDestroy(joints[i]);}for(auto b:bodies){nanoemPhysicsWorldRemoveRigidBody(world,b);nanoemPhysicsRigidBodyDestroy(b);}nanoemPhysicsWorldDestroy(world);}
 void followSoft(const Model& model,const std::vector<btTransform>& animation,bool all){
  for(size_t si=0;si<soft.size();si++){nanoem_rsize_t count=0;auto pins=nanoemModelSoftBodyGetAllPinnedVertexIndices(model.softBodies[si],&count);std::set<int> pinned(pins,pins+count);auto s=soft[si];
   for(int i=0;i<nanoemPhysicsSoftBodyGetNumVertexObjects(s);i++){int vi=vertexIndex(nanoemPhysicsSoftBodyGetVertexObject(s,i));if(vi<0||!(all||pinned.contains(vi)))continue;auto p=skinPosition(model.vertices[vi],animation),n=skinNormal(model.vertices[vi],animation);float position[4]={p.x(),p.y(),p.z(),0},normal[4]={n.x(),n.y(),n.z(),0};nanoemPhysicsSoftBodySetVertexPosition(s,i,position);nanoemPhysicsSoftBodySetVertexNormal(s,i,normal);}
  }
 }
 void tick(const Model& model,const std::vector<btTransform>& animation){for(size_t i=0;i<bodies.size();i++)if(followers[i]){int bi=boneIndex(nanoemModelRigidBodyGetBoneObject(model.bodies[i]));auto t=bi<0?initial[i]:animation[bi]*initial[i];float m[16];t.getOpenGLMatrix(m);nanoemPhysicsMotionStateSetCurrentWorldTransform(nanoemPhysicsRigidBodyGetMotionState(bodies[i]),m);nanoemPhysicsRigidBodyResetStates(bodies[i]);}followSoft(model,animation,false);nanoemPhysicsWorldStepSimulation(world,1.f/60);}
};
int wmain(int argc,wchar_t** argv){struct Cleanup{~Cleanup(){shutdownCompute();shutdownJobs();}}cleanup;try{if(argc<2)throw std::runtime_error("Pass a PMX file [--backend reference|cpu_mt|gpu_opencl] [--broadphase dbvt|dbvt-fast|sap] [--scene contacts] [--tolerance MMD] [--validate-compute-rows]");std::string broadphase="dbvt",backend="reference";double tolerance=1e-4;int collisionMode=0;
 for(int i=2;i<argc;i++){std::wstring key=argv[i];if(key==L"--validate-compute-rows"){setComputeValidation(true);continue;}if(i+1>=argc)throw std::runtime_error("Missing value");if(key==L"--backend")backend=utf8(argv[++i]);else if(key==L"--broadphase")broadphase=utf8(argv[++i]);else if(key==L"--scene"){if(std::wstring(argv[++i])!=L"contacts")throw std::runtime_error("Scene must be contacts");collisionMode=2;}else if(key==L"--tolerance")tolerance=std::stod(argv[++i]);else throw std::runtime_error("Unknown option");}
 double maxRowVelocity=0,maxRowImpulse=0;
 isSapBroadphase(broadphase);if(!std::isfinite(tolerance)||tolerance<=0)throw std::runtime_error("Invalid tolerance");auto model=parse(readFile(argv[1]));World host;auto handle=host.create(model,{{"backend","source"},{"secondaryBackend",backend},{"secondaryCollision",collisionMode},{"secondaryBroadphase",broadphase}});auto& instance=host.get(handle);auto& rig=*instance.sourceRig;Json result={{"secondaryBackendRequested",backend},{"secondaryBackend",instance.secondary->effectiveBackend},{"bodies",model->bodies.size()},{"joints",model->joints.size()},{"frames",600},{"carrierObjects",rig.bodies.size()}};double maximum=0,sum=0,bindError=0,first=0,followerMax=0,rotationError=0,feedbackPositionError=0,feedbackRotationError=0;int worstBody=0,worstFrame=0;size_t compared=0;std::vector<btTransform> pose(18),manual(rig.bones.size(),btTransform::getIdentity());
 for(size_t i=0;i<pose.size();i++)pose[i]=rig.bones[rig.bodies[i].bone].rest;
 if(collisionMode)publishScene(test::contactScene(0,collisionMode));
 instance.submitSourcePose(pose,manual,0);instance.evaluate(false);
 for(auto& bone:rig.bones)if(bone.mmd>=0)bindError=std::max(bindError,double((instance.global[bone.mmd].getOrigin()-model->bones[bone.mmd].position).length()));
 World animationHost;auto animationHandle=animationHost.create(model,{{"backend","source"},{"secondaryCollision",0}});auto& animation=animationHost.get(animationHandle);animation.submitSourcePose(pose,manual,0,true);animation.evaluate(false);Replay replay(animation);if(nanoemBroadphaseInfo(replay.world)["broadphase"]!="dbvt")throw std::runtime_error("Reference broadphase contaminated");test::ReferenceScene referenceScene(nanoemWorld(replay.world),instance);if(collisionMode)referenceScene.sync(*test::contactScene(0,collisionMode));replay.tick(*model,animation.skin);int maximumExternalContacts=0;double softError=0,softMagnitude=0,animationError=0;
 for(int frame=1;frame<=600;frame++){
  float t=frame/60.f;btTransform driving(btQuaternion(btVector3(0,0,1),btSin(t*1.3f)*.2f),btVector3(btSin(t*1.7f)*5,btCos(t*.9f)*3,btSin(t*2)*4));
  for(size_t i=0;i<pose.size();i++)pose[i]=driving*rig.bones[rig.bodies[i].bone].rest;
  if(collisionMode){auto scene=test::contactScene(double(frame)/60,collisionMode);publishScene(scene);referenceScene.sync(*scene);}
  instance.submitSourcePose(pose,manual,double(frame)/60);auto feedback=instance.global,locals=instance.local;animation.submitSourcePose(pose,manual,double(frame)/60,true);animation.evaluate(false);instance.evaluate(false);for(size_t k=0;k<instance.skin.size();k++)animationError=std::max(animationError,double((instance.skin[k].getOrigin()-animation.skin[k].getOrigin()).length()));replay.tick(*model,animation.skin);
  auto diagnostics=instance.secondary->diagnostics();maximumExternalContacts=std::max(maximumExternalContacts,diagnostics["externalContacts"].get<int>());auto actual=diagnostics["bodyList"];
  if(diagnostics.contains("compute")){auto& compute=diagnostics["compute"];maxRowVelocity=std::max(maxRowVelocity,compute.value("rowVelocityError",0.));maxRowImpulse=std::max(maxRowImpulse,compute.value("rowImpulseError",0.));}
  if(!replay.soft.empty()){Snapshot snapshot;snapshot.vertices.resize(model->vertices.size());instance.secondary->deformSoft(snapshot);
   for(size_t i=0;i<replay.soft.size();i++)for(int v=0;v<nanoemPhysicsSoftBodyGetNumVertexObjects(replay.soft[i]);v++){float expected[4];nanoemPhysicsSoftBodyGetVertexPosition(replay.soft[i],v,expected);softMagnitude=std::max(softMagnitude,double(vec(expected).length()));int vi=vertexIndex(nanoemPhysicsSoftBodyGetVertexObject(replay.soft[i],v));if(vi<0)continue;auto& observed=snapshot.vertices[vi];auto point=instance.placement*(toSource(vec(expected))*instance.scale)/Inch;double e=(point-btVector3(observed.x,observed.y,observed.z)).length()*Inch/instance.scale;if(!std::isfinite(e))throw std::runtime_error("Non-finite soft comparison");softError=std::max(softError,e);}
  }
  for(size_t i=0;i<replay.bodies.size();i++){
   float m[16];nanoemPhysicsRigidBodyGetWorldTransform(replay.bodies[i],m);auto p=actual[i]["position"];double error=btVector3(m[12]-p[0].get<float>(),m[13]-p[1].get<float>(),m[14]-p[2].get<float>()).length();if(!std::isfinite(error))throw std::runtime_error("Non-finite comparison");if(frame==1)first=std::max(first,error);if(replay.followers[i])followerMax=std::max(followerMax,error);if(error>maximum){maximum=error;worstBody=int(i);worstFrame=frame;}sum+=error*error;compared++;
   auto q=actual[i]["rotation"];btMatrix3x3 rotation(btQuaternion(q[0],q[1],q[2],q[3]));for(int row=0;row<3;row++)for(int col=0;col<3;col++)rotationError=std::max(rotationError,double(std::abs(rotation[row][col]-m[col*4+row])));
   int bone=boneIndex(nanoemModelRigidBodyGetBoneObject(model->bodies[i]));
   if(bone>=0&&!replay.followers[i]&&instance.secondary->drivers[bone]==int(i)){
    // nanoem emapp RigidBody.cc feedback, evaluated independently with GLM.
    float initial[16];replay.initial[i].getOpenGLMatrix(initial);auto expected=glm::make_mat4(m)*glm::inverse(glm::make_mat4(initial));auto origin=model->bones[bone].position;expected=glm::translate(expected,glm::vec3(origin.x(),origin.y(),origin.z()));
    if(nanoemModelRigidBodyGetTransformType(model->bodies[i])==NANOEM_MODEL_RIGID_BODY_TRANSFORM_TYPE_FROM_BONE_ORIENTATION_AND_SIMULATION_TO_BONE){auto offset=locals[bone].getOrigin();expected[3]-=glm::vec4(offset.x(),offset.y(),offset.z(),0);}
    auto position=feedback[bone].getOrigin();feedbackPositionError=std::max(feedbackPositionError,double(glm::length(glm::vec3(expected[3])-glm::vec3(position.x(),position.y(),position.z()))));
    for(int row=0;row<3;row++)for(int col=0;col<3;col++)feedbackRotationError=std::max(feedbackRotationError,double(std::abs(feedback[bone].getBasis()[row][col]-expected[col][row])));
   }
  }
 }
 auto files=carrierFiles(rig);auto& mdl=files.at(rig.path);auto integer=[&](size_t at){int value;std::memcpy(&value,mdl.data()+at,4);return value;};
 result.update({{"maxBodyPositionErrorMMD",maximum},{"firstFrameError",first},{"followerError",followerMax},{"worstBody",worstBody},{"worstFrame",worstFrame},{"rmsBodyPositionErrorMMD",std::sqrt(sum/compared)},{"bindErrorMMD",bindError},{"headerVersion",integer(4)},{"nativeBones",integer(156)},{"nativeFlexes",integer(268)},{"mdlSizeValid",integer(76)==int(mdl.size())},{"droppedTime",instance.secondary->dropped}});
 result.update({{"bodyRotationMatrixError",rotationError},{"boneFeedbackPositionErrorMMD",feedbackPositionError},{"boneFeedbackRotationMatrixError",feedbackRotationError}});
 result["animationInputError"]=animationError;result.update({{"softBodies",replay.soft.size()},{"softPositionErrorMMD",softError},{"maxSoftPositionMagnitudeMMD",softMagnitude}});
 // Soft-body iterative constraints accumulate tiny floating-point differences
 // between independent worlds; 0.01 MMD units is below one millimetre at the
 // acceptance scale. Rigid-body and feedback comparisons remain much tighter.
 result["collisionMode"]=collisionMode;result["maximumExternalContacts"]=maximumExternalContacts;
 result["candidateBroadphase"]=instance.secondary->diagnostics(false)["broadphase"];result["referenceBroadphase"]=nanoemBroadphaseInfo(replay.world)["broadphase"];result["positionToleranceMMD"]=tolerance;result["strictReferenceAgreement"]=maximum<1e-4&&rotationError<1e-4;
 auto finalDiagnostics=instance.secondary->diagnostics(false);result["secondaryBackend"]=finalDiagnostics["secondaryBackend"];result["secondaryBackendFallback"]=finalDiagnostics["secondaryBackendFallback"];result["rowVelocityError"]=maxRowVelocity;result["rowImpulseError"]=maxRowImpulse;
 bool ok=(!collisionMode||maximumExternalContacts>0)&&maximum<tolerance&&bindError<1e-4&&rotationError<1e-4&&feedbackPositionError<1e-3&&feedbackRotationError<1e-4&&softError<.01&&softMagnitude<10000&&rig.bodies.size()==18&&integer(156)==int(rig.bones.size())&&integer(268)<=96;result["passed"]=ok;std::cout<<result.dump(2)<<std::endl;return ok?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 2;}}
