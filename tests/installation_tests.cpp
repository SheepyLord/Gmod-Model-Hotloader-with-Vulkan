#include "compatibility.hpp"
#include "installation.hpp"
#include <windows.h>
#include <iostream>
#include <stdexcept>
using namespace mmd;
static void check(bool value){if(!value)throw std::runtime_error("Installation validation assertion failed");}
int main(int argc,char** argv){try{
 if(argc>=3&&std::string(argv[1])=="--installation-test"){
  if(GetEnvironmentVariableW(L"MMDHL_TEST_WORKER_TIMEOUT",nullptr,0))Sleep(15000);
  return 37;
 }
 wchar_t own[32768];GetModuleFileNameW(nullptr,own,32768);auto b=readFile(own);
 auto original=peEvidence(b);auto header=b;auto nt=*reinterpret_cast<uint32_t*>(header.data()+0x3c);header[nt+8]^=1;
 check(hash(header)!=hash(b));check(matchesEvidence(original,peEvidence(header)));
 auto pe=reinterpret_cast<IMAGE_NT_HEADERS64*>(b.data()+nt);
 auto sections=IMAGE_FIRST_SECTION(pe);
 auto changed=b;changed[sections[0].PointerToRawData]^=1;check(!matchesEvidence(original,peEvidence(changed)));
 // Rebase the on-disk image: every DIR64 pointer moves with ImageBase. Evidence
 // must remain equal, whereas changing a relocation target must not be masked.
 auto rebased=b;auto out=reinterpret_cast<IMAGE_NT_HEADERS64*>(rebased.data()+nt);uint64_t delta=0x10000000;out->OptionalHeader.ImageBase+=delta;
 auto offset=[&](uint32_t rva){for(unsigned i=0;i<pe->FileHeader.NumberOfSections;i++){auto& s=sections[i];if(rva>=s.VirtualAddress&&rva<s.VirtualAddress+s.SizeOfRawData)return s.PointerToRawData+rva-s.VirtualAddress;}throw std::runtime_error("Fixture RVA");};
 auto d=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];size_t at=offset(d.VirtualAddress),end=at+d.Size;size_t first=0;
 while(at<end){auto block=reinterpret_cast<IMAGE_BASE_RELOCATION*>(b.data()+at);for(size_t p=at+8;p<at+block->SizeOfBlock;p+=2){auto item=*reinterpret_cast<uint16_t*>(b.data()+p);if((item>>12)==10){auto location=offset(block->VirtualAddress+(item&4095));*reinterpret_cast<uint64_t*>(rebased.data()+location)+=delta;if(!first)first=location;}}at+=block->SizeOfBlock;}
 check(matchesEvidence(original,peEvidence(rebased)));check(first!=0);rebased[first]^=1;check(!matchesEvidence(original,peEvidence(rebased)));
 bool rejected=false;try{peEvidence(Bytes(10));}catch(...){rejected=true;}check(rejected);
 rejected=false;try{configureCompatibility({{"schema",1},{"family","other"},{"libraries",Json::array()}});}catch(...){rejected=true;}check(rejected);
 check(runtimeIdentity()["installApi"]==1);
 if(argc==2)std::cout<<peEvidence(readFile(wide(argv[1]))).dump()<<'\n';
 else {
  auto base=fs::current_path();auto fixture=base/("installation-worker-fixture-"+std::to_string(GetCurrentProcessId()));
  check(fixture.is_absolute()&&fixture.parent_path()==base&&!fs::exists(fixture));fs::create_directory(fixture);
  struct Clean{fs::path dir,base;~Clean(){SetEnvironmentVariableW(L"MMDHL_TEST_WORKER_TIMEOUT",nullptr);if(dir.parent_path()==base&&dir.filename().wstring().starts_with(L"installation-worker-fixture-")){std::error_code error;fs::remove_all(dir,error);}}} cleanup{fixture,base};
  rejected=false;try{probeWorker(fixture,false);}catch(const std::exception& e){rejected=std::string(e.what()).find("startup failed")!=std::string::npos;}check(rejected);
  fs::copy_file(own,fixture/L"mmdhl_worker.exe");
  // A missing imported runtime must report child startup failure, not hang.
  rejected=false;try{probeWorker(fixture,false);}catch(...){rejected=true;}check(rejected);
  fs::copy_file(wide(runtimeIdentity()["path"].get<std::string>()),fixture/L"mmdhl_runtime_win64.dll");
  rejected=false;try{probeWorker(fixture,false);}catch(const std::exception& e){rejected=std::string(e.what()).find("exit 37")!=std::string::npos;}check(rejected);
  SetEnvironmentVariableW(L"MMDHL_TEST_WORKER_TIMEOUT",L"1");
  rejected=false;try{probeWorker(fixture,false);}catch(const std::exception& e){rejected=std::string(e.what()).find("timed out")!=std::string::npos;}check(rejected);
  std::cout<<"PASS: PE/ABI evidence, native identity, missing worker/runtime, child failure and bounded worker timeout\n";
 }
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
