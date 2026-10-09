"""In-game check for issue #6: vertices weighted to MMD control roots (グルーブ above the pelvis)
stay with the body far from the world origin, with hardware skinning on and off.

Run inside an owned game-start.ps1 session with 2.3.0 natives, standing at least 500 units
from the world origin (the gm_flatgrass spawn is 12,000 units below it):
  python scripts/test-control-roots.py [model.pmx ...]
It imports tests/fixtures/native-control-root.pmx and the given models, spawns each one frozen
in front of the player and, after rendering, measures every part's farthest vertex from the
ragdoll and GetAlignmentProbe's farthest vertex from the skeleton. The report is written to
validation/control-roots.json.
"""
import json,pathlib,sys,time
from gamectl import execute,ROOT
def call(realm,code,timeout=45):
    result=execute(realm,code,timeout)
    if not result['ok']:raise RuntimeError(result.get('error'))
    return result.get('value')
def wait(realm,code,timeout=60):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        value=call(realm,code)
        if value:return value
        time.sleep(.2)
    raise TimeoutError(code)
def import_model(path):
    source=json.dumps(str(pathlib.Path(path).resolve()),ensure_ascii=False)
    job=call('client',"local h,e=mmdhl.native.BeginImport(SOURCE,'{}') assert(h,e) return h".replace('SOURCE',source))
    result=wait('client',"local s=mmdhl.Decode(mmdhl.native.PollJob(JOB)) if s.state~='running' then return s end".replace('JOB',str(job)),600)
    assert result['state']=='complete',result
    return result['asset']
# After a rendered frame: the probe's farthest vertex, the floating-root counts and, per part,
# the vertex farthest from the ragdoll's position (every vertex, CPU-deformed for the query).
MEASURE="""local e=Entity(ENTITY) if not IsValid(e) or mmdhl.GetInstance(e)<=0 then return end
local inst=mmdhl.GetInstance(e) local probe=mmdhl.Decode(mmdhl.native.GetAlignmentProbe(inst))
if not probe or not probe.farthestVertex or (probe.frame or 0)<=MMDHL_CR_FRAME then return end
local d=mmdhl.GetDiagnostics(e) local origin=e:GetPos() local worst,part=0,-1
for p=0,#mmdhl.GetMaterials(e)-1 do for _,v in ipairs(mmdhl.Decode(mmdhl.native.GetMaterialPositions(inst,p)) or {}) do
 local distance=origin:Distance(Vector(v[1],v[2],v[3])) if distance>worst then worst,part=distance,p end end end
return {farthest=probe.farthestVertex,floating=d.floatingRoots,partDistance=worst,part=part,origin={origin:Unpack()},gpu=mmdhl.Decode(mmdhl.native.RenderStats()).gpuSkinning}"""
report={'features':wait('client',"local s=mmdhl.GetInstallationStatus() if s.features.imports then return s.features end"),'models':{}}
spot=call('server',"local p=player.GetHumans()[1] local at=p:GetPos()+Vector(0,0,64) local tr=util.TraceLine({start=at,endpos=at+p:GetAimVector()*160,filter=p}) return {(tr.HitPos+tr.HitNormal*40):Unpack()}")
assert sum(v*v for v in spot)**.5>=500,'stand at least 500 units from the world origin'
gpu=call('client',"return GetConVar('mmdhl_gpu_skinning'):GetString()")
try:
    for path in [ROOT/'tests/fixtures/native-control-root.pmx']+[pathlib.Path(a) for a in sys.argv[1:]]:
        asset=import_model(path)
        call('server',"for _,e in ipairs(mmdhl.Entities()) do e:Remove() end MMDHL_CR=nil mmdhl.Spawn(player.GetHumans()[1],'ASSET',{backend='source',position={SPOT},frozen=true},function(e,err) MMDHL_CR=IsValid(e) and e:EntIndex() or tostring(err) end) return true"
             .replace('ASSET',asset).replace('SPOT',','.join(repr(float(v)) for v in spot)))
        entity=wait('server','return MMDHL_CR')
        assert isinstance(entity,(int,float)),entity
        result={}
        for mode in ('1','0'):
            call('client',"RunConsoleCommand('mmdhl_gpu_skinning','MODE') MMDHL_CR_FRAME=0 return true".replace('MODE',mode))
            time.sleep(1)
            call('client',"local e=Entity(ENTITY) MMDHL_CR_FRAME=IsValid(e) and mmdhl.GetInstance(e)>0 and (mmdhl.Decode(mmdhl.native.GetAlignmentProbe(mmdhl.GetInstance(e))) or {}).frame or 0 return true".replace('ENTITY',str(int(entity))))
            value=wait('client',MEASURE.replace('ENTITY',str(int(entity))))
            height=value['farthest']['modelHeight']
            value['ok']=value['partDistance']<1.5*height and value['farthest']['distance']<height
            result['hardware' if mode=='1' else 'cpu']=value
            print(path.name,'hardware' if mode=='1' else 'CPU','skinning: farthest vertex',round(value['farthest']['distance'],2),'units from the skeleton, part',value['part'],
                  round(value['partDistance'],2),'units from the ragdoll (model height',round(height,1),'), floating roots',value['floating'],'OK' if value['ok'] else 'FAIL',flush=True)
        report['models'][path.name]={'asset':asset,**result}
finally:
    call('client',"RunConsoleCommand('mmdhl_gpu_skinning','MODE') return true".replace('MODE',gpu))
    call('server',"for _,e in ipairs(mmdhl.Entities()) do e:Remove() end return true")
    (ROOT/'validation').mkdir(exist_ok=True)
    (ROOT/'validation/control-roots.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert all(m[k]['ok'] for m in report['models'].values() for k in ('hardware','cpu')),report
print('every vertex stays with the body')
