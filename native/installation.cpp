#include "installation.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include "posix.hpp"
#include <dlfcn.h>
#include <unistd.h>
#endif
#include <stdexcept>

namespace mmd {
#ifndef _WIN32
// The game folder of the running executable: bin/linux64/gmod (x86-64 branch), hl2_linux
// (32-bit default branch), srcds_linux or bin/linux64/srcds_linux64 (dedicated servers).
static fs::path gameRoot(){
 auto folder=posix::executablePath().parent_path();
 if(folder.filename()=="linux64"&&folder.parent_path().filename()=="bin")return folder.parent_path().parent_path();
 if(folder.filename()=="bin")return folder.parent_path();
 return folder;
}
// Every Linux file sits in garrysmod/lua/bin: the modules find the runtime beside them ($ORIGIN).
Json componentIdentity(const char* component,const void* address){
 auto filename=posix::modulePath(address);
 auto expected=gameRoot()/"garrysmod"/"lua"/"bin"/(std::string(component)=="runtime"?MMDHL_RUNTIME_FILE:std::string(component)=="server"?MMDHL_SERVER_FILE:MMDHL_CLIENT_FILE);
 auto bytes=readFile(filename);
 return {{"component",component},{"release",MMDHL_RELEASE},{"build",MMDHL_BUILD_ID},{"installApi",MMDHL_INSTALL_API},{"api",ApiVersion},{"platform",MMDHL_PLATFORM},{"path",filename.string()},{"expectedPath",expected.lexically_normal().string()},{"sha256",hash(bytes)},{"size",bytes.size()}};
}
Json runtimeIdentity(){return componentIdentity("runtime",reinterpret_cast<const void*>(&runtimeIdentity));}
Json workerSelfTest(bool coacd){
 Json result={{"runtime",runtimeIdentity()},{"worker",true},{"coacd",false}};
 if(coacd){
  auto library=posix::executablePath().parent_path()/MMDHL_COACD_FILE;
  auto handle=dlopen(library.c_str(),RTLD_NOW|RTLD_LOCAL);
  if(!handle){auto why=dlerror();result["coacdError"]=std::string("Cannot load CoACD (")+(why?why:"unknown error")+").";}
  else{result["coacd"]=dlsym(handle,"CoACD_run")&&dlsym(handle,"CoACD_freeMeshArray");if(!result["coacd"].get<bool>())result["coacdError"]="CoACD exports do not match the supported ABI";dlclose(handle);}
 }
 return result;
}
Json probeWorker(const fs::path& bin,bool coacd){
 auto report=(posix::tempDirectory()/"mhiXXXXXX").string();int fd=mkstemp(report.data());
 if(fd<0)throw std::runtime_error("Cannot create worker diagnostic report");
 close(fd);struct Cleanup{std::string file;posix::Child child;~Cleanup(){child.kill();unlink(file.c_str());}} cleanup{report,{}};
 auto exe=bin/MMDHL_WORKER_FILE;std::vector<std::string> args{"--installation-test",report};if(coacd)args.push_back("coacd");
 try{cleanup.child=posix::spawn(exe,args,bin);}catch(const std::exception& e){throw std::runtime_error(std::string("Worker startup failed (")+e.what()+"). Check that the native files are complete and executable.");}
 if(!cleanup.child.wait(10000)){cleanup.child.kill();throw std::runtime_error("Worker self-test timed out after 10 seconds");}
 if(cleanup.child.exitCode)throw std::runtime_error("Worker self-test failed (exit "+std::to_string(cleanup.child.exitCode)+"). "+(cleanup.child.exitCode==127?std::string("A shared library it needs is missing, or the worker is not executable."):std::string("Check the native runtime and dependencies.")));
 return readJson(report);
}
#else
Json componentIdentity(const char* component,const void* address){
 HMODULE module=nullptr;wchar_t filename[32768];
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(address),&module)||!GetModuleFileNameW(module,filename,32768))throw std::runtime_error("Cannot locate loaded native component");
 wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);auto executable=fs::path(exe).parent_path();
 auto root=executable.filename()==L"win64"?executable.parent_path().parent_path():executable;
 // The x86-64 branch starts bin/win64/gmod.exe; the main branch (since 2026-09-17) starts
 // gmod_win64.exe in the game folder and loads the engine and this runtime from bin/win64.
 // srcds_win64.exe and the worker load it from beside themselves.
 auto runtimeFolder=lstrcmpiW(fs::path(exe).filename().c_str(),L"gmod_win64.exe")==0?executable/L"bin"/L"win64":executable;
 auto expected=std::string(component)=="runtime"?runtimeFolder/L"mmdhl_runtime_win64.dll":root/L"garrysmod"/L"lua"/L"bin"/(std::string(component)=="server"?L"gmsv_mmdhl_win64.dll":L"gmcl_mmdhl_win64.dll");
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
#endif
}
