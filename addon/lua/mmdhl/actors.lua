-- Native actor classes keep Source AI, activities, damage and player prediction.
local native=mmdhl.native
local L=mmdhl.L
local capabilities=mmdhl.Decode(native.GetCapabilities()) or {}
-- A carrier this module loads: its rig version and a generator from rigGeneratorMin to
-- rigGenerator (2.3.0: 30, the fits of 2.2, to 31). Saves, dupes, published NPCs and
-- player models keep such a carrier as it was; new spawns and fits get rigGenerator.
-- A module without rigGeneratorMin (2.2.0 and older) loads its own generator only.
function mmdhl.IsLoadableRig(rig)
 local newest=capabilities.rigGenerator
 if not istable(rig) or rig.version~=capabilities.rigVersion or not isnumber(rig.generator) or not isnumber(newest) then return false end
 local oldest=isnumber(capabilities.rigGeneratorMin) and capabilities.rigGeneratorMin or newest
 return rig.generator>=oldest and rig.generator<=newest
end
-- A carrier of the fit this module makes now.
function mmdhl.IsCurrentRig(rig)
 return rig and rig.version==capabilities.rigVersion and rig.generator==capabilities.rigGenerator
end
mmdhl.actorProfiles={
 citizen={female='models/Humans/Group01/Female_01.mdl',male='models/Humans/Group01/Male_01.mdl'},
 combine={female='models/combine_soldier.mdl',male='models/combine_soldier.mdl'},
 player={female='models/player/alyx.mdl',male='models/player/kleiner.mdl'},
 arms={female='models/weapons/c_arms_citizen.mdl',male='models/weapons/c_arms_citizen.mdl'}
}
-- A character's mesh bind is not necessarily the reference used by its
-- included animation streams. In particular, Citizen mesh spines and the
-- Alyx mesh differ substantially from female_shared / f_anm.
mmdhl.actorAnimationReferences={
 citizen={female='models/humans/female_shared.mdl',male='models/humans/male_shared.mdl'},
 combine={female='models/combine_soldier_anims.mdl',male='models/combine_soldier_anims.mdl'},
 player={female='models/f_anm.mdl',male='models/m_anm.mdl'}
}
mmdhl.actorRegistrations=mmdhl.actorRegistrations or {}
function mmdhl.UnregisterAsset(id)
 for model,entry in pairs(mmdhl.actorRegistrations) do if entry.rig.asset==id then
  local key='mmd_'..entry.rig.key:sub(1,16)
  mmdhl.actorRegistrations[model]=nil
  list.GetForEdit('NPC')[key]=nil list.GetForEdit('PlayerOptionsModel')[key]=nil
  player_manager.RemoveValidModel(key)
 end end
end
-- Use the same userinfo selection and weapon whitelist as Sandbox's NPC menu.
-- An empty selection means the profile default; "none" explicitly means unarmed.
function mmdhl.NPCWeapon(p,role,requested)
 local weapon=requested
 if weapon==nil and IsValid(p) then weapon=p:GetInfo('gmod_npcweapon') end
 if not isstring(weapon) or weapon=='' then weapon=role=='combine' and 'weapon_ar2' or 'weapon_smg1' end
 if weapon=='none' then return 'none' end
 for _,entry in pairs(list.Get('NPCUsableWeapons') or {}) do if entry.class==weapon then return weapon end end
 local defaults=role=='combine' and {'weapon_smg1','weapon_ar2'} or {'weapon_pistol','weapon_smg1'}
 for _,entry in ipairs(defaults) do if entry==weapon then return weapon end end
 return 'none'
end
-- A new NPC's health and maximum health: the spawn's own value, else the
-- player's NPC health setting. nil (a setting of 0) keeps the health the game
-- gives its class: Citizens 40, Combine Soldiers 50 (skill.cfg).
mmdhl.MaxNPCHealth=10000
function mmdhl.NPCHealth(p,requested)
 local health=tonumber(requested)
 if health==nil then health=IsValid(p) and p:GetInfoNum('mmdhl_npc_health',0) or 0 end
 if health~=health or health<1 then return nil end
 return math.min(math.floor(health),mmdhl.MaxNPCHealth)
