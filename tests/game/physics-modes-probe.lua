-- Owned-session comparison: preserve source rigs, change only secondary accuracy.
assert(MMDHL_DEBUG_TOKEN)
if SERVER then
 local p=player.GetHumans()[1]assert(IsValid(p))p:GodEnable()p:SetPos(Vector(500,0,-143.96875))p:SetEyeAngles(Angle(5,90,0))
 mmdhl.SetPlayerModel(p,'801f3bcf7491bc1e53a8d7d91028817d50f0cd32dd1eae00087af28a3a7c7592',{},function(e,err) assert(IsValid(e),err) end)
 mmdhl.Spawn(p,'8f6507b0fad5769a24718bdb8dbcca19050b1e9730db313db649b8f89142881f',{position={560,50,-140},angles={0,90,0},frozen=true},function(e,err)
  assert(IsValid(e),err) MMDModeRag=e local poses={}
  for i=0,e:GetPhysicsObjectCount()-1 do local b=e:GetPhysicsObjectNum(i)poses[i]={pos=b:GetPos(),angles=b:GetAngles()}end
  local start=CurTime()
  hook.Add('Tick','MMDHL.ModeMotion',function()
   if not IsValid(e) then hook.Remove('Tick','MMDHL.ModeMotion')return end
   local offset=Vector(0,-math.sin((CurTime()-start)*2)*30,0)
   for i,pose in pairs(poses)do local b=e:GetPhysicsObjectNum(i)if IsValid(b)then b:SetPos(pose.pos+offset)b:SetAngles(pose.angles)end end
  end)
 end)
 return true
end
local player=LocalPlayer()assert(mmdhl.IsMMD(player) and mmdhl.GetInstance(player)>0,'Wait for player model')
mmdhl.CloseLibrary()gui.HideGameUI()
hook.Add('CalcView','MMDHL.ModeCamera',function(p,pos,ang,fov)return {origin=p:EyePos()-ang:Forward()*150+Vector(0,0,18),angles=ang,fov=fov,drawviewer=true}end)
local settings=GetConVar('mmdhl_secondary_iterations')local original=settings:GetInt()
local modes=MMDHLProbeModes or {10,0,-1}local phase=1 local samples={}local results={}local start=RealTime()+3
settings:SetInt(modes[phase])
local function diagnostics()
 local out={}for _,e in ipairs(mmdhl.Entities())do if mmdhl.GetInstance(e)>0 then out[#out+1]={entity=e:EntIndex(),class=e:GetClass(),state=mmdhl.GetDiagnostics(e,false)}end end return out
end
hook.Add('PostRender','MMDHL.ModeProbe',function()
 if RealTime()<start then return end
 local profile=mmdhl.frameProfile or {}
 samples[#samples+1]={ms=RealFrameTime()*1000,focus=system.HasFocus(),physicsMs=profile.physicsWorkMs or 0,poseMs=profile.poseWorkMs or 0,wall=profile.prepareWallMs or 0}
 if RealTime()-start<10 then return end
 local prefix='mmd_hotloader/debug/'..MMDHL_DEBUG_TOKEN..'/'
 file.Write(prefix..'accuracy-'..modes[phase]..'.png',render.Capture({format='png',x=0,y=0,w=ScrW(),h=ScrH(),alpha=false}))
 results[#results+1]={accuracy=modes[phase],frames=samples,diagnostics=diagnostics(),settingsError=mmdhl.physicsSettingsError}
 phase=phase+1
 if phase>#modes then
  hook.Remove('PostRender','MMDHL.ModeProbe')settings:SetInt(original)
  file.Write(prefix..'physics-modes.json',util.TableToJSON({resolution={ScrW(),ScrH()},results=results}))
 else settings:SetInt(modes[phase])samples={}start=RealTime()+2 end
end)
return true
