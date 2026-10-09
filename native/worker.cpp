#include "installation.hpp"
#include "runtime.hpp"
#include "rig.hpp"
#include "spring_bones.hpp"
#include "vrm.hpp"
#include "character_import.hpp"
#include "humanoid_map.hpp"
#include "import_error.hpp"
#include "compute_solver.hpp"
#include "vulkan_solver.hpp"
#include "props/core.hpp"
#include <windows.h>
#include <shobjidl.h>
#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cwctype>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
namespace {
// A crash leaves no final status: the module reads the exit code, and one line per
// crash from <job>/worker.log (which exception, in which module, at which step). The
// handlers only format into the stack and append; the process then ends.
wchar_t crashLog[1024]{};
void crashLine(const char* line){
    if(!crashLog[0])return;HANDLE h=CreateFileW(crashLog,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return;DWORD written=0;WriteFile(h,line,DWORD(strlen(line)),&written,nullptr);CloseHandle(h);
}
LONG WINAPI crashed(EXCEPTION_POINTERS* e){
    char module[MAX_PATH]="unknown";HMODULE owner=nullptr;auto address=e->ExceptionRecord->ExceptionAddress;uintptr_t offset=reinterpret_cast<uintptr_t>(address);
    if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,static_cast<LPCSTR>(address),&owner)&&GetModuleFileNameA(owner,module,MAX_PATH))offset-=reinterpret_cast<uintptr_t>(owner);
    const char* name=module;for(const char* c=module;*c;c++)if(*c=='\\'||*c=='/')name=c+1;
    char line[512];snprintf(line,sizeof line,"unhandled exception 0x%08lX at %s+0x%llX during stage %s\r\n",e->ExceptionRecord->ExceptionCode,name,static_cast<unsigned long long>(offset),*mmd::importStage()?mmd::importStage():"start");
    crashLine(line);return EXCEPTION_EXECUTE_HANDLER; // ends the process with the exception code, without an error dialog
}
void aborted(int){char line[160];snprintf(line,sizeof line,"abort during stage %s\r\n",*mmd::importStage()?mmd::importStage():"start");crashLine(line);}
void watchCrashes(const mmd::fs::path& log){
    wcsncpy_s(crashLog,log.c_str(),_TRUNCATE);
    // No Windows error dialog may wait for a click in a hidden process, and abort() exits
    // with code 3 (not a fail-fast report) so the module can tell the two apart.
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    ULONG reserve=64*1024;SetThreadStackGuarantee(&reserve); // room to log a stack overflow
    SetUnhandledExceptionFilter(crashed);signal(SIGABRT,aborted);
    std::set_terminate([]{char line[160];snprintf(line,sizeof line,"std::terminate during stage %s\r\n",*mmd::importStage()?mmd::importStage():"start");crashLine(line);std::abort();});
}
// Characters for --inspect/--fit: PMX/PMD as-is, VRM and other formats through the
// same conversion as import (FBX/glTF/DAE with the automatic bone assignment).
std::shared_ptr<mmd::Model> loadCharacter(const mmd::fs::path& path){
    if(mmd::convertibleCharacter(path)&&!mmd::isVrmPath(path)){
        auto converted=mmd::convertCharacter(path,mmd::Json::object(),{});auto model=mmd::parse(converted.pmx);
        for(auto& w:converted.warnings)model->warnings.push_back(w);model->springs=mmd::SpringSetup::fromManifest(converted.conversion,*model);
        for(auto& [key,value]:converted.conversion["boneMap"].items())model->conversionBoneMap[key]=value.get<int>();return model;
    }
    auto raw=mmd::readFile(path);if(!mmd::isVrmData(raw))return mmd::parse(raw);
    auto converted=mmd::convertVrm(raw,mmd::utf8(path.stem().wstring()));auto model=mmd::parse(converted.pmx);
    for(auto& w:converted.warnings)model->warnings.push_back(w);model->springs=mmd::SpringSetup::fromManifest(converted.vrm,*model);return model;
}
// Static-prop imports report every stage; a heartbeat keeps the elapsed time
// moving through long single steps such as collision decomposition.
class PropProgress {
    mmd::fs::path path;std::string filename;std::mutex mutex;std::condition_variable wake;bool stopping=false;
    uint64_t started=GetTickCount64(),stageStarted=started;
    props::Json state={{"state","running"},{"stage","Starting importer"},{"progress",0}},timings=props::Json::array();
    std::thread heartbeat;
    void publish(){auto now=GetTickCount64();state["elapsed_ms"]=now-started;state["stage_elapsed_ms"]=now-stageStarted;state["filename"]=filename;props::writeJson(path,state);}
public:
    PropProgress(mmd::fs::path p,std::string name):path(std::move(p)),filename(std::move(name)){
        publish();heartbeat=std::thread([this]{std::unique_lock lock(mutex);while(!wake.wait_for(lock,std::chrono::milliseconds(500),[this]{return stopping;})){try{publish();}catch(...){}}});
    }
    ~PropProgress(){stop();}
    void stop(){{std::lock_guard lock(mutex);stopping=true;}wake.notify_all();if(heartbeat.joinable())heartbeat.join();}
    void update(const props::ProgressUpdate& p){
        std::lock_guard lock(mutex);auto now=GetTickCount64();
        if(state["stage"]!=p.stage){timings.push_back({{"stage",state["stage"]},{"milliseconds",now-stageStarted}});stageStarted=now;}
        state["stage"]=p.stage;state["progress"]=std::clamp(p.progress,0.f,1.f);state["detail"]=p.detail;state["current"]=p.current;state["total"]=p.total;state["indeterminate"]=p.indeterminate;publish();
    }
    props::Json complete(props::Json result){stop();auto now=GetTickCount64();timings.push_back({{"stage",state["stage"]},{"milliseconds",now-stageStarted}});result["elapsed_ms"]=now-started;result["stage_timings"]=timings;result["filename"]=filename;return result;}
};
// A PMX imported as a prop may really be a character. Humanoid landmark bones
// (Japanese or English names) and physics bodies decide; the UI then offers to
// import it under Character Models instead.
props::Json classifyPmx(const mmd::fs::path& source){
    static const std::vector<std::vector<std::string>> landmarks={{"頭","head"},{"首","neck"},{"上半身","upper body","upperbody","spine"},{"下半身","lower body","lowerbody","hips","pelvis"},
        {"左腕","arm_l","left arm","leftarm","l arm"},{"右腕","arm_r","right arm","rightarm","r arm"},{"左ひじ","elbow_l","left elbow","leftelbow"},{"右ひじ","elbow_r","right elbow","rightelbow"},
        {"左足","leg_l","left leg","leftleg","l leg"},{"右足","leg_r","right leg","rightleg","r leg"},{"左ひざ","knee_l","left knee","leftknee"},{"右ひざ","knee_r","right knee","rightknee"}};
    auto model=mmd::parse(mmd::readFile(source));size_t found=0;
    auto lower=[](std::string s){for(auto& c:s)c=char(std::tolower((unsigned char)c));return s;};
    for(auto& group:landmarks){bool hit=false;for(auto& b:model->bones){auto english=lower(b.english);for(auto& n:group)if(b.name==n||english==n){hit=true;break;}if(hit)break;}found+=hit;}
    bool character=found>=6&&model->bones.size()>=15;
    return {{"kind",character?"character":"static"},{"bones",model->bones.size()},{"rigidBodies",model->bodies.size()},{"joints",model->joints.size()},{"landmarks",found}};
}
props::Json importProp(const mmd::fs::path& source,const mmd::fs::path& cache,const props::Json& request,const mmd::fs::path& status){
    // A VRM file is a rigged humanoid avatar: it belongs in Character Models.
    if(mmd::isVrmPath(source))throw std::runtime_error("This is a VRM avatar. Import it under Character Models, where it becomes a posable ragdoll with its spring-bone physics.");
    auto options=props::parseOptions(request);auto root=cache/L"static";
    PropProgress progress(status,mmd::utf8(source.filename().wstring()));
    props::Progress report{[&](const props::ProgressUpdate& event){progress.update(event);}};
    auto asset=props::importModel(source,options,report);auto id=props::saveAsset(root,asset,report,options.limits);
    // Local-only registry for Reimport. It is never shared with a server.
    auto registryPath=root/L"sources.local.json";props::Json registry=props::Json::object();
    if(mmd::fs::exists(registryPath))try{registry=props::readJson(registryPath,8ull<<20);}catch(...){registry=props::Json::object();}
    registry[id]={{"source",mmd::utf8(mmd::fs::absolute(source).wstring())},{"options",request}};props::writeJson(registryPath,registry);
    props::Json info={{"name",asset.manifest.value("name",std::string("Imported prop"))},{"warnings",asset.manifest.value("warnings",props::Json::array())},{"triangles",asset.indices.size()/3},{"vertices",asset.vertices.size()},
        {"materials",asset.manifest.at("materials").size()},{"collision_hulls",asset.hulls.size()},{"collision_method",asset.manifest.value("collision_method",std::string("coacd"))},{"format",asset.manifest.value("format",std::string())},{"mins",asset.manifest.at("mins")},{"maxs",asset.manifest.at("maxs")}};
    if(asset.manifest.contains("skeleton"))info["skeleton"]=asset.manifest["skeleton"];
    if(info["format"]=="pmx")try{info["classification"]=classifyPmx(source);}catch(...){}
    return progress.complete({{"state","complete"},{"kind","static"},{"progress",1},{"stage","Import complete"},{"asset",id},{"info",info}});
}
// A .blend file can hold many objects; list its meshes so the player picks.
props::Json listBlend(const mmd::fs::path& source,const props::Json& request,const mmd::fs::path& status){
    auto options=props::parseOptions(request);
    PropProgress progress(status,mmd::utf8(source.filename().wstring()));
    props::Progress report{[&](const props::ProgressUpdate& event){progress.update(event);}};
    auto scene=props::listBlendObjects(source,options,report);
    return progress.complete({{"state","complete"},{"kind","blend_scene"},{"progress",1},{"stage","Choose objects"},{"scene",scene},{"options",request}});
}
}
// Part presets: a new content-addressed prop cut from an imported one by
// material and/or a region box (triangles whose centre lies inside), with its
// own collision. The parent bundle is never modified.
props::Json deriveProp(const mmd::fs::path& cache,const props::Json& request,const mmd::fs::path& status){
    using props::Json;using props::Vec;
    auto root=cache/L"static";auto parent=request.value("parent",std::string());auto presetName=request.value("name",std::string("Preset"));
    if(!props::validHash(parent))throw std::runtime_error("Invalid original prop");
    if(presetName.empty()||presetName.size()>120)throw std::runtime_error("Choose a preset name of up to 120 characters");
    PropProgress progress(status,presetName);
    props::Progress report{[&](const props::ProgressUpdate& event){progress.update(event);}};
    report("Reading the original prop",.05f);
    auto a=props::loadAsset(root,parent);
    std::set<size_t> hidden;for(auto& v:request.value("hidden",Json::array()))if(v.is_number_unsigned())hidden.insert(v.get<size_t>());
    bool useBox=request.contains("box")&&request["box"].is_object();Vec lo{},hi{};
    if(useBox){lo=request["box"].at("min").get<Vec>();hi=request["box"].at("max").get<Vec>();if(lo.x>hi.x||lo.y>hi.y||lo.z>hi.z)throw std::runtime_error("The region box is inverted");}
    report("Selecting parts",.2f);
    std::vector<uint32_t> kept;Json parts=Json::array();
    for(auto& part:a.manifest.at("parts")){
        size_t material=part.at("material");if(hidden.count(material))continue;
        uint32_t first=part.at("first"),count=part.at("count"),start=uint32_t(kept.size());
        for(uint32_t i=first;i<first+count;i+=3){
            auto i0=a.indices[i],i1=a.indices[i+1],i2=a.indices[i+2];
            if(useBox){Vec c=(a.vertices[i0].pos+a.vertices[i1].pos+a.vertices[i2].pos)*(1.f/3);if(c.x<lo.x||c.y<lo.y||c.z<lo.z||c.x>hi.x||c.y>hi.y||c.z>hi.z)continue;}
            kept.insert(kept.end(),{i0,i1,i2});
        }
        if(kept.size()>start){Json p=part;p["first"]=start;p["count"]=kept.size()-start;parts.push_back(p);}
    }
    if(kept.empty())throw std::runtime_error("The preset keeps no geometry. Show at least one material inside the region");
    props::Asset out;out.manifest=a.manifest;out.manifest["parts"]=parts;
    std::vector<uint32_t> remap(a.vertices.size(),UINT32_MAX);
    for(auto& i:kept){if(remap[i]==UINT32_MAX){remap[i]=uint32_t(out.vertices.size());out.vertices.push_back(a.vertices[i]);}i=remap[i];}
    out.indices=std::move(kept);
    // Hidden materials keep their slot (indices stay valid) but drop their images.
    std::set<size_t> used;for(auto& p:parts)used.insert(p.at("material").get<size_t>());
    auto& materials=out.manifest.at("materials");
    for(size_t m=0;m<materials.size();++m)if(!used.count(m)){materials[m]["base_texture"]="";materials[m]["normal_texture"]="";}
    std::set<std::string> refs;for(auto& m:materials)for(auto key:{"base_texture","normal_texture"}){auto h=m.value(key,std::string());if(!h.empty())refs.insert(h);}
    for(auto& t:a.textures)if(refs.count(t.hash))out.textures.push_back(t);
    Json hiddenList=Json::array();for(auto m:hidden)hiddenList.push_back(m);
    out.manifest["name"]=a.manifest.value("name",std::string("Prop"))+" · "+presetName;
    out.manifest["derived_from"]=parent;
    out.manifest["preset"]={{"name",presetName},{"hidden",hiddenList}};if(useBox)out.manifest["preset"]["box"]={{"min",lo},{"max",hi}};
    out.manifest["triangles"]=out.indices.size()/3;out.manifest["vertices"]=out.vertices.size();
    props::updateBounds(out);
    out.hulls.clear();props::makeCollision(out,request.value("collision",std::string("hull")),report);
    auto id=props::saveAsset(root,out,report,props::Limits{});
    Json info={{"name",out.manifest["name"]},{"warnings",out.manifest.value("warnings",Json::array())},{"triangles",out.indices.size()/3},{"vertices",out.vertices.size()},{"materials",materials.size()},
        {"collision_hulls",out.hulls.size()},{"collision_method",out.manifest.value("collision_method",std::string("coacd"))},{"mins",out.manifest["mins"]},{"maxs",out.manifest["maxs"]},{"derived_from",parent},{"preset",out.manifest["preset"]}};
    return progress.complete({{"state","complete"},{"kind","static"},{"progress",1},{"stage","Preset saved"},{"asset",id},{"info",info}});
}
int wmain(int argc,wchar_t** argv){
    using namespace mmd;fs::path status;std::string requestSource,requestKind;const uint64_t started=GetTickCount64();
    try {
        if(argc==2&&std::wstring(argv[1])==L"--version"){std::cout<<Json({{"release",MMDHL_RELEASE},{"build",MMDHL_BUILD_ID},{"installApi",MMDHL_INSTALL_API},{"api",ApiVersion},{"platform","win64"}}).dump()<<std::endl;return 0;}
        if((argc==3||argc==4)&&std::wstring(argv[1])==L"--installation-test"){auto result=workerSelfTest(argc==4&&std::wstring(argv[3])==L"coacd");result["identity"]={{"release",MMDHL_RELEASE},{"build",MMDHL_BUILD_ID},{"installApi",MMDHL_INSTALL_API},{"api",ApiVersion},{"platform","win64"}};writeJson(argv[2],result);return 0;}
        if(argc==3&&std::wstring(argv[1])==L"--probe-compute"){auto result=openclCapabilities(false);writeJson(argv[2],result);return result.value("available",false)?0:1;}
        if(argc==3&&std::wstring(argv[1])==L"--probe-vulkan"){auto result=vulkanCapabilities(false);shutdownVulkan();writeJson(argv[2],result);return result.value("available",false)?0:1;}
        if(argc==3&&std::wstring(argv[1])==L"--inspect"){auto m=loadCharacter(argv[2]);std::cout<<m->info().dump()<<std::endl;return 0;}
        // Development aid: write the converted PMX, its textures and the VRM metadata.
        if(argc==4&&std::wstring(argv[1])==L"--convert-vrm"){auto converted=convertVrm(readFile(argv[2]),utf8(fs::path(argv[2]).stem().wstring()));fs::path out=argv[3];
            writeAtomic(out/L"model.pmx",converted.pmx);for(auto& [path,bytes]:converted.textures)writeAtomic(out/fs::path(wide(path)),bytes);
            auto info=converted.vrm;info["warnings"]=converted.warnings;info["textures"]=converted.textures.size();writeJson(out/L"vrm.json",info);std::cout<<converted.pmx.size()<<" bytes"<<std::endl;return 0;}
        if(argc==3&&std::wstring(argv[1])==L"--inspect-physics"){
            auto m=loadCharacter(argv[2]);Json result={{"bodies",Json::array()},{"joints",Json::array()}};
            auto xyz=[](const float* v){return Json::array({v[0],v[1],v[2]});};
            for(auto b:m->bodies)result["bodies"].push_back({{"name",m->text(nanoemModelRigidBodyGetName(b,NANOEM_LANGUAGE_TYPE_JAPANESE))},{"bone",boneIndex(nanoemModelRigidBodyGetBoneObject(b))},{"position",xyz(nanoemModelRigidBodyGetOrigin(b))},{"mass",nanoemModelRigidBodyGetMass(b)},{"mode",nanoemModelRigidBodyGetTransformType(b)}});
            for(size_t i=0;i<m->joints.size();i++){auto j=m->joints[i];auto r=m->jointReferences[i];result["joints"].push_back({{"name",m->text(nanoemModelJointGetName(j,NANOEM_LANGUAGE_TYPE_JAPANESE))},{"a",r.a},{"b",r.b},{"valid",r.valid},{"lower",xyz(nanoemModelJointGetLinearLowerLimit(j))},{"upper",xyz(nanoemModelJointGetLinearUpperLimit(j))},{"spring",xyz(nanoemModelJointGetLinearStiffness(j))}});}
            std::cout<<result.dump()<<std::endl;return 0;
        }
        // Development aids for characters in other formats: the bone window's probe,
        // a conversion written to a folder, and the bone map inspection of any character.
        if(argc==3&&std::wstring(argv[1])==L"--probe-character"){std::cout<<probeCharacter(argv[2],Json::object(),{}).dump()<<std::endl;return 0;}
        if((argc==4||argc==5)&&std::wstring(argv[1])==L"--convert-character"){auto converted=convertCharacter(argv[2],argc==5?readJson(argv[4]):Json::object(),{});fs::path out=argv[3];
            writeAtomic(out/L"model.pmx",converted.pmx);for(auto& [path,bytes]:converted.textures)writeAtomic(out/fs::path(wide(path)),bytes);
            auto info=converted.conversion;info["warnings"]=converted.warnings;info["textures"]=converted.textures.size();writeJson(out/L"conversion.json",info);std::cout<<converted.pmx.size()<<" bytes"<<std::endl;return 0;}
        if((argc==3||argc==4)&&std::wstring(argv[1])==L"--inspect-bone-map"){auto m=loadCharacter(argv[2]);std::cout<<inspectBoneMap(*m,argc==4?readJson(argv[3]):Json{{"include",{"skeleton"}}}).dump()<<std::endl;return 0;}
        if((argc==3||argc==4)&&(std::wstring(argv[1])==L"--fit"||std::wstring(argv[1])==L"--fit-raw")){auto m=loadCharacter(argv[2]);std::cout<<fitRig(*m,argc==4?Json::parse(utf8(argv[3])):Json{{"calibrated",std::wstring(argv[1])!=L"--fit-raw"}}).manifest.dump()<<std::endl;return 0;}
        if((argc==3||argc==4)&&std::wstring(argv[1])==L"--pick"){
            fs::path dir=argv[2];status=dir/L"status.json";bool prop=argc==4&&std::wstring(argv[3])==L"static";
            const wchar_t* title=prop?L"Import static prop":L"Import character";
            CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);IFileOpenDialog* dialog=nullptr;
            if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))throw std::runtime_error("Cannot open file picker");
            COMDLG_FILTERSPEC characters[]={{L"Character models (PMX, PMD, VRM, FBX, glTF, DAE)",L"*.pmx;*.pmd;*.vrm;*.fbx;*.glb;*.gltf;*.dae"},{L"All files",L"*.*"}};
            COMDLG_FILTERSPEC models[]={{L"3D models (OBJ, FBX, glTF, PMX, Blender)",L"*.obj;*.fbx;*.glb;*.gltf;*.pmx;*.blend"},{L"All files",L"*.*"}};
            if(prop)dialog->SetFileTypes(2,models);else dialog->SetFileTypes(2,characters);
            dialog->SetOptions(FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST);dialog->SetTitle(title);
            // The picker has no owner window (an owned dialog would disable the
            // game window and leave it disabled if this process is killed). A
            // full-screen game therefore covers it; this process was started by
            // the foreground game, so it may bring its own dialog to the front.
            std::atomic<bool> shown{false};
            std::thread raise([&]{for(int i=0;i<60&&!shown.load();i++){if(HWND h=FindWindowExW(nullptr,nullptr,L"#32770",title)){DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid==GetCurrentProcessId()){ShowWindow(h,SW_SHOW);SetForegroundWindow(h);return;}}Sleep(50);}});
            auto hr=dialog->Show(nullptr);shown=true;raise.join();
            Json result={{"state","cancelled"}};if(SUCCEEDED(hr)){IShellItem* item=nullptr;dialog->GetResult(&item);PWSTR p=nullptr;item->GetDisplayName(SIGDN_FILESYSPATH,&p);result={{"state","selected"},{"source",utf8(p)}};if(prop)result["kind"]="static";CoTaskMemFree(p);item->Release();}dialog->Release();CoUninitialize();writeJson(status,result);return 0;
        }
        // Test aid: the crash handlers on a deliberate crash (access, abort or terminate).
        if(argc==4&&std::wstring(argv[1])==L"--crash-test"){watchCrashes(fs::path(argv[2])/L"worker.log");setImportStage("test");std::wstring how=argv[3];
            if(how==L"abort")std::abort();if(how==L"terminate")std::terminate();RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return 0;}
        if(argc==3&&std::wstring(argv[1])==L"--request"){fs::path request=argv[2];status=request.parent_path()/L"status.json";watchCrashes(request.parent_path()/L"worker.log");auto j=readJson(request);auto options=j.value("options",Json::object());
            requestSource=j.value("source",std::string());requestKind=options.value("kind",std::string());
            auto source=fs::path(wide(j.at("source").get<std::string>())),cache=fs::path(wide(j.at("cache").get<std::string>()));
            auto kind=options.value("kind",std::string());
            auto filename=utf8(source.filename().wstring());
            auto report=[&](const char* code){return [&,code](const char* stage,float progress){setImportStage(code);writeJson(status,{{"state","running"},{"stage",stage},{"stageCode",code},{"progress",progress},{"filename",filename}});};};
            auto extension=source.extension().wstring();for(auto& c:extension)c=wchar_t(towlower(c));
            bool mmdFile=extension==L".pmx"||extension==L".pmd"||isVrmPath(source);
            Json result;
            // Characters in other formats: the probe reads the skeleton for the bone
            // window; the import converts with the player's assignment. PMX, PMD and VRM
            // (also a VRM saved as .glb) import as before.
            if(kind=="character_probe"){
                if(extension==L".obj")importFail("character.format","OBJ files have no skeleton. Characters must be PMX, PMD, VRM, FBX, glTF or DAE files.",{{"format","obj"}});
                if(extension==L".blend")importFail("character.blend","Blender files cannot be imported as characters. In Blender, export FBX or glTF with the armature and import that file.");
                if(mmdFile||!convertibleCharacter(source))result=importAsset(source,cache,Json::object(),status);
                else result={{"state","complete"},{"kind","bone_map"},{"progress",1},{"stage","Reading the skeleton"},{"filename",filename},{"probe",probeCharacter(source,options,report("probe"))}};
            }
            else if((kind=="character"||kind.empty())&&convertibleCharacter(source)&&!mmdFile)result=importConverted(source,cache,options,status,convertCharacter(source,options,report("convert_character")));
            else if(kind=="character")result=importAsset(source,cache,Json::object(),status);
            else result=kind=="derive"?deriveProp(cache,options,status):kind=="blend_scene"?listBlend(source,options,status):kind=="static"?importProp(source,cache,options,status):importAsset(source,cache,options,status);
            if(kind!="derive")result["source"]=requestSource;writeJson(status,result);return 0;}
        std::cerr<<"mmdhl_worker --inspect model.pmx | --request request.json | --pick job-directory [static]\n";return 2;
    }catch(...){
        // Any exception becomes a sentence with a code (import_error.hpp); a structured
        // failure names the part at fault, so the bone window can reopen on it.
        auto failure=describeException(std::current_exception());
        Json j={{"state","failed"},{"error",failure["error"]},{"errorCode",failure["errorCode"]},{"errorDetails",failure["errorDetails"]},{"context",failure["context"]},{"exceptionType",failure["exceptionType"]},
            {"elapsed_ms",GetTickCount64()-started},{"worker",{{"release",MMDHL_RELEASE},{"build",MMDHL_BUILD_ID}}}};
        if(!status.empty()){
            // The step that failed is the last one the import reported.
            try{auto last=readJson(status);for(auto key:{"stage","stageCode","filename","detail"})if(last.contains(key)&&last[key].is_string())j[key]=last[key];for(auto key:{"current","total"})if(last.contains(key)&&last[key].is_number())j[key]=last[key];}catch(...){}
            if(!j.contains("stageCode")&&*importStage())j["stageCode"]=importStage();
            if(!requestSource.empty()){j["source"]=requestSource;if(!j.contains("filename"))try{j["filename"]=utf8(fs::path(wide(requestSource)).filename().wstring());}catch(...){}}
            j["kind"]=requestKind.empty()?std::string("character"):requestKind;
            // Names come from model files: never let one invalid byte lose the report.
            try{auto text=j.dump(2,' ',false,Json::error_handler_t::replace);writeAtomic(status,std::span(reinterpret_cast<const unsigned char*>(text.data()),text.size()));}catch(...){}
        }
        std::cerr<<j.dump(-1,' ',false,Json::error_handler_t::replace)<<std::endl;return 1;}
}
