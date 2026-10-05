-- A registered client render entity participates in Source's depth/shadow views.
-- The server prop_ragdoll remains the sole interactive/physical entity.
local native=mmdhl.native
local L=mmdhl.L
local overlapFix=CreateClientConVar('mmdhl_flashlight_overlap_fix','1',true,false,'Light coincident opaque/cutout character model surfaces once per flashlight')
if native.SetFlashlightOverlapGuard then
 native.SetFlashlightOverlapGuard(overlapFix:GetBool())
 cvars.AddChangeCallback('mmdhl_flashlight_overlap_fix',function(_,_,v) native.SetFlashlightOverlapGuard(tonumber(v)~=0) end,'MMDHL.OverlapLighting')
end
local vertexCache=CreateClientConVar('mmdhl_vertex_cache','1',true,false,'Reuse native character model vertex/index buffers; 0 uses the immediate-stream fallback')
native.SetNativeVertexCache(vertexCache:GetBool())
cvars.AddChangeCallback('mmdhl_vertex_cache',function(_,_,value) native.SetNativeVertexCache(tonumber(value)~=0) end,'MMDHL.VertexCache')
-- Source's compressed vertex (normal and tangent packed into 4 bytes) halves the
-- per-frame upload of the shared vertex buffers. 0 restores the 64-byte layout.
local compactVertices=CreateClientConVar('mmdhl_compact_vertices','1',true,false,'Upload deformed character model vertices in the 32-byte compressed Source layout')
if native.SetCompactVertices then
 native.SetCompactVertices(compactVertices:GetBool())
 cvars.AddChangeCallback('mmdhl_compact_vertices',function(_,_,value) native.SetCompactVertices(tonumber(value)~=0) native.PruneRenderCache(true) end,'MMDHL.CompactVertices')
end
-- Skeletons posed over the network (an NPC driven by an animation addon through
-- bone manipulation) only change at the server tick rate; the module shows them
-- one update behind, interpolated, so body, hair anchors and the physics input
-- move every frame instead of stepping 66 times a second.
local smoothPoses=CreateClientConVar('mmdhl_smooth_stepped_poses','1',true,false,'Interpolate skeletons that update less often than the frame rate (networked bone poses) one update behind')
if native.SetPoseSmoothing then
 native.SetPoseSmoothing(smoothPoses:GetBool())
 cvars.AddChangeCallback('mmdhl_smooth_stepped_poses',function(_,_,value) native.SetPoseSmoothing(tonumber(value)~=0) end,'MMDHL.PoseSmoothing')
end
-- Source hardware skinning: vertices stay in static buffers and each frame
-- sends bone matrices instead of skinned vertices. Vertices the shaders cannot
-- reproduce (SDEF/QDEF, heavy fourth weights) and unsupported materials keep
-- CPU skinning automatically. 0 skins every vertex on the CPU.
local gpuSkinning=CreateClientConVar('mmdhl_gpu_skinning','1',true,false,'Skin character models on the GPU through Source hardware skinning')
if native.SetGpuSkinning then
 native.SetGpuSkinning(gpuSkinning:GetBool())
 cvars.AddChangeCallback('mmdhl_gpu_skinning',function(_,_,value) native.SetGpuSkinning(tonumber(value)~=0) native.PruneRenderCache(true) end,'MMDHL.GpuSkinning')
end
local directions={Vector(1,0,0),Vector(-1,0,0),Vector(0,1,0),Vector(0,-1,0),Vector(0,0,1),Vector(0,0,-1)}
if mmdhl.sourceTextureVersion~=8 then
 mmdhl.sourceMaterials={} mmdhl.sourceBlendMaterials={} mmdhl.depthMaterials={} mmdhl.shadowMaterials={} mmdhl.sourceTextureVersion=8
end
mmdhl.sourceMaterials=mmdhl.sourceMaterials or {}
mmdhl.sourceBlendMaterials=mmdhl.sourceBlendMaterials or {}
mmdhl.depthMaterials=mmdhl.depthMaterials or {}
mmdhl.shadowMaterials=mmdhl.shadowMaterials or {}
-- A mounted VMT can precede its texture upload, especially for large uncompressed
-- VTFs. Missing resources are retryable, never a completed material cache. Share
-- the retry deadline across entities and views so shadows/flashlights cannot turn
-- one upload into hundreds of material lookups per frame.
local materialRetries={color={},blend={},depth={},shadow={}}
local function pendingMaterial(kind,id)
 local pending=materialRetries[kind][id]
 return pending and SysTime()<pending.at and pending.err or nil
