if SERVER then AddCSLuaFile() end
SWEP.PrintName=mmdhl.Localize(mmdhl.L'weapon.name')
SWEP.Author='Model Hotloader'
SWEP.Instructions=mmdhl.Localize(mmdhl.L'weapon.instructions')
SWEP.Spawnable=true
SWEP.Category='Model Hotloader'
SWEP.UseHands=true
SWEP.ViewModel='models/weapons/c_superphyscannon.mdl'
SWEP.WorldModel='models/weapons/w_physics.mdl'
SWEP.Primary.ClipSize=-1 SWEP.Primary.DefaultClip=-1 SWEP.Primary.Automatic=false SWEP.Primary.Ammo='none'
SWEP.Secondary.ClipSize=-1 SWEP.Secondary.DefaultClip=-1 SWEP.Secondary.Automatic=false SWEP.Secondary.Ammo='none'
function SWEP:Initialize() self:SetHoldType('physgun') end
function SWEP:HitMMD()
 if not mmdhl or not mmdhl.FeatureAvailable or not mmdhl.FeatureAvailable('physics') then
  local p=self:GetOwner() if SERVER and IsValid(p) and mmdhl and mmdhl.ChatPrint then mmdhl.ChatPrint(p,mmdhl.L'common.unavailable') end return
 end
 local p=self:GetOwner() if not IsValid(p) then return end
 return mmdhl.Decode(mmdhl.native.Raycast(p:EyePos(),p:EyePos()+p:GetAimVector()*4096))
end
-- The weapon is spawnable in multiplayer, so every action needs the edit
-- permission the context-menu actions check (server.lua): prop protection and
-- gamemodes answer it through CanProperty and the MMDHLCanEdit hook.
function SWEP:TargetEntity(hit)
 local ent=mmdhl.EntityForInstance(hit.instance)
 if IsValid(ent) and mmdhl.CanEdit and mmdhl.CanEdit(self:GetOwner(),ent,'bodygroups') then return ent end
end
function SWEP:PrimaryAttack()
 self:SetNextPrimaryFire(CurTime()+.2) if CLIENT then return end
 local hit=self:HitMMD() if not hit then return end
 local ent=self:TargetEntity(hit) if not ent then return end
 local frozen=ent:GetFrozen() ent:SetFrozen(false) mmdhl.native.SetFrozen(hit.instance,false)
 local point=Vector(unpack(hit.position)) self.GrabDistance=point:Distance(self:GetOwner():EyePos())
 local _,err=mmdhl.native.BeginGrab(hit.instance,hit.body,point)
 self.Grabbing=not err
 if err and frozen then ent:SetFrozen(true) mmdhl.native.SetFrozen(hit.instance,true) end
end
function SWEP:Think()
 if CLIENT or not self.Grabbing then return end
 local p=self:GetOwner()
 if not IsValid(p) or not p:KeyDown(IN_ATTACK) then self:Release() return end
 mmdhl.native.UpdateGrab(p:EyePos()+p:GetAimVector()*self.GrabDistance)
end
function SWEP:SecondaryAttack()
 self:SetNextSecondaryFire(CurTime()+.2) if CLIENT then return end
 local hit=self:HitMMD() if not hit then return end
 local ent=self:TargetEntity(hit) if not ent then return end
 self:Release() ent:SetFrozen(not ent:GetFrozen()) mmdhl.native.SetFrozen(hit.instance,ent:GetFrozen())
end
function SWEP:Reload()
 if CLIENT or (self.NextReload or 0)>CurTime() then return end self.NextReload=CurTime()+.4
 local hit=self:HitMMD() if not hit then return end
 local ent=self:TargetEntity(hit) if not ent then return end
 self:Release()
 if self:GetOwner():KeyDown(IN_SPEED) then ent:Remove() else mmdhl.native.ResetPhysics(hit.instance) end
end
function SWEP:Release() if SERVER and self.Grabbing then mmdhl.native.EndGrab() self.Grabbing=false end end
function SWEP:Holster() self:Release() return true end
function SWEP:OnRemove() self:Release() end
if SERVER then concommand.Add('mmdhl_grabber',function(p) if game.SinglePlayer() and IsValid(p) then p:Give('weapon_mmdhl') p:SelectWeapon('weapon_mmdhl') end end) end
