"""Owned-session native limb, gravity-gun, collision, fit and migration checks."""
import json,time,subprocess,sys
from gamectl import execute,ROOT

def lua(realm,code):
 r=execute(realm,code,45)
 if not r['ok']:raise RuntimeError(r.get('error'))
 return r.get('value')
def wait(realm,code,seconds=20):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  value=lua(realm,code)
  if value:return value
  time.sleep(.2)
 raise TimeoutError(code)
report=[]
def check(value,name):
 report.append({'name':name,'passed':bool(value),'value':value})
 (ROOT/'validation/native-interactions.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 print(('PASS ' if value else 'FAIL ')+name,flush=True)
 assert value,name
asset=json.loads((ROOT/'validation/acceptance-models.json').read_text(encoding='utf-8'))['Xin']['asset']
subprocess.run(['powershell','-NoProfile','-File',str(ROOT/'scripts/game-focus.ps1')],check=True)
lua('client',"gui.HideGameUI() g_SpawnMenu:Close() for _,n in ipairs({'MMDHL.TestCamera','MMDHL.StressCamera','MMDHL.VisualCamera'}) do hook.Remove('CalcView',n) end return true")
def spawn(frozen=True):
 e=lua('server',f"for _,e in ipairs(mmdhl.Entities()) do e:Remove() end local p=player.GetHumans()[1] local e=mmdhl.SpawnNative(p,'{asset}',{{scaleMultiplier=1,position={{0,0,-12270}},frozen={str(frozen).lower()}}}) mmdhl.testEnt=e return e:EntIndex()")
 time.sleep(.4)
 return e
mega_default=lua('server',"return GetConVar('physcannon_mega_enabled'):GetString()")
try:
 for index in ([] if '--skip-limbs' in sys.argv else [7,14,1]):
  spawn()
  lua('server',f'MMDHL_TEST_PHYS={index}\n'+(ROOT/'tests/game/native-physgun.lua').read_text(encoding='utf-8-sig'))
  wait('server','return mmdhl.nativeGun.done',15)
  gun=lua('server','return mmdhl.nativeGun')
  print(json.dumps(gun),flush=True)
  check(gun if gun['pickups']>0 and gun['drops']>0 and gun['maxHeight']>gun['startHeight']+8 and any(gun['frozen']) and not all(gun['frozen']) else False,f'Actual physgun interaction with body {index}')
 spawn()
 lua('server',"RunConsoleCommand('physcannon_mega_enabled','1') return true")
 time.sleep(.2)
 lua('server',"""local e=mmdhl.testEnt local p=player.GetHumans()[1] local head=e:GetPhysicsObjectNum(3)
 local aim=LocalToWorld(head:GetMassCenter(),angle_zero,head:GetPos(),head:GetAngles())
 p:SetMoveType(MOVETYPE_NOCLIP) p:SetPos(aim-Vector(110,0,64)) p:SetEyeAngles(Angle(0,0,0)) p:Give('weapon_physcannon') p:SelectWeapon('weapon_physcannon')
 MMDHL_GRAV={picked=0,dropped=0,punted=0,peak=0} local s=MMDHL_GRAV local started=CurTime()
 hook.Add('GravGunOnPickedUp','MMDHL.GravityProbe',function(_,ent) if ent==e then s.picked=s.picked+1 end end)
 hook.Add('GravGunOnDropped','MMDHL.GravityProbe',function(_,ent) if ent==e then s.dropped=s.dropped+1 end end)
 hook.Add('GravGunPunt','MMDHL.GravityProbe',function(_,ent) if ent==e then s.punted=s.punted+1 end end)
 hook.Add('StartCommand','MMDHL.GravityInput',function(ply,cmd) if ply~=p then return end local t=CurTime()-started cmd:ClearButtons() cmd:ClearMovement()
  if t<.4 then return end
  if not s.released then for i=0,17 do local b=e:GetPhysicsObjectNum(i) b:EnableMotion(true) b:Wake() end s.released=true end
  local target=LocalToWorld(head:GetMassCenter(),angle_zero,head:GetPos(),head:GetAngles())
  local view=s.picked==0 and (target-p:EyePos()):Angle() or Angle(-20,0,0) cmd:SetViewAngles(view) p:SetEyeAngles(view)
  local tr=util.TraceLine({start=p:GetShootPos(),endpos=p:GetShootPos()+view:Forward()*300,filter=p,mask=MASK_SHOT}) s.trace={entity=IsValid(tr.Entity) and tr.Entity:GetClass() or 'world',physics=tr.PhysicsBone,position={tr.HitPos:Unpack()},distance=tr.HitPos:Distance(p:GetShootPos())} s.allowed=hook.Run('GravGunPickupAllowed',p,e)
  if t<2.7 then if t%.5<.25 then cmd:SetButtons(IN_ATTACK2) end elseif t<3 then cmd:SetButtons(IN_ATTACK) elseif t>3.5 then hook.Remove('StartCommand','MMDHL.GravityInput') s.done=true end
  s.peak=math.max(s.peak,head:GetVelocity():Length())
 end) return true""")
 wait('server','return MMDHL_GRAV.done',10)
 print('Gravity gun',lua('server','return MMDHL_GRAV'),flush=True)
 if '--skip-gravity' not in sys.argv:check(lua('server','return (MMDHL_GRAV.picked>0 and MMDHL_GRAV.dropped>0 or MMDHL_GRAV.punted>0) and MMDHL_GRAV.peak>10'),'Super Gravity Gun picks up or punts native flesh ragdoll')
 e=spawn()
 check(lua('server',"local e=mmdhl.testEnt local p=ents.Create('prop_physics') p:SetModel('models/props_c17/oildrum001.mdl') p:SetPos(e:GetPos()+Vector(100,0,0)) p:Spawn() local player=player.GetHumans()[1] player:Give('gmod_tool') local t=player:GetWeapon('gmod_tool'):GetToolObject('rope') t:ClearObjects() local a=t:LeftClick({Entity=e,PhysicsBone=11,HitPos=e:GetPhysicsObjectNum(11):GetPos(),HitNormal=Vector(0,0,1)}) local b=t:LeftClick({Entity=p,PhysicsBone=0,HitPos=p:GetPos(),HitNormal=Vector(0,0,1)}) local ok=a and b and constraint.HasConstraints(e) p:Remove() return ok"),'Stock Rope tool binds a native hand')
 lua('server',"local e=mmdhl.testEnt MMDHL_CONTACT={world=0,prop=0} e:AddCallback('PhysicsCollide',function(_,d) if d.HitEntity:IsWorld() then MMDHL_CONTACT.world=MMDHL_CONTACT.world+1 else MMDHL_CONTACT.prop=MMDHL_CONTACT.prop+1 end end) for i=0,17 do e:GetPhysicsObjectNum(i):EnableMotion(true) e:GetPhysicsObjectNum(i):Wake() end return true")
 wait('server','return MMDHL_CONTACT.world>0')
 check(lua('server','return MMDHL_CONTACT.world'),'Native ragdoll collides with terrain')
 lua('server',"local e=mmdhl.testEnt local head=e:GetPhysicsObjectNum(1) MMDHL_PROP=ents.Create('prop_physics') local p=MMDHL_PROP p:SetModel('models/props_c17/oildrum001.mdl') p:SetPos(head:GetPos()+Vector(0,0,90)) p:Spawn() p:GetPhysicsObject():EnableGravity(false) p:GetPhysicsObject():SetVelocity(Vector(0,0,-300)) return true")
 wait('server','return MMDHL_CONTACT.prop>0')
 check(lua('server','return MMDHL_CONTACT.prop'),'Native ragdoll collides with an incoming prop')
 lua('server',"if IsValid(MMDHL_PROP) then MMDHL_PROP:Remove() end local e=mmdhl.testEnt local body=e:GetPhysicsObjectNum(3) local damage=DamageInfo() damage:SetDamage(10) damage:SetDamageType(DMG_BLAST) damage:SetDamagePosition(body:GetPos()) damage:SetDamageForce(Vector(2000,0,1000)) MMDHL_DAMAGE_START=body:GetVelocity() e:TakeDamageInfo(damage) return true")
 time.sleep(.15)
 check(lua('server','return (mmdhl.testEnt:GetPhysicsObjectNum(3):GetVelocity()-MMDHL_DAMAGE_START):Length()>1'),'Damage force reaches Source ragdoll physics')
 e=spawn()
 saved=lua('server',f"return file.Read('mmd_hotloader/fit_overrides/{asset}.json','DATA') or false")
 original=lua('server','return mmdhl.GetRig(mmdhl.testEnt).key')
 try:
  lua('client',f"local e=Entity({e}) MMDHL_FIT_PANEL=vgui.Create('DFrame') MMDHL_FIT_PANEL:SetSize(550,440) mmdhl.CollisionEditor(MMDHL_FIT_PANEL,e) local combo for _,p in ipairs(MMDHL_FIT_PANEL:GetChildren()) do if p.ChooseOptionID~=nil then combo=p end end assert(combo) combo:ChooseOptionID(7) local b=mmdhl.GetRig(e).bodies[7] e.MMDHLFitOverrides[b.name].extent[2]=b.extent[2]*1.05 for _,p in ipairs(MMDHL_FIT_PANEL:GetChildren()) do if p.GetText and p:GetText()=='Save fit and spawn corrected copy' then p:DoClick() end end return true")
  wait('server','return #mmdhl.Entities()==2')
  check(lua('server',"local a=mmdhl.testEnt for _,b in ipairs(mmdhl.Entities()) do if b~=a then return b:GetPhysicsObjectCount()==18 and mmdhl.GetRig(b).key~=mmdhl.GetRig(a).key end end"),'Collision editor caches correction and spawns a distinct 18-body carrier')
 finally:
  lua('server',f"local path='mmd_hotloader/fit_overrides/{asset}.json' "+('file.Write(path,'+json.dumps(saved)+')' if saved else 'file.Delete(path)')+' return true')
  lua('client',"if IsValid(MMDHL_FIT_PANEL) then MMDHL_FIT_PANEL:Remove() end return true")
 native_default=lua('server',"return GetConVar('mmdhl_native_carrier'):GetString()")
 try:
  lua('server',"RunConsoleCommand('mmdhl_native_carrier','1') return true")
  time.sleep(.1)
  data={'EntityMods':{'MMDHL':{'asset':asset,'options':{'height':72,'position':[0,0,-12270],'frozen':True},'state':{'frozen':True,'center':[0,0,-12270],'morphs':[.4]}}}}
  migrated=lua('server',"local data=util.JSONToTable("+json.dumps(json.dumps(data))+") data.Pos=Vector(100,0,-12270) local factory=duplicator.FindEntityClass('mmdhl_ragdoll').Func local e=factory(player.GetHumans()[1],data) local ok=IsValid(e) and e:GetClass()=='prop_ragdoll' and e:GetPhysicsObjectCount()==18 and math.abs(mmdhl.GetMorphWeight(e,0)-.4)<.001 if IsValid(e) then e:Remove() end return ok")
  check(migrated,'Legacy duplicate factory migrates to native ragdoll with morph state')
 finally:lua('server',"RunConsoleCommand('mmdhl_native_carrier',"+json.dumps(native_default)+') return true')
finally:
 lua('server',"RunConsoleCommand('physcannon_mega_enabled',"+json.dumps(mega_default)+") return true")
 lua('server',"MMDHL_TEST_PHYS=nil for _,name in ipairs({'MMDHL.TestInput','MMDHL.GravityInput'}) do hook.Remove('StartCommand',name) end hook.Remove('GravGunOnPickedUp','MMDHL.GravityProbe') hook.Remove('GravGunOnDropped','MMDHL.GravityProbe') hook.Remove('GravGunPunt','MMDHL.GravityProbe') for _,e in ipairs(mmdhl.Entities()) do e:Remove() end if IsValid(MMDHL_PROP) then MMDHL_PROP:Remove() end return true")