end
local function deferMaterial(kind,id,err)
 materialRetries[kind][id]={at=SysTime()+.25,err=err}
 return nil,err
end
local function baseTexture(mat)
 local texture=mat:GetTexture('$basetexture')
 return texture and (not texture.IsError or not texture:IsError()) and texture or nil
end
local function materials(id,info,depth,blended)
 local cache=depth and mmdhl.depthMaterials or blended and mmdhl.sourceBlendMaterials or mmdhl.sourceMaterials
 if cache[id] then return cache[id] end
 local kind=depth and 'depth' or blended and 'blend' or 'color'
 local pending=pendingMaterial(kind,id) if pending then return nil,pending end
 local result,textures={},{}
 local source
 if depth then
  local err
  source,err=materials(id,info,false)
  if not source then return deferMaterial(kind,id,err) end
 else
  local archive='mmd_hotloader/assets/'..id..'/materials-v5.gma'
  if file.Exists(archive,'DATA') then
   local ok,err=mmdhl.MountPackage('data/'..archive)
   if not ok then return deferMaterial(kind,id,err or L('render.error.missing_material',{path=archive})) end
  end
 end
 for i,p in ipairs(info.materials) do
  -- Render the exact public VMT that tools/editors inspect. A separately
  -- created shader clone can diverge in parameter types and texture bindings,
  -- and ignores edits made through Material(entity:GetMaterials()[slot]).
  local mat=source and source[i] or Material(p.path)
  if not mat or mat:IsError() then return deferMaterial(kind,id,L('render.error.missing_material',{path=tostring(p.path)})) end
  if depth then
   textures[i]=baseTexture(mat)
   if not textures[i] then return deferMaterial(kind,id,L('render.error.pending_texture',{path=p.path})) end
  end
  result[i]=mat
 end
 -- Preflight every slot before CreateMaterial: it caches the first parameters
 -- for a name even if our Lua table is discarded. Never create a depth clone
 -- with a fallback texture that a later successful build cannot replace.
 if depth then for i,p in ipairs(info.materials) do
  local texture=textures[i]
  local mat=CreateMaterial('mmdhl_depth_v11_'..id..'_'..i,'DepthWrite',{
   ['$basetexture']=texture:GetName(),['$model']='1',
   ['$nocull']=p.twoSided and '1' or '0',['$alphatest']='1',
   ['$alphatestreference']='.5',['$allowalphatocoverage']='1'})
  mat:SetTexture('$basetexture',texture)
  result[i]=mat
 end end
 materialRetries[kind][id]=nil cache[id]=result return result
end
local function ready(ent)
 local id=mmdhl.GetAsset(ent)
 if not mmdhl.assets[id] then native.RequestAsset(id) local info=mmdhl.Decode(native.AssetInfo(id)) if not info then return end mmdhl.assets[id]=info end
 return id,mmdhl.assets[id]
end
function mmdhl.LightingOrigin(ent)
 if ent.MMDHLLightFrame==FrameNumber() then return ent.MMDHLLightOrigin end
 local center=ent:WorldSpaceCenter()
 -- PointContents is available clientside (IsInWorld is server-only).
 local function valid(p) return p:LengthSqr()<1e12 and bit.band(util.PointContents(p),CONTENTS_SOLID)==0 end
 local origin=center local reason='bounds center'
 if not valid(center) then
  local rig=mmdhl.GetRig(ent) local bestDistance=math.huge
  local previous=ent.MMDHLLightBone and ent:GetBoneMatrix(ent.MMDHLLightBone)
  if previous and valid(previous:GetTranslation()) then origin=previous:GetTranslation() reason='exposed skeleton' bestDistance=-1 end
  -- A rotated ragdoll's AABB center can be buried in a ledge. Sample the
  -- nearest actual primary bone in open map space, never the player's light.
  for _,body in ipairs(rig and rig.bodies or {}) do
   local matrix=ent:GetBoneMatrix(body.bone)
   if matrix then local point=matrix:GetTranslation() local distance=point:DistToSqr(center)
    if distance<bestDistance and valid(point) then origin=point bestDistance=distance reason='exposed skeleton' ent.MMDHLLightBone=body.bone end
   end
  end
  if reason=='bounds center' and ent.MMDHLLightOrigin and valid(ent.MMDHLLightOrigin) and ent.MMDHLLightOrigin:DistToSqr(center)<128^2 then origin=ent.MMDHLLightOrigin reason='last exposed point' end
 else ent.MMDHLLightBone=nil end
 ent.MMDHLLightFrame=FrameNumber() ent.MMDHLLightOrigin=origin ent.MMDHLLightReason=reason
 return origin
