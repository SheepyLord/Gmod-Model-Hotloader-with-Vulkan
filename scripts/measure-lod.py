"""Update-rate LOD check: frame times with mmdhl_update_lod 2 and 0 from the
saved camera of a measure-scene report, from 650 units further back, and facing
away. Load the scene first (measure-scene.py). Writes validation/lod-check.json.
Usage: python scripts/measure-lod.py validation/<label>-scene.json
"""
import json,sys,time,subprocess
from gamectl import execute,ROOT
def lua(code):
    r=execute('client',code,60)
    if not r['ok']:raise RuntimeError(r.get('error'))
    return r.get('value')
subprocess.run(['powershell','-NoProfile','-File',str(ROOT/'scripts/game-focus.ps1')],check=True)
cam=json.loads((ROOT/sys.argv[1]).read_text(encoding='utf-8'))['camera']
lua("""gui.HideGameUI() RunConsoleCommand('cl_drawhud','0') RunConsoleCommand('r_drawviewmodel','0')
MMDHL_LOD={frames={},skipped=0,count=0}
hook.Add('PostRender','MMDHL.LodCheck',function() local s=MMDHL_LOD if not s.on then return end s.count=s.count+1 s.frames[#s.frames+1]=RealFrameTime()*1000 s.skipped=s.skipped+(mmdhl.poseSkipped or 0) end)
return true""")
views={'saved':(cam['origin'],cam['angles']),
       'far':None,'away':(cam['origin'],[cam['angles'][0],cam['angles'][1]+180,0])}
results={}
for view in ('saved','far','away'):
    if view=='far':
        code=f"local o,a=Vector({cam['origin'][0]},{cam['origin'][1]},{cam['origin'][2]}),Angle({cam['angles'][0]},{cam['angles'][1]},0) MMDHL_CAM={{o-a:Forward()*650,a}} return true"
    else:
        o,a=views[view];code=f"MMDHL_CAM={{Vector({o[0]},{o[1]},{o[2]}),Angle({a[0]},{a[1]},{a[2]})}} return true"
    lua(code+"")
    lua("hook.Add('CalcView','MMDHL.LodCam',function() return {origin=MMDHL_CAM[1],angles=MMDHL_CAM[2],drawviewer=false} end) return true")
    # Mode 2 (size tiers) against off; mode 1 differs from off only for unseen models.
    for lod in ('2','0'):
        lua(f"RunConsoleCommand('mmdhl_update_lod','{lod}') return true");time.sleep(2.5)
        lua("MMDHL_LOD.frames={} MMDHL_LOD.skipped=0 MMDHL_LOD.count=0 MMDHL_LOD.on=true return true");time.sleep(5)
        v=lua("MMDHL_LOD.on=false local f=MMDHL_LOD.frames table.sort(f) return {p50=f[math.floor(#f*.5)+1],p95=f[math.floor(#f*.95)+1],n=#f,skipped=MMDHL_LOD.skipped/math.max(MMDHL_LOD.count,1)}")
        results[f'{view}/lod{lod}']=v;print(view,'lod',lod,f"frame p50 {v['p50']:.2f} p95 {v['p95']:.2f} ms ({v['n']} frames), skipped poses per frame {v['skipped']:.2f}",flush=True)
lua("hook.Remove('CalcView','MMDHL.LodCam') hook.Remove('PostRender','MMDHL.LodCheck') RunConsoleCommand('mmdhl_update_lod','1') RunConsoleCommand('cl_drawhud','1') RunConsoleCommand('r_drawviewmodel','1') return true")
(ROOT/'validation/lod-check.json').write_text(json.dumps(results,indent=1))
