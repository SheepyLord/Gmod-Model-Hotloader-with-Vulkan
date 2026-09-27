local native=mmdhl.native
local L,lazy=mmdhl.L,mmdhl.I18n.Lazy
-- The two current solvers first: the multicore CPU one (default) and the Vulkan
-- one, which shares the device of the bundled DXVK renderer.
mmdhl.SecondaryBackends={
 lazy({id='cpu_mt_v2'},{name=function() return L'secondary.backend.multicore' end}),
 lazy({id='gpu_vulkan'},{name=function() return L'secondary.backend.vulkan' end}),
 lazy({id='reference'},{name=function() return L'secondary.backend.compatibility' end}),
 lazy({id='cpu_mt'},{name=function() return L'secondary.backend.legacy_multicore' end}),
 lazy({id='gpu_opencl'},{name=function() return L'secondary.backend.gpu' end})
}
function mmdhl.ValidSecondaryBackend(value)
 for _,backend in ipairs(mmdhl.SecondaryBackends) do if value==backend.id then return value end end
 return 'cpu_mt_v2'
end
function mmdhl.GetSecondaryBackend(ent)
 if not mmdhl.IsMMD(ent) then return end
 local value,err=native.GetSecondaryBackend(mmdhl.GetInstance(ent))
 return value and mmdhl.Decode(value) or nil,err
end
function mmdhl.SetSecondaryBackend(ent,backend)
 if not mmdhl.IsMMD(ent) or mmdhl.ValidSecondaryBackend(backend)~=backend then return false,L'secondary.error.unknown_backend' end
 if CLIENT then
  if mmdhl.GetInstance(ent)<1 then return true end
  local ok,err=native.SetSecondaryBackend(mmdhl.GetInstance(ent),backend)
  if ok then ent.MMDPhysicsLOD=nil end
  return ok,err
 end
 if mmdhl.GetInstance(ent)>0 then local ok,err=native.SetSecondaryBackend(mmdhl.GetInstance(ent),backend) if not ok then return false,err end end
 ent:SetNW2String('MMDHLSecondaryBackend',backend) ent.MMDOptions=ent.MMDOptions or {} ent.MMDOptions.secondaryBackend=backend
 ent.MMDStopped=nil
 net.Start('mmdhl_physics_reset') net.WriteEntity(ent) net.Broadcast()
 return true
end
if CLIENT then
 -- v2.0 asynchronous worlds: resting bodies may sleep, and a frame may wait
 -- briefly for the tick it submitted instead of presenting the previous one.
 local sleep=CreateClientConVar('mmdhl_secondary_sleep','1',true,false,'Let resting character model secondary bodies sleep (cpu_mt_v2)',0,1)
 local waitBudget=CreateClientConVar('mmdhl_secondary_wait_ms','0',true,false,'Milliseconds a frame waits for its asynchronous physics tick (cpu_mt_v2)',0,16)
 local sleepLinear=CreateClientConVar('mmdhl_secondary_sleep_linear','0.5',true,false,'Linear speed (PMX units per second) below which a resting body may sleep',0,10)
 local sleepAngular=CreateClientConVar('mmdhl_secondary_sleep_angular','0.35',true,false,'Angular speed (radians per second) below which a resting body may sleep',0,10)
 local sleepSeconds=CreateClientConVar('mmdhl_secondary_sleep_seconds','1.5',true,false,'Seconds at rest before a body sleeps',0.1,60)
 local wakeDrift=CreateClientConVar('mmdhl_secondary_wake_drift','0.02',true,false,'PMX units a resting follower may drift before it wakes its chain (raise above an animation addon\'s breathing amplitude to let animated characters sleep)',0,10)
 local function applyAsyncSettings()
  if native.SetSecondarySleep then native.SetSecondarySleep(sleep:GetBool(),math.Clamp(sleepLinear:GetFloat(),0,10),math.Clamp(sleepAngular:GetFloat(),0,10),math.Clamp(sleepSeconds:GetFloat(),.1,60),math.Clamp(wakeDrift:GetFloat(),0,10)) end
  if native.SetSecondaryWaitBudget then native.SetSecondaryWaitBudget(math.Clamp(waitBudget:GetFloat(),0,16)) end
 end
 cvars.AddChangeCallback('mmdhl_secondary_sleep',applyAsyncSettings,'MMDHL.AsyncSettings')
 cvars.AddChangeCallback('mmdhl_secondary_wait_ms',applyAsyncSettings,'MMDHL.AsyncSettings')
 for _,name in ipairs({'mmdhl_secondary_sleep_linear','mmdhl_secondary_sleep_angular','mmdhl_secondary_sleep_seconds','mmdhl_secondary_wake_drift'}) do cvars.AddChangeCallback(name,applyAsyncSettings,'MMDHL.AsyncSettings') end
 applyAsyncSettings()
end
if SERVER then
 util.AddNetworkString('mmdhl_secondary_backend')
 net.Receive('mmdhl_secondary_backend',function(_,ply)
  local ent,backend=net.ReadEntity(),net.ReadString()
  if game.SinglePlayer() and mmdhl.IsMMD(ent) and gamemode.Call('CanProperty',ply,'bodygroups',ent) then mmdhl.SetSecondaryBackend(ent,backend) end
 end)
end