end
local remixPreview
function mmdhl.UsesRemixPreview()
 if remixPreview==nil then remixPreview=mmdhl.Decode(native.RenderStats()).fixedFunctionRemix==true end
 return remixPreview
end
local previewMaterials={}
local function drawRemixPreview(handle,id,info,visible)
 local source,err=materials(id,info,false) if not source then return false,err end
 local cached=previewMaterials[id] or {} previewMaterials[id]=cached
 for i,p in ipairs(info.materials) do
  if visible and visible[i]==false then continue end
  local texture=baseTexture(source[i])
  if not texture then return false,L('render.error.preview_texture',{path=p.path}) end
  local mat=cached[i]
  if not mat then
   -- Remix excludes vgui/ from scene material categorization/PBR conversion.
   mat=CreateMaterial('vgui/mmdhl/preview_v1_'..id..'_'..i,'UnlitGeneric',{
    ['$basetexture']=texture:GetName(),['$model']='1',['$vertexcolor']='1',
    ['$nocull']=p.twoSided and '1' or '0',['$alphatest']='1',['$alphatestreference']='.5'})
   cached[i]=mat
  end
  -- The fixed-function world shader is not a menu shader. Keep the same
  -- texture/cutouts and authored tint without mutating the public world VMT.
  mat:SetTexture('$basetexture',texture)
  mat:SetVector('$color',Vector(unpack(p.diffuse or {1,1,1})))
  mat:SetFloat('$alpha',p.alpha or 1)
  local _,err=native.DrawPreview(handle,i-1,'!'..mat:GetName(),false)
  if err then return false,err end
 end
 return true
end
-- Normal Source keeps the studio-shaded preview. Remix uses independent
-- texture materials in an offscreen menu target, never a second world actor.
-- visible (optional): one boolean per material slot from a bodygroup preset.
function mmdhl.DrawLibraryPreview(handle,id,info,visible)
 if mmdhl.UsesRemixPreview() then return drawRemixPreview(handle,id,info,visible) end
 local mats,err=materials(id,info,false) if not mats then return false,err end
 local ok,err=native.SetupSourceLighting(IsValid(LocalPlayer()) and LocalPlayer():EyePos() or Vector())
 if not ok then return false,err end
 render.ResetModelLighting(.3,.3,.3) render.SetLocalModelLights()
 render.SetModelLighting(BOX_FRONT,.65,.68,.75) render.SetModelLighting(BOX_BACK,.9,.85,.8) render.SetModelLighting(BOX_TOP,1,1,1)
 for i,mat in ipairs(mats) do
  local shown=visible and visible[i]
  if shown==false then continue end
  local alpha,color=mat:GetFloat('$alpha') or 1,mat:GetVector('$color2') or Vector(1,1,1)
  -- A preset may show a part authored invisible (alpha 0), like the Show bodygroups.
  local part=info.materials[i] mat:SetFloat('$alpha',shown and (part.alpha or 1)<=0 and 1 or part.alpha or 1) mat:SetVector('$color2',Vector(unpack(part.diffuse or {1,1,1})))
  local _,e=native.DrawPreview(handle,i-1,info.materials[i].path,false)
  mat:SetFloat('$alpha',alpha) mat:SetVector('$color2',color)
  if e then return false,e end
 end
 render.ResetModelLighting(1,1,1)
 return true
