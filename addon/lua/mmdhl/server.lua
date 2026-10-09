local native=mmdhl.native
local L=mmdhl.L
include('mmdhl/compat.lua')
util.AddNetworkString('mmdhl_action') util.AddNetworkString('mmdhl_notice')
util.AddNetworkString('mmdhl_spawn_status')
cleanup.Register('mmdhl')
local function notice(p,text) if IsValid(p) then net.Start('mmdhl_notice') net.WriteString(tostring(text)) net.Send(p) end end
local loading={}
local fitSequence=0
-- A map cleanup bumps this. Spawns still loading when it happens end with one
-- failure instead of creating entities (and undo entries) on the cleaned map.
mmdhl.cleanupGeneration=mmdhl.cleanupGeneration or 0
function mmdhl.LoadAsset(id,callback)
 local ok,err=native.RequestAsset(id) if not ok then callback(nil,err) return end
 if loading[id] then table.insert(loading[id],callback) return end
 loading[id]={callback} local started=SysTime()
 timer.Create('mmdhl_asset_'..id,.05,0,function()
  local info,e=mmdhl.Decode(native.AssetInfo(id))
  if info or e or SysTime()-started>60 then
   timer.Remove('mmdhl_asset_'..id) local callbacks=loading[id] loading[id]=nil
   for _,cb in ipairs(callbacks) do cb(info,e or (not info and L'server.error.asset_timeout')) end
  end
 end)
end
-- The model's saved collision corrections; physics_editor.lua replaces this with
-- the reader that also returns the saved mass and physics profile.
if not mmdhl.LoadSavedFit then
 function mmdhl.LoadSavedFit(id)
  local saved=util.JSONToTable(file.Read('mmd_hotloader/fit_overrides/'..id..'.json','DATA') or '')
  if not saved then return nil end
  if saved.version==3 and (saved.generator==14 or saved.generator==15 or saved.generator==18) then return {bodies=saved.bodies,scale=saved.scale,excludedMaterials=saved.excludedMaterials} end
  return nil,L'server.notice.old_fit_corrections'
 end
end
-- The bone window's pins for a model are server state in fit_overrides/<id>.json (saved
-- by admins): every new fit takes them from there, never from options an entity, a dupe
-- or a save carries. saved: the decoded file when the caller has read it (false: none).
function mmdhl.SavedBoneMap(id,saved)
 if not isstring(id) or #id~=64 or id:find('[^a-f0-9]') then return nil end
 if saved==nil then saved=util.JSONToTable(file.Read('mmd_hotloader/fit_overrides/'..id..'.json','DATA') or '') end
 if istable(saved) and istable(saved.boneMap) and next(saved.boneMap)~=nil then return saved.boneMap end
