-- Native eye bones are the shared pose consumed by Source tools and the PMX skin.
local native=mmdhl.native
function mmdhl.GetBones(ent)
 local info=mmdhl.GetMetadata(ent).model
 return info and info.boneList or {}
end
local function angles(q)
 local x,y,z,w=unpack(q)
 return Angle(math.deg(math.asin(math.Clamp(2*(w*y-z*x),-1,1))),math.deg(math.atan2(2*(w*z+x*y),1-2*(y*y+z*z))),math.deg(math.atan2(2*(w*x+y*z),1-2*(x*x+y*y))))
end
if SERVER and not mmdhl.nativeEyeAdapter then
 mmdhl.nativeEyeAdapter=true
 local meta=FindMetaTable('Entity')
 local targetMethod,angleMethod,positionMethod=meta.SetEyeTarget,meta.ManipulateBoneAngles,meta.ManipulateBonePosition
 local function eye(ent,index)
  local rig=mmdhl.IsMMD(ent) and mmdhl.GetRig(ent)
  local b=rig and rig.bones[index+1]
  return b and (b.name=='Eye_L' or b.name=='Eye_R')
 end
 local function release(ent)
  for index in pairs(ent.MMDHLManagedEyes or {}) do angleMethod(ent,index,angle_zero) end
  ent.MMDHLManagedEyes=nil
 end
 meta.ManipulateBonePosition=function(ent,index,value,...)
  if eye(ent,index) then release(ent) ent.MMDHLEyeDriver='bones' ent.MMDHLExternalEyeTick=engine.TickCount() end
  return positionMethod(ent,index,value,...)
 end
 meta.ManipulateBoneAngles=function(ent,index,value,...)
  if eye(ent,index) then release(ent) ent.MMDHLEyeDriver='bones' ent.MMDHLExternalEyeTick=engine.TickCount() end
  return angleMethod(ent,index,value,...)
 end
 meta.SetEyeTarget=function(ent,target,...)
  local result=targetMethod(ent,target,...)
  if not mmdhl.IsMMD(ent) then return result end
  local rig=mmdhl.GetRig(ent) if not rig then return result end
  ent.MMDEyeTarget=Vector(target.x,target.y,target.z)
  if ent.MMDHLExternalEyeTick==engine.TickCount() then return result end
  ent.MMDHLEyeDriver='target'
  local reset=target==vector_origin
  -- Source takes attachment-local targets only for ragdolls. Living actors
  -- take world positions; interpreting a world point as local drives their
  -- iris bones to the angular clamp (often entirely behind the eyelids).
  if not reset and not ent:IsRagdoll() and ent:GetClass()~='mmdhl_ragdoll' then
   local live=ent:GetAttachment(ent:LookupAttachment('eyes'))
   if not live then release(ent) return result end
   target=WorldToLocal(target,angle_zero,live.Pos,live.Ang)
  end
  local head=rig.bones[7] local attachment=rig.eyesAttachment
  local origin,orientation=LocalToWorld(Vector(unpack(attachment.position)),angles(attachment.rotation),Vector(unpack(head.position)),angles(head.rotation))
  ent.MMDHLManagedEyes=ent.MMDHLManagedEyes or {}
  for i,bone in ipairs(rig.bones) do
   if bone.name~='Eye_L' and bone.name~='Eye_R' then continue end
   local rotation=angle_zero
   if not reset then
    local pos,ang=Vector(unpack(bone.position)),angles(bone.rotation)
    local offset=WorldToLocal(pos,angle_zero,origin,orientation)
    local delta=target-offset
    if delta:LengthSqr()>0.000001 then
     local gaze=delta:Angle()
     -- PMX pupils are usually a shallow surface rather than Source's shader
     -- eyeballs. Large rotations put that surface behind the face.
     gaze.p=math.Clamp(math.NormalizeAngle(gaze.p),-15,15) gaze.y=math.Clamp(math.NormalizeAngle(gaze.y),-20,20) gaze.r=0
     local direction=LocalToWorld(gaze:Forward(),angle_zero,vector_origin,orientation)
     local forward=WorldToLocal(orientation:Forward(),angle_zero,vector_origin,ang)
     local aim=WorldToLocal(direction,angle_zero,vector_origin,ang)
     local axis=forward:Cross(aim) local w=1+math.Clamp(forward:Dot(aim),-1,1)
     local length=math.sqrt(axis:LengthSqr()+w*w)
     if length>0.000001 then rotation=angles({axis.x/length,axis.y/length,axis.z/length,w/length}) end
    end
   end
   angleMethod(ent,i-1,rotation) ent.MMDHLManagedEyes[i-1]=true
  end
  return result
 end
end
if SERVER then
 local function expressionEyeCompatibility()
  local convert=ConvertRelativeToEyesAttachment
  if not isfunction(convert) or convert==mmdhl.EEEREyeConverter then return end
  local info=debug.getinfo(convert,'S')
  if not info or not string.find(info.short_src or '', 'ragdoll_pain_expression_transition_sv.lua',1,true) then return end
  -- EEER's EyePoser-derived helper special-cases NPCs but still converts
  -- player targets to local space. Correct just MMD players at that boundary.
  mmdhl.EEEREyeConverter=function(ent,pos)
   if IsValid(ent) and ent:IsPlayer() and mmdhl.IsMMD(ent) then return pos end
   return convert(ent,pos)
  end
  ConvertRelativeToEyesAttachment=mmdhl.EEEREyeConverter
 end
 hook.Add('InitPostEntity','MMDHL.ExpressionEyes',expressionEyeCompatibility)
 hook.Add('OnReloaded','MMDHL.ExpressionEyes',expressionEyeCompatibility)
 timer.Simple(0,expressionEyeCompatibility)
end
