// libtogl_client.so for Garry's Mod on Linux (x86-64): the Direct3D 9 interface
// of Valve's ToGL (togl_abi.hpp), implemented on DXVK's native d3d9 instead of
// OpenGL, so the game renders through Vulkan.
//
// Most calls map one to one. The parts of ToGL the game inlines are read back
// here: render states arrive as OpenGL calls through the game's entry point
// table (gl_state below), and textures, samplers, shaders, constants, streams
// and the vertex declaration are left in the device and GLMContext members,
// which flush() applies before each draw, as ToGL's FlushDrawStates does.
// Where ToGL deliberately does nothing (fixed function, fog), so does this.
#include "togl_internal.hpp"
#include "backend.hpp"
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace togl;
namespace bk=dxbk;

namespace togl {
void* launcher=nullptr;
void log(const char* format,...) {
 char text[1024];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
 using Msg=void(*)(const char*,...);
 static Msg msg=reinterpret_cast<Msg>(dlsym(RTLD_DEFAULT,"Msg"));
 if(msg)msg("[ToGL/DXVK] %s\n",text);
 fprintf(stderr,"[ToGL/DXVK] %s\n",text);
}
}

namespace {
// Direct3D 9 values the game never passes through ToGL's enums.
constexpr uint32_t D3DERR_INVALIDCALL_=0x8876086C,D3DERR_NOTAVAILABLE_=0x8876086A,D3DERR_NOTFOUND_=0x88760866,E_FAIL_=0x80004005;
constexpr uint32_t D3DUSAGE_RENDERTARGET_=0x1,D3DUSAGE_DEPTHSTENCIL_=0x2,D3DUSAGE_DYNAMIC_=0x200,D3DUSAGE_TEXTURE_SRGB_=0x80000000u;
constexpr uint32_t D3DCREATE_FPU_PRESERVE_=0x2,D3DCREATE_MULTITHREADED_=0x4,D3DCREATE_HARDWARE_VERTEXPROCESSING_=0x40;
constexpr uint32_t D3DTADDRESS_WRAP_=1,D3DTADDRESS_CLAMP_=3,D3DTADDRESS_BORDER_=4;
constexpr uint32_t RS_CULLMODE=22,RS_ZENABLE=7,RS_FILLMODE=8,RS_ZWRITEENABLE=14,RS_ALPHATESTENABLE=15,RS_SRCBLEND=19,RS_DESTBLEND=20,RS_ZFUNC=23,RS_ALPHAREF=24,
 RS_ALPHAFUNC=25,RS_ALPHABLENDENABLE=27,RS_STENCILENABLE=52,RS_STENCILFAIL=53,RS_STENCILZFAIL=54,RS_STENCILPASS=55,RS_STENCILFUNC=56,RS_STENCILREF=57,
 RS_STENCILMASK=58,RS_STENCILWRITEMASK=59,RS_POINTSIZE=154,RS_CLIPPLANEENABLE=152,RS_COLORWRITEENABLE=168,RS_BLENDOP=171,RS_SCISSORTESTENABLE=174,
 RS_SLOPESCALEDEPTHBIAS=175,RS_ADAPTIVETESS_Y=181,RS_CCW_STENCILFAIL=186,RS_CCW_STENCILZFAIL=187,RS_CCW_STENCILPASS=188,RS_CCW_STENCILFUNC=189,
 RS_SRGBWRITEENABLE=194,RS_DEPTHBIAS=195;
constexpr uint32_t SAMP_ADDRESSU=1,SAMP_ADDRESSV=2,SAMP_ADDRESSW=3,SAMP_BORDERCOLOR=4,SAMP_MAGFILTER=5,SAMP_MINFILTER=6,SAMP_MIPFILTER=7,SAMP_MAXMIPLEVEL=9,
 SAMP_MAXANISOTROPY=10,SAMP_SRGBTEXTURE=11;
constexpr uint32_t fourcc(char a,char b,char c,char d) { return uint32_t(uint8_t(a))|uint32_t(uint8_t(b))<<8|uint32_t(uint8_t(c))<<16|uint32_t(uint8_t(d))<<24; }
// OpenGL values the game's inlined state code passes.
constexpr GLenum GL_CW_=0x0900,GL_CCW_=0x0901,GL_FRONT_=0x0404,GL_BACK_=0x0405,GL_FRONT_AND_BACK_=0x0408;
constexpr GLenum GL_DEPTH_TEST_=0x0B71,GL_BLEND_=0x0BE2,GL_CULL_FACE_=0x0B44,GL_ALPHA_TEST_=0x0BC0,GL_STENCIL_TEST_=0x0B90,GL_SCISSOR_TEST_=0x0C11,
 GL_POLYGON_OFFSET_FILL_=0x8037,GL_SAMPLE_ALPHA_TO_COVERAGE_=0x809E,GL_FRAMEBUFFER_SRGB_=0x8DB9,GL_CLIP_PLANE0_=0x3000;
constexpr uint32_t NvidiaVendor=0x10de,AmdVendor=0x1002;

HRESULT hr(int32_t value) { return HRESULT(value); }

// GMod's ToGL numbers D3DFORMAT in its own order (the Source SDK's POSIX
// imageformat.h, checked against its libtogl_client.so); Direct3D's values
// differ except for the vendor FOURCCs.
constexpr uint32_t ToglUnknown=0x31495442;
uint32_t d3dFormat(uint32_t f) {
 static const uint32_t sequential[30]={101,80,75,21,26,22,23,24,25,50,51,28,fourcc('D','X','T','1'),fourcc('D','X','T','3'),fourcc('D','X','T','5'),
  60,63,62,113,36,114,116,20,79,28,23,73,77,100,102};
 if(f<30)return sequential[f];
 if(f==36)return 31;  // A2B10G10R10
 if(f==37)return 35;  // A2R10G10B10
 switch(f){case fourcc('I','N','T','Z'):case fourcc('R','A','W','Z'):case fourcc('N','U','L','L'):case fourcc('D','F','1','6'):case fourcc('D','F','2','4'):
  case fourcc('A','T','I','2'):case fourcc('A','T','I','1'):return f;}
 return 0;
}
D3DFORMAT toglFormat(uint32_t d) {
 switch(d){
  case 101:return D3DFORMAT(0);case 80:return D3DFORMAT(1);case 75:return D3DFORMAT(2);case 21:return D3DFORMAT(3);case 26:return D3DFORMAT(4);case 22:return D3DFORMAT(5);
  case 24:return D3DFORMAT(7);case 25:return D3DFORMAT(8);case 50:return D3DFORMAT(9);case 51:return D3DFORMAT(10);
  case fourcc('D','X','T','1'):return D3DFORMAT(12);case fourcc('D','X','T','3'):return D3DFORMAT(13);case fourcc('D','X','T','5'):return D3DFORMAT(14);
  case 60:return D3DFORMAT(15);case 63:return D3DFORMAT(16);case 62:return D3DFORMAT(17);case 113:return D3DFORMAT(18);case 36:return D3DFORMAT(19);case 114:return D3DFORMAT(20);
  case 116:return D3DFORMAT(21);case 20:return D3DFORMAT(22);case 79:return D3DFORMAT(23);case 28:return D3DFORMAT(24);case 23:return D3DFORMAT(25);case 73:return D3DFORMAT(26);
  case 77:return D3DFORMAT(27);case 100:return D3DFORMAT(28);case 102:return D3DFORMAT(29);case 31:return D3DFORMAT(36);case 35:return D3DFORMAT(37);
  case fourcc('I','N','T','Z'):case fourcc('R','A','W','Z'):case fourcc('N','U','L','L'):case fourcc('D','F','1','6'):case fourcc('D','F','2','4'):
  case fourcc('A','T','I','2'):case fourcc('A','T','I','1'):return D3DFORMAT(d);
  default:return D3DFORMAT(ToglUnknown);
 }
}

// ILauncherMgr calls by GMod's vtable slots (read from launcher_client.so).
void launcherRenderedSize(uint& w,uint& h,bool set) { if(!launcher)return; auto vt=*reinterpret_cast<void***>(launcher);reinterpret_cast<void(*)(void*,uint&,uint&,bool)>(vt[24])(launcher,w,h,set); }
void* launcherWindow() { if(!launcher)return nullptr; auto vt=*reinterpret_cast<void***>(launcher);return reinterpret_cast<void*(*)(void*)>(vt[33])(launcher); }
void launcherGammaRamp(const WORD* r,const WORD* g,const WORD* b) { if(!launcher)return; auto vt=*reinterpret_cast<void***>(launcher);reinterpret_cast<void(*)(void*,const WORD*,const WORD*,const WORD*)>(vt[39])(launcher,r,g,b); }
// The adapter as ToGL describes it to the game: GLMDisplayDB::GetFakeAdapterInfo (vtable slot 4)
// on the launcher's display database (GetDisplayDB, slot 26). The game takes per-vendor
// defaults from its vendor and device (dxsupport.cfg: the HDR tone map scale, for one), so it
// must not see DXVK's GPU; its caps fields decide sRGB and clip plane use. Field offsets of
// GLMRendererInfoFields as GMod's libtogl_client.so reads them (ToGL 2014's layout).
struct AdapterRecord {
 bool valid=false;
 uint32_t vendor=0,device=0,vidMemory=0;int maxAniso=0;
 bool gammaWrites=false,mixedSizes=false,nativeClip=false,cantAttachSRGB=false;
};
const AdapterRecord& launcherAdapter() {
 static AdapterRecord rec;
 if(rec.valid||!launcher)return rec;
 auto vt=*reinterpret_cast<void***>(launcher);
 void* db=reinterpret_cast<void*(*)(void*)>(vt[26])(launcher);
 if(!db)return rec;
 alignas(8) uint8_t renderer[1024]={},display[256]={};int rendererIndex=-1,displayIndex=-1;
 using GetFakeAdapterInfo=bool(*)(void*,int,int*,int*,void*,void*);
 reinterpret_cast<GetFakeAdapterInfo>((*reinterpret_cast<void***>(db))[4])(db,0,&rendererIndex,&displayIndex,renderer,display);
 auto u32=[&](int o){uint32_t v;memcpy(&v,renderer+o,4);return v;};
 rec.vidMemory=u32(0x3c);rec.vendor=u32(0x44);rec.device=u32(0x48);rec.maxAniso=int(u32(0xe8));
 rec.gammaWrites=renderer[0xde];rec.mixedSizes=renderer[0xdf];rec.nativeClip=renderer[0xe2];rec.cantAttachSRGB=renderer[0xff];
 rec.valid=true;
 log("launcher adapter: vendor 0x%x device 0x%x, %u MB, aniso %d, gamma writes %d, mixed sizes %d, native clip %d, no sRGB targets %d",
  rec.vendor,rec.device,rec.vidMemory>>20,rec.maxAniso,rec.gammaWrites,rec.mixedSizes,rec.nativeClip,rec.cantAttachSRGB);
 return rec;
}
// tier0's CommandLine()->CheckParm (ICommandLine slot 3), as ToGL's caps ask for -nouserclip.
bool commandLineHas(const char* parm) {
 auto commandLine=reinterpret_cast<void*(*)()>(dlsym(RTLD_DEFAULT,"CommandLine"));
 void* cl=commandLine?commandLine():nullptr;
 if(!cl)return false;
 return reinterpret_cast<const char*(*)(void*,const char*,const char**)>((*reinterpret_cast<void***>(cl))[3])(cl,parm,nullptr)!=nullptr;
}

struct Device;
Device* g_dev=nullptr;
uint32_t g_vendor=0;
uint32_t g_errors=0;
void reportFailure(const char* what,int32_t result) { if(result<0&&g_errors<200){g_errors++;log("%s failed: 0x%08x",what,uint32_t(result));} }

// Resource objects: ToGL's layout first (the game reads it), then the DXVK object.
struct Surface : IDirect3DSurface9 { bk::Obj real=nullptr; CGLMTex glm{}; ~Surface() override; };
struct Texture2D : IDirect3DTexture9 { bk::Obj real=nullptr; CGLMTex glm{}; ~Texture2D() override; };
struct CubeTexture : IDirect3DCubeTexture9 { bk::Obj real=nullptr; CGLMTex glm{}; ~CubeTexture() override; };
struct VolumeTexture : IDirect3DVolumeTexture9 { bk::Obj real=nullptr; CGLMTex glm{}; ~VolumeTexture() override; };
struct VertexBuffer : IDirect3DVertexBuffer9 { bk::Obj real=nullptr; void* locked=nullptr; uint lockSize=0; ~VertexBuffer() override; };
struct IndexBuffer : IDirect3DIndexBuffer9 { bk::Obj real=nullptr; void* locked=nullptr; uint lockSize=0; ~IndexBuffer() override; };
struct VertexShader : IDirect3DVertexShader9 { bk::Obj real=nullptr; ~VertexShader() override; };
struct PixelShader : IDirect3DPixelShader9 { bk::Obj real=nullptr; ~PixelShader() override; };
struct VertexDecl : IDirect3DVertexDeclaration9 { bk::Obj real=nullptr; ~VertexDecl() override; };
struct Query : IDirect3DQuery9 { bk::Obj real=nullptr; ~Query() override; };
struct D3D9 : IDirect3D9 {};
uint32_t g_texName=1;

bk::Obj realOf(IDirect3DBaseTexture9* t) {
 if(!t)return nullptr;
 switch(t->m_restype){
  case D3DRTYPE_TEXTURE: return static_cast<Texture2D*>(t)->real;
  case D3DRTYPE_CUBETEXTURE: return static_cast<CubeTexture*>(t)->real;
  case D3DRTYPE_VOLUMETEXTURE: return static_cast<VolumeTexture*>(t)->real;
  default: return nullptr;
 }
}
bk::Obj realOf(IDirect3DSurface9* s) { return s?static_cast<Surface*>(s)->real:nullptr; }

struct Device : IDirect3DDevice9 {
 bk::Obj real=nullptr;
 GLMContextLayout* ctx=nullptr;
 bk::Obj nullPixelShader=nullptr;
 // What DXVK has bound (raw DXVK pointers; DXVK holds bound objects alive).
 bk::Obj vs=nullptr,ps=nullptr,decl=nullptr,ib=nullptr;
 struct Stream { bk::Obj vb=nullptr; uint offset=0,stride=0; } streams[D3D_MAX_STREAMS];
 bk::Obj textures[GLM_SAMPLER_COUNT]{};
 GLMTexSamplingParams samplers[GLM_SAMPLER_COUNT]{};
 bool samplerKnown[GLM_SAMPLER_COUNT]{};
 // Render states as set, for a reset; and the GL state that maps to several D3D states.
 uint32_t rs[256]{};
 bool rsSet[256]{};
 bool cullEnabled=false;GLenum frontFace=GL_CCW_;
 uint32_t clipPlanes=0;
 bool depthBiasOn=false;float biasFactor=0,biasUnits=0;int depthBits=24;
 bool skipDraw=false;

