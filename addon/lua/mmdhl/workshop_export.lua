-- Export dialog: packs selected library models into a Workshop-ready .gma in
-- data/mmd_hotloader/exports (native), with publishing help and, when a
-- character preview is open, a 512x512 icon from it.
local native=mmdhl.native
local L=mmdhl.L
local W=mmdhl.workshop
local Tags={'fun','roleplay','scenic','movie','realism','cartoon','water','comic','build'}
-- Everything a package carries per model, taken from the library.
local function characterItem(id,entry)
 local s=entry.settings or {}
 local item={kind='character',asset=id,name=entry.name,settings={spawn=s.spawn,bodygroups=s.bodygroups},terms=mmdhl.terms and mmdhl.terms.ForPackage('character',id) or nil}
 local arms=mmdhl.GetArmsParts and mmdhl.GetArmsParts(id) or {}
 if next(arms) then item.arms={} for k,v in pairs(arms) do item.arms[tostring(k)]=v end end
 local fit=util.JSONToTable(file.Read('mmd_hotloader/fit_overrides/'..id..'.json','DATA') or '')
 if istable(fit) then item.fit=fit end
 return item
end
local function propItem(id,entry) return {kind='static',asset=id,name=entry.name,settings={spawn=entry.settings and entry.settings.spawn},terms=mmdhl.terms and mmdhl.terms.ForPackage('static',id) or nil} end
local function candidates()
 local list={}
 for id,entry in pairs(mmdhl.library and mmdhl.library.entries or {}) do if not entry.shared and W.InCache('character',id) then list[#list+1]={kind='character',id=id,entry=entry} end end
 for id,entry in pairs(mmdhl.props and mmdhl.props.library and mmdhl.props.library.entries or {}) do if not entry.shared and W.InCache('static',id) then list[#list+1]={kind='static',id=id,entry=entry} end end
 table.sort(list,function(a,b) if a.kind~=b.kind then return a.kind=='character' end return a.entry.name:lower()<b.entry.name:lower() end)
 return list
end
-- Renders the library's current character preview into a JPEG beside the export.
local function captureIcon(assets,name,done)
 local owner=mmdhl.previewOwner
 if not IsValid(owner) or not owner.previewHandle or not assets[owner.previewAsset] or not mmdhl.ImmediateRendering() then done(false) return end
 local target=GetRenderTargetEx('mmdhl_export_icon_v1',512,512,RT_SIZE_NO_CHANGE,MATERIAL_RT_DEPTH_SEPARATE,bit.bor(2,4,8),0,IMAGE_FORMAT_RGBA8888)
 local attempts=0
 hook.Add('RenderScreenspaceEffects','MMDHL.ExportIcon',function()
  attempts=attempts+1
  local data
  if IsValid(owner) and owner.previewHandle then
   local angle=Angle(owner.pitch or 5,owner.yaw or 180,0)
   render.PushRenderTarget(target,0,0,512,512) render.Clear(34,44,58,255,true,true)
   cam.Start3D((owner.target or vector_origin)+angle:Forward()*(owner.distance or 80),(-angle:Forward()):Angle(),42,0,0,512,512,1,4096)
   local visible=mmdhl.BodygroupSpawnState and mmdhl.BodygroupSpawnState(owner.previewAsset,owner.bodygroupPreset) or nil
   local ok=pcall(mmdhl.DrawLibraryPreview,owner.previewHandle,owner.previewAsset,owner.previewInfo,visible)
   cam.End3D()
   if ok then data=render.Capture({format='jpeg',x=0,y=0,w=512,h=512,quality=92}) end
   render.PopRenderTarget()
  end
  if data or attempts>=3 then
   hook.Remove('RenderScreenspaceEffects','MMDHL.ExportIcon')
   if data then file.Write('mmd_hotloader/exports/'..name..'.jpg',data) end
   done(data~=nil and file.Exists('mmd_hotloader/exports/'..name..'.jpg','DATA'))
  end
 end)
end
local function publishCommand(result,icon)
 local gma=result.path
 local jpg=icon and (result.folder..'\\'..result.file:gsub('%.gma$','.jpg')) or L'export.icon_placeholder'
 return string.format('"%s" create -addon "%s" -icon "%s"',result.gmpublish or 'gmpublish.exe',gma,jpg)
end
function mmdhl.OpenPackageExport(kind,ids)
 if not native.StartPackageExport then Derma_Message(L'export.needs_native',L'export.title',L'common.ok') return end
 if IsValid(mmdhl.exportDialog) then mmdhl.exportDialog:Close() end
 local UI=mmdhl.UI local s,f=UI.metrics()
 local frame=vgui.Create('DFrame') mmdhl.exportDialog=frame
 frame:SetTitle(L'export.title') frame:SetSize(math.min(ScrW()*.9,s(820)),math.min(ScrH()*.9,s(720))) frame:Center() frame:MakePopup() frame:DockPadding(s(14),s(30),s(14),s(12))
 frame.Paint=function(_,w,h) draw.RoundedBox(6,0,0,w,h,Color(246,248,251)) draw.RoundedBoxEx(6,0,0,w,s(24),Color(40,105,180),true,true,false,false) end
 frame.lblTitle:SetTextColor(color_white)
 local chosen={} for _,id in ipairs(ids or {}) do chosen[(kind=='static' and 'static' or 'character')..':'..id]=true end
 local all=candidates()
 local first
 for _,c in ipairs(all) do if chosen[c.kind..':'..c.id] then first=first or c end end
 local intro=UI.label(frame,L'export.intro',f.Small,s(40)) intro:Dock(TOP) intro:SetWrap(true) intro:SetTextColor(UI.colors.muted)
 local body=frame:Add('DPanel') body:Dock(FILL) body:SetPaintBackground(false)
 -- Left: the models in the package.
 local left=body:Add('DPanel') left:Dock(LEFT) left:SetWide(s(330)) left:SetPaintBackground(false) left:DockMargin(0,0,s(14),0)
 local count=UI.label(left,'',f.Strong,s(28)) count:Dock(TOP)
 local scroll=left:Add('DScrollPanel') scroll:Dock(FILL)
 scroll.Paint=function(_,w,h) draw.RoundedBox(4,0,0,w,h,color_white) end
 local boxes={}
 local function refreshCount()
  local n,picked=0,{} for _,b in ipairs(boxes) do if b:GetChecked() then n=n+1 picked[#picked+1]={kind=b.candidate.kind,asset=b.candidate.id} end end
  count:SetText(L('export.selected',{count=n}))
  if IsValid(frame.TermsSummary) then local text=mmdhl.terms and mmdhl.terms.ExportSummary(picked) frame.TermsSummary:SetText(text or '') frame.TermsSummary:SetVisible(text~=nil) end
  return n
 end
 local lastKind
 for _,c in ipairs(all) do
  if c.kind~=lastKind then lastKind=c.kind local header=UI.label(scroll,c.kind=='static' and L'export.props' or L'export.characters',f.Small,s(24)) header:Dock(TOP) header:DockMargin(s(8),s(4),0,0) header:SetTextColor(UI.colors.muted) end
  local shown=mmdhl.names and mmdhl.names.EntryName(c.kind,c.entry) or c.entry.name
  local box=UI.checkbox(scroll,shown..(c.entry.workshop and '  ·  '..L'ui.badge.workshop' or ''),nil,f.Body,s(26)) box:Dock(TOP) box:DockMargin(s(8),0,s(8),0)
  box:SetChecked(chosen[c.kind..':'..c.id]==true) box.candidate=c box.OnChange=refreshCount
  if c.entry.workshop then box:SetTooltip(L('ui.tooltip.workshop_origin',{title=c.entry.workshop.title})) end
  boxes[#boxes+1]=box
 end
 -- Right: the Workshop description of the package.
 local right=body:Add('DPanel') right:Dock(FILL) right:SetPaintBackground(false)
 local function field(text,height)
  UI.label(right,text,f.Small,s(22)):Dock(TOP)
  local entry=right:Add('DTextEntry') entry:Dock(TOP) entry:SetTall(height or s(30)) entry:SetFont(f.Body) entry:DockMargin(0,0,0,s(6))
  return entry
 end
 local title=field(L'export.package_title')
 local defaultTitle=first and (#ids>1 and L('export.default_title_many',{name=first.entry.name,count=#ids-1}) or first.entry.name) or ''
 title:SetValue(utf8.sub and utf8.sub(defaultTitle,1,100) or defaultTitle)
 local author=field(L'export.author') author:SetValue(IsValid(LocalPlayer()) and LocalPlayer():Nick() or '')
 local description=field(L'export.description',s(84)) description:SetMultiline(true) description:SetPlaceholderText(L'export.description_placeholder')
 UI.label(right,L'export.tags',f.Small,s(22)):Dock(TOP)
 local tagRow=right:Add('DIconLayout') tagRow:Dock(TOP) tagRow:SetSpaceX(s(10)) tagRow:SetSpaceY(s(4)) tagRow:DockMargin(0,0,0,s(8))
 local tagBoxes={}
 local function limitTags()
  local n=0 for _,b in pairs(tagBoxes) do if b:GetChecked() then n=n+1 end end
  for _,b in pairs(tagBoxes) do b:SetEnabled(b:GetChecked() or n<2) end
 end
 for _,tag in ipairs(Tags) do
  local box=tagRow:Add('DCheckBoxLabel') box:SetText(L('export.tag.'..tag)) box:SetTextColor(UI.colors.ink) box.Label:SetFont(f.Small) box:SizeToContents()
  box:SetChecked(tag=='fun') box.OnChange=limitTags tagBoxes[tag]=box
 end
 -- i18n-keys: export.tag.fun export.tag.roleplay export.tag.scenic export.tag.movie export.tag.realism export.tag.cartoon export.tag.water export.tag.comic export.tag.build
 tagRow:SetTall(s(52)) limitTags()
 local user=UI.checkbox(right,L'export.install_user',nil,f.Body,s(26)) user:Dock(TOP)
 local userHelp=UI.label(right,L'export.install_user_help',f.Small,s(34)) userHelp:Dock(TOP) userHelp:SetWrap(true) userHelp:SetTextColor(UI.colors.muted)
 local folder=field(L'export.folder') folder:SetEnabled(false) folder:SetPlaceholderText(L'export.folder_placeholder')
 user.OnChange=function(_,value) folder:SetEnabled(value) if value and folder:GetValue()=='' then folder:SetValue(title:GetValue()) end end
 local rights=UI.label(right,L'export.rights',f.Small,s(34)) rights:Dock(TOP) rights:SetWrap(true) rights:SetTextColor(Color(150,90,20))
 frame.TermsSummary=UI.label(right,'',f.Small,s(34)) frame.TermsSummary:Dock(TOP) frame.TermsSummary:SetWrap(true) frame.TermsSummary:SetTextColor(Color(170,60,30)) frame.TermsSummary:SetVisible(false)
 if mmdhl.terms then mmdhl.terms.WrapLabel(frame.TermsSummary,f.Small) end
 -- Bottom: progress and actions.
 local status=UI.label(frame,'',f.Small,s(24)) status:Dock(BOTTOM) status:DockMargin(0,s(6),0,0) status:SetWrap(true)
 local bar=frame:Add('DPanel') bar:Dock(BOTTOM) bar:SetTall(s(8)) bar:DockMargin(0,s(8),0,0) bar.fraction=nil
 bar.Paint=function(p,w,h) if not p.fraction then return end draw.RoundedBox(3,0,0,w,h,Color(214,222,232)) draw.RoundedBox(3,0,0,w*math.Clamp(p.fraction,0,1),h,Color(40,105,180)) end
 local actions=frame:Add('DPanel') actions:Dock(BOTTOM) actions:SetTall(s(38)) actions:SetPaintBackground(false) actions:DockMargin(0,s(10),0,0)
 local close=UI.button(actions,L'common.close',function() frame:Close() end,s(38),f.Body) close:Dock(RIGHT) close:SetWide(s(120))
 local export=UI.button(actions,L'export.start',function() end,s(38),f.Strong,true) export:Dock(RIGHT) export:SetWide(s(200)) export:DockMargin(0,0,s(8),0)
 local reveal=UI.button(actions,L'export.show_file',function() end,s(38),f.Body) reveal:Dock(LEFT) reveal:SetWide(s(170)) reveal:SetVisible(false)
 local copy=UI.button(actions,L'export.copy_command',function() end,s(38),f.Body) copy:Dock(LEFT) copy:SetWide(s(220)) copy:DockMargin(s(8),0,0,0) copy:SetVisible(false)
 local function busy(on) export:SetEnabled(not on) for _,b in ipairs(boxes) do b:SetEnabled(not on) end end
 local function finish(result)
  bar.fraction=1 busy(false)
  local name=result.file:gsub('%.gma$','')
  local assets={} for _,b in ipairs(boxes) do if b:GetChecked() and b.candidate.kind=='character' then assets[b.candidate.id]=true end end
  status:SetText(L('export.done',{file=result.file,size=string.NiceSize(result.bytes or 0),count=result.items or 0}))
  reveal:SetVisible(true) reveal.DoClick=function() native.RevealPackageExport(result.file) end
  captureIcon(assets,name,function(icon)
   if not IsValid(frame) then return end
   local command=publishCommand(result,icon)
   file.Write('mmd_hotloader/exports/'..name..'.txt',L('export.readme',{title=result.title,file=result.path,command=command,id=result.packageId})..'\n')
   copy:SetVisible(true) copy.DoClick=function() SetClipboardText(command) status:SetText(L'export.command_copied') end
   status:SetText(L('export.done',{file=result.file,size=string.NiceSize(result.bytes or 0),count=result.items or 0})..'\n'..(icon and L'export.icon_saved' or L'export.icon_missing'))
   status:SetTooltip(command)
  end)
 end
 local function run(spec)
  busy(true) bar.fraction=0 reveal:SetVisible(false) copy:SetVisible(false) status:SetText(L'export.stage.collecting')
  local handle,err=native.StartPackageExport(util.TableToJSON(spec))
  if not handle then busy(false) bar.fraction=nil status:SetText(L('export.failed',{error=tostring(err)})) return end
  frame.handle=handle
  timer.Create('MMDHL.PackageExport',.1,0,function()
   if not IsValid(frame) then timer.Remove('MMDHL.PackageExport') return end
   local state=util.JSONToTable(native.PollPackageExport(handle) or '') or {state='failed'}
   if state.state=='running' then bar.fraction=state.progress status:SetText(L('export.stage.'..tostring(state.stage)))
    -- i18n-keys: export.stage.collecting export.stage.packing export.stage.writing export.stage.complete
    return end
   timer.Remove('MMDHL.PackageExport') frame.handle=nil
   if state.state=='complete' then finish(state)
   else busy(false) bar.fraction=nil status:SetText(state.state=='cancelled' and L'export.cancelled' or L('export.failed',{error=tostring(state.error)})) end
  end)
 end
 export.DoClick=function()
  local items,vrm={},{}
  for _,b in ipairs(boxes) do if b:GetChecked() then
   local c=b.candidate
   items[#items+1]=c.kind=='static' and propItem(c.id,c.entry) or characterItem(c.id,c.entry)
   local short,_,allowed=mmdhl.VrmSummary(c.entry.info)
   if c.kind=='character' and short and allowed~=true then vrm[#vrm+1]=c.entry.name end
  end end
  if #items==0 then status:SetText(L'export.nothing_selected') return end
  if string.Trim(title:GetValue())=='' then status:SetText(L'export.title_required') return end
  local tags={} for _,tag in ipairs(Tags) do if tagBoxes[tag]:GetChecked() then tags[#tags+1]=tag end end
  local spec={title=title:GetValue(),author=author:GetValue(),description=description:GetValue(),tags=tags,install=user:GetChecked() and 'user' or 'workshop',
   folder=user:GetChecked() and folder:GetValue() or nil,fileName=title:GetValue(),items=items}
  if mmdhl.terms then mmdhl.terms.BeforeExport(items,function() if IsValid(frame) then run(spec) end end)
  elseif #vrm>0 then
   Derma_Query(L('export.vrm_licence',{names=table.concat(vrm,', ')}),L'export.title',L'export.start',function() run(spec) end,L'common.cancel')
  else run(spec) end
 end
 frame.OnClose=function(self) timer.Remove('MMDHL.PackageExport') if self.handle then native.CancelPackageExport(self.handle) end end
 refreshCount()
 if #all==0 then status:SetText(L'export.nothing_available') export:SetEnabled(false) end
end
concommand.Add('mmdhl_export',function() mmdhl.OpenPackageExport(nil,{}) end)