end
-- flags.replace: the ragdoll replaces an existing one (physics editor), which keeps its creator, undo and cleanup entries.
function mmdhl.Spawn(p,id,options,done,progress,flags)
 local available,why=mmdhl.FeatureAvailable('physics') if not available then if done then done(nil,mmdhl.ServerIssue('physics',why)) end return end
 options=mmdhl.WithSpawnDefaults(p,options)
 if not options.angles and options.position and IsValid(p) and options.role~='player' then
  options.angles={mmdhl.FacingPlayerAngles(p,Vector(unpack(options.position)),options.role or 'ragdoll'):Unpack()}
 end
 local completed,generation=false,mmdhl.cleanupGeneration
 local function finish(ent,err)
  if completed then return end completed=true
  if done then done(ent,err) elseif err then notice(p,err) end
 end
 local function cleaned() return mmdhl.cleanupGeneration~=generation end
 if not isstring(id) or #id~=64 or id:find('[^a-f0-9]') then finish(nil,L'server.error.invalid_model_id') return end
 -- The model's saved default (physics editor, collision editor) fills what the request leaves out.
 local savedPhysics,savedStyles=false,false
 if options.collisionOverrides==nil or options.physicsOverrides==nil or options.mass==nil then
  local saved,why=mmdhl.LoadSavedFit(id) if why then notice(p,why) end
  if saved then
   if options.collisionOverrides==nil and saved.bodies then
    options.collisionOverrides=saved.bodies options.collisionOverrideScale=saved.scale options.excludedMaterials=saved.excludedMaterials
    for _,o in pairs(saved.bodies) do if istable(o) and o.style~=nil and o.style~='fitted' then savedStyles=true end end
   end
   if options.physicsOverrides==nil and saved.physics then options.physicsOverrides=saved.physics options.physicsEditor=saved.editor and saved.editor.ui savedPhysics=true end
   if options.mass==nil and saved.mass then options.mass=saved.mass end
  end
 end
 -- Bones assigned in the bone window (fitter pins; natives without them ignore the option),
 -- always the current ones: a respawn's copied options may carry older pins.
 options.boneMap=mmdhl.SavedBoneMap(id)
 if mmdhl.CanUseAsset and not mmdhl.CanUseAsset(p,id) then finish(nil,L'server.error.not_approved') return end
 if options.role=='combine' or options.hostile then options=mmdhl.HostileActorOptions(p,options) end
 local actorError options,actorError=mmdhl.ActorOptions(options) if not options then finish(nil,actorError) return end
 mmdhl.LoadAsset(id,function(info,error)
  if cleaned() then finish(nil,L'server.error.map_cleanup') return end
  if not info then finish(nil,error) return end
  if not IsValid(p) then finish(nil,L'server.error.player_disconnected') return end
  if options.backend=='source' or (options.backend~='legacy' and GetConVar('mmdhl_native_carrier'):GetBool()) then
   if progress then progress(L'server.progress.preparing') end
   local function attempt(opts,retried)
    fitSequence=fitSequence+1 local started=SysTime() local timerName='mmdhl_fit_'..fitSequence
    -- A saved profile that no longer builds (an older module, a rejected shape) must not
    -- block the spawn: it is retried once without what the saved file added, and the player is told.
    -- fit: the native fitter's (English) reason. Only its verdicts on the skeleton (no bone
    -- for a body part, no height) reach players inside a token that says what it means; a
    -- rejected shape, mass or setting is said as it is. The physics editor (any flags,
    -- every operation) reads the raw reason itself.
    local function failed(reason,fit)
     if not retried and (savedPhysics or savedStyles) then
      local retry=table.Copy(opts)
      if savedPhysics then retry.physicsOverrides={} retry.physicsEditor=nil end
      if savedStyles then for _,o in pairs(retry.collisionOverrides or {}) do if istable(o) then o.style=nil end end end
      notice(p,L('physics_editor.notice.saved_failed',{reason=reason})) attempt(retry,true) return
     end
     local verdict=fit and not flags and tostring(reason):lower()
     verdict=verdict and (verdict:find('no bone',1,true) or verdict:find('landmark',1,true) or verdict:find('no height',1,true))
     finish(nil,verdict and L('server.error.fit_failed',{reason=reason}) or reason)
    end
    timer.Create(timerName,.05,0,function()
     if not IsValid(p) then timer.Remove(timerName) finish(nil,L'server.error.player_disconnected') return end
     if cleaned() then timer.Remove(timerName) finish(nil,L'server.error.map_cleanup') return end
     local ready,err=native.RequestCarrierFit(id,util.TableToJSON(opts))
     if ready==false and SysTime()-started<30 then return end
     timer.Remove(timerName)
     if not ready then if err then failed(err,true) else failed(L'server.error.fit_timeout') end return end
     local ok,e=xpcall(function() mmdhl.SpawnNative(p,id,opts,function(ent,reason)
      if IsValid(ent) then finish(ent) else failed(reason or L'server.error.native_ragdoll_failed') end
     end,flags) end,debug.traceback)
     if not ok then ErrorNoHalt('[Model Hotloader spawn] '..mmdhl.Localize(e)..'\n') finish(nil,L('server.error.create_failed',{reason=e})) end
    end)
   end
   attempt(options)

   return
  end
  local handle,err=native.CreateInstance(id,util.TableToJSON(options))
  if not handle then finish(nil,err) return end
  local ent=ents.Create('mmdhl_ragdoll') ent:SetAsset(id) ent:SetInstance(handle) ent:SetFrozen(options.frozen or false)
  ent.MMDOptions=table.Copy(options) ent:SetPos(Vector(unpack(options.position))) ent:Spawn() ent:SetCreator(p)
  gamemode.Call('PlayerSpawnedRagdoll',p,ent:GetModel(),ent)
  undo.Create('mmdhl.undo.ragdoll') undo.AddEntity(ent) undo.SetPlayer(p) undo.Finish()
  p:AddCleanup('mmdhl',ent) mmdhl.simulationEnabled=true
  finish(ent)
 end)
