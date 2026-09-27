include('shared.lua')
local P=mmdhl.props
function ENT:Initialize() self:DrawShadow(false) self:SetSolid(SOLID_VPHYSICS) self:EnableCustomCollisions(true) if P and P.WatchCollision then P.WatchCollision(self) end end
function ENT:Think()
 if not P or not P.AcquireRender then return end
 local id=self:GetAssetID()
 if id~=self.MMDHLPropAsset then
  if self.MMDHLPropAsset then P.ReleaseRender(self.MMDHLPropAsset) end
  self.MMDHLPropAsset=id
  if P.ValidID(id) then P.AcquireRender(id) end
 end
 local info=P.Info[id] or P.CollisionInfo[id]
 local scale=P.ScaleOf(self)
 if info and (self.MMDHLPropBounds~=info or self.MMDHLPropBoundsScale~=scale) then
  self.MMDHLPropBounds=info self.MMDHLPropBoundsScale=scale
  self:SetRenderBounds(P.Vector(info.mins)*scale,P.Vector(info.maxs)*scale)
  self:SetCollisionBounds(P.Vector(info.mins)*scale,P.Vector(info.maxs)*scale)
 end
end
function ENT:Draw()
 if not P or not P.DrawAsset then return end
 if not P.DrawAsset(self:GetAssetID(),self:GetPos(),self:GetAngles(),false,self:GetColor(),P.ScaleOf(self),self) then
  -- Still loading or downloading: show where the prop is.
  render.SetColorMaterial() render.DrawWireframeBox(self:GetPos(),self:GetAngles(),self:OBBMins(),self:OBBMaxs(),Color(150,170,190),true)
 end
end
function ENT:DrawTranslucent() if P and P.DrawAsset then P.DrawAsset(self:GetAssetID(),self:GetPos(),self:GetAngles(),true,self:GetColor(),P.ScaleOf(self),self) end end
function ENT:OnRemove() if self.MMDHLPropAsset and P and P.ReleaseRender then P.ReleaseRender(self.MMDHLPropAsset) end end
-- Clients have no VPhysics body for this entity. Trace the same convex hulls
-- natively so the Physics Gun, tool gun and bullets hit the visible shape.
function ENT:TestCollision(start,delta,isbox,extents,mask)
 if not P or bit.band(mask,CONTENTS_SOLID)==0 then return end
 local localStart=self:WorldToLocal(start)
 local localDelta=self:WorldToLocal(start+delta)-localStart
 local function direction(v) return self:WorldToLocal(self:GetPos()+v) end
 local x,y,z=vector_origin,vector_origin,vector_origin
 if isbox then x=direction(Vector(extents.x,0,0)) y=direction(Vector(0,extents.y,0)) z=direction(Vector(0,0,extents.z)) end
 local fraction,normal=P.native.PropTrace(self:GetAssetID(),localStart,localDelta,x,y,z,P.ScaleOf(self))
 if not isnumber(fraction) then return end
 return {Fraction=fraction,HitPos=start+delta*fraction,Normal=(self:LocalToWorld(normal)-self:GetPos()):GetNormalized()}
end
