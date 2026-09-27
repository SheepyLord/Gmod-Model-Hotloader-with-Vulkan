-- Builds one stock GMod scene (no MMD content) on the server of the owned test
-- session for scripts/measure-vanilla.py. Cleans up the map, parks the frozen
-- player at the camera (entities outside the player's PVS are not sent to the
-- client) and spawns the scene. Returns the camera for scene-measure.lua as
-- JSON. Everything is seeded, so a scene is the same in every session.
local c=MMDHL_VANILLA_CONFIG
assert(c and MMDHL_DEBUG_TOKEN and SERVER,'Owned server test configuration required')
local ply=player.GetHumans()[1] assert(IsValid(ply),'No player')
timer.Remove('MMDHL.VanillaShaker')
ply:Freeze(false) ply:Flashlight(false)
game.CleanUpMap()
math.randomseed(c.seed or 1234)
ply:GodEnable()
-- Camera: the map's first spawn point (or c.origin) at eye height, along the
-- spawn yaw (or c.yaw), pitched by c.pitch.
local spawn=ents.FindByClass('info_player_start')[1]
local base=c.origin and Vector(c.origin[1],c.origin[2],c.origin[3]) or (IsValid(spawn) and spawn:GetPos() or ply:GetPos())
local yaw=c.yaw or (IsValid(spawn) and spawn:GetAngles().y or 0)
local eye=base+Vector(0,0,64)
local angles=Angle(c.pitch or 0,yaw,0)
ply:SetPos(base) ply:SetEyeAngles(angles) ply:SetLocalVelocity(vector_origin) ply:Freeze(true)
local flat=Angle(0,yaw,0) local forward,right=flat:Forward(),flat:Right()
local function ground(pos)
 local tr=util.TraceLine({start=pos+Vector(0,0,256),endpos=pos-Vector(0,0,4096),mask=MASK_SOLID_BRUSHONLY})
 return tr.HitPos
end
local function valid(list) local out={} for _,m in ipairs(list) do if util.IsValidModel(m) then out[#out+1]=m end end return out end
-- Stock HL2 props that ship with GMod.
local PROPS=valid{'models/props_c17/oildrum001.mdl','models/props_junk/wood_crate001a.mdl','models/props_borealis/bluebarrel001.mdl',
 'models/props_c17/furniturecouch001a.mdl','models/props_junk/trashdumpster01a.mdl','models/props_c17/furnituretable002a.mdl',
 'models/props_wasteland/kitchen_counter001b.mdl','models/props_junk/trafficcone001a.mdl','models/props_c17/chair02a.mdl',
 'models/props_c17/concrete_barrier001a.mdl','models/props_lab/monitor01a.mdl','models/props_interiors/vendingmachinesoda01a.mdl',
 'models/props_junk/watermelon01.mdl','models/props_c17/lampshade001a.mdl','models/props_wasteland/controlroom_filecabinet002a.mdl'}
local CHARACTERS=valid{'models/humans/group01/male_02.mdl','models/humans/group01/male_04.mdl','models/humans/group01/male_07.mdl',
 'models/humans/group01/female_01.mdl','models/humans/group01/female_03.mdl','models/humans/group02/male_08.mdl',
 'models/humans/group03/male_05.mdl','models/humans/group03/female_06.mdl'}
local spawned={}
-- count props in rows of `columns`, `spacing` apart, starting `distance` ahead.
local function grid(count,columns,spacing,distance,frozen,height)
 for i=0,count-1 do
  local row,col=math.floor(i/columns),i%columns
  local at=ground(base+forward*(distance+row*spacing)+right*((col-(columns-1)/2)*spacing))
  local e=ents.Create('prop_physics') e:SetModel(PROPS[i%#PROPS+1])
  e:SetPos(at+Vector(0,0,(height or 0)-e:OBBMins().z)) e:SetAngles(Angle(0,(i*37)%360,0)) e:Spawn()
  local phys=e:GetPhysicsObject()
  if IsValid(phys) then if frozen then phys:EnableMotion(false) else phys:Wake() end end
  spawned[#spawned+1]=e
 end
end
local scenes={}
function scenes.empty() end
function scenes.props() grid(c.count or 400,c.columns or 20,c.spacing or 90,c.distance or 160,true) end
-- Unfrozen props, kept tumbling: every 1.5 s each one gets a seeded random kick.
function scenes.physics()
 grid(c.count or 200,c.columns or 14,c.spacing or 70,c.distance or 220,false,c.height or 60)
 timer.Create('MMDHL.VanillaShaker',1.5,0,function()
  for _,e in ipairs(spawned) do
   local p=IsValid(e) and e:GetPhysicsObject()
   if IsValid(p) then p:Wake() p:AddVelocity(Vector(math.Rand(-60,60),math.Rand(-60,60),math.Rand(120,260))) end
  end
 end)
end
-- Animated characters (prop_dynamic looping a walk cycle in place): the
-- citizen paths (which an install may have replaced) or c.models.
function scenes.characters()
 local models=c.models and valid(c.models) or CHARACTERS assert(#models>0,'No valid character model')
 local columns=c.columns or 8
 for i=0,(c.count or 48)-1 do
  local row,col=math.floor(i/columns),i%columns
  local at=ground(base+forward*((c.distance or 180)+row*70)+right*((col-(columns-1)/2)*60))
  local e=ents.Create('prop_dynamic') e:SetModel(models[i%#models+1])
  local sequence=e:LookupSequence(c.sequence or 'walk_all')
  if not sequence or sequence<0 then sequence=e:SelectWeightedSequence(ACT_WALK) end
  if not sequence or sequence<0 then sequence=e:SelectWeightedSequence(ACT_IDLE) end
  e:SetKeyValue('DefaultAnim',e:GetSequenceName(sequence)) e:SetKeyValue('solid','0')
  e:SetPos(at) e:SetAngles(Angle(0,yaw+180+((i*23)%40-20),0)) e:Spawn() e:Activate()
  spawned[#spawned+1]=e
 end
end
function scenes.flashlight() ply:Flashlight(true) end
local build=scenes[c.scene] assert(build,'Unknown scene '..tostring(c.scene))
build()
if c.flashlight then ply:Flashlight(true) end
local animated=0 for _,e in ipairs(spawned) do if e:GetClass()=='prop_dynamic' and e:GetSequence()>0 then animated=animated+1 end end
return util.TableToJSON({origin={eye.x,eye.y,eye.z},angles={angles.p,angles.y,angles.r},spawned=#spawned,animated=animated,props=#PROPS,characters=#CHARACTERS,map=game.GetMap()})
