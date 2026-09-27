local native=mmdhl.native
local function ours(ent) return IsValid(ent) and ent:GetClass()=='mmdhl_ragdoll' end
local function angles(a) return Vector(a.p,a.y,a.r) end
local function pickup(p,ent)
 if not ours(ent) then return end
 local from=p:GetShootPos()
 local hit=mmdhl.Decode(native.Raycast(from,from+p:GetAimVector()*32768,ent:GetInstance(),true))
 if not hit then return end
 local phys=ent:GetPhysicsObject() if not IsValid(phys) then return end
 local _,err=native.BeginPhysgun(ent:GetInstance(),hit.body,phys:GetPos(),angles(phys:GetAngles()))
 if err then ErrorNoHalt('[Model Hotloader] '..err..'\n') return end
 ent.MMDHeld=p ent.MMDHeldBody=hit.body ent:SetFrozen(false)
 phys:EnableMotion(true) phys:Wake()
end
hook.Add('OnPhysgunPickup','MMDHL.Physgun',pickup)
hook.Add('GravGunOnPickedUp','MMDHL.Gravgun',pickup)
local function drop(_,ent)
 if not ours(ent) then return end
 if ent.MMDHeld then native.EndGrab() end
 ent.MMDHeld=nil
 local phys=ent:GetPhysicsObject()
 if IsValid(phys) then phys:SetVelocityInstantaneous(vector_origin) phys:AddAngleVelocity(-phys:GetAngleVelocity()) phys:EnableMotion(false) end
end
hook.Add('PhysgunDrop','MMDHL.Physgun',drop)
hook.Add('GravGunOnDropped','MMDHL.Gravgun',drop)
hook.Add('OnPhysgunFreeze','MMDHL.PhysgunFreeze',function(_,_,ent)
 if not ours(ent) then return end
 native.SetFrozen(ent:GetInstance(),true) ent:SetFrozen(true)
end)
hook.Add('CanPlayerUnfreeze','MMDHL.PhysgunUnfreeze',function(_,ent)
 if not ours(ent) then return end
 native.SetFrozen(ent:GetInstance(),false) ent:SetFrozen(false)
end)
function mmdhl.UpdatePhysgun()
 for _,ent in ipairs(ents.FindByClass('mmdhl_ragdoll')) do
  if ent.MMDHeld then
   if not IsValid(ent.MMDHeld) then drop(nil,ent) else
    local phys=ent:GetPhysicsObject()
    if IsValid(phys) then native.UpdatePhysgun(phys:GetPos(),angles(phys:GetAngles())) end
   end
  end
 end
end
