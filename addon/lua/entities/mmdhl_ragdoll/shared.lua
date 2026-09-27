ENT.Type='anim'
ENT.Base='base_anim'
ENT.PrintName=mmdhl.Localize(mmdhl.L'entity.ragdoll')
ENT.Spawnable=false
ENT.RenderGroup=RENDERGROUP_BOTH
function ENT:SetupDataTables()
 self:NetworkVar('String',0,'Asset')
 self:NetworkVar('Int',0,'Instance')
 self:NetworkVar('Bool',0,'Frozen')
end
include('tool_api.lua')
