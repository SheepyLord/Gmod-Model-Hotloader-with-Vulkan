#pragma once
// VRM spring bones (VRMC_springBone 1.0 and VRM 0.x secondaryAnimation): the
// Verlet chain every VRM runtime implements (UniVRM, three-vrm), run at the
// secondary world's fixed 60 Hz clock. Data arrives from the asset manifest in
// PMX bones, PMX units and MMD axes (vrm.cpp).
#include "runtime.hpp"
#include <functional>
namespace mmd {
struct SpringSetup {
 struct Collider {int bone=-1;bool capsule=false;btVector3 offset{0,0,0},tail{0,0,0};float radius=0;};
 // `axis`/`length`: rest direction and distance from the bone to its tail, in
 // the bone's rest frame (PMX rest frames carry no rotation).
 struct Joint {int spring=-1,bone=-1,tail=-1;btVector3 axis{0,-1,0};float length=1,hitRadius=0,stiffness=1,gravityPower=0,dragForce=.4f;btVector3 gravityDir{0,-1,0};};
 struct Spring {std::string name;int center=-1;std::vector<int> colliders;};
 float unitsPerMeter=12.5f;  // stiffness and gravity are VRM metres per second
 std::vector<Collider> colliders;std::vector<Spring> springs;std::vector<Joint> joints;
 std::vector<int> jointOfBone;      // bone -> joint, or -1
 std::vector<int> affected;         // joints and every bone below one, parents first
 std::vector<uint8_t> isAffected;
 // Throws on references outside the model; returns null when there are no joints.
 static std::shared_ptr<const SpringSetup> fromManifest(const Json& vrm,const Model&);
};
class SpringSystem {
public:
 // `controlled`: bones the Source skeleton drives. Their descendants move with
 // the character (a centre on the static avatar root cannot remove motion), and
 // each joint's nearest controlled ancestor is a point outside world geometry
 // for its contacts. `reference`: the hips bone the chains' damping is measured
 // against (-1 for plain VRM damping).
 SpringSystem(const Model&,std::shared_ptr<const SpringSetup>,const std::vector<uint8_t>& controlled,int reference);
 // Rest shape at this animated pose, no velocity.
 void reset(const std::vector<btTransform>& skin);
 // Keep the current bend at a new pose, no velocity (resuming a paused world).
 void settle(const std::vector<btTransform>& skin);
 // Keeps a joint tail (sphere of `radius`, `length` from `head`) out of world
 // geometry, on the side of `outside`; true when it moved.
 using WorldContact=std::function<bool(const btVector3& outside,const btVector3& head,btVector3& tail,float radius,float length)>;
 void step(float seconds,const std::vector<btTransform>& skin,float gravityScale,float dragScale,const WorldContact* world);
 void hold(){previousLocal=currentLocal;}
 // Per joint: rotation relative to the parent bone's global rotation.
 const std::vector<btQuaternion>& previous()const{return previousLocal;}
 const std::vector<btQuaternion>& current()const{return currentLocal;}
 // Current tail positions (simulation space), for diagnostics and tests.
 std::vector<btVector3> tails(const std::vector<btTransform>& skin)const;
 const SpringSetup& setup()const{return *data;}
 bool relativeDamping=true;  // mmdhl_vrm_relative_damping
 uint64_t colliderHits=0,worldHits=0,steps=0;double stepMs=0,totalMs=0;
private:
 const Model& model;std::shared_ptr<const SpringSetup> data;std::vector<uint8_t> moving;std::vector<int> anchors;int reference=-1;
 struct State {btVector3 tail{0,0,0},previousTail{0,0,0};};
 std::vector<State> state;std::vector<btQuaternion> previousLocal,currentLocal;std::vector<btTransform> global;
 btVector3 lastReference{0,0,0};bool haveReference=false;
 btTransform animated(const std::vector<btTransform>& skin,int bone)const{return skin[bone]*btTransform(btQuaternion::getIdentity(),model.bones[bone].position);}
 bool centred(const SpringSetup::Spring& s)const{return s.center>=0&&moving[s.center];}
 btTransform centre(const SpringSetup::Spring& s,const std::vector<btTransform>& skin)const;
 void place(const std::vector<btTransform>& skin,bool keepBend);
};
// A tail inside the surface through `point` with `normal` (either orientation:
// `outside` decides which side is free) is moved to that side, `radius` off the
// surface and, where the surface is in reach, still `length` from `head`: the
// strand slides along the floor instead of stretching or passing through it.
bool slideTail(const btVector3& outside,const btVector3& head,btVector3& tail,const btVector3& point,btVector3 normal,float radius,float length);
// Global switch shared by every instance (console variable in Lua).
void setSpringRelativeDamping(bool);
bool springRelativeDamping();
}
