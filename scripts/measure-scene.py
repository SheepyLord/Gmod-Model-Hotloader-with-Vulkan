"""Measure frame cost in a saved scene (.gms) under several variants.

Loads the save into the owned test session (scripts/game-start.ps1), locks the
camera to the saved player view, and runs tests/game/scene-measure.lua once per
variant. Prints frame percentiles, MMD stage timings, the most expensive Lua
hooks and GPU 3D-engine utilisation, and writes validation/<label>-scene.json.

python scripts/measure-scene.py <label> --save "path/to/map 2026-9-23 18-26-10.gms" [--variants a,b,...]
"""
import argparse,json,lzma,pathlib,subprocess,sys,threading,time
from gamectl import ROOT,read,execute
if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf-8')

def lua(realm,code,timeout=60):
    r=execute(realm,code,timeout)
    if not r['ok']:raise RuntimeError(f"{realm}: {r.get('error')}")
    return r.get('value')

# Each variant: client Lua applied before measuring and client Lua reverting it.
VARIANTS={
 'baseline':('',''),
 'overlay':("RunConsoleCommand('mmdhl_debug_overlay','1')","RunConsoleCommand('mmdhl_debug_overlay','0')"),
 'noshadow':("RunConsoleCommand('r_shadows','0')","RunConsoleCommand('r_shadows','1')"),
 'nophysics':("RunConsoleCommand('mmdhl_secondary_iterations','-1')","RunConsoleCommand('mmdhl_secondary_iterations','10')"),
 'jiggle':("RunConsoleCommand('mmdhl_secondary_iterations','0')","RunConsoleCommand('mmdhl_secondary_iterations','10')"),
 # Worker pool size (physics ticks and deformation share it); 0 restores automatic.
 'workers4':("mmdhl.native.SetWorkers(4)","mmdhl.native.SetWorkers(0)"),
 'workers8':("mmdhl.native.SetWorkers(8)","mmdhl.native.SetWorkers(0)"),
 'workers16':("mmdhl.native.SetWorkers(16)","mmdhl.native.SetWorkers(0)"),
 # CPU skinning of every vertex, full-rate poses for every model, and both (the pre-item-4/5 path).
 'cpuskin':("RunConsoleCommand('mmdhl_gpu_skinning','0')","RunConsoleCommand('mmdhl_gpu_skinning','1')"),
 'nolod':("RunConsoleCommand('mmdhl_update_lod','0')","RunConsoleCommand('mmdhl_update_lod','1')"),
 'cpunolod':("RunConsoleCommand('mmdhl_gpu_skinning','0') RunConsoleCommand('mmdhl_update_lod','0')","RunConsoleCommand('mmdhl_gpu_skinning','1') RunConsoleCommand('mmdhl_update_lod','1')"),
 # Stop presentation updates: every draw continues from the last uploaded buffers,
 # with no pose capture, skinning, morph sync or upload.
 'still':("for _,e in ipairs(mmdhl.Entities()) do e.MMDPresentationStopped='measurement' end",
          "for _,e in ipairs(mmdhl.Entities()) do if e.MMDPresentationStopped=='measurement' then e.MMDPresentationStopped=nil end end"),
 'stillnophysics':("RunConsoleCommand('mmdhl_secondary_iterations','-1') for _,e in ipairs(mmdhl.Entities()) do e.MMDPresentationStopped='measurement' end",
          "RunConsoleCommand('mmdhl_secondary_iterations','10') for _,e in ipairs(mmdhl.Entities()) do if e.MMDPresentationStopped=='measurement' then e.MMDPresentationStopped=nil end end"),
 # Engine floor with the scene present: no MMD pose capture, deformation, upload or draw.
 'floor0':("mmdhl.suspendNative=true mmdhl.native.SetRenderSuspended(true)","mmdhl.suspendNative=nil mmdhl.native.SetRenderSuspended(false)"),
 # The same floor with Source's queued (multicore) material system, which the addon normally forces off.
 'floor2':("mmdhl.suspendNative=true mmdhl.native.SetRenderSuspended(true) MMDHL_SAVED_IMMEDIATE=MMDHL_SAVED_IMMEDIATE or mmdhl.ImmediateRendering mmdhl.ImmediateRendering=function() return false,'measurement' end RunConsoleCommand('mat_queue_mode','2')",
           "if MMDHL_SAVED_IMMEDIATE then mmdhl.ImmediateRendering=MMDHL_SAVED_IMMEDIATE MMDHL_SAVED_IMMEDIATE=nil end mmdhl.suspendNative=nil mmdhl.native.SetRenderSuspended(false) RunConsoleCommand('mat_queue_mode','0')"),
}

def gpu_sampler(pid,seconds,out):
    counter=f"\\GPU Engine(pid_{pid}_*engtype_3D)\\Utilization Percentage"
    script=(f"$s=Get-Counter -Counter '{counter}' -SampleInterval 1 -MaxSamples {seconds} -ErrorAction SilentlyContinue;"
            "foreach($x in $s){[math]::Round(($x.CounterSamples|Measure-Object CookedValue -Sum).Sum,1)}")
    try:
        r=subprocess.run(['powershell','-NoProfile','-Command',script],capture_output=True,text=True,timeout=seconds+30)
        out.extend(float(v) for v in r.stdout.split() if v.replace('.','',1).isdigit())
    except Exception as e:out.append(-1)

