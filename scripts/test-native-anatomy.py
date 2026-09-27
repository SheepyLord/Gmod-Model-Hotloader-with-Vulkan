"""Verify the actual carrier/skin transforms, anatomical axes and self-contact.

Run in an owned visible session. Uses all three full acceptance rigs; contact
is tested on a disposable bare carrier so joint forces cannot fake a pass.
"""
import json, subprocess, time
from gamectl import execute, ROOT

def lua(realm, code):
    r=execute(realm, code,45)
    if not r['ok']: raise RuntimeError(r.get('error'))
    return r.get('value')

def wait(code, timeout=25):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        value=lua('server',code)
        if value: return value
        time.sleep(.3)
    raise TimeoutError(code)

subprocess.run(['powershell','-NoProfile','-File',str(ROOT/'scripts/game-focus.ps1')],check=True)
models=json.loads((ROOT/'validation/acceptance-models.json').read_text(encoding='utf-8'))
report={}
try:
    lua('client',"gui.HideGameUI() if IsValid(g_SpawnMenu) then g_SpawnMenu:Close() end return true")
    for name, model in models.items():
        lua('server',f"""for _,e in ipairs(mmdhl.Entities()) do e:Remove() end
if IsValid(MMDHL_CARRIER_ENTITY) then MMDHL_CARRIER_ENTITY:Remove() end
local p=player.GetHumans()[1] MMDHL_READY=nil
mmdhl.Spawn(p,'{model['asset']}',{{backend='source',scaleMultiplier=1,position={{0,0,50}},frozen=true}},function(e,err) assert(IsValid(e),err) MMDHL_CARRIER_ENTITY=e MMDHL_CARRIER=mmdhl.GetRig(e) MMDHL_READY=true end)
return true""")
        wait('return MMDHL_READY')
        time.sleep(.2)
        data=lua('server',"""local e=MMDHL_CARRIER_ENTITY local rig=MMDHL_CARRIER local out={bodies={},key=rig.key,generator=rig.generator}
out.pairs=mmdhl.Decode(mmdhl.native.ProbeCarrierCollisions(e:GetInternalVariable('m_nModelIndex')))
for i=0,17 do local p=e:GetPhysicsObjectNum(i) local body=rig.bodies[i+1] local b=rig.bones[body.bone+1]
 local live=mmdhl.Decode(mmdhl.native.GetBoneTransform(mmdhl.GetInstance(e),b.mmd))
 local lo,hi=Vector(1e6,1e6,1e6),Vector(-1e6,-1e6,-1e6)
 for _,h in ipairs(p:GetMeshConvexes()) do for _,v in ipairs(h) do for k=1,3 do lo[k]=math.min(lo[k],v.pos[k]) hi[k]=math.max(hi[k],v.pos[k]) end end end
 out.bodies[#out.bodies+1]={name=b.name,pivotError=p:GetPos():Distance(Vector(unpack(live.position))),serializedCenterError=((lo+hi)*.5):Distance(Vector(unpack(body.center))),x={p:GetAngles():Forward():Unpack()},z={p:GetAngles():Up():Unpack()}}
end return out""")
        assert data['generator']>=9
        assert max(b['pivotError'] for b in data['bodies'])<.01, data
        assert max(b['serializedCenterError'] for b in data['bodies'])<.002, data
        assert data['pairs']['count']==136, data['pairs']
        pairs={tuple(p) for p in data['pairs']['enabledPairs']}
        assert all(p in pairs for p in [(2,7),(2,11),(0,13),(0,16),(13,16)])
        for b in data['bodies']:
            if 'Calf' in b['name']:
                assert abs(b['z'][1])>.98 and abs(b['z'][0])<.05, b
        lua('server',(ROOT/'tests/game/native-limits.lua').read_text())
        wait('return MMDHL_LIMITS.done')
        data['limits']=lua('server','return MMDHL_LIMITS')
        for sample in data['limits']['samples']:
            # Drive both sides of the intended hinge to its real engine stops.
            limit=sample['upper'][2] if sample['sign']>0 else sample['lower'][2]
            reached=sample['maximum'][2] if sample['sign']>0 else sample['minimum'][2]
            assert abs(reached-limit)<3, (name,sample)
        lua('server',"""MMDHL_CARRIER_ENTITY:Remove() local e=ents.Create('prop_ragdoll')
e:SetModel(MMDHL_CARRIER.model) e:SetPos(Vector(0,0,150)) e:Spawn() MMDHL_CARRIER_ENTITY=e return true""")
        lua('server',(ROOT/'tests/game/native-self-contact.lua').read_text())
        wait('return MMDHL_SELF_CONTACT.done',8)
        data['selfContact']=lua('server','return MMDHL_SELF_CONTACT')
        assert data['selfContact']['contacts']>0 and data['selfContact']['furthest']<0, data['selfContact']
        report[name]=data
        (ROOT/'validation/native-anatomy.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        print(name, 'PASS: 18 aligned pivots/hulls, lateral knees, 8 hinge stops, 136 engine collision pairs, hand stopped by torso',flush=True)
finally:
    lua('server',"timer.Remove('MMDHL.SelfContact') timer.Remove('MMDHL.LimitStress') if IsValid(MMDHL_CARRIER_ENTITY) then MMDHL_CARRIER_ENTITY:Remove() end return true")