end
-- A hostile NPC is a Combine Soldier whatever it carries. Like the game's own,
-- it holds pistols, RPGs and melee weapons in the unarmed pose: the Combine
-- Soldier animation pack animates only rifle-type weapons and the unarmed stance.
function mmdhl.ActorClass(role)
 return role=='combine' and 'npc_combine_s' or 'npc_citizen'
end
function mmdhl.RegisterActor(rig,arms)
 if not mmdhl.IsLoadableRig(rig) or not rig.model then return end
 local role=rig.role local key='mmd_'..rig.key:sub(1,16)
 local name=CLIENT and mmdhl.names and (mmdhl.names.Display(rig.name,rig.asset)) or rig.name
 mmdhl.actorRegistrations[rig.model]={rig=rig,arms=arms}
 if role=='player' then
  player_manager.AddValidModel(key,rig.model,rig.name,mmdhl.Localize(L'common.external_models'))
  if arms then player_manager.AddValidHands(key,arms.model,0,'0000000') end
  list.Set('PlayerOptionsModel',key,rig.model)
 elseif role=='citizen' or role=='combine' then
  list.Set('NPC',key,{Name=mmdhl.Localize(role=='citizen' and L('actors.npc_citizen',{name=name}) or L('actors.npc_combine',{name=name})),Class=role=='citizen' and 'npc_citizen' or 'npc_combine_s',Category=mmdhl.Localize(L'common.external_models'),Model=rig.model,Weapons=role=='citizen' and {'weapon_pistol','weapon_smg1'} or {'weapon_smg1','weapon_ar2'},KeyValues=role=='citizen' and {citizentype=4} or {},MMDHLAsset=rig.asset,MMDHLRig=rig.key})
 end
