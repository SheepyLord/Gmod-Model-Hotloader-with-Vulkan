#pragma once
// The binary interface of Garry's Mod's libtogl_client.so (x86-64 branch), as
// shaderapidx9_client.so and launcher_client.so were built against it: Valve's
// ToGL (Direct3D 9 on OpenGL, MIT, github.com/ValveSoftware/ToGL) as of 2014
// with 64-bit fixes. Type names, member order and method signatures follow
// ToGL's dxabstract.h / glmgr.h, so symbols mangle and members land exactly
// where the game expects them. The game inlines parts of ToGL: it writes
// device and GLMContext members directly and calls OpenGL through the entry
// point table (gl_tables.cpp), so these layouts are checked against offsets
// read from the game's binaries (static_asserts at the end).
#include <cstddef>
#include <cstdint>

typedef int INT;
typedef unsigned long ULONG;
typedef long LONG;
typedef float FLOAT;
typedef unsigned int DWORD;
typedef unsigned short WORD;
typedef long long LONGLONG;
typedef unsigned int UINT;
typedef long HRESULT;
typedef unsigned char BYTE;
typedef int BOOL;
typedef unsigned int uint;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef uint8_t uint8;
typedef DWORD D3DCOLOR;
typedef void* VD3DHWND;
typedef void* VD3DHANDLE;
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef DWORD ThreadId_t;

#define TOGL_EXPORT __attribute__((visibility("default")))

typedef struct _RECT { int left,top,right,bottom; } RECT;
struct RGNDATA { };
// 8 bytes in GMod (its GetAdapterIdentifier clears 0x440 bytes and writes VendorId at 0x428), unlike
// ToGL 2014's headers, where the 64-bit LONG HighPart makes it 16.
typedef union _LARGE_INTEGER { struct { DWORD LowPart; int32_t HighPart; }; struct { DWORD LowPart; int32_t HighPart; } u; LONGLONG QuadPart; } LARGE_INTEGER;

// Enumerations: the values are Direct3D 9's except D3DTEXTUREADDRESS (ToGL
// packs wrap/clamp/border as 0/1/2) and the ToGL-only D3DSAMP_SHADOWFILTER.
enum _D3DFORMAT : int { D3DFMT_UNKNOWN=0, D3DFMT_FORCE_DWORD=0x7fffffff };
typedef _D3DFORMAT D3DFORMAT;
enum _D3DPOOL : int { D3DPOOL_DEFAULT=0, D3DPOOL_MANAGED=1, D3DPOOL_SYSTEMMEM=2, D3DPOOL_SCRATCH=3, D3DPOOL_FORCE_DWORD=0x7fffffff };
typedef _D3DPOOL D3DPOOL;
enum _D3DDEVTYPE : int { D3DDEVTYPE_HAL=1, D3DDEVTYPE_FORCE_DWORD=0x7fffffff };
typedef _D3DDEVTYPE D3DDEVTYPE;
enum _D3DRESOURCETYPE : int { D3DRTYPE_SURFACE=1, D3DRTYPE_TEXTURE=3, D3DRTYPE_VOLUMETEXTURE=4, D3DRTYPE_CUBETEXTURE=5, D3DRTYPE_VERTEXBUFFER=6, D3DRTYPE_INDEXBUFFER=7, D3DRTYPE_FORCE_DWORD=0x7fffffff };
typedef _D3DRESOURCETYPE D3DRESOURCETYPE;
enum _D3DMULTISAMPLE_TYPE : int { D3DMULTISAMPLE_NONE=0, D3DMULTISAMPLE_FORCE_DWORD=0x7fffffff };
typedef _D3DMULTISAMPLE_TYPE D3DMULTISAMPLE_TYPE;
enum _D3DQUERYTYPE : int { D3DQUERYTYPE_EVENT=8, D3DQUERYTYPE_OCCLUSION=9, D3DQUERYTYPE_FORCE_DWORD=0x7fffffff };
typedef _D3DQUERYTYPE D3DQUERYTYPE;
enum _D3DPRIMITIVETYPE : int { D3DPT_POINTLIST=1, D3DPT_LINELIST=2, D3DPT_LINESTRIP=3, D3DPT_TRIANGLELIST=4, D3DPT_TRIANGLESTRIP=5, D3DPT_TRIANGLEFAN=6, D3DPT_FORCE_DWORD=0x7fffffff };
typedef _D3DPRIMITIVETYPE D3DPRIMITIVETYPE;
enum _D3DRENDERSTATETYPE : int { D3DRS_ZENABLE=7, D3DRS_FILLMODE=8, D3DRS_ZWRITEENABLE=14, D3DRS_ALPHATESTENABLE=15, D3DRS_SRCBLEND=19, D3DRS_DESTBLEND=20,
 D3DRS_CULLMODE=22, D3DRS_ZFUNC=23, D3DRS_ALPHAREF=24, D3DRS_ALPHAFUNC=25, D3DRS_ALPHABLENDENABLE=27, D3DRS_FOGENABLE=28, D3DRS_STENCILENABLE=52,
 D3DRS_STENCILFAIL=53, D3DRS_STENCILZFAIL=54, D3DRS_STENCILPASS=55, D3DRS_STENCILFUNC=56, D3DRS_STENCILREF=57, D3DRS_STENCILMASK=58,
 D3DRS_STENCILWRITEMASK=59, D3DRS_CLIPPLANEENABLE=152, D3DRS_COLORWRITEENABLE=168, D3DRS_BLENDOP=171, D3DRS_SCISSORTESTENABLE=174,
 D3DRS_SLOPESCALEDEPTHBIAS=175, D3DRS_ADAPTIVETESS_Y=181, D3DRS_SRGBWRITEENABLE=194, D3DRS_DEPTHBIAS=195, D3DRS_FORCE_DWORD=0x7fffffff };
