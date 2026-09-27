// Diagnostic sampling profiler for the game's main thread (client module only).
// A background thread suspends the main thread about once per millisecond,
// records its instruction pointer and scans a copy of its stack for return
// addresses, then resumes it. Nothing is allocated or locked while the main
// thread is suspended (it may hold the heap or loader lock). Afterwards samples
// are attributed to modules: "self" (where the thread was executing) and
// "inclusive" (every module found on the stack), and, for our own modules,
// to functions through their PDBs.
#pragma once
#include <windows.h>
#include <timeapi.h>
#include <dbghelp.h>
#include <psapi.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <string>
#include <thread>
#include <vector>
namespace mmd {
class MainThreadSampler {
 struct Module {uintptr_t begin,end;std::string name;};
 std::vector<Module> modules;
 HANDLE target=nullptr;ULONG_PTR stackLow=0,stackHigh=0;
 std::thread worker;std::atomic<bool> finished{false},running{false};
 static constexpr size_t MaxSamples=60000,StackBytes=16384,MaxModules=512;
 std::vector<uintptr_t> rips;std::vector<uint32_t> selfCounts,inclusiveCounts;std::vector<std::vector<uint32_t>> callers; // callers[self][first foreign module]
 std::vector<unsigned char> stackCopy;
 // First distinct modules per sample (self, then stack order), for waiter attribution.
 static constexpr size_t ChainLength=8;std::vector<std::array<int16_t,ChainLength>> chains;
 // First return address (or the RIP) inside our own modules, per sample.
 std::vector<uintptr_t> ourFrames;std::vector<bool> ours;
 int find(uintptr_t address)const{
  auto it=std::upper_bound(modules.begin(),modules.end(),address,[](uintptr_t a,const Module& m){return a<m.begin;});
  if(it==modules.begin())return -1;--it;return address<it->end?int(it-modules.begin()):-1;
 }
 void snapshotModules(){
  modules.clear();HMODULE handles[1024];DWORD needed=0;auto process=GetCurrentProcess();
  if(!EnumProcessModules(process,handles,sizeof(handles),&needed))return;
  for(DWORD i=0;i<std::min<DWORD>(needed/sizeof(HMODULE),1024)&&modules.size()<MaxModules;i++){MODULEINFO info{};char name[MAX_PATH]{};
   if(GetModuleInformation(process,handles[i],&info,sizeof(info))&&GetModuleBaseNameA(process,handles[i],name,MAX_PATH))
    modules.push_back({reinterpret_cast<uintptr_t>(info.lpBaseOfDll),reinterpret_cast<uintptr_t>(info.lpBaseOfDll)+info.SizeOfImage,name});}
  std::sort(modules.begin(),modules.end(),[](const Module& a,const Module& b){return a.begin<b.begin;});
 }
 void run(double milliseconds){
  timeBeginPeriod(1);auto deadline=GetTickCount64()+ULONGLONG(milliseconds);
  bool seen[MaxModules];
  while(GetTickCount64()<deadline&&rips.size()<MaxSamples){
   if(SuspendThread(target)==DWORD(-1))break;
   CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;bool ok=GetThreadContext(target,&context)!=0;size_t copied=0;
   if(ok&&context.Rsp>=stackLow&&context.Rsp<stackHigh){copied=std::min<size_t>(StackBytes,stackHigh-context.Rsp);memcpy(stackCopy.data(),reinterpret_cast<const void*>(context.Rsp),copied);}
   ResumeThread(target);
   if(!ok){Sleep(1);continue;}
   uintptr_t rip=context.Rip;rips.push_back(rip);int self=find(rip);
   std::fill(std::begin(seen),std::end(seen),false);int foreign=-1;std::array<int16_t,ChainLength> chain;chain.fill(-1);size_t length=0;
   if(self>=0){seen[self]=true;chain[length++]=int16_t(self);}
   uintptr_t our=self>=0&&ours[self]?rip:0;
   for(size_t offset=0;offset+8<=copied;offset+=8){uintptr_t value;memcpy(&value,stackCopy.data()+offset,8);int m=find(value);if(m<0)continue;if(!our&&ours[m])our=value;if(foreign<0&&m!=self)foreign=m;if(!seen[m]&&length<ChainLength)chain[length++]=int16_t(m);seen[m]=true;}
   if(ourFrames.size()<ourFrames.capacity())ourFrames.push_back(our);
   if(chains.size()<chains.capacity())chains.push_back(chain);
   for(size_t m=0;m<modules.size();m++)if(seen[m])inclusiveCounts[m]++;
   if(self>=0){selfCounts[self]++;if(foreign>=0)callers[self][foreign]++;}
   Sleep(1);
  }
  timeEndPeriod(1);finished=true;
 }
public:
 ~MainThreadSampler(){if(worker.joinable())worker.join();if(target)CloseHandle(target);}
 bool busy()const{return running&&!finished;}
 // Call on the thread to be sampled.
 void start(double milliseconds){
  if(busy())throw std::runtime_error("Main-thread sampling is already running");
  if(worker.joinable())worker.join();if(target){CloseHandle(target);target=nullptr;}
  if(!DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&target,THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,0))throw std::runtime_error("Cannot open the main thread for sampling");
  GetCurrentThreadStackLimits(&stackLow,&stackHigh);snapshotModules();
  rips.clear();rips.reserve(MaxSamples);chains.clear();chains.reserve(MaxSamples);ourFrames.clear();ourFrames.reserve(MaxSamples);
  ours.assign(modules.size(),false);for(size_t m=0;m<modules.size();m++)ours[m]=modules[m].name.rfind("mmdhl",0)==0||modules[m].name.rfind("gmcl_mmdhl",0)==0;stackCopy.assign(StackBytes,0);selfCounts.assign(modules.size(),0);inclusiveCounts.assign(modules.size(),0);callers.assign(modules.size(),std::vector<uint32_t>(modules.size(),0));
  finished=false;running=true;worker=std::thread([this,milliseconds]{run(milliseconds);});
 }
 // JSON report once finished; null while sampling.
 nlohmann::json report(const std::wstring& symbolPath){
  if(!running)return {{"error","not started"}};if(!finished)return nullptr;if(worker.joinable())worker.join();
  nlohmann::json out={{"samples",rips.size()}};auto total=double(std::max<size_t>(1,rips.size()));
  std::vector<size_t> order(modules.size());for(size_t i=0;i<order.size();i++)order[i]=i;
  std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return selfCounts[a]>selfCounts[b];});
  nlohmann::json self=nlohmann::json::array();
  for(auto m:order){if(!selfCounts[m])break;nlohmann::json row={{"module",modules[m].name},{"percent",100*selfCounts[m]/total}};
   std::vector<std::pair<uint32_t,size_t>> by;for(size_t k=0;k<modules.size();k++)if(callers[m][k])by.push_back({callers[m][k],k});std::sort(by.rbegin(),by.rend());
   nlohmann::json via=nlohmann::json::object();for(size_t k=0;k<std::min<size_t>(4,by.size());k++)via[modules[by[k].second].name]=100*by[k].first/total;row["calledVia"]=via;self.push_back(row);}
  out["self"]=self;
  std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return inclusiveCounts[a]>inclusiveCounts[b];});
  nlohmann::json inclusive=nlohmann::json::array();for(auto m:order){if(!inclusiveCounts[m]||inclusive.size()>=25)break;inclusive.push_back({{"module",modules[m].name},{"percent",100*inclusiveCounts[m]/total}});}
  out["inclusive"]=inclusive;
  // Effective owner: skip system/runtime frames (waits, CRT, synchronization) to the first module doing the work or the waiting.
  auto system=[&](int m){if(m<0)return true;const auto& n=modules[m].name;for(auto s:{"ntdll.dll","KERNELBASE.dll","KERNEL32.DLL","kernel32.dll","MSVCP140.dll","VCRUNTIME140.dll","VCRUNTIME140_1.dll","ucrtbase.dll","win32u.dll"})if(_stricmp(n.c_str(),s)==0)return true;return false;};
  std::map<std::string,uint32_t> effective,waits;
  for(auto& chain:chains){int first=-1,second=-1;for(auto m:chain){if(m<0)break;if(system(m))continue;if(first<0)first=m;else if(m!=first){second=m;break;}}
   std::string key=first<0?"(system only)":modules[first].name+(second>=0?" <- "+modules[second].name:"");effective[key]++;
   if(chain[0]>=0&&system(chain[0]))waits[key]++;}
  auto ranked=[&](const std::map<std::string,uint32_t>& counts){std::vector<std::pair<uint32_t,std::string>> r;for(auto& [k,v]:counts)r.push_back({v,k});std::sort(r.rbegin(),r.rend());nlohmann::json j=nlohmann::json::array();for(size_t i=0;i<std::min<size_t>(25,r.size());i++)j.push_back({{"where",r[i].second},{"percent",100*r[i].first/total}});return j;};
  out["effective"]=ranked(effective);out["systemTimeOwners"]=ranked(waits);
  // Function-level attribution inside our own modules (their PDBs sit next to the build).
  auto process=GetCurrentProcess();std::string path(symbolPath.begin(),symbolPath.end());
  SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS);bool symbols=SymInitialize(process,path.c_str(),TRUE)!=0;
  std::map<std::string,uint32_t> functions;
  for(auto rip:rips){int m=find(rip);if(m<0)continue;const auto& name=modules[m].name;std::string key=name;
   if(symbols&&(name.rfind("mmdhl",0)==0||name.rfind("gmcl_mmdhl",0)==0)){alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO)+256];auto symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=255;DWORD64 displacement=0;if(SymFromAddr(process,rip,&displacement,symbol))key=name+"!"+symbol->Name;}
   functions[key]++;}
  // Where our code was on the stack, split by whether the thread itself was in system code (waiting/locking) or not.
  std::map<std::string,uint32_t> ourWaiting,ourWorking;
  for(size_t i=0;i<ourFrames.size()&&i<chains.size();i++){if(!ourFrames[i])continue;std::string key;
   int m=find(ourFrames[i]);key=m>=0?modules[m].name:"?";
   if(symbols){alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO)+256];auto symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=255;DWORD64 displacement=0;if(SymFromAddr(process,ourFrames[i],&displacement,symbol))key+="!"+std::string(symbol->Name);}
   int self=chains[i][0];bool waiting=self<0||system(self)||!ours[self];(waiting?ourWaiting:ourWorking)[key]++;}
  if(symbols)SymCleanup(process);
  out["ourCodeBlockedOrCalling"]=ranked(ourWaiting);out["ourCodeExecuting"]=ranked(ourWorking);
  out["top"]=ranked(functions);running=false;return out;
 }
};
}
