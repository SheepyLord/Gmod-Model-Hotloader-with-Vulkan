-- Owned-session acceptance probe. No hooks survive cleanup/map change.
local c=MMDHL_TEST_CONFIG
assert(c and MMDHL_DEBUG_TOKEN,'Owned test configuration required')
local base='mmd_hotloader/debug/'..MMDHL_DEBUG_TOKEN..'/'
local function write(name,data) file.Write(base..name,util.TableToJSON(data,true)) end
local function finite(n) return n==n and math.abs(n)<math.huge end
if SERVER then
 if MMDHL_STRESS then for _,e in ipairs(MMDHL_STRESS.entities or {}) do if IsValid(e) then e:Remove() end end end
 if IsValid(MMDHL_CARRIER_ENTITY) then MMDHL_CARRIER_ENTITY:Remove() end
 local p=player.GetHumans()[1] local origin=c.origin and Vector(unpack(c.origin)) or Vector(0,0,0)
 local ground=util.TraceLine({start=origin+Vector(0,0,2048),endpos=origin-Vector(0,0,30000),mask=MASK_SOLID_BRUSHONLY})
 origin=ground.HitPos+Vector(0,0,12)
 p:SetMoveType(MOVETYPE_NOCLIP) p:SetPos(origin+Vector(-300,0,100)) p:SetEyeAngles(Angle(10,0,0))
 MMDHL_STRESS={started=SysTime(),entities={},props={},errors={},origin={origin:Unpack()},cycles=0,maxPropVelocityError=0}
 local s=MMDHL_STRESS
 -- Time the addon's own server tick hooks; the harness cannot see engine internals.
 s.hookTimes={} s.hookTicks=0 s.wrapped={}
