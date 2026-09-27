"""Short owned-session terrain/prop contact and cleanup checks; no Source feedback."""
import argparse,json,runpy,time
from gamectl import ROOT
compat=runpy.run_path(str(ROOT/'scripts/test-compatibility.py'))
lua,spawn=compat['lua'],compat['spawn']
models=json.loads((ROOT/'validation/compatibility-models.json').read_text(encoding='utf8'))
lua('server','for _,e in ipairs(mmdhl.Entities()) do e:Remove() end return true')
parser=argparse.ArgumentParser();parser.add_argument('--backend',choices=['reference','cpu_mt','gpu_opencl'],default='reference');args=parser.parse_args()
entity=spawn(models['Cyrene']['asset'])
lua('server','assert(mmdhl.SetSecondaryBackend(mmdhl.testEnt,'+json.dumps(args.backend)+')) return true')
lua('server',"local e=mmdhl.testEnt MMDHL_CONTACT_ORIGIN=e:GetPos() MMDHL_CONTACT_BINDS={} for i=0,17 do MMDHL_CONTACT_BINDS[i]=e:GetPhysicsObjectNum(i):GetPos() end return true")
report={'backend':args.backend}
def sample(name):
    out={'maxContacts':0,'maxVelocityError':0,'samples':0}
    for _ in range(10):
        time.sleep(.12)
        d=lua('server',"local e=mmdhl.testEnt local d=mmdhl.GetDiagnostics(e) assert(d.feedbackApplied==0 and e:GetPhysicsObjectCount()==18 and not e.MMDStopped) for _,b in ipairs(d.bodyList) do for _,v in ipairs(b.worldPosition) do assert(v==v and math.abs(v)<100000) end end d.bodyList=nil if IsValid(MMDHL_CONTACT_PROP) and MMDHL_CONTACT_MOVING then d.velocityError=(MMDHL_CONTACT_PROP:GetPhysicsObject():GetVelocity()-Vector(0,3,0)):Length() end return d")
        out.update(proxies=d['sourceMirrors'],captureMs=d['sceneCaptureMs'],syncMs=d['sceneSyncMs'],stepMs=d['stepMs'])
        out['maxContacts']=max(out['maxContacts'],d['externalContacts']);out['maxVelocityError']=max(out['maxVelocityError'],d.get('velocityError',0));out['samples']+=1
    assert out['maxContacts']>0,(name,out)
    assert out['maxVelocityError']<.05,(name,out)
    report[name]=out;print('PASS',name,out,flush=True)
    (ROOT/f'validation/accelerated-contact-scenes-{args.backend}.json').write_text(json.dumps(report,indent=2),encoding='utf8')
try:
    lua('server',"local e=mmdhl.testEnt for i=0,17 do e:GetPhysicsObjectNum(i):SetPos(MMDHL_CONTACT_BINDS[i]-Vector(0,0,22)) end mmdhl.SetSecondaryCollisionMode(e,1) return true")
    sample('map-floor')
    lua('server',"local e=mmdhl.testEnt for i=0,17 do e:GetPhysicsObjectNum(i):SetPos(MMDHL_CONTACT_BINDS[i]) end mmdhl.SetSecondaryCollisionMode(e,2) return true")
    for name,offset,angles,moving in [('wall','Vector(8,0,40)','Angle(90,0,0)',False),('slope','Vector(0,0,8)','Angle(25,0,0)',False),('moving-prop','Vector(0,0,35)','angle_zero',True)]:
        lua('server',f"local e=mmdhl.testEnt if IsValid(MMDHL_CONTACT_PROP) then MMDHL_CONTACT_PROP:Remove() end local p=ents.Create('prop_physics') p:SetModel('models/hunter/plates/plate1x1.mdl') p:SetPos(MMDHL_CONTACT_ORIGIN+{offset}) p:SetAngles({angles}) p:Spawn() local ph=p:GetPhysicsObject() ph:EnableGravity(false) ph:EnableDrag(false) ph:EnableMotion({str(moving).lower()}) ph:SetVelocity(Vector(0,{3 if moving else 0},0)) for i=0,17 do constraint.NoCollide(e,p,i,0) end MMDHL_CONTACT_PROP=p MMDHL_CONTACT_MOVING={str(moving).lower()} return true")
        sample(name)
    lua('server',"MMDHL_CONTACT_PROP:Remove() MMDHL_CONTACT_MOVING=false local p=ents.Create('prop_ragdoll') p:SetModel('models/Humans/Group01/male_07.mdl') p:SetPos(MMDHL_CONTACT_ORIGIN+Vector(6,0,0)) p:Spawn() for i=0,p:GetPhysicsObjectCount()-1 do p:GetPhysicsObjectNum(i):EnableMotion(false) end MMDHL_CONTACT_PROP=p return true")
    sample('other-ragdoll')
    lua('server',"local e=mmdhl.testEnt for i=0,17 do e:GetPhysicsObjectNum(i):SetPos(MMDHL_CONTACT_BINDS[i]+Vector(1000,0,50)) end MMDHL_CONTACT_PROP:Remove() mmdhl.SetSecondaryCollisionMode(e,0) return true")
    time.sleep(.3)
    report['teleportAndRemoval']=lua('server',"local e=mmdhl.testEnt local d=mmdhl.GetDiagnostics(e) assert(d.sourceMirrors==0 and d.feedbackApplied==0 and not e.MMDStopped) e:Remove() game.CleanUpMap() return {proxies=d.sourceMirrors,stopped=e.MMDStopped or false}")
    print('PASS teleport, mode change, object removal, map cleanup',flush=True)
finally:
    lua('server','if IsValid(MMDHL_CONTACT_PROP) then MMDHL_CONTACT_PROP:Remove() end for _,e in ipairs(mmdhl.Entities()) do e:Remove() end return true')
    (ROOT/f'validation/accelerated-contact-scenes-{args.backend}.json').write_text(json.dumps(report,indent=2),encoding='utf8')
