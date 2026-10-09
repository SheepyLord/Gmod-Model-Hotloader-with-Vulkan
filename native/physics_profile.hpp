#pragma once
#include "rig.hpp"
#include <array>
#include <functional>
#include <future>
namespace mmd {
// Physics profiles: the player's edits to a carrier's .phy (joint limits and
// friction, per-body mass share, damping, inertia, drag and surface, total
// mass, self-collision, animated friction). options.physicsOverrides holds a
// flat diff against the defaults below; its canonical form is hashed into the
// rig key, so equal settings share one carrier and an empty diff changes nothing.
constexpr int PhysicsSchema=1;
// Bump with any change to physicsText or solidMasses: edited carriers record it.
constexpr int PhysicsWriterVersion=1;
// The 18 carrier bodies in fitRig order (body i is solid i and PhysObj i).
inline constexpr std::array<const char*,18> CarrierBodyNames{"ValveBiped.Bip01_Pelvis","ValveBiped.Bip01_Spine1","ValveBiped.Bip01_Spine4","ValveBiped.Bip01_Head1","ValveBiped.Bip01_L_Clavicle","ValveBiped.Bip01_L_UpperArm","ValveBiped.Bip01_L_Forearm","ValveBiped.Bip01_L_Hand","ValveBiped.Bip01_R_Clavicle","ValveBiped.Bip01_R_UpperArm","ValveBiped.Bip01_R_Forearm","ValveBiped.Bip01_R_Hand","ValveBiped.Bip01_L_Thigh","ValveBiped.Bip01_L_Calf","ValveBiped.Bip01_L_Foot","ValveBiped.Bip01_R_Thigh","ValveBiped.Bip01_R_Calf","ValveBiped.Bip01_R_Foot"};
inline constexpr std::array<int,18> CarrierBodyParents{-1,0,1,2,2,4,5,6,2,8,9,10,0,12,13,0,15,16};
// SCMI's values for one body (the same parse as fitRig): what an unedited carrier writes.
struct BodyDefaults { float massBias=1,rotationDamping=3; btVector3 lower{0,0,0},upper{0,0,0}; };
const std::array<BodyDefaults,18>& carrierBodyDefaults();
struct PhysicsError { std::string code,path,detail; };
struct CanonicalPhysics { Json value=Json::object(); std::vector<PhysicsError> errors; };
// Pure, rig-independent, never throws. value is {} when everything is default or when errors exist.
CanonicalPhysics canonicalPhysics(const Json& raw);
// Throws std::runtime_error("Invalid physics settings: <code> <path> <detail>") for the first error.
Json requireCanonicalPhysics(const Json& raw);
// options with physicsOverrides canonicalised (throws) and erased when empty; dropped for role "arms".
Json normalizeCarrierOptions(Json options);
// The fitted-rig cache key of RequestCarrierFit/PrepareCarrier/PreviewCarrierFit: the
// options a fit depends on. Without physicsOverrides it is the string 2.2 used.
std::string carrierFitKey(const std::string& id,const Json& options);
bool validSurfaceprop(std::string_view);
// A collision override's shape style: "fitted" (absent), "box" or "capsule". Throws otherwise.
std::string shapeStyle(const Json& override);
// Writes the effective RigBody fields, automass and the manifest fields of a canonical profile. No-op for {}.
void applyPhysics(Rig&,const Json& canonical);
// Cubic Source units (in³) of a closed convex hull; faces are fan-triangulated about the centroid.
double hullVolume(const std::vector<btVector3>& hull,const Json& faces);
// The "mass" of each solid as written (float): the 2.2 expression for unedited carriers.
std::vector<float> solidMasses(const Rig&);
// The .phy text section without its trailing NUL; byte-identical to 2.2 for an unedited rig.
std::string physicsText(const Rig&);
// Bone-local primitive points inside center±extent, before convexTopology. Throws for an unknown style.
std::vector<btVector3> primitiveHull(const std::string& style,const btVector3& center,const btVector3& extent);
// Non-adjacent pairs that collide under this canonical profile (all 136 by default), sorted.
std::vector<std::pair<int,int>> enabledPairs(const Json& canonical);
struct Penetration { int a,b; float depth; };
// GJK/EPA at the rest pose for the given pairs (fitRig's solver setup); depths over 0.01 scale units.
std::vector<Penetration> restPenetrations(const Rig&,const std::vector<std::pair<int,int>>& pairs);
// The "ready" result of PreviewCarrierFit for a fitted rig: bodies, masses, overlaps and the exact .phy text.
Json previewCarrier(const Rig&);
// PreviewCarrierFit's off-thread refits. The editor polls with its latest draft, so
// at most one refit runs per model (two in all) and a newer draft waits for it;
// finished ones, also of drafts the editor moved on from, wait in a cache of four.
struct PreviewQueue {
 // The finished result for key, else {"status":"pending"} (run starts when nothing of this model runs).
 Json poll(const std::string& id,const std::string& key,std::function<Json()> run);
 // Whether a refit of this model still runs: deleting it waits, finished ones never block.
 bool busy(const std::string& id);
 // Drops this model's finished results.
 void forget(const std::string& id);
 std::map<std::string,std::future<Json>> running;std::vector<std::pair<std::string,Json>> finished;
private:
 void harvest();
};
}