end
local function shadowNames(id,info)
 if mmdhl.shadowMaterials[id] then return mmdhl.shadowMaterials[id] end
 local pending=pendingMaterial('shadow',id) if pending then return nil,pending end
 local source,err=materials(id,info,false,true) if not source then return deferMaterial('shadow',id,err) end
 for i,p in ipairs(info.materials) do
  if p.alphaTexture and not baseTexture(source[i]) then return deferMaterial('shadow',id,L('render.error.pending_texture',{path=p.path})) end
 end
 local names={}
 for i,p in ipairs(info.materials) do
  local mat
  if p.alphaTexture then
   -- CreateMaterial snapshots the shader before RegisterSourceShadow converts
   -- this string into the typed IMaterial reference ShadowBuild requires. Add
   -- it afterwards with SetString (no Recompute), then let that existing native
   -- bridge set its type before the first shadow draw. Lua has no SetMaterial.
   mat=CreateMaterial('mmdhl_shadow_v4_'..id..'_'..i,'ShadowBuild',{['$model']='1',['$nocull']=p.twoSided and '1' or '0'})
   mat:SetString('$translucent_material',p.path)
  else
   -- Opaque parts share one shadow material per cull mode, so the native
   -- shadow pass binds once and merges their index ranges into a few draws.
   mat=CreateMaterial('mmdhl_shadow_v3_opaque'..(p.twoSided and '_nocull' or ''),'ShadowBuild',{['$model']='1',['$nocull']=p.twoSided and '1' or '0'})
  end
  names[i]='!'..mat:GetName()
 end
 materialRetries.shadow[id]=nil mmdhl.shadowMaterials[id]=names return names
end
local function draw(ent,translucent,depth)
 if mmdhl.PresentationSuppressed(ent) then return end
 if ent.MMDHLClientInstance and not ent.MMDPresentationFrame then return end
 if mmdhl.suspendNative then return true end
 local firstPerson=mmdhl.FirstPersonView and mmdhl.FirstPersonView(ent,depth)
 if firstPerson==false then return end
 local started=SysTime() local id,info=ready(ent) if not info then return end
 local instance=mmdhl.GetInstance(ent) local center=mmdhl.LightingOrigin(ent)
 -- The native module keeps the per-part material names; register them once per asset.
 if ent.MMDNamesSent~=id or ent.MMDNamesVersion~=mmdhl.sourceTextureVersion then
  if SysTime()<(ent.MMDNamesRetry or 0) then return end
  local color,err=materials(id,info,false) if not color then mmdhl.renderError=err return end
  local depthMats,depthError=materials(id,info,true)
  if not depthMats then mmdhl.renderError=depthError if depth then return end end
  -- Colour can draw while depth textures upload. The native API requires an
  -- entry for every slot; empty depth names stay private to this pending map
  -- and are never drawn. Only a complete registration earns MMDNamesSent.
  if depthMats or ent.MMDColorNamesSent~=id or ent.MMDNamesVersion~=mmdhl.sourceTextureVersion then
   local names={color={},depth={}}
   for i in ipairs(color) do names.color[i]=info.materials[i].path names.depth[i]=depthMats and '!'..depthMats[i]:GetName() or '' end
   local ok,err=native.SetInstanceMaterials(instance,util.TableToJSON(names))
   if not ok then ent.MMDNamesRetry=SysTime()+.25 mmdhl.renderError=err return end
   ent.MMDNamesRetry=nil ent.MMDColorNamesSent=id ent.MMDNamesVersion=mmdhl.sourceTextureVersion
   ent.MMDNamesSent=depthMats and id or nil
  end
 end
 if not depth then
  render.SuppressEngineLighting(false) render.SetLightingOrigin(center)
  local ok,err=native.SetupSourceLighting(center) if not ok then mmdhl.renderError=err return end
 end
 -- Another addon's copy of a player model wears that player's colour and materials.
 local look=IsValid(ent.MMDHLCopyOf) and ent.MMDHLCopyOf or ent
 local color=look:GetColor() local r,g,b=render.GetColorModulation() local blend=render.GetBlend()
 render.SetColorModulation(color.r/255,color.g/255,color.b/255) render.SetBlend(color.a/255)
 -- Material overrides are gathered once per frame; the native loop applies them per pass.
 -- Re-read every few frames (staggered with the visibility sync) or right after a local edit.
 local frame=FrameNumber() local revision=look.MMDHLMaterialRevision or 0
 if frame>=(ent.MMDOverrideNext or 0) or ent.MMDOverrideRevision~=revision then
  ent.MMDOverrideNext=frame+8+(ent.MMDOverrideNext and 0 or ent:EntIndex()%8) ent.MMDOverrideRevision=revision ent.MMDOverrideFrame=frame local overrides local base=look:GetMaterial()
  for i=1,#info.materials do local override=look:GetSubMaterial(i-1) if override=='' then override=base end if override~='' then overrides=overrides or {} overrides[tostring(i-1)]=override end end
  ent.MMDOverrideJson=overrides and util.TableToJSON(overrides) or nil
 end
 -- The first-person camera can sit inside a collar/cape weighted to the torso.
 -- Clip only this color view below the camera; reflection/depth/shadow views
 -- retain the full mesh and standard c_arms are drawn separately by Source.
 local clipState
 if firstPerson==true then
  clipState=render.EnableClipping(true)
  -- Another addon's copy turns with that addon's camera, not the player: world up.
  local up=ent.MMDHLCopyOf and Vector(0,0,1) or ent:GetUp() render.PushCustomClipPlane(-up,-up:Dot(EyePos()-up*12))
 end
 -- Default parts draw in the opaque pass only; the translucent pass has work only for override materials.
 -- 'mask': the first-person mask alone, for a body whose own addon sets the clip planes.
 if not (translucent and not depth and not ent.MMDOverrideJson) then
  local _,err=native.DrawInstance(instance,translucent,depth,ent.MMDOverrideJson,Vector(color.r/255,color.g/255,color.b/255),color.a/255,firstPerson==true or firstPerson=='mask')
  if err then mmdhl.renderError=err end
 end
 if firstPerson==true then render.PopCustomClipPlane() render.EnableClipping(clipState) end
 render.SetColorModulation(r,g,b) render.SetBlend(blend)
 if not depth then render.ResetModelLighting(1,1,1) render.SetLocalModelLights() end
 if mmdhl.renderFrame~=FrameNumber() then mmdhl.renderFrame=FrameNumber() mmdhl.renderMs=0 mmdhl.depthPasses=0 end
 mmdhl.renderMs=(mmdhl.renderMs or 0)+(SysTime()-started)*1000
 if depth then mmdhl.depthPasses=(mmdhl.depthPasses or 0)+1 end
