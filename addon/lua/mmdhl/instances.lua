-- A native handle belongs to this client; it is never an entity network ID.
local native=mmdhl.native
local L=mmdhl.L
local mounted,attached={},setmetatable({},{__mode='k'})
function mmdhl.ShadowKey(ent,instance) return ent:EntIndex()>=0 and ent:EntIndex() or 1048576+instance end
local function removeFallback(ent) if IsValid(ent.MMDHLFallback) then ent.MMDHLFallback:Remove() end ent.MMDHLFallback=nil end
local function fallback(ent)
 if mmdhl.PresentationSuppressed(ent) then removeFallback(ent) return end
 -- A copy is drawn by its own addon, often at the camera: no placeholder there.
 if ent==LocalPlayer() or ent.MMDHLCopyOf then return end
 if not IsValid(ent.MMDHLFallback) then
  local model=ClientsideModel('models/player/kleiner.mdl',RENDERGROUP_TRANSLUCENT)
  if not IsValid(model) then return end
  model:SetMaterial('models/wireframe') model:SetColor(Color(80,170,255,160)) model:SetRenderMode(RENDERMODE_TRANSCOLOR)
  ent.MMDHLFallback=model ent:CallOnRemove('MMDHL.Fallback',removeFallback)
 end
 ent.MMDHLFallback:SetPos(ent:GetPos()) ent.MMDHLFallback:SetAngles(ent:GetAngles())
end
local function release(ent)
 local handle=ent.MMDHLClientInstance
 if handle then native.RemoveSourceShadow(ent.MMDHLShadowIndex or math.max(0,ent:EntIndex())) native.DestroyInstance(handle) end
 ent.MMDHLClientInstance=nil ent.MMDHLAttachmentKey=nil ent.MMDShadowAlpha=nil ent.MMDVisibilitySent=nil ent.MMDHLManualApplied=nil
 ent.MMDRenderPose=nil ent.MMDPresentationFrame=nil ent.MMDPresentationStopped=nil ent.MMDPhysicsLOD=nil ent.MMDHLClientCollisionFlags=nil
 ent.MMDNamesSent=nil ent.MMDOverrideFrame=nil ent.MMDOverrideNext=nil ent.MMDOverrideJson=nil ent.MMDShadowSequence=nil ent.MMDHLClientMorphs=nil ent.MMDHLMorphBuffer=nil
 ent.MMDVisibilityNext=nil ent.MMDHLBoundsSequence=nil ent.MMDHLBatchEntry=nil
 ent.MMDHLLightFrame=nil ent.MMDHLLightOrigin=nil ent.MMDHLLightBone=nil
 if IsValid(ent.MMDHLVisual) then
  local proxy=ent.MMDHLVisual ent.MMDHLVisual=nil
  proxy:SetNoDraw(true)
  timer.Simple(0,function() if IsValid(proxy) then proxy:Remove() end end)
 end
 removeFallback(ent)
 attached[ent]=nil
end
mmdhl.ReleasePresentation=release
local function mount(path)
 if mounted[path] then return true end
 if not mmdhl.MountPackage(path) then return false end mounted[path]=true return true