end
function mmdhl.EntityForInstance(id) for _,ent in ipairs(ents.FindByClass('mmdhl_ragdoll')) do if ent:GetInstance()==id then return ent end end end
net.Receive('mmdhl_action',function(_,p)
 local action=net.ReadString() local id=net.ReadString() local ent=Entity(net.ReadUInt(16)) local value=net.ReadString()
 if action=='spawn' then
  local settings=mmdhl.WithSpawnDefaults(p,util.JSONToTable(value) or {})
  local request=math.Clamp(math.floor(tonumber(settings.request) or 0),0,4294967295)
  local function reply(state,message,created)
   if not IsValid(p) then return end
   net.Start('mmdhl_spawn_status') net.WriteUInt(request,32) net.WriteString(state) net.WriteString(tostring(message or '')) net.WriteUInt(IsValid(created) and created:EntIndex() or 0,16) net.Send(p)
  end
  if not mmdhl.CanUseAsset(p,id) then reply('error',L'server.error.spawn_not_approved') return end
  if p.MMDHLSpawnPending then reply('error',L'server.error.spawn_pending') return end
  local role=({ragdoll=true,citizen=true,combine=true,player=true})[settings.role or 'ragdoll'] and (settings.role or 'ragdoll') or 'ragdoll'
  local tr=p:GetEyeTrace()
  if role~='player' and (not tr.Hit or tr.HitSky or tr.StartSolid or tr.HitPos:DistToSqr(p:EyePos())>4096^2) then reply('error',L'server.error.spawn_aim') return end
  local pos=role=='player' and p:GetPos() or tr.HitPos+tr.HitNormal*3
  local weapon=mmdhl.NPCWeapon(p,role)
  if role~='player' and gamemode.Call(role=='ragdoll' and 'PlayerSpawnRagdoll' or 'PlayerSpawnNPC',p,role=='ragdoll' and id or mmdhl.ActorClass(role),weapon)==false then reply('error',L'server.error.spawn_forbidden') return end
  p.MMDHLSpawnPending=true reply('loading',L'server.progress.loading')
  mmdhl.Spawn(p,id,{role=role,weapon=weapon,gender=settings.gender,armsParts=settings.armsParts,bodygroups=mmdhl.CleanBodygroups(settings.bodygroups),position={pos.x,pos.y,pos.z},angles={mmdhl.FacingPlayerAngles(p,pos,role):Unpack()},backend='source',secondaryBackend=mmdhl.ValidSecondaryBackend(settings.secondaryBackend),frozen=settings.frozen==true,collisionFlags=mmdhl.ValidCollisionFlags(settings.collisionFlags) or mmdhl.CollideDefault,mass=tonumber(settings.mass) and math.Clamp(tonumber(settings.mass),1,500) or nil,scaleMultiplier=math.Clamp(tonumber(settings.scaleMultiplier) or 1,.1,4)},function(created,err)
   if IsValid(p) then p.MMDHLSpawnPending=nil end
   reply(IsValid(created) and 'ready' or 'error',err or (role=='player' and L'server.spawned.player' or role=='ragdoll' and L'server.spawned.ragdoll' or L'server.spawned.npc'),created)
  end,function(message) reply('loading',message) end)
  return
 end
 if action=='bonemap' then if mmdhl.boneMapper and mmdhl.boneMapper.HandleSave then mmdhl.boneMapper.HandleSave(p,id,value) end return end
 if action=='bonemap_pins' then if mmdhl.boneMapper and mmdhl.boneMapper.HandleQuery then mmdhl.boneMapper.HandleQuery(p,id) end return end
 if not mmdhl.CanEdit(p,ent,'bodygroups') then return end
 if IsValid(ent) and ent:GetClass()~='mmdhl_ragdoll' and mmdhl.IsMMD(ent) then
  local h=mmdhl.GetInstance(ent)
  if action=='freeze' and ent:GetClass()=='prop_ragdoll' then local motion=ent:GetPhysicsObjectNum(0):IsMotionEnabled() for i=0,17 do local b=ent:GetPhysicsObjectNum(i) b:EnableMotion(not motion) if not motion then b:Wake() end end
  elseif action=='reset' then mmdhl.ResetPhysics(ent)
  elseif action=='remove' then if ent:IsPlayer() then mmdhl.RemovePlayerModel(ent) else ent:Remove() end
  elseif action=='morph' then local data=util.JSONToTable(value) or {} mmdhl.SetMorphWeight(ent,tonumber(data.index) or 0,tonumber(data.weight) or 0)
  elseif action=='bone' then local data=util.JSONToTable(value) or {} mmdhl.SetManualBonePose(ent,tonumber(data.index) or 0,data)
  elseif action=='fit' and ent:GetClass()=='prop_ragdoll' then
   -- The collision editor's corrected copy: the physics editor's rules (who may, how often,
   -- which values, the pins, the saved file) apply to it too.
   local P=mmdhl.physics
   if P and P.CollisionFit then P.CollisionFit(p,ent,value,notice) end
  elseif action=='replace' and not ent:IsPlayer() then
   local options=table.Copy(ent.MMDOptions) options.backend='source' options.frozen=true
   mmdhl.Spawn(p,id,options,function(replacement,err) if IsValid(replacement) then ent:Remove() else notice(p,err) end end)
  end return
 end
 if not IsValid(ent) or ent:GetClass()~='mmdhl_ragdoll' then return end
 local h=ent:GetInstance()
 if action=='freeze' then ent:SetFrozen(not ent:GetFrozen()) native.SetFrozen(h,ent:GetFrozen())
 elseif action=='reset' then mmdhl.ResetPhysics(ent)
 elseif action=='remove' then ent:Remove()
 elseif action=='morph' then local data=util.JSONToTable(value) or {} native.SetMorph(h,tonumber(data.index) or 0,tonumber(data.weight) or 0)
 elseif action=='bone' then local data=util.JSONToTable(value) or {} native.SetBonePose(h,tonumber(data.index) or 0,util.TableToJSON(data))
 elseif action=='replace' then
  -- mmdhl.Spawn checks approval for the Source-backend replacement above; this
  -- path creates its instance directly. A withdrawn model is not served to
  -- other players, who would see nothing.
  if not mmdhl.CanUseAsset(p,id) then notice(p,L'server.error.not_approved') return end
  local options=table.Copy(ent.MMDOptions) local bounds=mmdhl.Decode(native.GetBounds(h)) options.center=bounds and bounds.center options.frozen=ent:GetFrozen()
  mmdhl.LoadAsset(id,function(info,err)
   if not IsValid(ent) then return end
   if not info then notice(p,err) return end
   local replacement,e=native.CreateInstance(id,util.TableToJSON(options))
   if not replacement then notice(p,e) return end
   ent:SetInstance(replacement) ent:SetAsset(id) ent.MMDOptions=options native.DestroyInstance(h)
   ent.MMDInfo=nil ent.MMDAngles=nil ent.MMDFlexWeights=nil ent.MMDEyeTarget=nil ent.MMDHeld=nil ent.FingerIndex=nil
  end)
 end
end)
hook.Add('Tick','MMDHL.Simulation',function()
 if not game.SinglePlayer() or not mmdhl.simulationEnabled then return end
 mmdhl.UpdatePhysgun()
 local report,err=mmdhl.Decode(native.CapturePhysics())
 if report then mmdhl.bridge=report local ignored; ignored,err=native.Step(engine.TickInterval(),physenv.GetGravity()) end
 if err then mmdhl.simulationError=err mmdhl.simulationEnabled=false ErrorNoHalt('[Model Hotloader] '..mmdhl.Localize(err)..'\n') end
end)
hook.Add('PostCleanupMap','MMDHL.Cleanup',function() mmdhl.cleanupGeneration=mmdhl.cleanupGeneration+1 native.Clear() mmdhl.simulationEnabled=false end)
duplicator.RegisterEntityClass('mmdhl_ragdoll',function(p,data)
 local entry=data.EntityMods and data.EntityMods.MMDHL if not entry then return end
 -- Both backends below create the character directly; a dupe or save may name
 -- a model whose approval was withdrawn since.
 if not mmdhl.CanUseAsset(p,entry.asset) then notice(p,L'persistence.error.model_not_approved') return end
 if GetConVar('mmdhl_native_carrier'):GetBool() and mmdhl.SpawnNative then
  local options=table.Copy(entry.options or {}) options.backend='source' options.frozen=entry.state and entry.state.frozen or options.frozen
  -- Dupe data comes from the pasting client: bone pins are the server's.
  options.boneMap=mmdhl.SavedBoneMap(entry.asset)
  local oldCenter=entry.state and entry.state.center or options.position or {0,0,0}
  local offset=data.Pos-Vector(unpack(oldCenter)) options.position={((Vector(unpack(options.position or {0,0,0}))+offset)):Unpack()} options.center=nil
  local ent=mmdhl.SpawnNative(p,entry.asset,options) if not IsValid(ent) then return end
  ent:SetFlexScale(entry.flexScale or 1)
  if entry.state then for i,pose in ipairs(entry.state.manual or {}) do native.SetBonePose(mmdhl.GetInstance(ent),i-1,util.TableToJSON(pose)) end for i,weight in ipairs(entry.state.morphs or {}) do mmdhl.SetMorphWeight(ent,i-1,entry.flexScale==0 and 0 or weight/(entry.flexScale or 1)) end end
  return ent
 end
 local options=table.Copy(entry.options or {}) options.center={data.Pos.x,data.Pos.y,data.Pos.z}
 local h,err=native.CreateInstance(entry.asset,util.TableToJSON(options)) if not h then notice(p,err) return end
 local ent=ents.Create('mmdhl_ragdoll') ent:SetAsset(entry.asset) ent:SetInstance(h) ent:SetFrozen(options.frozen or false) ent.MMDOptions=options ent:SetPos(data.Pos) ent:Spawn() ent:SetCreator(p)
 if entry.state then native.SetState(h,util.TableToJSON(entry.state),data.Pos) ent:SetFrozen(entry.state.frozen or false) end
 ent.MMDAngles=entry.angles ent:SetNW2Float('MMDFlexScale',entry.flexScale or 1)
 p:AddCleanup('mmdhl',ent) mmdhl.simulationEnabled=true return ent
end,'Data')

util.AddNetworkString('mmdhl_forget_assets')
net.Receive('mmdhl_forget_assets',function(_,ply)
 -- Only the game's own server shares the client's cache: single player, or the
 -- listen server's host. Other players' deletions are their local copies.
 if not game.SinglePlayer() and not (IsValid(ply) and ply:IsListenServerHost()) then return end
 local ids=util.JSONToTable(net.ReadString()) if not istable(ids) then return end
 native.ForgetAssets(util.TableToJSON(ids))
 if mmdhl.ForgetPublishedAssets then mmdhl.ForgetPublishedAssets(ids) end
 for _,id in ipairs(ids) do if mmdhl.assets then mmdhl.assets[id]=nil end end
end)