def main():
    p=argparse.ArgumentParser();p.add_argument('label');p.add_argument('--save',required=True,type=pathlib.Path)
    p.add_argument('--variants',default='baseline,overlay,noshadow,nophysics,floor0,floor2')
    p.add_argument('--duration',type=float,default=8);p.add_argument('--warmup',type=float,default=3);p.add_argument('--no-load',action='store_true');p.add_argument('--sample',action='store_true',help='also sample the main thread (adds a few percent of frame time)');p.add_argument('--wrap',default='',help='comma-separated Lua function paths to time, e.g. mmdhl.SyncActorMorphs')
    a=p.parse_args()
    session=read(ROOT/'validation/session.json');cache=pathlib.Path(session['cache']);directory=cache/'debug'/session['token']
    raw=a.save.read_bytes()
    if raw[:4]!=b'GMS3':raise RuntimeError('Not a GMod save (GMS3)')
    # Header: 'GMS3' map '\0' 'WSID:' ... '\0' then an LZMA-alone stream.
    for offset in range(8,128):
        try:text=lzma.LZMADecompressor(format=lzma.FORMAT_ALONE).decompress(raw[offset:]).decode('utf-8');break
        except Exception:continue
    else:raise RuntimeError('Cannot find the compressed save payload')
    save=json.loads(text);mapname=raw[4:raw.index(b'\0',4)].decode()
    current=lua('client','return game.GetMap()')
    if current!=mapname:raise RuntimeError(f'Start the session on {mapname} (currently {current})')
    player=save['Player'];origin=[float(v) for v in player['Origin'].strip('[]').split()];angles=[float(v) for v in player['Angle'].strip('{}').split()]
    camera={'origin':[origin[0],origin[1],origin[2]+64],'angles':angles}
    expected=sum(1 for e in save['Entities'].values() if (e.get('EntityMods') or {}).get('MMDHLNative'))
    if not a.no_load:
        directory.mkdir(parents=True,exist_ok=True);(directory/'scene-save.txt').write_text(text,encoding='utf-8')
        lua('server',f"gmsave.LoadMap(file.Read('mmd_hotloader/debug/{session['token']}/scene-save.txt','DATA'),player.GetHumans()[1]) return true")
        deadline=time.monotonic()+120
        while time.monotonic()<deadline:
            time.sleep(3)
            try:ready=lua('client',"local n=0 for _,e in ipairs(mmdhl.Entities()) do if mmdhl.GetInstance(e)>0 and e.MMDPresentationFrame then n=n+1 end end return n")
            except Exception:ready=0
            print('models presenting:',ready,'/',expected,flush=True)
            if ready>=expected:break
        else:raise RuntimeError('The saved MMD models did not attach')
        time.sleep(5)
    source=(ROOT/'tests/game/scene-measure.lua').read_text(encoding='utf-8')
    results={}
    for variant in a.variants.split(','):
        apply,revert=VARIANTS[variant]
        subprocess.run(['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'scripts/game-focus.ps1')],capture_output=True)
        if apply:lua('client',apply+' return true')
        time.sleep(2)
        name=f'{a.label}-{variant}';(directory/f'{name}-scene.json').unlink(missing_ok=True)
        config=dict(name=name,variant=variant,camera=camera,warmup=a.warmup,duration=a.duration,wrap=[w for w in a.wrap.split(',') if w])
        gpu=[];sampler=threading.Thread(target=gpu_sampler,args=(session['pid'],int(a.duration),gpu))
        lua('client','MMDHL_TEST_CONFIG=util.JSONToTable('+json.dumps(json.dumps(config))+')\n'+source)
        time.sleep(a.warmup);sampler.start()
        if a.sample:lua('client',f'return mmdhl.native.StartMainThreadSampling({int(a.duration*1000)-500})')
        deadline=time.monotonic()+a.duration+60
        while time.monotonic()<deadline:
            time.sleep(1);result=read(directory/f'{name}-scene.json')
            if result:break
        else:raise RuntimeError(f'{variant}: no result')
        sampler.join();result['gpu3dPercent']=gpu
        if a.sample:
            for _ in range(60):
                samples=lua('client','local r=mmdhl.native.ReadMainThreadSamples() return r and util.JSONToTable(r) or false',120)
                if samples:result['mainThread']=samples;break
                time.sleep(1)
        if revert:lua('client',revert+' return true')
        time.sleep(1);results[variant]=result
        f=result['frames'];st=result['stages']
        g=[v for v in gpu if v>=0]
        print(f"{variant:10s} frame p50 {f.get('p50')} p95 {f.get('p95')} mean {f.get('mean',0):.2f} ({result['frameCount']} frames)  gpu3d {sum(g)/len(g) if g else -1:.0f}%  draws/frame {result['drawsPerFrame']}",flush=True)
    report=dict(save=str(a.save),camera=camera,expectedModels=expected,results=results)
    (ROOT/'validation'/f'{a.label}-scene.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('\nMMD stages (p50 ms):')
    keys=['captureMs','prepareWallMs','luaDrawMs','nativeRenderMs','shadowMs','uploadMs','vertexLockMs','vertexFillMs','vertexUnlockMs','meshDrawMs','deformWorkMs','poseWorkMs','physicsWorkMs','drawCalls','drawBinds','depthPasses','changedBones','fullChanges']
    for v,r in results.items():print(f"  {v:10s} "+' '.join(f"{k}={r['stages'][k]['p50']}" for k in keys if k in r['stages']))
    print('\nTimed functions (ms per frame):')
    for v,r in results.items():
        if r.get('functions'):print(f'  {v}: '+', '.join(f"{k} {x:.3f}" for k,x in sorted(r['functions'].items(),key=lambda i:-i[1])))
    print('\nTop Lua hooks (ms per frame):')
    for v,r in results.items():
        print(f'  {v}:');[print(f"    {h['msPerFrame']:.3f}  {h['hook']}") for h in r['hooks'][:12]]
if __name__=='__main__':main()
