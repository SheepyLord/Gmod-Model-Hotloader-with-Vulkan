"""Gameplay gate for the generated prop_ragdoll, with actual Sandbox tool handlers."""
import argparse,atexit,json,time,subprocess
from gamectl import execute,ROOT
p=argparse.ArgumentParser();p.add_argument('model',choices=['Xin','Cyrene','Sandrone']);args=p.parse_args()
models=json.loads((ROOT/'validation/acceptance-models.json').read_text(encoding='utf-8'));asset=models[args.model]['asset'];report={'model':args.model,'checks':[]}
def lua(realm,code):
 r=execute(realm,code,45)
 if not r['ok']:raise RuntimeError(r.get('error'))
 return r.get('value')
def wait(realm,code,seconds=30):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  r=lua(realm,code)
  if r:return r
  time.sleep(.2)
 raise TimeoutError(code)
def check(ok,label,data=None):
 report['checks'].append({'name':label,'passed':bool(ok),'data':data})
 (ROOT/f'validation/native-tools-{args.model}.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
 print(('PASS ' if ok else 'FAIL ')+label,flush=True)
 if not ok:raise AssertionError(label)
# Bring the owned game forward with the computer-use tool before this probe.
wait('server','return IsValid(player.GetHumans()[1])')
native_default=lua('server',"return GetConVar('mmdhl_native_carrier'):GetString()")
lua('client',"hook.Remove('CalcView','MMDHL.TestCamera') hook.Remove('CalcView','MMDHL.StressCamera') return true")
saved=lua('client',"local out={} for _,n in ipairs({'gmod_toolmode','faceposer_scale','eyeposer_x','eyeposer_y','eyeposer_strabismus','finger_3'}) do local c=GetConVar(n) if c then out[n]=c:GetString() end end for i=0,95 do local n='faceposer_flex'..i local c=GetConVar(n) if c then out[n]=c:GetString() end end return out")
def restore():
 try:
  lua('client','for k,v in pairs(util.JSONToTable('+json.dumps(json.dumps(saved))+')) do RunConsoleCommand(k,v) end return true')
  lua('server',"hook.Remove('StartCommand','MMDHL.TestInput') hook.Remove('OnPhysgunPickup','MMDHL.TestPickup') hook.Remove('PhysgunDrop','MMDHL.TestDrop') timer.Remove('MMDHL.NativeGunSamples') RunConsoleCommand('mmdhl_native_carrier',"+json.dumps(native_default)+") return true")
 except Exception:pass
atexit.register(restore)
lua('server',"""for _,e in ipairs(mmdhl.Entities()) do e:Remove() end
if IsValid(MMDHL_CARRIER_ENTITY) then MMDHL_CARRIER_ENTITY:Remove() end
RunConsoleCommand('mmdhl_native_carrier','1') local p=player.GetHumans()[1]
local origin=game.GetMap()=='gm_construct' and Vector(822,-800,0) or Vector()
local ground=util.TraceLine({start=origin+Vector(0,0,4096),endpos=origin-Vector(0,0,30000),mask=MASK_SOLID_BRUSHONLY})
assert(ground.Hit and not ground.StartSolid,'Test setup has no valid ground')
p:Give('gmod_tool') p:SelectWeapon('gmod_tool') p:SetMoveType(MOVETYPE_NOCLIP)
p:SetPos(ground.HitPos+Vector(0,0,136)) p:SetEyeAngles(Angle(45,0,0)) return true""")
# Invoke the same library row handler the user uses, including its network spawn request.
lua('client',"g_SpawnMenu:Open() g_SpawnMenu:OpenCreationMenuTab('MMD') return true")
wait('client',"return IsValid(mmdhl.spawnPanel) and IsValid(mmdhl.spawnPanel.Library)")
opened=lua('client',f"""local p=mmdhl.spawnPanel.Library p:SelectAsset('{asset}')
 local list=p.Models for i,row in ipairs(list:GetLines()) do if row.asset=='{asset}' then list:DoDoubleClick(i,row) return true end end return false""")
assert opened,'Model is missing from the library'
wait('client',"local s=mmdhl.lastSpawnStatus if not s then return false end if s.state=='error' then error(s.message) end return s.state=='ready'")
wait('server',"for _,e in ipairs(mmdhl.Entities()) do if e:GetClass()=='prop_ragdoll' then mmdhl.testEnt=e return true end end")
check(True,'Q → MMD library places selected model')
entity=lua('server','return mmdhl.testEnt:EntIndex()')
wait('client',f'return mmdhl.IsMMD(Entity({entity})) and Entity({entity}):GetBoneCount()>18')
rig=lua('server','return mmdhl.GetRig(mmdhl.testEnt)');check(lua('server','return mmdhl.testEnt:GetPhysicsObjectCount()')==18,'Native ragdoll has 18 engine physics objects')
check(lua('server','return mmdhl.testEnt:GetFlexNum()')==rig['nativeFlexCount']<=96,'Native flex count respects the 96-controller limit')
# Actual engine physgun picks up, rotates, moves and freezes a body.
lua('server',"for i=0,17 do mmdhl.testEnt:GetPhysicsObjectNum(i):EnableMotion(false) end return true")
lua('server',(ROOT/'tests/game/native-physgun.lua').read_text(encoding='utf-8-sig'))
wait('server','return mmdhl.nativeGun.done',15);gun=lua('server','return mmdhl.nativeGun')
check(gun['pickups']>0 and gun['drops']>0 and gun['maxHeight']>gun['startHeight']+10 and gun['maxRotation']>10,'Actual Physics Gun grabs, lifts, rotates, and drops a native limb',gun)
check(any(gun['frozen']) and not all(gun['frozen']),'Physgun freezes individual bodies')
lua('server',"local p=player.GetHumans()[1] p:SelectWeapon('gmod_tool') for i=0,17 do mmdhl.testEnt:GetPhysicsObjectNum(i):EnableMotion(false) end return true")
lua('server',"local t=player.GetHumans()[1]:GetWeapon('gmod_tool'):GetToolObject('faceposer') assert(t:RightClick({Entity=mmdhl.testEnt})) return true")
lua('client',"RunConsoleCommand('gmod_toolmode','faceposer') RunConsoleCommand('faceposer_flex0','.7') RunConsoleCommand('faceposer_scale','1') return true")
time.sleep(1.4)
lua('server',"player.GetHumans()[1]:GetWeapon('gmod_tool'):GetToolObject('faceposer'):Think() return true")
check(lua('server','return math.abs(mmdhl.testEnt:GetFlexWeight(0)-.7)<.01'),'Stock Face Poser changes native controller')
check(lua('client',f"local e=Entity({entity}) LocalPlayer():GetWeapon('gmod_tool'):GetToolObject('faceposer'):RebuildControlPanel(e) return IsValid(controlpanel.Get('faceposer'))"),'Stock Face Poser panel builds with MMD extension')
overflow=[m for m in rig['morphs'] if m['native']<0]
if overflow:
 idx=overflow[0]['mmd'];lua('client',f'mmdhl.SetMorphWeight(Entity({entity}),{idx},.42) return true');time.sleep(.3)
 check(lua('server',f'return math.abs(mmdhl.GetMorphWeight(mmdhl.testEnt,{idx})-.42)<.01'),'Overflow controller updates without reassigning native IDs')
lua('server',"local e=mmdhl.testEnt local t=player.GetHumans()[1]:GetWeapon('gmod_tool'):GetToolObject('finger') assert(t:RightClick({Entity=e,HitPos=e:GetBonePosition(e:LookupBone('ValveBiped.Bip01_L_Hand'))})) return true")
time.sleep(.7)
lua('client',"RunConsoleCommand('finger_3','40 0') return true");time.sleep(.4)
finger=lua('server',"local e=mmdhl.testEnt local t=player.GetHumans()[1]:GetWeapon('gmod_tool'):GetToolObject('finger') t:ApplyValues(e,0) local b=e:LookupBone('ValveBiped.Bip01_L_Finger1') return {angle=e:GetManipulateBoneAngles(b).p,bone=b,mmd=mmdhl.GetRig(e).bones[b+1].mmd} ")
check(abs(finger['angle']-40)<.01 and finger['mmd']>=0,'Stock Finger Poser changes mapped ValveBiped fingers',finger)
lua('server',"local e=mmdhl.testEnt local t=player.GetHumans()[1]:GetWeapon('gmod_tool'):GetToolObject('eyeposer') assert(t:RightClick({Entity=e})) e:SetEyeTarget(Vector(100,40,20)) return true")
check(lua('server',"return mmdhl.testEnt.MMDEyeTarget==Vector(100,40,20)"),'Stock Eye Poser accepts the native eyes attachment')
constraints=lua('server',"local e=mmdhl.testEnt local p=ents.Create('prop_physics') p:SetModel('models/props_c17/oildrum001.mdl') p:SetPos(e:GetPos()+Vector(90,0,10)) p:Spawn() local w=constraint.Weld(e,p,7,0,0,false,false) local r=constraint.Rope(e,p,11,0,Vector(),Vector(),120,0,0,1,'cable/cable2',false) local ok=IsValid(w) and IsValid(r) if IsValid(w) then w:Remove() end if IsValid(r) then r:Remove() end p:Remove() return ok")
check(constraints,'Native weld and rope constraints accept limb indices')
lua('server',"local e=mmdhl.testEnt e:SetColor(Color(180,220,240,220)) e:SetMaterial('models/debug/debugwhite') e:SetSubMaterial(0,'models/wireframe') return true")
# Enabled expression addons can animate fingers between separate RPC steps.
# Apply the real Finger Poser and copy in the same tick to measure duplication.
dupe=lua('server',"local e=mmdhl.testEnt player.GetHumans()[1]:GetWeapon('gmod_tool'):GetToolObject('finger'):ApplyValues(e,0) assert(math.abs(e:GetManipulateBoneAngles(e:LookupBone('ValveBiped.Bip01_L_Finger1')).p-40)<.01) local copied=duplicator.Copy(e) local pasted=duplicator.Paste(player.GetHumans()[1],copied.Entities,copied.Constraints) local clone=pasted[e:EntIndex()] assert(IsValid(clone),'No duplicate') mmdhl.testClone=clone return {native=mmdhl.IsMMD(clone),bodies=clone:GetPhysicsObjectCount(),flex=clone:GetFlexWeight(0),finger=clone:GetManipulateBoneAngles(clone:LookupBone('ValveBiped.Bip01_L_Finger1')).p,color=clone:GetColor(),material=clone:GetMaterial(),submaterial=clone:GetSubMaterial(0),instances=mmdhl.Decode(mmdhl.native.GetDiagnostics()).instances}")
check(dupe['native'] and dupe['bodies']==18 and abs(dupe['flex']-.7)<.01 and abs(dupe['finger']-40)<.01,'Sandbox duplication preserves native carrier, morphs and fingers',dupe)
lua('server','mmdhl.testClone:Remove() mmdhl.testEnt:SetMaterial("") mmdhl.testEnt:SetSubMaterial(0,"") mmdhl.testEnt:SetColor(color_white) return true')
check(lua('server',"return player.GetHumans()[1]:GetWeapon('gmod_tool'):GetToolObject('remover'):LeftClick({Entity=mmdhl.testEnt,HitPos=mmdhl.testEnt:GetPos(),PhysicsBone=0})"),'Stock Remover accepts native MMD ragdoll')
wait('server','return not IsValid(mmdhl.testEnt)')
check(lua('server','return mmdhl.Decode(mmdhl.native.GetDiagnostics()).instances')==0,'Removal releases both native and nanoem state')
print(f'{len(report["checks"])} native gameplay checks passed')
