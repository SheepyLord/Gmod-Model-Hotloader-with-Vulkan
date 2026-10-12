#pragma once
// The Direct3D 9 side of the ToGL replacement: thin calls into DXVK's native
// d3d9 (libdxvk_d3d9.so). backend.cpp is the only file that sees DXVK's
// headers; ToGL's types (togl_abi.hpp) reuse the same names, so the two never
// meet in one translation unit. Objects are DXVK's COM pointers, opaque here;
// structs passed as void* have the same layout in ToGL and Direct3D 9
// (present parameters, surface descriptions, locked rects and boxes,
// viewports, vertex elements, display modes, gamma ramps). Every function
// returns a Direct3D HRESULT.
#include <cstdint>
#include <string>

namespace dxbk {
using Obj=void*;
struct AdapterInfo { char driver[512]; char description[512]; char deviceName[32]; int64_t driverVersion; uint32_t vendor,device,subsys,revision; };
struct Rect32 { int32_t x1,y1,x2,y2; };

bool load(const char* library,std::string& error);
uint32_t modeCount(uint32_t format);
int32_t enumMode(uint32_t format,uint32_t index,void* mode);
int32_t displayMode(void* mode);
int32_t adapterInfo(AdapterInfo& out);
int32_t checkFormat(uint32_t adapterFormat,uint32_t usage,uint32_t type,uint32_t format);
int32_t checkMultisample(uint32_t format,int windowed,uint32_t type,uint32_t* quality);
int32_t createDevice(void* window,uint32_t behavior,void* presentParams,Obj* device);

int32_t reset(Obj dev,void* presentParams);
int32_t present(Obj dev);
int32_t testCooperativeLevel(Obj dev);
int32_t beginScene(Obj dev);
int32_t endScene(Obj dev);
int32_t clear(Obj dev,uint32_t count,const Rect32* rects,uint32_t flags,uint32_t color,float z,uint32_t stencil);
int32_t setViewport(Obj dev,const void* viewport);
int32_t getViewport(Obj dev,void* viewport);
int32_t setScissorRect(Obj dev,const void* rect);
int32_t setClipPlane(Obj dev,uint32_t index,const float* plane);
int32_t setRenderState(Obj dev,uint32_t state,uint32_t value);
int32_t setSamplerState(Obj dev,uint32_t sampler,uint32_t type,uint32_t value);
int32_t setTexture(Obj dev,uint32_t stage,Obj texture);
int32_t setVertexShader(Obj dev,Obj shader);
int32_t setPixelShader(Obj dev,Obj shader);
int32_t setVertexShaderConstantF(Obj dev,uint32_t start,const float* data,uint32_t count);
int32_t setVertexShaderConstantB(Obj dev,uint32_t start,const int32_t* data,uint32_t count);
int32_t setVertexShaderConstantI(Obj dev,uint32_t start,const int32_t* data,uint32_t count);
int32_t setPixelShaderConstantF(Obj dev,uint32_t start,const float* data,uint32_t count);
int32_t setPixelShaderConstantB(Obj dev,uint32_t start,const int32_t* data,uint32_t count);
int32_t setPixelShaderConstantI(Obj dev,uint32_t start,const int32_t* data,uint32_t count);
int32_t setVertexDeclaration(Obj dev,Obj decl);
int32_t setStreamSource(Obj dev,uint32_t stream,Obj buffer,uint32_t offset,uint32_t stride);
int32_t setIndices(Obj dev,Obj buffer);
int32_t drawPrimitive(Obj dev,uint32_t type,uint32_t start,uint32_t count);
int32_t drawIndexedPrimitive(Obj dev,uint32_t type,int32_t baseVertex,uint32_t minIndex,uint32_t numVertices,uint32_t startIndex,uint32_t primCount);
int32_t drawIndexedPrimitiveUP(Obj dev,uint32_t type,uint32_t minIndex,uint32_t numVertices,uint32_t primCount,const void* indices,uint32_t indexFormat,const void* vertices,uint32_t stride);
int32_t setRenderTarget(Obj dev,uint32_t index,Obj surface);
int32_t setDepthStencilSurface(Obj dev,Obj surface);
int32_t getBackBuffer(Obj dev,Obj* surface);
int32_t getDepthStencilSurface(Obj dev,Obj* surface);
int32_t createTexture(Obj dev,uint32_t w,uint32_t h,uint32_t levels,uint32_t usage,uint32_t format,uint32_t pool,Obj* out);
int32_t createCubeTexture(Obj dev,uint32_t edge,uint32_t levels,uint32_t usage,uint32_t format,uint32_t pool,Obj* out);
int32_t createVolumeTexture(Obj dev,uint32_t w,uint32_t h,uint32_t d,uint32_t levels,uint32_t usage,uint32_t format,uint32_t pool,Obj* out);
int32_t createRenderTarget(Obj dev,uint32_t w,uint32_t h,uint32_t format,uint32_t samples,uint32_t quality,int lockable,Obj* out);
int32_t createDepthStencilSurface(Obj dev,uint32_t w,uint32_t h,uint32_t format,uint32_t samples,uint32_t quality,int discard,Obj* out);
int32_t createOffscreenPlainSurface(Obj dev,uint32_t w,uint32_t h,uint32_t format,uint32_t pool,Obj* out);
int32_t createVertexBuffer(Obj dev,uint32_t length,uint32_t usage,uint32_t fvf,uint32_t pool,Obj* out);
int32_t createIndexBuffer(Obj dev,uint32_t length,uint32_t usage,uint32_t format,uint32_t pool,Obj* out);
int32_t createVertexShader(Obj dev,const uint32_t* code,Obj* out);
int32_t createPixelShader(Obj dev,const uint32_t* code,Obj* out);
int32_t createVertexDeclaration(Obj dev,const void* elements,Obj* out);
int32_t createQuery(Obj dev,uint32_t type,Obj* out);
int32_t stretchRect(Obj dev,Obj src,const void* srcRect,Obj dst,const void* dstRect,uint32_t filter);
int32_t getRenderTargetData(Obj dev,Obj renderTarget,Obj dest);
int32_t getFrontBufferData(Obj dev,Obj dest);
void setGammaRamp(Obj dev,const void* ramp);
int32_t evictManagedResources(Obj dev);

uint32_t addRef(Obj o);
uint32_t release(Obj o);
int32_t textureLock(Obj tex,uint32_t level,void* lockedRect,const void* rect,uint32_t flags);
int32_t textureUnlock(Obj tex,uint32_t level);
int32_t textureSurface(Obj tex,uint32_t level,Obj* out);
int32_t textureLevelDesc(Obj tex,uint32_t level,void* desc);
uint32_t levelCount(Obj tex);
int32_t cubeSurface(Obj tex,uint32_t face,uint32_t level,Obj* out);
int32_t cubeLevelDesc(Obj tex,uint32_t level,void* desc);
int32_t volumeLock(Obj tex,uint32_t level,void* lockedBox,const void* box,uint32_t flags);
int32_t volumeUnlock(Obj tex,uint32_t level);
int32_t volumeLevelDesc(Obj tex,uint32_t level,void* desc);
int32_t surfaceLock(Obj surf,void* lockedRect,const void* rect,uint32_t flags);
int32_t surfaceUnlock(Obj surf);
int32_t surfaceDesc(Obj surf,void* desc);
int32_t vertexBufferLock(Obj vb,uint32_t offset,uint32_t size,void** data,uint32_t flags);
int32_t vertexBufferUnlock(Obj vb);
int32_t indexBufferLock(Obj ib,uint32_t offset,uint32_t size,void** data,uint32_t flags);
int32_t indexBufferUnlock(Obj ib);
int32_t queryIssue(Obj q,uint32_t flags);
int32_t queryGetData(Obj q,void* data,uint32_t size,uint32_t flags);
}
