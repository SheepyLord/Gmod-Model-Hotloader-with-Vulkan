#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>
namespace mmd { void registerSourceShadow(int entity,uint64_t instance,const std::vector<std::string>& materials,float alpha,void* corpsePhysics=nullptr);void removeSourceShadow(int entity);void shutdownSourceShadows(); }
namespace mmd {
struct Instance;
struct RenderTint {float r=1,g=1,b=1,a=1;};
void setupSourceLighting(float x,float y,float z);
void checkRenderer();
void setNativeVertexCache(bool enabled);
void setCompactVertices(bool enabled);
void setGpuSkinning(bool enabled);
uint64_t renderFrameNumber();
// Diagnostic: skip native colour and shadow drawing while set.
void setRenderSuspended(bool suspended);
void drawInstanceNative(Instance&,unsigned,const std::string&,bool,RenderTint tint={});
// Draws the listed parts of one instance with a single material bind.
void drawInstanceNativeParts(Instance&,std::span<const unsigned> parts,const std::string&,bool edges,RenderTint tint,bool perPartColor);
void drawNative(uint64_t instance,unsigned part,const std::string& material,bool edges,RenderTint tint={});
// Which triangles of a part one draw covers: all, only its soft ones (Model::softTriangles,
// drawn blended through the part's soft-alpha material) or all but those (RTX Remix).
enum class TriangleSubset : uint8_t {All,Soft,Firm};
struct PartDraw {unsigned part=0;std::string name;TriangleSubset subset=TriangleSubset::All;};
// Draws each part with its own bind, as one render call.
void drawInstanceNativeBatch(Instance&,std::span<const PartDraw> parts,RenderTint tint);
// Soft alpha on (default) or off: whether parts with soft triangles and a registered
// soft-alpha material draw them blended. softAlphaPass says where: 0 nowhere, 1 in
// the translucent pass after the part's alpha-tested opaque draw (Source), 2 in the
// opaque pass, the part's own material then leaving them out (RTX Remix).
void setSoftAlpha(bool enabled);
int softAlphaPass();
// Under Source's queued (multicore) material system native draws run on its
// render thread. drainRenderQueue leaves no call of this module queued (before
// the worker pool or the render buffers go away): the render thread finishes
// the calls it has, and the frame being recorded loses its draws and runs the
// rest now. closeRenderQueue drains when the module closes. takeAsyncRenderError
// returns and clears the last error such a call raised.
void drainRenderQueue();
void closeRenderQueue();
std::string takeAsyncRenderError();
bool materialIsTranslucent(const std::string& material);
std::string rendererStatus();std::string rendererStats();void pruneRenderCache(bool all=false);
std::string renderLightingState();
void setLightOverlapGuard(bool enabled);
void beginRenderFrame();
std::string renderFrameStats();
std::string modelAnimationDiagnostics(const std::string& path);
}
