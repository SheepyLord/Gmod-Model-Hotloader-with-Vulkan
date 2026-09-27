assert(SERVER and MMDHL_DEBUG_TOKEN,'Owned server session required')
local p=player.GetHumans()[1]
local source=MMDCompatEnt or mmdhl.testEnt
assert(IsValid(source) and mmdhl.IsMMD(source),'Spawn the test MMD ragdoll first')
local result={}
source:SetFlexWeight(0,.42) source:SetFlexScale(.8)
source:SetSubMaterial(35,'models/wireframe') source:SetBodygroup(43,1)
local finger=source:LookupBone('ValveBiped.Bip01_L_Finger1')
source:ManipulateBoneAngles(finger,Angle(27,0,0))
source:GetPhysicsObjectNum(7):EnableMotion(false)
local data=duplicator.CopyEntTable(source)
assert(data.EntityMods.MMDHLNative.version==4,'MMD state did not use the versioned capture')
assert(data.MMDOptions==nil and data.MMDHLClientInstance==nil,'Runtime state leaked into copy')
local copy=duplicator.Copy(source)
local pasted=duplicator.Paste(p,copy.Entities,copy.Constraints)
local clone=pasted[source:EntIndex()]
assert(IsValid(clone) and mmdhl.IsMMD(clone),'Duplicate lost its MMD identity')
assert(clone:GetPhysicsObjectCount()==18,'Duplicate lost native physics')
assert(math.abs(clone:GetFlexWeight(0)-.42)<.001 and math.abs(clone:GetFlexScale()-.8)<.001,'Morph state lost')
assert(clone:GetSubMaterial(35)=='models/wireframe' and clone:GetBodygroup(43)==1,'Overflow appearance lost')
assert(math.abs(clone:GetManipulateBoneAngles(finger).p-27)<.001,'Finger pose lost')
assert(not clone:GetPhysicsObjectNum(7):IsMotionEnabled(),'Per-limb freeze lost')
result.duplicate=true
clone:Remove()
-- Clearing a modifier must not merge the previous nonempty override back in.
source:SetSubMaterial(35,'') source:SetBodygroup(43,0)
data=duplicator.CopyEntTable(source)
assert(next(data.EntityMods.MMDHLNative.materials.overrides)==nil,'Cleared override resurrected')
assert(next(data.EntityMods.MMDHLNative.materials.groups)==nil,'Cleared bodygroup resurrected')
result.resetAppearance=true
-- Model-only copies use the same real class and physics; metadata attaches on
-- the next creation boundary rather than requiring the importer spawn factory.
local plain=ents.Create('prop_ragdoll')
plain:SetModel(source:GetModel()) plain:SetPos(source:GetPos()+Vector(0,160,0)) plain:Spawn()
for i=0,plain:GetPhysicsObjectCount()-1 do plain:GetPhysicsObjectNum(i):EnableMotion(false) end
MMDCompatPlain=plain
-- Keep an exact serialized copy for the separate cold-start/saves phase.
file.Write('mmd_hotloader/debug/persistence-copy.json',util.TableToJSON(copy))
MMDPersistenceProbe=result
return {warm=result,plain=plain:EntIndex(),source=source:EntIndex()}