local function wrapHook(event,name,key)
 local fn=hook.GetTable()[event] and hook.GetTable()[event][name] if not fn then return end
 s.wrapped[#s.wrapped+1]={event,name,fn}
 hook.Add(event,name,function(...) local t=SysTime() local a,b,c=fn(...) s.hookTimes[key]=(s.hookTimes[key] or 0)+(SysTime()-t)*1000 return a,b,c end)
end
 wrapHook('Tick','MMDHL.NativePose','tickNativePoseMs') wrapHook('Tick','MMDHL.SecondaryScene','tickSceneMs') wrapHook('Tick','MMDHL.Simulation','tickSimulationMs')
 hook.Add('Tick','MMDHL.StressTickCount',function() s.hookTicks=s.hookTicks+1 end)
 local function spawn(index,offset)
  local asset=c.assets[index] local columns=math.min(5,#c.assets)
  local spacing=c.layout=='clustered' and 20 or 110
  local pos=origin+(c.grid and Vector(math.floor((index-1)/columns)*(c.layout=='clustered' and 25 or 150),((index-1)%columns-(columns-1)*.5)*spacing,0) or Vector(0,offset or 0,0))
  local ent=mmdhl.SpawnNative(p,asset,{backend='source',position={pos:Unpack()},scaleMultiplier=1,frozen=true,secondaryBackend=c.secondaryBackend or 'reference',secondaryCollision=c.secondaryCollision or 0})
  assert(IsValid(ent),'Native spawn failed') assert(ent:GetPhysicsObjectCount()==18)
  ent.MMDTestAnchor=ent:GetPhysicsObjectNum(0):GetPos() ent.MMDTestAngle=ent:GetPhysicsObjectNum(0):GetAngles()
  if c.mode~='cycles' and c.motion~='standing' then for i=1,17 do ent:GetPhysicsObjectNum(i):EnableMotion(true) ent:GetPhysicsObjectNum(i):Wake() end end
  return ent
 end
 if c.mode=='cycles' then
  local current
  timer.Create('MMDHL.StressCycles',1.2,c.cycles or 50,function()
   if IsValid(current) then current:Remove() end
   current=spawn(s.cycles%#c.assets+1,0) s.entities={current} s.cycles=s.cycles+1
  end)
 else for i in ipairs(c.assets) do s.entities[i]=spawn(i,(i-(#c.assets+1)*.5)*130) end end
 if c.contacts then
  for j,e in ipairs(s.entities) do
   local prop=ents.Create('prop_physics') prop:SetModel('models/hunter/plates/plate1x1.mdl') prop:SetPos(e:GetPos()+Vector(8,-9,40)) prop:SetAngles(Angle(90,0,0)) prop:Spawn()
   local ph=prop:GetPhysicsObject() ph:EnableGravity(false) ph:EnableDrag(false) ph:SetVelocity(Vector(0,1,0))
   for _,other in ipairs(s.entities) do for i=0,17 do constraint.NoCollide(other,prop,i,0) end end
   for _,other in ipairs(s.props) do constraint.NoCollide(other,prop,0,0) end
   s.props[#s.props+1]=prop
  end
 end
 hook.Add('Tick','MMDHL.StressMotion',function()
  local t=SysTime()-s.started
  for _,prop in ipairs(s.props) do if IsValid(prop) then prop:GetPhysicsObject():Wake() end end
  if c.mode=='cycles' or c.motion=='standing' then return end
  for j,e in ipairs(s.entities) do if IsValid(e) then
   local body=e:GetPhysicsObjectNum(0) local phase=t+j*1.7
   body:SetPos(e.MMDTestAnchor+Vector(math.sin(phase*.7)*24,math.sin(phase*.9)*16,18+math.sin(phase*1.1)*14))
   body:SetAngles(e.MMDTestAngle+Angle(math.sin(phase)*12,math.sin(phase*.6)*18,math.sin(phase*.8)*10))
   for i=1,17 do e:GetPhysicsObjectNum(i):Wake() end
  end end
 end)
 timer.Create('MMDHL.StressHeartbeat',1,0,function()
  local items={} for _,e in ipairs(s.entities) do if IsValid(e) then
   local d=mmdhl.GetDiagnostics(e,false) d.bodyList=nil d.entity=e:EntIndex() d.stopped=e.MMDStopped
   for i=0,17 do local body=e:GetPhysicsObjectNum(i) local pos=body:GetPos() local a=body:GetAngles() if not finite(pos.x+pos.y+pos.z+a.p+a.y+a.r) then table.insert(s.errors,'non-finite Source transform') end end
   if e.MMDStopped then table.insert(s.errors,tostring(e.MMDStopped)) end items[#items+1]=d
  end end
  for _,prop in ipairs(s.props) do if IsValid(prop) then s.maxPropVelocityError=math.max(s.maxPropVelocityError,(prop:GetPhysicsObject():GetVelocity()-Vector(0,1,0)):Length()) end end
  write(c.name..'-server.json',{elapsed=SysTime()-s.started,cycles=s.cycles,entities=items,errors=s.errors,origin=s.origin,maxPropVelocityError=s.maxPropVelocityError,ticks=s.hookTicks,hookMsPerTick=(function() local out={} for k,v in pairs(s.hookTimes) do out[k]=v/math.max(1,s.hookTicks) end return out end)()})
 end)
 return {origin=s.origin}
end
gui.HideGameUI() if IsValid(g_SpawnMenu) then g_SpawnMenu:Close() end
for _,name in ipairs({'MMDHL.TestCamera','MMDHL.VisualCamera','MMDHL.MotionCamera'}) do hook.Remove('CalcView',name) end
hook.Remove('HUDShouldDraw','MMDHL.VisualHUD') hook.Remove('PreDrawViewModel','MMDHL.VisualViewModel')
local origin=Vector(unpack(c.origin)) local center=origin+Vector(0,0,47)
local cameraCount=c.cameraCount or #c.assets
local camera=center+Vector(c.grid and cameraCount>3 and -650 or cameraCount>1 and -420 or -220,20,c.grid and cameraCount>3 and 160 or 30)
if c.cameraDistance then camera=center+Vector(-c.cameraDistance,0,0) end
hook.Add('CalcView','MMDHL.StressCamera',function() return {origin=camera,angles=(center-camera):Angle()+Angle(0,c.lookAway and 180 or 0,0),fov=cameraCount>1 and 60 or 38,drawviewer=false} end)
hook.Add('HUDShouldDraw','MMDHL.StressHUD',function(name) if name~='CHudChat' then return false end end)
hook.Add('PreDrawViewModel','MMDHL.StressViewModel',function() return true end)
local settings={} for _,n in ipairs({'mat_queue_mode','mat_vsync','mat_antialias','mat_aaquality','mat_picmip','mat_forceaniso','mat_hdr_level','mat_dxlevel','r_shadows','r_flashlightdepthtexture','r_waterforceexpensive','fps_max','gmod_mcore_test','r_3dsky','r_drawmodeldecals'}) do local cv=GetConVar(n) settings[n]=cv and cv:GetString() end
local addonFiles,addonFolders=file.Find('addons/*','MOD')
write(c.name..'-environment.json',{resolution={ScrW(),ScrH()},build=VERSIONSTR,branch=BRANCH,map=game.GetMap(),settings=settings,addons=engine.GetAddons(),folderAddonsPresent=addonFolders,addonArchivesPresent=addonFiles,native=mmdhl.Decode(mmdhl.native.GetCapabilities())})
local function histogram() return {buckets={},count=0,maximum=0} end
local function add(a,value) local bin=math.min(1000000,math.ceil(value*100)) a.buckets[bin]=(a.buckets[bin] or 0)+1 a.count=a.count+1 a.maximum=math.max(a.maximum,value) end
local function physicsState()
 local out={} for _,e in ipairs(mmdhl.Entities()) do local d,derr=mmdhl.GetDiagnostics(e,false) if not d and mmdhl.GetInstance(e)>0 then out[tostring(mmdhl.GetInstance(e))]={error=tostring(derr),asset=mmdhl.GetAsset(e)} end if d and mmdhl.GetInstance(e)>0 then local lod=mmdhl.GetPhysicsLOD and mmdhl.GetPhysicsLOD(e); out[tostring(mmdhl.GetInstance(e))]={asset=d.asset,ticks=d.ticks,lastSteps=d.lastSteps,stepMs=d.stepMs,poseMs=d.poseMs,sceneSyncMs=d.sceneSyncMs,sceneCaptureMs=d.sceneCaptureMs,surfaceGuardMs=d.surfaceGuardMs,stretchGuardMs=d.stretchGuardMs,sourceMirrors=d.sourceMirrors,externalContacts=d.externalContacts,worldContacts=d.worldContacts,objectContacts=d.objectContacts,maxJointDistance=d.maxJointDistance,physicsTotalMs=d.physicsTotalMs,tickTotalMs=d.tickTotalMs,guardTotalMs=d.guardTotalMs,tickCpuTotalMs=d.tickCpuTotalMs,resets=d.resets,dropped=d.droppedTime,inputTime=d.inputTime,simulationTime=d.simulationTime,debtSeconds=d.debtSeconds,broadphase=d.broadphase,broadphaseFallback=d.broadphaseFallback,secondaryBackend=d.secondaryBackend,secondaryBackendFallback=d.secondaryBackendFallback,asynchronous=d.asynchronous,lagMs=d.lagMs,sleepingBodies=d.sleepingBodies,compute=d.compute,solverIterations=d.solverIterations,bodies=d.bodies,joints=d.joints,authoredJoints=d.authoredJoints,quality=d.quality,physicsMs=d.physicsMs,lod=lod and {divisor=lod.divisor,suspended=lod.suspended,reason=lod.reason,distance=lod.distance,iterations=lod.iterations}} end end return out
end
local s={started=SysTime(),frames=histogram(),foregroundFrames=histogram(),foregroundSeconds=0,deform=histogram(),render=histogram(),unfocused=0,errors={},maxBounds=0,maxDepthPasses=0,maxExternalContacts=0,maxWorldContacts=0,maxObjectContacts=0,renderModes={},drawn={},drawnFrame=-1,minDrawn=math.huge,stages={}}
MMDHL_STRESS_CLIENT=s mmdhl.frameProfileForced=true
-- Diagnostic floor: the scene stays, native prepare/draw/shadow work is skipped.
mmdhl.suspendNative=c.suspend or nil if mmdhl.native.SetRenderSuspended then mmdhl.native.SetRenderSuspended(c.suspend and true or false) end
-- Lua runtime errors during the scene fail the functional gate; the harness cannot see the console.
hook.Add('OnLuaError','MMDHL.StressLuaErrors',function(message) s.errors['Lua error: '..tostring(message):sub(1,160)]=true end)
MMDHL_STRESS_CLIENT=s
-- Time the addon's own client hooks per frame.
s.hookTimes={} s.wrapped={}
local function wrapHook(event,name,key)
 local fn=hook.GetTable()[event] and hook.GetTable()[event][name] if not fn then return end
 s.wrapped[#s.wrapped+1]={event,name,fn}
 hook.Add(event,name,function(...) local t=SysTime() local a,b,c=fn(...) s.hookTimes[key]=(s.hookTimes[key] or 0)+(SysTime()-t)*1000 return a,b,c end)
end
wrapHook('Think','MMDHL.NativeVisuals','hookVisualsMs') wrapHook('PreRender','MMDHL.Shadows','hookPreRenderMs') wrapHook('Think','MMDHL.RenderMode','hookRenderModeMs') wrapHook('Think','MMDHL.LocalDebug','hookDebugMs')
MMDHL_ORIGINAL_DRAW=MMDHL_ORIGINAL_DRAW or mmdhl.DrawCarrier
mmdhl.DrawCarrier=function(ent,translucent,flags)
 if s.drawnFrame~=FrameNumber() then s.drawnFrame=FrameNumber() s.drawn={} end
 if IsValid(ent) and not translucent and bit.band(flags or 0,bit.bor(STUDIO_SHADOWDEPTHTEXTURE or 1073741824,STUDIO_SSAODEPTHTEXTURE or 536870912))==0 then s.drawn[ent:EntIndex()]=true end
 return MMDHL_ORIGINAL_DRAW(ent,translucent,flags)
end
-- Fixed 0.01 ms histogram bins avoid retaining one allocation per frame during
-- the leak test. Percentiles cover the entire run and round upward by <0.01 ms.
local function summary(a) if a.count==0 then return {} end local keys=table.GetKeys(a.buckets) table.sort(keys) local function q(p) local seen=0 for _,k in ipairs(keys) do seen=seen+a.buckets[k] if seen>=math.ceil(a.count*p) then return k/100 end end end return {samples=a.count,p50=q(.5),p95=q(.95),p99=q(.99),maximum=a.maximum} end
hook.Add('PostRender','MMDHL.StressFrames',function()
 local now=SysTime() local dt=s.previousFrame and (now-s.previousFrame)*1000 or 0 s.previousFrame=now local t=now-s.started
 local focused=system.HasFocus()
 if not focused then s.focusedSince=nil elseif not s.focusedSince then s.focusedSince=now end
 if t>=c.duration then if not s.physicsEnd then s.physicsEnd=physicsState() end s.finished=true return end
 if t<(c.warmup or 8) or s.finished then return end
 s.physicsStart=s.physicsStart or physicsState()
 local mode=tostring(GetConVar('mat_queue_mode'):GetInt()) s.renderModes[mode]=(s.renderModes[mode] or 0)+1
 add(s.frames,dt) add(s.deform,mmdhl.deformMs or 0)
 -- Keep every observed frame, and separately measure active gameplay. A full
 -- second after focus returns excludes the background-throttle transition.
 if focused and now-s.focusedSince>=1 then add(s.foregroundFrames,dt) s.foregroundSeconds=s.foregroundSeconds+dt/1000 end
 add(s.render,mmdhl.renderFrame==FrameNumber() and (mmdhl.renderMs or 0) or 0)
 s.minDrawn=math.min(s.minDrawn,table.Count(s.drawn))
 if mmdhl.frameProfile then
  if mmdhl.native.RenderFrameStats then table.Merge(mmdhl.frameProfile,mmdhl.Decode(mmdhl.native.RenderFrameStats()) or {}) end
  for name,value in pairs(mmdhl.frameProfile) do if isnumber(value) then s.stages[name]=s.stages[name] or histogram() add(s.stages[name],value) end end
  for key,value in pairs(s.hookTimes) do s.stages[key]=s.stages[key] or histogram() add(s.stages[key],value) end s.hookTimes={}
 end
 if not focused then s.unfocused=s.unfocused+1 end
 s.maxDepthPasses=math.max(s.maxDepthPasses,mmdhl.depthPasses or 0)
 if mmdhl.renderError then s.errors[tostring(mmdhl.renderError)]=true end
end)
timer.Create('MMDHL.StressClientHeartbeat',2,0,function()
 local t=SysTime()-s.started
 for _,e in ipairs(mmdhl.Entities()) do local b=mmdhl.Decode(mmdhl.native.GetBounds(mmdhl.GetInstance(e))) if b then
  local extent=(Vector(unpack(b.maximum))-Vector(unpack(b.minimum))):Length() s.maxBounds=math.max(s.maxBounds,extent)
  if not finite(extent) or extent>10000 then s.errors['Mesh bounds exploded']=true end
 end end
 for _,e in ipairs(mmdhl.Entities()) do local d=mmdhl.GetDiagnostics(e,false) if d then if t>=(c.warmup or 8) then s.maxExternalContacts=math.max(s.maxExternalContacts,d.externalContacts or 0) s.maxWorldContacts=math.max(s.maxWorldContacts,d.worldContacts or 0) s.maxObjectContacts=math.max(s.maxObjectContacts,d.objectContacts or 0) end if d.sourceError and d.sourceError~='' then s.errors[d.sourceError]=true end end end
 local stats=mmdhl.Decode(mmdhl.native.RenderStats())
 if stats.sourceShadowError and stats.sourceShadowError~='' then s.errors[stats.sourceShadowError]=true end
 s.firstUpload=s.firstUpload or stats.uploadedBytesTotal
 local stages={} for name,h in pairs(s.stages) do stages[name]=summary(h) end
 write(c.name..'-client.json',{elapsed=t,frames=summary(s.frames),foregroundFrames=summary(s.foregroundFrames),foregroundSeconds=s.foregroundSeconds,deformMs=summary(s.deform),renderMs=summary(s.render),renderModes=s.renderModes,stages=stages,physicsStart=s.physicsStart,physicsEnd=s.physicsEnd,minDrawn=s.minDrawn,unfocused=s.unfocused,errors=s.errors,maxBounds=s.maxBounds,maxDepthPasses=s.maxDepthPasses,maxExternalContacts=s.maxExternalContacts,maxWorldContacts=s.maxWorldContacts,maxObjectContacts=s.maxObjectContacts,renderer=stats,uploadedBytes=stats.uploadedBytesTotal-s.firstUpload,luaKB=collectgarbage('count'),finished=t>=c.duration})
 if t>=c.duration then s.finished=true end
end)
hook.Add('HUDPaint','MMDHL.StressOverlay',function()
 draw.SimpleText(c.name..' | '..ScrW()..' x '..ScrH()..' | '..string.format('%.1f ms',RealFrameTime()*1000),'DermaDefaultBold',20,20,color_white)
 draw.SimpleText(string.format('deformation %.2f ms | rendering %.2f ms | %d characters',mmdhl.deformMs or 0,mmdhl.renderMs or 0,#mmdhl.Entities()),'DermaDefault',20,40,color_white)
end)
return true
