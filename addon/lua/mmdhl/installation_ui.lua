local M=mmdhl
local L=mmdhl.L
local scale=math.Clamp(ScrH()/1080,1,2)
local fonts={}
local function bodyFont()
 local face=M.I18n.FontFace() local name='MMDHL.InstallationBody.'..face
 if not fonts[name] then surface.CreateFont(name,M.I18n.FontData(16*scale,400)) fonts[name]=true end
 return name
end
local function featureNames() return {core=L'install.feature.core',imports=L'install.feature.imports',detailedCollision=L'install.feature.detailed_collision',rendering=L'install.feature.rendering',physics=L'install.feature.physics'} end
local function describe(v)
 local feature,message=featureNames()[v.feature] or v.component or L'install.feature.installation',M.Localize(v.message)
 return v.feature=='detailedCollision' and L('install.issue_hull_fallback',{feature=feature,message=message}) or L('install.issue',{feature=feature,message=message})
end
-- The listen-server host (or single player) runs the server's files too.
local function hostsServer() return game.SinglePlayer() or (IsValid(LocalPlayer()) and LocalPlayer():IsListenServerHost()) end
-- This client's problems (the module did not load) that installing the recommended
-- package fixes. They read as one instruction (install.binary_problem); detailed lines
-- are for the tooltip and Copy diagnostics. When Windows refused the module, its files
-- did not run either: the warnings that say they run anyway (runsAnyway) join it.
local downloadable={missing=true,unreadable=true,mixed_builds=true}
-- Warnings that say the installed module runs anyway: files this addon does not know, a
-- release too old to check and an outdated one.
local function runsAnyway(v) return v.warning and (v.identity or v.code=='outdated' or v.code=='outdated_release') end
-- Whether Windows refused a status's module (require failed): nothing of it runs. A Recheck
-- while nothing is loaded says a restart loads the files; they run then.
local function refused(s)
 for _,v in ipairs(s and s.issues or {}) do if v.code=='loader_failed' and not v.warning then return true end end
 return false
