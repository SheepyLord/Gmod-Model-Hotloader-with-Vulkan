#pragma once
#include "runtime.hpp"
#include <unordered_map>
#include <cmath>

namespace mmd {
// A stable, camera-independent depth tie for fixed-function ray-traced capture.
// Units are Source inches; the total separation is always below 0.05 units.
inline float remixLayerSeparation(unsigned rank,unsigned maximumRank){
 return std::min(rank,maximumRank)*std::min(.005f,.05f/(float(maximumRank)+1.f));
}
template<class T,size_t N> struct ArrayHash {
 size_t operator()(const std::array<T,N>& a)const{
  size_t h=0;for(auto v:a)h^=std::hash<T>{}(v)+0x9e3779b9+(h<<6)+(h>>2);return h;
 }
};
inline void buildLightOverlaps(Model& m){
 constexpr float epsilon=1e-4f;
 using Cell=std::array<int64_t,3>;using Key=std::array<unsigned,3>;
 std::unordered_map<Cell,std::vector<unsigned>,ArrayHash<int64_t,3>> cells;
 std::vector<unsigned> points(m.vertices.size());
 for(unsigned i=0;i<m.vertices.size();i++){
  auto p=m.vertices[i].position;Cell cell{};bool bounded=true;
  for(int k=0;k<3;k++){double v=std::floor(double(p[k])/epsilon);if(!std::isfinite(v)||std::abs(v)>1e12){bounded=false;break;}cell[k]=int64_t(v);}
  points[i]=i;if(!bounded)continue;bool found=false;
  for(int x=-1;x<=1&&!found;x++)for(int y=-1;y<=1&&!found;y++)for(int z=-1;z<=1&&!found;z++){
   auto it=cells.find({cell[0]+x,cell[1]+y,cell[2]+z});if(it==cells.end())continue;
   for(unsigned j:it->second)if((m.vertices[j].position-p).length2()<=epsilon*epsilon){points[i]=j;found=true;break;}
  }
  if(!found)cells[cell].push_back(i);
 }
 std::unordered_map<Key,std::vector<OverlapTriangle>,ArrayHash<unsigned,3>> groups;
 for(unsigned part=0;part<m.materials.size();part++){
  const auto& material=m.materials[part];
  for(unsigned first=material.first;first<material.first+material.count;first+=3){
   std::array<unsigned,3> v{m.indices[first],m.indices[first+1],m.indices[first+2]};
   std::array<unsigned,3> p{points[v[0]],points[v[1]],points[v[2]]};
   if(p[0]==p[1]||p[1]==p[2]||p[0]==p[2])continue;
   auto a=m.vertices[v[1]].position-m.vertices[v[0]].position,b=m.vertices[v[2]].position-m.vertices[v[0]].position;
   if(a.cross(b).length2()<1e-16f)continue;
   unsigned k=unsigned(std::min_element(p.begin(),p.end())-p.begin());
   // Cyclic order preserves winding: intentional backfaces remain separate.
   groups[{p[k],p[(k+1)%3],p[(k+2)%3]}].push_back({first/3,{v[k],v[(k+1)%3],v[(k+2)%3]},part});
  }
 }
 m.lightOverlaps.clear();
 m.lightLayerRanks.assign(m.materials.size(),0);
 std::vector<std::vector<unsigned>> next(m.materials.size());
 for(auto& [key,group]:groups)if(group.size()>1){
  // Overlaid *materials* require per-pixel ownership, because their alpha
  // masks may differ. Give later layers distinct depth ties instead of
  // deleting triangles or sampling alpha on the CPU.
  for(size_t i=0;i<group.size();){
   size_t j=i+1;while(j<group.size()&&group[j].material==group[i].material)++j;
   if(j-i>1)m.lightOverlaps.emplace_back(group.begin()+i,group.begin()+j);
   if(j<group.size())next[group[i].material].push_back(group[j].material);
   i=j;
  }
 }
 for(unsigned i=0;i<next.size();i++){
  auto& edges=next[i];std::sort(edges.begin(),edges.end());edges.erase(std::unique(edges.begin(),edges.end()),edges.end());
  for(unsigned j:edges)m.lightLayerRanks[j]=std::max(m.lightLayerRanks[j],m.lightLayerRanks[i]+1);
 }
}
inline std::vector<uint8_t> lightOverlapMask(const Model& model,const Snapshot& snapshot,float tolerance,
                                          std::span<const uint8_t> viewMask={}){
 if(model.lightOverlaps.empty())return {};
 std::vector<uint8_t> keep(model.indices.size()/3,1);
 auto visible=[&](unsigned p){return viewMask.empty()||viewMask[p]!=0;};
 for(const auto& group:model.lightOverlaps)for(size_t i=0;i+1<group.size();i++){
  auto& a=group[i];if(!visible(a.primitive))continue;
  for(size_t j=group.size();j-->i+1;){
   auto& b=group[j];if(!visible(b.primitive))continue;bool same=true;
   for(int k=0;k<3;k++){
    const auto& u=snapshot.vertices[a.vertices[k]];const auto& v=snapshot.vertices[b.vertices[k]];
    float x=u.x-v.x,y=u.y-v.y,z=u.z-v.z;
    // Match alpha-test coverage too. UV morphs may separate two otherwise
    // coincident surfaces; never erase those independent layers.
    if(x*x+y*y+z*z>tolerance*tolerance||std::abs(u.u-v.u)>1e-5f||std::abs(u.v-v.v)>1e-5f){same=false;break;}
   }
   // Use one owner in BOTH base/depth-writing and additive light passes.
   // Even sub-tolerance depth differences can make hardware choose an earlier
   // copy, so filtering only the light pass would leave dark/unlit triangles.
   if(same){keep[a.primitive]=0;break;}
  }
 }
 return keep;
}
}