end
function mmdhl.DrawCarrier(ent,translucent,flags)
 if not mmdhl.IsMMD(ent) or mmdhl.GetInstance(ent)<1 then return end
 local depth=bit.band(flags or 0,STUDIO_SHADOWDEPTHTEXTURE or 1073741824)~=0 or bit.band(flags or 0,STUDIO_SSAODEPTHTEXTURE or 536870912)~=0
 draw(ent,translucent,depth)
 if not depth then render.RenderFlashlights(function() draw(ent,translucent,false) end) end
end
local visual={Type='anim',Base='base_anim',RenderGroup=RENDERGROUP_BOTH}
function visual:Initialize()
 self:SetModel('models/hunter/blocks/cube025x025x025.mdl') self:SetSolid(SOLID_NONE) self:DrawShadow(true)
end
function visual:Draw(flags) mmdhl.DrawCarrier(self.MMDOwner,false,flags) end
function visual:DrawTranslucent(flags) mmdhl.DrawCarrier(self.MMDOwner,true,flags) end
scripted_ents.Register(visual,'mmdhl_native_visual')
hook.Remove('PostDrawOpaqueRenderables','MMDHL.NativeDraw')
hook.Remove('PostDrawTranslucentRenderables','MMDHL.NativeDraw')
hook.Remove('PreRender','MMDHL.NativeVisuals')
hook.Remove('Think','MMDHL.NativeVisuals')
function mmdhl.UpdateNativeVisuals(create)
 mmdhl.renderError=nil local count=0
 for _,ent in ipairs(mmdhl.Entities()) do
  -- Another addon's copy has no proxy, bounds or shadow of ours: that addon draws it.
  if not mmdhl.IsMMD(ent) or mmdhl.GetInstance(ent)<1 or mmdhl.PresentationSuppressed(ent) or ent.MMDHLCopyOf then continue end count=count+1
  if not IsValid(ent.MMDHLVisual) then
   if not create then continue end -- Entity creation is illegal during rendering.
   local proxy=ents.CreateClientside('mmdhl_native_visual') proxy.MMDOwner=ent proxy:SetPos(ent:GetPos()) proxy:Spawn() ent.MMDHLVisual=proxy ent.MMDHLBoundsSequence=nil
   ent:CallOnRemove('MMDHL.NativeVisualRemove',function(owner) if IsValid(proxy) and proxy.MMDOwner==owner then proxy:Remove() end end)
  end
  -- Seven numbers instead of a JSON object; unchanged geometry keeps its bounds.
  local x0,y0,z0,x1,y1,z1,sequence=native.GetBoundsValues(mmdhl.GetInstance(ent))
  if sequence and ent.MMDPresentationFrame and sequence~=ent.MMDHLBoundsSequence then
   ent.MMDHLBoundsSequence=sequence
   local minimum,maximum=Vector(x0-2,y0-2,z0-2),Vector(x1+2,y1+2,z1+2)
   ent.MMDHLVisual:SetPos(ent:GetPos()) ent.MMDHLVisual:SetRenderBoundsWS(minimum,maximum) ent:SetRenderBoundsWS(minimum,maximum)
   ent.MMDHLBoundsCenter=(minimum+maximum)*.5 ent.MMDHLBoundsRadius=(maximum-minimum):Length()*.5
  end
  if not create then continue end
  local id,info=ready(ent)
  local firstPerson=mmdhl.IsLocalFirstPerson and mmdhl.IsLocalFirstPerson(ent)
  local alpha=(ent:GetNoDraw() or not ent.MMDPresentationFrame or firstPerson) and 0 or ent:GetColor().a/255
  if info and (ent.MMDShadowAlpha~=alpha or ent.MMDShadowVersion~=mmdhl.sourceTextureVersion) and SysTime()>=(ent.MMDShadowRetry or 0) then
   local instance=mmdhl.GetInstance(ent) local index=mmdhl.ShadowKey(ent,instance)
   local names,err=shadowNames(id,info)
   if names then
    local ok
    ok,err=native.RegisterSourceShadow(ent:EntIndex()>=0 and ent:EntIndex() or ent:GetPhysicsObject(),instance,util.TableToJSON(names),alpha)
    if ok then
     if ent.MMDShadowAlpha==nil then ent:CreateShadow() ent:CallOnRemove('MMDHL.NativeShadowRemove',function() native.RemoveSourceShadow(index) end) end
     ent.MMDShadowAlpha=alpha ent.MMDShadowVersion=mmdhl.sourceTextureVersion ent.MMDShadowRetry=nil
    else ent.MMDShadowRetry=SysTime()+.25 mmdhl.renderError=err end
   else mmdhl.renderError=err end
  end
  -- Idle characters keep their snapshot; the engine shadow is re-rendered only when the geometry sequence changed.
  if ent.MMDShadowAlpha and sequence and sequence~=ent.MMDShadowSequence then ent.MMDShadowSequence=sequence ent:MarkShadowAsDirty() end
 end
