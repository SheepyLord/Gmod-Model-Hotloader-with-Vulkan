#pragma once
#include "runtime.hpp"
#include <emmintrin.h>
#include <cstring>
namespace mmd {
// The snapshot vertex is the verified Source common layout (one 64-byte cache
// line), so the upload is four full streaming stores; the engine padding after
// the tangent is written as zero. The caller validates the buffer layout and
// alignment and fences before unlocking the buffer.
inline void streamSourceVertex(float* aligned,const DrawVertex& v){
    const float* source=reinterpret_cast<const float*>(&v);
    _mm_stream_ps(aligned,_mm_loadu_ps(source));
    _mm_stream_ps(aligned+4,_mm_loadu_ps(source+4));
    _mm_stream_ps(aligned+8,_mm_loadu_ps(source+8));
    _mm_stream_ps(aligned+12,_mm_move_ss(_mm_setzero_ps(),_mm_loadu_ps(source+12)));
}
// Source's compressed-vertex encoding (COMPRESSED_NORMALS_COMBINEDTANGENTS_UBYTE4):
// a unit vector projected onto the octant plane x+y+z=1, 64 levels per axis,
// sign bits folded in. The normal takes the low 16 bits and the tangent, with
// the binormal sign, the high 16 bits. mathlib's PackNormal_UBYTE4 truncates to
// a level (up to 3.7 degrees off); this rounds to the nearest one and folds the
// signs exactly as the shader's _DecompressUByte4Normal unfolds them.
inline uint32_t packUbyte4(float nx,float ny,float nz,bool tangent,float binormalSign){
    float ax=std::abs(nx),ay=std::abs(ny),sum=ax+ay+std::abs(nz);
    if(!(sum>1e-20f)){nx=ny=ax=ay=0;nz=sum=1;} // degenerate input: +Z
    auto level=[](float v){int q=int(v*63+.5f);return q<0?0:q>63?63:q;};
    int qx=level(ax/sum),qy=level(ay/sum);
    int bx=nx<0?63-qx:64+qx,by=ny<0?63-qy:64+qy;                            // 0..127
    uint32_t bits=uint32_t(nz<0?127-bx:128+bx)|(uint32_t(binormalSign<0?127-by:128+by)<<8);
    return tangent?bits<<16:bits;
}
inline uint32_t packNormalTangent(const DrawVertex& v){
    return packUbyte4(v.nx,v.ny,v.nz,false,1.f)|packUbyte4(v.tx,v.ty,v.tz,true,v.tw<0?-1.f:1.f);
}
// Decode as Source's vertex shaders do (_DecompressUByte4Normal); used by tests.
inline void unpackUbyte4(uint32_t bits,bool tangent,float out[3],float* binormalSign=nullptr){
    if(tangent)bits>>=16;float x=float(bits&255),y=float((bits>>8)&255);
    float zSign=(x-128)<0?1.f:0.f,tSign=(y-128)<0?1.f:0.f;
    float ax=std::abs(x-128)-zSign,ay=std::abs(y-128)-tSign;
    float xSign=(ax-64)<0?1.f:0.f,ySign=(ay-64)<0?1.f:0.f;
    float nx=(std::abs(ax-64)-xSign)/63,ny=(std::abs(ay-64)-ySign)/63,nz=1-nx-ny;
    float length=std::sqrt(nx*nx+ny*ny+nz*nz);nx/=length;ny/=length;nz/=length;
    out[0]=xSign?-nx:nx;out[1]=ySign?-ny:ny;out[2]=zSign?-nz:nz;
    if(binormalSign)*binormalSign=tSign?-1.f:1.f;
}
// The compressed common layout (32 bytes): position, packed normal+tangent,
// colour, UV, zero padding. Integer stores keep every bit pattern intact.
inline void streamCompactVertex(float* aligned,const DrawVertex& v){
    int32_t x,y,z,u,w;std::memcpy(&x,&v.x,4);std::memcpy(&y,&v.y,4);std::memcpy(&z,&v.z,4);std::memcpy(&u,&v.u,4);std::memcpy(&w,&v.v,4);
    _mm_stream_si128(reinterpret_cast<__m128i*>(aligned),_mm_setr_epi32(x,y,z,int32_t(packNormalTangent(v))));
    _mm_stream_si128(reinterpret_cast<__m128i*>(aligned+4),_mm_setr_epi32(int32_t(v.color),u,w,0));
}
}
