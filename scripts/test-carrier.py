"""First gate: engine-created bodies, bone queries, traces, constraints, and lifecycle."""
import json,pathlib,sys,time
from gamectl import execute,ROOT
def run(code):
    result=execute('server',code,45)
    if not result['ok']:raise RuntimeError(result.get('error'))
    return result.get('value')
def main():
    models=json.loads((ROOT/'validation/acceptance-models.json').read_text());results={}
    for name,entry in models.items():
        asset=entry['asset']
        result=run('''
if IsValid(MMDHL_CARRIER_ENTITY) then MMDHL_CARRIER_ENTITY:Remove() end
local raw,err=mmdhl.native.PrepareCarrier("'''+asset+'''","{}") assert(raw,err)
local rig=util.JSONToTable(raw) assert(game.MountGMA(rig.materialGma)) assert(game.MountGMA(rig.gma)) MMDHL_CARRIER=rig
local e=ents.Create('prop_ragdoll') e:SetModel(rig.model) e:SetPos(player.GetHumans()[1]:GetPos()+Vector(100,0,15)) e:Spawn()
assert(e:GetPhysicsObjectCount()==18,'Incorrect object count') MMDHL_CARRIER_ENTITY=e
local hits,objects=0,{}
for i=0,17 do local p=e:GetPhysicsObjectNum(i) assert(IsValid(p)) p:EnableMotion(false)
 local bone=e:TranslatePhysBoneToBone(i) assert(e:GetBoneName(bone)==rig.bodies[i+1].name,'Bone mapping mismatch')
 assert(e:TranslateBoneToPhysBone(bone)==i,'Inverse mapping mismatch')
 local center=LocalToWorld(p:GetMassCenter(),angle_zero,p:GetPos(),p:GetAngles())
 local tr=util.TraceLine({start=center+Vector(80,0,0),endpos=center,filter=function(other) return other==e end})
 if tr.Entity==e and tr.PhysicsBone>=0 and tr.PhysicsBone<18 then hits=hits+1 end
 objects[#objects+1]={index=i,bone=bone,mass=p:GetMass(),hulls=#p:GetMeshConvexes(),position=p:GetPos()}
end
assert(hits>=16,'Missing collision traces')
local weld=constraint.Weld(e,game.GetWorld(),0,0,0,false,false) assert(IsValid(weld),'Weld failed') weld:Remove()
local rope=constraint.Rope(e,game.GetWorld(),7,0,Vector(),e:GetPos()+Vector(0,0,80),90,0,0,1,'cable/cable2',false) assert(IsValid(rope),'Rope failed') rope:Remove()
e:GetPhysicsObjectNum(7):EnableMotion(true) assert(not e:GetPhysicsObjectNum(0):IsMotionEnabled(),'Per-body freeze failed') e:GetPhysicsObjectNum(7):EnableMotion(false)
return {class=e:GetClass(),objects=objects,traces=hits,bones=e:GetBoneCount(),flexes=e:GetFlexNum(),key=rig.key}
''')
        results[name]=result;print(name,'18 objects,',result['traces'],'traces, constraints passed',flush=True)
    run('''MMDHL_CARRIER_CYCLES={completed=0,errors={}} local model=MMDHL_CARRIER.model
timer.Create('MMDHL.CarrierCycles',.1,50,function() local e=ents.Create('prop_ragdoll') e:SetModel(model) e:SetPos(player.GetHumans()[1]:GetPos()+Vector(120,0,30)) e:Spawn() if e:GetPhysicsObjectCount()~=18 then table.insert(MMDHL_CARRIER_CYCLES.errors,'count') end e:Remove() MMDHL_CARRIER_CYCLES.completed=MMDHL_CARRIER_CYCLES.completed+1 end) return true''')
    time.sleep(6);results['cycles']=run('return MMDHL_CARRIER_CYCLES')
    assert results['cycles']['completed']==50 and not results['cycles']['errors']
    (ROOT/'validation/carrier-gate.json').write_text(json.dumps(results,indent=2),encoding='utf-8');print('50 create/remove cycles passed')
if __name__=='__main__':main()
