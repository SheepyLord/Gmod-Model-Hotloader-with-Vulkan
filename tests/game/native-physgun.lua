-- The test drives real CUserCmd input; it never invokes physgun pickup hooks.
local p=player.GetHumans()[1] local e=mmdhl.testEnt
for i=0,17 do e:GetPhysicsObjectNum(i):EnableMotion(false) end
local index=MMDHL_TEST_PHYS or 3 local body=e:GetPhysicsObjectNum(index)
local aim=LocalToWorld(body:GetMassCenter(),angle_zero,body:GetPos(),body:GetAngles())
p:SetMoveType(MOVETYPE_NOCLIP) p:SetPos(aim-Vector(140,0,64)) p:SetEyeAngles(Angle(0,0,0))
p:Give('weapon_physgun') p:SelectWeapon('weapon_physgun')
local result={body=index,pickups=0,drops=0,startHeight=body:GetPos().z,maxHeight=body:GetPos().z,maxRotation=0}
mmdhl.nativeGun=result
local startAngle=body:GetAngles() local started=CurTime() local playerStart=p:GetPos()
hook.Add('OnPhysgunPickup','MMDHL.TestPickup',function(_,ent) if ent==e then result.pickups=result.pickups+1 for i=0,17 do e:GetPhysicsObjectNum(i):EnableMotion(true) e:GetPhysicsObjectNum(i):Wake() end end end)
hook.Add('PhysgunDrop','MMDHL.TestDrop',function(_,ent) if ent==e then result.drops=result.drops+1 end end)
hook.Add('StartCommand','MMDHL.TestInput',function(ply,cmd)
 if ply~=p then return end
 local dt=CurTime()-started cmd:ClearButtons() cmd:ClearMovement() cmd:SetMouseX(0) cmd:SetMouseY(0)
 local view=Angle(dt>1.2 and -25 or 0,0,0) cmd:SetViewAngles(view) p:SetEyeAngles(view)
 p:SetPos(playerStart+Vector(0,0,math.Clamp((dt-1.2)*35,0,60)))
 if dt>.5 and dt<4 then cmd:SetButtons(IN_ATTACK)
  if dt>2 and dt<3 then cmd:SetButtons(bit.bor(IN_ATTACK,IN_USE)) cmd:SetMouseX(12) end
 elseif dt>=4 and dt<4.3 then cmd:SetButtons(bit.bor(IN_ATTACK,IN_ATTACK2))
 elseif dt>4.5 then hook.Remove('StartCommand','MMDHL.TestInput') end
end)
timer.Create('MMDHL.NativeGunSamples',.1,50,function()
 if not IsValid(e) then return end
 result.maxHeight=math.max(result.maxHeight,body:GetPos().z)
 local a=body:GetAngles()
 -- Observe rotation about every axis without Euler wrap/singularity artifacts.
 local forward=math.deg(math.acos(math.Clamp(a:Forward():Dot(startAngle:Forward()),-1,1)))
 local up=math.deg(math.acos(math.Clamp(a:Up():Dot(startAngle:Up()),-1,1)))
 result.maxRotation=math.max(result.maxRotation,forward,up)
 if CurTime()-started>=4.8 then
  result.frozen={} for i=0,17 do result.frozen[i+1]=not e:GetPhysicsObjectNum(i):IsMotionEnabled() end result.done=true
  hook.Remove('OnPhysgunPickup','MMDHL.TestPickup') hook.Remove('PhysgunDrop','MMDHL.TestDrop')
 end
end)
return true
