-- Records a static prop attached to a bone as a duplicator constraint, so
-- copying the target also copies the prop and re-attaches it on paste.
AddCSLuaFile()
ENT.Type='point'
ENT.PrintName=mmdhl.Localize(mmdhl.L'entity.attach_link')
ENT.Spawnable=false
function ENT:UpdateTransmitState() return TRANSMIT_NEVER end
