-- Integrate with Source entities and the existing Sandbox factories. Native
-- handles, render proxies and worker state never belong in a dupe/save.
local native=mmdhl.native
local L=mmdhl.L
local function carrierPath(path)
 return isstring(path) and path:lower():match('^models/mmd/[a-f0-9]+/[^/]+%.mdl$')~=nil
end
mmdhl.IsCarrierModel=carrierPath
local function available(rig)
 if not rig or not mmdhl.IsCurrentRig(rig) then return nil,L'persistence.error.carrier_outdated' end
 if SERVER and not mmdhl.CanUseAsset(nil,rig.asset) then return nil,L'persistence.error.asset_not_approved' end
 if rig.materialGma and not mmdhl.MountPackage(rig.materialGma) then return nil,L'persistence.error.mount_materials' end
 if not mmdhl.MountPackage('data/mmd_hotloader/rigs/'..rig.key..'/carrier.gma') then return nil,L'persistence.error.mount_carrier' end
 return rig
end
function mmdhl.EnsureModel(model)
 if not carrierPath(model) then return end
 return available(mmdhl.GetRigForModel(model:lower()))
end
if CLIENT then
 -- Model previews created by tools still get real metadata/material interfaces.
 -- Networked actors wait for the authoritative server identity in instances.lua.
 return
end

local function stateFrom(data)
 return istable(data) and istable(data.EntityMods) and data.EntityMods.MMDHLNative or nil
end
local function optionsFor(rig,state)
 local o=table.Copy(state and state.options or {})
 o.backend='source' o.role=rig.role or 'ragdoll' o.scale=rig.scale*.0254 o.scaleMultiplier=nil o.height=nil
 o.mass=rig.mass o.rigManifest=rig
 return o
end
local variants={}
local function rigForClass(rig,class,state)
 local role=class=='npc_citizen' and 'citizen' or class=='npc_combine_s' and 'combine'
 if not role or rig.role==role then return rig,optionsFor(rig,state) end
 -- A ragdoll carrier keeps the PMX bind facing -X (its packs are there for
 -- animation tools); an NPC needs its actor variant's Source reference facing
 -- and profile. Convert the model before the caller's NPC:Spawn(), retaining its real class.
 local gender=(state and state.options and state.options.gender) or
  (rig.animation and rig.animation.profile:find('_male$') and 'male') or 'female'
 local key=rig.key..':'..role..':'..gender
 local converted=variants[key]
 local o=optionsFor(rig,state) o.role=role o.gender=gender o.rigManifest=nil
 if not converted then
  local err o,err=mmdhl.ActorOptions(o) if not o then return nil,err end
  local raw,e=native.PrepareCarrier(rig.asset,util.TableToJSON(o))
  if not raw then return nil,e end
  converted=util.JSONToTable(raw)
  local mounted,error=available(converted) if not mounted then return nil,error end
  variants[key]=converted
  mmdhl.RegisterActor(converted) mmdhl.PublishRig(converted)
 end
 o.rigManifest=converted
 return converted,o
end
function mmdhl.CaptureNativeState(ent)
 local rig=mmdhl.GetRig(ent) if not rig then return end
 local weights={} for i in ipairs(rig.morphs) do weights[i]=mmdhl.GetMorphWeight(ent,i-1) end
 local options=optionsFor(rig,{options=ent.MMDOptions})
 options.rigManifest=nil options.animationReference=nil options.position=nil options.angles=nil
 local eye=ent.MMDEyeTarget
 local worldTarget=not ent:IsRagdoll() and ent:GetClass()~='mmdhl_ragdoll'
 if worldTarget and eye and eye~=vector_origin then eye=ent:WorldToLocal(eye) end
 return {version=4,asset=rig.asset,rigKey=rig.key,model=rig.model,options=options,
  morphs=weights,scale=ent:GetFlexScale(),manual=table.Copy(ent.MMDHLManual or {}),
  eyeTarget=eye,eyeWorld=worldTarget,eyeDriver=ent.MMDHLEyeDriver,
  materials=mmdhl.CaptureMaterialState(ent)}
