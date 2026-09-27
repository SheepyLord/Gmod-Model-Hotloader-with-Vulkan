local e=mmdhl.testEnt local id=e:GetInstance()
mmdhl.native.ResetPhysics(id) mmdhl.native.SetFrozen(id,true) e:SetFrozen(true)
if IsValid(mmdhl.testProp) then mmdhl.testProp:Remove() end
local p=ents.Create('prop_physics') p:SetModel('models/hunter/blocks/cube025x025x025.mdl') p:SetPos(Vector(-25,-384,-12215)) p:Spawn()
local ph=p:GetPhysicsObject() ph:EnableGravity(false) ph:SetMass(5) ph:SetVelocity(Vector(250,0,0)) ph:Wake()
mmdhl.testProp=p mmdhl.feedbackCount=0 mmdhl.samples={}
local finish=CurTime()+1
hook.Add('Tick','MMDHL.PropTest',function()
 mmdhl.feedbackCount=mmdhl.feedbackCount+(mmdhl.bridge and mmdhl.bridge.feedbackApplied or 0)
 table.insert(mmdhl.samples,{x=p:GetPos().x,velocity=ph:GetVelocity().x})
 if CurTime()>finish then hook.Remove('Tick','MMDHL.PropTest') end
end)
return {ray=mmdhl.Decode(mmdhl.native.Raycast(Vector(-80,-384,-12215),Vector(164,-384,-12215))),inertia=ph:GetInertia()}
