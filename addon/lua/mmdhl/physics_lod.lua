-- Client-local quality decisions. Native Source motion/AI is never suspended.
local definitions={
 {'enabled','1',0,1}, {'near','300',0,10000}, {'full','1000',0,20000},
 {'middle','2000',0,30000}, {'cutoff','4000',1,50000},
 {'hidden_seconds','2',0,30}, {'speed','120',1,2000}, {'angular_speed','90',1,1440}
}
for _,d in ipairs(definitions) do
 local name='mmdhl_lod_'..d[1]
 CreateClientConVar(name,d[2],true,false,'Adaptive character model physics '..d[1],d[3],d[4])
 mmdhl.PhysicsSettingDefaults[name]=d[2]
end
function mmdhl.PhysicsLODPolicy(input,settings,previous,wasSuspended)
 if input.firstPerson then return 1,true,'first-person player' end
 if not settings.enabled or input.protected or input.fast then return 1,false,'full priority' end
 local distance=input.distance
 -- Leave a tier only after crossing its boundary by 5%; promotions are immediate.
 local near=settings.near local full=settings.full local middle=settings.middle local cutoff=settings.cutoff
 if previous==1 then near=near*1.05 full=full*1.05 elseif previous==2 then middle=middle*1.05 end
 if wasSuspended==false then cutoff=cutoff*1.05 end
 if distance<=near then return 1,false,'near' end
 if input.visible and distance<=full then return 1,false,'visible near' end
 if (not input.visible and input.hiddenFor>=settings.hiddenSeconds) or distance>cutoff then return 4,true,distance>cutoff and 'distance' or 'not visible' end
 if distance<=middle then return 2,false,'reduced' end
 return 4,false,'distant'
end
local probeFrame,probeBudget=-1,0
local function visibility(ent,state,camera,distance,now)
 if ent:IsDormant() then return false end -- outside the receiving client's PVS
 local frame=FrameNumber() if frame~=probeFrame then probeFrame=frame probeBudget=6 end
 local center=ent:WorldSpaceCenter() local radius=ent:BoundingRadius()
 local direction=center-camera.origin
 if distance>radius and direction:GetNormalized():Dot(camera.angles:Forward())<math.cos(math.rad(math.min(175,camera.fov*.8+math.deg(math.atan2(radius,math.max(1,distance)))))) then return false end
 if now<(state.probeAt or 0) or probeBudget<3 then return state.visible~=false end
 probeBudget=probeBudget-3 state.probeAt=now+.2
 local filter={LocalPlayer(),ent,ent.MMDHLVisual}
 for _,offset in ipairs({vector_origin,Vector(0,0,radius*.5),Vector(0,0,-radius*.5)}) do
  local trace=util.TraceLine({start=camera.origin,endpos=center+offset,filter=filter,mask=MASK_VISIBLE})
  if not trace.Hit or trace.Fraction>=.99 then return true end
 end
 return false
end
local settingsFrame,settingsCache
local function settings()
 local frame=FrameNumber() if settingsFrame==frame then return settingsCache end
 settingsFrame=frame
 local function n(key) return GetConVar('mmdhl_lod_'..key):GetFloat() end
 local near=n('near') local full=math.max(near,n('full')) local middle=math.max(full,n('middle'))
 settingsCache={enabled=n('enabled')~=0,near=near,full=full,middle=middle,cutoff=math.max(middle,n('cutoff')),hiddenSeconds=n('hidden_seconds'),speed=n('speed'),angular=n('angular_speed')}
 return settingsCache
end
function mmdhl.GetPhysicsLOD(ent) return ent.MMDPhysicsLOD end
local iterationsVar
local cameraFrame,cameraCache
local function camera()
 local frame=FrameNumber() if cameraFrame==frame then return cameraCache end
 local view=render.GetViewSetup and render.GetViewSetup() or {}
 view.origin=view.origin or EyePos() view.angles=view.angles or EyeAngles()
 view.fov=view.fov or (IsValid(LocalPlayer()) and LocalPlayer():GetFOV() or 90)
 cameraFrame=frame cameraCache=view return view
