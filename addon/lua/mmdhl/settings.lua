-- Player-wide defaults. Explicit API/duplicator options remain authoritative.
local names={frozen='mmdhl_spawn_frozen',collisionFlags='mmdhl_collide_with',secondaryBackend='mmdhl_secondary_backend'}
if CLIENT then
 CreateClientConVar(names.frozen,'0',true,true,'Freeze newly placed character model ragdolls',0,1)
 CreateClientConVar(names.collisionFlags,tostring(mmdhl.CollideDefault),true,true,'What hair and clothing collide with, a sum of: 1 world, 2 character, 4 objects, 8 living players, 16 living NPCs',0,31)
 CreateClientConVar(names.secondaryBackend,'cpu_mt_v2',true,true,'Global character model secondary physics backend')
 -- Earlier releases chose a level (0 the character, 1 and the map, 2 and
 -- objects). Carry a changed level over once; the old default (2) takes the new
 -- default, which leaves the map out.
 local level=CreateClientConVar('mmdhl_secondary_collision','2',true,false,'Replaced by mmdhl_collide_with',0,2)
 local version=CreateClientConVar('mmdhl_collision_settings_version','0',true,false)
 if version:GetInt()<1 then
  local migrated=({[0]=mmdhl.Collide.character,[1]=mmdhl.Collide.character+mmdhl.Collide.world})[level:GetInt()]
  if migrated then GetConVar(names.collisionFlags):SetInt(migrated) end
  version:SetInt(1)
 end
end
function mmdhl.GetGlobalSettings(ply)
 local function value(key,default)
  if CLIENT then return GetConVar(names[key]):GetString() end
  if IsValid(ply) then return ply:GetInfo(names[key]) end
  return default
 end
 local flags=mmdhl.ValidCollisionFlags(value('collisionFlags',mmdhl.CollideDefault)) or mmdhl.CollideDefault
 -- secondaryCollision: the same choice as a level, for native modules before 2.2.
 return {frozen=value('frozen','0')=='1',collisionFlags=flags,secondaryCollision=mmdhl.CollisionLevel(flags),secondaryBackend=mmdhl.ValidSecondaryBackend(value('secondaryBackend','cpu_mt_v2'))}
end
function mmdhl.WithSpawnDefaults(ply,options)
 local result=table.Copy(options or {})
 for key,value in pairs(mmdhl.GetGlobalSettings(ply)) do if result[key]==nil then result[key]=value end end
 return result
end
function mmdhl.FacingPlayerAngles(ply,position,role)
 local toward=ply:GetPos()-position toward.z=0
 local yaw=toward:LengthSqr()>1 and toward:Angle().y or ply:EyeAngles().y+180
 -- The original PMX ragdoll faces -X. Animated carriers have the Source +X
 -- front after proportion correction. Do not rotate the player-model owner.
 return Angle(0,math.NormalizeAngle(yaw+(role=='ragdoll' and 180 or 0)),0)
end
if CLIENT then
 function mmdhl.SetGlobalSetting(key,value)
  if not names[key] then return false end
  if key=='secondaryBackend' and mmdhl.ValidSecondaryBackend(value)~=value then return false end
  if key=='frozen' then value=value and '1' or '0' end
  GetConVar(names[key]):SetString(tostring(value)) return true
 end
 function mmdhl.BindGlobalChoice(combo,key,choices)
  for _,choice in ipairs(choices) do combo:AddChoice(choice.name,choice.id) end
  combo:SetConVar(names[key])
  combo:SetValue(combo:GetOptionTextByData(GetConVar(names[key]):GetString()))
  combo.OnSelect=function(_,_,_,value) mmdhl.SetGlobalSetting(key,value) end
 end
 -- A checkbox for one bit of mmdhl_collide_with; it follows console changes too.
 function mmdhl.BindCollisionCheckbox(box,flag)
  local cvar=GetConVar(names.collisionFlags)
  local function on() return bit.band(cvar:GetInt(),flag)~=0 end
  box:SetChecked(on())
  box.OnChange=function(_,checked)
   local flags=cvar:GetInt() local wanted=checked and bit.bor(flags,flag) or bit.band(flags,bit.bnot(flag))
   if wanted~=flags then cvar:SetInt(wanted) end
  end
  local think=box.Think
  box.Think=function(self) if think then think(self) end if self:GetChecked()~=on() then self:SetChecked(on()) end end
  return box
 end
 local function synchronize()
  local settings=mmdhl.GetGlobalSettings()
  for _,ent in ipairs(mmdhl.Entities()) do
   if mmdhl.GetInstance(ent)>0 then
    mmdhl.SetSecondaryBackend(ent,settings.secondaryBackend)
    mmdhl.SetCollisionFlags(ent,settings.collisionFlags)
   end
  end
 end
 local function changed() timer.Create('MMDHL.GlobalSettings',.1,1,synchronize) end
 -- Spawn freezing never changes an existing ragdoll or resets secondary motion.
 cvars.AddChangeCallback(names.collisionFlags,changed,'MMDHL.GlobalSettings')
 cvars.AddChangeCallback(names.secondaryBackend,changed,'MMDHL.GlobalSettings')
 hook.Add('InitPostEntity','MMDHL.GlobalSettings',changed)
end
