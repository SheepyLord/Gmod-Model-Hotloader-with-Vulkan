-- Ragdoll physics editor: the server's operations (permissions, rate limits,
-- builds, replace in place, saved defaults) and the client's request glue,
-- context menu entry and console command. The profile model is
-- physics_profile.lua; the window is physics_editor_ui.lua (client).
-- Builds happen only on the server; clients download the keyed carrier like any other.
-- autorun sends physics_profile.lua and physics_editor_ui.lua before its installation check.
include('mmdhl/physics_profile.lua')
local P=mmdhl.physics if not P then return end
local L=mmdhl.L
local Protocol=1
local MaxPayload=60000
local Ops={open=true,close=true,test=true,apply=true,previous=true,reset=true,restore_saved=true,save_default=true,clear_default=true}
local Builds={test=true,apply=true,previous=true,reset=true,restore_saved=true}
local Replaces={apply=true,previous=true,reset=true,restore_saved=true}
local Based={test=true,apply=true,previous=true,reset=true,restore_saved=true,save_default=true}
local Pinned={test=true,apply=true,save_default=true}
-- i18n-keys: physics_editor.error.disabled physics_editor.error.admin_only physics_editor.error.not_allowed physics_editor.error.no_saved physics_editor.error.no_previous
-- The options a fit depends on besides the edited ones; the client preview sends them back.
local FitKeys={'scaleMultiplier','scale','height','role','gender','animationSource','animationReference','armsParts'}
local EditedKeys={'collisionOverrides','collisionOverrideScale','excludedMaterials','mass','physicsOverrides','physicsEditor'}
function P.SavedPath(asset) return 'mmd_hotloader/fit_overrides/'..asset..'.json' end
local function compress(t) local raw=util.TableToJSON(t or {}) return util.Compress(raw) or '' end
local function decompress(data) if not isstring(data) or data=='' then return nil end local raw=util.Decompress(data,262144) return raw and util.JSONToTable(raw) or nil end
-- What a ragdoll was built with, for Previous version. Absent values become their explicit
-- defaults: rebuilding must not pick up a saved default the ragdoll never had.
function P.Subset(options)
 -- mass and collisionOverrideScale are numbers; table.Copy accepts only tables.
 local out={} for _,k in ipairs(EditedKeys) do local v=options and options[k] if istable(v) then v=table.Copy(v) end out[k]=v end
 out.collisionOverrides=out.collisionOverrides or {} out.excludedMaterials=out.excludedMaterials or {} out.physicsOverrides=out.physicsOverrides or {} out.mass=out.mass or 70
 return out
end
function P.StripStyles(overrides) local out=table.Copy(overrides or {}) for _,o in pairs(out) do if istable(o) then o.style=nil end end return out end