end
function mmdhl.UpdatePhysicsLOD(ent,palette)
 local now=RealTime() local state=ent.MMDPhysicsLOD or {lastVisible=now,divisor=1}
 local config=settings() local delta=now-(state.sampleAt or now)
 -- A mirror showing the player in first person needs the hair and clothing moving.
 local firstPerson=mmdhl.HiddenFirstPerson and mmdhl.HiddenFirstPerson(ent)
 local corpseSleeping=mmdhl.ClientRagdollAsleep and mmdhl.ClientRagdollAsleep(ent)
 iterationsVar=iterationsVar or GetConVar('mmdhl_secondary_iterations') local full=iterationsVar:GetInt()
 local forcePause=firstPerson==true or full<0 or corpseSleeping==true
 if forcePause or not config.enabled then
  local signature='1:'..tostring(forcePause)
  if state.applied~=signature then
   local ok,err=mmdhl.native.SetSecondaryQuality(mmdhl.GetInstance(ent),1,forcePause)
   if ok then state.applied=signature else mmdhl.renderError=err end
  end
  state.divisor=1 state.suspended=forcePause state.suspendedAt=forcePause and (state.suspendedAt or now) or nil
  state.suspendedSeconds=state.suspendedAt and now-state.suspendedAt or 0
  state.reason=firstPerson and 'first-person player' or full<0 and 'physics disabled' or corpseSleeping and 'sleeping client ragdoll' or 'adaptive disabled' state.iterations=forcePause and 0 or full
  state.bones=nil state.sampleAt=now state.lastVisible=now
  ent.MMDPhysicsLOD=state return
 end
 local fast=false local speed,angular=0,0
 local rig=mmdhl.GetRig(ent)
 state.bones=state.bones or {}
 -- The palette holds bone-to-world matrices (palette[bone+1]); only body bones are read.
 for _,body in ipairs(rig and rig.bodies or {}) do
  local index=body.bone local matrix=palette[index+1]
  if matrix then
   local p,a=matrix:GetTranslation(),matrix:GetAngles()
   local old=state.bones[index]
   if old and delta>0 and delta<.5 then
    speed=math.max(speed,p:Distance(old.pos)/delta)
    angular=math.max(angular,math.abs(math.AngleDifference(a.p,old.ang.p))/delta,math.abs(math.AngleDifference(a.y,old.ang.y))/delta,math.abs(math.AngleDifference(a.r,old.ang.r))/delta)
   end
   if old then old.pos=p old.ang=a else state.bones[index]={pos=p,ang=a} end
  end
 end
 if speed>=config.speed or angular>=config.angular or ent:GetNW2Bool('MMDHLPhysgunHeld',false) then state.fullUntil=now+.5 end
 fast=now<(state.fullUntil or 0)
 local view=camera()
 local distance=ent:NearestPoint(view.origin):Distance(view.origin)
 state.visible=visibility(ent,state,view,distance,now)
 if state.visible then state.lastVisible=now end
 local protected=ent==LocalPlayer() or hook.Run('MMDHLFullPhysics',ent)==true
 local divisor,paused,reason=mmdhl.PhysicsLODPolicy({distance=distance,visible=state.visible,hiddenFor=now-state.lastVisible,protected=protected,fast=fast},config,state.divisor,state.suspended)
 local signature=divisor..':'..tostring(paused)
 if state.applied~=signature then
  local result,err=mmdhl.native.SetSecondaryQuality(mmdhl.GetInstance(ent),divisor,paused)
  if result then state.applied=signature else mmdhl.renderError=err end
 end
 state.suspendedAt=paused and (state.suspendedAt or now) or nil
 state.suspendedSeconds=state.suspendedAt and now-state.suspendedAt or 0
 state.distance=distance state.divisor=divisor state.suspended=paused state.reason=reason
 state.speed=speed state.angularSpeed=angular state.sampleAt=now
 state.mode=full==0 and 'jiggle' or 'full'
 state.iterations=paused and 0 or math.min(full,math.max(2,math.ceil(full/divisor)))
 ent.MMDPhysicsLOD=state
end
