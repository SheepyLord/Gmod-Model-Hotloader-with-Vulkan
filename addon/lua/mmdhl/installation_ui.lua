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
local function messages(detailed)
 local result,pending,binary={},0,false
 local status=M.GetInstallationStatus()
 local server=M.serverInstallation
 if (status and status.acceptedIssues) or (server and server.unverifiedAccepted) then result[#result+1]=L'install.unverified.in_use' end
 for _,v in ipairs(status and status.issues or {}) do
  if not v.accepted then pending=pending+1 end
  if not detailed and not v.accepted and downloadable[v.code] then binary=true else result[#result+1]=describe(v) end
 end
 if binary then table.insert(result,1,L'install.binary_problem') end
 for _,v in ipairs(server and server.issues or {}) do result[#result+1]=L('install.server_issue',{issue=describe(v)}) if not v.accepted then pending=pending+1 end end
 if acceptable(server) and not hostsServer() then result[#result+1]=L'install.unverified.server_admin' end
 return result,pending,binary
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
function M.InstallationSummary()
 local status=M.GetInstallationStatus()
 local issues=messages()
 local versions=L('install.native_release',{version=tostring(status and status.installed or L'install.not_verified')})..'   '..L('install.recommended_release',{version=tostring(status and status.recommended or L'install.unknown_version')})
 local renderer=rendererLine(status)
 if renderer then versions=versions..'   '..renderer end
 return table.concat(issues,'\n'),versions,table.concat(messages(true),'\n')
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
 local scroll=panel:Add('DScrollPanel') scroll:Dock(FILL) scroll:DockMargin(0,0,0,8)
 local summary=scroll:Add('DLabel') summary:Dock(TOP) summary:SetWrap(true) summary:SetAutoStretchVertical(true) summary:SetFont(bodyFont()) summary:SetDark(true)
 local function refresh()
  if not IsValid(panel) then return end
  local text,versions,details=M.InstallationSummary()
  local mirror=alternative()
  panel:SetVisible(always or text~='') panel:SetTall((always and 190 or 150)*scale)
  alt:SetVisible(mirror~=nil) useAnyway:SetVisible(M.CanAcceptUnverifiedNative()) stopUsing:SetVisible(M.UsingUnverifiedNative()) controls:InvalidateLayout()
  summary:SetText(versions..'\n'..(mirror and L('install.alternative_link',{url=mirror})..'\n' or '')..(text~='' and text or L'install.verified')) summary:SetTooltip(details~='' and details or nil)
  parent:InvalidateLayout(true)
 end
 hook.Add('MMDHL.InstallationChanged',panel,refresh) refresh()
 -- Windows stay open across a language switch (the spawn-menu copy is rebuilt with the menu).
 hook.Add('MMDHL.LanguageChanged',panel,function()
  label(get,L'install.button.download') label(alt,L'install.button.download_alternative') label(copy,L'install.button.copy_diagnostics') label(recheck,L'install.button.recheck') label(useAnyway,L'install.button.use_anyway') label(stopUsing,L'install.button.stop_using')
  summary:SetFont(bodyFont()) refresh()
 end)
 return panel
end
function M.OpenInstallation()
 local frame=vgui.Create('DFrame') frame:SetTitle(L'install.window_title') frame:SetSize(math.min(850*scale,ScrW()-40),math.min(410*scale,ScrH()-40)) frame:Center() frame:MakePopup()
 M.AddInstallationBanner(frame,true)
 local help=frame:Add('DLabel') help:SetFont(bodyFont()) help:Dock(FILL) help:DockMargin(12,8,12,8) help:SetWrap(true)
 help:SetText(L'install.help')
 hook.Add('MMDHL.LanguageChanged',frame,function() frame:SetTitle(L'install.window_title') help:SetFont(bodyFont()) help:SetText(L'install.help') end)
end
concommand.Add('mmdhl_open',function() M.OpenInstallation() end)
concommand.Add('mmdhl_open_props',function() M.OpenInstallation() end)
local function notify()
 -- Warnings the player already accepted stay in the banner but do not raise the notice.
 local _,pending,binary=messages()
 if M.installationNoticeShown or pending==0 or not IsValid(LocalPlayer()) then return end
 M.installationNoticeShown=true
 local panel=vgui.Create('DPanel') panel:SetSize(math.min(540,ScrW()-40),92) panel:SetPos(ScrW()-panel:GetWide()-20,40)
 panel:SetMouseInputEnabled(true)
 local close=panel:Add('DButton') close:Dock(RIGHT) close:SetWide(28) close:SetText('×') close.DoClick=function() panel:Remove() end
 local details=panel:Add('DButton') details:Dock(FILL) details:SetWrap(true) details:SetFont(bodyFont()) details:SetText(binary and L'install.notice_binary' or L'install.notice') details.DoClick=function() panel:Remove() M.OpenInstallation() end
 timer.Simple(20,function() if IsValid(panel) then panel:Remove() end end)
end
hook.Add('MMDHL.InstallationChanged','MMDHL.InstallationNotice',notify)
hook.Add('InitPostEntity','MMDHL.InstallationNotice',function() timer.Simple(1,notify) end)
