-- Static prop parts editor: keep chosen materials and/or a region of a model
-- and save the result as a named preset. A preset is a new prop cut by the
-- worker from the original, so it spawns, shares and duplicates like any prop.
local P=mmdhl.props
local Lib=P.library
local L=mmdhl.L
local axes={'x','y','z'}
local function region(spec,mins,maxs)
 -- Fractions 0..1 of the original bounds for each axis.
 local out={}
 for i,axis in ipairs(axes) do
  local size=maxs[axis]-mins[axis]
  local lo=spec and spec.box and (spec.box.min[i]-mins[axis])/math.max(size,1e-6) or 0
  local hi=spec and spec.box and (spec.box.max[i]-mins[axis])/math.max(size,1e-6) or 1
  out[axis]={math.Clamp(lo,0,1),math.Clamp(hi,0,1)}
 end
 return out
end
function P.OpenEditor(id)
 local UI=mmdhl.UI local entry=Lib.entries[id]
 if not UI or not entry then return end
 -- Editing a preset edits its original with the preset's selection.
 local parentId=entry.settings.parent and Lib.entries[entry.settings.parent] and entry.settings.parent or id
 local parent=Lib.entries[parentId]
 local editing=parentId~=id and id or nil
 local spec=editing and entry.settings.presetSpec or nil
 if IsValid(P.editor) then P.editor:Close() end
 local s,f=UI.metrics()
 local frame=vgui.Create('DFrame') P.editor=frame
 frame:SetTitle('') frame:SetSize(math.min(ScrW()*.92,s(1240)),math.min(ScrH()*.9,s(780))) frame:Center() frame:MakePopup() frame:DockPadding(s(14),s(12),s(14),s(12)) frame.btnMinim:SetVisible(false) frame.btnMaxim:SetVisible(false)
 frame.Paint=function(_,w,h) draw.RoundedBox(6,0,0,w,h,Color(246,248,251)) end
 local header=frame:Add('DPanel') header:Dock(TOP) header:SetTall(s(38)) header:SetPaintBackground(false)
 local close=UI.button(header,L'common.close',function() frame:Close() end,s(32),f.Body) close:Dock(RIGHT) close:SetWide(s(90))
 local title=UI.label(header,L('props.editor.title',{name=parent.name}),f.Title,s(36)) title:Dock(FILL)
 if mmdhl.names then mmdhl.names.Bind(title,function(p) p:SetText(L('props.editor.title',{name=(mmdhl.names.EntryName('static',parent))})) end) end
 local hint=UI.label(frame,L'props.editor.hint',f.Small,s(36)) hint:Dock(TOP) hint:SetWrap(true) hint:SetTextColor(UI.colors.muted)
 local left=frame:Add('DScrollPanel') left:Dock(LEFT) left:SetWide(s(420)) left:DockMargin(0,s(6),s(14),0)
 local status=UI.label(frame,'',f.Small,s(22)) status:Dock(BOTTOM) status:SetTextColor(UI.colors.muted)
 local function say(text,bad) status:SetText(text) status:SetTextColor(bad and Color(180,46,46) or UI.colors.muted) end
 local state={hidden={},useRegion=spec and spec.box~=nil or false,collision=spec and spec.collision or 'hull'}
 for _,m in ipairs(spec and spec.hidden or {}) do state.hidden[m+1]=true end
 -- Preset choice and name.
 local presetLabel=UI.label(left,L'props.editor.preset',f.Strong,s(26)) presetLabel:Dock(TOP)
 local combo=left:Add('DComboBox') combo:Dock(TOP) combo:SetTall(s(30)) UI.styleChoices(combo,s,f.Body) combo:DockMargin(0,0,0,s(6))
 combo:AddChoice(L'props.editor.new_preset',parentId,editing==nil)
 for _,preset in ipairs(Lib.Presets(parentId)) do combo:AddChoice(L('props.editor.edit_preset',{name=tostring(preset.settings.preset or preset.name)}),preset.id,preset.id==editing) end
 combo.OnSelect=function(_,_,_,chosen) if chosen~=(editing or parentId) then frame:Close() P.OpenEditor(chosen) end end
 local name=left:Add('DTextEntry') name:Dock(TOP) name:SetTall(s(30)) name:SetFont(f.Body) name:SetPlaceholderText(L'props.editor.name_placeholder') name:DockMargin(0,0,0,s(10))
 if editing then name:SetText(tostring(entry.settings.preset or '')) end
 -- Materials.
 local materialsLabel=UI.label(left,L'props.editor.materials',f.Strong,s(26)) materialsLabel:Dock(TOP)
 local tools=left:Add('DPanel') tools:Dock(TOP) tools:SetTall(s(28)) tools:SetPaintBackground(false) tools:DockMargin(0,0,0,s(6))
 local list=left:Add('DPanel') list:Dock(TOP) list:SetPaintBackground(false)
 local checks={}
 local function syncChecks() for i,c in pairs(checks) do c:SetChecked(not state.hidden[i]) end end
 local function setAll(fn) for i in pairs(checks) do state.hidden[i]=fn(i) or nil end syncChecks() end
 local showAll=UI.button(tools,L'props.editor.show_all',function() setAll(function() return false end) end,s(28),f.Small)
 local invert=UI.button(tools,L'props.editor.invert',function() setAll(function(i) return not state.hidden[i] end) end,s(28),f.Small)
 local hideAll=UI.button(tools,L'props.editor.hide_all',function() setAll(function() return true end) end,s(28),f.Small)
 tools.PerformLayout=function(_,w,h) local third=math.floor((w-s(12))/3) showAll:SetPos(0,0) showAll:SetSize(third,h) invert:SetPos(third+s(6),0) invert:SetSize(third,h) hideAll:SetPos(2*third+s(12),0) hideAll:SetSize(w-2*third-s(12),h) end
 -- Region cut: keep triangles whose centre lies inside the box.
 local regionLabel=UI.label(left,L'props.editor.region',f.Strong,s(26)) regionLabel:Dock(TOP) regionLabel:DockMargin(0,s(10),0,0)
 local regionCheck=UI.checkbox(left,L'props.editor.region_only',nil,f.Body,s(28)) regionCheck:Dock(TOP) regionCheck:SetChecked(state.useRegion)
 regionCheck.OnChange=function(_,v) state.useRegion=v end
 local sliders={}
 local info,mins,maxs,box
 local makeButtons
 local function build(renderInfo)
  info=renderInfo mins=P.Vector(info.mins) maxs=P.Vector(info.maxs)
  box=region(spec,mins,maxs)
  -- Triangle counts per material from the render parts.
  local counts={} for _,part in ipairs(info.parts or {}) do counts[part.material+1]=(counts[part.material+1] or 0)+part.count/3 end
  for i,m in ipairs(info.materials) do
   local function row(name) return L('props.editor.material_row',{number=i,name=name,triangles=string.Comma(math.floor(counts[i] or 0))}) end
   local c=UI.checkbox(list,row(tostring(m.name or L('props.editor.material_number',{number=i}))),nil,f.Body,s(26))
   if mmdhl.names and isstring(m.name) then mmdhl.names.Bind(c,function(p) mmdhl.names.SetCheckboxText(p,row(mmdhl.names.Both(m.name,parentId))) end) end
   c:Dock(TOP) c.OnChange=function(_,v) state.hidden[i]=not v or nil end checks[i]=c
  end
  list:SetTall(#info.materials*s(26))
  syncChecks()
  for _,axis in ipairs(axes) do
   for side=1,2 do
    local sl=left:Add('DNumSlider') sl:Dock(TOP) sl:SetTall(s(28)) sl:SetText(side==1 and L('props.editor.axis_from',{axis=axis:upper()}) or L('props.editor.axis_to',{axis=axis:upper()})) sl:SetMinMax(0,100) sl:SetDecimals(0) sl:SetDark(true) sl.Label:SetFont(f.Body)
    sl:SetValue(box[axis][side]*100) sl.OnValueChanged=function(_,v) box[axis][side]=v/100 if side==1 and box[axis][1]>box[axis][2] then box[axis][2]=box[axis][1] sliders[axis][2]:SetValue(v) elseif side==2 and box[axis][2]<box[axis][1] then box[axis][1]=box[axis][2] sliders[axis][1]:SetValue(v) end state.useRegion=true regionCheck:SetChecked(true) end
    sliders[axis]=sliders[axis] or {} sliders[axis][side]=sl
   end
  end
  local collisionLabel=UI.label(left,L'props.editor.collision',f.Strong,s(26)) collisionLabel:Dock(TOP) collisionLabel:DockMargin(0,s(10),0,0)
  local collision=left:Add('DComboBox') collision:Dock(TOP) collision:SetTall(s(30)) UI.styleChoices(collision,s,f.Body)
  collision:AddChoice(L'props.editor.collision_hull','hull',state.collision~='balanced') collision:AddChoice(L'props.editor.collision_detailed','balanced',state.collision=='balanced')
  collision.OnSelect=function(_,_,_,v) state.collision=v end
  local buttons=left:Add('DPanel') buttons:Dock(TOP) buttons:SetTall(s(76)) buttons:SetPaintBackground(false) buttons:DockMargin(0,s(12),0,s(4))
  makeButtons(buttons)
  UI.ownScale(left) left:InvalidateLayout(true)
 end
 makeButtons=function(buttons)
 local save=UI.button(buttons,editing and L'props.editor.save_changes' or L'props.editor.save_preset',function()
  local presetName=string.Trim(name:GetText())
  if presetName=='' then say(L'props.editor.name_required',true) return end
  local hidden={} for i in pairs(state.hidden) do hidden[#hidden+1]=i-1 end table.sort(hidden)
  if #hidden>=#info.materials then say(L'props.editor.keep_material',true) return end
  local out={name=utf8.sub(presetName,1,60),hidden=hidden,collision=state.collision}
  if state.useRegion then
   local lo,hi={},{}
   for i,axis in ipairs(axes) do local size=maxs[axis]-mins[axis] lo[i]=mins[axis]+size*box[axis][1] hi[i]=mins[axis]+size*box[axis][2] end
   out.box={min=lo,max=hi}
  end
  local ok,err=Lib.SavePreset(parentId,out,editing)
  if not ok then say(err,true) return end
  say(L'props.editor.saving')
  timer.Simple(.5,function() if IsValid(frame) then frame:Close() end end)
 end,s(34),f.Strong,true)
 local reset=UI.button(buttons,L'props.editor.reset',function()
  state.hidden={} syncChecks() state.useRegion=false regionCheck:SetChecked(false)
  for _,axis in ipairs(axes) do box[axis]={0,1} if sliders[axis] then sliders[axis][1]:SetValue(0) sliders[axis][2]:SetValue(100) end end
 end,s(34),f.Body)
 local delete=UI.button(buttons,L'props.editor.delete_preset',function()
  if not editing then return end
  Derma_Query(L('props.editor.delete_question',{name=tostring(entry.settings.preset)}),L'props.editor.delete_title',L'common.delete',function()
   Lib.Delete({editing},function(ok,message) if ok and IsValid(frame) then frame:Close() P.OpenEditor(parentId) elseif IsValid(frame) then say(message,true) end end)
  end,L'common.cancel')
 end,s(34),f.Body,'danger') delete:SetEnabled(editing~=nil)
 local cancel=UI.button(buttons,L'common.cancel',function() frame:Close() end,s(34),f.Body)
 buttons.PerformLayout=function(_,w,h)
  local half=math.floor((w-s(6))/2) local row=s(34)
  save:SetPos(0,0) save:SetSize(half,row) reset:SetPos(half+s(6),0) reset:SetSize(w-half-s(6),row)
  delete:SetPos(0,row+s(8)) delete:SetSize(half,row) cancel:SetPos(half+s(6),row+s(8)) cancel:SetSize(w-half-s(6),row)
 end
 end
 -- Preview: hidden materials are skipped and the region is clipped live
 -- (six custom clip planes on Windows), with the box outlined.
 P.AcquireRender(parentId)
 local camera={yaw=-65,pitch=18}
 local preview=frame:Add('DPanel') preview:Dock(FILL) preview:DockMargin(0,s(6),0,0)
 preview.OnMousePressed=function(p,key) p:MouseCapture(true) camera.drag={key=key,x=gui.MouseX(),y=gui.MouseY()} end
 preview.OnMouseReleased=function(p) p:MouseCapture(false) camera.drag=nil end
 preview.OnMouseWheeled=function(_,delta) if camera.distance then camera.distance=math.Clamp(camera.distance*(1-delta*.1),camera.radius*.25,camera.radius*25) end return true end
 preview.Paint=function(p,w,h)
  draw.RoundedBox(6,0,0,w,h,Color(28,36,48))
  local cached=P.RenderCache[parentId]
  if not cached or cached.state~='ready' then
   local s2=P.AssetStatus[parentId]
   draw.SimpleText(cached and cached.state=='failed' and L('props.editor.preview_unavailable',{reason=tostring(cached.error)}) or (s2 and s2.detail) or L'props.editor.preview_preparing',f.Body,w/2,h/2,Color(184,202,222),TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER)
   return
  end
  if not info then build(cached.info) end
  if not camera.distance then
   camera.radius=math.max(.5,(maxs-mins):Length()*.5) camera.target=(mins+maxs)*.5
   camera.distance=camera.radius/math.sin(math.rad(21))*1.1
  end
  if camera.drag then
   local x,y=gui.MouseX(),gui.MouseY()
   if camera.drag.key==MOUSE_RIGHT then camera.target.z=camera.target.z+(y-camera.drag.y)*camera.distance/math.max(h,1)
   else camera.yaw=camera.yaw+(x-camera.drag.x)*.5 camera.pitch=math.Clamp(camera.pitch+(y-camera.drag.y)*.3,-80,80) end
   camera.drag.x=x camera.drag.y=y
  end
  local x,y=p:LocalToScreen(0,0) local angle=Angle(camera.pitch,camera.yaw,0)
  local lo,hi=Vector(),Vector()
  for _,axis in ipairs(axes) do local size=maxs[axis]-mins[axis] lo[axis]=mins[axis]+size*box[axis][1] hi[axis]=mins[axis]+size*box[axis][2] end
  render.ClearDepth() render.SetScissorRect(x,y,x+w,y+h,true)
  cam.Start3D(camera.target+angle:Forward()*camera.distance,(-angle:Forward()):Angle(),42,x,y,w,h,math.max(.5,camera.distance*.01),camera.distance+camera.radius*4)
  render.SuppressEngineLighting(true)
  for i,light in ipairs({{.8,.8,.8},{.55,.55,.6},{.72,.72,.72},{.72,.72,.72},{1,1,1},{.3,.3,.33}}) do render.SetModelLighting(i-1,light[1],light[2],light[3]) end
  local clipping=state.useRegion local previous
  if clipping then
   previous=render.EnableClipping(true)
   render.PushCustomClipPlane(Vector(1,0,0),lo.x) render.PushCustomClipPlane(Vector(-1,0,0),-hi.x)
   render.PushCustomClipPlane(Vector(0,1,0),lo.y) render.PushCustomClipPlane(Vector(0,-1,0),-hi.y)
   render.PushCustomClipPlane(Vector(0,0,1),lo.z) render.PushCustomClipPlane(Vector(0,0,-1),-hi.z)
  end
  local ok,err=pcall(function()
   P.DrawAsset(parentId,vector_origin,angle_zero,false,nil,1,false,state.hidden)
   P.DrawAsset(parentId,vector_origin,angle_zero,true,nil,1,false,state.hidden)
  end)
  if clipping then for _=1,6 do render.PopCustomClipPlane() end render.EnableClipping(previous) end
  render.SuppressEngineLighting(false)
  if clipping then render.DrawWireframeBox(vector_origin,angle_zero,lo,hi,Color(255,196,64),true) end
  cam.End3D() render.SetScissorRect(0,0,0,0,false)
  if not ok then say(tostring(err),true) end
  draw.SimpleText(L'props.editor.controls',f.Small,s(12),s(12),Color(184,202,222))
 end
 frame.OnRemove=function() P.ReleaseRender(parentId) end
 UI.ownScale(frame)
 return frame
end
