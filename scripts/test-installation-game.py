"""Functional smoke test inside an owned game-start.ps1 session.

Native files that native_policy.lua does not know (a local or GitHub Actions build
that the policy has no record of) load with warnings that disable nothing, so the
session must have no problems, while warnings are allowed (none about the files
themselves when they are a recorded release)."""
import json,time
from gamectl import execute,ROOT

def problems(status):return [x for x in status['issues'] if not x.get('warning')]
def call(realm,code):
    result=execute(realm,code)
    if not result['ok']:raise RuntimeError(result)
    return result.get('value')
def wait(realm,code,timeout=45):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        result=call(realm,code)
        if result:return result
        time.sleep(.2)
    raise TimeoutError(code)
def import_model(relative,options):
    source=(ROOT/relative).as_posix()
    job=call('client',f"local h,e=mmdhl.native.BeginImport([[{source}]],[[{json.dumps(options)}]]) assert(h,e) return h")
    result=wait('client',f"local s,e=mmdhl.Decode(mmdhl.native.PollJob({job})) assert(s,e) if s.state~='running' then return s end")
    assert result['state']=='complete',result
    return result['asset']

report={}
report['client']=wait('client',"local s=mmdhl.GetInstallationStatus() if s.features.imports then return s end")
report['server']=call('server',"return mmdhl.GetInstallationStatus()")
for realm in ('client','server'):
    status=report[realm]
    assert status['features']['core'] and not problems(status),(realm,status['issues'])
    # A recorded release must be complete: files that are no release this addon knows
    # (identity warnings) are expected only when the realm module is none either.
    assert not status.get('installed') or not [x for x in status['issues'] if x.get('identity')],(realm,status['issues'])
report['character']=import_model('tests/fixtures/native-chain.pmx',{})
asset=report['character']
call('server',f"mmdhl.Spawn(Entity(1),'{asset}',{{position={{0,0,80}},frozen=true}},function(e,err) mmdhl.installTestEntity=e mmdhl.installTestError=err end) return true")
report['ragdoll']=wait('server',"if mmdhl.installTestError then error(mmdhl.installTestError) end local e=mmdhl.installTestEntity if IsValid(e) then return {class=e:GetClass(),bodies=e:GetPhysicsObjectCount()} end")
assert report['ragdoll']['bodies']==18,report['ragdoll']
report['prop']=import_model('tests/fixtures/props/cube.obj',{'kind':'static','collision':'balanced'})
prop=report['prop']
report['propPhysics']=call('server',f"local e,err=mmdhl.props.CreateProp(Entity(1),'{prop}',Vector(90,0,80),angle_zero,1,{{wait=true,frozen=true}}) assert(IsValid(e),err) mmdhl.installTestProp=e return {{bodies=e:GetPhysicsObjectCount(),model=e:GetModel()}}")
assert report['propPhysics']['bodies']>0
call('client',"mmdhl.Open() gui.HideGameUI() if IsValid(g_SpawnMenu) then g_SpawnMenu:Close() end hook.Add('CalcView','MMDHL.InstallationCamera',function() return {origin=Vector(230,210,150),angles=(Vector(45,0,80)-Vector(230,210,150)):Angle(),fov=55} end) return true")
report['render']=wait('client',"local r=mmdhl.Decode(mmdhl.native.RenderStats()) if r.draws+r.liveDraws>0 then return r end")
assert not report['render'].get('sourceShadowError'),report['render']
# Recheck reads the files again: no new problem, and the same warnings as just before it.
report['recheck']=call('client',"local function warnings() local t={} for _,v in ipairs(mmdhl.GetInstallationStatus().issues) do if v.warning then t[#t+1]=v.code..'/'..v.component end end table.sort(t) return t end "
    "local before,was=mmdhl.native,warnings() mmdhl.CheckInstallation(true) "
    "return {sameModule=before==mmdhl.native,issues=mmdhl.GetInstallationStatus().issues,before=was,after=warnings()}")
assert report['recheck']['sameModule'] and not problems(report['recheck']) and report['recheck']['before']==report['recheck']['after'],report['recheck']
call('client',"RunConsoleCommand('mmdhl_debug_capture','installation-library','ui') return true")
call('server',"mmdhl.installTestEntity:Remove() mmdhl.installTestProp:Remove() return true")
(ROOT/'validation/installation-game-smoke.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('PASS: installation without problems (unknown files only warn), worker self-test, PMX/OBJ imports, 18-body ragdoll, detailed prop collision, rendering, shadow status and recheck without reload or new problems')
