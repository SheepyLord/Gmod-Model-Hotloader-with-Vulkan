-- Measure the frame in an existing scene (e.g. a loaded .gms save) without
-- spawning anything: frame times, MMD stage timings, and the per-frame cost of
-- every Lua hook of the profiled events (all addons). The camera is locked so
-- variants are comparable. Nothing survives completion or cleanup.
local c=MMDHL_TEST_CONFIG
assert(c and MMDHL_DEBUG_TOKEN and CLIENT,'Owned client test configuration required')
local base='mmd_hotloader/debug/'..MMDHL_DEBUG_TOKEN..'/'
local function write(name,data) file.Write(base..name,util.TableToJSON(data,true)) end
if MMDHL_SCENE_MEASURE and MMDHL_SCENE_MEASURE.cleanup then MMDHL_SCENE_MEASURE.cleanup() end
local s={started=SysTime(),frames={},hooks={},stages={},views={},drawCounts={},errors={},lua={},frameCount=0}
MMDHL_SCENE_MEASURE=s mmdhl.frameProfileForced=true
local function histogram() return {buckets={},count=0,maximum=0,sum=0} end
local function add(a,value) local bin=math.min(1000000,math.ceil(value*100)) a.buckets[bin]=(a.buckets[bin] or 0)+1 a.count=a.count+1 a.maximum=math.max(a.maximum,value) a.sum=a.sum+value end
local function summary(a) if not a or a.count==0 then return {} end local keys=table.GetKeys(a.buckets) table.sort(keys) local function q(p) local seen=0 for _,k in ipairs(keys) do seen=seen+a.buckets[k] if seen>=math.ceil(a.count*p) then return k/100 end end end return {samples=a.count,mean=a.sum/a.count,p50=q(.5),p95=q(.95),p99=q(.99),maximum=a.maximum} end
s.frameHistogram=histogram()
-- Camera lock at the save's player view.
if c.camera then
 local origin,angles=Vector(unpack(c.camera.origin)),Angle(unpack(c.camera.angles))
 hook.Add('CalcView','MMDHL.SceneMeasureCamera',function(ply,pos,ang,fov) return {origin=origin,angles=angles,fov=c.camera.fov or fov,drawviewer=false} end)
