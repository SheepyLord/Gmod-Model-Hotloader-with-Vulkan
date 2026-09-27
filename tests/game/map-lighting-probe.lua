-- Exact gm_construct map-light regression. Run in an owned addons-enabled session.
assert(MMDHL_DEBUG_TOKEN and game.GetMap()=='gm_construct','Owned gm_construct session required')
local asset='e2c25f9ea0b736c55fe2fd26660f94f85fb5c0f1952fa4562ea3b3df39658eb7'
local position=Vector(1295,-670,-140.96875)
if SERVER then
 local p=player.GetHumans()[1] assert(IsValid(p)) p:GodEnable()
 p:SetPos(Vector(1327.741089,-722.169128,-143.968750)) p:SetEyeAngles(Angle(10.226360,120.087006,0))
 if IsValid(MMDMapLight) then MMDMapLight:Remove() end
 if IsValid(MMDMapCompiled) then MMDMapCompiled:Remove() end
 local e=ents.Create('prop_ragdoll') e:SetModel('models/sheepylord/honkai_star_rail/march_7th.mdl')
 e:SetPos(position) e:SetAngles(Angle(0,300.087006,0)) e:Spawn()
 for i=0,e:GetPhysicsObjectCount()-1 do e:GetPhysicsObjectNum(i):EnableMotion(false) end
 e:SetNW2Bool('MMDHLMapLightComparison',true) MMDMapCompiled=e
 mmdhl.Spawn(p,asset,{frozen=true,position={position:Unpack()},angles={0,120.087006,0}},function(h,err)
  MMDMapLight=h MMDMapLightError=err
  if IsValid(h) then h:SetNW2Int('MMDHLProbeBodies',h:GetPhysicsObjectCount()) end
 end)
 return true
end
mmdhl.CloseLibrary() gui.HideGameUI() RunConsoleCommand('r_drawviewmodel','0')
hook.Add('CalcView','MMDHL.MapLightingView',function()return {origin=Vector(1327.741089,-722.169128,-79.968750),angles=Angle(10.226360,120.087006,0),fov=75}end)
local hot,compiled
for _,e in ipairs(mmdhl.Entities()) do if mmdhl.GetAsset(e)==asset and e:GetClass()=='prop_ragdoll' and e:GetNW2Int('MMDHLProbeBodies',0)==18 then hot=e break end end
for _,e in ipairs(ents.FindByClass('prop_ragdoll')) do if e:GetNW2Bool('MMDHLMapLightComparison') then compiled=e end end
assert(IsValid(hot) and mmdhl.GetInstance(hot)>0 and IsValid(compiled),'Wait for both models to finish loading')
local oldDraw=mmdhl.DrawCarrier local oldCompiled=compiled.RenderOverride
local phase='compiled' local started=RealTime()+2 local frames={} local report={resolution={ScrW(),ScrH()},asset=asset,physicsObjects=hot:GetNW2Int('MMDHLProbeBodies',0)}
mmdhl.DrawCarrier=function(...) if phase~='compiled' then return oldDraw(...) end end
compiled.RenderOverride=function(e) if phase=='compiled' then e:DrawModel() end end
hook.Add('PostRender','MMDHL.MapLightProbe',function()
 if RealTime()<started then return end
 local stats=util.JSONToTable(mmdhl.native.RenderFrameStats())
 frames[#frames+1]={ms=RealFrameTime()*1000,focus=system.HasFocus(),drawMs=stats.nativeRenderMs}
 local duration=phase=='compiled' and 1 or 10
 if RealTime()-started<duration then return end
 local path='mmd_hotloader/debug/'..MMDHL_DEBUG_TOKEN..'/'
 file.Write(path..'map-'..phase..'.png',render.Capture({format='png',x=0,y=0,w=ScrW(),h=ScrH(),alpha=false}))
 if phase=='compiled' then phase='imported' frames={} started=RealTime()+2 return end
 report.frames=frames report.diagnostics=mmdhl.GetDiagnostics(hot,false)
 local ok,err=mmdhl.native.SetupSourceLighting(hot:WorldSpaceCenter()) assert(ok,err)
 report.lights=util.JSONToTable(mmdhl.native.GetLightingState())
 local count=0 for _,light in ipairs(report.lights.lights) do if light.type>0 then count=count+1 end end
 report.lightCount=count report.pass=count>=3 and report.physicsObjects==18 and not mmdhl.renderError
 file.Write(path..'map-lighting.json',util.TableToJSON(report))
 mmdhl.DrawCarrier=oldDraw compiled.RenderOverride=oldCompiled
 hook.Remove('PostRender','MMDHL.MapLightProbe')
end)
return true
