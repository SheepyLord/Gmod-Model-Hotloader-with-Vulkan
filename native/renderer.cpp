#include <windows.h>
#include <materialsystem/imaterialsystem.h>
#include <materialsystem/imaterial.h>
#include <materialsystem/imaterialvar.h>
#include <materialsystem/itexture.h>
#include <materialsystem/imesh.h>
#include <icliententitylist.h>
#include <icliententity.h>
#include <vphysics_interface.h>
#include <engine/ivmodelinfo.h>
#include <studio.h>
#include "runtime.hpp"
#include "renderer.hpp"
#include "validation_once.hpp"
#include "compatibility.hpp"
#include "rig_geometry.hpp"
#include "mesh_topology.hpp"
#include "vertex_upload.hpp"
#include "light_overlaps.hpp"
#include "gpu_topology.hpp"
#include "jobs.hpp"
#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <map>
#include <tuple>
#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_map>
// SDK mesh utility implementations are not shipped as an x64 static library.
void GenerateSequentialIndexBuffer(unsigned short* p,int n,int first){for(int i=0;i<n;i++)p[i]=static_cast<unsigned short>(first+i);}
void GenerateQuadIndexBuffer(unsigned short* p,int n,int first){const int order[]={0,1,2,0,2,3};for(int i=0;i<n;i++)p[i]=static_cast<unsigned short>(first+(i/6)*4+order[i%6]);}
void GeneratePolygonIndexBuffer(unsigned short* p,int n,int first){for(int i=0;i<n;i++)p[i]=static_cast<unsigned short>(first+(i%3==0?0:i/3+i%3));}
void GenerateLineStripIndexBuffer(unsigned short* p,int n,int first){for(int i=0;i<n;i++)p[i]=static_cast<unsigned short>(first+i/2+i%2);}
void GenerateLineLoopIndexBuffer(unsigned short* p,int n,int first){GenerateLineStripIndexBuffer(p,n,first);if(n)p[n-1]=static_cast<unsigned short>(first);}
namespace mmd {
std::string modelAnimationDiagnostics(const std::string& path){
    requireGameBinary(L"engine.dll");
    auto engine=GetModuleHandleW(L"engine.dll");
    auto factory=engine?reinterpret_cast<void*(*)(const char*,int*)>(GetProcAddress(engine,"CreateInterface")):nullptr;
    auto info=factory?static_cast<IVModelInfo*>(factory(VMODELINFO_CLIENT_INTERFACE_VERSION,nullptr)):nullptr;
    if(!info)throw std::runtime_error("Source model information interface unavailable");
    auto model=info->GetModel(info->GetModelIndex(path.c_str()));
    if(!model)throw std::runtime_error("Model must be loaded before inspecting animations");
    auto header=info->GetStudiomodel(model);if(!header)throw std::runtime_error("Model has no studio header");
    unsigned short* list=nullptr;int count=info->GetAutoplayList(header,&list);Json ids=Json::array();
    if(count<0||count>4096)throw std::runtime_error("Invalid Source autoplay count");
    for(int i=0;i<count;i++)ids.push_back(list[i]);
    Json sequences=Json::array();
    for(int i=0;i<header->numlocalseq;i++){auto seq=header->pLocalSeqdesc(i);sequences.push_back({{"label",seq->pszLabel()},{"flags",seq->flags},{"rootWeight",seq->weight(0)}});}
    return Json({{"autoplay",ids},{"sequences",sequences}}).dump();
}
static IMaterialSystem* materials=nullptr;
static bool remixFixedFunction=false;
static std::string status="not initialized";
struct CachedMesh {std::weak_ptr<const Snapshot> snapshot;uint64_t geometry=0;std::vector<IMesh*> meshes;size_t bytes=0;};
static std::map<std::tuple<const Snapshot*,unsigned,bool,VertexFormat_t>,CachedMesh> cache;
// Draw ranges derived from a mask are kept while its content is unchanged:
// recomputing them walked every triangle of every draw in every pass.
struct MaskedRanges {std::vector<CPrimList> ranges;unsigned dropped=0;};
struct LightMask {std::weak_ptr<const Snapshot> snapshot;uint64_t geometry=0;std::vector<uint8_t> triangles;std::map<std::tuple<const unsigned*,unsigned,unsigned,unsigned>,MaskedRanges> ranges;};
static std::map<std::pair<uint64_t,unsigned>,LightMask> lightMasks;
// Draw execution and the buffer caches belong to the thread that holds the
// hardware render context (the caller in mat_queue_mode 0, Source's render
// thread in queued mode); main-thread statistics readers take this lock too.
static std::recursive_mutex renderMutex;
// Counters are atomic: under Source's queued (multicore) material system the
// draws that update them run on its render thread while the main thread reads
// and resets them, so a frame's figures cover the render-thread work finished
// between two PrepareFrame calls. nativePrepareMs is the main thread's share.
static std::atomic<uint64_t> flashlightDuplicates{0};
static std::atomic<double> overlapMs{0};
static std::atomic<bool> lightOverlapGuard{true};
void setLightOverlapGuard(bool enabled){lightOverlapGuard=enabled;}
static std::atomic<uint64_t> cacheBuilds{0},cacheDraws{0},streamDraws{0},uploadedBytes{0};
static std::atomic<uint64_t> shadowDraws{0};
static std::atomic<double> nativeMs{0},uploadMs{0},drawMs{0},shadowMs{0},lockMs{0},fillMs{0},unlockMs{0},prepareMs{0};
static std::atomic<unsigned> frameBinds{0},frameDrawCalls{0},frameSpans{0},frameDirtySpans{0},frameQueued{0};
static std::map<VertexFormat_t,VertexFormat_t> observedFormats;
static Json observedLayouts=Json::object();
struct Measure {std::atomic<double>& result;bool enabled=true;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();~Measure(){if(enabled)result.fetch_add(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),std::memory_order_relaxed);}};
static std::atomic<unsigned> frameSkinBatches{0},frameBoneLoads{0};
static uint64_t renderFrame=1;
uint64_t renderFrameNumber(){return renderFrame;}
// Draw counters of one render frame, taken from the live atomics by the thread
// that draws (see publishFrame).
static Json takeDrawCounters(){
    return Json({{"flashlightDuplicates",flashlightDuplicates.exchange(0)},{"overlapCheckMs",overlapMs.exchange(0)},{"nativeRenderMs",nativeMs.exchange(0)},{"uploadMs",uploadMs.exchange(0)},{"meshDrawMs",drawMs.exchange(0)},{"shadowMs",shadowMs.exchange(0)},{"vertexLockMs",lockMs.exchange(0)},{"vertexFillMs",fillMs.exchange(0)},{"vertexUnlockMs",unlockMs.exchange(0)},
        {"drawBinds",frameBinds.exchange(0)},{"drawCalls",frameDrawCalls.exchange(0)},{"spansTotal",frameSpans.exchange(0)},{"spansDirty",frameDirtySpans.exchange(0)},{"skinBatches",frameSkinBatches.exchange(0)},{"boneLoads",frameBoneLoads.exchange(0)}});
}
static std::mutex publishedMutex;static Json publishedCounters=Json::object();static bool countersOnRenderThread=false;
static bool submit(std::function<void()> work);static bool recordsForRenderThread(IMatRenderContext* context);
// At the start of each frame (PrepareFrame) the previous frame's draw counters
// are published. In queued mode this runs as a call on the render thread, in
// order after the previous frame's draws, so the published figures are the
// last frame the render thread finished; otherwise they are taken right away.
void beginRenderFrame(){
    ++renderFrame;prepareMs=0;frameQueued=0;
    auto publish=[]{auto counters=takeDrawCounters();std::lock_guard lock(publishedMutex);publishedCounters=std::move(counters);};
    if(materials)countersOnRenderThread=submit(publish);else publish();
}
// Mode 0 reports this frame's draws so far; queued mode the last frame the
// render thread finished (its draws run after this frame's main-thread work).
std::string renderFrameStats(){
    Json out;
    if(countersOnRenderThread){std::lock_guard lock(publishedMutex);out=publishedCounters;}
    else out=Json({{"flashlightDuplicates",flashlightDuplicates.load()},{"overlapCheckMs",overlapMs.load()},{"nativeRenderMs",nativeMs.load()},{"uploadMs",uploadMs.load()},{"meshDrawMs",drawMs.load()},{"shadowMs",shadowMs.load()},{"vertexLockMs",lockMs.load()},{"vertexFillMs",fillMs.load()},{"vertexUnlockMs",unlockMs.load()},
        {"drawBinds",frameBinds.load()},{"drawCalls",frameDrawCalls.load()},{"spansTotal",frameSpans.load()},{"spansDirty",frameDirtySpans.load()},{"skinBatches",frameSkinBatches.load()},{"boneLoads",frameBoneLoads.load()}});
    out["nativePrepareMs"]=prepareMs.load();out["queuedDraws"]=frameQueued.load();out["renderThreadDraws"]=countersOnRenderThread?1:0;
    return out.dump();
}
static std::string shadowError;
struct Topology: BatchedTopology {std::weak_ptr<Model> model;std::vector<std::vector<std::vector<unsigned>>> spanBones;};
static std::map<std::pair<const Model*,unsigned>,Topology> topology;
// Three buffers keep the CPU from modifying the geometry submitted in the
// preceding frames. Index data is immutable; only dirty vertex data is uploaded.
struct VertexSlot {uint64_t sequence=0;std::vector<IMesh*> meshes;size_t bytes=0;std::vector<std::vector<uint64_t>> spanUploaded;};
// layout: 0 builder writes, 1 common 64-byte vertex streamed, 2 compressed 32-byte vertex streamed.
struct LiveMesh {std::array<VertexSlot,3> slots;uint64_t lastSequence=0;int layout=0;};
static std::map<std::tuple<uint64_t,unsigned,bool,VertexFormat_t>,LiveMesh> liveMeshes;
static std::atomic<bool> nativeVertexCache{false};
// Shared buffers use Source's compressed vertex (normal+tangent packed into 4 bytes,
// 32 instead of 64 bytes); the first buffer whose engine layout differs disables it.
static std::atomic<bool> compactVertices{true};static std::string compactFailure;
static std::atomic<uint64_t> liveUpdates{0},liveDraws{0};
void setNativeVertexCache(bool enabled){nativeVertexCache=enabled;}
void setCompactVertices(bool enabled){compactVertices=enabled;}
// Hardware skinning. Static buffers hold rest vertices with batch-local bone
// slots; they change only when morph weights change (partial rewrite, one of
// three buffers per rest version). Each batch loads its bone matrices, then draws.
struct SkinGeometry {std::weak_ptr<Model> model;std::shared_ptr<const GpuSkin> plan;SkinTopology topology;std::vector<unsigned> cpuParts;};
static std::map<std::pair<const Model*,unsigned>,SkinGeometry> skinGeometry;
struct SkinSlot {uint64_t restVersion=0;std::vector<IMesh*> meshes;size_t bytes=0;};
struct SkinMesh {std::array<SkinSlot,3> slots;uint64_t lastSequence=0;};
static std::map<std::pair<uint64_t,unsigned>,SkinMesh> skinMeshes;
static std::atomic<bool> gpuSkinning{true};
static std::atomic<uint64_t> skinBuilds{0},skinUpdates{0},skinDraws{0};
static std::map<std::string,bool> skinShaders;
void setGpuSkinning(bool enabled){
    gpuSkinning=enabled;world().gpuSkinning=enabled;
    for(auto& [id,instance]:world().instances){instance->gpuBlocked=false;instance->gpuBlockReason.clear();instance->poseDirty=true;}
}
// Shaders known to take Source's SKINNING combo (the ones studio models draw with).
static bool skinCapable(IMaterial* material){
    static std::unordered_map<IMaterial*,bool> known;auto it=known.find(material);if(it!=known.end())return it->second;
    std::string shader=material->GetShaderName();bool capable=false;
    for(const char* name:{"VertexLitGeneric","UnlitGeneric","DepthWrite","ShadowBuild"})capable|=shader.rfind(name,0)==0;
    skinShaders[shader]=capable;known.emplace(material,capable);return capable;
}
static bool renderSuspended=false;
void setRenderSuspended(bool suspended){renderSuspended=suspended;}
// Engine materials resolved once per name. The held reference keeps each
// material alive for the session, so the pointer stays valid; Lua-created
// materials already live that long.
static std::unordered_map<std::string,IMaterial*> materialCache;
static void initialize();
static IMaterial* findMaterial(const std::string& name){
    auto it=materialCache.find(name);if(it!=materialCache.end())return it->second;
    // DrawInstance classifies override materials before its draw path runs, so a
    // module's first draw may arrive here before anything else resolved the
    // material system.
    initialize();
    auto material=materials->FindMaterial(name.c_str(),"Model textures",false);if(!material||material->IsErrorMaterial())throw std::runtime_error("Character model render material missing: "+name);
    material->IncrementReferenceCount();materialCache.emplace(name,material);return material;
}
// The Lua renderer's translucency test for override materials: Source keeps
// $translucent/$alphatest/$additive in the parsed flags, plus $alpha below one.
bool materialIsTranslucent(const std::string& name){
    auto material=findMaterial(name);
    if(material->GetMaterialVarFlag(MATERIAL_VAR_TRANSLUCENT)||material->GetMaterialVarFlag(MATERIAL_VAR_ALPHATEST)||material->GetMaterialVarFlag(MATERIAL_VAR_ADDITIVE))return true;
    bool found=false;auto alpha=material->FindVar("$alpha",&found,false);return found&&alpha->GetFloatValue()<1;
}
static bool submit(std::function<void()> work);
// Staleness is decided on the main thread from the live instances; the buffers
// are released on the thread that draws with them (queued after this frame's
// draws in queued mode).
void pruneRenderCache(bool all){
    if(!materials)return;
    std::unordered_map<uint64_t,uint64_t> sequences;for(auto& [id,owner]:world().instances)sequences[id]=owner->snapshot?owner->snapshot->sequence:0;
    const bool vertexCache=nativeVertexCache,skinning=gpuSkinning;
    submit([all,vertexCache,skinning,sequences=std::move(sequences)]{
        CMatRenderContextPtr context(materials);
        auto stale=[&](uint64_t id,uint64_t lastSequence){auto owner=sequences.find(id);return owner==sequences.end()||owner->second>lastSequence+4;};
        for(auto it=lightMasks.begin();it!=lightMasks.end();)if(all||it->second.snapshot.expired())it=lightMasks.erase(it);else ++it;
        for(auto it=cache.begin();it!=cache.end();)if(all||it->second.snapshot.expired()){
            for(auto mesh:it->second.meshes)context->DestroyStaticMesh(mesh);it=cache.erase(it);
        }else ++it;
        for(auto it=topology.begin();it!=topology.end();)if(all||it->second.model.expired())it=topology.erase(it);else ++it;
        for(auto it=liveMeshes.begin();it!=liveMeshes.end();){
            if(all||!vertexCache||stale(std::get<0>(it->first),it->second.lastSequence)){for(auto& slot:it->second.slots)for(auto mesh:slot.meshes)context->DestroyStaticMesh(mesh);it=liveMeshes.erase(it);}else ++it;
        }
        for(auto it=skinGeometry.begin();it!=skinGeometry.end();)if(all||it->second.model.expired())it=skinGeometry.erase(it);else ++it;
        for(auto it=skinMeshes.begin();it!=skinMeshes.end();){
            if(all||!skinning||stale(it->first.first,it->second.lastSequence)){for(auto& slot:it->second.slots)for(auto mesh:slot.meshes)context->DestroyStaticMesh(mesh);it=skinMeshes.erase(it);}else ++it;
        }
    });
}
static Json skinningStats(){
    size_t bytes=0,buffers=0;for(auto& [key,item]:skinMeshes)for(auto& slot:item.slots){bytes+=slot.bytes;buffers+=slot.meshes.size();}
    Json models=Json::array(),blocked=Json::array(),shaders=Json::object();unsigned skinned=0;
    for(auto& [key,g]:skinGeometry){auto m=g.model.lock();if(!m)continue;const auto& t=g.topology;
        models.push_back({{"model",m->name},{"view",key.second},{"batches",t.batches},{"materials",m->materials.size()},{"gpuTriangles",t.gpuTriangles},{"cpuTriangles",t.cpuTriangles},{"vertices",m->vertices.size()},{"bufferVertices",t.vertices},
            {"cpuVertices",g.plan->cpuVertices.size()},{"fourWeight",g.plan->fourWeight},{"droppedWeightToCpu",g.plan->droppedWeight}});}
    for(auto& [id,p]:world().instances){if(p->snapshot&&p->snapshot->gpu)skinned++;if(p->gpuBlocked)blocked.push_back({{"instance",id},{"reason",p->gpuBlockReason}});}
    for(auto& [name,capable]:skinShaders)shaders[name]=capable;
    return {{"enabled",gpuSkinning.load()},{"skinnedInstances",skinned},{"blocked",blocked},{"shaders",shaders},{"models",models},{"bytes",bytes},{"buffers",buffers},{"builds",skinBuilds.load()},{"updates",skinUpdates.load()},{"draws",skinDraws.load()}};
}
static Json queueStats();
std::string rendererStats(){
    std::lock_guard lock(renderMutex);size_t bytes=0,buffers=0,indices=0;for(auto& [key,item]:cache){bytes+=item.bytes;buffers+=item.meshes.size();}for(auto& [key,item]:liveMeshes)for(auto& slot:item.slots){bytes+=slot.bytes;buffers+=slot.meshes.size();}for(auto& [key,item]:topology)for(auto& chunk:item.chunks)indices+=chunk.indices.size()*2;
    return Json({{"gpuSkinning",skinningStats()},{"fixedFunctionRemix",remixFixedFunction},{"overlapLayerMode",remixFixedFunction?"geometry":"projection"},{"cachedBytes",bytes},{"cachedBuffers",buffers},{"builds",cacheBuilds.load()},{"draws",cacheDraws.load()},{"streamDrawsTotal",streamDraws.load()},{"uploadedBytesTotal",uploadedBytes.load()},{"topologyIndexBytes",indices},{"sourceShadowDraws",shadowDraws.load()},{"sourceShadowError",shadowError},{"nativeVertexCache",nativeVertexCache.load()},{"compactVertices",compactVertices&&compactFailure.empty()},{"compactFailure",compactFailure},{"vertexFormats",observedFormats},{"vertexLayouts",observedLayouts},{"liveUpdates",liveUpdates.load()},{"liveDraws",liveDraws.load()},{"renderQueue",queueStats()}}).dump();
}
static void initialize(){
    static ValidationOnce validation;validation.check([]{
    auto module=GetModuleHandleW(L"materialsystem.dll");if(!module)throw std::runtime_error("Material system not loaded");
    // Remix replaces the programmable shader family with its fixed-function
    // stdshader_dx6.dll. It intentionally does not load stdshader_dx9.dll.
    const auto shader=GetModuleHandleW(L"stdshader_dx9.dll")?L"stdshader_dx9.dll":L"stdshader_dx6.dll";
    remixFixedFunction=std::wstring_view(shader)==L"stdshader_dx6.dll";
    for(auto library:{L"materialsystem.dll",L"shaderapidx9.dll",shader}){
        requireGameBinary(library);
    }
    auto factory=reinterpret_cast<void*(*)(const char*,int*)>(GetProcAddress(module,"CreateInterface"));if(!factory)throw std::runtime_error("Material factory unavailable");
    materials=static_cast<IMaterialSystem*>(factory("VMaterialSystem080",nullptr));if(!materials)throw std::runtime_error("VMaterialSystem080 unavailable");status="VMaterialSystem080/native dynamic mesh";
    });
}
std::string rendererStatus(){std::lock_guard lock(renderMutex);return status;}
static std::array<LightDesc_t,4> lastSourceLights{};
static int lastSourceLightCount=0;
static Vector lastSourceLightOrigin(0,0,0);
std::string renderLightingState(){
    initialize();CMatRenderContextPtr context(materials);Vector eye;
    context->GetWorldSpaceCameraPosition(&eye);Json lights=Json::array();
    for(int i=0;i<lastSourceLightCount;i++){
        const auto& l=lastSourceLights[i];
        lights.push_back({{"type",int(l.m_Type)},{"color",{l.m_Color.x,l.m_Color.y,l.m_Color.z}},
            {"position",{l.m_Position.x,l.m_Position.y,l.m_Position.z}},
            {"direction",{l.m_Direction.x,l.m_Direction.y,l.m_Direction.z}},
            {"attenuation",{l.m_Attenuation0,l.m_Attenuation1,l.m_Attenuation2}}});
    }
    // Report our last complete lighting query, not shader internals that may
    // belong to another entity or have no bound material outside a draw call.
    return Json({{"flashlight",context->GetFlashlightMode()},{"camera",{eye.x,eye.y,eye.z}},
        {"sourceLightCount",lastSourceLightCount},{"sourceLightOrigin",{lastSourceLightOrigin.x,lastSourceLightOrigin.y,lastSourceLightOrigin.z}},{"lights",lights}}).dump();
}

