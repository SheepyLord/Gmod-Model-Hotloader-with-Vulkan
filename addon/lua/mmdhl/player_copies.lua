-- First-person body addons (First-Person Body, legs addons) draw the player's
-- body from a clientside copy of the player's model: a prop they pose in their
-- own bone callback and draw with DrawModel under their own camera and clip
-- planes. A carrier has no visible mesh, so such a copy of a character model
-- showed nothing. A copy of an MMD player's model drawn this way becomes that
-- player's character: it gets its own presentation, posed by the copy's bones,
-- draws wherever its owner calls DrawModel and mirrors the player's parts,
-- materials, colour and expressions. In the player's own first-person view the
-- head, hair and arms are left out (the native first-person mask), as the
-- addons hide the head and arms of standard models.
local meta=FindMetaTable('Entity')
mmdhl.EngineDrawModel=mmdhl.EngineDrawModel or meta.DrawModel
local engineDraw=mmdhl.EngineDrawModel
mmdhl.playerCopies=mmdhl.playerCopies or setmetatable({},{__mode='k'})
local copies=mmdhl.playerCopies
-- Client-only entities seen drawing: the model last checked and when to look again.
local checked=setmetatable({},{__mode='k'})
-- Model previews in panels (DModelPanel: the player model selectors) keep the carrier.
local panelDepth=0
local function ownerOf(model)
 local ply=LocalPlayer()
 if IsValid(ply) and ply:GetModel()==model and mmdhl.IsMMD(ply) then return ply end
 for _,p in ipairs(player.GetAll()) do if p:GetModel()==model and mmdhl.IsMMD(p) then return p end end
end
function mmdhl.ForgetPlayerCopy(ent)
 if not IsValid(ent) or not ent.MMDHLCopyOf then return end
 if mmdhl.ReleasePresentation then mmdhl.ReleasePresentation(ent) end
 ent.MMDHLLocalAsset=nil ent.MMDHLLocalRigKey=nil ent.MMDHLLocalRig=nil ent.MMDHLCopyOf=nil ent.MMDHLDrawnAt=nil
 copies[ent]=nil mmdhl.InvalidateEntityList()
end
local function adopt(ent,owner)
 local rig=mmdhl.GetRig(owner)
 if not rig or rig.model~=ent:GetModel() then return end
 ent.MMDHLLocalAsset=rig.asset ent.MMDHLLocalRigKey=rig.key ent.MMDHLLocalRig=rig ent.MMDHLCopyOf=owner
 copies[ent]=true mmdhl.InvalidateEntityList()
 ent:CallOnRemove('MMDHL.PlayerCopy',function(e) copies[e]=nil mmdhl.InvalidateEntityList() end)
end
-- The copy is drawn where its owner draws it; the presentation catches up from
-- the next frame. Until then nothing is drawn (the carrier has no mesh either).
local function drawCopy(ent,flags)
 ent.MMDHLDrawnAt=RealTime()
 if mmdhl.GetInstance(ent)<1 then return end
 mmdhl.DrawCarrier(ent,false,flags) mmdhl.DrawCarrier(ent,true,flags)
end
-- Where the copy's chest was last posed: first_person.lua clips below a camera inside it.
function mmdhl.CopyChest(ent)
 local bone=ent.MMDHLChestBone
 if bone==nil then bone=ent:LookupBone('ValveBiped.Bip01_Spine4') or ent:LookupBone('ValveBiped.Bip01_Spine2') or false ent.MMDHLChestBone=bone end
 local matrix=bone and ent.MMDRenderPose and ent.MMDRenderPose[bone+1]
 return matrix and matrix:GetTranslation()
end
meta.DrawModel=function(ent,flags,...)
 local state=checked[ent]
 if state==nil then
  -- Only client-only entities (index -1) can be another addon's copy.
  state=IsValid(ent) and ent:EntIndex()<0 and not ent.MMDHLEditorPreview and {retry=0} or false
  checked[ent]=state
 end
 if state and panelDepth==0 then
  local model=ent:GetModel() local owner=ent.MMDHLCopyOf
  if owner and (not IsValid(owner) or owner:GetModel()~=model or not mmdhl.IsMMD(owner)) then mmdhl.ForgetPlayerCopy(ent) owner=nil end
  -- Each model is looked at once; a carrier no player wears yet, again each second.
  if not owner and (model~=state.model or RealTime()>=state.retry) then
   state.model=model state.retry=math.huge
   -- Corpses and other client entities with an identity of their own stay as they are.
   if mmdhl.IsCarrierModel and mmdhl.IsCarrierModel(model) and not mmdhl.IsMMD(ent) then
    state.retry=RealTime()+1 owner=ownerOf(model)
    if owner then adopt(ent,owner) owner=ent.MMDHLCopyOf end
   end
  end
  if owner then drawCopy(ent,flags) return end
 end
 return engineDraw(ent,flags,...)
end
-- A copy no longer drawn never reaches the check above: one whose player left or
-- took another model lets go of the character (a library deletion waits for that).
timer.Create('MMDHL.PlayerCopies',.5,0,function()
 for ent in pairs(copies) do
  local owner=IsValid(ent) and ent.MMDHLCopyOf
  if not IsValid(ent) then copies[ent]=nil
  elseif not IsValid(owner) or owner:GetModel()~=ent:GetModel() or not mmdhl.IsMMD(owner) then mmdhl.ForgetPlayerCopy(ent) end
 end
end)
local function guardPanels()
 local panel=vgui.GetControlTable and vgui.GetControlTable('DModelPanel')
 if not panel or panel.MMDHLCopyGuard then return end
 local draw=panel.DrawModel
 panel.MMDHLCopyGuard=true
 panel.DrawModel=function(self,...)
  panelDepth=panelDepth+1
  local ok,err=pcall(draw,self,...)
  panelDepth=panelDepth-1
  if not ok then error(err,0) end
 end
end
guardPanels()
timer.Simple(0,guardPanels)
hook.Add('InitPostEntity','MMDHL.PlayerCopyPanels',guardPanels)