 void state(uint32_t s,uint32_t v) { if(rsSet[s]&&rs[s]==v)return; rs[s]=v;rsSet[s]=true; reportFailure("SetRenderState",bk::setRenderState(real,s,v)); }
 void stateFloat(uint32_t s,float v) { uint32_t bits;memcpy(&bits,&v,4);state(s,bits); }
 void forgetBindings() {
  vs=ps=decl=ib=nullptr;
  for(auto& s:streams)s=Stream{};
  for(auto& t:textures)t=nullptr;
  for(auto& k:samplerKnown)k=false;
 }
 void flush();
 void applySampler(uint s);
 void resetCaches();
};
Device* device(IDirect3DDevice9* d) { return static_cast<Device*>(d); }

// The GLMContext the game's inline code works on.
GLMContextLayout* newContext() {
 auto* c=static_cast<GLMContextLayout*>(calloc(1,sizeof(GLMContextLayout)));
 c->m_nCurOwnerThreadId=uint64(pthread_self());
 c->m_bUseSamplerObjects=false;   // SetTexture then marks its sampler dirty
 c->m_hasGammaWrites=true;        // D3DRS_SRGBWRITEENABLE then reaches GL_FRAMEBUFFER_SRGB
 c->m_bUseBoneUniformBuffers=false;
 for(int i=0;i<GLM_SAMPLER_COUNT;i++){
  c->m_nDirtySamplerFlags[i]=1;
  auto& p=c->m_samplers[i].m_samp.m_packed;p.m_addressU=p.m_addressV=p.m_addressW=0;p.m_minFilter=p.m_magFilter=1;p.m_mipFilter=0;p.m_maxAniso=1;p.m_isValid=1;
 }
 for(auto& f:c->m_programParamsF){f.m_firstDirtySlotNonBone=kGLMProgramParamFloat4Limit;f.m_dirtySlotHighWaterNonBone=0;f.m_dirtySlotHighWaterBone=0;}
 static char nullProgram;
 c->m_pNullFragmentProgram=reinterpret_cast<CGLMProgram*>(&nullProgram);
 c->m_bDirtyPrograms=true;
 c->m_nMaxUsedVertexProgramConstantsHint=kGLMProgramParamFloat4Limit;
 return c;
}

void Device::applySampler(uint s) {
 IDirect3DBaseTexture9* t=m_textures[s];
 bk::Obj tex=realOf(t);
 if(tex!=textures[s]){reportFailure("SetTexture",bk::setTexture(real,s,tex));textures[s]=tex;}
 const GLMTexSamplingParams& want=ctx->m_samplers[s].m_samp;
 GLMTexSamplingParams& have=samplers[s];
 bool all=!samplerKnown[s];
 static const uint32_t address[4]={D3DTADDRESS_WRAP_,D3DTADDRESS_CLAMP_,D3DTADDRESS_BORDER_,D3DTADDRESS_WRAP_};
 auto set=[&](uint32_t type,uint32_t value){reportFailure("SetSamplerState",bk::setSamplerState(real,s,type,value));};
 if(all||want.m_packed.m_addressU!=have.m_packed.m_addressU)set(SAMP_ADDRESSU,address[want.m_packed.m_addressU]);
 if(all||want.m_packed.m_addressV!=have.m_packed.m_addressV)set(SAMP_ADDRESSV,address[want.m_packed.m_addressV]);
 if(all||want.m_packed.m_addressW!=have.m_packed.m_addressW)set(SAMP_ADDRESSW,address[want.m_packed.m_addressW]);
 if(all||want.m_packed.m_minFilter!=have.m_packed.m_minFilter)set(SAMP_MINFILTER,want.m_packed.m_minFilter);
 if(all||want.m_packed.m_magFilter!=have.m_packed.m_magFilter)set(SAMP_MAGFILTER,want.m_packed.m_magFilter);
 if(all||want.m_packed.m_mipFilter!=have.m_packed.m_mipFilter)set(SAMP_MIPFILTER,want.m_packed.m_mipFilter);
 if(all||want.m_packed.m_minLOD!=have.m_packed.m_minLOD)set(SAMP_MAXMIPLEVEL,want.m_packed.m_minLOD);
 if(all||want.m_packed.m_maxAniso!=have.m_packed.m_maxAniso)set(SAMP_MAXANISOTROPY,want.m_packed.m_maxAniso?want.m_packed.m_maxAniso:1);
 if(all||want.m_packed.m_srgb!=have.m_packed.m_srgb)set(SAMP_SRGBTEXTURE,want.m_packed.m_srgb);
 if(all||want.m_borderColor!=have.m_borderColor)set(SAMP_BORDERCOLOR,want.m_borderColor);
 // m_compareMode (D3DSAMP_SHADOWFILTER) needs nothing: DXVK compares when a depth format is sampled.
 have=want;samplerKnown[s]=true;
}

// ToGL's FlushDrawStates: push what the game left in the device and context.
void Device::flush() {
 GLMContextLayout* c=ctx;
 if(c->m_bDirtyPrograms){
  c->m_bDirtyPrograms=false;
  bk::Obj v=m_vertexShader?static_cast<VertexShader*>(m_vertexShader)->real:nullptr;
  bk::Obj p=m_pixelShader?static_cast<PixelShader*>(m_pixelShader)->real:nullPixelShader;
  skipDraw=!v||(m_pixelShader&&!p);
  if(v!=vs){reportFailure("SetVertexShader",bk::setVertexShader(real,v));vs=v;}
  if(p!=ps){reportFailure("SetPixelShader",bk::setPixelShader(real,p));ps=p;}
 }
 auto& vf=c->m_programParamsF[kGLMVertexProgram];
 if(vf.m_dirtySlotHighWaterNonBone>vf.m_firstDirtySlotNonBone){
  int first=vf.m_firstDirtySlotNonBone,end=std::min(vf.m_dirtySlotHighWaterNonBone,256);
  reportFailure("SetVertexShaderConstantF",bk::setVertexShaderConstantF(real,first,vf.m_values[first],end-first));
 }
 vf.m_firstDirtySlotNonBone=kGLMProgramParamFloat4Limit;vf.m_dirtySlotHighWaterNonBone=0;vf.m_dirtySlotHighWaterBone=0;
 auto& pf=c->m_programParamsF[kGLMFragmentProgram];
 if(pf.m_dirtySlotHighWaterNonBone>pf.m_firstDirtySlotNonBone){
  int first=pf.m_firstDirtySlotNonBone,end=std::min(pf.m_dirtySlotHighWaterNonBone,224);  // ps_3_0 has 224
  if(end>first)reportFailure("SetPixelShaderConstantF",bk::setPixelShaderConstantF(real,first,pf.m_values[first],end-first));
 }
 pf.m_firstDirtySlotNonBone=kGLMProgramParamFloat4Limit;pf.m_dirtySlotHighWaterNonBone=0;pf.m_dirtySlotHighWaterBone=0;
 auto& vb=c->m_programParamsB[kGLMVertexProgram];
 if(vb.m_dirtySlotCount){reportFailure("SetVertexShaderConstantB",bk::setVertexShaderConstantB(real,0,vb.m_values,std::min(vb.m_dirtySlotCount,16u)));vb.m_dirtySlotCount=0;}
 auto& pb=c->m_programParamsB[kGLMFragmentProgram];
 if(pb.m_dirtySlotCount){reportFailure("SetPixelShaderConstantB",bk::setPixelShaderConstantB(real,0,pb.m_values,std::min(pb.m_dirtySlotCount,16u)));pb.m_dirtySlotCount=0;}
 auto& vi=c->m_programParamsI[kGLMVertexProgram];
 if(vi.m_dirtySlotCount){reportFailure("SetVertexShaderConstantI",bk::setVertexShaderConstantI(real,0,vi.m_values[0],std::min(vi.m_dirtySlotCount,16u)));vi.m_dirtySlotCount=0;}
 for(uint32_t i=0;i<c->m_nNumDirtySamplers;i++){uint s=c->m_nDirtySamplers[i];c->m_nDirtySamplerFlags[s]=1;if(s<GLM_SAMPLER_COUNT)applySampler(s);}
 c->m_nNumDirtySamplers=0;
 bk::Obj d=m_pVertDecl?static_cast<VertexDecl*>(m_pVertDecl)->real:nullptr;
 if(d!=decl){reportFailure("SetVertexDeclaration",bk::setVertexDeclaration(real,d));decl=d;}
 for(int i=0;i<D3D_MAX_STREAMS;i++){
  auto& want=m_streams[i];bk::Obj b=want.m_vtxBuffer?static_cast<VertexBuffer*>(want.m_vtxBuffer)->real:nullptr;
  auto& have=streams[i];
  if(b!=have.vb||(b&&(want.m_offset!=have.offset||want.m_stride!=have.stride))){
   reportFailure("SetStreamSource",bk::setStreamSource(real,i,b,b?want.m_offset:0,b?want.m_stride:0));have={b,want.m_offset,want.m_stride};
  }
 }
 bk::Obj idx=m_indices.m_idxBuffer?static_cast<IndexBuffer*>(m_indices.m_idxBuffer)->real:nullptr;
 if(idx!=ib){reportFailure("SetIndices",bk::setIndices(real,idx));ib=idx;}
}

// After Reset (DXVK restores Direct3D's defaults) everything is pushed again on the next draw.
void Device::resetCaches() {
 forgetBindings();
 GLMContextLayout* c=ctx;
 c->m_bDirtyPrograms=true;
 for(int t=0;t<2;t++){c->m_programParamsF[t].m_firstDirtySlotNonBone=0;c->m_programParamsF[t].m_dirtySlotHighWaterNonBone=t?224:256;}
 c->m_programParamsB[0].m_dirtySlotCount=c->m_programParamsB[1].m_dirtySlotCount=16;c->m_programParamsI[0].m_dirtySlotCount=16;
 c->m_nNumDirtySamplers=0;
 for(uint s=0;s<GLM_SAMPLER_COUNT;s++){c->m_nDirtySamplers[c->m_nNumDirtySamplers++]=uint8(s);c->m_nDirtySamplerFlags[s]=0;}
 bool set[256];memcpy(set,rsSet,sizeof(set));
 for(auto& v:rsSet)v=false;
 for(int s=0;s<256;s++)if(set[s])state(s,rs[s]);
}

Surface* wrapSurface(IDirect3DDevice9* dev,bk::Obj real,D3DRESOURCETYPE type,const CGLMTex* parent,int face,int mip) {
 auto* s=new Surface();s->m_device=dev;s->m_restype=type;s->real=real;
 memset(&s->m_desc,0,sizeof(s->m_desc));
 if(real){bk::surfaceDesc(real,&s->m_desc);s->m_desc.Format=toglFormat(s->m_desc.Format);}
 s->glm=parent?*parent:CGLMTex{g_texName++,0x0DE1};
 s->m_tex=&s->glm;s->m_face=face;s->m_mip=mip;
 return s;
}
}

