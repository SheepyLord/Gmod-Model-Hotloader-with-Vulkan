-- This entry still opens when loading the binary fails, showing its actual error.
local L=mmdhl.L
local tabName=L'common.external_models'
spawnmenu.AddCreationTab(tabName,function()
 local panel=vgui.Create('DPanel') mmdhl.spawnPanel=panel
 panel:DockPadding(8,8,8,8)
 mmdhl.AddInstallationBanner(panel)
 panel.Think=function(self)
  if IsValid(self.Library) then return end
  if mmdhl.CreateLibrary then
   if IsValid(self.Error) then self.Error:Remove() end
   self.Library=mmdhl.CreateLibrary(self) self.Library:Dock(FILL)
  elseif not IsValid(self.Error) then
   self.Error=self:Add('DLabel') self.Error:Dock(FILL) self.Error:SetWrap(true) self.Error:SetDark(true)
   self.Error:SetText(mmdhl.loadError or L'menu.loading')
  elseif mmdhl.loadError then self.Error:SetText(mmdhl.loadError) end
 end
 return panel
end,'icon16/user.png',25,L'menu.tab_tooltip')
-- A language change renames the tab at once. The tool gun's texts, the Utilities pages and
-- the weapon list are built with the spawn menu, so it is rebuilt too: now if it is closed,
-- otherwise when it closes, as Sandbox does for its own language changes.
hook.Add('MMDHL.LanguageChanged','MMDHL.SpawnMenu',function()
 local name=L'common.external_models'
 if name~=tabName then
  local tabs=spawnmenu.GetCreationTabs() if tabs[tabName] then tabs[name],tabs[tabName]=tabs[tabName],nil end
  local menu=IsValid(g_SpawnMenu) and g_SpawnMenu.CreateMenu
  local sheet=IsValid(menu) and menu.CreationTabs and menu.CreationTabs[tabName]
  if sheet then
   sheet.Name=name menu.CreationTabs[name],menu.CreationTabs[tabName]=sheet,nil
   if IsValid(sheet.Tab) then sheet.Tab:SetText(name) sheet.Tab:InvalidateLayout(true) menu:InvalidateLayout(true) end
  end
  tabName=name
 end
 -- Sandbox rebuilds the tab from its registered tooltip, which is plain text.
 local tip=L'menu.tab_tooltip'
 local entry=spawnmenu.GetCreationTabs()[tabName] if entry then entry.Tooltip=tip end
 local menu=IsValid(g_SpawnMenu) and g_SpawnMenu.CreateMenu
 local sheet=IsValid(menu) and menu.CreationTabs and menu.CreationTabs[tabName]
 if sheet and IsValid(sheet.Tab) then sheet.Tab:SetTooltip(tip) end
 if not IsValid(g_SpawnMenu) then return end
 if g_SpawnMenu:IsVisible() or g_SpawnMenu.m_UnsavedModifications then g_SpawnMenu.m_NeedsLanguageRefresh=true else RunConsoleCommand('spawnmenu_reload') end
end)