end
function mmdhl.AttachPresentation(ent)
 if mmdhl.PresentationReleased(ent) then
  if ent.MMDHLClientInstance or IsValid(ent.MMDHLFallback) then release(ent) end
  return true
 end
 -- A world is created only for a model that is currently shown; hidden
 -- controllers never get one. An existing world survives brief local hides.
 if not ent.MMDHLClientInstance and mmdhl.PresentationSuppressed(ent) then removeFallback(ent) return true end
 local available,renderError=mmdhl.RenderAvailable()
 if not available then return false,renderError end
 local id=mmdhl.GetAsset(ent) local rig=mmdhl.GetRig(ent)
 if not rig then if mmdhl.RequestSharedRig then mmdhl.RequestSharedRig(ent:GetNW2String('MMDHLRig','')) end return false,L'instances.waiting_metadata',true end
 if rig.role=='arms' or ent:GetClass()=='gmod_hands' or ent:GetClass()=='viewmodel' then
  ent.MMDHLHands=true if ent.MMDHLClientInstance then release(ent) end
  return true
 end
 local key=rig.key..':'..ent:GetNW2Int('MMDHLGeneration',0)
 if ent.MMDHLTransferredGeneration==key then
  if ent:IsPlayer() and ent:Alive() then ent.MMDHLTransferredGeneration=nil else return true end
 end
 if ent.MMDHLClientInstance and ent.MMDHLAttachmentKey==key then attached[ent]=true return true end
 if ent.MMDHLClientInstance then release(ent) end
 if not mount(rig.materialGma) or not mount('data/mmd_hotloader/rigs/'..rig.key..'/carrier.gma') then if mmdhl.RequestSharedRig then mmdhl.RequestSharedRig(rig.key) end return false,L'instances.waiting_files',true end
 native.RequestAsset(id)
 local info,err=mmdhl.Decode(native.AssetInfo(id)) if not info then if err and mmdhl.RequestSharedRig then mmdhl.RequestSharedRig(rig.key) end return false,err or L'instances.loading',true end
 local settings=mmdhl.GetGlobalSettings()
 settings.backend='source' settings.presentationDriven=true settings.rigManifest=rig
 settings.sourceEntity=math.max(0,ent:EntIndex())
 local handle,createError=native.CreateInstance(id,util.TableToJSON(settings))
 if not handle then return false,createError end
 ent.MMDHLClientInstance=handle ent.MMDHLAttachmentKey=key ent.MMDHLClientMorphs={} ent.MMDHLShadowIndex=mmdhl.ShadowKey(ent,handle)
 ent.MMDHLClientCollisionFlags=settings.collisionFlags
 mmdhl.assets[id]=info attached[ent]=true
 if ent:GetModel()~=rig.model then
  -- Only repair the missing-asset placeholder. Another addon/gamemode may
  -- deliberately select a different player model; never fight that choice.
  if ent:GetModel()=='models/error.mdl' then ent:SetModel(rig.model)
  else release(ent) return false,L'instances.waiting_selection',true end
 end
 -- A network entity can arrive before its approved files. Source has already
 -- cached an error-model binding under that model index; rebind it only after
 -- the authoritative carrier is mounted, without changing server state.
 if not ent:LookupBone('ValveBiped.Bip01_Pelvis') then
  ent:SetModel('models/error.mdl') ent:SetModel(rig.model)
 end
 ent:InvalidateBoneCache()
 removeFallback(ent)
 ent:CallOnRemove('MMDHL.ClientInstance',release)
 return true
end
function mmdhl.TransferPresentation(source,corpse)
 -- A retained corpse may first enter a client's PVS after its player has
 -- respawned. Never take the new life's world for that old corpse.
 if source:IsPlayer() and source:Alive() then return false end
 local generation=corpse.MMDHLFormerGeneration or corpse:GetNW2Int('MMDHLFormerGeneration',0)
 if generation>0 and generation~=source:GetNW2Int('MMDHLGeneration',0) then return false end
 local handle=source.MMDHLClientInstance if not handle then return false end
 local sourceRig,corpseRig=mmdhl.GetRig(source),mmdhl.GetRig(corpse)
 if not sourceRig or not corpseRig or not sourceRig.key or not sourceRig.asset or sourceRig.key~=corpseRig.key or sourceRig.asset~=corpseRig.asset then return false end
 if corpse.MMDHLClientInstance then release(corpse) end
 local ok=native.RebindSourceEntity(handle,math.max(0,corpse:EntIndex())) if not ok then return false end
 native.RemoveSourceShadow(source.MMDHLShadowIndex or math.max(0,source:EntIndex()))
 local collisionFlags=source.MMDHLClientCollisionFlags
 local proxy=source.MMDHLVisual source.MMDHLVisual=nil
 source.MMDHLTransferredGeneration=source.MMDHLAttachmentKey source.MMDHLClientInstance=nil
 release(source) -- Clear per-instance render caches without destroying the transferred world.
 local rig=mmdhl.GetRig(corpse)
 corpse.MMDHLClientInstance=handle corpse.MMDHLAttachmentKey=rig.key..':'..corpse:GetNW2Int('MMDHLGeneration',0)
 corpse.MMDHLClientMorphs={} corpse.MMDHLShadowIndex=mmdhl.ShadowKey(corpse,handle) attached[corpse]=true
 corpse.MMDHLClientCollisionFlags=collisionFlags
 if IsValid(proxy) then
  corpse.MMDHLVisual=proxy proxy.MMDOwner=corpse corpse.MMDHLBoundsSequence=nil
  corpse:CallOnRemove('MMDHL.NativeVisualRemove',function(owner) if IsValid(proxy) and proxy.MMDOwner==owner then proxy:Remove() end end)
 end
 corpse:CallOnRemove('MMDHL.ClientInstance',release)
 return true
