// Direct3D 9 calls into DXVK's native d3d9 (see backend.hpp).
#include "backend.hpp"
#include <d3d9.h>
#include <dlfcn.h>
#include <cstring>

namespace dxbk {
namespace {
IDirect3D9* d3d=nullptr;
template<class T> T* as(Obj o) { return static_cast<T*>(o); }
IDirect3DDevice9* dev9(Obj o) { return static_cast<IDirect3DDevice9*>(o); }
}

bool load(const char* library,std::string& error) {
 if(d3d)return true;
 void* module=dlopen(library,RTLD_NOW|RTLD_LOCAL);
 if(!module){const char* e=dlerror();error=e?e:"dlopen failed";return false;}
 auto create=reinterpret_cast<IDirect3D9*(*)(UINT)>(dlsym(module,"Direct3DCreate9"));
 if(!create){error="Direct3DCreate9 is missing";return false;}
 d3d=create(D3D_SDK_VERSION);
 if(!d3d){error="Direct3DCreate9 failed (no Vulkan device?)";return false;}
 return true;
}
uint32_t modeCount(uint32_t format) { return d3d->GetAdapterModeCount(D3DADAPTER_DEFAULT,D3DFORMAT(format)); }
int32_t enumMode(uint32_t format,uint32_t index,void* mode) { return d3d->EnumAdapterModes(D3DADAPTER_DEFAULT,D3DFORMAT(format),index,static_cast<D3DDISPLAYMODE*>(mode)); }
int32_t displayMode(void* mode) { return d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT,static_cast<D3DDISPLAYMODE*>(mode)); }
int32_t adapterInfo(AdapterInfo& out) {
 D3DADAPTER_IDENTIFIER9 id{};
 HRESULT hr=d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT,0,&id);
 if(FAILED(hr))return hr;
 static_assert(sizeof(out.driver)==sizeof(id.Driver)&&sizeof(out.description)==sizeof(id.Description)&&sizeof(out.deviceName)==sizeof(id.DeviceName));
 memcpy(out.driver,id.Driver,sizeof(out.driver));memcpy(out.description,id.Description,sizeof(out.description));memcpy(out.deviceName,id.DeviceName,sizeof(out.deviceName));
 out.driverVersion=id.DriverVersion.QuadPart;out.vendor=id.VendorId;out.device=id.DeviceId;out.subsys=id.SubSysId;out.revision=id.Revision;
 return hr;
}
int32_t checkFormat(uint32_t adapterFormat,uint32_t usage,uint32_t type,uint32_t format) { return d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,D3DFORMAT(adapterFormat),usage,D3DRESOURCETYPE(type),D3DFORMAT(format)); }
int32_t checkMultisample(uint32_t format,int windowed,uint32_t type,uint32_t* quality) { return d3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,D3DFORMAT(format),windowed,D3DMULTISAMPLE_TYPE(type),reinterpret_cast<DWORD*>(quality)); }
int32_t createDevice(void* window,uint32_t behavior,void* presentParams,Obj* device) {
 IDirect3DDevice9* out=nullptr;
 HRESULT hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,HWND(window),behavior,static_cast<D3DPRESENT_PARAMETERS*>(presentParams),&out);
 *device=out;return hr;
}

