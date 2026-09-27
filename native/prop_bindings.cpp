#include "prop_bindings.hpp"
#include "props/core.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <regex>
#include <set>

namespace mmd {
namespace {
using Lua=GarrysMod::Lua::ILuaBase;
using props::Asset;using props::Hull;using props::Json;using props::Vec;
namespace fs=std::filesystem;
// Cached bundles load off the engine thread. A released entry is dropped once
// its load completes, so a quick reselect reuses the pending work.
struct Loaded{std::shared_ptr<Asset> asset;std::future<std::shared_ptr<Asset>> future;std::string error;bool released=false;};
struct State{fs::path cache,root;std::map<std::string,Loaded> assets;std::map<std::string,std::vector<Hull>> colliders;};
std::unique_ptr<State> state;
int failure(Lua* l,const std::exception& e){l->PushNil();l->PushString(e.what());return 2;}
void pushJson(Lua* l,const Json& j){auto s=j.dump();l->PushString(s.data(),unsigned(s.size()));}
void pushVec(Lua* l,Vec v){Vector x;x.x=v.x;x.y=v.y;x.z=v.z;l->PushVector(x);}
Vec vec(Lua* l,int i){if(!l->IsType(i,GarrysMod::Lua::Type::Vector))throw std::runtime_error("Expected Vector");auto& v=l->GetVector(i);if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw std::runtime_error("Non-finite Vector");return {v.x,v.y,v.z};}
std::string text(Lua* l,int i){if(!l->IsType(i,GarrysMod::Lua::Type::String))throw std::runtime_error("Expected string");unsigned n=0;auto s=l->GetString(i,&n);return {s,n};}
uint64_t integer(Lua* l,int i,uint64_t maximum){double n=l->GetNumber(i);if(!std::isfinite(n)||n<0||n>double(maximum)||std::floor(n)!=n)throw std::runtime_error("Invalid integer argument");return uint64_t(n);}
std::string id(Lua* l,int i){auto s=text(l,i);if(!props::validHash(s))throw std::runtime_error("Invalid prop ID");return s;}
fs::path bundle(const std::string& asset){return state->root/L"assets"/props::wide(asset+".gmdl");}
std::shared_ptr<Asset> prepare(const fs::path& root,const std::string& asset){
    // Textures are content-addressed; skip rewriting a PNG the engine may be reading.
    auto bytes=props::readFile(root/L"assets"/props::wide(asset+".gmdl"));auto a=std::make_shared<Asset>(props::decode(bytes));
    if(a->id!=asset)throw std::runtime_error("Prop SHA-256 mismatch");
    for(auto& t:a->textures){auto path=root/L"textures"/props::wide(t.hash+".png");std::error_code ec;if(!fs::is_regular_file(path,ec)||fs::file_size(path,ec)!=t.png.size())props::writeAtomic(path,t.png);}
    props::prepareRenderPlan(*a);return a;
}
std::shared_ptr<Asset> get(const std::string& asset){
    auto it=state->assets.find(asset);if(it==state->assets.end())return {};
    auto& slot=it->second;
    if(slot.future.valid()&&slot.future.wait_for(std::chrono::seconds(0))==std::future_status::ready){try{slot.asset=slot.future.get();}catch(const std::exception& e){slot.error=e.what();}}
    if(!slot.error.empty())throw std::runtime_error(slot.error);
    return slot.asset;
}
void request(const std::string& asset){
    for(auto it=state->assets.begin();it!=state->assets.end();){auto& s=it->second;if(s.released&&(!s.future.valid()||s.future.wait_for(std::chrono::seconds(0))==std::future_status::ready))it=state->assets.erase(it);else ++it;}
    auto it=state->assets.find(asset);
    if(it!=state->assets.end()){it->second.released=false;if(it->second.error.empty())return;state->assets.erase(it);}
    if(!fs::is_regular_file(bundle(asset)))throw std::runtime_error("This prop is not in the local cache; import or download it again");
    Loaded entry;auto root=state->root;entry.future=std::async(std::launch::async,[root,asset]{return prepare(root,asset);});
    state->assets.emplace(asset,std::move(entry));
}
// Synchronous path for duplicator/save restoration, which must create the prop now.
std::shared_ptr<Asset> require(const std::string& asset){
    request(asset);auto& slot=state->assets.at(asset);
    if(slot.future.valid())slot.future.wait();
    auto a=get(asset);if(!a)throw std::runtime_error("Prop failed to load");return a;
}
Json info(const std::string& asset,const Asset& a){
    Json j=a.renderManifest;j.erase("hulls");j["id"]=asset;j["triangles"]=a.indices.size()/3;j["vertices"]=a.vertices.size();j["collision_hulls"]=a.hulls.size();
    props::warnLargeGeometry(j,a.vertices.size(),a.indices.size());
    uint64_t expanded=a.vertices.size()*sizeof(props::Vertex)+(a.indices.size()+a.renderIndices.size())*4;uint32_t dimension=0;
    for(auto& t:a.textures){expanded+=uint64_t(t.width)*t.height*4+t.png.size();dimension=std::max({dimension,t.width,t.height});}
    std::error_code ec;j["expanded_bytes"]=expanded;j["texture_dimension"]=dimension;j["bytes"]=fs::file_size(bundle(asset),ec);return j;
}
LUA_FUNCTION(PropRequest){try{request(id(LUA,1));LUA->PushBool(true);return 1;}catch(const std::exception& e){return failure(LUA,e);}}
LUA_FUNCTION(PropHas){try{std::error_code ec;LUA->PushBool(fs::is_regular_file(bundle(id(LUA,1)),ec));return 1;}catch(const std::exception& e){return failure(LUA,e);}}
LUA_FUNCTION(PropInfo){try{
    auto asset=id(LUA,1);bool wait=LUA->IsType(2,GarrysMod::Lua::Type::Bool)&&LUA->GetBool(2);
    auto a=wait?require(asset):get(asset);if(!a){LUA->PushNil();return 1;}
    pushJson(LUA,info(asset,*a));return 1;
}catch(const std::exception& e){return failure(LUA,e);}}
LUA_FUNCTION(PropReadMeshBatch){try{
    auto a=get(id(LUA,1));if(!a)throw std::runtime_error("Prop is still loading");
    auto part=integer(LUA,2,65536),offset=integer(LUA,3,60000),count=integer(LUA,4,2048);
    if(part>=a->renderParts.size())throw std::runtime_error("Invalid mesh part");
    auto& p=a->renderParts[part];uint32_t first=p["first"],total=p["count"];
    bool backface=LUA->IsType(5,GarrysMod::Lua::Type::Bool)&&LUA->GetBool(5);
    if(offset>total)throw std::runtime_error("Invalid mesh offset");count=std::min<uint64_t>(count,total-offset);
    LUA->CreateTable();
    for(uint64_t i=0;i<count;i++){
        auto v=props::renderVertex(*a,first+offset+i,backface);
        LUA->PushNumber(double(i+1));LUA->CreateTable();
        pushVec(LUA,v.pos);LUA->SetField(-2,"pos");pushVec(LUA,v.normal);LUA->SetField(-2,"normal");
        LUA->PushNumber(v.u);LUA->SetField(-2,"u");LUA->PushNumber(v.v);LUA->SetField(-2,"v");
        LUA->CreateTable();for(int t=0;t<4;t++){LUA->PushNumber(t+1);LUA->PushNumber(v.tangent[t]);LUA->SetTable(-3);}
        LUA->SetField(-2,"userdata");LUA->SetTable(-3);
    }
    return 1;
}catch(const std::exception& e){return failure(LUA,e);}}
LUA_FUNCTION(PropHulls){try{
    auto asset=id(LUA,1);bool wait=LUA->IsType(3,GarrysMod::Lua::Type::Bool)&&LUA->GetBool(3);
    auto a=wait?require(asset):get(asset);if(!a)throw std::runtime_error("Prop is still loading");
    auto scale=props::propScale(LUA->IsType(2,GarrysMod::Lua::Type::Number)?LUA->GetNumber(2):1);props::validateHullScale(a->hulls,scale);
    LUA->CreateTable();
    for(size_t h=0;h<a->hulls.size();h++){LUA->PushNumber(double(h+1));LUA->CreateTable();for(size_t i=0;i<a->hulls[h].points.size();i++){LUA->PushNumber(double(i+1));pushVec(LUA,a->hulls[h].points[i]*scale);LUA->SetTable(-3);}LUA->SetTable(-3);}
    return 1;
}catch(const std::exception& e){return failure(LUA,e);}}
LUA_FUNCTION(PropCollisionBlob){try{
    auto a=get(id(LUA,1));if(!a)throw std::runtime_error("Prop is still loading");
    pushJson(LUA,{{"hulls",props::hullJson(a->hulls)},{"mins",a->manifest["mins"]},{"maxs",a->manifest["maxs"]}});return 1;
}catch(const std::exception& e){return failure(LUA,e);}}
LUA_FUNCTION(PropInstallCollision){try{
    auto asset=id(LUA,1),blob=text(LUA,2);if(blob.size()>(256u<<10))throw std::runtime_error("Collision metadata exceeds limits");
    state->colliders[asset]=props::parseHulls(Json::parse(blob).at("hulls"));LUA->PushBool(true);return 1;
}catch(const std::exception& e){return failure(LUA,e);}}
LUA_FUNCTION(PropTrace){try{
    auto asset=id(LUA,1);const std::vector<Hull>* hulls=nullptr;
    if(auto a=get(asset))hulls=&a->hulls;else if(auto it=state->colliders.find(asset);it!=state->colliders.end())hulls=&it->second;
    if(!hulls){LUA->PushNil();return 1;}
    auto scale=props::propScale(LUA->IsType(7,GarrysMod::Lua::Type::Number)?LUA->GetNumber(7):1);
    auto hit=props::traceScaled(*hulls,vec(LUA,2),vec(LUA,3),{vec(LUA,4),vec(LUA,5),vec(LUA,6)},scale);
    if(!hit.hit){LUA->PushNil();return 1;}LUA->PushNumber(hit.fraction);pushVec(LUA,hit.normal);return 2;
}catch(const std::exception& e){return failure(LUA,e);}}
LUA_FUNCTION(PropRelease){try{
    auto asset=id(LUA,1);auto it=state->assets.find(asset);
    if(it!=state->assets.end()){if(!it->second.future.valid()||it->second.future.wait_for(std::chrono::seconds(0))==std::future_status::ready)state->assets.erase(it);else it->second.released=true;}
    state->colliders.erase(asset);LUA->PushBool(true);return 1;
}catch(const std::exception& e){return failure(LUA,e);}}
LUA_FUNCTION(PropSharedManifest){try{
    auto asset=id(LUA,1);auto a=get(asset);auto bytes=props::readFile(bundle(asset));
    if(props::sha256(bytes)!=asset)throw std::runtime_error("Cached prop failed SHA-256 verification; import it again");
    std::string name=a?a->manifest.value("name",std::string("Imported prop")):std::string("Imported prop");
    pushJson(LUA,{{"version",1},{"kind","static"},{"asset",asset},{"name",name},{"size",bytes.size()},{"files",Json::array({{{"path","static/assets/"+asset+".gmdl"},{"size",bytes.size()},{"sha256",asset}}})}});return 1;
}catch(const std::exception& e){return failure(LUA,e);}}
#ifndef MMDHL_SERVER
LUA_FUNCTION(PropDelete){try{
    auto ids=Json::parse(text(LUA,1)).get<std::vector<std::string>>();
    for(auto& asset:ids){if(!props::validHash(asset))throw std::runtime_error("Invalid prop ID");auto it=state->assets.find(asset);if(it!=state->assets.end()&&it->second.future.valid()&&it->second.future.wait_for(std::chrono::seconds(0))!=std::future_status::ready)throw std::runtime_error("A prop is still loading; retry deletion in a moment");}
    for(auto& asset:ids){state->assets.erase(asset);state->colliders.erase(asset);}
    auto result=deleteProps(ids);LUA->PushString(result.c_str());return 1;
}catch(const std::exception& e){return failure(LUA,e);}}
#endif
}
bool isPropBundle(const std::string& relative){static const std::regex pattern("static/assets/[a-f0-9]{64}\\.gmdl");return std::regex_match(relative,pattern);}
void validatePropBundle(const std::string& relative,std::span<const unsigned char> bytes){
    if(!isPropBundle(relative))return;
    auto asset=relative.substr(14,64);auto a=props::decode(bytes);
    if(a.id!=asset)throw std::runtime_error("Shared prop does not match its content hash");
}
namespace {
// Library listing: one summary per cached bundle, memoized by size and time.
struct Summary{uintmax_t size=0;fs::file_time_type time;Json value;};
std::map<std::string,Summary> summaries;
LUA_FUNCTION(PropCatalog){try{
    Json out=Json::object();std::error_code ec;std::set<std::string> seen;
    for(auto& entry:fs::directory_iterator(state->root/L"assets",ec)){
        auto name=props::utf8(entry.path().filename().wstring());if(name.size()!=69||!name.ends_with(".gmdl"))continue;auto asset=name.substr(0,64);if(!props::validHash(asset))continue;
        std::error_code e;auto size=entry.file_size(e);auto time=entry.last_write_time(e);if(e)continue;seen.insert(asset);
        auto& cached=summaries[asset];
        // A damaged bundle (valid JSON, wrong types) is left out of the listing
        // instead of failing it for every other prop.
        if(cached.value.is_null()||cached.size!=size||cached.time!=time)try{
            auto m=props::bundleManifest(entry.path());if(m.is_null()){summaries.erase(asset);continue;}
            cached={size,time,{{"name",m.value("name",std::string("Imported prop"))},{"format",m.value("format",std::string())},{"triangles",m.value("triangles",uint64_t(0))},{"vertices",m.value("vertices",uint64_t(0))},
                {"materials",m.value("materials",Json::array()).size()},{"collision_hulls",m.value("hulls",Json::array()).size()},{"collision_method",m.value("collision_method",std::string("coacd"))},
                {"warnings",m.value("warnings",Json::array())},{"mins",m.value("mins",Json::array({0,0,0}))},{"maxs",m.value("maxs",Json::array({0,0,0}))},{"options",m.value("options",Json::object())},{"bytes",size}}};
        }catch(const Json::exception&){summaries.erase(asset);continue;}
        out[asset]=cached.value;
    }
    for(auto it=summaries.begin();it!=summaries.end();)if(seen.contains(it->first))++it;else it=summaries.erase(it);
    pushJson(LUA,out);return 1;
}catch(const std::exception& e){return failure(LUA,e);}}
}
std::string deleteProps(const std::vector<std::string>& ids){return props::deleteBundles(state->root,ids).dump();}
void registerPropFunctions(Lua* LUA,const fs::path& cache){
    state=std::make_unique<State>();state->cache=cache;state->root=cache/L"static";
    fs::create_directories(state->root/L"assets");fs::create_directories(state->root/L"textures");
#define PROP_REGISTER(name) LUA->PushCFunction(name);LUA->SetField(-2,#name)
    PROP_REGISTER(PropRequest);PROP_REGISTER(PropHas);PROP_REGISTER(PropInfo);PROP_REGISTER(PropReadMeshBatch);PROP_REGISTER(PropHulls);
    PROP_REGISTER(PropCatalog);PROP_REGISTER(PropCollisionBlob);PROP_REGISTER(PropInstallCollision);PROP_REGISTER(PropTrace);PROP_REGISTER(PropRelease);PROP_REGISTER(PropSharedManifest);
#ifndef MMDHL_SERVER
    PROP_REGISTER(PropDelete);
#endif
#undef PROP_REGISTER
}
void shutdownProps(){
    if(!state)return;
    for(auto& [_,slot]:state->assets)if(slot.future.valid())slot.future.wait();
    state.reset();
}
}
