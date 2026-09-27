-- Scripted full-rig ground sandwich, lift, and independent pelvis motion.
assert(MMDHL_DEBUG_TOKEN,'This probe requires an owned test session')
local c=MMDHL_MOTION_CONFIG
if SERVER then
 for _,e in ipairs(mmdhl.Entities()) do e:Remove() end
 local tr=util.TraceLine({start=Vector(822,-800,2048),endpos=Vector(822,-800,-30000),mask=MASK_SOLID_BRUSHONLY})
 local origin=tr.HitPos+Vector(0,0,4)
 local e=mmdhl.SpawnNative(player.GetHumans()[1],c.asset,{position={origin:Unpack()},angles={0,0,0},frozen=true,secondaryBackend=c.backend,secondaryCollision=2})
 assert(IsValid(e),'Spawn failed')
 assert(e:GetPhysicsObjectCount()==18,'Native ragdoll body count changed')
 local poses={} local armPivot
 for i=0,17 do
  local ph=e:GetPhysicsObjectNum(i) local name=e:GetBoneName(e:TranslatePhysBoneToBone(i))
  poses[i]={pos=ph:GetPos()-origin,ang=ph:GetAngles(),arm=name=='ValveBiped.Bip01_L_UpperArm' or name=='ValveBiped.Bip01_L_Forearm' or name=='ValveBiped.Bip01_L_Hand'}
  if name=='ValveBiped.Bip01_L_UpperArm' then armPivot=poses[i].pos end
 end
 if c.scenario=='sleeve' then assert(armPivot,'Left arm mapping missing') end
 local start=CurTime()+6
 MMDHL_MOTION={ent=e,origin=origin,start=start,poses=poses}
 player.GetHumans()[1]:SetMoveType(MOVETYPE_NOCLIP)
 player.GetHumans()[1]:SetPos(origin+Vector(-140,80,60))
 hook.Add('Tick','MMDHL.MotionBugProbe',function()
  if not IsValid(e) then return end
  local t=CurTime()-start
  local angle,offset=angle_zero,Vector()
  if c.scenario=='sandwich' then
   local alpha=math.Clamp(t/2,0,1)
   if t<4 then angle=Angle(-90*alpha,0,0) offset=Vector(0,0,4*alpha)
   elseif t<6 then angle=Angle(-90,0,0) offset=Vector(0,0,4+(t-4)*30)
   else angle=Angle(-90,0,0) offset=Vector(0,0,64) end
  elseif c.scenario=='carry' or c.scenario=='sleeve' then offset=Vector(0,math.Clamp(t,0,5)*c.speed,30)
  end
  if c.scenario=='carry' or c.scenario=='sleeve' then player.GetHumans()[1]:SetPos(origin+offset+Vector(-140,80,60)) end
  for i=0,17 do
   local pos,ang=LocalToWorld(poses[i].pos,poses[i].ang,origin+offset,angle)
   if c.scenario=='sleeve' and poses[i].arm and t>=0 then
    pos,ang=LocalToWorld(poses[i].pos-armPivot,poses[i].ang,origin+offset+armPivot,Angle(0,0,math.sin(t*8)*46))
   end
   if c.scenario=='pelvis' and i==0 and t>=0 then pos=pos+Vector(math.sin(t*2)*8,0,0) end
   local ph=e:GetPhysicsObjectNum(i) ph:SetPos(pos) ph:SetAngles(ang)
  end
 end)
 return {entity=e:EntIndex(),origin={origin:Unpack()},start=start}
end
local e=Entity(c.entity) local origin=Vector(unpack(c.origin)) local base='mmd_hotloader/debug/'..MMDHL_DEBUG_TOKEN..'/'
local samples,frames={},{} local last=-1 local screenshots={} local finished=false
local drawnFrame=-1 MMDHL_MOTION_ORIGINAL_DRAW=mmdhl.DrawCarrier
mmdhl.DrawCarrier=function(ent,...)
 if ent==e then drawnFrame=FrameNumber() end
 return MMDHL_MOTION_ORIGINAL_DRAW(ent,...)
end
gui.HideGameUI() if IsValid(g_SpawnMenu) then g_SpawnMenu:Close() end
hook.Add('CalcView','MMDHL.MotionBugCamera',function()
 local moving=c.scenario=='carry' or c.scenario=='sleeve'
 local follow=moving and Vector(0,math.Clamp(CurTime()-c.start,0,5)*c.speed,0) or Vector()
 local center=origin+follow+Vector(-8,0,moving and 70 or 40) local camera=center+Vector(-135,100,32)
 return {origin=camera,angles=(center-camera):Angle(),fov=46,drawviewer=false}
end)
hook.Add('PreDrawViewModel','MMDHL.MotionBugHideGun',function() return true end)
hook.Add('HUDShouldDraw','MMDHL.MotionBugHideHUD',function() return false end)
hook.Add('PostRender','MMDHL.MotionBugSample',function()
 local t=CurTime()-c.start
 if t<0 or finished then return end
 frames[#frames+1]={ms=RealFrameTime()*1000,focus=system.HasFocus(),drawn=drawnFrame==FrameNumber()}
 if t-last>=.1 then
  last=t local d=mmdhl.GetDiagnostics(e,true)
  if d then
   local minZ=math.huge local below=0 local head=-1 local headError,headBodies=0,0
   local rig=mmdhl.GetRig(e) for _,bone in ipairs(rig.bones) do if bone.name=='ValveBiped.Bip01_Head1' then head=bone.mmd end end
   for _,b in ipairs(d.bodyList) do if not b.follower then
    local z=b.worldPosition[3]-(origin.z-4) minZ=math.min(minZ,z) if z<-.2 then below=below+1 end
    if head>=0 and b.presentationAnchorBone==head then headBodies=headBodies+1 headError=math.max(headError,Vector(unpack(b.presentationCompensation or {0,0,0})):Length()*rig.scale) end
   end end
   if d.maxLinearLimitBodies then d.worstBodies={} for _,index in ipairs(d.maxLinearLimitBodies) do if index>=0 then d.worstBodies[#d.worstBodies+1]=d.bodyList[index+1] end end end
   local lead,count=0,0 assert(d.attachmentJoints,'Install the attachment-diagnostics binary before running this probe')
   for _,j in ipairs(d.attachmentJoints) do if j.lockedLinear then lead=lead+j.displayOffsetSource[2] count=count+1 end end
   d.attachmentLead=count>0 and lead/count or 0 d.attachmentCount=count
   d.sampleTime=t d.minHeight=minZ d.belowFloor=below d.headPresentationError=headError d.headBodies=headBodies d.bodyList=nil
   samples[#samples+1]=d
  end
 end
 for _,when in ipairs({3.8,7,9.8}) do if t>=when and not screenshots[when] then
  screenshots[when]=true
  file.Write(base..c.label..'-'..when..'.jpg',render.Capture({format='jpeg',quality=90,x=0,y=0,w=ScrW(),h=ScrH()}))
 end end
 -- Serializing a growing detailed report every rendered frame perturbs the
 -- very physics timing under test. Write once, after the measured interval.
 if t>=10 then finished=true file.Write(base..c.label..'.json',util.TableToJSON({samples=samples,frames=frames,finished=true,entity=c.entity,resolution={ScrW(),ScrH()}},false)) end
end)
return true
