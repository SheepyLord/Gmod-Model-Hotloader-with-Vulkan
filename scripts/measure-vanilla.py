"""Frame cost of stock GMod scenes (no MMD models) in the owned test session,
for renderer A/B runs such as stock D3D9 vs DXVK (one run per game session).

Each scene is built by tests/game/vanilla-scene.lua (server, seeded) and
measured by tests/game/scene-measure.lua (client, camera locked, no hook
profiling). Prints frame percentiles, mean FPS, GPU 3D-engine load and the
game process's CPU use in cores; writes validation/<label>-vanilla.json.

python scripts/measure-vanilla.py <label> [--scenes a,b,...] [--duration 10] [--warmup 3] [--shots <dir>]
                                  [--queue-modes 2,0] [--affinity 0xffff]
python scripts/measure-vanilla.py --compare labelA,labelB[,...]    (table of saved runs)
"""
import argparse,ctypes,json,pathlib,subprocess,sys,threading,time
from ctypes import wintypes
from gamectl import ROOT,read,execute
if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf-8')

SCENES={
 'flatgrass_empty':dict(map='gm_flatgrass',scene='empty',pitch=6),
 'flatgrass_props':dict(map='gm_flatgrass',scene='props',pitch=14,count=400),
 'flatgrass_physics':dict(map='gm_flatgrass',scene='physics',pitch=12,count=200),
 'flatgrass_characters':dict(map='gm_flatgrass',scene='characters',pitch=8,count=48),
 # Stock HL2 models (the citizen paths above may be replaced in an install).
 'flatgrass_combine':dict(map='gm_flatgrass',scene='characters',pitch=8,count=48,models=['models/combine_soldier.mdl','models/combine_super_soldier.mdl','models/police.mdl']),
 'construct_view':dict(map='gm_construct',scene='empty',pitch=0),
 'construct_props':dict(map='gm_construct',scene='props',pitch=10,count=300,columns=15),
 'construct_flashlight':dict(map='gm_construct',scene='empty',pitch=0,flashlight=True),
}

def lua(realm,code,timeout=60):
    r=execute(realm,code,timeout)
    if not r['ok']:raise RuntimeError(f"{realm}: {r.get('error')}")
    return r.get('value')

def cpu_seconds(pid):
    """Kernel + user CPU time of a process in seconds (GetProcessTimes)."""
    k32=ctypes.WinDLL('kernel32',use_last_error=True)
    handle=k32.OpenProcess(0x1000,False,pid)
    if not handle:return None
    times=[wintypes.FILETIME() for _ in range(4)]
    ok=k32.GetProcessTimes(handle,*[ctypes.byref(t) for t in times]);k32.CloseHandle(handle)
    if not ok:return None
    return sum((t.dwHighDateTime<<32|t.dwLowDateTime)/1e7 for t in times[2:])

def gpu_sampler(pid,seconds,out):
    counter=f"\\GPU Engine(pid_{pid}_*engtype_3D)\\Utilization Percentage"
    script=(f"$s=Get-Counter -Counter '{counter}' -SampleInterval 1 -MaxSamples {seconds} -ErrorAction SilentlyContinue;"
            "foreach($x in $s){[math]::Round(($x.CounterSamples|Measure-Object CookedValue -Sum).Sum,1)}")
    try:
        r=subprocess.run(['powershell','-NoProfile','-Command',script],capture_output=True,text=True,timeout=seconds+30)
        out.extend(float(v) for v in r.stdout.split() if v.replace('.','',1).isdigit())
    except Exception:out.append(-1)