// ---------------------------------------------------------------- render state from the game's GL calls
// The game's inlined SetRenderState converts Direct3D values to OpenGL; these undo it.
namespace {
uint32_t cmpFromGL(GLenum f) { return f>=0x0200&&f<=0x0207?f-0x0200+1:8; }
uint32_t blendFromGL(GLenum f) {
 switch(f){case 0:return 1;case 1:return 2;case 0x8001:return 14;case 0x8002:return 15;default:return f>=0x0300&&f<=0x0308?f-0x0300+3:2;}
}
uint32_t blendOpFromGL(GLenum e) { switch(e){case 0x800A:return 2;case 0x800B:return 3;case 0x8007:return 4;case 0x8008:return 5;default:return 1;} }
uint32_t stencilOpFromGL(GLenum o) {
 switch(o){case 0x1E00:return 1;case 0:return 2;case 0x1E01:return 3;case 0x1E02:return 4;case 0x1E03:return 5;case 0x150A:return 6;case 0x8507:return 7;case 0x8508:return 8;default:return 1;}
}
void applyCull(Device& d) { d.state(RS_CULLMODE,!d.cullEnabled?1u:d.frontFace==GL_CW_?2u:3u); }
// glPolygonOffset's units are steps of the depth buffer (2^-bits), and the game sets OpenGL
// values (its shadow-map pass uses 20); Direct3D's depth bias is a depth difference, which
// DXVK turns back into steps of the bound depth format. The slope factor means the same in both.
void applyDepthBias(Device& d) {
 d.stateFloat(RS_SLOPESCALEDEPTHBIAS,d.depthBiasOn?d.biasFactor:0.f);
 d.stateFloat(RS_DEPTHBIAS,d.depthBiasOn?std::ldexp(d.biasUnits,-d.depthBits):0.f);
}
int depthBitsOf(uint32_t d3d) {
 constexpr uint32_t DF16=0x36314644;  // MAKEFOURCC('D','F','1','6')
 switch(d3d){case 70:case 80:case DF16:return 16;case 73:return 15;case 71:return 32;default:return 24;}  // D16_LOCKABLE, D16; D15S1; D32
}
void setCap(GLenum cap,bool on) {
 Device* d=g_dev;if(!d)return;
 switch(cap){
  case GL_DEPTH_TEST_: d->state(RS_ZENABLE,on);break;
  case GL_BLEND_: d->state(RS_ALPHABLENDENABLE,on);break;
  case GL_CULL_FACE_: d->cullEnabled=on;applyCull(*d);break;
  case GL_ALPHA_TEST_: d->state(RS_ALPHATESTENABLE,on);break;
  case GL_STENCIL_TEST_: d->state(RS_STENCILENABLE,on);break;
  case GL_SCISSOR_TEST_: d->state(RS_SCISSORTESTENABLE,on);break;
  case GL_POLYGON_OFFSET_FILL_: d->depthBiasOn=on;applyDepthBias(*d);break;
  case GL_FRAMEBUFFER_SRGB_: d->state(RS_SRGBWRITEENABLE,on);break;
  case GL_SAMPLE_ALPHA_TO_COVERAGE_:
   if(g_vendor==AmdVendor)d->state(RS_POINTSIZE,on?fourcc('A','2','M','1'):fourcc('A','2','M','0'));
   else d->state(RS_ADAPTIVETESS_Y,on?fourcc('A','T','O','C'):0u);
   break;
  default:
   if(cap>=GL_CLIP_PLANE0_&&cap<GL_CLIP_PLANE0_+6){uint bit=1u<<(cap-GL_CLIP_PLANE0_);d->clipPlanes=on?d->clipPlanes|bit:d->clipPlanes&~bit;d->state(RS_CLIPPLANEENABLE,d->clipPlanes);}
   break;
 }
}
void glEnable_(GLenum cap) { setCap(cap,true); }
void glDisable_(GLenum cap) { setCap(cap,false); }
void glAlphaFunc_(GLenum func,GLclampf ref) {
 Device* d=g_dev;if(!d)return;
 d->state(RS_ALPHAFUNC,cmpFromGL(func));
 d->state(RS_ALPHAREF,uint32_t(std::lround(std::clamp(ref,0.f,1.f)*255.f)));
}
void glFrontFace_(GLenum mode) { Device* d=g_dev;if(!d)return; d->frontFace=mode;applyCull(*d); }
void glPolygonMode_(GLenum face,GLenum mode) {
 Device* d=g_dev;if(!d||face==GL_BACK_)return;
 d->state(RS_FILLMODE,mode==0x1B00?1u:mode==0x1B01?2u:3u);
}
void glPolygonOffset_(GLfloat factor,GLfloat units) {
 Device* d=g_dev;if(!d)return;
 d->biasFactor=factor;d->biasUnits=units;applyDepthBias(*d);
}
void glDepthFunc_(GLenum func) { Device* d=g_dev;if(d)d->state(RS_ZFUNC,cmpFromGL(func)); }
void glDepthMask_(GLboolean mask) { Device* d=g_dev;if(d)d->state(RS_ZWRITEENABLE,mask!=0); }
void glColorMask_(GLboolean r,GLboolean g,GLboolean b,GLboolean a) {
 Device* d=g_dev;if(d)d->state(RS_COLORWRITEENABLE,(r?1u:0u)|(g?2u:0u)|(b?4u:0u)|(a?8u:0u));
}
void glBlendFunc_(GLenum src,GLenum dst) { Device* d=g_dev;if(!d)return; d->state(RS_SRCBLEND,blendFromGL(src));d->state(RS_DESTBLEND,blendFromGL(dst)); }
void glBlendEquation_(GLenum e) { Device* d=g_dev;if(d)d->state(RS_BLENDOP,blendOpFromGL(e)); }
void glStencilFuncSeparate_(GLenum face,GLenum func,GLint ref,GLuint mask) {
 Device* d=g_dev;if(!d)return;
 if(face!=GL_BACK_){d->state(RS_STENCILFUNC,cmpFromGL(func));d->state(RS_STENCILREF,uint32_t(ref));d->state(RS_STENCILMASK,mask);}
 if(face!=GL_FRONT_)d->state(RS_CCW_STENCILFUNC,cmpFromGL(func));
}
void glStencilOpSeparate_(GLenum face,GLenum sfail,GLenum dpfail,GLenum dppass) {
 Device* d=g_dev;if(!d)return;
 if(face==GL_BACK_){d->state(RS_CCW_STENCILFAIL,stencilOpFromGL(sfail));d->state(RS_CCW_STENCILZFAIL,stencilOpFromGL(dpfail));d->state(RS_CCW_STENCILPASS,stencilOpFromGL(dppass));return;}
 d->state(RS_STENCILFAIL,stencilOpFromGL(sfail));d->state(RS_STENCILZFAIL,stencilOpFromGL(dpfail));d->state(RS_STENCILPASS,stencilOpFromGL(dppass));
}
void glStencilMask_(GLuint mask) { Device* d=g_dev;if(d)d->state(RS_STENCILWRITEMASK,mask); }
// Texture binding happens at draw time from the device's m_textures; ToGL's
// own state writes (clear values, viewport, scissor) never come from the game.
void glIgnore1(GLenum) {}
void glIgnore2(GLenum,GLuint) {}
void glIgnore3(GLenum,GLenum,GLuint) {}
void glIgnoreClearColor(GLclampf,GLclampf,GLclampf,GLclampf) {}
void glIgnoreClearDepth(GLclampd) {}
void glIgnoreClearStencil(GLint) {}
void glIgnoreColorMaskIndexed(GLuint,GLboolean,GLboolean,GLboolean,GLboolean) {}
void glIgnoreRect(GLint,GLint,GLsizei,GLsizei) {}
void glIgnoreDepthRange(GLclampd,GLclampd) {}
void glIgnoreClipPlane(GLenum,const GLdouble*) {}
void glIgnoreBlendColor(GLclampf,GLclampf,GLclampf,GLclampf) {}
GLenum glNoError() { return 0; }
}

void* togl::fakeGL(const char* name) {
 struct Fake { const char* name; void* fn; };
 static const Fake fakes[]={
  {"glEnable",(void*)&glEnable_},{"glDisable",(void*)&glDisable_},{"glAlphaFunc",(void*)&glAlphaFunc_},{"glFrontFace",(void*)&glFrontFace_},
  {"glPolygonMode",(void*)&glPolygonMode_},{"glPolygonOffset",(void*)&glPolygonOffset_},{"glDepthFunc",(void*)&glDepthFunc_},{"glDepthMask",(void*)&glDepthMask_},
  {"glColorMask",(void*)&glColorMask_},{"glBlendFunc",(void*)&glBlendFunc_},{"glBlendEquation",(void*)&glBlendEquation_},
  {"glStencilFuncSeparate",(void*)&glStencilFuncSeparate_},{"glStencilOpSeparate",(void*)&glStencilOpSeparate_},{"glStencilMask",(void*)&glStencilMask_},
  {"glActiveTexture",(void*)&glIgnore1},{"glBindTexture",(void*)&glIgnore2},{"glBindMultiTextureEXT",(void*)&glIgnore3},
  {"glClearColor",(void*)&glIgnoreClearColor},{"glClearDepth",(void*)&glIgnoreClearDepth},{"glClearStencil",(void*)&glIgnoreClearStencil},
  {"glColorMaskIndexedEXT",(void*)&glIgnoreColorMaskIndexed},{"glScissor",(void*)&glIgnoreRect},{"glViewport",(void*)&glIgnoreRect},
  {"glDepthRange",(void*)&glIgnoreDepthRange},{"glClipPlane",(void*)&glIgnoreClipPlane},{"glBlendColor",(void*)&glIgnoreBlendColor},{"glGetError",(void*)&glNoError},
 };
 for(auto& f:fakes)if(!strcmp(f.name,name))return f.fn;
 return nullptr;
}

