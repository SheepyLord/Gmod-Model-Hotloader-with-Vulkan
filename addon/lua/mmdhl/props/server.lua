-- Server side of static props: approval, physics, placement, resizing,
-- reimport replacement and duplication. The server owns every prop's VPhysics.
local P=mmdhl.props
local L=mmdhl.L
local native=P.native
local approved=mmdhl.approved
approved.props=approved.props or {}
for name,default in pairs({mmdhl_prop_max_materials=128,mmdhl_prop_max_package_mb=256,mmdhl_prop_max_texture_dimension=4096,mmdhl_prop_max_expanded_mb=1024}) do
 CreateConVar(name,tostring(default),FCVAR_ARCHIVE,'Largest static prop this server accepts from uploads (native hard caps still apply)',1,default)
end
-- Approval changes also drop the prepared download manifests (sharing.lua).
local function save() file.CreateDir('mmd_hotloader') file.Write('mmd_hotloader/approved.json',util.TableToJSON(approved,true)) if mmdhl.InvalidateSharedManifests then mmdhl.InvalidateSharedManifests() end end
local function notice(p,text) if IsValid(p) then net.Start('mmdhl_notice') net.WriteString(tostring(text)) net.Send(p) end end
P.Notice=notice
-- Sandbox translates 'Undone <phrase>' on each client, so only the prop's own name
-- goes into custom undo text; unnamed props keep the translated undo name.
local function customUndoText(info) if info and info.name then undo.SetCustomUndoText('Undone '..tostring(info.name)) end end
function P.CanUse(p,id) return game.SinglePlayer() or approved.props[id]~=nil end
function P.InLimits(info)
 return info.texture_dimension<=GetConVar('mmdhl_prop_max_texture_dimension'):GetInt() and info.expanded_bytes<=GetConVar('mmdhl_prop_max_expanded_mb'):GetInt()*1048576
  and #info.materials<=GetConVar('mmdhl_prop_max_materials'):GetInt() and info.bytes<=GetConVar('mmdhl_prop_max_package_mb'):GetInt()*1048576
