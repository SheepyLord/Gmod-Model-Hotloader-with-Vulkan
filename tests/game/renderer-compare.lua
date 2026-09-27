hook.Remove('PostDrawOpaqueRenderables','MMDHL.NativeDraw')
hook.Remove('PostDrawTranslucentRenderables','MMDHL.NativeDraw')
hook.Add('PostDrawOpaqueRenderables','MMDHL.CompareShader',function(depth,sky)
 if sky then return end
 for _,e in ipairs(mmdhl.Entities()) do
  local id=mmdhl.GetAsset(e) local info=mmdhl.assets[id]
  if info then mmdhl.DrawInstance(mmdhl.GetInstance(e),id,info,'surface',false) end
 end
end)
return true
