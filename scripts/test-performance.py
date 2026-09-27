"""Ten distinct full-rig characters; ten seconds after warm-up, all in view."""
import argparse,ctypes,json,msvcrt,os,pathlib,time,subprocess
from ctypes import wintypes

class Memory(ctypes.Structure):
    _fields_=[('cb',wintypes.DWORD),('PageFaultCount',wintypes.DWORD)]+[(k,ctypes.c_size_t) for k in ['PeakWorkingSetSize','WorkingSetSize','QuotaPeakPagedPoolUsage','QuotaPagedPoolUsage','QuotaPeakNonPagedPoolUsage','QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage','PrivateUsage']]
from gamectl import ROOT,read,execute
# Animated stock citizens as render load: 8 per row, 60 units apart, rows 70
# apart from 330 units behind the stress origin, facing the stress camera.
CITIZENS='''local o=Vector(unpack(MMDHL_STRESS.origin))
for _,e in ipairs(MMDHL_CITIZENS or {}) do if IsValid(e) then e:Remove() end end MMDHL_CITIZENS={}
local models={} for _,m in ipairs({'models/humans/group01/male_02.mdl','models/humans/group01/male_04.mdl','models/humans/group01/male_07.mdl','models/humans/group01/female_01.mdl','models/humans/group01/female_03.mdl','models/humans/group02/male_08.mdl','models/humans/group03/male_05.mdl','models/humans/group03/female_06.mdl'}) do if util.IsValidModel(m) then models[#models+1]=m end end
local animated=0
for i=0,COUNT-1 do
 local pos=o+Vector(330+math.floor(i/8)*70,(i%8-3.5)*60,0)
 local tr=util.TraceLine({start=pos+Vector(0,0,256),endpos=pos-Vector(0,0,1024),mask=MASK_SOLID_BRUSHONLY})
 local e=ents.Create('prop_dynamic') e:SetModel(models[i%#models+1])
 local sequence=e:LookupSequence('walk_all') if not sequence or sequence<0 then sequence=e:SelectWeightedSequence(ACT_WALK) end
 e:SetKeyValue('DefaultAnim',e:GetSequenceName(sequence)) e:SetKeyValue('solid','0')
 e:SetPos(tr.HitPos) e:SetAngles(Angle(0,180+((i*23)%40-20),0)) e:Spawn() e:Activate()
 if e:GetSequence()>0 then animated=animated+1 end
 MMDHL_CITIZENS[#MMDHL_CITIZENS+1]=e
end
return {spawned=#MMDHL_CITIZENS,animated=animated,models=#models}'''
def cpu_seconds(kernel,process):
    times=[wintypes.FILETIME() for _ in range(4)]
    if not kernel.GetProcessTimes(process,*[ctypes.byref(t) for t in times]):return None
    return sum((t.dwHighDateTime<<32|t.dwLowDateTime)/1e7 for t in times[2:])

def lua(realm,code):
    r=execute(realm,code,90)
    if not r['ok']:raise RuntimeError(r.get('error'))
    return r.get('value')