// ---------------------------------------------------------------- objects
IDirect3DDevice9::IDirect3DDevice9() : m_nValidMarker(0) {}
IDirect3DDevice9::~IDirect3DDevice9() {}
IDirect3DBaseTexture9::~IDirect3DBaseTexture9() {}
IDirect3DTexture9::~IDirect3DTexture9() {}
IDirect3DCubeTexture9::~IDirect3DCubeTexture9() {}
IDirect3DVolumeTexture9::~IDirect3DVolumeTexture9() {}
IDirect3DSurface9::~IDirect3DSurface9() {}
IDirect3D9::~IDirect3D9() {}
IDirect3DVertexDeclaration9::~IDirect3DVertexDeclaration9() {}
IDirect3DQuery9::~IDirect3DQuery9() {}
IDirect3DVertexBuffer9::~IDirect3DVertexBuffer9() {}
IDirect3DIndexBuffer9::~IDirect3DIndexBuffer9() {}
IDirect3DPixelShader9::~IDirect3DPixelShader9() {}
IDirect3DVertexShader9::~IDirect3DVertexShader9() {}

namespace {
// Like ToGL's Released*: a deleted object must not stay referenced by the device.
template<class T> void scrub(T*& slot,const void* obj) { if(slot==obj)slot=nullptr; }
bool live(IDirect3DDevice9* dev) { return dev&&dev==g_dev; }

Surface::~Surface() { bk::release(real); }
Texture2D::~Texture2D() {
 if(live(m_device))for(auto& t:g_dev->m_textures)scrub(t,static_cast<IDirect3DBaseTexture9*>(this));
 if(m_surfZero)m_surfZero->Release(0);
 bk::release(real);
}
CubeTexture::~CubeTexture() {
 if(live(m_device))for(auto& t:g_dev->m_textures)scrub(t,static_cast<IDirect3DBaseTexture9*>(this));
 for(auto& s:m_surfZero)if(s)s->Release(0);
 bk::release(real);
}
VolumeTexture::~VolumeTexture() {
 if(live(m_device))for(auto& t:g_dev->m_textures)scrub(t,static_cast<IDirect3DBaseTexture9*>(this));
 bk::release(real);
}
VertexBuffer::~VertexBuffer() { if(live(m_device))for(auto& s:g_dev->m_streams)if(s.m_vtxBuffer==this){s.m_vtxBuffer=nullptr;s.m_offset=s.m_stride=0;} bk::release(real); }
IndexBuffer::~IndexBuffer() { if(live(m_device))scrub(g_dev->m_indices.m_idxBuffer,this); bk::release(real); }
VertexShader::~VertexShader() { if(live(m_device)){if(g_dev->m_vertexShader==this){g_dev->m_vertexShader=nullptr;g_dev->ctx->m_bDirtyPrograms=true;}} bk::release(real); }
PixelShader::~PixelShader() { if(live(m_device)){if(g_dev->m_pixelShader==this){g_dev->m_pixelShader=nullptr;g_dev->ctx->m_bDirtyPrograms=true;}} bk::release(real); }
VertexDecl::~VertexDecl() { if(live(m_device))scrub(g_dev->m_pVertDecl,this); bk::release(real); }
Query::~Query() { bk::release(real); }
}

// ---------------------------------------------------------------- IDirect3D9
namespace {
// ToGL's CheckDeviceFormat table (dxabstract.cpp), which decides the formats the
// game asks for; a format must also exist in DXVK.
uint32_t toglLegalUsage(uint32_t format) {
 constexpr uint32_t DYN=0x200,AUTOGEN=0x400,FILTER=0x20000,SRGBREAD=0x10000,SRGBWRITE=0x40000,BLEND=0x80000,RT=1,DS=2;
 switch(format){
  case fourcc('D','X','T','1'): case fourcc('D','X','T','3'): case fourcc('D','X','T','5'): return DYN|AUTOGEN|FILTER|SRGBREAD;
  case 21: case 35: case 31: case 114: case 36: return DYN|AUTOGEN|FILTER|RT|SRGBREAD|SRGBWRITE|BLEND;  // A8R8G8B8 A2R10G10B10 A2B10G10R10 R32F A16B16G16R16
  case 113: return DYN|AUTOGEN|RT|SRGBREAD|SRGBWRITE|FILTER|BLEND;   // A16B16G16R16F
  case 116: return DYN|AUTOGEN|RT|SRGBREAD|SRGBWRITE|FILTER|BLEND;   // A32B32G32R32F
  case 23: return DYN|AUTOGEN|FILTER;                                // R5G6B5
  case 20: return DYN|AUTOGEN|FILTER|SRGBREAD;                       // R8G8B8
  case 22: return DYN|AUTOGEN|FILTER|SRGBREAD|SRGBWRITE;             // X8R8G8B8
  case 50: case 51: case 28: case 24: case 26: case 25: case 60: case 63: case 62: return DYN|AUTOGEN|FILTER;  // L8 A8L8 A8 X1R5G5B5 A4R4G4B4 A1R5G5B5 V8U8 Q8W8V8U8 X8L8V8U8
  case 80: case 75: return DYN|RT|DS;                                // D16 D24S8
  default: return 0;  // vendor formats (INTZ, NULL, DF16, DF24, ATI1N, ATI2N) and the rest: unavailable, as in ToGL
 }
}
void fillCaps(D3DCAPS9* c) {
 // GMod's values (FillD3DCaps9 in its libtogl_client.so).
 const AdapterRecord& a=launcherAdapter();
 memset(c,0,sizeof(*c));
 c->DeviceType=D3DDEVTYPE_HAL;c->Caps2=0x20000000;c->DevCaps=0x10000;c->TextureCaps=0x10d00;c->RasterCaps=0x7000000;c->TextureFilterCaps=0x4000400;
 c->MaxTextureWidth=c->MaxTextureHeight=4096;c->MaxVolumeExtent=1024;c->MaxAnisotropy=16;c->TextureOpCaps=0x50;c->VertexProcessingCaps=0x100;c->MaxActiveLights=8;
 c->MaxUserClipPlanes=2;c->MaxPrimitiveCount=0x8000;c->MaxStreams=D3D_MAX_STREAMS;c->VertexShaderVersion=0x300;c->MaxVertexShaderConst=219;c->PixelShaderVersion=0x300;
 c->DevCaps2=1;c->PS20Caps.NumInstructionSlots=512;c->NumSimultaneousRTs=1;
 c->FakeSRGBWrite=0;c->MixedSizeTargets=1;c->CanDoSRGBReadFromRTs=1;
 if(a.valid){
  c->MaxAnisotropy=uint32_t(a.maxAniso);c->MaxUserClipPlanes=a.nativeClip&&!commandLineHas("-nouserclip")?2:0;
  c->FakeSRGBWrite=!a.gammaWrites;c->MixedSizeTargets=a.mixedSizes;c->CanDoSRGBReadFromRTs=!a.cantAttachSRGB;
 }
}
D3D9* g_d3d=nullptr;
}

UINT IDirect3D9::GetAdapterCount() { return 1; }
HRESULT IDirect3D9::GetDeviceCaps(UINT,D3DDEVTYPE,D3DCAPS9* pCaps) { fillCaps(pCaps); return 0; }
HRESULT IDirect3D9::GetAdapterIdentifier(UINT,DWORD,D3DADAPTER_IDENTIFIER9* p) {
 memset(p,0,sizeof(*p));
 bk::AdapterInfo info{};
 if(bk::adapterInfo(info)<0)return hr(int32_t(E_FAIL_));
 snprintf(p->Driver,sizeof(p->Driver),"Vulkan (DXVK)");
 snprintf(p->Description,sizeof(p->Description),"%.480s (Vulkan via DXVK)",info.description);
 memcpy(p->DeviceName,info.deviceName,sizeof(p->DeviceName));
 // The identity ToGL reports (launcherAdapter); DXVK's only without a launcher.
 const AdapterRecord& a=launcherAdapter();
 if(a.valid){p->VendorId=a.vendor;p->DeviceId=a.device;p->VideoMemory=a.vidMemory;}
 else{p->VendorId=info.vendor;p->DeviceId=info.device;p->VideoMemory=0x7FF00000;}
 return 0;
}
HRESULT IDirect3D9::CheckDeviceFormat(UINT,D3DDEVTYPE,D3DFORMAT AdapterFormat,DWORD Usage,D3DRESOURCETYPE RType,D3DFORMAT CheckFormat) {
 uint32_t format=d3dFormat(CheckFormat);
 if(d3dFormat(AdapterFormat)!=22)return hr(int32_t(D3DERR_NOTAVAILABLE_));
 if(RType==D3DRTYPE_SURFACE)return format==75?0:hr(int32_t(D3DERR_NOTAVAILABLE_));
 if(RType!=D3DRTYPE_TEXTURE)return hr(int32_t(D3DERR_NOTAVAILABLE_));
 uint32_t legal=toglLegalUsage(format);
 if(!legal||(Usage&legal)!=Usage)return hr(int32_t(D3DERR_NOTAVAILABLE_));
 // DXVK must have it too (it has no 24-bit R8G8B8, for one); Direct3D does not know DYNAMIC on render targets.
 uint32_t usage=Usage;if(usage&(D3DUSAGE_RENDERTARGET_|D3DUSAGE_DEPTHSTENCIL_))usage&=~D3DUSAGE_DYNAMIC_;
 return bk::checkFormat(22,usage,D3DRTYPE_TEXTURE,format)<0?hr(int32_t(D3DERR_NOTAVAILABLE_)):0;
}
UINT IDirect3D9::GetAdapterModeCount(UINT,D3DFORMAT Format) { return bk::modeCount(d3dFormat(Format)); }
HRESULT IDirect3D9::EnumAdapterModes(UINT,D3DFORMAT Format,UINT Mode,D3DDISPLAYMODE* pMode) {
 int32_t r=bk::enumMode(d3dFormat(Format),Mode,pMode);
 if(r>=0)pMode->Format=Format;
 return hr(r);
}
HRESULT IDirect3D9::CheckDeviceType(UINT,D3DDEVTYPE,D3DFORMAT,D3DFORMAT,BOOL) { return 0; }
HRESULT IDirect3D9::GetAdapterDisplayMode(UINT,D3DDISPLAYMODE* pMode) { int32_t r=bk::displayMode(pMode); if(r>=0)pMode->Format=toglFormat(22); return hr(r); }
HRESULT IDirect3D9::CheckDepthStencilMatch(UINT,D3DDEVTYPE,D3DFORMAT AdapterFormat,D3DFORMAT RenderTargetFormat,D3DFORMAT DepthStencilFormat) {
 return d3dFormat(AdapterFormat)==22&&d3dFormat(RenderTargetFormat)==21&&d3dFormat(DepthStencilFormat)==75?0:hr(int32_t(D3DERR_NOTAVAILABLE_));
}
HRESULT IDirect3D9::CheckDeviceMultiSampleType(UINT,D3DDEVTYPE,D3DFORMAT SurfaceFormat,BOOL Windowed,D3DMULTISAMPLE_TYPE MultiSampleType,DWORD* pQualityLevels) {
 return hr(bk::checkMultisample(d3dFormat(SurfaceFormat),Windowed,MultiSampleType,pQualityLevels));
}

