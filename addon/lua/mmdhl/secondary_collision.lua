local native=mmdhl.native
local L,lazy=mmdhl.L,mmdhl.I18n.Lazy
-- What hair and clothing collide with: one bit per checkbox of the physics
-- settings (mmdhl_collide_with), as Collide:: in the native module. Character
-- is the model's own body; the others come from the captured Source scene.
local Collide={world=1,character=2,objects=4,players=8,npcs=16}
mmdhl.Collide=Collide
mmdhl.CollideDefault=Collide.character+Collide.objects
mmdhl.CollideScene=Collide.world+Collide.objects+Collide.players+Collide.npcs
mmdhl.CollisionTargets={
 lazy({key='world',flag=Collide.world},{name=function() return L'physics.collide.world' end,tooltip=function() return L'physics.collide.world_tooltip' end}),
 lazy({key='character',flag=Collide.character},{name=function() return L'physics.collide.character' end,tooltip=function() return L'physics.collide.character_tooltip' end}),
 lazy({key='objects',flag=Collide.objects},{name=function() return L'physics.collide.objects' end,tooltip=function() return L'physics.collide.objects_tooltip' end}),
 lazy({key='players',flag=Collide.players},{name=function() return L'physics.collide.players' end,tooltip=function() return L'physics.collide.players_tooltip' end}),
 lazy({key='npcs',flag=Collide.npcs},{name=function() return L'physics.collide.npcs' end,tooltip=function() return L'physics.collide.npcs_tooltip' end})
}
function mmdhl.ValidCollisionFlags(flags)
 flags=tonumber(flags) if flags and flags==math.floor(flags) and flags>=0 and flags<=31 then return flags end
end
-- Native modules before 2.2 take a level: 0 the character only, 1 and the map, 2 and objects.
function mmdhl.CollisionLevel(flags)
 if bit.band(flags,Collide.objects)~=0 then return 2 end
 return bit.band(flags,Collide.world)~=0 and 1 or 0
end
-- The scene objects these flags need, as far as this machine's native module can
-- tell them apart: one before 2.2 cannot tag living players and NPCs.
function mmdhl.SceneKinds(flags)
 local kinds=bit.band(flags,mmdhl.CollideScene)
 if not native.SetSecondaryCollisionFlags then kinds=bit.band(kinds,Collide.world+Collide.objects) end
 return kinds
end
function mmdhl.GetCollisionFlags(ent)
 if CLIENT then
  if ent.MMDHLClientCollisionFlags~=nil then return ent.MMDHLClientCollisionFlags end
  if mmdhl.GetGlobalSettings then return mmdhl.GetGlobalSettings().collisionFlags end
 end
 return ent:GetNW2Int('MMDHLCollisionFlags',mmdhl.CollideDefault)
end
function mmdhl.SetCollisionFlags(ent,flags)
 flags=mmdhl.ValidCollisionFlags(flags) if not mmdhl.IsMMD(ent) or not flags then return false end
 local handle=mmdhl.GetInstance(ent)
 if handle>0 then
  local _,err if native.SetSecondaryCollisionFlags then _,err=native.SetSecondaryCollisionFlags(handle,flags) else _,err=native.SetSecondaryCollisionMode(handle,mmdhl.CollisionLevel(flags)) end
  if err then return false,err end
 end
 if CLIENT then ent.MMDHLClientCollisionFlags=flags return true end
 ent:SetNW2Int('MMDHLCollisionFlags',flags) ent.MMDOptions=ent.MMDOptions or {} ent.MMDOptions.collisionFlags=flags return true
