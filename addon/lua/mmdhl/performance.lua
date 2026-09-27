-- Opt-in user telemetry; independent of the developer RPC/debug-session gate.
local overlay=CreateClientConVar('mmdhl_debug_overlay','0',true,false,'Show Model Hotloader performance HUD',0,1)
local printing=CreateClientConVar('mmdhl_debug_print','0',true,false,'Print Model Hotloader performance every five seconds',0,1)
local native=mmdhl.native
local frames={} local cursor=0 local lastFrame,lastTime,nextSummary,nextPrint
local uploads,uploadTime
local metrics={'prepareWallMs','physicsWorkMs','poseWorkMs','deformWorkMs','sceneWorkMs','captureMs','renderMs','asyncLagMs','asyncWorlds','sleepingBodies','nativeRenderMs','shadowMs','uploadMs','drawBinds','drawCalls'}
mmdhl.performance={polls=0}
local state=mmdhl.performance
local function enabled() return overlay:GetBool() or printing:GetBool() end
local function reset()
 frames={} cursor=0 lastFrame=nil lastTime=nil nextSummary=nil nextPrint=nil uploads=nil uploadTime=nil state.summary=nil
end
local function changed() reset() end
cvars.AddChangeCallback('mmdhl_debug_overlay',changed,'MMDHL.Performance')
cvars.AddChangeCallback('mmdhl_debug_print',changed,'MMDHL.Performance')
local function summarize(now)
 local result={models=0,nativeBodies=0,bodies=0,joints=0,contacts=0,mirrors=0,resets=0,dropped=0,debt=0,backends={},errors={},frames=0,frameMs=0,p50=0,p95=0,max=0,fps=0,graph={},luaMB=collectgarbage('count')/1024}
 local times={} local samples={}
 for _,sample in pairs(frames) do if now-sample.time<=5 then samples[#samples+1]=sample end end
 table.sort(samples,function(a,b) return a.time<b.time end)
 for _,key in ipairs(metrics) do result[key]=0 end
 for _,sample in ipairs(samples) do
  times[#times+1]=sample.ms result.frameMs=result.frameMs+sample.ms
  result.graph[#result.graph+1]=sample.ms
  for _,key in ipairs(metrics) do result[key]=result[key]+(sample[key] or 0) end
 end
 result.frames=#times
 if #times>0 then
  table.sort(times) result.frameMs=result.frameMs/#times result.fps=1000/math.max(.001,result.frameMs)
  result.p50=times[math.ceil(#times*.5)] result.p95=times[math.ceil(#times*.95)] result.max=times[#times]
  for _,key in ipairs(metrics) do result[key]=result[key]/#times end
 end
 local profile=mmdhl.frameProfile or {} result.workers=profile.workers or 0
 result.lodFull=0 result.lodReduced=0 result.lodSuspended=0 result.lodReasons={}
 for _,ent in ipairs(mmdhl.Entities()) do
  if not ent.MMDHLPreviewRig and mmdhl.GetInstance(ent)>0 then
   result.models=result.models+1 result.nativeBodies=result.nativeBodies+ent:GetNW2Int('MMDHLNativeBodyCount',ent:GetPhysicsObjectCount())
   local lod=mmdhl.GetPhysicsLOD and mmdhl.GetPhysicsLOD(ent)
   if lod then
    local key=lod.suspended and 'lodSuspended' or lod.divisor==1 and 'lodFull' or 'lodReduced'
    result[key]=result[key]+1 result.lodReasons[ent:EntIndex()]={reason=lod.reason,iterations=lod.iterations,seconds=lod.suspendedSeconds,distance=lod.distance}
   else result.lodFull=result.lodFull+1 end
   local d,err=mmdhl.GetDiagnostics(ent,false)
   if d then
    result.bodies=result.bodies+(d.bodies or 0) result.joints=result.joints+(d.joints or 0)
    result.contacts=result.contacts+(d.externalContacts or 0) result.mirrors=result.mirrors+(d.sourceMirrors or 0)
    result.resets=result.resets+(d.resets or 0) result.dropped=result.dropped+(d.droppedTime or 0)
    result.debt=math.max(result.debt,d.debtSeconds or 0)
    local backend=d.secondaryBackend or 'unknown' result.backends[backend]=(result.backends[backend] or 0)+1
    if d.secondaryBackendFallback and d.secondaryBackendFallback~='' then result.errors[d.secondaryBackendFallback]=true end
    if d.sourceError and d.sourceError~='' then result.errors[d.sourceError]=true end
   elseif err then result.errors[tostring(err)]=true end
  end
 end
 local stats=mmdhl.Decode(native.RenderStats()) or {}
 if mmdhl.renderInitError then result.errors[mmdhl.renderInitError]=true end
 if mmdhl.renderError then result.errors[mmdhl.renderError]=true end
 if stats.sourceShadowError and stats.sourceShadowError~='' then result.errors[stats.sourceShadowError]=true end
 result.cacheMB=(stats.cachedBytes or 0)/1048576 result.uploadMBps=0
 if uploads and now>uploadTime then result.uploadMBps=math.max(0,(stats.uploadedBytesTotal or 0)-uploads)/(now-uploadTime)/1048576 end
 uploads=stats.uploadedBytesTotal or 0 uploadTime=now
 state.polls=state.polls+1 state.summary=result return result
end
local function report(s)
 print(string.format('[Model Hotloader] %d models | %d Source / %d character model bodies, %d joints | %.1f FPS | frame p50/p95/max %.2f/%.2f/%.2f ms | prepare %.2f ms | worker physics %.2f ms | %d external contacts | debt %.3fs | dropped %.3fs',s.models,s.nativeBodies,s.bodies,s.joints,s.fps,s.p50,s.p95,s.max,s.prepareWallMs,s.physicsWorkMs,s.contacts,s.debt,s.dropped))
end
hook.Add('PostRender','MMDHL.Performance',function()
 if not enabled() then return end
 if mmdhl.RequestFrameProfile then mmdhl.RequestFrameProfile(1) end
 local frame=FrameNumber() if frame==lastFrame then return end lastFrame=frame
 local now=SysTime()
 if lastTime then
  cursor=cursor%4096+1
  local sample={time=now,ms=(now-lastTime)*1000}
  local profile=mmdhl.frameProfile or {}
  local render=native.RenderFrameStats and mmdhl.Decode(native.RenderFrameStats()) or {}
  for _,key in ipairs(metrics) do sample[key]=profile[key] or render[key] or 0 end
  sample.renderMs=mmdhl.renderFrame==frame and (mmdhl.renderMs or 0) or 0
  frames[cursor]=sample
 end
 lastTime=now
 if not nextSummary or now>=nextSummary then summarize(now) nextSummary=now+.5 end
 if printing:GetBool() and (not nextPrint or now>=nextPrint) then report(state.summary) nextPrint=now+5 end
end)
concommand.Add('mmdhl_debug_toggle',function() overlay:SetBool(not overlay:GetBool()) end)
concommand.Add('mmdhl_debug_report',function()
 local s=summarize(SysTime()) report(s)
 file.CreateDir('mmd_hotloader/diagnostics')
 local path='mmd_hotloader/diagnostics/performance-'..os.time()..'.json'
 local data=table.Copy(s) data.graph=nil data.map=game.GetMap() data.resolution={ScrW(),ScrH()} data.globalSettings=mmdhl.GetGlobalSettings()
 file.Write(path,util.TableToJSON(data,true)) print('[Model Hotloader] Saved data/'..path..(s.frames==0 and ' (enable the overlay to collect frame timings)' or ''))
end)
local fontScale
local function font(scale)
 if fontScale~=scale then surface.CreateFont('MMDHL.Performance',{font='Segoe UI',size=math.floor(14*scale),weight=500,extended=true}) fontScale=scale end
 return 'MMDHL.Performance'
end
local white=Color(235,240,248) local green=Color(110,225,165) local orange=Color(255,185,100)
-- Keep explicitly enabled diagnostics visible when a camera/tool hides the game HUD.
hook.Remove('HUDPaint','MMDHL.Performance')
hook.Add('PostDrawHUD','MMDHL.Performance',function()
 if not overlay:GetBool() then return end
 local s=state.summary if not s then return end
 local scale=math.Clamp(ScrH()/1080,1,1.5) local w=math.min(ScrW()-32,520*scale) local x=ScrW()-w-16 local y=30
 local face=font(scale)
 local line=21*scale local height=line*((s.asyncWorlds or 0)>0 and 15 or 14)+74
 draw.RoundedBox(8,x,y,w,height,Color(12,19,30,225))
 local function text(value,color) draw.SimpleText(value,face,x+12,y+10,color or white) y=y+line end
 text('Model Hotloader performance — rolling 5s ('..s.frames..' frames)',green)
 text(string.format('%.1f FPS   frame p50 %.2f / p95 %.2f / max %.2f ms',s.fps,s.p50,s.p95,s.max),s.p95>16.7 and orange or green)
 text(string.format('%d models   %d Source bodies   %d character model bodies / %d joints',s.models,s.nativeBodies,s.bodies,s.joints))
 local backends={} for key,n in SortedPairs(s.backends) do backends[#backends+1]=key..' '..n end
 text('Backends: '..(#backends>0 and table.concat(backends,', ') or 'none')..'   workers: '..s.workers)
 text(string.format('Physics LOD: %d full / %d reduced / %d suspended',s.lodFull,s.lodReduced,s.lodSuspended))
 text(string.format('Prepare wall %.2f ms   pose capture %.2f ms   draw %.2f ms',s.prepareWallMs,s.captureMs,s.renderMs))
 text(string.format('Native render %.2f ms (shadow %.2f, upload %.2f)   binds %d   draw calls %d',s.nativeRenderMs,s.shadowMs,s.uploadMs,s.drawBinds,s.drawCalls))
 text(string.format('Worker CPU totals: physics %.2f / pose %.2f / deform %.2f ms',s.physicsWorkMs,s.poseWorkMs,s.deformWorkMs))
 text(string.format('External scene CPU %.2f ms   contacts %d   proxies %d',s.sceneWorkMs,s.contacts,s.mirrors))
 text(string.format('Max sim debt %.3fs   dropped %.3fs   resets %d',s.debt,s.dropped,s.resets))
 if (s.asyncWorlds or 0)>0 then text(string.format('Async worlds %d   presentation lag %.1f ms   sleeping bodies %d',s.asyncWorlds,s.asyncLagMs,s.sleepingBodies)) end
 text(string.format('Mesh cache %.1f MB   upload %.1f MB/s   Lua %.1f MB',s.cacheMB,s.uploadMBps,s.luaMB))
 text('Worker totals may run in parallel; they are not frame wall time.')
 local errors=table.GetKeys(s.errors) table.sort(errors)
 local warning=errors[1] or (s.backends.gpu_opencl and 'OpenCL is experimental; full-rig motion parity is not guaranteed.')
 text(warning and utf8.sub(warning,1,85) or 'Diagnostics refresh twice per second.',warning and orange or white)
 text('Toggle: mmdhl_debug_toggle   Report: mmdhl_debug_report')
 local gx,gy,gw,gh=x+12,y+12,w-24,44 local values=s.graph local n=#values
 surface.SetDrawColor(80,90,110,180) surface.DrawRect(gx,gy,gw,gh)
 surface.SetDrawColor(green) local budget=gy+gh-16.7/50*gh surface.DrawLine(gx,budget,gx+gw,budget)
 surface.SetDrawColor(190,210,255)
 for i=2,n do surface.DrawLine(gx+(i-2)/math.max(1,n-1)*gw,gy+gh-math.min(50,values[i-1])/50*gh,gx+(i-1)/math.max(1,n-1)*gw,gy+gh-math.min(50,values[i])/50*gh) end
 draw.SimpleText('16.7 ms',face,gx+gw-55,budget-14,green)
end)
