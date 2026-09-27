"""Repeatable Source-lighting, face/hand, collision and flashlight captures."""
import argparse,json,time,subprocess
from gamectl import execute,ROOT
def lua(realm,code):
 r=execute(realm,code,45)
 if not r['ok']:raise RuntimeError(r.get('error'))
 return r.get('value')
p=argparse.ArgumentParser();p.add_argument('model',choices=['Xin','Cyrene','Sandrone']);p.add_argument('--origin');p.add_argument('--prefix');a=p.parse_args()
asset=json.loads((ROOT/'validation/acceptance-models.json').read_text(encoding='utf-8'))[a.model]['asset'];origin=[float(v) for v in a.origin.split(',')] if a.origin else lua('server',"local at=game.GetMap()=='gm_construct' and Vector(822,-800,0) or vector_origin local tr=util.TraceLine({start=at+Vector(0,0,2048),endpos=at-Vector(0,0,30000),mask=MASK_SOLID_BRUSHONLY}) return {(tr.HitPos+Vector(0,0,12)):Unpack()}");prefix=a.prefix or a.model.lower()
# Focus the owned game with the computer-use tool before this probe.
entity=lua('server',f"for _,e in ipairs(mmdhl.Entities()) do e:Remove() end if IsValid(MMDHL_CARRIER_ENTITY) then MMDHL_CARRIER_ENTITY:Remove() end local p=player.GetHumans()[1] p:SetMoveType(MOVETYPE_NOCLIP) p:SetPos(Vector({origin[0]-250},{origin[1]},{origin[2]+70})) p:SetEyeAngles(Angle(5,0,0)) local e=mmdhl.SpawnNative(p,'{asset}',{{scaleMultiplier=1,position=util.JSONToTable('{json.dumps(origin)}'),frozen=true}}) mmdhl.testEnt=e return e:EntIndex()")
time.sleep(1)
lua('client',f"MMDHL_VISUAL=Entity({entity}) gui.HideGameUI() if IsValid(g_SpawnMenu) then g_SpawnMenu:Close() end hook.Remove('CalcView','MMDHL.TestCamera') hook.Remove('CalcView','MMDHL.StressCamera') hook.Add('HUDShouldDraw','MMDHL.VisualHUD',function() return false end) hook.Add('PreDrawViewModel','MMDHL.VisualViewModel',function() return true end) return true")
def camera(scene):
 return lua('client',"""local e=MMDHL_VISUAL e:SetupBones() local foot=e:GetBoneMatrix(e:LookupBone('ValveBiped.Bip01_L_Foot')):GetTranslation() local head=e:GetBoneMatrix(e:LookupBone('ValveBiped.Bip01_Head1')):GetTranslation()
 local center=LerpVector(.5,foot,head) local offset=Vector(-250,30,55) local fov=38
 if '"""+scene+"""'=='face' then center=head+Vector(-1,0,4) offset=Vector(-42,0,2) fov=30
 elseif '"""+scene+"""'=='hand' then center=e:GetBoneMatrix(e:LookupBone('ValveBiped.Bip01_L_Hand')):GetTranslation() offset=Vector(-36,-20,16) fov=30
 elseif '"""+scene+"""'=='shadow' then offset=Vector(-190,-145,100) fov=50 end
 hook.Add('CalcView','MMDHL.VisualCamera',function() return {origin=center+offset,angles=(-offset):Angle(),fov=fov,drawviewer=false} end) return {center={center:Unpack()},camera={(center+offset):Unpack()}}""")
def capture(name):
 time.sleep(1)
 lua('client',"RunConsoleCommand('mmdhl_debug_capture','"+prefix+'-'+name+"') return {renderError=mmdhl.renderError,depth=mmdhl.depthPasses}")
 time.sleep(.3)
camera('full');capture('full')
lua('client','MMDHL_VISUAL.MMDHLFitOverlay=true MMDHL_VISUAL.MMDHLFitSelected=7 return true');capture('collision');lua('client','MMDHL_VISUAL.MMDHLFitOverlay=false return true')
camera('face');capture('face-neutral')
eye=lua('server',"local e=mmdhl.testEnt e:SetEyeTarget(Vector(1000,350,150)) for _,m in ipairs(mmdhl.GetMorphs(e)) do if m.name=='blink' then mmdhl.SetMorphWeight(e,m.mmd,.65) return m.mmd end end return -1")
capture('face-posed')
lua('server',f"local e=mmdhl.testEnt e:SetEyeTarget(vector_origin) if {eye}>=0 then mmdhl.SetMorphWeight(e,{eye},0) end return true")
camera('hand');capture('hand-neutral')
lua('server',"local e=mmdhl.testEnt for _,n in ipairs({'ValveBiped.Bip01_L_Finger1','ValveBiped.Bip01_L_Finger11','ValveBiped.Bip01_L_Finger12'}) do e:ManipulateBoneAngles(e:LookupBone(n),Angle(45,0,0)) end return true");capture('hand-posed')
camera('shadow')
lua('client',"local e=MMDHL_VISUAL local center=e:WorldSpaceCenter() MMDHL_LAMP=ProjectedTexture() MMDHL_LAMP:SetPos(center+Vector(-100,70,100)) MMDHL_LAMP:SetAngles((center-MMDHL_LAMP:GetPos()):Angle()) MMDHL_LAMP:SetFOV(65) MMDHL_LAMP:SetNearZ(4) MMDHL_LAMP:SetFarZ(600) MMDHL_LAMP:SetBrightness(2) MMDHL_LAMP:SetEnableShadows(true) MMDHL_LAMP:SetTexture('effects/flashlight001') MMDHL_LAMP:Update() return true")
capture('flashlight')
lua('client','if IsValid(MMDHL_LAMP) then MMDHL_LAMP:Remove() end return true')
lua('server',"mmdhl.testEnt:SetMaterial('debug/env_cubemap_model') return true");camera('full');capture('cubemap')
lua('server',"mmdhl.testEnt:SetMaterial('') mmdhl.testEnt:SetColor(Color(80,160,255,190)) mmdhl.testEnt:SetSubMaterial(0,'models/wireframe') return true");capture('color-submaterial')
lua('server',"mmdhl.testEnt:SetColor(Color(255,255,255,0)) return true");capture('alpha-zero')
lua('client',"for _,mat in ipairs(mmdhl.sourceMaterials[mmdhl.GetAsset(MMDHL_VISUAL)] or {}) do assert(mat:GetVector('$color')==Vector(1,1,1) and mat:GetFloat('$alpha')==1,'Material modulation leaked out of native draw') end return true")
lua('server',"mmdhl.testEnt:SetColor(color_white) mmdhl.testEnt:SetSubMaterial(0,'') return true")
print(prefix+' visual capture set complete',flush=True)
