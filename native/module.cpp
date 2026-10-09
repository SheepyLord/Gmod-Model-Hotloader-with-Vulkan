#include "runtime.hpp"
#include "installation.hpp"
#include "compatibility.hpp"
#include "renderer.hpp"
#include "rig_animation.hpp"
#include "sharing.hpp"
#include "packages.hpp"
#include "model_notes.hpp"
#include "humanoid_map.hpp"
#include "scene_share.hpp"
#include <fstream>
#include "bridge.hpp"
#include "secondary.hpp"
#include "compute_solver.hpp"
#include "vulkan_solver.hpp"
#include "ordered_dispatcher.hpp"
#ifndef MMDHL_SERVER
#include "thread_sampler.hpp"
#endif
#include <GarrysMod/Lua/Interface.h>
#include <windows.h>
#include <shellapi.h>
#include <future>
#include <map>
#include <stdexcept>
#include "jobs.hpp"
#include "prop_bindings.hpp"
#include "physics_profile.hpp"
namespace {
using namespace mmd;using Lua=GarrysMod::Lua::ILuaBase;
struct Job{HANDLE process=nullptr,group=nullptr;fs::path dir;uint64_t started=0;Json result;};
struct SharedTransfer{fs::path staging;std::string relative,digest;uint64_t size=0,rawSize=0,written=0;bool packed=false;std::atomic_bool canceled=false;std::ofstream stream;~SharedTransfer(){stream.close();std::error_code error;fs::remove(staging,error);}};
struct SharedExport{fs::path path;std::future<fs::path> preparing;bool discarded=false;};
struct PackageJob{std::shared_ptr<PackageProgress> progress;std::future<Json> future;Json result;bool finished=false;};
struct Context{std::map<uint64_t,PackageJob> packages;std::map<uint64_t,std::future<Json>> addonScans;fs::path root;SceneShare sceneShare;std::map<uint64_t,SharedExport> exports;std::map<uint64_t,std::future<bool>> commits;std::map<uint64_t,std::shared_ptr<SharedTransfer>> transfers;std::unique_ptr<World> runtime=std::make_unique<World>();fs::path bin,cache;uint64_t sequence=1;std::map<uint64_t,Job> jobs;std::map<std::string,std::shared_ptr<Model>> assets;std::map<std::string,std::future<std::shared_ptr<Model>>> loading;std::map<std::string,std::future<Rig>> fitting;std::map<std::string,Rig> fitted;PreviewQueue previews;std::unique_ptr<World> preview;std::map<uint64_t,std::unique_ptr<World>> editors;};
std::unique_ptr<Context> context;
std::future<Json> installationProbe;
void pruneSharedWork(){
 for(auto it=context->exports.begin();it!=context->exports.end();)if(it->second.discarded&&(!it->second.preparing.valid()||it->second.preparing.wait_for(std::chrono::seconds(0))==std::future_status::ready))it=context->exports.erase(it);else ++it;
 for(auto it=context->commits.begin();it!=context->commits.end();)if(!context->transfers.contains(it->first)&&(!it->second.valid()||it->second.wait_for(std::chrono::seconds(0))==std::future_status::ready))it=context->commits.erase(it);else ++it;
}
std::string stringArg(Lua* l,int i){if(!l->IsType(i,GarrysMod::Lua::Type::String))throw std::runtime_error("Expected string");unsigned n=0;auto s=l->GetString(i,&n);return {s,n};}
uint64_t number(Lua* l,int i){double v=l->GetNumber(i);if(!std::isfinite(v)||v<0||v>9007199254740991.||std::floor(v)!=v)throw std::runtime_error("Invalid handle/index");return uint64_t(v);}
Json json(Lua* l,int i){return l->IsType(i,GarrysMod::Lua::Type::String)?Json::parse(stringArg(l,i)):Json::object();}
void push(Lua* l,const Json& j){auto s=j.dump();l->PushString(s.data(),unsigned(s.size()));}
int failure(Lua* l,const std::exception& e){l->PushNil();l->PushString(e.what());return 2;}
btVector3 vector(Lua* l,int i){auto& v=l->GetVector(i);if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw std::runtime_error("Invalid vector");return {v.x,v.y,v.z};}
#define FUNCTION(name) LUA_FUNCTION(name) { WorldScope realm(context?context->runtime.get():nullptr); try
#define END_FUNCTION catch(const std::exception& e){return failure(LUA,e);} }
void requireServer(){
#ifndef MMDHL_SERVER
throw std::runtime_error("Server realm owns simulation");
#endif
}
// A job's folder (request and status) is only needed until its result reaches
// Lua; the newest results stay in memory for late polls. Never follow a junction.
bool plainDirectory(const fs::path& path){auto attributes=GetFileAttributesW(path.c_str());return attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT);}
void retireJob(Job& j){std::error_code error;if(plainDirectory(j.dir))fs::remove_all(j.dir,error);}
#ifndef MMDHL_SERVER
void pruneJobs(){std::vector<uint64_t> done;for(auto& [id,j]:context->jobs)if(!j.process&&!j.result.is_null())done.push_back(id);for(size_t i=0;i+16<done.size();i++)context->jobs.erase(done[i]);}
#endif
uint64_t launch(bool picker,const std::string& source,const Json& options){
#ifdef MMDHL_SERVER
    throw std::runtime_error("Imports must be started locally in the client realm");
#else
    for(auto& [id,j]:context->jobs)if(j.process&&WaitForSingleObject(j.process,0)==WAIT_TIMEOUT)throw std::runtime_error("An import is already running");
    pruneJobs();
    Job j;uint64_t id=context->sequence++;j.started=GetTickCount64();j.dir=context->cache/L"jobs"/(std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(j.started)+L"_"+std::to_wstring(id));fs::create_directories(j.dir);
    writeJson(j.dir/L"status.json",{{"state","running"},{"stage",picker?"Select model":"Starting import"},{"progress",0}});
    if(!picker)writeJson(j.dir/L"request.json",{{"source",source},{"cache",utf8(context->cache.wstring())},{"options",options}});
    auto exe=context->bin/L"mmdhl_worker.exe";if(!fs::is_regular_file(exe))throw std::runtime_error("Import worker missing");
    auto quote=[](const fs::path& p){if(p.wstring().find(L'"')!=std::wstring::npos)throw std::runtime_error("Invalid path");return L"\""+p.wstring()+L"\"";};
    auto cmd=quote(exe)+(picker?L" --pick ":L" --request ")+quote(picker?j.dir:j.dir/L"request.json")+(picker&&options.value("kind",std::string())=="static"?L" static":L"");
    j.group=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;SetInformationJobObject(j.group,JobObjectExtendedLimitInformation,&limits,sizeof(limits));
    STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,context->bin.c_str(),&start,&process)){CloseHandle(j.group);throw std::runtime_error("Cannot start import worker");}
    if(!AssignProcessToJobObject(j.group,process.hProcess)){TerminateProcess(process.hProcess,1);CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(j.group);throw std::runtime_error("Cannot isolate worker");}
    ResumeThread(process.hThread);CloseHandle(process.hThread);j.process=process.hProcess;context->jobs.emplace(id,std::move(j));return id;
#endif
}
static void forgetAssets(const std::vector<std::string>& ids){
 for(auto& id:ids){
  if(!validId(id))throw std::runtime_error("Invalid asset ID");
  for(auto& [handle,p]:world().instances)if(p->model->id==id)throw std::runtime_error("Remove the model from the map before deleting it");
  if(auto it=context->loading.find(id);it!=context->loading.end()){if(it->second.wait_for(std::chrono::seconds(0))!=std::future_status::ready)throw std::runtime_error("Model is still loading; retry deletion in a moment");context->loading.erase(it);}
  for(auto& [key,future]:context->fitting)if(key.starts_with(id))throw std::runtime_error("Model is still being fitted; retry deletion in a moment");
  if(context->previews.busy(id))throw std::runtime_error("Model is still being fitted; retry deletion in a moment");
 }
 for(auto& id:ids){
  context->assets.erase(id);
  for(auto it=context->fitted.begin();it!=context->fitted.end();)if(it->first.starts_with(id))it=context->fitted.erase(it);else ++it;
  context->previews.forget(id);
 }
}
FUNCTION(ForgetAssets) {forgetAssets(json(LUA,1).get<std::vector<std::string>>());LUA->PushBool(true);return 1;} END_FUNCTION
#ifndef MMDHL_SERVER
FUNCTION(DeleteAssets) {
 auto ids=json(LUA,1).get<std::vector<std::string>>();
 pruneSharedWork();if(!ids.empty()&&(!context->exports.empty()||!context->transfers.empty()||!context->commits.empty()))throw std::runtime_error("Finish or cancel the model transfer before deleting models");
 for(auto& [id,job]:context->jobs)if(job.process&&WaitForSingleObject(job.process,0)==WAIT_TIMEOUT)throw std::runtime_error("Finish or cancel the import before deleting models");
 // UI releases its preview first. Drop any remaining native-only preview owners.
 for(auto& id:ids){if(context->preview)for(auto& [handle,p]:context->preview->instances)if(p->model->id==id){context->preview.reset();break;}
  for(auto it=context->editors.begin();it!=context->editors.end();){bool found=false;for(auto& [handle,p]:it->second->instances)found|=p->model->id==id;if(found)it=context->editors.erase(it);else ++it;}}
 forgetAssets(ids);push(LUA,deleteAssets(context->cache,ids));return 1;
} END_FUNCTION
#endif
FUNCTION(GetInstallationInfo) {
 auto own=componentIdentity(
#ifdef MMDHL_SERVER
 "server",
#else
 "client",
#endif
 reinterpret_cast<const void*>(&GetInstallationInfo));
 own["release"]=MMDHL_RELEASE;own["build"]=MMDHL_BUILD_ID;
 push(LUA,{{"module",own},{"runtime",runtimeIdentity()}});return 1;
} END_FUNCTION
FUNCTION(ConfigureCompatibility) {push(LUA,configureCompatibility(json(LUA,1)));return 1;} END_FUNCTION
FUNCTION(CheckCompatibility) {push(LUA,checkCompatibility(
#ifdef MMDHL_SERVER
 true
#else
 false
#endif
 ));return 1;} END_FUNCTION
