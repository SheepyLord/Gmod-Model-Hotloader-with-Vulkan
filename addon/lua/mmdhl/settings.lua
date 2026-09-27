-- Player-wide defaults. Explicit API/duplicator options remain authoritative.
local names={frozen='mmdhl_spawn_frozen',secondaryCollision='mmdhl_secondary_collision',secondaryBackend='mmdhl_secondary_backend'}
local L=mmdhl.L
if CLIENT then
 CreateClientConVar(names.frozen,'0',true,true,'Freeze newly placed character model ragdolls',0,1)
 CreateClientConVar(names.secondaryCollision,'2',true,true,'Global character model secondary collision mode',0,2)
 CreateClientConVar(names.secondaryBackend,'cpu_mt_v2',true,true,'Global character model secondary physics backend')
end
function mmdhl.GetGlobalSettings(ply)
 local function value(key,default)
  if CLIENT then return GetConVar(names[key]):GetString() end
  if IsValid(ply) then return ply:GetInfo(names[key]) end
  return default
 end
 return {frozen=value('frozen','0')=='1',secondaryCollision=math.Clamp(math.floor(tonumber(value('secondaryCollision','2')) or 2),0,2),secondaryBackend=mmdhl.ValidSecondaryBackend(value('secondaryBackend','cpu_mt_v2'))}
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
if SERVER then
 util.AddNetworkString('mmdhl_global_settings')
 net.Receive('mmdhl_global_settings',function(_,ply)
  local collision,backend=net.ReadUInt(2),net.ReadString()
  if not game.SinglePlayer() or collision>2 or mmdhl.ValidSecondaryBackend(backend)~=backend then return end
  local failures={}
  for _,ent in ipairs(mmdhl.Entities()) do
   if mmdhl.GetInstance(ent)>0 and gamemode.Call('CanProperty',ply,'bodygroups',ent) then
    if ent:GetNW2String('MMDHLSecondaryBackend','reference')~=backend then
     local ok,err=mmdhl.SetSecondaryBackend(ent,backend)
     if not ok then failures[#failures+1]=L('settings.error.backend_failed',{entity=ent:EntIndex(),reason=tostring(err)}) end
    end
    if mmdhl.GetSecondaryCollisionMode(ent)~=collision then
     local ok,err=mmdhl.SetSecondaryCollisionMode(ent,collision)
     if not ok then failures[#failures+1]=L('settings.error.collision_failed',{entity=ent:EntIndex(),reason=tostring(err)}) end
    end
   end
  end
  if #failures>0 then net.Start('mmdhl_notice') net.WriteString(L('settings.error.global_failed',{failures=table.concat(failures,'; '):sub(1,3000)})) net.Send(ply) end
 end)
else
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
 local function synchronize()
  local settings=mmdhl.GetGlobalSettings()
  for _,ent in ipairs(mmdhl.Entities()) do
   if mmdhl.GetInstance(ent)>0 then
    mmdhl.SetSecondaryBackend(ent,settings.secondaryBackend)
    mmdhl.SetSecondaryCollisionMode(ent,settings.secondaryCollision)
   end
  end
 end
 local function changed() timer.Create('MMDHL.GlobalSettings',.1,1,synchronize) end
 -- Spawn freezing never changes an existing ragdoll or resets secondary motion.
 cvars.AddChangeCallback(names.secondaryCollision,changed,'MMDHL.GlobalSettings')
 cvars.AddChangeCallback(names.secondaryBackend,changed,'MMDHL.GlobalSettings')
 hook.Add('InitPostEntity','MMDHL.GlobalSettings',changed)
end
