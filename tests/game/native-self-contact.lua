-- Isolate two bodies on a disposable carrier. Breaking its internal joints
-- prevents a joint from masquerading as a contact response. All pair rules
-- remain those loaded by the engine from the generated .phy.
local e=assert(MMDHL_CARRIER_ENTITY)
local moving,target=7,2 -- left hand / upper torso
e:RemoveInternalConstraint(-1)
for i=0,17 do local p=e:GetPhysicsObjectNum(i) p:EnableMotion(false) p:EnableGravity(false) p:EnableCollisions(i==moving or i==target) end
local hand,chest=e:GetPhysicsObjectNum(moving),e:GetPhysicsObjectNum(target)
local center=chest:LocalToWorld(chest:GetMassCenter())
local start=center-Vector(35,0,0)
hand:SetPos(start-hand:LocalToWorld(hand:GetMassCenter())+hand:GetPos())
hand:EnableMotion(true) hand:Wake() hand:SetVelocity(Vector(140,0,0))
MMDHL_SELF_CONTACT={contacts=0,closest=1e6,furthest=-1e6,done=false}
local result=MMDHL_SELF_CONTACT
local callback=e:AddCallback('PhysicsCollide',function(_,d)
 if d.HitEntity==e then result.contacts=result.contacts+1 result.last={speed=d.Speed,hit={d.HitPos:Unpack()}} end
end)
local begun=CurTime()
timer.Create('MMDHL.SelfContact',.015,0,function()
 if not IsValid(e) then timer.Remove('MMDHL.SelfContact') return end
 local signed=(hand:LocalToWorld(hand:GetMassCenter())-center).x
 result.closest=math.min(result.closest,math.abs(signed)) result.furthest=math.max(result.furthest,signed)
 if CurTime()-begun>1 then
  result.done=true result.finalOffset=signed result.finalSpeed=hand:GetVelocity():Length()
  e:RemoveCallback('PhysicsCollide',callback) timer.Remove('MMDHL.SelfContact') hand:EnableMotion(false)
 end
end)
return true
