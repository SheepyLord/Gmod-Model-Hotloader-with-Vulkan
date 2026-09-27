"""Exercise menu placement options across the real client/server boundary."""
import json,pathlib,runpy
from gamectl import ROOT,read
c=runpy.run_path(str(ROOT/'scripts/test-compatibility.py'));lua,wait=c['lua'],c['wait']
asset=read(ROOT/'validation/acceptance-models.json')['Cyrene']['asset'];session=read(ROOT/'validation/session.json')
p=pathlib.Path(session['cache'])/'library'/(asset+'.json');saved=p.read_bytes() if p.exists() else None
try:
 lua('server',"for _,e in ipairs(mmdhl.Entities()) do e:Remove() end local p=player.GetHumans()[1] local t=util.TraceLine({start=Vector(0,0,2048),endpos=Vector(0,0,-30000),mask=MASK_SOLID_BRUSHONLY}) p:SetMoveType(MOVETYPE_NOCLIP) p:SetPos(t.HitPos+Vector(0,0,136)) p:SetEyeAngles(Angle(45,0,0)) return true")
 lua('client',f"mmdhl.Open() mmdhl.lastSpawnStatus=nil local p=mmdhl.window.Library p:SelectAsset('{asset}') p.ScaleMultiplier:SetValue(1.25) p.Frozen:SetChecked(true) p.CollisionMode:ChooseOptionID(3) p.Spawn:DoClick() return true")
 wait('client',"local s=mmdhl.lastSpawnStatus if s and s.state=='error' then error(s.message) end return s and s.state=='ready'")
 result=lua('server',"local e=mmdhl.Entities()[1] assert(IsValid(e) and mmdhl.GetSecondaryCollisionMode(e)==2) assert(math.abs(mmdhl.GetRig(e).scaleMultiplier-1.25)<.001) return {bodies=e:GetPhysicsObjectCount(),mode=mmdhl.GetSecondaryCollisionMode(e),scale=mmdhl.GetRig(e).scaleMultiplier}")
 result['restoredInMenu']=lua('client',f"mmdhl.CloseLibrary() if IsValid(mmdhl.window) then mmdhl.window:Remove() end mmdhl.Open() local p=mmdhl.window.Library p:SelectAsset('{asset}') assert(p.secondaryCollision==2 and math.abs(p.ScaleMultiplier:GetValue()-1.25)<.001 and p.Frozen:GetChecked()) return true")
 (ROOT/'validation/compatibility-spawn-settings.json').write_text(json.dumps(result,indent=2),encoding='utf8');print('PASS menu placement and persisted scale/freeze/collision mode',result)
finally:
 lua('server','for _,e in ipairs(mmdhl.Entities()) do e:Remove() end return true')
 lua('client','mmdhl.CloseLibrary() return true')
 if saved is not None:p.write_bytes(saved)
 else:p.unlink(missing_ok=True)
 lua('client','mmdhl.library.Refresh() return true')
