// The D3DX helpers ToGL exports to the game, ported from Valve's ToGL
// (dxabstract.cpp, MIT) with the same behavior, including what ToGL leaves
// unimplemented (those matrix-stack operations do nothing there either).
#include "togl_abi.hpp"
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

struct ID3DXMatrixStackData { std::vector<D3DMATRIX> stack; };

D3DXMATRIX D3DXMATRIX::operator*(const D3DXMATRIX& o) const {
 D3DXMATRIX r;
 for(int i=0;i<4;i++)for(int j=0;j<4;j++)r.m[i][j]=m[i][0]*o.m[0][j]+m[i][1]*o.m[1][j]+m[i][2]*o.m[2][j]+m[i][3]*o.m[3][j];
 return r;
}
D3DXMATRIX::operator FLOAT*() { return reinterpret_cast<float*>(this); }
float& D3DXMATRIX::operator()(int row,int column) { return m[row][column]; }
const float& D3DXMATRIX::operator()(int row,int column) const { return m[row][column]; }
// Compares sizeof(this) bytes, as ToGL (and GMod's build) does.
bool D3DXMATRIX::operator!=(const D3DXMATRIX& src) const { constexpr size_t compared=sizeof(void*);return memcmp(this,&src,compared)!=0; }
float& D3DXPLANE::operator[](int i) { return reinterpret_cast<float*>(this)[i]; }
bool D3DXPLANE::operator==(const D3DXPLANE& o) { return a==o.a&&b==o.b&&c==o.c&&d==o.d; }
bool D3DXPLANE::operator!=(const D3DXPLANE& o) { return !(*this==o); }
D3DXPLANE::operator float*() { return reinterpret_cast<float*>(this); }
D3DXPLANE::operator const float*() const { return reinterpret_cast<const float*>(this); }
D3DXVECTOR2::operator FLOAT*() { return reinterpret_cast<float*>(this); }
D3DXVECTOR2::operator const FLOAT*() const { return reinterpret_cast<const float*>(this); }
D3DXVECTOR3::D3DXVECTOR3(float a,float b,float c) { x=a;y=b;z=c; }
D3DXVECTOR3::operator FLOAT*() { return reinterpret_cast<float*>(this); }
D3DXVECTOR3::operator const FLOAT*() const { return reinterpret_cast<const float*>(this); }
D3DXVECTOR4::D3DXVECTOR4(float a,float b,float c,float d) { x=a;y=b;z=c;w=d; }

void* ID3DXBuffer::GetBufferPointer() { return nullptr; }
DWORD ID3DXBuffer::GetBufferSize() { return 0; }

extern "C" TOGL_EXPORT void D3DXMatrixIdentity(D3DXMATRIX* mat) { for(int i=0;i<4;i++)for(int j=0;j<4;j++)mat->m[i][j]=i==j?1.f:0.f; }

ID3DXMatrixStack::ID3DXMatrixStack() { m_refcount[0]=1;m_refcount[1]=0;m_mark=false;m_data=new ID3DXMatrixStackData; }
void ID3DXMatrixStack::AddRef(int which,char*) { m_refcount[which]++; }
ULONG ID3DXMatrixStack::Release(int which,char*) {
 m_refcount[which]--;
 if(!m_refcount[0]&&!m_refcount[1]){delete m_data;delete this;return 0;}
 return ULONG(m_refcount[0]);
}
HRESULT ID3DXMatrixStack::Create() { m_data->stack.reserve(16);m_data->stack.emplace_back();LoadIdentity();return 0; }
D3DXMATRIX* ID3DXMatrixStack::GetTop() { return static_cast<D3DXMATRIX*>(&m_data->stack.back()); }
void ID3DXMatrixStack::Push() { D3DMATRIX top=m_data->stack.back();m_data->stack.push_back(top); }
void ID3DXMatrixStack::Pop() { if(m_data->stack.size()>1)m_data->stack.pop_back(); }
void ID3DXMatrixStack::LoadIdentity() { D3DXMatrixIdentity(GetTop()); }
void ID3DXMatrixStack::LoadMatrix(const D3DXMATRIX* pMat) { *GetTop()=*pMat; }
void ID3DXMatrixStack::MultMatrix(const D3DXMATRIX*) {}
void ID3DXMatrixStack::MultMatrixLocal(const D3DXMATRIX*) {}
HRESULT ID3DXMatrixStack::ScaleLocal(FLOAT,FLOAT,FLOAT) { return 0; }
HRESULT ID3DXMatrixStack::RotateAxisLocal(const D3DXVECTOR3*,FLOAT) { return 0; }
HRESULT ID3DXMatrixStack::TranslateLocal(FLOAT,FLOAT,FLOAT) { return 0; }

