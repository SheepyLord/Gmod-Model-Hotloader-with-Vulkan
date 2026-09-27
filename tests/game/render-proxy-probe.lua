local T={Type='anim',Base='base_anim',RenderGroup=RENDERGROUP_BOTH}
function T:Initialize() self:SetModel('models/hunter/blocks/cube025x025x025.mdl') self:DrawShadow(true) end
function T:Draw(flags) mmdhl.proxyFlags=mmdhl.proxyFlags or {} mmdhl.proxyFlags[tostring(flags)]=(mmdhl.proxyFlags[tostring(flags)] or 0)+1 end
function T:DrawTranslucent(flags) mmdhl.proxyTransFlags=mmdhl.proxyTransFlags or {} mmdhl.proxyTransFlags[tostring(flags)]=(mmdhl.proxyTransFlags[tostring(flags)] or 0)+1 end
scripted_ents.Register(T,'mmdhl_render_probe')
local e=mmdhl.Entities()[1] local p=ents.CreateClientside('mmdhl_render_probe') p:SetPos(e:GetPos()) p:SetRenderBounds(Vector(-50,-50,-70),Vector(50,50,80)) p:Spawn() mmdhl.proxyProbe=p
mmdhl.depthCalls={} hook.Add('PostDrawOpaqueRenderables','MMDHL.DepthProbe',function(depth,sky) local k=tostring(depth)..'_'..tostring(sky) mmdhl.depthCalls[k]=(mmdhl.depthCalls[k] or 0)+1 end)
return true
