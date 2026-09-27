-- Client rendering for static props. Geometry is prepared natively (render
-- winding, clusters, centered origin); Lua uploads it in bounded batches and
-- draws engine-lit VertexLitGeneric meshes through hidden proxy entities.
local P=mmdhl.props
local L=mmdhl.L
local native=P.native
P.TextureRoot='../data/mmd_hotloader/static/textures/'
P.RenderCache=P.RenderCache or {}
P.CollisionInfo=P.CollisionInfo or {}
P.AssetStatus=P.AssetStatus or {}
P.RenderStats=P.RenderStats or {batches=0,builds=0,maxBatchMs=0,maxBuildMs=0}
local proxy={Type='anim',Base='base_anim',RenderGroup=RENDERGROUP_OPAQUE}
function proxy:Initialize() self:SetModel(P.Placeholder) self:SetNoDraw(true) self:DrawShadow(false) end
function proxy:GetRenderMesh() return self.MMDHLMesh end
function proxy:Draw(flags) self:DrawModel(flags) end
function proxy:DrawTranslucent(flags) self:DrawModel(flags) end
scripted_ents.Register(proxy,'mmdhl_prop_piece')
local translucentProxy=table.Copy(proxy) translucentProxy.RenderGroup=RENDERGROUP_TRANSLUCENT
scripted_ents.Register(translucentProxy,'mmdhl_prop_piece_translucent')
function P.SetAssetStatus(id,phase,detail,current,total)
 local s=P.AssetStatus[id] or {started=RealTime()}
 if s.phase~=phase then s.phaseStarted=RealTime() end
 s.phase=phase s.detail=detail s.current=current s.total=total s.updated=RealTime()
 P.AssetStatus[id]=s hook.Run('MMDHL.PropStatus',id,phase,detail)
end
-- Textures repeat, as every source format expects: models such as game rips
-- keep their UVs in 1..2 and would otherwise smear the texture's edge row.
local function textureName(hash)
 local image=Material(P.TextureRoot..hash..'.png','smooth mips noclamp')
 local texture=image and image:GetTexture('$basetexture')
 return texture and texture:GetName()
end
local glossy=CreateClientConVar('mmdhl_prop_specular','1',true,false,'Glossy (specular) highlights on static props',0,1)
-- Source has only Phong. glTF and Blender describe roughness, metalness and
-- reflectance instead: a rough surface gets no highlight, a smooth one a tight
-- highlight scaled like a normalized Blinn-Phong lobe (a 4% dielectric
-- reflects little; metals much more). Returns tint, exponent and boost.
local function phong(m,format)
 local pbr=m.pbr
 if not pbr then
  if format~='glb' and format~='gltf' then return m.specular,m.shininess,1 end
  -- Imports before the PBR fields: only KHR_materials_specular set a colour,
  -- and Assimp stored shininess as (1-roughness)^2*1000.
  local s=m.specular or {0,0,0} if math.max(s[1] or 0,s[2] or 0,s[3] or 0)<=0 then return end
  pbr={roughness=1-math.sqrt(math.Clamp((m.shininess or 0)/1000,0,1)),metallic=0,specular=s}
 end
 local roughness=math.Clamp(tonumber(pbr.roughness) or 1,.02,1)
 if roughness>.85 then return end
 local metal=math.Clamp(tonumber(pbr.metallic) or 0,0,1) local reflect=pbr.specular or {1,1,1}
 local exponent=math.Clamp(2/roughness^4-2,1,128) local peak=math.min((exponent+8)/(8*math.pi),4)
 local tint={}
 for i=1,3 do
  local f0=math.Clamp(tonumber(reflect[i]) or 1,0,1) if pbr.workflow~='specular_glossiness' then f0=f0*.04 end
  tint[i]=Lerp(metal,f0,.6)*peak
 end
 local strength=math.max(tint[1],tint[2],tint[3]) if strength<.03 then return end
 local boost=math.max(strength,1)
 return {tint[1]/boost,tint[2]/boost,tint[3]/boost},exponent,boost