end
function P.Catalog(p,only)
 local list={} if only then list[1]=only else for id in pairs(approved.props) do list[#list+1]=id end table.sort(list) end
 for first=1,#list,32 do
  net.Start('mmdhl_prop_catalog') net.WriteUInt(math.min(32,#list-first+1),6)
  for i=first,math.min(first+31,#list) do local id=list[i] local entry=approved.props[id]
   net.WriteString(id) net.WriteString(tostring(entry.name or L'props.imported_prop'):sub(1,256)) net.WriteUInt(entry.triangles or 0,32) net.WriteUInt(entry.bytes or 0,32)
  end
  if IsValid(p) then net.Send(p) else net.Broadcast() end
 end
end
-- Called by the shared transfer once an administrator's upload is verified.
function P.AcceptUpload(p,id,done)
 P.Load(id,function(info,err)
  if not info then done(false,err or L'props.error.invalid_prop') return end
  if not P.InLimits(info) then done(false,L'props.error.over_limits') return end
  if not game.SinglePlayer() and (not IsValid(p) or not p:IsAdmin()) then done(false,L'props.error.admin_share') return end
  approved.props[id]={name=info.name and tostring(info.name) or nil,triangles=info.triangles,bytes=info.bytes,approvedBy=IsValid(p) and p:SteamID64() or 'console',approvedAt=os.time()}
  save() P.Catalog(nil,id) done(true)
 end)
end
function P.ForgetApproved(ids)
 for _,id in ipairs(ids) do approved.props[id]=nil end
 save() if mmdhl.SendCatalogWithdrawal then mmdhl.SendCatalogWithdrawal(true,ids) end
end
-- Props of a mounted Workshop package, within this server's size limits.
function P.ApproveWorkshop(id,name,package)
 local entry=approved.props[id]
 if entry and (entry.approvedBy~='workshop' or entry.package==package) then return end
 P.Load(id,function(info)
  if not info or not P.InLimits(info) then return end
  approved.props[id]={name=name~='' and name or (info.name and tostring(info.name)) or nil,triangles=info.triangles,bytes=info.bytes,approvedBy='workshop',package=package,approvedAt=os.time()}
  save() P.Catalog(nil,id)
 end)
end
-- Clients without the bundle still trace against its hulls while downloading.
function P.SendCollision(p,id)
 local blob=native.PropCollisionBlob(id) local packed=blob and util.Compress(blob)
 if not packed or #packed>60000 then return end
 net.Start('mmdhl_prop_collision') net.WriteString(id) net.WriteUInt(#packed,16) net.WriteData(packed,#packed) net.Send(p)
end
function P.DefaultMass(phys)
 -- About 250 kg/m^3 of the convex volume: light enough for hollow objects that
 -- a single hull fills, heavy enough for large solid props.
 local volume=phys:GetVolume() or 0
 return math.Clamp(volume*.000016387*250,2,50000)
end
function P.ApplyPhysics(ent,id,scale,wait)
 local info=wait and P.LoadNow(id) or P.Info[id]
 if not info then return false,L'props.error.still_loading' end
 scale=scale or P.ScaleOf(ent)
 local allowed,why=P.CheckScale(scale,info) if not allowed then return false,why end
 local hulls,err=native.PropHulls(id,scale,wait==true)
 if not hulls then return false,err end
 ent:SetModel(P.Placeholder) ent:SetSolid(SOLID_VPHYSICS)
 if not ent:PhysicsInitMultiConvex(hulls) then return false,L'props.error.collider_rejected' end
 ent:SetMoveType(MOVETYPE_VPHYSICS) ent:EnableCustomCollisions(true)
 ent:SetCollisionBounds(P.Vector(info.mins)*scale,P.Vector(info.maxs)*scale)
 local phys=ent:GetPhysicsObject()
 if not IsValid(phys) then return false,L'props.error.no_body_created' end
 ent:SetAssetID(id) ent:SetPropScale(scale)
 -- A new physics body starts with default collision; restore the prop's mode.
 P.ApplyCollision(ent)
 return true
end
local supportDistance=P.SupportDistance
function P.CreateProp(p,id,pos,ang,scale,options)
 options=options or {}
 local info=P.Info[id] or (options.wait and P.LoadNow(id))
 if not info then return nil,L'props.error.not_loaded' end
 if IsValid(p) and gamemode.Call('PlayerSpawnProp',p,P.Placeholder)==false then return nil,L'props.error.spawn_blocked' end
 local ent=ents.Create('mmdhl_prop') if not IsValid(ent) then return nil,L'props.error.create_failed' end
 ent:SetPos(pos) ent:SetAngles(ang or angle_zero) ent:Spawn() ent:Activate()
 local ok,err=P.ApplyPhysics(ent,id,scale,options.wait) if not ok then ent:Remove() return nil,err end
 local phys=ent:GetPhysicsObject()
 phys:SetMass(math.Clamp(tonumber(options.mass) or P.DefaultMass(phys),1,50000))
 if options.frozen then phys:EnableMotion(false) else phys:Wake() end
 if IsValid(p) then
  ent:SetCreator(p)
  gamemode.Call('PlayerSpawnedProp',p,P.Placeholder,ent)
  p:AddCleanup('props',ent)
 end
 return ent
end
local function owns(p,ent)
 local owner=ent.CPPIGetOwner and ent:CPPIGetOwner() or ent:GetCreator()
 if IsValid(owner) and owner~=p and not p:IsAdmin() then return false end
 return gamemode.Call('CanProperty',p,'mmdhl_prop',ent)~=false
end
P.Owns=owns
local function reply(p,request,state,message,created)
 if not IsValid(p) then return end
 net.Start('mmdhl_prop_status') net.WriteUInt(request,32) net.WriteString(state) net.WriteString(tostring(message or '')) net.WriteUInt(IsValid(created) and created:EntIndex() or 0,16) net.Send(p)
end
-- Placement shared by the library's Spawn Prop and the Static Prop tool.
-- settings: scale, yaw (extra turn), frozen, collide (a P.CollisionModes id),
-- gravity, physprop, color.
P.PhysicsMaterials={default=true,wood=true,metal=true,metal_bouncy=true,concrete=true,glass=true,plastic=true,rubber=true,flesh=true,ice=true,paper=true,dirt=true,gravel=true,foliage=true,cardboard=true,porcelain=true,carpet=true,gmod_ice=true,gmod_bouncy=true,gmod_silent=true}
function P.PlaceAt(p,id,tr,settings,callback)
 local scale=P.CanonicalScale(tonumber(settings.scale) or 1)
 if not tr.Hit or tr.HitSky or tr.StartSolid or tr.HitPos:DistToSqr(p:EyePos())>4096^2 then callback(nil,L'props.error.aim_surface') return end
 local generation=mmdhl.cleanupGeneration
 P.Load(id,function(info,err)
  if not IsValid(p) then return end
  if mmdhl.cleanupGeneration~=generation then callback(nil,L'server.error.map_cleanup') return end
  if not info then callback(nil,err or L'props.error.load_failed') return end
  local allowed,why=P.CheckScale(scale,info) if not allowed then callback(nil,why) return end
  local pos,ang=P.SpawnPose(p,info,scale,tr,settings.yaw)
  local ent,e=P.CreateProp(p,id,pos,ang,scale,{frozen=settings.frozen==true})
  if not IsValid(ent) then callback(nil,e) return end
  local phys=ent:GetPhysicsObject()
  if isstring(settings.physprop) and P.PhysicsMaterials[settings.physprop] and IsValid(phys) then phys:SetMaterial(settings.physprop) end
  P.SetCollision(ent,P.CollisionModeIds[settings.collide] and settings.collide or P.DefaultCollision,settings.gravity~=false)
  if istable(settings.color) then ent:SetColor(Color(math.Clamp(tonumber(settings.color[1]) or 255,0,255),math.Clamp(tonumber(settings.color[2]) or 255,0,255),math.Clamp(tonumber(settings.color[3]) or 255,0,255))) end
  undo.Create('mmdhl.undo.static_prop') undo.AddEntity(ent) undo.SetPlayer(p) customUndoText(info) undo.Finish()
  callback(ent,P.Name(info))
 end)
end
local function spawnAtAim(p,id,settings,request)
 p.MMDHLPropPending=true reply(p,request,'loading',L'props.spawn.loading')
 P.PlaceAt(p,id,p:GetEyeTrace(),settings,function(ent,result)
  if IsValid(p) then p.MMDHLPropPending=nil end
  if IsValid(ent) then reply(p,request,'ready',L('props.spawn.placed',{name=result}),ent) else reply(p,request,'error',result) end
 end)
end
-- Attachments: a prop follows a bone of a character, NPC, ragdoll or prop.
-- It is parented (engine bone following, smooth on clients), non-solid to
-- players and physics, still hit by traces so the tool can edit it again.
function P.Attach(ent,target,bone,pos,ang)
 ent:PhysicsDestroy() ent:SetMoveType(MOVETYPE_NONE) ent:SetSolid(SOLID_OBB) ent:SetCollisionGroup(COLLISION_GROUP_DEBRIS)
 local info=P.Info[ent:GetAssetID()] local scale=P.ScaleOf(ent)
 if info then ent:SetCollisionBounds(P.Vector(info.mins)*scale,P.Vector(info.maxs)*scale) end
 if bone>=0 then ent:FollowBone(target,bone) else ent:SetParent(target) end
 ent:SetLocalPos(pos) ent:SetLocalAngles(ang)
 ent.MMDHLAttach={bone=bone,pos=pos,ang=ang}
 ent:SetNW2Bool('MMDHLAttached',true) ent:SetNW2Int('MMDHLAttachBone',bone) ent:SetNW2Vector('MMDHLAttachPos',pos) ent:SetNW2Angle('MMDHLAttachAng',ang)
 -- Duplicator constraint: copying the target brings the prop along.
 if IsValid(ent.MMDHLAttachLink) then ent.MMDHLAttachLink:Remove() end
 local link=ents.Create('mmdhl_attach_link')
 if IsValid(link) then
  link:Spawn()
  link:SetTable({Type='MMDHLAttach',Ent1=target,Ent2=ent,Bone=bone,LPos=pos,LAng=ang})
  constraint.AddConstraintTable(target,link,ent)
  ent:DeleteOnRemove(link) target:DeleteOnRemove(link)
  ent.MMDHLAttachLink=link
 end
end
duplicator.RegisterConstraint('MMDHLAttach',function(target,ent,bone,pos,ang)
 if not IsValid(target) or not IsValid(ent) or ent:GetClass()~='mmdhl_prop' then return end
 bone=tonumber(bone) or -1
 if bone>=math.max(target:GetBoneCount(),0) then return end
 P.Attach(ent,target,bone,isvector(pos) and pos or Vector(),isangle(ang) and ang or Angle())
 return ent.MMDHLAttachLink
end,'Ent1','Ent2','Bone','LPos','LAng')
function P.Detach(ent)
 local pos,ang=ent:GetPos(),ent:GetAngles()
 ent:SetParent(NULL) ent:RemoveEffects(EF_FOLLOWBONE) ent:SetPos(pos) ent:SetAngles(ang)
 ent.MMDHLAttach=nil ent:SetNW2Bool('MMDHLAttached',false) ent:SetCollisionGroup(COLLISION_GROUP_NONE)
 if ent:GetNW2String('MMDHLCollide','')=='' then ent:SetNW2String('MMDHLCollide',P.DefaultCollision) end
 if IsValid(ent.MMDHLAttachLink) then ent.MMDHLAttachLink:Remove() end ent.MMDHLAttachLink=nil
 local ok,err=P.ApplyPhysics(ent,ent:GetAssetID(),P.ScaleOf(ent),true)
 if not ok then return false,err end
 local phys=ent:GetPhysicsObject() phys:SetMass(P.DefaultMass(phys)) phys:EnableMotion(false)
 return true
end
local function mayUseTarget(p,target)
 if not IsValid(target) or target:IsWorld() then return false,L'props.attach.aim_target' end
 if target:IsPlayer() and target~=p and not p:IsAdmin() then return false,L'props.attach.admin_players' end
 local tr={Entity=target,Hit=true,HitPos=target:WorldSpaceCenter(),HitNormal=Vector(0,0,1),HitNonWorld=true}
 local weapon=p:GetActiveWeapon() local tool=IsValid(weapon) and weapon:GetClass()=='gmod_tool' and weapon:GetToolObject() or nil
 if not target:IsPlayer() and hook.Run('CanTool',p,tr,'mmdhl_prop',tool,2)==false then return false,L'props.attach.not_permitted' end
 return true
end
util.AddNetworkString('mmdhl_prop_attach') util.AddNetworkString('mmdhl_prop_attach_open')
net.Receive('mmdhl_prop_attach',function(_,p)
 local action=net.ReadString() local target=net.ReadEntity() local existing=net.ReadEntity() local id=net.ReadString()
 local bone=net.ReadInt(16) local pos=net.ReadVector() local ang=net.ReadAngle() local scale=P.CanonicalScale(net.ReadFloat())
 if (p.MMDHLNextAttach or 0)>CurTime() then return end p.MMDHLNextAttach=CurTime()+.2
 if IsValid(existing) then
  if existing:GetClass()~='mmdhl_prop' or not owns(p,existing) then notice(p,L'props.error.not_owner') return end
  if action=='detach' then local ok,err=P.Detach(existing) notice(p,ok and L'props.detached' or err) return end
 end
 if action~='attach' then return end
 local allowed,why=mayUseTarget(p,target) if not allowed then notice(p,why) return end
 if bone<-1 or bone>=math.max(target:GetBoneCount(),0) then notice(p,L'props.attach.bone_missing') return end
 if pos:Length()>4096 then notice(p,L'props.attach.offset_too_far') return end
 local assetId=IsValid(existing) and existing:GetAssetID() or id
 if not P.ValidID(assetId) or not P.CanUse(p,assetId) then notice(p,L'props.attach.select_shared') return end
 local generation=mmdhl.cleanupGeneration
 P.Load(assetId,function(info,err)
  if not IsValid(p) or not IsValid(target) then return end
  if mmdhl.cleanupGeneration~=generation then notice(p,L'server.error.map_cleanup') return end
  if not info then notice(p,err) return end
  local allowed2,reason=P.CheckScale(scale,info) if not allowed2 then notice(p,reason) return end
  local ent=existing
  if not IsValid(ent) then
   local created,e=P.CreateProp(p,assetId,target:GetPos(),target:GetAngles(),scale,{frozen=true})
   if not IsValid(created) then notice(p,e) return end
   ent=created
   undo.Create('mmdhl.undo.attached_prop') undo.AddEntity(ent) undo.SetPlayer(p) customUndoText(info) undo.Finish()
  elseif math.abs(P.ScaleOf(ent)-scale)>.000001 then ent:SetPropScale(scale) end
  P.Attach(ent,target,bone,pos,ang)
  notice(p,bone>=0 and L('props.attach.attached_to_bone',{name=P.Name(info),bone=tostring(target:GetBoneName(bone) or L('props.attach.bone_number',{number=bone}))}) or L('props.attach.attached',{name=P.Name(info)}))
 end)
end)
function P.OpenAttach(p,target,existing)
 net.Start('mmdhl_prop_attach_open') net.WriteEntity(target or NULL) net.WriteEntity(existing or NULL) net.Send(p)
end
-- Resizing and reimport keep the entity, its transform, motion and Weld /
-- NoCollide constraints. Other constraint types are rejected up front.
local supported={Weld=true,NoCollide=true}
local function constraintSnapshot(ent)
 local result={}
 for _,row in pairs(constraint.GetTable(ent)) do
  if not supported[row.Type] then return nil,L('props.error.unsupported_constraint',{type=tostring(row.Type)}) end
  result[#result+1]=row
 end
 return result
end
local function restoreConstraints(rows)
 for _,r in ipairs(rows) do
  local c
  if r.Type=='Weld' then c=constraint.Weld(r.Ent1,r.Ent2,r.Bone1 or 0,r.Bone2 or 0,r.forcelimit or 0,r.nocollide or false,r.deleteonbreak or false)
  else c=constraint.NoCollide(r.Ent1,r.Ent2,r.Bone1 or 0,r.Bone2 or 0,r.disableOnRemove) end
  if not IsValid(c) then return false end
  if IsValid(r.Constraint) then
   c:SetCreator(r.Constraint:GetCreator())
   if undo.ReplaceEntity then undo.ReplaceEntity(r.Constraint,c) end
   if cleanup.ReplaceEntity then cleanup.ReplaceEntity(r.Constraint,c) end
  end
 end
 return true
end
local function detachConstraints(ent)
 -- Entity:Remove is deferred to the end of the tick. Drop the old entries from
 -- both participants now so the replacement is not rejected as a duplicate.
 local rows=constraint.GetTable(ent)
 constraint.RemoveAll(ent)
 for _,row in ipairs(rows) do
  for _,participant in ipairs({row.Ent1,row.Ent2}) do
   if IsValid(participant) and participant.Constraints then table.RemoveByValue(participant.Constraints,row.Constraint) end
  end
 end
end
function P.Rebuild(ent,id,scale)
 local permitted,problem=P.CheckScale(scale,P.Info[id]) if not permitted then return false,problem end
 local rows,err=constraintSnapshot(ent) if not rows then return false,err end
 local old,oldScale=ent:GetAssetID(),P.ScaleOf(ent) local phys=ent:GetPhysicsObject()
 if not IsValid(phys) then return false,L'props.error.no_body' end
 -- Validate the replacement body before touching the existing entity.
 local probe=ents.Create('mmdhl_prop') probe:SetNoDraw(true) probe:SetPos(Vector(0,0,-16000)) probe:Spawn()
 local valid,why=P.ApplyPhysics(probe,id,scale) probe:Remove()
 if not valid then return false,why end
 local state={pos=ent:GetPos(),angles=ent:GetAngles(),mass=phys:GetMass(),motion=phys:IsMotionEnabled(),gravity=phys:IsGravityEnabled(),velocity=phys:GetVelocity(),angular=phys:GetAngleVelocity(),material=phys:GetMaterial()}
 local function restorePhysics()
  ent:SetPos(state.pos) ent:SetAngles(state.angles)
  local p=ent:GetPhysicsObject() if not IsValid(p) then return end
  p:SetMass(state.mass) p:EnableGravity(state.gravity) p:SetMaterial(state.material)
  p:SetVelocity(state.velocity) p:AddAngleVelocity(state.angular-p:GetAngleVelocity())
  p:EnableMotion(state.motion) p:Wake()
 end
 detachConstraints(ent)
 local ok,reason=P.ApplyPhysics(ent,id,scale)
 if ok then restorePhysics() ok=restoreConstraints(rows) end
 if not ok then
  detachConstraints(ent) P.ApplyPhysics(ent,old,oldScale) restorePhysics() restoreConstraints(rows)
  return false,reason or L'props.error.constraints_restored'
 end
 return true
end
net.Receive('mmdhl_prop_action',function(_,p)
 local action=net.ReadString() local id=net.ReadString() local ent=Entity(net.ReadUInt(16)) local settings=util.JSONToTable(net.ReadString()) or {}
 local request=math.Clamp(math.floor(tonumber(settings.request) or 0),0,4294967295)
 if action=='spawn' then
  if not P.ValidID(id) then reply(p,request,'error',L'props.error.select_prop') return end
  if not P.CanUse(p,id) then reply(p,request,'error',L'props.error.not_shared_admin') return end
  if p.MMDHLPropPending then reply(p,request,'error',L'props.spawn.busy_wait') return end
  spawnAtAim(p,id,settings,request) return
 end
 if action=='remove_asset' then
  -- Library deletion: remove this player's props of the deleted bundle.
  if not P.ValidID(id) then return end
  for _,prop in ipairs(ents.FindByClass('mmdhl_prop')) do if prop:GetAssetID()==id and owns(p,prop) then prop:Remove() end end
  return
 end
 if action=='replace_asset' then
  -- Reimport in single-player: move props of the old bundle onto the new one.
  local target=tostring(settings.target or '')
  if not game.SinglePlayer() or not P.ValidID(id) or not P.ValidID(target) then return end
  P.Load(target,function(info,err)
   if not info then notice(p,err) return end
   local failed=0
   for _,prop in ipairs(ents.FindByClass('mmdhl_prop')) do if prop:GetAssetID()==id then local ok=P.Rebuild(prop,target,P.ScaleOf(prop)) if not ok then failed=failed+1 end end end
   if failed>0 then notice(p,L('props.reimport_kept',{count=failed})) end
  end)
  return
 end
 if not IsValid(ent) or ent:GetClass()~='mmdhl_prop' then notice(p,L'props.error.aim_at_prop') return end
 if not owns(p,ent) then notice(p,L'props.error.not_owner') return end
 if action=='resize' then
  if (p.MMDHLNextResize or 0)>CurTime() then return end p.MMDHLNextResize=CurTime()+.25
  local scale=P.CanonicalScale(tonumber(settings.scale) or 1)
  local permitted,why=P.CheckScale(scale,P.Info[ent:GetAssetID()]) if not permitted then notice(p,why) return end
  if math.abs(P.ScaleOf(ent)-scale)<.000001 then return end
  P.Load(ent:GetAssetID(),function(info,err)
   if not IsValid(ent) then return end
   if not info then notice(p,err) return end
   if ent.MMDHLAttach then ent:SetPropScale(scale) ent:SetCollisionBounds(P.Vector(info.mins)*scale,P.Vector(info.maxs)*scale) notice(p,L('props.resize.attached',{scale=string.format('%.2f',scale)})) return end
   local ok,reason=P.Rebuild(ent,ent:GetAssetID(),scale)
   notice(p,ok and L('props.resize.done',{scale=string.format('%.2f',scale)}) or reason)
  end)
 elseif action=='freeze' then
  local phys=ent:GetPhysicsObject() if IsValid(phys) then phys:EnableMotion(not phys:IsMotionEnabled()) phys:Wake() end
 elseif action=='remove' then ent:Remove() end
end)
duplicator.RegisterEntityClass('mmdhl_prop',function(p,data)
 local entry=data.MMDHLProp
 if not istable(entry) or not P.ValidID(entry.asset) then return end
 if not P.CanUse(p,entry.asset) or not native.PropHas(entry.asset) then notice(p,L'props.error.dupe_unavailable') return end
 local ent,err=P.CreateProp(p,entry.asset,data.Pos,data.Angle,tonumber(entry.scale) or 1,{wait=true,mass=entry.mass,frozen=entry.frozen})
 if not IsValid(ent) then notice(p,err) return end
 duplicator.DoGeneric(ent,data)
 -- Copies made before collision modes collided with everything.
 P.SetCollision(ent,P.CollisionModeIds[entry.collide] and entry.collide or 'all',entry.gravity~=false)
 return ent
end,'Data')
-- Release decoded bundles no prop has used for a minute.
timer.Create('MMDHL.PropReleaseUnused',15,0,function()
 local live={}
 for _,ent in ipairs(ents.FindByClass('mmdhl_prop')) do live[ent:GetAssetID()]=true end
 for id in pairs(P.Info) do
  if not live[id] and (P.Pins[id] or 0)==0 and not P.Loading[id] and RealTime()-(P.LastUse[id] or 0)>60 then P.Forget(id) P.LastUse[id]=nil end
 end
end)
net.Receive('mmdhl_prop_forget',function(_,p)
 -- Single-player deletion shares one cache between realms.
 if not game.SinglePlayer() then return end
 local ids,valid=util.JSONToTable(net.ReadString()),{} if not istable(ids) then return end
 for _,id in ipairs(ids) do if P.ValidID(id) then P.Forget(id) valid[#valid+1]=id end end
 P.ForgetApproved(valid)
end)
