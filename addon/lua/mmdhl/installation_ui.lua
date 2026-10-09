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
 local line=v.feature=='detailedCollision' and L('install.issue_hull_fallback',{feature=feature,message=message}) or L('install.issue',{feature=feature,message=message})
 return v.accepted and line..' '..L'install.accepted_tag' or line
end
-- The listen-server host (or single player) shares the server's data folder and
-- may accept the server's unverified files together with its own.
local function hostsServer() return game.SinglePlayer() or (IsValid(LocalPlayer()) and LocalPlayer():IsListenServerHost()) end
local function acceptable(s) return s and s.unverified and not s.blocked and s.fingerprint end
-- This client's problems that installing the recommended package fixes. They read
-- as one instruction (install.binary_problem); detailed lines are for the tooltip
-- and Copy diagnostics.
local downloadable={missing=true,unreadable=true,mixed_installation=true,damaged_or_unrecognized=true,outdated=true,unapproved_release=true}
-- An outdated release still runs: the update reminder speaks for it, so its warning is
-- listed but never pending, and the server's is for its administrator (installation window).
local function messages(detailed)
 local result,pending,binary={},0,false
 local status=M.GetInstallationStatus()
 local server=M.serverInstallation
 if (status and status.acceptedIssues) or (server and server.unverifiedAccepted) then result[#result+1]=L'install.unverified.in_use' end
 for _,v in ipairs(status and status.issues or {}) do
  if not v.accepted and v.code~='outdated_release' then pending=pending+1 end
  if not detailed and not v.accepted and downloadable[v.code] then binary=true else result[#result+1]=describe(v) end
 end
 if binary then table.insert(result,1,L'install.binary_problem') end
 for _,v in ipairs(server and server.issues or {}) do
  if detailed or v.code~='outdated_release' then result[#result+1]=L('install.server_issue',{issue=describe(v)}) end
  if not v.accepted and v.code~='outdated_release' then pending=pending+1 end
 end
 if acceptable(server) and not hostsServer() then result[#result+1]=L'install.unverified.server_admin' end
 return result,pending,binary
end
-- Dismiss hides the banner and the notice until the unaccepted problems change;
-- the installation window still lists them.
local dismissedPath='mmd_hotloader/installation_dismissed.txt'
local function problemsKey()
 local parts={}
 local status,server=M.GetInstallationStatus(),M.serverInstallation
 for _,v in ipairs(status and status.issues or {}) do if not v.accepted and v.code~='outdated_release' then parts[#parts+1]=tostring(v.code)..'|'..tostring(v.component)..'|'..tostring(v.message) end end
 for _,v in ipairs(server and server.issues or {}) do if not v.accepted and v.code~='outdated_release' then parts[#parts+1]='server|'..tostring(v.code)..'|'..tostring(v.component)..'|'..tostring(v.message) end end
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
-- The banner's line while an update is due; the installation window always has it.
local function updateLine(always)
 local update=always and M.NativeUpdate() or M.NativeUpdateDue()
 if not update or update.required then return nil end
 local line=L('install.update.banner',{recommended=tostring(update.recommended),installed=tostring(update.installed)})
 return update.advisory and line..' '..advisoryText(update) or line
end
-- The server's update, for its administrators: a dedicated server, or a listen
-- server whose files differ from the host's own.
local function serverUpdateLine()
 local server,own=M.serverInstallation,M.NativeUpdate()
 local update=server and istable(server.update) and server.update
 if not update or update.required or game.SinglePlayer() then return nil end
 local p=LocalPlayer()
 if not (hostsServer() or (IsValid(p) and p:IsAdmin())) or (own and own.installed==update.installed) then return nil end
 return L('install.update.server',{installed=tostring(update.installed),recommended=tostring(update.recommended)})
end
function M.CanAcceptUnverifiedNative()
 return not not (acceptable(M.GetInstallationStatus()) or (hostsServer() and acceptable(M.serverInstallation)))
end
function M.UsingUnverifiedNative()
 local status=M.GetInstallationStatus()
 return not not ((status and status.unverifiedAccepted) or (hostsServer() and M.serverInstallation and M.serverInstallation.unverifiedAccepted))
end
-- Acceptance takes effect when the addon loads again (map change or restart).
function M.AcceptUnverifiedNative()
 local status,server=M.GetInstallationStatus(),M.serverInstallation
 if acceptable(status) then M.SetUnverifiedAccepted('client',status.fingerprint) end
 if hostsServer() and acceptable(server) then M.SetUnverifiedAccepted('server',server.fingerprint) end
 Derma_Message(L'install.unverified.accepted',L'install.window_title',L'common.ok')
end
function M.RevokeUnverifiedNative()
 M.SetUnverifiedAccepted('client',nil)
 if hostsServer() then M.SetUnverifiedAccepted('server',nil) end
 Derma_Message(L'install.unverified.revoked',L'install.window_title',L'common.ok')
end
function M.ConfirmUnverifiedNative()
 Derma_Query(L'install.unverified.warning',L'install.unverified.title',L'install.unverified.confirm',M.AcceptUnverifiedNative,L'common.cancel')
end
-- Which d3d9.dll the game renders through (installation.lua reports it; it never blocks a feature).
local function rendererLine(status)
 local r=status and status.renderer
 if not r then return nil end
 if r.kind=='dxvk' then return r.current and L'install.renderer.dxvk' or L('install.renderer.dxvk_release',{release=tostring(r.release)}) end
 return r.kind=='d3d9' and L'install.renderer.d3d9' or L'install.renderer.other'
end
-- Also returns how many problems the player has not accepted.
function M.InstallationSummary()
 local status=M.GetInstallationStatus()
 local issues,pending=messages()
 local versions=L('install.native_release',{version=tostring(status and status.installed or L'install.not_verified')})..'   '..L('install.recommended_release',{version=tostring(status and status.recommended or L'install.unknown_version')})
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
 local useAnyway=button(L'install.button.use_anyway',function() M.ConfirmUnverifiedNative() end,120)
 local stopUsing=button(L'install.button.stop_using',function() M.RevokeUnverifiedNative() end,200)
 -- At the right edge, so a narrow window cannot push it out of view.
 local dismiss=button(L'install.button.dismiss',function() if updateLine() then M.SkipNativeUpdate() end M.DismissInstallation() end,90) dismiss:Dock(RIGHT) dismiss:DockMargin(8,0,0,0) dismiss:SetTooltip(L'install.button.dismiss_tip')
 local scroll=panel:Add('DScrollPanel') scroll:Dock(FILL) scroll:DockMargin(0,0,0,8)
 local summary=scroll:Add('DLabel') summary:Dock(TOP) summary:SetWrap(true) summary:SetAutoStretchVertical(true) summary:SetFont(bodyFont()) summary:SetDark(true)
 local function refresh()
  if not IsValid(panel) then return end
  local text,versions,details,pending=M.InstallationSummary()
  local mirror=alternative()
  local update,server=updateLine(always),always and serverUpdateLine()
  -- Accepted (Use anyway) and dismissed warnings stay listed in the installation window only;
  -- an update line shows the banner by itself, never as a problem.
  panel:SetVisible(always or (pending>0 and not M.InstallationDismissed()) or update~=nil) panel:SetTall((always and 190 or 150)*scale)
  alt:SetVisible(mirror~=nil) useAnyway:SetVisible(M.CanAcceptUnverifiedNative()) stopUsing:SetVisible(M.UsingUnverifiedNative()) dismiss:SetVisible(not always and (pending>0 or update~=nil)) controls:InvalidateLayout()
  local lines={versions}
  for _,line in ipairs({update or false,server or false,mirror and L('install.alternative_link',{url=mirror}) or false}) do if line then lines[#lines+1]=line end end
  lines[#lines+1]=text~='' and text or L'install.verified'
  summary:SetText(table.concat(lines,'\n')) summary:SetTooltip(details~='' and details or nil)
  parent:InvalidateLayout(true)
 end
 hook.Add('MMDHL.InstallationChanged',panel,refresh) refresh()
 -- Windows stay open across a language switch (the spawn-menu copy is rebuilt with the menu).
 hook.Add('MMDHL.LanguageChanged',panel,function()
  label(get,L'install.button.download') label(alt,L'install.button.download_alternative') label(copy,L'install.button.copy_diagnostics') label(recheck,L'install.button.recheck') label(useAnyway,L'install.button.use_anyway') label(stopUsing,L'install.button.stop_using')
  label(dismiss,L'install.button.dismiss') dismiss:SetTooltip(L'install.button.dismiss_tip')
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
local function notify()
 -- Warnings the player already accepted or dismissed do not raise the notice.
 local _,pending,binary=messages()
 if M.installationNoticeShown or pending==0 or M.InstallationDismissed() or not IsValid(LocalPlayer()) then return end
 -- Never beside the update window; a module this Lua cannot use is that window's to say.
 local update=M.NativeUpdateDue()
 if IsValid(M.updateWindow) or (update and update.required) then return end
 M.installationNoticeShown=true
 local panel=vgui.Create('DPanel') panel:SetSize(math.min(540,ScrW()-40),92) panel:SetPos(ScrW()-panel:GetWide()-20,40)
 panel:SetMouseInputEnabled(true)
 local close=panel:Add('DButton') close:Dock(RIGHT) close:SetWide(28) close:SetText('×') close.DoClick=function() panel:Remove() end
 local details=panel:Add('DButton') details:Dock(FILL) details:SetWrap(true) details:SetFont(bodyFont()) details:SetText(binary and L'install.notice_binary' or L'install.notice') details.DoClick=function() panel:Remove() M.OpenInstallation() end
 timer.Simple(20,function() if IsValid(panel) then panel:Remove() end end)
end
-- The update window: Download (the releases page only) or the mirror, then Remind me
-- later (the next game run), Skip this version or Don't remind me again. A required
-- update (a module this Lua cannot use) cannot be skipped.
function M.OpenNativeUpdate()
 local update=M.NativeUpdate()
 if not update then return M.OpenInstallation() end
 if IsValid(M.updateWindow) then M.updateWindow:MakePopup() return M.updateWindow end
 local frame=vgui.Create('DFrame') M.updateWindow=frame
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
 -- A problem that came up meanwhile gets its notice once the window closes.
 frame.OnClose=function() timer.Simple(0,notify) end
 return frame
end
concommand.Add('mmdhl_native_update',function() M.OpenNativeUpdate() end)
local function notifyUpdate()
 local update=M.NativeUpdateDue()
 if not update or M.updateNoticeShown or not IsValid(LocalPlayer()) then return end
 -- Once per game run (a required update every map), never beside the problem notice:
 -- problems the download fixes already say so.
 local _,_,binary=messages()
 if not update.required and (remindedThisRun() or binary or M.installationNoticeShown) then return end
 M.updateNoticeShown=true
 if not update.required then saveUpdateState({reminded=os.time()}) end
 M.OpenNativeUpdate()
end
-- For a feature whose native function this binary lacks (other features call this, with
-- an already localized name): a small dialog per feature. Its button opens the update
-- window, or the installation window while no newer release is known.
local needed={}
function M.ShowNativeUpdateNeeded(feature,release)
 local key=tostring(feature)
 if IsValid(needed[key]) then needed[key]:MakePopup() return needed[key] end
 needed[key]=Derma_Query(L('install.update.needed',{feature=key,release=tostring(release or L'install.unknown_version')}),L'install.update.needed_title',L'install.update.button.show',function() M.OpenNativeUpdate() end,L'common.close')
 return needed[key]
end
hook.Add('MMDHL.InstallationChanged','MMDHL.InstallationNotice',notify)
hook.Add('InitPostEntity','MMDHL.InstallationNotice',function() timer.Simple(1,notify) end)
hook.Add('InitPostEntity','MMDHL.NativeUpdateNotice',function() timer.Simple(5,notifyUpdate) end)
