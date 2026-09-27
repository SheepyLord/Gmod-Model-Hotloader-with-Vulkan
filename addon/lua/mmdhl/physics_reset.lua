local native=mmdhl.native
local L=mmdhl.L
function mmdhl.ResetPhysics(ent)
 if not mmdhl.IsMMD(ent) then return false,L'physics_reset.select_model' end
 if CLIENT then
  if mmdhl.GetInstance(ent)<1 then return false,L'physics_reset.loading' end
  ent.MMDPresentationStopped=nil ent.MMDPresentationFrame=nil ent.MMDPhysicsLOD=nil
  return native.ResetPhysics(mmdhl.GetInstance(ent))
 end
 if mmdhl.GetInstance(ent)>0 then local ok,err=native.ResetPhysics(mmdhl.GetInstance(ent)) if not ok then return false,err end end
 ent.MMDStopped=nil
 net.Start('mmdhl_physics_reset') net.WriteEntity(ent) net.Broadcast()
 return true
end
if SERVER then util.AddNetworkString('mmdhl_physics_reset') end
if CLIENT then
 local resetKey=CreateClientConVar('mmdhl_reset_all_key','0',true,false,'Reset all character model physics shortcut; 0 disables. F8 is reserved for recording.',0,159)
 if resetKey:GetInt()==KEY_F8 then resetKey:SetInt(0) end
 local toggleKey=CreateClientConVar('mmdhl_toggle_physics_key','0',true,false,'Toggle character model physics shortcut; 0 disables',0,159)
 local lastAccuracy=CreateClientConVar('mmdhl_last_physics_accuracy','10',true,false,'Last enabled character model physics accuracy',0,100)
 function mmdhl.TogglePhysics()
  local accuracy=GetConVar('mmdhl_secondary_iterations') if not accuracy then return end
  local old=accuracy:GetInt()
  if old>=0 then lastAccuracy:SetInt(old) accuracy:SetInt(-1) else accuracy:SetInt(lastAccuracy:GetInt()) end
  notification.AddLegacy(old>=0 and L'physics_reset.disabled' or lastAccuracy:GetInt()==0 and L'physics_reset.restored_jiggle' or L'physics_reset.restored_full',NOTIFY_GENERIC,4)
 end
 concommand.Add('mmdhl_toggle_physics',mmdhl.TogglePhysics,nil,'Toggle secondary physics off and restore the previous full/jiggle setting.')
 function mmdhl.ResetAllPhysics()
  local count=0
  for _,ent in ipairs(mmdhl.Entities()) do if mmdhl.ResetPhysics(ent) then count=count+1 end end
  notification.AddLegacy(L('physics_reset.reset_all',{count=count}),NOTIFY_GENERIC,4)
  return count
 end
 concommand.Add('mmdhl_reset_all_physics',mmdhl.ResetAllPhysics,nil,'Reset every character model secondary world on this client.')
 local wasDown,toggleWasDown=false,false
 hook.Add('Think','MMDHL.ResetShortcut',function()
  local key=resetKey:GetInt() local down=key>0 and input.IsKeyDown(key)
  local usable=system.HasFocus() and not gui.IsGameUIVisible() and not vgui.CursorVisible() and not IsValid(vgui.GetKeyboardFocus())
  if down and not wasDown and key~=KEY_F8 and usable then mmdhl.ResetAllPhysics() end
  local toggle=toggleKey:GetInt() local toggleDown=toggle>0 and input.IsKeyDown(toggle)
  if toggleDown and not toggleWasDown and usable and (toggle~=key or key==KEY_F8) then mmdhl.TogglePhysics() end
  toggleWasDown=toggleDown
  wasDown=down
 end)
 net.Receive('mmdhl_physics_reset',function()
  local ent=net.ReadEntity() if not IsValid(ent) then return end
  if mmdhl.GetInstance(ent)>0 then native.ResetPhysics(mmdhl.GetInstance(ent)) end
  ent.MMDPresentationStopped=nil ent.MMDPresentationFrame=nil ent.MMDPhysicsLOD=nil ent.MMDPresentationCheckAt=RealTime()+1
  mmdhl.renderError=nil
  notification.AddLegacy(L'physics_reset.reset_one',NOTIFY_GENERIC,4)
 end)
 concommand.Add('mmdhl_reset_physics',function(_,_,args)
  if args[1]=='all' then mmdhl.ResetAllPhysics() return end
  local ent=LocalPlayer():GetEyeTrace().Entity
  local ok,err=mmdhl.ResetPhysics(ent) if not ok then notification.AddLegacy(err,NOTIFY_HINT,4) end
 end,nil,'Reset secondary physics on the character model ragdoll you aim at. Add all to reset every character model ragdoll.')
end
properties.Add('mmdhl_reset_physics',{
 MenuLabel='#mmdhl.physics_reset.menu',Order=605,MenuIcon='icon16/arrow_refresh.png',
 Filter=function(_,ent,ply) return mmdhl.IsMMD(ent) and gamemode.Call('CanProperty',ply,'mmdhl_reset_physics',ent) end,
 Action=function(_,ent) mmdhl.ResetPhysics(ent) end
})