int32_t reset(Obj dev,void* presentParams) { return dev9(dev)->Reset(static_cast<D3DPRESENT_PARAMETERS*>(presentParams)); }
int32_t present(Obj dev) { return dev9(dev)->Present(nullptr,nullptr,nullptr,nullptr); }
int32_t testCooperativeLevel(Obj dev) { return dev9(dev)->TestCooperativeLevel(); }
int32_t beginScene(Obj dev) { return dev9(dev)->BeginScene(); }
int32_t endScene(Obj dev) { return dev9(dev)->EndScene(); }
int32_t clear(Obj dev,uint32_t count,const Rect32* rects,uint32_t flags,uint32_t color,float z,uint32_t stencil) {
 static_assert(sizeof(Rect32)==sizeof(D3DRECT));
 return dev9(dev)->Clear(count,reinterpret_cast<const D3DRECT*>(rects),flags,color,z,stencil);
}
int32_t setViewport(Obj dev,const void* viewport) { return dev9(dev)->SetViewport(static_cast<const D3DVIEWPORT9*>(viewport)); }
int32_t getViewport(Obj dev,void* viewport) { return dev9(dev)->GetViewport(static_cast<D3DVIEWPORT9*>(viewport)); }
int32_t setScissorRect(Obj dev,const void* rect) { return dev9(dev)->SetScissorRect(static_cast<const RECT*>(rect)); }
int32_t setClipPlane(Obj dev,uint32_t index,const float* plane) { return dev9(dev)->SetClipPlane(index,plane); }
int32_t setRenderState(Obj dev,uint32_t state,uint32_t value) { return dev9(dev)->SetRenderState(D3DRENDERSTATETYPE(state),value); }
int32_t setSamplerState(Obj dev,uint32_t sampler,uint32_t type,uint32_t value) { return dev9(dev)->SetSamplerState(sampler,D3DSAMPLERSTATETYPE(type),value); }
int32_t setTexture(Obj dev,uint32_t stage,Obj texture) { return dev9(dev)->SetTexture(stage,as<IDirect3DBaseTexture9>(texture)); }
int32_t setVertexShader(Obj dev,Obj shader) { return dev9(dev)->SetVertexShader(as<IDirect3DVertexShader9>(shader)); }
int32_t setPixelShader(Obj dev,Obj shader) { return dev9(dev)->SetPixelShader(as<IDirect3DPixelShader9>(shader)); }
int32_t setVertexShaderConstantF(Obj dev,uint32_t start,const float* data,uint32_t count) { return dev9(dev)->SetVertexShaderConstantF(start,data,count); }
int32_t setVertexShaderConstantB(Obj dev,uint32_t start,const int32_t* data,uint32_t count) { return dev9(dev)->SetVertexShaderConstantB(start,reinterpret_cast<const BOOL*>(data),count); }
int32_t setVertexShaderConstantI(Obj dev,uint32_t start,const int32_t* data,uint32_t count) { return dev9(dev)->SetVertexShaderConstantI(start,data,count); }
int32_t setPixelShaderConstantF(Obj dev,uint32_t start,const float* data,uint32_t count) { return dev9(dev)->SetPixelShaderConstantF(start,data,count); }
int32_t setPixelShaderConstantB(Obj dev,uint32_t start,const int32_t* data,uint32_t count) { return dev9(dev)->SetPixelShaderConstantB(start,reinterpret_cast<const BOOL*>(data),count); }
int32_t setPixelShaderConstantI(Obj dev,uint32_t start,const int32_t* data,uint32_t count) { return dev9(dev)->SetPixelShaderConstantI(start,data,count); }
int32_t setVertexDeclaration(Obj dev,Obj decl) { return dev9(dev)->SetVertexDeclaration(as<IDirect3DVertexDeclaration9>(decl)); }
int32_t setStreamSource(Obj dev,uint32_t stream,Obj buffer,uint32_t offset,uint32_t stride) { return dev9(dev)->SetStreamSource(stream,as<IDirect3DVertexBuffer9>(buffer),offset,stride); }
int32_t setIndices(Obj dev,Obj buffer) { return dev9(dev)->SetIndices(as<IDirect3DIndexBuffer9>(buffer)); }
int32_t drawPrimitive(Obj dev,uint32_t type,uint32_t start,uint32_t count) { return dev9(dev)->DrawPrimitive(D3DPRIMITIVETYPE(type),start,count); }
int32_t drawIndexedPrimitive(Obj dev,uint32_t type,int32_t baseVertex,uint32_t minIndex,uint32_t numVertices,uint32_t startIndex,uint32_t primCount) {
 return dev9(dev)->DrawIndexedPrimitive(D3DPRIMITIVETYPE(type),baseVertex,minIndex,numVertices,startIndex,primCount);
}
int32_t drawIndexedPrimitiveUP(Obj dev,uint32_t type,uint32_t minIndex,uint32_t numVertices,uint32_t primCount,const void* indices,uint32_t indexFormat,const void* vertices,uint32_t stride) {
 return dev9(dev)->DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE(type),minIndex,numVertices,primCount,indices,D3DFORMAT(indexFormat),vertices,stride);
}
int32_t setRenderTarget(Obj dev,uint32_t index,Obj surface) { return dev9(dev)->SetRenderTarget(index,as<IDirect3DSurface9>(surface)); }
int32_t setDepthStencilSurface(Obj dev,Obj surface) { return dev9(dev)->SetDepthStencilSurface(as<IDirect3DSurface9>(surface)); }
int32_t getBackBuffer(Obj dev,Obj* surface) { IDirect3DSurface9* s=nullptr;HRESULT hr=dev9(dev)->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&s);*surface=s;return hr; }
int32_t getDepthStencilSurface(Obj dev,Obj* surface) { IDirect3DSurface9* s=nullptr;HRESULT hr=dev9(dev)->GetDepthStencilSurface(&s);*surface=s;return hr; }
int32_t createTexture(Obj dev,uint32_t w,uint32_t h,uint32_t levels,uint32_t usage,uint32_t format,uint32_t pool,Obj* out) {
 IDirect3DTexture9* t=nullptr;HRESULT hr=dev9(dev)->CreateTexture(w,h,levels,usage,D3DFORMAT(format),D3DPOOL(pool),&t,nullptr);*out=t;return hr;
}
int32_t createCubeTexture(Obj dev,uint32_t edge,uint32_t levels,uint32_t usage,uint32_t format,uint32_t pool,Obj* out) {
 IDirect3DCubeTexture9* t=nullptr;HRESULT hr=dev9(dev)->CreateCubeTexture(edge,levels,usage,D3DFORMAT(format),D3DPOOL(pool),&t,nullptr);*out=t;return hr;
}
int32_t createVolumeTexture(Obj dev,uint32_t w,uint32_t h,uint32_t d,uint32_t levels,uint32_t usage,uint32_t format,uint32_t pool,Obj* out) {
 IDirect3DVolumeTexture9* t=nullptr;HRESULT hr=dev9(dev)->CreateVolumeTexture(w,h,d,levels,usage,D3DFORMAT(format),D3DPOOL(pool),&t,nullptr);*out=t;return hr;
}
int32_t createRenderTarget(Obj dev,uint32_t w,uint32_t h,uint32_t format,uint32_t samples,uint32_t quality,int lockable,Obj* out) {
 IDirect3DSurface9* s=nullptr;HRESULT hr=dev9(dev)->CreateRenderTarget(w,h,D3DFORMAT(format),D3DMULTISAMPLE_TYPE(samples),quality,lockable,&s,nullptr);*out=s;return hr;
}
int32_t createDepthStencilSurface(Obj dev,uint32_t w,uint32_t h,uint32_t format,uint32_t samples,uint32_t quality,int discard,Obj* out) {
 IDirect3DSurface9* s=nullptr;HRESULT hr=dev9(dev)->CreateDepthStencilSurface(w,h,D3DFORMAT(format),D3DMULTISAMPLE_TYPE(samples),quality,discard,&s,nullptr);*out=s;return hr;
}
int32_t createOffscreenPlainSurface(Obj dev,uint32_t w,uint32_t h,uint32_t format,uint32_t pool,Obj* out) {
 IDirect3DSurface9* s=nullptr;HRESULT hr=dev9(dev)->CreateOffscreenPlainSurface(w,h,D3DFORMAT(format),D3DPOOL(pool),&s,nullptr);*out=s;return hr;
}
int32_t createVertexBuffer(Obj dev,uint32_t length,uint32_t usage,uint32_t fvf,uint32_t pool,Obj* out) {
 IDirect3DVertexBuffer9* b=nullptr;HRESULT hr=dev9(dev)->CreateVertexBuffer(length,usage,fvf,D3DPOOL(pool),&b,nullptr);*out=b;return hr;
}
int32_t createIndexBuffer(Obj dev,uint32_t length,uint32_t usage,uint32_t format,uint32_t pool,Obj* out) {
 IDirect3DIndexBuffer9* b=nullptr;HRESULT hr=dev9(dev)->CreateIndexBuffer(length,usage,D3DFORMAT(format),D3DPOOL(pool),&b,nullptr);*out=b;return hr;
}
int32_t createVertexShader(Obj dev,const uint32_t* code,Obj* out) { IDirect3DVertexShader9* s=nullptr;HRESULT hr=dev9(dev)->CreateVertexShader(reinterpret_cast<const DWORD*>(code),&s);*out=s;return hr; }
int32_t createPixelShader(Obj dev,const uint32_t* code,Obj* out) { IDirect3DPixelShader9* s=nullptr;HRESULT hr=dev9(dev)->CreatePixelShader(reinterpret_cast<const DWORD*>(code),&s);*out=s;return hr; }
int32_t createVertexDeclaration(Obj dev,const void* elements,Obj* out) {
 IDirect3DVertexDeclaration9* d=nullptr;HRESULT hr=dev9(dev)->CreateVertexDeclaration(static_cast<const D3DVERTEXELEMENT9*>(elements),&d);*out=d;return hr;
}
int32_t createQuery(Obj dev,uint32_t type,Obj* out) { IDirect3DQuery9* q=nullptr;HRESULT hr=dev9(dev)->CreateQuery(D3DQUERYTYPE(type),&q);*out=q;return hr; }
int32_t stretchRect(Obj dev,Obj src,const void* srcRect,Obj dst,const void* dstRect,uint32_t filter) {
 return dev9(dev)->StretchRect(as<IDirect3DSurface9>(src),static_cast<const RECT*>(srcRect),as<IDirect3DSurface9>(dst),static_cast<const RECT*>(dstRect),D3DTEXTUREFILTERTYPE(filter));
}
int32_t getRenderTargetData(Obj dev,Obj renderTarget,Obj dest) { return dev9(dev)->GetRenderTargetData(as<IDirect3DSurface9>(renderTarget),as<IDirect3DSurface9>(dest)); }
int32_t getFrontBufferData(Obj dev,Obj dest) { return dev9(dev)->GetFrontBufferData(0,as<IDirect3DSurface9>(dest)); }
void setGammaRamp(Obj dev,const void* ramp) { dev9(dev)->SetGammaRamp(0,D3DSGR_NO_CALIBRATION,static_cast<const D3DGAMMARAMP*>(ramp)); }
int32_t evictManagedResources(Obj dev) { return dev9(dev)->EvictManagedResources(); }

