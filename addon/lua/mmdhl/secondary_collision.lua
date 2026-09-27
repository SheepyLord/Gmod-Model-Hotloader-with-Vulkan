local native=mmdhl.native
local L,lazy=mmdhl.L,mmdhl.I18n.Lazy
mmdhl.SecondaryCollisionModes={
 lazy({id=0},{name=function() return L'secondary.collision.character' end}),
 lazy({id=1},{name=function() return L'secondary.collision.map' end}),
 lazy({id=2},{name=function() return L'secondary.collision.objects' end})
}
function mmdhl.GetSecondaryCollisionMode(ent)
 if CLIENT then
  if ent.MMDHLClientCollisionMode~=nil then return ent.MMDHLClientCollisionMode end
  if mmdhl.GetGlobalSettings then return mmdhl.GetGlobalSettings().secondaryCollision end
 end
 return ent:GetNW2Int('MMDHLSecondaryCollision',2)
end
function mmdhl.SetSecondaryCollisionMode(ent,mode)
 mode=tonumber(mode) if not mmdhl.IsMMD(ent) or not mode or mode~=math.floor(mode) or mode<0 or mode>2 then return false end
 if CLIENT then
  if mmdhl.GetInstance(ent)>0 then local _,err=native.SetSecondaryCollisionMode(mmdhl.GetInstance(ent),mode) if err then return false,err end end
  ent.MMDHLClientCollisionMode=mode return true
 end
 if mmdhl.GetInstance(ent)>0 then local _,err=native.SetSecondaryCollisionMode(mmdhl.GetInstance(ent),mode) if err then return false,err end end
 ent:SetNW2Int('MMDHLSecondaryCollision',mode) ent.MMDOptions=ent.MMDOptions or {} ent.MMDOptions.secondaryCollision=mode return true
end
if SERVER then
 util.AddNetworkString('mmdhl_scene_active')
 local localDemand=true
 net.Receive('mmdhl_scene_active',function(_,ply) if game.SinglePlayer() then localDemand=net.ReadBool() end end)
 util.AddNetworkString('mmdhl_secondary_collision')
 net.Receive('mmdhl_secondary_collision',function(_,ply) local ent,mode=net.ReadEntity(),net.ReadUInt(2) if game.SinglePlayer() and mmdhl.IsMMD(ent) and gamemode.Call('CanProperty',ply,'bodygroups',ent) then mmdhl.SetSecondaryCollisionMode(ent,mode) end end)
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
  -- Classify on the engine thread; each immutable snapshot excludes living
  -- actors before the native bridge reads their physics. Dead ragdolls remain.
  local excluded={} local living=0 local classified={}
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
    living=living+1
    for _,body in pairs(bodies or {}) do if IsValid(body) then excluded[#excluded+1]=body end end
   end
  end
  -- Players are few and can outrun the region margin (noclip, vehicles, falls).
  for _,ent in ipairs(player.GetAll()) do classify(ent) end
  for _,ent in ipairs(nearbyEntities()) do classify(ent) end
  active=true local value,err=native.CaptureSecondaryScene(owners,CurTime(),excluded)
  if err then
   mmdhl.sceneError=err ErrorNoHalt('[Model Hotloader scene] '..err..'\n')
   file.CreateDir('mmd_hotloader/diagnostics') file.Write('mmd_hotloader/diagnostics/secondary-scene-'..os.time()..'.json',util.TableToJSON({error=err,map=game.GetMap(),scene=mmdhl.sceneDiagnostics,time=CurTime()},true))
   net.Start('mmdhl_notice') net.WriteString(L('secondary.error.contacts_disabled',{reason=err})) net.Broadcast()
   for _,ent in ipairs(mmdhl.Entities()) do mmdhl.SetSecondaryCollisionMode(ent,0) end
  else mmdhl.sceneDiagnostics=mmdhl.Decode(value) mmdhl.sceneDiagnostics.excludedLivingEntities=living end
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
   if mmdhl.GetInstance(ent)>0 and mmdhl.GetSecondaryCollisionMode(ent)>0 and not (lod and lod.suspended) and not (mmdhl.IsLocalFirstPerson and mmdhl.IsLocalFirstPerson(ent)) then active=true break end
  end end
  if active~=wasActive then wasActive=active net.Start('mmdhl_scene_active') net.WriteBool(active) net.SendToServer() end
 end)
end