-- Replicated: clients see why the editor is read-only and what the limits are.
local mode=CreateConVar('mmdhl_physics_editor',game.IsDedicated() and '1' or '2',FCVAR_ARCHIVE+FCVAR_REPLICATED+FCVAR_NOTIFY,'Who may change ragdoll physics: 0 nobody, 1 admins, 2 anyone who may edit the ragdoll',0,2)
local cooldown=CreateConVar('mmdhl_physics_editor_cooldown','3',FCVAR_ARCHIVE+FCVAR_REPLICATED,'Seconds between two physics builds of one player',0,60)
local budget=CreateConVar('mmdhl_physics_editor_budget','20',FCVAR_ARCHIVE+FCVAR_REPLICATED,'Physics builds per player per 10 minutes (0: unlimited; superadmins are exempt)',0,1000)
if SERVER then
 util.AddNetworkString('mmdhl_physics') util.AddNetworkString('mmdhl_physics_status')
 -- 1 when this server's native module builds physics profiles; 0 means shapes only.
 function mmdhl.PhysicsLevel()
  local native=mmdhl.native local caps=native and native.GetCapabilities and mmdhl.Decode(native.GetCapabilities())
  return istable(caps) and tonumber(caps.physicsEditor) or 0
 end
 local function publishLevel() SetGlobal2Int('MMDHLPhysicsEditor',mmdhl.PhysicsLevel()) end
 publishLevel() hook.Add('MMDHL.InstallationChanged','MMDHL.PhysicsEditorLevel',publishLevel)
 function P.CanSaveDefault(p,asset)
  local ok=game.SinglePlayer() or (IsValid(p) and (p:IsListenServerHost() or p:IsAdmin()))
  local h=hook.Run('MMDHLCanSavePhysicsDefault',p,asset) if h~=nil then ok=h==true end
  return ok
 end
 function P.Can(p,ent,op)
  if op=='open' or op=='close' then return true end
  local m=mode:GetInt()
  if m==0 then return false,'physics_editor.error.disabled' end
  if m==1 and not (IsValid(p) and p:IsAdmin()) then return false,'physics_editor.error.admin_only' end
  -- CanProperty: prop protection decides who may edit someone else's ragdoll.
  if not mmdhl.CanEdit(p,ent,'mmdhl_physics') then return false,'physics_editor.error.not_allowed' end
  if hook.Run('MMDHLCanEditPhysics',p,ent,op)==false then return false,'physics_editor.error.not_allowed' end
  if (op=='save_default' or op=='clear_default') and not P.CanSaveDefault(p,mmdhl.GetAsset(ent)) then return false,'physics_editor.error.admin_only' end
  return true
 end
 local function readSaved(asset) local t=util.JSONToTable(file.Read(P.SavedPath(asset),'DATA') or '') return istable(t) and t or nil end
 -- fit_overrides/<asset>.json is shared: the model's default for new spawns (these keys,
 -- written by this editor and the collision editor) and the bone window's pins, which
 -- every fit of the model takes. Each writer changes its own keys and keeps the rest.
 local DefaultKeys={'bodies','scale','collisionOverrideScale','excludedMaterials','mass','physics','editor'}
 local PinKeys={'boneMap','boneMapVersion','boneMapSavedAt'}
 local function currentFormat(saved) return saved.version==3 and (saved.generator==14 or saved.generator==15 or saved.generator==18) end
 -- Whether the file holds a default for new spawns, not only pins.
 function P.HasSavedDefault(saved) return istable(saved) and (istable(saved.bodies) or istable(saved.physics) or tonumber(saved.mass)~=nil) end
 -- The file as a writer starts from: as it is, or, in an older format whose corrections
 -- no longer load, only the bone window's pins.
 local function savedForWrite(asset)
  local saved=readSaved(asset) or {}
  if not currentFormat(saved) then local pins={} for _,k in ipairs(PinKeys) do pins[k]=saved[k] end saved=pins end
  saved.version=3 saved.generator=18
  return saved
 end
 local warned={}
 -- The saved per-model default (§5.8) for a spawn, or nil; the second value is a notice token.
 function mmdhl.LoadSavedFit(asset)
  local saved=readSaved(asset) if not saved then return nil end
  if not currentFormat(saved) then return nil,L'server.notice.old_fit_corrections' end
  local out={bodies=istable(saved.bodies) and saved.bodies or nil,scale=tonumber(saved.scale),excludedMaterials=istable(saved.excludedMaterials) and saved.excludedMaterials or nil}
  local mass=tonumber(saved.mass) if mass and mass>=1 and mass<=500 then out.mass=mass end
  local why
  if saved.physics~=nil then
   if istable(saved.physics) and P.Validate({physicsOverrides=saved.physics}) then
    out.physics=saved.physics out.editor=istable(saved.editor) and saved.editor or nil
    -- Kept for when the server's module is updated; only the shapes apply until then.
    if mmdhl.PhysicsLevel()<1 and not warned[asset] then warned[asset]=true why=L'physics_editor.notice.saved_physics_needs_update' end
   else why=L'physics_editor.notice.saved_physics_invalid' end
  end
  return out,why
 end
 local function writeSaved(asset,fit)
  -- file.Write takes only some extensions (.txt, .json…); the old default stays until the new one is on disk.
  local path=P.SavedPath(asset) local temp=path:sub(1,-6)..'.new.txt' local text=util.TableToJSON(fit,true)
  file.CreateDir('mmd_hotloader/fit_overrides') file.Write(temp,text)
  if file.Read(temp,'DATA')~=text then file.Delete(temp) return false end
  file.Delete(path) if not (file.Rename and file.Rename(temp,path)) then file.Write(path,text) file.Delete(temp) end
  return file.Read(path,'DATA')==text
 end
 P.WriteSaved=writeSaved P.ReadSaved=readSaved
 -- Saves what the ragdoll has now (never what a client sends) as the model's default.
 function mmdhl.SavePhysicsDefault(p,ent)
  local asset=mmdhl.GetAsset(ent) local o=ent.MMDOptions or {} local rig=mmdhl.GetRig(ent) or {}
  local physics=istable(rig.physicsOverrides) and rig.physicsOverrides or o.physicsOverrides
  -- The whole default is replaced; the bone window's pins stay.
  local fit=savedForWrite(asset) for _,k in ipairs(DefaultKeys) do fit[k]=nil end
  fit.bodies=table.Copy(o.collisionOverrides or {}) fit.scale=tonumber(o.collisionOverrideScale) or rig.scale fit.excludedMaterials=table.Copy(o.excludedMaterials or {}) fit.mass=tonumber(o.mass)
  fit.physics=istable(physics) and next(physics)~=nil and table.Copy(physics) or nil
  fit.editor={schema=1,ui=P.SanitizeEditor(o.physicsEditor),savedAt=os.time(),savedBy=IsValid(p) and p:SteamID64() or nil,savedByName=IsValid(p) and p:Nick() or nil}
  if not writeSaved(asset,fit) then return false,'physics_editor.error.save_failed' end
  return true,fit
 end
 -- Forgets the default; a file with the bone window's pins keeps them (the model would no
 -- longer fit without them), any other is deleted.
 function mmdhl.ClearPhysicsDefault(p,ent)
  local asset=mmdhl.GetAsset(ent) local path=P.SavedPath(asset)
  local rest=savedForWrite(asset) for _,k in ipairs(DefaultKeys) do rest[k]=nil end
  for k in pairs(rest) do if k~='version' and k~='generator' then return writeSaved(asset,rest) end end
  file.Delete(path)
  return not file.Exists(path,'DATA')
 end
 function mmdhl.PhysicsState(p,ent)
  local o=ent.MMDOptions or {} local rig=mmdhl.GetRig(ent) or {} local asset=mmdhl.GetAsset(ent) local saved=readSaved(asset)
  local fit={} for _,k in ipairs(FitKeys) do if o[k]~=nil then fit[k]=o[k] end end
  -- Builds take the saved pins (mmdhl.Spawn); the client's preview must fit with the same ones.
  fit.boneMap=mmdhl.SavedBoneMap and mmdhl.SavedBoneMap(asset,saved or false) or nil
  local editor=saved and istable(saved.editor) and saved.editor or {}
  -- A previous version made for other bones than the saved pins cannot come back (handle).
  local previous=istable(ent.MMDHLPhysicsHistory) and ent.MMDHLPhysicsHistory[1]
  return {base=ent:GetNW2String('MMDHLRig',''),level=mmdhl.PhysicsLevel(),canEdit=P.Can(p,ent,'apply')==true,canSave=P.Can(p,ent,'save_default')==true,
   hasPrevious=istable(previous) and P.PinsCurrent(previous.boneMap,asset),fitOptions=fit,
   applied={collisionOverrides=o.collisionOverrides or {},collisionOverrideScale=tonumber(o.collisionOverrideScale) or rig.scale,excludedMaterials=o.excludedMaterials or {},mass=tonumber(o.mass) or 70,
    physicsOverrides=istable(rig.physicsOverrides) and rig.physicsOverrides or o.physicsOverrides or {},physicsEditor=o.physicsEditor},
   savedDefault={exists=P.HasSavedDefault(saved),hasPhysics=saved~=nil and istable(saved.physics),savedAt=editor.savedAt,savedByName=editor.savedByName},
   materialCount=tonumber(rig.materialCount) or 0}
 end
 -- Swaps a freshly built ragdoll in for the old one, keeping its pose, look,
 -- constraints, owner, undo and cleanup entries. Nothing changes until it all worked.
 function mmdhl.ReplaceRagdoll(p,old,new,op)
  local warnings={}
  local ok,err=xpcall(function()
   for i=0,17 do local a,b=old:GetPhysicsObjectNum(i),new:GetPhysicsObjectNum(i)
    if IsValid(a) and IsValid(b) then
     b:SetPos(a:GetPos()) b:SetAngles(a:GetAngles()) b:EnableMotion(a:IsMotionEnabled())
     if a:IsMotionEnabled() then b:SetVelocity(a:GetVelocity()) b:AddAngleVelocity(a:GetAngleVelocity()-b:GetAngleVelocity()) if a:IsAsleep() then b:Sleep() else b:Wake() end end
    end
   end
   new:SetSkin(old:GetSkin()) new:SetColor(old:GetColor()) new:SetMaterial(old:GetMaterial()) new:SetRenderMode(old:GetRenderMode()) new:SetRenderFX(old:GetRenderFX()) new:SetCollisionGroup(old:GetCollisionGroup())
   for _,group in ipairs(old:GetBodyGroups() or {}) do new:SetBodygroup(group.id,old:GetBodygroup(group.id)) end
   if mmdhl.BindEntity and mmdhl.CaptureNativeState then mmdhl.BindEntity(new,mmdhl.CaptureNativeState(old)) end
   local lost=0
   for _,c in ipairs(constraint.GetTable(old) or {}) do
    local map={} for _,e in pairs(c.Entity or {}) do if e.Index then map[e.Index]=e.Entity==old and new or e.Entity end end
    local made,result=pcall(duplicator.CreateConstraintFromTable,c,map,p)
    if not made or not result then lost=lost+1 end
   end
   if lost>0 then warnings[#warnings+1]=L('physics_editor.notice.constraints_lost',{count=lost}) end
   local owner=IsValid(old:GetCreator()) and old:GetCreator() or p
   new:SetCreator(owner) if new.CPPISetOwner then new:CPPISetOwner(owner) end
   if IsValid(owner) then gamemode.Call('PlayerSpawnedRagdoll',owner,new:GetModel(),new) end
   undo.ReplaceEntity(old,new) cleanup.ReplaceEntity(old,new)
   local history=istable(old.MMDHLPhysicsHistory) and old.MMDHLPhysicsHistory or {}
   if op=='previous' then local rest={} for i=2,#history do rest[#rest+1]=history[i] end new.MMDHLPhysicsHistory=rest
   else
    -- A version keeps the pins it was fitted with: its shapes are for those bones.
    local version=P.Subset(old.MMDOptions) local pins=old.MMDOptions and old.MMDOptions.boneMap version.boneMap=istable(pins) and table.Copy(pins) or nil
    local list={version} for i=1,math.min(#history,9) do list[#list+1]=history[i] end new.MMDHLPhysicsHistory=list
   end
   if mmdhl.StoreNativeState then mmdhl.StoreNativeState(new) end
  end,debug.traceback)
  if not ok then if IsValid(new) then new:Remove() end return false,err end
  hook.Run('MMDHLPhysicsApplied',p,old,new)
  if IsValid(p) and p.MMDHLPhysicsTestCopy==old then p.MMDHLPhysicsTestCopy=nil end
  old:Remove()
  return true,warnings
 end
 local function reply(p,request,state,message,ent,payload)
  if not IsValid(p) then return end
  local data=payload and compress(payload) or ''
  -- The state carries the animation reference for an exact key; drop it before going over the net limit.
  if #data>MaxPayload and istable(payload) and istable(payload.fitOptions) then payload.fitOptions.animationReference=nil payload.approximateKey=true data=compress(payload) end
  if #data>MaxPayload then data='' end
  net.Start('mmdhl_physics_status') net.WriteUInt(request,32) net.WriteString(state) net.WriteString(message or '') net.WriteUInt(IsValid(ent) and ent:EntIndex() or 0,16) net.WriteUInt(#data,16) if #data>0 then net.WriteData(data,#data) end net.Send(p)
 end
 P.Reply=reply
 local function removeTestCopy(p) if IsValid(p) and IsValid(p.MMDHLPhysicsTestCopy) then p.MMDHLPhysicsTestCopy:Remove() end if IsValid(p) then p.MMDHLPhysicsTestCopy=nil end end
 hook.Add('PlayerDisconnected','MMDHL.PhysicsTestCopy',removeTestCopy)
 -- A native build failure as the token the editor shows (§7.5).
 function P.FailureToken(err)
  local text=tostring(err or '')
  local body=tonumber(text:match('VPhysics distorted body (%d+)') or text:match('%(body (%d+)%)'))
  if body and P.IDS[body+1] then return L('physics_editor.error.hull_rejected',{part=L('physics_editor.part.'..P.IDS[body+1])}) end
  local code,path=text:match('Invalid physics settings: (%S+) (%S*)')
  if code then return L('physics_editor.error.invalid',{field=path,reason=code}) end
  if text:find('Invalid collision',1,true) then return L'physics_editor.error.invalid_shape' end
  if text:find('server.error.fit_timeout',1,true) then return L'physics_editor.error.timeout' end
  return L('physics_editor.error.build_failed',{reason=text})
 end
 -- A build's options start from the ragdoll's own, as a ragdoll: an NPC's corpse (actors.lua)
 -- keeps the NPC's role, weapon and side, with which mmdhl.Spawn would make an NPC.
 local function ragdollOptions(ent)
  local o=table.Copy(ent.MMDOptions or {})
  o.rigManifest=nil o.backend='source' o.role='ragdoll' o.hostile=nil o.weapon=nil
  return o
 end
 -- The options a build uses, from the ragdoll's own and the operation's (§7.5 step 11).
 function P.BuildOptions(p,ent,op,request,level)
  local o=ragdollOptions(ent) local rig=mmdhl.GetRig(ent) or {}
  o.position={ent:GetPos():Unpack()} o.angles={ent:GetAngles():Unpack()} o.frozen=Replaces[op] and true or false
  if op=='test' or op=='apply' then
   o.collisionOverrides=request.collisionOverrides or {} o.collisionOverrideScale=tonumber(rig.scale) or request.collisionOverrideScale o.excludedMaterials=request.excludedMaterials or {}
   o.mass=tonumber(request.mass) or o.mass or 70
   -- An older server module cannot build them; the ragdoll keeps its own until it is updated.
   if level>=1 then o.physicsOverrides=request.physicsOverrides or {} o.physicsEditor=P.SanitizeEditor(request.physicsEditor) end
  elseif op=='reset' then
   -- A real 70, never nil: nil would load the saved mass again.
   o.collisionOverrides={} o.excludedMaterials={} o.physicsOverrides={} o.mass=70 o.physicsEditor=nil
  elseif op=='restore_saved' then
   -- A file with only the bone window's pins is no saved physics.
   local saved=mmdhl.LoadSavedFit(mmdhl.GetAsset(ent)) if not P.HasSavedDefault(saved) then return nil,'physics_editor.error.no_saved' end
   o.collisionOverrides=saved.bodies or {} o.collisionOverrideScale=saved.scale o.excludedMaterials=saved.excludedMaterials or {} o.physicsOverrides=saved.physics or {} o.mass=saved.mass or 70
   o.physicsEditor=saved.editor and P.SanitizeEditor(saved.editor.ui) or nil
  elseif op=='previous' then
   local h=istable(ent.MMDHLPhysicsHistory) and ent.MMDHLPhysicsHistory[1] if not h then return nil,'physics_editor.error.no_previous' end
   for _,k in ipairs(EditedKeys) do local v=h[k] if istable(v) then v=table.Copy(v) end o[k]=v end
  end
  if level<1 then o.collisionOverrides=P.StripStyles(o.collisionOverrides) end
  return o
 end
 local function recordBuild(p)
  if not IsValid(p) then return end
  p.MMDHLPhysicsLastBuild=CurTime() local window={} for _,t in ipairs(p.MMDHLPhysicsBuilds or {}) do if CurTime()-t<600 then window[#window+1]=t end end
  window[#window+1]=CurTime() p.MMDHLPhysicsBuilds=window
 end
 -- Whether shapes made on a carrier fitted with these pins still fit the model: every build
 -- takes the saved pins (mmdhl.Spawn), and a shape sits in its body part's bone frame. Only
 -- the body parts' pins count, by the rule with which saving bones keeps saved corrections.
 function P.PinsCurrent(pins,asset)
  local BM=mmdhl.boneMapper
  if not (BM and BM.SamePhysicalPins and mmdhl.SavedBoneMap) then return true end
  return BM.SamePhysicalPins(pins,mmdhl.SavedBoneMap(asset))
 end
 function P.RateLimited(p)
  local wait=cooldown:GetFloat()-(CurTime()-(p.MMDHLPhysicsLastBuild or -math.huge))
  if wait>0 then return math.ceil(wait) end
  local limit=budget:GetInt()
  if limit>0 and not p:IsSuperAdmin() then local recent,oldest=0,math.huge
   for _,t in ipairs(p.MMDHLPhysicsBuilds or {}) do if CurTime()-t<600 then recent=recent+1 oldest=math.min(oldest,t) end end
   if recent>=limit then return math.max(1,math.ceil(600-(CurTime()-oldest))) end
  end
 end
 local function handle(p,request,op,ent,payload)
  local function answer(state,message,target,data) reply(p,request,state,message,target,data) end
  -- A test copy still building when the editor closes is removed when it arrives.
  if op=='close' then p.MMDHLPhysicsSession=(p.MMDHLPhysicsSession or 0)+1 removeTestCopy(p) return end
  if op=='open' then
   -- Cheap to ask, not to answer: at most 4 states a second per player.
   local recent={} for _,t in ipairs(p.MMDHLPhysicsOpens or {}) do if CurTime()-t<1 then recent[#recent+1]=t end end
   p.MMDHLPhysicsOpens=recent if #recent>=4 then answer('error',L('physics_editor.error.rate_limited',{seconds=1}),ent,{seconds=1}) return end
   recent[#recent+1]=CurTime()
  end
  if not mmdhl.native then answer('error',L'physics_editor.error.server_core') return end
  if not (IsValid(ent) and ent:GetClass()=='prop_ragdoll' and mmdhl.IsMMD(ent) and ent:GetPhysicsObjectCount()==18) then answer('error',L'physics_editor.error.not_ragdoll') return end
  if op=='open' then answer('state','',ent,mmdhl.PhysicsState(p,ent)) return end
  local allowed,why=P.Can(p,ent,op) if not allowed then answer('error',L(why)) return end
  if ent.MMDHLPhysicsBusy or p.MMDHLPhysicsBusy then answer('error',L'physics_editor.error.busy') return end
  if Based[op] and payload.base~=ent:GetNW2String('MMDHLRig','') then answer('error',L'physics_editor.error.stale') return end
  if Replaces[op] and ent:GetNW2Bool('MMDHLPhysgunHeld',false) then answer('error',L'physics_editor.error.held') return end
  local asset=mmdhl.GetAsset(ent)
  -- The ragdoll's shapes (its draft, Save for new spawns) and an earlier version's were made
  -- for the bones it was fitted with; after the bone window changed them, they would not fit.
  -- Reset and Restore saved use none of them: they rebuild it with the current bones.
  if Pinned[op] and not P.PinsCurrent(ent.MMDOptions and ent.MMDOptions.boneMap,asset) then answer('error',L'physics_editor.error.bones_changed') return end
  local version=op=='previous' and istable(ent.MMDHLPhysicsHistory) and ent.MMDHLPhysicsHistory[1]
  if istable(version) and not P.PinsCurrent(version.boneMap,asset) then answer('error',L'physics_editor.error.previous_bones_changed') return end
  if op=='save_default' then
   local saved=mmdhl.SavePhysicsDefault(p,ent)
   if saved then answer('ready',L('physics_editor.notice.saved',{name=asset:sub(1,12)}),ent,{savedAt=os.time()}) else answer('error',L'physics_editor.error.save_failed') end
   return
  end
  if op=='clear_default' then
   if mmdhl.ClearPhysicsDefault(p,ent) then answer('ready',L('physics_editor.notice.forgotten',{name=asset:sub(1,12)}),ent) else answer('error',L'physics_editor.error.save_failed') end
   return
  end
  local wait=P.RateLimited(p) if wait then answer('error',L('physics_editor.error.rate_limited',{seconds=wait}),ent,{seconds=wait}) return end
  local level=mmdhl.PhysicsLevel() local warnings={}
  local req=istable(payload.request) and payload.request or {}
  if op=='test' or op=='apply' then
   if level<1 then req=P.ShapesOnly(req) warnings[#warnings+1]=L'physics_editor.notice.server_shapes_only' end
   local rig=mmdhl.GetRig(ent)
   local valid,errors=P.Validate(req,{unit=P.Unit(rig),level=level,materialCount=rig and rig.materialCount,surfaceKnown=function(name) return util.GetSurfaceIndex(name)>=0 end})
   if not valid then local e=errors[1] answer('error',e.code=='surfaceprop_unknown' and L('physics_editor.issue.surfaceprop_unknown',{name=e.reason}) or L('physics_editor.error.invalid',{field=e.field,reason=e.reason}),ent,{field=e.field,reason=e.reason}) return end
  end
  if op=='test' and not IsValid(p.MMDHLPhysicsTestCopy) and gamemode.Call('PlayerSpawnRagdoll',p,asset)==false then answer('error',L'physics_editor.error.spawn_limit') return end
  local o,missing=P.BuildOptions(p,ent,op,req,level) if not o then answer('error',L(missing)) return end
  if op=='test' then
   local right=Angle(0,p:EyeAngles().y,0):Right()
   o.position={(ent:GetPos()+right*(ent:BoundingRadius()*2+24)+Vector(0,0,10)):Unpack()}
  end
  ent.MMDHLPhysicsBusy=true p.MMDHLPhysicsBusy=true local session=p.MMDHLPhysicsSession
  answer('building','',ent)
  local finished=false
  local function finish() finished=true if IsValid(ent) then ent.MMDHLPhysicsBusy=nil end if IsValid(p) then p.MMDHLPhysicsBusy=nil end recordBuild(p) end
  local ok,err=xpcall(function()
   mmdhl.Spawn(p,asset,o,function(new,failure)
    if finished then if IsValid(new) then new:Remove() end return end
    local done,problem=xpcall(function()
     if not IsValid(new) then answer('error',P.FailureToken(failure),ent) return end
     if op=='test' then
      if not IsValid(p) or p.MMDHLPhysicsSession~=session then new:Remove() answer('error','') return end
      removeTestCopy(p) new:SetNW2Bool('MMDHLPhysicsTestCopy',true) p.MMDHLPhysicsTestCopy=new
      answer('ready',L'physics_editor.notice.test_spawned',new,{key=new:GetNW2String('MMDHLRig',''),warnings=warnings}) return
     end
     if not IsValid(ent) then new:Remove() answer('error',L'physics_editor.error.not_ragdoll') return end
     local replaced,result=mmdhl.ReplaceRagdoll(p,ent,new,op)
     if not replaced then ErrorNoHalt('[Model Hotloader physics] '..tostring(result)..'\n') answer('error',L('physics_editor.error.build_failed',{reason=tostring(result):match('^[^\n]*')})) return end
     for _,w in ipairs(result) do warnings[#warnings+1]=w end
     local state=mmdhl.PhysicsState(p,new) state.warnings=warnings
     answer('ready',L'physics_editor.notice.applied',new,state)
    end,debug.traceback)
    finish()
    if not done then ErrorNoHalt('[Model Hotloader physics] '..tostring(problem)..'\n') answer('error',L('physics_editor.error.build_failed',{reason=tostring(problem):match('^[^\n]*')})) end
   end,nil,{replace=Replaces[op]==true})
  end,debug.traceback)
  if not ok then finish() ErrorNoHalt('[Model Hotloader physics] '..tostring(err)..'\n') answer('error',L('physics_editor.error.build_failed',{reason=tostring(err):match('^[^\n]*')})) end
 end
 P.Handle=handle
 -- The collision editor's "Save fit and spawn corrected copy" (mmdhl_action 'fit'): a new
 -- ragdoll beside this one with the sent shapes, saved for new spawns when the player may
 -- save defaults. It is a build like a test copy: the same permission, limits and checks.
 -- notice(p, token) answers the player.
 function P.CollisionFit(p,ent,value,notice)
  local allowed,why=P.Can(p,ent,'test') if not allowed then notice(p,L(why)) return end
  if ent.MMDHLPhysicsBusy or p.MMDHLPhysicsBusy then notice(p,L'physics_editor.error.busy') return end
  local asset=mmdhl.GetAsset(ent)
  if not P.PinsCurrent(ent.MMDOptions and ent.MMDOptions.boneMap,asset) then notice(p,L'server.error.fit_bones_changed') return end
  local wait=P.RateLimited(p) if wait then notice(p,L('physics_editor.error.rate_limited',{seconds=wait})) return end
  local data=isstring(value) and #value<=MaxPayload and util.JSONToTable(value) or nil
  if not istable(data) then notice(p,L('physics_editor.error.invalid',{field='collisionOverrides',reason='not_object'})) return end
  local rig=mmdhl.GetRig(ent) or {} local level=mmdhl.PhysicsLevel()
  local req={collisionOverrides=data.bodies or data,excludedMaterials=data.excludedMaterials or {}}
  if level<1 then req=P.ShapesOnly(req) end
  local valid,errors=P.Validate(req,{unit=P.Unit(rig),level=level,materialCount=rig.materialCount})
  if not valid then local e=errors[1] notice(p,L('physics_editor.error.invalid',{field=e.field,reason=e.reason})) return end
  if gamemode.Call('PlayerSpawnRagdoll',p,asset)==false then notice(p,L'server.error.spawn_forbidden') return end
  -- The model's default is server-wide: only those who may save physics defaults change it.
  local canSave=P.Can(p,ent,'save_default')==true
  local o=ragdollOptions(ent)
  o.collisionOverrides=req.collisionOverrides o.collisionOverrideScale=rig.scale o.excludedMaterials=req.excludedMaterials
  o.position={ent:GetPos():Unpack()} o.position[2]=o.position[2]+100 o.frozen=true
  p.MMDHLPhysicsBusy=true
  local finished=false
  local function finish() if finished then return false end finished=true if IsValid(p) then p.MMDHLPhysicsBusy=nil end recordBuild(p) return true end
  local ok,err=xpcall(function()
   mmdhl.Spawn(p,asset,o,function(created,failure)
    if not finish() then return end
    if not IsValid(created) then notice(p,failure) return end
    if not canSave then notice(p,L'physics_editor.notice.fit_not_saved') return end
    -- Only the shapes change: a saved mass, physics profile and the bone window's pins stay.
    local fit=savedForWrite(asset) fit.bodies=o.collisionOverrides fit.scale=o.collisionOverrideScale fit.excludedMaterials=o.excludedMaterials
    notice(p,writeSaved(asset,fit) and L'server.notice.fit_saved' or L'physics_editor.error.save_failed')
   end)
  end,debug.traceback)
  if not ok then finish() ErrorNoHalt('[Model Hotloader physics] '..tostring(err)..'\n') notice(p,L('physics_editor.error.build_failed',{reason=tostring(err):match('^[^\n]*')})) end
 end
 net.Receive('mmdhl_physics',function(_,p)
  local protocol=net.ReadUInt(8) local request=net.ReadUInt(32) local op=net.ReadString() local ent=net.ReadEntity() local n=net.ReadUInt(16)
  local data=n>0 and net.ReadData(n) or ''
  if protocol~=Protocol or not Ops[op] then reply(p,request,'error',L('physics_editor.error.invalid',{field='op',reason='protocol'})) return end
  if n>MaxPayload then reply(p,request,'error',L'physics_editor.error.too_large') return end
  local payload=n>0 and decompress(data) or {}
  if not istable(payload) then reply(p,request,'error',L'physics_editor.error.too_large') return end
  local ok,err=xpcall(function() return handle(p,request,op,ent,payload) end,debug.traceback)
  if not ok then
   if IsValid(ent) then ent.MMDHLPhysicsBusy=nil end if IsValid(p) then p.MMDHLPhysicsBusy=nil end
   ErrorNoHalt('[Model Hotloader physics] '..tostring(err)..'\n') reply(p,request,'error',L('physics_editor.error.build_failed',{reason=tostring(err):match('^[^\n]*')}))
  end
 end)
 return
end

-- Client: requests, the context menu entry and the console command.
CreateClientConVar('mmdhl_physics_editor_advanced','0',true,false,'Show the Numbers and Model tabs of the ragdoll physics editor',0,1)
CreateClientConVar('mmdhl_physics_editor_mirror','1',true,false,'Ragdoll physics editor: apply part edits to both sides',0,1)
CreateClientConVar('mmdhl_physics_editor_camera','1',true,false,'Ragdoll physics editor: orbit the camera around the ragdoll',0,1)
CreateClientConVar('mmdhl_physics_editor_actual','0',true,false,'Ragdoll physics editor: also draw the game\'s actual collision',0,1)
CreateClientConVar('mmdhl_physics_editor_coached','0',true,false,'Ragdoll physics editor: the first-use tip was shown',0,1)
local pending,sequence={},0
-- Sends one editor operation; onReply(state, message, entity, data) runs once (error 'no_answer' after 45 s).
function mmdhl.PhysicsRequest(op,ent,payload,onReply)
 sequence=sequence%4294967295+1 local id=sequence
 local data=util.Compress(util.TableToJSON(payload or {})) or ''
 if #data>MaxPayload then if onReply then onReply('error',L'physics_editor.error.too_large') end return end
 pending[id]=onReply or false
 net.Start('mmdhl_physics') net.WriteUInt(Protocol,8) net.WriteUInt(id,32) net.WriteString(op) net.WriteEntity(ent) net.WriteUInt(#data,16) if #data>0 then net.WriteData(data,#data) end net.SendToServer()
 if op~='close' then timer.Create('MMDHL.PhysicsRequest.'..id,45,1,function() local f=pending[id] pending[id]=nil if f then f('error',L'physics_editor.error.no_answer') end end) else pending[id]=nil end
 return id
end
net.Receive('mmdhl_physics_status',function()
 local id=net.ReadUInt(32) local state=net.ReadString() local message=net.ReadString() local index=net.ReadUInt(16) local n=net.ReadUInt(16)
 local data=n>0 and decompress(net.ReadData(n)) or nil
 local f=pending[id] if f==nil then return end
 -- "building" is progress: the same request still gets its final answer.
 if state~='building' then pending[id]=nil timer.Remove('MMDHL.PhysicsRequest.'..id) end
 if f then f(state,message~='' and mmdhl.Localize(message) or '',index>0 and Entity(index) or NULL,data,index) end
end)
function P.Level() return GetGlobal2Int('MMDHLPhysicsEditor',0) end
function P.ClientPreview() return mmdhl.native~=nil and isfunction(mmdhl.native.PreviewCarrierFit) end
-- An older native module on this computer: say so once, through the update reminder when it exists.
function P.NoteOldBinary()
 if P.oldBinaryNoted then return end P.oldBinaryNoted=true
 if mmdhl.ShowNativeUpdateNeeded then mmdhl.ShowNativeUpdateNeeded(L'physics_editor.feature','2.3.0')
 else notification.AddLegacy(L'physics_editor.banner.client_approximate',NOTIFY_HINT,8) end
end
-- The context menu shows its label through the game's own phrases.
local function phrases() if language and language.Add then language.Add('mmdhl.physics_editor.menu',L'physics_editor.menu') end end
phrases() hook.Add('MMDHL.LanguageChanged','MMDHL.PhysicsEditorPhrases',phrases) hook.Add('InitPostEntity','MMDHL.PhysicsEditorPhrases',phrases)
local function editable(ent) return IsValid(ent) and ent:GetClass()=='prop_ragdoll' and mmdhl.IsMMD(ent) and ent:GetNW2Int('MMDHLNativeBodyCount',0)==18 end
P.Editable=editable
properties.Add('mmdhl_physics_editor',{
 MenuLabel='#mmdhl.physics_editor.menu',Order=606,MenuIcon='icon16/shape_handles.png',
 Filter=function(_,ent,ply) return editable(ent) end,
 Action=function(_,ent) if mmdhl.OpenPhysicsEditor then mmdhl.OpenPhysicsEditor(ent) end end
})
-- Not "mmdhl_physics_editor": that name is the server's replicated setting, and a command cannot share it.
concommand.Add('mmdhl_physics_editor_open',function()
 local ent=LocalPlayer():GetEyeTrace().Entity
 if editable(ent) and mmdhl.OpenPhysicsEditor then mmdhl.OpenPhysicsEditor(ent) else notification.AddLegacy(L'physics_editor.error.not_ragdoll',NOTIFY_ERROR,5) end
end,nil,'Open the ragdoll physics editor for the Model Hotloader ragdoll you are looking at.')
-- Test copies say so above their head. The hook runs for every view rendered (reflections,
-- cameras): they are looked up once a frame among the addon's own characters.
local testCopies,testCopiesFrame={},nil
local function findTestCopies()
 local frame=FrameNumber() if frame==testCopiesFrame then return testCopies end
 testCopiesFrame=frame for i=#testCopies,1,-1 do testCopies[i]=nil end
 for _,ent in ipairs(mmdhl.Entities and mmdhl.Entities() or {}) do
  if IsValid(ent) and ent:GetClass()=='prop_ragdoll' and ent:GetNW2Bool('MMDHLPhysicsTestCopy',false) then testCopies[#testCopies+1]=ent end
 end
 return testCopies
end
hook.Add('PostDrawTranslucentRenderables','MMDHL.PhysicsTestCopy',function(depth,sky)
 if depth or sky then return end
 for _,ent in ipairs(findTestCopies()) do
  local rig=IsValid(ent) and mmdhl.GetRig(ent) local head=rig and rig.bodies and rig.bodies[4] local matrix=head and ent:GetBoneMatrix(head.bone)
  if matrix then local m=(tonumber(rig.scale) or 3.23656)/3.23656 local ang=EyeAngles() ang:RotateAroundAxis(ang:Up(),-90) ang:RotateAroundAxis(ang:Forward(),90)
   cam.Start3D2D(matrix:GetTranslation()+Vector(0,0,12*m),ang,.1*m) draw.SimpleTextOutlined(L'physics_editor.test_copy_label','DermaLarge',0,0,Color(255,215,0),TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER,2,Color(0,0,0)) cam.End3D2D() end
 end
end)
include('mmdhl/physics_editor_ui.lua')