namespace {
// ps_2_0 writing opaque black: ToGL's "null fragment program" for draws without a pixel shader.
const uint32_t nullPixelShaderCode[]={0xFFFF0200,0x05000051,0xA00F0000,0,0,0,0x3F800000,0x02000001,0x800F0800,0xA0E40000,0x0000FFFF};

D3DPRESENT_PARAMETERS presentParams(const D3DPRESENT_PARAMETERS& in,void* window) {
 // The launcher owns the window and its fullscreen state (SDL); DXVK only presents into it.
 D3DPRESENT_PARAMETERS p=in;p.hDeviceWindow=window;p.Windowed=1;p.FullScreen_RefreshRateInHz=0;
 p.BackBufferFormat=D3DFORMAT(d3dFormat(in.BackBufferFormat));p.AutoDepthStencilFormat=D3DFORMAT(d3dFormat(in.AutoDepthStencilFormat));
 if(!p.BackBufferCount)p.BackBufferCount=1;
 return p;
}
void bindDefaultTargets(Device& d) {
 bk::Obj color=nullptr,depth=nullptr;
 reportFailure("GetBackBuffer",bk::getBackBuffer(d.real,&color));
 bk::getDepthStencilSurface(d.real,&depth);
 d.m_pDefaultColorSurface=wrapSurface(&d,color,D3DRTYPE_SURFACE,nullptr,0,0);
 d.m_pDefaultDepthStencilSurface=depth?wrapSurface(&d,depth,D3DRTYPE_SURFACE,nullptr,0,0):nullptr;
 // DXVK binds them itself on creation and reset; mirror ToGL's bookkeeping (private references).
 for(auto& rt:d.m_pRenderTargets){if(rt)rt->Release(1);rt=nullptr;}
 if(d.m_pDepthStencil){d.m_pDepthStencil->Release(1);d.m_pDepthStencil=nullptr;}
 d.m_pRenderTargets[0]=d.m_pDefaultColorSurface;d.m_pRenderTargets[0]->AddRef(1);
 if(d.m_pDefaultDepthStencilSurface){d.m_pDepthStencil=d.m_pDefaultDepthStencilSurface;d.m_pDepthStencil->AddRef(1);d.depthBits=depthBitsOf(d3dFormat(d.m_pDepthStencil->m_desc.Format));}
}
void releaseDefaultTargets(Device& d) {
 for(auto** s:{&d.m_pDefaultColorSurface,&d.m_pDefaultDepthStencilSurface}){
  if(!*s)continue;
  auto* surf=static_cast<Surface*>(*s);bk::release(surf->real);surf->real=nullptr;  // DXVK replaces them on reset
  surf->Release(0);*s=nullptr;
 }
}
}

HRESULT IDirect3D9::CreateDevice(UINT Adapter,D3DDEVTYPE DeviceType,VD3DHWND hFocusWindow,DWORD BehaviorFlags,D3DPRESENT_PARAMETERS* pp,IDirect3DDevice9** out) {
 void* window=launcherWindow();
 if(!window)window=hFocusWindow;
 log("creating the Direct3D 9 device on DXVK: %ux%u, format %d, %d samples, interval 0x%x, window %p (focus %p)",pp->BackBufferWidth,pp->BackBufferHeight,
  int(pp->BackBufferFormat),int(pp->MultiSampleType),pp->PresentationInterval,window,hFocusWindow);
 auto* d=new Device();
 d->m_params.m_adapter=Adapter;d->m_params.m_deviceType=DeviceType;d->m_params.m_focusWindow=hFocusWindow;d->m_params.m_behaviorFlags=BehaviorFlags;d->m_params.m_presentationParameters=*pp;
 D3DPRESENT_PARAMETERS p=presentParams(*pp,window);
 int32_t r=bk::createDevice(window,D3DCREATE_HARDWARE_VERTEXPROCESSING_|D3DCREATE_MULTITHREADED_|D3DCREATE_FPU_PRESERVE_,&p,&d->real);
 if(r<0||!d->real){log("DXVK could not create the device: 0x%08x",uint32_t(r));delete d;*out=nullptr;return hr(r<0?r:int32_t(E_FAIL_));}
 d->ctx=newContext();d->m_ctx=reinterpret_cast<GLMContext*>(d->ctx);
 static char dummyBuffer;d->m_pDummy_vtx_buffer=reinterpret_cast<CGLMBuffer*>(&dummyBuffer);
 for(int i=0;i<4;i++)d->m_vtx_buffers[i]=d->m_pDummy_vtx_buffer;
 reportFailure("CreatePixelShader (null)",bk::createPixelShader(d->real,nullPixelShaderCode,&d->nullPixelShader));
 bindDefaultTargets(*d);
 g_dev=d;
 bk::AdapterInfo info{};if(bk::adapterInfo(info)>=0){g_vendor=info.vendor;log("adapter: %s (vendor 0x%04x, device 0x%04x)",info.description,info.vendor,info.device);}
 uint w=pp->BackBufferWidth,h=pp->BackBufferHeight;launcherRenderedSize(w,h,true);
 *out=d;
 return 0;
}

// ---------------------------------------------------------------- IDirect3DDevice9
HRESULT IDirect3DDevice9::Reset(D3DPRESENT_PARAMETERS* pp) {
 Device& d=*device(this);
 log("reset: %ux%u, %d samples, interval 0x%x",pp->BackBufferWidth,pp->BackBufferHeight,int(pp->MultiSampleType),pp->PresentationInterval);
 m_params.m_presentationParameters=*pp;
 for(auto& rt:m_pRenderTargets){if(rt)rt->Release(1);rt=nullptr;}
 if(m_pDepthStencil){m_pDepthStencil->Release(1);m_pDepthStencil=nullptr;}
 releaseDefaultTargets(d);
 D3DPRESENT_PARAMETERS p=presentParams(*pp,launcherWindow()?launcherWindow():m_params.m_focusWindow);
 int32_t r=bk::reset(d.real,&p);
 reportFailure("Reset",r);
 bindDefaultTargets(d);
 d.resetCaches();
 uint w=pp->BackBufferWidth,h=pp->BackBufferHeight;launcherRenderedSize(w,h,true);
 return r<0?hr(r):0;
}
HRESULT IDirect3DDevice9::SetViewport(const D3DVIEWPORT9* vp) { return hr(bk::setViewport(device(this)->real,vp)); }
HRESULT IDirect3DDevice9::GetViewport(D3DVIEWPORT9* vp) { return hr(bk::getViewport(device(this)->real,vp)); }
HRESULT IDirect3DDevice9::BeginScene() { return hr(bk::beginScene(device(this)->real)); }
HRESULT IDirect3DDevice9::EndScene() { return hr(bk::endScene(device(this)->real)); }
HRESULT IDirect3DDevice9::Clear(DWORD Count,const D3DRECT* pRects,DWORD Flags,D3DCOLOR Color,float Z,DWORD Stencil) {
 Device& d=*device(this);
 // ToGL flushes the fixed state first (FlushDrawStatesNoShaders): the bound targets are current already.
 if(!Count||!pRects)return hr(bk::clear(d.real,0,nullptr,Flags,Color,Z,Stencil));
 bk::Rect32 rects[16];int32_t r=0;
 for(DWORD i=0;i<Count;i+=16){
  DWORD n=std::min<DWORD>(16,Count-i);
  for(DWORD k=0;k<n;k++)rects[k]={int32_t(pRects[i+k].x1),int32_t(pRects[i+k].y1),int32_t(pRects[i+k].x2),int32_t(pRects[i+k].y2)};
  r=bk::clear(d.real,n,rects,Flags,Color,Z,Stencil);
 }
 return hr(r);
}
HRESULT IDirect3DDevice9::Present(const RECT*,const RECT*,VD3DHWND,const RGNDATA*) {
 int32_t r=bk::present(device(this)->real);
 if(r<0)reportFailure("Present",r);
 return 0;
}

namespace {
uint32_t textureUsage(DWORD usage) { return usage&~D3DUSAGE_TEXTURE_SRGB_; }  // ToGL's sRGB storage flag: sRGB is a sampler state in Direct3D
template<class Create> int32_t createWithFallback(const char* what,uint32_t& usage,uint32_t& pool,Create create) {
 int32_t r=create(usage,pool);
 if(r<0&&(usage&D3DUSAGE_DYNAMIC_)&&pool==1){uint32_t u=usage&~D3DUSAGE_DYNAMIC_;r=create(u,pool);if(r>=0)usage=u;}  // ToGL accepts DYNAMIC in the managed pool
 if(r<0&&pool==1&&(usage&(D3DUSAGE_RENDERTARGET_|D3DUSAGE_DEPTHSTENCIL_))){uint32_t p0=0;r=create(usage,p0);if(r>=0)pool=0;}
 reportFailure(what,r);
 return r;
}
}

