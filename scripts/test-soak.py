"""Long in-game placed-model regression, including native-state and frame cost.
Run game-start.ps1 first. Use --WithAddons there to cover real addon interactions.
"""
import argparse, ctypes, ctypes.wintypes as w, json, math, time
from gamectl import execute, ROOT, read

p=argparse.ArgumentParser()
p.add_argument('--asset', required=True)
p.add_argument('--seconds', type=float, default=180)
p.add_argument('--dynamic', action='store_true')
p.add_argument('--output', default='soak-report.json')
args=p.parse_args()

def lua(realm, code):
    result=execute(realm, code, 20)
    if not result['ok']: raise RuntimeError(result)
    return result.get('value')

for realm in ('server','client'):
    lua(realm,f"return mmdhl.native.RequestAsset('{args.asset}')")
    end=time.monotonic()+60
    while not lua(realm,f"return mmdhl.Decode(mmdhl.native.AssetInfo('{args.asset}'))~=nil"):
        if time.monotonic()>end: raise TimeoutError('Asset load')
        time.sleep(.1)
lua('server', "for _,e in ipairs(ents.FindByClass('mmdhl_ragdoll')) do e:Remove() end mmdhl.native.Clear() mmdhl.testErr=nil return true")
lua('client', "mmdhl.soakPerf={frames=0,drawMs=0,draws=0} if not mmdhl.soakDraw then mmdhl.soakDraw=mmdhl.DrawInstance mmdhl.DrawInstance=function(...) local t=SysTime() local a,b=mmdhl.soakDraw(...) mmdhl.soakPerf.drawMs=mmdhl.soakPerf.drawMs+(SysTime()-t)*1000 mmdhl.soakPerf.draws=mmdhl.soakPerf.draws+1 return a,b end end hook.Add('PostRender','MMDHL.Soak',function() mmdhl.soakPerf.frames=mmdhl.soakPerf.frames+1 for _,e in ipairs(ents.FindByClass('mmdhl_ragdoll')) do local b=mmdhl.Decode(mmdhl.native.GetBounds(e:GetInstance())) if b then for k=1,3 do mmdhl.soakPerf.peakExtent=math.max(mmdhl.soakPerf.peakExtent or 0,b.maximum[k]-b.minimum[k]) end end end end) return true")
lua('server',f"local p=Entity(1) local v=p:GetPos()+p:GetForward()*100 mmdhl.Spawn(p,'{args.asset}',{{position={{v.x,v.y,v.z+10}},frozen={str(not args.dynamic).lower()}}},function(e,err) mmdhl.testEnt=e mmdhl.testErr=err end) return true")

class Counters(ctypes.Structure):
    _fields_=[('cb',w.DWORD),('PageFaultCount',w.DWORD)]+[(n,ctypes.c_size_t) for n in ('PeakWorkingSetSize','WorkingSetSize','QuotaPeakPagedPoolUsage','QuotaPagedPoolUsage','QuotaPeakNonPagedPoolUsage','QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage','PrivateUsage')]
k=ctypes.WinDLL('kernel32',use_last_error=True);ps=ctypes.WinDLL('psapi',use_last_error=True)
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.CloseHandle.argtypes=[w.HANDLE];ps.GetProcessMemoryInfo.argtypes=[w.HANDLE,ctypes.POINTER(Counters),w.DWORD]
handle=k.OpenProcess(0x410,False,read(ROOT/'validation/session.json')['pid'])
report={'asset':args.asset,'dynamic':args.dynamic,'samples':[],'passed':False}
start=time.monotonic()
try:
    while time.monotonic()-start<args.seconds:
        time.sleep(2)
        server=lua('server',"local e=mmdhl.testEnt if not IsValid(e) then error(mmdhl.testErr or 'Missing test entity') end return {world=mmdhl.Decode(mmdhl.native.GetDiagnostics()),rig=mmdhl.Decode(mmdhl.native.GetDiagnostics(e:GetInstance())),error=mmdhl.simulationError,bridge=mmdhl.bridge}")
        client=lua('client',"local p=mmdhl.soakPerf local v={frames=p.frames,drawMs=p.drawMs,draws=p.draws,peakExtent=p.peakExtent or 0,error=mmdhl.renderError,cache=mmdhl.native.RenderStats and mmdhl.Decode(mmdhl.native.RenderStats())} mmdhl.soakPerf={frames=0,drawMs=0,draws=0} return v")
        assert not server.get('error') and not client.get('error'),(server.get('error'),client.get('error'))
        assert client['peakExtent']<720,('Transient mesh explosion',client['peakExtent'])
        for b in server['rig']['bodyList']:
            assert all(isinstance(x,(int,float)) and math.isfinite(x) and abs(x)<100000 for x in b['position']),b
        mem=Counters();mem.cb=ctypes.sizeof(mem)
        assert ps.GetProcessMemoryInfo(handle,ctypes.byref(mem),mem.cb),'Game process exited'
        row={'seconds':round(time.monotonic()-start,1),'stepMs':server['world']['stepMs'],'frames':client['frames'],'drawMsPerFrame':client['drawMs']/max(1,client['frames']),'drawsPerFrame':client['draws']/max(1,client['frames']),'privateMB':mem.PrivateUsage/(1024*1024),'mirrors':server['world']['mirrors'],'cache':client.get('cache'),'peakExtent':client['peakExtent']}
        report['samples'].append(row);print(json.dumps(row),flush=True)
        (ROOT/'validation'/args.output).write_text(json.dumps(report,indent=2))
    if not args.dynamic and len(report['samples'])>3:
        samples=report['samples'][2:]
        if samples[0].get('cache'): assert samples[-1]['cache']['builds']==samples[0]['cache']['builds'],'Frozen geometry is being rebuilt'
    report['passed']=True
finally:
    k.CloseHandle(handle)
    (ROOT/'validation'/args.output).write_text(json.dumps(report,indent=2))
print('PASS placed-model soak',flush=True)