typedef _D3DRENDERSTATETYPE D3DRENDERSTATETYPE;
enum _D3DSAMPLERSTATETYPE : int { D3DSAMP_ADDRESSU=1, D3DSAMP_ADDRESSV=2, D3DSAMP_ADDRESSW=3, D3DSAMP_BORDERCOLOR=4, D3DSAMP_MAGFILTER=5, D3DSAMP_MINFILTER=6,
 D3DSAMP_MIPFILTER=7, D3DSAMP_MIPMAPLODBIAS=8, D3DSAMP_MAXMIPLEVEL=9, D3DSAMP_MAXANISOTROPY=10, D3DSAMP_SRGBTEXTURE=11, D3DSAMP_SHADOWFILTER=12, D3DSAMP_FORCE_DWORD=0x7fffffff };
typedef _D3DSAMPLERSTATETYPE D3DSAMPLERSTATETYPE;
enum _D3DTEXTURESTAGESTATETYPE : int { D3DTSS_FORCE_DWORD=0x7fffffff };
typedef _D3DTEXTURESTAGESTATETYPE D3DTEXTURESTAGESTATETYPE;
enum _D3DTRANSFORMSTATETYPE : int { D3DTS_FORCE_DWORD=0x7fffffff };
typedef _D3DTRANSFORMSTATETYPE D3DTRANSFORMSTATETYPE;
enum _D3DCUBEMAP_FACES : int { D3DCUBEMAP_FACE_FORCE_DWORD=0x7fffffff };
typedef _D3DCUBEMAP_FACES D3DCUBEMAP_FACES;
enum _D3DTEXTUREFILTERTYPE : int { D3DTEXF_NONE=0, D3DTEXF_POINT=1, D3DTEXF_LINEAR=2, D3DTEXF_ANISOTROPIC=3, D3DTEXF_FORCE_DWORD=0x7fffffff };
typedef _D3DTEXTUREFILTERTYPE D3DTEXTUREFILTERTYPE;
enum _D3DSWAPEFFECT : int { D3DSWAPEFFECT_DISCARD=1, D3DSWAPEFFECT_FORCE_DWORD=0x7fffffff };
typedef _D3DSWAPEFFECT D3DSWAPEFFECT;
enum _D3DLIGHTTYPE : int { D3DLIGHT_FORCE_DWORD=0x7fffffff };
typedef _D3DLIGHTTYPE D3DLIGHTTYPE;

typedef struct _D3DRECT { LONG x1,y1,x2,y2; } D3DRECT;  // LONG is 64-bit here: 32 bytes, unlike Direct3D's
typedef struct _D3DVERTEXELEMENT9 { WORD Stream; WORD Offset; BYTE Type; BYTE Method; BYTE Usage; BYTE UsageIndex; } D3DVERTEXELEMENT9;
typedef struct _D3DSURFACE_DESC { D3DFORMAT Format; D3DRESOURCETYPE Type; DWORD Usage; D3DPOOL Pool; D3DMULTISAMPLE_TYPE MultiSampleType; DWORD MultiSampleQuality; UINT Width; UINT Height; } D3DSURFACE_DESC;
typedef struct _D3DVOLUME_DESC { D3DFORMAT Format; D3DRESOURCETYPE Type; DWORD Usage; D3DPOOL Pool; UINT Width; UINT Height; UINT Depth; } D3DVOLUME_DESC;
typedef struct _D3DLOCKED_RECT { INT Pitch; void* pBits; } D3DLOCKED_RECT;
typedef struct _D3DLOCKED_BOX { INT RowPitch; INT SlicePitch; void* pBits; } D3DLOCKED_BOX;
typedef struct _D3DBOX { UINT Left,Top,Right,Bottom,Front,Back; } D3DBOX;
typedef struct _D3DVIEWPORT9 { DWORD X,Y,Width,Height; float MinZ,MaxZ; } D3DVIEWPORT9;
typedef struct _D3DDISPLAYMODE { UINT Width; UINT Height; UINT RefreshRate; D3DFORMAT Format; } D3DDISPLAYMODE;
typedef struct _D3DGAMMARAMP { WORD red[256]; WORD green[256]; WORD blue[256]; } D3DGAMMARAMP;
typedef struct _D3DCOLORVALUE { float r,g,b,a; } D3DCOLORVALUE;
typedef struct _D3DMATERIAL9 { D3DCOLORVALUE Diffuse,Ambient,Specular,Emissive; float Power; } D3DMATERIAL9;
typedef struct _D3DVECTOR { float x,y,z; } D3DVECTOR;
typedef struct _D3DLIGHT9 { D3DLIGHTTYPE Type; D3DCOLORVALUE Diffuse,Specular,Ambient; D3DVECTOR Position,Direction; float Range,Falloff,Attenuation0,Attenuation1,Attenuation2,Theta,Phi; } D3DLIGHT9;
typedef struct _D3DINDEXBUFFER_DESC { D3DFORMAT Format; D3DRESOURCETYPE Type; DWORD Usage; D3DPOOL Pool; UINT Size; } D3DINDEXBUFFER_DESC;
typedef struct _D3DVERTEXBUFFER_DESC { D3DFORMAT Format; D3DRESOURCETYPE Type; DWORD Usage; D3DPOOL Pool; UINT Size; DWORD FVF; } D3DVERTEXBUFFER_DESC;
typedef struct _D3DPRESENT_PARAMETERS_ {
 UINT BackBufferWidth; UINT BackBufferHeight; D3DFORMAT BackBufferFormat; UINT BackBufferCount;
 D3DMULTISAMPLE_TYPE MultiSampleType; DWORD MultiSampleQuality; D3DSWAPEFFECT SwapEffect; VD3DHWND hDeviceWindow;
 BOOL Windowed; BOOL EnableAutoDepthStencil; D3DFORMAT AutoDepthStencilFormat; DWORD Flags; UINT FullScreen_RefreshRateInHz; UINT PresentationInterval;
} D3DPRESENT_PARAMETERS;
typedef struct _D3DPSHADERCAPS2_0 { DWORD Caps; INT DynamicFlowControlDepth; INT NumTemps; INT StaticFlowControlDepth; INT NumInstructionSlots; } D3DPSHADERCAPS2_0;
// ToGL's own, shorter D3DCAPS9 (the game reads these offsets).
typedef struct _D3DCAPS9 {
 D3DDEVTYPE DeviceType; DWORD Caps,Caps2,CursorCaps,DevCaps,PrimitiveMiscCaps,RasterCaps,TextureCaps,TextureFilterCaps;
 DWORD MaxTextureWidth,MaxTextureHeight,MaxVolumeExtent,MaxTextureAspectRatio,MaxAnisotropy,TextureOpCaps,MaxTextureBlendStages,MaxSimultaneousTextures;
 DWORD VertexProcessingCaps,MaxActiveLights,MaxUserClipPlanes,MaxVertexBlendMatrices,MaxVertexBlendMatrixIndex,MaxPrimitiveCount,MaxStreams;
 DWORD VertexShaderVersion,MaxVertexShaderConst,PixelShaderVersion,DevCaps2; D3DPSHADERCAPS2_0 PS20Caps;
 DWORD NumSimultaneousRTs,MaxVertexShader30InstructionSlots,MaxPixelShader30InstructionSlots,FakeSRGBWrite,MixedSizeTargets,CanDoSRGBReadFromRTs;
} D3DCAPS9;
typedef struct _D3DADAPTER_IDENTIFIER9 { char Driver[512]; char Description[512]; char DeviceName[32]; LARGE_INTEGER DriverVersion; DWORD VendorId,DeviceId,SubSysId,Revision,VideoMemory; } D3DADAPTER_IDENTIFIER9;