HRESULT IDirect3DDevice9::CreateTexture(UINT Width,UINT Height,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DTexture9** ppTexture,VD3DHANDLE*,char*) {
 Device& d=*device(this);
 uint32_t usage=textureUsage(Usage),pool=uint32_t(Pool);bk::Obj real=nullptr;
 int32_t r=createWithFallback("CreateTexture",usage,pool,[&](uint32_t u,uint32_t p){return bk::createTexture(d.real,Width,Height,Levels,u,d3dFormat(Format),p,&real);});
 if(r<0){log("  texture %ux%u levels %u usage 0x%x format 0x%x pool %d",Width,Height,Levels,Usage,uint32_t(Format),int(Pool));*ppTexture=nullptr;return hr(r);}
 auto* t=new Texture2D();t->real=real;t->m_device=this;t->m_restype=D3DRTYPE_TEXTURE;
 t->m_descZero={Format,D3DRTYPE_TEXTURE,Usage,Pool,D3DMULTISAMPLE_NONE,0,Width,Height};
 t->glm={g_texName++,0x0DE1};t->m_tex=&t->glm;
 bk::Obj level0=nullptr;bk::textureSurface(real,0,&level0);
 t->m_surfZero=level0?wrapSurface(this,level0,D3DRESOURCETYPE(0),&t->glm,0,0):nullptr;
 *ppTexture=t;return 0;
}
HRESULT IDirect3DDevice9::CreateCubeTexture(UINT EdgeLength,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DCubeTexture9** ppCubeTexture,VD3DHANDLE*,char*) {
 Device& d=*device(this);
 uint32_t usage=textureUsage(Usage),pool=uint32_t(Pool);bk::Obj real=nullptr;
 int32_t r=createWithFallback("CreateCubeTexture",usage,pool,[&](uint32_t u,uint32_t p){return bk::createCubeTexture(d.real,EdgeLength,Levels,u,d3dFormat(Format),p,&real);});
 if(r<0){*ppCubeTexture=nullptr;return hr(r);}
 auto* t=new CubeTexture();t->real=real;t->m_device=this;t->m_restype=D3DRTYPE_CUBETEXTURE;
 t->m_descZero={Format,D3DRTYPE_CUBETEXTURE,Usage,Pool,D3DMULTISAMPLE_NONE,0,EdgeLength,EdgeLength};
 t->glm={g_texName++,0x8513};t->m_tex=&t->glm;
 for(int f=0;f<6;f++){bk::Obj s=nullptr;bk::cubeSurface(real,f,0,&s);t->m_surfZero[f]=s?wrapSurface(this,s,D3DRESOURCETYPE(0),&t->glm,f,0):nullptr;}
 *ppCubeTexture=t;return 0;
}
HRESULT IDirect3DDevice9::CreateVolumeTexture(UINT Width,UINT Height,UINT Depth,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DVolumeTexture9** ppVolumeTexture,VD3DHANDLE*,char*) {
 Device& d=*device(this);
 uint32_t usage=textureUsage(Usage),pool=uint32_t(Pool);bk::Obj real=nullptr;
 int32_t r=createWithFallback("CreateVolumeTexture",usage,pool,[&](uint32_t u,uint32_t p){return bk::createVolumeTexture(d.real,Width,Height,Depth,Levels,u,d3dFormat(Format),p,&real);});
 if(r<0){*ppVolumeTexture=nullptr;return hr(r);}
 auto* t=new VolumeTexture();t->real=real;t->m_device=this;t->m_restype=D3DRTYPE_VOLUMETEXTURE;
 t->m_descZero={Format,D3DRTYPE_VOLUMETEXTURE,Usage,Pool,D3DMULTISAMPLE_NONE,0,Width,Height};
 t->m_volDescZero={Format,D3DRTYPE_VOLUMETEXTURE,Usage,Pool,Width,Height,Depth};
 t->glm={g_texName++,0x806F};t->m_tex=&t->glm;t->m_surfZero=nullptr;
 *ppVolumeTexture=t;return 0;
}
HRESULT IDirect3DDevice9::GetTexture(DWORD Stage,IDirect3DBaseTexture9** ppTexture) {
 if(Stage>=GLM_SAMPLER_COUNT||!ppTexture)return hr(int32_t(D3DERR_INVALIDCALL_));
 *ppTexture=m_textures[Stage];if(*ppTexture)(*ppTexture)->AddRef(0);
 return 0;
}
HRESULT IDirect3DDevice9::CreateRenderTarget(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Lockable,IDirect3DSurface9** ppSurface,VD3DHANDLE*,char*) {
 bk::Obj real=nullptr;
 int32_t r=bk::createRenderTarget(device(this)->real,Width,Height,d3dFormat(Format),MultiSample,MultisampleQuality,Lockable,&real);
 reportFailure("CreateRenderTarget",r);
 if(r<0){*ppSurface=nullptr;return hr(r);}
 *ppSurface=wrapSurface(this,real,D3DRTYPE_SURFACE,nullptr,0,0);return 0;
}
HRESULT IDirect3DDevice9::SetRenderTarget(DWORD index,IDirect3DSurface9* surface) {
 if(index>=4)return hr(int32_t(D3DERR_INVALIDCALL_));
 IDirect3DSurface9* old=m_pRenderTargets[index];
 if(surface==old)return 0;
 if(surface)surface->AddRef(1);
 m_pRenderTargets[index]=surface;
 bk::Obj real=realOf(surface);
 if(index==0&&!real){
  static int warned;if(warned++<4)log("SetRenderTarget(0, null) kept the previous color target bound (Direct3D 9 needs one)");
 }else reportFailure("SetRenderTarget",bk::setRenderTarget(device(this)->real,index,real));
 if(old)old->Release(1);
 return 0;
}
HRESULT IDirect3DDevice9::GetRenderTarget(DWORD index,IDirect3DSurface9** ppRenderTarget) {
 if(index>=4||!ppRenderTarget)return hr(int32_t(D3DERR_INVALIDCALL_));
 if(!m_pRenderTargets[index])return hr(int32_t(D3DERR_NOTFOUND_));
 m_pRenderTargets[index]->AddRef(0);*ppRenderTarget=m_pRenderTargets[index];
 return 0;
}
HRESULT IDirect3DDevice9::CreateOffscreenPlainSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DPOOL Pool,IDirect3DSurface9** ppSurface,VD3DHANDLE*) {
 bk::Obj real=nullptr;
 int32_t r=bk::createOffscreenPlainSurface(device(this)->real,Width,Height,d3dFormat(Format),Pool,&real);
 reportFailure("CreateOffscreenPlainSurface",r);
 if(r<0){*ppSurface=nullptr;return hr(r);}
 *ppSurface=wrapSurface(this,real,D3DRTYPE_SURFACE,nullptr,0,0);return 0;
}
HRESULT IDirect3DDevice9::CreateDepthStencilSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Discard,IDirect3DSurface9** ppSurface,VD3DHANDLE*) {
 bk::Obj real=nullptr;
 int32_t r=bk::createDepthStencilSurface(device(this)->real,Width,Height,d3dFormat(Format),MultiSample,MultisampleQuality,Discard,&real);
 reportFailure("CreateDepthStencilSurface",r);
 if(r<0){*ppSurface=nullptr;return hr(r);}
 *ppSurface=wrapSurface(this,real,D3DRTYPE_SURFACE,nullptr,0,0);return 0;
}
HRESULT IDirect3DDevice9::SetDepthStencilSurface(IDirect3DSurface9* surface) {
 if(surface==m_pDepthStencil)return 0;
 if(surface)surface->AddRef(1);
 IDirect3DSurface9* old=m_pDepthStencil;m_pDepthStencil=surface;
 Device& d=*device(this);
 reportFailure("SetDepthStencilSurface",bk::setDepthStencilSurface(d.real,realOf(surface)));
 if(surface){int bits=depthBitsOf(d3dFormat(surface->m_desc.Format));if(bits!=d.depthBits){d.depthBits=bits;applyDepthBias(d);}}
 if(old)old->Release(1);
 return 0;
}
HRESULT IDirect3DDevice9::GetDepthStencilSurface(IDirect3DSurface9** pp) {
 if(!pp)return hr(int32_t(D3DERR_INVALIDCALL_));
 if(!m_pDepthStencil){*pp=nullptr;return hr(int32_t(D3DERR_NOTFOUND_));}
 m_pDepthStencil->AddRef(0);*pp=m_pDepthStencil;
 return 0;
}
HRESULT IDirect3DDevice9::GetRenderTargetData(IDirect3DSurface9* src,IDirect3DSurface9* dst) {
 Device& d=*device(this);
 int32_t r=bk::getRenderTargetData(d.real,realOf(src),realOf(dst));
 if(r<0)r=bk::stretchRect(d.real,realOf(src),nullptr,realOf(dst),nullptr,0);  // ToGL's way (a blit)
 reportFailure("GetRenderTargetData",r);
 return 0;
}
HRESULT IDirect3DDevice9::GetFrontBufferData(UINT,IDirect3DSurface9* dst) { reportFailure("GetFrontBufferData",bk::getFrontBufferData(device(this)->real,realOf(dst))); return 0; }
HRESULT IDirect3DDevice9::StretchRect(IDirect3DSurface9* src,const RECT* srcRect,IDirect3DSurface9* dst,const RECT* dstRect,D3DTEXTUREFILTERTYPE Filter) {
 if(!realOf(src)||!realOf(dst))return 0;
 reportFailure("StretchRect",bk::stretchRect(device(this)->real,realOf(src),srcRect,realOf(dst),dstRect,Filter));
 return 0;
}

HRESULT IDirect3DDevice9::CreatePixelShader(const DWORD* pFunction,IDirect3DPixelShader9** ppShader,const char* pShaderName,char*,const uint32*) {
 auto* s=new PixelShader();s->m_device=this;s->m_restype=D3DRESOURCETYPE(0);s->m_pixProgram=reinterpret_cast<CGLMProgram*>(s);
 int32_t r=bk::createPixelShader(device(this)->real,pFunction,&s->real);
 if(r<0){reportFailure("CreatePixelShader",r);log("  pixel shader %s (version token 0x%08x) is not drawn",pShaderName?pShaderName:"?",pFunction?pFunction[0]:0);}
 *ppShader=s;return 0;
}
HRESULT IDirect3DDevice9::SetPixelShaderConstantB(UINT StartRegister,const BOOL* data,UINT BoolCount) { return hr(bk::setPixelShaderConstantB(device(this)->real,StartRegister,data,BoolCount)); }
HRESULT IDirect3DDevice9::SetPixelShaderConstantI(UINT StartRegister,const int* data,UINT Vector4iCount) { return hr(bk::setPixelShaderConstantI(device(this)->real,StartRegister,data,Vector4iCount)); }
HRESULT IDirect3DDevice9::CreateVertexShader(const DWORD* pFunction,IDirect3DVertexShader9** ppShader,const char* pShaderName,char*) {
 auto* s=new VertexShader();s->m_device=this;s->m_restype=D3DRESOURCETYPE(0);s->m_vtxProgram=reinterpret_cast<CGLMProgram*>(s);
 int32_t r=bk::createVertexShader(device(this)->real,pFunction,&s->real);
 if(r<0){reportFailure("CreateVertexShader",r);log("  vertex shader %s (version token 0x%08x) is not drawn",pShaderName?pShaderName:"?",pFunction?pFunction[0]:0);}
 *ppShader=s;return 0;
}
// The GLSL pair cache ToGL keeps (glshaders.cfg) has no counterpart: DXVK caches pipelines itself.
HRESULT IDirect3DDevice9::LinkShaderPair(IDirect3DVertexShader9*,IDirect3DPixelShader9*) { return 0; }
HRESULT IDirect3DDevice9::QueryShaderPair(int,GLMShaderPairInfo* info) { memset(info,0,sizeof(*info));info->m_status=-1;return 0; }
HRESULT IDirect3DDevice9::CreateVertexDeclaration(const D3DVERTEXELEMENT9* elements,IDirect3DVertexDeclaration9** ppDecl) {
 auto* d=new VertexDecl();d->m_device=this;d->m_elemCount=0;
 memset(d->m_elements,0,sizeof(d->m_elements));memset(d->m_VertexAttribDescToStreamIndex,0xFF,sizeof(d->m_VertexAttribDescToStreamIndex));
 for(uint i=0;i<MAX_D3DVERTEXELEMENTS&&elements[i].Stream!=0xFF;i++){d->m_elements[i].m_dxdecl=elements[i];d->m_elemCount++;}
 // GMod's Linux engine writes D3DCOLOR vertex data in R,G,B,A byte order, which
 // ToGL reads unswizzled (4 normalized bytes): UBYTE4N is that in Direct3D.
 D3DVERTEXELEMENT9 adjusted[MAX_D3DVERTEXELEMENTS+1];
 for(uint i=0;i<=d->m_elemCount&&i<=MAX_D3DVERTEXELEMENTS;i++){adjusted[i]=elements[i];if(adjusted[i].Stream!=0xFF&&adjusted[i].Type==4)adjusted[i].Type=8;}
 int32_t r=bk::createVertexDeclaration(device(this)->real,adjusted,&d->real);
 reportFailure("CreateVertexDeclaration",r);
 *ppDecl=d;return 0;
}
HRESULT IDirect3DDevice9::SetFVF(DWORD) { return 0; }
HRESULT IDirect3DDevice9::GetFVF(DWORD* pFVF) { if(pFVF)*pFVF=0; return 0; }
HRESULT IDirect3DDevice9::CreateVertexBuffer(UINT Length,DWORD Usage,DWORD FVF,D3DPOOL Pool,IDirect3DVertexBuffer9** ppVertexBuffer,VD3DHANDLE*) {
 Device& d=*device(this);
 uint32_t usage=Usage,pool=uint32_t(Pool);bk::Obj real=nullptr;
 int32_t r=createWithFallback("CreateVertexBuffer",usage,pool,[&](uint32_t u,uint32_t p){return bk::createVertexBuffer(d.real,Length,u,FVF,p,&real);});
 if(r<0){*ppVertexBuffer=nullptr;return hr(r);}
 auto* b=new VertexBuffer();b->real=real;b->m_device=this;b->m_restype=D3DRTYPE_VERTEXBUFFER;b->m_ctx=m_ctx;b->m_vtxBuffer=reinterpret_cast<CGLMBuffer*>(b);
 b->m_vtxDesc={D3DFORMAT(28),D3DRTYPE_VERTEXBUFFER,Usage,Pool,Length,FVF};  // D3DFMT_VERTEXDATA
 *ppVertexBuffer=b;return 0;
}
HRESULT IDirect3DDevice9::CreateIndexBuffer(UINT Length,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DIndexBuffer9** ppIndexBuffer,VD3DHANDLE*) {
 Device& d=*device(this);
 uint32_t usage=Usage,pool=uint32_t(Pool);bk::Obj real=nullptr;
 int32_t r=createWithFallback("CreateIndexBuffer",usage,pool,[&](uint32_t u,uint32_t p){return bk::createIndexBuffer(d.real,Length,u,d3dFormat(Format),p,&real);});
 if(r<0){*ppIndexBuffer=nullptr;return hr(r);}
 auto* b=new IndexBuffer();b->real=real;b->m_device=this;b->m_restype=D3DRTYPE_INDEXBUFFER;b->m_ctx=m_ctx;b->m_idxBuffer=reinterpret_cast<CGLMBuffer*>(b);
 b->m_idxDesc={Format,D3DRTYPE_INDEXBUFFER,Usage,Pool,Length};
 *ppIndexBuffer=b;return 0;
}

