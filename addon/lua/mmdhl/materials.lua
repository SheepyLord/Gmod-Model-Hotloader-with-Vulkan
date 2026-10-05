-- MMD-specific overflow state; ordinary entities always use the chained methods.
local meta=FindMetaTable('Entity')
mmdhl.materialMethods=mmdhl.materialMethods or {}
local original=mmdhl.materialMethods
for _,name in ipairs({'GetMaterials','GetSubMaterial','SetSubMaterial','GetBodyGroups','GetNumBodyGroups','GetBodygroup','SetBodygroup','GetBodygroupCount','GetBodygroupName','FindBodygroupByName','SetBodyGroups'}) do
 original[name]=original[name] or meta[name]
end
local function rig(ent) return mmdhl.IsMMD(ent) and mmdhl.GetRig(ent) end
function mmdhl.GetMaterials(ent)
 local r=rig(ent) return r and r.materials or {}
end
local function slot(ent,index)
 local r=rig(ent)
 return r and isnumber(index) and index==math.floor(index) and index>=0 and r.materials and r.materials[index+1],r
end
local function groupName(m) return (m.defaultHidden and 'Show ' or 'Hide ')..m.name end
local function groupValue(m,visible) return visible~=not m.defaultHidden and 1 or 0 end
local function localOverride(ent,kind,index)
 local values=ent['MMDHLClient'..kind] local value=values and values[index]
 if value~=nil then
  local revisions=ent['MMDHLClient'..kind..'Revision']
  if revisions and revisions[index]==ent:GetNW2Int('MMDHL'..kind..'Revision'..index,0) then return value end
  values[index]=nil
 end
end
local function setOverride(ent,kind,index,value)
 ent['MMDHLClient'..kind]=ent['MMDHLClient'..kind] or {}
 ent['MMDHLClient'..kind..'Revision']=ent['MMDHLClient'..kind..'Revision'] or {}
 ent['MMDHLClient'..kind][index]=value
 ent['MMDHLClient'..kind..'Revision'][index]=ent:GetNW2Int('MMDHL'..kind..'Revision'..index,0)
end
local function changed(ent,kind,index)
 local key='MMDHL'..kind..'Revision'..index ent:SetNW2Int(key,(ent:GetNW2Int(key,0)+1)%2147483647)
end
meta.GetMaterials=function(ent)
 local r=rig(ent) if not r or not r.materials then return original.GetMaterials(ent) end
 local out={} for i,m in ipairs(r.materials) do out[i]=m.path end return out
end
meta.GetSubMaterial=function(ent,index)
 local m=slot(ent,index) if not m or index<32 then return original.GetSubMaterial(ent,index) end
 local value=CLIENT and localOverride(ent,'Materials',index)
 if value~=nil and value~=false then return value end
 return ent:GetNW2String('MMDHLMaterial'..index,'')
end
meta.SetSubMaterial=function(ent,index,value)
 local r=rig(ent) if not r or not r.materials then return original.SetSubMaterial(ent,index,value) end
 ent.MMDHLMaterialRevision=(ent.MMDHLMaterialRevision or 0)+1
 if index==nil then
  original.SetSubMaterial(ent) ent.MMDHLClientMaterials={}
  if SERVER then for i=32,#r.materials-1 do ent:SetNW2String('MMDHLMaterial'..i,'') changed(ent,'Materials',i) end end
  return
 end
 if not slot(ent,index) then return end
 if index<32 then return original.SetSubMaterial(ent,index,value) end
 if value~=nil and not isstring(value) then return end
 if SERVER then ent:SetNW2String('MMDHLMaterial'..index,value or '') changed(ent,'Materials',index)
 else setOverride(ent,'Materials',index,value or '') end
