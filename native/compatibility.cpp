#include "compatibility.hpp"
#include "validation_once.hpp"
#include <windows.h>
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
bool matchesEvidence(const Json& expected,const Json& observed){return expected==observed;}
// Validates the whole policy before taking it: installation.lua offers a newer policy this
// build rejects again without the library it does not know, then without libraries.
Json configureCompatibility(const Json& input){
 std::lock_guard lock(policyMutex);
 if(input.value("schema",0)!=1||input.value("family","")!="source-win64-v1"||!input.contains("libraries")||!input["libraries"].is_array()||input["libraries"].size()>128)throw std::runtime_error("Unsupported compatibility profile schema or ABI family");
 for(auto& p:input["libraries"]){auto name=p.at("name").get<std::string>();if(!libraries.contains(name)||p.at("sha256").get<std::string>().size()!=64||p.at("evidence").value("format",0)!=1||p.at("evidence").at("sections").empty())throw std::runtime_error("Invalid compatibility library profile");
  auto g=guards.find(name);if(g!=guards.end())for(auto& key:g->second){auto rva=p.at("guards").at(key).get<uint64_t>();if(!rva||rva>=p["evidence"].at("imageSize").get<uint64_t>())throw std::runtime_error("Invalid ABI guard RVA");}
 }
 if(!policy.is_null()&&policy!=input)throw std::runtime_error("Compatibility policy changed; restart Garry's Mod");
 policy=input;return {{"configured",true},{"family","source-win64-v1"}};
}
const Json& requireGameBinary(const wchar_t* name){
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
}
bool matchesAbiRva(const wchar_t* name,const char* guard,uintptr_t observed){
 const auto& pinned=requireGameBinary(name).at("guards");
 if(pinned.contains(guard))return pinned.at(guard).get<uintptr_t>()==observed;
 std::lock_guard lock(policyMutex);
 return learned.emplace(utf8(name)+"/"+guard,observed).first->second==observed;
}
void requireAbiRva(const wchar_t* name,const char* guard,uintptr_t observed){if(!matchesAbiRva(name,guard,observed))throw std::runtime_error("Game ABI layout mismatch: "+utf8(name)+" / "+guard);}
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
namespace {
std::mutex shiftMutex;
Json shifts=Json::object();
}
size_t vtableLength(void* object,const wchar_t* library){
 auto module=GetModuleHandleW(library);if(!module||!object)return 0;
 // The vtable ends at the first entry that is not code of library: the next
 // table's RTTI locator (materialsystem.dll) or other data (vphysics.dll).
 auto table=*reinterpret_cast<void* const* const*>(object);
 size_t length=0;MEMORY_BASIC_INFORMATION memory{};
 for(;length<1024;length++){
  if(!VirtualQuery(table+length,&memory,sizeof(memory))||memory.State!=MEM_COMMIT||(memory.Protect&(PAGE_NOACCESS|PAGE_GUARD)))break;
  auto target=table[length];
  if(!VirtualQuery(target,&memory,sizeof(memory))||memory.AllocationBase!=module||!(memory.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))break;
 }
 return length;
}
size_t appSystemShift(void* object,const wchar_t* library,size_t compiledLength){
 auto length=vtableLength(object,library);
 // Any other length keeps the compiled slots: a later build that only appends methods.
 size_t shift=length+4==compiledLength?4:0;
 std::lock_guard lock(shiftMutex);auto& entry=shifts[utf8(library)];entry["slotShift"]=shift;entry["vtableLength"]=length;entry["compiledLength"]=compiledLength;
 return shift;
}
bool olderPhysicsLayout(void* physics,void* collision){
 auto length=vtableLength(collision,L"vphysics.dll");
 bool older=appSystemShift(physics,L"vphysics.dll",PhysicsVtableLength)==4&&length+7==CollisionVtableLength;
 std::lock_guard lock(shiftMutex);auto& entry=shifts["vphysics.dll"];entry["collisionVtableLength"]=length;entry["olderPhysics"]=older;
 return older;
}
Json appSystemShifts(){std::lock_guard lock(shiftMutex);return shifts;}
static void checkInterfaces(const wchar_t* name){
 // Check once before hooks are installed. Later checks must not mistake our own
 // shadow callbacks for a third-party replacement, nor call private methods.
 static std::map<std::wstring,std::unique_ptr<ValidationOnce>> checks;
 auto& entry=checks[name];if(!entry)entry=std::make_unique<ValidationOnce>();
 entry->check([&]{
  auto module=GetModuleHandleW(name);
  auto factory=reinterpret_cast<void*(*)(const char*,int*)>(GetProcAddress(module,"CreateInterface"));
  auto get=[&](const char* version,std::initializer_list<size_t> slots){
   auto object=factory?factory(version,nullptr):nullptr;
   if(!object)throw std::runtime_error(std::string("Required game interface unavailable: ")+version);
   requireOwnedSlots(object,name,slots);return object;
  };
  std::wstring library(name);
  if(library==L"engine.dll"){
   get("VEngineModel016",{12,13,21});auto tools=get("VENGINETOOL003",{77});
   requireAbiRva(name,"lighting",reinterpret_cast<uintptr_t>((*reinterpret_cast<void***>(tools))[77])-reinterpret_cast<uintptr_t>(module));
  }else if(library==L"client.dll")get("VClientEntityList003",{3,4});
  else if(library==L"materialsystem.dll")appSystemShift(get("VMaterialSystem080",{0}),name,MaterialSystemVtableLength);
  else if(library==L"vphysics.dll"){
   // GetActiveEnvironmentByIndex and FindCollisionSet, and the collision methods the
   // bridge calls, at the running layout's slots.
   auto physics=get("VPhysics031",{0});auto collision=get("VPhysicsCollision007",{0});
   auto older=olderPhysicsLayout(physics,collision);auto shift=appSystemShift(physics,name,PhysicsVtableLength);
   requireOwnedSlots(physics,name,{11-shift,15-shift});
   requireOwnedSlots(collision,name,{collisionSlot(8,older),collisionSlot(14,older),collisionSlot(16,older),collisionSlot(41,older),collisionSlot(42,older),collisionSlot(43,older),collisionSlot(44,older)});
   requireAbiRva(name,"physics",reinterpret_cast<uintptr_t>((*reinterpret_cast<void***>(physics))[11-shift])-reinterpret_cast<uintptr_t>(module));
  }
 });
}
Json checkCompatibility(bool server){
 Json result={{"ready",true},{"pending",false},{"libraries",Json::array()},{"issues",Json::array()}};
 std::vector<const wchar_t*> names=server?std::vector<const wchar_t*>{L"vphysics.dll"}:std::vector<const wchar_t*>{L"engine.dll",L"client.dll",L"materialsystem.dll",L"shaderapidx9.dll",GetModuleHandleW(L"stdshader_dx9.dll")?L"stdshader_dx9.dll":L"stdshader_dx6.dll"};
 for(auto name:names){if(!GetModuleHandleW(name)){result["ready"]=false;result["pending"]=true;continue;}try{auto p=requireGameBinary(name);checkInterfaces(name);Json library={{"name",utf8(name)},{"sha256",p["observedSHA256"]},{"match",p["match"]}};auto layout=appSystemShifts();if(layout.contains(utf8(name)))library["layout"]=layout[utf8(name)];result["libraries"].push_back(library);}catch(const std::exception& e){result["ready"]=false;result["issues"].push_back({{"code","game_incompatible"},{"component",utf8(name)},{"message",e.what()}});}}
 return result;
}
}