FUNCTION(StartInstallationProbe) {
 if(installationProbe.valid())throw std::runtime_error("Worker self-test already started");
 auto bin=context->bin;bool coacd=LUA->GetBool(1);
 installationProbe=std::async(std::launch::async,[bin,coacd]{return probeWorker(bin,coacd);});LUA->PushBool(true);return 1;
} END_FUNCTION
FUNCTION(PollInstallationProbe) {
 if(!installationProbe.valid()){LUA->PushNil();return 1;}
 if(installationProbe.wait_for(std::chrono::seconds(0))!=std::future_status::ready){push(LUA,{{"pending",true}});return 1;}
 push(LUA,installationProbe.get());return 1;
} END_FUNCTION
FUNCTION(GetCapabilities) {push(LUA,{{"api",ApiVersion},{"version",MMDHL_RELEASE},{"build",MMDHL_BUILD_ID},{"installApi",MMDHL_INSTALL_API},{"rigVersion",RigVersion},{"rigGenerator",RigGenerator},{"physicsEditor",PhysicsSchema},{"sharingVersion",2},{"workers",workerCount()},{"presentationClock","fixed60-interpolated"},{"bulletApiSIMD",false},{"secondaryBroadphaseDefault",secondaryBroadphaseDefault()},{"platform","win64"},{"parser","nanoem"},{"physics","Bullet 3.25"},{"sharedProcessSnapshots",false},{"skinning",{"BDEF1","BDEF2","BDEF4","SDEF","QDEF"}},{"characterImport",{{"version",1},{"probeVersion",1},{"requestVersion",1},{"formats",{"fbx","glb","gltf","dae"}}}}});return 1;} END_FUNCTION
FUNCTION(Browse) {auto kind=LUA->IsType(1,GarrysMod::Lua::Type::String)?stringArg(LUA,1):std::string();if(!kind.empty()&&kind!="static")throw std::runtime_error("Unknown import kind");LUA->PushNumber(double(launch(true,"",kind.empty()?Json::object():Json{{"kind",kind}})));return 1;} END_FUNCTION
// Save a part preset of a cached static prop (materials and/or region cut).
FUNCTION(PropDerive) {auto id=stringArg(LUA,1);if(!validId(id))throw std::runtime_error("Invalid prop ID");if(!fs::exists(context->cache/L"static"/L"assets"/wide(id+".gmdl")))throw std::runtime_error("The original prop is not in the local cache");auto options=json(LUA,2);options["kind"]="derive";options["parent"]=id;LUA->PushNumber(double(launch(false,"",options)));return 1;} END_FUNCTION
// Reimport a static prop from its recorded local source with new import options.
FUNCTION(PropReload) {auto id=stringArg(LUA,1);if(!validId(id))throw std::runtime_error("Invalid prop ID");auto path=context->cache/L"static"/L"sources.local.json";if(!fs::exists(path))throw std::runtime_error("Source path unavailable; import the file again");auto registry=readJson(path);if(!registry.contains(id))throw std::runtime_error("Source path unavailable; import the file again");auto options=json(LUA,2);options["kind"]="static";
    // A .blend reimport keeps the objects chosen at import.
    auto previous=registry[id].value("options",Json::object());if(!options.contains("objects")&&previous.contains("objects"))options["objects"]=previous["objects"];
    LUA->PushNumber(double(launch(false,registry[id].at("source"),options)));return 1;} END_FUNCTION
FUNCTION(BeginImport) {LUA->PushNumber(double(launch(false,stringArg(LUA,1),json(LUA,2))));return 1;} END_FUNCTION
FUNCTION(Reload) {auto id=stringArg(LUA,1);auto registry=readJson(context->cache/L"sources.local.json");if(!registry.contains(id))throw std::runtime_error("Source path unavailable; select the model again");auto entry=registry[id];LUA->PushNumber(double(launch(false,entry.at("source"),entry.value("options",Json::object()))));return 1;} END_FUNCTION
FUNCTION(PollJob) {auto it=context->jobs.find(number(LUA,1));if(it==context->jobs.end())throw std::runtime_error("Unknown job");auto& j=it->second;if(!j.result.is_null()){push(LUA,j.result);return 1;}
    bool finished=WaitForSingleObject(j.process,0)==WAIT_OBJECT_0;Json result;try{result=readJson(j.dir/L"status.json");}catch(const std::exception& e){if(finished)result={{"state","failed"},{"error",e.what()}};else result={{"state","running"},{"stage","Waiting for worker status"},{"progress",0}};}
    if(!finished&&GetTickCount64()-j.started>300000)result["warning"]="Import is taking longer than five minutes. You can keep waiting or cancel.";
    if(finished){if(result.value("state","")=="running")result={{"state","failed"},{"error","Import worker exited before completion"}};j.result=result;CloseHandle(j.process);CloseHandle(j.group);j.process=j.group=nullptr;retireJob(j);}
    else if(result.value("state","")!="running")result={{"state","running"},{"stage","Committing asset"},{"progress",.99}};
    push(LUA,result);return 1;} END_FUNCTION
FUNCTION(CancelJob) {auto it=context->jobs.find(number(LUA,1));if(it!=context->jobs.end()){auto& j=it->second;if(j.group){TerminateJobObject(j.group,1);CloseHandle(j.group);j.group=nullptr;}if(j.process){CloseHandle(j.process);j.process=nullptr;}j.result={{"state","cancelled"}};retireJob(j);}LUA->PushBool(true);return 1;} END_FUNCTION
FUNCTION(RequestAsset) {auto id=stringArg(LUA,1);if(!validId(id))throw std::runtime_error("Invalid asset ID");if(!fs::exists(context->cache/L"assets"/wide(id)/L"manifest.json"))throw std::runtime_error("Model was deleted; import it again");if(!context->assets.contains(id)&&!context->loading.contains(id)){auto cache=context->cache;context->loading[id]=std::async(std::launch::async,[cache,id]{return loadAsset(cache,id);});}LUA->PushBool(true);return 1;} END_FUNCTION
std::shared_ptr<Model> asset(const std::string& id){if(context->assets.contains(id))return context->assets.at(id);auto it=context->loading.find(id);if(it!=context->loading.end()&&it->second.wait_for(std::chrono::seconds(0))==std::future_status::ready){auto pending=std::move(it->second);context->loading.erase(it);auto m=pending.get();for(auto old=context->assets.begin();old!=context->assets.end()&&context->assets.size()>=8;)if(old->second.use_count()==1)old=context->assets.erase(old);else ++old;context->assets[id]=m;return m;}return {};}
FUNCTION(AssetInfo) {auto m=asset(stringArg(LUA,1));if(!m){LUA->PushNil();return 1;}push(LUA,m->info());return 1;} END_FUNCTION
// The physics editor's exact preview, in both realms: the carrier a build with
// these options would produce (shapes, masses, overlaps, .phy text), without
// writing anything. A cached fit answers at once; a refit runs off-thread and
// reports "pending" until the editor's poll finds it done (PreviewQueue).
FUNCTION(PreviewCarrierFit) {
 auto id=stringArg(LUA,1);auto m=asset(id);if(!m){LUA->PushNil();LUA->PushString("Asset not loaded");return 2;}
 auto options=json(LUA,2);if(!options.is_object())throw std::runtime_error("Invalid preview options");
 if(options.value("role",std::string("ragdoll"))=="arms")options.erase("physicsOverrides");
 else if(options.contains("physicsOverrides")){
  auto canonical=canonicalPhysics(options["physicsOverrides"]);
  if(!canonical.errors.empty()){Json errors=Json::array();for(auto& e:canonical.errors)errors.push_back({{"code",e.code},{"path",e.path},{"detail",e.detail}});push(LUA,{{"status","error"},{"errors",errors}});return 1;}
  if(canonical.value.empty())options.erase("physicsOverrides");else options["physicsOverrides"]=canonical.value;
 }
 auto run=[m,options]{try{return previewCarrier(fitRig(*m,options));}catch(const std::exception& e){return Json{{"status","error"},{"errors",Json::array({{{"code","fit_failed"},{"path",""},{"detail",e.what()}}})}};}};
 if(m->fittedRig&&!options.contains("height")&&options.value("excludedMaterials",Json::array()).empty()){push(LUA,run());return 1;}
 push(LUA,context->previews.poll(id,carrierFitKey(id,options),run));return 1;
} END_FUNCTION
// The bone window: a loaded asset's skeleton (options.include ["skeleton"]), the
// automatic assignment, and the structural problems of options.values.
FUNCTION(InspectBoneMap) {auto m=asset(stringArg(LUA,1));if(!m)throw std::runtime_error("Load the asset before requesting a bone map");Json options;try{options=json(LUA,2);}catch(const Json::exception&){throw std::runtime_error("Invalid bone map options");}if(!options.is_object())throw std::runtime_error("Invalid bone map options");push(LUA,inspectBoneMap(*m,options));return 1;} END_FUNCTION
FUNCTION(CreateInstance) {auto id=stringArg(LUA,1);auto m=asset(id);if(!m){m=loadAsset(context->cache,id);context->assets[id]=m;}LUA->PushNumber(double(world().create(m,json(LUA,2))));return 1;} END_FUNCTION
FUNCTION(SubmitSourcePose) {requireServer();auto& p=world().get(number(LUA,1));if(!p.sourceRig)throw std::runtime_error("Instance has no Source carrier");
 auto read=[&](int table,size_t count){std::vector<btTransform> out;out.reserve(count);for(size_t i=0;i<count;i++){LUA->PushNumber(double(i*2+1));LUA->GetTable(table);auto pos=vector(LUA,-1);LUA->Pop();LUA->PushNumber(double(i*2+2));LUA->GetTable(table);auto a=LUA->GetAngle(-1);LUA->Pop();if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.z))throw std::runtime_error("Invalid Source angle");btQuaternion q;q.setEulerZYX(a.y*SIMD_RADS_PER_DEG,a.x*SIMD_RADS_PER_DEG,a.z*SIMD_RADS_PER_DEG);out.emplace_back(q,pos);}return out;};
 auto physical=read(2,18),manual=read(3,p.sourceRig->bones.size());p.submitSourcePose(physical,manual,LUA->GetNumber(4),LUA->IsType(5,GarrysMod::Lua::Type::Bool)&&LUA->GetBool(5));LUA->PushBool(true);return 1;} END_FUNCTION
