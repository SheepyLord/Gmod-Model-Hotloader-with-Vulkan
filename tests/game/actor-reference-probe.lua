-- Set MMDHLReferenceEntity to a generated actor in a frozen Reference pose.
local ent=isentity(MMDHLReferenceEntity) and MMDHLReferenceEntity or Entity(MMDHLReferenceEntity)
local rig=mmdhl.GetRig(ent) assert(rig,'Missing reference probe rig')
local function rotation(q)
 local x,y,z,w=unpack(q)
 return Angle(math.deg(math.asin(math.Clamp(2*(w*y-z*x),-1,1))),math.deg(math.atan2(2*(w*z+x*y),1-2*(y*y+z*z))),math.deg(math.atan2(2*(w*x+y*z),1-2*(x*x+y*y))))
end
ent:InvalidateBoneCache() ent:SetupBones()
local world=Matrix() world:SetTranslation(ent:GetPos()) world:SetAngles(ent:GetAngles())
local turn=Matrix() turn:SetAngles(Angle(0,rig.referenceYaw or 0,0))
local result={model=ent:GetModel(),sequence=ent:GetSequenceName(ent:GetSequence()),maxPosition=0,maxRotationDegrees=0,bones={}}
for _,body in ipairs(rig.bodies)do
 local b=rig.bones[body.bone+1] local rest=Matrix()
 rest:SetTranslation(Vector(unpack(b.position))) rest:SetAngles(rotation(b.rotation))
 local expected=world*turn*rest local actual=ent:GetBoneMatrix(body.bone)
 local position=expected:GetTranslation():Distance(actual:GetTranslation())
 local trace=0 for r=1,3 do for c=1,3 do trace=trace+expected:GetField(r,c)*actual:GetField(r,c) end end
 local angle=math.deg(math.acos(math.Clamp((trace-1)/2,-1,1)))
 result.maxPosition=math.max(result.maxPosition,position) result.maxRotationDegrees=math.max(result.maxRotationDegrees,angle)
 result.bones[#result.bones+1]={name=b.name,position=position,rotationDegrees=angle}
end
return result
