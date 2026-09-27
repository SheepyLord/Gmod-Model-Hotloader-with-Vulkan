#include "compute_solver.hpp"
#include "vulkan_solver.hpp"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <vector>
namespace mmd {
namespace {
// Driver startup runs first in a disposable worker, so a driver that hangs or
// crashes while creating a context cannot take the game down with it.
nlohmann::json probeWorker(const std::string& label,const wchar_t* flag,const wchar_t* disable,const std::string& fallback){
 auto unavailable=[&](const std::string& reason){return nlohmann::json{{"available",false},{"error",reason},{"api",label},{"experimental",true},{"validated",false}};};
 if(GetEnvironmentVariableW(disable,nullptr,0))return unavailable(label+" disabled by environment; using the "+fallback+" backend.");
 HMODULE module=nullptr;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&probeWorker),&module))return unavailable("Cannot locate the "+label+" probe worker.");
 wchar_t modulePath[32768]={},tempDirectory[MAX_PATH]={},tempFile[MAX_PATH]={};
 if(!GetModuleFileNameW(module,modulePath,32768)||!GetTempPathW(MAX_PATH,tempDirectory)||!GetTempFileNameW(tempDirectory,L"mmd",0,tempFile))return unavailable("Cannot create the "+label+" probe report.");
 struct Cleanup {wchar_t* file;HANDLE process=nullptr,thread=nullptr,job=nullptr;~Cleanup(){if(job)CloseHandle(job);if(thread)CloseHandle(thread);if(process)CloseHandle(process);DeleteFileW(file);}} cleanup{tempFile};
 auto worker=std::filesystem::path(modulePath).parent_path()/L"mmdhl_worker.exe";
 // GMod loads the shared runtime beside gmod.exe, while its worker lives
 // beside the Lua modules. Standalone validation keeps all binaries together.
 if(!std::filesystem::is_regular_file(worker))worker=std::filesystem::path(modulePath).parent_path().parent_path().parent_path()/L"garrysmod"/L"lua"/L"bin"/L"mmdhl_worker.exe";
 if(!std::filesystem::is_regular_file(worker))return unavailable(label+" probe worker is missing; reinstall the matching native binaries.");
 cleanup.job=CreateJobObjectW(nullptr,nullptr);if(!cleanup.job)return unavailable("Cannot create the "+label+" probe job.");
 JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
 if(!SetInformationJobObject(cleanup.job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return unavailable("Cannot configure the "+label+" probe job.");
 std::wstring command=L"\""+worker.wstring()+L"\" "+flag+L" \""+tempFile+L"\"";
 STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
 if(!CreateProcessW(worker.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,worker.parent_path().c_str(),&startup,&process))return unavailable("Cannot launch the "+label+" probe worker.");
 cleanup.process=process.hProcess;cleanup.thread=process.hThread;
 if(!AssignProcessToJobObject(cleanup.job,process.hProcess)){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,1000);return unavailable("Cannot contain the "+label+" probe worker.");}
 ResumeThread(process.hThread);
 if(WaitForSingleObject(process.hProcess,15000)!=WAIT_OBJECT_0){TerminateJobObject(cleanup.job,1);WaitForSingleObject(process.hProcess,1000);return unavailable(label+" startup probe timed out; "+fallback+" retained.");}
 DWORD code=1;GetExitCodeProcess(process.hProcess,&code);
 try{auto result=nlohmann::json::parse(std::ifstream(std::filesystem::path(tempFile)));if(code==0&&result.value("available",false))return result;if(result.contains("error"))return unavailable(result["error"].get<std::string>());}catch(const std::exception&){}
 return unavailable(label+" startup probe failed or crashed (exit "+std::to_string(code)+").");
}
}
nlohmann::json probeOpenClWorker(){return probeWorker("OpenCL 1.2",L"--probe-compute",L"MMDHL_DISABLE_OPENCL","reference CPU");}
nlohmann::json probeVulkanWorker(){return probeWorker("Vulkan",L"--probe-vulkan",L"MMDHL_DISABLE_VULKAN","Claude CPU v2");}
}
