-- Drive angular velocity against real VPhysics elbow/knee constraints.
local e=assert(MMDHL_CARRIER_ENTITY) local rig=MMDHL_CARRIER
local initial={}
for i=0,17 do local p=e:GetPhysicsObjectNum(i) p:EnableMotion(false) p:EnableGravity(false) p:EnableCollisions(false) initial[i]={p:GetPos(),p:GetAngles()} end
local targets={6,10,13,16} local result={samples={},done=false} MMDHL_LIMITS=result
local phase=0 local phaseStart=0 local selected
local function begin()
 phase=phase+1 if phase>#targets*2 then result.done=true timer.Remove('MMDHL.LimitStress') for i=0,17 do e:GetPhysicsObjectNum(i):EnableMotion(false) e:GetPhysicsObjectNum(i):EnableGravity(true) e:GetPhysicsObjectNum(i):EnableCollisions(true) end return end
 selected=targets[math.ceil(phase/2)]
 for i=0,17 do local p=e:GetPhysicsObjectNum(i) p:EnableMotion(false) p:SetPos(initial[i][1]) p:SetAngles(initial[i][2]) p:SetVelocity(vector_origin) p:AddAngleVelocity(-p:GetAngleVelocity()) end
 for i=selected,selected+1 do e:GetPhysicsObjectNum(i):EnableMotion(true) end
 phaseStart=CurTime() result.samples[phase]={body=selected,sign=phase%2==1 and 1 or -1,maximum={0,0,0},minimum={0,0,0},lower=rig.bodies[selected+1].lower,upper=rig.bodies[selected+1].upper}
end
begin()
timer.Create('MMDHL.LimitStress',.015,0,function()
 if not IsValid(e) then timer.Remove('MMDHL.LimitStress') return end
 local p=e:GetPhysicsObjectNum(selected) local s=result.samples[phase]
 p:AddAngleVelocity(Vector(0,0,30)*s.sign)
 local rest=Matrix() rest:SetAngles(initial[selected][2]) local live=Matrix() live:SetAngles(p:GetAngles())
 local a=(rest:GetInverse()*live):GetAngles() local angles={math.NormalizeAngle(a.r),math.NormalizeAngle(a.p),math.NormalizeAngle(a.y)}
 if CurTime()-phaseStart>.2 then for i,v in ipairs(angles) do s.maximum[i]=math.max(s.maximum[i],v) s.minimum[i]=math.min(s.minimum[i],v) end end
 if CurTime()-phaseStart>1.5 then begin() end
end)
return true