end
local function buildMaterial(id,index,m,explicitBackfaces,format)
 -- Missing source images are reported by the importer. A valid neutral texture
 -- is used here: models/wireframe is a material, not a texture resource.
 local base=m.base_texture and m.base_texture~='' and textureName(m.base_texture) or 'color/white'
 local c=m.color or {1,1,1,1}
 local blend,mask=m.alpha_mode=='blend',m.alpha_mode=='mask'
 -- glTF MASK compares texture alpha * base-color alpha with its cutoff. Fold
 -- that scalar into the threshold so Source keeps depth writes enabled.
 local cutoff=m.alpha_cutoff or .5
 if mask then cutoff=math.Clamp(cutoff/math.max(c[4] or 1,.000001),0,1) end
 -- Vertex alpha is itself a Source translucency flag; enabling it for every
 -- material sends opaque atlas regions through the wrong depth/layering path.
 -- CreateMaterial ignores Vector userdata: colors must use bracket strings.
 local params={['$basetexture']=base,['$model']=1,['$vertexcolor']=0,['$vertexalpha']=0,
  ['$nocull']=m.two_sided and not explicitBackfaces and 1 or 0,['$color2']=string.format('[%f %f %f]',c[1],c[2],c[3]),
  ['$alpha']=blend and (c[4] or 1) or 1,['$translucent']=blend and 1 or 0,['$alphatest']=mask and 1 or 0,['$alphatestreference']=cutoff}
 if m.unlit then params['$selfillum']=1 end
 if m.normal_texture and m.normal_texture~='' then params['$bumpmap']=textureName(m.normal_texture) end
 local shine=glossy:GetBool()
 local specular,exponent,boost
 if shine and not m.unlit then specular,exponent,boost=phong(m,format) end
 if specular and math.max(specular[1] or 0,specular[2] or 0,specular[3] or 0)>.0001 then
  params['$phong']=1
  params['$phongtint']=string.format('[%f %f %f]',math.Clamp(specular[1] or 0,0,1),math.Clamp(specular[2] or 0,0,1),math.Clamp(specular[3] or 0,0,1))
  params['$phongexponent']=math.Clamp(exponent or 16,1,128) params['$phongboost']=boost or 1 params['$phongfresnelranges']='[1 1 1]'
  -- Phong without a normal map renders black; supply a valid flat normal.
  if not params['$bumpmap'] then params['$basemapalphaphongmask']=1 params['$bumpmap']='dev/flat_normal' end
 end
 -- Names survive Lua refresh; the revision keeps old flags from being reused,
 -- and the suffix keeps glossy and matte variants apart.
 return CreateMaterial('mmdhl_prop_r2_'..id..'_'..index..(shine and 'g' or 'm'),'VertexLitGeneric',params)
