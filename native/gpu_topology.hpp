#pragma once
#include "runtime.hpp"
#include "mesh_topology.hpp"
#include <algorithm>
#include <span>
namespace mmd {
// Draw batches for Source hardware skinning. A batch references at most
// `maxBones` palette entries, loaded into matrix slots 0..n-1 before its draw;
// its vertices store those batch-local slots, so a vertex used by two batches
// is duplicated. Triangles of one material are ordered by their dominant bone
// first, which keeps limbs together and the batch count close to the minimum.
struct SkinBatch {unsigned chunk=0,first=0,count=0;std::vector<unsigned> bones;};
struct SkinChunk: DrawChunk {std::vector<std::array<uint8_t,3>> slots;std::vector<std::array<float,2>> weights;};
struct SkinTopology {std::vector<SkinChunk> chunks;std::vector<std::vector<SkinBatch>> parts;size_t gpuTriangles=0,cpuTriangles=0,batches=0,vertices=0;};
inline SkinTopology skinTopology(const Model& model,const GpuSkin& plan,std::span<const uint8_t> viewMask={},unsigned maxBones=GpuSkin::MaxBones){
    SkinTopology out;out.parts.resize(model.materials.size());
    std::vector<int> slot(plan.palette,-1);std::vector<unsigned> current,order;std::vector<std::pair<unsigned,unsigned>> keyed;
    std::unordered_map<unsigned,unsigned short> remap;
    for(size_t part=0;part<model.materials.size();part++){
        const auto& m=model.materials[part];keyed.clear();
        for(unsigned first=m.first;first<m.first+m.count;first+=3){
            unsigned t=first/3;if(!viewMask.empty()&&!viewMask[t])continue;
            if(plan.cpuTriangle[t]){out.cpuTriangles++;continue;}
            // Dominant bone: the largest summed weight over the three vertices.
            std::array<std::pair<unsigned,float>,9> sums{};int used=0;
            for(int k=0;k<3;k++){unsigned v=model.indices[first+k];for(int j=0;j<3;j++){float w=plan.weights[v][j];if(w<=0)continue;unsigned b=plan.bones[v][j];int e=0;while(e<used&&sums[e].first!=b)e++;if(e==used)sums[used++]={b,0.f};sums[e].second+=w;}}
            unsigned dominant=used?std::max_element(sums.begin(),sums.begin()+used,[](auto& a,auto& b){return a.second<b.second;})->first:0;
            keyed.push_back({dominant,t});
        }
        std::stable_sort(keyed.begin(),keyed.end(),[](auto& a,auto& b){return a.first<b.first;});
        auto& batches=out.parts[part];bool open=false;
        auto close=[&]{if(open)batches.back().bones=current;for(auto b:current)slot[b]=-1;current.clear();remap.clear();open=false;};
        for(const auto& entry:keyed){const unsigned t=entry.second;
            unsigned fresh=0;std::array<unsigned,9> added{};
            auto collect=[&]{fresh=0;for(int k=0;k<3;k++){unsigned v=model.indices[t*3+k];for(int j=0;j<3;j++){unsigned b=plan.bones[v][j];if(slot[b]>=0||std::find(added.begin(),added.begin()+fresh,b)!=added.begin()+fresh)continue;added[fresh++]=b;}}};
            collect();
            bool full=out.chunks.empty()||out.chunks.back().vertices.size()+3>32760||out.chunks.back().indices.size()+3>60000;
            if(open&&(full||current.size()+fresh>maxBones)){close();collect();}
            if(full){out.chunks.emplace_back();remap.clear();}
            auto& chunk=out.chunks.back();unsigned ci=unsigned(out.chunks.size()-1);
            if(!open){batches.push_back({ci,unsigned(chunk.indices.size()),0,{}});open=true;out.batches++;}
            for(unsigned k=0;k<fresh;k++){slot[added[k]]=int(current.size());current.push_back(added[k]);}
            chunk.triangles.push_back(t);
            for(int k=0;k<3;k++){
                unsigned v=model.indices[t*3+k];auto [entry,inserted]=remap.emplace(v,static_cast<unsigned short>(chunk.vertices.size()));
                if(inserted){chunk.vertices.push_back(v);chunk.slots.push_back({uint8_t(slot[plan.bones[v][0]]),uint8_t(slot[plan.bones[v][1]]),uint8_t(slot[plan.bones[v][2]])});chunk.weights.push_back({plan.weights[v][0],plan.weights[v][1]});out.vertices++;}
                chunk.indices.push_back(entry->second);
            }
            batches.back().count+=3;out.gpuTriangles++;
        }
        close();
    }
    return out;
}
}