end
-- Whether this realm's module loaded (CheckInstallation turns core off when it did not).
-- The update windows and the banner's update line say it runs or keeps working, so while it
-- did not, the problem notice and the installation window speak instead.
local function moduleLoaded() local s=M.GetInstallationStatus() return not (s and istable(s.features) and s.features.core==false) end
-- An outdated release still runs: the update reminder speaks for it, so its warning is
-- listed but never pending, and the server's is for its administrator (installation window).
-- So is the warning of a compatibility policy newer than the binary while an update is known.
local function covered(v,update) return v.code=='outdated_release' or (v.code=='compatibility_fallback' and istable(update)) end
-- A server's files this addon does not know are its administrator's business: the
-- installation window lists them, the banner and the notice do not. The listen-server
-- host (or single player) is that administrator and runs the server's files in its own
-- game: for it they read as its own (the one install.binary_unrecognized line, pending).
local function serverOnly(v) return v.warning and v.identity and not hostsServer() end
local function messages(detailed)
 local result,pending,binary,unknown={},0,false,false
 local status=M.GetInstallationStatus()
 local server=M.serverInstallation
 local ownRefused,serverRefused=refused(status),refused(server)
 -- The package unpacked into the wrong folder: its own sentence says where and what to do,
 -- in place of the download instruction (downloading again would not help).
 local misplaced
 for _,v in ipairs(status and status.issues or {}) do
  if not covered(v,status.update) then pending=pending+1 end
  if v.code=='misplaced_package' and not detailed then misplaced=M.Localize(v.message)
  elseif not detailed and ((not v.warning and downloadable[v.code]) or (ownRefused and runsAnyway(v))) then binary=true
  -- Files this addon does not know (they run anyway) read as one line too.
  elseif not detailed and v.warning and v.identity then unknown=true
  else result[#result+1]=describe(v) end
 end
 for _,v in ipairs(server and server.issues or {}) do
  -- The host's server files join that line, unless the server's module did not load (its
  -- loader line speaks for them).
  if not detailed and v.warning and v.identity then unknown=unknown or not (serverOnly(v) or serverRefused)
  elseif detailed or v.code~='outdated_release' then result[#result+1]=L('install.server_issue',{issue=describe(v)}) end
  if not covered(v,server.update) and not serverOnly(v) then pending=pending+1 end
 end
 if misplaced then table.insert(result,1,misplaced) elseif binary then table.insert(result,1,L'install.binary_problem') elseif unknown then table.insert(result,1,L'install.binary_unrecognized') end
 return result,pending,binary,misplaced~=nil
end
-- Dismiss hides the banner and the notice until the problems change; the installation
-- window still lists them. A warning about files this addon does not know comes back for
-- other files (its detail).
local dismissedPath='mmd_hotloader/installation_dismissed.txt'
local function problemsKey()
 local parts,own={},false
 local status,server=M.GetInstallationStatus(),M.serverInstallation
 local function part(v) return tostring(v.code)..'|'..tostring(v.component)..'|'..tostring(v.message)..(v.detail and '|'..tostring(v.detail) or '') end
 for _,v in ipairs(status and status.issues or {}) do if not covered(v,status.update) then parts[#parts+1]=part(v) own=own or (v.warning==true and v.identity==true) end end
 -- The host's server files count too, except beside its own such warnings: the same line
 -- speaks for both, and the server's status comes about a second after the notice checks
 -- Dismiss, which would bring a dismissed notice back on every map.
 for _,v in ipairs(server and server.issues or {}) do if not covered(v,server.update) and not serverOnly(v) and not (own and v.warning and v.identity) then parts[#parts+1]='server|'..part(v) end end
 table.sort(parts) return table.concat(parts,'\n')
end
function M.InstallationDismissed()
 local key=problemsKey()
 return key~='' and file.Read(dismissedPath,'DATA')==key
end
function M.DismissInstallation()
 file.CreateDir('mmd_hotloader') file.Write(dismissedPath,problemsKey())
 hook.Run('MMDHL.InstallationChanged',M.GetInstallationStatus())
end
-- Update reminders: the window once per game run while an older native release runs;
-- Skip this version lasts until a newer release is recommended (or an advisory comes),
-- and mmdhl_native_update_reminder 0 turns them off. The installation window always
-- shows the update. Both are conveniences in DATA, never security decisions.
local reminder=CreateClientConVar('mmdhl_native_update_reminder','1',true,false,'Remind me when a newer Model Hotloader binary module is available',0,1)
cvars.AddChangeCallback('mmdhl_native_update_reminder',function() hook.Run('MMDHL.InstallationChanged',M.GetInstallationStatus()) end,'MMDHL.NativeUpdate')
local updatePath='mmd_hotloader/native_update.json'
local function updateState() local state=util.JSONToTable(file.Read(updatePath,'DATA') or '') return istable(state) and state or {} end
local function saveUpdateState(changes)
 local state=updateState() state.schema=1 for k,v in pairs(changes) do state[k]=v end
 file.CreateDir('mmd_hotloader') file.Write(updatePath,util.TableToJSON(state))
end
local function skipKey(update) return tostring(update.recommended)..(update.advisory and '/'..tostring(update.advisory) or '') end
-- The update this realm's native module would get (status.update), reminded or not.
function M.NativeUpdate() local status=M.GetInstallationStatus() return status and istable(status.update) and status.update or nil end
-- The update the reminders show: on, and not skipped (a required update cannot be skipped).
function M.NativeUpdateDue()
 local update=M.NativeUpdate()
 if not update or not reminder:GetBool() then return nil end
 if not update.required and updateState().skipped==skipKey(update) then return nil end
 return update
end
function M.SkipNativeUpdate()
 local update=M.NativeUpdate() if update then saveUpdateState({skipped=skipKey(update)}) end
 hook.Run('MMDHL.InstallationChanged',M.GetInstallationStatus())
end
function M.StopNativeUpdateReminders() RunConsoleCommand('mmdhl_native_update_reminder','0') end
-- Lua starts again with every map; the game's clock (SysTime) runs from its start.
local function remindedThisRun() local at=tonumber(updateState().reminded) return at~=nil and at>=os.time()-SysTime()-2 end
-- policy.revoked names the advisory's phrase; a phrase this catalogue lacks reads as the general one.
local function advisoryText(update)
 local key=update.advisory
 if not isstring(key) or not key:match('^install%.advisory%.[%w_]+$') then return L'install.update.advisory' end
 local text=L(key)
 return text~='mmdhl.'..key and text or L'install.update.advisory'
end
-- The banner's line while an update is due; the installation window always has it, while
-- the module runs (the line says everything keeps working; the update tag stays).
local function updateLine(always)
 local update=always and M.NativeUpdate() or M.NativeUpdateDue()
 if not update or update.required or not moduleLoaded() then return nil end
 local line=L('install.update.banner',{recommended=tostring(update.recommended),installed=tostring(update.installed)})
 return update.advisory and line..' '..advisoryText(update) or line
end
-- The server's update, for its administrators: a dedicated server (whatever their own
-- game runs), or a listen server whose files differ from the host's own.
local function serverUpdateLine()
 local server,own=M.serverInstallation,M.NativeUpdate()
 local update=server and istable(server.update) and server.update
 if not update or update.required or game.SinglePlayer() then return nil end
 local p=LocalPlayer()
 if not (hostsServer() or (IsValid(p) and p:IsAdmin())) or (hostsServer() and own and own.installed==update.installed) then return nil end
 return L('install.update.server',{installed=tostring(update.installed),recommended=tostring(update.recommended)})
end
-- Which d3d9.dll the game renders through (installation.lua reports it; it never blocks a feature).
-- On Linux the game always renders through OpenGL.
local function rendererLine(status)
 local r=status and status.renderer
 if not r then return nil end
 if r.kind=='opengl' then return L'install.renderer.opengl' end
 if r.kind=='dxvk' then return r.current and L'install.renderer.dxvk' or L('install.renderer.dxvk_release',{release=tostring(r.release)}) end
 return r.kind=='d3d9' and L'install.renderer.d3d9' or L'install.renderer.other'
end
-- The installed release, or for files this addon does not know the loaded module's own
-- label and build.
local function nativeVersion(status)
 if status and status.installed then return tostring(status.installed) end
 local module=status and istable(status.loaded) and istable(status.loaded.module) and status.loaded.module
 if module and isstring(module.release) then return module.release..(isstring(module.build) and ' ('..module.build..')' or '') end
 return L'install.not_verified'
end
-- Also returns how many problems and warnings the banner raises.
function M.InstallationSummary()
 local status=M.GetInstallationStatus()
 local issues,pending=messages()
 local versions=L('install.native_release',{version=nativeVersion(status)})..'   '..L('install.recommended_release',{version=tostring(status and status.recommended or L'install.unknown_version')})
 if M.NativeUpdate() then versions=versions..' '..L'install.update.available_tag' end
 local renderer=rendererLine(status)
 if renderer then versions=versions..'   '..renderer end
 return table.concat(issues,'\n'),versions,table.concat(messages(true),'\n'),pending
end
-- The Download button opens only the public repository's releases page (or a release on it).
local releases='https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/releases'
local function download()
 local status=M.GetInstallationStatus()
 local url=status and status.download
 if isstring(url) and (url==releases or url:find(releases..'/',1,true)==1) then gui.OpenURL(url) end
end
-- The alternative link comes from the Workshop policy; accept only a plain https address.
local function alternative()
 local status=M.GetInstallationStatus()
 local url=status and status.downloadAlt
 return isstring(url) and url:match('^https://[%w%-%.]+[:/][%w%-%._~:/%?#%[%]@!%$&\'%(%)%*%+,;=%%]*$') and url or nil
end
function M.AddInstallationBanner(parent,always)
 local panel=parent:Add('DPanel') panel:Dock(TOP) panel:DockMargin(0,0,0,8) panel:DockPadding(10,8,10,8)
 panel.Paint=function(_,w,h) surface.SetDrawColor(255,232,188) surface.DrawRect(0,0,w,h) end
 local controls=panel:Add('DPanel') controls:Dock(BOTTOM) controls:SetTall(30*scale) controls:SetPaintBackground(false)
 local function label(b,text) b:SetText(text) surface.SetFont(b:GetFont()) b:SetWide(math.max(b.minimum,surface.GetTextSize(text)+16*scale)) end
 local function button(text,click,width)
  local b=controls:Add('DButton') b:Dock(LEFT) b:DockMargin(0,0,8,0) b.DoClick=click b.minimum=(width or 140)*scale
  label(b,text) return b
 end
 local get=button(L'install.button.download',download,190)
 local alt=button(L'install.button.download_alternative',function() local url=alternative() if url then gui.OpenURL(url) end end,170)
 local copy=button(L'install.button.copy_diagnostics',function() SetClipboardText(util.TableToJSON({installation=M.GetInstallationStatus(),server=M.serverInstallation},true)) end)
 local recheck=button(L'install.button.recheck',function() M.CheckInstallation(true) end,90)
 -- Dismiss hides the problems first; with none left, the update line (Skip this version).
 local function skips() local _,pending=messages() return updateLine()~=nil and (pending==0 or M.InstallationDismissed()) end
 -- At the right edge, so a narrow window cannot push it out of view.
 local dismiss=button(L'install.button.dismiss',function() if skips() then M.SkipNativeUpdate() else M.DismissInstallation() end end,90) dismiss:Dock(RIGHT) dismiss:DockMargin(8,0,0,0)
 local scroll=panel:Add('DScrollPanel') scroll:Dock(FILL) scroll:DockMargin(0,0,0,8)
 local summary=scroll:Add('DLabel') summary:Dock(TOP) summary:SetWrap(true) summary:SetAutoStretchVertical(true) summary:SetFont(bodyFont()) summary:SetDark(true)
 local function refresh()
  if not IsValid(panel) then return end
  local text,versions,details,pending=M.InstallationSummary()
  local mirror=alternative()
  local update,server=updateLine(always),always and serverUpdateLine()
  -- Dismissed warnings stay listed in the installation window only; an update line shows
  -- the banner by itself, never as a problem.
  panel:SetVisible(always or (pending>0 and not M.InstallationDismissed()) or update~=nil) panel:SetTall((always and 190 or 150)*scale)
  alt:SetVisible(mirror~=nil) dismiss:SetVisible(not always and (pending>0 or update~=nil)) controls:InvalidateLayout()
  dismiss:SetTooltip(skips() and L('install.update.dismiss_tip',{recommended=tostring(M.NativeUpdateDue().recommended)}) or L'install.button.dismiss_tip')
  local lines={versions}
  for _,line in ipairs({update or false,server or false,mirror and L('install.alternative_link',{url=mirror}) or false}) do if line then lines[#lines+1]=line end end
  lines[#lines+1]=text~='' and text or L'install.verified'
  summary:SetText(table.concat(lines,'\n')) summary:SetTooltip(details~='' and details or nil)
  parent:InvalidateLayout(true)
 end
 hook.Add('MMDHL.InstallationChanged',panel,refresh) refresh()
 -- Windows stay open across a language switch (the spawn-menu copy is rebuilt with the menu).
 hook.Add('MMDHL.LanguageChanged',panel,function()
  label(get,L'install.button.download') label(alt,L'install.button.download_alternative') label(copy,L'install.button.copy_diagnostics') label(recheck,L'install.button.recheck')
  label(dismiss,L'install.button.dismiss')
  summary:SetFont(bodyFont()) refresh()
 end)
 return panel
end
function M.OpenInstallation()
 local frame=vgui.Create('DFrame') frame:SetTitle(L'install.window_title') frame:SetSize(math.min(850*scale,ScrW()-40),math.min(410*scale,ScrH()-40)) frame:Center() frame:MakePopup()
 M.AddInstallationBanner(frame,true)
 -- Turns reminders back on after Don't remind me again; also under Utilities, which needs the loaded addon.
 local remind=frame:Add('DCheckBoxLabel') remind:Dock(BOTTOM) remind:DockMargin(12,0,12,10) remind:SetTall(22*scale) remind:SetConVar('mmdhl_native_update_reminder')
 remind:SetText(L'install.update.setting') remind:SetFont(bodyFont())
 local help=frame:Add('DLabel') help:SetFont(bodyFont()) help:Dock(FILL) help:DockMargin(12,8,12,8) help:SetWrap(true)
 help:SetText(L'install.help')
 hook.Add('MMDHL.LanguageChanged',frame,function() frame:SetTitle(L'install.window_title') help:SetFont(bodyFont()) help:SetText(L'install.help') remind:SetText(L'install.update.setting') remind:SetFont(bodyFont()) end)
 return frame
end
concommand.Add('mmdhl_open',function() M.OpenInstallation() end)
concommand.Add('mmdhl_open_props',function() M.OpenInstallation() end)
-- The library replaces mmdhl_open once it loads; this one always opens the installation window.
concommand.Add('mmdhl_installation',function() M.OpenInstallation() end)
-- A closed window stays valid until VGUI deletes it on a later frame.
local function updateWindowOpen() return IsValid(M.updateWindow) and M.updateWindow:IsVisible() end
-- Whether the update window opens on this map: once per game run, before the problem
-- notice, which waits until it closes. A problem the download fixes says so itself, so
-- then it stays closed. A required update stands in for the notice, and Dismiss in the
-- banner silences it as it does the notice. Both windows say the module runs: while it
-- did not load, neither opens and the notice speaks for the failure.
local function updateComing()
 local update=M.NativeUpdateDue()
 if not update or M.updateNoticeShown or remindedThisRun() or not moduleLoaded() then return false end
 if update.required then return not M.InstallationDismissed() end
 local _,_,binary=messages() return not binary
end
-- Installed into the wrong folder (issue #6): a window says so once per game run, until the
-- files are moved or the player turns it off for this copy. The notice, the banner and the
-- External Models tab say it too.
local misplacedPath='mmd_hotloader/misplaced_dismissed.txt'
local guide='https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan#install-and-use'
local function misplacedIssue()
 local status=M.GetInstallationStatus()
 for _,v in ipairs(status and status.issues or {}) do if v.code=='misplaced_package' then return v end end
end
-- Don't show this again: for this misplaced copy, neither the window nor the notice.
local function misplacedSilenced() local v=misplacedIssue() return v~=nil and file.Read(misplacedPath,'DATA')==tostring(v.detail) end
local function misplacedDue()
 local v=misplacedIssue()
 if not v or misplacedSilenced() then return false end
 local at=tonumber(updateState().misplaced) return not (at~=nil and at>=os.time()-SysTime()-2)
end
function M.OpenMisplaced()
 local v=misplacedIssue()
 if not v then return M.OpenInstallation() end
 if IsValid(M.misplacedWindow) and M.misplacedWindow:IsVisible() then M.misplacedWindow:MakePopup() return M.misplacedWindow end
 local frame=vgui.Create('DFrame') M.misplacedWindow=frame M.installationNoticeShown=true
 saveUpdateState({misplaced=os.time()})
 frame:SetSize(math.min(700*scale,ScrW()-40),math.min(420*scale,ScrH()-40)) frame:Center() frame:MakePopup()
 local buttons=frame:Add('DPanel') buttons:Dock(BOTTOM) buttons:DockMargin(6,6,6,6) buttons:SetTall(30*scale) buttons:SetPaintBackground(false)
 local function label(b,text) b:SetText(text) surface.SetFont(b:GetFont()) b:SetWide(math.max(b.minimum,surface.GetTextSize(text)+16*scale)) end
 local function button(side,click,width) local b=buttons:Add('DButton') b:Dock(side) b:DockMargin(side==RIGHT and 8 or 0,0,side==LEFT and 8 or 0,0) b.DoClick=click b.minimum=width*scale return b end
 local open=button(LEFT,function() gui.OpenURL(guide) end,170)
 local close=button(RIGHT,function() frame:Close() end,110)
 local never=button(RIGHT,function() file.CreateDir('mmd_hotloader') file.Write(misplacedPath,tostring(v.detail)) frame:Close() end,170)
 local scroll=frame:Add('DScrollPanel') scroll:Dock(FILL) scroll:DockMargin(10,6,10,0)
 local body=scroll:Add('DLabel') body:Dock(TOP) body:SetWrap(true) body:SetAutoStretchVertical(true)
 local function refresh()
  local status=M.GetInstallationStatus() local files=status and status.files or {}
  local expected={}
  -- Relative to the Garry's Mod folder: the module's path is in garrysmod (MOD). Windows
  -- paths are shown with backslashes.
  local separator=(status and status.platform or 'win64')=='win64' and '\\' or '/'
  for _,key in ipairs({'runtime',status and status.realm or 'client'}) do local f=files[key] if f and isstring(f.relative) then expected[#expected+1]=(((f.search=='MOD' and 'garrysmod/' or '')..f.relative):gsub('/',separator)) end end
  frame:SetTitle(L'install.misplaced.title')
  body:SetFont(bodyFont()) body:SetText(L('install.misplaced.text',{found=(tostring(v.found or v.detail):gsub('/',separator)),expected=table.concat(expected,'\n')}))
  label(open,L'install.misplaced.button.guide') label(never,L'install.misplaced.button.never') label(close,L'common.close')
  buttons:InvalidateLayout()
 end
 refresh()
 hook.Add('MMDHL.LanguageChanged',frame,refresh)
 return frame
end
concommand.Add('mmdhl_installation_folder',function() M.OpenMisplaced() end)
local function notify()
 -- Warnings the player dismissed do not raise the notice.
 local _,pending,binary,misplaced=messages()
 if M.installationNoticeShown or pending==0 or not IsValid(LocalPlayer()) then return end
 -- Installed into the wrong folder: the window is this run's notice.
 if misplacedDue() then M.OpenMisplaced() return end
 if M.InstallationDismissed() or (misplaced and misplacedSilenced()) then return end
 -- Never beside the update window (its OnClose asks again); a required update's window was this map's notice.
 local update=M.NativeUpdateDue()
 if updateWindowOpen() or updateComing() or (update and update.required and M.updateNoticeShown) then return end
 M.installationNoticeShown=true
 local panel=vgui.Create('DPanel') panel:SetSize(math.min(540,ScrW()-40),92) panel:SetPos(ScrW()-panel:GetWide()-20,40)
 panel:SetMouseInputEnabled(true)
 local close=panel:Add('DButton') close:Dock(RIGHT) close:SetWide(28) close:SetText('×') close.DoClick=function() panel:Remove() end
 local details=panel:Add('DButton') details:Dock(FILL) details:SetWrap(true) details:SetFont(bodyFont()) details:SetText(misplaced and L'install.notice_misplaced' or binary and L'install.notice_binary' or L'install.notice')
 details.DoClick=function() panel:Remove() if misplaced then M.OpenMisplaced() else M.OpenInstallation() end end
 timer.Simple(20,function() if IsValid(panel) then panel:Remove() end end)
end
-- The update window: Download (the releases page only) or the mirror, then Remind me
-- later (the next game run), Skip this version or Don't remind me again. A needed
-- update (a module this addon cannot check: too old, or another interface) cannot be
-- skipped; it still runs. While the module did not load, the installation window opens
-- instead (its Download buttons and the update tag): this one says the module runs.
function M.OpenNativeUpdate()
 local update=M.NativeUpdate()
 if not update or not moduleLoaded() then return M.OpenInstallation() end
 if updateWindowOpen() then M.updateWindow:MakePopup() return M.updateWindow end
 -- Opened by hand too, it is this map's reminder.
 local frame=vgui.Create('DFrame') M.updateWindow=frame M.updateNoticeShown=true
 frame:SetSize(math.min(640*scale,ScrW()-40),math.min(360*scale,ScrH()-40)) frame:Center() frame:MakePopup()
 local function row() local r=frame:Add('DPanel') r:Dock(BOTTOM) r:DockMargin(6,6,6,0) r:SetTall(30*scale) r:SetPaintBackground(false) return r end
 local choices,links=row(),row()
 local function label(b,text) b:SetText(text) surface.SetFont(b:GetFont()) b:SetWide(math.max(b.minimum,surface.GetTextSize(text)+16*scale)) end
 local function button(parent,side,click,width)
  local b=parent:Add('DButton') b:Dock(side) b:DockMargin(side==RIGHT and 8 or 0,0,side==LEFT and 8 or 0,0) b.DoClick=click b.minimum=width*scale return b
 end
 local get=button(links,LEFT,download,170)
 local alt=button(links,LEFT,function() local url=alternative() if url then gui.OpenURL(url) end end,150)
 -- Docked from the right edge: Remind me later is the rightmost.
 local later=button(choices,RIGHT,function() frame:Close() end,120)
 local skip=button(choices,RIGHT,function() M.SkipNativeUpdate() frame:Close() end,120)
 local never=button(choices,RIGHT,function() M.StopNativeUpdateReminders() frame:Close() end,150)
 local scroll=frame:Add('DScrollPanel') scroll:Dock(FILL) scroll:DockMargin(6,4,6,0)
 local body=scroll:Add('DLabel') body:Dock(TOP) body:SetWrap(true) body:SetAutoStretchVertical(true)
 local function refresh()
  local vars={installed=tostring(update.installed or L'install.unknown_version'),recommended=tostring(update.recommended)}
  local mirror=alternative()
  local parts={update.required and L('install.update.text_required',vars) or L('install.update.text',vars)}
  if update.advisory then parts[#parts+1]=advisoryText(update) end
  if mirror then parts[#parts+1]=L('install.alternative_link',{url=mirror}) end
  frame:SetTitle(update.required and L'install.update.title_required' or L'install.update.title')
  body:SetFont(bodyFont()) body:SetText(table.concat(parts,'\n\n'))
  label(get,L'install.update.button.download') label(alt,L'install.button.download_alternative') alt:SetVisible(mirror~=nil)
  label(later,update.required and L'common.close' or L'install.update.button.later') later:SetTooltip(not update.required and L'install.update.later_tip' or nil)
  label(skip,L'install.update.button.skip') skip:SetTooltip(L('install.update.skip_tip',{recommended=vars.recommended})) skip:SetVisible(not update.required)
  label(never,L'install.update.button.never') never:SetTooltip(L'install.update.never_tip') never:SetVisible(not update.required)
  links:InvalidateLayout() choices:InvalidateLayout()
 end
 refresh()
 hook.Add('MMDHL.LanguageChanged',frame,refresh)
 -- The problems left (or one that came up meanwhile) get their notice once the window closes.
 frame.OnClose=function() if M.updateWindow==frame then M.updateWindow=nil end timer.Simple(0,notify) end
 return frame
end
concommand.Add('mmdhl_native_update',function() M.OpenNativeUpdate() end)
local function notifyUpdate()
 if not IsValid(LocalPlayer()) then return end
 -- Not this map: the problem notice the window held back, if any.
 if not updateComing() then return notify() end
 saveUpdateState({reminded=os.time()})
 M.OpenNativeUpdate()
end
-- For a feature whose native function this binary lacks (other features call this, with
-- an already localized name): a small dialog per feature. Its button opens the update
-- window, or the installation window while no newer release is known.
local needed={}
function M.ShowNativeUpdateNeeded(feature,release)
 local key=tostring(feature)
 if IsValid(needed[key]) and needed[key]:IsVisible() then needed[key]:MakePopup() return needed[key] end
 needed[key]=Derma_Query(L('install.update.needed',{feature=key,release=tostring(release or L'install.unknown_version')}),L'install.update.needed_title',L'install.update.button.show',function() M.OpenNativeUpdate() end,L'common.close')
 return needed[key]
end
hook.Add('MMDHL.InstallationChanged','MMDHL.InstallationNotice',notify)
hook.Add('InitPostEntity','MMDHL.InstallationNotice',function() timer.Simple(1,notify) end)
hook.Add('InitPostEntity','MMDHL.NativeUpdateNotice',function() timer.Simple(5,notifyUpdate) end)
