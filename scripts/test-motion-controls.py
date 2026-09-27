"""Owned-game regression for reset isolation and generated alpha-test materials."""
import json, runpy, time
from gamectl import ROOT


def main():
    helper = runpy.run_path(str(ROOT / 'scripts/test-compatibility.py'))
    lua, spawn, wait = (helper[k] for k in ('lua', 'spawn', 'wait'))
    models = json.loads((ROOT / 'validation/acceptance-models.json').read_text(encoding='utf-8'))
    report = {}
    # A fixed appearance makes the reset assertion independent of automatic
    # expressions. The addon remains installed and its setting is restored.
    saved = lua('server', "local c=GetConVar('sv_rpe_enable') if c then local v=c:GetString() c:SetBool(false) return v end")
    try:
        lua('server', 'for _,e in ipairs(mmdhl.Entities()) do e:Remove() end return true')
        entity = spawn(models['Cyrene']['asset'])
        lua('client', f"""MMDHL_RESET_ENTITY=Entity({entity}) local e=MMDHL_RESET_ENTITY
for _,n in ipairs({{'MMDHL.TestCamera','MMDHL.VisualCamera','MMDHL.StressCamera','MMDHL.MotionCamera'}}) do hook.Remove('CalcView',n) end
hook.Add('CalcView','MMDHL.ControlCamera',function() local center=e:WorldSpaceCenter() local offset=Vector(-180,15,20) return {{origin=center+offset,angles=(-offset):Angle(),fov=45,drawviewer=false}} end)
MMDHL_CONTROL_ORIGINAL_DRAW=mmdhl.DrawCarrier MMDHL_CONTROL_DRAWS=0 MMDHL_CONTROL_ERROR=nil
mmdhl.DrawCarrier=function(...) local ok,err=pcall(MMDHL_CONTROL_ORIGINAL_DRAW,...) MMDHL_CONTROL_DRAWS=MMDHL_CONTROL_DRAWS+1 if not ok then MMDHL_CONTROL_ERROR=err end end
return true""")
        time.sleep(.5)
        report['materials'] = lua('client', """local e=MMDHL_RESET_ENTITY local n=0
for _,path in ipairs(e:GetMaterials()) do local m=Material(path)
 local flags=m:GetInt('$flags') or 0
 assert(not m:IsError() and bit.band(flags,0x200000)==0 and bit.band(flags,0x100)~=0 and bit.band(flags,0x20000000)~=0,path) n=n+1
end
for _,m in ipairs(mmdhl.sourceMaterials[mmdhl.GetAsset(e)] or {}) do
 local flags=m:GetInt('$flags') or 0
 assert(bit.band(flags,0x200000)==0 and bit.band(flags,0x100)~=0 and bit.band(flags,0x20000000)~=0)
end
local custom=CreateMaterial('mmdhl_motion_override_check','UnlitGeneric',{['$basetexture']='models/debug/debugwhite',['$translucent']='1'})
assert(bit.band(custom:GetInt('$flags'),0x200000)~=0)
return {slots=n,generatedAlphaTest=true,explicitTranslucentOverride=true}""")
        lua('server', "mmdhl.testEnt:SetSubMaterial(0,'!mmdhl_motion_override_check') return true")
        wait('client', "return MMDHL_RESET_ENTITY:GetSubMaterial(0)=='!mmdhl_motion_override_check'")
        time.sleep(.4)
        report['overrideDraws'] = lua('client', "assert(not MMDHL_CONTROL_ERROR,MMDHL_CONTROL_ERROR) assert(MMDHL_CONTROL_DRAWS>0) return MMDHL_CONTROL_DRAWS")
        lua('server', """local e=mmdhl.testEnt local poses={}
e:SetSubMaterial(0,'')
for i=0,17 do local p=e:GetPhysicsObjectNum(i) poses[i]={object=p,position=p:GetPos(),angles=p:GetAngles(),motion=p:IsMotionEnabled()} end
MMDHL_RESET_POSES=poses e:SetColor(Color(120,180,240,255)) e:SetSubMaterial(40,'models/wireframe') e:SetBodygroup(41,1)
for _,m in ipairs(mmdhl.GetMorphs(e)) do mmdhl.SetMorphWeight(e,m.mmd,0) end mmdhl.SetMorphWeight(e,0,.37)
return true""")
        time.sleep(.3)
        before = lua('server', "return mmdhl.GetDiagnostics(mmdhl.testEnt,false)")
        lua('server', 'assert(mmdhl.ResetPhysics(mmdhl.testEnt)) return true')
        time.sleep(.6)
        after = lua('server', """local e=mmdhl.testEnt assert(e:GetPhysicsObjectCount()==18)
for i=0,17 do local p=e:GetPhysicsObjectNum(i) local old=MMDHL_RESET_POSES[i]
 assert(p==old.object and p:IsMotionEnabled()==old.motion and p:GetPos():Distance(old.position)<.001)
 assert(math.abs(math.AngleDifference(p:GetAngles().p,old.angles.p))<.001 and math.abs(math.AngleDifference(p:GetAngles().y,old.angles.y))<.001 and math.abs(math.AngleDifference(p:GetAngles().r,old.angles.r))<.001)
end
assert(e:GetColor()==Color(120,180,240,255) and e:GetSubMaterial(40)=='models/wireframe' and e:GetBodygroup(41)==1)
assert(math.abs(mmdhl.GetMorphWeight(e,0)-.37)<.001 and mmdhl.GetSecondaryCollisionMode(e)==2)
return mmdhl.GetDiagnostics(e,false)""")
        assert after['resets'] == before['resets'] + 1 and after['resetReason'] == 'manual', (before, after)
        assert after['debtSeconds'] < 1/60 + 1e-6 and not after['sourceError'], after
        report['reset'] = dict(resetsAdded=after['resets']-before['resets'], reason=after['resetReason'], nativeObjectsPreserved=18,
                               appearancePreserved=True, debtSeconds=after['debtSeconds'])
        report['passed'] = True
        print(json.dumps(report, indent=2))
    finally:
        lua('client', "hook.Remove('CalcView','MMDHL.ControlCamera') if MMDHL_CONTROL_ORIGINAL_DRAW then mmdhl.DrawCarrier=MMDHL_CONTROL_ORIGINAL_DRAW MMDHL_CONTROL_ORIGINAL_DRAW=nil end return true")
        lua('server', 'for _,e in ipairs(mmdhl.Entities()) do e:Remove() end MMDHL_RESET_POSES=nil return true')
        if saved is not None:
            lua('server', "GetConVar('sv_rpe_enable'):SetString(" + json.dumps(saved) + ") return true")
        (ROOT / 'validation/motion-controls.json').write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
