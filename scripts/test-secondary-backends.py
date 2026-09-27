"""Switch complete live rigs without changing Source entities or appearance."""
import json,runpy,time
from gamectl import ROOT
helper=runpy.run_path(str(ROOT/'scripts/test-compatibility.py'))
lua,spawn,wait=(helper[k] for k in ('lua','spawn','wait'))
models=json.loads((ROOT/'validation/acceptance-models.json').read_text(encoding='utf8'))
report={'checks':[]}
saved=lua('server',"local c=GetConVar('sv_rpe_enable') if c then local v=c:GetString() c:SetBool(false) return v end")
try:
    lua('server','for _,e in ipairs(mmdhl.Entities()) do e:Remove() end return true')
    entity=spawn(models['Cyrene']['asset'])
    lua('server',"local e=mmdhl.testEnt MMDHL_BACKEND_POSES={} for i=0,17 do local p=e:GetPhysicsObjectNum(i) MMDHL_BACKEND_POSES[i]={body=p,pos=p:GetPos(),ang=p:GetAngles()} end e:SetColor(Color(120,180,240)) e:SetSubMaterial(40,'models/wireframe') e:SetBodygroup(41,1) mmdhl.SetMorphWeight(e,0,.37) return true")
    for backend in ['cpu_mt','gpu_opencl','reference','cpu_mt']:
        lua('client',f'assert(mmdhl.SetSecondaryBackend(Entity({entity}),'+json.dumps(backend)+')) return true')
        wait('server','return mmdhl.GetSecondaryBackend(mmdhl.testEnt).requested=='+json.dumps(backend))
        time.sleep(.5)
        result=lua('server',"""local e=mmdhl.testEnt assert(e:GetPhysicsObjectCount()==18)
for i=0,17 do local p=e:GetPhysicsObjectNum(i) local old=MMDHL_BACKEND_POSES[i] assert(p==old.body and not p:IsMotionEnabled() and p:GetPos():Distance(old.pos)<.001) assert(math.abs(math.AngleDifference(p:GetAngles().p,old.ang.p))<.001 and math.abs(math.AngleDifference(p:GetAngles().y,old.ang.y))<.001 and math.abs(math.AngleDifference(p:GetAngles().r,old.ang.r))<.001) end
assert(e:GetColor()==Color(120,180,240) and e:GetSubMaterial(40)=='models/wireframe' and e:GetBodygroup(41)==1 and math.abs(mmdhl.GetMorphWeight(e,0)-.37)<.001)
assert(mmdhl.GetSecondaryCollisionMode(e)==2 and not e.MMDStopped)
e:PreEntityCopy() assert(e.EntityMods.MMDHLNative.options.secondaryBackend==mmdhl.GetSecondaryBackend(e).requested)
local state=mmdhl.GetSecondaryBackend(e) state.nativeObjectsPreserved=18 return state""")
        assert result['requested']==backend and result['effective']==backend,result
        report['checks'].append(result)
    report['duplicate']=lua('server',"""local e=mmdhl.testEnt e:PreEntityCopy() local data=duplicator.CopyEntTable(e) local copies=duplicator.Paste(player.GetHumans()[1],{[e:EntIndex()]=data},{}) local count=0 for _,c in pairs(copies) do assert(mmdhl.GetSecondaryBackend(c).requested=='cpu_mt' and c:GetPhysicsObjectCount()==18) count=count+1 c:Remove() end assert(count==1) return count""")
    report['invalidRejected']=lua('server',"local e=mmdhl.testEnt local old=mmdhl.GetSecondaryBackend(e).requested assert(not mmdhl.SetSecondaryBackend(e,'typo')) assert(mmdhl.GetSecondaryBackend(e).requested==old) return true")
    report['reset']=lua('server','assert(mmdhl.ResetPhysics(mmdhl.testEnt)) return mmdhl.GetSecondaryBackend(mmdhl.testEnt).requested')
    assert report['reset']=='cpu_mt'
    report['passed']=True
    print(json.dumps(report,indent=2))
finally:
    lua('server','for _,e in ipairs(mmdhl.Entities()) do e:Remove() end MMDHL_BACKEND_POSES=nil return true')
    if saved is not None:lua('server','GetConVar("sv_rpe_enable"):SetString('+json.dumps(saved)+') return true')
    (ROOT/'validation/accelerated-backend-controls.json').write_text(json.dumps(report,indent=2),encoding='utf8')
