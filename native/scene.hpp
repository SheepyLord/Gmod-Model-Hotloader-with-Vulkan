#pragma once
#include "runtime.hpp"
#include <ext/physics.h>
#include "broadphase.hpp"
namespace mmd {
constexpr int ExternalCollisionTag=0x4d4d44;
// What hair and clothing collide with: the checkboxes in the physics settings.
// Character is the model's own body (its bone-following PMX bodies and VRM
// colliders); the others come from the captured Source scene.
namespace Collide {
constexpr unsigned World=1,Character=2,Objects=4,Players=8,Npcs=16,All=31;
constexpr unsigned Scene=World|Objects|Players|Npcs,Default=Character|Objects;
}
struct SceneGeometry {
 enum Kind { Sphere,Triangles,Convexes } kind=Convexes;
 std::vector<btVector3> vertices;
 std::vector<int> hullCounts;
 float radius=0;
 btVector3 minimum{0,0,0},maximum{0,0,0};
};
struct SceneObject {
 uint64_t id=0,owner=0;
 int physicsBone=0;btVector3 localCenter{0,0,0};
 std::shared_ptr<const SceneGeometry> geometry;
 btTransform transform=btTransform::getIdentity();
 btVector3 velocity{0,0,0},angular{0,0,0};
 bool isStatic=false;
 // A living player's or NPC's physics shadow: characters collide with them only
 // when asked (Collide::Players, Collide::Npcs).
 enum Actor:uint8_t {NoActor=0,LivingPlayer=1,LivingNpc=2};
 uint8_t actor=NoActor;
};
struct SceneFrame {uint64_t sequence=0;double timestamp=0,captureMs=0;unsigned ownedObjects=0;std::vector<SceneObject> objects;};
// Frames and geometry cross realms: the client's secondary worlds keep what the
// server captured (bridge.cpp, in the server module). A shared_ptr's control
// block runs the code of the module that made it when the last reference goes,
// and quitting unloads the server module before the client drops its mirrors.
// These make them here, in the runtime, which outlives both realm modules.
std::shared_ptr<SceneFrame> newSceneFrame();
std::shared_ptr<SceneGeometry> newSceneGeometry();
void publishScene(std::shared_ptr<const SceneFrame>);
std::shared_ptr<const SceneFrame> readScene(World* host=nullptr);
// Where the scene's consumers need objects (Source units). Each secondary world
// registers its character's collision box when it syncs, each remote subscriber
// the sphere it is sent; the server capture skips objects outside every region.
// A region lapses when it is not refreshed for two seconds.
struct SceneRegion {btVector3 lower,upper;};
void noteSceneInterest(uintptr_t consumer,const btVector3& lower,const btVector3& upper);
void forgetSceneInterest(uintptr_t consumer);
std::vector<SceneRegion> sceneInterest();
btSoftRigidDynamicsWorld* nanoemWorld(nanoem_physics_world_t*);
nanoem_physics_world_t* nanoemCreateSecondaryWorld(const BroadphaseConfig&,nanoem_status_t*,const std::string& backend="reference");
Json nanoemBroadphaseInfo(nanoem_physics_world_t*);
// Diagnostic pair counters of the multicore dispatchers (profiler only).
void nanoemSetPairCounting(nanoem_physics_world_t*,bool enabled);
void nanoemStepFixed(nanoem_physics_world_t*,float seconds);
btRigidBody* nanoemRigidBody(nanoem_physics_rigid_body_t*);
btTypedConstraint* nanoemConstraint(nanoem_physics_joint_t*);
}
