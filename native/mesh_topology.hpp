#pragma once
#include "runtime.hpp"
#include <limits>
namespace mmd {
struct DrawChunk {std::vector<unsigned> vertices;std::vector<unsigned short> indices;std::vector<unsigned> triangles;};
struct DrawRange {unsigned chunk,first,count;};
struct BatchedTopology {
    std::vector<DrawChunk> chunks;
    std::vector<std::vector<DrawRange>> parts;
};
// Keep material ranges and triangle order exactly, while sharing vertex buffers
// between materials. Limits are below both the SDK vertex and 16-bit index bounds.
inline BatchedTopology batchTopology(const Model& model,std::span<const uint8_t> triangleMask={}) {
    BatchedTopology out;out.parts.resize(model.materials.size());
    std::unordered_map<unsigned,unsigned short> remap;
    for(size_t part=0;part<model.materials.size();part++){
        auto& m=model.materials[part];
        for(unsigned first=m.first;first<m.first+m.count;first+=3){
            if(!triangleMask.empty()&&!triangleMask[first/3])continue;
            if(out.chunks.empty()||out.chunks.back().vertices.size()+3>32760||out.chunks.back().indices.size()+3>60000){out.chunks.emplace_back();remap.clear();}
            auto& chunk=out.chunks.back();auto& ranges=out.parts[part];unsigned ci=unsigned(out.chunks.size()-1);
            chunk.triangles.push_back(first/3);
            if(ranges.empty()||ranges.back().chunk!=ci)ranges.push_back({ci,unsigned(chunk.indices.size()),0});
            for(unsigned k=0;k<3;k++){
                auto index=model.indices[first+k];auto [entry,added]=remap.emplace(index,static_cast<unsigned short>(chunk.vertices.size()));
                if(added)chunk.vertices.push_back(index);chunk.indices.push_back(entry->second);
            }
            ranges.back().count+=3;
        }
    }
    return out;
}
// The index ranges of [first,first+count) one masked draw keeps: triangles both masks keep
// (an empty mask keeps all), the overlap guard's owners and the draw's soft-alpha subset.
// ids maps the range's triangles to model triangles (empty: implicitFirst onward). Returns
// how many of the subset's triangles the owner mask dropped (lit by another copy).
template<class Range> unsigned keptRanges(std::vector<Range>& out,unsigned first,unsigned count,std::span<const unsigned> ids,unsigned implicitFirst,
                                          std::span<const uint8_t> owner,std::span<const uint8_t> subset){
    out.clear();unsigned dropped=0;int start=-1;
    for(unsigned at=first;at<first+count;at+=3){
        unsigned triangle=ids.empty()?implicitFirst+at/3:ids[at/3];
        bool wanted=subset.empty()||subset[triangle]!=0,keep=wanted&&(owner.empty()||owner[triangle]!=0);
        if(keep&&start<0)start=int(at);
        if(!keep){if(wanted)dropped++;if(start>=0){out.emplace_back(start,int(at)-start);start=-1;}}
    }
    if(start>=0)out.emplace_back(start,int(first+count)-start);
    return dropped;
}
}