struct D3DMATRIX { union { struct { float _11,_12,_13,_14,_21,_22,_23,_24,_31,_32,_33,_34,_41,_42,_43,_44; }; float m[4][4]; }; };
class TOGL_EXPORT D3DXMATRIX : public D3DMATRIX {
public:
 D3DXMATRIX operator*(const D3DXMATRIX& o) const;
 operator FLOAT*();
 float& operator()(int row,int column);
 const float& operator()(int row,int column) const;
 bool operator!=(const D3DXMATRIX& src) const;
};
typedef struct TOGL_EXPORT D3DXPLANE {
 float& operator[](int i);
 bool operator==(const D3DXPLANE& o);
 bool operator!=(const D3DXPLANE& o);
 operator float*();
 operator const float*() const;
 float a,b,c,d;
} D3DXPLANE;
class TOGL_EXPORT D3DXVECTOR2 { public: operator FLOAT*(); operator const FLOAT*() const; float x,y; };
class TOGL_EXPORT D3DXVECTOR3 : public D3DVECTOR { public: D3DXVECTOR3() {} D3DXVECTOR3(float a,float b,float c); operator FLOAT*(); operator const FLOAT*() const; };
class TOGL_EXPORT D3DXVECTOR4 { public: D3DXVECTOR4() {} D3DXVECTOR4(float a,float b,float c,float d); float x,y,z,w; };
struct ID3DXInclude;
struct _D3DXMACRO;
struct ID3DXConstantTable;
struct TOGL_EXPORT ID3DXBuffer { int m_refcount[2]; bool m_mark; void* GetBufferPointer(); DWORD GetBufferSize(); };
struct ID3DXMatrixStackData;
// Never derived from IUnknown in ToGL; the game only calls these methods.
struct TOGL_EXPORT ID3DXMatrixStack {
 int m_refcount[2];
 bool m_mark;
 ID3DXMatrixStackData* m_data;
 ID3DXMatrixStack();
 void AddRef(int which=0,char* comment=nullptr);
 ULONG Release(int which=0,char* comment=nullptr);
 HRESULT Create();
 D3DXMATRIX* GetTop();
 void Push();
 void Pop();
 void LoadIdentity();
 void LoadMatrix(const D3DXMATRIX* pMat);
 void MultMatrix(const D3DXMATRIX* pMat);
 void MultMatrixLocal(const D3DXMATRIX* pMat);
 HRESULT ScaleLocal(FLOAT x,FLOAT y,FLOAT z);
 HRESULT RotateAxisLocal(const D3DXVECTOR3* pV,FLOAT Angle);
 HRESULT TranslateLocal(FLOAT x,FLOAT y,FLOAT z);
};

class CCommand;
class CGLMTex;
class CGLMBuffer;
class CGLMProgram;
class CGLMQuery;
class GLMContext;
struct GLMShaderPairInfo { int m_status; char m_vsName[128]; int m_vsStaticIndex; int m_vsDynamicIndex; char m_psName[128]; int m_psStaticIndex; int m_psDynamicIndex; };

struct IDirect3DDevice9;
struct IDirect3DBaseTexture9;
struct IDirect3DTexture9;
struct IDirect3DCubeTexture9;
struct IDirect3DVolumeTexture9;
struct IDirect3DSurface9;
struct IDirect3DVertexDeclaration9;
struct IDirect3DQuery9;
struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;
struct IDirect3DPixelShader9;
struct IDirect3DVertexShader9;

