-- File access for other addons (docs/FILE_ACCESS.md). Another client addon asks to read
-- a file or folder on the player's computer; the native module decides. The player
-- answers in Windows dialogs that Lua cannot click, remembered folders live outside the
-- game folders, and only single player and the listen host can ask (on another server
-- every client script comes from that server). This file only forwards requests and
-- calls each callback exactly once, from a Think poll, never during the call.
if SERVER then return end
local L=mmdhl.L
local native=mmdhl.native or {}
local FA=mmdhl.FileAccess or {}
mmdhl.FileAccess=FA
FA.Version=1
local unpack=unpack or table.unpack
local functions={'FileAccessInfo','FileAccessPick','FileAccessRequest','FileAccessPoll','FileAccessRead','FileAccessPollRead','FileAccessList','FileAccessPollList','FileAccessRelease','FileAccessCancel','FileAccessGrants','FileAccessRevoke','FileAccessSetEnabled'}
local function supported() for _,name in ipairs(functions) do if not isfunction(native[name]) then return false end end return true end
local function decode(value,err,code) if not isstring(value) then return nil,err,code end return util.JSONToTable(value) end
local function language() return mmdhl.I18n and mmdhl.I18n.Language and mmdhl.I18n.Language() or 'en' end
local function localHost() local p=LocalPlayer() return game.SinglePlayer() or (IsValid(p) and p:IsListenServerHost()) end
-- Native refusal codes (file_access.hpp) and the phrases that explain them.
-- i18n-keys: file_access.error.denied file_access.error.auto_denied file_access.error.auto_denied_session file_access.error.busy file_access.error.not_found file_access.error.network file_access.error.remote_drive file_access.error.invalid_path file_access.error.denied_location file_access.error.link file_access.error.hidden file_access.error.too_large file_access.error.offset file_access.error.not_a_file file_access.error.not_a_folder file_access.error.released file_access.error.unreadable file_access.error.dialog_failed file_access.error.too_many_items file_access.error.denied_by_hook file_access.unavailable_disabled file_access.unavailable_remote file_access.unavailable_server_realm file_access.unavailable_worker file_access.needs_update
local messages={denied='denied',auto_denied='auto_denied',auto_denied_session='auto_denied_session',busy='busy',not_found='not_found',network='network',remote_drive='remote_drive',
 relative='invalid_path',parent='invalid_path',stream='invalid_path',device='invalid_path',invalid_path='invalid_path',denied_location='denied_location',link='link',outside='link',hidden='hidden',
 too_large='too_large',offset_too_large='offset',not_a_file='not_a_file',not_a_folder='not_a_folder',released='released',unknown_request='released',unreadable='unreadable',
 dialog_failed='dialog_failed',too_many_items='too_many_items',denied_by_hook='denied_by_hook'}
local unavailable={disabled='unavailable_disabled',unavailable_remote='unavailable_remote',no_server_realm='unavailable_server_realm',worker_missing='unavailable_worker',needs_update='needs_update'}
local function message(code,detail)
 -- Native cannot tell a remote server from single player whose server part failed to load.
 if code=='no_local_server' or code=='unavailable_remote' then code=localHost() and 'no_server_realm' or 'unavailable_remote' end
 if code=='invalid_options' then return L('file_access.error.invalid_options',{error=tostring(detail or '')}) end
 -- The installation check's own reason: a worker from another release, one that failed its self-test, the check still running.
 if code=='worker_unavailable' then return L('file_access.worker_unavailable',{reason=tostring(detail or '')}) end
 if messages[code] then return L('file_access.error.'..messages[code]) end
 if unavailable[code] then return L('file_access.'..unavailable[code]) end
 return L('file_access.error.other',{error=tostring(detail or code or '?')})
end
function FA.Message(code,detail) return message(code,detail) end
local function noteOldBinary()
 if FA.oldBinaryNoted then return end FA.oldBinaryNoted=true
 if mmdhl.ShowNativeUpdateNeeded then mmdhl.ShowNativeUpdateNeeded(L'file_access.feature','2.3.0')
 else notification.AddLegacy(L'file_access.needs_update',NOTIFY_HINT,8) end