// The same switch as ToGL's SetRenderState (and the game's inlined copy of it):
// the device's gl mirror is updated and the state goes the same way as an inlined call.
HRESULT IDirect3DDevice9::SetRenderState(D3DRENDERSTATETYPE State,DWORD Value) {
 auto glcmp=[](DWORD v)->GLenum{return v>=1&&v<=8?0x0200+v-1:0x0207;};
 auto glblend=[](DWORD v)->GLenum{static const GLenum m[]={0,0,1,0x0300,0x0301,0x0302,0x0303,0x0304,0x0305,0x0306,0x0307,0x0308};return v<12?m[v]:0xFFFFFFFF;};
 auto glstencil=[](DWORD v)->GLenum{static const GLenum m[]={0x1E00,0x1E00,0,0x1E01,0x1E02,0x1E03,0x150A,0x8507,0x8508};return v<9?m[v]:0x1E00;};
 switch(int(State)){
  case RS_ZENABLE: gl.m_DepthTestEnable.enable=Value;setCap(GL_DEPTH_TEST_,Value!=0);break;
  case RS_ZWRITEENABLE: gl.m_DepthMask.mask=char(Value);glDepthMask_(GLboolean(Value!=0));break;
  case RS_ZFUNC: gl.m_DepthFunc.func=glcmp(Value);glDepthFunc_(gl.m_DepthFunc.func);break;
  case RS_COLORWRITEENABLE:
   gl.m_ColorMaskSingle={char(Value&1?0xFF:0),char(Value&2?0xFF:0),char(Value&4?0xFF:0),char(Value&8?0xFF:0)};
   glColorMask_(Value&1,Value&2,Value&4,Value&8);break;
  case RS_CULLMODE:
   gl.m_CullFaceEnable.enable=Value!=1;gl.m_CullFrontFace.value=Value==2?GL_CW_:GL_CCW_;
   setCap(GL_CULL_FACE_,Value!=1);glFrontFace_(gl.m_CullFrontFace.value);break;
  case RS_ALPHABLENDENABLE: gl.m_BlendEnable.enable=Value;setCap(GL_BLEND_,Value!=0);break;
  case RS_BLENDOP: {static const GLenum m[]={0x8006,0x8006,0x800A,0x800B,0x8007,0x8008};gl.m_BlendEquation.equation=Value<6?m[Value]:0x8006;glBlendEquation_(gl.m_BlendEquation.equation);break;}
  case RS_SRCBLEND: case RS_DESTBLEND:
   (State==RS_SRCBLEND?gl.m_BlendFactor.srcfactor:gl.m_BlendFactor.dstfactor)=glblend(Value);
   glBlendFunc_(gl.m_BlendFactor.srcfactor,gl.m_BlendFactor.dstfactor);break;
  case RS_SRGBWRITEENABLE: gl.m_BlendEnableSRGB.enable=Value;setCap(GL_FRAMEBUFFER_SRGB_,Value!=0);break;
  case RS_ALPHATESTENABLE: gl.m_AlphaTestEnable.enable=Value;setCap(GL_ALPHA_TEST_,Value!=0);break;
  case RS_ALPHAREF: gl.m_AlphaTestFunc.ref=Value/255.f;glAlphaFunc_(gl.m_AlphaTestFunc.func?gl.m_AlphaTestFunc.func:0x0207,gl.m_AlphaTestFunc.ref);break;
  case RS_ALPHAFUNC: gl.m_AlphaTestFunc.func=glcmp(Value);glAlphaFunc_(gl.m_AlphaTestFunc.func,gl.m_AlphaTestFunc.ref);break;
  case RS_STENCILENABLE: gl.m_StencilTestEnable.enable=Value;setCap(GL_STENCIL_TEST_,Value!=0);break;
  case RS_STENCILFAIL: case RS_STENCILZFAIL: case RS_STENCILPASS:
   (State==RS_STENCILFAIL?gl.m_StencilOp.sfail:State==RS_STENCILZFAIL?gl.m_StencilOp.dpfail:gl.m_StencilOp.dppass)=glstencil(Value);
   glStencilOpSeparate_(GL_FRONT_,gl.m_StencilOp.sfail?gl.m_StencilOp.sfail:0x1E00,gl.m_StencilOp.dpfail?gl.m_StencilOp.dpfail:0x1E00,gl.m_StencilOp.dppass?gl.m_StencilOp.dppass:0x1E00);break;
  case RS_STENCILFUNC: gl.m_StencilFunc.frontfunc=gl.m_StencilFunc.backfunc=glcmp(Value);glStencilFuncSeparate_(GL_FRONT_AND_BACK_,gl.m_StencilFunc.frontfunc,gl.m_StencilFunc.ref,gl.m_StencilFunc.mask);break;
  case RS_STENCILREF: gl.m_StencilFunc.ref=GLint(Value);glStencilFuncSeparate_(GL_FRONT_AND_BACK_,gl.m_StencilFunc.frontfunc?gl.m_StencilFunc.frontfunc:0x0207,gl.m_StencilFunc.ref,gl.m_StencilFunc.mask);break;
  case RS_STENCILMASK: gl.m_StencilFunc.mask=Value;glStencilFuncSeparate_(GL_FRONT_AND_BACK_,gl.m_StencilFunc.frontfunc?gl.m_StencilFunc.frontfunc:0x0207,gl.m_StencilFunc.ref,gl.m_StencilFunc.mask);break;
  case RS_STENCILWRITEMASK: gl.m_StencilWriteMask.mask=GLint(Value);glStencilMask_(Value);break;
  case 28: gl.m_FogEnable=Value!=0;break;  // D3DRS_FOGENABLE: latched, never applied (as ToGL)
  case RS_SCISSORTESTENABLE: gl.m_ScissorEnable.enable=Value;setCap(GL_SCISSOR_TEST_,Value!=0);break;
  case RS_DEPTHBIAS: case RS_SLOPESCALEDEPTHBIAS: {
   float f;memcpy(&f,&Value,4);
   (State==RS_DEPTHBIAS?gl.m_DepthBias.units:gl.m_DepthBias.factor)=f;
   setCap(GL_POLYGON_OFFSET_FILL_,gl.m_DepthBias.factor!=0||gl.m_DepthBias.units!=0);glPolygonOffset_(gl.m_DepthBias.factor,gl.m_DepthBias.units);break;
  }
  case RS_ADAPTIVETESS_Y: gl.m_AlphaToCoverageEnable.enable=GLint(Value);setCap(GL_SAMPLE_ALPHA_TO_COVERAGE_,Value!=0);break;
  case RS_CLIPPLANEENABLE:
   for(int i=0;i<kGLMUserClipPlanes;i++){gl.m_ClipPlaneEnable[i].enable=(Value>>i)&1;setCap(GL_CLIP_PLANE0_+i,(Value>>i)&1);}
   break;
  case RS_FILLMODE: {GLenum mode=Value==1?0x1B00:Value==2?0x1B01:0x1B02;gl.m_PolygonMode.values[0]=gl.m_PolygonMode.values[1]=mode;glPolygonMode_(GL_FRONT_,mode);break;}
  default: break;  // everything else is ignored by ToGL too
 }
 return 0;
}

HRESULT IDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE type,UINT start,UINT count) {
 Device& d=*device(this);
 if(!m_vertexShader)return 0;
 d.flush();if(d.skipDraw)return 0;
 reportFailure("DrawPrimitive",bk::drawPrimitive(d.real,type,start,count));
 return 0;
}
HRESULT IDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE type,INT base,UINT minIndex,UINT numVertices,UINT startIndex,UINT primCount) {
 Device& d=*device(this);
 if(!m_indices.m_idxBuffer||!m_vertexShader)return hr(int32_t(E_FAIL_));
 if(type!=D3DPT_LINELIST&&type!=D3DPT_TRIANGLELIST&&type!=D3DPT_TRIANGLESTRIP)return 0;  // ToGL draws only these
 d.flush();if(d.skipDraw)return 0;
 reportFailure("DrawIndexedPrimitive",bk::drawIndexedPrimitive(d.real,type,base,minIndex,numVertices,startIndex,primCount));
 return 0;
}
HRESULT IDirect3DDevice9::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type,UINT minIndex,UINT numVertices,UINT primCount,const void* indices,D3DFORMAT indexFormat,const void* vertices,UINT stride) {
 Device& d=*device(this);
 if(!m_vertexShader)return 0;
 d.flush();if(d.skipDraw)return 0;
 reportFailure("DrawIndexedPrimitiveUP",bk::drawIndexedPrimitiveUP(d.real,type,minIndex,numVertices,primCount,indices,d3dFormat(indexFormat),vertices,stride));
 // Direct3D unbinds stream 0 and the indices after a UP draw.
 d.streams[0]=Device::Stream{};d.ib=nullptr;
 return 0;
}
BOOL IDirect3DDevice9::ShowCursor(BOOL) { return 1; }
HRESULT IDirect3DDevice9::ValidateDevice(DWORD* pNumPasses) { if(pNumPasses)*pNumPasses=1; return 0; }
HRESULT IDirect3DDevice9::SetMaterial(const D3DMATERIAL9*) { return 0; }
HRESULT IDirect3DDevice9::LightEnable(DWORD,BOOL) { return 0; }
HRESULT IDirect3DDevice9::SetScissorRect(const RECT* pRect) {
 gl.m_ScissorBox={pRect->left,pRect->top,pRect->right-pRect->left,pRect->bottom-pRect->top};
 return hr(bk::setScissorRect(device(this)->real,pRect));
}
HRESULT IDirect3DDevice9::CreateQuery(D3DQUERYTYPE Type,IDirect3DQuery9** ppQuery) {
 bk::Obj real=nullptr;int32_t r=bk::createQuery(device(this)->real,Type,&real);
 if(r<0){reportFailure("CreateQuery",r);*ppQuery=nullptr;return hr(r);}
 auto* q=new Query();q->real=real;q->m_device=this;q->m_restype=D3DRESOURCETYPE(0);q->m_type=Type;q->m_ctx=m_ctx;q->m_query=nullptr;
 *ppQuery=q;return 0;
}
HRESULT IDirect3DDevice9::GetDeviceCaps(D3DCAPS9* pCaps) { fillCaps(pCaps); return 0; }
HRESULT IDirect3DDevice9::TestCooperativeLevel() { return 0; }  // never lost, as with ToGL
HRESULT IDirect3DDevice9::EvictManagedResources() { return 0; }
HRESULT IDirect3DDevice9::SetLight(DWORD,const D3DLIGHT9*) { return 0; }
void IDirect3DDevice9::SetGammaRamp(UINT,DWORD,const D3DGAMMARAMP* pRamp) { if(pRamp)launcherGammaRamp(pRamp->red,pRamp->green,pRamp->blue); }
HRESULT IDirect3DDevice9::SetClipPlane(DWORD Index,const float* pPlane) {
 if(Index<kGLMUserClipPlanes)gl.m_ClipPlaneEquation[Index]={pPlane[0],pPlane[1],pPlane[2],pPlane[3]};
 return hr(bk::setClipPlane(device(this)->real,Index,pPlane));
}
HRESULT IDirect3DDevice9::SetTransform(D3DTRANSFORMSTATETYPE,const D3DMATRIX*) { return 0; }
HRESULT IDirect3DDevice9::SetTextureStageState(DWORD,D3DTEXTURESTAGESTATETYPE,DWORD) { return 0; }
void IDirect3DDevice9::AcquireThreadOwnership() { device(this)->ctx->m_nCurOwnerThreadId=uint64(pthread_self()); }
void IDirect3DDevice9::ReleaseThreadOwnership() { device(this)->ctx->m_nCurOwnerThreadId=0; }