end
-- Wrap every hook of the profiled events. Totals are milliseconds per measured frame.
local events={'Think','Tick','PreRender','PostRender','RenderScene','PreDrawOpaqueRenderables','PostDrawOpaqueRenderables','PreDrawTranslucentRenderables','PostDrawTranslucentRenderables','PreDrawEffects','PostDrawEffects','PreDrawHalos','PreDrawHUD','PostDrawHUD','HUDPaint','HUDPaintBackground','DrawOverlay','RenderScreenspaceEffects','NeedsDepthPass','PreDrawViewModel','PostDrawViewModel','PrePlayerDraw','PostPlayerDraw','CreateMove','SetupWorldFog','SetupSkyboxFog','PreDrawSkyBox','PostDrawSkyBox','PostDraw2DSkyBox','GetMotionBlurValues','HUDShouldDraw','ShouldDrawLocalPlayer','CalcViewModelView','InputMouseApply','PlayerTick','FinishMove','Move','SetupMove','StartCommand','OnEntityCreated','NetworkEntityCreated','EntityEmitSound','EntityFireBullets','PlayerFootstep'}
s.wrapped={}
local current={}
if c.profileHooks~=false then
 for _,event in ipairs(events) do
  local list=hook.GetTable()[event]
  if list then for name,fn in pairs(list) do
   if name=='MMDHL.SceneMeasureCamera' or name=='MMDHL.SceneMeasureFrames' then continue end
   local key=event..' | '..tostring(name)
   s.wrapped[#s.wrapped+1]={event,name,fn}
   hook.Add(event,name,function(...) local t=SysTime() local a,b,cc,d,e,f=fn(...) current[key]=(current[key] or 0)+(SysTime()-t)*1000 return a,b,cc,d,e,f end)
  end end
 end
end
-- Time listed functions ("mmdhl.SyncActorMorphs", "mmdhl.native.PrepareFrame", ...);
-- inclusive milliseconds per measured frame, reported under "functions".
s.functionWraps={} s.functionTimes={}
local functionCurrent={}
for _,path in ipairs(c.wrap or {}) do
 local parent,key=_G,nil local parts=string.Explode('.',path)
 for i=1,#parts-1 do parent=parent and parent[parts[i]] end key=parts[#parts]
 local fn=istable(parent) and parent[key]
 if isfunction(fn) then
  s.functionWraps[#s.functionWraps+1]={parent,key,fn}
  parent[key]=function(...) local t=SysTime() local a,b,cc,d,e,f=fn(...) functionCurrent[path]=(functionCurrent[path] or 0)+(SysTime()-t)*1000 return a,b,cc,d,e,f end
 end
end
-- Count views that draw each MMD carrier per frame (colour, depth, flashlight...).
MMDHL_SCENE_ORIGINAL_DRAW=MMDHL_SCENE_ORIGINAL_DRAW or mmdhl.DrawCarrier
local frameDraws=0
mmdhl.DrawCarrier=function(ent,translucent,flags) frameDraws=frameDraws+1 return MMDHL_SCENE_ORIGINAL_DRAW(ent,translucent,flags) end
hook.Add('OnLuaError','MMDHL.SceneMeasureErrors',function(message) s.errors['Lua error: '..tostring(message):sub(1,160)]=true end)
function s.cleanup()
 for _,w in ipairs(s.wrapped or {}) do hook.Add(w[1],w[2],w[3]) end s.wrapped={}
 for _,w in ipairs(s.functionWraps or {}) do w[1][w[2]]=w[3] end s.functionWraps={}
 hook.Remove('CalcView','MMDHL.SceneMeasureCamera') hook.Remove('PostRender','MMDHL.SceneMeasureFrames') hook.Remove('OnLuaError','MMDHL.SceneMeasureErrors')
 if MMDHL_SCENE_ORIGINAL_DRAW then mmdhl.DrawCarrier=MMDHL_SCENE_ORIGINAL_DRAW MMDHL_SCENE_ORIGINAL_DRAW=nil end
 mmdhl.frameProfileForced=nil
end
local warmup,duration=c.warmup or 3,c.duration or 8
hook.Add('PostRender','MMDHL.SceneMeasureFrames',function()
 local now=SysTime() local dt=s.previous and (now-s.previous)*1000 or 0 s.previous=now
 local t=now-s.started
 if t<warmup or s.finished then current={} functionCurrent={} frameDraws=0 return end
 if not system.HasFocus() then s.unfocused=(s.unfocused or 0)+1 current={} functionCurrent={} frameDraws=0 return end
 s.frameCount=s.frameCount+1 add(s.frameHistogram,dt)
 for key,ms in pairs(current) do s.hooks[key]=(s.hooks[key] or 0)+ms end current={}
 for key,ms in pairs(functionCurrent) do s.functionTimes[key]=(s.functionTimes[key] or 0)+ms end functionCurrent={}
 s.drawCounts[frameDraws]=(s.drawCounts[frameDraws] or 0)+1 frameDraws=0
 local profile=mmdhl.frameProfile
 if profile and not mmdhl.suspendNative then
  local merged=table.Copy(profile)
  if mmdhl.native.RenderFrameStats then table.Merge(merged,mmdhl.Decode(mmdhl.native.RenderFrameStats()) or {}) end
  merged.luaDrawMs=mmdhl.renderFrame==FrameNumber() and (mmdhl.renderMs or 0) or 0
  merged.depthPasses=mmdhl.depthPasses or 0
  for name,value in pairs(merged) do if isnumber(value) then s.stages[name]=s.stages[name] or histogram() add(s.stages[name],value) end end
 end
 if s.frameCount%30==0 then s.lua[#s.lua+1]=collectgarbage('count')/1024 end
 if mmdhl.renderError then s.errors[tostring(mmdhl.renderError)]=true end
 if t>=warmup+duration then
  s.finished=true
  local hooks={} for key,ms in pairs(s.hooks) do hooks[#hooks+1]={hook=key,msPerFrame=ms/math.max(1,s.frameCount)} end
  table.sort(hooks,function(a,b) return a.msPerFrame>b.msPerFrame end)
  local stages={} for name,h in pairs(s.stages) do stages[name]=summary(h) end
  local functions={} for key,ms in pairs(s.functionTimes) do functions[key]=ms/math.max(1,s.frameCount) end
  local models=0 for _,e in ipairs(mmdhl.Entities()) do if mmdhl.GetInstance(e)>0 then models=models+1 end end
  local settings={} for _,n in ipairs({'mat_queue_mode','r_shadows','fps_max','mat_vsync','mmdhl_debug_overlay','mmdhl_secondary_iterations','mmdhl_secondary_stretch','mmdhl_lod_enabled','r_drawentities','r_drawworld','gmod_mcore_test'}) do local cv=GetConVar(n) settings[n]=cv and cv:GetString() end
  local result={name=c.name,variant=c.variant,frames=summary(s.frameHistogram),frameCount=s.frameCount,unfocused=s.unfocused or 0,stages=stages,hooks=hooks,functions=functions,drawsPerFrame=s.drawCounts,errors=s.errors,luaMB=s.lua,models=models,resolution={ScrW(),ScrH()},settings=settings,map=game.GetMap()}
  s.cleanup()
  write(c.name..'-scene.json',result)
 end
end)
return true
