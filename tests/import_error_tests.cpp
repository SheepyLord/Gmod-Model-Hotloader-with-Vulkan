// Import failures explain why: codes, messages and places for damaged PMX files (made
// here), VRM files and spring bones; library exceptions through describeException;
// the format sniffer; file errors; worker exit codes; and the worker's status.json
// and crash log (argv[1] = mmdhl_worker.exe). No GPU, game, network or user models.
#include "runtime.hpp"
#include "import_error.hpp"
#include "release.hpp"
#include "rig.hpp"
#include "spring_bones.hpp"
#include "vrm.hpp"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <iostream>
using namespace mmd;
namespace {
int checks=0;
void check(bool ok,const std::string& name){if(!ok)throw std::runtime_error("FAIL "+name);++checks;std::cout<<"PASS "<<name<<"\n";}
bool has(const std::string& text,const std::string& part){return text.find(part)!=std::string::npos;}
// The ImportError a call throws; anything else fails the test. Like the worker, it
// describes what it caught, which ends the scopes' record of that exception.
ImportError failure(const std::function<void()>& f,const std::string& name){
 try{f();}catch(const ImportError& e){describeException(std::current_exception());std::cout<<"  "<<e.code<<": "<<e.what()<<"\n";return e;}catch(const std::exception& e){throw std::runtime_error("FAIL "+name+": not an ImportError: "+e.what());}
 throw std::runtime_error("FAIL "+name+": no error");
}
Json describe(const std::function<void()>& f){try{f();}catch(...){auto d=describeException(std::current_exception());std::cout<<"  "<<d.dump()<<"\n";return d;}return Json();}
bool placed(const Json& where,const std::string& kind,int64_t index,const std::string& name={}){
 for(auto& p:where)if(p.value("kind","")==kind&&p.value("index",int64_t(-1))==index&&p.value("name","")==name)return true;return false;
}

// ---- a PMX 2.0 file written field by field, with the damage a test asks for ----
struct PmxSpec {
 size_t bones=3,vertices=60;std::vector<std::pair<std::string,int>> materials={{"スカート",-1},{"Body",-1}}; // -1: the real index count
 int vertexType=-1;size_t badVertex=0;int boneParent=-2;size_t parentOf=2;size_t morphs=0,badMorph=size_t(-1);
};
Bytes pmx(const PmxSpec& s){
 Bytes b;auto u8=[&](uint8_t v){b.push_back(v);};auto i32=[&](int32_t v){auto p=reinterpret_cast<const unsigned char*>(&v);b.insert(b.end(),p,p+4);};
 auto f32=[&](float v){auto p=reinterpret_cast<const unsigned char*>(&v);b.insert(b.end(),p,p+4);};auto text=[&](const std::string& t){i32(int32_t(t.size()));b.insert(b.end(),t.begin(),t.end());};
 b.insert(b.end(),{'P','M','X',' '});f32(2.f);u8(8);for(uint8_t v:{1,0,4,4,4,4,4,4})u8(v);
 text("Fixture");text("Fixture");text("import_errors");text("");
 i32(int32_t(s.vertices));
 for(size_t i=0;i<s.vertices;i++){for(float v:{float(i%10),float(i/10),0.f,0.f,0.f,-1.f,0.f,0.f})f32(v);u8(uint8_t(i==s.badVertex&&s.vertexType>=0?s.vertexType:0));i32(int32_t(i%s.bones));f32(1);}
 size_t triangles=s.vertices/3;i32(int32_t(triangles*3));for(size_t i=0;i<triangles*3;i++)i32(int32_t(i));
 i32(0); // textures
 i32(int32_t(s.materials.size()));size_t first=0;
 for(size_t m=0;m<s.materials.size();m++){auto& [name,count]=s.materials[m];size_t real=m+1<s.materials.size()?(triangles/2)*3:triangles*3-first;
  text(name);text(name);for(float v:{.8f,.8f,.8f,1.f,0.f,0.f,0.f,5.f,.4f,.4f,.4f})f32(v);u8(0x0E);for(float v:{0.f,0.f,0.f,1.f,1.f})f32(v);i32(-1);i32(-1);u8(0);u8(0);i32(-1);text("");
  i32(count>=0?count:int32_t(real));first+=real;}
 i32(int32_t(s.bones));
 for(size_t i=0;i<s.bones;i++){std::string name=i==0?"センター":"bone"+std::to_string(i);
  text(name);text("bone"+std::to_string(i));f32(0);f32(float(i));f32(0);i32(i==s.parentOf&&s.boneParent>=-1?s.boneParent:int32_t(i)-1);i32(0);u8(0x1A);u8(0);f32(0);f32(1);f32(0);}
 // Empty vertex morphs; a category outside 0-4 is damage nanoem reports for that morph.
 i32(int32_t(s.morphs));for(size_t i=0;i<s.morphs;i++){text(i==0?"まばたき":"morph"+std::to_string(i));text("");u8(i==s.badMorph?9:2);u8(1);i32(0);}
 i32(0);i32(0);i32(0); // display frames, rigid bodies, joints
 return b;
}

// ---- a VRM 1.0 avatar: fifteen humanoid nodes and one triangle ----
Bytes vrm(const std::function<void(Json&)>& edit={}){
 Json j;Bytes bin;
 auto view=[&](const void* p,size_t n){while(bin.size()%4)bin.push_back(0);size_t at=bin.size();bin.insert(bin.end(),static_cast<const unsigned char*>(p),static_cast<const unsigned char*>(p)+n);
  j["bufferViews"].push_back({{"buffer",0},{"byteOffset",at},{"byteLength",n}});return int(j["bufferViews"].size()-1);};
 j["asset"]={{"version","2.0"}};
 const char* human[]={"hips","spine","head","leftUpperArm","leftLowerArm","leftHand","rightUpperArm","rightLowerArm","rightHand","leftUpperLeg","leftLowerLeg","leftFoot","rightUpperLeg","rightLowerLeg","rightFoot"};
 Json bones=Json::object();
 for(int i=0;i<15;i++){j["nodes"].push_back({{"name",human[i]},{"translation",{.1f*i,1.f,0.f}}});bones[human[i]]={{"node",i}};}
 for(int i=1;i<15;i++)j["nodes"][0]["children"].push_back(i);
 j["nodes"].push_back({{"name","BodyNode"},{"mesh",0}});j["scenes"]=Json::array({{{"nodes",{0,15}}}});j["scene"]=0;
 float position[]={0,0,0,1,0,0,0,1,0};uint32_t index[]={0,1,2};
 j["accessors"].push_back({{"bufferView",view(position,sizeof position)},{"componentType",5126},{"count",3},{"type","VEC3"}});
 j["accessors"].push_back({{"bufferView",view(index,sizeof index)},{"componentType",5125},{"count",3},{"type","SCALAR"}});
 j["meshes"]=Json::array({{{"name","Body"},{"primitives",Json::array({{{"attributes",{{"POSITION",0}}},{"indices",1}}})}}});
 j["extensionsUsed"]={"VRMC_vrm"};j["extensions"]["VRMC_vrm"]={{"specVersion","1.0"},{"meta",{{"name","Tiny"}}},{"humanoid",{{"humanBones",bones}}}};
 if(edit)edit(j);
 while(bin.size()%4)bin.push_back(0);j["buffers"]=Json::array({{{"byteLength",bin.size()}}});auto text=j.dump();while(text.size()%4)text.push_back(' ');
 Bytes out;auto u32=[&](uint32_t v){for(int k=0;k<4;k++)out.push_back(uint8_t(v>>(8*k)));};
 out.insert(out.end(),{'g','l','T','F'});u32(2);u32(uint32_t(12+8+text.size()+8+bin.size()));
 u32(uint32_t(text.size()));u32(0x4E4F534Au);out.insert(out.end(),text.begin(),text.end());u32(uint32_t(bin.size()));u32(0x004E4942u);out.insert(out.end(),bin.begin(),bin.end());return out;
}
Bytes bytes(std::string_view s){return Bytes(s.begin(),s.end());}

// ---- the worker ----
struct Run {DWORD exit=0;Json status;std::string log;};
DWORD run(const fs::path& worker,const std::wstring& arguments){
 std::wstring command=L"\""+worker.wstring()+L"\" "+arguments;STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};
 if(!CreateProcessW(worker.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process))throw std::runtime_error("cannot start the worker");
 WaitForSingleObject(process.hProcess,120000);DWORD code=0;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);return code;
}
Run request(const fs::path& worker,const fs::path& dir,const fs::path& source,const Json& options=Json::object()){
 fs::create_directories(dir);writeJson(dir/L"request.json",{{"source",utf8(source.wstring())},{"cache",utf8((dir.parent_path()/L"cache").wstring())},{"options",options}});
 Run r;r.exit=run(worker,L"--request \""+(dir/L"request.json").wstring()+L"\"");r.status=readJson(dir/L"status.json");std::cout<<"  "<<r.status.dump()<<"\n";return r;
}
}