end
if SERVER then
 local function friendlyCitizen(ent)
  -- Other addons commonly assign faction relationships on the creation tick.
  -- Apply the explicitly selected friendly role after that initialization.
  timer.Simple(.2,function()
   if not IsValid(ent) then return end
   for _,p in ipairs(player.GetAll()) do ent:AddEntityRelationship(p,D_LI,99) end
  end)
 end
 -- Hostile citizens come from saves and dupes of earlier versions, which made one
 -- for a hostile NPC holding a pistol, RPG or melee weapon. They keep Citizen AI but
 -- take the Combine soldier's side: players and their allies are enemies,
 -- Combine forces and other hostile citizens are friends. Others keep their class.
 local combineSide={[CLASS_COMBINE]=true,[CLASS_COMBINE_GUNSHIP]=true,[CLASS_COMBINE_HUNTER]=true,[CLASS_METROPOLICE]=true,[CLASS_MANHACK]=true,[CLASS_SCANNER]=true,[CLASS_STALKER]=true,[CLASS_PROTOSNIPER]=true,[CLASS_MILITARY]=true}
 local playerSide={[CLASS_PLAYER_ALLY]=true,[CLASS_PLAYER_ALLY_VITAL]=true,[CLASS_CITIZEN_PASSIVE]=true,[CLASS_CITIZEN_REBEL]=true,[CLASS_VORTIGAUNT]=true,[CLASS_HACKED_ROLLERMINE]=true}
 local function relate(hostile,other)
  if other==hostile or not IsValid(other) then return end
  if other:IsPlayer() then hostile:AddEntityRelationship(other,D_HT,99) return end
  if not other:IsNPC() then return end
  local disposition
  if other.MMDHLHostile then disposition=D_LI
  else local class=other:Classify() disposition=combineSide[class] and D_LI or playerSide[class] and D_HT end
  if not disposition then return end
  hostile:AddEntityRelationship(other,disposition,99) other:AddEntityRelationship(hostile,disposition,99)
 end
 local function hostiles() local out={} for _,ent in ipairs(ents.FindByClass('npc_citizen')) do if ent.MMDHLHostile then out[#out+1]=ent end end return out end
 function mmdhl.MakeHostileCitizen(ent)
  -- Its own squad keeps it out of the player's squad and shares enemy sightings.
  ent.MMDHLHostile=true ent:SetSquad('mmdhl_hostile')
  ent:AddRelationship('player D_HT 99')
  for _,other in ipairs(ents.GetAll()) do relate(ent,other) end
  -- Other addons commonly assign faction relationships on the creation tick.
  timer.Simple(.2,function()
   if not IsValid(ent) then return end
   for _,other in ipairs(ents.GetAll()) do relate(ent,other) end
  end)
 end
 hook.Add('OnEntityCreated','MMDHL.HostileCitizens',function(other)
  timer.Simple(.2,function()
   if not IsValid(other) or not other:IsNPC() or other.MMDHLHostile then return end
   for _,ent in ipairs(hostiles()) do relate(ent,other) end
  end)
 end)
 hook.Add('PlayerInitialSpawn','MMDHL.CitizenFriend',function(p)
  timer.Simple(0,function() if not IsValid(p) then return end for _,ent in ipairs(mmdhl.Entities()) do
   if ent:GetClass()=='npc_citizen' then ent:AddEntityRelationship(p,ent.MMDHLHostile and D_HT or D_LI,99) end
  end end)
 end)
 -- Hostile NPCs are Combine Soldiers with the Combine weapon choice. A respawn of
 -- an earlier version's hostile citizen (options.hostile) becomes one too.
 function mmdhl.HostileActorOptions(p,options)
  options.weapon=mmdhl.NPCWeapon(p,'combine',options.weapon)
  options.role='combine' options.hostile=nil
  return options
 end
 function mmdhl.ClearActorIdentity(ent)
  for i in pairs(ent.MMDHLManual or {}) do ent:SetNW2String('MMDHLManual'..(i-1),'') end
  ent.MMDHLManual=nil ent:SetNW2Int('MMDHLManualRevision',0)
  mmdhl.ClearAssetState(ent,mmdhl.GetRig(ent))
  ent.MMDHLPlayerSelection=nil ent.MMDOptions=nil
  for _,name in ipairs({'MMDHLAsset','MMDHLRig','MMDHLRole','MMDHLArms'}) do ent:SetNW2String(name,'') end
  ent:SetNW2Int('MMDHLGeneration',ent:GetNW2Int('MMDHLGeneration',0)+1)
 end
 function mmdhl.AttachRegisteredPlayer(p)
  local registration=mmdhl.actorRegistrations[p:GetModel()]
  if not registration or registration.rig.role~='player' or not registration.arms then return false end
  local r,a=registration.rig,registration.arms
  if not mmdhl.CanUseAsset(p,r.asset) or hook.Run('MMDHLCanSetPlayerModel',p,r.asset,r)==false then return false end
  -- A gamemode or a standard model selector already chose this native model.
  -- Attach visual state without changing movement, collision or model choice.
  local options={role='player',backend='source',rigManifest=r,scale=r.scale*.0254,
   gender=r.animation and r.animation.profile:find('_male$') and 'male' or 'female',armsParts=a.armsParts}
  local attached,err=mmdhl.AttachNative(p,r.asset,options)
  if not attached then return false,err end
  p.MMDHLPlayerSelection={asset=r.asset,options=options,rig=r,arms=a}
  p:SetNW2String('MMDHLRole','player') p:SetNW2String('MMDHLArms',a.key) p:SetupHands()
  return true
 end
 timer.Create('MMDHL.PlayerModelOwnership',.25,0,function()
  for _,p in ipairs(player.GetAll()) do
   local choice=p.MMDHLPlayerSelection
   if p:Alive() then
    if choice and p:GetModel()~=choice.rig.model then mmdhl.ClearActorIdentity(p) end
    if not p.MMDHLPlayerSelection then mmdhl.AttachRegisteredPlayer(p) end
   end
  end
 end)
 util.AddNetworkString('mmdhl_actor_registration')
 util.AddNetworkString('mmdhl_player_selection')
 util.AddNetworkString('mmdhl_player_clear')
 function mmdhl.RemovePlayerModel(p)
  if not IsValid(p) or not p:IsPlayer() or not mmdhl.IsMMD(p) then return false end
  local model=p.MMDHLPreviousModel or 'models/player/kleiner.mdl'
  if mmdhl.actorRegistrations[model] or not util.IsValidModel(model) then model='models/player/kleiner.mdl' end
  mmdhl.ClearActorIdentity(p)
  FindMetaTable('Entity').SetModel(p,model) p:SetupHands()
  net.Start('mmdhl_player_clear') net.WriteString(player_manager.TranslateToPlayerModelName(model)) net.Send(p)
  return true
 end
 util.AddNetworkString('mmdhl_arms_preview')
 net.Receive('mmdhl_arms_preview',function(_,p)
  local request,id,gender,raw=net.ReadUInt(16),net.ReadString(),net.ReadString(),net.ReadString()
  -- Every request is answered: the arms editor waits for its reply.
  local function reply(rig,err)
   if not IsValid(p) then return end
   if rig then mmdhl.PublishRig(rig) end
   net.Start('mmdhl_arms_preview') net.WriteUInt(request,16) net.WriteString(rig and rig.key or '') net.WriteString(err or '') net.Send(p)
  end
  if not mmdhl.CanUseAsset(p,id) then reply(nil,L'share.error.model_not_approved') return end
  if RealTime()<(p.MMDHLArmsAt or 0) then reply(nil,L'actors.error.arms_preview_wait') return end p.MMDHLArmsAt=RealTime()+1
  local clean=mmdhl.CleanArmsParts(util.JSONToTable(raw))
  mmdhl.LoadAsset(id,function(info,err)
   if not info then reply(nil,err) return end
   local options,error=mmdhl.ActorOptions({role='arms',gender=gender,armsParts=clean}) if not options then reply(nil,error) return end
   -- The same bones as the player model the preview stands for (pins from the bone window).
   options.boneMap=mmdhl.SavedBoneMap and mmdhl.SavedBoneMap(id) or nil
   local rig,e=mmdhl.Decode(native.PrepareCarrier(id,util.TableToJSON(options))) reply(rig,e)
  end)
 end)
 local references={}
 -- The model's metadata as the game loads it ('GAME', with addons' replacements)
 -- or as the game ships it ('MOD': no Workshop or mounted addons); nil and no
 -- error when that copy is missing.
 local function readReference(source,path)
  path=path or 'GAME'
  local key=path..'|'..source
  if not references[key] then
   local bytes=file.Read(source,path) if not bytes then return nil end
   local raw,err=native.ReadAnimationModel(bytes) if not raw then return nil,err end
   references[key]=util.JSONToTable(raw)
  end
  return references[key]
 end
 -- The carrier takes its IK chains from the donor and its proportions from the
 -- reference skeleton (native configureAnimations): the chains must name
 -- ValveBiped bones, and the skeleton must have 40 of them and both arm chains.
 local function usableDonor(model)
  for _,chain in ipairs(model.ikChains or {}) do for _,link in ipairs(chain.links or {}) do
   if not isstring(link.bone) or link.bone:sub(1,11)~='ValveBiped.' then return false end
  end end
  return true
 end
 local function usableSkeleton(model)
  local names,count={},0
  for _,bone in ipairs(model.bones or {}) do
   local name=bone.name
   if isstring(name) and not names[name] then names[name]=true if name:sub(1,17)=='ValveBiped.Bip01_' then count=count+1 end end
  end
  for _,side in ipairs({'L','R'}) do for _,part in ipairs({'UpperArm','Forearm','Hand'}) do
   if not names['ValveBiped.Bip01_'..side..'_'..part] then return false end
  end end
  return count>=40
 end
 -- Addons that replace an animation pack (or a donor model) may compile it on a
 -- skeleton of their own: a retargeted rig without fingers, or a pack that only
 -- includes others. Its animations still play by bone name, but the carrier's
 -- proportions and IK then come from the game's own copy of the file.
 local function readUsable(source,usable)
  local model,err=readReference(source)
  if model and usable(model) then return model end
  local own=readReference(source,'MOD')
  if own and usable(own) then return own end
  if model then return nil,L('actors.error.replaced_animation_reference',{path=source}) end
  return nil,err
 end
 function mmdhl.ActorOptions(options)
  options=table.Copy(options or {}) local role=options.role or 'ragdoll'
  if options.armsParts~=nil then options.armsParts=mmdhl.CleanArmsParts(options.armsParts) end
  -- A ragdoll takes the Citizen reference: its carrier then also includes the
  -- player and Citizen animation packs of the chosen style (native
  -- configureAnimations), so animation tools can pose it with their sequences.
  local profileRole=role=='ragdoll' and 'citizen' or role
  local profile=mmdhl.actorProfiles[profileRole] if not profile then return nil,L'actors.error.unknown_type' end
  options.gender=options.gender=='male' and 'male' or 'female'
  local source=profile[options.gender]
  local animationProfile=mmdhl.actorAnimationReferences[profileRole]
  local referenceSource=animationProfile and animationProfile[options.gender] or source
  local donor,err=readUsable(source,usableDonor)
  if not donor and not err then err=L('actors.error.missing_animation_reference',{path=source}) end
  local skeleton
  if donor then
   skeleton,err=readUsable(referenceSource,animationProfile and usableSkeleton or usableDonor)
   if not skeleton and not err then err=L('actors.error.missing_animation_skeleton',{path=referenceSource}) end
  end
  if not skeleton then
   -- The animations are an addition to a ragdoll: without them it spawns as before.
   if role=='ragdoll' then options.animationSource=nil options.animationReference=nil return options end
   return nil,err
  end
  options.animationSource=source options.animationReference=table.Copy(donor)
  options.animationReference.bones=skeleton.bones
  options.animationReference.referenceSource=referenceSource
  options.animationReference.referenceHash=skeleton.sha256
  return options
 end
 -- The registration message alone, for catalogs; PublishActor also registers and approves.
 function mmdhl.SendActorRegistration(rig,arms,target)
  net.Start('mmdhl_actor_registration') net.WriteString(rig.key) net.WriteString(arms and arms.key or '') if target then net.Send(target) else net.Broadcast() end
 end
 function mmdhl.PublishActor(rig,arms,target)
  mmdhl.RegisterActor(rig,arms)
  if mmdhl.PublishRig then mmdhl.PublishRig(rig,arms) end
  mmdhl.SendActorRegistration(rig,arms,target)
 end
 function mmdhl.SpawnNPC(p,id,kind,options,done,progress)
  options=table.Copy(options or {}) options.role=kind=='combine' and 'combine' or 'citizen' options.backend='source'
  return mmdhl.Spawn(p,id,options,done,progress)
 end
 function mmdhl.SetPlayerModel(p,id,options,done,progress)
  if not IsValid(p) or not p:IsPlayer() then if done then done(nil,L'actors.error.player_required') end return end
  options=table.Copy(options or {}) options.role='player' options.backend='source'
  return mmdhl.Spawn(p,id,options,done,progress)
 end
 -- The class sets its own health in Spawn; a chosen health replaces both values
 -- after it, as a spawn-menu entry's Health does.
 local function setHealth(ent,health) if health then ent:SetMaxHealth(health) ent:SetHealth(health) end end
 function mmdhl.SpawnActorNative(p,id,options,rig,done)
  local role=options.role local arms
  if role=='player' then
   local armOptions=table.Copy(options) armOptions.role='arms'
   local error armOptions,error=mmdhl.ActorOptions(armOptions) if not armOptions then if done then done(nil,error) end return end
   local raw,err=native.PrepareCarrier(id,util.TableToJSON(armOptions)) if not raw then if done then done(nil,err) end return end
   arms=util.JSONToTable(raw) if not mmdhl.MountPackage(arms.gma) then if done then done(nil,L'actors.error.arms_mount_failed') end return end
  end
  local ent=role=='player' and p or ents.Create(role=='combine' and 'npc_combine_s' or 'npc_citizen')
  if not IsValid(ent) then if done then done(nil,L'actors.error.create_failed') end return end
  if role=='player' and hook.Run('MMDHLCanSetPlayerModel',p,id,rig)==false then if done then done(nil,L'actors.error.player_model_disallowed') end return end
  -- This is an explicit player-model selection, like player_manager's own
  -- selector. Player:SetModel can be replaced by a selector's force-model
  -- wrapper; publish the matching userinfo below so it agrees on respawn.
  if role=='player' then
   if not mmdhl.IsMMD(ent) then ent.MMDHLPreviousModel=ent:GetModel() end
   FindMetaTable('Entity').SetModel(ent,rig.model)
  else ent:SetModel(rig.model) end
  if role~='player' then
   ent:SetPos(Vector(unpack(options.position or {0,0,0}))) ent:SetAngles(Angle(unpack(options.angles or {0,0,0})))
   if role=='citizen' then ent:SetKeyValue('citizentype','4') end
   local weapon=mmdhl.NPCWeapon(p,role,options.weapon)
   if weapon~='none' then ent:SetKeyValue('additionalequipment',weapon) ent.Equipment=weapon end
   ent:Spawn() ent:Activate()
   setHealth(ent,mmdhl.NPCHealth(p,options.npcHealth))
   if role=='citizen' then
    ent:AddRelationship('player D_LI 99')
    for _,friendly in ipairs(player.GetAll()) do ent:AddEntityRelationship(friendly,D_LI,99) end
   end
  end
  local attached,err=mmdhl.AttachNative(ent,id,options)
  if not attached then if role~='player' then ent:Remove() end if done then done(nil,err) end return end
  ent:SetNW2String('MMDHLRole',role)
  if options.bodygroups then mmdhl.ApplyBodygroupState(ent,options.bodygroups) end
  if role=='citizen' then friendlyCitizen(ent) end
  if role=='player' then
   p.MMDHLPlayerSelection={asset=id,options=table.Copy(options),rig=rig,arms=arms}
   p:SetNW2String('MMDHLArms',arms.key)
   p:SetupHands()
   local hands=p:GetHands() if IsValid(hands) then hands:SetModel(arms.model) hands:SetSkin(0) end
  elseif IsValid(p) then
   -- Sandbox's completion hook counts the NPC toward the player's limit. Our own
   -- PlayerSpawnedNPC handler is for spawn-menu entries and skips this entity.
   ent:SetCreator(p) ent.MMDHLNativeSpawn=true gamemode.Call('PlayerSpawnedNPC',p,ent) ent.MMDHLNativeSpawn=nil
   undo.Create(role=='citizen' and 'mmdhl.undo.citizen' or 'mmdhl.undo.combine') undo.AddEntity(ent) undo.SetPlayer(p) undo.Finish() p:AddCleanup('mmdhl',ent)
  end
  mmdhl.PublishActor(rig,arms)
  if role=='player' then net.Start('mmdhl_player_selection') net.WriteString(rig.key) net.Send(p) end
  hook.Run('MMDHLActorCreated',ent,rig,options)
  if done then done(ent) end return ent
 end
 hook.Add('PlayerSpawnedNPC','MMDHL.AttachMenuNPC',function(p,ent)
  if ent.MMDHLNativeSpawn then return end -- already attached by SpawnActorNative
  local registration=mmdhl.actorRegistrations[ent:GetModel()] if not registration then return end
  local r=registration.rig
  local gender=r.animation and r.animation.profile:find('_male$') and 'male' or 'female'
  mmdhl.AttachNative(ent,r.asset,{role=r.role,rigManifest=r,scale=r.scale*.0254,gender=gender})
  -- Sandbox's NPC duplicator restores a pasted NPC's own health after this hook.
  setHealth(ent,mmdhl.NPCHealth(p))
  if r.role=='citizen' then friendlyCitizen(ent) end
 end)
 hook.Add('PlayerSetHandsModel','MMDHL.PlayerHands',function(p,hands)
  local choice=p.MMDHLPlayerSelection
  if choice and p:GetModel()==choice.rig.model then hands:SetModel(choice.arms.model) hands:SetSkin(0) hands:SetBodyGroups('0000000') return true end
 end)
 hook.Add('PlayerSpawn','MMDHL.RestorePlayerChoice',function(p)
  timer.Simple(0,function()
   if not IsValid(p) then return end
   local c=p.MMDHLPlayerSelection if not c then mmdhl.AttachRegisteredPlayer(p) return end
   if p:GetInfo('cl_playermodel')~='mmd_'..c.rig.key:sub(1,16) or hook.Run('MMDHLCanSetPlayerModel',p,c.asset,c.rig)==false then mmdhl.ClearActorIdentity(p) return end
   p:SetModel(c.rig.model) mmdhl.AttachNative(p,c.asset,c.options) mmdhl.ApplyBodygroupState(p,c.options.bodygroups) p:SetNW2String('MMDHLRole','player') p:SetNW2String('MMDHLArms',c.arms.key) p:SetupHands()
  end)
 end)
 local function inherit(source,corpse)
  if not mmdhl.IsMMD(source) or not IsValid(corpse) then return end
  -- The engine has already created and posed its corpse. Do not SetModel,
  -- Spawn, alter velocities, replace the corpse or change cleanup ownership.
  corpse:SetNW2String('MMDHLAsset',mmdhl.GetAsset(source)) corpse:SetNW2String('MMDHLRig',source:GetNW2String('MMDHLRig')) corpse:SetNW2String('MMDHLRole','corpse') corpse:SetNW2Int('MMDHLGeneration',1)
  corpse.MMDOptions=table.Copy(source.MMDOptions or {}) corpse:SetColor(source:GetColor()) corpse:SetMaterial(source:GetMaterial())
  mmdhl.ApplyMaterialState(corpse,mmdhl.CaptureMaterialState(source))
  for index,pose in pairs(source.MMDHLManual or {}) do mmdhl.SetManualBonePose(corpse,index-1,pose) end
  local values={} for i in ipairs(mmdhl.GetMorphs(source)) do values[i]=mmdhl.GetMorphWeight(source,i-1) end mmdhl.SetMorphWeights(corpse,values)
  corpse:SetFlexScale(source:GetFlexScale()) corpse:SetNW2Entity('MMDHLFormerActor',source)
  corpse:SetNW2Int('MMDHLFormerGeneration',source:GetNW2Int('MMDHLGeneration',0))
 end
 hook.Add('CreateEntityRagdoll','MMDHL.NativeCorpse',inherit)
 -- EEER deliberately replaces a player's native corpse with an independent
 -- prop_ragdoll. It publishes the source after Spawn and does not re-emit
 -- CreateEntityRagdoll. Attach to that exact replacement after creation;
 -- never infer ownership from matching model names or create another corpse.
 hook.Add('OnEntityCreated','MMDHL.IndependentCorpse',function(corpse)
  if corpse:GetClass()~='prop_ragdoll' then return end
  timer.Simple(0,function()
   if not IsValid(corpse) or not corpse.RPE_IndependentPlayerCorpse then return end
   local source=corpse.RPE_SourceEnt
   if IsValid(source) and source:IsPlayer() and source:GetModel()==corpse:GetModel() and mmdhl.IsMMD(source) then
    if mmdhl.IsMMD(corpse) then
     -- The ordinary model-binding path may win this race. Still publish the
     -- exact source link, without overwriting EEER's copied death expression.
     corpse:SetNW2Entity('MMDHLFormerActor',source)
     corpse:SetNW2Int('MMDHLFormerGeneration',source:GetNW2Int('MMDHLGeneration',0))
    else inherit(source,corpse) end
   end
  end)
 end)
