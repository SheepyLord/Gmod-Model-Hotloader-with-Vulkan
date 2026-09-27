-- These policies belong only to locally simulated corpses. Server ragdolls,
-- NPCs, players and editor previews retain their existing ownership/physics.
local L=mmdhl.L
mmdhl.clientRagdolls=mmdhl.clientRagdolls or setmetatable({},{__mode='k'})
function mmdhl.IsClientRagdoll(ent)
 if not IsValid(ent) or not mmdhl.IsMMD(ent) or ent.MMDHLEditorPreview then return false end
 return ent.MMDHLCorpse==true or (ent:EntIndex()<0 and ent:IsRagdoll())
end
function mmdhl.RegisterClientRagdoll(ent)
 if not mmdhl.IsClientRagdoll(ent) then return false end
 if not ent.MMDHLClientRagdollId then
  mmdhl.nextClientRagdollId=(mmdhl.nextClientRagdollId or 0)+1
  ent.MMDHLClientRagdollId=mmdhl.nextClientRagdollId
 end
 mmdhl.clientRagdolls[ent]=true
 mmdhl.InvalidateEntityList()
 ent:CallOnRemove('MMDHL.LocalRagdoll',function(e)
  mmdhl.clientRagdolls[e]=nil mmdhl.InvalidateEntityList()
 end)
 return true
end
-- Other tools may create a client ragdoll directly from a cached carrier model,
-- without a dying actor and therefore without CreateClientsideRagdoll metadata.
hook.Add('OnEntityCreated','MMDHL.LocalRagdollModel',function(ent)
 if not IsValid(ent) or ent:GetClass()~='class C_ClientRagdoll' then return end
 timer.Simple(0,function()
  if not IsValid(ent) then return end
  if not mmdhl.IsMMD(ent) and mmdhl.IsCarrierModel and mmdhl.IsCarrierModel(ent:GetModel()) then
   local rig=mmdhl.GetRigForModel(ent:GetModel())
   if rig then ent.MMDHLLocalAsset=rig.asset ent.MMDHLLocalRigKey=rig.key ent.MMDHLLocalRig=rig ent.MMDHLCorpse=true end
  end
  mmdhl.RegisterClientRagdoll(ent)
 end)
end)
function mmdhl.ClientRagdollAsleep(ent)
 if not mmdhl.IsClientRagdoll(ent) then return false end
 local found=false
 for i=0,ent:GetPhysicsObjectCount()-1 do
  local body=ent:GetPhysicsObjectNum(i)
  if not IsValid(body) then return false end
  found=true
  if not body:IsAsleep() then return false end
 end
 -- Missing/not-yet-created physics is not a sleeping corpse.
 return found
end
function mmdhl.RemoveClientRagdoll(ent)
 if not mmdhl.IsClientRagdoll(ent) then return false end
 mmdhl.ReleasePresentation(ent)
 ent:Remove()
 mmdhl.InvalidateEntityList()
 return true
end
function mmdhl.CleanupClientRagdolls()
 local count=0
 for _,ent in ipairs(mmdhl.Entities()) do
  if mmdhl.RemoveClientRagdoll(ent) then count=count+1 end
 end
 mmdhl.InvalidateEntityList()
 return count
end
concommand.Add('mmdhl_cleanup_client_ragdolls',function()
 local count=mmdhl.CleanupClientRagdolls()
 notification.AddLegacy(L('ragdolls.removed',{count=count}),NOTIFY_GENERIC,4)
end,nil,'Remove clientside character model ragdolls and release their rendering and secondary physics.')