// AddRef/Release are inline in the game: it changes m_refcount itself and
// deletes through the virtual destructor when both counts reach zero.
struct TOGL_EXPORT IUnknown {
 int m_refcount[2];
 bool m_mark;
 IUnknown() { m_refcount[0]=1; m_refcount[1]=0; m_mark=false; }
 virtual ~IUnknown() {}
 void AddRef(int which=0,char* =nullptr) { m_refcount[which]++; }
 ULONG Release(int which=0,char* =nullptr) {
  m_refcount[which]--;
  if(!m_refcount[0]&&!m_refcount[1]) { delete this; return 0; }
  return ULONG(m_refcount[0]);
 }
};

struct TOGL_EXPORT IDirect3DResource9 : public IUnknown {
 IDirect3DDevice9* m_device;
 D3DRESOURCETYPE m_restype;
 DWORD SetPriority(DWORD PriorityNew);
};

struct TOGL_EXPORT IDirect3DBaseTexture9 : public IDirect3DResource9 {
 D3DSURFACE_DESC m_descZero;
 CGLMTex* m_tex;
 virtual ~IDirect3DBaseTexture9();
 D3DRESOURCETYPE GetType();
 DWORD GetLevelCount();
 HRESULT GetLevelDesc(UINT Level,D3DSURFACE_DESC* pDesc);
};
struct TOGL_EXPORT IDirect3DTexture9 : public IDirect3DBaseTexture9 {
 IDirect3DSurface9* m_surfZero;
 virtual ~IDirect3DTexture9();
 HRESULT LockRect(UINT Level,D3DLOCKED_RECT* pLockedRect,const RECT* pRect,DWORD Flags);
 HRESULT UnlockRect(UINT Level);
 HRESULT GetSurfaceLevel(UINT Level,IDirect3DSurface9** ppSurfaceLevel);
};
struct TOGL_EXPORT IDirect3DCubeTexture9 : public IDirect3DBaseTexture9 {
 IDirect3DSurface9* m_surfZero[6];
 virtual ~IDirect3DCubeTexture9();
 HRESULT GetCubeMapSurface(D3DCUBEMAP_FACES FaceType,UINT Level,IDirect3DSurface9** ppCubeMapSurface);
 HRESULT GetLevelDesc(UINT Level,D3DSURFACE_DESC* pDesc);
};
struct TOGL_EXPORT IDirect3DVolumeTexture9 : public IDirect3DBaseTexture9 {
 IDirect3DSurface9* m_surfZero;
 D3DVOLUME_DESC m_volDescZero;
 virtual ~IDirect3DVolumeTexture9();
 HRESULT LockBox(UINT Level,D3DLOCKED_BOX* pLockedVolume,const D3DBOX* pBox,DWORD Flags);
 HRESULT UnlockBox(UINT Level);
 HRESULT GetLevelDesc(UINT level,D3DVOLUME_DESC* pDesc);
};
struct TOGL_EXPORT IDirect3DSurface9 : public IDirect3DResource9 {
 virtual ~IDirect3DSurface9();
 HRESULT LockRect(D3DLOCKED_RECT* pLockedRect,const RECT* pRect,DWORD Flags);
 HRESULT UnlockRect();
 HRESULT GetDesc(D3DSURFACE_DESC* pDesc);
 D3DSURFACE_DESC m_desc;
 CGLMTex* m_tex;
 int m_face;
 int m_mip;
};

struct IDirect3DDevice9Params {
 UINT m_adapter;
 D3DDEVTYPE m_deviceType;
 VD3DHWND m_focusWindow;
 DWORD m_behaviorFlags;
 D3DPRESENT_PARAMETERS m_presentationParameters;
};

struct TOGL_EXPORT IDirect3D9 : public IUnknown {
 virtual ~IDirect3D9();
 UINT GetAdapterCount();
 HRESULT GetDeviceCaps(UINT Adapter,D3DDEVTYPE DeviceType,D3DCAPS9* pCaps);
 HRESULT GetAdapterIdentifier(UINT Adapter,DWORD Flags,D3DADAPTER_IDENTIFIER9* pIdentifier);
 HRESULT CheckDeviceFormat(UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT AdapterFormat,DWORD Usage,D3DRESOURCETYPE RType,D3DFORMAT CheckFormat);
 UINT GetAdapterModeCount(UINT Adapter,D3DFORMAT Format);
 HRESULT EnumAdapterModes(UINT Adapter,D3DFORMAT Format,UINT Mode,D3DDISPLAYMODE* pMode);
 HRESULT CheckDeviceType(UINT Adapter,D3DDEVTYPE DevType,D3DFORMAT AdapterFormat,D3DFORMAT BackBufferFormat,BOOL bWindowed);
 HRESULT GetAdapterDisplayMode(UINT Adapter,D3DDISPLAYMODE* pMode);
 HRESULT CheckDepthStencilMatch(UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT AdapterFormat,D3DFORMAT RenderTargetFormat,D3DFORMAT DepthStencilFormat);
 HRESULT CheckDeviceMultiSampleType(UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT SurfaceFormat,BOOL Windowed,D3DMULTISAMPLE_TYPE MultiSampleType,DWORD* pQualityLevels);
 HRESULT CreateDevice(UINT Adapter,D3DDEVTYPE DeviceType,VD3DHWND hFocusWindow,DWORD BehaviorFlags,D3DPRESENT_PARAMETERS* pPresentationParameters,IDirect3DDevice9** ppReturnedDeviceInterface);
};