extern "C" {
TOGL_EXPORT HRESULT D3DXCreateMatrixStack(DWORD,ID3DXMatrixStack** ppStack) { *ppStack=new ID3DXMatrixStack;(*ppStack)->Create();return 0; }
TOGL_EXPORT D3DXMATRIX* D3DXMatrixMultiply(D3DXMATRIX* pOut,const D3DXMATRIX* pM1,const D3DXMATRIX* pM2) { *pOut=*pM1**pM2;return pOut; }
TOGL_EXPORT D3DXVECTOR3* D3DXVec3TransformCoord(D3DXVECTOR3* pOut,const D3DXVECTOR3* pV,const D3DXMATRIX* pM) {
 float wp=pM->m[3][0]*pV->x+pM->m[3][1]*pV->y+pM->m[3][2]*pV->z+pM->m[3][3];
 if(wp!=0.f){
  float x=(pM->m[0][0]*pV->x+pM->m[0][1]*pV->y+pM->m[0][2]*pV->z)/wp;
  float y=(pM->m[1][0]*pV->x+pM->m[1][1]*pV->y+pM->m[1][2]*pV->z)/wp;
  float z=(pM->m[2][0]*pV->x+pM->m[2][1]*pV->y+pM->m[2][2]*pV->z)/wp;
  pOut->x=x;pOut->y=y;pOut->z=z;
 }
 return pOut;
}
TOGL_EXPORT D3DXMATRIX* D3DXMatrixTranslation(D3DXMATRIX* pOut,FLOAT x,FLOAT y,FLOAT z) { D3DXMatrixIdentity(pOut);pOut->m[3][0]=x;pOut->m[3][1]=y;pOut->m[3][2]=z;return pOut; }
// Gauss-Jordan with partial pivoting (Source's MatrixInverseGeneral).
TOGL_EXPORT D3DXMATRIX* D3DXMatrixInverse(D3DXMATRIX* pOut,FLOAT*,const D3DXMATRIX* pM) {
 float mat[4][8];
 for(int i=0;i<4;i++)for(int j=0;j<4;j++){mat[i][j]=pM->m[i][j];mat[i][j+4]=i==j?1.f:0.f;}
 int rows[4]={0,1,2,3};
 for(int c=0;c<4;c++){
  int best=c;float bestValue=std::fabs(mat[rows[c]][c]);
  for(int r=c+1;r<4;r++)if(std::fabs(mat[rows[r]][c])>bestValue){best=r;bestValue=std::fabs(mat[rows[r]][c]);}
  if(bestValue==0.f)return pOut;
  std::swap(rows[c],rows[best]);
  float* pivot=mat[rows[c]];float inv=1.f/pivot[c];
  for(int k=0;k<8;k++)pivot[k]*=inv;
  for(int r=0;r<4;r++)if(r!=c){float* row=mat[rows[r]];float f=row[c];if(f!=0.f)for(int k=0;k<8;k++)row[k]-=f*pivot[k];}
 }
 for(int i=0;i<4;i++)for(int j=0;j<4;j++)pOut->m[i][j]=mat[rows[i]][j+4];
 return pOut;
}
// Returns null, as ToGL's does.
TOGL_EXPORT D3DXMATRIX* D3DXMatrixTranspose(D3DXMATRIX* pOut,const D3DXMATRIX* pM) {
 D3DXMATRIX t=*pM;
 for(int i=0;i<4;i++)for(int j=0;j<4;j++)pOut->m[i][j]=t.m[j][i];
 return nullptr;
}
TOGL_EXPORT D3DXPLANE* D3DXPlaneNormalize(D3DXPLANE* pOut,const D3DXPLANE* pP) {
 float len=std::sqrt(pP->a*pP->a+pP->b*pP->b+pP->c*pP->c);
 if(len>1e-10f){float a=pP->a/len,b=pP->b/len,c=pP->c/len,d=pP->d/len;pOut->a=a;pOut->b=b;pOut->c=c;pOut->d=d;}
 else{pOut->a=0;pOut->b=0;pOut->c=1;pOut->d=0;}
 return pOut;
}
TOGL_EXPORT D3DXVECTOR4* D3DXVec4Transform(D3DXVECTOR4* pOut,const D3DXVECTOR4* pV,const D3DXMATRIX* pM) {
 float in[4]={pV->x,pV->y,pV->z,pV->w},out[4];
 for(int i=0;i<4;i++)out[i]=pM->m[0][i]*in[0]+pM->m[1][i]*in[1]+pM->m[2][i]*in[2]+pM->m[3][i]*in[3];
 pOut->x=out[0];pOut->y=out[1];pOut->z=out[2];pOut->w=out[3];
 return pOut;
}
TOGL_EXPORT D3DXVECTOR4* D3DXVec4Normalize(D3DXVECTOR4* pOut,const D3DXVECTOR4* pV) {
 float x=pV->x,y=pV->y,z=pV->z,w=pV->w,len=std::sqrt(x*x+y*y+z*z+w*w);
 if(len!=0.f){pOut->x=x/len;pOut->y=y/len;pOut->z=z/len;pOut->w=w/len;}
 else{pOut->x=pOut->y=pOut->z=pOut->w=0;}
 return pOut;
}
TOGL_EXPORT D3DXMATRIX* D3DXMatrixOrthoOffCenterRH(D3DXMATRIX*,FLOAT,FLOAT,FLOAT,FLOAT,FLOAT,FLOAT) { return nullptr; }
TOGL_EXPORT D3DXMATRIX* D3DXMatrixPerspectiveRH(D3DXMATRIX*,FLOAT,FLOAT,FLOAT,FLOAT) { return nullptr; }
TOGL_EXPORT D3DXMATRIX* D3DXMatrixPerspectiveOffCenterRH(D3DXMATRIX*,FLOAT,FLOAT,FLOAT,FLOAT,FLOAT,FLOAT) { return nullptr; }
TOGL_EXPORT D3DXPLANE* D3DXPlaneTransform(D3DXPLANE* pOut,const D3DXPLANE* pP,const D3DXMATRIX* pM) {
 float in[4]={pP->a,pP->b,pP->c,pP->d},out[4];
 for(int x=0;x<4;x++)out[x]=pM->m[0][x]*in[0]+pM->m[1][x]*in[1]+pM->m[2][x]*in[2]+pM->m[3][x]*in[3];
 pOut->a=out[0];pOut->b=out[1];pOut->c=out[2];pOut->d=out[3];
 return pOut;
}
// Runtime HLSL compilation: ToGL has none.
TOGL_EXPORT HRESULT D3DXCompileShader(const char*,UINT,const _D3DXMACRO*,ID3DXInclude*,const char*,const char*,DWORD,ID3DXBuffer**,ID3DXBuffer**,ID3DXConstantTable**) { return 0; }
}
