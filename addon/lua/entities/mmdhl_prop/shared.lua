ENT.Type='anim'
ENT.Base='base_anim'
ENT.PrintName=mmdhl.Localize(mmdhl.L'props.entity_name')
ENT.Spawnable=false
ENT.RenderGroup=RENDERGROUP_BOTH
function ENT:SetupDataTables()
 self:NetworkVar('String',0,'AssetID')
 self:NetworkVar('Float',0,'PropScale')
end