#define MAX_D3DVERTEXELEMENTS 16
struct D3DVERTEXELEMENT9_GL { D3DVERTEXELEMENT9 m_dxdecl; uint8 m_glm[40]; };  // GLMVertexAttributeDesc: only its size matters
struct TOGL_EXPORT IDirect3DVertexDeclaration9 : public IUnknown {
 IDirect3DDevice9* m_device;
 uint m_elemCount;
 D3DVERTEXELEMENT9_GL m_elements[MAX_D3DVERTEXELEMENTS];
 uint8 m_VertexAttribDescToStreamIndex[256];
 virtual ~IDirect3DVertexDeclaration9();
};
struct TOGL_EXPORT IDirect3DQuery9 : public IDirect3DResource9 {
 D3DQUERYTYPE m_type;
 GLMContext* m_ctx;
 CGLMQuery* m_query;
 uint m_nIssueStartThreadID,m_nIssueEndThreadID;
 uint m_nIssueStartDrawCallIndex,m_nIssueEndDrawCallIndex;
 uint m_nIssueStartFrameIndex,m_nIssueEndFrameIndex;
 uint m_nIssueStartQueryCreationCounter,m_nIssueEndQueryCreationCounter;
 virtual ~IDirect3DQuery9();
 HRESULT Issue(DWORD dwIssueFlags);
 HRESULT GetData(void* pData,DWORD dwSize,DWORD dwGetDataFlags);
};
struct TOGL_EXPORT IDirect3DVertexBuffer9 : public IDirect3DResource9 {
 GLMContext* m_ctx;
 CGLMBuffer* m_vtxBuffer;
 D3DVERTEXBUFFER_DESC m_vtxDesc;
 virtual ~IDirect3DVertexBuffer9();
 HRESULT Lock(UINT OffsetToLock,UINT SizeToLock,void** ppbData,DWORD Flags);
 HRESULT Unlock();
 void UnlockActualSize(uint nActualSize,const void* pActualData=nullptr);
};
struct TOGL_EXPORT IDirect3DIndexBuffer9 : public IDirect3DResource9 {
 GLMContext* m_ctx;
 CGLMBuffer* m_idxBuffer;
 D3DINDEXBUFFER_DESC m_idxDesc;
 virtual ~IDirect3DIndexBuffer9();
 HRESULT Lock(UINT OffsetToLock,UINT SizeToLock,void** ppbData,DWORD Flags);
 HRESULT Unlock();
 void UnlockActualSize(uint nActualSize,const void* pActualData=nullptr);
 HRESULT GetDesc(D3DINDEXBUFFER_DESC* pDesc);
};
struct TOGL_EXPORT IDirect3DPixelShader9 : public IDirect3DResource9 {
 CGLMProgram* m_pixProgram;
 uint m_pixHighWater;
 uint m_pixSamplerMask;
 uint m_pixSamplerTypes;
 uint m_pixFragDataMask;
 virtual ~IDirect3DPixelShader9();
};
struct TOGL_EXPORT IDirect3DVertexShader9 : public IDirect3DResource9 {
 CGLMProgram* m_vtxProgram;
 uint m_vtxHighWater;
 uint m_vtxHighWaterBone;
 unsigned char m_vtxAttribMap[16];
 uint m_maxVertexAttrs;
 virtual ~IDirect3DVertexShader9();
};

// glmgr.h's state mirrors: the game fills these inline before calling into GL.
struct GLAlphaTestEnable_t { GLint enable; };
struct GLAlphaTestFunc_t { GLenum func; GLclampf ref; };
struct GLCullFaceEnable_t { GLint enable; };
struct GLCullFrontFace_t { GLenum value; };
struct GLPolygonMode_t { GLenum values[2]; };
struct GLDepthBias_t { GLfloat factor; GLfloat units; };
struct GLScissorEnable_t { GLint enable; };
struct GLScissorBox_t { GLint x,y; GLsizei width,height; };
struct GLAlphaToCoverageEnable_t { GLint enable; };
struct GLViewportBox_t { GLint x,y; GLsizei width,height; uint widthheight; };
struct GLViewportDepthRange_t { GLdouble flNear,flFar; };
struct GLClipPlaneEnable_t { GLint enable; };
struct GLClipPlaneEquation_t { GLfloat x,y,z,w; };
struct GLColorMaskSingle_t { char r,g,b,a; };
struct GLColorMaskMultiple_t { char r,g,b,a; };
struct GLBlendEnable_t { GLint enable; };
struct GLBlendFactor_t { GLenum srcfactor,dstfactor; };
struct GLBlendEquation_t { GLenum equation; };
struct GLBlendColor_t { GLfloat r,g,b,a; };
struct GLBlendEnableSRGB_t { GLint enable; };
struct GLDepthTestEnable_t { GLint enable; };
struct GLDepthFunc_t { GLenum func; };
struct GLDepthMask_t { char mask; };
struct GLStencilTestEnable_t { GLint enable; };
struct GLStencilFunc_t { GLenum frontfunc,backfunc; GLint ref; GLuint mask; };
struct GLStencilOp_t { GLenum sfail; GLenum dpfail; GLenum dppass; };
struct GLStencilWriteMask_t { GLint mask; };
struct GLClearColor_t { GLfloat r,g,b,a; };
struct GLClearDepth_t { GLdouble d; };
struct GLClearStencil_t { GLint s; };