static void* sourceModelRender(){
    // IVModelRender::SetupLighting sets Source's light cache, ambient cube,
    // local light descriptors and local cubemap for this model origin.
    static void* modelRender=nullptr;static ValidationOnce validation;
    validation.check([]{auto dll=GetModuleHandleW(L"engine.dll");wchar_t path[32768];
        if(!dll||!GetModuleFileNameW(dll,path,32768))throw std::runtime_error("Source engine library unavailable");
        requireGameBinary(L"engine.dll");
        auto factory=reinterpret_cast<void*(*)(const char*,int*)>(GetProcAddress(dll,"CreateInterface"));if(!factory||!(modelRender=factory("VEngineModel016",nullptr)))throw std::runtime_error("Source model renderer unavailable");});
    requireOwnedSlots(modelRender,L"engine.dll",{21});
    return modelRender;
}
static IClientEntityList* sourceEntityList(){
    static IClientEntityList* entities=nullptr;static ValidationOnce validation;
    validation.check([]{auto module=GetModuleHandleW(L"client.dll");wchar_t path[32768];
        if(!module||!GetModuleFileNameW(module,path,32768))throw std::runtime_error("Source client library unavailable");
        requireGameBinary(L"client.dll");
        auto factory=reinterpret_cast<void*(*)(const char*,int*)>(GetProcAddress(module,"CreateInterface"));if(!factory||!(entities=static_cast<IClientEntityList*>(factory("VClientEntityList003",nullptr))))throw std::runtime_error("Source entity list unavailable");});
    requireOwnedSlots(entities,L"client.dll",{3,4});
    return entities;
}
void checkRenderer(){initialize();sourceModelRender();sourceEntityList();}
void setupSourceLighting(float x,float y,float z){
    auto modelRender=sourceModelRender();Vector center(x,y,z);auto table=*reinterpret_cast<void***>(modelRender);reinterpret_cast<void(*)(void*,const Vector&)>(table[21])(modelRender,center);
    // SetupLighting is an old two-light convenience path, even on hardware
    // supporting four model lights. It discards the other lights instead of
    // folding them into ambient. gm_construct's ceiling spotlight is third.
    // Keep its ambient/cubemap setup, then supply the same complete light list
    // as studio models through IEngineTool::GetLightingConditions.
    static void* lightingTools=nullptr;static ValidationOnce validation;
    validation.check([]{
        auto dll=GetModuleHandleW(L"engine.dll");
        auto factory=reinterpret_cast<void*(*)(const char*,int*)>(GetProcAddress(dll,"CreateInterface"));
        lightingTools=factory?factory("VENGINETOOL003",nullptr):nullptr;
        if(!lightingTools)throw std::runtime_error("Source map-lighting interface unavailable");
        auto slots=*reinterpret_cast<void***>(lightingTools);
        // Source's pinned x64 interface has the inherited destructor at slot 0.
        requireOwnedSlots(lightingTools,L"engine.dll",{77});
        requireAbiRva(L"engine.dll","lighting",reinterpret_cast<uintptr_t>(slots[77])-reinterpret_cast<uintptr_t>(dll));
    });
    static_assert(sizeof(LightDesc_t)==88);
    Vector ambient[6];LightDesc_t lights[4];std::memset(lights,0,sizeof(lights));
    auto slots=*reinterpret_cast<void***>(lightingTools);
    int count=reinterpret_cast<int(*)(void*,const Vector&,Vector*,int,LightDesc_t*)>(slots[77])(lightingTools,center,ambient,4,lights);
    if(count<0||count>4)throw std::runtime_error("Invalid Source map-light count");
    initialize();CMatRenderContextPtr context(materials);
    for(int i=0;i<4;i++)context->SetLight(i,lights[i]);
    std::memcpy(lastSourceLights.data(),lights,sizeof(lights));lastSourceLightCount=count;lastSourceLightOrigin=center;
}
namespace {
struct SourceShadow {World* host;int entity;uint64_t instance;std::vector<std::string> materials;float alpha;std::vector<std::pair<std::string,std::vector<unsigned>>> groups;};
std::map<IClientRenderable*,SourceShadow> sourceShadows;
using ShadowSetup=matrix3x4_t*(*)(void*,IClientRenderable*,int,int,void*,matrix3x4_t*);
using ShadowDraw=void(*)(void*,IClientRenderable*,const void*,matrix3x4_t*);
ShadowSetup originalShadowSetup=nullptr;ShadowDraw originalShadowDraw=nullptr;void** shadowTable=nullptr;
matrix3x4_t* shadowSetup(void* self,IClientRenderable* renderable,int body,int skin,void* info,matrix3x4_t* custom){
    // The carrier intentionally has no studio mesh. Its shadow uses deformed
    // native vertices, so DrawModelShadowSetup's studio mesh requirement is skipped.
    if(sourceShadows.contains(renderable)){static matrix3x4_t identity(1,0,0,0,0,1,0,0,0,0,1,0);return &identity;}
    return originalShadowSetup(self,renderable,body,skin,info,custom);
}
void shadowDraw(void* self,IClientRenderable* renderable,const void* info,matrix3x4_t* custom);
void writeShadowSlot(size_t slot,void* value){DWORD previous;if(!VirtualProtect(shadowTable+slot,sizeof(void*),PAGE_READWRITE,&previous))throw std::runtime_error("Cannot install Source shadow callback");InterlockedExchangePointer(shadowTable+slot,value);DWORD ignored;VirtualProtect(shadowTable+slot,sizeof(void*),previous,&ignored);}
void installSourceShadows(){
    if(shadowTable)return;auto render=sourceModelRender();auto table=*reinterpret_cast<void***>(render);
    // Fail closed if another native module already replaced either callback.
    auto engine=GetModuleHandleW(L"engine.dll");for(int slot:{12,13}){MEMORY_BASIC_INFORMATION memory{};VirtualQuery(table[slot],&memory,sizeof(memory));if(memory.AllocationBase!=engine)throw std::runtime_error("Source shadow interface was replaced by another module");}
    originalShadowSetup=reinterpret_cast<ShadowSetup>(table[12]);originalShadowDraw=reinterpret_cast<ShadowDraw>(table[13]);shadowTable=table;
    try{writeShadowSlot(12,reinterpret_cast<void*>(shadowSetup));writeShadowSlot(13,reinterpret_cast<void*>(shadowDraw));}catch(...){shutdownSourceShadows();throw;}
}
}
void registerSourceShadow(int entity,uint64_t instance,const std::vector<std::string>& names,float alpha,void* corpsePhysics){
    if(!std::isfinite(alpha)||alpha<0||alpha>1)throw std::runtime_error("Invalid shadow alpha");
    auto& owner=world().get(instance);if(!owner.sourceRig||names.size()!=owner.model->materials.size())throw std::runtime_error("Source shadow mapping does not match model");
    auto entities=sourceEntityList();
    // Client-only corpses have EntIndex -1. Source CRagdoll::Init stores its
    // C_BaseEntity in every IPhysicsObject's game data. Use that supported
    // physics interface, not the undocumented extended Lua-interface vtable.
    auto client=corpsePhysics?static_cast<IClientEntity*>(static_cast<IPhysicsObject*>(corpsePhysics)->GetGameData()):entities->GetClientEntity(entity);
    if(!client)throw std::runtime_error("Source shadow entity not available");
    MEMORY_BASIC_INFORMATION region{};
    if(!VirtualQuery(*reinterpret_cast<void***>(client),&region,sizeof(region))||region.AllocationBase!=GetModuleHandleW(L"client.dll"))throw std::runtime_error("Source shadow physics owner is not a client entity");
    auto renderable=client->GetClientRenderable();
    initialize();
    for(auto& name:names){
        auto material=materials->FindMaterial(name.c_str(),"Model textures",false);bool found=false;auto original=material->FindVar("$translucent_material",&found,false);
        // CreateMaterial supplies strings, but ShadowBuild requires a typed
        // IMaterial reference. Leaving it as a string loses texture alpha and
        // emits one engine warning per draw, causing severe frame-time spikes.
        if(found&&original->GetType()!=MATERIAL_VAR_TYPE_MATERIAL){auto source=materials->FindMaterial(original->GetStringValue(),"Model textures",false);if(!source||source->IsErrorMaterial())throw std::runtime_error("Shadow source material unavailable");original->SetMaterialValue(source);}
    }
    installSourceShadows();SourceShadow binding{&world(),entity,instance,names,alpha,{}};
    for(size_t i=0;i<names.size();i++){auto group=std::find_if(binding.groups.begin(),binding.groups.end(),[&](const auto& g){return g.first==names[i];});if(group==binding.groups.end()){binding.groups.push_back({names[i],{}});group=binding.groups.end()-1;}group->second.push_back(unsigned(i));}
    sourceShadows[renderable]=std::move(binding);
}
void removeSourceShadow(int entity){for(auto i=sourceShadows.begin();i!=sourceShadows.end();)if(i->second.entity==entity)i=sourceShadows.erase(i);else ++i;if(sourceShadows.empty())shutdownSourceShadows();}
void shutdownSourceShadows(){
    sourceShadows.clear();if(!shadowTable)return;
    if(shadowTable[12]==reinterpret_cast<void*>(shadowSetup))writeShadowSlot(12,reinterpret_cast<void*>(originalShadowSetup));
    if(shadowTable[13]==reinterpret_cast<void*>(shadowDraw))writeShadowSlot(13,reinterpret_cast<void*>(originalShadowDraw));shadowTable=nullptr;
}
namespace {
// One native draw, captured on the calling (main) thread. In Source's queued
// (multicore) mode it runs later on the render thread while the main thread
// already prepares the next frame, so it reads only this: the immutable model
// and snapshot, the rest data (under its lock) and copied instance state.
// cutout: RTX Remix draws only the triangles the alpha test leaves (Model::cutoutTriangles).
struct DrawItem {std::vector<unsigned> parts;IMaterial* material=nullptr;std::string name;bool perPartColor=true,cutout=false;};
struct DrawJob {
    uint64_t instance=0;std::shared_ptr<Model> model;std::shared_ptr<const Snapshot> snapshot;
    std::shared_ptr<GpuRest> rest;std::shared_ptr<const std::vector<uint8_t>> firstPerson;
    unsigned renderView=0;bool sourceRig=false,frozen=false,edges=false,shadow=false;
    float rigScale=1,alphaScale=1;btVector3 center{0,0,0};RenderTint tint;std::vector<DrawItem> items;
};
// Failures and errors of draws that ran on the render thread, for the main thread.
std::mutex asyncMutex;std::string asyncError;std::vector<std::pair<uint64_t,std::string>> gpuFailures;
std::atomic<int64_t> outstandingCalls{0};std::atomic<uint64_t> queuedCallsTotal{0};
// ABI mirror of tier1's CFunctor: IRefCounted AddRef/Release, virtual
// destructor, operator() (vtable slot 3).
struct SourceFunctor {virtual int AddRef()=0;virtual int Release()=0;virtual ~SourceFunctor(){}virtual void operator()()=0;unsigned userId=0;};
// One native call for the render thread. The queue runs operator() once and
// then drops the element without destroying or releasing it (Source resets its
// per-frame arenas), so the call frees itself when it has run.
class RenderThreadCall final:public SourceFunctor {
    std::function<void()> work;
public:
    explicit RenderThreadCall(std::function<void()> w):work(std::move(w)){outstandingCalls++;}
    ~RenderThreadCall()override{outstandingCalls--;}
    int AddRef()override{return 1;}
    int Release()override{return 1;}
    // Called by Source's render thread; nothing may escape into it.
    void operator()()override{
        {std::lock_guard lock(renderMutex);
         try{work();}catch(const std::exception& e){std::lock_guard l(asyncMutex);asyncError=e.what();}catch(...){std::lock_guard l(asyncMutex);asyncError="Native render call failed";}}
        delete this;
    }
};
// What GMod's IMatRenderContext::GetCallQueue returns on the queued context
// (this+0x2B0): not tier1's virtual ICallQueue but the context's concrete call
// list, a singly linked list of {next, functor} elements followed by the bump
// allocator the elements come from (next pointer, 16 MB buffer). Source's own
// studiorender.dll appends to it inline exactly as appendCall does; the render
// thread runs each functor's operator() in order, then resets the list.
struct QueueElement {QueueElement* next;SourceFunctor* functor;};
struct SourceCallList {QueueElement* head;QueueElement* tail;uint8_t* next;uint8_t buffer[8];};
static_assert(offsetof(SourceCallList,next)==0x10&&offsetof(SourceCallList,buffer)==0x18);
constexpr size_t CallListCapacity=0x1000000;
constexpr ptrdiff_t CallListOffset=0x2B0;
void appendCall(SourceCallList* list,SourceFunctor* call){
    uint8_t* begin=list->buffer;uint8_t* end=begin+CallListCapacity;
    auto inside=[&](const void* p){auto b=static_cast<const uint8_t*>(p);return b>=begin&&b<end;};
    // Fail closed on anything but the layout the pinned builds have.
    if(!list->next||list->next<begin||list->next>end||(list->head==nullptr)!=(list->tail==nullptr)||(list->head&&(!inside(list->head)||!inside(list->tail))))
        throw std::runtime_error("Unexpected Source render queue layout");
    auto at=reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(list->next)+7)&~uintptr_t(7));
    if(at+sizeof(QueueElement)>end)throw std::runtime_error("Source render queue is full");
    auto element=reinterpret_cast<QueueElement*>(at);list->next=at+sizeof(QueueElement);
    element->next=nullptr;element->functor=call;
    if(list->tail)list->tail->next=element;else list->head=element;
    list->tail=element;
}
}
static constexpr VertexFormat_t commonFormat=VERTEX_POSITION|VERTEX_NORMAL|VERTEX_COLOR|VERTEX_FORMAT_VERTEX_SHADER|VERTEX_USERDATA_SIZE(4)|(2ULL<<TEX_COORD_SIZE_BIT);
// The calling thread gets the queued context (CMatQueuedRenderContext) on the
// main thread in multicore mode, the hardware context (CMatRenderContext)
// otherwise and on the render thread. They are told apart by their RTTI class,
// which holds across game builds; each class's vtable is remembered once seen.
static std::atomic<void*> queuedContextTable{nullptr},hardwareContextTable{nullptr};
static void* contextClass(IMatRenderContext* context){
    auto table=*reinterpret_cast<void**>(context);
    if(table==hardwareContextTable.load(std::memory_order_relaxed)||table==queuedContextTable.load(std::memory_order_relaxed))return table;
    auto name=rttiClass(context,L"materialsystem.dll");
    if(name==".?AVCMatRenderContext@@")hardwareContextTable=table;
    else if(name==".?AVCMatQueuedRenderContext@@")queuedContextTable=table;
    return table;
}
static bool recordsForRenderThread(IMatRenderContext* context){return contextClass(context)==queuedContextTable.load(std::memory_order_relaxed);}
static bool drawsDirectly(IMatRenderContext* context){return contextClass(context)==hardwareContextTable.load(std::memory_order_relaxed);}
// Source's queued material system hands the main thread a render context that
// records calls for its render thread. A native draw is recorded there as one
// call: it runs on the render thread, in order with the calls recorded before
// it (lighting, flashlight state, clip planes), where the thread's context is
// the hardware one, so buffers are created, filled and drawn directly as in
// mat_queue_mode 0. Otherwise the work runs now. Returns true when queued.
static bool submit(std::function<void()> work){
    initialize();CMatRenderContextPtr context(materials);
    if(drawsDirectly(context)){std::lock_guard lock(renderMutex);work();return false;}
    if(!recordsForRenderThread(context))throw std::runtime_error("Unrecognized Source render context");
    auto list=reinterpret_cast<SourceCallList*>(context->GetCallQueue());
    if(reinterpret_cast<char*>(list)-reinterpret_cast<char*>(static_cast<IMatRenderContext*>(context))!=CallListOffset)throw std::runtime_error("Unexpected Source render queue");
    auto call=new RenderThreadCall(std::move(work));
    try{appendCall(list,call);}catch(...){delete call;throw;}
    queuedCallsTotal++;frameQueued++;return true;
}
static Json queueStats(){
    Json out={{"outstandingCalls",outstandingCalls.load()},{"queuedCallsTotal",queuedCallsTotal.load()}};
    // The calling thread's context, as submit() classifies it.
    if(materials){CMatRenderContextPtr context(materials);const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"materialsystem.dll"));
        out["threadMode"]=int(materials->GetThreadMode());out["contextVtableRva"]=reinterpret_cast<uintptr_t>(*reinterpret_cast<void**>(static_cast<IMatRenderContext*>(context)))-base;
        auto rva=[&](void* table){return table?reinterpret_cast<uintptr_t>(table)-base:0;};
        out["contextClass"]=rttiClass(static_cast<IMatRenderContext*>(context),L"materialsystem.dll");
        out["queuedContextRva"]=rva(queuedContextTable.load());out["hardwareContextRva"]=rva(hardwareContextTable.load());
        out["recordsForRenderThread"]=recordsForRenderThread(context);out["drawsDirectly"]=drawsDirectly(context);
        if(recordsForRenderThread(context)){
            auto list=reinterpret_cast<SourceCallList*>(context->GetCallQueue());out["callListOffset"]=reinterpret_cast<char*>(list)-reinterpret_cast<char*>(static_cast<IMatRenderContext*>(context));
            if(list)out["callListUsedBytes"]=list->next-list->buffer;
        }}
    return out;
}
// Runs every call still queued for the render thread: IMaterialSystem::Lock
// waits for the render thread and executes the calls recorded so far. Needed
// before anything a queued draw uses goes away (worker pool, this module).
void drainRenderQueue(){if(materials&&outstandingCalls.load()>0){auto lock=materials->Lock();materials->Unlock(lock);}}
std::string takeAsyncRenderError(){std::lock_guard lock(asyncMutex);std::string error;error.swap(asyncError);return error;}
// A hardware-path failure reported by an earlier draw returns the instance to
// CPU skinning (the next snapshot deforms every vertex again).
static void applyGpuFailures(Instance& instance){
    std::string reason;
    {std::lock_guard lock(asyncMutex);if(gpuFailures.empty())return;
     for(auto it=gpuFailures.begin();it!=gpuFailures.end();)if(it->first==instance.id){reason=it->second;it=gpuFailures.erase(it);}else ++it;}
    if(reason.empty())return;instance.gpuBlocked=true;instance.gpuBlockReason=reason;instance.poseDirty=true;
}
// Main thread: the instance state every draw item of a job shares.
static bool prepareDraw(Instance& instance,DrawJob& job,bool edges,RenderTint tint){
    if(renderSuspended)return false;
    instance.lastDrawFrame=renderFrame;
    initialize();instance.ensureSnapshot();if(!instance.snapshot)return false;
    job.instance=instance.id;job.model=instance.model;job.snapshot=instance.snapshot;job.rest=instance.gpuRest;
    job.renderView=instance.renderView;job.sourceRig=instance.sourceRig!=nullptr;job.frozen=instance.frozen;job.edges=edges;job.tint=tint;
    job.rigScale=instance.sourceRig?instance.sourceRig->scale:1.f;
    if(instance.renderView&&instance.sourceRig){
        if(!instance.firstPersonMask)instance.firstPersonMask=std::make_shared<const std::vector<uint8_t>>(firstPersonTriangles(*instance.model,*instance.sourceRig,false));
        job.firstPerson=instance.firstPersonMask;
    }
    job.center=instance.presentationBones.empty()?(instance.snapshot->minimum+instance.snapshot->maximum)*.5f:instance.presentationBones[0].getOrigin();
    return true;
}
// Main thread: one material bind over the requested parts that are visible and
// not transparent. A material or pass the hardware path cannot cover returns
// the instance to CPU skinning here (the snapshot is re-published).
// RTX Remix traces alpha-tested draws as opaque for shadows (until opacity
// micromaps, which it bakes over tens of seconds and coarsely, catch up), so MMD
// overlay layers that raster alpha testing removes (hair highlight and shadow
// shells, stockings over legs) left what lies beneath them dark. For a part's
// own generated material (never an override): triangles with no texel at the
// 0.5 reference are not drawn, as raster shows nothing of them, and a layer
// left with none is skipped; one whose remaining triangles are mostly cut away
// is blended instead, which Remix renders without shadows and which matches the
// test for the binary alpha of such layers. Mostly opaque parts keep the test.
static bool generatedMaterial(const Instance& instance,unsigned part,const std::string& name){
    return (part<instance.colorNames.size()&&instance.colorNames[part]==name)||(part<instance.depthNames.size()&&instance.depthNames[part]==name);
}
static void addDrawItem(Instance& instance,DrawJob& job,std::span<const unsigned> requested,const std::string& name,bool perPartColor){
    auto& model=*job.model;DrawItem item;item.name=name;item.perPartColor=perPartColor;item.parts.reserve(requested.size());
    for(auto candidate:requested){if(candidate>=model.materials.size())throw std::runtime_error("Invalid material index");if(candidate<instance.materialVisible.size()&&!instance.materialVisible[candidate])continue;auto& m=job.snapshot->materials[candidate];if((job.edges&&!m.edge)||m.alpha<=.0001f)continue;
        if(remixFixedFunction&&model.materials[candidate].alphaTexture&&model.materials[candidate].alphaCoverage<=0&&generatedMaterial(instance,candidate,name))continue;
        item.parts.push_back(candidate);}
    if(item.parts.empty())return;
    item.material=findMaterial(name);
    if(remixFixedFunction&&!item.material->GetMaterialVarFlag(MATERIAL_VAR_TRANSLUCENT)){
        bool cutAway=false;for(auto part:item.parts){const auto& m=model.materials[part];cutAway|=m.alphaTexture&&m.alphaCoverage<.5f&&generatedMaterial(instance,part,name);}
        if(cutAway){item.material->SetMaterialVarFlag(MATERIAL_VAR_TRANSLUCENT,true);item.material->RecomputeStateSnapshots();}
    }
    // Triangles the test removes entirely are not drawn under Remix: a near-empty
    // shell drawn blended over the body hid the body behind it (Furina's legs).
    if(remixFixedFunction&&!model.cutoutTriangles.empty()){
        item.cutout=true;for(auto part:item.parts)item.cutout&=generatedMaterial(instance,part,name);
    }
    if(job.snapshot->gpu){
        const char* reason=remixFixedFunction?"fixed-function renderer":!nativeVertexCache?"vertex cache disabled":job.edges?"edge pass":
            ((item.material->GetVertexFormat()&~VERTEX_FORMAT_COMPRESSED)|commonFormat)!=commonFormat?"material vertex format":!skinCapable(item.material)?"shader without hardware skinning":nullptr;
        if(reason){instance.gpuBlocked=true;instance.gpuBlockReason=std::string(reason)+" ("+name+", "+item.material->GetShaderName()+")";instance.poseDirty=true;instance.ensureSnapshot();job.snapshot=instance.snapshot;if(!job.snapshot){job.items.clear();return;}}
    }
    job.items.push_back(std::move(item));
}
static void executeItem(const DrawJob& job,const DrawItem& draw,std::span<const unsigned> parts);
// Where the hardware context is current: the caller, or Source's render thread.
static void executeJob(const DrawJob& job){
    Measure measure{nativeMs};Measure shadowTime{shadowMs,job.shadow};
    for(auto& item:job.items)executeItem(job,item,item.parts);
}
static void drawPrepared(Instance& instance,bool edges,RenderTint tint,bool shadow,float alphaScale,const std::function<void(DrawJob&)>& fill){
    applyGpuFailures(instance);
    auto job=std::make_shared<DrawJob>();
    {Measure time{prepareMs};if(!prepareDraw(instance,*job,edges,tint))return;job->shadow=shadow;job->alphaScale=alphaScale;fill(*job);}
    if(job->items.empty()||!job->snapshot)return;
    if(!submit([job]{executeJob(*job);}))applyGpuFailures(instance);
}
void drawNative(uint64_t id,unsigned part,const std::string& name,bool edges,RenderTint tint){
    drawInstanceNative(world().get(id),part,name,edges,tint);
}
void drawInstanceNative(Instance& instance,unsigned part,const std::string& name,bool edges,RenderTint tint){unsigned parts[]={part};drawInstanceNativeParts(instance,parts,name,edges,tint,true);}
// Parts are filtered for visibility and alpha; the first remaining part
// supplies the modulation for the whole bind.
void drawInstanceNativeParts(Instance& instance,std::span<const unsigned> requested,const std::string& name,bool edges,RenderTint tint,bool perPartColor){
    drawPrepared(instance,edges,tint,false,1.f,[&](DrawJob& job){addDrawItem(instance,job,requested,name,perPartColor);});
}
void drawInstanceNativeBatch(Instance& instance,std::span<const std::pair<unsigned,std::string>> parts,RenderTint tint){
    drawPrepared(instance,false,tint,false,1.f,[&](DrawJob& job){for(auto& [part,name]:parts){unsigned one[]={part};addDrawItem(instance,job,one,name,true);if(!job.snapshot)return;}});
}
namespace {
void shadowDraw(void* self,IClientRenderable* renderable,const void* info,matrix3x4_t* custom){
    auto at=sourceShadows.find(renderable);if(at==sourceShadows.end()){originalShadowDraw(self,renderable,info,custom);return;}
    if(renderSuspended)return;
    try{
        auto& binding=at->second;WorldScope realm(binding.host);auto found=world().instances.find(binding.instance);if(found==world().instances.end())return;auto& instance=*found->second;
        if(binding.alpha<=0)return;
        // Parts sharing a shadow material draw in one bind; parts whose alpha
        // differs split into sub-groups. The depth-only pass has no order and
        // no per-part colour. The binding's alpha scales each bind where it runs.
        drawPrepared(instance,false,{},true,binding.alpha,[&](DrawJob& job){
            for(auto& [name,members]:binding.groups){
                std::vector<std::pair<int,unsigned>> keyed;keyed.reserve(members.size());
                for(auto part:members){float alpha=part<job.snapshot->materials.size()?job.snapshot->materials[part].alpha:1.f;keyed.push_back({int(std::lround(std::clamp(alpha,0.f,1.f)*255)),part});}
                std::sort(keyed.begin(),keyed.end());
                for(size_t i=0;i<keyed.size();){std::vector<unsigned> parts;size_t j=i;while(j<keyed.size()&&keyed[j].first==keyed[i].first)parts.push_back(keyed[j++].second);addDrawItem(instance,job,parts,name,false);if(!job.snapshot)return;i=j;}
            }
        });
        shadowDraws++;
    }catch(const std::exception& error){shadowError=error.what();}
}
}
static void executeItem(const DrawJob& job,const DrawItem& draw,std::span<const unsigned> parts){
    const auto& snapshot=job.snapshot;const auto& model=*job.model;unsigned part=parts[0];
    if(remixFixedFunction&&parts.size()>1){
        for(auto single:parts){unsigned one[]={single};executeItem(job,draw,one);}return;
    }
    auto engineMaterial=draw.material;const bool edges=job.edges,perPartColor=draw.perPartColor;const auto& tint=job.tint;
    const std::span<const uint8_t> firstPerson=job.firstPerson?std::span<const uint8_t>(*job.firstPerson):std::span<const uint8_t>{};
    auto& material=snapshot->materials[part];
    float originalColor[3];engineMaterial->GetColorModulation(originalColor,originalColor+1,originalColor+2);float originalAlpha=engineMaterial->GetAlphaModulation();
    struct RestoreMaterial{IMaterial* material;float alpha;float r,g,b;~RestoreMaterial(){material->AlphaModulate(alpha);material->ColorModulate(r,g,b);}} restoreMaterial{engineMaterial,originalAlpha,originalColor[0],originalColor[1],originalColor[2]};
    // Lua render.SetColorModulation/SetBlend target studio rendering. These
    // immediate IMesh submissions need entity modulation on the bound material.
    if(job.sourceRig){engineMaterial->AlphaModulate(std::clamp(material.alpha,0.f,1.f)*originalAlpha*job.alphaScale*tint.a);if(perPartColor)engineMaterial->ColorModulate(material.diffuse.x()*originalColor[0]*tint.r,material.diffuse.y()*originalColor[1]*tint.g,material.diffuse.z()*originalColor[2]*tint.b);else engineMaterial->ColorModulate(originalColor[0]*tint.r,originalColor[1]*tint.g,originalColor[2]*tint.b);}
    auto constant=[&](int reg,btVector3 value,float w){float values[]={value.x(),value.y(),value.z(),w};const char* axes[]={"x","y","z","w"};for(int k=0;k<4;k++){auto key="$c"+std::to_string(reg)+"_"+axes[k];bool found=false;auto var=engineMaterial->FindVar(key.c_str(),&found,false);if(found)var->SetFloatValue(values[k]);}};
    // These uniforms belong to the legacy screenspace_general shader. Source
    // model/depth/shadow shaders have none of them; avoid 17 name lookups and
    // temporary strings per material/pass on the native carrier path.
    if(!job.sourceRig){
        constant(0,material.diffuse,material.alpha);constant(1,material.ambient,float(material.sphereMode));constant(2,material.specular,material.power);constant(3,material.edgeColor,material.edgeAlpha);
        bool found=false;auto blend=engineMaterial->FindVar("$invviewprojmat",&found,false);if(found){VMatrix matrix;for(int r=0;r<4;r++)for(int c=0;c<4;c++)matrix[r][c]=r==c?1.f:0.f;for(int r=0;r<3;r++)for(int c=0;c<4;c++)matrix[r][c]=material.textureBlend[r][c];blend->SetMatrixValue(matrix);}
    }
    CMatRenderContextPtr context(materials);
    // Draws run where the hardware context is current (see submit). Vertices
    // written through the queued context would be copied into a bounded Source
    // arena, which detailed multi-model scenes exhaust.
    if(!drawsDirectly(context))throw std::runtime_error("Native character model draw reached a render context other than the hardware one");
    context->Bind(engineMaterial);frameBinds++;
    context->MatrixMode(MATERIAL_MODEL);context->PushMatrix();context->LoadIdentity();context->SetNumBoneWeights(0);
    struct RestoreMatrix{IMatRenderContext* context;~RestoreMatrix(){context->MatrixMode(MATERIAL_MODEL);context->PopMatrix();}} restore{context};
    struct RestoreProjection{
        IMatRenderContext* context;bool changed=false;
        ~RestoreProjection(){if(changed){context->MatrixMode(MATERIAL_PROJECTION);context->PopMatrix();context->MatrixMode(MATERIAL_MODEL);}}
    } restoreProjection{context};
    float layerSeparation=0;
    if(lightOverlapGuard&&job.sourceRig&&perPartColor&&parts.size()==1&&part<model.lightLayerRanks.size()&&
       model.lightLayerRanks[part]&&!engineMaterial->GetMaterialVarFlag(MATERIAL_VAR_TRANSLUCENT)&&
       !engineMaterial->GetMaterialVarFlag(MATERIAL_VAR_ADDITIVE)&&!engineMaterial->GetMaterialVarFlag(MATERIAL_VAR_IGNOREZ)){
        // Break only known coplanar material ties, by a few depth-buffer units.
        // Use exactly the same projection in the base and flashlight passes:
        // the depth buffer then selects the top layer *per covered sample*.
        // Alpha-test holes retain the lower surface, with no stencil ownership
        // conflicts and no edits to world vertices, normals, UVs or physics.
        const auto layers=*std::max_element(model.lightLayerRanks.begin(),model.lightLayerRanks.end());
        if(remixFixedFunction){
            // Remix infers cameras from projection matrices. Raster depth bias
            // makes these layers look like a different camera and drops them.
            // Separate ONLY the render copies by a fraction of a Source unit
            // along their normals. The PMX, skinning and physics stay unchanged.
            layerSeparation=remixLayerSeparation(model.lightLayerRanks[part],layers);
        }else{
            VMatrix projection;context->GetMatrix(MATERIAL_PROJECTION,&projection);
            Vector camera;context->GetWorldSpaceCameraPosition(&camera);
            auto center=job.center;
            const float distance=std::max(8.f,(center-btVector3(camera.x,camera.y,camera.z)).length());
            // Skinning/Source angle quantization can separate copies by more than
            // one depth unit. Keep a small world-equivalent allowance, tapering
            // with distance, with only a floating-point precision floor.
            const float step=std::min(.025f,.15f/float(layers+1));
            const float unit=std::max(0x1p-23f,step*std::abs(projection[2][3])/(distance*distance));
            const float offset=float(model.lightLayerRanks[part])*unit;
            for(int j=0;j<4;j++)projection[2][j]-=offset*projection[3][j];
            context->MatrixMode(MATERIAL_PROJECTION);context->PushMatrix();context->LoadMatrix(projection);
            context->MatrixMode(MATERIAL_MODEL);restoreProjection.changed=true;
        }
    }
    std::span<const uint8_t> lightMask;LightMask* maskEntry=nullptr;
    // The opaque pass and its additive lights must use the same owner. Leaving
    // both nearly coincident copies in the depth-writing pass can make a copy
    // we omit from the light pass win depth (dark triangles), or vice versa.
    // Match the current deformed positions AND cutout UV coverage; independent
    // morph surfaces and intentionally blended materials remain untouched.
    // Not under RTX Remix: it traces through what its opacity micromaps cut
    // from an upper layer (above, the layers are separated along their normals
    // instead), so a dropped lower layer left holes. Furina's legs vanished
    // under her stocking shell, which Remix renders transparent above the ankle.
    if(lightOverlapGuard&&!remixFixedFunction&&job.sourceRig&&!edges&&!model.lightOverlaps.empty()&&
       !engineMaterial->GetMaterialVarFlag(MATERIAL_VAR_TRANSLUCENT)&&!engineMaterial->GetMaterialVarFlag(MATERIAL_VAR_ADDITIVE)){
        auto& filter=lightMasks[{job.instance,job.renderView}];
        if(filter.snapshot.lock()!=snapshot||filter.geometry!=snapshot->geometry){
            Measure time{overlapMs};
            auto triangles=lightOverlapMask(model,*snapshot,std::max(.0005f,job.rigScale*.0001f),
                job.renderView?firstPerson:std::span<const uint8_t>{});
            if(triangles!=filter.triangles){filter.triangles=std::move(triangles);filter.ranges.clear();}
            filter.snapshot=snapshot;filter.geometry=snapshot->geometry;
        }
        lightMask=filter.triangles;maskEntry=&filter;
    }
    auto drawRange=[&](IMesh* mesh,unsigned first,unsigned count,std::span<const unsigned> ids,unsigned implicitFirst=0){
        if(lightMask.empty()){mesh->Draw(int(first),int(count));frameDrawCalls++;return;}
        MaskedRanges computed;MaskedRanges* cached=nullptr;
        if(maskEntry){auto key=std::make_tuple(ids.empty()?nullptr:ids.data(),implicitFirst,first,count);auto it=maskEntry->ranges.find(key);if(it!=maskEntry->ranges.end())cached=&it->second;else{cached=&maskEntry->ranges.emplace(key,MaskedRanges{}).first->second;cached->dropped=~0u;}}
        auto& result=cached?*cached:computed;
        if(!cached||result.dropped==~0u){
            result.ranges.clear();result.dropped=0;int start=-1;
            for(unsigned at=first;at<first+count;at+=3){
                unsigned triangle=ids.empty()?implicitFirst+at/3:ids[at/3];
                bool keep=lightMask[triangle]!=0;
                if(keep&&start<0)start=int(at);
                if(!keep){result.dropped++;if(start>=0){result.ranges.emplace_back(start,int(at)-start);start=-1;}}
            }
            if(start>=0)result.ranges.emplace_back(start,int(first+count)-start);
        }
        if(context->GetFlashlightMode())flashlightDuplicates+=result.dropped;
        if(!result.ranges.empty()){mesh->Draw(result.ranges.data(),int(result.ranges.size()));frameDrawCalls+=unsigned(result.ranges.size());}
    };
    unsigned end=material.first+material.count;
    auto vertex=[&](CMeshBuilder& builder,unsigned index){const auto& v=snapshot->vertices[index];float offset=edges?v.edge*material.edgeSize*.025f:layerSeparation;
        builder.Position3f(v.x+v.nx*offset,v.y+v.ny*offset,v.z+v.nz*offset);builder.Normal3f(v.nx,v.ny,v.nz);builder.Color4ub(255,255,255,255);builder.TexCoord2f(0,v.u,v.v);builder.TexCoord4f(1,v.extra0,v.extra1,v.edge,edges?1.f:0.f);if(builder.m_VertexSize_TangentS)builder.TangentS3f(v.tx,v.ty,v.tz);if(builder.m_VertexSize_TangentT){auto b=btVector3(v.nx,v.ny,v.nz).cross(btVector3(v.tx,v.ty,v.tz))*v.tw;builder.TangentT3f(b.x(),b.y(),b.z());}if(builder.m_VertexSize_UserData){float tangent[]={v.tx,v.ty,v.tz,v.tw};builder.UserData(tangent);}builder.AdvanceVertex();};
    bool cpuRemainder=false;
    if(snapshot->gpu)try{
        auto geometryKey=std::make_pair(&model,job.renderView);auto gi=skinGeometry.find(geometryKey);
        if(gi!=skinGeometry.end()&&gi->second.model.expired()){skinGeometry.erase(gi);gi=skinGeometry.end();}
        if(gi==skinGeometry.end()){
            SkinGeometry built;built.model=job.model;built.plan=model.gpuSkin();std::span<const uint8_t> view=job.renderView?firstPerson:std::span<const uint8_t>{};
            built.topology=skinTopology(model,*built.plan,view);built.cpuParts.assign(model.materials.size(),0);
            for(size_t p=0;p<model.materials.size();p++){auto& m=model.materials[p];for(unsigned first=m.first;first<m.first+m.count;first+=3)if(built.plan->cpuTriangle[first/3]&&(view.empty()||view[first/3]))built.cpuParts[p]++;}
            gi=skinGeometry.emplace(geometryKey,std::move(built)).first;
        }
        const auto& geometry=gi->second;const auto& skinned=geometry.topology;
        // Rest data is shared with the publishing thread; uploads hold its lock.
        auto& rest=*job.rest;
        auto checkRest=[&]{if(rest.positions.size()!=model.vertices.size()*3||rest.changed.size()!=model.vertices.size())throw std::runtime_error("Hardware skinning rest data is missing");};
        // Rest vertex: morphed MMD position, rest normal and tangent, current UVs,
        // two stored weights (the third is 1-w0-w1) and three batch-local slots.
        constexpr VertexFormat_t skinFormat=commonFormat|VERTEX_BONE_INDEX|VERTEX_BONEWEIGHT(2);
        auto skinVertex=[&](CMeshBuilder& builder,const SkinChunk& chunk,size_t k){
            unsigned i=chunk.vertices[k];const float* p=&rest.positions[size_t(i)*3];const auto& v=model.vertices[i];const auto& d=snapshot->vertices[i];
            builder.Position3f(p[0],p[1],p[2]);builder.BoneWeight(0,chunk.weights[k][0]);builder.BoneWeight(1,chunk.weights[k][1]);
            for(int s=0;s<3;s++)builder.BoneMatrix(s,chunk.slots[k][s]);builder.BoneMatrix(3,0);
            builder.Normal3f(v.normal.x(),v.normal.y(),v.normal.z());builder.Color4ub(255,255,255,255);builder.TexCoord2f(0,d.u,d.v);
            auto t=i<model.tangents.size()?model.tangents[i]:btVector3(1,0,0);float tangent[4]={t.x(),t.y(),t.z(),d.tw};builder.UserData(tangent);builder.AdvanceVertex();
        };
        auto& item=skinMeshes[{job.instance,job.renderView}];item.lastSequence=snapshot->sequence;
        auto& slot=item.slots[snapshot->restVersion%item.slots.size()];
        if(slot.meshes.empty()){
            Measure time{uploadMs};std::lock_guard restLock(rest.mutex);checkRest();
            try{for(const auto& chunk:skinned.chunks){
                auto mesh=context->CreateStaticMesh(skinFormat,"Model textures",nullptr);if(!mesh)throw std::runtime_error("Cannot allocate skinned vertex buffer");slot.meshes.push_back(mesh);
                CMeshBuilder builder;builder.Begin(mesh,MATERIAL_TRIANGLES,int(chunk.vertices.size()),int(chunk.indices.size()));
                if(builder.m_NumBoneWeights!=2||!builder.m_pBoneMatrixIndex)throw std::runtime_error("Source did not provide a skinned vertex layout");
                for(size_t k=0;k<chunk.vertices.size();k++)skinVertex(builder,chunk,k);
                for(auto index:chunk.indices){builder.Index(index);builder.AdvanceIndex();}
                size_t bytes=chunk.vertices.size()*builder.m_ActualVertexSize+chunk.indices.size()*2;slot.bytes+=bytes;uploadedBytes+=bytes;builder.End();
            }}catch(...){for(auto mesh:slot.meshes)context->DestroyStaticMesh(mesh);slot=SkinSlot{};throw;}
            slot.restVersion=snapshot->restVersion;skinBuilds++;
        }else if(slot.restVersion!=snapshot->restVersion){
            Measure time{uploadMs};std::lock_guard restLock(rest.mutex);checkRest();
            // Rewrite the 8192-vertex spans holding a vertex whose rest data changed
            // after this buffer was last written; a run of such spans is one lock.
            constexpr size_t spanSize=8192;struct Run {size_t chunk,first,end;};std::vector<Run> runs;
            for(size_t c=0;c<skinned.chunks.size();c++){const auto& chunk=skinned.chunks[c];bool open=false;
                for(size_t first=0;first<chunk.vertices.size();first+=spanSize){size_t end=std::min(chunk.vertices.size(),first+spanSize);bool dirty=false;
                    for(size_t k=first;k<end&&!dirty;k++)dirty=rest.changed[chunk.vertices[k]]>slot.restVersion;
                    if(dirty){if(open)runs.back().end=end;else{runs.push_back({c,first,end});open=true;}}else open=false;}}
            for(auto& run:runs){const auto& chunk=skinned.chunks[run.chunk];CMeshBuilder builder;
                builder.BeginModify(slot.meshes[run.chunk],int(run.first),int(run.end-run.first),0,0);
                for(size_t k=run.first;k<run.end;k++)skinVertex(builder,chunk,k);
                uploadedBytes+=size_t(builder.m_ActualVertexSize)*(run.end-run.first);builder.EndModify();}
            slot.restVersion=snapshot->restVersion;skinUpdates++;
        }
        {Measure time{drawMs};
            // Source copies the model matrix into bone slot 0 when it commits the
            // skinning constants, so slot 0 is also loaded as the model matrix.
            context->MatrixMode(MATERIAL_MODEL);context->SetNumBoneWeights(3);
            // Leave the engine unskinned with an identity model matrix even if a draw throws.
            struct Unskin{IMatRenderContext* context;~Unskin(){context->SetNumBoneWeights(0);context->MatrixMode(MATERIAL_MODEL);context->LoadIdentity();}} unskin{context};
            for(auto drawn:parts)for(const auto& batch:skinned.parts[drawn]){
                const float* first=&snapshot->palette[size_t(batch.bones[0])*12];VMatrix modelMatrix;
                for(int r=0;r<3;r++)for(int c=0;c<4;c++)modelMatrix[r][c]=first[r*4+c];modelMatrix[3][0]=modelMatrix[3][1]=modelMatrix[3][2]=0;modelMatrix[3][3]=1;
                context->LoadMatrix(modelMatrix);
                for(size_t s=0;s<batch.bones.size();s++)context->LoadBoneMatrix(int(s),*reinterpret_cast<const matrix3x4_t*>(&snapshot->palette[size_t(batch.bones[s])*12]));
                frameBoneLoads+=unsigned(batch.bones.size());frameSkinBatches++;
                drawRange(slot.meshes[batch.chunk],batch.first,batch.count,skinned.chunks[batch.chunk].triangles);
            }
        }
        skinDraws++;status="Native hardware-skinned static buffers";
        // The parts' CPU-skinned triangles (SDEF/QDEF, heavy fourth weights) follow on the shared CPU path.
        for(auto drawn:parts)cpuRemainder|=geometry.cpuParts[drawn]!=0;
        if(!cpuRemainder)return;
    }catch(const std::exception& error){
        // A failed hardware path (allocation, unexpected layout) returns the
        // instance to CPU skinning at its next draw (applyGpuFailures) instead of
        // failing every frame; this draw skips the part.
        std::lock_guard lock(asyncMutex);if(gpuFailures.size()<256)gpuFailures.emplace_back(job.instance,std::string("hardware path failed: ")+error.what());return;
    }
    if(job.frozen&&!job.renderView){for(auto frozenPart:parts){auto& frozenMaterial=snapshot->materials[frozenPart];unsigned frozenEnd=frozenMaterial.first+frozenMaterial.count;
        auto format=engineMaterial->GetVertexFormat()&~VERTEX_FORMAT_COMPRESSED;
        auto key=std::make_tuple(snapshot.get(),frozenPart|(layerSeparation>0?0x20000000u:0u),edges,format);auto it=cache.find(key);
        // Snapshot buffers are reused for later poses: the address alone does not identify the geometry.
        if(it!=cache.end()&&(it->second.snapshot.expired()||it->second.geometry!=snapshot->geometry)){for(auto mesh:it->second.meshes)context->DestroyStaticMesh(mesh);cache.erase(it);it=cache.end();}
        if(it==cache.end()){
            CachedMesh item;item.snapshot=snapshot;item.geometry=snapshot->geometry;
            try{for(unsigned first=frozenMaterial.first;first<frozenEnd;first+=54000){
                unsigned count=std::min(54000u,frozenEnd-first);std::unordered_map<unsigned,unsigned short> remap;std::vector<unsigned> vertices;std::vector<unsigned short> indices;indices.reserve(count);
                for(unsigned k=0;k<count;k++){auto index=model.indices[first+k/3*3+(edges?2-k%3:k%3)];auto [entry,inserted]=remap.emplace(index,static_cast<unsigned short>(vertices.size()));if(inserted)vertices.push_back(index);indices.push_back(entry->second);}
                auto format=engineMaterial->GetVertexFormat()&~VERTEX_FORMAT_COMPRESSED;
                auto mesh=context->CreateStaticMesh(format,"Model textures",engineMaterial);if(!mesh)throw std::runtime_error("Cannot allocate frozen character model mesh");item.meshes.push_back(mesh);
                CMeshBuilder builder;builder.Begin(mesh,MATERIAL_TRIANGLES,int(vertices.size()),int(indices.size()));for(auto index:vertices)vertex(builder,index);for(auto index:indices){builder.Index(index);builder.AdvanceIndex();}builder.End();
                item.bytes+=vertices.size()*64+indices.size()*2;
            }}catch(...){for(auto mesh:item.meshes)context->DestroyStaticMesh(mesh);throw;}
            it=cache.emplace(key,std::move(item)).first;cacheBuilds++;
        }
        {Measure time{drawMs};for(size_t i=0;i<it->second.meshes.size();i++)drawRange(it->second.meshes[i],0,
            std::min(54000u,frozenMaterial.count-unsigned(i)*54000),{},frozenMaterial.first/3+unsigned(i)*18000);}cacheDraws++;}status="Native cached indexed mesh (frozen)";return;
    }
    auto requestedFormat=engineMaterial->GetVertexFormat()&~VERTEX_FORMAT_COMPRESSED;
    // Render-only layer positions are material-specific, even when two parts
    // share PMX vertex indices. Keep their upload buffers separate in Remix.
    bool batched=!remixFixedFunction&&job.sourceRig&&nativeVertexCache&&!edges&&(requestedFormat|commonFormat)==commonFormat;
    // Unbatched topologies are per part; draw them one at a time.
    if(!batched&&parts.size()>1){for(auto single:parts){unsigned one[]={single};executeItem(job,draw,one);}return;}
    unsigned cachePart=batched?std::numeric_limits<unsigned>::max():part;
    if(layerSeparation>0)cachePart|=0x20000000u;
    // Under hardware skinning this path draws only the CPU-skinned triangles.
    if(cpuRemainder)cachePart&=~0x10000000u;
    if(job.renderView)cachePart^=0x40000000u;
    // Remix: the triangles the alpha test removes entirely stay out of the index lists.
    const bool cutout=draw.cutout&&!batched&&model.cutoutTriangles.size()*3==model.indices.size();
    if(cutout)cachePart^=0x08000000u;
    auto topologyKey=std::make_pair(&model,cachePart);auto ti=topology.find(topologyKey);
    if(ti!=topology.end()&&ti->second.model.expired()){topology.erase(ti);ti=topology.end();}
    if(ti==topology.end()){
        Topology item;item.model=job.model;
        std::vector<uint8_t> remainder;
        if(cpuRemainder){remainder=model.gpuSkin()->cpuTriangle;if(job.renderView&&!firstPerson.empty())for(size_t t=0;t<remainder.size();t++)remainder[t]&=firstPerson[t];}
        if(batched)static_cast<BatchedTopology&>(item)=batchTopology(model,cpuRemainder?std::span<const uint8_t>(remainder):job.renderView?firstPerson:std::span<const uint8_t>{});
        else for(unsigned first=material.first;first<end;first+=18000){DrawChunk chunk;std::unordered_map<unsigned,unsigned short> remap;unsigned count=std::min(18000u,end-first);
            for(unsigned k=0;k<count;k++){if(job.renderView&&!firstPerson.empty()&&!firstPerson[(first+k)/3])continue;if(cutout&&!model.cutoutTriangles[(first+k)/3])continue;if(k%3==0)chunk.triangles.push_back((first+k)/3);auto index=model.indices[first+k];auto [entry,inserted]=remap.emplace(index,static_cast<unsigned short>(chunk.vertices.size()));if(inserted)chunk.vertices.push_back(index);chunk.indices.push_back(entry->second);}if(!chunk.indices.empty())item.chunks.push_back(std::move(chunk));
        }
        // Bones influencing each 8192-vertex span of a chunk, for partial uploads.
        for(auto& chunk:item.chunks){std::vector<std::vector<unsigned>> spans;
            for(size_t begin=0;begin<chunk.vertices.size();begin+=8192){std::vector<unsigned> bones;
                for(size_t k=begin;k<std::min(chunk.vertices.size(),begin+8192);k++){const auto& v=model.vertices[chunk.vertices[k]];for(int w=0;w<4;w++)if(v.bones[w]>=0)bones.push_back(unsigned(v.bones[w]));}
                std::sort(bones.begin(),bones.end());bones.erase(std::unique(bones.begin(),bones.end()),bones.end());spans.push_back(std::move(bones));}
            item.spanBones.push_back(std::move(spans));}
        ti=topology.emplace(topologyKey,std::move(item)).first;
    }
    if(job.sourceRig&&nativeVertexCache){
        const auto declared=engineMaterial->GetVertexFormat();auto requested=declared&~VERTEX_FORMAT_COMPRESSED;
        // Shaders with a COMPRESSED_VERTS combo declare VERTEX_FORMAT_COMPRESSED (as
        // StudioRender requires); one that reads normals or tangents without it keeps
        // its whole instance on the uncompressed layout, so no model uploads both.
        static std::unordered_set<uint64_t> uncompressedInstances;
        if(!(declared&VERTEX_FORMAT_COMPRESSED)&&((declared&(VERTEX_NORMAL|VERTEX_TANGENT_S|VERTEX_TANGENT_T))||UserDataSize(declared)>0))uncompressedInstances.insert(job.instance);
        // A material describes the attributes it consumes, not a separate copy
        // of the geometry. Share the common Source vertex layout across color,
        // flashlight, depth and projected-shadow shaders. Unusual user shaders
        // still get their own complete layout; no shader attributes are removed.
        VertexFormat_t format=requested|VERTEX_POSITION|VERTEX_NORMAL|VERTEX_COLOR|VERTEX_FORMAT_VERTEX_SHADER;
        format=(format&~USER_DATA_SIZE_MASK)|VERTEX_USERDATA_SIZE(std::max(4,UserDataSize(requested)));
        format=(format&~VERTEX_TEXCOORD_MASK(0))|VERTEX_TEXCOORD_SIZE(0,std::max(2,TexCoordSize(0,requested)));
        // Materials sharing these buffers decode normals through the mesh-selected
        // COMPRESSED_VERTS combo; depth and shadow shaders read positions only.
        // The small CPU remainder of a hardware-skinned model is filled on the
        // render thread, where packing normals costs more than the bytes it saves.
        if(compactVertices&&compactFailure.empty()&&batched&&layerSeparation==0&&!edges&&format==commonFormat&&!cpuRemainder&&!uncompressedInstances.contains(job.instance))format|=VERTEX_FORMAT_COMPRESSED;
        observedFormats.try_emplace(declared,format);
        // The layout check runs on the render thread once per lock; filling only writes memory.
        auto describe=[&](CMeshBuilder& builder){
            const auto d=static_cast<const VertexDesc_t&>(builder);
            auto offset=[&](const void* p){return reinterpret_cast<const unsigned char*>(p)-reinterpret_cast<const unsigned char*>(d.m_pPosition);};
            static std::unordered_set<VertexFormat_t> described;
            if(described.insert(format).second&&!observedLayouts.contains(std::to_string(format)))observedLayouts[std::to_string(format)]={{"stride",d.m_ActualVertexSize},{"normal",offset(d.m_pNormal)},{"color",offset(d.m_pColor)},{"uv",offset(d.m_pTexCoord[0])},{"user",offset(d.m_pUserData)}};
            bool aligned=reinterpret_cast<uintptr_t>(d.m_pPosition)%16==0;
            if(layerSeparation==0&&format==commonFormat&&!edges&&d.m_ActualVertexSize==64&&d.m_VertexSize_Position==64&&aligned
                &&offset(d.m_pNormal)==12&&offset(d.m_pColor)==24&&offset(d.m_pTexCoord[0])==28&&offset(d.m_pUserData)==36)return 1;
            if(format==(commonFormat|VERTEX_FORMAT_COMPRESSED)){
                // The UBYTE4 normal carries the tangent; the userdata element is
                // zero bytes wide (its pointer sits after the UV) and 28 pads to 32.
                if(layerSeparation==0&&!edges&&d.m_ActualVertexSize==32&&d.m_VertexSize_Position==32&&aligned
                   &&offset(d.m_pNormal)==12&&offset(d.m_pColor)==16&&offset(d.m_pTexCoord[0])==20)return 2;
                compactFailure="unexpected compressed layout: stride "+std::to_string(d.m_ActualVertexSize)+", position size "+std::to_string(d.m_VertexSize_Position)+", aligned "+std::to_string(aligned)
                    +", normal "+std::to_string(offset(d.m_pNormal))+", colour "+std::to_string(offset(d.m_pColor))+", uv "+std::to_string(offset(d.m_pTexCoord[0]))
                    +", user "+std::to_string(offset(d.m_pUserData))+"/"+std::to_string(d.m_VertexSize_UserData);
            }
            return 0;
        };
        // Builder writes for layouts we do not stream; a compressed buffer needs the packed normal/tangent.
        const bool compressed=(format&VERTEX_FORMAT_COMPRESSED)!=0;
        auto write=[&](CMeshBuilder& builder,unsigned index){
            if(!compressed){vertex(builder,index);return;}
            const auto& v=snapshot->vertices[index];float tangent[4]={v.tx,v.ty,v.tz,v.tw<0?-1.f:1.f};
            builder.Position3f(v.x,v.y,v.z);builder.CompressedNormal3f<VERTEX_COMPRESSION_ON>(v.nx,v.ny,v.nz);builder.Color4ub(255,255,255,255);builder.TexCoord2f(0,v.u,v.v);
            builder.CompressedUserData<VERTEX_COMPRESSION_ON>(tangent);builder.AdvanceVertex();
        };
        auto fillChunk=[&](CMeshBuilder& builder,const DrawChunk& chunk,int layout){
            const auto d=static_cast<const VertexDesc_t&>(builder);
            if(layout==1){
                for(size_t i=0;i<chunk.vertices.size();i++)streamSourceVertex(d.m_pPosition+i*16,snapshot->vertices[chunk.vertices[i]]);
                _mm_sfence();builder.AdvanceVertices(int(chunk.vertices.size()));
            }else if(layout==2){
                for(size_t i=0;i<chunk.vertices.size();i++)streamCompactVertex(d.m_pPosition+i*8,snapshot->vertices[chunk.vertices[i]]);
                _mm_sfence();builder.AdvanceVertices(int(chunk.vertices.size()));
            }else for(auto index:chunk.vertices)write(builder,index);
        };
        auto& item=liveMeshes[{job.instance,cachePart,edges,format}];item.lastSequence=snapshot->sequence;
        auto& slot=item.slots[snapshot->sequence%item.slots.size()];
        if(slot.meshes.empty()){
            Measure time{uploadMs};
            try{for(const auto& chunk:ti->second.chunks){
                auto mesh=context->CreateStaticMesh(format,"Model textures",nullptr);if(!mesh)throw std::runtime_error("Cannot allocate native vertex buffer");slot.meshes.push_back(mesh);
                CMeshBuilder builder;if(compressed)builder.SetCompressionType(VERTEX_COMPRESSION_ON);{Measure time{lockMs};builder.Begin(mesh,MATERIAL_TRIANGLES,int(chunk.vertices.size()),int(chunk.indices.size()));}
                {Measure time{fillMs};int layout=describe(builder);item.layout=layout;fillChunk(builder,chunk,layout);}
                for(size_t k=0;k<chunk.indices.size();k++){builder.Index(chunk.indices[k/3*3+(edges?2-k%3:k%3)]);builder.AdvanceIndex();}
                size_t bytes=chunk.vertices.size()*builder.m_VertexSize_Position+chunk.indices.size()*2;slot.bytes+=bytes;uploadedBytes+=bytes;{Measure time{unlockMs};builder.End();}
            }}catch(...){for(auto mesh:slot.meshes)context->DestroyStaticMesh(mesh);slot=VertexSlot{};throw;}
            slot.spanUploaded.clear();for(auto& chunk:ti->second.chunks)slot.spanUploaded.push_back(std::vector<uint64_t>((chunk.vertices.size()+8191)/8192,snapshot->sequence));
            slot.sequence=snapshot->sequence;liveUpdates++;
        }else if(slot.sequence!=snapshot->sequence){
            Measure time{uploadMs};
            // Only spans whose bones moved since this slot's last upload are
            // locked and streamed; a run of dirty spans is one lock. Layouts
            // other than the common one fall back to whole-chunk builder fills.
            constexpr size_t spanSize=8192;size_t chunkCount=slot.meshes.size();
            struct Run {size_t chunk,firstSpan,endSpan;};std::vector<Run> runs;
            if(slot.spanUploaded.size()!=chunkCount)slot.spanUploaded.assign(chunkCount,{});
            for(size_t i=0;i<chunkCount;i++){auto& chunk=ti->second.chunks[i];size_t spans=(chunk.vertices.size()+spanSize-1)/spanSize;auto& uploaded=slot.spanUploaded[i];if(uploaded.size()!=spans)uploaded.assign(spans,0);
                const std::vector<std::vector<unsigned>>* bones=i<ti->second.spanBones.size()?&ti->second.spanBones[i]:nullptr;bool open=false;
                for(size_t s=0;s<spans;s++){bool dirty=item.layout==0||!bones||s>=bones->size()||uploaded[s]<snapshot->allChanged;
                    if(!dirty)for(auto b:(*bones)[s]){if(b>=snapshot->boneChanged.size()||snapshot->boneChanged[b]>uploaded[s]){dirty=true;break;}}
                    frameSpans++;if(dirty){frameDirtySpans++;if(open)runs.back().endSpan=s+1;else{runs.push_back({i,s,s+1});open=true;}uploaded[s]=snapshot->sequence;}else open=false;}
            }
            auto runFirst=[&](const Run& run){return run.firstSpan*spanSize;};
            auto runCount=[&](const Run& run){return std::min(ti->second.chunks[run.chunk].vertices.size(),run.endSpan*spanSize)-run.firstSpan*spanSize;};
            std::vector<CMeshBuilder> builders(runs.size());
            {Measure time{lockMs};for(size_t r=0;r<runs.size();r++){if(compressed)builders[r].SetCompressionType(VERTEX_COMPRESSION_ON);builders[r].BeginModify(slot.meshes[runs[r].chunk],int(runFirst(runs[r])),int(runCount(runs[r])),0,0);}}
            {Measure time{fillMs};
                if(item.layout!=0){
                    const size_t stride=item.layout==2?8:16;
                    struct Span {size_t run,begin,end;};std::vector<Span> spans;
                    for(size_t r=0;r<runs.size();r++){auto& run=runs[r];size_t limit=ti->second.chunks[run.chunk].vertices.size();for(size_t s=run.firstSpan;s<run.endSpan;s++)spans.push_back({r,s*spanSize,std::min(limit,(s+1)*spanSize)});}
                    parallelFor(spans.size(),1,[&](size_t a,size_t b){for(size_t s=a;s<b;s++){auto span=spans[s];auto& run=runs[span.run];auto& chunk=ti->second.chunks[run.chunk];const auto d=static_cast<const VertexDesc_t&>(builders[span.run]);size_t base=runFirst(run);
                        if(stride==8)for(size_t k=span.begin;k<span.end;k++)streamCompactVertex(d.m_pPosition+(k-base)*8,snapshot->vertices[chunk.vertices[k]]);
                        else for(size_t k=span.begin;k<span.end;k++)streamSourceVertex(d.m_pPosition+(k-base)*16,snapshot->vertices[chunk.vertices[k]]);_mm_sfence();}});
                    for(size_t r=0;r<runs.size();r++)builders[r].AdvanceVertices(int(runCount(runs[r])));
                }else for(size_t r=0;r<runs.size();r++){auto& run=runs[r];auto& chunk=ti->second.chunks[run.chunk];size_t first=runFirst(run),end=first+runCount(run);for(size_t k=first;k<end;k++)write(builders[r],chunk.vertices[k]);}
            }
            {Measure time{unlockMs};for(size_t r=0;r<runs.size();r++){uploadedBytes+=size_t(builders[r].m_VertexSize_Position)*runCount(runs[r]);builders[r].EndModify();}}
            slot.sequence=snapshot->sequence;liveUpdates++;
        }
        {Measure time{drawMs};
            if(batched){
                // Adjacent index ranges of the listed parts merge into one draw.
                DrawRange pending{0,0,0};bool have=false;
                for(auto drawn:parts)for(auto& range:ti->second.parts[drawn]){
                    if(have&&range.chunk==pending.chunk&&range.first==pending.first+pending.count)pending.count+=range.count;
                    else{if(have)drawRange(slot.meshes[pending.chunk],pending.first,pending.count,ti->second.chunks[pending.chunk].triangles);pending=range;have=true;}
                }
                if(have)drawRange(slot.meshes[pending.chunk],pending.first,pending.count,ti->second.chunks[pending.chunk].triangles);
            }else for(size_t i=0;i<slot.meshes.size();i++)drawRange(slot.meshes[i],0,unsigned(ti->second.chunks[i].indices.size()),ti->second.chunks[i].triangles);
        }liveDraws++;status="Native shared triple-buffered vertex cache";return;
    }
    for(const auto& chunk:ti->second.chunks){
        std::vector<unsigned short> filtered;
        std::span<const unsigned short> indices=chunk.indices;
        if(!lightMask.empty()){
            for(size_t k=0;k<chunk.indices.size();k+=3){
                if(!lightMask[chunk.triangles[k/3]]){if(context->GetFlashlightMode())flashlightDuplicates++;continue;}
                filtered.insert(filtered.end(),chunk.indices.begin()+k,chunk.indices.begin()+k+3);
            }
            indices=filtered;
        }
        if(indices.empty())continue;
        auto mesh=context->GetDynamicMesh(true,nullptr,nullptr,engineMaterial);if(!mesh)throw std::runtime_error("Dynamic mesh unavailable");
        CMeshBuilder builder;builder.Begin(mesh,MATERIAL_TRIANGLES,int(chunk.vertices.size()),int(indices.size()));
        status="Native indexed stream; stride="+std::to_string(builder.m_VertexSize_Position)+" vertices="+std::to_string(chunk.vertices.size());
        for(auto index:chunk.vertices)vertex(builder,index);
        for(size_t k=0;k<indices.size();k++){builder.Index(indices[k/3*3+(edges?2-k%3:k%3)]);builder.AdvanceIndex();}
        uploadedBytes+=chunk.vertices.size()*builder.m_VertexSize_Position+chunk.indices.size()*2;streamDraws++;
        builder.End();mesh->Draw();context->Flush();
    }
}
}