end
if SERVER then
 util.AddNetworkString('mmdhl_scene_active')
 local localDemand=true
 net.Receive('mmdhl_scene_active',function(_,ply) if game.SinglePlayer() then localDemand=net.ReadBool() end end)
 local active=false
 local function physicsBodies(ent)
  local count=ent:GetPhysicsObjectCount()
  if count==0 then ent.MMDSceneBodies=nil return end
  local first=ent:GetPhysicsObjectNum(0) local bodies=ent.MMDSceneBodies
  -- Short-circuit before equality: Source throws when comparing a deleted
  -- PhysObj. PhysicsInit replaces the array, so count/first detect rebuilding.
  if not bodies or #bodies~=count or not IsValid(bodies[1]) or not IsValid(first) or bodies[1]~=first then
   bodies={} for i=0,count-1 do local body=ent:GetPhysicsObjectNum(i) if IsValid(body) then bodies[i+1]=body end end
   ent.MMDSceneBodies=bodies
  end
  return bodies
 end
 -- Entities overlapping the scene consumers' regions (native.SceneInterest:
 -- each character's collision box, each remote subscriber's sphere), or every
 -- entity until one has registered. Objects outside every region are not
 -- captured, so their entities need no classification; a busy map is no
 -- longer scanned whole each tick. 64 units cover motion until the next tick.
 local function nearbyEntities()
  local regions=native.SceneInterest and native.SceneInterest()
  if not regions or #regions<6 then return ents.GetAll() end
  local out,seen={},{}
  for i=1,#regions-5,6 do
   for _,ent in ipairs(ents.FindInBox(Vector(regions[i]-64,regions[i+1]-64,regions[i+2]-64),Vector(regions[i+3]+64,regions[i+4]+64,regions[i+5]+64))) do
    if not seen[ent] then seen[ent]=true out[#out+1]=ent end
   end
  end
  return out
 end
 hook.Add('Tick','MMDHL.SecondaryScene',function()
  -- A failed capture (the game's physics library refused by the binary's own checks) turned
  -- contacts off below; asking again every tick would only repeat its error and notice.
  if mmdhl.sceneError then return end
  if game.SinglePlayer() and not localDemand then
   if active then native.CaptureSecondaryScene() active=false end
   mmdhl.sceneDiagnostics={active=false,captureMs=0,objects=0} return
  end
  local wanted=false local owners={}
  for _,ent in ipairs(mmdhl.Entities()) do
   -- Animated actors need contacts even though they have no native physics
   -- object. Build owners once below; comparing cached, destroyed PhysObjs
   -- invokes Source's __eq on a NULL object during death/respawn.
   -- A remote client's collision choice is independent of the server's spawn
   -- defaults. Capture the shared scene while actors exist; each active client
   -- world culls/synchronizes only the contacts requested by its local mode.
   wanted=true
   local bodies=physicsBodies(ent) if bodies then owners[ent:EntIndex()]=bodies end
  end
  if not wanted then if active then native.CaptureSecondaryScene() active=false end return end
  -- Classify on the engine thread. Living players' and NPCs' bodies are tagged
  -- for the characters that collide with them; a native module before 2.2
  -- cannot tag them, so they are left out. Dead ragdolls are objects.
  local tagged=native.SetSecondaryCollisionFlags~=nil
  local excluded,players,npcs={},{},{} local living=0 local classified={}
  local function classify(ent)
   if classified[ent] then return end classified[ent]=true
   -- In a listen-server process, VPhysics already publishes exact transforms.
   -- Remote clients also need an entity/bone owner for other moving objects.
   local livingActor=(ent:IsPlayer() and ent:Alive()) or ((ent:IsNPC() or ent:IsNextBot()) and ent:Health()>0)
   -- Exclusion is required in single-player too, including stock actors that
   -- never appear in the MMD entity list.
   local bodies=(livingActor or not game.SinglePlayer()) and physicsBodies(ent) or owners[ent:EntIndex()]
   if bodies then owners[ent:EntIndex()]=bodies end
   if livingActor then
    living=living+1 local list=not tagged and excluded or ent:IsPlayer() and players or npcs
    for _,body in pairs(bodies or {}) do if IsValid(body) then list[#list+1]=body end end
   end
  end
  -- Players are few and can outrun the region margin (noclip, vehicles, falls).
  for _,ent in ipairs(player.GetAll()) do classify(ent) end
  for _,ent in ipairs(nearbyEntities()) do classify(ent) end
  active=true local value,err=native.CaptureSecondaryScene(owners,CurTime(),excluded,players,npcs)
  if err then
   mmdhl.sceneError=err ErrorNoHalt('[Model Hotloader scene] '..err..'\n')
   file.CreateDir('mmd_hotloader/diagnostics') file.Write('mmd_hotloader/diagnostics/secondary-scene-'..os.time()..'.json',util.TableToJSON({error=err,map=game.GetMap(),scene=mmdhl.sceneDiagnostics,time=CurTime()},true))
   net.Start('mmdhl_notice') net.WriteString(L('secondary.error.contacts_disabled',{reason=err})) net.Broadcast()
   for _,ent in ipairs(mmdhl.Entities()) do mmdhl.SetCollisionFlags(ent,Collide.character) end
  else mmdhl.sceneDiagnostics=mmdhl.Decode(value) mmdhl.sceneDiagnostics.livingEntities=living end
 end)
 hook.Add('PostCleanupMap','MMDHL.SecondarySceneCleanup',function() native.CaptureSecondaryScene() active=false end)
end
if CLIENT then
 local nextCheck=0 local wasActive
 hook.Add('Think','MMDHL.LocalSceneDemand',function()
  if not game.SinglePlayer() or RealTime()<nextCheck then return end nextCheck=RealTime()+.25
  local quality=GetConVar('mmdhl_secondary_iterations') local active=false
  if quality and quality:GetInt()>0 then for _,ent in ipairs(mmdhl.Entities()) do
   local lod=ent.MMDPhysicsLOD
   if mmdhl.GetInstance(ent)>0 and bit.band(mmdhl.GetCollisionFlags(ent),mmdhl.CollideScene)~=0 and not (lod and lod.suspended) and not (mmdhl.HiddenFirstPerson and mmdhl.HiddenFirstPerson(ent)) then active=true break end
  end end
  if active~=wasActive then wasActive=active net.Start('mmdhl_scene_active') net.WriteBool(active) net.SendToServer() end
 end)
end
