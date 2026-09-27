assert(CLIENT and MMDHL_DEBUG_TOKEN,'Owned client session required')
local e
for _,candidate in ipairs(mmdhl.Entities()) do if candidate:GetClass()=='prop_ragdoll' then e=candidate break end end
assert(IsValid(e),'Spawn the lighting test model first')
local original=GetConVar('mmdhl_flashlight_overlap_fix'):GetString()
local phase=0 local started=RealTime()+2 local samples={} local results={}
GetConVar('mmdhl_flashlight_overlap_fix'):SetInt(0)
hook.Add('PostRender','MMDHL.OverlapProbe',function()
 if RealTime()<started then return end
 local s=util.JSONToTable(mmdhl.native.RenderFrameStats())
 samples[#samples+1]={ms=RealFrameTime()*1000,focus=system.HasFocus(),duplicates=s.flashlightDuplicates,
  overlapMs=s.overlapCheckMs,drawMs=s.nativeRenderMs,draws=s.drawCalls}
 if RealTime()-started<10 then return end
 local label=phase==0 and 'overlap-disabled' or 'overlap-enabled'
 file.Write('mmd_hotloader/debug/'..MMDHL_DEBUG_TOKEN..'/'..label..'.png',render.Capture({format='png',x=0,y=0,w=ScrW(),h=ScrH()}))
 results[#results+1]={enabled=phase==1,frames=samples,diagnostics=mmdhl.GetDiagnostics(e,false),
  vertices=mmdhl.assets[mmdhl.GetAsset(e)].vertices,triangles=mmdhl.assets[mmdhl.GetAsset(e)].triangles}
 if phase==0 then phase=1 samples={} started=RealTime()+1 GetConVar('mmdhl_flashlight_overlap_fix'):SetInt(1)
 else
  hook.Remove('PostRender','MMDHL.OverlapProbe')
  GetConVar('mmdhl_flashlight_overlap_fix'):SetString(original)
  file.Write('mmd_hotloader/debug/'..MMDHL_DEBUG_TOKEN..'/overlap-comparison.json',util.TableToJSON({resolution={ScrW(),ScrH()},results=results}))
 end
end)
return true