end
hook.Add('Think','MMDHL.NativeVisuals',function() mmdhl.UpdateNativeVisuals(true) end)
-- A newly received corpse can already have a complete pose before its first
-- legal proxy-creation opportunity. Draw it directly for that frame. Once the
-- proxy exists, only the normal engine render path draws it.
local pendingOpaque=setmetatable({},{__mode='k'})
local function pendingVisuals(translucent,depth,sky)
 if sky then return end
 local flags=depth and (STUDIO_SHADOWDEPTHTEXTURE or 1073741824) or 0
 for _,ent in ipairs(mmdhl.Entities()) do
  if not IsValid(ent.MMDHLVisual) and not ent.MMDHLCopyOf then
   local previous=pendingOpaque[ent]
   -- Some render integrations bypass the main opaque hook. In that case
   -- submit the missing opaque surface before its translucent overrides.
   if translucent and (not previous or previous.frame~=FrameNumber() or previous.flags~=flags or previous.eye~=EyePos()) then mmdhl.DrawCarrier(ent,false,flags) end
   mmdhl.DrawCarrier(ent,translucent,flags)
   if translucent then pendingOpaque[ent]=nil else pendingOpaque[ent]={frame=FrameNumber(),flags=flags,eye=EyePos()} end
  end
 end
end
hook.Add('PostDrawOpaqueRenderables','MMDHL.PendingVisuals',function(depth,sky) pendingVisuals(false,depth,sky) end)
hook.Add('PostDrawTranslucentRenderables','MMDHL.PendingVisuals',function(depth,sky) pendingVisuals(true,depth,sky) end)
hook.Remove('ShutDown','MMDHL.NativeRenderRestore')

