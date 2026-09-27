#include "installation.hpp"
#include <windows.h>
#include <stdexcept>

namespace mmd {
Json componentIdentity(const char* component,const void* address){
 HMODULE module=nullptr;wchar_t filename[32768];
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(address),&module)||!GetModuleFileNameW(module,filename,32768))throw std::runtime_error("Cannot locate loaded native component");
 wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);auto executable=fs::path(exe).parent_path();
 auto root=executable.filename()==L"win64"?executable.parent_path().parent_path():executable;
 auto expected=std::string(component)=="runtime"?executable/L"mmdhl_runtime_win64.dll":root/L"garrysmod"/L"lua"/L"bin"/(std::string(component)=="server"?L"gmsv_mmdhl_win64.dll":L"gmcl_mmdhl_win64.dll");
 auto bytes=readFile(filename);
 return {{"component",component},{"release",MMDHL_RELEASE},{"build",MMDHL_BUILD_ID},{"installApi",MMDHL_INSTALL_API},{"api",ApiVersion},{"platform","win64"},{"path",utf8(filename)},{"expectedPath",utf8(expected.wstring())},{"sha256",hash(bytes)},{"size",bytes.size()}};
}
Json runtimeIdentity(){return componentIdentity("runtime",reinterpret_cast<const void*>(&runtimeIdentity));}
Json workerSelfTest(bool coacd){
 Json result={{"runtime",runtimeIdentity()},{"worker",true},{"coacd",false}};
 if(coacd){
  wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);auto dll=fs::path(path).parent_path()/L"lib_coacd.dll";
  auto library=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if(!library)result["coacdError"]="Cannot load CoACD (Windows error "+std::to_string(GetLastError())+"). Install the Microsoft Visual C++ x64 Redistributable, including VCOMP140.";
  else{result["coacd"]=GetProcAddress(library,"CoACD_run")&&GetProcAddress(library,"CoACD_freeMeshArray");if(!result["coacd"].get<bool>())result["coacdError"]="CoACD exports do not match the supported ABI";FreeLibrary(library);}
 }
 return result;
}
Json probeWorker(const fs::path& bin,bool coacd){
 wchar_t temp[MAX_PATH],report[MAX_PATH];if(!GetTempPathW(MAX_PATH,temp)||!GetTempFileNameW(temp,L"mhi",0,report))throw std::runtime_error("Cannot create worker diagnostic report");
 struct Cleanup{wchar_t* path;HANDLE job=nullptr,process=nullptr,thread=nullptr;~Cleanup(){if(job)CloseHandle(job);if(thread)CloseHandle(thread);if(process)CloseHandle(process);DeleteFileW(path);}} cleanup{report};
 auto exe=bin/L"mmdhl_worker.exe";std::wstring command=L"\""+exe.wstring()+L"\" --installation-test \""+report+L"\""+(coacd?L" coacd":L"");
 cleanup.job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
 if(!cleanup.job||!SetInformationJobObject(cleanup.job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("Cannot contain worker diagnostic process");
 STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
 if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,bin.c_str(),&start,&process))throw std::runtime_error("Worker startup failed (Windows error "+std::to_string(GetLastError())+"). Check native dependencies and the Microsoft Visual C++ x64 Redistributable.");
 cleanup.process=process.hProcess;cleanup.thread=process.hThread;
 if(!AssignProcessToJobObject(cleanup.job,process.hProcess)){TerminateProcess(process.hProcess,1);throw std::runtime_error("Cannot contain worker diagnostic process");}
 ResumeThread(process.hThread);
 if(WaitForSingleObject(process.hProcess,10000)!=WAIT_OBJECT_0){TerminateJobObject(cleanup.job,1);WaitForSingleObject(process.hProcess,1000);throw std::runtime_error("Worker self-test timed out after 10 seconds");}
 DWORD exit=1;GetExitCodeProcess(process.hProcess,&exit);if(exit)throw std::runtime_error("Worker self-test failed (exit "+std::to_string(exit)+"). Check the native runtime and dependencies.");
 return readJson(report);
}
}
