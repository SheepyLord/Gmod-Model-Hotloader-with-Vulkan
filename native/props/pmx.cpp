#include "core.hpp"
#include "texture_resolver.hpp"
#include <algorithm>
#include <cstring>
#include <map>

namespace props {
// Same size as imported MMD characters (SCMI: 0.08 m per PMX unit at 40.457
// Source units per metre), so MMD stages and accessories fit them.
constexpr float PmxSourceUnits=.08f*40.457f;
struct PMXReader {
    Bytes b;size_t p=0;uint8_t encoding{},extraUV{},vertexIndex{},textureIndex{},boneIndex{};
    void need(size_t n){if(n>b.size()-p)throw std::runtime_error("Truncated PMX geometry/material data");}
    template<class T>T get(){need(sizeof(T));T v;std::memcpy(&v,b.data()+p,sizeof(T));p+=sizeof(T);return v;}
    void skip(size_t n){need(n);p+=n;}
    int32_t count(uint32_t max){auto n=get<int32_t>();if(n<0||uint32_t(n)>max)throw std::runtime_error("PMX count exceeds limits");return n;}
    int32_t index(uint8_t bytes,bool sign=true){if(bytes==1)return sign?get<int8_t>():get<uint8_t>();if(bytes==2)return sign?get<int16_t>():get<uint16_t>();if(bytes==4)return get<int32_t>();throw std::runtime_error("Unsupported PMX index width");}
    std::string text(){auto n=count(1<<20);need(n);std::string s;if(encoding){s.assign(reinterpret_cast<char*>(b.data()+p),n);wide(s);}else{if(n%2)throw std::runtime_error("Odd-length UTF-16 PMX string");std::wstring w(n/2,0);std::memcpy(w.data(),b.data()+p,n);s=utf8(w);}p+=n;return s;}
    Vec vec(){return {get<float>(),get<float>(),get<float>()};}
};
Asset importPMX(const fs::path& path,const Options& o,const Progress& progress){
    progress("Reading PMX geometry",.02f,{},0,0,true);
    PMXReader r{readFile(path)};r.need(9);if(std::memcmp(r.b.data(),"PMX ",4))throw std::runtime_error("Invalid PMX signature");r.skip(4);float version=r.get<float>();if(!std::isfinite(version)||(std::abs(version-2.f)>.001f&&std::abs(version-2.1f)>.001f))throw std::runtime_error("Only PMX 2.0 and 2.1 are supported");auto headerSize=r.get<uint8_t>();if(headerSize<8||headerSize>64)throw std::runtime_error("Invalid PMX header");r.need(headerSize);r.encoding=r.get<uint8_t>();r.extraUV=r.get<uint8_t>();r.vertexIndex=r.get<uint8_t>();r.textureIndex=r.get<uint8_t>();auto materialIndex=r.get<uint8_t>();r.boneIndex=r.get<uint8_t>();auto morphIndex=r.get<uint8_t>(),rigidIndex=r.get<uint8_t>();for(auto width:{r.vertexIndex,r.textureIndex,materialIndex,r.boneIndex,morphIndex,rigidIndex})if(width!=1&&width!=2&&width!=4)throw std::runtime_error("Invalid PMX index width");if(r.encoding>1||r.extraUV>4)throw std::runtime_error("Invalid PMX encoding or UV count");r.skip(headerSize-8);
    auto localName=r.text(),englishName=r.text();r.text();r.text();Asset a;a.manifest={{"version",1},{"name",localName.empty()?englishName:localName},{"format","pmx"},{"materials",Json::array()},{"parts",Json::array()},{"warnings",Json::array()},{"unit_scale",PmxSourceUnits}};if(a.manifest["name"].get<std::string>().empty())a.manifest["name"]=utf8(path.stem().wstring());
    auto count=r.count(uint32_t(std::min(o.limits.packageBytes,o.limits.expandedBytes)/sizeof(Vertex)));
    r.need(uint64_t(count)*(37+uint64_t(r.extraUV)*16+r.boneIndex));
    checkGeometryStorage(count,0,o.limits);a.vertices.reserve(count);
    for(int v=0;v<count;v++){if(v%8192==0)progress("Reading PMX geometry",.02f+.06f*float(v)/std::max(1,count),"Vertices",v,count);Vertex x;x.pos=r.vec()*PmxSourceUnits;x.normal=r.vec();x.u=r.get<float>();x.v=r.get<float>();r.skip(size_t(r.extraUV)*16);auto skin=r.get<uint8_t>();switch(skin){case 0:r.skip(r.boneIndex);break;case 1:r.skip(2*r.boneIndex+4);break;case 2:r.skip(4*r.boneIndex+16);break;case 3:r.skip(2*r.boneIndex+4+36);break;case 4:if(version<2.05f)throw std::runtime_error("QDEF requires PMX 2.1");r.skip(4*r.boneIndex+16);break;default:throw std::runtime_error("Unknown PMX skinning record");}r.skip(4);a.vertices.push_back(x);}
    progress("Reading PMX indices",.08f);
    auto indices=r.count(uint32_t(std::min(o.limits.packageBytes,o.limits.expandedBytes)/4));
    r.need(uint64_t(indices)*r.vertexIndex);checkGeometryStorage(a.vertices.size(),indices,o.limits);a.indices.reserve(indices);if(indices%3)throw std::runtime_error("PMX face count is not triangular");for(int i=0;i<indices;i++){auto v=r.index(r.vertexIndex,false);if(v<0||size_t(v)>=a.vertices.size())throw std::runtime_error("Invalid PMX vertex index");a.indices.push_back(uint32_t(v));}
    auto nt=r.count(4096);std::vector<std::string> textures;for(int i=0;i<nt;i++)textures.push_back(r.text());
    auto nm=r.count(o.limits.materials);std::map<std::string,size_t> convertedTextures;TextureResolver resources(path.parent_path());unsigned repairedTextures=0;uint32_t cursor=0;for(int i=0;i<nm;i++){auto name=r.text();r.text();std::array<float,4> diffuse;for(auto& c:diffuse)c=std::clamp(r.get<float>(),0.f,1.f);std::array<float,3> specular,ambient;for(auto& c:specular)c=std::clamp(r.get<float>(),0.f,1.f);float shininess=std::clamp(r.get<float>(),0.f,1000.f);for(auto& c:ambient)c=std::clamp(r.get<float>(),0.f,1.f);uint8_t flags=r.get<uint8_t>();r.skip(16+4);int32_t base=r.index(r.textureIndex),sphere=r.index(r.textureIndex);auto sphereMode=r.get<uint8_t>();auto sharedToon=r.get<uint8_t>();if(sharedToon>1)throw std::runtime_error("Invalid PMX toon flag");int toon=sharedToon?int(r.get<uint8_t>()):r.index(r.textureIndex);if(sharedToon&&toon>9)throw std::runtime_error("Invalid PMX shared toon index");if(!sharedToon&&toon>=int(textures.size()))throw std::runtime_error("Invalid PMX toon texture index");if(sphere>=int(textures.size())||sphereMode>3)throw std::runtime_error("Invalid PMX sphere texture reference");r.text();auto n=r.count(uint32_t(a.indices.size()));if(n%3||uint64_t(cursor)+n>a.indices.size())throw std::runtime_error("Invalid PMX material face range");
        progress("Converting textures",.10f+.30f*float(i)/std::max(1,nm),"Material "+std::to_string(i+1)+" of "+std::to_string(nm)+": "+name,i,nm);
        Json m={{"source_order",i},{"alpha_explicit",false},{"specular",specular},{"shininess",shininess},{"ambient",ambient},{"sphere_mode",sphereMode},{"has_toon",sharedToon||toon>=0},{"source_shading","pmx"},{"name",name},{"color",diffuse},{"two_sided",bool(flags&1)},{"alpha_mode",diffuse[3]<.999f?"blend":"opaque"},{"alpha_cutoff",.5},{"base_texture",""},{"normal_texture",""}};
        if(base>=0){if(size_t(base)>=textures.size())throw std::runtime_error("Invalid PMX texture index");try{auto resolved=resources.resolve(textures[base]);repairedTextures+=resolved.repaired;auto key=utf8(resolved.path.wstring());auto found=convertedTextures.find(key);size_t index=0;
            if(found!=convertedTextures.end())index=found->second;else{auto t=makeTexture(readFile(resolved.path,128ull<<20),o.limits.textureDimension);auto hash=t.hash;addTexture(a,std::move(t));for(;index<a.textures.size();++index)if(a.textures[index].hash==hash)break;convertedTextures[key]=index;}
            const auto& t=a.textures[index];m["base_texture"]=t.hash;}catch(const std::exception& e){m["missing_texture"]=true;auto message=std::string("Texture for ")+name+": "+e.what();a.manifest["warnings"].push_back(message);m["texture_errors"]["base_texture"]=message;}}
        if((sphere>=0&&sphereMode)||sharedToon||toon>=0)a.manifest["warnings"].push_back("Material "+name+": PMX sphere/toon shading is approximated with Source diffuse/specular lighting; view-dependent sphere maps and toon ramps are not reproduced.");a.manifest["materials"].push_back(m);if(n)a.manifest["parts"].push_back({{"material",i},{"first",cursor},{"count",n}});cursor+=n;
    }
    a.manifest["resolved_texture_references"]=repairedTextures;
    if(cursor!=a.indices.size())throw std::runtime_error("PMX materials do not cover every triangle");
    // Tangents are derived from UVs, never from bones/morphs. Remaining sections are intentionally ignored.
    progress("Generating tangents",.42f);
    std::vector<Vec> ts(a.vertices.size()),bs(a.vertices.size());for(size_t i=0;i<a.indices.size();i+=3){auto i0=a.indices[i],i1=a.indices[i+1],i2=a.indices[i+2];auto& p=a.vertices[i0];auto& q=a.vertices[i1];auto& s=a.vertices[i2];Vec e=q.pos-p.pos,f=s.pos-p.pos;float u=q.u-p.u,v=q.v-p.v,u2=s.u-p.u,v2=s.v-p.v,d=u*v2-u2*v;if(std::abs(d)<1e-10f)continue;Vec t=(e*v2-f*v)*(1/d),b=(f*u-e*u2)*(1/d);for(auto k:{i0,i1,i2}){ts[k]=ts[k]+t;bs[k]=bs[k]+b;}}
    for(size_t i=0;i<a.vertices.size();i++){auto& v=a.vertices[i];v.normal=v.normal.normalized();auto t=(ts[i]-v.normal*ts[i].dot(v.normal)).normalized();v.tangent={t.x,t.y,t.z,v.normal.cross(t).dot(bs[i])<0?-1.f:1.f};}return a;
}
}
