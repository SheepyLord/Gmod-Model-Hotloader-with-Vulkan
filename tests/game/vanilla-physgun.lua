local p=Entity(1) local e=mmdhl.testEnt local d=mmdhl.Decode(mmdhl.native.GetDiagnostics(e:GetInstance()))
local head
for i,b in ipairs(d.bodyList) do if b.core and (e:GetBoneName(b.bone)=='頭' or e:GetBoneName(b.bone)=='head') then head=i break end end
assert(head,'Test needs a standard humanoid head')
local aim=Vector(unpack(d.bodyList[head].position))
p:SetMoveType(MOVETYPE_NOCLIP) p:SetPos(aim-Vector(140,0,64)) p:SetEyeAngles(Angle(0,0,0))
p:Give('weapon_physgun') p:SelectWeapon('weapon_physgun')
local result={pickups=0,drops=0,startHeight=aim.z,maxHeight=aim.z} mmdhl.vanillaGun=result
hook.Add('OnPhysgunPickup','MMDHL.TestPickup',function(_,ent) if ent==e then result.pickups=result.pickups+1 end end)
hook.Add('PhysgunDrop','MMDHL.TestDrop',function(_,ent) if ent==e then result.drops=result.drops+1 end end)
local started=CurTime()
hook.Add('StartCommand','MMDHL.TestInput',function(ply,cmd)
 if ply~=p then return end
 local dt=CurTime()-started cmd:ClearButtons() cmd:ClearMovement()
 if dt<.5 then return end
 if dt<4 then cmd:SetButtons(IN_ATTACK) cmd:SetViewAngles(Angle(dt>1.2 and -25 or 0,0,0))
 elseif dt<4.3 then cmd:SetButtons(bit.bor(IN_ATTACK,IN_ATTACK2)) cmd:SetViewAngles(Angle(-25,0,0))
 else hook.Remove('StartCommand','MMDHL.TestInput') end
end)
timer.Create('MMDHL.VanillaGunSamples',.1,48,function()
 local rig=mmdhl.Decode(mmdhl.native.GetDiagnostics(e:GetInstance()))
 result.maxHeight=math.max(result.maxHeight,rig.bodyList[head].position[3])
 if CurTime()-started>=4.7 then
  result.frozen=e:GetFrozen() result.done=true
  hook.Remove('OnPhysgunPickup','MMDHL.TestPickup') hook.Remove('PhysgunDrop','MMDHL.TestDrop')
 end
end)
return true