end
-- The dialogs run in mmdhl_worker.exe: the installation check's verdict on it applies, as to imports.
local function workerReady() if not isfunction(mmdhl.FeatureAvailable) then return true end return mmdhl.FeatureAvailable('imports') end
-- ok, reason text, reason code. Old binaries and remote servers say why.
function FA.IsAvailable()
 if not supported() then return false,message('needs_update'),'needs_update' end
 local info,err,code=decode(native.FileAccessInfo())
 if not istable(info) then return false,message(code or 'unavailable_remote',err),code or 'unavailable_remote' end
 -- Asking needs the worker's windows, and so does turning file access on.
 if info.available or info.reason=='disabled' then
  local ready,why=workerReady() if not ready then return false,message('worker_unavailable',why),'worker_unavailable' end
 end
 if info.available then return true end
 local reason=info.reason=='no_local_server' and 'unavailable_remote' or tostring(info.reason or 'unavailable_remote')
 return false,message(info.reason,info.reason),reason
end
-- ---- Callbacks: queued, then run one by one from Think ----
local due,pending={},{}
local nextPoll=0
local poll
local function wake() hook.Add('Think','MMDHL.FileAccess',function() poll() end) end
local function deliver(cb,...) due[#due+1]={cb,{n=select('#',...),...}} wake() end
local function fail(cb,code,detail) deliver(cb,false,message(code,detail),code) end
local function run(entry)
 local ok,err=pcall(entry[1],unpack(entry[2],1,entry[2].n))
 if not ok then ErrorNoHalt('[Model Hotloader file access] an addon callback failed: '..tostring(err)..'\n') end
end
-- A notice while a Windows dialog waits: a full-screen game covers it.
local NoticeId='MMDHL.FileAccess'
local function notice(entry)
 local key=entry and (entry.enable and 'confirm' or entry.label) or nil
 if key==FA.noticeKey then return end
 if FA.noticeKey then notification.Kill(NoticeId) end
 FA.noticeKey=key if not key then return end
 local fullscreen=system.IsWindowed and not system.IsWindowed()
 local text=entry.enable and L'file_access.waiting_confirm' or fullscreen and L('file_access.waiting_fullscreen',{addon=entry.label}) or L('file_access.waiting_windowed',{addon=entry.label})
 notification.AddProgress(NoticeId,text)
 surface.PlaySound('garrysmod/content_downloaded.wav')
end
-- What addons hold: a table with the native handle, never a path.
local Item={} Item.__index=Item
function Item:Read(opts,cb) return FA.Read(self,opts,cb) end
function Item:List(opts,cb) return FA.List(self,opts,cb) end
function Item:Release() return FA.Release(self) end
FA.Item=Item
local function item(t) return setmetatable({handle=t.handle,name=t.name,size=t.size,folder=t.folder==true,remembered=t.remembered==true,displayPath=t.displayPath},Item) end
local function handleOf(value) if istable(value) then value=value.handle end return isstring(value) and value or nil end
-- The player's choice applies, but native could not save it: it lasts until the map changes.
local function notSaved() notification.AddLegacy(L'file_access.not_saved',NOTIFY_ERROR,8) end
-- Queued behind the callbacks, never run inside the poll: a listener that fails or asks
-- again cannot lose an answer or add to `pending` while the poll walks it.
local function changed() due[#due+1]={function() hook.Run('MMDHL.FileAccessChanged') end,{n=0}} wake() end
local function finishRequest(p,status)
 if status.notSaved then notSaved() end
 if status.state~='granted' then fail(p.cb,status.code or 'denied',status.error)
 elseif p.enable then deliver(p.cb,true)
 else
  local items={} for _,t in ipairs(status.items or {}) do items[#items+1]=item(t) end
  deliver(p.cb,true,items,status.refused or {})
 end
 if status.changed then changed() end
end
local function call(f,...) local ok,a,b,c=pcall(f,...) if not ok then return nil,tostring(a),'dialog_failed' end return a,b,c end
local function check()
 if RealTime()<nextPoll then return end
 nextPoll=RealTime()+.1
 local dialog
 for id,p in pairs(pending) do
  if p.kind=='request' then
   local status,err,code=decode(call(native.FileAccessPoll,id))
   if not istable(status) then pending[id]=nil fail(p.cb,code or 'dialog_failed',err)
   elseif status.state=='pending' then if status.dialog then dialog=p end
   else pending[id]=nil finishRequest(p,status) end
  else
   local result,info,code=call(p.kind=='read' and native.FileAccessPollRead or native.FileAccessPollList,id)
   if result==nil then pending[id]=nil fail(p.cb,code or 'unreadable',info)
   elseif result~=false then
    pending[id]=nil
    if p.kind=='read' then deliver(p.cb,true,result,decode(info) or {})
    else local t=decode(result) or {} deliver(p.cb,true,t.entries or {},{truncated=t.truncated==true}) end
   end
  end
 end
 notice(dialog)
end
-- Answers found in this poll run in the same Think, after it.
poll=function()
 if next(pending)~=nil then check() end
 local list=due due={}
 for _,entry in ipairs(list) do run(entry) end
 if next(pending)==nil and #due==0 then hook.Remove('Think','MMDHL.FileAccess') notice(nil) end
end
-- ---- Requests ----
-- Text the native dialogs quote: whole UTF-8 characters, no control characters.
local function clip(text,limit)
 if not isstring(text) and not isnumber(text) then return '' end
 text=tostring(text):gsub('%c',' '):gsub('%s+',' '):match('^%s*(.-)%s*$')
 local count,cut=0,#text
 for position in text:gmatch('()[^\128-\191]') do count=count+1 if count>limit then cut=position-1 break end end
 return text:sub(1,cut)
end
FA.Clip=clip
-- The requesting script as Lua reports it, shown in the dialog marked as unverified.
local function caller()
 for level=3,16 do
  local info=debug.getinfo(level,'S') if not info then break end
  local source=tostring(info.short_src or '')
  if source~='[C]' and not source:find('mmdhl/file_access.lua',1,true) and not source:find('hook.lua',1,true) then return clip(source,160) end
 end
 return ''
end
local function needCallback(cb) if not isfunction(cb) then error('Model Hotloader file access: pass a callback function',3) end end
-- Checks shared by Pick and RequestPath; nil when the callback already has its answer.
local function prepare(kind,opts,cb,path)
 needCallback(cb)
 if not istable(opts) then opts={} end
 local label=clip(opts.addon,64)
 if label=='' then fail(cb,'invalid_options','name your addon in opts.addon') return end
 if not supported() then noteOldBinary() fail(cb,'needs_update') return end
 local request={kind=kind,addon=label,script=caller(),path=path,folder=opts.folder==true,multiple=opts.multiple==true,purpose=clip(opts.purpose,120),title=clip(opts.title,80)}
 -- Deny-only: another addon (a server's rules, a privacy tool) may refuse first; it can never grant.
 local copy={} for k,v in pairs(request) do copy[k]=v end
 if hook.Run('MMDHLCanAccessUserFile',label,copy)==false then fail(cb,'denied_by_hook') return end
 return request
end
local function submit(id,err,code,cb,entry)
 if not id then fail(cb,code or 'dialog_failed',err) return false end
 entry.kind='request' entry.cb=cb pending[id]=entry wake() return true
end
-- opts: addon (required), title, purpose, filters {{'JSON files','*.json'}}, multiple, folder.
-- cb(true, items, refused) or cb(false, message, code). Returns whether the request was sent.
function FA.Pick(opts,cb)
 local request=prepare('pick',opts,cb) if not request then return false end
 local payload={requester=request.addon,script=request.script,title=request.title,purpose=request.purpose,multiple=request.multiple,folder=request.folder,language=language()}
 local filters={}
 for _,f in ipairs(istable(opts.filters) and opts.filters or {}) do if istable(f) and isstring(f[1]) and isstring(f[2]) then filters[#filters+1]={clip(f[1],40),f[2]} end end
 if #filters>0 then payload.filters=filters end
 local id,err,code=call(native.FileAccessPick,util.TableToJSON(payload))
 return submit(id,err,code,cb,{label=request.addon})
end
-- An exact absolute path (C:\...). The player still answers a dialog unless they chose
-- "always" for its folder before; opts: addon (required), folder, purpose.
function FA.RequestPath(path,opts,cb)
 if isfunction(opts) and cb==nil then opts,cb={},opts end
 if not isstring(path) then needCallback(cb) fail(cb,'invalid_path') return false end
 local request=prepare('path',opts,cb,path) if not request then return false end
 local payload={requester=request.addon,script=request.script,path=path,folder=request.folder,purpose=request.purpose,language=language()}
 local id,err,code=call(native.FileAccessRequest,util.TableToJSON(payload))
 return submit(id,err,code,cb,{label=request.addon})
end
local function start(kind,value,opts,cb)
 if isfunction(opts) and cb==nil then opts,cb={},opts end
 needCallback(cb) if not istable(opts) then opts={} end
 if not supported() then fail(cb,'needs_update') return false end
 local handle=handleOf(value) if not handle then fail(cb,'released') return false end
 local payload={relative=isstring(opts.relative) and opts.relative~='' and opts.relative or nil,hidden=opts.hidden==true}
 if kind=='read' then payload.mode=opts.mode=='text' and 'text' or 'binary' payload.offset=tonumber(opts.offset) payload.length=tonumber(opts.maxBytes or opts.length) end
 local id,err,code=call(kind=='read' and native.FileAccessRead or native.FileAccessList,handle,util.TableToJSON(payload))
 if not id then fail(cb,code or 'unreadable',err) return false end
 pending[id]={kind=kind,cb=cb} wake() return true
end
-- opts: mode 'binary' (default) or 'text' (whole file, decoded to UTF-8), offset, maxBytes
-- (default 1 MiB, at most 16 MiB), relative (a path inside a folder item), hidden.
-- cb(true, data, info {size, offset, read, eof, encoding}) or cb(false, message, code).
function FA.Read(value,opts,cb) return start('read',value,opts,cb) end
-- One level of a folder item. opts: relative, hidden. cb(true, entries {name, folder,
-- size, modified}, {truncated}) or cb(false, message, code). At most 4000 entries.
function FA.List(value,opts,cb) return start('list',value,opts,cb) end
function FA.Release(value) local handle=handleOf(value) if handle and supported() then native.FileAccessRelease(handle) end end
-- Soft dependency: hook.Run('MMDHL.RequestUserFile', opts, cb) returns true when Model
-- Hotloader took the request (opts.path asks for that path, otherwise the picker).
hook.Add('MMDHL.RequestUserFile','MMDHL.FileAccess',function(opts,cb)
 if not istable(opts) or not isfunction(cb) then return end
 if isstring(opts.path) then FA.RequestPath(opts.path,opts,cb) else FA.Pick(opts,cb) end
 return true
end)
timer.Simple(0,function() hook.Run('MMDHL.FileAccessReady',FA) end)
-- ---- Management window (Utilities -> Character Models) ----
function FA.Grants() if not supported() then return {enabled=false,grants={}} end local t=decode(native.FileAccessGrants()) return istable(t) and t or {enabled=false,grants={}} end
function FA.Revoke(id) if supported() and isstring(id) then if native.FileAccessRevoke(id)==false then notSaved() end hook.Run('MMDHL.FileAccessChanged') end end
-- Off at once. On: native asks the player to confirm; done(enabled) runs afterwards.
function FA.SetEnabled(enabled,done)
 done=isfunction(done) and done or function() end
 if not supported() then noteOldBinary() deliver(done,false) return end
 local result=decode(native.FileAccessSetEnabled(enabled==true,language()))
 if istable(result) and result.request then pending[result.request]={kind='request',enable=true,cb=function(ok) done(ok==true) end} wake() return end
 if istable(result) and result.notSaved then notSaved() end
 hook.Run('MMDHL.FileAccessChanged') deliver(done,istable(result) and result.enabled==true)
end
function FA.OpenManager()
 if IsValid(FA.manager) then FA.manager:MakePopup() return end
 local UI=mmdhl.UI local s,f=UI.metrics()
 -- The addon's light dialog style: its labels and colours are made for a light background.
 local frame=vgui.Create('DFrame') FA.manager=frame frame:SetTitle('')
 frame:SetSize(math.min(ScrW()-40,s(760)),math.min(ScrH()-40,s(560))) frame:Center() frame:MakePopup() frame:DockPadding(s(18),s(16),s(18),s(14))
 frame.btnMinim:SetVisible(false) frame.btnMaxim:SetVisible(false)
 frame.Paint=function(_,w,h) draw.RoundedBox(6,0,0,w,h,Color(246,248,251)) draw.RoundedBoxEx(6,0,0,w,s(6),UI.colors.accent,true,true,false,false) end
 local title=UI.label(frame,L'file_access.manage.title',f.Title,s(34)) title:Dock(TOP)
 local function wrapped(text,font,color) local l=UI.label(frame,text,font,s(20)) l:Dock(TOP) l:SetWrap(true) l:SetAutoStretchVertical(true) l:DockMargin(0,0,0,s(8)) if color then l:SetTextColor(color) end return l end
 wrapped(L'file_access.manage.help',f.Body)
 local ok,reason=FA.IsAvailable()
 if not supported() then noteOldBinary() end
 local status=wrapped(ok and '' or reason,f.Small,Color(170,90,20)) status:SetVisible(not ok)
 local enabled=UI.checkbox(frame,L'file_access.manage.enabled',nil,f.Body,s(28)) enabled:Dock(TOP) enabled:DockMargin(0,0,0,s(4)) enabled:SetTooltip(L'file_access.manage.enabled_tip')
 wrapped(L'file_access.manage.note',f.Small,UI.colors.muted)
 local bottom=frame:Add('DPanel') bottom:Dock(BOTTOM) bottom:SetTall(s(36)) bottom:SetPaintBackground(false) bottom:DockMargin(0,s(8),0,0)
 local list=frame:Add('DListView') list:Dock(FILL) list:SetMultiSelect(false) list:SetDataHeight(s(24)) list:SetHeaderHeight(s(24))
 list:AddColumn(L'file_access.manage.column_addon'):SetFixedWidth(s(200)) list:AddColumn(L'file_access.manage.column_folder') list:AddColumn(L'file_access.manage.column_used'):SetFixedWidth(s(140))
 local empty=UI.label(list,L'file_access.manage.empty',f.Small,s(60)) empty:SetWrap(true) empty:SetContentAlignment(5) empty:SetTextColor(UI.colors.muted)
 local layout=list.PerformLayout
 list.PerformLayout=function(self,w,h) if layout then layout(self,w,h) end empty:SetPos(s(12),s(30)) empty:SetSize(w-s(24),s(60)) end
 local close=UI.button(bottom,L'common.close',function() frame:Close() end,s(36),f.Body) close:Dock(RIGHT) close:SetWide(s(130))
 local revoke=UI.button(bottom,L'file_access.manage.revoke',nil,s(36),f.Body,'danger') revoke:Dock(LEFT) revoke:SetWide(s(150)) revoke:DockMargin(0,0,s(8),0)
 local revokeAll=UI.button(bottom,L'file_access.manage.revoke_all',nil,s(36),f.Body,'danger') revokeAll:Dock(LEFT) revokeAll:SetWide(s(170))
 local updating=false
 local function refresh()
  if not IsValid(frame) then return end
  local state=FA.Grants()
  -- The switch works while file access is available or merely turned off, and it always
  -- turns file access off (that needs no window).
  local okNow,why,code=FA.IsAvailable() status:SetText(okNow and '' or why) status:SetVisible(not okNow)
  updating=true enabled:SetChecked(state.enabled==true) updating=false enabled:SetEnabled(okNow==true or code=='disabled' or state.enabled==true)
  list:Clear()
  for _,g in ipairs(state.grants or {}) do
   local line=list:AddLine(tostring(g.requester or '?'),tostring(g.folder or '?'),(tonumber(g.used) or 0)>0 and os.date('%Y-%m-%d %H:%M',tonumber(g.used)) or '') line.grant=g.id
  end
  empty:SetVisible(#(state.grants or {})==0) revoke:SetEnabled(false) revokeAll:SetEnabled(#(state.grants or {})>0)
  frame:InvalidateLayout()
 end
 list.OnRowSelected=function() revoke:SetEnabled(true) end
 revoke.DoClick=function() local _,line=list:GetSelectedLine() if line and line.grant then FA.Revoke(line.grant) end end
 revokeAll.DoClick=function() Derma_Query(L'file_access.manage.revoke_all_confirm',L'file_access.manage.title',L'file_access.manage.revoke_all',function() FA.Revoke('all') end,L'common.cancel') end
 -- Turning on is confirmed natively; the box shows the native state, not the click.
 enabled.OnChange=function(_,value) if updating then return end FA.SetEnabled(value,function() refresh() end) timer.Simple(0,refresh) end
 hook.Add('MMDHL.FileAccessChanged',frame,function() refresh() end)
 -- The worker's self-test ends, or the player accepts or repairs the installation.
 hook.Add('MMDHL.InstallationChanged',frame,function() refresh() end)
 refresh()
end
concommand.Add('mmdhl_file_access',function() FA.OpenManager() end)
