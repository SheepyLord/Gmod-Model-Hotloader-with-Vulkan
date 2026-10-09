#include "core.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <map>
#include <optional>
#include <cstring>

namespace props {
std::wstring wide(std::string_view s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    if(!n) throw std::runtime_error("Invalid UTF-8 path");
    std::wstring out(n,0); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n);return out;
}
std::string utf8(std::wstring_view s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    if(!n) throw std::runtime_error("Invalid Unicode string");
    std::string out(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;
}
bool networkPath(std::string_view s) {
    // Opening \\host, //host, /\host, \\?\UNC\..., \??\..., \\.\... or a URL makes
    // Windows connect to another computer with the user's credentials. Only a drive
    // letter may start a rooted path; text that is not UTF-8 is refused as well.
    if(s.find("://")!=s.npos) return true;
    auto separator=[](char c){return c=='\\'||c=='/';};
    if(s.size()>=2&&separator(s[0])&&separator(s[1])) return true;
    if(s.size()>=4&&separator(s[0])&&s[1]=='?'&&s[2]=='?'&&separator(s[3])) return true;
    std::wstring text;try{text=wide(s);}catch(...){return true;}
    for(auto& c:text)if(c==L'/')c=L'\\';
    auto root=fs::path(text).root_name().wstring();
    return !(root.empty()||(root.size()==2&&root[1]==L':'));
}
Bytes readFile(const fs::path& p,uint64_t maximum) {
    // Status and cached assets are replaced atomically while readers may be open.
    // std::ifstream does not grant FILE_SHARE_DELETE on Windows and can make a
    // progress heartbeat fail with a sharing violation during concurrent polling.
    HANDLE f=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open file: "+utf8(p.filename().wstring()));
    try {
        LARGE_INTEGER size{};if(!GetFileSizeEx(f,&size)||size.QuadPart<0||uint64_t(size.QuadPart)>maximum)throw std::runtime_error("File exceeds the configured byte limit");
        Bytes b(size_t(size.QuadPart));size_t cursor=0;
        while(cursor<b.size()){DWORD received=0;auto count=DWORD(std::min<size_t>(b.size()-cursor,1u<<28));
            if(!ReadFile(f,b.data()+cursor,count,&received,nullptr)||!received)throw std::runtime_error("File was truncated while reading");cursor+=received;}
        CloseHandle(f);return b;
    }catch(...){CloseHandle(f);throw;}
}
void writeAtomic(const fs::path& p,std::span<const uint8_t> b) {
    fs::create_directories(p.parent_path());static std::atomic_uint64_t seq{0};
    auto tmp=p;tmp+=L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(++seq);
    try {
        {std::ofstream f(tmp,std::ios::binary|std::ios::trunc);if(!f||!f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()))) throw std::runtime_error("Cannot write cache; check disk space and permissions");f.flush();if(!f)throw std::runtime_error("Cache flush failed");}
        bool replaced=false;DWORD error=0;
        for(unsigned attempt=0;attempt<50;++attempt){
            if(MoveFileExW(tmp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){replaced=true;break;}
            error=GetLastError();if(error!=ERROR_SHARING_VIOLATION&&error!=ERROR_ACCESS_DENIED&&error!=ERROR_LOCK_VIOLATION)break;
            // Windows can hold the replaced destination in delete-pending state
            // until its last reader closes. Keep the old snapshot intact and retry.
            Sleep(2);
        }
        if(!replaced)throw std::runtime_error("Cannot finalize cache file (Windows error "+std::to_string(error)+")");
    } catch(...) {std::error_code ec;fs::remove(tmp,ec);throw;}
}
void writeJson(const fs::path& p,const Json& j){auto s=j.dump();writeAtomic(p,{reinterpret_cast<const uint8_t*>(s.data()),s.size()});}
Json readJson(const fs::path& p,uint64_t max){auto b=readFile(p,max);return Json::parse(b);}
std::string sha256(std::span<const uint8_t> b) {
    BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE h{};std::array<unsigned char,32> digest{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA-256 unavailable");
    auto cleanup=[&]{if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(alg,0);};
    if(BCryptCreateHash(alg,&h,nullptr,0,nullptr,0,0)<0){cleanup();throw std::runtime_error("SHA-256 initialization failed");}
    size_t at=0;while(at<b.size()){auto n=static_cast<ULONG>(std::min<size_t>(b.size()-at,1u<<28));if(BCryptHashData(h,const_cast<PUCHAR>(b.data()+at),n,0)<0){cleanup();throw std::runtime_error("SHA-256 failed");}at+=n;}
    if(BCryptFinishHash(h,digest.data(),ULONG(digest.size()),0)<0){cleanup();throw std::runtime_error("SHA-256 finalization failed");}cleanup();
    constexpr char hex[]="0123456789abcdef";std::string s;for(auto x:digest){s+=hex[x>>4];s+=hex[x&15];}return s;
}
bool validHash(std::string_view s){return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});}
Json bundleManifest(const fs::path& path){
    std::ifstream f(path,std::ios::binary);unsigned char header[24]{};
    if(!f.read(reinterpret_cast<char*>(header),24)||std::memcmp(header,"GMLHOT1\0",8))return Json();
    uint32_t length=uint32_t(header[8])|uint32_t(header[9])<<8|uint32_t(header[10])<<16|uint32_t(header[11])<<24;if(length>4u<<20)return Json();
    std::string text(length,'\0');if(!f.read(text.data(),length))return Json();
    auto manifest=Json::parse(text,nullptr,false);return manifest.is_object()?manifest:Json();
}
// A bundle's texture references; nothing when its manifest is unreadable or
// malformed, so the caller can keep textures it cannot rule out.
static std::optional<std::set<std::string>> textureRefs(const fs::path& path){
    auto manifest=bundleManifest(path);if(manifest.is_null())return std::nullopt;
    std::set<std::string> refs;auto materials=manifest.find("materials");if(materials==manifest.end())return refs;
    if(!materials->is_array())return std::nullopt;
    for(auto& m:*materials){
        if(!m.is_object())return std::nullopt;
        for(auto key:{"base_texture","normal_texture"}){auto it=m.find(key);if(it==m.end()||it->is_null())continue;if(!it->is_string())return std::nullopt;
            auto hash=it->get<std::string>();if(validHash(hash))refs.insert(hash);}
    }
    return refs;
}
Json deleteBundles(const fs::path& root,const std::vector<std::string>& ids){
    // Only content-addressed files inside root are candidates. Textures are removed
    // only when no remaining bundle refers to them; one damaged bundle keeps all
    // of them (its references are unknown) but blocks no deletion.
    std::set<std::string> selected(ids.begin(),ids.end()),used;uint64_t bytes=0;size_t files=0,pending=0;bool unknown=false;
    auto remove=[&](const fs::path& path){std::error_code ec;auto size=fs::is_regular_file(path,ec)?fs::file_size(path,ec):0;ec.clear();if(fs::remove(path,ec)){bytes+=size;files++;}else if(ec)pending++;};
    std::error_code ec;
    for(auto& entry:fs::directory_iterator(root/L"assets",ec)){
        auto name=utf8(entry.path().filename().wstring());if(name.size()!=69||!name.ends_with(".gmdl"))continue;auto asset=name.substr(0,64);
        if(!validHash(asset)||selected.contains(asset))continue;
        if(auto refs=textureRefs(entry.path()))used.merge(*refs);else unknown=true;
    }
    for(auto& asset:selected){
        auto path=root/L"assets"/wide(asset+".gmdl");
        if(!unknown)if(auto refs=textureRefs(path))for(auto& hash:*refs)if(!used.contains(hash))remove(root/L"textures"/wide(hash+".png"));
        remove(path);auto packed=path;packed+=L".share2";remove(packed);
        remove(root/L"library"/wide(asset+".json"));
    }
    auto registryPath=root/L"sources.local.json";
    if(fs::exists(registryPath))try{auto registry=readJson(registryPath,8ull<<20);for(auto& asset:selected)registry.erase(asset);writeJson(registryPath,registry);}catch(...){}
    return Json{{"removedFiles",files},{"removedBytes",bytes},{"pendingFiles",pending}};
}
static bool finite(Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::abs(v.x)<=1e7f&&std::abs(v.y)<=1e7f&&std::abs(v.z)<=1e7f;}
Json hullJson(const std::vector<Hull>& hs){Json j=Json::array();for(auto& h:hs)j.push_back({{"points",h.points},{"indices",h.indices}});return j;}
std::vector<Hull> parseHulls(const Json& j){
    if(!j.is_array()||j.empty()||j.size()>16)throw std::runtime_error("Invalid collision hull count");
    std::vector<Hull> out;
    for(auto& e:j){Hull h;auto& ps=e.at("points");auto& is=e.at("indices");if(!ps.is_array()||ps.size()<4||ps.size()>64||!is.is_array()||is.size()<12||is.size()>384||is.size()%3)throw std::runtime_error("Invalid collision hull size");
        h.points=ps.get<std::vector<Vec>>();h.indices=is.get<std::vector<uint32_t>>();for(auto p:h.points)if(!finite(p))throw std::runtime_error("Invalid hull coordinate");for(auto i:h.indices)if(i>=h.points.size())throw std::runtime_error("Invalid hull index");
        Vec center{};for(auto p:h.points)center=center+p;center=center*(1.f/h.points.size());
        float radius=0;for(auto p:h.points)radius=std::max(radius,(p-center).length());
        if(radius>32768)throw std::runtime_error("Collider is too large for Source physics; reduce import scale");
        std::map<std::pair<uint32_t,uint32_t>,unsigned> edges;std::set<uint32_t> used;double volume=0;
        for(size_t i=0;i<h.indices.size();i+=3){
            auto ia=h.indices[i],ib=h.indices[i+1],ic=h.indices[i+2];
            Vec p=h.points[ia],q=h.points[ib],r=h.points[ic],normal=(q-p).cross(r-p);
            if(normal.length()<1e-10f)throw std::runtime_error("Degenerate collision face");
            if(normal.dot(center-p)>0)normal=normal*-1;normal=normal.normalized();
            for(auto v:h.points)if(normal.dot(v-p)>std::max(1e-4f,radius*.0002f))throw std::runtime_error("Collision shape is not convex");
            for(auto edge:{std::pair{ia,ib},std::pair{ib,ic},std::pair{ic,ia}}){if(edge.first>edge.second)std::swap(edge.first,edge.second);++edges[edge];}
            used.insert(ia);used.insert(ib);used.insert(ic);
            volume+=std::abs((p-center).dot((q-center).cross(r-center)));
        }
        for(auto [edge,count]:edges)if(count!=2)throw std::runtime_error("Collision hull is not closed");
        if(used.size()!=h.points.size()||volume<1e-9)throw std::runtime_error("Collision hull has no volume or unused points");
        out.push_back(std::move(h));}
    return out;
}
void updateBounds(Asset& a){Vec mn{INFINITY,INFINITY,INFINITY},mx{-INFINITY,-INFINITY,-INFINITY};for(auto& v:a.vertices){mn={std::min(mn.x,v.pos.x),std::min(mn.y,v.pos.y),std::min(mn.z,v.pos.z)};mx={std::max(mx.x,v.pos.x),std::max(mx.y,v.pos.y),std::max(mx.z,v.pos.z)};}a.manifest["mins"]=mn;a.manifest["maxs"]=mx;}
void checkGeometryStorage(uint64_t vertices,uint64_t indices,const Limits& limits){
    if(vertices>UINT32_MAX||indices>UINT32_MAX)throw std::runtime_error("Geometry exceeds the cache format's 32-bit indexing range");
    const uint64_t bytes=vertices*sizeof(Vertex)+indices*sizeof(uint32_t);
    if(bytes>limits.expandedBytes||bytes>limits.packageBytes)throw std::runtime_error("Geometry exceeds the asset byte/allocation budget; simplify or split the model");
}
void warnLargeGeometry(Json& metadata,uint64_t vertices,uint64_t indices,const Limits& limits){
    if(indices/3<=limits.triangleWarning&&vertices<=uint64_t(limits.triangleWarning)*3)return;
    auto warning="High-detail model: "+std::to_string(indices/3)+" triangles, "+std::to_string(vertices)+" vertices. Import is allowed; preview preparation, rendering, sharing and memory use may be high.";
    metadata["geometry_warning"]=warning;
    auto& warnings=metadata["warnings"];if(!warnings.is_array())warnings=Json::array();
    if(std::find(warnings.begin(),warnings.end(),Json(warning))==warnings.end())warnings.insert(warnings.begin(),warning);
}
void validate(const Asset& a,const Limits& l){
    if(a.manifest.value("version",0)!=1)throw std::runtime_error("Unsupported asset version");
    if(a.vertices.empty()||a.indices.empty()||a.indices.size()%3)throw std::runtime_error("Invalid geometry counts");
    checkGeometryStorage(a.vertices.size(),a.indices.size(),l);
    auto& mats=a.manifest.at("materials");auto& parts=a.manifest.at("parts");
    if(!mats.is_array()||mats.empty()||mats.size()>l.materials||!parts.is_array()||parts.empty()||parts.size()>65536)throw std::runtime_error("Material or mesh count limit exceeded");
    if(a.manifest.value("name",std::string{}).size()>512)throw std::runtime_error("Asset name is too long");
    uint64_t expanded=a.vertices.size()*sizeof(Vertex)+a.indices.size()*4;
    for(auto& v:a.vertices){if(!finite(v.pos)||!finite(v.normal)||!std::isfinite(v.u)||!std::isfinite(v.v))throw std::runtime_error("Non-finite vertex data");for(float t:v.tangent)if(!std::isfinite(t))throw std::runtime_error("Non-finite tangent");}
    for(auto i:a.indices)if(i>=a.vertices.size())throw std::runtime_error("Vertex index is out of bounds");
    uint64_t cursor=0;for(auto& p:parts){auto first=p.at("first").get<uint64_t>(),count=p.at("count").get<uint64_t>();if(first!=cursor||!count||count%3||count>60000||first+count>a.indices.size()||p.at("material").get<uint32_t>()>=mats.size())throw std::runtime_error("Invalid mesh range");cursor+=count;}if(cursor!=a.indices.size())throw std::runtime_error("Mesh ranges do not cover geometry");
    std::set<std::string> hashes;
    for(auto& t:a.textures){if(!validHash(t.hash)||!hashes.insert(t.hash).second||!t.width||!t.height||t.width>l.textureDimension||t.height>l.textureDimension||t.png.size()<33)throw std::runtime_error("Invalid texture metadata");
        static const uint8_t sig[]={137,80,78,71,13,10,26,10};if(std::memcmp(t.png.data(),sig,8)||std::memcmp(t.png.data()+12,"IHDR",4))throw std::runtime_error("Only normalized PNG textures are allowed");
        auto be=[&](size_t n){return uint32_t(t.png[n])<<24|uint32_t(t.png[n+1])<<16|uint32_t(t.png[n+2])<<8|uint32_t(t.png[n+3]);};
        if(be(16)!=t.width||be(20)!=t.height||sha256(t.png)!=t.hash)throw std::runtime_error("Texture checksum or dimensions mismatch");validatePNG(t);expanded+=uint64_t(t.width)*t.height*4+t.png.size();}
    if(expanded>l.expandedBytes)throw std::runtime_error("Expanded asset exceeds the memory limit");
    for(auto& m:mats){auto c=m.at("color").get<std::array<float,4>>();for(float n:c)if(!std::isfinite(n)||n<0||n>1)throw std::runtime_error("Invalid material color");for(auto key:{"base_texture","normal_texture"}){auto h=m.value(key,std::string{});if(!h.empty()&&!hashes.contains(h))throw std::runtime_error("Material references missing texture");}auto mode=m.value("alpha_mode",std::string("opaque"));if(mode!="opaque"&&mode!="mask"&&mode!="blend")throw std::runtime_error("Invalid alpha mode");float cutoff=m.value("alpha_cutoff",0.5f);if(!std::isfinite(cutoff)||cutoff<0||cutoff>1)throw std::runtime_error("Invalid alpha threshold");}
    for(const auto& m:mats){
        for(auto key:{"specular","ambient"})if(m.contains(key)){auto values=m.at(key).get<std::array<float,3>>();for(float x:values)if(!std::isfinite(x)||x<0||x>1)throw std::runtime_error("Invalid material lighting color");}
        if(m.contains("shininess")){float x=m.at("shininess");if(!std::isfinite(x)||x<0||x>1000)throw std::runtime_error("Invalid material shininess");}
        if(m.contains("source_order")&&m.at("source_order").get<uint64_t>()>=128)throw std::runtime_error("Invalid material source order");
        for(auto key:{"two_sided","alpha_explicit","has_toon","unlit","emissive_baked"})if(m.contains(key)&&!m.at(key).is_boolean())throw std::runtime_error("Invalid material flag");
        if(m.contains("sphere_mode")&&m.at("sphere_mode").get<uint64_t>()>3)throw std::runtime_error("Invalid sphere map mode");
        if(m.contains("toon_shared")){int x=m.at("toon_shared");if(x< -1||x>9)throw std::runtime_error("Invalid shared toon index");}
        if(m.contains("source_shading")){auto mode=m.at("source_shading").get<std::string>();if(mode!="pmx"&&mode!="phong"&&mode!="pbr")throw std::runtime_error("Invalid shading model");}
    }
    parseHulls(hullJson(a.hulls));
    Vec mn=a.manifest.at("mins").get<Vec>(),mx=a.manifest.at("maxs").get<Vec>();if(!finite(mn)||!finite(mx)||mn.x>mx.x||mn.y>mx.y||mn.z>mx.z)throw std::runtime_error("Invalid bounds");
    for(auto& v:a.vertices)if(v.pos.x<mn.x-.01f||v.pos.y<mn.y-.01f||v.pos.z<mn.z-.01f||v.pos.x>mx.x+.01f||v.pos.y>mx.y+.01f||v.pos.z>mx.z+.01f)throw std::runtime_error("Geometry exceeds declared bounds");
}
struct Writer{Bytes b;void u32(uint32_t v){for(int i=0;i<4;i++)b.push_back(uint8_t(v>>(8*i)));}void raw(std::span<const uint8_t> v){b.insert(b.end(),v.begin(),v.end());}};
struct Reader{std::span<const uint8_t> b;size_t p=0;std::span<const uint8_t> raw(size_t n){if(n>b.size()-p)throw std::runtime_error("Truncated asset package");auto s=b.subspan(p,n);p+=n;return s;}uint32_t u32(){auto s=raw(4);return uint32_t(s[0])|uint32_t(s[1])<<8|uint32_t(s[2])<<16|uint32_t(s[3])<<24;}};
Bytes encode(const Asset& a){
    Json j=a.manifest;j["hulls"]=hullJson(a.hulls);auto s=j.dump();Writer w;w.raw({reinterpret_cast<const uint8_t*>("GMLHOT1\0"),8});w.u32(uint32_t(s.size()));w.u32(uint32_t(a.vertices.size()));w.u32(uint32_t(a.indices.size()));w.u32(uint32_t(a.textures.size()));w.raw({reinterpret_cast<const uint8_t*>(s.data()),s.size()});
    w.raw({reinterpret_cast<const uint8_t*>(a.vertices.data()),a.vertices.size()*sizeof(Vertex)});w.raw({reinterpret_cast<const uint8_t*>(a.indices.data()),a.indices.size()*4});
    for(auto& t:a.textures){w.raw({reinterpret_cast<const uint8_t*>(t.hash.data()),64});w.u32(t.width);w.u32(t.height);w.u32(uint32_t(t.png.size()));w.raw(t.png);}return std::move(w.b);
}
Asset decode(std::span<const uint8_t> bytes,const Limits& limits){
    if(bytes.size()>limits.packageBytes)throw std::runtime_error("Asset exceeds package limit");Reader r{bytes};auto magic=r.raw(8);if(std::memcmp(magic.data(),"GMLHOT1\0",8))throw std::runtime_error("Not a GModel asset");
    auto jn=r.u32(),nv=r.u32(),ni=r.u32(),nt=r.u32();if(jn>4u<<20||nt>uint64_t(limits.materials)*2)throw std::runtime_error("Asset header exceeds limits");
    checkGeometryStorage(nv,ni,limits);
    Asset a;auto jb=r.raw(jn);a.manifest=Json::parse(jb);a.hulls=parseHulls(a.manifest.at("hulls"));auto vb=r.raw(uint64_t(nv)*sizeof(Vertex)),ib=r.raw(uint64_t(ni)*4);a.vertices.resize(nv);a.indices.resize(ni);std::memcpy(a.vertices.data(),vb.data(),vb.size());std::memcpy(a.indices.data(),ib.data(),ib.size());
    for(uint32_t i=0;i<nt;i++){Texture t;auto h=r.raw(64);t.hash.assign(reinterpret_cast<const char*>(h.data()),64);t.width=r.u32();t.height=r.u32();auto data=r.raw(r.u32());t.png.assign(data.begin(),data.end());a.textures.push_back(std::move(t));}
    if(r.p!=bytes.size())throw std::runtime_error("Trailing data in asset package");validate(a,limits);a.id=sha256(bytes);return a;
}
std::string saveAsset(const fs::path& cache,Asset& a,const Progress& progress,const Limits& limits){
    progress("Validating asset",.90f);validate(a,limits);progress("Encoding asset",.94f);auto b=encode(a);
    if(b.size()>limits.packageBytes)throw std::runtime_error("Asset package exceeds the configured byte limit");
    progress("Hashing asset",.96f,{},0,b.size(),true);a.id=sha256(b);
    progress("Writing cache",.98f,{},0,b.size(),true);writeAtomic(cache/L"assets"/wide(a.id+".gmdl"),b);return a.id;
}
Asset loadAsset(const fs::path& cache,const std::string& id,const Limits& limits){if(!validHash(id))throw std::runtime_error("Invalid asset identifier");auto bytes=readFile(cache/L"assets"/wide(id+".gmdl"),limits.packageBytes);auto a=decode(bytes,limits);if(a.id!=id)throw std::runtime_error("Asset SHA-256 mismatch");for(auto& t:a.textures)writeAtomic(cache/L"textures"/wide(t.hash+".png"),t.png);return a;}
Options parseOptions(const Json& j){Options o;if(j.contains("rotation"))o.rotation=j.at("rotation").get<std::array<float,3>>();for(float n:o.rotation)if(!std::isfinite(n)||std::abs(n)>360)throw std::runtime_error("Rotation must be between -360 and 360 degrees");o.scale=j.value("scale",1.f);o.axis=j.value("axis",std::string("auto"));o.collision=j.value("collision",std::string("hull"));o.autoUnits=j.value("auto_units",true);if(j.contains("objects")){const auto& list=j.at("objects");if(!list.is_array()||list.size()>4096)throw std::runtime_error("Invalid object selection");for(auto& n:list){if(!n.is_string()||n.get<std::string>().size()>1024)throw std::runtime_error("Invalid object name");o.objects.push_back(n.get<std::string>());}}if(!std::isfinite(o.scale)||o.scale<=0||o.scale>10000)throw std::runtime_error("Scale must be positive and at most 10000");if(o.axis!="auto"&&o.axis!="y_up"&&o.axis!="z_up")throw std::runtime_error("Unknown axis setting");if(o.collision!="balanced"&&o.collision!="hull")throw std::runtime_error("Unknown collision mode");return o;}
}
