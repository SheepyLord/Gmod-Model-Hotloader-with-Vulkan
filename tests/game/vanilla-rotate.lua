-- Actual engine physgun E + mouse rotation. Run in an owned debug session.
local p=Entity(1) local e=mmdhl.testEnt local d=mmdhl.Decode(mmdhl.native.GetDiagnostics(e:GetInstance()))
local head,headBone for i,b in ipairs(d.bodyList) do if b.core and (e:GetBoneName(b.bone)=='頭' or e:GetBoneName(b.bone)=='head') then head=i headBone=b.bone break end end
assert(head,'Rotation scenario needs a standard humanoid head')
local aim=Vector(unpack(d.bodyList[head].position)) p:SetMoveType(MOVETYPE_NOCLIP) p:SetPos(aim-Vector(140,0,64)) p:SetEyeAngles(Angle(0,0,0)) p:SelectWeapon('weapon_physgun')
mmdhl.rotationResult={maxHandleAngle=0,maxBoneAngle=0,samples={}}
local started=CurTime()
hook.Add('StartCommand','MMDHL.TestInput',function(ply,cmd)
 if ply~=p then return end
 local dt=CurTime()-started cmd:ClearButtons() cmd:ClearMovement() cmd:SetMouseX(0) cmd:SetMouseY(0)
 if dt>.5 and dt<6 then
  cmd:SetButtons(IN_ATTACK) cmd:SetViewAngles(Angle(-15,0,0))
  if dt<1.2 then cmd:SetViewAngles(Angle(0,0,0)) end
  if dt>2 and dt<2.3 then cmd:SetButtons(bit.bor(IN_ATTACK,IN_USE)) cmd:SetMouseX(10) end
 end
 if dt>6.2 then hook.Remove('StartCommand','MMDHL.TestInput') end
end)
timer.Create('MMDHL.RotateSamples',.2,30,function()
 local ph=e:GetPhysicsObject():GetAngles() local bone=e:GetBoneMatrix(headBone):GetAngles() local r=mmdhl.rotationResult
 local function size(a) return math.abs(math.NormalizeAngle(a.p))+math.abs(math.NormalizeAngle(a.y))+math.abs(math.NormalizeAngle(a.r)) end
 r.maxHandleAngle=math.max(r.maxHandleAngle,size(ph)) r.maxBoneAngle=math.max(r.maxBoneAngle,size(bone))
 r.samples[#r.samples+1]={t=CurTime()-started,handle={ph:Unpack()},bone={bone:Unpack()}}
 if CurTime()-started>5.8 then r.yawError=math.abs(math.AngleDifference(ph.y,bone.y)) r.passed=r.maxHandleAngle>5 and r.yawError<5 end
end)
return true