def focus():subprocess.run(['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'scripts/game-focus.ps1')],capture_output=True)

def ensure_map(name):
    try:
        if lua('client','return game.GetMap()',20)==name:return
    except Exception:pass
    # Guarded so a replayed request cannot change level again.
    lua('server',f"if game.GetMap()~='{name}' then timer.Simple(0.5,function() RunConsoleCommand('changelevel','{name}') end) end return true")
    time.sleep(10);deadline=time.monotonic()+300
    while time.monotonic()<deadline:
        try:
            if lua('client',"return game.GetMap()..'|'..tostring(IsValid(LocalPlayer()))",10)==f'{name}|true':break
        except Exception:pass
        time.sleep(3)
    else:raise RuntimeError(f'{name} did not load')
    time.sleep(8)

def set_affinity(pid,mask):
    """Pins the game process (and threads it creates later) to the CPUs in mask."""
    k32=ctypes.WinDLL('kernel32',use_last_error=True)
    k32.OpenProcess.restype=wintypes.HANDLE;k32.SetProcessAffinityMask.argtypes=[wintypes.HANDLE,ctypes.c_size_t]
    handle=k32.OpenProcess(0x0200|0x0400,False,pid)
    if not handle or not k32.SetProcessAffinityMask(handle,mask):raise OSError(f'SetProcessAffinityMask failed ({ctypes.get_last_error()})')
    k32.CloseHandle(handle)

def compare(labels):
    runs={l:json.loads((ROOT/'validation'/f'{l}-vanilla.json').read_text(encoding='utf-8')) for l in labels}
    first=next(iter(runs.values()))['results']
    keys=[k for k in first if all(k in r['results'] for r in runs.values())]
    print(f"{'scene':26s}"+''.join(f"| {l[:26]:26s} " for l in labels))
    print(f"{'':26s}"+''.join(f"| {'p50/p95 ms  FPS  gpu cpu':26s} " for _ in labels))
    for k in keys:
        row=f'{k:26s}'
        for l in labels:
            r=runs[l]['results'][k];f=r['frames']
            row+=f"| {f['p50']:5.2f}/{f['p95']:5.2f} {r['fps']:5.0f} {r['gpu3d']:3.0f}% {r['cpuCores']:4.2f} "
        print(row)

def main():
    p=argparse.ArgumentParser();p.add_argument('label',nargs='?');p.add_argument('--scenes',default=','.join(SCENES))
    p.add_argument('--duration',type=float,default=10);p.add_argument('--warmup',type=float,default=3);p.add_argument('--settle',type=float,default=3)
    p.add_argument('--queue-modes',help='comma-separated mat_queue_mode values measured on each built scene (results keyed scene@qN); default: leave the setting alone')
    p.add_argument('--affinity',help='pin the game process to this CPU mask first (e.g. 0xffff), so runs compare on the same cores')
    p.add_argument('--shots',type=pathlib.Path,help='also save a screenshot of every scene here (after measuring)');p.add_argument('--compare')
    a=p.parse_args()
    if a.compare:return compare(a.compare.split(','))
    if not a.label:p.error('label required')
    session=read(ROOT/'validation/session.json');pid=session['pid']
    directory=pathlib.Path(session['cache'])/'debug'/session['token']
    builder=(ROOT/'tests/game/vanilla-scene.lua').read_text(encoding='utf-8')
    source=(ROOT/'tests/game/scene-measure.lua').read_text(encoding='utf-8')
    if a.affinity:set_affinity(pid,int(a.affinity,0))
    modes=[int(m) for m in a.queue_modes.split(',')] if a.queue_modes else [None]
    original=lua('client',"return GetConVar('mat_queue_mode'):GetString()") if a.queue_modes else None
    results={}
    for name in a.scenes.split(','):
        spec=dict(SCENES[name]);ensure_map(spec['map'])
        built=json.loads(lua('server','MMDHL_VANILLA_CONFIG=util.JSONToTable('+json.dumps(json.dumps(spec))+')\n'+builder,120))
        camera={'origin':built['origin'],'angles':built['angles']}
        focus();time.sleep(a.settle)
        for mode in modes:
            key=name if mode is None else f'{name}@q{mode}'
            if mode is not None:lua('client',f"RunConsoleCommand('mat_queue_mode','{mode}') return true");time.sleep(1.5)
            measure=f'{a.label}-{key.replace("@","-")}';(directory/f'{measure}-scene.json').unlink(missing_ok=True)
            config=dict(name=measure,variant=key,camera=camera,warmup=a.warmup,duration=a.duration,profileHooks=False)
            lua('client','MMDHL_TEST_CONFIG=util.JSONToTable('+json.dumps(json.dumps(config))+')\n'+source)
            time.sleep(a.warmup)
            gpu=[];sampler=threading.Thread(target=gpu_sampler,args=(pid,int(a.duration),gpu));sampler.start()
            cpu0,wall0=cpu_seconds(pid),time.monotonic()
            deadline=time.monotonic()+a.duration+60
            while time.monotonic()<deadline:
                time.sleep(.5);result=read(directory/f'{measure}-scene.json')
                if result:break
            else:raise RuntimeError(f'{key}: no result')
            cpu1,wall1=cpu_seconds(pid),time.monotonic();sampler.join()
            g=[v for v in gpu if v>=0];f=result['frames']
            entry=dict(scene=spec,queueMode=mode,affinity=a.affinity,built=built,frames=f,frameCount=result['frameCount'],unfocused=result['unfocused'],fps=1000/f['mean'] if f.get('mean') else 0,
                       gpu3d=sum(g)/len(g) if g else -1,gpuSamples=gpu,cpuCores=(cpu1-cpu0)/(wall1-wall0) if cpu0 is not None and cpu1 is not None else -1,settings=result['settings'],map=result['map'],errors=result['errors'])
            results[key]=entry
            print(f"{key:26s} p50 {f['p50']:6.2f} p95 {f['p95']:6.2f} p99 {f['p99']:6.2f} ms  {entry['fps']:6.1f} FPS  gpu3d {entry['gpu3d']:4.0f}%  cpu {entry['cpuCores']:.2f} cores  ({result['frameCount']} frames, spawned {built['spawned']}, animated {built['animated']}, queue {result['settings'].get('mat_queue_mode')})",flush=True)
            if a.shots:
                a.shots.mkdir(parents=True,exist_ok=True)
                lua('client','MMDHL_TEST_CONFIG=util.JSONToTable('+json.dumps(json.dumps(dict(config,name=measure+'-shot',warmup=0,duration=30)))+')\n'+source)
                time.sleep(2);subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('capture-window.py')),str(pid),str(a.shots/f'{a.label}-{key.replace("@","-")}.png')],capture_output=True)
                lua('client','if MMDHL_SCENE_MEASURE then MMDHL_SCENE_MEASURE.finished=true MMDHL_SCENE_MEASURE.cleanup() end return true')
    if original is not None:lua('client',f"RunConsoleCommand('mat_queue_mode','{original}') return true")
    lua('server',"timer.Remove('MMDHL.VanillaShaker') for _,p in ipairs(player.GetHumans()) do p:Freeze(false) p:Flashlight(false) end game.CleanUpMap() return true")
    (ROOT/'validation'/f'{a.label}-vanilla.json').write_text(json.dumps(dict(label=a.label,affinity=a.affinity,queueModes=a.queue_modes,results=results),ensure_ascii=False,indent=2),encoding='utf-8')
if __name__=='__main__':main()
