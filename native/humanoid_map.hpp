#pragma once
// Which bone of a character is its hips, chest, left forearm...: the automatic
// assignment the bone window starts from, for skeletons read by the character
// converter (character_import.cpp) and for cached PMX models (InspectBoneMap).
// Every name token here was written for this project from public naming
// conventions (Mixamo, Unreal, Unity/VRoid, Rigify, 3ds Max Biped, Daz,
// ValveBiped and the MMD standard names); no third-party tables are used.
#include "runtime.hpp"
#include "humanoid_slots.hpp"
#include <map>
namespace mmd {
struct NameParts {
 std::string base,family,chain;  // folded name without its number; body family; swinging-part kind
 std::string digits;             // trailing ASCII digits as written ("01")
 char side=0;int number=-1;
 bool helper=false,weakHelper=false,ik=false,twist=false,nonDeform=false,secondaryHint=false,dbone=false,metacarpal=false,armToken=false,legToken=false,endToken=false,tipToken=false;
};
// Valid UTF-8 is kept; Shift-JIS (cp932) and GBK (cp936) names are decoded, other
// bytes become U+FFFD; control characters go and the result is cut to 255 bytes.
// issue: "", "cp932", "cp936" or "replaced". An empty result is the caller's to name.
std::string sanitizeBoneName(std::string_view raw,std::string* issue);
NameParts parseBoneName(std::string_view utf8,bool dazStyle);
// A skeleton in the character frame: metres, x the character's left, y up, z
// the direction it faces. Bones are parents first.
struct SkeletonBone {std::string name,english,nameIssue;int parent=-1;std::array<float,3> position{};uint32_t weighted=0;std::vector<std::string> flags;};
struct SkeletonView {std::vector<SkeletonBone> bones;std::vector<float> points;float height=0;};  // points: flat x,y,z,bone
struct BoneMeaning {std::string meaning;char side=0;int segment=0;int mirror=-1;bool helper=false,ik=false,twist=false,dbone=false,nonDeform=false,secondaryHint=false;};
struct SlotGuess {int bone=-1;float confidence=0;std::string method;};  // method: "name" | "topology" | ""
struct HumanoidGuess {std::map<std::string,SlotGuess> slots;SlotGuess eyeL,eyeR;bool humanoid=false;size_t candidates=0;};
std::vector<BoneMeaning> classifyBones(const SkeletonView&);
HumanoidGuess guessHumanoid(const SkeletonView&,const std::vector<BoneMeaning>&);
std::string skeletonSignature(const SkeletonView&,const std::vector<BoneMeaning>&);
int skeletonMaxDepth(const SkeletonView&);
// A cached PMX model in the character frame (PMX is +Y up, faces -Z, 8 cm units).
SkeletonView skeletonFromModel(const Model&,int maxPoints);
Json skeletonJson(const SkeletonView&,const std::vector<BoneMeaning>&);
Json guessJson(const HumanoidGuess&);
// The structural rules both the window and the importers enforce. values: slot
// key (eyes included) -> bone index or -1; chainRoots: swinging parts imported.
struct MapProblem {std::string code,slot,severity="error",reason;int bone=-1,other=-1;std::string text;};
std::vector<MapProblem> checkBoneMap(const SkeletonView&,const std::map<std::string,int>& values,const std::vector<int>& chainRoots);
// Swinging bones a part rooted at `root` brings: its subtree without unweighted leaves (their parent's tail).
int jiggleJointCount(const SkeletonView&,int root);
constexpr int MaxJiggleChains=64,MaxJiggleJoints=256;
// native.InspectBoneMap: the model's skeleton, the automatic assignment and,
// for options.values, the structural problems of that assignment.
Json inspectBoneMap(const Model&,const Json& options);
}