#define kGLMUserClipPlanes 2
#define GLM_SAMPLER_COUNT 16
#define D3D_MAX_STREAMS 5
struct D3DStreamDesc { IDirect3DVertexBuffer9* m_vtxBuffer; uint m_offset; uint m_stride; };
struct D3DIndexDesc { IDirect3DIndexBuffer9* m_idxBuffer; };
struct GLMDeviceStateMirror {
 GLAlphaTestEnable_t m_AlphaTestEnable; GLAlphaTestFunc_t m_AlphaTestFunc; GLAlphaToCoverageEnable_t m_AlphaToCoverageEnable;
 GLDepthTestEnable_t m_DepthTestEnable; GLDepthMask_t m_DepthMask; GLDepthFunc_t m_DepthFunc;
 GLClipPlaneEnable_t m_ClipPlaneEnable[kGLMUserClipPlanes]; GLClipPlaneEquation_t m_ClipPlaneEquation[kGLMUserClipPlanes];
 GLColorMaskSingle_t m_ColorMaskSingle; GLColorMaskMultiple_t m_ColorMaskMultiple;
 GLCullFaceEnable_t m_CullFaceEnable; GLCullFrontFace_t m_CullFrontFace; GLPolygonMode_t m_PolygonMode; GLDepthBias_t m_DepthBias;
 GLScissorEnable_t m_ScissorEnable; GLScissorBox_t m_ScissorBox; GLViewportBox_t m_ViewportBox; GLViewportDepthRange_t m_ViewportDepthRange;
 GLBlendEnable_t m_BlendEnable; GLBlendFactor_t m_BlendFactor; GLBlendEquation_t m_BlendEquation; GLBlendColor_t m_BlendColor; GLBlendEnableSRGB_t m_BlendEnableSRGB;
 GLStencilTestEnable_t m_StencilTestEnable; GLStencilFunc_t m_StencilFunc; GLStencilOp_t m_StencilOp; GLStencilWriteMask_t m_StencilWriteMask;
 GLClearColor_t m_ClearColor; GLClearDepth_t m_ClearDepth; GLClearStencil_t m_ClearStencil;
 bool m_FogEnable;
};
struct D3DObjectStats { int counts[10]; };

struct TOGL_EXPORT IDirect3DDevice9 : public IUnknown {
 IDirect3DDevice9();
 virtual ~IDirect3DDevice9();
 HRESULT Reset(D3DPRESENT_PARAMETERS* pPresentationParameters);
 HRESULT SetViewport(const D3DVIEWPORT9* pViewport);
 HRESULT GetViewport(D3DVIEWPORT9* pViewport);
 HRESULT BeginScene();
 HRESULT Clear(DWORD Count,const D3DRECT* pRects,DWORD Flags,D3DCOLOR Color,float Z,DWORD Stencil);
 HRESULT EndScene();
 HRESULT Present(const RECT* pSourceRect,const RECT* pDestRect,VD3DHWND hDestWindowOverride,const RGNDATA* pDirtyRegion);
 HRESULT CreateTexture(UINT Width,UINT Height,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DTexture9** ppTexture,VD3DHANDLE* pSharedHandle,char* debugLabel=nullptr);
 HRESULT CreateCubeTexture(UINT EdgeLength,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DCubeTexture9** ppCubeTexture,VD3DHANDLE* pSharedHandle,char* debugLabel=nullptr);
 HRESULT CreateVolumeTexture(UINT Width,UINT Height,UINT Depth,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DVolumeTexture9** ppVolumeTexture,VD3DHANDLE* pSharedHandle,char* debugLabel=nullptr);
 HRESULT GetTexture(DWORD Stage,IDirect3DBaseTexture9** ppTexture);
 HRESULT CreateRenderTarget(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Lockable,IDirect3DSurface9** ppSurface,VD3DHANDLE* pSharedHandle,char* debugLabel=nullptr);
 HRESULT SetRenderTarget(DWORD RenderTargetIndex,IDirect3DSurface9* pRenderTarget);
 HRESULT GetRenderTarget(DWORD RenderTargetIndex,IDirect3DSurface9** ppRenderTarget);
 HRESULT CreateOffscreenPlainSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DPOOL Pool,IDirect3DSurface9** ppSurface,VD3DHANDLE* pSharedHandle);
 HRESULT CreateDepthStencilSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Discard,IDirect3DSurface9** ppSurface,VD3DHANDLE* pSharedHandle);
 HRESULT SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil);
 HRESULT GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface);
 HRESULT GetRenderTargetData(IDirect3DSurface9* pRenderTarget,IDirect3DSurface9* pDestSurface);
 HRESULT GetFrontBufferData(UINT iSwapChain,IDirect3DSurface9* pDestSurface);
 HRESULT StretchRect(IDirect3DSurface9* pSourceSurface,const RECT* pSourceRect,IDirect3DSurface9* pDestSurface,const RECT* pDestRect,D3DTEXTUREFILTERTYPE Filter);
 HRESULT CreatePixelShader(const DWORD* pFunction,IDirect3DPixelShader9** ppShader,const char* pShaderName,char* debugLabel=nullptr,const uint32* pCentroidMask=nullptr);
 HRESULT SetPixelShaderConstantB(UINT StartRegister,const BOOL* pConstantData,UINT BoolCount);
 HRESULT SetPixelShaderConstantI(UINT StartRegister,const int* pConstantData,UINT Vector4iCount);
 HRESULT CreateVertexShader(const DWORD* pFunction,IDirect3DVertexShader9** ppShader,const char* pShaderName,char* debugLabel=nullptr);
 HRESULT LinkShaderPair(IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps);
 HRESULT QueryShaderPair(int index,GLMShaderPairInfo* infoOut);
 HRESULT CreateVertexDeclaration(const D3DVERTEXELEMENT9* pVertexElements,IDirect3DVertexDeclaration9** ppDecl);
 HRESULT SetFVF(DWORD FVF);
 HRESULT GetFVF(DWORD* pFVF);
 HRESULT CreateVertexBuffer(UINT Length,DWORD Usage,DWORD FVF,D3DPOOL Pool,IDirect3DVertexBuffer9** ppVertexBuffer,VD3DHANDLE* pSharedHandle);
 HRESULT CreateIndexBuffer(UINT Length,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DIndexBuffer9** ppIndexBuffer,VD3DHANDLE* pSharedHandle);
 HRESULT SetRenderState(D3DRENDERSTATETYPE State,DWORD Value);
 HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType,UINT StartVertex,UINT PrimitiveCount);
 HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType,INT BaseVertexIndex,UINT MinVertexIndex,UINT NumVertices,UINT startIndex,UINT primCount);
 HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType,UINT MinVertexIndex,UINT NumVertices,UINT PrimitiveCount,const void* pIndexData,D3DFORMAT IndexDataFormat,const void* pVertexStreamZeroData,UINT VertexStreamZeroStride);
 BOOL ShowCursor(BOOL bShow);
 HRESULT ValidateDevice(DWORD* pNumPasses);
 HRESULT SetMaterial(const D3DMATERIAL9* pMaterial);
 HRESULT LightEnable(DWORD Index,BOOL Enable);
 HRESULT SetScissorRect(const RECT* pRect);
 HRESULT CreateQuery(D3DQUERYTYPE Type,IDirect3DQuery9** ppQuery);
 HRESULT GetDeviceCaps(D3DCAPS9* pCaps);
 HRESULT TestCooperativeLevel();
 HRESULT EvictManagedResources();
 HRESULT SetLight(DWORD Index,const D3DLIGHT9*);
 void SetGammaRamp(UINT iSwapChain,DWORD Flags,const D3DGAMMARAMP* pRamp);
 HRESULT SetClipPlane(DWORD Index,const float* pPlane);
 HRESULT SetTransform(D3DTRANSFORMSTATETYPE State,const D3DMATRIX* pMatrix);
 HRESULT SetTextureStageState(DWORD Stage,D3DTEXTURESTAGESTATETYPE Type,DWORD Value);
 void AcquireThreadOwnership();
 void ReleaseThreadOwnership();

 DWORD m_nValidMarker;
 IDirect3DDevice9Params m_params;
 IDirect3DSurface9* m_pRenderTargets[4];
 IDirect3DSurface9* m_pDepthStencil;
 IDirect3DSurface9* m_pDefaultColorSurface;
 IDirect3DSurface9* m_pDefaultDepthStencilSurface;
 IDirect3DVertexDeclaration9* m_pVertDecl;            // set inline by the game
 D3DStreamDesc m_streams[D3D_MAX_STREAMS];            // set inline
 CGLMBuffer* m_vtx_buffers[D3D_MAX_STREAMS];          // set inline
 CGLMBuffer* m_pDummy_vtx_buffer;                     // read inline
 D3DIndexDesc m_indices;                              // set inline
 IDirect3DVertexShader9* m_vertexShader;              // set inline
 IDirect3DPixelShader9* m_pixelShader;                // set inline
 IDirect3DBaseTexture9* m_textures[GLM_SAMPLER_COUNT];// set inline
 GLMContext* m_ctx;                                   // read inline
 void* m_pFBOs;
 bool m_bFBODirty;
 D3DObjectStats m_ObjectStats;
 D3DObjectStats m_PrevObjectStats;
 GLMDeviceStateMirror gl;                             // written inline
};