end
local function renderChunks(info)
 if info.render_chunks then return info.render_chunks end
 local chunks={}
 for part,p in ipairs(info.parts) do
  for offset=0,p.count-1,18000 do chunks[#chunks+1]={part=part-1,offset=offset,count=math.min(18000,p.count-offset),material=p.material,mins=p.mins or info.mins,maxs=p.maxs or info.maxs,center=p.center} end
 end
 return chunks
end
local function releaseGraphics(entry)
 for _,p in ipairs(entry.pieces or {}) do if IsValid(p.entity) then p.entity:Remove() end if p.mesh then p.mesh:Destroy() end end
end
local function fail(id,entry,err)
 entry.error=tostring(err) entry.state='failed' P.SetAssetStatus(id,'failed',entry.error)
end
local function beginRender(id,entry)
 P.SetAssetStatus(id,'cache',L'props.status.opening')
 P.Load(id,function(info,err)
  if P.RenderCache[id]~=entry then return end
  if not info then fail(id,entry,err) return end
  local blob=native.PropCollisionBlob(id)
  if blob then P.CollisionInfo[id]=util.JSONToTable(blob) end
  releaseGraphics(entry)
  entry.info=info entry.part=1 entry.offset=0 entry.vertices={} entry.pieces={} entry.materials={}
  entry.chunks=renderChunks(info) entry.opaque={} entry.blended={}
  entry.mins=P.Vector(info.mins) entry.maxs=P.Vector(info.maxs) entry.center=(entry.mins+entry.maxs)*.5
  -- Native vertices are centered only in the render view. Proxies and their
  -- explicit world-light sample use that same model center.
  entry.origin=info.render_origin and P.Vector(info.render_origin) or vector_origin
  entry.prepared=0 entry.totalVertices=0
  for _,p in ipairs(entry.chunks) do entry.totalVertices=entry.totalVertices+p.count end
  entry.state='materials'
  P.SetAssetStatus(id,'materials',L('props.status.preparing_textures',{current=0,total=#info.materials}),0,#info.materials)
 end)
end
-- Only called for bundles missing from this client's cache (multiplayer).
local function download(id,entry)
 entry.state='downloading' P.SetAssetStatus(id,'download',L'props.status.downloading')
 if not mmdhl.RequestSharedProp then fail(id,entry,L'props.error.not_cached') return end
 mmdhl.RequestSharedProp(id,function(ok,err)
  if P.RenderCache[id]~=entry then return end
  if ok and native.PropHas(id) then P.Info[id]=nil beginRender(id,entry) else fail(id,entry,err or L'props.error.server_missing') end
 end)
end
function P.AcquireRender(id)
 if not P.ValidID(id) then return end
 local entry=P.RenderCache[id]
 if entry then entry.refs=entry.refs+1 entry.expires=nil return entry end
 entry={refs=1,state='waiting',pieces={},materials={}} P.RenderCache[id]=entry
 if native.PropHas(id) then beginRender(id,entry) elseif game.SinglePlayer() then fail(id,entry,L'props.error.deleted') else download(id,entry) end
 return entry
end
function P.ReleaseRender(id)
 local entry=P.RenderCache[id] if not entry then return end
 entry.refs=math.max(0,entry.refs-1) if entry.refs==0 then entry.expires=RealTime()+15 end
end
-- Retry a failed entry, for example after the bundle was downloaded or reimported.
function P.RefreshRender(id)
 local entry=P.RenderCache[id]
 if entry and entry.state=='failed' and native.PropHas(id) then P.Info[id]=nil beginRender(id,entry) end
end
-- Switching glossy highlights rebuilds every loaded prop with the other materials.
cvars.AddChangeCallback('mmdhl_prop_specular',function()
 for id,entry in pairs(P.RenderCache) do if entry.info and entry.state~='failed' and entry.state~='downloading' then beginRender(id,entry) end end
end,'MMDHL.PropSpecular')
local function destroy(id,entry)
 releaseGraphics(entry) P.RenderCache[id]=nil P.CollisionInfo[id]=nil P.Forget(id)
end
function P.DestroyRender(id) local entry=P.RenderCache[id] if entry then destroy(id,entry) end end
local function buildPiece(id,entry,p)
 local started=SysTime() local mat=entry.materials[p.material+1] local meshObject=Mesh(mat)
 local ok,err=pcall(meshObject.BuildFromTriangles,meshObject,entry.vertices)
 if not ok then meshObject:Destroy() fail(id,entry,err) return end
 local materialInfo=entry.info.materials[p.material+1]
 local ent=ents.CreateClientside(materialInfo.alpha_mode=='blend' and 'mmdhl_prop_piece_translucent' or 'mmdhl_prop_piece')
 if not IsValid(ent) then meshObject:Destroy() fail(id,entry,L'props.error.render_piece') return end
 ent:Spawn() ent.MMDHLMesh={Mesh=meshObject,Material=mat}
 local mins,maxs=P.Vector(p.mins or entry.info.mins),P.Vector(p.maxs or entry.info.maxs)
 ent:SetRenderBounds(mins,maxs)
 local piece={entity=ent,mesh=meshObject,translucent=materialInfo.alpha_mode=='blend',
  hidden=materialInfo.alpha_mode=='mask' and (materialInfo.color[4] or 1)<(materialInfo.alpha_cutoff or .5),
  opacity=materialInfo.alpha_mode=='blend' and (materialInfo.color[4] or 1) or 1,
  mins=mins,maxs=maxs,center=p.center and P.Vector(p.center) or (mins+maxs)*.5,order=#entry.pieces+1,material=p.material+1,backface=p.backface==true}
 entry.pieces[#entry.pieces+1]=piece
 local pass=piece.translucent and entry.blended or entry.opaque pass[#pass+1]=piece
 entry.part=entry.part+1 entry.offset=0 entry.vertices={}
 P.RenderStats.builds=P.RenderStats.builds+1 P.RenderStats.maxBuildMs=math.max(P.RenderStats.maxBuildMs,(SysTime()-started)*1000)
end
-- Cooperative preparation: 2 ms of Lua per frame and at most one GPU upload.
hook.Add('Think','MMDHL.PropMeshes',function()
 local deadline=SysTime()+.002
 local ordered={}
 if P.PreviewAsset and P.RenderCache[P.PreviewAsset] then ordered[1]=P.PreviewAsset end
 for id in pairs(P.RenderCache) do if id~=P.PreviewAsset then ordered[#ordered+1]=id end end
 for _,id in ipairs(ordered) do
  local entry=P.RenderCache[id]
  if entry.expires and RealTime()>=entry.expires then destroy(id,entry)
  elseif entry.refs>0 and entry.state=='materials' then
   while SysTime()<deadline do
    local i=#entry.materials+1 local m=entry.info.materials[i]
    if not m then entry.state='meshes' break end
    entry.materials[i]=buildMaterial(id,i,m,entry.info.render_backfaces,entry.info.format)
    P.SetAssetStatus(id,'materials',L('props.status.preparing_textures',{current=i,total=#entry.info.materials}),i,#entry.info.materials)
   end
   if SysTime()>=deadline then break end
  elseif entry.refs>0 and entry.state=='meshes' then
   local p=entry.chunks[entry.part]
   if not p then
    entry.vertices=nil entry.state='ready'
    P.SetAssetStatus(id,'ready',L('props.status.ready',{materials=#entry.materials,pieces=#entry.pieces}),entry.totalVertices,entry.totalVertices)
    hook.Run('MMDHL.PropRenderReady',id)
   else
    while #entry.vertices<p.count and SysTime()<deadline do
     local started=SysTime()
     local batch,err=native.PropReadMeshBatch(id,p.part,p.offset+entry.offset,math.min(512,p.count-#entry.vertices),p.backface==true)
     if not batch or #batch==0 then fail(id,entry,err or L'props.error.empty_batch') break end
     for _,v in ipairs(batch) do entry.vertices[#entry.vertices+1]=v end
     entry.offset=entry.offset+#batch entry.prepared=entry.prepared+#batch
     P.RenderStats.batches=P.RenderStats.batches+1 P.RenderStats.maxBatchMs=math.max(P.RenderStats.maxBatchMs,(SysTime()-started)*1000)
    end
    if entry.state=='meshes' then
     P.SetAssetStatus(id,'meshes',L('props.status.building_meshes',{current=#entry.pieces,total=#entry.chunks}),entry.prepared,entry.totalVertices)
     if #entry.vertices==p.count and SysTime()<deadline then buildPiece(id,entry,p) break end
    end
   end
   if SysTime()>=deadline then break end
  end
 end
end)
local function farthestFirst(a,b)
 if a.depth==b.depth then if a.backface~=b.backface then return a.backface end return a.order<b.order end
 return a.depth>b.depth
end
local lightNormals={Vector(1,0,0),Vector(-1,0,0),Vector(0,1,0),Vector(0,-1,0),Vector(0,0,1),Vector(0,0,-1)}
local function worldLighting(origin,state)
 -- A manually drawn, shared NoDraw proxy inherits the last studio lighting
 -- state; moving it does not sample its new position. Six engine queries give
 -- a world-space ambient cube (including local dynamic lights), reused by both passes.
 local frame=FrameNumber()
 if state.lightFrame~=frame or not state.lightOrigin or state.lightOrigin:DistToSqr(origin)>.0001 then
  state.lightFrame=frame state.lightOrigin=origin state.lightCube={}
  for i,normal in ipairs(lightNormals) do state.lightCube[i]=render.ComputeLighting(origin,normal) end
 end
 render.SuppressEngineLighting(true)
 for i,light in ipairs(state.lightCube) do render.SetModelLighting(i-1,light.x,light.y,light.z) end
end
-- Pass lighting=false for a studio-lit preview, or an entity/table to keep its
-- lighting sample separate from other instances sharing the same GPU meshes.
-- hidden (optional) maps 1-based material indices to true to skip them.
function P.DrawAsset(id,pos,ang,translucent,color,scale,lighting,hidden)
 local entry=P.RenderCache[id]
 if not entry or entry.state~='ready' then return false end
 scale=tonumber(scale) or 1 if scale~=scale then scale=1 end scale=math.Clamp(scale,P.MinScale,P.MaxScale)
 local r,g,b=render.GetColorModulation() local blend=render.GetBlend()
 color=color or color_white
 render.SetColorModulation(color.r/255,color.g/255,color.b/255) render.SetBlend(color.a/255)
 local pass=translucent and entry.blended or entry.opaque
 if #pass==0 then render.SetColorModulation(r,g,b) render.SetBlend(blend) return true end
 local renderPosition=LocalToWorld(entry.origin*scale,angle_zero,pos,ang)
 if lighting~=false then worldLighting(renderPosition,istable(lighting) and lighting or IsValid(lighting) and lighting:GetTable() or entry) end
 if translucent then
  -- Clusters are prepared natively once; only their centers are sorted here.
  local localForward=WorldToLocal(pos+EyeAngles():Forward(),angle_zero,pos,ang)
  for _,piece in ipairs(pass) do piece.depth=piece.center:Dot(localForward) end
  table.sort(pass,farthestFirst)
 end
 for _,piece in ipairs(pass) do
  local ent=piece.entity
  if IsValid(ent) and not piece.hidden and not (hidden and hidden[piece.material]) then
   if piece.scale~=scale then
    if scale==1 then ent:DisableMatrix('RenderMultiply')
    else piece.matrix=piece.matrix or Matrix() piece.matrix:SetScale(Vector(scale,scale,scale)) ent:EnableMatrix('RenderMultiply',piece.matrix) end
    ent:SetRenderBounds(piece.mins*scale,piece.maxs*scale) piece.scale=scale
   end
   ent:SetPos(renderPosition) ent:SetAngles(ang) ent:SetupBones()
  end
 end
 local function drawPieces()
  for _,piece in ipairs(pass) do
   if IsValid(piece.entity) and not piece.hidden and not (hidden and hidden[piece.material]) then
    -- DrawModel overwrites material $alpha from the current blend.
    render.SetBlend(color.a/255*(piece.opacity or 1)) piece.entity:DrawModel()
   end
  end
 end
 drawPieces()
 if lighting~=false then
  -- A hidden proxy is absent from Source's flashlight entity pass.
  -- RenderFlashlights draws each affecting projected light additively.
  render.RenderFlashlights(drawPieces)
  render.SuppressEngineLighting(false)
 end
 render.SetColorModulation(r,g,b) render.SetBlend(blend)
 return true
end
function P.DrawCollision(id,scale,color)
 local data=P.CollisionInfo[id] if not data then return end
 color=color or Color(80,240,160)
 for _,hull in ipairs(data.hulls or {}) do for i=1,#hull.indices,3 do
  local a=P.Vector(hull.points[hull.indices[i]+1])*scale local b=P.Vector(hull.points[hull.indices[i+1]+1])*scale local c=P.Vector(hull.points[hull.indices[i+2]+1])*scale
  render.DrawLine(a,b,color,true) render.DrawLine(b,c,color,true) render.DrawLine(c,a,color,true)
 end end
end
net.Receive('mmdhl_prop_collision',function()
 local id=net.ReadString() local n=net.ReadUInt(16)
 if not P.ValidID(id) or n>60000 then return end
 local packed=net.ReadData(n) local blob=packed and util.Decompress(packed,262144)
 if not blob then return end
 local ok=native.PropInstallCollision(id,blob) local info=ok and util.JSONToTable(blob)
 if info then P.CollisionInfo[id]={mins=info.mins,maxs=info.maxs,hulls=info.hulls} end
end)
hook.Add('ShutDown','MMDHL.PropRenderCleanup',function() for id,e in pairs(P.RenderCache) do destroy(id,e) end end)
hook.Add('PostCleanupMap','MMDHL.PropMapCleanup',function() for id,e in pairs(P.RenderCache) do if e.refs==0 then destroy(id,e) end end end)
concommand.Add('mmdhl_prop_stats',function() PrintTable(P.RenderStats) end)