end
function mmdhl.StoreNativeState(ent)
 local state=mmdhl.CaptureNativeState(ent) if not state then return end
 -- StoreEntityModifier merges tables; clearing first is essential when a
 -- material override, hidden part, manual pose or eye target has been reset.
 duplicator.ClearEntityModifier(ent,'MMDHLNative')
 duplicator.StoreEntityModifier(ent,'MMDHLNative',state)
 return state
end
local function applyAppearance(ent,state)
 if not state then return end
 ent:SetFlexScale(state.scale or 1)
 if state.morphs then mmdhl.SetMorphWeights(ent,table.Copy(state.morphs)) end
 if state.materials then mmdhl.ApplyMaterialState(ent,state.materials) end
 if next(state.manual or {}) then
  local generation=ent:GetNW2Int('MMDHLGeneration',0)
  mmdhl.LoadAsset(mmdhl.GetAsset(ent),function(info)
   if not info or not IsValid(ent) or ent:GetNW2Int('MMDHLGeneration',0)~=generation then return end
   for i,pose in pairs(state.manual) do mmdhl.SetManualBonePose(ent,tonumber(i)-1,pose) end
   mmdhl.StoreNativeState(ent)
  end)
 end
 if state.eyeTarget then
  local eye=state.eyeTarget
  local world=not ent:IsRagdoll() and ent:GetClass()~='mmdhl_ragdoll'
  if eye~=vector_origin then
   if state.eyeWorld then eye=ent:LocalToWorld(eye) end
   if state.eyeWorld~=nil and state.eyeWorld~=world then
    local at=ent:GetAttachment(ent:LookupAttachment('eyes'))
    if at then
     if world then eye=LocalToWorld(eye,angle_zero,at.Pos,at.Ang)
     else eye=WorldToLocal(eye,angle_zero,at.Pos,at.Ang) end
    end
   end
  end
  if state.eyeDriver~='bones' then ent:SetEyeTarget(eye) else ent.MMDEyeTarget=eye ent.MMDHLEyeDriver='bones' end
 end
end
function mmdhl.BindEntity(ent,state)
 if not IsValid(ent) then return false end
 local rig,err=mmdhl.EnsureModel(ent:GetModel()) if not rig then return false,err end
 if rig.role=='arms' or ent:GetClass()=='gmod_hands' or ent:GetClass()=='viewmodel' then
  ent.MMDHLHands=true
  for _,key in ipairs({'MMDHLAsset','MMDHLRig','MMDHLRole'}) do ent:SetNW2String(key,'') end
  return true -- Native c_arms rendering; no world renderer or secondary world.
 end
 if state and state.asset~=rig.asset then return false,L'persistence.error.appearance_mismatch' end
 if ent:GetNW2String('MMDHLRig','')~=rig.key then
  local result,error=mmdhl.AttachNative(ent,rig.asset,optionsFor(rig,state))
  if not result then return false,error end
 end
 ent:SetNW2Int('MMDHLNativeBodyCount',ent:GetPhysicsObjectCount())
 local role=ent:IsRagdoll() and 'ragdoll' or rig.role
 ent:SetNW2String('MMDHLRole',role or 'ragdoll')
 applyAppearance(ent,state)
 -- Restored hostile NPCs are ordinary citizens to the engine; re-apply their side.
 local hostile=(state and state.options and state.options.hostile) or (ent.MMDOptions and ent.MMDOptions.hostile)
 if hostile and ent:GetClass()=='npc_citizen' and not ent.MMDHLHostile and mmdhl.MakeHostileCitizen then mmdhl.MakeHostileCitizen(ent) end
 mmdhl.StoreNativeState(ent)
 return true
end

local function report(ent,err)
 if err and ent.MMDHLBindingError~=err then ent.MMDHLBindingError=err ErrorNoHalt('[Model Hotloader restore] '..mmdhl.Localize(tostring(err))..'\n') end
end
local function afterSpawn(ent,state)
 timer.Simple(0,function()
  if not IsValid(ent) or not carrierPath(ent:GetModel()) then return end
  local ok,err=mmdhl.BindEntity(ent,state) if not ok then report(ent,err) end
 end)
