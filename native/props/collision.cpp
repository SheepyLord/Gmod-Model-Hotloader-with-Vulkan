#include "core.hpp"
#include <windows.h>
#include <meshoptimizer.h>
#include <algorithm>

namespace props {
// Stable C ABI from the pinned CoACD 1.0.14 wheel (see dependencies.lock.json).
struct CoMesh{double* vertices;uint64_t vertexCount;int* triangles;uint64_t triangleCount;};
struct CoArray{CoMesh* meshes;uint64_t count;};
using CoRun=CoArray(*)(const CoMesh*,double,int,int,int,int,int,int,int,bool,bool,bool,int,bool,double,int,unsigned,bool);
namespace {
void warn(Asset& a,const std::string& text){auto& w=a.manifest["warnings"];if(!w.is_array())w=Json::array();w.push_back(text);}
void fastCollision(Asset& a,const Progress& progress){
    std::string note;a.hulls={makeFastHull(a,progress,&note)};
    a.manifest["collision_method"]=note.empty()?"direct_support_hull_26":"bounding_box";
    a.manifest["collision_triangles"]=a.indices.size()/3;
    if(!note.empty())warn(a,note);
    progress("Validating collision hulls",.86f,{},1,1);
    parseHulls(hullJson(a.hulls));
}
void detailedCollision(Asset& a,const std::string& mode,const Progress& progress);
}
// The import never fails because of collision: detailed decomposition falls
// back to the fast single hull, which falls back to a thickened bounding box.
void makeCollision(Asset& a,const std::string& mode,const Progress& progress){
    if(mode!="hull"){
        try{detailedCollision(a,mode,progress);a.manifest["collision_method"]="coacd";return;}
        catch(const std::exception& e){a.hulls.clear();warn(a,std::string("Detailed collision failed (")+e.what()+"); a single enclosing hull is used instead.");}
    }
    fastCollision(a,progress);
}
namespace {
void detailedCollision(Asset& a,const std::string& mode,const Progress& progress){
    // CoACD owns runtime threads. Keep its DLL loaded until this short-lived worker exits.
    wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);auto dllPath=fs::path(path).parent_path()/L"lib_coacd.dll";HMODULE library=LoadLibraryExW(dllPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!library)throw std::runtime_error("Cannot load CoACD: install lib_coacd.dll beside the worker and Microsoft Visual C++ 2015-2022 x64 Redistributable (VCOMP140)");
    auto run=reinterpret_cast<CoRun>(GetProcAddress(library,"CoACD_run"));auto freeMesh=reinterpret_cast<void(*)(CoArray)>(GetProcAddress(library,"CoACD_freeMeshArray"));auto log=reinterpret_cast<void(*)(const char*)>(GetProcAddress(library,"CoACD_setLogLevel"));if(!run||!freeMesh){FreeLibrary(library);throw std::runtime_error("CoACD ABI mismatch");}if(log)log("error");
    // Welding by position is essential: UV/material seams are not holes in the collider.
    progress("Welding collision geometry",.55f,{},0,a.indices.size()/3);
    std::vector<unsigned> remap(a.vertices.size());meshopt_generatePositionRemap(remap.data(),reinterpret_cast<const float*>(&a.vertices[0].pos),a.vertices.size(),sizeof(Vertex));
    std::vector<Vec> vertices(a.vertices.size());for(size_t i=0;i<a.vertices.size();i++)vertices[remap[i]]=a.vertices[i].pos;
    std::vector<unsigned> indices(a.indices.size());for(size_t i=0;i<indices.size();i++)indices[i]=remap[a.indices[i]];
    progress("Simplifying collision geometry",.58f,"Visual geometry is preserved",0,indices.size()/3);
    if(indices.size()>18000){std::vector<unsigned> simplified(indices.size());auto n=meshopt_simplify(simplified.data(),indices.data(),indices.size(),reinterpret_cast<const float*>(vertices.data()),vertices.size(),sizeof(Vec),18000,.025f,0,nullptr);if(n>=12){simplified.resize(n);indices=std::move(simplified);}}
    std::vector<unsigned> dense(vertices.size(),~0u);std::vector<double> xyz;std::vector<int> tris;for(auto i:indices){if(dense[i]==~0u){dense[i]=unsigned(xyz.size()/3);auto p=vertices[i];xyz.insert(xyz.end(),{p.x,p.y,p.z});}tris.push_back(int(dense[i]));}
    CoMesh mesh{xyz.data(),xyz.size()/3,tris.data(),tris.size()/3};a.manifest["collision_triangles"]=mesh.triangleCount;progress(mode=="hull"?"Building outer hull":"Decomposing collision",.65f,std::to_string(mesh.triangleCount)+" collision triangles; this stage has no percentage estimate",0,0,true);
    CoArray result=run(&mesh,mode=="hull"?1.0:.05,mode=="hull"?1:16,0,30,600,8,20,2,false,true,true,64,false,.01,0,0,false);
    progress("Validating collision hulls",.86f,{},result.count,result.count);
    size_t repaired=0,dropped=0;
    try{
        if(!result.count||result.count>16)throw std::runtime_error("the decomposition returned "+std::to_string(result.count)+" pieces");
        for(size_t i=0;i<result.count;i++){
            auto& src=result.meshes[i];Hull h;
            for(size_t v=0;v<src.vertexCount;v++)h.points.push_back({float(src.vertices[v*3]),float(src.vertices[v*3+1]),float(src.vertices[v*3+2])});
            bool valid=src.vertexCount>=4&&src.vertexCount<=64&&src.triangleCount<=128;
            for(size_t t=0;valid&&t<src.triangleCount*3;t++){if(src.triangles[t]<0||uint64_t(src.triangles[t])>=src.vertexCount)valid=false;else h.indices.push_back(uint32_t(src.triangles[t]));}
            if(valid)try{parseHulls(hullJson({h}));}catch(...){valid=false;}
            // Sliver faces or too many points: rebuild the piece from its points.
            if(!valid){try{h=hullFromPoints(h.points);++repaired;}catch(...){++dropped;continue;}}
            a.hulls.push_back(std::move(h));
        }
        if(a.hulls.empty())throw std::runtime_error("no usable pieces");
        parseHulls(hullJson(a.hulls));
    }catch(...){freeMesh(result);throw;}
    freeMesh(result);
    if(repaired||dropped)warn(a,"Detailed collision: "+std::to_string(repaired)+" piece(s) rebuilt and "+std::to_string(dropped)+" degenerate piece(s) skipped.");
}
}
}