// GLMContext: the game's inline code reaches these members (the rest of the
// 18720-byte object is padding to us). Offsets are GMod's.
struct GLMTexSamplingParams {
 union {
  struct { uint32 m_addressU:2,m_addressV:2,m_addressW:2,m_minFilter:2,m_magFilter:2,m_mipFilter:2,m_minLOD:4,m_maxAniso:5,m_compareMode:1,m_srgb:1,m_isValid:1; } m_packed;
  uint32 m_bits;
 };
 uint32 m_borderColor;
};
struct GLMTexSampler { CGLMTex* m_pBoundTex; GLMTexSamplingParams m_samp; };
#define kGLMProgramParamFloat4Limit 256
#define kGLMProgramParamBoolLimit 16
#define kGLMProgramParamInt4Limit 16
struct GLMProgramParamsF { float m_values[kGLMProgramParamFloat4Limit][4]; int m_firstDirtySlotNonBone; int m_dirtySlotHighWaterNonBone; int m_dirtySlotHighWaterBone; };
struct GLMProgramParamsB { int m_values[kGLMProgramParamBoolLimit]; uint m_dirtySlotCount; };
struct GLMProgramParamsI { int m_values[kGLMProgramParamInt4Limit][4]; uint m_dirtySlotCount; };
enum { kGLMVertexProgram=0, kGLMFragmentProgram=1 };
struct GLMContextLayout {
 uint8 pad0[112];
 uint64 m_nCurOwnerThreadId;                    // 112
 uint32 m_nThreadOwnershipReleaseCounter;       // 120
 bool m_bUseSamplerObjects;                     // 124 (read inline: false marks samplers dirty on SetTexture)
 uint8 pad1[358-125];
 bool m_hasGammaWrites;                         // 358 = m_caps (136) + 222 (read inline for D3DRS_SRGBWRITEENABLE)
 uint8 pad2[873-359];
 bool m_bUseBoneUniformBuffers;                 // 873 (read inline by SetVertexShaderConstantF)
 uint8 pad3[1092-874];
 GLBlendEnableSRGB_t m_BlendEnableSRGB;         // 1092
 bool m_FakeBlendEnableSRGB;                    // 1096 (written inline when m_hasGammaWrites is false)
 uint8 pad4[1188-1097];
 int m_activeTexture;                           // 1188
 GLMTexSampler m_samplers[GLM_SAMPLER_COUNT];   // 1192 (written inline)
 uint8 m_nDirtySamplerFlags[GLM_SAMPLER_COUNT]; // 1448: 0 when dirty
 uint32 m_nNumDirtySamplers;                    // 1464
 uint8 m_nDirtySamplers[GLM_SAMPLER_COUNT+1];   // 1468
 uint8 pad5[7792-1485];
 CGLMProgram* m_drawingProgram[2];              // 7792 (written inline)
 bool m_bDirtyPrograms;                         // 7808
 GLMProgramParamsF m_programParamsF[2];         // 7812 (written inline)
 GLMProgramParamsB m_programParamsB[2];         // 16028
 GLMProgramParamsI m_programParamsI[2];         // 16164
 int m_paramWriteMode;                          // 16684
 CGLMProgram* m_pNullFragmentProgram;           // 16688 (read inline when no pixel shader is set)
 uint8 pad6[17980-16696];
 int m_nMaxUsedVertexProgramConstantsHint;      // 17980 (written inline)
 uint8 pad7[18720-17984];
};