def main():
    p=argparse.ArgumentParser();p.add_argument('label');p.add_argument('--count',type=int,choices=[0,1,3,8,10],default=10);p.add_argument('--mode',type=int,choices=[0,1,2],default=2);p.add_argument('--workers',type=int,default=0)
    p.add_argument('--update-lod',action='store_true',help='keep update-rate LOD on (distant characters take poses at 10-30 Hz)')
    p.add_argument('--backend',choices=['reference','cpu_mt','gpu_opencl','cpu_mt_v2','gpu_vulkan'],default='reference');p.add_argument('--iterations',type=int,default=10,help='mmdhl_secondary_iterations for the run (restored afterwards)');p.add_argument('--vulkan-order',choices=['ordered','colored'],default='ordered');p.add_argument('--broadphase',choices=['auto','dbvt','dbvt-fast','sap'],default='auto',help='auto: sweep-and-prune for cpu_mt_v2, ordered DBVT otherwise');p.add_argument('--layout',choices=['spaced','clustered'],default='spaced');p.add_argument('--contacts',action='store_true');p.add_argument('--motion',choices=['standing','moving'],default='moving');p.add_argument('--wait',type=float,default=0,help='asynchronous wait budget in ms');p.add_argument('--suspend',action='store_true',help='diagnostic: keep the scene but skip native prepare, draw and shadow work');p.add_argument('--midphase',type=int,choices=[0,1],default=1,help='diagnostic A/B: 0 disables the exact narrowphase mid-phase gate')
    p.add_argument('--citizens',type=int,default=0,help='also spawn this many animated stock citizens (prop_dynamic walk cycle) behind the characters, as render load')
    p.add_argument('--force-immediate',type=int,choices=[0,1],help='mmdhl_force_immediate_rendering for the run: 1 switches Source to single-threaded rendering while characters exist (old behaviour), 0 keeps mat_queue_mode');a=p.parse_args()
    if not a.label.replace('-','').replace('_','').isalnum():p.error('Use letters, digits, hyphens or underscores for the label')
    capacity=lua('client','return mmdhl.native.GetWorkerCapabilities and mmdhl.Decode(mmdhl.native.GetWorkerCapabilities()).maximum or 32')
    if a.workers<0 or a.workers>capacity:p.error(f'Workers must be 0 through {capacity}')
    if a.contacts and (a.mode!=2 or a.count==0):p.error('Contact comparisons require characters and collision mode 2')
    lock=(ROOT/'validation/performance.lock').open('a+b');lock.seek(0)
    try:msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
    except OSError:raise RuntimeError('Another performance scenario is still running')
    session=read(ROOT/'validation/session.json');cache=pathlib.Path(session['cache'])
    wanted=['xin','星穹铁道—昔涟','Marionette','zankou','星穹铁道—三月七','大国主 R18','ホタル R18','克罗瑞娜 R18','Daniya','初雪']
    inventory={}
    for f in sorted((cache/'assets').glob('*/manifest.json')):
        m=read(f)
        if m and m.get('name') in wanted:inventory[m['name']]=dict(m,asset=f.parent.name)
    selected=[inventory[n] for n in wanted[:a.count]] if a.count!=8 else []
    if a.count==8:
        manifest=read(ROOT/'tests/v2-assets.json')
        if not manifest:raise RuntimeError('The pinned eight-character manifest is missing')
        # A re-imported model keeps its source hash under a new asset id; the pinned set is the set of source models.
        byHash={}
        for f in (cache/'assets').glob('*/manifest.json'):
            cached=read(f)
            if cached:byHash.setdefault(cached.get('sourceHash'),[]).append((f.parent.name,cached))
        for m in manifest:
            cached=read(cache/'assets'/m['asset']/'manifest.json')
            if cached and cached['sourceHash']==m['sourceHash']:selected.append(dict(cached,asset=m['asset']));continue
            candidates=byHash.get(m['sourceHash']) or []
            assert candidates,f"Pinned eight-character model {m['label']} ({m['sourceHash'][:12]}) is not in the cache"
            selected.append(dict(candidates[0][1],asset=candidates[0][0]))
    assert len({m['sourceHash'] for m in selected})==a.count
    config=dict(name=a.label,mode='stress',grid=True,cameraCount=8 if a.count in (0,8) else a.count,layout=a.layout,motion=a.motion,contacts=a.contacts,assets=[m['asset'] for m in selected],warmup=8,duration=18,secondaryCollision=a.mode,broadphase=a.broadphase,secondaryBackend=a.backend,waitBudget=a.wait,suspend=a.suspend,midphase=a.midphase)
    directory=cache/'debug'/session['token'];dest=ROOT/'validation'/f'{a.label}.json'
    for realm in ['server','client']:(directory/f'{a.label}-{realm}.json').unlink(missing_ok=True)
    source=(ROOT/'tests/game/native-stress.lua').read_text(encoding='utf-8')
    report=dict(session=session,memory=[],config=config,models=[{k:m[k] for k in ['name','asset','sourceHash','vertices','rigidBodies','joints']} for m in selected])
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    process=kernel.OpenProcess(0x1000|0x10,False,session['pid']);assert process,'Owned game not running'
    psapi=ctypes.WinDLL('psapi');psapi.GetProcessMemoryInfo.argtypes=[wintypes.HANDLE,ctypes.POINTER(Memory),wintypes.DWORD]
    # Physics LOD and update-rate LOD are off unless requested: every character is presented every frame.
    settings=['mmdhl_secondary_backend','mmdhl_secondary_collision','mmdhl_workers','mmdhl_secondary_wait_ms','mmdhl_lod_enabled','mmdhl_update_lod','mmdhl_secondary_iterations']+(['mmdhl_force_immediate_rendering'] if a.force_immediate is not None else [])
    saved=lua('client','local t={} for _,n in ipairs({'+','.join(json.dumps(n) for n in settings)+'}) do local c=GetConVar(n) if c then t[n]=c:GetString() end end return t')
    report['savedSettings']=saved
    try:
        overrides=dict(zip(settings,[a.backend,str(a.mode),str(a.workers),str(a.wait),'0','1' if a.update_lod else '0',str(a.iterations)]+([str(a.force_immediate)] if a.force_immediate is not None else [])))
        lua('client','for k,v in pairs(util.JSONToTable('+json.dumps(json.dumps(overrides))+')) do local c=GetConVar(k) if c then c:SetString(v) end end return true')
        time.sleep(.35) # Let the normal client settings callbacks reach a safe worker boundary.
        lua('client',f'mmdhl.native.SetWorkers({a.workers}) mmdhl.native.SetSecondaryWaitBudget({a.wait}) return true')
        if a.backend=='gpu_vulkan':lua('client',f'mmdhl.native.SetVulkanSolverOrdering({json.dumps(a.vulkan_order)}) return true')
        lua('client',f'if mmdhl.native.SetSecondaryMidphase then mmdhl.native.SetSecondaryMidphase({"true" if a.midphase else "false"}) end return true')
        previousBroadphase=lua('client',f'local old=mmdhl.Decode(mmdhl.native.GetCapabilities()).secondaryBroadphaseDefault assert(mmdhl.native.SetSecondaryBroadphase({json.dumps(a.broadphase)})=={json.dumps(a.broadphase)}) return old')
        config['origin']=lua('server','MMDHL_TEST_CONFIG=util.JSONToTable('+json.dumps(json.dumps(config))+')\n'+source)['origin']
        if a.citizens>0:
            report['citizens']=lua('server',CITIZENS.replace('COUNT',str(a.citizens)))
        lua('client','MMDHL_TEST_CONFIG=util.JSONToTable('+json.dumps(json.dumps(config))+')\n'+source)
        start=time.monotonic()
        while time.monotonic()-start<65:
            time.sleep(2)
            memory=Memory();memory.cb=ctypes.sizeof(memory)
            if not psapi.GetProcessMemoryInfo(process,ctypes.byref(memory),memory.cb):raise RuntimeError('Owned game exited')
            report['memory'].append(dict(elapsed=time.monotonic()-start,privateMB=memory.PrivateUsage/1048576,workingMB=memory.WorkingSetSize/1048576,cpuSeconds=cpu_seconds(kernel,process)))
            if 'queueModeDuringRun' not in report and time.monotonic()-start>config['warmup']+2:
                report['queueModeDuringRun']=lua('client',"return GetConVar('mat_queue_mode'):GetInt()")
            report.update(client=read(directory/f'{a.label}-client.json'),server=read(directory/f'{a.label}-server.json'))
            c=report['client']
            dest.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
            if c and c.get('finished'):break
        else:raise RuntimeError('Owned benchmark stopped responding')
        lua('client',"RunConsoleCommand('mmdhl_debug_capture',"+json.dumps(a.label)+") return true")
        report['environment']=read(directory/f'{a.label}-environment.json')
        report['functionalPassed']=not c['errors'] and not report['server']['errors']
        report['fidelityValidated']=a.backend in ('reference','cpu_mt')
        report['backendVerified']=len(c.get('physicsEnd') or {})==a.count and all(d.get('broadphase')==(a.broadphase if a.broadphase!='auto' else ('sap' if a.backend in ('cpu_mt_v2','gpu_vulkan') else 'dbvt-fast')) and not d.get('broadphaseFallback') and d.get('secondaryBackend')==a.backend and not d.get('secondaryBackendFallback') for d in (c.get('physicsEnd') or {}).values())
        expected={m['asset']:m for m in selected}
        actual=list((c.get('physicsEnd') or {}).values())
        report['fullRigsVerified']=len(actual)==a.count and all(d.get('asset') in expected and d.get('bodies')==expected[d['asset']]['rigidBodies'] and d.get('authoredJoints')==expected[d['asset']]['joints'] and d.get('solverIterations')==a.iterations for d in actual)
        report['contactVerified']=not a.contacts or (c.get('maxWorldContacts',0)>0 and c.get('maxObjectContacts',0)>0 and report['server'].get('maxPropVelocityError',1e9)<.05)
        report['functionalPassed']=report['functionalPassed'] and report['backendVerified'] and report['fullRigsVerified'] and report['contactVerified']
        startStates,endStates=c.get('physicsStart') or {},c.get('physicsEnd') or {}
        def perStep(v,start,key):
            ticks=v['ticks']-start['ticks']
            return (v[key]-start[key])/ticks if ticks>0 and v.get(key) is not None and start.get(key) is not None else None
        report['physicsMeasurement']={k:dict(ticks=v['ticks']-startStates[k]['ticks'],resets=v['resets']-startStates[k]['resets'],droppedSeconds=v['dropped']-startStates[k]['dropped'],inputSeconds=v['inputTime']-startStates[k]['inputTime'],remainingDebtSeconds=v['debtSeconds'],physicsMsPerStep=perStep(v,startStates[k],'physicsTotalMs'),tickMsPerStep=perStep(v,startStates[k],'tickTotalMs'),guardMsPerStep=perStep(v,startStates[k],'guardTotalMs'),cpuMsPerStep=perStep(v,startStates[k],'tickCpuTotalMs'),bodies=v.get('bodies')) for k,v in endStates.items() if k in startStates}
        # Worker milliseconds per simulated 60 Hz tick, summed over the characters: the throughput cost that decides how many rigs keep up.
        report['workerMsPerTick']={key:sum(v[key] for v in report['physicsMeasurement'].values() if v.get(key) is not None) for key in ['physicsMsPerStep','tickMsPerStep','guardMsPerStep','cpuMsPerStep']}
        report['simulationKeepsUp']=len(report['physicsMeasurement'])==a.count and all(v['resets']==0 and v['droppedSeconds']==0 and v['remainingDebtSeconds']<=1/60+1e-6 for v in report['physicsMeasurement'].values())
        # Asynchronous worlds keep their own clock: every 60 Hz tick of the measured input interval must exist and the presented state must lag by at most two frames.
        report['asyncKeepsUp']=a.backend not in ('cpu_mt_v2','gpu_vulkan') or (all(v['ticks']>=int(v['inputSeconds']*60)-2 for v in report['physicsMeasurement'].values()) and (c.get('stages') or {}).get('asyncLagMs',{}).get('p95',1e9)<=max(2*1000/60+1,2*c['foregroundFrames'].get('p95',0)+1))
        report['sceneChecksPassed']=report['functionalPassed']
        report['functionalPassed']=report['sceneChecksPassed'] and report['simulationKeepsUp'] and report['asyncKeepsUp']
        # Game process CPU (all threads) over the measured interval, in cores.
        window=[m for m in report['memory'] if m.get('cpuSeconds') is not None and m['elapsed']>=config['warmup']]
        report['processCores']=(window[-1]['cpuSeconds']-window[0]['cpuSeconds'])/(window[-1]['elapsed']-window[0]['elapsed']) if len(window)>1 and window[-1]['elapsed']>window[0]['elapsed'] else None
        report['performanceValid']=c['unfocused']==0 and c['foregroundSeconds']>=9.8 and c['minDrawn']>=a.count
        report['performanceTargetPassed']=report['performanceValid'] and report['simulationKeepsUp'] and report['asyncKeepsUp'] and c['foregroundFrames'].get('p95',1e9)<=16.7
        print(json.dumps({k:report[k] for k in ['functionalPassed','performanceValid','performanceTargetPassed']}),c['foregroundFrames'],'workerMsPerTick',{k:round(v,3) for k,v in report['workerMsPerTick'].items()},flush=True)
        if not report['functionalPassed'] or not report['performanceValid']:
            raise RuntimeError('Correctness or measurement-validity gate failed; inspect the report')
    except Exception as e:
        report['failure']=str(e);raise
    finally:
        kernel.CloseHandle(process)
        if a.citizens>0:
            try:lua('server',"for _,e in ipairs(MMDHL_CITIZENS or {}) do if IsValid(e) then e:Remove() end end MMDHL_CITIZENS=nil return true")
            except Exception:pass
        dest.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        try:
            lua('server',"hook.Remove('Tick','MMDHL.StressMotion') hook.Remove('Tick','MMDHL.StressTickCount') timer.Remove('MMDHL.StressHeartbeat') if MMDHL_STRESS then if MMDHL_STRESS.wrapped then for _,w in ipairs(MMDHL_STRESS.wrapped) do hook.Add(w[1],w[2],w[3]) end MMDHL_STRESS.wrapped=nil end for _,e in ipairs(MMDHL_STRESS.entities or {}) do if IsValid(e) then e:Remove() end end for _,e in ipairs(MMDHL_STRESS.props or {}) do if IsValid(e) then e:Remove() end end end return true")
            lua('client',"for event,name in pairs({CalcView='MMDHL.StressCamera',HUDShouldDraw='MMDHL.StressHUD',PreDrawViewModel='MMDHL.StressViewModel',PostRender='MMDHL.StressFrames',HUDPaint='MMDHL.StressOverlay',OnLuaError='MMDHL.StressLuaErrors'}) do hook.Remove(event,name) end timer.Remove('MMDHL.StressClientHeartbeat') if MMDHL_ORIGINAL_DRAW then mmdhl.DrawCarrier=MMDHL_ORIGINAL_DRAW MMDHL_ORIGINAL_DRAW=nil end mmdhl.suspendNative=nil if mmdhl.native.SetRenderSuspended then mmdhl.native.SetRenderSuspended(false) end if MMDHL_STRESS_CLIENT and MMDHL_STRESS_CLIENT.wrapped then for _,w in ipairs(MMDHL_STRESS_CLIENT.wrapped) do hook.Add(w[1],w[2],w[3]) end MMDHL_STRESS_CLIENT.wrapped=nil end return true")
            lua('client','for k,v in pairs(util.JSONToTable('+json.dumps(json.dumps(saved))+')) do GetConVar(k):SetString(v) end mmdhl.native.SetSecondaryBroadphase('+json.dumps(locals().get('previousBroadphase','auto'))+') if mmdhl.native.SetSecondaryMidphase then mmdhl.native.SetSecondaryMidphase(true) end return true')
        except Exception:subprocess.run(['powershell','-NoProfile','-File',str(ROOT/'scripts/game-stop.ps1')],check=False)
        lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1);lock.close()
if __name__=='__main__':main()
