#include "import_error.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <new>
#include <system_error>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace mmd {
namespace {
struct Scope { std::string what; Json place; std::string domain; int unwinding; };
thread_local std::vector<Scope> scopes;
// Scopes left by an exception that is not an ImportError, innermost last, so the
// report still says where a library error (JSON, filesystem) happened.
thread_local std::vector<Scope> unwound;
std::vector<std::string> names(const std::vector<Scope>& list){std::vector<std::string> r;for(auto& s:list)r.push_back(s.what);return r;}
Json places(const std::vector<Scope>& list){Json r=Json::array();for(auto& s:list)if(!s.place.is_null())r.push_back(s.place);return r;}
bool samePlace(const Json& a,const Json& b){return a.value("kind","")==b.value("kind","")&&a.value("index",int64_t(-1))==b.value("index",int64_t(-1))&&a.value("name","")==b.value("name","");}
// Valid UTF-8 for JSON and messages: names come from model files.
std::string clean(std::string_view s,bool lines=false){
 std::string out;out.reserve(s.size());
 for(size_t i=0;i<s.size();){unsigned char c=s[i];size_t n=c<0x80?1:(c>>5)==6?2:(c>>4)==14?3:(c>>3)==30?4:0;bool ok=n&&i+n<=s.size();
  for(size_t k=1;ok&&k<n;k++)ok=(static_cast<unsigned char>(s[i+k])>>6)==2;
  if(ok&&(c>=0x20||(lines&&(c=='\n'||c=='\r'||c=='\t')))){out.append(s.substr(i,n));i+=n;}else{if(c>=0x20||!ok)out+="\xEF\xBF\xBD";else out+=' ';i+=ok?n:1;}}
 return out;
}
char stage[48]{};
}
ImportError::ImportError(std::string c,const std::string& message,Json d):std::runtime_error(message),code(std::move(c)),details(std::move(d)),context(names(scopes)){
    if(!details.is_object())details=Json::object();
    auto where=places(scopes);
    if(details.contains("where")&&details["where"].is_array())for(auto& p:details["where"])if(std::none_of(where.begin(),where.end(),[&](const Json& w){return samePlace(w,p);}))where.push_back(p);
    if(!where.empty())details["where"]=where;
}
void importFail(std::string code,const std::string& message,Json details){throw ImportError(std::move(code),message,std::move(details));}
Json place(std::string_view kind,int64_t index,std::string_view name){
 Json p={{"kind",std::string(kind)}};if(index>=0)p["index"]=index;if(!name.empty())p["name"]=clean(name);return p;
}
std::string placeText(const Json& p){
 std::string kind=p.value("kind",std::string());std::replace(kind.begin(),kind.end(),'_',' ');std::string text=kind;
 if(p.contains("index"))text+=" "+std::to_string(p["index"].get<int64_t>());
 auto name=p.value("name",std::string());
 if(name.size()>48){size_t cut=48;while(cut>0&&(static_cast<unsigned char>(name[cut])>>6)==2)cut--;name=name.substr(0,cut)+"…";}
 if(!name.empty())text+=" “"+name+"”";
 return text;
}
std::string cleanText(std::string_view s){return clean(s);}
std::string thousands(uint64_t v){auto s=std::to_string(v);for(int at=int(s.size())-3;at>0;at-=3)s.insert(size_t(at),",");return s;}
ImportScope::ImportScope(std::string what):ImportScope(std::move(what),std::string()){}
ImportScope::ImportScope(std::string what,std::string domain){unwound.clear();scopes.push_back({std::move(what),Json(),std::move(domain),std::uncaught_exceptions()});}
ImportScope::ImportScope(std::string_view kind,int64_t index,std::string_view name){unwound.clear();auto p=place(kind,index,name);auto what=placeText(p);scopes.push_back({std::move(what),std::move(p),std::string(),std::uncaught_exceptions()});}
ImportScope::~ImportScope(){
    if(scopes.empty())return;
    if(std::uncaught_exceptions()>scopes.back().unwinding)unwound.insert(unwound.begin(),std::move(scopes.back()));
    else if(!std::uncaught_exceptions())unwound.clear(); // the code went on: an exception handled inside is over
    scopes.pop_back();
}
std::vector<std::string> importScopes(){return names(scopes);}
Json importPlaces(){return places(scopes);}
Json describeException(std::exception_ptr error){
    Json r={{"error","Unknown error"},{"errorCode","unknown"},{"errorDetails",Json::object()},{"context",Json::array()},{"exceptionType","unknown"}};
    // The scopes this exception unwound through: the innermost domain and place say what was being read.
    auto trail=std::move(unwound);unwound.clear();std::string domain;for(auto& s:trail)if(!s.domain.empty())domain=s.domain;
    auto where=places(trail);auto at=where.empty()?std::string():" (in "+placeText(where.back())+")";
    try{if(error)std::rethrow_exception(error);}
    catch(const ImportError& e){
        // A file error's sentence is short and stable (fileFailure): the report adds why.
        std::string text=e.what();auto field=[&](const char* key){auto it=e.details.find(key);return it!=e.details.end()&&it->is_string()?it->get<std::string>():std::string();};
        if(auto why=field("why");!why.empty()){auto path=field("path");if(!path.empty()&&text.find(path)==std::string::npos)text+=" ("+path+")";text+=": "+why;}
        r["error"]=text;r["errorCode"]=e.code;r["errorDetails"]=e.details;r["context"]=e.context;r["exceptionType"]="import";return r;}
    catch(const Json::exception& e){
        // nlohmann prefixes "[json.exception.out_of_range.403] "; the sentence after it is the useful part.
        std::string text=e.what();if(auto end=text.find("] ");text.starts_with("[json.exception.")&&end!=std::string::npos)text=text.substr(end+2);
        r["error"]=(domain=="vrm"?std::string("The VRM file's data is malformed or incomplete: "):std::string("The file's JSON data is malformed or incomplete: "))+text+at;
        r["errorCode"]=domain.empty()?std::string("json"):domain+".json";r["exceptionType"]="json";r["errorDetails"]={{"id",e.id},{"reason",text}};
    }
    catch(const fs::filesystem_error& e){
        auto value=uint32_t(e.code().value());bool system=e.code().category()==std::system_category();
        bool full=e.code()==std::errc::no_space_on_device||(system&&(value==ERROR_DISK_FULL||value==ERROR_HANDLE_DISK_FULL));
        // The cache's long-path form (\\?\C:\…, \\?\UNC\server\…) is shown as the player knows it.
        std::string path=e.path1().empty()?std::string():utf8(e.path1().wstring());
        if(path.starts_with("\\\\?\\UNC\\"))path="\\\\"+path.substr(8);else if(path.starts_with("\\\\?\\"))path=path.substr(4);
        std::string reason=system?systemErrorText(value):e.code().message();
        // what() starts with the operation; one that changes files works on the cache.
        std::string operation=e.what();bool writing=false;for(auto op:{"create_","rename","copy","remove","resize_file","permissions"})writing|=operation.starts_with(op);
        r["error"]=full?"The disk is full: "+path:(writing?"Cannot write ":"Cannot access ")+(path.empty()?std::string("a file"):path)+": "+reason;
        r["errorCode"]=full?std::string("io.disk_full"):writing?std::string("io.write"):system&&systemErrorCode(value)!="io.read"?systemErrorCode(value):std::string("io.filesystem");r["exceptionType"]="filesystem";
        r["errorDetails"]={{"path",path},{"systemError",e.code().value()},{"systemMessage",e.code().message()}};
    }
    catch(const std::bad_alloc&){r["error"]="The importer ran out of memory"+at;r["errorCode"]="memory";r["exceptionType"]="memory";}
    catch(const std::exception& e){r["error"]=e.what();r["exceptionType"]="std";if(!domain.empty())r["errorCode"]=domain+".error";}
    catch(...){}
    r["context"]=names(trail);if(!where.empty())r["errorDetails"]["where"]=where;
    return r;
}
void setImportStage(const char* code){strncpy_s(stage,code?code:"",_TRUNCATE);}
const char* importStage(){return stage;}