-- Update-rate LOD. Mode 1 (default): a model the renderer has not drawn in any
-- pass for a few frames (no view, reflection, flashlight or shadow) takes a new
-- pose at 10 Hz; every model you can see, or whose shadow you can see, updates
-- every frame. Mode 2 also slows models small on screen to 30 or 20 Hz, which
-- visibly steps their motion and hair. Pose capture, the pose and physics
-- hand-off and CPU skinning all follow; models on a reduced rate take turns.
local updateLod=CreateClientConVar('mmdhl_update_lod','1',true,false,'Pose update-rate LOD: 0 off, 1 unseen models at 10 Hz, 2 also small on-screen models at 20-30 Hz',0,2)
local smoothFrame=1/60
local function poseInterval(ent,eye,tanHalf,mode)
 if not ent.MMDPresentationFrame then return 0 end
 local age=native.GetDrawAge and native.GetDrawAge(mmdhl.GetInstance(ent)) or 0
 if age>3 then return 1/10 end
 if (mode or 1)<2 then return 0 end
 local center,radius=ent.MMDHLBoundsCenter,ent.MMDHLBoundsRadius
 if not center then return 0 end
 -- Bounding radius as a fraction of half the view height.
 local size=radius/(math.max(center:Distance(eye)-radius,1)*tanHalf)
 if size>=.35 then return 0 elseif size>=.15 then return 1/30 else return 1/20 end
end
mmdhl.PoseInterval=poseInterval
-- The main view's origin and FOV as rendered (after CalcView overrides such as
-- third-person cameras); EyePos() in PreRender still reports the player's eye.
hook.Add('RenderScene','MMDHL.UpdateLodView',function(origin,angles,fov) mmdhl.viewOrigin=origin mmdhl.viewFov=fov end)
-- Other addons pose their copies of a player model freely: First-Person Body
-- scales the head to nothing in vehicles and never restores it. The module
-- rejects a matrix without scale or position, and one rejected entry stops the
-- whole frame's batch, so such a bone keeps only its position (else the copy's).
local function finite(v) return v.x==v.x and v.y==v.y and v.z==v.z and math.abs(v.x)<1e9 and math.abs(v.y)<1e9 and math.abs(v.z)<1e9 end
local function saneMatrix(matrix,ent)
 local scale,position=matrix:GetScale(),matrix:GetTranslation()
 if scale.x>1e-4 and scale.y>1e-4 and scale.z>1e-4 and finite(scale) and finite(position) then return matrix end
 local fixed=Matrix() fixed:SetTranslation(finite(position) and position or ent:GetPos()) return fixed
end
mmdhl.SaneBoneMatrix=saneMatrix
-- In vehicles it also moves that head 10000 units away. Every vertex the head
-- shares with the neck or collar would stretch across the view, so a bone far
-- from the skeleton's root stays where its parent is (parents come first).
local function gatherBones(ent,palette)
 local root=palette[1] and palette[1]:GetTranslation() if not root then return end
 for i=2,#palette do
  if palette[i]:GetTranslation():DistToSqr(root)>512*512 then
   local parent=ent:GetBoneParent(i-1) local base=parent and parent>=0 and palette[parent+1] or palette[1]
   local fixed=Matrix() fixed:Set(base) palette[i]=fixed
  end
 end