FUNCTION(StepSources) {requireServer();std::vector<Instance*> pending;for(auto& [id,p]:world().instances)if(p->pendingSourceDelta>0&&p->sourceError.empty())pending.push_back(p.get());parallelFor(pending.size(),1,[&](size_t begin,size_t end){for(size_t i=begin;i<end;i++){auto p=pending[i];try{p->stepSource();}catch(const std::exception& e){p->sourceError=e.what();}}});Json errors=Json::array();for(auto p:pending)if(!p->sourceError.empty())errors.push_back({{"id",p->id},{"error",p->sourceError}});push(LUA,errors);return 1;} END_FUNCTION
FUNCTION(SetMorphs) {auto& p=world().get(number(LUA,1));auto weights=json(LUA,2);if(!weights.is_array()||weights.size()!=p.morphWeights.size())throw std::runtime_error("Morph state does not match model");bool changed=false;std::vector<float> values;for(auto& item:weights){float value=item.get<float>();if(!std::isfinite(value))throw std::runtime_error("Invalid morph weight");values.push_back(std::clamp(value,-2.f,2.f));}for(size_t i=0;i<values.size();i++)if(p.morphWeights[i]!=values[i]){p.morphWeights[i]=values[i];changed=true;}if(changed)p.updatePose();LUA->PushBool(true);return 1;} END_FUNCTION
FUNCTION(GetMaterialState) {auto& p=world().get(number(LUA,1));p.ensureSnapshot();Json alphas=Json::array();for(auto& m:p.snapshot->materials)alphas.push_back(m.alpha);push(LUA,alphas);return 1;} END_FUNCTION
FUNCTION(DestroyInstance) {world().remove(number(LUA,1));return 0;} END_FUNCTION
// Argument 4: the Collide:: kinds the subscriber asked for (all when absent).
FUNCTION(ExportSecondaryScene) {unsigned kinds=Collide::All;if(LUA->IsType(4,GarrysMod::Lua::Type::Number)){double v=LUA->GetNumber(4);if(v!=std::floor(v)||v<0||v>Collide::All)throw std::runtime_error("Invalid scene collision flags");kinds=unsigned(v);}
 push(LUA,context->sceneShare.describe(vector(LUA,1),float(LUA->GetNumber(2)),LUA->IsType(3,GarrysMod::Lua::Type::Number)?uint64_t(LUA->GetNumber(3)):0,kinds));return 1;} END_FUNCTION