int main(int argc,char** argv){try{
 SetConsoleOutputCP(CP_UTF8);
 auto temp=fs::temp_directory_path()/(L"mmdhl-import-errors-"+std::to_wstring(GetCurrentProcessId()));fs::remove_all(temp);fs::create_directories(temp);

 // ---- PMX: nanoem's failures name the section, the element, the last good one and the byte ----
 {auto raw=readFile("tests/fixtures/truncated.pmx");auto e=failure([&]{parse(raw);},"truncated PMX");
  check((e.code=="pmx.truncated"||e.code=="pmx.section_corrupt")&&e.details.value("offset",size_t(1)<<40)<=e.details.value("size",size_t(0))&&e.details.value("size",size_t(0))==raw.size(),"a truncated PMX says where reading stopped (offset within the file)");
  check(has(e.what(),"PMX")&&has(e.what(),"Invalid")&&has(e.what(),"byte "),"the truncated PMX message keeps the words PMX and Invalid (scripts/test-import-feedback.py)");}
 {PmxSpec s;s.vertices=120;s.vertexType=9;s.badVertex=5;auto e=failure([&]{parse(pmx(s));},"damaged vertex");
  check(e.code=="pmx.section_corrupt"&&e.details.value("section","")=="vertex"&&placed(e.details["where"],"vertex",5)&&has(e.what(),"vertex 5 is damaged"),"a damaged vertex in the middle of the file is named, not called truncated");}
 {PmxSpec s;s.morphs=150;s.badMorph=1;auto e=failure([&]{parse(pmx(s));},"damaged morph");
  check(e.code=="pmx.section_corrupt"&&placed(e.details["where"],"morph",1)&&e.details["after"].value("name","")=="まばたき"&&has(e.what(),"morph 1 (the one after “まばたき”) is damaged"),"a damaged morph is named with the intact morph before it");
  s.morphs=3;e=failure([&]{auto raw=pmx(s);raw.resize(raw.size()-40);parse(raw);},"cut morphs");check(e.code=="pmx.truncated"&&has(e.what(),"it ends early"),"a file cut off near its end is truncated");}
 {PmxSpec s;s.materials={{"スカート",3000},{"Body",-1}};auto e=failure([&]{parse(pmx(s));},"material range");
  check(e.code=="pmx.materials"&&placed(e.details["where"],"material",0,"スカート")&&has(e.what(),"material 0 “スカート”")&&has(e.what(),"2,999")&&has(e.what(),"past the 60"),"a material range past the index count names the material and the counts");}
 {PmxSpec s;s.materials={{"スカート",4},{"Body",-1}};auto e=failure([&]{parse(pmx(s));},"material count");
  check(e.code=="pmx.materials"&&has(e.what(),"“スカート” uses 4 triangle indices, which is not a multiple of 3"),"a material whose index count is not a multiple of 3 is named");}
 {PmxSpec s;s.materials={{"スカート",0},{"Body",-1}};auto e=failure([&]{parse(pmx(s));},"material coverage");
  check(e.code=="pmx.materials"&&has(e.what(),"cover 30 of the model's 60")&&e.details.value("covered",0)==30,"materials that leave triangles uncovered say how many they cover");}
 {PmxSpec s;s.boneParent=999;s.parentOf=2;auto clean=parse(pmx(PmxSpec{}));auto m=parse(pmx(s));
  check(m->bones[2].parent==-1&&m->warnings==clean->warnings,"a bone parent outside the model loads as a root bone, as before (no new note changes asset identities)");}
 {auto m=parse(readFile("tests/fixtures/corrupt-weights.pmx"));bool repaired=false;for(auto& w:m->warnings)repaired|=w.starts_with("Repaired ");
  check(repaired,"bad skin weights are repaired with a note, not a failure");}
 {auto e=failure([&]{parse(readFile("tests/fixtures/corrupt-soft.pmx"));},"corrupt soft body");
  check(e.code=="pmx.number"&&placed(e.details["where"],"soft_body",0,"soft fabric")&&e.details.value("field","")=="velocity correction factor"&&e.details.value("value","")=="NaN"
   &&has(e.what(),"soft body 0 “soft fabric”")&&has(e.what(),"velocity correction factor is NaN"),"a corrupt soft body names itself and the field");}
 {auto e=failure([&]{parse(readFile("tests/fixtures/corrupt-soft_iterations.pmx"));},"soft body iterations");
  check(e.code=="pmx.number"&&e.details.value("field","")=="position solver iterations"&&has(e.what(),"1000000000"),"an out-of-range iteration count names its field and value");}

 // ---- a model the fitter cannot rig imports; its fit result says why, outside the manifest ----
 {auto file=temp/L"box.pmx";writeAtomic(file,pmx(PmxSpec{}));auto progress=temp/L"progress.json";auto result=importAsset(file,temp/L"cache",Json::object(),progress);auto& fit=result["fit"];
  check(result["state"]=="complete"&&fit.value("ok",true)==false&&fit.value("errorCode","").starts_with("fit.")&&fit["errorDetails"].is_object()&&fit["errorDetails"]["missing"]==fit.value("missing",Json::array())&&!result["info"].contains("fit"),
   "a model without a ragdoll skeleton imports with the fit's failure, code and details beside the manifest");
  auto last=readJson(progress);check(last["stageCode"]=="fit"&&last["state"]=="running","importAsset reports each step with its code (the last is the fit)");}

 // ---- files that are not PMX, PMD or VRM say what they are ----
 struct Sniff{std::string head,id,family;};
 for(auto& t:std::vector<Sniff>{{std::string("PK\x03\x04",4)+"zip","zip","archive"},{"Rar!\x1A\x07\x01","rar","archive"},{"Vocaloid Motion Data 0002","vmd","motion"},{"\x89PNG\r\n\x1A\n","png","image"},
   {std::string("Kaydara FBX Binary  \0",21),"fbx","convertible"},{"; FBX 7.4.0 project file","fbx","convertible"},{"# Blender\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n","obj","static"},
   {"{\"asset\":{\"version\":\"2.0\"}}","gltf","convertible"},{"<?xml version=\"1.0\"?><COLLADA>","dae","convertible"},{"Readme: thanks for downloading","text","other"},{std::string("\x01\x02\x03\x04\xFE",5),"unknown","unknown"}}){
  auto f=sniffFormat(bytes(t.head));check(f.id==t.id&&f.family==t.family,"sniffed "+t.id+" ("+f.name+")");}
 {auto e=failure([&]{notCharacterFile(bytes(std::string("PK\x03\x04",4)+"x"),L"model.pmx");},"zip");
  check(e.code=="format.archive"&&has(e.what(),"ZIP archive")&&has(e.what(),"Extract it first"),"an archive picked as a model is told to be extracted");
  e=failure([&]{notCharacterFile(bytes("Kaydara FBX Binary  "),L"hero.pmx");},"renamed fbx");
  check(e.code=="format.renamed"&&e.details.value("extension","")==".fbx"&&has(e.what(),"ends in .pmx")&&has(e.what(),"Rename it to end in .fbx"),"an FBX file named .pmx is told to be renamed");
  e=failure([&]{notCharacterFile(bytes("v 0 0 0\nv 1 0 0\nf 1 2 3\n"),L"box.obj");},"obj");
  check(e.code=="character.format"&&has(e.what(),"belong in Static Props"),"a static model keeps the words the older addon's hint looks for");
  e=failure([&]{notCharacterFile({},L"empty.pmx");},"empty");check(e.code=="io.empty","an empty file says it is empty");
  e=failure([&]{parse(bytes("Vocaloid Motion Data 0002 and more bytes"));},"vmd parse");check(e.code=="format.motion","the parser says what a non-model file is too");}

 // ---- VRM: places from the mesh and primitive scopes; JSON errors become vrm.json ----
 {auto converted=convertVrm(vrm(),"Tiny");auto m=parse(converted.pmx);check(m->bones.size()==15,"the generated VRM converts");}
 {auto d=describe([&]{convertVrm(vrm([](Json& j){j["bufferViews"][0].erase("byteLength");}),"Tiny");});
  auto text=d.value("error","");auto context=d["context"].dump();
  check(d["errorCode"]=="vrm.json"&&!text.starts_with("[json.exception")&&has(text,"The VRM file's data is malformed or incomplete")&&has(text,"byteLength")&&has(text,"(in buffer view 0)"),"a VRM without byteLength reports vrm.json in a sentence, not nlohmann's text");
  check(has(context,"Converting VRM avatar")&&has(context,"mesh 0 “Body”")&&placed(d["errorDetails"]["where"],"mesh",0,"Body")&&placed(d["errorDetails"]["where"],"primitive",0)&&placed(d["errorDetails"]["where"],"buffer_view",0),"the VRM JSON error keeps its place: mesh, primitive, accessor, buffer view");}
 {auto e=failure([&]{convertVrm(vrm([](Json& j){j["meshes"][0]["primitives"][0]["attributes"]["POSITION"]=7;}),"Tiny");},"vrm accessor");
  check(e.code=="vrm.data"&&placed(e.details["where"],"mesh",0,"Body")&&placed(e.details["where"],"primitive",0)&&placed(e.details["where"],"accessor",7)&&has(e.what(),"accessor 7 does not exist (the file has 2)"),"a VRM accessor reference out of range names the mesh, primitive and accessor");}
 {auto e=failure([&]{convertVrm(vrm([](Json& j){j["extensions"]["VRMC_vrm"]["humanoid"]["humanBones"].erase("leftFoot");}),"Tiny");},"vrm humanoid");
  check(e.code=="vrm.humanoid"&&e.details.value("bone","")=="leftFoot"&&placed(e.details["where"],"humanoid_bone",-1,"leftFoot"),"a missing humanoid bone is named");}
 {auto e=failure([&]{convertVrm(vrm([](Json& j){j["accessors"][1]["count"]=4;}),"Tiny");},"vrm index");
  check(e.code=="vrm.truncated"&&e.details.contains("viewSize")&&!e.details.contains("offset"),"a VRM index accessor past its data is vrm.truncated; its position inside the view is not a file offset");}
 // A damaged .vrm is called damaged, never a GLB to rename: cut off, broken JSON, no VRM extension.
 {auto cut=temp/L"cut.vrm";auto data=vrm();data.resize(40);writeAtomic(cut,data);
  auto e=failure([&]{importAsset(cut,temp/L"cache",Json::object(),temp/L"cut.json");},"cut vrm");
  check(e.code=="vrm.truncated"&&e.details.value("offset",0)==20&&readJson(temp/L"cut.json")["stageCode"]=="convert_vrm","a .vrm cut off inside its JSON chunk is vrm.truncated while converting");
  auto stub=temp/L"stub.vrm";writeAtomic(stub,bytes("glTF\x02"));e=failure([&]{importAsset(stub,temp/L"cache",Json::object(),{});},"stub vrm");
  check(e.code=="vrm.truncated","a .vrm that ends inside its GLB header is vrm.truncated");
  auto broken=temp/L"broken-json.vrm";data=vrm();data[20]='#';writeAtomic(broken,data);
  auto d=describe([&]{importAsset(broken,temp/L"cache",Json::object(),{});});check(d["errorCode"]=="vrm.json"&&has(d["error"],"The VRM file's data is malformed"),"a .vrm whose JSON does not parse is vrm.json");
  auto plain=temp/L"plain.vrm";writeAtomic(plain,vrm([](Json& j){j["extensions"].erase("VRMC_vrm");j.erase("extensionsUsed");}));
  e=failure([&]{importAsset(plain,temp/L"cache",Json::object(),{});},"no vrm extension");check(e.code=="vrm.container"&&has(e.what(),"no VRM extension"),"a glTF binary named .vrm without the VRM extension says so");
  e=failure([&]{notCharacterFile(vrm(),L"avatar.pmx");},"renamed glb");check(e.code=="format.renamed"&&e.details.value("extension","")==".glb","a GLB named .pmx is still told to be renamed to .glb");}

 // ---- spring bones name the spring joint and the value ----
 {auto model=parse(pmx(PmxSpec{}));Json spring={{"springBone",{{"colliders",Json::array()},{"colliderGroups",Json::array()},{"springs",Json::array({{{"name","Hair"}}})},
   {"joints",Json::array({{{"spring",0},{"bone",1},{"tailOffset",{0,1,0}},{"stiffness",1e9}}})}}}};
  auto e=failure([&]{SpringSetup::fromManifest(spring,*model);},"spring stiffness");
  check(e.code=="spring.data"&&e.details.value("field","")=="stiffness"&&placed(e.details["where"],"spring_joint",0)&&has(e.what(),"Invalid spring bone stiffness"),"a spring stiffness out of range names the joint and the value");
  spring["springBone"]["joints"][0]["stiffness"]=1;spring["springBone"]["joints"][0]["bone"]=99;
  e=failure([&]{SpringSetup::fromManifest(spring,*model);},"spring bone");check(e.code=="spring.data"&&has(e.what(),"bone 99")&&has(e.what(),"it has 3"),"a spring joint on a bone outside the model gives both numbers");}

 // ---- library exceptions read as sentences with codes ----
 {auto d=describe([]{throw fs::filesystem_error("open",fs::path(L"C:/missing/a.pmx"),std::error_code(ERROR_FILE_NOT_FOUND,std::system_category()));});
  check(d["errorCode"]=="io.missing"&&has(d["error"],"a.pmx")&&has(d["error"],"does not exist")&&d["errorDetails"]["systemError"]==ERROR_FILE_NOT_FOUND,"a filesystem error says the file is missing");
  d=describe([]{throw fs::filesystem_error("write",fs::path(L"C:/cache/x"),std::error_code(ERROR_DISK_FULL,std::system_category()));});check(d["errorCode"]=="io.disk_full"&&has(d["error"],"disk is full"),"a full disk is io.disk_full");
  d=describe([]{throw std::bad_alloc();});check(d["errorCode"]=="memory"&&d["exceptionType"]=="memory","bad_alloc is a memory error");
  d=describe([]{throw std::runtime_error("plain text");});check(d["errorCode"]=="unknown"&&d["error"]=="plain text"&&d["context"].empty(),"other exceptions keep their text");
  d=describe([]{(void)Json::parse("{");});check(d["errorCode"]=="json"&&!d.value("error","").starts_with("[json"),"a JSON error outside a file scope is json");
  d=describe([]{ImportScope a("Reading","vrm");ImportScope b("spring",2,"Hair");(void)Json::object().at("x");});
  check(d["errorCode"]=="vrm.json"&&placed(d["errorDetails"]["where"],"spring",2,"Hair")&&d["context"].size()==2&&d["context"][1]=="spring 2 “Hair”","the scopes an exception unwinds through give its domain and place");
  {ImportScope outer("Reading","vrm");try{ImportScope inner("mesh",9,"Old");(void)Json::object().at("y");}catch(const Json::exception&){}}
  d=describe([]{throw std::runtime_error("later");});check(d["context"].empty()&&!d["errorDetails"].contains("where"),"an exception handled inside the scopes leaves no trail for a later one");
  d=describe([]{throw 42;});check(d["errorCode"]=="unknown"&&d["error"]=="Unknown error","a non-standard throw is still reported");
  auto e=failure([]{ImportScope a("mesh",1,"A");importFail("x.y","message",{{"where",Json::array({place("accessor",2)})}});},"merge");
  check(e.details["where"].size()==2&&placed(e.details["where"],"mesh",1,"A")&&placed(e.details["where"],"accessor",2)&&e.context.size()==1,"an ImportError's places are its scopes plus its own");
  check(placeText(place("rigid_body",3,std::string(80,'x')))=="rigid body 3 “"+std::string(48,'x')+"…”"&&place("bone",1,"\xFF\xFE")["name"]=="\xEF\xBF\xBD\xEF\xBF\xBD","places shorten long names and keep valid UTF-8");}

 // ---- file errors say why: in the code and the report, while what() keeps 2.2's sentence ----
 {auto missing=temp/L"nothing here.pmx";auto e=failure([&]{readFile(missing);},"missing file");
  check(e.code=="io.missing"&&e.details.value("path","")==utf8(missing.wstring())&&std::string(e.what())=="Cannot read "+utf8(missing.wstring()),"a missing file is io.missing with its path, and what() is 2.2's sentence");
  auto d=describe([&]{readFile(missing);});check(d["error"]=="Cannot read "+utf8(missing.wstring())+": the file does not exist (it may have been moved, renamed or deleted)","the report adds why the file cannot be read");
  e=failure([&]{readFile(temp);},"folder");d=describe([&]{readFile(temp);});check(e.code=="io.missing"&&has(d["error"],"folder"),"a folder picked as a file says so");
  auto locked=temp/L"locked.pmx";writeAtomic(locked,bytes("PMX "));
  HANDLE h=CreateFileW(locked.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  e=failure([&]{readFile(locked);},"locked file");d=describe([&]{readFile(locked);});CloseHandle(h);check(e.code=="io.locked"&&has(d["error"],"another program is using it"),"a file another program holds open is io.locked");
  // Writes go to the cache: a denied or failed write is io.write, never the model file's io.denied or io.missing.
  auto target=temp/L"read-only.png";writeAtomic(target,bytes("old"));SetFileAttributesW(target.c_str(),FILE_ATTRIBUTE_READONLY);
  e=failure([&]{writeAtomic(target,bytes("new"));},"read-only target");d=describe([&]{writeAtomic(target,bytes("new"));});SetFileAttributesW(target.c_str(),FILE_ATTRIBUTE_NORMAL);
  check(e.code=="io.write"&&std::string(e.what())=="Cannot commit output file: "+utf8(target.wstring())&&e.details.value("systemError",0)==ERROR_ACCESS_DENIED&&has(d["error"],"Windows denied access"),"a write Windows denies is io.write, with 2.2's sentence and the reason in the report");
  auto plain=temp/L"plain.txt";writeAtomic(plain,bytes("x"));d=describe([&]{fs::create_directories(plain/L"sub");});
  check(d["errorCode"]=="io.write"&&d["exceptionType"]=="filesystem"&&has(d["error"],"Cannot write "),"a folder that cannot be created is io.write (the library's message names the operation)");
  d=describe([&]{writeAtomic(plain/L"sub"/L"x.png",bytes("y"));});
  check(d["errorCode"]=="io.write"&&d["errorDetails"]["path"]==utf8((plain/L"sub").wstring())&&has(d["error"],"in the way"),"a cache file whose folder cannot be created is io.write, with the path as the player knows it");
  d=describe([]{throw fs::filesystem_error("create_directories",fs::path(L"C:/cache/x"),std::error_code(ERROR_ACCESS_DENIED,std::system_category()));});check(d["errorCode"]=="io.write","a denied filesystem write is io.write");}

 // ---- a missing texture keeps 2.2's warning text: warnings are part of the asset's identity ----
 {auto folder=temp/L"textured";fs::create_directories(folder);fs::copy_file("tests/fixtures/textured21.pmx",folder/L"model.pmx");
  auto result=importAsset(folder/L"model.pmx",temp/L"cache",Json::object(),{});auto expected="Cannot read "+utf8((folder/L"checker.dds").lexically_normal().wstring());size_t found=0;
  for(auto& w:result["info"]["warnings"]){auto text=w.get<std::string>();if(has(text,"checker.dds")){found++;check(text==expected,"missing texture warning is 2.2's: "+text);}}
  check(found>0,"the model without its texture imports with a warning");
  // A damaged cached texture names its file, so the player can delete it.
  fs::copy_file("tests/fixtures/checker.dds",folder/L"checker.dds");result=importAsset(folder/L"model.pmx",temp/L"cache2",Json::object(),{});auto id=result["asset"].get<std::string>();
  size_t textured=0;while(textured+1<result["info"]["textures"].size()&&result["info"]["textures"][textured].value("base","").empty())textured++;
  auto base=result["info"]["textures"][textured].value("base","");auto vtf=temp/L"cache2"/L"textures"/wide(base+".vtf");check(!base.empty()&&fs::is_regular_file(vtf),"the texture has its Source derivative");
  writeAtomic(vtf,bytes("not a vtf"));fs::remove(temp/L"cache2"/L"assets"/wide(id)/L"materials-v5.gma");
  auto e=failure([&]{prepareSourceMaterials(temp/L"cache2",id);},"damaged vtf");
  check(e.code=="texture.derivative"&&e.details.value("path","")==utf8(vtf.wstring())&&placed(e.details["where"],"material",int64_t(textured),result["info"]["materials"][textured].value("name","")),"a damaged cached texture names the material and the file's path");}

 // ---- worker exits ----
 {auto x=describeWorkerExit(0xC0000005);check(x["cause"]=="access_violation"&&x["errorCode"]=="worker.crash"&&has(x["error"],"exited before completion")&&has(x["error"],"0xC0000005"),"0xC0000005 is an access violation (and keeps the word exited)");
  check(describeWorkerExit(0xC00000FD)["cause"]=="stack_overflow"&&describeWorkerExit(0xC0000017)["errorCode"]=="memory"&&describeWorkerExit(3)["cause"]=="abort"&&describeWorkerExit(0xDEADBEEF)["cause"]=="unknown","other exit codes: stack overflow, out of memory, abort, unknown");
  Json running={{"state","running"},{"stage","Preparing textures"},{"stageCode","textures"},{"detail","Material 3 of 9 “Hair”: hair.png"},{"current",3},{"total",9},{"filename","x.pmx"}};
  auto r=finishedWorkerStatus(running,0xC0000005,"unhandled exception 0xC0000005 at mmdhl_runtime_win64.dll+0x1234 during stage textures\r\n","C:/models/x.pmx","");
  check(r["state"]=="failed"&&r["errorCode"]=="worker.crash"&&r["stageCode"]=="textures"&&r["detail"]==running["detail"]&&r["source"]=="C:/models/x.pmx"&&r["kind"]=="character"&&r["exitCode"]==0xC0000005u&&has(r["log"],"during stage textures")&&r["errorDetails"]["cause"]=="access_violation",
   "a crashed worker's result keeps the step, file and detail it reached, with the exit code and log");
  Json final={{"state","failed"},{"error","x"},{"errorCode","pmx.truncated"}};r=finishedWorkerStatus(final,1,"","C:/m.pmx","");
  check(r["errorCode"]=="pmx.truncated"&&r["exitCode"]==1&&!r.contains("log"),"a reported failure keeps its own error and gains the exit code");
  r=finishedWorkerStatus(Json(),0xC0000409,"","C:/m.pmx","static","bad json");check(r["errorCode"]=="worker.crash"&&r["kind"]=="static"&&r["errorDetails"]["statusError"]=="bad json","an unreadable status is a crash with the reason");
  Json complete={{"state","complete"},{"asset","a"}};check(finishedWorkerStatus(complete,0,"","","")==complete,"a completed import is unchanged");}

 // ---- the worker's status.json and crash log ----
 if(argc>=2){fs::path worker=argv[1];
  auto truncated=fs::absolute("tests/fixtures/truncated.pmx");auto r=request(worker,temp/L"job1",truncated);auto& s=r.status;
  check(r.exit==1&&s["state"]=="failed"&&s["errorCode"]=="pmx.truncated"&&s["stage"]=="Parsing skeleton, materials and physics"&&s["stageCode"]=="parse"&&s["context"].is_array()
   &&s["source"]==utf8(truncated.wstring())&&s["kind"]=="character"&&s["filename"]=="truncated.pmx"&&s["worker"]["release"]==MMDHL_RELEASE&&s.contains("elapsed_ms")&&s["errorDetails"].contains("where"),"worker: a truncated PMX fails with code, stage, stageCode, context, source, kind and build");
  auto zip=temp/L"model.pmx";writeAtomic(zip,bytes(std::string("PK\x03\x04",4)+std::string(64,'z')));r=request(worker,temp/L"job2",zip);
  check(r.status["errorCode"]=="format.archive"&&r.status["stageCode"]=="read","worker: an archive named .pmx fails while reading the file with format.archive");
  auto broken=temp/L"broken.vrm";writeAtomic(broken,vrm([](Json& j){j["bufferViews"][0].erase("byteLength");}));r=request(worker,temp/L"job3",broken);
  check(r.status["errorCode"]=="vrm.json"&&r.status["stageCode"]=="convert_vrm"&&has(r.status["context"].dump(),"Converting VRM avatar"),"worker: a broken VRM fails with vrm.json while converting");
  auto cut=temp/L"download.vrm";auto data=vrm();data.resize(40);writeAtomic(cut,data);r=request(worker,temp/L"job4",cut);
  check(r.status["errorCode"]=="vrm.truncated"&&r.status["stageCode"]=="convert_vrm"&&has(r.status["error"],"incomplete"),"worker: a .vrm cut off by an interrupted download is truncated, not a GLB to rename");
  auto crash=temp/L"crash";fs::create_directories(crash);
  auto code=run(worker,L"--crash-test \""+crash.wstring()+L"\" access");auto log=readFile(crash/L"worker.log");std::string text(log.begin(),log.end());
  check(code==0xC0000005&&has(text,"unhandled exception 0xC0000005")&&has(text,"during stage test"),"worker: an access violation ends with its exception code and one log line");
  code=run(worker,L"--crash-test \""+crash.wstring()+L"\" terminate");log=readFile(crash/L"worker.log");text.assign(log.begin(),log.end());
  check(code==3&&has(text,"std::terminate during stage test"),"worker: std::terminate exits with 3 and logs");}
 else std::cout<<"SKIP worker checks: pass the path of mmdhl_worker.exe\n";
 std::error_code ec;fs::remove_all(temp,ec);
 std::cout<<checks<<" import error checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
