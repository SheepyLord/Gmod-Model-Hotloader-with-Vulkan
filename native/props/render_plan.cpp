#include "core.hpp"
#include "material_processing.hpp"
#include <algorithm>
#include <numeric>

namespace props {
float propScale(double scale) {
    // NetworkVar floats round the public lower endpoint slightly below .01.
    if(!std::isfinite(scale)||scale<.01-1e-9||scale>100)
        throw std::runtime_error("Prop size must be between 0.01 and 100");
    return float(std::max(.01,scale));
}
void validateHullScale(const std::vector<Hull>& hulls,float scale) {
    propScale(scale);
    for(const auto& h:hulls) for(auto p:h.points)
        if(std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})*scale>32768)
            throw std::runtime_error("Scaled collider exceeds Source physics limits; reduce prop size");
}
Hit traceScaled(const std::vector<Hull>& hulls,Vec start,Vec delta,const std::array<Vec,3>& axes,float scale) {
    scale=propScale(scale);const float inverse=1/scale;
    return trace(hulls,start*inverse,delta*inverse,{axes[0]*inverse,axes[1]*inverse,axes[2]*inverse});
}
Vertex renderVertex(const Asset& a,size_t index,bool backface) {
    if(backface) { auto corner=index%3; if(corner)index=index-corner+(3-corner); }
    if(index>=a.renderIndices.size())throw std::runtime_error("Invalid render vertex offset");
    auto v=a.vertices.at(a.renderIndices[index]);
    v.pos=v.pos-a.renderOrigin;
    if(backface){v.normal=v.normal*-1;v.tangent[3]*=-1;}
    return v;
}
void prepareRenderPlan(Asset& a) {
    // Alpha inference for old caches is a runtime view: byte identity stays intact.
    auto original=a.manifest;
    try { analyzeMaterials(a,{});a.renderManifest=a.manifest;a.manifest=original; }
    catch(...) { a.manifest=std::move(original);throw; }
    a.renderIndices.clear();a.renderIndices.reserve(a.indices.size());
    a.renderParts=Json::array();a.renderChunks=Json::array();
    a.renderOrigin=(a.manifest.at("mins").get<Vec>()+a.manifest.at("maxs").get<Vec>())*.5f;
    const auto& materials=a.renderManifest.at("materials");
    std::vector<std::vector<uint32_t>> groups(materials.size());
    size_t blendTriangles=0;
    for(const auto& p:a.manifest.at("parts")) {
        auto m=p.at("material").get<size_t>(),first=p.at("first").get<size_t>(),count=p.at("count").get<size_t>();
        auto& g=groups.at(m);
        for(size_t i=first;i<first+count;i+=3)g.push_back(uint32_t(i));
        if(materials[m].value("alpha_mode",std::string("opaque"))=="blend")blendTriangles+=count/3;
    }
    // Adapt cluster count, but never grow an indivisible GPU upload for a very
    // dense model. Large accepted assets use more clusters, not oversized ones.
    const size_t blendLeaf=std::min<size_t>(6000,std::max<size_t>(128,(blendTriangles+1023)/1024));
    auto centroid=[&](uint32_t tri){return (a.vertices[a.indices[tri]].pos+a.vertices[a.indices[tri+1]].pos+a.vertices[a.indices[tri+2]].pos)*(1.f/3);};
    for(size_t material=0;material<groups.size();++material) {
        auto& g=groups[material];bool blend=materials[material].value("alpha_mode",std::string("opaque"))=="blend";
        bool twoSided=materials[material].value("two_sided",false);
        auto emit=[&](size_t begin,size_t end) {
            if(begin==end)return;
            Vec mn{INFINITY,INFINITY,INFINITY},mx{-INFINITY,-INFINITY,-INFINITY};
            const auto first=a.renderIndices.size(),part=a.renderParts.size();
            for(size_t n=begin;n<end;++n)for(size_t k=0;k<3;++k){
                // Cache/import geometry uses outward CCW faces. Source's IMesh
                // front face is clockwise: convert exactly once in the render
                // view, leaving collision, source normals and content IDs intact.
                // Without this swap two-sided meshes expose their inverted back
                // normals, so a flashlight in front lights almost nothing.
                auto index=a.indices[g[n]+(k?3-k:0)];a.renderIndices.push_back(index);auto p=a.vertices[index].pos-a.renderOrigin;
                mn={std::min(mn.x,p.x),std::min(mn.y,p.y),std::min(mn.z,p.z)};
                mx={std::max(mx.x,p.x),std::max(mx.y,p.y),std::max(mx.z,p.z)};
            }
            auto count=a.renderIndices.size()-first;
            a.renderParts.push_back({{"first",first},{"count",count},{"material",material}});
            Json chunk={{"part",part},{"offset",0},{"count",count},{"material",material},{"mins",mn},{"maxs",mx},{"center",(mn+mx)*.5f},{"backface",false}};
            a.renderChunks.push_back(chunk);
            if(twoSided){chunk["backface"]=true;a.renderChunks.push_back(std::move(chunk));}
        };
        std::function<void(size_t,size_t)> split=[&](size_t begin,size_t end){
            if(end-begin<=blendLeaf){emit(begin,end);return;}
            Vec mn{INFINITY,INFINITY,INFINITY},mx{-INFINITY,-INFINITY,-INFINITY};
            for(size_t n=begin;n<end;++n){auto c=centroid(g[n]);mn={std::min(mn.x,c.x),std::min(mn.y,c.y),std::min(mn.z,c.z)};mx={std::max(mx.x,c.x),std::max(mx.y,c.y),std::max(mx.z,c.z)};}
            auto extent=mx-mn;int axis=extent.y>extent.x?1:0;if(extent.z>(axis?extent.y:extent.x))axis=2;
            auto coordinate=[&](uint32_t i){auto c=centroid(i);return axis==0?c.x:axis==1?c.y:c.z;};
            size_t mid=begin+(end-begin)/2;
            std::nth_element(g.begin()+begin,g.begin()+mid,g.begin()+end,[&](uint32_t x,uint32_t y){auto a=coordinate(x),b=coordinate(y);return a==b?x<y:a<b;});
            split(begin,mid);split(mid,end);
        };
        if(blend)split(0,g.size());else for(size_t first=0;first<g.size();first+=6000)emit(first,std::min(g.size(),first+6000));
    }
    a.renderManifest["parts"]=a.renderParts;
    a.renderManifest["render_chunks"]=a.renderChunks;
    a.renderManifest["render_backfaces"]=true;
    a.renderManifest["render_winding"]="source_cw";
    a.renderManifest["render_origin"]=a.renderOrigin;
}
}