end
mmdhl.GatherCopyBones=gatherBones
-- Source's client bone palette is the single pose used by tools and visible skin.
function mmdhl.PrepareNativePresentation()
 if mmdhl.UpdateRemoteScene then mmdhl.UpdateRemoteScene() end
 local started=SysTime() local frame=FrameNumber() local count=0
 local mode=updateLod:GetInt() local lod=mode>0 local eye,tanHalf
 if lod then
  smoothFrame=smoothFrame+(math.Clamp(RealFrameTime(),1/300,1/10)-smoothFrame)*.1
  local ply=LocalPlayer() local fov=mmdhl.viewFov or (IsValid(ply) and ply:GetFOV()) or 75
  -- Source's FOV is horizontal at 4:3; the vertical half-angle tangent is 3/4 of it.
  eye=mmdhl.viewOrigin or EyePos() tanHalf=math.tan(math.rad(math.Clamp(fov,1,179)*.5))*.75
 end
 mmdhl.poseSkipped=0
 local matrices=native.SubmitPresentationMatrixBatch~=nil local batch=mmdhl.presentationBatch or {} mmdhl.presentationBatch=batch
 for _,ent in ipairs(mmdhl.Entities()) do
  if not mmdhl.IsMMD(ent) or ent.MMDPresentationFrame==frame or ent.MMDPresentationStopped then continue end
  if lod then
   local every=math.max(1,math.floor(poseInterval(ent,eye,tanHalf,mode)/smoothFrame+.5))
   -- Each model updates on its own phase of the interval (by entity index); a rate
   -- change can move the phase, so no model waits longer than two intervals.
   if every>1 and (frame+ent:EntIndex())%every~=0 and frame-(ent.MMDHLPoseFrame or -math.huge)<every*2 then mmdhl.poseSkipped=mmdhl.poseSkipped+1 continue end
  end
  if (ent.MMDPresentationCheckAt or 0)<RealTime() then
   ent.MMDPresentationCheckAt=RealTime()+1 local d=mmdhl.GetDiagnostics(ent,false)
   if d and d.sourceError and d.sourceError~='' then
    ent.MMDPresentationStopped=d.sourceError mmdhl.renderError=d.sourceError
    file.CreateDir('mmd_hotloader/failures') file.Write('mmd_hotloader/failures/'..os.time()..'-presentation-'..ent:EntIndex()..'.json',util.TableToJSON({error=d.sourceError,diagnostics=d,rig=mmdhl.GetRig(ent),engine=VERSIONSTR,map=game.GetMap()},true))
    ErrorNoHalt('[Model Hotloader presentation] '..d.sourceError..'\n') continue
   end
  end
  local rig=mmdhl.GetRig(ent) if not rig then continue end
  if mmdhl.GetInstance(ent)<1 then continue end
  mmdhl.SyncMaterialState(ent,mmdhl.GetInstance(ent))
  if mmdhl.SyncActorMorphs then mmdhl.SyncActorMorphs(ent) end
  ent:InvalidateBoneCache() ent:SetupBones()
  -- The bone-to-world matrices go to the module as they are (palette[bone+1]).
  ent.MMDRenderPose=ent.MMDRenderPose or {} local palette=ent.MMDRenderPose local valid=true
  local copy=ent.MMDHLCopyOf~=nil
  for i=0,#rig.bones-1 do local matrix=ent:GetBoneMatrix(i) if not matrix then valid=false break end palette[i+1]=copy and saneMatrix(matrix,ent) or matrix end
  if copy and valid then gatherBones(ent,palette) end
  if valid then
   if mmdhl.UpdatePhysicsLOD then mmdhl.UpdatePhysicsLOD(ent,palette) end
   local entry=ent.MMDHLBatchEntry or {} ent.MMDHLBatchEntry=entry entry[1]=mmdhl.GetInstance(ent)
   if matrices then entry[2]=palette else local legacy={} for i,m in ipairs(palette) do legacy[i*2-1]=m:GetTranslation() legacy[i*2]=m:GetAngles() end entry[2]=legacy end
   count=count+1 batch[count]=entry ent.MMDPresentationFrame=frame
   ent.MMDHLPoseFrame=frame
  end
 end
 for k=count+1,#batch do batch[k]=nil end
 if count>0 then local ok,err=(matrices and native.SubmitPresentationMatrixBatch or native.SubmitPresentationBatch)(batch,CurTime(),frame) if not ok then mmdhl.renderError=err end end
 mmdhl.poseCaptureMs=(SysTime()-started)*1000
end
