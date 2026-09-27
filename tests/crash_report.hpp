// Symbolized stack trace for unhandled native exceptions in the standalone tools.
// Release builds carry PDBs next to their binaries (CMake /Zi + /DEBUG:FULL).
#pragma once
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
namespace mmd::test {
inline LONG WINAPI reportCrash(EXCEPTION_POINTERS* info){
 auto process=GetCurrentProcess(),thread=GetCurrentThread();
 SymSetOptions(SYMOPT_LOAD_LINES|SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS);SymInitialize(process,nullptr,TRUE);
 std::fprintf(stderr,"\nFATAL native exception 0x%08lx at %p\n",info->ExceptionRecord->ExceptionCode,info->ExceptionRecord->ExceptionAddress);
 CONTEXT context=*info->ContextRecord;STACKFRAME64 frame{};frame.AddrPC.Offset=context.Rip;frame.AddrFrame.Offset=context.Rbp;frame.AddrStack.Offset=context.Rsp;frame.AddrPC.Mode=frame.AddrFrame.Mode=frame.AddrStack.Mode=AddrModeFlat;
 alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO)+512];auto symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);
 for(int depth=0;depth<48;depth++){
  if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr)||!frame.AddrPC.Offset)break;
  symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=511;DWORD64 displacement=0;
  IMAGEHLP_LINE64 line{};line.SizeOfStruct=sizeof(line);DWORD lineDisplacement=0;
  const char* name=SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol)?symbol->Name:"?";
  if(SymGetLineFromAddr64(process,frame.AddrPC.Offset,&lineDisplacement,&line))std::fprintf(stderr,"  #%02d %s+0x%llx  %s:%lu\n",depth,name,static_cast<unsigned long long>(displacement),line.FileName,line.LineNumber);
  else std::fprintf(stderr,"  #%02d %s+0x%llx\n",depth,name,static_cast<unsigned long long>(displacement));
 }
 std::fflush(stderr);return EXCEPTION_EXECUTE_HANDLER;
}
inline void installCrashReport(){SetUnhandledExceptionFilter(reportCrash);}
}
