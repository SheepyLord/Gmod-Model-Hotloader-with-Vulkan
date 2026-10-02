-- Character bodygroup presets. Each PMX material slot is a carrier bodygroup;
-- a preset stores absolute visibility per slot, and the default preset is
-- applied to new ragdolls, NPCs and player models. Saved in library settings.
local native=mmdhl.native
local library=mmdhl.library
local L=mmdhl.L
local function store(id) local entry=library.entries[id] return entry and entry.settings.bodygroups or {} end
-- Preset names are table keys, and util.JSONToTable turns numeric ones ("1",
-- "2024") into numbers when the library entry is read back. Names are strings.
function mmdhl.BodygroupPresets(id)
 local out={} for name,preset in pairs(store(id).presets or {}) do out[tostring(name)]=preset end return out
end
function mmdhl.BodygroupDefault(id) local name=store(id).default return name~=nil and tostring(name) or nil end
-- The visibility array sent with a spawn request, or nil for the authored look.
function mmdhl.BodygroupSpawnState(id,name)
 if not name or name=='' then return nil end
 local preset=mmdhl.BodygroupPresets(id)[name]
 return preset and preset.visible or nil
end
local function authored(info)
 local visible={} for i,m in ipairs(info.materials or {}) do visible[i]=(m.alpha or 1)>0 end return visible
end
local function save(id,presets,default)
 return library.Update(id,{bodygroups={presets=presets,default=default}})
