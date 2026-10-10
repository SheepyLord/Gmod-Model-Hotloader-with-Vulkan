#include "dds.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace mmd {
namespace {
uint32_t u32(std::span<const unsigned char> b,size_t at){if(at+4>b.size())throw std::runtime_error("Truncated DDS header");return uint32_t(b[at])|uint32_t(b[at+1])<<8|uint32_t(b[at+2])<<16|uint32_t(b[at+3])<<24;}
constexpr uint32_t fourcc(const char* s){return uint32_t(uint8_t(s[0]))|uint32_t(uint8_t(s[1]))<<8|uint32_t(uint8_t(s[2]))<<16|uint32_t(uint8_t(s[3]))<<24;}
void color565(uint16_t c,unsigned char* rgb){unsigned r=c>>11&31,g=c>>5&63,b=c&31;rgb[0]=uint8_t(r<<3|r>>2);rgb[1]=uint8_t(g<<2|g>>4);rgb[2]=uint8_t(b<<3|b>>2);}
// One 4x4 colour block (8 bytes); opaqueOnly: BC2/BC3 colour blocks always use four colours.
void colorBlock(const unsigned char* p,unsigned char out[16][4],bool opaqueOnly){
 uint16_t c0=uint16_t(p[0]|p[1]<<8),c1=uint16_t(p[2]|p[3]<<8);unsigned char c[4][4]={};
 color565(c0,c[0]);color565(c1,c[1]);c[0][3]=c[1][3]=255;
 if(c0>c1||opaqueOnly){for(int k=0;k<3;k++){c[2][k]=uint8_t((2*c[0][k]+c[1][k]+1)/3);c[3][k]=uint8_t((c[0][k]+2*c[1][k]+1)/3);}c[2][3]=c[3][3]=255;}
 else{for(int k=0;k<3;k++)c[2][k]=uint8_t((c[0][k]+c[1][k]+1)/2);c[2][3]=255;c[3][0]=c[3][1]=c[3][2]=0;c[3][3]=0;}
 uint32_t bits=uint32_t(p[4])|uint32_t(p[5])<<8|uint32_t(p[6])<<16|uint32_t(p[7])<<24;
 for(int i=0;i<16;i++)std::memcpy(out[i],c[bits>>(2*i)&3],4);
}
void alphaBc3(const unsigned char* p,unsigned char out[16][4]){
 unsigned a[8];a[0]=p[0];a[1]=p[1];
 if(a[0]>a[1])for(int i=1;i<7;i++)a[i+1]=((7-i)*a[0]+i*a[1]+3)/7;
 else{for(int i=1;i<5;i++)a[i+1]=((5-i)*a[0]+i*a[1]+2)/5;a[6]=0;a[7]=255;}
 uint64_t bits=0;for(int i=0;i<6;i++)bits|=uint64_t(p[2+i])<<(8*i);
 for(int i=0;i<16;i++)out[i][3]=uint8_t(a[bits>>(3*i)&7]);
}
void alphaBc2(const unsigned char* p,unsigned char out[16][4]){for(int i=0;i<16;i++){unsigned v=p[i/2]>>(4*(i&1))&15;out[i][3]=uint8_t(v<<4|v);}}
int shiftOf(uint32_t mask){if(!mask)return 0;int s=0;while(!(mask&1)){mask>>=1;s++;}return s;}
int bitsOf(uint32_t mask){int n=0;while(mask){n+=int(mask&1);mask>>=1;}return n;}
uint8_t channel(uint32_t pixel,uint32_t mask){if(!mask)return 255;int n=bitsOf(mask);uint32_t v=(pixel&mask)>>shiftOf(mask);if(n>=8)return uint8_t(v>>(n-8));return uint8_t((v*255+((1u<<n)-1)/2)/((1u<<n)-1));}
}
std::vector<unsigned char> decodeDds(std::span<const unsigned char> b,int& width,int& height){
 if(b.size()<128||std::memcmp(b.data(),"DDS ",4)||u32(b,4)!=124)throw std::runtime_error("Unsupported DDS texture (not a DDS file)");
 uint32_t h=u32(b,12),w=u32(b,16),pfFlags=u32(b,80),code=u32(b,84),bitCount=u32(b,88),rMask=u32(b,92),gMask=u32(b,96),bMask=u32(b,100),aMask=u32(b,104);
 if(!w||!h||w>16384||h>16384)throw std::runtime_error("DDS dimensions exceed the texture limit");
 size_t at=128;width=int(w);height=int(h);std::vector<unsigned char> rgba(size_t(w)*h*4);
 const bool fourCC=(pfFlags&0x4)!=0;
 if(fourCC&&code==fourcc("DX10"))throw std::runtime_error("Unsupported DDS texture (supported: BC1, BC2, BC3 and uncompressed colour)");
 if(fourCC){
  int kind=code==fourcc("DXT1")?1:(code==fourcc("DXT2")||code==fourcc("DXT3"))?2:(code==fourcc("DXT4")||code==fourcc("DXT5"))?3:0;
  if(!kind)throw std::runtime_error("Unsupported DDS texture (supported: BC1, BC2, BC3 and uncompressed colour)");
  size_t blockBytes=kind==1?8:16,bw=(w+3)/4,bh=(h+3)/4;
  if(b.size()-at<bw*bh*blockBytes)throw std::runtime_error("Truncated DDS texture data");
  unsigned char block[16][4];
  for(size_t by=0;by<bh;by++)for(size_t bx=0;bx<bw;bx++){
   const unsigned char* p=b.data()+at+(by*bw+bx)*blockBytes;
   if(kind==1)colorBlock(p,block,false);else{colorBlock(p+8,block,true);if(kind==2)alphaBc2(p,block);else alphaBc3(p,block);}
   for(int y=0;y<4;y++)for(int x=0;x<4;x++){size_t px=bx*4+size_t(x),py=by*4+size_t(y);if(px<w&&py<h)std::memcpy(&rgba[(py*w+px)*4],block[y*4+x],4);}
  }
  return rgba;
 }
 if(!(pfFlags&0x40)||(bitCount!=32&&bitCount!=24))throw std::runtime_error("Unsupported DDS texture (supported: BC1, BC2, BC3 and uncompressed colour)");
 size_t bytes=bitCount/8,pitch=size_t(w)*bytes;const bool hasAlpha=(pfFlags&0x1)!=0;
 if(b.size()-at<pitch*h)throw std::runtime_error("Truncated DDS texture data");
 for(size_t y=0;y<h;y++)for(size_t x=0;x<w;x++){
  const unsigned char* p=b.data()+at+y*pitch+x*bytes;uint32_t pixel=uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|(bytes==4?uint32_t(p[3])<<24:0);
  auto* o=&rgba[(y*w+x)*4];o[0]=channel(pixel,rMask);o[1]=channel(pixel,gMask);o[2]=channel(pixel,bMask);o[3]=hasAlpha?channel(pixel,aMask):255;
 }
 return rgba;
}
}
