#pragma once
#include <atomic>
#include <btBulletDynamicsCommon.h>
#include <BulletSoftBody/btSoftRigidDynamicsWorld.h>
#include <nanoem.h>
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace mmd {
using Json=nlohmann::json;
using Bytes=std::vector<unsigned char>;
namespace fs=std::filesystem;
fs::path ioPath(const fs::path& path);
constexpr float Inch=.0254f;
constexpr float ScmiSourceUnitsPerPmx=.08f*40.457f;
float resolveSourceScale(const Json&,float height);
constexpr unsigned ApiVersion=1;
struct Vertex {
    btVector3 position{0,0,0},normal{0,1,0},c{0,0,0},r0{0,0,0},r1{0,0,0};
    std::array<float,2> uv{};
    std::array<std::array<float,4>,4> extra{};
    std::array<int,4> bones{-1,-1,-1,-1};
    std::array<float,4> weights{};
    float edge=1;
    int type=0;
};
struct Bone {
    std::string name,english;
    int parent=-1,inherit=-1,stage=0;
    float coefficient=0;
    bool inheritRotation=false,inheritTranslation=false,localInherit=false,afterPhysics=false;
    btVector3 position{0,0,0},fixedAxis{0,0,0};
    const nanoem_model_bone_t* source=nullptr;
};
struct Material {
    std::string name,base,sphere,toon;
    unsigned first=0,count=0;
    btVector3 diffuse{1,1,1},ambient{.2f,.2f,.2f},specular{0,0,0},edgeColor{0,0,0};
    float alpha=1,power=1,edgeAlpha=1,edgeSize=1;
    // Share of the part's surface whose base texels pass the 0.5 alpha test,
    // sampled at its triangles' UVs (loadAsset; 1 without texture alpha).
    float alphaCoverage=1;
    std::array<std::array<float,4>,3> textureBlend{{{1,1,1,1},{1,1,1,1},{1,1,1,1}}};
    int sphereMode=0,toonIndex=-1;
    bool twoSided=false,edge=false,shadow=true,alphaTexture=false,translucentTexture=false;
};
struct Rig;
struct SpringSetup;
struct OverlapTriangle {unsigned primitive;std::array<unsigned,3> vertices;unsigned material;};
// Source hardware skinning (the path studio models use): three bones per
// vertex, 53 bone matrices per draw. Built once per model. Vertices the shaders
// cannot reproduce (SDEF/QDEF, or a fourth weight that visibly matters) keep
// CPU skinning together with every triangle that uses them.
struct GpuSkin {
    static constexpr unsigned MaxBones=53;
    // Largest position change allowed from dropping a fourth weight, in PMX units
    // (about 0.16 Source units at the default scale), under the test poses.
    static constexpr float MaxDropError=.05f;
    unsigned palette=0;                          // palette entries: bones plus the identity entry
    std::vector<uint8_t> cpuVertex,cpuTriangle;
    std::vector<std::array<unsigned,3>> bones;   // palette indices; unused slots repeat the first bone
    std::vector<std::array<float,3>> weights;    // renormalised to sum 1; unused slots are 0
    std::vector<unsigned> cpuVertices;           // deformed on the CPU: CPU triangles plus overlap-guard candidates
    std::vector<std::array<float,6>> boneBounds; // rest-space box per palette entry, widened by morph reach; min>max if unused
    std::vector<float> boneReach;                // largest summed vertex-morph offset per palette entry (the reach at weight 1)
    size_t fourWeight=0,droppedWeight=0;
};
struct Model {
    nanoem_unicode_string_factory_t* factory=nullptr;
    nanoem_model_t* source=nullptr;
    std::string name,id;
    std::vector<Vertex> vertices;
    std::vector<btVector3> tangents;
    std::vector<float> tangentSigns;
    std::vector<unsigned> indices;
    // Candidate coincident surfaces; retained in the model and checked again
    // after deformation before additive lighting. No authored faces are deleted.
    std::vector<std::vector<OverlapTriangle>> lightOverlaps;
    std::vector<unsigned> lightLayerRanks;
    // Per triangle, 0 when the 0.5 alpha test removes every sampled texel of it
    // (loadAsset; empty: nothing measured). Only RTX Remix draws skip those.
    std::vector<uint8_t> cutoutTriangles;
    std::vector<Bone> bones;
    std::vector<unsigned> order;
    std::vector<Material> materials;
    std::vector<std::string> morphNames,warnings;
    std::vector<const nanoem_model_rigid_body_t*> bodies;
    std::vector<const nanoem_model_joint_t*> joints;
    struct JointReference { int a=-1,b=-1; bool valid=false; std::string reason; };
    std::vector<JointReference> jointReferences;
    std::vector<const nanoem_model_soft_body_t*> softBodies;
    std::vector<const nanoem_model_morph_t*> morphs;
    btVector3 minimum{0,0,0},maximum{0,0,0};
    std::shared_ptr<Rig> fittedRig;
    // VRM avatars: spring bones simulated natively in place of PMX rigid bodies.
    std::shared_ptr<const SpringSetup> springs;
    // Characters converted from other formats: their bone assignment (slot key ->
    // bone index or -1, manifest.conversion.boneMap). Empty for PMX, PMD and VRM.
    std::map<std::string,int> conversionBoneMap;
    // Vertices grouped by skinning type and bone set, padded to SIMD blocks; built once per model.
    struct SkinLayout {
        struct Group {int influences=0;std::array<int,4> bones{-1,-1,-1,-1};unsigned first=0,count=0;};
        std::vector<int> order,sorted;
        std::vector<Group> groups;
        std::vector<unsigned> scalar;
        std::vector<std::pair<unsigned,unsigned>> chunks;
        std::vector<float> px,py,pz,nx,ny,nz,tx,ty,tz,w0,w1,w2,w3;
        unsigned identityBone=0;
    };
    mutable std::mutex skinMutex;
    mutable std::shared_ptr<const SkinLayout> skin;
    mutable std::mutex gpuMutex;
    mutable std::shared_ptr<const GpuSkin> gpu;
    std::shared_ptr<const SkinLayout> skinLayout() const;
    std::shared_ptr<const GpuSkin> gpuSkin() const;
    void invalidateSkinLayout();
    ~Model();
    std::string text(const nanoem_unicode_string_t*) const;
    Json info() const;
};
std::shared_ptr<Model> parse(std::span<const unsigned char>);
btVector3 vec(const float*);
btQuaternion quat(const float*);
int boneIndex(const nanoem_model_bone_t*);
int bodyIndex(const nanoem_model_rigid_body_t*);
int vertexIndex(const nanoem_model_vertex_t*);
btVector3 skinPosition(const Vertex&,const std::vector<btTransform>&);
btVector3 skinNormal(const Vertex&,const std::vector<btTransform>&);
struct SkinnedFrame { btVector3 position,normal,tangent; };
SkinnedFrame skinFrame(const Vertex&,const std::vector<btTransform>&,const btVector3& tangent);
// C converts MMD coordinates to Source Z-up; rotations are C R C^-1.
btVector3 toSource(const btVector3&);
btVector3 fromSource(const btVector3&);
btMatrix3x3 basis();
btTransform convert(const btTransform&,float scale);
std::wstring wide(std::string_view);
std::string utf8(std::wstring_view);
Bytes readFile(const fs::path&);
std::string readableName(std::string_view,size_t maxBytes=40);
std::string materialPath(const std::string& asset,size_t slot,std::string_view name);
void retainCacheFiles(const fs::path& cache,const std::vector<fs::path>& paths);
void registerShortName(const fs::path& cache,const std::string& kind,const std::string& id);
Json deleteAssets(const fs::path& cache,const std::vector<std::string>& ids);
// Removes import job folders untouched for `age` (left by a game that closed mid-import).
size_t sweepJobFolders(const fs::path& cache,std::chrono::hours age);
// CPU skinning's SIMD path needs AVX2, FMA3 and OS-saved YMM registers (XCR0 bits 1-2).
bool simdDeformSupported(bool avx2,bool fma,bool osxsave,uint64_t xcr0);
void writeAtomic(const fs::path&,std::span<const unsigned char>);
// Streams the content through `write` into a temporary file, then replaces the target.
void writeAtomic(const fs::path&,const std::function<void(std::ostream&)>& write);
void writeJson(const fs::path&,const Json&);
Json readJson(const fs::path&);
std::string hash(std::span<const unsigned char>);
bool validId(std::string_view);
struct CharacterConversion;
// character: a model the worker converted from another format (assets.hpp).
Json importAsset(const fs::path& source,const fs::path& cache,const Json& options,const fs::path& progress={},CharacterConversion* character=nullptr);
std::shared_ptr<Model> loadAsset(const fs::path& cache,const std::string& id);

// Bone hierarchy, bone morphs and IK for one pose. The render thread evaluates
// into the Instance arrays; an asynchronous secondary world evaluates its tick
// pose into private arrays, so neither thread touches the other's state.
struct PoseArrays {std::vector<btTransform> local,global,skin,effective;};
struct PoseHooks {
    std::function<void(size_t bone,btTransform& global,btTransform& effective,const btVector3& rest,const btTransform* parentGlobal)> physics;
    std::function<bool(size_t bone)> driven;
};
void evaluatePose(const Model&,const std::vector<btTransform>& manual,const std::vector<float>& weights,const std::vector<int>* sourceControl,const std::vector<btTransform>* sourcePose,const PoseHooks* hooks,std::vector<btTransform>& local,std::vector<btTransform>& global,std::vector<btTransform>& skin,std::vector<btTransform>& effective);
// Exactly the verified Source model vertex (stride 64): position, normal,
// colour, one UV set and a four-float tangent in user data. The trailing
// bytes are padding for the engine and carry the edge/extra UV values the
// legacy shader path reads.
struct DrawVertex { float x=0,y=0,z=0,nx=0,ny=0,nz=0; uint32_t color=0xffffffffu; float u=0,v=0,tx=1,ty=0,tz=0,tw=1,edge=0,extra0=0,extra1=0; };
static_assert(sizeof(DrawVertex)==64&&offsetof(DrawVertex,nx)==12&&offsetof(DrawVertex,color)==24&&offsetof(DrawVertex,u)==28&&offsetof(DrawVertex,tx)==36);
struct Snapshot {
    uint64_t sequence=0,staticsVersion=0;
    double time=0;
    bool materialsPristine=false;
    std::vector<DrawVertex> vertices;
    std::vector<btTransform> bones;
    std::vector<Material> materials;
    btVector3 minimum{0,0,0},maximum{0,0,0};
    // Change tracking for partial uploads: the publish sequence at which each
    // palette bone last moved, and at which every vertex last changed.
    std::vector<uint64_t> boneChanged;uint64_t allChanged=0;
    // The publish sequence at which vertex data last changed. Snapshot buffers are
    // reused, so equal addresses do not mean equal geometry; equal revisions do.
    uint64_t geometry=0;
    // Hardware skinning: only GpuSkin::cpuVertices are deformed in `vertices`;
    // the renderer draws the rest from the instance's rest data and this
    // palette (12 floats per entry, rest MMD space to Source world).
    bool gpu=false;uint64_t restVersion=0;std::vector<float> palette;
};
struct Body {
    std::unique_ptr<btCollisionShape> shape;
    std::unique_ptr<btRigidBody> rigid;
    btTransform offset=btTransform::getIdentity();
    btTransform initial=btTransform::getIdentity();
    int bone=-1,mode=0,group=0,mask=0xffff;
    bool core=false,generated=false;
    float mass=0;
};
struct Soft {
    std::unique_ptr<btSoftBody> body;
    std::vector<unsigned> vertexIndices,pins;
};
class World;
struct Rig;
class Secondary;
// Hardware-skinning rest data (morphed rest positions, 3 floats per vertex, and
// the rest version at which each vertex last changed). Shared with native draws
// that run on Source's render thread in queued mode: writers and the renderer's
// buffer uploads hold the mutex.
struct GpuRest {std::mutex mutex;std::vector<float> positions;std::vector<uint64_t> changed;};
struct Instance {
    uint64_t id=0;
    World* owner=nullptr;
    std::shared_ptr<Model> model;
    float scale=.08f;
    btTransform placement=btTransform::getIdentity();
    std::vector<btTransform> local,global,skin,manual;
    // Shared copies of the manual pose for asynchronous ticks; refreshed only when it changes.
    uint64_t manualVersion=1,manualSharedVersion=0;
    std::shared_ptr<const std::vector<btTransform>> manualShared;
    std::shared_ptr<const std::vector<btTransform>> shareManual();
    std::vector<btTransform> effectiveScratch;
    std::vector<btQuaternion> rotationPalette,dualPalette;
    // Sparse morph state in the skin layout order plus per-vertex UV overrides.
    std::vector<float> morphX,morphY,morphZ,uvU,uvV,uvE0,uvE1,palette;
    std::vector<unsigned> morphTouched,uvTouched;
    std::shared_ptr<const Model::SkinLayout> morphLayout;
    std::vector<float> previousPalette;std::vector<uint64_t> boneChangedState;uint64_t allChangedState=0,geometryState=0;
    unsigned changedBones=0;bool fullChange=false;uint64_t idleFrames=0;unsigned fullChangeReasons=0;// 1 morph, 2 statics, 4 soft, 8 layout, 16 new buffer
    // Engine material names per part for the native colour and depth passes (set from Lua once per asset).
    std::vector<std::string> colorNames,depthNames;
    std::unordered_map<int,btVector3> scalarMorph;
    uint64_t staticsVersion=1;
    mutable std::vector<float> cachedMorphInputs,cachedMorphWeights;
    double evaluateMs=0,deformMs=0;
    std::shared_ptr<Rig> sourceRig;
    std::unique_ptr<Secondary> secondary;
    std::string secondaryBroadphase;
    std::string secondaryBackend="reference";
    std::vector<int> sourceControl;
    std::vector<btTransform> sourcePose;
    std::vector<btTransform> presentationBones;
    bool presentationDriven=false;
    uint64_t presentationFrame=0;
    bool presentationDirty=false;
    // Skeletons that only change at the server tick rate (bone poses networked
    // to the client) are interpolated one update interval behind: the last
    // distinct poses with their timestamps, the observed update and frame
    // intervals, and the delay in use (moved gradually, never a jump).
    struct PresentationSample{double time;std::vector<btTransform> bones;};
    std::deque<PresentationSample> presentationSamples;
    double presentationUpdateInterval=0,presentationFrameInterval=0,presentationDelay=0;
    std::vector<btTransform> presentationSmoothed;
    btVector3 presentationLastRoot{0,0,0};bool presentationHaveRoot=false;
    std::span<const btTransform> smoothPresentation(std::span<const btTransform> live,double timestamp,double frameDt);
    double sourceTimestamp=-1;
    double pendingSourceDelta=0;
    bool sourceTeleport=false;
    std::string sourceError;
    void stepSource();
    void submitSourcePose(std::span<const btTransform> physical,std::span<const btTransform> manipulation,double timestamp,bool defer=false);
    void submitPresentationPose(std::span<const btTransform> bones,double timestamp,uint64_t frame);
    std::vector<bool> materialVisible;
    uint64_t sceneOwner=0;
    std::vector<bool> materialForceOpaque;
    // renderView marks the first-person colour view; its triangle mask is built
    // once and shared with queued draws.
    unsigned renderView=0;std::shared_ptr<const std::vector<uint8_t>> firstPersonMask;
    void setMaterialState(std::vector<bool> visible,std::vector<bool> forceOpaque);
    std::vector<float> morphWeights,lastImpulseWeights;
    const std::vector<float>& expandedMorphs() const;
    std::vector<int> drivers;
    std::vector<std::unique_ptr<Body>> bodies;
    std::vector<std::unique_ptr<btTypedConstraint>> joints;
    struct AnatomicalJoint { int bone,parent; btGeneric6DofConstraint* constraint; btVector3 lower,upper; };
    std::vector<AnatomicalJoint> anatomicalJoints;
    std::vector<Soft> softBodies;
    std::shared_ptr<const Snapshot> snapshot;
    std::shared_ptr<Snapshot> backSnapshot;
    std::vector<std::string> warnings;
    bool frozen=false,poseDirty=false;
    // Hardware skinning state: the rest data (see GpuRest) and its version.
    bool gpuBlocked=false,cpuRequest=false;std::string gpuBlockReason;
    // Render frame (renderer's counter) of the last draw in any pass, shadows included.
    uint64_t lastDrawFrame=0;
    std::shared_ptr<GpuRest> gpuRest=std::make_shared<GpuRest>();uint64_t gpuRestVersion=0;
    std::vector<float> gpuMorphWeights;std::vector<unsigned> gpuTouched;
    // Deform every vertex in the next publish (Lua position queries), then return to hardware skinning.
    void requireCpuVertices(){if(snapshot&&snapshot->gpu){cpuRequest=true;poseDirty=true;}ensureSnapshot();}
    Instance(World&,std::shared_ptr<Model>,uint64_t,const Json&);
    ~Instance();
    void evaluate(bool physics);
    void publish(double time);
    void ensureSnapshot();
    void beforeStep();
    void buildPhysics(const Json&);
    void reset();
    void applyPose();
    void updatePose();
    void setBonePose(size_t,const btTransform&);
    void freeze(bool);
    Json diagnostics(bool detailed=true) const;
};
struct Mirror {
    uint64_t id=0;
    std::unique_ptr<btTriangleMesh> triangles;
    std::vector<std::unique_ptr<btCollisionShape>> children;
    std::unique_ptr<btCollisionShape> shape;
    std::unique_ptr<btRigidBody> body;
    btVector3 impulse{0,0,0},torque{0,0,0};
    btVector3 beforeLinear{0,0,0},beforeAngular{0,0,0};
    btMatrix3x3 beforeInertia=btMatrix3x3::getIdentity();
    uint64_t touched=0;
};
struct SceneFrame;
class World {
public:
    std::atomic<std::shared_ptr<const SceneFrame>> externalScene;
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::unordered_map<uint64_t,std::unique_ptr<Instance>> instances;
    std::unordered_map<uint64_t,std::unique_ptr<Mirror>> mirrors;
    uint64_t next=1,tick=0;
    double accumulator=0,time=0,dropped=0,lastStepMs=0;
    // Set by the client renderer: carrier instances publish rest data plus a
    // bone palette and draw through Source hardware skinning.
    bool gpuSkinning=false;
    bool poseSmoothing=true; // interpolate stepped (tick-rate) skeletons one update behind
    World();
    ~World();
    btSoftRigidDynamicsWorld& dynamics();
    uint64_t create(std::shared_ptr<Model>,const Json&);
    Instance& get(uint64_t);
    void remove(uint64_t);
    void step(double seconds,const btVector3& gravity,bool publishSnapshots=true);
    void clear();
    void setMirror(uint64_t,const Json&,std::span<const float> geometry={});
    void removeMirror(uint64_t);
    Json takeImpulses();
    Json raycast(const btVector3&,const btVector3&,uint64_t instance=0,bool coreOnly=false);
    void beginGrab(uint64_t,int,const btVector3&);
    void updateGrab(const btVector3&);
    void endGrab();
    void beginPhysgun(uint64_t,int,const btTransform&);
    void updatePhysgun(const btTransform&);
    void drivePhysgun();
    void captureBefore();
    void captureAfter();
};
World& world();
// Lua realms have independent simulation ownership even in a listen server.
class WorldScope {
 World* previous;
public:
 explicit WorldScope(World*);
 ~WorldScope();
};
void shutdownWorld();
void acquireRuntimeRealm();void releaseRuntimeRealm();
} // namespace mmd