FileFormat sniffFormat(std::span<const unsigned char> b,const fs::path& path){
 auto starts=[&](std::string_view s,size_t at=0){return b.size()>=at+s.size()&&std::memcmp(b.data()+at,s.data(),s.size())==0;};
 std::string head(reinterpret_cast<const char*>(b.data()),std::min<size_t>(b.size(),4096));
 auto lower=head;for(auto& c:lower)c=char(std::tolower(static_cast<unsigned char>(c)));
 auto extension=path.extension().wstring();for(auto& c:extension)c=wchar_t(towlower(c));
 // Names carry their article: "This is " + name.
 if(starts("PMX "))return {"pmx","a PMX model","model"};
 if(starts("Pmd"))return {"pmd","a PMD model","model"};
 if(starts("glTF"))return {"glb","a binary glTF (GLB) file","convertible"};
 if(starts("Kaydara FBX Binary")||starts("; FBX"))return {"fbx","an FBX file","convertible"};
 if(starts("BLENDER"))return {"blend","a Blender file","static"};
 // Compressed .blend files are zstd (Blender 3+) or gzip (older) streams.
 if(starts("\x28\xB5\x2F\xFD"))return extension==L".blend"?FileFormat{"blend","a Blender file","static"}:FileFormat{"zstd","a zstd-compressed file","archive"};
 if(starts("\x1F\x8B"))return extension==L".blend"?FileFormat{"blend","a Blender file","static"}:FileFormat{"gzip","a gzip archive","archive"};
 if(starts("PK\x03\x04")||starts("PK\x05\x06"))return {"zip","a ZIP archive","archive"};
 if(starts("Rar!\x1A\x07"))return {"rar","a RAR archive","archive"};
 if(starts("7z\xBC\xAF\x27\x1C"))return {"7z","a 7-Zip archive","archive"};
 if(starts("Vocaloid Motion Data"))return {"vmd","an MMD motion (VMD)","motion"};
 if(starts("Vocaloid Pose Data"))return {"vpd","an MMD pose (VPD)","motion"};
 if(starts("Polygon Movie maker"))return {"pmm","an MMD project (PMM)","motion"};
 if(starts("\x89PNG"))return {"png","a PNG image","image"};
 if(starts("\xFF\xD8\xFF"))return {"jpeg","a JPEG image","image"};
 if(starts("GIF8"))return {"gif","a GIF image","image"};
 if(starts("RIFF")&&starts("WEBP",8))return {"webp","a WebP image","image"};
 if(starts("DDS "))return {"dds","a DDS texture","image"};
 if(starts("8BPS"))return {"psd","a Photoshop image","image"};
 if(starts("BM")&&extension==L".bmp")return {"bmp","a BMP image","image"};
 if(extension==L".tga")return {"tga","a TGA image","image"};
 if(starts("%PDF"))return {"pdf","a PDF document","other"};
 if(starts("MZ"))return {"exe","a Windows program","other"};
 if(starts("xof "))return {"x","a DirectX model (.x)","static"};
 if(starts("Metasequoia Document"))return {"mqo","a Metasequoia model (MQO)","static"};
 if(starts("ply\n")||starts("ply\r\n"))return {"ply","a PLY model","static"};
 if(extension==L".3ds")return {"3ds","a 3DS model","static"};
 if(extension==L".stl")return {"stl","an STL model","static"};
 // Text formats: glTF JSON, COLLADA XML, OBJ lines; anything else readable is a text file.
 bool text=std::all_of(head.begin(),head.end(),[](char c){auto u=static_cast<unsigned char>(c);return u>=0x20||c=='\n'||c=='\r'||c=='\t';});
 auto first=lower.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
 if(first!=std::string::npos&&lower[first]=='{'&&lower.find("\"asset\"")!=std::string::npos)return {"gltf","a glTF file","convertible"};
 if(lower.find("<collada")!=std::string::npos)return {"dae","a COLLADA (DAE) file","convertible"};
 if(text&&!head.empty()){
  size_t lines=0,geometry=0;
  for(size_t at=0;at<lower.size();){auto end=lower.find('\n',at);auto line=lower.substr(at,end==std::string::npos?std::string::npos:end-at);
   if(!line.empty()&&line[0]!='#'){lines++;for(auto key:{"v ","vn ","vt ","f ","o ","g ","s ","mtllib ","usemtl "})if(line.starts_with(key)){geometry++;break;}}
   if(end==std::string::npos)break;at=end+1;}
  if(geometry>0&&geometry*2>=lines)return {"obj","an OBJ model","static"};
  return {"text","a text file","other"};
 }
 return {"unknown","a file of an unknown type","unknown"};
}
[[noreturn]] void notCharacterFile(std::span<const unsigned char> head,const fs::path& path){
 if(head.empty())importFail("io.empty","The file is empty (0 bytes). It may still be downloading or syncing, or an earlier copy failed.",{{"size",0}});
 auto f=sniffFormat(head,path);auto extension=utf8(path.extension().wstring());
 Json details={{"format",f.id},{"family",f.family}};
 if(f.family=="archive")importFail("format.archive","This is "+f.name+", not a model. Extract it first, then import the .pmx, .pmd or .vrm file inside.",details);
 if(f.family=="motion")importFail("format.motion","This is "+f.name+", not a model. Import the character's .pmx or .pmd file; motions and poses are not imported.",details);
 if(f.family=="image")importFail("format.image","This is "+f.name+", not a model. Import the model file (.pmx, .pmd or .vrm); its textures are found by themselves.",details);
 if(f.family=="convertible"){
  // The extension routes a file to its importer, so a renamed file reaches the wrong one.
  std::string want=f.id=="dae"?".dae":f.id=="gltf"?".gltf":f.id=="glb"?".glb":".fbx",label=f.id=="dae"?"COLLADA (DAE)":f.id=="gltf"?"glTF":f.id=="glb"?"binary glTF (GLB)":"FBX";details["extension"]=want;
  importFail("format.renamed","This file contains "+label+" data, but its name ends in "+(extension.empty()?std::string("no extension"):extension)+". Rename it to end in "+want+" and import it again.",details);
 }
 if(f.family=="static")importFail("character.format","This is "+f.name+" without a skeleton, not a character model this importer can read. Static 3D models (OBJ, BLEND) belong in Static Props.",details);
 importFail("format.unknown","This file is not a character model this importer can read: it does not start like a PMX, PMD or VRM file"+(f.family=="other"?" (it is "+f.name+")":std::string())+". Static 3D models (OBJ, BLEND) belong in Static Props.",details);
}
std::string systemErrorText(uint32_t e){
 switch(e){
  case ERROR_FILE_NOT_FOUND:case ERROR_PATH_NOT_FOUND:return "the file does not exist (it may have been moved, renamed or deleted)";
  case ERROR_ACCESS_DENIED:return "Windows denied access to it";
  case ERROR_SHARING_VIOLATION:case ERROR_LOCK_VIOLATION:return "another program is using it";
  case ERROR_DISK_FULL:case ERROR_HANDLE_DISK_FULL:return "the disk is full";
  case ERROR_WRITE_PROTECT:return "the drive is write-protected";
  case ERROR_NOT_READY:case ERROR_DEV_NOT_EXIST:return "the drive is not available";
  case ERROR_CRC:case ERROR_IO_DEVICE:case ERROR_FILE_CORRUPT:case ERROR_DISK_CORRUPT:return "the drive reported a read error (the disk may be damaged)";
  case ERROR_BAD_NETPATH:case ERROR_NETNAME_DELETED:case ERROR_UNEXP_NET_ERR:return "the network location is not available";
  case ERROR_FILENAME_EXCED_RANGE:return "the path is too long";
  case ERROR_INVALID_NAME:return "the path is not valid";
  case ERROR_DIRECTORY:return "it is a folder, not a file";
  case ERROR_ALREADY_EXISTS:case ERROR_FILE_EXISTS:return "a file of that name is in the way";
  case ERROR_VIRUS_INFECTED:case ERROR_VIRUS_DELETED:return "antivirus software blocked it";
 }
 return "Windows error "+std::to_string(e);
}
std::string systemErrorCode(uint32_t e,bool writing){
 if(writing)return e==ERROR_DISK_FULL||e==ERROR_HANDLE_DISK_FULL?"io.disk_full":"io.write";
 switch(e){
  case ERROR_FILE_NOT_FOUND:case ERROR_PATH_NOT_FOUND:case ERROR_INVALID_NAME:case ERROR_DIRECTORY:case ERROR_BAD_NETPATH:return "io.missing";
  case ERROR_ACCESS_DENIED:case ERROR_WRITE_PROTECT:case ERROR_VIRUS_INFECTED:case ERROR_VIRUS_DELETED:return "io.denied";
  case ERROR_SHARING_VIOLATION:case ERROR_LOCK_VIOLATION:return "io.locked";
  case ERROR_DISK_FULL:case ERROR_HANDLE_DISK_FULL:return "io.disk_full";
  case ERROR_NOT_READY:case ERROR_DEV_NOT_EXIST:case ERROR_CRC:case ERROR_IO_DEVICE:case ERROR_FILE_CORRUPT:case ERROR_DISK_CORRUPT:case ERROR_NETNAME_DELETED:case ERROR_UNEXP_NET_ERR:return "io.device";
 }
 return "io.read";
}
void fileFailure(const std::string& message,const fs::path& path,uint32_t systemError,bool writing,Json details){
 if(!details.is_object())details=Json::object();details["path"]=utf8(path.wstring());
 if(systemError){details["systemError"]=systemError;details["why"]=systemErrorText(systemError);}
 importFail(systemError?systemErrorCode(systemError,writing):writing?"io.write":"io.read",message,details);
}
Json describeWorkerExit(uint32_t code){
 struct Cause{uint32_t code;const char* id;const char* text;};
 static const Cause causes[]={
  {0xC0000005u,"access_violation","an access violation (it used memory it does not own)"},
  {0xC00000FDu,"stack_overflow","a stack overflow (the file's data may nest too deeply)"},
  {0xC0000409u,"fail_fast","a fail-fast stop (a safety check found corrupted memory or an invalid argument)"},
  {0xC0000374u,"heap_corruption","heap corruption"},
  {0xC0000017u,"out_of_memory","running out of memory"},
  {0xC000012Du,"out_of_memory","running out of memory (the page file is full)"},
  {0xE06D7363u,"cpp_exception","an unhandled C++ exception"},
  {3u,"abort","an abort (an unhandled error or a failed internal check)"},
  {0xC000001Du,"illegal_instruction","an illegal instruction (the processor lacks an instruction the importer uses)"},
  {0xC0000096u,"illegal_instruction","a privileged instruction"},
  {0xC0000094u,"divide_by_zero","an integer division by zero"},
  {0xC0000135u,"missing_dll","a missing DLL (part of the native files is missing)"},
  {0xC0000142u,"dll_init","a DLL that failed to start"},
  {0xC000007Bu,"bad_image","a DLL for the wrong processor type (32-bit and 64-bit files mixed)"},
  {0x80000003u,"breakpoint","a breakpoint (an internal check failed)"},
  {0xC0000420u,"assertion","a failed assertion"},
  {0xC000013Au,"killed","being closed by Windows"},
  {0x40010004u,"killed","being closed by a debugger or Windows"},
  {1u,"killed","being stopped by another program, or failing to write its result"},
  {0u,"no_result","finishing without a result"},
 };
 char hex[16];snprintf(hex,sizeof hex,"0x%08X",code);
 Json r={{"exitCode",code},{"exitCodeHex",hex},{"cause","unknown"},{"errorCode","worker.crash"}};std::string text="an unknown error";
 for(auto& c:causes)if(c.code==code){r["cause"]=c.id;text=c.text;break;}
 if(r["cause"]=="out_of_memory")r["errorCode"]="memory";
 r["error"]="The import worker exited before completion after "+text+" (exit code "+(code<0x10000u?std::to_string(code):std::string(hex))+").";
 return r;
}
Json finishedWorkerStatus(Json last,uint32_t exitCode,const std::string& log,const std::string& source,const std::string& kind,const std::string& readError){
 if(!last.is_object())last=Json::object();
 if(!readError.empty()||last.value("state","")=="running"||!last.contains("state")){
  auto crash=describeWorkerExit(exitCode);
  Json failed={{"state","failed"},{"error",crash["error"]},{"errorCode",crash["errorCode"]},{"exceptionType","crash"},{"context",Json::array()},
   {"errorDetails",{{"exitCode",crash["exitCode"]},{"exitCodeHex",crash["exitCodeHex"]},{"cause",crash["cause"]}}}};
  for(auto key:{"stage","stageCode","filename","detail","current","total"})if(last.contains(key))failed[key]=last[key];
  if(!readError.empty())failed["errorDetails"]["statusError"]=readError;
  if(!source.empty()){failed["source"]=source;if(!failed.contains("filename"))try{failed["filename"]=utf8(fs::path(wide(source)).filename().wstring());}catch(...){}}
  failed["kind"]=kind.empty()?std::string("character"):kind;last=std::move(failed);
 }
 if(last.value("state","")=="failed"){last["exitCode"]=exitCode;if(!log.empty())last["log"]=clean(log,true);}
 return last;
}
}