else
 local pendingSelection
 net.Receive('mmdhl_player_clear',function() pendingSelection=nil RunConsoleCommand('cl_playermodel',net.ReadString()) end)
 net.Receive('mmdhl_player_selection',function() pendingSelection=net.ReadString() end)
 hook.Add('Think','MMDHL.SelectRegisteredPlayer',function()
  if not pendingSelection then return end
  local key='mmd_'..pendingSelection:sub(1,16)
  if player_manager.AllValidModels()[key] then
   RunConsoleCommand('cl_playermodel',key) pendingSelection=nil
   -- Enhanced PlayerModel Selector owns its own hands/bodygroup refresh.
   if util.NetworkStringToID('lf_playermodel_update')~=0 then timer.Simple(.2,function() net.Start('lf_playermodel_update') net.SendToServer() end) end
  end
 end)
 net.Receive('mmdhl_actor_registration',function()
  local key,armsKey=net.ReadString(),net.ReadString()
  local function ready()
   local rig=util.JSONToTable(file.Read('mmd_hotloader/rigs/'..key..'/rig.json','DATA') or '')
   local arms=armsKey~='' and util.JSONToTable(file.Read('mmd_hotloader/rigs/'..armsKey..'/rig.json','DATA') or '') or nil
   if not rig or (armsKey~='' and not arms) then return false end
   mmdhl.MountPackage('data/mmd_hotloader/rigs/'..key..'/carrier.gma') if arms then mmdhl.MountPackage('data/mmd_hotloader/rigs/'..armsKey..'/carrier.gma') end
   mmdhl.RegisterActor(rig,arms) return true
  end
  if not ready() and mmdhl.RequestSharedRig then mmdhl.RequestSharedRig(key,ready) if armsKey~='' then mmdhl.RequestSharedRig(armsKey,ready) end end
 end)
 -- NPC and player-model list entries hold translated names; renew them before the spawn menu is rebuilt.
 hook.Add('MMDHL.LanguageChanged','MMDHL.Actors',function() for _,entry in pairs(mmdhl.actorRegistrations) do mmdhl.RegisterActor(entry.rig,entry.arms) end end)