end
function mmdhl.SyncActorMorphs(ent)
 -- Two buffers alternate: nothing is allocated unless a weight changed.
 local previous=ent.MMDHLClientMorphs or {} local weights=ent.MMDHLMorphBuffer or {} local changed=false
 -- Another addon's copy of a player model shows that player's expressions and poses.
 local source=IsValid(ent.MMDHLCopyOf) and ent.MMDHLCopyOf or ent
 local scale=source:GetFlexScale() local localMorphs=source.MMDHLLocalMorphs
 for i,m in ipairs(mmdhl.GetMorphs(ent)) do
  local value
  if m.native>=0 then value=source:GetFlexWeight(m.native)
  elseif localMorphs then value=localMorphs[i] or 0
  else m.networkKey=m.networkKey or ('MMDHLMorph'..m.mmd) value=source:GetNW2Float(m.networkKey,0) end
  weights[i]=value*scale changed=changed or weights[i]~=previous[i]
 end
 for i=#mmdhl.GetMorphs(ent)+1,#weights do weights[i]=nil changed=true end
 if #previous~=#weights then changed=true end
 if changed then native.SetMorphs(mmdhl.GetInstance(ent),util.TableToJSON(weights)) ent.MMDHLClientMorphs=weights ent.MMDHLMorphBuffer=previous end
 local revision=source:GetNW2Int('MMDHLManualRevision',0)
 if ent.MMDHLManualApplied~=revision then
  for i=0,(mmdhl.assets[mmdhl.GetAsset(ent)].bones or 0)-1 do
   local raw=source:GetNW2String('MMDHLManual'..i,'')
   if raw~='' then native.SetBonePose(mmdhl.GetInstance(ent),i,raw) end
  end
  ent.MMDHLManualApplied=revision
 end
end
function mmdhl.UpdateClientInstances()
 -- Network creation/no-draw can arrive after the last Think hook. Refresh the
 -- entity list at the render boundary, and hand off before destroying the
 -- former actor's world. Otherwise a death produces several empty frames.
 mmdhl.InvalidateEntityList()
 local entities=mmdhl.Entities()
 for _,ent in ipairs(entities) do
  if not ent.MMDHLClientInstance and not mmdhl.PresentationSuppressed(ent) then
   local former=ent.MMDHLFormerActor or ent:GetNW2Entity('MMDHLFormerActor')
   if IsValid(former) then mmdhl.TransferPresentation(former,ent) end
  end
 end
 for ent in pairs(attached) do if not IsValid(ent) or not mmdhl.IsMMD(ent) or mmdhl.PresentationReleased(ent) then release(ent) end end
 for _,ent in ipairs(entities) do
  if not ent.MMDHLEditorPreview and not ent.MMDHLHands and (not ent:IsPlayer() or ent:Alive()) and RealTime()>=(ent.MMDHLAttachAfter or 0) then
   local former=ent.MMDHLFormerActor or ent:GetNW2Entity('MMDHLFormerActor')
   if IsValid(former) and not ent.MMDHLClientInstance then mmdhl.TransferPresentation(former,ent) end
   local ok,err,waiting=mmdhl.AttachPresentation(ent)
   if not ok and not ent.MMDHLFallbackPending then
    ent.MMDHLFallbackPending=true
    timer.Simple(0,function()
     if not IsValid(ent) then return end ent.MMDHLFallbackPending=nil
     if not ent.MMDHLClientInstance then fallback(ent) end
    end)
   end
   ent.MMDHLAttachError=not ok and err or nil ent.MMDHLAttachAfter=RealTime()+(ok and 0 or waiting and .05 or .5)
  end
 end
end
hook.Remove('Think','MMDHL.ClientInstances')
hook.Add('NotifyShouldTransmit','MMDHL.ClientVisibility',function(ent,transmitting)
 ent.MMDHLNotTransmitting=not transmitting
 -- Drawing already respects dormancy. Release at the next ordered render
 -- boundary so a corpse delivered in this update can inherit this world.
 if transmitting then ent.MMDHLAttachAfter=nil end
end)
hook.Add('HUDPaint','MMDHL.MissingAssetLabels',function()
 for _,ent in ipairs(mmdhl.Entities()) do if IsValid(ent.MMDHLFallback) then
  local p=(ent:GetPos()+Vector(0,0,80)):ToScreen()
  if p.visible then draw.SimpleText(ent.MMDHLAttachError or L'instances.loading_label','DermaDefault',p.x,p.y,Color(125,200,255),TEXT_ALIGN_CENTER) end
 end end
end)
hook.Add('PostCleanupMap','MMDHL.ClientInstances',function() for ent in pairs(attached) do release(ent) end end)
hook.Add('ShutDown','MMDHL.ClientInstances',function() for ent in pairs(attached) do release(ent) end end)