// ---------------------------------------------------------------- resources
DWORD IDirect3DResource9::SetPriority(DWORD) { return 0; }
D3DRESOURCETYPE IDirect3DBaseTexture9::GetType() { return m_restype; }
DWORD IDirect3DBaseTexture9::GetLevelCount() { return bk::levelCount(realOf(this)); }
HRESULT IDirect3DBaseTexture9::GetLevelDesc(UINT Level,D3DSURFACE_DESC* pDesc) {
 if(m_restype==D3DRTYPE_CUBETEXTURE)return static_cast<IDirect3DCubeTexture9*>(this)->GetLevelDesc(Level,pDesc);
 if(m_restype==D3DRTYPE_VOLUMETEXTURE){
  D3DVOLUME_DESC v{};HRESULT r=static_cast<IDirect3DVolumeTexture9*>(this)->GetLevelDesc(Level,&v);
  *pDesc={v.Format,v.Type,v.Usage,v.Pool,D3DMULTISAMPLE_NONE,0,v.Width,v.Height};return r;
 }
 int32_t r=bk::textureLevelDesc(realOf(this),Level,pDesc);
 if(r>=0){pDesc->Format=m_descZero.Format;pDesc->Usage=m_descZero.Usage;}  // ToGL's numbering and usage bits (D3DUSAGE_TEXTURE_SRGB)
 return hr(r);
}
HRESULT IDirect3DTexture9::LockRect(UINT Level,D3DLOCKED_RECT* pLockedRect,const RECT* pRect,DWORD Flags) { return hr(bk::textureLock(static_cast<Texture2D*>(this)->real,Level,pLockedRect,pRect,Flags)); }
HRESULT IDirect3DTexture9::UnlockRect(UINT Level) { return hr(bk::textureUnlock(static_cast<Texture2D*>(this)->real,Level)); }
HRESULT IDirect3DTexture9::GetSurfaceLevel(UINT Level,IDirect3DSurface9** pp) {
 // A new surface the caller releases, as ToGL hands out.
 auto* t=static_cast<Texture2D*>(this);bk::Obj s=nullptr;
 int32_t r=bk::textureSurface(t->real,Level,&s);
 if(r<0){*pp=nullptr;return hr(r);}
 *pp=wrapSurface(m_device,s,D3DRESOURCETYPE(0),&t->glm,0,int(Level));return 0;
}
HRESULT IDirect3DCubeTexture9::GetCubeMapSurface(D3DCUBEMAP_FACES FaceType,UINT Level,IDirect3DSurface9** pp) {
 auto* t=static_cast<CubeTexture*>(this);bk::Obj s=nullptr;
 int32_t r=bk::cubeSurface(t->real,uint32_t(FaceType),Level,&s);
 if(r<0){*pp=nullptr;return hr(r);}
 *pp=wrapSurface(m_device,s,D3DRESOURCETYPE(0),&t->glm,int(FaceType),int(Level));return 0;
}
HRESULT IDirect3DCubeTexture9::GetLevelDesc(UINT Level,D3DSURFACE_DESC* pDesc) {
 int32_t r=bk::cubeLevelDesc(static_cast<CubeTexture*>(this)->real,Level,pDesc);
 if(r>=0){pDesc->Format=m_descZero.Format;pDesc->Usage=m_descZero.Usage;}
 return hr(r);
}
HRESULT IDirect3DVolumeTexture9::LockBox(UINT Level,D3DLOCKED_BOX* pLockedVolume,const D3DBOX* pBox,DWORD Flags) { return hr(bk::volumeLock(static_cast<VolumeTexture*>(this)->real,Level,pLockedVolume,pBox,Flags)); }
HRESULT IDirect3DVolumeTexture9::UnlockBox(UINT Level) { return hr(bk::volumeUnlock(static_cast<VolumeTexture*>(this)->real,Level)); }
HRESULT IDirect3DVolumeTexture9::GetLevelDesc(UINT Level,D3DVOLUME_DESC* pDesc) {
 int32_t r=bk::volumeLevelDesc(static_cast<VolumeTexture*>(this)->real,Level,pDesc);
 if(r>=0){pDesc->Format=m_descZero.Format;pDesc->Usage=m_descZero.Usage;}
 return hr(r);
}
HRESULT IDirect3DSurface9::LockRect(D3DLOCKED_RECT* pLockedRect,const RECT* pRect,DWORD Flags) { return hr(bk::surfaceLock(static_cast<Surface*>(this)->real,pLockedRect,pRect,Flags)); }
HRESULT IDirect3DSurface9::UnlockRect() { return hr(bk::surfaceUnlock(static_cast<Surface*>(this)->real)); }
HRESULT IDirect3DSurface9::GetDesc(D3DSURFACE_DESC* pDesc) { *pDesc=m_desc; return 0; }

HRESULT IDirect3DVertexBuffer9::Lock(UINT OffsetToLock,UINT SizeToLock,void** ppbData,DWORD Flags) {
 auto* b=static_cast<VertexBuffer*>(this);
 int32_t r=bk::vertexBufferLock(b->real,OffsetToLock,SizeToLock,ppbData,Flags);
 b->locked=r>=0?*ppbData:nullptr;b->lockSize=SizeToLock?SizeToLock:m_vtxDesc.Size-OffsetToLock;
 return hr(r);
}
HRESULT IDirect3DVertexBuffer9::Unlock() { auto* b=static_cast<VertexBuffer*>(this);b->locked=nullptr;return hr(bk::vertexBufferUnlock(b->real)); }
// ToGL's partial unlock: only nActualSize bytes count, optionally copied from pActualData.
void IDirect3DVertexBuffer9::UnlockActualSize(uint nActualSize,const void* pActualData) {
 auto* b=static_cast<VertexBuffer*>(this);
 if(pActualData&&b->locked)memcpy(b->locked,pActualData,std::min(nActualSize,b->lockSize));
 Unlock();
}
HRESULT IDirect3DIndexBuffer9::Lock(UINT OffsetToLock,UINT SizeToLock,void** ppbData,DWORD Flags) {
 auto* b=static_cast<IndexBuffer*>(this);
 int32_t r=bk::indexBufferLock(b->real,OffsetToLock,SizeToLock,ppbData,Flags);
 b->locked=r>=0?*ppbData:nullptr;b->lockSize=SizeToLock?SizeToLock:m_idxDesc.Size-OffsetToLock;
 return hr(r);
}
HRESULT IDirect3DIndexBuffer9::Unlock() { auto* b=static_cast<IndexBuffer*>(this);b->locked=nullptr;return hr(bk::indexBufferUnlock(b->real)); }
void IDirect3DIndexBuffer9::UnlockActualSize(uint nActualSize,const void* pActualData) {
 auto* b=static_cast<IndexBuffer*>(this);
 if(pActualData&&b->locked)memcpy(b->locked,pActualData,std::min(nActualSize,b->lockSize));
 Unlock();
}
HRESULT IDirect3DIndexBuffer9::GetDesc(D3DINDEXBUFFER_DESC* pDesc) { *pDesc=m_idxDesc; return 0; }
HRESULT IDirect3DQuery9::Issue(DWORD flags) { return hr(bk::queryIssue(static_cast<Query*>(this)->real,flags)); }
HRESULT IDirect3DQuery9::GetData(void* pData,DWORD dwSize,DWORD flags) { return hr(bk::queryGetData(static_cast<Query*>(this)->real,pData,dwSize,flags)); }

// ---------------------------------------------------------------- entry points
namespace {
std::string libraryDir() {
 Dl_info info{};
 if(!dladdr(reinterpret_cast<void*>(&libraryDir),&info)||!info.dli_fname)return ".";
 std::string path=info.dli_fname;auto slash=path.rfind('/');
 return slash==std::string::npos?".":path.substr(0,slash);
}
void showError(const std::string& text) {
 using ShowMessage=int(*)(uint32_t,const char*,const char*,void*);
 auto show=reinterpret_cast<ShowMessage>(dlsym(RTLD_DEFAULT,"SDL_ShowSimpleMessageBox"));
 if(show)show(0x10,"Garry's Mod (Vulkan)",text.c_str(),launcherWindow());
}
}

extern "C" TOGL_EXPORT IDirect3D9* Direct3DCreate9(UINT) {
 if(g_d3d){g_d3d->AddRef(0);return g_d3d;}
 // The game's window is SDL2's; DXVK must not pick another windowing backend.
 setenv("DXVK_WSI_DRIVER","SDL2",1);
 // ToGL never needed the game to free its default-pool resources before a reset.
 std::string config=getenv("DXVK_CONFIG")?getenv("DXVK_CONFIG"):"";
 if(config.find("countLosableResources")==std::string::npos){config+=(config.empty()?"":";");config+="d3d9.countLosableResources = False";}
 // DXVK reports an Intel GPU as an AMD one; the game sees the launcher's adapter (launcherAdapter),
 // and DXVK's own vendor then picks the alpha-to-coverage hack that GPU has.
 if(config.find("hideIntelGpu")==std::string::npos){config+=(config.empty()?"":";");config+="d3d9.hideIntelGpu = False";}
 setenv("DXVK_CONFIG",config.c_str(),1);
 std::string path=libraryDir()+"/libdxvk_d3d9.so",error;
 if(!bk::load(path.c_str(),error)){
  log("cannot start DXVK (%s): %s",path.c_str(),error.c_str());
  showError("Garry's Mod could not start its Vulkan renderer (DXVK):\n"+error+"\n\nCheck that the GPU driver supports Vulkan 1.3. To return to OpenGL, verify the game files in Steam (Properties > Installed Files), which restores bin/linux64/libtogl_client.so.");
  return nullptr;
 }
 log("rendering through DXVK (%s)",path.c_str());
 g_d3d=new D3D9();
 return g_d3d;
}
extern "C" TOGL_EXPORT void toglGetClientRect(VD3DHWND,RECT* destRect) {
 uint w=0,h=0;launcherRenderedSize(w,h,false);
 *destRect={0,0,int(w),int(h)};
}
using CreateInterfaceFn=void*(*)(const char*,int*);
extern "C" TOGL_EXPORT void* ToGLConnectLibraries(CreateInterfaceFn factory) {
 if(factory&&!launcher)launcher=factory("SDLMgrInterface001",nullptr);
 if(!launcher)log("the launcher's SDLMgrInterface001 is missing");
 return gameTable();
}
extern "C" TOGL_EXPORT void ToGLDisconnectLibraries() {}
extern "C" TOGL_EXPORT void* GetOpenGLEntryPoints(GLLookup lookup) { return launcherTable(lookup); }
extern "C" TOGL_EXPORT void ClearOpenGLEntryPoints() {}
extern "C" TOGL_EXPORT void D3DPERF_SetOptions(DWORD) {}
