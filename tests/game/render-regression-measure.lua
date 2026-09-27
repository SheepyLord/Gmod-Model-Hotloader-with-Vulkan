-- Run only through the owned-session debug bridge; ten seconds after warm-up.
MMDHLRenderMeasure={frames={},focus=true,ready=SysTime()+2,finish=SysTime()+12,errors={},sums={},count=0}
hook.Add('PostRender','MMDHL.RenderRegressionMeasure',function()
 local b=MMDHLRenderMeasure local now=SysTime()
 if now<b.ready then return end
 if b.last then b.frames[#b.frames+1]=(now-b.last)*1000 end b.last=now b.focus=b.focus and system.HasFocus()
 for _,err in ipairs({mmdhl.renderInitError or '',mmdhl.renderError or ''}) do if err~='' then b.errors[err]=true end end
 local p=mmdhl.frameProfile or {} b.count=b.count+1
 for _,k in ipairs({'prepareWallMs','physicsWorkMs','poseWorkMs','deformWorkMs','captureMs'}) do b.sums[k]=(b.sums[k] or 0)+(p[k] or 0) end
 if now>=b.finish then
  hook.Remove('PostRender','MMDHL.RenderRegressionMeasure') b.done=true table.sort(b.frames)
  local total=0 for _,v in ipairs(b.frames) do total=total+v end local n=#b.frames
  for k,v in pairs(b.sums) do b.sums[k]=v/b.count end
  local entities={} for _,e in ipairs(mmdhl.Entities()) do entities[#entities+1]={class=e:GetClass(),asset=mmdhl.GetAsset(e),physicsObjects=e:GetPhysicsObjectCount(),bones=e:GetBoneCount(),instance=mmdhl.GetInstance(e),error=e.MMDHLAttachError,stopped=e.MMDPresentationStopped} end
  b.result={frames=n,fps=1000*n/total,p50=b.frames[math.ceil(n*.5)],p95=b.frames[math.ceil(n*.95)],max=b.frames[n],focus=b.focus,errors=b.errors,meanStages=b.sums,resolution={ScrW(),ScrH()},player=LocalPlayer():GetModel(),view={origin=EyePos(),angles=EyeAngles()},entities=entities,stats=mmdhl.Decode(mmdhl.native.RenderStats()),queue=GetConVar('mat_queue_mode'):GetString(),firstPerson=GetConVar('mmdhl_first_person_body'):GetInt()}
 end
end)
return {focus=system.HasFocus(),started=true}
