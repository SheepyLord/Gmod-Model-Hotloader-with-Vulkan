local native=mmdhl.native
local rendererReady,rendererError
function mmdhl.RenderAvailable()
 if mmdhl.FeatureAvailable then local ok,err=mmdhl.FeatureAvailable('rendering') if not ok then return false,mmdhl.Localize(err) end end
 if rendererReady==nil then
  local ok,err=native.CheckRenderer()
  rendererReady=ok==true rendererError=err
  mmdhl.renderInitError=err
 end
 return rendererReady,rendererError
end
function mmdhl.PanelVisible(panel)
 while IsValid(panel) do if not panel:IsVisible() then return false end panel=panel:GetParent() end
 return true
end
function mmdhl.ImmediateRendering()
 local available,err=mmdhl.RenderAvailable() if not available then return false,err end
 local queue=GetConVar('mat_queue_mode')
 if queue and queue:GetInt()~=0 then
  mmdhl.previousQueueMode=mmdhl.previousQueueMode or queue:GetInt()
  RunConsoleCommand('mat_queue_mode','0')
  return false
 end
 return true
end
local function restoreRendering()
 if mmdhl.previousQueueMode then RunConsoleCommand('mat_queue_mode',tostring(mmdhl.previousQueueMode)) mmdhl.previousQueueMode=nil end