uint32_t addRef(Obj o) { return o?as<IUnknown>(o)->AddRef():0; }
uint32_t release(Obj o) { return o?as<IUnknown>(o)->Release():0; }
int32_t textureLock(Obj tex,uint32_t level,void* lockedRect,const void* rect,uint32_t flags) { return as<IDirect3DTexture9>(tex)->LockRect(level,static_cast<D3DLOCKED_RECT*>(lockedRect),static_cast<const RECT*>(rect),flags); }
int32_t textureUnlock(Obj tex,uint32_t level) { return as<IDirect3DTexture9>(tex)->UnlockRect(level); }
int32_t textureSurface(Obj tex,uint32_t level,Obj* out) { IDirect3DSurface9* s=nullptr;HRESULT hr=as<IDirect3DTexture9>(tex)->GetSurfaceLevel(level,&s);*out=s;return hr; }
int32_t textureLevelDesc(Obj tex,uint32_t level,void* desc) { return as<IDirect3DTexture9>(tex)->GetLevelDesc(level,static_cast<D3DSURFACE_DESC*>(desc)); }
uint32_t levelCount(Obj tex) { return as<IDirect3DBaseTexture9>(tex)->GetLevelCount(); }
int32_t cubeSurface(Obj tex,uint32_t face,uint32_t level,Obj* out) { IDirect3DSurface9* s=nullptr;HRESULT hr=as<IDirect3DCubeTexture9>(tex)->GetCubeMapSurface(D3DCUBEMAP_FACES(face),level,&s);*out=s;return hr; }
int32_t cubeLevelDesc(Obj tex,uint32_t level,void* desc) { return as<IDirect3DCubeTexture9>(tex)->GetLevelDesc(level,static_cast<D3DSURFACE_DESC*>(desc)); }
int32_t volumeLock(Obj tex,uint32_t level,void* lockedBox,const void* box,uint32_t flags) { return as<IDirect3DVolumeTexture9>(tex)->LockBox(level,static_cast<D3DLOCKED_BOX*>(lockedBox),static_cast<const D3DBOX*>(box),flags); }
int32_t volumeUnlock(Obj tex,uint32_t level) { return as<IDirect3DVolumeTexture9>(tex)->UnlockBox(level); }
int32_t volumeLevelDesc(Obj tex,uint32_t level,void* desc) { return as<IDirect3DVolumeTexture9>(tex)->GetLevelDesc(level,static_cast<D3DVOLUME_DESC*>(desc)); }
int32_t surfaceLock(Obj surf,void* lockedRect,const void* rect,uint32_t flags) { return as<IDirect3DSurface9>(surf)->LockRect(static_cast<D3DLOCKED_RECT*>(lockedRect),static_cast<const RECT*>(rect),flags); }
int32_t surfaceUnlock(Obj surf) { return as<IDirect3DSurface9>(surf)->UnlockRect(); }
int32_t surfaceDesc(Obj surf,void* desc) { return as<IDirect3DSurface9>(surf)->GetDesc(static_cast<D3DSURFACE_DESC*>(desc)); }
int32_t vertexBufferLock(Obj vb,uint32_t offset,uint32_t size,void** data,uint32_t flags) { return as<IDirect3DVertexBuffer9>(vb)->Lock(offset,size,data,flags); }
int32_t vertexBufferUnlock(Obj vb) { return as<IDirect3DVertexBuffer9>(vb)->Unlock(); }
int32_t indexBufferLock(Obj ib,uint32_t offset,uint32_t size,void** data,uint32_t flags) { return as<IDirect3DIndexBuffer9>(ib)->Lock(offset,size,data,flags); }
int32_t indexBufferUnlock(Obj ib) { return as<IDirect3DIndexBuffer9>(ib)->Unlock(); }
int32_t queryIssue(Obj q,uint32_t flags) { return as<IDirect3DQuery9>(q)->Issue(flags); }
int32_t queryGetData(Obj q,void* data,uint32_t size,uint32_t flags) { return as<IDirect3DQuery9>(q)->GetData(data,size,flags); }

// The layouts the front end relies on.
static_assert(sizeof(D3DPRESENT_PARAMETERS)==64&&offsetof(D3DPRESENT_PARAMETERS,hDeviceWindow)==32&&offsetof(D3DPRESENT_PARAMETERS,PresentationInterval)==60);
static_assert(sizeof(D3DSURFACE_DESC)==32&&sizeof(D3DVOLUME_DESC)==28&&sizeof(D3DLOCKED_RECT)==16&&sizeof(D3DLOCKED_BOX)==16&&sizeof(D3DBOX)==24);
static_assert(sizeof(D3DVIEWPORT9)==24&&sizeof(D3DVERTEXELEMENT9)==8&&sizeof(D3DDISPLAYMODE)==16&&sizeof(D3DGAMMARAMP)==1536&&sizeof(RECT)==16);
}
