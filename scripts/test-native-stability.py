"""Full renderer/Source/nanoem acceptance run with an owned-process watchdog."""
import argparse,ctypes,json,pathlib,subprocess,time
from ctypes import wintypes
from gamectl import execute,ROOT,read

class Memory(ctypes.Structure):
 _fields_=[('cb',wintypes.DWORD),('PageFaultCount',wintypes.DWORD)]+[(k,ctypes.c_size_t) for k in ['PeakWorkingSetSize','WorkingSetSize','QuotaPeakPagedPoolUsage','QuotaPagedPoolUsage','QuotaPeakNonPagedPoolUsage','QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage','PrivateUsage']]

def lua(realm,code):
 r=execute(realm,code,45)
 if not r['ok']:raise RuntimeError(r.get('error'))
 return r.get('value')

def main():
 p=argparse.ArgumentParser();p.add_argument('mode',choices=['empty','Xin','Cyrene','Sandrone','Zankou','March7th','Daikokuten','stress','cycles']);p.add_argument('--seconds',type=int);p.add_argument('--models',default='validation/acceptance-models.json');p.add_argument('--collision-mode',type=int,choices=[0,1,2],default=2);p.add_argument('--skip-focus',action='store_true');a=p.parse_args()
 measurement=a.seconds or 10
 duration=measurement+2
 session=read(ROOT/'validation/session.json');directory=pathlib.Path(session['cache'])/'debug'/session['token']
 models=read(ROOT/a.models);names=[] if a.mode=='empty' else list(models) if a.mode in ('stress','cycles') else [a.mode]
 config={'name':'native-'+a.mode.lower(),'mode':a.mode,'assets':[models[n]['asset'] for n in names],'duration':duration,'warmup':2,'cycles':8,'secondaryCollision':a.collision_mode}
 if a.collision_mode:config['name']+='-collision'+str(a.collision_mode)
 # A repeat in the same game must not consume the previous run's finished
 # heartbeat while waiting for its first new sample.
 for realm in ['server','client']:(directory/f'{config["name"]}-{realm}.json').unlink(missing_ok=True)
 if not a.skip_focus:subprocess.run(['powershell','-NoProfile','-File',str(ROOT/'scripts/game-focus.ps1')],check=True)
 source=(ROOT/'tests/game/native-stress.lua').read_text(encoding='utf-8')
 result=lua('server','MMDHL_TEST_CONFIG=util.JSONToTable('+json.dumps(json.dumps(config))+')\n'+source);config['origin']=result['origin']
 lua('client','MMDHL_TEST_CONFIG=util.JSONToTable('+json.dumps(json.dumps(config))+')\n'+source)
 kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.OpenProcess.restype=wintypes.HANDLE
 process=kernel.OpenProcess(0x1000|0x10,False,session['pid']);assert process,'Cannot inspect owned game process'
 psapi=ctypes.WinDLL('psapi');psapi.GetProcessMemoryInfo.argtypes=[wintypes.HANDLE,ctypes.POINTER(Memory),wintypes.DWORD]
 samples=[];started=time.monotonic();last_announcement=0;captured=set();report={'models':names,'session':session,'config':config}
 destination=ROOT/f'validation/{config["name"]}-stability.json'
 try:
  while True:
   elapsed=time.monotonic()-started
   for realm in ['server','client']:
    path=directory/f'{config["name"]}-{realm}.json'
    if elapsed>50 and (not path.exists() or time.time()-path.stat().st_mtime>40):
     report['watchdogFailure']=realm+' heartbeat stopped'
     log=pathlib.Path(session['gameRoot'])/'garrysmod/console.log'
     if log.exists():
      with log.open('rb') as f:f.seek(max(0,log.stat().st_size-32000));report['consoleTail']=f.read().decode('utf-8',errors='replace')
     destination.write_text(json.dumps(report,indent=2),encoding='utf-8')
     # capture-dump independently verifies the same owned PID/token.
     try:subprocess.run(['python',str(ROOT/'scripts/capture-dump.py')],timeout=30,check=False)
     except subprocess.TimeoutExpired:report['dumpFailure']='Owned-process dump timed out'
     finally:
      # Stop still runs if dump collection fails. It independently rechecks the
      # PID, executable command line and session token before terminating anything.
      subprocess.run(['powershell','-NoProfile','-File',str(ROOT/'scripts/game-stop.ps1')],check=True)
     raise RuntimeError(report['watchdogFailure'])
   memory=Memory();memory.cb=ctypes.sizeof(memory)
   if not psapi.GetProcessMemoryInfo(process,ctypes.byref(memory),memory.cb):raise RuntimeError('Owned game exited')
   client=read(directory/f'{config["name"]}-client.json');server=read(directory/f'{config["name"]}-server.json')
   samples.append({'elapsed':elapsed,'workingMB':memory.WorkingSetSize/1048576,'privateMB':memory.PrivateUsage/1048576})
   if elapsed>2 and 'start' not in captured:
    lua('client',f"RunConsoleCommand('mmdhl_debug_capture','{config['name']}-start') return true");captured.add('start')
   if elapsed>duration-4 and 'end' not in captured:
    lua('client',f"RunConsoleCommand('mmdhl_debug_capture','{config['name']}-end') return true");captured.add('end')
   report.update(client=client,server=server,memory=samples)
   destination.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
   if client and client.get('errors') or server and server.get('errors'):raise RuntimeError('Probe reported runtime failures')
   if elapsed-last_announcement>30:
    print(f"{a.mode}: {elapsed:.0f}s, p95={client and client.get('frames',{}).get('p95')}, private={samples[-1]['privateMB']:.1f} MB",flush=True);last_announcement=elapsed
   if client and client.get('finished'):break
   if elapsed>duration+50:raise RuntimeError('Probe did not finish')
   time.sleep(2)
  assert not client['errors'] and not server['errors'],'Probe reported runtime failures'
  report['benchmarkFrames']=client['foregroundFrames']
  report['performanceValid']=client['foregroundSeconds']>=measurement*.8 and client['foregroundFrames'].get('samples',0)>=200
  # Background frames remain in client.frames. Performance acceptance uses
  # only sustained foreground samples, with a minimum duration and sample count.
  if a.mode not in ('stress','cycles'):assert report['performanceValid'],'Insufficient sustained foreground samples for a performance result'
  if a.mode=='cycles':assert server['cycles']==config['cycles']
  report['functionalPassed']=True
  report['performanceTargetPassed']=len(names)!=1 or report['benchmarkFrames']['p95']<=16.7
  if len(names)==1:assert report['benchmarkFrames']['p95']<=16.7,'Single-character performance target failed'
  report['passed']=True;destination.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
  print('PASS '+a.mode,json.dumps(report['benchmarkFrames'] if report['performanceValid'] else client['frames']),flush=True)
 except Exception as error:
  report.update(passed=False,failure=str(error))
  destination.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
  raise
 finally:
  kernel.CloseHandle.argtypes=[wintypes.HANDLE];kernel.CloseHandle(process)
  try:
   lua('server',"hook.Remove('Tick','MMDHL.StressMotion') timer.Remove('MMDHL.StressCycles') timer.Remove('MMDHL.StressHeartbeat') for _,e in ipairs(mmdhl.Entities()) do e:Remove() end return true")
   lua('client',"for event,name in pairs({CalcView='MMDHL.StressCamera',HUDShouldDraw='MMDHL.StressHUD',PreDrawViewModel='MMDHL.StressViewModel',PostRender='MMDHL.StressFrames',HUDPaint='MMDHL.StressOverlay'}) do hook.Remove(event,name) end timer.Remove('MMDHL.StressClientHeartbeat') return true")
  except Exception:pass
if __name__=='__main__':main()
