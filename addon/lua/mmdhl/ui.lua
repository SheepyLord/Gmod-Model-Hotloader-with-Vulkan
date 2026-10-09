local native=mmdhl.native
local library=mmdhl.library
local L=mmdhl.L
local ink,muted,accent=Color(31,43,58),Color(93,109,127),Color(34,109,185)
local function ownScale(panel)
 -- GUI Scaler marks processed panels with gScF. Our dimensions and fonts are
 -- already resolution-aware; only opt our subtree out of its second scaling.
 if not GetConVar('guiscaler_scale_multiplier') then return end
 panel.gScF=true
 for _,child in ipairs(panel:GetChildren()) do ownScale(child) end
end
local smallFont,textPadding
local function metrics()
 local scale=math.Clamp(ScrH()/1080,1,2)
 local fonts={} local face=mmdhl.I18n.FontFace()
 for name,size in pairs({Body=15,Small=13,Title=23,Strong=15}) do
  local font='MMDHL.'..name..'.'..math.Round(scale,2)..'.'..face
  surface.CreateFont(font,mmdhl.I18n.FontData(size*scale,(name=='Title' or name=='Strong') and 600 or 500)) fonts[name]=font
 end
 smallFont,textPadding=fonts.Small,math.Round(12*scale)
 return function(n) return math.Round(n*scale) end,fonts
end
local function label(parent,text,font,height)
 local p=parent:Add('DLabel') p:SetText(text) p:SetFont(font) p:SetTextColor(ink) p:SetTall(height) return p
end
-- Button styles: colour marks importance (primary actions, tool, caution, danger).
local styles={
 primary={Color(34,109,185),Color(28,123,216),color_white},
 success={Color(33,138,84),Color(40,158,97),color_white},
 warning={Color(191,120,22),Color(209,135,32),color_white},
 danger={Color(249,228,228),Color(244,211,211),Color(166,38,38)},
 secondary={Color(232,237,242),Color(218,229,240),ink},
 tab={Color(232,237,242),Color(218,229,240),ink},
}
local function button(parent,text,callback,height,font,style)
 local p=parent:Add('DButton') p:SetText(text) p:SetFont(font) p:SetTall(height) p.DoClick=callback p.BaseFont=font
 -- Translations run longer than English: text that does not fit uses the small font.
 local layout=p.PerformLayout
 p.PerformLayout=function(self,w,h)
  local fit=self.BaseFont
  if smallFont and fit~=smallFont then surface.SetFont(fit) if surface.GetTextSize(self:GetText() or '')>w-textPadding then fit=smallFont end end
  if self:GetFont()~=fit then self:SetFont(fit) end
  if layout then layout(self,w,h) end
 end
 local setText=p.SetText p.SetText=function(self,value) setText(self,value) self:InvalidateLayout() end
 p.Style=style==true and 'primary' or style or 'secondary'
 p.Paint=function(self,w,h)
  local st=styles[self.Style] or styles.secondary
  if self.Selected then draw.RoundedBox(4,0,0,w,h,accent) self:SetTextColor(color_white) return end
  if not self:IsEnabled() then draw.RoundedBox(4,0,0,w,h,Color(226,230,235)) self:SetTextColor(muted) return end
  draw.RoundedBox(4,0,0,w,h,self:IsHovered() and st[2] or st[1]) self:SetTextColor(st[3])
 end
 return p
end
-- Labels must be measured after their font is set, or Derma clips the text.
local function checkbox(parent,text,cvar,font,height,color)
 local c=parent:Add('DCheckBoxLabel') c:SetText(text) c:SetTextColor(color or ink) c.Label:SetFont(font)
 if cvar then c:SetConVar(cvar) end
 c.Label:SizeToContents() c:SizeToContents() c:SetTall(height) return c
end
-- Two buttons sharing a row half and half.
local function pair(parent,s,height,first,second)
 local row=parent:Add('DPanel') row:Dock(TOP) row:SetTall(height) row:SetPaintBackground(false) row:DockMargin(0,0,0,s(6))
 first:SetParent(row) second:SetParent(row)
 row.PerformLayout=function(_,w,h) local half=math.floor((w-s(6))/2) first:SetPos(0,0) first:SetSize(half,h) second:SetPos(half+s(6),0) second:SetSize(w-half-s(6),h) end
 return row
end
-- Height of a panel stacked from TOP-docked children.
local function stackHeight(panel)
 local total=0
 for _,child in ipairs(panel:GetChildren()) do if child:IsVisible() then local _,top,_,bottom=child:GetDockMargin() total=total+child:GetTall()+top+bottom end end
 return total
end
-- A section with a clickable header; opening it grows its parent stack.
local function expander(parent,title,s,f,onToggle)
 local holder=parent:Add('DPanel') holder:Dock(TOP) holder:SetPaintBackground(false) holder:DockMargin(0,0,0,s(6))
 local head=button(holder,'▸   '..title,nil,s(30),f.Body) head:Dock(TOP) head:SetContentAlignment(4) head:SetTextInset(s(10),0)
 local body=holder:Add('DPanel') body:Dock(TOP) body:SetPaintBackground(false) body:SetVisible(false) body:DockMargin(0,s(6),0,0)
 holder.Body=body
 function holder:Resize() self:SetTall(s(30)+(body:IsVisible() and stackHeight(body)+s(6) or 0)) end
 head.DoClick=function()
  body:SetVisible(not body:IsVisible()) head:SetText((body:IsVisible() and '▾   ' or '▸   ')..title)
  body:SetTall(stackHeight(body)) holder:Resize() if onToggle then onToggle(body:IsVisible()) end
 end
 holder:Resize() return holder,body
end
local function styleChoices(combo,s,font)
 combo:SetFont(font) combo:SetSortItems(false)
 combo.OnMenuOpened=function(_,menu)
  ownScale(menu)
  for _,option in ipairs(menu:GetCanvas():GetChildren()) do
   if option.SetFont then
    option:SetFont(font) local layout=option.PerformLayout
    option.PerformLayout=function(p,w,h) layout(p,w,h) p:SetTall(s(32)) end
   end
  end
  menu:InvalidateLayout(true)
 end
end
-- A combo box bound to a string convar, showing labels for its values.
local function convarChoices(parent,cvar,choices,s,font)
 local combo=parent:Add('DComboBox') combo:SetTall(s(30)) styleChoices(combo,s,font)
 for _,choice in ipairs(choices) do combo:AddChoice(choice[2],choice[1]) end
 local value=GetConVar(cvar):GetString() local chosen=1
 for i,choice in ipairs(choices) do if choice[1]==value then chosen=i end end
 combo:ChooseOptionID(chosen)
 combo.OnSelect=function(_,_,_,data) if GetConVar(cvar):GetString()~=data then RunConsoleCommand(cvar,data) end end
 return combo
end
local function releasePreview(owner)
 if mmdhl.previewOwner==owner then native.ClearPreview() mmdhl.previewOwner=nil end
 owner.previewHandle=nil owner.previewAsset=nil
 if owner.propPreview and mmdhl.props then mmdhl.props.ReleasePreview() owner.propPreview=nil end
end
function mmdhl.CloseLibrary()
 if IsValid(mmdhl.window) then mmdhl.window:Close() end
 if IsValid(g_SpawnMenu) then g_SpawnMenu:Close() end
end
mmdhl.UI={button=button,checkbox=checkbox,label=label,pair=pair,expander=expander,stackHeight=stackHeight,styleChoices=styleChoices,convarChoices=convarChoices,metrics=metrics,ownScale=ownScale,colors={ink=ink,muted=muted,accent=accent}}

local PANEL={}
-- Virtual folders: Workshop models (optionally one package) and deleted Workshop models.
local WORKSHOP,DELETED='\2workshop','\2deleted'
local function special(path) return isstring(path) and path:sub(1,1)=='\2' end
local function kindOf(mode) return mode=='static' and 'static' or 'character' end
-- A model's name as shown, and its name as authored when they differ (names.lua).
local function shownName(mode,entry) if mmdhl.names and entry then return mmdhl.names.EntryName(kindOf(mode),entry) end return entry and entry.name,nil end
-- A small pill at the right end of the name column: where a model comes from.
local function badge(row,text,color,font,s)
 row.PaintOver=function(r,w,h)
  local column=r.Columns and r.Columns[1] if not IsValid(column) then return end
  surface.SetFont(font) local tw,th=surface.GetTextSize(text) local bw,bh=tw+s(10),th+s(4)
  local x,y=column:GetX()+column:GetWide()-bw-s(6),math.floor((h-bh)/2)
  draw.RoundedBox(4,x,y,bw,bh,color) draw.SimpleText(text,font,x+bw/2,y+bh/2,color_white,TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER)
 end
