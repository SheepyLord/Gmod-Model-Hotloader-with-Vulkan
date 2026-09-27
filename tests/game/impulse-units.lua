local p=mmdhl.testProp local ph=p:GetPhysicsObject()
p:SetPos(Vector(-1000,-1000,-12000)) ph:SetVelocityInstantaneous(Vector()) ph:AddAngleVelocity(-ph:GetAngleVelocity())
ph:EnableDrag(false) ph:EnableGravity(false) ph:SetDamping(0,0) ph:SetMass(2)
local inertia=ph:GetInertia()
ph:ApplyForceCenter(Vector(200,0,0)) ph:ApplyTorqueCenter(p:LocalToWorldAngles(Angle()):Forward()*inertia.x*10)
timer.Simple(.04,function() mmdhl.unitResult={velocity=ph:GetVelocity().x,angular=ph:GetAngleVelocity().x,inertia=inertia.x} end)
return true
