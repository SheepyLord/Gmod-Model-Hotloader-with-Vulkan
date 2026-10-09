#pragma once
#include "runtime.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace mmd {
// RTX Remix traces alpha-tested draws as opaque until it has baked their opacity
// micromaps, so its renderer leaves out the triangles the materials' 0.5 alpha
// test removes entirely (renderer.cpp). loadAsset cuts at rest UVs; triangles a
// UV morph moves are cut again at each instance's current UVs. Both use this
// rule, so the two can never disagree.
// One bit per texel: alpha 128 and above passes $alphatestreference .5.
struct AlphaPassMask {
 int width=0,height=0;std::vector<uint64_t> bits;
 AlphaPassMask(const unsigned char* rgba,int w,int h):width(w),height(h),bits((size_t(w)*size_t(h)+63)/64,0){
  for(size_t i=0,n=size_t(w)*size_t(h);i<n;i++)if(rgba[i*4+3]>=128)bits[i>>6]|=uint64_t(1)<<(i&63);}
 // UVs wrap. A non-finite UV (out-of-range morph sums) samples nothing.
 bool passes(float u,float v)const{
  if(!std::isfinite(u)||!std::isfinite(v))return false;
  if(!(u>=0&&u<1))u-=std::floor(u);if(!(v>=0&&v<1))v-=std::floor(v);int x=std::min(width-1,int(u*float(width))),y=std::min(height-1,int(v*float(height)));
  size_t i=size_t(y)*size_t(width)+size_t(x);return ((bits[i>>6]>>(i&63))&1)!=0;}
};
// Passing samples of a triangle mapped to UVs a, b and c: its corners, edge
// midpoints and centre. Counting stops at `enough` (1: is anything left?).
inline int cutoutHits(const AlphaPassMask& mask,const std::array<float,2>& a,const std::array<float,2>& b,const std::array<float,2>& c,int enough=7){
 int hits=0;
 for(auto [s,r,q]:{std::array<float,3>{1,0,0},{0,1,0},{0,0,1},{.5f,.5f,0},{0,.5f,.5f},{.5f,0,.5f},{1/3.f,1/3.f,1/3.f}})
  if((hits+=mask.passes(a[0]*s+b[0]*r+c[0]*q,a[1]*s+b[1]*r+c[1]*q))>=enough)break;
 return hits;
}
// What RTX Remix draws of one instance at its current UVs. Immutable once made:
// draws queued for Source's render thread hold it.
struct RemixCutout {
 uint64_t revision=0;                       // content identity (the renderer rebuilds index lists on change)
 std::vector<uint8_t> keep;                 // per triangle: Model::cutoutTriangles, UV-morphed ones cut again
 std::vector<unsigned> kept;                // per part: triangles kept
 size_t keptTriangles=0,droppedTriangles=0; // of Model::uvCutoutTriangles
};
// vertices: a snapshot's, whose u/v carry the instance's UV morphs.
inline RemixCutout cutRemixTriangles(const Model& model,std::span<const DrawVertex> vertices){
 RemixCutout out;out.keep=model.cutoutTriangles;out.kept.assign(model.materials.size(),0);
 if(out.keep.size()*3!=model.indices.size()||vertices.size()!=model.vertices.size())return out;
 size_t part=0;
 for(auto t:model.uvCutoutTriangles){
  while(part<model.materials.size()&&size_t(model.materials[part].first)+model.materials[part].count<=size_t(t)*3)part++;
  if(part>=model.materials.size()||t>=out.keep.size())break;
  const AlphaPassMask* mask=part<model.cutoutMasks.size()?model.cutoutMasks[part].get():nullptr;if(!mask)continue;
  auto uv=[&](unsigned k){const auto& d=vertices[model.indices[size_t(t)*3+k]];return std::array<float,2>{d.u,d.v};};
  bool hit=cutoutHits(*mask,uv(0),uv(1),uv(2),1)!=0;out.keep[t]=hit?1:0;(hit?out.keptTriangles:out.droppedTriangles)++;
 }
 for(size_t p=0;p<model.materials.size();p++){const auto& m=model.materials[p];
  for(size_t k=m.first;k+2<size_t(m.first)+m.count&&k+2<model.indices.size();k+=3)out.kept[p]+=out.keep[k/3];}
 return out;
}
} // namespace mmd
