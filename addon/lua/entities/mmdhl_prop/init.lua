AddCSLuaFile('shared.lua') AddCSLuaFile('cl_init.lua') include('shared.lua')
function ENT:Initialize()
 self:SetModel('models/hunter/blocks/cube025x025x025.mdl')
 self:SetUseType(SIMPLE_USE) self:DrawShadow(false)
 if self:GetPropScale()<=0 then self:SetPropScale(1) end
end
-- The server keeps the real VPhysics hulls; traces use them directly.
function ENT:TestCollision() return true end
function ENT:OnEntityCopyTableFinish(data)
 local phys=self:GetPhysicsObject()
 local scale=mmdhl.props and mmdhl.props.ScaleOf(self) or 1
 data.MMDHLProp={asset=self:GetAssetID(),scale=scale,mass=IsValid(phys) and phys:GetMass() or nil,frozen=IsValid(phys) and not phys:IsMotionEnabled() or false,
  collide=mmdhl.props and mmdhl.props.CollisionMode(self) or nil,gravity=self:GetNW2Bool('MMDHLGravity',true),
  surface=IsValid(phys) and phys:GetMaterial() or self.MMDHLSurface}
end