end
meta.GetBodyGroups=function(ent)
 local r=rig(ent) if not r or not r.materials then return original.GetBodyGroups(ent) end
 local out={{id=0,name='carrier',num=1,submodels={[0]='carrier'}}}
 for i,m in ipairs(r.materials) do out[#out+1]={id=i,name=groupName(m),num=2,submodels={[0]=m.defaultHidden and 'hidden' or 'visible',[1]=m.defaultHidden and 'visible' or 'hidden'}} end
 return out
end
meta.GetNumBodyGroups=function(ent) local r=rig(ent) return r and r.materials and #r.materials+1 or original.GetNumBodyGroups(ent) end
meta.GetBodygroup=function(ent,index)
 local m,r=slot(ent,index-1) if not m or index<=31 then return original.GetBodygroup(ent,index) end
 local value=CLIENT and localOverride(ent,'Bodygroups',index)
 if value~=nil and value~=false then return value end
 return ent:GetNW2Int('MMDHLPart'..index,0)
end
meta.SetBodygroup=function(ent,index,value)
 local m=slot(ent,index-1) if not m then return original.SetBodygroup(ent,index,value) end
 ent.MMDHLMaterialRevision=(ent.MMDHLMaterialRevision or 0)+1
 value=value==1 and 1 or 0
 if index<=31 then return original.SetBodygroup(ent,index,value) end
 if SERVER then ent:SetNW2Int('MMDHLPart'..index,value) changed(ent,'Bodygroups',index)
 else setOverride(ent,'Bodygroups',index,value) end
end
meta.GetBodygroupCount=function(ent,index) if slot(ent,index-1) then return 2 end return original.GetBodygroupCount(ent,index) end
meta.GetBodygroupName=function(ent,index) local m=slot(ent,index-1) return m and groupName(m) or original.GetBodygroupName(ent,index) end
meta.FindBodygroupByName=function(ent,name) local r=rig(ent) if r and r.materials then for i,m in ipairs(r.materials) do if name==groupName(m) then return i end end end return original.FindBodygroupByName(ent,name) end
meta.SetBodyGroups=function(ent,groups)
 local r=rig(ent) if not r or not r.materials then return original.SetBodyGroups(ent,groups) end
 for i=1,#r.materials do ent:SetBodygroup(i,tonumber(groups:sub(i+1,i+1),36) or 0) end
end
-- Overflow parts (submaterials from slot 32, bodygroups from 32) and expressions
-- without a flex controller are networked by index. Those indices belong to one
-- model: the next model on this entity would read them as its own parts, so a
-- model change clears them (engine-side slots follow the engine's own rules).
function mmdhl.ClearAssetState(ent,rig)
 if not SERVER or not IsValid(ent) or not istable(rig) then return end
 local count=istable(rig.materials) and #rig.materials or 0
 for index=32,count-1 do
  if ent:GetNW2String('MMDHLMaterial'..index,'')~='' then ent:SetNW2String('MMDHLMaterial'..index,'') changed(ent,'Materials',index) end
 end
 for group=32,count do
  if ent:GetNW2Int('MMDHLPart'..group,0)~=0 then ent:SetNW2Int('MMDHLPart'..group,0) changed(ent,'Bodygroups',group) end
 end
 for i,morph in ipairs(istable(rig.morphs) and rig.morphs or {}) do
  if istable(morph) and not (isnumber(morph.native) and morph.native>=0) then
   for _,key in ipairs({'MMDHLMorph'..(i-1),morph.mmd~=nil and 'MMDHLMorph'..tostring(morph.mmd) or nil}) do
    if ent:GetNW2Float(key,0)~=0 then ent:SetNW2Float(key,0) end
   end
  end
 end
end
function mmdhl.IsMaterialVisible(ent,index)
 local m=slot(ent,index) if not m then return false end
 return (ent:GetBodygroup(index+1)==0)~=not not m.defaultHidden
end
-- Visibility follows networked part state, so it is re-read every few frames
-- (staggered per model) and immediately after a local edit, not every frame.
local SyncInterval=8
function mmdhl.SyncMaterialState(ent,instance)
 -- Another addon's copy of a player model shows the parts that player shows.
 local look=IsValid(ent.MMDHLCopyOf) and ent.MMDHLCopyOf or ent
 local frame=CLIENT and FrameNumber() or 0 local revision=look.MMDHLMaterialRevision or 0
 if CLIENT and ent.MMDVisibilitySent and ent.MMDVisibilityRevision==revision and frame<(ent.MMDVisibilityNext or 0) then return true end
 -- The first sync spreads models over the interval so they do not all re-sync on one frame.
 ent.MMDVisibilityNext=frame+SyncInterval+(ent.MMDVisibilityNext and 0 or ent:EntIndex()%SyncInterval) ent.MMDVisibilityRevision=revision
 local visible,opaque={},{}
 for i,m in ipairs(mmdhl.GetMaterials(ent)) do
  visible[i]=mmdhl.IsMaterialVisible(look,i-1)
  opaque[i]=not not (m.defaultHidden and visible[i])
 end
 local encoded=util.TableToJSON({visible=visible,forceOpaque=opaque})
 if ent.MMDVisibilitySent~=encoded then
  local _,err=mmdhl.native.SetMaterialVisibility(instance,encoded)
  if err then return false,err end
  ent.MMDVisibilitySent=encoded
 end
 return true
end
function mmdhl.SetMaterialVisible(ent,index,visible)
 local m=slot(ent,index) if not m then return false end
 if CLIENT and not ent.MMDHLEditorPreview and not mmdhl.IsClientOnly(ent) then net.Start('mmdhl_material_visibility') net.WriteEntity(ent) net.WriteUInt(index,16) net.WriteBool(visible) net.SendToServer()
 else ent:SetBodygroup(index+1,groupValue(m,visible)) end return true
end
-- Bodygroup presets from the library travel with spawn requests as an array of
-- booleans (one per material slot, true = shown). Clean before applying.
function mmdhl.CleanBodygroups(value)
 if not istable(value) then return nil end
 local out={} local count=0
 for i=1,math.min(#value,512) do if value[i]==true or value[i]==false then out[i]=value[i] count=count+1 else out[i]=nil end end
 return count>0 and out or nil
end
-- First-person arms choices map a material slot to -1 (exclude), 0 (automatic)
-- or 1 (include). util.JSONToTable turns the slot keys into numbers; kept that
-- way, the editor (string keys) shows Auto and slots 1..n encode as a JSON
-- array the carrier builder rejects. Keys are always decimal strings.
function mmdhl.CleanArmsParts(value)
 local out,count={},0
 if not istable(value) then return out end
 for key,mode in pairs(value) do
  local slot=tonumber(key)
  if slot and slot>=0 and slot<4096 and slot%1==0 and (mode==-1 or mode==0 or mode==1) and count<1024 then out[string.format('%d',slot)]=mode count=count+1 end
 end
 return out
end
function mmdhl.ApplyBodygroupState(ent,visible)
 visible=mmdhl.CleanBodygroups(visible) if not visible or not IsValid(ent) then return end
 for i,m in ipairs(mmdhl.GetMaterials(ent)) do if visible[i]~=nil then mmdhl.SetMaterialVisible(ent,i-1,visible[i]) end end
end
function mmdhl.CaptureMaterialState(ent)
 local out={version=2,overrides={},groups={}}
 for i in ipairs(mmdhl.GetMaterials(ent)) do local index=i-1 local value=ent:GetSubMaterial(index) if value~='' then out.overrides[tostring(index)]=value end if ent:GetBodygroup(i)==1 then out.groups[tostring(index)]=1 end end
 return out
end
function mmdhl.ApplyMaterialState(ent,state)
 if not istable(state) then return end
 -- JSONToTable converts numeric-looking object keys to numbers by default.
 -- In-memory dupes retain strings, while disk saves use numeric keys.
 local overrides,groups=state.overrides or {},state.groups or {}
 for i in ipairs(mmdhl.GetMaterials(ent)) do
  local index=tostring(i-1)
  ent:SetSubMaterial(i-1,overrides[index] or overrides[i-1])
  ent:SetBodygroup(i,groups[index] or groups[i-1] or 0)
 end
end
if SERVER then
 util.AddNetworkString('mmdhl_material_visibility')
 net.Receive('mmdhl_material_visibility',function(_,ply)
  local ent,index,visible=net.ReadEntity(),net.ReadUInt(16),net.ReadBool()
  local m=slot(ent,index)
  if m and mmdhl.CanEdit(ply,ent,'bodygroups') then ent:SetBodygroup(index+1,groupValue(m,visible)) end
 end)
else
 -- The stock property's wire ID is eight bits. Only MMD uses our wider route.
 local function install()
  local property=properties.List and properties.List.bodygroups
  if not property or property.MMDHLWide then return end property.MMDHLWide=true
  local set=property.SetBodyGroup
  property.SetBodyGroup=function(self,ent,group,value) local m=slot(ent,group-1) if m then return mmdhl.SetMaterialVisible(ent,group-1,(value==0)~=not not m.defaultHidden) end return set(self,ent,group,value) end
 end
 timer.Simple(0,install) hook.Add('InitPostEntity','MMDHL.MaterialProperties',install)
end

function mmdhl.GetRigForModel(model)
 if not isstring(model) then return end
 local short=model:match('^models/mmd/([a-f0-9]+)/[^/]+%.mdl$') if not short or #short~=16 then return end
 local entry=util.JSONToTable(file.Read('mmd_hotloader/names/rigs/'..short..'.json','DATA') or '') local key=entry and entry.id if not key then return end
 local r=mmdhl.rigs[key] or util.JSONToTable(file.Read('mmd_hotloader/rigs/'..key..'/rig.json','DATA') or '')
 if r and r.model==model then mmdhl.rigs[key]=r return r end
end
if CLIENT then
 mmdhl.editorPreviews=mmdhl.editorPreviews or {}
 function mmdhl.AttachEditorPreview(ent)
  if not IsValid(ent) then return false end
  local r=mmdhl.GetRigForModel(ent:GetModel()) if not r or not r.materials then return false end
  if ent.MMDHLEditorPreview then return true end
  mmdhl.native.RequestAsset(r.asset)
  local handle,err=mmdhl.native.CreateEditorPreview(r.asset,util.TableToJSON(r))
  if not handle then ent.MMDHLEditorError=err return false end
  ent.MMDHLEditorError=nil ent.MMDHLPreviewRig=r ent.MMDHLEditorPreview=handle mmdhl.editorPreviews[handle]=ent
  if mmdhl.NoteCarrier then mmdhl.NoteCarrier(ent) end
  local bounds=mmdhl.Decode(mmdhl.native.GetEditorPreviewBounds(handle))
  if bounds then ent:SetRenderBounds(Vector(unpack(bounds.minimum)),Vector(unpack(bounds.maximum))) end
  ent.RenderOverride=function(e)
   if not mmdhl.ImmediateRendering() then return end
   mmdhl.SyncMaterialState(e,handle)
   local lit,lightingError=mmdhl.native.SetupSourceLighting(e:GetPos()) if not lit then e.MMDHLEditorError=lightingError return end
   local matrix=Matrix() matrix:SetTranslation(e:GetPos()) matrix:SetAngles(e:GetAngles()) matrix:SetScale(Vector(1,1,1)*e:GetModelScale())
   cam.PushModelMatrix(matrix)
   for i,material in ipairs(r.materials) do if mmdhl.IsMaterialVisible(e,i-1) then
    local name=e:GetSubMaterial(i-1) if name=='' then name=e:GetMaterial() end if name=='' then name=material.path end
    local _,drawError=mmdhl.native.DrawEditorPreview(handle,i-1,name) if drawError then e.MMDHLEditorError=drawError break end
   end end
   cam.PopModelMatrix()
  end
  ent:CallOnRemove('MMDHL.EditorRelease',function() mmdhl.native.DestroyEditorPreview(handle) mmdhl.editorPreviews[handle]=nil end)
  return true
 end
 function mmdhl.GetModelMaterialMeshes(model,slot,mask)
  local r=mmdhl.GetRigForModel(model) if not r or not r.materials then return end
  mmdhl.native.RequestAsset(r.asset) local result={}
  local first,last=slot~=nil and slot+1 or 1,slot~=nil and slot+1 or #r.materials
  for i=first,last do local material=r.materials[i] local hidden=(mmdhl.meshVisibilityMasks or {})[model..':'..tostring(mask or 0)] if material and not (hidden and hidden[i]) then
   local raw=mmdhl.Decode(mmdhl.native.GetMaterialMesh(r.asset,i-1,r.scale))
   if raw then local vertices,triangles={},{} for j,v in ipairs(raw.vertices) do vertices[j]={pos=Vector(v[1],v[2],v[3]),normal=Vector(v[4],v[5],v[6]),u=v[7],v=v[8]} end for j,index in ipairs(raw.indices) do triangles[j]=vertices[index+1] end result[#result+1]={material=material.path,triangles=triangles} end
  end end return result
 end
 function mmdhl.GetMaterialPositions(ent,material)
  if not mmdhl.IsMMD(ent) or ent.MMDHLEditorPreview then return end
  for i,m in ipairs(mmdhl.GetMaterials(ent)) do if m.path==material then
   local raw=mmdhl.Decode(mmdhl.native.GetMaterialPositions(mmdhl.GetInstance(ent),i-1)) if not raw then return end
   local out={} for j,p in ipairs(raw) do out[j]=Vector(unpack(p)) end return out
  end end
 end
end

function mmdhl.GetMaterialVisibilityMask(ent)
 local hidden,bits={},{} local any=false
 for i in ipairs(mmdhl.GetMaterials(ent)) do hidden[i]=not mmdhl.IsMaterialVisible(ent,i-1) bits[i]=hidden[i] and '1' or '0' any=any or hidden[i] end
 if not any then return 0 end
 local signature=tonumber(util.CRC(table.concat(bits)))
 mmdhl.meshVisibilityMasks=mmdhl.meshVisibilityMasks or {}
 if table.Count(mmdhl.meshVisibilityMasks)>128 then mmdhl.meshVisibilityMasks={} end
 mmdhl.meshVisibilityMasks[ent:GetModel()..':'..signature]=hidden
 return signature
end

function mmdhl.GetMaterialAsset(path)
 if not isstring(path) then return end
 local short=path:match('^mmd/([a-f0-9]+)/[^/]+$') if not short or #short~=16 then return end
 local entry=util.JSONToTable(file.Read('mmd_hotloader/names/assets/'..short..'.json','DATA') or '')
 if entry and isstring(entry.id) and #entry.id==64 and not entry.id:find('[^a-f0-9]') then return entry.id end
end

local materialAssetMounts={}
-- Saved material-editor rules load before the model's first presentation. Mount
-- the verified asset package before Material/CreateMaterial can cache a lookup
-- of a VMT or VTF that is not on Source's search path yet. Only successful mounts
-- are remembered: a model arriving from a server can finish downloading later.
function mmdhl.MountMaterialAsset(path)
 if not CLIENT or not isstring(path) or not mmdhl.MountPackage then return false end
 local short=path:match('^mmd/([a-f0-9]+)/[^/]+$')
 if not short or #short~=16 then return false end
 if materialAssetMounts[short] then return true end
 local asset=mmdhl.GetMaterialAsset(path) if not asset then return false end
 local ok,err=mmdhl.MountPackage('data/mmd_hotloader/assets/'..asset..'/materials-v5.gma')
 if ok then materialAssetMounts[short]=true end
 return ok==true,err
end