end
-- Keep the server's no-draw intent even when a client addon changes its local
-- flag. Source stops transmitting no-draw entities; dormancy is also checked
-- clientside, so a stale proxy cannot continue drawing while it is absent.
-- Only the server (or a client-only entity's own client) records the intent: a
-- client addon's local hide, such as distance culling, must not look like it.
if not mmdhl.PersistentSetNoDraw then
 local meta=FindMetaTable('Entity') local original=meta.SetNoDraw
 mmdhl.PersistentSetNoDraw=original
 meta.SetNoDraw=function(ent,hidden,...)
  if (SERVER or ent:EntIndex()<0) and (carrierPath(ent:GetModel()) or mmdhl.IsMMD(ent)) then ent:SetNW2Bool('MMDHLNoDraw',hidden==true) end
  return original(ent,hidden,...)
 end
end
-- Bind model-only copies/replacements as well as explicit importer spawns.
-- Mount before SetModel: a late entity modifier cannot fix missing physics.
if not mmdhl.PersistentSetModel then
 local meta=FindMetaTable('Entity') local original=meta.SetModel
 mmdhl.PersistentSetModel=original
 meta.SetModel=function(ent,path,...)
  if not carrierPath(path) then return original(ent,path,...) end
  local rig,err=mmdhl.EnsureModel(path)
  if not rig then report(ent,err) return original(ent,path,...) end
  local converted,options=rigForClass(rig,ent:GetClass())
  if not converted then report(ent,options) return original(ent,path,...) end
  local result=original(ent,converted.model,...)
  afterSpawn(ent)
  return result
 end
end
hook.Add('OnEntityCreated','MMDHL.BindCachedModel',function(ent)
 afterSpawn(ent)
end)

if not mmdhl.PersistenceDuplicator then
 mmdhl.PersistenceDuplicator=true
 local copy,create,generic=duplicator.CopyEntTable,duplicator.CreateEntityFromTable,duplicator.DoGeneric
 duplicator.CopyEntTable=function(ent,...)
  local data=copy(ent,...)
  if data and mmdhl.IsMMD(ent) then
   local state=mmdhl.StoreNativeState(ent)
   data.EntityMods=data.EntityMods or {} data.EntityMods.MMDHLNative=table.Copy(state)
   for k in pairs(data) do
    if isstring(k) and (k:sub(1,5)=='MMDHL' or k=='MMDOptions' or k=='MMDPose' or k=='MMDManipulation' or k=='MMDMorphs') then data[k]=nil end
   end
  end
  return data
 end
 duplicator.CreateEntityFromTable=function(p,data,...)
  if carrierPath(data.Model) then
   local rig,err=mmdhl.EnsureModel(data.Model)
   if not rig then error(mmdhl.Localize(err)) end
   if not mmdhl.CanUseAsset(p,rig.asset) then error(mmdhl.Localize(L'persistence.error.model_not_approved')) end
  end
  return create(p,data,...)
 end
 duplicator.DoGeneric=function(ent,data,...)
  if not data or not carrierPath(data.Model) then return generic(ent,data,...) end
  local rig,err=mmdhl.EnsureModel(data.Model) if not rig then error(mmdhl.Localize(err)) end
  local state=stateFrom(data)
  local converted,options=rigForClass(rig,ent:GetClass(),state) if not converted then error(mmdhl.Localize(options)) end
  local values=table.Copy(data) values.Model=converted.model
  if ent:GetModel()~=converted.model then ent:SetModel(converted.model) end
  if ent:GetNW2String('MMDHLRig','')~=converted.key then
   local bound,bindError=mmdhl.AttachNative(ent,converted.asset,options)
   if not bound then error(mmdhl.Localize(bindError)) end
  end
  -- Attach before the normal loader writes bodygroups/submaterials, including
  -- overflow slots. Bone indices and local finger frames remain stable.
  local result=generic(ent,values,...)
  applyAppearance(ent,state)
  afterSpawn(ent,state)
  return result
 end
end
duplicator.RegisterEntityModifier('MMDHLNative',function(p,ent,state)
 if not IsValid(ent) or not istable(state) or not mmdhl.CanUseAsset(p,state.asset) then return end
 local ok,err=mmdhl.BindEntity(ent,state) if not ok then report(ent,err) end
end)