end
-- The character and static-prop subtabs share this panel; Lib() is the list shown.
function PANEL:Lib() return self.mode=='static' and mmdhl.props and mmdhl.props.library or library end
function PANEL:Init()
 self.S,self.Fonts=metrics() local s,f=self.S,self.Fonts
 self.Paint=function(_,w,h) surface.SetDrawColor(246,248,251) surface.DrawRect(0,0,w,h) end
 self.mode='library' self:SetPaintBackground(false) self:DockPadding(s(10),s(8),s(10),s(8))
 self.importStatus=library.status
 -- Header: section tabs on the left, the section's main actions on the right.
 local top=self:Add('DPanel') top:Dock(TOP) top:SetTall(s(40)) top:SetPaintBackground(false) top:DockMargin(0,0,0,s(8))
 self.Import=button(top,L'ui.import_characters',function() library.BrowseImport(self.mode=='static' and 'static' or nil) end,s(36),f.Strong,true)
 self.Import:Dock(RIGHT) self.Import:SetWide(s(220)) self.Import:DockMargin(s(8),0,0,0)
 local refresh=button(top,L'ui.refresh',function() if self.previewError then self.previewError=nil releasePreview(self) end library.Refresh() if mmdhl.props then mmdhl.props.library.Refresh() end if mmdhl.workshop then mmdhl.workshop.Scan() end self:Refresh() end,s(36),f.Body) refresh:Dock(RIGHT) refresh:SetWide(s(90))
 local language=button(top,'',function() mmdhl.LanguageMenu() end,s(36),f.Body) language:Dock(RIGHT) language:SetWide(s(36)) language:DockMargin(0,0,s(6),0)
 language:SetImage('icon16/world.png') language:SetTooltip(L('ui.language.tooltip',{language=mmdhl.I18n.LanguageName(mmdhl.I18n.Language())}))
 language.PerformLayout=function(p,w,h) if IsValid(p.m_Image) then p.m_Image:SetPos(math.floor((w-p.m_Image:GetWide())/2),math.floor((h-p.m_Image:GetTall())/2)) end end
 self.Tabs={} local tabs={}
 for _,tab in ipairs({{'library',L'ui.tab.characters',165},{'static',L'ui.tab.static_props',135},{'scene',L'ui.tab.scene',120},{'physics',L'ui.tab.physics',200}}) do
  local b=button(top,tab[2],function() if self.mode~=tab[1] then self:SetMode(tab[1]) end end,s(36),f.Strong,'tab') b:Dock(LEFT) b:SetWide(s(tab[3])) b:DockMargin(0,0,s(6),0) self.Tabs[tab[1]]=b
  b.MinWide=s(tab[3]) tabs[#tabs+1]=b
 end
 local function natural(font,text) surface.SetFont(font) return surface.GetTextSize(text)+s(28) end
 self.Import:SetWide(math.max(s(220),natural(f.Strong,L'ui.import_characters'),natural(f.Strong,L'ui.import_props')))
 refresh:SetWide(math.max(s(90),natural(f.Body,L'ui.refresh')))
 -- Tabs take the room left of the actions; if it runs out they use the small font, then shrink.
 top.PerformLayout=function(_,w)
  local room=w-self.Import:GetWide()-s(8)-refresh:GetWide()-language:GetWide()-s(6)
  for _,font in ipairs({f.Strong,f.Small}) do
   local total,widths=0,{}
   for i,b in ipairs(tabs) do widths[i]=math.max(b.MinWide,natural(font,b:GetText())) total=total+widths[i]+s(6) end
   if total<=room or font==f.Small then
    local k=math.min(1,room/total)
    for i,b in ipairs(tabs) do b.BaseFont=font b:SetWide(math.floor(widths[i]*k)) end
    break
   end
  end
 end
 self.ImportBanner=self:Add('DPanel') self.ImportBanner:Dock(TOP) self.ImportBanner:SetTall(s(74)) self.ImportBanner:DockMargin(0,0,0,s(8)) self.ImportBanner:SetVisible(false)
 self.ImportBanner.Paint=function(_,w,h)
  draw.RoundedBox(5,0,0,w,h,Color(220,237,251))
  draw.SimpleText(library.filename and L('ui.import.file',{file=library.filename}) or library.jobKind=='static' and L'ui.import.static_prop' or L'ui.import.model',f.Strong,s(12),s(8),ink)
  draw.SimpleText((library.status or L'ui.import.working')..(library.progress and ('  '..math.floor(library.progress*100)..'%') or ''),f.Small,s(12),s(31),muted)
  if not library.job then return end
  surface.SetDrawColor(183,203,221) surface.DrawRect(s(12),h-s(15),w-s(24)-s(147),s(7)) surface.SetDrawColor(accent)
  local width=w-s(24)-s(147)
  if library.progress then surface.DrawRect(s(12),h-s(15),width*library.progress,s(7))
  else surface.DrawRect(s(12)+(width-s(80))*(RealTime()%1),h-s(15),s(80),s(7)) end
 end
 self.Cancel=button(self.ImportBanner,L'ui.import.cancel',library.CancelImport,s(36),f.Body,'danger') self.Cancel:Dock(RIGHT) self.Cancel:SetWide(s(135)) self.Cancel:DockMargin(s(8),s(19),s(12),s(19))
 self.ShowBones=button(self.ImportBanner,L'bonemap.status.show_window',function() local BM=mmdhl.boneMapper if BM and IsValid(BM.frame) then BM.frame:MakePopup() end end,s(36),f.Body,true)
 self.ShowBones:Dock(RIGHT) self.ShowBones:SetWide(s(135)) self.ShowBones:DockMargin(s(8),s(19),s(12),s(19)) self.ShowBones:SetVisible(false)
 -- One-time notice before names of existing models are sent for translation (names.lua).
 self.NamesNotice=self:Add('DPanel') self.NamesNotice:Dock(TOP) self.NamesNotice:SetTall(s(60)) self.NamesNotice:DockMargin(0,0,0,s(8)) self.NamesNotice:DockPadding(s(12),s(6),s(8),s(6)) self.NamesNotice:SetVisible(false)
 self.NamesNotice.Paint=function(_,w,h) draw.RoundedBox(5,0,0,w,h,Color(222,233,247)) end
 local keep=button(self.NamesNotice,L'names.notice_decline',function() if mmdhl.names then mmdhl.names.AnswerNotice(false) end end,s(36),f.Small) keep:Dock(RIGHT) keep:SetWide(s(170)) keep:DockMargin(s(6),s(6),0,s(6))
 local translate=button(self.NamesNotice,L'names.notice_accept',function() if mmdhl.names then mmdhl.names.AnswerNotice(true) end end,s(36),f.Small,true) translate:Dock(RIGHT) translate:SetWide(s(150)) translate:DockMargin(s(6),s(6),0,s(6))
 local noticeText=label(self.NamesNotice,L'names.notice',f.Small,s(48)) noticeText:Dock(FILL) noticeText:SetWrap(true) noticeText:SetTextColor(ink)
 if mmdhl.terms and mmdhl.terms.WrapLabel then mmdhl.terms.WrapLabel(noticeText,f.Small) end
 self.Status=label(self,L'ui.status.initial',f.Body,s(40)) self.Status:Dock(BOTTOM) self.Status:SetWrap(true)
 self.Body=self:Add('DPanel') self.Body:Dock(FILL) self.Body:SetPaintBackground(false)
 self.Left=self.Body:Add('DPanel') self.Left:Dock(LEFT) self.Left:SetWide(s(345)) self.Left:SetPaintBackground(false) self.Left:DockMargin(0,0,s(16),0)
 self.Search=self.Left:Add('DTextEntry') self.Search:Dock(TOP) self.Search:SetTall(s(34)) self.Search:SetFont(f.Body) self.Search:SetPlaceholderText(L'ui.search_placeholder') self.Search:DockMargin(0,0,0,s(8))
 self.Search.OnChange=function() self:Refresh() end
 self.FolderPanel=self.Left:Add('DPanel') self.FolderPanel:Dock(TOP) self.FolderPanel:SetTall(s(130)) self.FolderPanel:SetPaintBackground(false) self.FolderPanel:DockMargin(0,0,0,s(8))
 local folderHeader=self.FolderPanel:Add('DPanel') folderHeader:Dock(TOP) folderHeader:SetTall(s(30)) folderHeader:SetPaintBackground(false) folderHeader:DockMargin(0,0,0,s(4))
 local folderLabel=label(folderHeader,L'ui.folder.header',f.Strong,s(28)) folderLabel:Dock(LEFT) folderLabel:SetWide(s(90))
 local addFolder=button(folderHeader,L'ui.folder.new',function() self:NewFolder() end,s(28),f.Small) addFolder:Dock(RIGHT) addFolder:SetWide(s(105))
 self.Folders=self.FolderPanel:Add('DTree') self.Folders:Dock(FILL)
 self.Count=label(self.Left,'',f.Small,s(26)) self.Count:Dock(BOTTOM) self.Count:SetTextColor(muted)
 -- Library management as a two-by-two grid: organizing on top, removal below.
 local management=self.Left:Add('DPanel') management:Dock(BOTTOM) management:SetTall(s(76)) management:SetPaintBackground(false) management:DockMargin(0,s(8),0,0)
 self.Manage=management
 self.Rename=button(management,L'common.rename',function() self:RenameSelected() end,s(32),f.Body)
 self.Favorite=button(management,L'ui.manage.favorite',function() local lib=self:Lib() local entry=self.selected and lib.entries[self.selected] if entry then lib.Update(self.selected,{favorite=not entry.settings.favorite}) end end,s(32),f.Body)
 self.Move=button(management,L'ui.manage.move',function() self:MoveSelected() end,s(32),f.Body)
 self.Delete=button(management,L'ui.manage.delete',function() self:DeleteSelected() end,s(32),f.Body,'danger')
 self.Export=button(management,L'ui.manage.export',function() self:ExportSelected() end,s(32),f.Body)
 self.Export:SetTooltip(L'ui.tooltip.export')
 management.PerformLayout=function(_,w,h)
  local half=math.floor((w-s(6))/2) local row=s(32)
  self.Rename:SetPos(0,0) self.Rename:SetSize(half,row) self.Favorite:SetPos(half+s(6),0) self.Favorite:SetSize(w-half-s(6),row)
  if self.Move:IsVisible() then self.Move:SetPos(0,row+s(6)) self.Move:SetSize(half,row) self.Delete:SetPos(half+s(6),row+s(6)) self.Delete:SetSize(w-half-s(6),row)
  elseif self.Rename:IsVisible() then self.Delete:SetPos(0,row+s(6)) self.Delete:SetSize(w,row)
  else self.Delete:SetPos(0,0) self.Delete:SetSize(w,row) end
  if self.Export:IsVisible() then self.Export:SetPos(0,(row+s(6))*2) self.Export:SetSize(w,row) end
 end
 self.Models=self.Left:Add('DListView') self.Models:Dock(FILL) self.Models:SetMultiSelect(true) self.Models:SetDataHeight(s(40)) self.Models:SetHeaderHeight(s(30))
 self.Models:AddColumn(L'ui.column.model') self.Models:AddColumn(L'ui.column.vertices'):SetFixedWidth(s(80))
 self.MMDHLLibrary=self.Models
 for _,c in pairs(self.Models.Columns) do c.Header:SetFont(f.Small) end
 self.Models.OnRowSelected=function(_,_,row) if not self.refreshing then self:Choose(row) end end
 self.Models.DoDoubleClick=function(_,_,row) self:Choose(row) if self.mode=='library' then self:Place() elseif self.mode=='static' then self:PlaceProp() elseif self.mode=='scene' then mmdhl.OpenEntityEditor(row.entity) end end
 self.Models.OnRowRightClick=function(_,_,row)
  self:Choose(row) local menu=DermaMenu()
  if row.entry and row.entry.deleted then
   menu:AddOption(L'ui.workshop.restore_selected',function() self:DeleteSelected() end):SetIcon('icon16/arrow_undo.png')
   menu:Open() return
  end
  if self.mode=='library' then
   menu:AddOption(L'ui.spawn.ragdoll',function() self:Place('ragdoll') end)
   menu:AddOption(L'ui.bodygroups.edit',function() self:EditBodygroups() end)
   local BM=mmdhl.boneMapper local picked=row.entry
   if BM and picked and BM.Available('fit') then menu:AddOption(L'ui.menu.assign_bones',function() BM.OpenFit(picked.id,'edit',picked.name) end):SetIcon('icon16/user_edit.png') end
   if BM and picked and istable(picked.info.conversion) and picked.source~='' and not picked.shared and BM.Convertible(picked.source) then
    menu:AddOption(L'ui.menu.import_again_bones',function() if library.job then self:SetStatus(L'library.import.busy',true) elseif not BM.BringToFront() then BM.Probe(picked.source) end end):SetIcon('icon16/arrow_refresh.png')
   end
   self:PopulateMoveMenu(menu:AddSubMenu(L'ui.menu.move_selected'))
   menu:AddOption(L'common.rename',function() self:RenameSelected() end)
  elseif self.mode=='static' then
   menu:AddOption(L'ui.spawn.prop',function() self:PlaceProp() end)
   menu:AddOption(L'ui.prop.tool',function() self:ToolProp() end)
   menu:AddOption(L'ui.prop.edit_parts',function() self:EditParts() end)
   self:PopulateMoveMenu(menu:AddSubMenu(L'ui.menu.move_selected'))
   menu:AddOption(L'common.rename',function() self:RenameSelected() end)
   if row.entry and row.entry.source~='' and not row.entry.shared then menu:AddOption(L'ui.menu.reimport',function() self:ReimportProp() end) end
  end
  if self.mode=='scene' then menu:AddOption(L'ui.menu.edit_character',function() mmdhl.OpenEntityEditor(row.entity) end) end
  -- Terms of use were acknowledged at import; they stay readable here, not above the preview.
  local entry=self.selected and self:Lib().entries[self.selected]
  if entry and mmdhl.terms and (self.mode=='library' or self.mode=='static') and mmdhl.terms.Summary(kindOf(self.mode),entry.id) then
   local terms=menu:AddOption(L'terms.button',function() mmdhl.terms.Show(kindOf(self.mode),entry.id,entry.name) end) terms:SetIcon('icon16/page_white_text.png') terms:SetTooltip(L'terms.button_tip')
  end
  if self.mode=='library' or self.mode=='static' then menu:AddOption(L'ui.menu.export',function() self:ExportSelected() end):SetIcon('icon16/package_go.png') end
  menu:AddOption(self.mode=='scene' and L'ui.menu.remove_characters' or L'ui.manage.delete_selected',function() self:DeleteSelected() end)
  if row.entry and row.entry.source~='' then menu:AddOption(L'ui.menu.copy_source',function() SetClipboardText(row.entry.source) end) end menu:Open()
 end
 self.Right=self.Body:Add('DPanel') self.Right:Dock(FILL) self.Right:SetPaintBackground(false)
 self.Name=label(self.Right,L'ui.select_model',f.Title,s(36)) self.Name:Dock(TOP)
 self.Details=label(self.Right,L'ui.details.intro',f.Small,s(40)) self.Details:Dock(TOP) self.Details:SetWrap(true) self.Details:SetTextColor(muted)
 self.Warnings=button(self.Right,'',function() local entry=self:Lib().entries[self.selected] if entry then mmdhl.ShowWarnings(entry.name,entry.info.warnings) end end,s(30),f.Small,'warning') self.Warnings:Dock(TOP) self.Warnings:SetVisible(false) self.Warnings:DockMargin(0,0,0,s(6))
 self.Origin=self.Right:Add('DPanel') self.Origin:Dock(TOP) self.Origin:SetTall(s(34)) self.Origin:DockMargin(0,0,0,s(6)) self.Origin:SetVisible(false)
 self.Origin.Paint=function(_,w,h) draw.RoundedBox(5,0,0,w,h,Color(222,233,247)) end
 -- The button is OriginButton: a field named OriginAction would hide PANEL:OriginAction.
 self.OriginButton=button(self.Origin,'',function() self:OriginAction() end,s(28),f.Small) self.OriginButton:Dock(RIGHT) self.OriginButton:DockMargin(s(6),s(3),s(3),s(3))
 self.OriginText=label(self.Origin,'',f.Small,s(34)) self.OriginText:Dock(FILL) self.OriginText:DockMargin(s(10),0,0,0)
 self:BuildCharacterActions()
 self:BuildPropActions()
 self.SceneActions=self.Right:Add('DPanel') self.SceneActions:Dock(BOTTOM) self.SceneActions:SetTall(s(116)) self.SceneActions:SetPaintBackground(false) self.SceneActions:SetVisible(false) self.SceneActions:DockMargin(0,s(8),0,0)
 local edit=button(self.SceneActions,L'ui.scene.edit',function() mmdhl.OpenEntityEditor(self.entity) end,s(36),f.Strong,true) edit:Dock(TOP) edit:DockMargin(0,0,0,s(6))
 pair(self.SceneActions,s,s(34),
  button(self.SceneActions,L'ui.scene.reset_physics',function() mmdhl.ResetPhysics(self.entity) end,s(34),f.Body),
  button(self.SceneActions,L'ui.scene.freeze',function() if IsValid(self.entity) then mmdhl.Action('freeze',nil,self.entity) end end,s(34),f.Body))
 self.Preview=self.Right:Add('DPanel') self.Preview:Dock(FILL)
 local preview=self.Preview
 preview.Paint=function(p,w,h) if self.mode=='static' then self:PaintPropPreview(p,w,h) else self:PaintPreview(p,w,h) end end
 preview.OnMousePressed=function(p,key) p:MouseCapture(true) self.drag={key=key,x=gui.MouseX(),y=gui.MouseY()} end
 preview.OnMouseReleased=function(p) p:MouseCapture(false) self.drag=nil end
 preview.OnMouseWheeled=function(_,delta)
  if self.mode=='static' then local r=self.propRadius or 40 self.distance=math.Clamp(self.distance*(1-delta*.1),r*.25,r*25) return true end
  local ratio=self.cameraScale or 1 self.distance=math.Clamp(self.distance-delta*7*ratio,1.8*ratio,400*ratio) return true
 end
 local camera=preview:Add('DPanel') camera:Dock(BOTTOM) camera:SetTall(s(38)) camera:SetPaintBackground(false) camera:DockMargin(s(10),0,s(10),s(8))
 for _,preset in ipairs({{'full',L'ui.camera.full'},{'face',L'ui.camera.face'}}) do
  local b=button(camera,preset[2],function() self:ResetCamera(preset[1]=='face') end,s(30),f.Small) b:Dock(LEFT) b:SetWide(s(95)) b:DockMargin(0,0,s(8),0)
  if preset[1]=='face' then self.FaceCamera=b end
 end
 self.ShowCollision=checkbox(camera,L'ui.show_collision',nil,f.Small,s(30),Color(184,202,222)) self.ShowCollision:Dock(LEFT) self.ShowCollision:DockMargin(s(4),0,0,0) self.ShowCollision:SetVisible(false)
 self:ResetCamera(false) self:Refresh() ownScale(self)
end
-- Character actions: size, spawn roles, bodygroups, player options and sharing.
function PANEL:BuildCharacterActions()
 local s,f=self.S,self.Fonts
 local actions=self.Right:Add('DPanel') self.Actions=actions actions:Dock(BOTTOM) actions:SetPaintBackground(false) actions:DockMargin(0,s(8),0,0)
 self.AssignBones=button(actions,'',function() local BM=mmdhl.boneMapper local entry=self.selected and library.entries[self.selected] if BM and entry then BM.OpenFit(entry.id,'rescue',entry.name) end end,s(36),f.Strong,'warning')
 self.AssignBones:Dock(TOP) self.AssignBones:DockMargin(0,0,0,s(6)) self.AssignBones:SetVisible(false)
 self.ScaleMultiplier=actions:Add('DNumSlider') self.ScaleMultiplier:Dock(TOP) self.ScaleMultiplier:SetTall(s(32)) self.ScaleMultiplier:SetText(L'ui.character.size') self.ScaleMultiplier:SetMinMax(.1,4) self.ScaleMultiplier:SetDecimals(2) self.ScaleMultiplier:SetValue(1) self.ScaleMultiplier:SetDark(true) self.ScaleMultiplier.Label:SetFont(f.Body)
 self.ScaleMultiplier.OnValueChanged=function() releasePreview(self) self:ResetCamera(false) end
 self.SizeInfo=label(actions,'',f.Small,s(22)) self.SizeInfo:Dock(TOP) self.SizeInfo:SetTextColor(muted)
 -- Bodygroup preset used by the spawn buttons below; the default preset is preselected.
 local bodyRow=actions:Add('DPanel') bodyRow:Dock(TOP) bodyRow:SetTall(s(30)) bodyRow:SetPaintBackground(false) bodyRow:DockMargin(0,s(2),0,s(6))
 self.BodygroupEdit=button(bodyRow,L'ui.bodygroups.edit',function() self:EditBodygroups() end,s(30),f.Body) self.BodygroupEdit:Dock(RIGHT) self.BodygroupEdit:SetWide(s(150)) self.BodygroupEdit:DockMargin(s(6),0,0,0)
 self.BodygroupPreset=bodyRow:Add('DComboBox') self.BodygroupPreset:Dock(FILL) styleChoices(self.BodygroupPreset,s,f.Body)
 self.BodygroupPreset.OnSelect=function(_,_,_,data) self.bodygroupPreset=data end
 self.BodygroupPreset:SetTooltip(L'ui.tooltip.bodygroup_preset')
 self.Frozen=checkbox(actions,L'ui.character.freeze','mmdhl_spawn_frozen',f.Body,s(28)) self.Frozen:Dock(TOP) self.Frozen:DockMargin(0,0,0,s(4))
 -- Health of new NPCs, from these buttons or the spawn menu; at 0 the label says the game's health is kept.
 self.NPCHealth=actions:Add('DNumSlider') self.NPCHealth:Dock(TOP) self.NPCHealth:SetTall(s(32)) self.NPCHealth:SetMinMax(0,mmdhl.MaxNPCHealth) self.NPCHealth:SetDecimals(0) self.NPCHealth:SetDark(true) self.NPCHealth.Label:SetFont(f.Body) self.NPCHealth:DockMargin(0,0,0,s(4))
 self.NPCHealth:SetConVar('mmdhl_npc_health') self.NPCHealth:SetTooltip(L'ui.tooltip.npc_health')
 local function healthLabel(value) self.NPCHealth:SetText((tonumber(value) or 0)<1 and L'ui.character.npc_health_default' or L'ui.character.npc_health') end
 self.NPCHealth.OnValueChanged=function(_,value) healthLabel(value) end healthLabel(GetConVar('mmdhl_npc_health'):GetFloat())
 self.SpawnButtons={}
 for _,pairItems in ipairs({{{'ragdoll',L'ui.spawn.ragdoll'},{'citizen',L'ui.spawn.friendly_npc'}},{{'combine',L'ui.spawn.hostile_npc'},{'player',L'ui.spawn.player_model'}}}) do
  local made={}
  for _,item in ipairs(pairItems) do
   local role=item[1] local b=button(actions,item[2],function() self:Place(role) end,s(40),f.Strong,true)
   b:SetEnabled(false) self.SpawnButtons[role]=b made[#made+1]=b
   b:SetTooltip(role=='player' and L'ui.tooltip.spawn_player' or role=='ragdoll' and L'ui.tooltip.spawn_ragdoll' or L'ui.tooltip.spawn_npc')
  end
  pair(actions,s,s(40),made[1],made[2])
 end
 -- Keep the callable ragdoll action used by existing library integrations.
 self.Spawn=self.SpawnButtons.ragdoll
 local _,content=expander(actions,L'ui.character.player_options',s,f,function() actions:SetTall(stackHeight(actions)) end)
 self.ActorGender=content:Add('DComboBox') self.ActorGender:Dock(TOP) self.ActorGender:SetTall(s(30)) styleChoices(self.ActorGender,s,f.Body) self.ActorGender:SetValue(L'ui.character.animation_female') self.actorGender='female'
 self.ActorGender:AddChoice(L'ui.character.animation_female','female') self.ActorGender:AddChoice(L'ui.character.animation_male','male') self.ActorGender.OnSelect=function(_,_,_,value) self.actorGender=value end
 local armsButton=button(content,L'ui.character.arms',function() if self.selected then mmdhl.OpenArmsEditor(self.selected,self.actorGender) end end,s(30),f.Body) armsButton:Dock(TOP) armsButton:DockMargin(0,s(4),0,0)
 local publish=button(actions,L'ui.character.share',function()
  if not self.selected then return end
  local id=self.selected local entry=self:Lib().entries[id]
  local function share() local ok,err=mmdhl.PublishAsset(id) self:SetStatus(ok and L'ui.status.sharing_model' or err,not ok) end
  -- A VRM licence may forbid redistribution; sharing sends the model to every player.
  local short,_,allowed=mmdhl.VrmSummary(entry and entry.info)
  if short and allowed~=true then
   Derma_Query(allowed==false and L('ui.vrm_share.no_redistribution',{name=entry.name}) or L('ui.vrm_share.unknown',{name=entry.name}),L'ui.vrm_share.title',L'ui.vrm_share.confirm',share,L'common.cancel')
  else share() end
 end,s(30),f.Body,'warning') publish:Dock(TOP) publish:SetVisible(not game.SinglePlayer())
 actions:SetTall(stackHeight(actions))
end
-- Static prop actions: size, placement, tool gun, parts, import options and sharing.
function PANEL:BuildPropActions()
 local s,f=self.S,self.Fonts
 local panel=self.Right:Add('DPanel') self.PropActions=panel panel:Dock(BOTTOM) panel:SetPaintBackground(false) panel:SetVisible(false) panel:DockMargin(0,s(8),0,0)
 self.PropScale=panel:Add('DNumSlider') self.PropScale:Dock(TOP) self.PropScale:SetTall(s(32)) self.PropScale:SetText(L'ui.prop.size') self.PropScale:SetMinMax(.05,20) self.PropScale:SetDecimals(2) self.PropScale:SetValue(1) self.PropScale:SetDark(true) self.PropScale.Label:SetFont(f.Body)
 self.PropScale:SetTooltip(L'ui.tooltip.prop_size')
 self.PropScale.OnValueChanged=function() self:ResetCamera(false) end
 self.PropSizeInfo=label(panel,'',f.Small,s(22)) self.PropSizeInfo:Dock(TOP) self.PropSizeInfo:SetTextColor(muted)
 self.PropFrozen=checkbox(panel,L'ui.prop.freeze','mmdhl_prop_spawn_frozen',f.Body,s(28)) self.PropFrozen:Dock(TOP)
 self.PropGlossy=checkbox(panel,L'ui.prop.glossy','mmdhl_prop_specular',f.Body,s(28)) self.PropGlossy:Dock(TOP) self.PropGlossy:DockMargin(0,0,0,s(4))
 self.PropGlossy:SetTooltip(L'ui.tooltip.prop_glossy')
 -- What new placements collide with; placed props change from the context menu.
 local collideRow=panel:Add('DPanel') collideRow:Dock(TOP) collideRow:SetTall(s(30)) collideRow:SetPaintBackground(false) collideRow:DockMargin(0,0,0,s(6))
 local collideLabel=label(collideRow,L'ui.prop.collides_with',f.Body,s(30)) collideLabel:Dock(LEFT) collideLabel:SizeToContentsX(s(12))
 self.PropGravity=checkbox(collideRow,L'ui.prop.gravity',nil,f.Body,s(30)) self.PropGravity:Dock(RIGHT) self.PropGravity:DockMargin(s(12),0,0,0)
 local choices={} for _,m in ipairs(mmdhl.props.CollisionModes) do choices[#choices+1]={m.id,m.label} end
 self.PropCollide=convarChoices(collideRow,'mmdhl_prop_collide',choices,s,f.Body) self.PropCollide:Dock(FILL)
 self.PropCollide:SetTooltip(L'ui.tooltip.prop_collide')
 local function syncGravity(mode)
  local none=mode=='none' self.PropGravity.syncing=true
  self.PropGravity:SetEnabled(not none) self.PropGravity.Button:SetEnabled(not none) self.PropGravity:SetTextColor(none and muted or ink)
  self.PropGravity:SetChecked(not none and GetConVar('mmdhl_prop_gravity'):GetBool())
  self.PropGravity.syncing=nil
  self.PropGravity:SetTooltip(none and L'ui.tooltip.gravity_none' or L'ui.tooltip.gravity')
 end
 self.PropGravity.OnChange=function(box,value) if not box.syncing and GetConVar('mmdhl_prop_collide'):GetString()~='none' then RunConsoleCommand('mmdhl_prop_gravity',value and '1' or '0') end end
 local select=self.PropCollide.OnSelect
 self.PropCollide.OnSelect=function(combo,index,value,data) select(combo,index,value,data) syncGravity(data) end
 -- The Static Prop tool shares these settings.
 cvars.AddChangeCallback('mmdhl_prop_collide',function(_,_,mode)
  if not IsValid(self) or not IsValid(self.PropCollide) then return end
  for i,m in ipairs(mmdhl.props.CollisionModes) do if m.id==mode and self.PropCollide:GetSelectedID()~=i then self.PropCollide:ChooseOptionID(i) end end
  syncGravity(mode)
 end,'MMDHL.PropPanelCollide')
 cvars.AddChangeCallback('mmdhl_prop_gravity',function() if IsValid(self) and IsValid(self.PropGravity) then syncGravity(GetConVar('mmdhl_prop_collide'):GetString()) end end,'MMDHL.PropPanelGravity')
 syncGravity(GetConVar('mmdhl_prop_collide'):GetString())
 -- What new placements are made of: impact sounds, friction and bounce.
 local surfaceRow=panel:Add('DPanel') surfaceRow:Dock(TOP) surfaceRow:SetTall(s(30)) surfaceRow:SetPaintBackground(false) surfaceRow:DockMargin(0,0,0,s(6))
 local surfaceLabel=label(surfaceRow,L'ui.prop.surface',f.Body,s(30)) surfaceLabel:Dock(LEFT) surfaceLabel:SizeToContentsX(s(12))
 local width=math.max(collideLabel:GetWide(),surfaceLabel:GetWide()) collideLabel:SetWide(width) surfaceLabel:SetWide(width)
 local surfaces={} for _,m in ipairs(mmdhl.props.SurfaceMaterials) do surfaces[#surfaces+1]={m.id,m.label} end
 self.PropSurface=convarChoices(surfaceRow,'mmdhl_prop_physprop',surfaces,s,f.Body) self.PropSurface:Dock(FILL)
 self.PropSurface:SetTooltip(L'ui.tooltip.prop_surface')
 cvars.AddChangeCallback('mmdhl_prop_physprop',function(_,_,surface)
  if not IsValid(self) or not IsValid(self.PropSurface) then return end
  for i,m in ipairs(mmdhl.props.SurfaceMaterials) do if m.id==surface and self.PropSurface:GetSelectedID()~=i then self.PropSurface:ChooseOptionID(i) end end
 end,'MMDHL.PropPanelSurface')
 self.PropSpawn=button(panel,L'ui.spawn.prop',function() self:PlaceProp() end,s(40),f.Strong,true) self.PropSpawn:SetEnabled(false)
 self.PropSpawn:SetTooltip(L'ui.tooltip.prop_spawn')
 self.PropTool=button(panel,L'ui.prop.tool',function() self:ToolProp() end,s(40),f.Strong,'success') self.PropTool:SetEnabled(false)
 self.PropTool:SetTooltip(L'ui.tooltip.prop_tool')
 pair(panel,s,s(40),self.PropSpawn,self.PropTool)
 self.PropParts=button(panel,L'ui.prop.edit_parts',function() self:EditParts() end,s(32),f.Body)
 self.PropParts:SetTooltip(L'ui.tooltip.prop_parts')
 self.PropResize=button(panel,L'ui.prop.resize',function()
  local ok,err=mmdhl.props.ResizeAimed(self.PropScale:GetValue()) self:SetStatus(ok and L'ui.status.resizing' or err,not ok)
 end,s(32),f.Body)
 self.PropResize:SetTooltip(L'ui.tooltip.prop_resize')
 pair(panel,s,s(32),self.PropParts,self.PropResize)
 local _,content=expander(panel,L'ui.prop.import_options',s,f,function() panel:SetTall(stackHeight(panel)) end)
 local axis=convarChoices(content,'mmdhl_prop_import_axis',{{'auto',L'ui.prop.axis_auto'},{'y_up',L'ui.prop.axis_y_up'},{'z_up',L'ui.prop.axis_z_up'}},s,f.Body) axis:Dock(TOP)
 local yaw=convarChoices(content,'mmdhl_prop_import_yaw',{{'0',L'ui.prop.turn_none'},{'90',L'ui.prop.turn_left'},{'180',L'ui.prop.turn_around'},{'-90',L'ui.prop.turn_right'}},s,f.Body) yaw:Dock(TOP) yaw:DockMargin(0,s(4),0,0)
 local collision=convarChoices(content,'mmdhl_prop_import_collision',{{'hull',L'ui.prop.collision_hull'},{'balanced',L'ui.prop.collision_detailed'}},s,f.Body) collision:Dock(TOP) collision:DockMargin(0,s(4),0,0)
 collision:SetTooltip(L'ui.tooltip.import_collision')
 local scale=content:Add('DNumSlider') scale:Dock(TOP) scale:SetTall(s(32)) scale:SetText(L'ui.prop.import_scale') scale:SetMinMax(.01,100) scale:SetDecimals(2) scale:SetDark(true) scale.Label:SetFont(f.Body) scale:SetConVar('mmdhl_prop_import_scale') scale:DockMargin(0,s(4),0,0)
 scale:SetTooltip(L'ui.tooltip.import_scale')
 self.PropReimport=button(content,L'ui.prop.reimport',function() self:ReimportProp() end,s(30),f.Body) self.PropReimport:Dock(TOP) self.PropReimport:DockMargin(0,s(4),0,0)
 local share=button(panel,L'ui.prop.share',function() if self.selected then local ok,err=mmdhl.PublishProp(self.selected) self:SetStatus(ok and L'ui.status.sharing_prop' or err,not ok) end end,s(30),f.Body,'warning') share:Dock(TOP) share:SetVisible(not game.SinglePlayer())
 panel:SetTall(stackHeight(panel))
end
function PANEL:SetStatus(text,isError)
 text=tostring(text or '')
 -- Keep the cause visible in the small status area. A centered, wrapped
 -- traceback used to show only its last frames and hide the actual error.
 self.Status:SetTooltip(isError and text or nil)
 local message=isError and (text:match('^[^\r\n]+') or text):gsub('^.-:%d+:%s*','') or text
 self.Status:SetText(message) self.Status:SetTextColor(isError and Color(180,46,46) or ink)
end
function PANEL:ResetCamera(face)
 if self.mode=='static' then
  local P=mmdhl.props local entry=self.selected and P and P.library.entries[self.selected] local info=entry and entry.info
  local cached=self.selected and P and P.RenderCache[self.selected]
  if cached and cached.info then info=cached.info end
  local scale=IsValid(self.PropScale) and self.PropScale:GetValue() or 1
  local mins,maxs=info and info.mins and P.Vector(info.mins) or Vector(-20,-20,0),info and info.maxs and P.Vector(info.maxs) or Vector(20,20,40)
  local radius=math.max(.5,(maxs-mins):Length()*.5*scale)
  -- Imported fronts land on local -Y; start from a three-quarter front view.
  self.yaw=-65 self.pitch=18 self.target=(mins+maxs)*.5*scale self.propRadius=radius
  self.distance=radius/math.sin(math.rad(21))*1.1 self.cameraScale=radius/40
  return
 end
 self.cameraScale=IsValid(self.ScaleMultiplier) and self.ScaleMultiplier:GetValue() or 1
 self.yaw=180 self.pitch=0 self.distance=(face and 40 or 185)*self.cameraScale self.target=Vector(0,0,face and 27 or 0)
 local entry=library.entries[self.selected] local info=entry and entry.info
 if not info or not info.bounds then return end
 local lo,hi=Vector(unpack(info.bounds[1])),Vector(unpack(info.bounds[2]))
 local scale=3.23656*(IsValid(self.ScaleMultiplier) and self.ScaleMultiplier:GetValue() or 1) local center=(lo+hi)*.5 local head,foot
 for _,bone in ipairs(info.boneList or {}) do
  if bone.name=='頭' or (bone.english or ''):lower()=='head' then head=Vector(unpack(bone.position)) end
  if bone.name=='左足首' or (bone.english or ''):lower()=='leftfoot' then foot=Vector(unpack(bone.position)) end
 end
 if not head or not foot or head.y<=foot.y then return end
 -- Authored bounds can include hidden geometry below the feet (Cyrene does).
 -- Landmarks frame the character, and the face preset follows its actual head.
 local bodyHeight=(head.y-foot.y)*1.2
 local target=face and (head+Vector(0,bodyHeight*.065,0)) or Vector(head.x,foot.y+bodyHeight*.45,head.z)
 local delta=target-center self.target=Vector(delta.z,-delta.x,delta.y)*scale
 self.distance=math.Clamp(bodyHeight*scale*(face and .3 or 1.4),(face and 18 or 30)*self.cameraScale,400*self.cameraScale)
end
function PANEL:SetMode(mode)
 -- Workshop packages and deleted models differ per tab.
 if special(self.folder) then self.folder=nil end
 self.mode=mode self.selected=nil self.entity=nil releasePreview(self) self.Search:SetText('') self:Refresh()
 self:SetStatus(mode=='physics' and L'ui.status.physics_tab' or mode=='scene' and L'ui.status.scene_tab'
  or mode=='static' and L'ui.status.static_tab'
  or L'ui.status.library_tab')
end
function PANEL:RefreshFolders()
 local lib=self:Lib()
 local paths=lib.Folders()
 local groups,deleted=self:WorkshopGroups(),mmdhl.workshop and mmdhl.workshop.Deleted(kindOf(self.mode)) or {}
 local extra={} for _,g in ipairs(groups) do extra[#extra+1]=g.id..'='..g.title..'#'..g.count end
 local signature=self.mode..'\n'..table.concat(paths,'\n')..'\n'..table.concat(extra,'\n')..'\n#'..#deleted..'\n'..mmdhl.I18n.Language()
 if self.folderSignature==signature then self.Folders:SetSelectedItem(self.folderNodes[self.folder or false]) return end self.folderSignature=signature
 self.FolderPanel:SetTall(self.S(math.min(190,74+(#paths+(#groups>0 and #groups+1 or 0)+(#deleted>0 and 1 or 0))*22)))
 self.folderNodes={}
 self.Folders:Clear() local nodes={}
 local function node(parent,name,path,icon)
  local n=parent:AddNode(name,icon or 'icon16/folder.png') n.Label:SetFont(self.Fonts.Small)
  self.folderNodes[path or false]=n
  n.DoClick=function() self.folder=path self.selected=nil releasePreview(self) self:Refresh() end
  if path and path~='' and not special(path) then
   n.DoRightClick=function()
    local menu=DermaMenu()
    menu:AddOption(L'ui.folder.new_subfolder',function() self:NewFolder(path) end)
    menu:AddOption(L'ui.folder.rename',function() Derma_StringRequest(L'ui.folder.rename_title',L'ui.folder.rename_prompt',path:match('[^/]+$'),function(name)
     local nextPath,err=self:Lib().RenameFolder(path,name) if nextPath then self.folder=nextPath self:Refresh() else self:SetStatus(err,true) end
    end,nil,L'common.ok',L'common.cancel') end)
    menu:AddOption(L'ui.folder.remove',function() Derma_Query(L'ui.folder.remove_question',L'ui.folder.remove_title',L'common.remove',function()
     local ok,err=self:Lib().RemoveFolder(path) self.folder=nil self:Refresh() if not ok then self:SetStatus(err,true) end
    end,L'common.cancel') end)
    menu:Open()
   end
  end
  if path==self.folder then self.Folders:SetSelectedItem(n) end
  return n
 end
 node(self.Folders,self.mode=='static' and L'ui.folder.all_props' or L'ui.folder.all_models',nil,self.mode=='static' and 'icon16/box.png' or 'icon16/group.png') node(self.Folders,L'common.unfiled','','icon16/page_white.png')
 for _,path in ipairs(paths) do local parent=path:match('^(.*)/[^/]+$')
  nodes[path]=node(nodes[parent] or self.Folders,path:match('[^/]+$'),path)
  if nodes[parent] then nodes[parent]:SetExpanded(true) end
 end
 if #groups>0 then
  local root=node(self.Folders,L'ui.folder.workshop',WORKSHOP,'icon16/world.png')
  for _,g in ipairs(groups) do node(root,L('ui.folder.workshop_package',{title=g.title,count=g.count}),WORKSHOP..':'..g.id,g.workshop and 'icon16/package.png' or 'icon16/package_green.png') end
  root:SetExpanded(true)
 end
 if #deleted>0 then node(self.Folders,L('ui.folder.deleted_workshop',{count=#deleted}),DELETED,'icon16/bin_closed.png') end
end
-- Workshop packages that currently provide visible models of this tab.
function PANEL:WorkshopGroups()
 local groups,list={},{}
 for _,entry in pairs(self:Lib().entries) do
  local w=entry.workshop
  if w then
   local g=groups[w.package]
   if not g then g={id=w.package,title=w.title,workshop=w.workshop,count=0} groups[w.package]=g list[#list+1]=g end
   g.count=g.count+1
  end
 end
 table.sort(list,function(a,b) return a.title:lower()<b.title:lower() end)
 return list
end
function PANEL:NewFolder(parent)
 if parent==nil then parent=self.folder end
 Derma_StringRequest(L'ui.folder.new_title',parent and parent~='' and L('ui.folder.create_inside',{folder=parent}) or (self.mode=='static' and L'ui.folder.new_props_prompt' or L'ui.folder.new_characters_prompt'),'',function(name)
  local path,err=self:Lib().CreateFolder(name,parent) if path then self.folder=path self.selected=nil self:Refresh() else self:SetStatus(err,true) end
 end,nil,L'common.ok',L'common.cancel')
end
function PANEL:PopulateMoveMenu(menu)
 local ids={} for _,row in ipairs(self.Models:GetSelected()) do ids[#ids+1]=row.asset end
 if #ids==0 then self:SetStatus(L'ui.status.select_items',true) return end
 local lib=self:Lib()
 local function add(text,path) menu:AddOption(text,function()
  if not IsValid(self) then return end
  local ok,err=lib.MoveToFolder(ids,path) self:Refresh() self:SetStatus(ok and L('ui.moved_items',{count=#ids,folder=path=='' and L'common.unfiled' or path}) or err,not ok)
 end) end
 add(L'common.unfiled','') for _,path in ipairs(lib.Folders()) do add(path,path) end
end
function PANEL:MoveSelected()
 local menu=DermaMenu() self:PopulateMoveMenu(menu) menu:Open()
end
function PANEL:Refresh()
 local physics=self.mode=='physics' local static=self.mode=='static' local listed=self.mode=='library' or static
 self.namesRevision=mmdhl.names and mmdhl.names.revision
 self.Body:SetVisible(not physics)
 if physics and not IsValid(self.PhysicsSettings) then
  self.PhysicsSettings=mmdhl.BuildPhysicsSettings(self,self.S,self.Fonts,styleChoices)
  ownScale(self.PhysicsSettings)
 end
 if IsValid(self.PhysicsSettings) then self.PhysicsSettings:SetVisible(physics) end
 for name,b in pairs(self.Tabs) do b.Selected=name==self.mode end
 self.Import:SetText(static and L'ui.import_props' or L'ui.import_characters')
 self.Import:SetTooltip(static and L'ui.tooltip.import_props' or L'ui.tooltip.import_characters')
 if physics then self.revision=library.revision return end
 local lib=self:Lib()
 local deletedView=listed and self.folder==DELETED
 self.FolderPanel:SetVisible(listed) self.Move:SetVisible(listed and not deletedView) self.Export:SetVisible(listed and not deletedView)
 self.Manage:SetTall(self.S(deletedView and 32 or listed and 108 or 32)) self.Manage:InvalidateLayout()
 if listed then self:RefreshFolders() end
 local selected,entity=self.selected,self.entity local rows={}
 local query=string.Trim(self.Search:GetText()):lower()
 if self.mode=='scene' then
  -- Other addons' copies of player models (a first-person body) are not characters of their own.
  for _,ent in ipairs(mmdhl.Entities()) do if ent.MMDHLCopyOf then continue end local id=mmdhl.GetAsset(ent) local entry=library.entries[id]
   local tag=ent.MMDHLClientRagdollId and ('  · '..L('ui.scene.local_ragdoll',{number=ent.MMDHLClientRagdollId})) or ('  #'..ent:EntIndex())
   local shown,original=shownName('library',entry)
   rows[#rows+1]={id=id,entry=entry,name=(shown or id:sub(1,12))..tag,original=original,entity=ent}
  end
 else
  if deletedView then
   for _,item in ipairs(mmdhl.workshop and mmdhl.workshop.Deleted(kindOf(self.mode)) or {}) do
    rows[#rows+1]={id=item.id,name=item.name,entry={id=item.id,name=item.name,deleted=true,workshop=item,settings={},info={vertices=0,warnings={}},source=''}}
   end
  end
  for id,entry in pairs(deletedView and {} or lib.entries) do
   local folder=entry.settings.folder or ''
   local shown
   if self.folder==nil then shown=true
   elseif self.folder==WORKSHOP then shown=entry.workshop~=nil
   elseif special(self.folder) then shown=entry.workshop~=nil and WORKSHOP..':'..entry.workshop.package==self.folder
   elseif self.folder=='' then shown=folder=='' and not entry.workshop
   else shown=self.folder==folder or folder:sub(1,#self.folder+1)==self.folder..'/' end
   if shown then local name,original=shownName(self.mode,entry) rows[#rows+1]={id=id,entry=entry,name=(entry.settings.favorite and '★  ' or '')..name,original=original} end
  end
 end
 table.sort(rows,function(a,b)
  local af=a.entry and a.entry.settings.favorite or false local bf=b.entry and b.entry.settings.favorite or false
  if af~=bf then return af end return a.name:lower()==b.name:lower() and a.id<b.id or a.name:lower()<b.name:lower()
 end)
 self.refreshing=true self.Models:Clear() local kept
 for _,item in ipairs(rows) do
  if query=='' or (item.name..' '..(item.original or '')..' '..(item.entry and item.entry.source or '')..' '..item.id):lower():find(query,1,true) then
   local e=item.entry
   local row=self.Models:AddLine(item.name,e and ((e.shared or e.deleted) and '—' or string.Comma(e.info.vertices or 0)) or '?') row.asset=item.id row.entity=item.entity row.entry=e
   local origin=e and e.workshop and L('ui.tooltip.workshop_origin',{title=e.workshop.title}) or ''
   row:SetTooltip(item.name..'\n'..(item.original and L('names.original',{name=item.original})..'\n' or '')..(origin~='' and origin..'\n' or '')..(e and e.source~='' and e.source..'\n' or '')..L('ui.tooltip.cache',{id=item.id:sub(1,12)}))
   if listed and e then
    if e.deleted then badge(row,L'ui.badge.deleted',Color(128,136,148),self.Fonts.Small,self.S)
    elseif self.mode=='library' and istable(e.settings.fit) and e.settings.fit.ok==false then badge(row,L'bonemap.badge.needs_bones',Color(191,120,22),self.Fonts.Small,self.S)
    elseif e.workshop then badge(row,L'ui.badge.workshop',Color(40,105,180),self.Fonts.Small,self.S)
    elseif e.shared then badge(row,L'ui.badge.server',Color(142,104,40),self.Fonts.Small,self.S)
    else badge(row,L'ui.badge.own',Color(74,128,92),self.Fonts.Small,self.S) end
   end
   for _,column in pairs(row.Columns) do column:SetFont(self.Fonts.Body) end
   if item.id==selected and (self.mode~='scene' or item.entity==entity) then kept=row self.Models:SelectItem(row) end
  end
 end
 self.refreshing=false self.revision=lib.revision
 self.Count:SetText((static and L('ui.count.props',{shown=#self.Models:GetLines(),total=#rows}) or L('ui.count.models',{shown=#self.Models:GetLines(),total=#rows}))..' · '..L'ui.count.multiselect')
 self.Rename:SetVisible(listed and not deletedView) self.Favorite:SetVisible(listed and not deletedView)
 self.Delete:SetText(self.mode=='scene' and L'ui.manage.remove_selected' or deletedView and L'ui.workshop.restore_selected' or L'ui.manage.delete_selected')
 self.Delete.Style=deletedView and 'primary' or 'danger'
 self.Actions:SetVisible(self.mode=='library' and not deletedView) self.SceneActions:SetVisible(self.mode=='scene') self.PropActions:SetVisible(static and not deletedView)
 if IsValid(self.FaceCamera) then self.FaceCamera:SetVisible(not static) end
 self.ShowCollision:SetVisible(static)
 if kept then self:Choose(kept) else
  self.selected=nil self.entity=nil self.deletedRow=nil releasePreview(self) self:EnableActions(false) self:ShowFitStatus(nil) self.Name:SetText(static and L'ui.select_prop' or L'ui.select_model') self.Warnings:SetVisible(false) self.Origin:SetVisible(false)
  self.Details:SetText(#rows==0 and (self.mode=='scene' and L'ui.empty.scene' or deletedView and L'ui.empty.deleted_workshop' or self.folder==WORKSHOP and L'ui.empty.workshop' or static and L'ui.empty.props' or L'ui.empty.models') or L'ui.select_row')
 end
 self.Delete:SetEnabled(#self.Models:GetSelected()>0) self.Rename:SetEnabled(self.selected~=nil) self.Favorite:SetEnabled(self.selected~=nil) self.Move:SetEnabled(self.selected~=nil)
 self.Export:SetEnabled(#self.Models:GetSelected()>0)
 ownScale(self)
end
function PANEL:Choose(row)
 if row.entry and row.entry.deleted then
  releasePreview(self) self.selected=nil self.entity=nil self.deletedRow=row self:EnableActions(false)
  self.Delete:SetEnabled(true) self.Export:SetEnabled(false) self.Warnings:SetVisible(false)
  self.Name:SetText(row.entry.name) self.Name:SetTooltip(row.entry.name)
  self.Details:SetText(L'ui.details.deleted_workshop') self.Details:SetTooltip('')
  self:ShowOrigin(row.entry.workshop,true)
  return
 end
 self.deletedRow=nil
 local changed=self.selected~=row.asset self.selected=row.asset self.entity=row.entity local entry=row.entry or self:Lib().entries[row.asset]
 self.Delete:SetEnabled(true) self.Rename:SetEnabled(entry~=nil) self.Favorite:SetEnabled(entry~=nil) self.Export:SetEnabled(entry~=nil and not entry.shared)
 if changed then releasePreview(self) self.previewError=nil end
 self:ShowOrigin(entry and entry.workshop,false)
 if entry then
  -- Notes alone keep the neutral style; only real warnings turn the bar orange.
  local warnings,notes=mmdhl.SplitWarnings(entry.info.warnings)
  self.Warnings:SetVisible(#warnings+#notes>0) self.Warnings.Style=#warnings>0 and 'warning' or 'secondary'
  self.Warnings:SetText(#warnings>0 and (#notes>0 and L('ui.warnings.both',{warnings=#warnings,notes=#notes}) or L('ui.warnings.only',{warnings=#warnings})) or L('ui.warnings.notes',{notes=#notes}))
  if self.mode=='static' then
   if self.settingsAsset~=entry.id then
    self.settingsAsset=entry.id local saved=entry.settings.spawn or {}
    self.PropScale:SetValue(math.Clamp(tonumber(saved.scale) or 1,.05,20))
   end
   local info=entry.info
   local shown,original=shownName(self.mode,entry)
   self.Name:SetText(shown) self.Name:SetTooltip(original and L('names.original',{name=original}) or shown)
   self.Details:SetText((entry.shared and L'ui.details.shared_prop' or entry.source~='' and string.GetFileFromFilename(entry.source) or L'ui.details.imported_prop')..'\n'..(entry.settings.folder and entry.settings.folder~='' and L('ui.details.folder',{folder=entry.settings.folder}) or entry.workshop and L'ui.folder.workshop' or L'common.unfiled'))
   self.Details:SetTooltip(L('ui.details.prop_stats',{triangles=string.Comma(info.triangles or 0),vertices=string.Comma(info.vertices or 0),materials=info.materials or 0,hulls=info.collision_hulls or 1})..'\n'..L('ui.details.source',{path=entry.source})..'\n'..L('ui.details.original_name',{name=info.name or ''})..'\n'..table.concat(info.warnings or {},'\n'))
   self.Favorite:SetText(entry.settings.favorite and L'ui.manage.unfavorite' or L'ui.manage.favorite')
   if changed then self:ResetCamera(false) end
   if changed and not library.job and not mmdhl.props.pendingSpawn then self:SetStatus(L('ui.status.ready_prop',{name=shown})) end
   self:EnableActions(not mmdhl.props.pendingSpawn) self.Move:SetEnabled(true)
   return
  end
  if changed then self:ResetCamera(false) end
  if self.settingsAsset~=entry.id then
   self.settingsAsset=entry.id local saved=entry.settings.spawn or {}
   self.ScaleMultiplier:SetValue(math.Clamp(tonumber(saved.scaleMultiplier) or 1,.1,4))
  end
  self:RefreshBodygroupChoices(entry)
  self:ShowFitStatus(entry)
  local shown,original=shownName(self.mode,entry)
  self.Name:SetText(shown) self.Name:SetTooltip(original and L('names.original',{name=original}) or shown)
  local vrmShort,vrmLong=mmdhl.VrmSummary(entry.info)
  self.Details:SetText((entry.source~='' and string.GetFileFromFilename(entry.source) or L'ui.details.imported_character')..(vrmShort and (' · '..vrmShort) or '')..'\n'..(entry.settings.folder and entry.settings.folder~='' and L('ui.details.folder',{folder=entry.settings.folder}) or entry.workshop and L'ui.folder.workshop' or L'common.unfiled'))
  self.Details:SetTooltip(L('ui.details.character_stats',{vertices=string.Comma(entry.info.vertices or 0),bones=entry.info.bones or 0,bodies=entry.info.rigidBodies or 0})..'\n'..L('ui.details.source',{path=entry.source})..'\n'..L('ui.details.original_name',{name=entry.info.name or ''})..(vrmLong and ('\n'..vrmLong) or '')..'\n'..table.concat(entry.info.warnings or {},'\n'))
  self.Favorite:SetText(entry.settings.favorite and L'ui.manage.unfavorite' or L'ui.manage.favorite')
  if changed and not library.job and not mmdhl.pendingSpawn then self:SetStatus(self.mode=='scene' and L'ui.status.scene_selected' or L('ui.status.ready_character',{name=shown})) end
 elseif changed then self:ResetCamera(false) end
 self:EnableActions(self.mode=='library' and not mmdhl.pendingSpawn) self.Move:SetEnabled(entry~=nil)
end
-- A character the fitter cannot map offers the bone window above the spawn buttons.
function PANEL:ShowFitStatus(entry)
 local fit=entry and istable(entry.settings.fit) and entry.settings.fit
 local BM=mmdhl.boneMapper
 local show=fit and fit.ok==false and BM~=nil and BM.Available('fit') or false
 if show then self.AssignBones:SetText(L('bonemap.library.assign_button',{count=istable(fit.missing) and #fit.missing or 0})) end
 if self.AssignBones:IsVisible()~=show then self.AssignBones:SetVisible(show) self.Actions:SetTall(stackHeight(self.Actions)) self.Right:InvalidateLayout() end
end
function PANEL:SelectAsset(id,kind)
 local mode=kind=='static' and 'static' or 'library'
 if self.selected~=id or self.mode~=mode then releasePreview(self) self.previewError=nil end
 self.mode=mode self.selected=id self.folder=nil self.Search:SetText('') self:Refresh()
 self:ResetCamera(false)
end
function PANEL:RenameSelected()
 local lib=self:Lib() local entry=lib.entries[self.selected] if not entry then return end
 self.RenameDialog=Derma_StringRequest(self.mode=='static' and L'ui.rename.prop_title' or L'ui.rename.model_title',L'ui.rename.prompt',entry.name,function(name)
  name=string.Trim(name) if name=='' then return end local ok,err=lib.Update(entry.id,{name=utf8.sub(name,1,100)}) if not ok then self:SetStatus(err,true) end
 end,nil,L'common.ok',L'common.cancel')
end
function PANEL:DeleteSelected()
 local rows=self.Models:GetSelected() if #rows==0 then self:SetStatus(L'ui.status.select_items',true) return end
 local ids,entities={},{} for _,row in ipairs(rows) do ids[#ids+1]=row.asset entities[#entities+1]=row.entity end
 local mode=self.mode
 local W=mmdhl.workshop
 if self.folder==DELETED and (mode=='library' or mode=='static') then
  local ok,message=W.Restore(kindOf(mode),ids) self:SetStatus(message,not ok) self:Refresh() return
 end
 local provided=0 if W and W.Provided and mode~='scene' then for _,id in ipairs(ids) do if W.Provided(kindOf(mode),id) then provided=provided+1 end end end
 local function apply()
  if mode=='scene' then for _,ent in ipairs(entities) do if IsValid(ent) and not mmdhl.RemoveClientRagdoll(ent) then mmdhl.Action('remove',nil,ent) end end self:SetStatus(L('ui.status.removed_characters',{count=#rows})) timer.Simple(.2,function() if IsValid(self) then self:Refresh() end end)
  elseif mode=='static' then
   releasePreview(self)
   local delete=W and W.Delete and function(i,d) W.Delete('static',i,d) end or mmdhl.props.library.Delete
   delete(ids,function(ok,message) if IsValid(self) then self:Refresh() self:SetStatus(message,not ok) end end)
  else
   local delete=W and W.Delete and function(i,d) W.Delete('character',i,d) end or library.Delete
   delete(ids,function(ok,message) if IsValid(self) then self:Refresh() self:SetStatus(message,not ok) end end)
  end
 end
 local question=mode=='scene' and L('ui.delete.scene_question',{count=#rows})
  or mode=='static' and L('ui.delete.props_question',{count=#rows})
  or L('ui.delete.models_question',{count=#rows})
 if provided>0 then question=question..'\n\n'..L('ui.delete.workshop_note',{count=provided}) end
 Derma_Query(question,mode=='scene' and L'ui.delete.scene_title' or mode=='static' and L'ui.delete.props_title' or L'ui.delete.models_title',L'common.delete',apply,L'common.cancel')
end
function PANEL:ShowOrigin(origin,deleted)
 self.originInfo=origin self.originDeleted=deleted
 if not origin then self.Origin:SetVisible(false) self:InvalidateLayout() return end
 local text=origin.workshop and L('ui.workshop.from_workshop',{title=origin.title}) or L('ui.workshop.from_addon',{title=origin.title})
 if origin.author and origin.author~='' then text=text..'  ·  '..L('ui.workshop.by_author',{author=origin.author}) end
 self.OriginText:SetText(text) self.OriginText:SetTooltip(text)
 local action=deleted and L'ui.workshop.restore' or (origin.wsid and origin.wsid~='0') and L'ui.workshop.page' or nil
 self.OriginButton:SetVisible(action~=nil)
 if action then self.OriginButton:SetText(action) surface.SetFont(self.Fonts.Small) self.OriginButton:SetWide(math.max(self.S(110),surface.GetTextSize(action)+self.S(24))) end
 self.OriginButton.Style=deleted and 'primary' or nil
 self.Origin:SetVisible(true) self:InvalidateLayout()
end
function PANEL:OriginAction()
 local origin=self.originInfo if not origin then return end
 if self.originDeleted and self.deletedRow then
  local ok,message=mmdhl.workshop.Restore(kindOf(self.mode),{self.deletedRow.asset}) self:SetStatus(message,not ok) self:Refresh()
 elseif origin.wsid and origin.wsid~='0' then steamworks.ViewFile(origin.wsid) end
end
function PANEL:ExportSelected()
 local ids={} for _,row in ipairs(self.Models:GetSelected()) do if row.entry and not row.entry.deleted and not row.entry.shared then ids[#ids+1]=row.asset end end
 if #ids==0 then self:SetStatus(L'ui.status.select_export',true) return end
 mmdhl.OpenPackageExport(kindOf(self.mode),ids)
end
function PANEL:EnableActions(enabled)
 for _,b in pairs(self.SpawnButtons) do b:SetEnabled(enabled) end
 local prop=enabled and self.mode=='static' and self.selected~=nil
 if IsValid(self.PropSpawn) then self.PropSpawn:SetEnabled(prop) self.PropTool:SetEnabled(prop) self.PropParts:SetEnabled(prop) end
 if IsValid(self.BodygroupEdit) then self.BodygroupEdit:SetEnabled(self.mode=='library' and self.selected~=nil) end
end
function PANEL:Place(role)
 if not self.selected or self.mode~='library' then self:SetStatus(L'ui.status.select_character_tab',true) return end
 local options=mmdhl.GetGlobalSettings() options.scaleMultiplier=self.ScaleMultiplier:GetValue() options.role=role or 'ragdoll' options.gender=self.actorGender or 'female' options.armsParts=mmdhl.GetArmsParts(self.selected)
 options.bodygroups=mmdhl.BodygroupSpawnState and mmdhl.BodygroupSpawnState(self.selected,self.bodygroupPreset) or nil
 library.Update(self.selected,{spawn={scaleMultiplier=options.scaleMultiplier}})
 local ok,err=mmdhl.RequestSpawn(self.selected,options,function(state,message)
  if not IsValid(self) then return end self:SetStatus(message,state=='error') self:EnableActions(state~='loading')
  if state=='ready' then mmdhl.CloseLibrary() end
 end)
 if ok then self:EnableActions(false) self:SetStatus(L'ui.status.sending') else self:SetStatus(err,true) end
end
function PANEL:PlaceProp()
 local P=mmdhl.props
 if not self.selected or self.mode~='static' then self:SetStatus(L'ui.status.select_prop_tab',true) return end
 local scale=self.PropScale:GetValue()
 P.library.Update(self.selected,{spawn={scale=scale}})
 local collide,gravity=P.PlacementCollision()
 local ok,err=P.RequestSpawn(self.selected,{scale=scale,frozen=GetConVar('mmdhl_prop_spawn_frozen'):GetBool(),collide=collide,gravity=gravity,physprop=P.PlacementSurface()},function(state,message)
  if not IsValid(self) then return end self:SetStatus(message,state=='error') self:EnableActions(state~='loading')
  if state=='ready' then mmdhl.CloseLibrary() end
 end)
 if ok then self:EnableActions(false) self:SetStatus(L'ui.status.sending') else self:SetStatus(err,true) end
end
-- Static props: equip the placement tool with the selected prop.
function PANEL:ToolProp()
 local entry=self.selected and mmdhl.props.library.entries[self.selected]
 if not entry then self:SetStatus(L'ui.status.select_prop_tab',true) return end
 local ok,err=mmdhl.props.SelectForTool(self.selected,self.PropScale:GetValue())
 if ok then mmdhl.CloseLibrary() else self:SetStatus(err,true) end
end
function PANEL:EditParts()
 local entry=self.selected and mmdhl.props.library.entries[self.selected]
 if not entry or entry.shared then self:SetStatus(L'ui.status.select_imported_prop',true) return end
 mmdhl.props.OpenEditor(self.selected)
end
function PANEL:EditBodygroups()
 if not self.selected or not library.entries[self.selected] then self:SetStatus(L'ui.status.select_character',true) return end
 mmdhl.OpenBodygroupEditor(self.selected,function() if IsValid(self) then self:RefreshBodygroupChoices(library.entries[self.selected]) end end)
end
function PANEL:RefreshBodygroupChoices(entry)
 local combo=self.BodygroupPreset if not IsValid(combo) then return end
 combo:Clear()
 local presets=mmdhl.BodygroupPresets and mmdhl.BodygroupPresets(entry.id) or {}
 local default=mmdhl.BodygroupDefault and mmdhl.BodygroupDefault(entry.id)
 combo:AddChoice(L'ui.bodygroups.all','',default==nil)
 local names=table.GetKeys(presets) table.sort(names)
 for _,name in ipairs(names) do combo:AddChoice(name==default and L('ui.bodygroups.preset_default',{name=name}) or L('ui.bodygroups.preset',{name=name}),name,name==default) end
 self.bodygroupPreset=default or ''
end
function PANEL:ReimportProp()
 local entry=self.selected and mmdhl.props.library.entries[self.selected]
 if not entry or entry.shared or entry.source=='' then self:SetStatus(L'ui.status.no_source',true) return end
 local ok,err=library.ReimportProp(self.selected)
 self:SetStatus(ok and L('ui.status.reimporting',{name=entry.name}) or err,not ok)
end
function PANEL:Think()
 local visible=mmdhl.PanelVisible(self)
 if not visible then if mmdhl.previewOwner==self or self.propPreview then releasePreview(self) end self.wasVisible=false return end
 if not self.wasVisible then self.wasVisible=true self:Refresh() end
 local notice=mmdhl.names and mmdhl.names.NeedsNotice() or false
 if self.NamesNotice:IsVisible()~=notice then self.NamesNotice:SetVisible(notice) self:InvalidateLayout() end
 if self.mode~='physics' and (self.revision~=self:Lib().revision or self.namesRevision~=(mmdhl.names and mmdhl.names.revision)) then self:Refresh() end
 if self.mode=='scene' and (self.nextScene or 0)<RealTime() then
  self.nextScene=RealTime()+1 local ids={}
  for _,ent in ipairs(mmdhl.Entities()) do if not ent.MMDHLCopyOf then ids[#ids+1]=(ent.MMDHLClientRagdollId and ('local'..ent.MMDHLClientRagdollId) or ent:EntIndex())..':'..mmdhl.GetAsset(ent) end end
  table.sort(ids) local signature=table.concat(ids,'|')
  if self.sceneSignature~=signature then self.sceneSignature=signature self:Refresh() end
 end
 local busy=library.job~=nil
 -- The bone window of an import waiting for the player keeps the banner, with Show.
 local BM=mmdhl.boneMapper local waiting=not busy and BM~=nil and IsValid(BM.frame) and istable(BM.state) and BM.state.mode=='convert'
 -- A hidden docked panel keeps its space until the parent lays out again.
 if self.ImportBanner:IsVisible()~=(busy or waiting) then self.ImportBanner:SetVisible(busy or waiting) self:InvalidateLayout() end
 self.Import:SetEnabled(not library.job and not library.deleting and not (mmdhl.props and mmdhl.props.library.deleting)) self.Cancel:SetVisible(library.job~=nil)
 if self.ShowBones:IsVisible()~=waiting then self.ShowBones:SetVisible(waiting) end
 if self.importStatus~=library.status then self.importStatus=library.status if library.status then self:SetStatus(library.status) end end
 if self.imported~=library.lastImported then self.imported=library.lastImported if self.imported then self:SelectAsset(self.imported,library.lastImportedKind) end end
 if self.mode=='static' then self:ThinkProp() return end
 local info=self.selected and library.entries[self.selected] and library.entries[self.selected].info
 if info then local head,foot for _,b in ipairs(info.boneList or {}) do if b.name=='頭' or (b.english or ''):lower()=='head' then head=b.position[2] end if b.name=='左足首' or (b.english or ''):lower()=='leftfoot' then foot=b.position[2] end end
  local ratio=3.23656*self.ScaleMultiplier:GetValue() self.SizeInfo:SetText(L('ui.character.size_percent',{percent=math.Round(self.ScaleMultiplier:GetValue()*100)})..(head and foot and '  ·  '..L('ui.character.height',{height=string.format('%.0f',(head-foot)*ratio*1.2)}) or ''))
 else self.SizeInfo:SetText('') end
 if self.mode=='physics' then return end
 if self.selected and (mmdhl.previewOwner~=self or self.previewAsset~=self.selected) and not self.previewError then
  local available,renderError=mmdhl.RenderAvailable()
  if not available then self.previewError=renderError self:SetStatus(renderError,true) return end
  if IsValid(mmdhl.previewOwner) and mmdhl.previewOwner~=self and mmdhl.PanelVisible(mmdhl.previewOwner) then return end
  mmdhl.previewOwner=self native.RequestAsset(self.selected)
  local info,err=mmdhl.Decode(native.AssetInfo(self.selected))
  if info then
   self.previewInfo=info self.previewHandle,err=native.CreatePreview(self.selected,util.TableToJSON({scaleMultiplier=self.ScaleMultiplier:GetValue()})) self.previewAsset=self.selected
   if err then self.previewError=err self:SetStatus(err,true) end
  elseif err then self.previewError=err self:SetStatus(err,true) end
 end
 local width=math.Clamp(self.Body:GetWide()*.37,self.S(260),self.S(460))
 if math.abs(self.Left:GetWide()-width)>1 then self.Left:SetWide(width) end
end
function PANEL:ThinkProp()
 local P=mmdhl.props
 local entry=self.selected and P.library.entries[self.selected]
 local info=entry and entry.info
 if info and info.mins and info.maxs then
  local size=(P.Vector(info.maxs)-P.Vector(info.mins))*self.PropScale:GetValue()
  self.PropSizeInfo:SetText(L('ui.prop.size_percent',{percent=math.Round(self.PropScale:GetValue()*100)})..'  ·  '..L('ui.prop.dimensions',{width=string.format('%.0f',size.x),depth=string.format('%.0f',size.y),height=string.format('%.0f',size.z)}))
 else self.PropSizeInfo:SetText('') end
 if IsValid(self.PropReimport) then self.PropReimport:SetEnabled(entry~=nil and not entry.shared and entry.source~='' and not mmdhl.library.job) end
 if self.selected and P.PreviewAsset~=self.selected then self.propPreview=true P.SetPreview(self.selected) self.cameraFramed=nil end
 -- Bounds arrive with the decoded bundle for server-shared props; frame once ready.
 local cached=self.selected and P.RenderCache[self.selected]
 if cached and cached.state=='ready' and self.cameraFramed~=self.selected then self.cameraFramed=self.selected self:ResetCamera(false) end
 local width=math.Clamp(self.Body:GetWide()*.37,self.S(260),self.S(460))
 if math.abs(self.Left:GetWide()-width)>1 then self.Left:SetWide(width) end
end
local previewTargets={}
local function previewTarget(w,h)
 -- Source render targets persist until shutdown. Bucket sizes so resizing or
 -- UI scaling cannot allocate a new target for every pixel of window size.
 local function bucket(n) local v=256 while v<n and v<4096 do v=v*2 end return v end
 local rw,rh=bucket(w),bucket(h) local key=rw..'x'..rh
 if not previewTargets[key] then
  local name='mmdhl_ui_preview_v1_'..key
  local target=GetRenderTargetEx(name,rw,rh,RT_SIZE_NO_CHANGE,MATERIAL_RT_DEPTH_SEPARATE,bit.bor(2,4,8),0,IMAGE_FORMAT_RGBA8888)
  local mat=CreateMaterial('vgui/mmdhl/'..name..'_screen','UnlitGeneric',{['$basetexture']=target:GetName(),['$vertexcolor']='1',['$vertexalpha']='1'})
  previewTargets[key]={target=target,material=mat,w=rw,h=rh}
 end
 return previewTargets[key]
end
function PANEL:DragCamera(h)
 if not self.drag then return end
 local x,y=gui.MouseX(),gui.MouseY()
 if self.drag.key==MOUSE_RIGHT then self.target.z=self.target.z+(y-self.drag.y)*self.distance/math.max(h,1)
 else self.yaw=self.yaw+(x-self.drag.x)*.5 self.pitch=math.Clamp(self.pitch+(y-self.drag.y)*.3,-65,65) end
 self.drag.x=x self.drag.y=y
end
function PANEL:PaintPreview(p,w,h)
 draw.RoundedBox(6,0,0,w,h,Color(28,36,48))
 if not self.previewHandle or mmdhl.previewOwner~=self then draw.SimpleText(self.previewError and L'ui.preview.unavailable' or self.selected and L'ui.preview.loading' or L'ui.preview.select_model',self.Fonts.Body,w/2,h/2,Color(184,202,222),TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER) return end
 self:DragCamera(h)
 if not mmdhl.ImmediateRendering() then return end
 local x,y=p:LocalToScreen(0,0) local angle=Angle(self.pitch,self.yaw,0) local distance=self.distance*math.max(.65,w/math.max(h-self.S(50),1))
 local target=mmdhl.UsesRemixPreview() and previewTarget(w,h)
 if target then
  render.SetScissorRect(0,0,0,0,false)
  render.PushRenderTarget(target.target,0,0,w,h) render.Clear(28,36,48,255,true,true)
 else render.ClearDepth() render.SetScissorRect(x,y,x+w,y+h,true) end
 cam.Start3D(self.target+angle:Forward()*distance,(-angle:Forward()):Angle(),42,target and 0 or x,target and 0 or y,w,h,1,2048)
 local visible=mmdhl.BodygroupSpawnState and mmdhl.BodygroupSpawnState(self.previewAsset,self.bodygroupPreset) or nil
 local ok,drawn,err=xpcall(function() return mmdhl.DrawLibraryPreview(self.previewHandle,self.previewAsset,self.previewInfo,visible) end,debug.traceback)
 cam.End3D()
 if target then render.PopRenderTarget() end
 -- surface.GetScissorRect reports VGUI's current panel clip, not the previous
 -- render override. Leaving that rectangle enabled clips later popup menus
 -- (including the Q-menu NPC/Server/Drawing menus) to this preview panel.
 -- VGUI manages its own clip stack; relinquish our override after 3D drawing.
 render.SetScissorRect(0,0,0,0,false)
 if target then
  surface.SetDrawColor(255,255,255,255) surface.SetMaterial(target.material)
  surface.DrawTexturedRectUV(0,0,w,h,0,0,w/target.w,h/target.h)
 end
 if not ok or not drawn then err=not ok and drawn or err self.previewError=err self:SetStatus(err,true) releasePreview(self) end
 draw.SimpleText((self.mode=='scene' and L'ui.preview.model_preview'..'  ·  ' or '')..L'ui.preview.controls',self.Fonts.Small,self.S(12),self.S(12),Color(184,202,222))
end
-- Studio lighting for the prop preview: a key light from above and the front,
-- darker below, so shape and normal maps read clearly while rotating.
local studio={{1,.8,.8,.8},{2,.55,.55,.6},{3,.72,.72,.72},{4,.72,.72,.72},{5,1,1,1},{6,.3,.3,.33}}
function PANEL:PaintPropPreview(p,w,h)
 local P=mmdhl.props
 draw.RoundedBox(6,0,0,w,h,Color(28,36,48))
 local id=self.selected local entry=id and P.RenderCache[id]
 if not id or not entry or entry.state~='ready' then
  local status=id and P.AssetStatus[id]
  local text=not id and L'ui.preview.select_prop' or entry and entry.state=='failed' and L('ui.preview.failed',{reason=tostring(entry.error or L'ui.preview.unknown_error')}) or (status and status.detail) or L'ui.preview.preparing'
  draw.SimpleText(text,self.Fonts.Body,w/2,h/2,Color(184,202,222),TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER)
  if status and status.total and status.total>0 and entry and entry.state~='failed' then
   surface.SetDrawColor(57,73,91) surface.DrawRect(w*.2,h/2+self.S(18),w*.6,self.S(6))
   surface.SetDrawColor(67,164,244) surface.DrawRect(w*.2,h/2+self.S(18),w*.6*math.Clamp((status.current or 0)/status.total,0,1),self.S(6))
  end
  return
 end
 self:DragCamera(h)
 local x,y=p:LocalToScreen(0,0) local angle=Angle(self.pitch,self.yaw,0)
 local scale=self.PropScale:GetValue()
 render.ClearDepth() render.SetScissorRect(x,y,x+w,y+h,true)
 cam.Start3D(self.target+angle:Forward()*self.distance,(-angle:Forward()):Angle(),42,x,y,w,h,math.max(.5,self.distance*.01),self.distance+self.propRadius*4)
 render.SuppressEngineLighting(true)
 for _,light in ipairs(studio) do render.SetModelLighting(light[1]-1,light[2],light[3],light[4]) end
 local ok,err=xpcall(function()
  P.DrawAsset(id,vector_origin,angle_zero,false,nil,scale,false)
  P.DrawAsset(id,vector_origin,angle_zero,true,nil,scale,false)
  if self.ShowCollision:GetChecked() then P.DrawCollision(id,scale) end
 end,debug.traceback)
 render.SuppressEngineLighting(false)
 cam.End3D()
 render.SetScissorRect(0,0,0,0,false)
 if not ok then self:SetStatus(err,true) end
 draw.SimpleText(L'ui.preview.controls',self.Fonts.Small,self.S(12),self.S(12),Color(184,202,222))
end
function PANEL:OnRemove() releasePreview(self) end
vgui.Register('MMDHLLibrary',PANEL,'DPanel')
function mmdhl.CreateLibrary(parent) return vgui.Create('MMDHLLibrary',parent) end
-- Language choices: automatic (the game's language) or one of the translations.
function mmdhl.LanguageMenu()
 local I=mmdhl.I18n local chosen=I.Chosen() local menu=DermaMenu()
 local auto=menu:AddOption(L('ui.language.automatic',{language=I.LanguageName(I.GameLanguage())}),function() I.Choose('') end)
 if chosen=='' then auto:SetIcon('icon16/tick.png') end
 menu:AddSpacer()
 for _,v in ipairs(I.Languages) do
  if I.Translated(v[1]) then local option=menu:AddOption(v[2],function() I.Choose(v[1]) end) if chosen==v[1] then option:SetIcon('icon16/tick.png') end end
 end
 menu:Open()
end
-- The same choices in a combo box, for the Utilities page.
function mmdhl.BindLanguageChoice(combo)
 local I=mmdhl.I18n local chosen=I.Chosen()
 combo:AddChoice(L('ui.language.automatic',{language=I.LanguageName(I.GameLanguage())}),'',chosen=='')
 for _,v in ipairs(I.Languages) do if I.Translated(v[1]) then combo:AddChoice(v[2],v[1],chosen==v[1]) end end
 combo.OnSelect=function(_,_,_,code) I.Choose(code) end
end
local shownLanguage=mmdhl.I18n.Language()
local function rebuildLibraries(languageChanged)
 for _,holder in ipairs({mmdhl.spawnPanel or false,mmdhl.window or false}) do
  if IsValid(holder) and IsValid(holder.Library) then
   local old=holder.Library local mode,selected,entity,folder,imported=old.mode,old.selected,old.entity,old.folder,old.imported
   local query=old.Search:GetText() local height=old.ScaleMultiplier:GetValue() local size=old.PropScale:GetValue()
   old:Remove() holder.Library=mmdhl.CreateLibrary(holder) holder.Library:Dock(FILL)
   -- imported: the new panel must not select the last import again over the restored state.
   local panel=holder.Library panel.mode=mode panel.selected=selected panel.entity=entity panel.folder=folder panel.imported=imported panel.Search:SetText(query)
   panel:Refresh() panel.ScaleMultiplier:SetValue(height) panel.PropScale:SetValue(size)
   holder.MMDHLLibrary=panel.Models
   if languageChanged then panel:SetStatus(L('ui.language.changed',{language=mmdhl.I18n.LanguageName(mmdhl.I18n.Language())})) end
  end
 end
 if IsValid(mmdhl.window) then mmdhl.window:SetTitle(L'common.external_models') end
end
hook.Add('OnScreenSizeChanged','MMDHL.LibraryScale',function() timer.Simple(.2,function() rebuildLibraries(false) end) end)
-- Also fires when the translation check mode changes, which needs the rebuild but no notice.
hook.Add('MMDHL.LanguageChanged','MMDHL.Library',function(language)
 local changed=language~=shownLanguage shownLanguage=language
 timer.Simple(0,function() rebuildLibraries(changed) end)
end)
function mmdhl.Open(mode)
 if IsValid(mmdhl.window) then mmdhl.window:MakePopup() if mode then mmdhl.window.Library:SetMode(mode) end return end
 if IsValid(g_SpawnMenu) then g_SpawnMenu:Close() end
 local scale=math.Clamp(ScrH()/1080,1,2)
 local frame=vgui.Create('DFrame') mmdhl.window=frame frame:SetTitle(L'common.external_models') frame:SetSize(math.min(ScrW()*.9,1100*scale),math.min(ScrH()*.9,820*scale)) frame:Center() frame:MakePopup()
 if mmdhl.AddInstallationBanner then mmdhl.AddInstallationBanner(frame) end
 frame.Library=mmdhl.CreateLibrary(frame) frame.Library:Dock(FILL) frame.MMDHLLibrary=frame.Library.Models
 if mode then frame.Library:SetMode(mode) end
end
concommand.Add('mmdhl_open',function() mmdhl.Open() end)
concommand.Add('mmdhl_open_props',function() mmdhl.Open('static') end)
include('mmdhl/entity_editor.lua')
hook.Add('PopulateToolMenu','MMDHL.Menu',function()
 spawnmenu.AddToolMenuOption('Utilities','User','MMDHL',L'ui.toolmenu.characters','','',function(panel)
  panel:Help(L'ui.toolmenu.characters_help')
  panel:Button(L'ui.toolmenu.open_library','mmdhl_open')
  panel:Button(L'install.window_title','mmdhl_installation')
  panel:CheckBox(L'install.update.setting','mmdhl_native_update_reminder')
  mmdhl.BindLanguageChoice(panel:ComboBox(L'ui.toolmenu.language'))
  panel:ControlHelp(L'ui.toolmenu.language_help')
  panel:CheckBox(L'terms.setting_import','mmdhl_terms_warning_import')
  panel:CheckBox(L'terms.setting_export','mmdhl_terms_warning_export')
  panel:ControlHelp(L'terms.setting_help')
  panel:Help(L'ui.toolmenu.settings_help')
  panel:CheckBox(L'ui.toolmenu.spawn_frozen','mmdhl_spawn_frozen')
  panel:NumSlider(L'ui.character.npc_health','mmdhl_npc_health',0,mmdhl.MaxNPCHealth,0)
  panel:ControlHelp(L'ui.toolmenu.npc_health_help')
  panel:Help(L'ui.toolmenu.secondary_collision')
  for _,target in ipairs(mmdhl.CollisionTargets) do local box=panel:CheckBox(target.name) box:SetTooltip(target.tooltip) mmdhl.BindCollisionCheckbox(box,target.flag) end
  local backend=panel:ComboBox(L'ui.toolmenu.backend','mmdhl_secondary_backend')
  mmdhl.BindGlobalChoice(backend,'secondaryBackend',mmdhl.SecondaryBackends)
  panel:Help(L'ui.toolmenu.backend_help')
  panel:CheckBox(L'ui.toolmenu.debug_overlay','mmdhl_debug_overlay')
  panel:CheckBox(L'ui.toolmenu.debug_print','mmdhl_debug_print')
  panel:Button(L'ui.toolmenu.debug_report','mmdhl_debug_report')
  panel:Help(L('ui.toolmenu.debug_bind_help',{command='bind F7 mmdhl_debug_toggle'}))
 end)
 spawnmenu.AddToolMenuOption('Utilities','User','MMDHLProps',L'ui.toolmenu.static_props','','',function(panel)
  panel:Help(L'ui.toolmenu.static_props_help')
  panel:Button(L'ui.toolmenu.open_props','mmdhl_open_props')
  panel:CheckBox(L'ui.prop.freeze','mmdhl_prop_spawn_frozen')
 end)
end)
