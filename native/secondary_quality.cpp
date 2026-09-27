#include "secondary.hpp"
#include "scene.hpp"
#include "jiggle_direction.hpp"
#include <chrono>
#include <cmath>
namespace mmd {
void Secondary::preparePresentationMode(){
 // Spring bones are already a lightweight solver: the jiggle level keeps them.
 int desired=std::min(1,accuracy());if(springOnly&&desired==0)desired=1;if(desired==presentationMode)return;
 // A mode switch is the only cheap-mode operation that touches the worker.
 // Drain first; never let an old Bullet result overwrite the new presentation.
 waitAsyncIdle();presentationMode=desired;jiggle.clear();simplifiedMs=0;suspendedReset=false;
 qualityBlendStart=-1;qualityBlendPose.clear();qualityBlendAnchors.clear();
 if(desired>0){
  instance.sourceTeleport=true; // fresh contacts/warm starts at latest pose
  presentHistory.clear();presentClock=-1;presentLag.fill(0);
  std::lock_guard lock(mutex);published.reset();presented.reset();
 }
}
void Secondary::stepSimplified(double seconds,bool teleport){
 auto started=std::chrono::steady_clock::now();
 if(!std::isfinite(seconds)||seconds<0)throw std::runtime_error("Invalid lightweight physics delta");
 if(presentationMode<0){instance.evaluate(false);simplifiedMs=0;return;}
 if(teleport||seconds>.25||qualityResume){jiggle.clear();qualityResume=false;}
 if(jiggle.empty()){
  jiggleRootReady=false;jiggleTravel.setZero();
  jiggle.resize(instance.model->bones.size());
  // Cache the nearest ValveBiped ancestor in one topological pass. Original
  // PMX names stay intact; sourceControl resolves the native bone mapping.
  std::vector<int> ancestors(jiggle.size(),-1);
  std::vector<bool> drivenAncestor(jiggle.size(),false);
  for(auto i:instance.model->order){
   int parent=instance.model->bones[i].parent;
   // Only the first simulated bone below an animated attachment has a spring.
   // Carry this through helper bones and branches; Source-controlled bones
   // start a new attachment. Children keep their animated local transforms.
   bool inherited=parent>=0&&drivenAncestor[parent];
   jiggle[i].active=drivers[i]>=0&&!inherited;
   drivenAncestor[i]=instance.sourceControl[i]<0&&(inherited||drivers[i]>=0);
   ancestors[i]=parent>=0?ancestors[parent]:-1;
   int control=instance.sourceControl[i];
   if(control>=0&&size_t(control)<instance.sourceRig->bones.size()){
    const auto& source=instance.sourceRig->bones[control];
    if(source.name.starts_with("ValveBiped.")&&source.mmd>=0)ancestors[i]=source.mmd;
   }
  }
  for(size_t i=0;i<jiggle.size();i++)if(jiggle[i].active){
   const auto& bone=instance.model->bones[i];auto& j=jiggle[i];btVector3 axis(0,0,0);
   j.valveParent=bone.parent>=0?ancestors[bone.parent]:-1;
   if(nanoemModelBoneHasDestinationBone(bone.source)){
    auto target=nanoemModelBoneGetTargetBoneObject(bone.source);
    if(target){auto p=nanoemModelBoneGetOrigin(target);axis=btVector3(p[0],p[1],p[2])-bone.position;}
   }else{auto p=nanoemModelBoneGetDestinationOrigin(bone.source);axis=btVector3(p[0],p[1],p[2]);}
   // Tails are display data the loader does not validate; a NaN one counts as missing.
   if(!std::isfinite(axis.length2()))axis.setZero();
   if(axis.length2()<1e-6f&&bone.parent>=0)axis=bone.position-instance.model->bones[bone.parent].position;
   if(axis.length2()<1e-6f)axis=btVector3(0,-.5f,0);
   j.length=btClamped(axis.length(),.15f,5.f);j.axis=axis.normalized();
  }
 }
 auto config=tuning();jiggleDamping=config["damping"];
 jiggleSeconds=qualitySuspended?0.f:float(std::min(seconds,.05));++jiggleFrame;
 // Root springs use the pure animation target. Descendants inherit that
 // root's presentation without adding independent spring motion.
 instance.evaluate(false);jiggleTargets=instance.global;
 const int root=instance.sourceRig->bones[0].mmd;
 if(root>=0&&jiggleSeconds>0){
  const auto position=jiggleTargets[root].getOrigin();auto delta=position-previousJiggleRoot;
  // Reject sub-unit idle jitter; a stopped/teleported character has no travel
  // direction. This is character translation, not the bone's spring velocity.
  jiggleTravel=jiggleRootReady&&delta.length2()*instance.sourceRig->scale*instance.sourceRig->scale>seconds*seconds?delta:btVector3(0,0,0);
  previousJiggleRoot=position;jiggleRootReady=true;
 }
 instance.evaluate(true);
 for(size_t i=0;i<bodies.size();i++)display[i]=bodies[i].bone>=0?instance.skin[bodies[i].bone]*bodies[i].initial:bodies[i].initial;
 simplifiedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
}
btTransform Secondary::jiggleFeedback(size_t bone,const btTransform& animated)const{
 if(bone>=jiggle.size()||!jiggle[bone].active)return animated;
 auto& j=jiggle[bone];const auto& targetPose=jiggleTargets[bone];const auto pivot=targetPose.getOrigin();const auto axis=targetPose.getBasis()*j.axis;
 if(j.frame!=jiggleFrame){
  j.frame=jiggleFrame;
  if(!j.ready){j.tip=pivot+axis*j.length;j.velocity.setZero();j.rotation=btQuaternion::getIdentity();j.ready=true;}
  else if(jiggleSeconds>0){
   constexpr float omega=20.f;
   const float damping=std::clamp(.65f*jiggleDamping,.15f,1.f),dt=jiggleSeconds;
   // The authored bone pose is the equilibrium. Constant gravity would curl
   // chains of tiny PMX bones even on a stationary character; this cheap mode
   // models motion inertia only, not a second approximation of Bullet gravity.
   auto target=pivot+axis*j.length;
   auto error=j.tip-target;auto velocity=j.velocity;
   const float decay=std::exp(-damping*omega*dt);
   if(damping<.999f){
    const float wd=omega*std::sqrt(1-damping*damping),c=std::cos(wd*dt),s=std::sin(wd*dt)/wd;
    j.tip=target+(error*c+(velocity+error*(damping*omega))*s)*decay;
    j.velocity=(velocity*c-(velocity*(damping*omega)+error*(omega*omega))*s)*decay;
   }else{
    const auto c=velocity+error*omega;j.tip=target+(error+c*dt)*decay;j.velocity=(velocity-c*(omega*dt))*decay;
   }
   auto delta=j.tip-pivot;auto direction=delta.length2()>1e-10f?delta.normalized():axis;
   auto swing=shortestArcQuat(axis,direction);float angle=swing.getAngleShortestPath();
   if(angle>.65f)swing=btQuaternion::getIdentity().slerp(swing,.65f/angle);
   j.directionScale=j.valveParent>=0?jiggleDirectionScale(jiggleTargets[j.valveParent].getOrigin()-pivot,jiggleTravel):1.f;
   swing=btQuaternion::getIdentity().slerp(swing,j.directionScale);
   j.velocity*=j.directionScale; // do not retain an inward impulse for later
   direction=quatRotate(swing,axis);
   // Rotation only: every bone pivot remains on its animated parent and the
   // tip retains its authored length. No stretch, contacts or Bullet work.
   j.tip=pivot+direction*j.length;
   j.velocity-=direction*j.velocity.dot(direction);
   if(j.velocity.length2()>j.length*j.length*omega*omega*4)j.velocity=j.velocity.normalized()*(j.length*omega*2);
   j.rotation=targetPose.getRotation().inverse()*swing*targetPose.getRotation();
  }
 }
 return btTransform((targetPose.getRotation()*j.rotation).normalized(),animated.getOrigin());
}
void Secondary::setQuality(int divisor,bool suspended){
 if(divisor!=1&&divisor!=2&&divisor!=4)throw std::runtime_error("Invalid secondary quality divisor");
 requestedDivisor=divisor;
 if(suspended==qualitySuspended)return;
 // Only transitions wait. A paused world submits no jobs and performs no
 // scene capture, stepping or solver work; presentation still follows anchors.
 waitAsyncIdle();
 qualitySuspended=suspended;
 if(!suspended){
  qualityResume=true;++qualityWakes;qualityBlendStart=instance.sourceTimestamp;qualityBlendPose=display;qualityBlendAnchors.clear();
  for(int index:anchors){auto& b=bodies[index];qualityBlendAnchors.push_back((instance.skin[b.bone]*b.initial).inverse());}
 }
}
void Secondary::blendQualityPresentation(){
 if(qualityBlendStart<0||qualityBlendPose.size()!=display.size())return;
 float t=float(std::clamp((instance.sourceTimestamp-qualityBlendStart)/.12,0.,1.));
 if(t>=1){qualityBlendStart=-1;qualityBlendPose.clear();qualityBlendAnchors.clear();return;}
 t=t*t*(3-2*t);
 for(size_t i=0;i<bodies.size();i++)if(!bodies[i].follower){
  auto old=qualityBlendPose[i];int anchor=bodies[i].anchor;
  if(anchor>=0){auto& a=bodies[anchors[anchor]];old=instance.skin[a.bone]*a.initial*qualityBlendAnchors[anchor]*old;}
  display[i]=btTransform(old.getRotation().slerp(display[i].getRotation(),t),old.getOrigin().lerp(display[i].getOrigin(),t));
 }
}
Json Secondary::qualityInfo()const{
 auto t=tuning();int full=t["iterations"];
 return {{"suspended",qualitySuspended||full<0},{"mode",full<0?"disabled":springOnly?"springs":full==0?"jiggle":"full"},{"divisor",requestedDivisor},{"iterations",qualitySuspended?0:std::min(full,std::max(2,(full+requestedDivisor-1)/requestedDivisor))},{"wakes",qualityWakes}};
}
void Secondary::resumeQualityPose(const std::vector<btTransform>& skin,const std::vector<btTransform>& pose){
 std::vector<btTransform> live;live.reserve(anchors.size());
 for(int index:anchors){auto& b=bodies[index];live.push_back(skin[b.bone]*b.initial);}
 for(auto& b:bodies){
  auto p=b.current;
  if(b.follower)p=b.bone>=0?skin[b.bone]*b.initial:b.initial;
  else if(b.anchor>=0)p=live[b.anchor]*currentAnchors[b.anchor].inverse()*p;
  auto rigid=nanoemRigidBody(b.value);rigid->setWorldTransform(p);rigid->setInterpolationWorldTransform(p);
  if(rigid->getMotionState())rigid->getMotionState()->setWorldTransform(p);
  rigid->setLinearVelocity({0,0,0});rigid->setAngularVelocity({0,0,0});rigid->clearForces();rigid->activate(true);
 }
 nanoemPhysicsWorldReset(world);followSoft(true,skin);if(springs)springs->settle(skin);
 inputs.clear();inputs.push_back({inputTime,pose});accumulator=0;simulationTime=inputTime;
 readSolved(true,instance.sourceRig->bones[0].mmd>=0?pose[instance.sourceRig->bones[0].mmd]:btTransform::getIdentity(),skin);
}
}
