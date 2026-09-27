"""Capture a minidump of this project's owned test game for WinDbg + release PDBs."""
import ctypes,ctypes.wintypes as w,msvcrt,time,subprocess
from gamectl import ROOT,read
s=read(ROOT/'validation/session.json');pid=s['pid'];k=ctypes.WinDLL('kernel32',use_last_error=True);d=ctypes.WinDLL('Dbghelp',use_last_error=True)
command=subprocess.run(['powershell','-NoProfile','-Command',f'(Get-CimInstance Win32_Process -Filter "ProcessId={int(pid)}").CommandLine'],capture_output=True,text=True,check=True).stdout
if s['token'] not in command:raise SystemExit('PID is not the owned test game; refusing to capture.')
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.CloseHandle.argtypes=[w.HANDLE]
d.MiniDumpWriteDump.argtypes=[w.HANDLE,w.DWORD,w.HANDLE,w.DWORD,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p];d.MiniDumpWriteDump.restype=w.BOOL
h=k.OpenProcess(0x410,False,pid)
if not h:raise ctypes.WinError(ctypes.get_last_error())
out=ROOT/'validation'/f'game-{pid}-{int(time.time())}.dmp'
try:
 with out.open('wb') as f:
  if not d.MiniDumpWriteDump(h,pid,msvcrt.get_osfhandle(f.fileno()),0x1000|0x20,None,None,None):raise ctypes.WinError(ctypes.get_last_error())
finally:k.CloseHandle(h)
print(out)
