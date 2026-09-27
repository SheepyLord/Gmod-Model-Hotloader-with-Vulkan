AddCSLuaFile('shared.lua') AddCSLuaFile('cl_init.lua') AddCSLuaFile('tool_api.lua') include('shared.lua')
function ENT:Initialize()
 self:SetModel('models/props_junk/PopCan01a.mdl')
 self:PhysicsInitBox(Vector(-4,-4,-4),Vector(4,4,4))
 self:SetMoveType(MOVETYPE_VPHYSICS) self:SetSolid(SOLID_OBB) self:EnableCustomCollisions(true) self:DrawShadow(false)
 local phys=self:GetPhysicsObject()
 if IsValid(phys) then phys:SetMass(10) phys:EnableGravity(false) phys:EnableCollisions(false) phys:EnableMotion(false) end
end
function ENT:UpdateTransmitState() return TRANSMIT_ALWAYS end
function ENT:OnRemove()
 if mmdhl and mmdhl.native and self:GetInstance()>0 then mmdhl.native.DestroyInstance(self:GetInstance()) end
end
function ENT:Think()
 if not mmdhl or not mmdhl.native or not mmdhl.FeatureAvailable('physics') then return end
 local bounds=mmdhl.Decode(mmdhl.native.GetBounds(self:GetInstance()))
 if bounds then
  if not self.MMDHeld then self:SetAngles(angle_zero) self:SetPos(Vector(unpack(bounds.center))) end
  -- The tiny invisible VPhysics handle is excluded from the Source/Bullet
  -- bridge. Custom ray tests select the actual native body, including limbs.
  self:SetCollisionBoundsWS(Vector(unpack(bounds.minimum))-Vector(3,3,3),Vector(unpack(bounds.maximum))+Vector(3,3,3))
 end
 self:NextThink(CurTime()) return true
end
function ENT:PreEntityCopy()
 if not mmdhl or not mmdhl.native then return end
 duplicator.StoreEntityModifier(self,'MMDHL',{asset=self:GetAsset(),options=self.MMDOptions,state=mmdhl.Decode(mmdhl.native.GetState(self:GetInstance())),angles=self.MMDAngles,flexScale=self:GetFlexScale()})
end