FUNCTION(ReadSceneChunk) {auto b=context->sceneShare.chunk(stringArg(LUA,1),number(LUA,2),number(LUA,3));LUA->PushString(reinterpret_cast<const char*>(b.data()),unsigned(b.size()));return 1;} END_FUNCTION
FUNCTION(AcceptSceneGeometry) {auto b=stringArg(LUA,2);context->sceneShare.accept(stringArg(LUA,1),std::span(reinterpret_cast<const unsigned char*>(b.data()),b.size()));LUA->PushBool(true);return 1;} END_FUNCTION
FUNCTION(PublishRemoteScene) {context->sceneShare.publish(world(),json(LUA,1));LUA->PushBool(true);return 1;} END_FUNCTION
FUNCTION(ClearRemoteScene) {context->sceneShare.imported.clear();world().externalScene.store({});return 0;} END_FUNCTION
FUNCTION(GetSharedManifest) {push(LUA,sharedManifest(context->cache,stringArg(LUA,1),json(LUA,2),LUA->GetBool(3)));return 1;} END_FUNCTION
FUNCTION(GetMountablePackage) {auto path=mountablePackage(context->cache,stringArg(LUA,1));LUA->PushString(path.c_str());return 1;} END_FUNCTION
FUNCTION(SharedFileMatches) {auto path=sharedPath(context->cache,stringArg(LUA,1));auto size=number(LUA,2);auto digest=stringArg(LUA,3);LUA->PushBool(fs::is_regular_file(path)&&fs::file_size(path)==size&&hash(readFile(path))==digest);return 1;} END_FUNCTION
FUNCTION(ReadSharedChunk) {auto path=sharedPath(context->cache,stringArg(LUA,1));auto offset=number(LUA,2),length=number(LUA,3);if(length>32768)throw std::runtime_error("Transfer chunk too large");std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Shared file missing");stream.seekg(offset);std::string bytes(size_t(length),'\0');stream.read(bytes.data(),length);bytes.resize(size_t(stream.gcount()));LUA->PushString(bytes.data(),unsigned(bytes.size()));return 1;} END_FUNCTION
FUNCTION(StartSharedExport) {
 pruneSharedWork();if(context->exports.size()>=16)throw std::runtime_error("Model export queue is full");
 auto relative=stringArg(LUA,1),digest=stringArg(LUA,3);auto size=number(LUA,2);sharedPath(context->cache,relative);
 auto id=context->sequence++;SharedExport output;auto cache=context->cache;
 output.preparing=std::async(std::launch::async,[cache,relative,size,digest]{return packSharedFile(cache,relative,size,digest);});
 context->exports.emplace(id,std::move(output));LUA->PushNumber(double(id));return 1;
} END_FUNCTION
FUNCTION(StartSharedMaterialBuild) {
 pruneSharedWork();auto asset=stringArg(LUA,1);if(!validId(asset)||context->exports.size()>=16)throw std::runtime_error("Invalid asset or model preparation queue is full");
 auto cache=context->cache;auto id=context->sequence++;SharedExport output;
 output.preparing=std::async(std::launch::async,[cache,asset]{prepareSourceMaterials(cache,asset);return sharedPath(cache,"assets/"+asset+"/materials-v5.gma");});
 context->exports.emplace(id,std::move(output));LUA->PushNumber(double(id));return 1;
} END_FUNCTION
FUNCTION(GetSharedExportSize) {
 auto& e=context->exports.at(number(LUA,1));if(e.discarded)throw std::runtime_error("Model export was canceled");
 if(e.path.empty()){if(e.preparing.wait_for(std::chrono::seconds(0))!=std::future_status::ready){LUA->PushNumber(0);return 1;}e.path=e.preparing.get();}
 LUA->PushNumber(double(fs::file_size(e.path)));return 1;
} END_FUNCTION
FUNCTION(ReadSharedExport) {
 auto& e=context->exports.at(number(LUA,1));auto offset=number(LUA,2),length=number(LUA,3);
 if(e.discarded||e.path.empty()||!length||length>SharedChunkSize)throw std::runtime_error("Invalid shared chunk request");
 std::ifstream stream(e.path,std::ios::binary);if(!stream)throw std::runtime_error("Shared packet missing");stream.seekg(offset);
 std::string bytes(size_t(length),'\0');if(!stream.read(bytes.data(),length))throw std::runtime_error("Incomplete shared chunk");LUA->PushString(bytes.data(),unsigned(bytes.size()));return 1;
} END_FUNCTION
FUNCTION(ReleaseSharedExport) {auto it=context->exports.find(number(LUA,1));if(it!=context->exports.end())it->second.discarded=true;pruneSharedWork();return 0;} END_FUNCTION
FUNCTION(BeginSharedFile) {
 pruneSharedWork();auto relative=stringArg(LUA,1);sharedPath(context->cache,relative);auto size=number(LUA,2);auto digest=stringArg(LUA,3);
 bool packed=LUA->IsType(4,GarrysMod::Lua::Type::Number);auto wireSize=packed?number(LUA,4):size;
 if(!validId(digest)||size>UINT32_MAX||wireSize>UINT32_MAX||context->transfers.size()>=16||wireSize>size+80+8*((size+SharedBlockSize-1)/SharedBlockSize))throw std::runtime_error("Invalid transfer or transfer queue is full");
 // The packet is staged, then inflated in memory and written beside it.
 {std::error_code error;auto space=fs::space(context->cache,error);if(!error&&space.available<uint64_t(size)+wireSize+(512ull<<20))throw std::runtime_error("Not enough free disk space for this model transfer");}
 auto id=context->sequence++;auto t=std::make_shared<SharedTransfer>();t->relative=relative;t->rawSize=size;t->size=wireSize;t->digest=digest;t->packed=packed;
 t->staging=context->cache/L"sharing"/wide(std::to_string(GetCurrentProcessId())+"-"+std::to_string(id)+".part");fs::create_directories(t->staging.parent_path());t->stream.open(t->staging,std::ios::binary|std::ios::trunc);
 if(!t->stream)throw std::runtime_error("Cannot stage shared file");context->transfers.emplace(id,std::move(t));LUA->PushNumber(double(id));return 1;
} END_FUNCTION
FUNCTION(AppendSharedChunk) {
 auto id=number(LUA,1);auto& t=*context->transfers.at(id);auto offset=number(LUA,2);auto bytes=stringArg(LUA,3);
 if(context->commits.contains(id)||t.canceled||offset!=t.written||bytes.empty()||bytes.size()>SharedChunkSize||bytes.size()>t.size-t.written)throw std::runtime_error("Out-of-order or oversized transfer chunk");
 t.stream.write(bytes.data(),bytes.size());if(!t.stream)throw std::runtime_error("Cannot write shared file");t.written+=bytes.size();LUA->PushBool(true);return 1;
} END_FUNCTION
FUNCTION(CommitSharedFile) {
 auto id=number(LUA,1);auto t=context->transfers.at(id);if(t->written!=t->size||context->commits.contains(id))throw std::runtime_error("Incomplete or already committing transfer");t->stream.close();auto cache=context->cache;
 context->commits.emplace(id,std::async(std::launch::async,[t,cache]{
  auto packet=readFile(t->staging);auto bytes=t->packed?unpackSharedFile(packet,t->rawSize,t->digest):std::move(packet);
  if(!t->packed&&hash(bytes)!=t->digest)throw std::runtime_error("Shared file failed SHA256 verification");
  validateSharedFile(t->relative,bytes);validatePropBundle(t->relative,bytes);if(t->canceled)return false;
  writeAtomic(sharedPath(cache,t->relative),bytes);return true;
 }));LUA->PushBool(true);return 1;
} END_FUNCTION
FUNCTION(PollSharedCommit) {
 auto id=number(LUA,1);auto& future=context->commits.at(id);if(future.wait_for(std::chrono::seconds(0))!=std::future_status::ready){LUA->PushBool(false);return 1;}
 if(!future.get())throw std::runtime_error("Model transfer was canceled");auto& t=*context->transfers.at(id);
 auto slash=t.relative.find('/'),last=t.relative.rfind('/');if(slash!=last&&(t.relative.ends_with("/rig.json")||t.relative.ends_with("/manifest.json")))registerShortName(context->cache,t.relative.substr(0,slash),t.relative.substr(slash+1,last-slash-1));
 context->commits.erase(id);context->transfers.erase(id);LUA->PushBool(true);return 1;
} END_FUNCTION
FUNCTION(CancelSharedFile) {auto id=number(LUA,1);auto it=context->transfers.find(id);if(it!=context->transfers.end()){it->second->canceled=true;context->transfers.erase(it);}pruneSharedWork();return 0;} END_FUNCTION
// Workshop packages. Exports run off the engine thread; one at a time.
#ifndef MMDHL_SERVER
FUNCTION(StartPackageExport) {
 // A completed export counts as finished even if nobody polled it (its window closed).
 for(auto it=context->packages.begin();it!=context->packages.end();){
  if(it->second.finished){it=context->packages.erase(it);continue;}
  if(it->second.future.wait_for(std::chrono::seconds(0))!=std::future_status::ready)throw std::runtime_error("A model package export is already running");
  ++it;
 }
 auto spec=json(LUA,1);auto progress=std::make_shared<PackageProgress>();auto cache=context->cache;auto root=context->root;
 auto id=context->sequence++;PackageJob job;job.progress=progress;
 job.future=std::async(std::launch::async,[cache,root,spec,progress]{return exportPackage(cache,root,spec,*progress);});
 context->packages.emplace(id,std::move(job));LUA->PushNumber(double(id));return 1;
} END_FUNCTION
FUNCTION(PollPackageExport) {
 auto it=context->packages.find(number(LUA,1));if(it==context->packages.end())throw std::runtime_error("Unknown package export");auto& job=it->second;
 if(!job.finished){
  if(job.future.wait_for(std::chrono::seconds(0))!=std::future_status::ready){std::lock_guard lock(job.progress->lock);push(LUA,{{"state","running"},{"progress",job.progress->fraction.load()},{"stage",job.progress->stage}});return 1;}
  try{job.result=job.future.get();job.result["state"]="complete";}catch(const std::exception& e){job.result={{"state",job.progress->cancel?"cancelled":"failed"},{"error",e.what()}};}
  job.finished=true;
 }
 push(LUA,job.result);return 1;
} END_FUNCTION
// Readme, licence and embedded terms of a model file the player chose, before importing it.
FUNCTION(InspectModelNotes) {push(LUA,inspectModelNotes(fs::path(wide(stringArg(LUA,1)))));return 1;} END_FUNCTION
FUNCTION(CancelPackageExport) {auto it=context->packages.find(number(LUA,1));if(it!=context->packages.end())it->second.progress->cancel=true;LUA->PushBool(true);return 1;} END_FUNCTION
// Shows an export in Explorer. Only plain file names inside data/mmd_hotloader/exports.
FUNCTION(RevealPackageExport) {
 auto name=stringArg(LUA,1);if(name.empty()||name.size()>128||name.find_first_of("/\\:*?\"<>|")!=std::string::npos||name.front()=='.'||!name.ends_with(".gma"))throw std::runtime_error("Invalid export name");
 auto path=context->root/L"garrysmod"/L"data"/L"mmd_hotloader"/L"exports"/wide(name);if(!fs::is_regular_file(ioPath(path)))throw std::runtime_error("The exported file no longer exists");
 auto arguments=L"/select,\""+path.wstring()+L"\"";
 LUA->PushBool(reinterpret_cast<intptr_t>(ShellExecuteW(nullptr,L"open",L"explorer.exe",arguments.c_str(),nullptr,SW_SHOWNORMAL))>32);return 1;
} END_FUNCTION
#endif
// Which mounted GMAs contain which package manifests (engine.GetAddons() file paths).
FUNCTION(StartAddonPackageScan) {
 auto files=json(LUA,1);if(!files.is_array()||files.size()>100000)throw std::runtime_error("Invalid addon list");
 auto root=context->root;auto id=context->sequence++;
 context->addonScans.emplace(id,std::async(std::launch::async,[root,files]{return readAddonPackages(root,files);}));LUA->PushNumber(double(id));return 1;
} END_FUNCTION
FUNCTION(PollAddonPackageScan) {
 auto it=context->addonScans.find(number(LUA,1));if(it==context->addonScans.end())throw std::runtime_error("Unknown addon scan");
 if(it->second.wait_for(std::chrono::seconds(0))!=std::future_status::ready){LUA->PushBool(false);return 1;}
 auto result=it->second.get();context->addonScans.erase(it);push(LUA,result);return 1;
} END_FUNCTION
FUNCTION(RebindSourceEntity) {auto& p=world().get(number(LUA,1));if(p.secondary)p.secondary->waitAsyncIdle();p.sceneOwner=number(LUA,2);LUA->PushBool(true);return 1;} END_FUNCTION
// Seven numbers (minimum xyz, maximum xyz, sequence) or nil: the per-frame render path decodes no JSON.
FUNCTION(GetBoundsValues) {auto& p=world().get(number(LUA,1));auto s=p.snapshot;if(!s){LUA->PushNil();return 1;}for(float v:{s->minimum.x(),s->minimum.y(),s->minimum.z(),s->maximum.x(),s->maximum.y(),s->maximum.z()})LUA->PushNumber(v);LUA->PushNumber(double(s->sequence));return 7;} END_FUNCTION
FUNCTION(GetBounds) {auto& p=world().get(number(LUA,1));auto s=p.snapshot;if(!s){LUA->PushNil();return 1;}auto a=s->minimum,b=s->maximum,c=(a+b)*.5f;push(LUA,{{"minimum",{a.x(),a.y(),a.z()}},{"maximum",{b.x(),b.y(),b.z()}},{"center",{c.x(),c.y(),c.z()}},{"sequence",s->sequence}});return 1;} END_FUNCTION
FUNCTION(GetState) {auto& p=world().get(number(LUA,1));p.ensureSnapshot();auto s=p.snapshot;auto c=(s->minimum+s->maximum)*.5f;Json j={{"center",{c.x(),c.y(),c.z()}},{"bodies",Json::array()},{"morphs",p.morphWeights},{"frozen",p.frozen}};
 j["manual"]=Json::array();for(auto& t:p.manual){auto v=t.getOrigin();auto q=t.getRotation();j["manual"].push_back({{"translation",{v.x(),v.y(),v.z()}},{"rotation",{q.x(),q.y(),q.z(),q.w()}}});}
 j["soft"]=Json::array();for(auto& soft:p.softBodies){Json nodes=Json::array();for(int n=0;n<soft.body->m_nodes.size();n++){auto v=soft.body->m_nodes[n].m_x/Inch;nodes.push_back({v.x(),v.y(),v.z()});}j["soft"].push_back(nodes);}
 for(auto& b:p.bodies){auto t=b->rigid->getWorldTransform();auto v=t.getOrigin()/Inch;auto q=t.getRotation();j["bodies"].push_back({{"position",{v.x(),v.y(),v.z()}},{"rotation",{q.x(),q.y(),q.z(),q.w()}}});}push(LUA,j);return 1;} END_FUNCTION
FUNCTION(SetState) {requireServer();auto& p=world().get(number(LUA,1));auto j=json(LUA,2);auto c=j.at("center").get<std::vector<float>>();if(c.size()!=3||j.at("bodies").size()!=p.bodies.size())throw std::runtime_error("State does not match rig");auto offset=vector(LUA,3)-btVector3(c[0],c[1],c[2]);
 for(float x:c)if(!std::isfinite(x))throw std::runtime_error("Invalid state center");
 auto manual=p.manual;if(j.contains("manual")){if(j["manual"].size()!=manual.size())throw std::runtime_error("Manual bone state does not match rig");for(size_t i=0;i<manual.size();i++){auto v=j["manual"][i].at("translation").get<std::vector<float>>(),q=j["manual"][i].at("rotation").get<std::vector<float>>();if(v.size()!=3||q.size()!=4)throw std::runtime_error("Invalid saved pose");for(float x:v)if(!std::isfinite(x))throw std::runtime_error("Invalid saved translation");for(float x:q)if(!std::isfinite(x))throw std::runtime_error("Invalid saved rotation");btQuaternion rot(q[0],q[1],q[2],q[3]);if(rot.length2()<1e-8f)throw std::runtime_error("Zero saved quaternion");manual[i]=btTransform(rot.normalized(),btVector3(v[0],v[1],v[2]));}}
 std::vector<std::vector<btVector3>> softPos;if(j.contains("soft")){if(j["soft"].size()!=p.softBodies.size())throw std::runtime_error("Soft state does not match rig");for(size_t i=0;i<p.softBodies.size();i++){auto& nodes=j["soft"][i];if(nodes.size()!=p.softBodies[i].body->m_nodes.size())throw std::runtime_error("Soft node count mismatch");std::vector<btVector3> out;for(auto& entry:nodes){auto v=entry.get<std::vector<float>>();if(v.size()!=3)throw std::runtime_error("Invalid soft node");for(float x:v)if(!std::isfinite(x))throw std::runtime_error("Invalid soft coordinate");out.push_back((btVector3(v[0],v[1],v[2])+offset)*Inch);}softPos.push_back(std::move(out));}}
 std::vector<btTransform> poses;for(auto& b:j["bodies"]){auto v=b.at("position").get<std::vector<float>>(),q=b.at("rotation").get<std::vector<float>>();if(v.size()!=3||q.size()!=4)throw std::runtime_error("Invalid body state");for(float x:v)if(!std::isfinite(x))throw std::runtime_error("Invalid body position");for(float x:q)if(!std::isfinite(x))throw std::runtime_error("Invalid body rotation");auto rot=btQuaternion(q[0],q[1],q[2],q[3]);if(rot.length2()<1e-8f)throw std::runtime_error("Invalid quaternion");poses.emplace_back(rot.normalized(),(btVector3(v[0],v[1],v[2])+offset)*Inch);}
 p.manual=std::move(manual);p.manualVersion++;for(size_t i=0;i<poses.size();i++){p.bodies[i]->rigid->setWorldTransform(poses[i]);p.bodies[i]->rigid->setInterpolationWorldTransform(poses[i]);p.bodies[i]->rigid->setLinearVelocity({0,0,0});p.bodies[i]->rigid->setAngularVelocity({0,0,0});p.bodies[i]->rigid->clearForces();world().dynamics().updateSingleAabb(p.bodies[i]->rigid.get());}if(j.contains("morphs")){auto m=j["morphs"].get<std::vector<float>>();if(m.size()==p.morphWeights.size())for(size_t i=0;i<m.size();i++)if(std::isfinite(m[i]))p.morphWeights[i]=std::clamp(m[i],-2.f,2.f);}p.freeze(j.value("frozen",false));p.lastImpulseWeights=p.expandedMorphs();p.evaluate(true);p.beforeStep();for(size_t i=0;i<softPos.size();i++){auto& b=*p.softBodies[i].body;for(size_t n=0;n<softPos[i].size();n++){b.m_nodes[int(n)].m_x=b.m_nodes[int(n)].m_q=softPos[i][n];b.m_nodes[int(n)].m_v.setZero();}b.updateNormals();}p.publish(world().time);return 0;} END_FUNCTION
FUNCTION(GetSecondaryCapabilities) {push(LUA,{{"reference",true},{"cpu_mt",true},{"cpu_mt_v2",true},{"gpu_opencl",openclCapabilities()},{"gpu_vulkan",vulkanCapabilities()},{"sleep",Secondary::sleepPolicy()},{"tuning",Secondary::tuning()},{"workers",workerCount()},{"waitBudgetMs",Secondary::asyncWaitBudget()}});return 1;} END_FUNCTION
FUNCTION(GetSecondaryBackend) {auto& p=world().get(number(LUA,1));if(!p.secondary)throw std::runtime_error("This entity has no secondary physics");auto d=p.secondary->diagnostics(false);push(LUA,{{"requested",d["secondaryBackendRequested"]},{"effective",d["secondaryBackend"]},{"reason",d["secondaryBackendFallback"]},{"compute",d.value("compute",Json::object())}});return 1;} END_FUNCTION
FUNCTION(SetVulkanSolverOrdering) {auto order=stringArg(LUA,1);if(order!="colored"&&order!="ordered")throw std::runtime_error("Ordering must be colored or ordered");setVulkanColoring(order=="colored");LUA->PushBool(true);return 1;} END_FUNCTION
FUNCTION(SetSecondaryBackend) {auto& p=world().get(number(LUA,1));if(!p.secondary)throw std::runtime_error("This entity has no secondary physics");auto backend=stringArg(LUA,2);if(backend!="reference"&&backend!="cpu_mt"&&backend!="gpu_opencl"&&backend!="cpu_mt_v2"&&backend!="gpu_vulkan")throw std::runtime_error("Unknown secondary backend");if(backend!=p.secondaryBackend){auto previous=p.secondaryBackend;p.secondaryBackend=backend;try{p.reset();}catch(...){p.secondaryBackend=previous;p.reset();throw;}p.secondary->resetReason="backend_change";}LUA->PushBool(true);return 1;} END_FUNCTION
FUNCTION(SetSecondaryCollisionMode) {auto& p=world().get(number(LUA,1));if(!p.secondary)throw std::runtime_error("Secondary collision modes require a native ragdoll");p.secondary->setCollisionMode(int(number(LUA,2)));return 0;} END_FUNCTION
// Collide:: flags (2.2); SetSecondaryCollisionMode keeps the earlier levels.
FUNCTION(SetSecondaryCollisionFlags) {auto& p=world().get(number(LUA,1));if(!p.secondary)throw std::runtime_error("Secondary collision modes require a native ragdoll");double flags=number(LUA,2);if(flags!=std::floor(flags)||flags<0||flags>Collide::All)throw std::runtime_error("Invalid secondary collision flags");p.secondary->setCollisionFlags(unsigned(flags));return 0;} END_FUNCTION
FUNCTION(SetMaterialVisibility) {auto id=number(LUA,1);auto preview=context->editors.find(id);auto& p=preview==context->editors.end()?world().get(id):preview->second->get(id);auto state=json(LUA,2);auto visible=(state.is_array()?state:state.at("visible")).get<std::vector<bool>>();auto opaque=state.is_array()?std::vector<bool>(visible.size(),false):state.at("forceOpaque").get<std::vector<bool>>();p.setMaterialState(std::move(visible),std::move(opaque));return 0;} END_FUNCTION
FUNCTION(GetWorkerCapabilities) {push(LUA,{{"logicalThreads",availableThreadCount()},{"automatic",automaticWorkerCount(availableThreadCount())},{"maximum",maximumWorkerCount()},{"lanes",workerCount()},{"backgroundThreads",workerCount()-1}});return 1;} END_FUNCTION
FUNCTION(SetSecondaryQuality) {auto& p=world().get(number(LUA,1));if(!p.secondary)throw std::runtime_error("No secondary world");p.secondary->setQuality(int(number(LUA,2)),LUA->GetBool(3));push(LUA,p.secondary->qualityInfo());return 1;} END_FUNCTION
FUNCTION(GetDiagnostics) {if(LUA->IsType(1,GarrysMod::Lua::Type::Number))push(LUA,world().get(number(LUA,1)).diagnostics(!LUA->IsType(2,GarrysMod::Lua::Type::Bool)||LUA->GetBool(2)));else push(LUA,{{"instances",world().instances.size()},{"mirrors",world().mirrors.size()},{"time",world().time},{"tick",world().tick},{"stepMs",world().lastStepMs},{"droppedTime",world().dropped}});return 1;} END_FUNCTION
FUNCTION(Step) {requireServer();world().step(LUA->GetNumber(1),vector(LUA,2)*Inch,false);return 0;} END_FUNCTION
FUNCTION(ResetPhysics) {world().get(number(LUA,1)).reset();LUA->PushBool(true);return 1;} END_FUNCTION
FUNCTION(SetFrozen) {requireServer();world().get(number(LUA,1)).freeze(LUA->GetBool(2));return 0;} END_FUNCTION
FUNCTION(SetMorph) {auto& p=world().get(number(LUA,1));size_t i=number(LUA,2);float v=float(LUA->GetNumber(3));if(i>=p.morphWeights.size()||!std::isfinite(v))throw std::runtime_error("Invalid morph");v=std::clamp(v,-2.f,2.f);if(p.morphWeights[i]!=v){p.morphWeights[i]=v;p.updatePose();}return 0;} END_FUNCTION
FUNCTION(SetBonePose) {auto& p=world().get(number(LUA,1));size_t i=number(LUA,2);if(i>=p.manual.size())throw std::runtime_error("Invalid bone");auto j=json(LUA,3);auto v=j.value("translation",std::vector<float>{0,0,0});auto q=j.value("rotation",std::vector<float>{0,0,0,1});if(v.size()!=3||q.size()!=4)throw std::runtime_error("Invalid pose");for(float x:v)if(!std::isfinite(x))throw std::runtime_error("Invalid pose translation");for(float x:q)if(!std::isfinite(x))throw std::runtime_error("Invalid pose rotation");if(btQuaternion(q[0],q[1],q[2],q[3]).length2()<1e-8f)throw std::runtime_error("Zero pose quaternion");p.setBonePose(i,btTransform(btQuaternion(q[0],q[1],q[2],q[3]).normalized(),btVector3(v[0],v[1],v[2])));return 0;} END_FUNCTION
FUNCTION(SetMirror) {requireServer();auto id=number(LUA,1);auto j=json(LUA,2);std::string buffer=LUA->IsType(3,GarrysMod::Lua::Type::String)?stringArg(LUA,3):std::string();if(buffer.size()%sizeof(float))throw std::runtime_error("Invalid collider buffer");world().setMirror(id,j,std::span(reinterpret_cast<const float*>(buffer.data()),buffer.size()/sizeof(float)));return 0;} END_FUNCTION
FUNCTION(RemoveMirror) {requireServer();world().removeMirror(number(LUA,1));return 0;} END_FUNCTION
FUNCTION(TakeImpulses) {requireServer();push(LUA,world().takeImpulses());return 1;} END_FUNCTION
FUNCTION(Raycast) {push(LUA,world().raycast(vector(LUA,1)*Inch,vector(LUA,2)*Inch,LUA->IsType(3,GarrysMod::Lua::Type::Number)?number(LUA,3):0,LUA->GetBool(4)));return 1;} END_FUNCTION
static btTransform toolTransform(Lua* LUA,int index){auto p=vector(LUA,index)*Inch,a=vector(LUA,index+1)*SIMD_RADS_PER_DEG;btQuaternion q;q.setEulerZYX(a.y(),a.x(),a.z());return btTransform(q,p);}
FUNCTION(BeginPhysgun) {requireServer();world().beginPhysgun(number(LUA,1),int(number(LUA,2)),toolTransform(LUA,3));return 0;} END_FUNCTION
FUNCTION(UpdatePhysgun) {requireServer();world().updatePhysgun(toolTransform(LUA,1));return 0;} END_FUNCTION
FUNCTION(GetBoneTransform) {auto& p=world().get(number(LUA,1));size_t i=number(LUA,2);if(i>=p.global.size())throw std::runtime_error("Invalid bone");auto t=p.placement*convert(p.global[i],p.scale);auto v=t.getOrigin()/Inch;float y,pitch,r;t.getBasis().getEulerZYX(y,pitch,r);push(LUA,{{"position",{v.x(),v.y(),v.z()}},{"angles",{pitch*SIMD_DEGS_PER_RAD,y*SIMD_DEGS_PER_RAD,r*SIMD_DEGS_PER_RAD}}});return 1;} END_FUNCTION
FUNCTION(GetMorphWeights) {push(LUA,world().get(number(LUA,1)).morphWeights);return 1;} END_FUNCTION
FUNCTION(BeginGrab) {requireServer();world().beginGrab(number(LUA,1),int(number(LUA,2)),vector(LUA,3)*Inch);return 0;} END_FUNCTION
FUNCTION(UpdateGrab) {requireServer();world().updateGrab(vector(LUA,1)*Inch);return 0;} END_FUNCTION
FUNCTION(EndGrab) {requireServer();world().endGrab();return 0;} END_FUNCTION
FUNCTION(Clear) {world().clear();return 0;} END_FUNCTION
#ifdef MMDHL_SERVER
FUNCTION(ReadAnimationModel) {auto bytes=stringArg(LUA,1);push(LUA,readAnimationModel(std::span(reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size())));return 1;} END_FUNCTION
FUNCTION(RequestCarrierFit) {
 // Canonical physics first: equal profiles share one fit, and a stale fit is never reused.
 auto id=stringArg(LUA,1);auto options=normalizeCarrierOptions(json(LUA,2));auto key=carrierFitKey(id,options);if(context->fitted.contains(key)){LUA->PushBool(true);return 1;}
 auto m=asset(id);if(!m)throw std::runtime_error("Load the asset before requesting a fit");
 if(!context->fitting.contains(key)){context->fitting[key]=std::async(std::launch::async,[m,options]{return fitRig(*m,options);});LUA->PushBool(false);return 1;}
 auto& pending=context->fitting.at(key);if(pending.wait_for(std::chrono::seconds(0))!=std::future_status::ready){LUA->PushBool(false);return 1;}
 auto ready=std::move(pending);context->fitting.erase(key);if(context->fitted.size()>=16)context->fitted.erase(context->fitted.begin());context->fitted.emplace(key,ready.get());LUA->PushBool(true);return 1;
} END_FUNCTION
FUNCTION(PrepareCarrier) {
 auto id=stringArg(LUA,1);auto options=normalizeCarrierOptions(json(LUA,2));auto key=carrierFitKey(id,options);auto m=asset(id);if(!m){m=loadAsset(context->cache,id);context->assets[id]=m;}
 auto rig=context->fitted.contains(key)?context->fitted.at(key):fitRig(*m,options);auto cached=context->cache/L"rigs"/wide(rig.key)/L"carrier.gma";
 if(fs::is_regular_file(cached)){registerShortName(context->cache,"rigs",rig.key);retainCacheFiles(context->cache,{fs::path(L"rigs")/wide(rig.key)});auto result=rig.manifest;result["gma"]="data/mmd_hotloader/rigs/"+rig.key+"/carrier.gma";push(LUA,result);}else push(LUA,packageCarrier(context->cache,rig,carrierPhysics(rig)));return 1;
} END_FUNCTION

FUNCTION(ProbePhysics) {push(LUA,probePhysics());return 1;} END_FUNCTION
FUNCTION(ProbeCarrierCollisions) {push(LUA,probeCarrierCollisions(unsigned(number(LUA,1))));return 1;} END_FUNCTION
FUNCTION(CaptureSecondaryScene) {
 if(!LUA->IsType(1,GarrysMod::Lua::Type::Table)){clearSecondaryScene();return 0;}
 std::unordered_map<void*,uint64_t> owners;LUA->PushNil();while(LUA->Next(1)){
  auto id=number(LUA,-2);if(LUA->IsType(-1,GarrysMod::Lua::Type::Table)){LUA->PushNil();while(LUA->Next(-2)){if(auto object=LUA->GetUserType<void>(-1,GarrysMod::Lua::Type::PhysObj))owners[object]=(id<<16)|(number(LUA,-2)-1);LUA->Pop();}}
  LUA->Pop();
 }
 std::unordered_set<void*> excluded;
 if(LUA->IsType(3,GarrysMod::Lua::Type::Table)){LUA->PushNil();while(LUA->Next(3)){if(auto object=LUA->GetUserType<void>(-1,GarrysMod::Lua::Type::PhysObj))excluded.insert(object);LUA->Pop();}}
 // Living players' (4) and NPCs' (5) bodies: tagged, each world chooses (2.2).
 std::unordered_map<void*,uint8_t> actors;
 for(int index:{4,5})if(LUA->IsType(index,GarrysMod::Lua::Type::Table)){LUA->PushNil();while(LUA->Next(index)){if(auto object=LUA->GetUserType<void>(-1,GarrysMod::Lua::Type::PhysObj))actors[object]=index==4?SceneObject::LivingPlayer:SceneObject::LivingNpc;LUA->Pop();}}
 push(LUA,captureSecondaryScene(owners,LUA->GetNumber(2),excluded,actors));return 1;
} END_FUNCTION
FUNCTION(CapturePhysics) {push(LUA,capturePhysics());return 1;} END_FUNCTION
// The scene consumers' regions as a flat list (6 numbers each: minimum, maximum);
// empty when none has registered yet.
FUNCTION(SceneInterest) {LUA->CreateTable();double k=1;for(auto& r:sceneInterest())for(float v:{r.lower.x(),r.lower.y(),r.lower.z(),r.upper.x(),r.upper.y(),r.upper.z()}){LUA->PushNumber(k++);LUA->PushNumber(v);LUA->SetTable(-3);}return 1;} END_FUNCTION
#endif
#ifndef MMDHL_SERVER
static void readPresentation(GarrysMod::Lua::ILuaBase* LUA,Instance& p,int table,double timestamp,uint64_t frame){
 if(!p.sourceRig)throw std::runtime_error("No Source rig");
 thread_local std::vector<btTransform> palette;palette.clear();palette.reserve(p.sourceRig->bones.size());
 for(size_t i=0;i<p.sourceRig->bones.size();i++){
  LUA->PushNumber(double(i*2+1));LUA->GetTable(table);auto position=vector(LUA,-1);LUA->Pop();
  LUA->PushNumber(double(i*2+2));LUA->GetTable(table);auto a=LUA->GetAngle(-1);LUA->Pop();
  if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.z))throw std::runtime_error("Invalid presentation angle");
  btQuaternion q;q.setEulerZYX(a.y*SIMD_RADS_PER_DEG,a.x*SIMD_RADS_PER_DEG,a.z*SIMD_RADS_PER_DEG);palette.emplace_back(q,position);
 }
 p.submitPresentationPose(palette,timestamp,frame);
}
FUNCTION(SubmitPresentationPose) {readPresentation(LUA,world().get(number(LUA,1)),2,LUA->GetNumber(3),number(LUA,4));LUA->PushBool(true);return 1;} END_FUNCTION
// Bone-to-world matrices straight from Entity:GetBoneMatrix (Lua Matrix userdata, a
// row-major 4x4 VMatrix). Columns are normalised so bone scale, which the angle path
// dropped, does not reach the rotation.
struct LuaMatrix {float m[4][4];};
static void readPresentationMatrices(GarrysMod::Lua::ILuaBase* LUA,Instance& p,int table,double timestamp,uint64_t frame){
 if(!p.sourceRig)throw std::runtime_error("No Source rig");
 thread_local std::vector<btTransform> palette;palette.clear();palette.reserve(p.sourceRig->bones.size());
 for(size_t i=0;i<p.sourceRig->bones.size();i++){
  LUA->PushNumber(double(i+1));LUA->GetTable(table);auto matrix=LUA->GetUserType<LuaMatrix>(-1,GarrysMod::Lua::Type::Matrix);
  if(!matrix){LUA->Pop();throw std::runtime_error("Presentation palette entry is not a matrix");}
  const auto& m=matrix->m;btVector3 axes[3];
  for(int c=0;c<3;c++){axes[c]=btVector3(m[0][c],m[1][c],m[2][c]);float length=axes[c].length();if(!std::isfinite(length)||length<1e-6f){LUA->Pop();throw std::runtime_error("Invalid presentation matrix");}axes[c]/=length;}
  btVector3 origin(m[0][3],m[1][3],m[2][3]);LUA->Pop();
  if(!std::isfinite(origin.x())||!std::isfinite(origin.y())||!std::isfinite(origin.z()))throw std::runtime_error("Invalid presentation position");
  btMatrix3x3 basis(axes[0].x(),axes[1].x(),axes[2].x(),axes[0].y(),axes[1].y(),axes[2].y(),axes[0].z(),axes[1].z(),axes[2].z());
  btQuaternion q;basis.getRotation(q);palette.emplace_back(q.normalized(),origin);
 }
 p.submitPresentationPose(palette,timestamp,frame);
}
FUNCTION(SubmitPresentationMatrixBatch) {
 double timestamp=LUA->GetNumber(2);auto frame=number(LUA,3);
 for(unsigned i=1;;i++){
  LUA->PushNumber(i);LUA->GetTable(1);if(LUA->IsType(-1,GarrysMod::Lua::Type::Nil)){LUA->Pop();break;}
  int entry=LUA->Top();LUA->PushNumber(1);LUA->GetTable(entry);auto& p=world().get(number(LUA,-1));LUA->Pop();
  LUA->PushNumber(2);LUA->GetTable(entry);readPresentationMatrices(LUA,p,LUA->Top(),timestamp,frame);LUA->Pop(2);
 }
 LUA->PushBool(true);return 1;
} END_FUNCTION
FUNCTION(SubmitPresentationBatch) {
 double timestamp=LUA->GetNumber(2);auto frame=number(LUA,3);
 for(unsigned i=1;;i++){
  LUA->PushNumber(i);LUA->GetTable(1);if(LUA->IsType(-1,GarrysMod::Lua::Type::Nil)){LUA->Pop();break;}
  int entry=LUA->Top();LUA->PushNumber(1);LUA->GetTable(entry);auto& p=world().get(number(LUA,-1));LUA->Pop();
  LUA->PushNumber(2);LUA->GetTable(entry);readPresentation(LUA,p,LUA->Top(),timestamp,frame);LUA->Pop(2);
 }
 LUA->PushBool(true);return 1;
} END_FUNCTION
FUNCTION(GetAlignmentProbe) {
 auto& p=world().get(number(LUA,1));p.requireCpuVertices();Json bones=Json::array(),vertices=Json::array();
 if(p.sourceRig)for(size_t i=0;i<p.sourceRig->bones.size();i++){auto& b=p.sourceRig->bones[i];if(b.mmd<0)continue;auto t=p.placement*convert(p.snapshot->bones[b.mmd],p.scale);auto pos=t.getOrigin()/Inch;Json j={{"source",i},{"mmd",b.mmd},{"meshBone",{pos.x(),pos.y(),pos.z()}}};if(i<p.presentationBones.size()){auto v=p.presentationBones[i].getOrigin();j["sourceBone"]={v.x(),v.y(),v.z()};j["error"]=(pos-v).length();}bones.push_back(j);}
 // Independent rigidly weighted vertices detect skinning/bind errors that a bone-only comparison misses.
 for(size_t i=0;i<p.model->vertices.size();i++){auto& v=p.model->vertices[i];if(v.weights[0]<.9999f||v.bones[0]<0||!p.sourceRig)continue;int b=p.sourceControl[v.bones[0]];if(b<0||size_t(b)>=p.presentationBones.size())continue;if(i%37!=0)continue;auto facing=rigMeshBind(*p.sourceRig);auto expected=p.presentationBones[b]*p.sourceRig->bones[b].rest.inverse()*(facing*(toSource(v.position)*p.sourceRig->scale));auto& d=p.snapshot->vertices[i];auto rendered=btVector3(d.x,d.y,d.z);vertices.push_back({{"index",i},{"sourceBone",b},{"expected",{expected.x(),expected.y(),expected.z()}},{"rendered",{d.x,d.y,d.z}},{"error",(expected-rendered).length()}});if(vertices.size()>=128)break;}
 push(LUA,{{"frame",p.presentationFrame},{"time",p.sourceTimestamp},{"bones",bones},{"vertices",vertices}});return 1;
} END_FUNCTION
FUNCTION(GetModelAnimationDiagnostics) {auto value=modelAnimationDiagnostics(stringArg(LUA,1));LUA->PushString(value.c_str());return 1;} END_FUNCTION
FUNCTION(CreateEditorPreview) {auto m=asset(stringArg(LUA,1));if(!m)throw std::runtime_error("Asset not loaded");auto host=std::make_unique<World>();host->next=1000000000+context->sequence++;auto rig=json(LUA,2);auto id=host->create(m,{{"backend","source"},{"rigManifest",rig},{"frozen",true}});context->editors[id]=std::move(host);LUA->PushNumber(double(id));return 1;} END_FUNCTION
FUNCTION(GetEditorPreviewBounds) {auto id=number(LUA,1);auto it=context->editors.find(id);if(it==context->editors.end())throw std::runtime_error("Unknown editor preview");auto& p=it->second->get(id);p.ensureSnapshot();btVector3 lo(BT_LARGE_FLOAT,BT_LARGE_FLOAT,BT_LARGE_FLOAT),hi=-lo;bool found=false;
 for(auto& part:p.snapshot->materials)if(part.alpha>.0001f)for(unsigned i=part.first;i<part.first+part.count;i++){auto& v=p.snapshot->vertices[p.model->indices[i]];btVector3 pos(v.x,v.y,v.z);lo.setMin(pos);hi.setMax(pos);found=true;}
 if(!found){lo={-1,-1,-1};hi={1,1,1};}push(LUA,{{"minimum",{lo.x(),lo.y(),lo.z()}},{"maximum",{hi.x(),hi.y(),hi.z()}}});return 1;} END_FUNCTION
