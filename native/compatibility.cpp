#include "compatibility.hpp"
#include "validation_once.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include "posix.hpp"
#include "release.hpp"
#include <elf.h>
#include <sys/mman.h>
#endif
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>

namespace mmd {
namespace {
template<class T> T read(const Bytes& b,size_t at){if(at>b.size()||sizeof(T)>b.size()-at)throw std::runtime_error("Invalid PE bounds");T v;std::memcpy(&v,b.data()+at,sizeof(v));return v;}
std::mutex policyMutex;
Json policy;
struct Cached {Json profile;std::string error;};
std::map<std::wstring,Cached> verified;
// Guards of unverified builds: the first observation, which later ones must equal.
std::map<std::string,uintptr_t> learned;
const std::set<std::string> libraries={"engine.dll","client.dll","vphysics.dll","materialsystem.dll","shaderapidx9.dll","stdshader_dx9.dll","stdshader_dx6.dll"};
// The render contexts are identified by their RTTI class; profiles may still carry
// materialsystem.dll's queuedContext/hardwareContext for older native releases.
const std::map<std::string,std::set<std::string>> guards={
 {"engine.dll",{"lighting"}},
 {"vphysics.dll",{"physics","environment","objectTable","objectPosition","objectForce"}}
};
}
#ifndef _WIN32
// The ABI family of this build's profiles: source-linux64-v1 or source-linux-v1.
constexpr const char* Family="source-" MMDHL_PLATFORM "-v1";
namespace {
// Shared objects found once stay where they are for the game's lifetime.
std::mutex librariesMutex;std::map<std::wstring,posix::Library> loadedLibraries;
std::optional<posix::Library> gameLibrary(const wchar_t* name){
 std::wstring key(name);{std::lock_guard lock(librariesMutex);auto it=loadedLibraries.find(key);if(it!=loadedLibraries.end())return it->second;}
 auto base=utf8(key);if(!base.ends_with(".dll"))return std::nullopt;base.resize(base.size()-4);
 for(const auto& candidate:{base+"_client.so",base+".so"})if(auto lib=posix::loadedLibrary(candidate)){std::lock_guard lock(librariesMutex);loadedLibraries.emplace(key,*lib);return lib;}
 return std::nullopt;
}
}
std::string gameLibraryName(const wchar_t* name){auto lib=gameLibrary(name);return lib?fs::path(lib->path).filename().string():std::string();}
void* gameInterfaceFactory(const wchar_t* name){auto lib=gameLibrary(name);return lib?lib->symbol("CreateInterface"):nullptr;}
uintptr_t gameLibraryBase(const wchar_t* name){auto lib=gameLibrary(name);return lib?lib->base:0;}
bool gameLibraryCode(const wchar_t* name,const void* p){auto lib=gameLibrary(name);return lib&&lib->executes(p);}
bool gameLibraryImage(const wchar_t* name,const void* p){auto lib=gameLibrary(name);return lib&&lib->contains(p);}
Json elfEvidence(const Bytes& b){
 if(b.size()<64||std::memcmp(b.data(),ELFMAG,SELFMAG))throw std::runtime_error("Not an ELF image");
 const bool wide=b[EI_CLASS]==ELFCLASS64;
 if(wide!=(sizeof(void*)==8)||b[EI_DATA]!=ELFDATA2LSB)throw std::runtime_error("ELF image for another platform");
 Json result={{"format",1},{"machine",MMDHL_PLATFORM},{"sections",Json::array()}};uint64_t image=0;
 auto segments=[&](auto header,auto program){
  auto h=read<decltype(header)>(b,0);if(h.e_phentsize!=sizeof(program)||h.e_phnum>64)throw std::runtime_error("Invalid ELF program headers");
  for(unsigned i=0;i<h.e_phnum;i++){auto ph=read<decltype(program)>(b,size_t(h.e_phoff)+i*sizeof(program));if(ph.p_type!=PT_LOAD)continue;
   image=std::max<uint64_t>(image,uint64_t(ph.p_vaddr)+ph.p_memsz);
   // Code and read-only data only; writable data holds relocated pointers.
   if(ph.p_flags&PF_W)continue;
   if(uint64_t(ph.p_offset)>b.size()||uint64_t(ph.p_filesz)>b.size()-ph.p_offset)throw std::runtime_error("Invalid ELF segment");
   result["sections"].push_back({{"name","LOAD"+std::to_string(i)},{"rva",uint64_t(ph.p_vaddr)},{"virtualSize",uint64_t(ph.p_memsz)},{"size",uint64_t(ph.p_filesz)},{"flags",uint32_t(ph.p_flags)},{"sha256",hash(std::span(b.data()+ph.p_offset,size_t(ph.p_filesz)))}});}
 };
 if(wide)segments(Elf64_Ehdr{},Elf64_Phdr{});else segments(Elf32_Ehdr{},Elf32_Phdr{});
 result["imageSize"]=image;
 if(result["sections"].empty())throw std::runtime_error("ELF image contains no ABI evidence");
 return result;
}
#endif
#ifdef _WIN32
Json peEvidence(const Bytes& b){
 if(read<uint16_t>(b,0)!=IMAGE_DOS_SIGNATURE)throw std::runtime_error("Not a PE image");
 auto nt=read<uint32_t>(b,0x3c);
 if(read<uint32_t>(b,nt)!=IMAGE_NT_SIGNATURE)throw std::runtime_error("Invalid PE signature");
 auto fh=read<IMAGE_FILE_HEADER>(b,nt+4);
 if(fh.Machine!=IMAGE_FILE_MACHINE_AMD64||fh.NumberOfSections>96||fh.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER64))throw std::runtime_error("Expected Windows x64 PE");
 auto opt=read<IMAGE_OPTIONAL_HEADER64>(b,nt+4+sizeof(fh));
 if(opt.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)throw std::runtime_error("Expected PE32+");
 std::vector<IMAGE_SECTION_HEADER> sections;
 auto at=size_t(nt)+4+sizeof(fh)+fh.SizeOfOptionalHeader;
 for(unsigned i=0;i<fh.NumberOfSections;i++)sections.push_back(read<IMAGE_SECTION_HEADER>(b,at+i*sizeof(IMAGE_SECTION_HEADER)));
 auto offset=[&](uint32_t rva,size_t size)->size_t{for(auto& s:sections)if(rva>=s.VirtualAddress&&uint64_t(rva)-s.VirtualAddress+size<=s.SizeOfRawData){auto o=size_t(s.PointerToRawData)+rva-s.VirtualAddress;if(o>b.size()||size>b.size()-o)break;return o;}throw std::runtime_error("PE RVA outside file");};
 Bytes normalized=b;
 // Only normalize documented DIR64 base relocations. Preserve their target RVA;
 // do not wildcard RIP-relative instructions, function bodies or referenced data.
 auto reloc=opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
 if(reloc.Size){size_t pos=offset(reloc.VirtualAddress,reloc.Size),end=pos+reloc.Size;
  while(pos<end){auto block=read<IMAGE_BASE_RELOCATION>(b,pos);if(block.SizeOfBlock<8||block.SizeOfBlock>end-pos||block.SizeOfBlock%2)throw std::runtime_error("Invalid PE relocations");
   for(size_t p=pos+8;p<pos+block.SizeOfBlock;p+=2){auto item=read<uint16_t>(b,p);auto type=item>>12;if(type==IMAGE_REL_BASED_ABSOLUTE)continue;if(type!=IMAGE_REL_BASED_DIR64)throw std::runtime_error("Unsupported PE relocation");auto o=offset(block.VirtualAddress+(item&4095),8);auto value=read<uint64_t>(b,o);if(value<opt.ImageBase||value-opt.ImageBase>=opt.SizeOfImage)throw std::runtime_error("Relocation target outside image");value-=opt.ImageBase;std::memcpy(normalized.data()+o,&value,8);}
   pos+=block.SizeOfBlock;
  }
 }
 Json result={{"format",1},{"machine","win64"},{"imageSize",opt.SizeOfImage},{"sections",Json::array()}};
 for(auto& s:sections){
  // Include all executable and read-only initialized data. This deliberately
  // errs toward rejection when a change cannot be shown to preserve the ABI.
  if(!(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)&&((s.Characteristics&IMAGE_SCN_MEM_WRITE)||!(s.Characteristics&IMAGE_SCN_CNT_INITIALIZED_DATA)))continue;
  char name[9]={};std::memcpy(name,s.Name,8);
  if(std::string(name)==".reloc"||std::string(name)==".rsrc")continue;
  if(size_t(s.PointerToRawData)>b.size()||s.SizeOfRawData>b.size()-s.PointerToRawData)throw std::runtime_error("Invalid PE section");
  result["sections"].push_back({{"name",name},{"rva",s.VirtualAddress},{"virtualSize",s.Misc.VirtualSize},{"size",s.SizeOfRawData},{"flags",s.Characteristics},{"sha256",hash(std::span(normalized.data()+s.PointerToRawData,s.SizeOfRawData))}});
 }
 if(result["sections"].empty())throw std::runtime_error("PE contains no ABI evidence");
 return result;
}
#else
Json peEvidence(const Bytes&){throw std::runtime_error("Not a PE image");}
#endif
bool matchesEvidence(const Json& expected,const Json& observed){return expected==observed;}
// Validates the whole policy before taking it: installation.lua offers a newer policy this
// build rejects (an unknown library, a profile without a required guard) again without one
// library at a time, then without libraries. Guard names this build does not use are ignored.
Json configureCompatibility(const Json& input){
 std::lock_guard lock(policyMutex);
 #ifdef _WIN32
 constexpr const char* Family="source-win64-v1";
 #endif
 if(input.value("schema",0)!=1||input.value("family","")!=Family||!input.contains("libraries")||!input["libraries"].is_array()||input["libraries"].size()>128)throw std::runtime_error("Unsupported compatibility profile schema or ABI family");
 for(auto& p:input["libraries"]){auto name=p.at("name").get<std::string>();if(!libraries.contains(name)||p.at("sha256").get<std::string>().size()!=64||p.at("evidence").value("format",0)!=1||p.at("evidence").at("sections").empty())throw std::runtime_error("Invalid compatibility library profile");
  auto g=guards.find(name);if(g!=guards.end())for(auto& key:g->second){auto rva=p.at("guards").at(key).get<uint64_t>();if(!rva||rva>=p["evidence"].at("imageSize").get<uint64_t>())throw std::runtime_error("Invalid ABI guard RVA");}
 }
 if(!policy.is_null()&&policy!=input)throw std::runtime_error("Compatibility policy changed; restart Garry's Mod");
 policy=input;return {{"configured",true},{"family",Family}};
}
const Json& requireGameBinary(const wchar_t* name){
#ifndef _WIN32
 auto library=gameLibrary(name);
 std::lock_guard lock(policyMutex);
 if(!library)throw std::runtime_error("Game library not loaded: "+utf8(name));
 auto found=verified.find(name);if(found!=verified.end()){if(!found->second.error.empty())throw std::runtime_error(found->second.error);return found->second.profile;}
 Cached result;
 try{
  if(policy.is_null())throw std::runtime_error("Compatibility policy not configured");
  auto bytes=readFile(library->path);auto sha=hash(bytes);Json evidence;
  try{evidence=elfEvidence(bytes);}catch(const std::exception&){}
  if(!evidence.is_null())for(auto& p:policy["libraries"])if(p["name"]==utf8(name)&&matchesEvidence(p["evidence"],evidence)){
   result.profile=p;result.profile["observedSHA256"]=sha;result.profile["match"]=sha==p["sha256"]?"tested":"abi-evidence";break;
  }
  // Game updates replace these files. A build no profile describes still runs:
  // the interface, slot and class checks below guard every private use.
  if(result.profile.is_null())result.profile={{"name",utf8(name)},{"variant","unverified"},{"guards",Json::object()},{"observedSHA256",sha},{"match","unverified"}};
  result.profile["file"]=fs::path(library->path).filename().string();
 }catch(const std::exception& e){result.error=e.what();}
 auto& saved=verified.emplace(name,std::move(result)).first->second;
 if(!saved.error.empty())throw std::runtime_error(saved.error);return saved.profile;
#else
 std::lock_guard lock(policyMutex);
 auto module=GetModuleHandleW(name);if(!module)throw std::runtime_error("Game library not loaded: "+utf8(name));
 auto found=verified.find(name);if(found!=verified.end()){if(!found->second.error.empty())throw std::runtime_error(found->second.error);return found->second.profile;}
 Cached result;
 try{
  if(policy.is_null())throw std::runtime_error("Compatibility policy not configured");
  wchar_t path[32768];if(!GetModuleFileNameW(module,path,32768))throw std::runtime_error("Cannot locate loaded game library");
  auto bytes=readFile(path);auto sha=hash(bytes);Json evidence;
  try{evidence=peEvidence(bytes);}catch(const std::exception&){}
  if(!evidence.is_null())for(auto& p:policy["libraries"])if(p["name"]==utf8(name)&&matchesEvidence(p["evidence"],evidence)){
   result.profile=p;result.profile["observedSHA256"]=sha;result.profile["match"]=sha==p["sha256"]?"tested":"abi-evidence";break;
  }
  // Game updates replace these files. A build no profile describes still runs:
  // the interface, slot and class checks below guard every private use.
  if(result.profile.is_null())result.profile={{"name",utf8(name)},{"variant","unverified"},{"guards",Json::object()},{"observedSHA256",sha},{"match","unverified"}};
 }catch(const std::exception& e){result.error=e.what();}
 auto& saved=verified.emplace(name,std::move(result)).first->second;
 if(!saved.error.empty())throw std::runtime_error(saved.error);return saved.profile;
#endif
}
bool matchesAbiRva(const wchar_t* name,const char* guard,uintptr_t observed){
 const auto& pinned=requireGameBinary(name).at("guards");
 if(pinned.contains(guard))return pinned.at(guard).get<uintptr_t>()==observed;
 std::lock_guard lock(policyMutex);
 return learned.emplace(utf8(name)+"/"+guard,observed).first->second==observed;
}
void requireAbiRva(const wchar_t* name,const char* guard,uintptr_t observed){if(!matchesAbiRva(name,guard,observed))throw std::runtime_error("Game ABI layout mismatch: "+utf8(name)+" / "+guard);}
#ifndef _WIN32
void requireOwnedSlots(void* object,const wchar_t* library,std::initializer_list<size_t> slots){
 requireGameBinary(library);auto owner=gameLibrary(library);
 if(!owner||!object||!posix::readable(object,sizeof(void*)))throw std::runtime_error("Unreadable game interface");
 auto table=*reinterpret_cast<void***>(object);
 for(auto slot:slots){if(!posix::readable(table+slot,sizeof(void*)))throw std::runtime_error("Unreadable game vtable");if(!owner->executes(table[slot]))throw std::runtime_error("Game interface slot replaced or outside expected library");}
}
// Itanium RTTI: the slot before the address point holds the std::type_info, whose second
// word is the mangled name ("23CMatQueuedRenderContext"; '*' marks a local type).
std::string rttiClass(const void* object,const wchar_t* library){
 auto lib=gameLibrary(library);if(!lib||!object||!posix::readable(object,sizeof(void*)))return {};
 auto table=*reinterpret_cast<const uintptr_t* const*>(object);
 if(!lib->contains(table)||!posix::readable(table-1,sizeof(void*)))return {};
 auto info=reinterpret_cast<const uintptr_t*>(table[-1]);
 if(!lib->contains(info)||!posix::readable(info,2*sizeof(void*)))return {};
 auto name=reinterpret_cast<const char*>(info[1]);if(!lib->contains(name))return {};
 std::string out;for(size_t i=0;i<256;i++){if(!posix::readable(name+i,1))return {};if(!name[i])break;out.push_back(name[i]);}
 if(!out.empty()&&out[0]=='*')out.erase(0,1);
 return out;
}
#else
void requireOwnedSlots(void* object,const wchar_t* library,std::initializer_list<size_t> slots){
 requireGameBinary(library);auto owner=GetModuleHandleW(library);MEMORY_BASIC_INFORMATION memory{};
 if(!object||!VirtualQuery(object,&memory,sizeof(memory))||memory.State!=MEM_COMMIT||(memory.Protect&(PAGE_NOACCESS|PAGE_GUARD)))throw std::runtime_error("Unreadable game interface");
 auto table=*reinterpret_cast<void***>(object);
 for(auto slot:slots){if(!VirtualQuery(table+slot,&memory,sizeof(memory))||memory.State!=MEM_COMMIT||(memory.Protect&(PAGE_NOACCESS|PAGE_GUARD)))throw std::runtime_error("Unreadable game vtable");auto target=table[slot];if(!VirtualQuery(target,&memory,sizeof(memory))||memory.AllocationBase!=owner||!(memory.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))throw std::runtime_error("Game interface slot replaced or outside expected library");}
}
std::string rttiClass(const void* object,const wchar_t* library){
 auto module=reinterpret_cast<uintptr_t>(GetModuleHandleW(library));if(!module||!object)return {};
 // Readable bytes from p to the end of its committed region; the vtable, locator
 // and type name must lie in library, the object itself anywhere.
 auto readable=[&](const void* p,bool owned=true)->size_t{MEMORY_BASIC_INFORMATION m{};
  if(!VirtualQuery(p,&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD))||(owned&&reinterpret_cast<uintptr_t>(m.AllocationBase)!=module))return 0;
  return reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize-reinterpret_cast<uintptr_t>(p);};
 if(readable(object,false)<8)return {};
 auto table=*reinterpret_cast<const uintptr_t* const*>(object);
 if(readable(table-1)<8)return {};
 // x64 complete object locator: signature 1, offsets, then image-relative type, hierarchy and self.
 auto locator=reinterpret_cast<const uint32_t*>(table[-1]);
 if(readable(locator)<24||locator[0]!=1||module+locator[5]!=reinterpret_cast<uintptr_t>(locator))return {};
 auto name=reinterpret_cast<const char*>(module+locator[3]+16);
 auto size=std::min<size_t>(readable(name),256);
 auto end=std::find(name,name+size,'\0');
 return end==name+size?std::string():std::string(name,end);
}
#endif
namespace {
std::mutex shiftMutex;
Json shifts=Json::object();
}
// The vtable ends at its last entry that is code of library before the first entry that is
// not code at all: the next table's RTTI locator or offset-to-top, or other data. An entry
// another module replaced (a hook: code outside library) inside the table does not end it,
// so a hooked table keeps its length; requireOwnedSlots refuses the slots actually called
// when they are the hooked ones.
size_t vtableLength(void* object,const wchar_t* library){
#ifndef _WIN32
 auto lib=gameLibrary(library);if(!lib||!object||!posix::readable(object,sizeof(void*)))return 0;
 auto table=*reinterpret_cast<void* const* const*>(object);size_t length=0;
 for(size_t at=0;at<1024;at++){
  if(!posix::readable(table+at,sizeof(void*)))break;
  if(lib->executes(table[at])){length=at+1;continue;}
  auto access=posix::protection(table[at]);if(access<0||!(access&PROT_EXEC))break;
 }
 return length;
#else
 auto module=GetModuleHandleW(library);if(!module||!object)return 0;
 auto table=*reinterpret_cast<void* const* const*>(object);
 size_t length=0;MEMORY_BASIC_INFORMATION memory{};
 for(size_t at=0;at<1024;at++){
  if(!VirtualQuery(table+at,&memory,sizeof(memory))||memory.State!=MEM_COMMIT||(memory.Protect&(PAGE_NOACCESS|PAGE_GUARD)))break;
  auto target=table[at];
  if(!VirtualQuery(target,&memory,sizeof(memory))||memory.State!=MEM_COMMIT||!(memory.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))break;
  if(memory.AllocationBase==module)length=at+1;
 }
 return length;
#endif
}
std::optional<size_t> knownAppSystemShift(size_t length,size_t compiledLength){
 if(length>=compiledLength)return 0;
 if(length+4==compiledLength)return 4;
 return std::nullopt;
}
size_t appSystemShift(void* object,const wchar_t* library,size_t compiledLength){
 auto length=vtableLength(object,library);auto shift=knownAppSystemShift(length,compiledLength);
 {std::lock_guard lock(shiftMutex);auto& entry=shifts[utf8(library)];entry["slotShift"]=shift?Json(*shift):Json();entry["vtableLength"]=length;entry["compiledLength"]=compiledLength;}
 if(!shift)throw std::runtime_error("Unrecognized "+utf8(library)+" interface layout ("+std::to_string(length)+" vtable entries, expected "+std::to_string(compiledLength)+" or "+std::to_string(compiledLength-4)+")");
 return *shift;
}
bool olderPhysicsLayout(void* physics,void* collision){
 auto length=vtableLength(collision,L"vphysics.dll");auto shift=appSystemShift(physics,L"vphysics.dll",PhysicsVtableLength);
 // Two layouts are known: the x86-64 build's (or one that appends methods), and the default
 // branch's older one, short of the IAppSystem methods and of seven collision methods.
 bool older=shift==4&&length+7==CollisionVtableLength,newer=shift==0&&length>=CollisionVtableLength;
 {std::lock_guard lock(shiftMutex);auto& entry=shifts["vphysics.dll"];entry["collisionVtableLength"]=length;entry["olderPhysics"]=older;}
 if(!older&&!newer)throw std::runtime_error("Unrecognized VPhysicsCollision007 layout ("+std::to_string(length)+" vtable entries beside a VPhysics031 shift of "+std::to_string(shift)+")");
 return older;
}
Json appSystemShifts(){std::lock_guard lock(shiftMutex);return shifts;}
static void checkInterfaces(const wchar_t* name){
 // Check once before hooks are installed. Later checks must not mistake our own
 // shadow callbacks for a third-party replacement, nor call private methods.
 static std::map<std::wstring,std::unique_ptr<ValidationOnce>> checks;
 auto& entry=checks[name];if(!entry)entry=std::make_unique<ValidationOnce>();
 entry->check([&]{
#ifdef _WIN32
  auto module=GetModuleHandleW(name);
  auto factory=reinterpret_cast<void*(*)(const char*,int*)>(GetProcAddress(module,"CreateInterface"));
  auto base=reinterpret_cast<uintptr_t>(module);
#else
  auto factory=reinterpret_cast<void*(*)(const char*,int*)>(gameInterfaceFactory(name));
  auto base=gameLibraryBase(name);
#endif
  auto get=[&](const char* version,std::initializer_list<size_t> slots){
   auto object=factory?factory(version,nullptr):nullptr;
   if(!object)throw std::runtime_error(std::string("Required game interface unavailable: ")+version);
   requireOwnedSlots(object,name,slots);return object;
  };
  std::wstring library(name);
  if(library==L"engine.dll"){
   get("VEngineModel016",{abi::ModelRenderShadowSetup,abi::ModelRenderShadow,abi::ModelRenderSetupLighting});auto tools=get("VENGINETOOL003",{abi::EngineToolLightingConditions});
   requireAbiRva(name,"lighting",reinterpret_cast<uintptr_t>((*reinterpret_cast<void***>(tools))[abi::EngineToolLightingConditions])-base);
  }else if(library==L"client.dll")get("VClientEntityList003",{abi::EntityListClientEntity,abi::EntityListClientEntityFromHandle});
  else if(library==L"materialsystem.dll")appSystemShift(get("VMaterialSystem080",{0}),name,MaterialSystemVtableLength);
  else if(library==L"vphysics.dll"){
   // GetActiveEnvironmentByIndex and FindCollisionSet, and the collision methods the
   // bridge calls, at the running layout's slots.
   auto physics=get("VPhysics031",{0});auto collision=get("VPhysicsCollision007",{0});
   auto older=olderPhysicsLayout(physics,collision);auto shift=appSystemShift(physics,name,PhysicsVtableLength);
   requireOwnedSlots(physics,name,{abi::PhysicsActiveEnvironment-shift,abi::PhysicsFindCollisionSet-shift});
   requireOwnedSlots(collision,name,{collisionSlot(abi::CollisionConvexFromPolyhedron,older),collisionSlot(abi::CollisionConvertConvex,older),collisionSlot(abi::CollisionDestroy,older),collisionSlot(abi::CollisionDebugMesh,older),collisionSlot(abi::CollisionDestroyDebugMesh,older),collisionSlot(abi::CollisionQueryModel,older),collisionSlot(abi::CollisionDestroyQueryModel,older)});
   requireAbiRva(name,"physics",reinterpret_cast<uintptr_t>((*reinterpret_cast<void***>(physics))[abi::PhysicsActiveEnvironment-shift])-base);
  }
 });
}
Json checkCompatibility(bool server){
 Json result={{"ready",true},{"pending",false},{"libraries",Json::array()},{"issues",Json::array()}};
#ifdef _WIN32
 std::vector<const wchar_t*> names=server?std::vector<const wchar_t*>{L"vphysics.dll"}:std::vector<const wchar_t*>{L"engine.dll",L"client.dll",L"materialsystem.dll",L"shaderapidx9.dll",GetModuleHandleW(L"stdshader_dx9.dll")?L"stdshader_dx9.dll":L"stdshader_dx6.dll"};
 auto loaded=[](const wchar_t* name){return GetModuleHandleW(name)!=nullptr;};
 auto shown=[](const wchar_t* name){return utf8(name);};
#else
 // Linux has no RTX Remix (stdshader_dx6): the programmable shaders are always there.
 std::vector<const wchar_t*> names=server?std::vector<const wchar_t*>{L"vphysics.dll"}:std::vector<const wchar_t*>{L"engine.dll",L"client.dll",L"materialsystem.dll",L"shaderapidx9.dll",L"stdshader_dx9.dll"};
 auto loaded=[](const wchar_t* name){return gameLibrary(name).has_value();};
 auto shown=[](const wchar_t* name){auto file=gameLibraryName(name);return file.empty()?utf8(name):file;};
#endif
 for(auto name:names){if(!loaded(name)){result["ready"]=false;result["pending"]=true;continue;}try{auto p=requireGameBinary(name);checkInterfaces(name);Json library={{"name",shown(name)},{"sha256",p["observedSHA256"]},{"match",p["match"]}};auto layout=appSystemShifts();if(layout.contains(utf8(name)))library["layout"]=layout[utf8(name)];result["libraries"].push_back(library);}catch(const std::exception& e){result["ready"]=false;result["issues"].push_back({{"code","game_incompatible"},{"component",shown(name)},{"message",e.what()}});}}
 return result;
}
}