// CGLMTex as SetTexture's inline code reads it (glBindMultiTextureEXT arguments).
class CGLMTex { public: GLuint m_texName; GLenum m_texGLTarget; };

static_assert(sizeof(IUnknown)==24);
static_assert(offsetof(IDirect3DResource9,m_device)==24&&offsetof(IDirect3DResource9,m_restype)==32);
static_assert(offsetof(IDirect3DBaseTexture9,m_descZero)==36&&offsetof(IDirect3DBaseTexture9,m_tex)==72);
static_assert(offsetof(IDirect3DTexture9,m_surfZero)==80&&sizeof(IDirect3DTexture9)==88);
static_assert(sizeof(IDirect3DCubeTexture9)==128&&sizeof(IDirect3DVolumeTexture9)==120&&offsetof(IDirect3DVolumeTexture9,m_volDescZero)==88);
static_assert(offsetof(IDirect3DSurface9,m_desc)==36&&offsetof(IDirect3DSurface9,m_tex)==72&&sizeof(IDirect3DSurface9)==88);
static_assert(offsetof(IDirect3DVertexBuffer9,m_vtxBuffer)==48&&sizeof(IDirect3DVertexBuffer9)==80);
static_assert(offsetof(IDirect3DIndexBuffer9,m_idxBuffer)==48&&sizeof(IDirect3DIndexBuffer9)==80);
static_assert(offsetof(IDirect3DPixelShader9,m_pixProgram)==40&&sizeof(IDirect3DPixelShader9)==64);
static_assert(offsetof(IDirect3DVertexShader9,m_vtxProgram)==40&&sizeof(IDirect3DVertexShader9)==80);
static_assert(sizeof(IDirect3DVertexDeclaration9)==1064&&sizeof(IDirect3DQuery9)==88);
static_assert(sizeof(D3DPRESENT_PARAMETERS)==64&&sizeof(D3DCAPS9)==156&&sizeof(D3DADAPTER_IDENTIFIER9)==0x440&&sizeof(D3DRECT)==32);
static_assert(offsetof(D3DADAPTER_IDENTIFIER9,VendorId)==0x428&&offsetof(D3DADAPTER_IDENTIFIER9,VideoMemory)==0x438&&offsetof(D3DCAPS9,MaxAnisotropy)==0x34&&offsetof(D3DCAPS9,FakeSRGBWrite)==0x90);
static_assert(sizeof(IDirect3DDevice9Params)==88&&sizeof(GLMDeviceStateMirror)==264&&sizeof(GLMShaderPairInfo)==276);
static_assert(offsetof(IDirect3DDevice9,m_params)==24&&offsetof(IDirect3DDevice9,m_pVertDecl)==168&&offsetof(IDirect3DDevice9,m_streams)==176);
static_assert(offsetof(IDirect3DDevice9,m_vtx_buffers)==256&&offsetof(IDirect3DDevice9,m_pDummy_vtx_buffer)==296&&offsetof(IDirect3DDevice9,m_indices)==304);
static_assert(offsetof(IDirect3DDevice9,m_vertexShader)==312&&offsetof(IDirect3DDevice9,m_pixelShader)==320&&offsetof(IDirect3DDevice9,m_textures)==328);
static_assert(offsetof(IDirect3DDevice9,m_ctx)==456&&offsetof(IDirect3DDevice9,gl)==560&&sizeof(IDirect3DDevice9)==824);
static_assert(offsetof(GLMContextLayout,m_bUseSamplerObjects)==124&&offsetof(GLMContextLayout,m_hasGammaWrites)==358&&offsetof(GLMContextLayout,m_bUseBoneUniformBuffers)==873);
static_assert(offsetof(GLMContextLayout,m_BlendEnableSRGB)==1092&&offsetof(GLMContextLayout,m_FakeBlendEnableSRGB)==1096&&offsetof(GLMContextLayout,m_activeTexture)==1188);
static_assert(offsetof(GLMContextLayout,m_samplers)==1192&&offsetof(GLMContextLayout,m_nDirtySamplerFlags)==1448&&offsetof(GLMContextLayout,m_nNumDirtySamplers)==1464&&offsetof(GLMContextLayout,m_nDirtySamplers)==1468);
static_assert(offsetof(GLMContextLayout,m_drawingProgram)==7792&&offsetof(GLMContextLayout,m_bDirtyPrograms)==7808&&offsetof(GLMContextLayout,m_programParamsF)==7812);
static_assert(offsetof(GLMContextLayout,m_programParamsB)==16028&&offsetof(GLMContextLayout,m_programParamsI)==16164&&offsetof(GLMContextLayout,m_pNullFragmentProgram)==16688);
static_assert(offsetof(GLMContextLayout,m_nMaxUsedVertexProgramConstantsHint)==17980&&sizeof(GLMContextLayout)==18720);
