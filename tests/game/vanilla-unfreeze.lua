local p=Entity(1) local e=mmdhl.testEnt local c=e:GetPos()
for _,b in ipairs(mmdhl.Decode(mmdhl.native.GetDiagnostics(e:GetInstance())).bodyList) do if b.core then c=Vector(unpack(b.position)) break end end
p:SelectWeapon('weapon_physgun') p:SetPos(c-Vector(140,0,64)) p:SetEyeAngles(Angle(0,0,0))
mmdhl.vanillaUnfreezeDone=false local started=CurTime()
hook.Add('StartCommand','MMDHL.TestInput',function(ply,cmd)
 if ply~=p then return end
 cmd:ClearButtons() cmd:ClearMovement() cmd:SetViewAngles(Angle(0,0,0))
 local dt=CurTime()-started
 if dt>.5 and dt<.8 then cmd:SetButtons(IN_RELOAD) end
 if dt>1.3 then mmdhl.vanillaUnfreezeDone=true hook.Remove('StartCommand','MMDHL.TestInput') end
end)
return true
