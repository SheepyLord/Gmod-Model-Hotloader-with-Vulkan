-- This file and its status transport must work without any native module.
mmdhl=mmdhl or {}
local M=mmdhl
local L=mmdhl.L
local function decode(value,err)
 if value==nil then return nil,err end
 if type(value)=='string' then return util.JSONToTable(value) end
 return value
end
M.Decode=decode
-- Unverified issues (hash or release mismatches) can be accepted by the player:
-- they stay listed, marked accepted, but no longer disable features. Missing or
-- unreadable files and incompatible APIs are never overridable.
local function issue(status,code,component,message,feature,unverified)
 local accepted=unverified and status.unverifiedAccepted or nil
 status.issues[#status.issues+1]={code=code,component=component,message=message,feature=feature or 'core',unverified=unverified or nil,accepted=accepted}
 if accepted then status.acceptedIssues=true return end
 -- blocked: a core problem that accepting unverified files would not fix.
 if unverified then status.unverified=true elseif (feature or 'core')=='core' then status.blocked=true end
 if not feature or feature=='core' then for key in pairs(status.features) do status.features[key]=false end else status.features[feature]=false end
end
local function featureName(key)
 local names={core=L'install.feature.core',imports=L'install.feature.imports',detailedCollision=L'install.feature.detailed_collision',rendering=L'install.feature.rendering',physics=L'install.feature.physics'}
 return names[key] or tostring(key)
end
-- A problem of this server's installation in a reply to a player, worded like
-- the banner's server lines.
function M.ServerIssue(feature,message)
 return L('install.server_issue',{issue=L('install.issue',{feature=featureName(feature),message=message})})
end
local function digitsBefore(a,b)
 local left,right={},{}
 for n in tostring(a):gmatch('%d+') do left[#left+1]=tonumber(n) end
 for n in tostring(b):gmatch('%d+') do right[#right+1]=tonumber(n) end
 for i=1,math.max(#left,#right) do local x,y=left[i] or 0,right[i] or 0 if x~=y then return x<y,true end end
 return false,false
end
-- Release labels: the numbers first; a pre-release (2.1.0-native.12) comes before its
-- release (2.1.0); +metadata does not count. Other labels compare their digit runs.
local function olderRelease(a,b)
 local x,y=tostring(a):gsub('%+.*$',''),tostring(b):gsub('%+.*$','')
 local cx,cy=x:match('^%d+%.%d+%.%d+'),y:match('^%d+%.%d+%.%d+')
 if not cx or not cy then return (digitsBefore(x,y)) end
 local before,differ=digitsBefore(cx,cy) if differ then return before end
 local px,py=x:sub(#cx+1),y:sub(#cy+1)
 if (px=='')~=(py=='') then return px~='' end
 return (digitsBefore(px,py))
end
-- Releases ({release,build}) in build order: the UTC time ending the build ID
-- (<commit>-YYYYMMDDTHHMMSSZ) when both have one, else their labels.
local function buildTime(build) return type(build)=='string' and build:match('%-(%d%d%d%d%d%d%d%dT%d%d%d%d%d%dZ)$') or nil end
local function olderBuild(a,b)
 local x,y=buildTime(a.build),buildTime(b.build)
 if x and y and x~=y then return x<y end
 return olderRelease(a.release,b.release)
end
local function approved(policy,id)
 for _,v in ipairs(policy.approved or {}) do if v==id then return true end end
 return false
end
-- Pure policy evaluator: reader returns {size,sha256,path}, or nil and an error.
-- env.accepted is the fingerprint of an unverified file set the player accepted.
function M.EvaluateInstallation(policy,reader,env)
 local s={schema=1,realm=env.server and 'server' or 'client',issues={},files={},features={core=true,imports=not env.server,detailedCollision=not env.server,rendering=not env.server,physics=env.server},recommended=policy.recommended}
 local releases=policy.releases or {}
 local recommended=releases[policy.recommended]
 s.download=recommended and recommended.url
 -- A mirror for players who cannot reach GitHub (set by update-native-policy.ps1 -AltReleaseUrl).
 s.downloadAlt=recommended and recommended.altUrl
 if not env.windows or env.arch~='x64' then issue(s,'unsupported_platform','platform',L'install.error.unsupported_platform') return s end
 if policy.schema~=1 or not recommended then issue(s,'policy_invalid','addon',L'install.error.policy_invalid') return s end
 local role=env.server and 'server' or 'client'
 local own=reader('lua/bin/'..recommended.files[role].name,'MOD',recommended.files[role].size)
 local selected
 if own then
  for id,release in pairs(releases) do
   if release.files[role].sha256==own.sha256 and release.files[role].size==own.size then selected=release s.installed=id break end
  end
 end
 local known=selected
 selected=selected or recommended
 s.expected=selected
 -- The runtime the game loads. Clients load it from bin/win64. srcds_win64.exe
 -- looks beside itself first, then in bin/win64, where the native package puts it.
 local runtime=selected.files.runtime
 local runtimePath='bin/win64/'..runtime.name
 if env.dedicated then
  local function present(path) local found,err=reader(path,'BASE_PATH',runtime.size) return found~=nil or err~='missing' end
  if present(runtime.name) or not present(runtimePath) then runtimePath=runtime.name end
 end
 local checks={{role,'lua/bin/'..selected.files[role].name,'MOD','core'}, {'runtime',runtimePath,'BASE_PATH','core'}}
 if not env.server then
  checks[#checks+1]={'worker','lua/bin/'..selected.files.worker.name,'MOD','imports'}
  checks[#checks+1]={'workerRuntime','lua/bin/'..selected.files.runtime.name,'MOD','imports'}
  checks[#checks+1]={'coacd','lua/bin/'..selected.files.coacd.name,'MOD','detailedCollision'}
 end
 -- Acceptance covers exactly these bytes: any changed file asks again.
 local parts={}
 for _,check in ipairs(checks) do
  local actual=reader(check[2],check[3],selected.files[check[1]=='workerRuntime' and 'runtime' or check[1]].size)
  parts[#parts+1]=check[1]..'='..(actual and actual.size..':'..actual.sha256 or 'missing')
 end
 s.fingerprint=s.realm..';'..table.concat(parts,';')
 -- Loading anyway extends the accepted fingerprint with the loaded files (CheckInstallation).
 local accepted=type(env.accepted)=='string' and env.accepted or ''
 s.unverifiedAccepted=(accepted==s.fingerprint or accepted:sub(1,#s.fingerprint+8)==s.fingerprint..';loaded:') or nil
 -- An older known release runs and gets the update reminder (s.update, never an issue):
 -- one no longer approved adds a warning that disables nothing. Releases without
 -- installation verification (installApi 0) cannot be checked after loading: they need
 -- the update. A newer release this addon does not know stays unverified.
 if known then
  local older=s.installed~=policy.recommended and olderBuild({release=s.installed,build=known.build},{release=policy.recommended,build=recommended.build})
  local ok=approved(policy,s.installed)
  if known.installApi~=1 then issue(s,'outdated','module',L('install.error.release_not_approved',{installed=s.installed,recommended=policy.recommended}))
  elseif not ok and not older then issue(s,'unapproved_release','module',L('install.error.release_not_approved',{installed=s.installed,recommended=policy.recommended}),nil,true)
  elseif not ok then s.issues[#s.issues+1]={code='outdated_release',component='module',feature='core',warning=true,message=L('install.warning.outdated_release',{installed=s.installed,recommended=policy.recommended})} end
  if older or known.installApi~=1 then
   -- policy.revoked: {label = i18n key} for releases with a known problem; still run, with that advisory.
   local advisory=type(policy.revoked)=='table' and policy.revoked[s.installed]
   s.update={installed=s.installed,recommended=policy.recommended,url=recommended.url,altUrl=recommended.altUrl,approved=ok or nil,advisory=type(advisory)=='string' and advisory or nil,required=known.installApi~=1 or nil}
  end
 end
 for _,check in ipairs(checks) do
  local key,path,search,feature=unpack(check)
  local expected=selected.files[key=='workerRuntime' and 'runtime' or key]
  local actual,err=reader(path,search,expected.size)
  s.files[key]={relative=path,search=search,expected=expected,actual=actual,error=err}
  if not actual then issue(s,err=='missing' and 'missing' or 'unreadable',key,err=='missing' and L('install.error.file_missing',{path=path}) or L('install.error.file_unreadable',{path=path}),feature)
  elseif actual.size~=expected.size or actual.sha256~=expected.sha256 then
   local other
   for id,release in pairs(releases) do local f=release.files[key=='workerRuntime' and 'runtime' or key] if f and f.sha256==actual.sha256 and f.size==actual.size then other=id break end end
   issue(s,other and 'mixed_installation' or 'damaged_or_unrecognized',key,other and L('install.error.file_mixed',{path=path,release=other,required=selected.release}) or L('install.error.file_unrecognized',{path=path}),feature,true)
  end
 end
 -- The renderer (bin/win64/d3d9.dll) is reported, never required: the Vulkan
 -- package ships the patched DXVK there, the no-Vulkan package leaves Source's
 -- Direct3D 9, and RTX Remix or other tools may own the file.
 if not env.server then
  local actual,err=reader('bin/win64/d3d9.dll','BASE_PATH',selected.renderer and selected.renderer.size)
  local function same(record) return type(record)=='table' and record.size==actual.size and record.sha256==actual.sha256 end
  if not actual then s.renderer={kind=err=='missing' and 'd3d9' or 'other',error=err~='missing' and err or nil}
  elseif same(selected.renderer) then s.renderer={kind='dxvk',release=selected.release,current=true}
  else
   s.renderer={kind='other'}
   for id,release in pairs(releases) do if same(release.renderer) then s.renderer={kind='dxvk',release=id} break end end
  end
 end
 if not s.installed and s.features.core and not s.acceptedIssues then s.installed=selected.release end
 if not s.features.core then s.features.imports=false s.features.detailedCollision=false s.features.rendering=false s.features.physics=false end
 return s
end

local policy=include('mmdhl/native_policy.lua') or {}
local status
local rawNative
local loadedIdentity
local loggedIssues={}
local lastFingerprint
local function readerForSession()
 local cache={}
 return function(path,search)
  local key=search..'/'..path
  if cache[key] then return cache[key][1],cache[key][2] end
  local value,err
  if not file.Exists(path,search) then err='missing'
  else
   local f=file.Open(path,'rb',search)
   if not f then err='unreadable'
   else
    local size=f:Size()
    -- Refuse unexpectedly enormous files rather than allocate unbounded memory.
    if size<0 or size>128*1024*1024 then err='invalid_size'
    else local bytes=f:Read(size) if not bytes or #bytes~=size then err='short_read' else value={size=size,sha256=util.SHA256(bytes),path=search..'/'..path} end end
    f:Close()
   end
  end
  cache[key]={value,err} return value,err
 end
end
function M.GetInstallationStatus() return status end
-- Accepted unverified file sets, one per realm. A listen server's client and
-- server share this data folder; a dedicated server has its own.
local acceptancePath='mmd_hotloader/unverified_native.json'
local function acceptedFingerprints() return util.JSONToTable(file.Read(acceptancePath,'DATA') or '') or {} end
function M.SetUnverifiedAccepted(realm,fingerprint)
 local all=acceptedFingerprints() all[realm]=fingerprint
 file.CreateDir('mmd_hotloader') file.Write(acceptancePath,util.TableToJSON(all))
end
function M.FeatureAvailable(feature)
 if not status or not status.features.core then return false,(M.loadError or L'install.unavailable_repair') end
 if status.features[feature]==false then
  -- Accepted (Use anyway) and plain warnings disable nothing, so they are never the reason.
  for _,v in ipairs(status.issues) do if not v.accepted and not v.warning and (v.feature==feature or v.feature=='core') then return false,v.message end end
  return false,L('install.checking_feature',{feature=featureName(feature)})
 end
 return true
end
local function publicStatus()
 return {schema=1,realm='server',recommended=status.recommended,installed=status.installed,features=status.features,issues=status.issues,
  fingerprint=status.fingerprint,unverified=status.unverified,blocked=status.blocked,unverifiedAccepted=status.unverifiedAccepted,update=status.update}
end
-- Whether release a (build ID ab) comes before release b (bb) in the update reminder's
-- order; a build of the same label never does.
local function releaseBefore(a,ab,b,bb)
 if a:gsub('%+.*$','')==b:gsub('%+.*$','') then return false end
 return olderBuild({release=a,build=ab},{release=b,build=bb})
end
-- Whether this realm's native module is release label or newer, in the update reminder's
-- order: the release its files match, else (accepted unverified files) the loaded module's own.
function M.NativeReleaseAtLeast(label)
 local releases=policy.releases or {}
 local loaded=status and status.loaded and status.loaded.module
 local record=status and status.installed and releases[status.installed]
 local current,build=record and status.installed or loaded and loaded.release,record and record.build or loaded and loaded.build
 if type(label)~='string' or type(current)~='string' then return false end
 return not releaseBefore(current,build,label,releases[label] and releases[label].build)
end
-- A loaded module this Lua cannot use: the update reminder offers the download, worded as
-- required, when it is older than the recommended release (nil: too old to say which).
-- installed is the module's own label (the files on disk may say otherwise). The same or
-- a newer release needs a restart or another addon version: its problem says so.
local function requireUpdate(installed,build)
 local recommended=(policy.releases or {})[policy.recommended] or {}
 if installed~=nil and not (type(installed)=='string' and type(policy.recommended)=='string' and releaseBefore(installed,build,policy.recommended,recommended.build)) then return end
 status.update={installed=installed,recommended=policy.recommended,url=recommended.url,altUrl=recommended.altUrl,required=true}
end
local updateLogged
local function notifyChanged()
 -- The problem that stops the addon: accepted files and warnings never do.
 local blocking
 for _,v in ipairs(status.issues) do if not v.accepted and not v.warning then blocking=v.message break end end
 M.loadError=not status.features.core and (blocking or L'install.unavailable') or nil
 local encoded=util.TableToJSON(publicStatus())
 if encoded~=lastFingerprint then
  lastFingerprint=encoded
  for _,v in ipairs(status.issues) do local key=v.code..'/'..v.component..'/'..v.message if not loggedIssues[key] then loggedIssues[key]=true MsgN('[Model Hotloader / '..status.realm..'] '..M.Localize(v.message)..(v.accepted and ' '..M.Localize(L'install.accepted_tag') or '')) end end
  -- An approved older release has no issue to print: one line tells the administrator.
  if SERVER and status.update and status.update.approved and not updateLogged then
   updateLogged=true MsgN('[Model Hotloader / server] '..M.Localize(L('install.update.server',{installed=tostring(status.update.installed),recommended=tostring(status.update.recommended)})))
  end
  hook.Run('MMDHL.InstallationChanged',status)
  if SERVER then net.Start('mmdhl_install_status') net.WriteString(encoded) net.Broadcast() end
 end
end
if SERVER then
 util.AddNetworkString('mmdhl_install_status')
 -- Pool operational channels before native loading: healthy clients still run
 -- their initialization hooks when this server's native installation fails.
 -- Clients do not check the server's status first, so a spawn request gets this
 -- server's problem as its answer instead of waiting for the client's timeout.
 local spawnReplies={mmdhl_action='mmdhl_spawn_status',mmdhl_prop_action='mmdhl_prop_status'}
 -- The physics editor's reply channel: physics_editor.lua, which pools it too, does not load
 -- when this check fails, and net.Start refuses a name that was never pooled.
 util.AddNetworkString('mmdhl_physics_status')
 for _,name in ipairs({'mmdhl_action','mmdhl_actor_registration','mmdhl_arms_preview','mmdhl_catalog','mmdhl_collision_mesh','mmdhl_forget_assets','mmdhl_material_visibility','mmdhl_native_morph','mmdhl_native_morphs','mmdhl_notice','mmdhl_physics','mmdhl_physics_reset','mmdhl_player_clear','mmdhl_player_selection','mmdhl_prop_action','mmdhl_prop_attach','mmdhl_prop_attach_open','mmdhl_prop_catalog','mmdhl_prop_collision','mmdhl_prop_forget','mmdhl_prop_status','mmdhl_scene','mmdhl_scene_active','mmdhl_share','mmdhl_spawn_status'}) do
  util.AddNetworkString(name)
  net.Receive(name,function(_,p)
   if not status or status.features.core then return end
   if spawnReplies[name] and net.ReadString()=='spawn' then
    net.ReadString() net.ReadUInt(16)
    local settings=util.JSONToTable(net.ReadString()) or {}
    net.Start(spawnReplies[name]) net.WriteUInt(math.Clamp(math.floor(tonumber(settings.request) or 0),0,4294967295),32) net.WriteString('error')
    net.WriteString(M.ServerIssue('core',M.loadError or L'install.unavailable')) net.WriteUInt(0,16) net.Send(p)
   end
   -- The physics editor's request (protocol 1) waits for its own answer (pooled below).
   if name=='mmdhl_physics' and net.ReadUInt(8)==1 then
    local request=net.ReadUInt(32)
    if net.ReadString()~='close' then net.Start('mmdhl_physics_status') net.WriteUInt(request,32) net.WriteString('error') net.WriteString(L'physics_editor.error.server_core') net.WriteUInt(0,16) net.WriteUInt(0,16) net.Send(p) end
   end
   if (p.MMDHLNextFailure or 0)>CurTime() then return end p.MMDHLNextFailure=CurTime()+5
   net.Start('mmdhl_install_status') net.WriteString(util.TableToJSON(publicStatus())) net.Send(p)
  end)
 end
 -- Dedicated servers accept or revoke unverified native files from the server console.
 local function reply(p,message)
  MsgN('[Model Hotloader / server] '..M.Localize(message)) if IsValid(p) then p:ChatPrint(M.Localize(message)) end
 end
 concommand.Add('mmdhl_accept_unverified_native',function(p)
  if IsValid(p) and not p:IsSuperAdmin() then return end
  if not status or not status.unverified or status.blocked or not status.fingerprint then reply(p,L'install.unverified.nothing') return end
  M.SetUnverifiedAccepted('server',status.fingerprint) reply(p,L'install.unverified.accepted')
 end)
 concommand.Add('mmdhl_revoke_unverified_native',function(p)
  if IsValid(p) and not p:IsSuperAdmin() then return end
  M.SetUnverifiedAccepted('server',nil) reply(p,L'install.unverified.revoked')
 end)
 net.Receive('mmdhl_install_status',function(_,p)
  if not status or (p.MMDHLNextInstallStatus or 0)>CurTime() then return end p.MMDHLNextInstallStatus=CurTime()+5
  net.Start('mmdhl_install_status') net.WriteString(util.TableToJSON(publicStatus())) net.Send(p)
 end)
else
 net.Receive('mmdhl_install_status',function()
  local value=util.JSONToTable(net.ReadString())
  if not istable(value) or value.schema~=1 or value.realm~='server' or not istable(value.features) then return end
  M.serverInstallation=value hook.Run('MMDHL.InstallationChanged',status)
 end)
 hook.Add('InitPostEntity','MMDHL.InstallationServerStatus',function()
  timer.Create('MMDHL.InstallationServerStatus',1,5,function()
   if M.serverInstallation then timer.Remove('MMDHL.InstallationServerStatus') return end
   net.Start('mmdhl_install_status') net.SendToServer()
  end)
 end)
end
local function normalize(path) return tostring(path or ''):gsub('\\','/'):gsub('/+','/'):lower() end
-- Where a loaded file may be. The x86-64 branch starts bin/win64/gmod.exe; since
-- 2026-09-17 the main branch starts gmod_win64.exe in the game folder and still
-- loads the engine, and this runtime, from bin/win64. Native builds up to
-- 2.1.0-native.5 expect the runtime beside the executable, so the runtime may also
-- be the file this check read, under the game folder of the loaded module.
local function expectedPaths(key,info)
 local paths={normalize(info[key].expectedPath)}
 local root=key=='runtime' and info.module and normalize(info.module.expectedPath):match('^(.+)/garrysmod/lua/bin/[^/]+$')
 if root and status.files.runtime then paths[2]=root..'/'..normalize(status.files.runtime.relative) end
 return paths
end
-- The loaded files that loading anyway accepts, appended to the installation fingerprint.
local function loadedFingerprint(info)
 local parts={}
 for _,key in ipairs({'module','runtime'}) do local found=info and info[key] or {} parts[#parts+1]=key..'='..tostring(found.size)..':'..tostring(found.sha256) end
 return ';loaded:'..table.concat(parts,',')
end
-- On a mismatch, also returns whether the player may load anyway: another
-- interface never loads; other bytes or another location may, once accepted.
local function identitiesMatch(info)
 for _,key in ipairs({'module','runtime'}) do
  local record=status.files[key=='module' and status.realm or 'runtime']
  local found=info and info[key]
  local message=key=='module' and L'install.error.loaded_module_mismatch' or L'install.error.loaded_runtime_mismatch'
  if not (found and found.installApi==1 and found.api==status.expected.api and found.platform=='win64') then return false,message,false end
  local valid=false
  for _,path in ipairs(expectedPaths(key,info)) do if normalize(found.path)==path then valid=true end end
  -- Accepted unverified files are identified by their bytes on disk, not by a release.
  if valid and status.unverifiedAccepted then valid=record.actual~=nil and found.sha256==record.actual.sha256
  elseif valid then valid=found.release==status.expected.release and found.build==status.expected.build and found.sha256==record.expected.sha256 end
  if not valid then return false,message,true end
 end
 return true
end
-- A loaded file that does not match stops the addon until the player accepts
-- exactly these loaded files (Use anyway); true when loading may continue.
local function checkLoaded(info)
 local ok,message,overridable=identitiesMatch(info)
 if ok then return true end
 if not overridable then
  issue(status,'restart_required','module',message)
  local module=type(info)=='table' and type(info.module)=='table' and info.module
  if module and type(module.release)=='string' then requireUpdate(module.release,module.build) end
  return false
 end
 status.fingerprint=status.fingerprint..loadedFingerprint(info)
 status.unverifiedAccepted=acceptedFingerprints()[status.realm]==status.fingerprint or nil
 issue(status,'loaded_mismatch','module',message,nil,true)
 return status.features.core
end
local function removeIssues(feature)
 for i=#status.issues,1,-1 do if status.issues[i].feature==feature then table.remove(status.issues,i) end end
end
function M.RefreshGameCompatibility()
 if not rawNative or not status.features.core then return end
 local ok,value,err=pcall(rawNative.CheckCompatibility)
 local report=ok and decode(value,err)
 local feature=SERVER and 'physics' or 'rendering'
 removeIssues(feature)
 status.features[feature]=report and report.ready==true or false
 status.compatibility=report
 if not report then issue(status,'game_check_failed','game',tostring(err or value),feature)
 else
  for _,v in ipairs(report.issues or {}) do issue(status,v.code,v.component,v.message,feature) end
  -- Game libraries change with Garry's Mod updates. A build no profile describes
  -- ("unverified") still runs; the player gets a warning that disables nothing and
  -- that Dismiss hides. From 2.1.0-native.7 the native module also follows the
  -- default branch's IAppSystem layout (four fewer methods, every later slot lower).
  local unverified={}
  for _,v in ipairs(report.libraries or {}) do if v.match=='unverified' then unverified[#unverified+1]=tostring(v.name) end end
  if #unverified>0 then status.issues[#status.issues+1]={code='game_unverified',component='game',message=L('install.warning.game_untested',{libraries=table.concat(unverified,', ')}),feature=feature,warning=true} end
 end
 notifyChanged()
 return report and report.pending
end
-- Every native call that starts mmdhl_worker.exe. Reload takes its options from the source registry.
-- File access shows its dialogs in the worker (turning it off shows none); its refusals
-- carry a code, as native's do, and it takes no import options. A request for a path in a
-- folder the player always allowed for that addon needs no dialog: without the worker,
-- native answers only those (noDialog) and refuses the rest as the guard would.
local workerCalls={Browse=true,BeginImport=true,Reload=true,PropDerive=true,PropReload=true,FileAccessPick=true,FileAccessRequest=true,FileAccessSetEnabled=true}
local fileAccessCalls={FileAccessPick=true,FileAccessRequest=true,FileAccessSetEnabled=true}
local function guardedNative(native)
 local proxy={}
 for name,value in pairs(native) do
  if workerCalls[name] then proxy[name]=function(...)
   if name=='FileAccessSetEnabled' and (...)~=true then return value(...) end
   local ok,err=M.FeatureAvailable('imports')
   if fileAccessCalls[name] then
    if ok then return value(...) end
    local request=name=='FileAccessRequest' and util.JSONToTable(tostring((...) or ''))
    if istable(request) then
     request.noDialog=true
     local id,why,code=value(util.TableToJSON(request))
     if id~=nil or code~='worker_unavailable' then return id,why,code end
    end
    return nil,err,'worker_unavailable'
   end
   if not ok then return nil,err end
   local args={...}
   if name~='Browse' and name~='Reload' and not status.features.detailedCollision then
    local options=util.JSONToTable(args[2] or '{}') or {} options.collision='hull' args[2]=util.TableToJSON(options)
   end
   return value(unpack(args))
  end
  else proxy[name]=value end
 end
 return proxy
end
local function startProbe()
 if not CLIENT or not status.features.imports then return end
 local coacd=status.features.detailedCollision
 status.features.imports=false status.probePending=true
 local started,err=rawNative.StartInstallationProbe(coacd)
 if not started then status.probePending=false issue(status,'worker_failed','worker',tostring(err),'imports') notifyChanged() return end
 -- The native probe gives up after about 11 s, but starting the process is not
 -- bounded (antivirus scans); an unanswered probe must not leave imports pending.
 local deadline=RealTime()+30
 timer.Create('MMDHL.InstallationWorker',.2,0,function()
  local result,why=decode(rawNative.PollInstallationProbe())
  if result and result.pending then
   if RealTime()<deadline then return end
   result=nil
  end
  timer.Remove('MMDHL.InstallationWorker') status.probePending=false
  if not result then issue(status,'worker_failed','worker',why or L'install.error.worker_failed','imports')
  else
   local mismatch
   -- Accepted unverified files have no approved release: the worker must match its own runtime bytes.
   if status.unverifiedAccepted then local runtime=status.files.workerRuntime and status.files.workerRuntime.actual mismatch=result.identity.build~=result.runtime.build or not runtime or result.runtime.sha256~=runtime.sha256
   else mismatch=result.identity.release~=status.expected.release or result.identity.build~=status.expected.build or result.runtime.build~=status.expected.build or result.runtime.sha256~=status.expected.files.runtime.sha256 end
   if mismatch then issue(status,'mixed_installation','worker',L'install.error.worker_mismatch','imports',true) end
   if not mismatch or status.unverifiedAccepted then status.features.imports=true
    if coacd and not result.coacd then issue(status,'dependency_failed','coacd',result.coacdError or L'install.error.coacd_failed','detailedCollision') end
   end
  end
  notifyChanged()
 end)
end
-- A compatibility policy newer than this binary (a library it does not know, or a profile
-- it cannot validate: a guard it requires missing, another evidence format) must not stop
-- the addon; guard names it does not know it ignores. The binary validates the whole
-- policy before taking it, once per process, so it is offered whole, then without one
-- library at a time, then without libraries: the libraries left out run as unverified
-- game builds behind the binary's own interface, slot and class checks (releases
-- before 2.1.0-native.6 turn the affected engine features off instead). The order is
-- fixed, so every later map finds the same policy. Also returns the libraries left out.
local function configureCompatibility(compat)
 local configured,err=decode(rawNative.ConfigureCompatibility(util.TableToJSON(compat)))
 if configured or not istable(compat) or not istable(compat.libraries) then return configured,err end
 local names,seen={},{}
 for _,v in ipairs(compat.libraries) do local name=istable(v) and tostring(v.name) if name and not seen[name] then seen[name]=true names[#names+1]=name end end
 local all=table.concat(names,', ') names[#names+1]=false
 for _,without in ipairs(names) do
  local kept,rest={},{}
  if without then for _,v in ipairs(compat.libraries) do if not istable(v) or tostring(v.name)~=without then kept[#kept+1]=v end end end
  for k,v in pairs(compat) do if k~='libraries' then rest[k]=v end end
  -- Written by hand: an empty Lua table is no JSON array.
  local text=util.TableToJSON(rest):sub(1,-2)..',"libraries":'..(#kept>0 and util.TableToJSON(kept) or '[]')..'}'
  if decode(rawNative.ConfigureCompatibility(text)) then return true,nil,without or all end
 end
 return nil,err
end
local compatibilityFallback
local function fallbackWarning()
 if compatibilityFallback then status.issues[#status.issues+1]={code='compatibility_fallback',component='compatibility',message=compatibilityFallback,feature='core',warning=true} end
end
function M.CheckInstallation(recheck)
 if recheck and status and status.probePending then return status end
 local previous=status
 status=M.EvaluateInstallation(policy,readerForSession(),{server=SERVER,windows=system.IsWindows(),arch=jit.arch,dedicated=SERVER and game.IsDedicated(),accepted=acceptedFingerprints()[SERVER and 'server' or 'client']})
 if recheck then
  status.loaded=loadedIdentity
  if not rawNative or not status.features.core then issue(status,'restart_required','module',L'install.error.restart_to_load')
  elseif checkLoaded(loadedIdentity) and previous then
   -- Runtime failures are carried over below; valid files alone do not mean a restart repairs them.
   local runtime={}
   for _,v in ipairs(previous.issues) do if v.code=='worker_failed' or v.code=='dependency_failed' or v.code=='game_incompatible' then runtime[v.feature]=true end end
   for key,allowed in pairs(previous.features) do
    if not allowed and status.features[key] and not runtime[key] then issue(status,'restart_required',key,L('install.error.restart_to_enable',{feature=featureName(key)}),key) end
    status.features[key]=status.features[key] and allowed
   end
   for _,v in ipairs(previous.issues) do if v.code=='worker_failed' or v.code=='dependency_failed' or v.code=='game_incompatible' then status.issues[#status.issues+1]=v end end
  end
  if status.features.core and rawNative then fallbackWarning() M.RefreshGameCompatibility() end
  notifyChanged() return status
 end
 if not status.features.core then notifyChanged() return false end
 local ok,err=pcall(require,'mmdhl')
 if not ok or not istable(mmdhl_native) then issue(status,'loader_failed','module',L('install.error.loader_failed',{reason=tostring(err)})) notifyChanged() return false end
 rawNative=mmdhl_native
 if not rawNative.GetInstallationInfo or not rawNative.ConfigureCompatibility or not rawNative.CheckCompatibility then
  local ok,capabilities=pcall(function() return decode(rawNative.GetCapabilities()) end)
  issue(status,'outdated','module',L('install.error.no_verification',{recommended=tostring(policy.recommended)})) requireUpdate(ok and istable(capabilities) and isstring(capabilities.version) and capabilities.version or nil) notifyChanged() return false
 end
 loadedIdentity=decode(rawNative.GetInstallationInfo())
 if not checkLoaded(loadedIdentity) then notifyChanged() return false end
 local compat=include('mmdhl/compatibility_policy.lua')
 local configured,configError,without=configureCompatibility(compat)
 if not configured then issue(status,'policy_invalid','compatibility',tostring(configError)) notifyChanged() return false end
 compatibilityFallback=without and L('install.warning.compatibility_fallback',{libraries=without,recommended=tostring(policy.recommended)}) fallbackWarning()
 status.loaded=loadedIdentity
 M.native=guardedNative(rawNative)
 M.RefreshGameCompatibility()
 startProbe()
 notifyChanged()
 return true
end
hook.Add('InitPostEntity','MMDHL.InstallationCompatibility',function()
 if M.RefreshGameCompatibility() then
  local attempts=0
  timer.Create('MMDHL.InstallationCompatibility',.5,20,function()
   attempts=attempts+1
   if not M.RefreshGameCompatibility() then timer.Remove('MMDHL.InstallationCompatibility')
   elseif attempts==20 then issue(status,'game_not_ready','game',L'install.error.game_not_ready',SERVER and 'physics' or 'rendering') notifyChanged() end
  end)
 end
end)
concommand.Add('mmdhl_check_installation',function(p)
 if SERVER and IsValid(p) and not p:IsAdmin() then return end
 M.CheckInstallation(true)
end)