if CLIENT then hook.Add('MMDHL.NamesTranslated','MMDHL.Actors',function() for _,entry in pairs(mmdhl.actorRegistrations) do mmdhl.RegisterActor(entry.rig,entry.arms) end end) end
 hook.Add('CreateClientsideRagdoll','MMDHL.ClientCorpse',function(source,corpse)
  if not mmdhl.IsMMD(source) or not IsValid(corpse) then return end
  corpse.MMDHLLocalAsset=mmdhl.GetAsset(source) corpse.MMDHLLocalRigKey=source:GetNW2String('MMDHLRig') corpse.MMDHLLocalRig=mmdhl.GetRig(source) corpse.MMDHLCorpse=true
  if mmdhl.RegisterClientRagdoll then mmdhl.RegisterClientRagdoll(corpse) end
  corpse.MMDHLFormerGeneration=source:GetNW2Int('MMDHLGeneration',0)
  corpse.MMDHLFormerActor=source
  mmdhl.ApplyMaterialState(corpse,mmdhl.CaptureMaterialState(source))
  for _,m in ipairs(mmdhl.GetMorphs(source)) do if m.native>=0 then corpse:SetFlexWeight(m.native,source:GetFlexWeight(m.native)) end end
  corpse.MMDHLLocalMorphs={} for i in ipairs(mmdhl.GetMorphs(source)) do corpse.MMDHLLocalMorphs[i]=mmdhl.GetMorphWeight(source,i-1) end
  corpse:SetFlexScale(source:GetFlexScale()) corpse:SetColor(source:GetColor()) corpse:SetMaterial(source:GetMaterial())
  if mmdhl.TransferPresentation then mmdhl.TransferPresentation(source,corpse) end
 end)
end