end
function mmdhl.OpenBodygroupEditor(id,onChange)
 local UI=mmdhl.UI local entry=library.entries[id]
 if not UI or not entry then return end
 if IsValid(mmdhl.bodygroupEditor) then mmdhl.bodygroupEditor:Close() end
 local s,f=UI.metrics() local info=entry.info
 local frame=vgui.Create('DFrame') mmdhl.bodygroupEditor=frame
 frame:SetTitle('') frame:SetSize(math.min(ScrW()*.9,s(1080)),math.min(ScrH()*.9,s(720))) frame:Center() frame:MakePopup() frame:DockPadding(s(14),s(12),s(14),s(12)) frame.btnMinim:SetVisible(false) frame.btnMaxim:SetVisible(false)
 frame.Paint=function(_,w,h) draw.RoundedBox(6,0,0,w,h,Color(246,248,251)) end
 local header=frame:Add('DPanel') header:Dock(TOP) header:SetTall(s(38)) header:SetPaintBackground(false)
 local close=UI.button(header,L'common.close',function() frame:Close() end,s(32),f.Body) close:Dock(RIGHT) close:SetWide(s(90))
 local title=UI.label(header,L'bodygroups.title'..' · '..entry.name,f.Title,s(36)) title:Dock(FILL)
 if mmdhl.names then mmdhl.names.Bind(title,function(p) p:SetText(L'bodygroups.title'..' · '..(mmdhl.names.EntryName('character',entry))) end) end
 local hint=UI.label(frame,L'bodygroups.hint',f.Small,s(36)) hint:Dock(TOP) hint:SetWrap(true) hint:SetTextColor(UI.colors.muted)
 local left=frame:Add('DPanel') left:Dock(LEFT) left:SetWide(s(390)) left:SetPaintBackground(false) left:DockMargin(0,s(6),s(14),0)
 local presets=table.Copy(mmdhl.BodygroupPresets(id)) local default=mmdhl.BodygroupDefault(id)
 local visible=authored(info) local current=''
 -- Preset selection and naming.
 local presetRow=left:Add('DPanel') presetRow:Dock(TOP) presetRow:SetTall(s(30)) presetRow:SetPaintBackground(false) presetRow:DockMargin(0,0,0,s(6))
 local combo=presetRow:Add('DComboBox') combo:Dock(FILL) UI.styleChoices(combo,s,f.Body)
 local nameEntry=left:Add('DTextEntry') nameEntry:Dock(TOP) nameEntry:SetTall(s(30)) nameEntry:SetFont(f.Body) nameEntry:SetPlaceholderText(L'bodygroups.name_placeholder') nameEntry:DockMargin(0,0,0,s(6))
 local checks={}
 local function syncChecks() for i,c in pairs(checks) do c:SetChecked(visible[i]==true) end end
 local function fillCombo()
  combo:Clear() combo:AddChoice(L'bodygroups.new_preset','',current=='')
  local names=table.GetKeys(presets) table.sort(names)
  for _,name in ipairs(names) do combo:AddChoice(name..(name==default and '   ★ '..L'bodygroups.default_tag' or ''),name,name==current) end
 end
 combo.OnSelect=function(_,_,_,name)
  current=name
  visible=name~='' and table.Copy(presets[name].visible) or authored(info)
  nameEntry:SetText(name) syncChecks()
 end
 fillCombo()
 -- Part checklist.
 local tools=left:Add('DPanel') tools:Dock(TOP) tools:SetTall(s(30)) tools:SetPaintBackground(false) tools:DockMargin(0,0,0,s(6))
 local function setAll(fn) for i in ipairs(info.materials or {}) do visible[i]=fn(i) end syncChecks() end
 local showAll=UI.button(tools,L'bodygroups.show_all',function() setAll(function() return true end) end,s(30),f.Small)
 local asAuthored=UI.button(tools,L'bodygroups.as_authored',function() local a=authored(info) setAll(function(i) return a[i] end) end,s(30),f.Small)
 local hideAll=UI.button(tools,L'bodygroups.hide_all',function() setAll(function() return false end) end,s(30),f.Small)
 tools.PerformLayout=function(_,w,h) local third=math.floor((w-s(12))/3) showAll:SetPos(0,0) showAll:SetSize(third,h) asAuthored:SetPos(third+s(6),0) asAuthored:SetSize(third,h) hideAll:SetPos(2*third+s(12),0) hideAll:SetSize(w-2*third-s(12),h) end
 local buttons=left:Add('DPanel') buttons:Dock(BOTTOM) buttons:SetTall(s(76)) buttons:SetPaintBackground(false) buttons:DockMargin(0,s(8),0,0)
 local status=UI.label(left,'',f.Small,s(22)) status:Dock(BOTTOM) status:SetTextColor(UI.colors.muted)
 local list=left:Add('DScrollPanel') list:Dock(FILL)
 list.Paint=function(_,w,h) draw.RoundedBox(4,0,0,w,h,color_white) end
 for i,m in ipairs(info.materials or {}) do
  local partName=tostring(m.name or L('bodygroups.material_fallback',{index=i}))
  local function label(name) return (m.alpha or 1)<=0 and L('bodygroups.part_hidden',{index=i,name=name}) or string.format('%d. %s',i,name) end
  local c=UI.checkbox(list,label(partName),nil,f.Body,s(28)) c:Dock(TOP) c:DockMargin(s(8),s(2),s(8),0)
  -- Translated part names keep the authored name beside them: it is what other tools show.
  if mmdhl.names and m.name then mmdhl.names.Bind(c,function(p) mmdhl.names.SetCheckboxText(p,label(mmdhl.names.Both(partName,entry.id))) end) end
  c.OnChange=function(_,value) visible[i]=value end
  checks[i]=c
 end
 syncChecks()
 local function persist(message)
  if not save(id,presets,default) then status:SetText(L'bodygroups.save_failed') status:SetTextColor(Color(180,46,46)) return end
  entry=library.entries[id] fillCombo() status:SetText(message) status:SetTextColor(UI.colors.muted)
  if onChange then onChange() end
 end
 local saveButton=UI.button(buttons,L'bodygroups.save',function()
  local name=string.Trim(nameEntry:GetText())
  if name=='' then status:SetText(L'bodygroups.name_required') status:SetTextColor(Color(180,46,46)) return end
  name=utf8.sub(name,1,60) presets[name]={visible=table.Copy(visible)} current=name
  persist(L('bodygroups.saved',{name=name}))
 end,s(34),f.Strong,true)
 local defaultButton=UI.button(buttons,L'bodygroups.use_default',function()
  local name=string.Trim(nameEntry:GetText())
  if current~='' and presets[current] then default=current persist(L('bodygroups.now_default',{name=current}))
  elseif name~='' then presets[name]={visible=table.Copy(visible)} current=name default=name persist(L('bodygroups.saved_default',{name=name}))
  else default=nil persist(L'bodygroups.authored_default') end
 end,s(34),f.Strong,'success')
 local deleteButton=UI.button(buttons,L'bodygroups.delete',function()
  if current=='' or not presets[current] then status:SetText(L'bodygroups.choose_to_delete') return end
  local name=current presets[name]=nil if default==name then default=nil end current='' visible=authored(info) nameEntry:SetText('') syncChecks()
  persist(L('bodygroups.deleted',{name=name}))
 end,s(34),f.Body,'danger')
 local clearDefault=UI.button(buttons,L'bodygroups.clear_default',function() default=nil persist(L'bodygroups.authored_default') end,s(34),f.Body)
 buttons.PerformLayout=function(_,w,h)
  local half=math.floor((w-s(6))/2) local row=s(34)
  saveButton:SetPos(0,0) saveButton:SetSize(half,row) defaultButton:SetPos(half+s(6),0) defaultButton:SetSize(w-half-s(6),row)
  deleteButton:SetPos(0,row+s(8)) deleteButton:SetSize(half,row) clearDefault:SetPos(half+s(6),row+s(8)) clearDefault:SetSize(w-half-s(6),row)
 end
 -- Live preview of the selection (the shared native library preview).
 local preview=frame:Add('DPanel') preview:Dock(FILL) preview:DockMargin(0,s(6),0,0)
 local camera={yaw=180,pitch=0,distance=185,target=Vector(0,0,0)}
 local handle,previewInfo
 do
  local bounds=info.bounds
  if bounds then
   local lo,hi=Vector(unpack(bounds[1])),Vector(unpack(bounds[2])) local size=(hi-lo)*3.23656
   camera.distance=math.max(40,math.max(size.x,size.y,size.z)*1.3)
  end
 end
 preview.OnMousePressed=function(p,key) p:MouseCapture(true) camera.drag={key=key,x=gui.MouseX(),y=gui.MouseY()} end
 preview.OnMouseReleased=function(p) p:MouseCapture(false) camera.drag=nil end
 preview.OnMouseWheeled=function(_,delta) camera.distance=math.Clamp(camera.distance*(1-delta*.1),15,800) return true end
 preview.Paint=function(p,w,h)
  draw.RoundedBox(6,0,0,w,h,Color(28,36,48))
  if not handle then
   if mmdhl.previewOwner~=frame then
    if IsValid(mmdhl.previewOwner) and mmdhl.previewOwner.previewHandle then mmdhl.previewOwner.previewHandle=nil mmdhl.previewOwner.previewAsset=nil end
    native.ClearPreview() mmdhl.previewOwner=frame
   end
   native.RequestAsset(id)
   previewInfo=mmdhl.Decode(native.AssetInfo(id))
   if previewInfo then handle=native.CreatePreview(id,'{}') end
   draw.SimpleText(L'bodygroups.loading_preview',f.Body,w/2,h/2,Color(184,202,222),TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER) return
  end
  if camera.drag then
   local x,y=gui.MouseX(),gui.MouseY()
   if camera.drag.key==MOUSE_RIGHT then camera.target.z=camera.target.z+(y-camera.drag.y)*camera.distance/math.max(h,1)
   else camera.yaw=camera.yaw+(x-camera.drag.x)*.5 camera.pitch=math.Clamp(camera.pitch+(y-camera.drag.y)*.3,-65,65) end
   camera.drag.x=x camera.drag.y=y
  end
  if not mmdhl.ImmediateRendering() then return end
  local x,y=p:LocalToScreen(0,0) local angle=Angle(camera.pitch,camera.yaw,0)
  render.ClearDepth() render.SetScissorRect(x,y,x+w,y+h,true)
  cam.Start3D(camera.target+angle:Forward()*camera.distance,(-angle:Forward()):Angle(),42,x,y,w,h,1,4096)
  local ok,err=pcall(mmdhl.DrawLibraryPreview,handle,id,previewInfo,visible)
  cam.End3D() render.SetScissorRect(0,0,0,0,false)
  if not ok then status:SetText(tostring(err)) end
  draw.SimpleText(L'bodygroups.preview_controls',f.Small,s(12),s(12),Color(184,202,222))
 end
 frame.OnRemove=function()
  if mmdhl.previewOwner==frame then native.ClearPreview() mmdhl.previewOwner=nil end
 end
 if default and presets[default] then combo:ChooseOptionID(1) for i,choice in ipairs(combo.Data or {}) do if choice==default then combo:ChooseOptionID(i) end end end
 UI.ownScale(frame)
 return frame
end