FUNCTION(DestroyEditorPreview) {context->editors.erase(number(LUA,1));return 0;} END_FUNCTION
FUNCTION(DrawEditorPreview) {auto id=number(LUA,1);auto it=context->editors.find(id);if(it!=context->editors.end())drawInstanceNative(it->second->get(id),unsigned(number(LUA,2)),stringArg(LUA,3),false);return 0;} END_FUNCTION
FUNCTION(GetMaterialMesh) {
 auto m=asset(stringArg(LUA,1));if(!m)throw std::runtime_error("Asset not loaded");size_t index=number(LUA,2);if(index>=m->materials.size())throw std::runtime_error("Invalid material slot");float scale=float(LUA->GetNumber(3));if(!std::isfinite(scale)||scale<=0)throw std::runtime_error("Invalid mesh scale");
 auto& part=m->materials[index];Json vertices=Json::array(),indices=Json::array();std::map<unsigned,unsigned> seen;
 for(unsigned i=part.first;i<part.first+std::min(part.count,90000u);i++){auto vi=m->indices[i];auto [it,added]=seen.emplace(vi,unsigned(seen.size()));if(added){auto& v=m->vertices[vi];auto p=toSource(v.position)*scale,n=toSource(v.normal);vertices.push_back({p.x(),p.y(),p.z(),n.x(),n.y(),n.z(),v.uv[0],v.uv[1]});}indices.push_back(it->second);}
 push(LUA,{{"vertices",vertices},{"indices",indices}});return 1;
} END_FUNCTION
FUNCTION(GetMaterialPositions) {auto& p=world().get(number(LUA,1));auto index=number(LUA,2);if(index>=p.model->materials.size())throw std::runtime_error("Invalid material slot");p.requireCpuVertices();Json out=Json::array();auto& part=p.model->materials[index];for(unsigned i=part.first;i<part.first+std::min(part.count,90000u);i++){auto& v=p.snapshot->vertices[p.model->indices[i]];out.push_back({v.x,v.y,v.z});}push(LUA,out);return 1;} END_FUNCTION
FUNCTION(CreatePreview) {auto m=asset(stringArg(LUA,1));if(!m)throw std::runtime_error("Asset not loaded");context->preview=std::make_unique<World>();auto options=json(LUA,2);options["center"]={0,0,0};options["frozen"]=true;auto id=context->preview->create(m,options);LUA->PushNumber(double(id));return 1;} END_FUNCTION
FUNCTION(ClearPreview) {context->preview.reset();return 0;} END_FUNCTION
FUNCTION(DrawPreview) {if(context->preview)drawInstanceNative(context->preview->get(number(LUA,1)),unsigned(number(LUA,2)),stringArg(LUA,3),LUA->GetBool(4));return 0;} END_FUNCTION
FUNCTION(Draw) {RenderTint tint;if(LUA->IsType(5,GarrysMod::Lua::Type::Vector)){auto rgb=vector(LUA,5);tint.r=rgb.x();tint.g=rgb.y();tint.b=rgb.z();}if(LUA->IsType(6,GarrysMod::Lua::Type::Number))tint.a=float(LUA->GetNumber(6));for(float v:{tint.r,tint.g,tint.b,tint.a})if(!std::isfinite(v)||v<0||v>1)throw std::runtime_error("Invalid entity render tint");drawNative(number(LUA,1),unsigned(number(LUA,2)),stringArg(LUA,3),LUA->GetBool(4),tint);return 0;} END_FUNCTION
// Per-part engine material names for the native passes, registered once per asset.
FUNCTION(SetInstanceMaterials) {auto& p=world().get(number(LUA,1));auto names=json(LUA,2);p.colorNames=names.value("color",std::vector<std::string>{});p.depthNames=names.value("depth",std::vector<std::string>{});if(p.colorNames.size()!=p.model->materials.size()||p.depthNames.size()!=p.model->materials.size())throw std::runtime_error("Material names do not match the model");LUA->PushBool(true);return 1;} END_FUNCTION
// One call per entity and pass: (instance, translucent pass, depth pass, override JSON or nil, tint vector, alpha).
// Default parts belong to the opaque pass; override materials go to the pass their flags select, as the Lua loop did.
FUNCTION(DrawInstance) {
 auto& p=world().get(number(LUA,1));bool translucent=LUA->GetBool(2),depth=LUA->GetBool(3);
 struct ViewRestore{Instance& p;~ViewRestore(){p.renderView=0;}} restoreView{p};p.renderView=LUA->GetBool(7)?1:0;
 std::unordered_map<unsigned,std::string> overrides;
 if(LUA->IsType(4,GarrysMod::Lua::Type::String)){
  // items() is a proxy referencing its JSON owner. Iterating a temporary
  // destroyed that owner before the loop and corrupted restored submaterials.
  const auto values=json(LUA,4);
  for(const auto& [key,value]:values.items())overrides[unsigned(std::stoul(key))]=value.get<std::string>();
 }
 RenderTint tint;if(LUA->IsType(5,GarrysMod::Lua::Type::Vector)){auto rgb=vector(LUA,5);tint.r=rgb.x();tint.g=rgb.y();tint.b=rgb.z();}if(LUA->IsType(6,GarrysMod::Lua::Type::Number))tint.a=float(LUA->GetNumber(6));
 for(float v:{tint.r,tint.g,tint.b,tint.a})if(!std::isfinite(v)||v<0||v>1)throw std::runtime_error("Invalid entity render tint");
 if(tint.a<=0)return 0;
 auto& names=depth?p.depthNames:p.colorNames;if(names.size()!=p.model->materials.size())throw std::runtime_error("Material names are not registered for this instance");
 std::vector<std::pair<unsigned,std::string>> parts;parts.reserve(names.size());
 for(unsigned i=0;i<names.size();i++){
  if(i<p.materialVisible.size()&&!p.materialVisible[i])continue;
  const std::string* name=&names[i];
  if(!depth){bool translucentPart=false;auto o=overrides.find(i);if(o!=overrides.end()){name=&o->second;translucentPart=materialIsTranslucent(*name);}if(translucentPart!=translucent)continue;}
  parts.emplace_back(i,*name);
 }
 drawInstanceNativeBatch(p,parts,tint);
 // An error raised by an earlier draw on Source's render thread (queued mode).
 if(auto error=takeAsyncRenderError();!error.empty()){LUA->PushNil();LUA->PushString(error.c_str());return 2;}
 return 0;
} END_FUNCTION
FUNCTION(RenderStatus) {LUA->PushString(rendererStatus().c_str());return 1;} END_FUNCTION
FUNCTION(CheckRenderer) {checkRenderer();LUA->PushBool(true);return 1;} END_FUNCTION
FUNCTION(RenderStats) {LUA->PushString(rendererStats().c_str());return 1;} END_FUNCTION
FUNCTION(GetLightingState) {LUA->PushString(renderLightingState().c_str());return 1;} END_FUNCTION
FUNCTION(SetFlashlightOverlapGuard) {setLightOverlapGuard(LUA->GetBool(1));return 0;} END_FUNCTION
FUNCTION(RenderFrameStats) {LUA->PushString(renderFrameStats().c_str());return 1;} END_FUNCTION
FUNCTION(SetNativeVertexCache) {setNativeVertexCache(LUA->GetBool(1));return 0;} END_FUNCTION
FUNCTION(SetCompactVertices) {setCompactVertices(LUA->GetBool(1));return 0;} END_FUNCTION
FUNCTION(SetGpuSkinning) {setGpuSkinning(LUA->GetBool(1));return 0;} END_FUNCTION
// Render frames since the instance was last drawn in any pass (color, depth, flashlight or shadow).
FUNCTION(SetPoseSmoothing) {world().poseSmoothing=LUA->GetBool(1);return 0;} END_FUNCTION
FUNCTION(GetDrawAge) {auto& p=world().get(number(LUA,1));LUA->PushNumber(p.lastDrawFrame?double(renderFrameNumber()-p.lastDrawFrame):1e9);return 1;} END_FUNCTION
FUNCTION(SetupSourceLighting) {auto p=vector(LUA,1);setupSourceLighting(p.x(),p.y(),p.z());LUA->PushBool(true);return 1;} END_FUNCTION
FUNCTION(RegisterSourceShadow) {
 auto instance=number(LUA,2);int key;void* physics=nullptr;
 if(LUA->IsType(1,GarrysMod::Lua::Type::PhysObj)){
  physics=LUA->GetUserType<void>(1,GarrysMod::Lua::Type::PhysObj);
  if(!physics||instance>1000000000)throw std::runtime_error("Source shadow physics object not available");key=int(1048576+instance);
 }else key=int(number(LUA,1));
 registerSourceShadow(key,instance,json(LUA,3).get<std::vector<std::string>>(),float(LUA->GetNumber(4)),physics);LUA->PushBool(true);return 1;
} END_FUNCTION
FUNCTION(RemoveSourceShadow) {removeSourceShadow(int(number(LUA,1)));return 0;} END_FUNCTION
FUNCTION(SetSecondaryBroadphase) {setSecondaryBroadphaseDefault(stringArg(LUA,1));LUA->PushString(secondaryBroadphaseDefault().c_str());return 1;} END_FUNCTION
FUNCTION(SetWorkers) {for(auto& [id,p]:world().instances)if(p->secondary)p->secondary->waitAsyncIdle();drainRenderQueue();/* queued draws stream vertices on the pool */setWorkerCount(unsigned(number(LUA,1)));LUA->PushBool(true);return 1;} END_FUNCTION
FUNCTION(SetSecondaryTuning) {double accuracy=LUA->GetNumber(1);if(!std::isfinite(accuracy)||accuracy<-1||accuracy>100||std::floor(accuracy)!=accuracy)throw std::runtime_error("Physics accuracy must be -1, 0, or 1–100");Secondary::setTuning(int(accuracy),float(LUA->GetNumber(2)),float(LUA->GetNumber(3)),LUA->GetBool(4),float(LUA->GetNumber(5)));LUA->PushBool(true);return 1;} END_FUNCTION
FUNCTION(SetSecondaryWaitBudget) {Secondary::setAsyncWaitBudget(LUA->GetNumber(1));return 0;} END_FUNCTION
FUNCTION(SetSpringRelativeDamping) {setSpringRelativeDamping(LUA->GetBool(1));LUA->PushBool(springRelativeDamping());return 1;} END_FUNCTION
// Diagnostic A/B switch for the narrowphase mid-phase gate (harness only; the gate is exact).
FUNCTION(SetSecondaryMidphase) {setMidphaseGate(LUA->GetBool(1));LUA->PushBool(midphaseGate());return 1;} END_FUNCTION
FUNCTION(SetRenderSuspended) {setRenderSuspended(LUA->GetBool(1));return 0;} END_FUNCTION
FUNCTION(SetSecondarySleep) {Secondary::setSleepPolicy(LUA->GetBool(1),float(LUA->GetNumber(2)),float(LUA->GetNumber(3)),float(LUA->GetNumber(4)),LUA->IsType(5,GarrysMod::Lua::Type::Number)?float(LUA->GetNumber(5)):.02f);return 0;} END_FUNCTION
FUNCTION(PrepareFrame) {
 beginRenderFrame();auto start=std::chrono::steady_clock::now();std::vector<Instance*> pending;
 for(auto& [id,p]:world().instances)if(p->sourceError.empty()&&(!p->snapshot||p->poseDirty||(p->presentationDriven&&(p->pendingSourceDelta>0||p->presentationDirty))))pending.push_back(p.get());
 setSecondaryWorkload(unsigned(pending.size()));
 unsigned gpuDue=0;for(auto p:pending)if(p->secondary&&p->secondary->effectiveBackend=="gpu_opencl"&&p->secondary->accumulator+p->pendingSourceDelta+1e-8>=1./60)gpuDue++;setComputeBatchSize(gpuDue);
 parallelFor(pending.size(),1,[&](size_t begin,size_t end){for(size_t i=begin;i<end;i++){auto p=pending[i];p->evaluateMs=p->deformMs=0;try{if(p->presentationDriven)p->stepSource();p->ensureSnapshot();}catch(const std::exception& e){p->sourceError=e.what();}}});
 double physics=0,pose=0,deform=0,scene=0,lag=0;unsigned steps=0,asynchronous=0,sleeping=0,changed=0,full=0,idle=0,fullMorph=0,fullStatics=0,fullSoft=0,fullBuffer=0;
 for(auto p:pending){pose+=p->evaluateMs;deform+=p->deformMs;changed+=p->changedBones;full+=p->fullChange;fullMorph+=(p->fullChangeReasons&1)!=0;fullStatics+=(p->fullChangeReasons&2)!=0;fullSoft+=(p->fullChangeReasons&4)!=0;fullBuffer+=(p->fullChangeReasons&16)!=0;idle+=!p->poseDirty&&p->snapshot&&p->changedBones==0&&!p->fullChange;if(p->secondary){auto stats=p->secondary->frameStats();physics+=stats.physicsMs;scene+=stats.sceneMs;steps+=stats.steps;if(p->secondary->asynchronous()){asynchronous++;lag=std::max(lag,p->secondary->lagMs);sleeping+=p->secondary->sleepingBodies;}}}
 double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();LUA->PushNumber(elapsed);
 // The profile is JSON for the HUD and probes; the ordinary frame asks for none.
 if(LUA->IsType(1,GarrysMod::Lua::Type::Bool)&&!LUA->GetBool(1))return 1;
 push(LUA,{{"prepareWallMs",elapsed},{"physicsWorkMs",physics},{"poseWorkMs",pose},{"deformWorkMs",deform},{"sceneWorkMs",scene},{"simulationSteps",steps},{"workers",workerCount()},{"asyncWorlds",asynchronous},{"asyncLagMs",lag},{"sleepingBodies",sleeping},{"changedBones",changed},{"fullChanges",full},{"fullMorph",fullMorph},{"fullStatics",fullStatics},{"fullSoft",fullSoft},{"fullBuffer",fullBuffer},{"idleInstances",idle}});return 2;
} END_FUNCTION
FUNCTION(PruneRenderCache) {pruneRenderCache(LUA->GetBool(1));return 0;} END_FUNCTION
// Diagnostic sampling profiler of the calling (main) thread; see thread_sampler.hpp.
MainThreadSampler mainThreadSampler;
FUNCTION(StartMainThreadSampling) {double ms=LUA->GetNumber(1);if(!std::isfinite(ms)||ms<100||ms>60000)throw std::runtime_error("Sampling duration must be 100 to 60000 ms");mainThreadSampler.start(ms);LUA->PushBool(true);return 1;} END_FUNCTION
FUNCTION(ReadMainThreadSamples) {auto report=mainThreadSampler.report(context->bin.wstring());if(report.is_null()){LUA->PushNil();return 1;}push(LUA,report);return 1;} END_FUNCTION
#endif
}
GMOD_MODULE_OPEN(){
    try {context=std::make_unique<Context>();acquireRuntimeRealm();wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);auto path=fs::path(exe).parent_path();auto root=path.filename()==L"win64"?path.parent_path().parent_path():path;
        context->root=root;context->bin=root/L"garrysmod"/L"lua"/L"bin";context->cache=ioPath(root/L"garrysmod"/L"data"/L"mmd_hotloader");fs::create_directories(context->cache);
#ifndef MMDHL_SERVER
        sweepJobFolders(context->cache,std::chrono::hours(24));
#endif
        LUA->CreateTable();
#define REGISTER(name) LUA->PushCFunction(name);LUA->SetField(-2,#name)
        REGISTER(GetInstallationInfo);REGISTER(ConfigureCompatibility);REGISTER(CheckCompatibility);
#ifndef MMDHL_SERVER
        REGISTER(StartInstallationProbe);REGISTER(PollInstallationProbe);REGISTER(StartPackageExport);REGISTER(PollPackageExport);REGISTER(CancelPackageExport);REGISTER(RevealPackageExport);REGISTER(InspectModelNotes);
#endif
        REGISTER(GetMountablePackage);REGISTER(StartAddonPackageScan);REGISTER(PollAddonPackageScan);
        REGISTER(ExportSecondaryScene);REGISTER(ReadSceneChunk);REGISTER(AcceptSceneGeometry);REGISTER(PublishRemoteScene);REGISTER(ClearRemoteScene);REGISTER(StartSharedMaterialBuild);REGISTER(StartSharedExport);REGISTER(GetSharedExportSize);REGISTER(ReadSharedExport);REGISTER(ReleaseSharedExport);REGISTER(PollSharedCommit);REGISTER(GetSharedManifest);REGISTER(SharedFileMatches);REGISTER(ReadSharedChunk);REGISTER(BeginSharedFile);REGISTER(AppendSharedChunk);REGISTER(CommitSharedFile);REGISTER(CancelSharedFile);REGISTER(ForgetAssets);REGISTER(GetCapabilities);REGISTER(Browse);REGISTER(BeginImport);REGISTER(Reload);REGISTER(PollJob);REGISTER(CancelJob);REGISTER(RequestAsset);REGISTER(AssetInfo);REGISTER(InspectBoneMap);REGISTER(PreviewCarrierFit);REGISTER(CreateInstance);REGISTER(DestroyInstance);REGISTER(GetDiagnostics);REGISTER(SetMaterialVisibility);REGISTER(SetSecondaryCollisionMode);REGISTER(SetSecondaryCollisionFlags);REGISTER(GetSecondaryCapabilities);REGISTER(GetSecondaryBackend);REGISTER(SetSecondaryBackend);REGISTER(SetVulkanSolverOrdering);REGISTER(Step);REGISTER(ResetPhysics);REGISTER(SetFrozen);REGISTER(SetMorph);REGISTER(SetBonePose);REGISTER(SetMirror);REGISTER(RemoveMirror);REGISTER(TakeImpulses);REGISTER(Raycast);REGISTER(BeginPhysgun);REGISTER(UpdatePhysgun);REGISTER(GetBoneTransform);REGISTER(GetMorphWeights);REGISTER(BeginGrab);REGISTER(UpdateGrab);REGISTER(EndGrab);REGISTER(Clear);
        REGISTER(RebindSourceEntity);REGISTER(GetBounds);REGISTER(GetBoundsValues);REGISTER(GetState);REGISTER(SetState);REGISTER(SubmitSourcePose);REGISTER(StepSources);REGISTER(SetMorphs);REGISTER(GetMaterialState);
#ifdef MMDHL_SERVER
        REGISTER(ReadAnimationModel);REGISTER(ProbePhysics);REGISTER(ProbeCarrierCollisions);REGISTER(CapturePhysics);REGISTER(CaptureSecondaryScene);REGISTER(SceneInterest);REGISTER(PrepareCarrier);REGISTER(RequestCarrierFit);
#endif
#ifndef MMDHL_SERVER
        REGISTER(SetSecondaryTuning);REGISTER(SetSpringRelativeDamping);
        REGISTER(GetModelAnimationDiagnostics);REGISTER(DeleteAssets);REGISTER(PropReload);REGISTER(PropDerive);REGISTER(SubmitPresentationPose);REGISTER(SubmitPresentationBatch);REGISTER(SubmitPresentationMatrixBatch);REGISTER(GetAlignmentProbe);REGISTER(Draw);REGISTER(RenderStatus);REGISTER(CheckRenderer);REGISTER(RenderStats);REGISTER(GetLightingState);REGISTER(SetFlashlightOverlapGuard);REGISTER(RenderFrameStats);REGISTER(SetNativeVertexCache);REGISTER(SetCompactVertices);REGISTER(SetGpuSkinning);REGISTER(SetPoseSmoothing);REGISTER(GetDrawAge);REGISTER(SetupSourceLighting);REGISTER(RegisterSourceShadow);REGISTER(RemoveSourceShadow);REGISTER(SetWorkers);REGISTER(GetWorkerCapabilities);REGISTER(SetSecondaryQuality);REGISTER(SetSecondaryWaitBudget);REGISTER(SetSecondaryMidphase);REGISTER(SetSecondarySleep);REGISTER(SetRenderSuspended);REGISTER(SetInstanceMaterials);REGISTER(DrawInstance);REGISTER(SetSecondaryBroadphase);REGISTER(PrepareFrame);REGISTER(PruneRenderCache);REGISTER(StartMainThreadSampling);REGISTER(ReadMainThreadSamples);REGISTER(CreateEditorPreview);REGISTER(DestroyEditorPreview);REGISTER(GetEditorPreviewBounds);REGISTER(DrawEditorPreview);REGISTER(GetMaterialMesh);REGISTER(GetMaterialPositions);REGISTER(CreatePreview);REGISTER(ClearPreview);REGISTER(DrawPreview);
#endif
        registerPropFunctions(LUA,context->cache);
        LUA->Push(-1);LUA->SetField(GarrysMod::Lua::INDEX_GLOBAL,"mmdhl_native");return 1;
    }catch(const std::exception& e){LUA->ThrowError(e.what());return 0;}
}
GMOD_MODULE_CLOSE(){
shutdownProps();
#ifndef MMDHL_SERVER
// Queued calls run this module's code on Source's render thread: none may stay
// queued once the shadow hooks, the buffers and the module go away.
drainRenderQueue();shutdownSourceShadows();pruneRenderCache(true);closeRenderQueue();
#endif
if(context){
#ifdef MMDHL_SERVER
 // The bridge's mirrors live in this realm's world: clear them there, before it
 // goes away (an unscoped world() would create a fresh singleton after shutdown).
 {WorldScope realm(context->runtime.get());clearPhysicsBridge();}
#endif
 for(auto& [id,job]:context->packages)job.progress->cancel=true;for(auto& [id,j]:context->jobs){if(j.group){TerminateJobObject(j.group,1);CloseHandle(j.group);}if(j.process)CloseHandle(j.process);}context.reset();releaseRuntimeRealm();}
return 0;}