end
-- Character models draw with Source's multicore rendering (mat_queue_mode 2):
-- the native module runs their draws on Source's render thread. Previews, the
-- export icon and legacy mmdhl_ragdoll entities set material parameters from
-- Lua around each native draw and still switch to single-threaded rendering
-- while they draw; 1 also switches for every character model (old behaviour).
local forceImmediate=CreateClientConVar('mmdhl_force_immediate_rendering','0',true,false,'Switch Source to single-threaded rendering (mat_queue_mode 0) while character models exist')
-- One owner for the render mode: previews and legacy entities need it too.
-- The carrier-only restoration used to undo their request every frame.
hook.Add('Think','MMDHL.RenderMode',function()
 local preview=mmdhl.previewOwner
 if IsValid(preview) and not mmdhl.PanelVisible(preview) then
  native.ClearPreview() preview.previewHandle=nil preview.previewAsset=nil mmdhl.previewOwner=nil preview=nil
 end
 local needed=(mmdhl.editorPreviews and next(mmdhl.editorPreviews)~=nil) or (IsValid(preview) and mmdhl.PanelVisible(preview))
 if not needed then needed=#ents.FindByClass('mmdhl_ragdoll')>0 end
 if not needed and forceImmediate:GetBool() and mmdhl.Entities then needed=#mmdhl.Entities()>0 end
 if needed then mmdhl.ImmediateRendering() else restoreRendering() end
end)
hook.Add('ShutDown','MMDHL.RestoreRenderMode',restoreRendering)
mmdhl.materials=mmdhl.materials or {}
mmdhl.assets=mmdhl.assets or {}
local white='models/debug/debugwhite'
local light=Vector(.35,-.5,1):GetNormalized()
-- R32F (27) is not exported as a Lua enum on this build. The 32-bit Linux game (the default
-- branch's OpenGL translation) crashes creating an R32F render target: legacy models draw
-- there without their projected shadow.
local shadowRT=not (system.IsLinux() and jit.arch=='x86') and GetRenderTargetEx('mmdhl_shadow_r32',1024,1024,RT_SIZE_NO_CHANGE,MATERIAL_RT_DEPTH_SEPARATE,bit.bor(2,4,8),0,27) or nil
local shadow={origin=Vector(),angles=(-light):Angle(),span=256,range=768,valid=false}
local function constant(mat,index,v,w)
 local values=isvector(v) and {v.x,v.y,v.z,w or 0} or {v[1] or 0,v[2] or 0,v[3] or 0,w or v[4] or 0}
 for i,axis in ipairs({'x','y','z','w'}) do mat:SetFloat('$c'..index..'_'..axis,values[i]) end
end
local function texture(hash,fallback)
 if not hash or hash=='' then return Material(fallback or white):GetTexture('$basetexture') end
 return Material('../data/mmd_hotloader/textures/'..hash..'.png','smooth'):GetTexture('$basetexture')
end
function mmdhl.PrepareMaterials(asset,info)
 local result={}
 for i,part in ipairs(info.materials) do
  local variants={}
  for _,kind in ipairs({'surface','edge','shadow'}) do
   local transparent=kind=='surface' and (part.alpha<.999 or part.alphaTexture)
   local mat=CreateMaterial('mmdhl_v4_'..asset..'_'..i..'_'..kind,'screenspace_general',{
    ['$vertexshader']='mmdhl_vs20',['$pixshader']='mmdhl_ps20b',
    ['$basetexture']=white,['$texture1']=white,['$texture2']=white,['$texture3']=shadowRT and shadowRT:GetName() or white,
    ['$vertexnormal']='1',['$vertexcolor']='1',['$vertextransform']='1',['$tcsize0']='2',['$tcsize1']='4',
    -- On the supported build $depthtest=1 also selects DEPTHFUNC_ALWAYS.
    -- OverrideDepthEnable below enables testing while retaining LESS_EQUAL.
    ['$cull']=part.twoSided and kind~='edge' and '0' or '1',['$depthtest']='0',
    ['$writedepth']=part.alpha<.999 and '0' or '1',['$alphablend']=kind~='shadow' and '1' or '0',
    ['$linearread_basetexture']='1',['$linearread_texture1']='1',['$linearread_texture2']='1',['$linearread_texture3']='1',['$linearwrite']='1'
   })
   mat:SetTexture('$basetexture',texture(part.base))
   mat:SetTexture('$texture1',texture(part.sphere))
   mat:SetTexture('$texture2',texture(part.toon,'mmdhl/toon.png'))
   if shadowRT then mat:SetTexture('$texture3',shadowRT) end
   variants[kind]=mat
  end
  result[i]=variants
 end
 mmdhl.materials[asset]=result return result
end
function mmdhl.DrawInstance(instance,asset,info,pass,preview)
 if not mmdhl.ImmediateRendering() then return false,'Switching to immediate native rendering' end
 pass=pass or 'surface'
 local materials=mmdhl.materials[asset] or mmdhl.PrepareMaterials(asset,info)
 local eye,angles=EyePos(),EyeAngles()
 for i,variants in ipairs(materials) do
  local part=info.materials[i] local mat=variants[pass]
  if (pass~='edge' or part.edge) and (pass~='shadow' or part.shadow) then
   local controls,axes=Matrix(),Matrix()
   local function row(matrix,index,v,w) matrix:SetField(index,1,v.x) matrix:SetField(index,2,v.y) matrix:SetField(index,3,v.z) matrix:SetField(index,4,w or 0) end
   row(controls,1,light,pass=='edge' and 1 or pass=='shadow' and 2 or 0)
   row(controls,2,angles:Right(),not preview and shadow.valid and part.shadow and pass=='surface' and 1 or 0)
   row(controls,3,angles:Up(),shadow.span) row(controls,4,shadow.origin,shadow.range)
   row(axes,1,shadow.angles:Right()) row(axes,2,shadow.angles:Up()) row(axes,3,shadow.angles:Forward())
   mat:SetMatrix('$viewprojmat',controls) mat:SetMatrix('$invviewprojmat',axes)
   render.OverrideDepthEnable(true,pass~='surface' or part.alpha>=.999,true)
   local _,err=(preview and native.DrawPreview or native.Draw)(instance,i-1,'!'..mat:GetName(),pass=='edge')
   render.OverrideDepthEnable(false,false)
   if err then mmdhl.renderError=err return false,err end
  end
 end
 mmdhl.renderError=nil return true
end
local function ready(ent)
 local id=ent:GetAsset() if id=='' or ent:GetInstance()<1 then return end
 if not mmdhl.assets[id] then
  native.RequestAsset(id)
  local info,err=mmdhl.Decode(native.AssetInfo(id))
  if err then mmdhl.renderError=err return end
  if not info then return end
  mmdhl.assets[id]=info
 end
 return mmdhl.assets[id]
end
local drawingShadow=false
local shadowSignature
-- The native frame profile is JSON for the HUD, settings panel, editors and probes;
-- an ordinary frame requests none. Readers call RequestFrameProfile while they read.
function mmdhl.RequestFrameProfile(seconds) mmdhl.frameProfileUntil=math.max(mmdhl.frameProfileUntil or 0,RealTime()+(seconds or 2)) end
function mmdhl.WantFrameProfile() return mmdhl.frameProfileForced==true or RealTime()<(mmdhl.frameProfileUntil or 0) end
hook.Add('PreRender','MMDHL.Shadows',function()
 if drawingShadow or mmdhl.suspendNative then return end
 if mmdhl.UpdateClientInstances then mmdhl.UpdateClientInstances() end
 if mmdhl.PrepareNativePresentation then mmdhl.PrepareNativePresentation() end
 if native.PrepareFrame then
  local want=mmdhl.WantFrameProfile() local ms,profile=native.PrepareFrame(want) mmdhl.deformMs=ms
  if want and profile then mmdhl.frameProfile=mmdhl.Decode(profile) if mmdhl.frameProfile then mmdhl.frameProfile.captureMs=mmdhl.poseCaptureMs or 0 mmdhl.frameProfile.poseSkipped=mmdhl.poseSkipped or 0 end end
 end
 -- Bounds/proxies must exist after deformation, before this frame's draw list.
 if mmdhl.UpdateNativeVisuals then mmdhl.UpdateNativeVisuals() end
 if native.PruneRenderCache then native.PruneRenderCache(false) end
 local entries={} local minimum,maximum local signature={}
 for _,ent in ipairs(ents.FindByClass('mmdhl_ragdoll')) do
  local info=ready(ent) local b=mmdhl.Decode(native.GetBounds(ent:GetInstance()))
  if info and b then
   signature[#signature+1]=ent:GetInstance()..':'..ent:GetAsset()..':'..b.sequence
   table.insert(entries,{ent,info}) local a,c=Vector(unpack(b.minimum)),Vector(unpack(b.maximum))
   minimum=minimum and Vector(math.min(minimum.x,a.x),math.min(minimum.y,a.y),math.min(minimum.z,a.z)) or a
   maximum=maximum and Vector(math.max(maximum.x,c.x),math.max(maximum.y,c.y),math.max(maximum.z,c.z)) or c
  end
 end
 if #entries==0 or not shadowRT then shadow.valid=false shadowSignature=nil return end
 table.sort(signature) signature=table.concat(signature,'|')
 if shadow.valid and signature==shadowSignature then return end
 local center=(minimum+maximum)*.5
 shadow.span=math.Clamp((maximum-minimum):Length()*1.2,128,4096) shadow.range=shadow.span*3
 shadow.origin=center+light*shadow.range*.5
 drawingShadow=true shadow.valid=false
 render.PushRenderTarget(shadowRT) render.Clear(255,255,255,255,true,true)
 cam.Start({type='3D',origin=shadow.origin,angles=shadow.angles,x=0,y=0,w=1024,h=1024,znear=1,zfar=shadow.range,ortho={left=-shadow.span*.5,right=shadow.span*.5,top=-shadow.span*.5,bottom=shadow.span*.5}})
 local ok,err=xpcall(function() for _,entry in ipairs(entries) do local ent,info=unpack(entry) local drawn,e=mmdhl.DrawInstance(ent:GetInstance(),ent:GetAsset(),info,'shadow') if not drawn then error(e) end end end,debug.traceback)
 cam.End3D() render.PopRenderTarget() drawingShadow=false shadow.valid=ok
 if ok then shadowSignature=signature end
 if not ok then mmdhl.renderError=err end
end)
hook.Add('PostDrawTranslucentRenderables','MMDHL.Models',function(depth,sky)
 if depth or sky or drawingShadow then return end
 local entries=ents.FindByClass('mmdhl_ragdoll')
 table.sort(entries,function(a,b)return a:GetPos():DistToSqr(EyePos())>b:GetPos():DistToSqr(EyePos()) end)
 for _,ent in ipairs(entries) do
  local info=ready(ent)
  if info then mmdhl.DrawInstance(ent:GetInstance(),ent:GetAsset(),info,'edge') mmdhl.DrawInstance(ent:GetInstance(),ent:GetAsset(),info,'surface') end
 end
end)
-- The server checks its own installation: a spawn it cannot make fails with the server's problem.
function mmdhl.Action(action,id,ent,value)
 net.Start('mmdhl_action') net.WriteString(action) net.WriteString(id or '') net.WriteUInt(IsValid(ent) and ent:EntIndex() or 0,16) net.WriteString(istable(value) and util.TableToJSON(value) or value or '') net.SendToServer()
end
net.Receive('mmdhl_notice',function() local message=mmdhl.Localize(net.ReadString()) notification.AddLegacy(message,NOTIFY_ERROR,8) hook.Run('MMDHL.Notice',message) end)
include('mmdhl/library.lua')
include('mmdhl/ui.lua')
include('mmdhl/bodygroups.lua')
