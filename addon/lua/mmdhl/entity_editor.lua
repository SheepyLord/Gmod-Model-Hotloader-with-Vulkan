-- Detailed posing stays separate from choosing/importing a library model.
local L=mmdhl.L
function mmdhl.OpenEntityEditor(ent)
 if not mmdhl.IsMMD(ent) then return end
 local id=mmdhl.GetAsset(ent) local entry=mmdhl.library.entries[id]
 local names=mmdhl.names
 local frame=vgui.Create('DFrame') frame:SetTitle(L('editor.title',{name=entry and (names and names.EntryName('character',entry) or entry.name) or id:sub(1,12)})) frame:SetSize(620,math.min(860,ScrH()-100)) frame:Center() frame:MakePopup()
 local scroll=frame:Add('DScrollPanel') scroll:Dock(FILL)
 local function action(text,fn)
  local b=scroll:Add('DButton') b:Dock(TOP) b:DockMargin(4,4,4,0) b:SetTall(32) b:SetText(text) b.DoClick=fn return b
 end
 local function tool(mode)
  RunConsoleCommand('gmod_toolmode',mode) RunConsoleCommand('use','gmod_tool') frame:Close() mmdhl.CloseLibrary()
 end
 action(L'editor.physgun',function() RunConsoleCommand('use','weapon_physgun') frame:Close() mmdhl.CloseLibrary() end)
 action(L'editor.faceposer',function() tool('faceposer') end)
 action(L'editor.fingerposer',function() tool('finger') end)
 action(L'editor.eyeposer',function() tool('eyeposer') end)
 action(L'editor.freeze',function() if IsValid(ent) then mmdhl.Action('freeze',nil,ent) end end)
 action(L'editor.reset_physics',function() if IsValid(ent) then mmdhl.ResetPhysics(ent) end end)
 local globalLabel=scroll:Add('DLabel') globalLabel:Dock(TOP) globalLabel:SetTall(28) globalLabel:SetDark(true) globalLabel:SetText(L'editor.physics_settings')
 local backend=scroll:Add('DComboBox') backend:Dock(TOP) backend:SetTall(32)
 mmdhl.BindGlobalChoice(backend,'secondaryBackend',mmdhl.SecondaryBackends)
 backend:SetTooltip(L'editor.backend_tooltip')
 local backendStatus=scroll:Add('DLabel') backendStatus:Dock(TOP) backendStatus:SetTall(62) backendStatus:SetDark(true) backendStatus:SetWrap(true)
 backendStatus.Think=function(label)
  if (label.nextCheck or 0)>RealTime() then return end label.nextCheck=RealTime()+1
  local state=mmdhl.GetSecondaryBackend(ent) if not state then return end
  local name=state.effective for _,mode in ipairs(mmdhl.SecondaryBackends) do if mode.id==state.effective then name=mode.name break end end
  local reason=state.reason or ''
  if state.effective=='gpu_vulkan' and reason=='' then
   -- Only the bundled DXVK shares its device with the solver (installation.lua reports the renderer).
   local status=mmdhl.GetInstallationStatus() local renderer=status and status.renderer
   reason=renderer and renderer.kind~='dxvk' and L'editor.vulkan_without_dxvk' or L'editor.experimental_backend'
  elseif state.effective=='gpu_opencl' and reason=='' then reason=L'editor.experimental_backend' end
  label:SetText(name..(reason~='' and (' - '..reason) or ''))
 end
 action(L'editor.reload',function() mmdhl.library.ReloadCharacter(id) end)
 -- Ragdolls open the physics editor; NPCs and player models inherit what is saved for new spawns.
 if mmdhl.OpenPhysicsEditor then
  local physics=action(L'physics_editor.open_button',function() if IsValid(ent) then frame:Close() mmdhl.OpenPhysicsEditor(ent) end end)
  if ent:GetClass()~='prop_ragdoll' then physics:SetEnabled(false) physics:SetTooltip(L'physics_editor.actor_hint') end
 end
 if ent:GetClass()=='prop_ragdoll' and mmdhl.CollisionEditor then mmdhl.CollisionEditor(scroll,ent) end
 local collisions=scroll:Add('DLabel') collisions:Dock(TOP) collisions:SetTall(26) collisions:SetText(L'physics.collision.label') collisions:SetDark(true)
 for _,target in ipairs(mmdhl.CollisionTargets) do
  local box=scroll:Add('DCheckBoxLabel') box:Dock(TOP) box:DockMargin(12,0,0,4) box:SetText(target.name) box:SetDark(true) box:SetTooltip(target.tooltip) mmdhl.BindCollisionCheckbox(box,target.flag)
 end
 local collisionHelp=scroll:Add('DLabel') collisionHelp:Dock(TOP) collisionHelp:SetTall(40) collisionHelp:SetWrap(true) collisionHelp:SetText(L'editor.collision_help') collisionHelp:SetDark(true)
 local parts=scroll:Add('DCollapsibleCategory') parts:Dock(TOP) parts:SetLabel(L'editor.visible_parts') parts:SetExpanded(false)
 local contents=vgui.Create('DListLayout',parts) parts:SetContents(contents)
 for i,material in ipairs(mmdhl.GetMaterials(ent)) do local control=contents:Add('DCheckBoxLabel') control:SetText(material.name) if names then names.Bind(control,function(p) names.SetCheckboxText(p,names.Both(material.name,id)) end) end control:SetDark(true) control:SetTall(25) control:SetValue(mmdhl.IsMaterialVisible(ent,i-1)) control.OnChange=function(_,value) mmdhl.SetMaterialVisible(ent,i-1,value) end end
 local heading=scroll:Add('DLabel') heading:Dock(TOP) heading:SetTall(32) heading:SetText(L'editor.expressions') heading:SetDark(true)
 local search=scroll:Add('DTextEntry') search:Dock(TOP) search:SetTall(28) search:SetPlaceholderText(L'editor.filter_expressions')
 local sliders={}
 for _,morph in ipairs(mmdhl.GetMorphs(ent)) do
  local slider=scroll:Add('DNumSlider') slider:Dock(TOP) slider:SetTall(30) slider:SetDark(true) slider:SetText(morph.displayName or morph.name) slider:SetTooltip(morph.original) slider:SetMinMax(0,1) slider:SetDecimals(2) slider:SetValue(mmdhl.GetMorphWeight(ent,morph.mmd) or 0)
  slider.OnValueChanged=function(_,value) if IsValid(ent) then mmdhl.SetMorphWeight(ent,morph.mmd,value) end end
  local item={panel=slider,name=((morph.displayName or morph.name)..' '..morph.name..' '..morph.original):lower()} sliders[#sliders+1]=item
  if names then names.Bind(slider,function(p) local shown=names.MorphLabel(morph,id) p:SetText(shown) item.name=(shown..' '..morph.name..' '..morph.original):lower() end) end
 end
 search.OnChange=function() local q=search:GetText():lower() for _,v in ipairs(sliders) do v.panel:SetVisible(q=='' or v.name:find(q,1,true)~=nil) end scroll:InvalidateLayout() end
 action(L'editor.reset_expressions',function() if not IsValid(ent) then return end local weights={} for i in ipairs(mmdhl.GetMorphs(ent)) do weights[i]=0 end mmdhl.SetMorphWeights(ent,weights) for _,v in ipairs(sliders) do v.panel:SetValue(0) end end)
 frame.Think=function() if not IsValid(ent) then frame:Close() end end
end
