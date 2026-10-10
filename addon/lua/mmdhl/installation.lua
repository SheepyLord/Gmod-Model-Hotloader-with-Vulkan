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
-- Model Hotloader always tries its native files; it never refuses them. An issue without
-- warning is a problem: something that keeps a feature off (the module did not load, the
-- worker did not start, the game build failed its compatibility check). Every other issue
-- is a warning that disables nothing, above all files that match no release this addon
-- knows: a test build, a newer release, modified, damaged or mixed files.
local function issue(status,code,component,message,feature)
 local v={code=code,component=component,message=message,feature=feature or 'core'}
 status.issues[#status.issues+1]=v
 if v.feature=='core' then status.blocked=true for key in pairs(status.features) do status.features[key]=false end else status.features[v.feature]=false end
 return v
end
-- extra.identity: the files are not a release this addon knows or approves (one banner
-- line speaks for all of them). extra.detail: what tells this warning from an earlier
-- one, for Dismiss. extra.cause: it explains a failure, and becomes the problem when
-- loading (or, for its feature, the worker's self-test) fails.
local function warn(status,code,component,message,feature,extra)
 local v={code=code,component=component,message=message,feature=feature or 'core',warning=true}
 for k,x in pairs(extra or {}) do v[k]=x end
 if v.identity then status.unverified=true end
 status.issues[#status.issues+1]=v
 return v
end
local function promote(status,feature)
 for _,v in ipairs(status.issues) do if v.warning and v.cause and (feature==nil or v.feature==feature) then v.warning=nil end end
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
-- Where players unpack the package by mistake (issue #6), relative to the game folder: the
-- contents of its GarrysMod folder into garrysmod (or addons), or the GarrysMod folder itself
-- into the game folder, garrysmod or addons; also an unpacked release folder
-- (Model-Hotloader-<release>-<commit>-win64-<variant>) left where it was unpacked. find(pattern)
-- lists the folders matching pattern in the game folder.
local wrongRoots={'garrysmod/','garrysmod/GarrysMod/','GarrysMod/','garrysmod/addons/','garrysmod/addons/GarrysMod/','garrysmod/lua/bin/GarrysMod/'}
local function misplacedRoots(find)
 local roots={}
 for _,root in ipairs(wrongRoots) do roots[#roots+1]=root end
 if type(find)=='function' then
  for _,base in ipairs({'','garrysmod/','garrysmod/addons/'}) do
   for _,dir in ipairs(find(base..'Model-Hotloader-*') or {}) do roots[#roots+1]=base..dir..'/GarrysMod/' roots[#roots+1]=base..dir..'/' end
  end
 end
 return roots
end
-- File names when the policy cannot name them (it is missing or broken).
local defaultFiles={client={name='gmcl_mmdhl_win64.dll'},server={name='gmsv_mmdhl_win64.dll'},runtime={name='mmdhl_runtime_win64.dll'},worker={name='mmdhl_worker.exe'},coacd={name='lib_coacd.dll'}}
-- Pure policy evaluator: reader returns {size,sha256,path[,build]}, or nil and an error.
-- It never turns a feature off: it lists what CheckInstallation should know when it loads
-- the files, as warnings.
function M.EvaluateInstallation(policy,reader,env)
 local s={schema=1,realm=env.server and 'server' or 'client',issues={},files={},features={core=true,imports=not env.server,detailedCollision=not env.server,rendering=not env.server,physics=env.server},recommended=policy.recommended}
 local releases=type(policy.releases)=='table' and policy.releases or {}
 local recommended=releases[policy.recommended]
 s.download=recommended and recommended.url
 -- A mirror for players who cannot reach GitHub (set by update-native-policy.ps1 -AltReleaseUrl).
 s.downloadAlt=recommended and recommended.altUrl
 -- No binary exists for another platform: this says why nothing loads.
 if not env.windows or env.arch~='x64' then warn(s,'unsupported_platform','platform',L'install.error.unsupported_platform','core',{cause=true}) return s end
 -- Without the list of releases the files are only read, never compared.
 if policy.schema~=1 or not recommended then warn(s,'policy_invalid','addon',L'install.error.policy_invalid') recommended=nil end
 local role=env.server and 'server' or 'client'
 local names=recommended and recommended.files or defaultFiles
 local own=reader('lua/bin/'..names[role].name,'MOD',names[role].size)
 local selected
 if own and recommended then
  for id,release in pairs(releases) do
   if release.files[role].sha256==own.sha256 and release.files[role].size==own.size then selected=release s.installed=id break end
  end
 end
 local known=selected
 selected=selected or recommended
 s.expected=selected
 local files=selected and selected.files or defaultFiles
 -- The runtime the game loads. Clients load it from bin/win64. srcds_win64.exe
 -- looks beside itself first, then in bin/win64, where the native package puts it.
 local runtime=files.runtime
 local runtimePath='bin/win64/'..runtime.name
 if env.dedicated then
  local function present(path) local found,err=reader(path,'BASE_PATH',runtime.size) return found~=nil or err~='missing' end
  if present(runtime.name) or not present(runtimePath) then runtimePath=runtime.name end
 end
 local checks={{role,'lua/bin/'..files[role].name,'MOD','core'}, {'runtime',runtimePath,'BASE_PATH','core'}}
 if not env.server then
  checks[#checks+1]={'worker','lua/bin/'..files.worker.name,'MOD','imports'}
  checks[#checks+1]={'workerRuntime','lua/bin/'..files.runtime.name,'MOD','imports'}
  checks[#checks+1]={'coacd','lua/bin/'..files.coacd.name,'MOD','detailedCollision'}
 end
 -- The exact bytes checked, for diagnostics.
 local parts={}
 for _,check in ipairs(checks) do
  local actual=reader(check[2],check[3],files[check[1]=='workerRuntime' and 'runtime' or check[1]].size)
  parts[#parts+1]=check[1]..'='..(actual and actual.size..':'..actual.sha256 or 'missing')
 end
 s.fingerprint=s.realm..';'..table.concat(parts,';')
 -- An older known release runs and gets the update reminder (s.update, never an issue):
 -- one no longer approved adds a warning. So do a recorded release newer than the
 -- recommended one that is not approved, and a release without installation
 -- verification (installApi 0), which the reminder also offers to replace.
 if known then
  local older=s.installed~=policy.recommended and olderBuild({release=s.installed,build=known.build},{release=policy.recommended,build=recommended.build})
  local ok=approved(policy,s.installed)
  if known.installApi~=1 then warn(s,'outdated','module',L('install.error.no_verification',{recommended=policy.recommended}))
  elseif not ok and not older then warn(s,'unapproved_release','module',L('install.error.release_not_approved',{installed=s.installed,recommended=policy.recommended}),nil,{identity=true,detail=s.installed})
  elseif not ok then s.issues[#s.issues+1]={code='outdated_release',component='module',feature='core',warning=true,message=L('install.warning.outdated_release',{installed=s.installed,recommended=policy.recommended})} end
  if older or known.installApi~=1 then
   -- policy.revoked: {label = i18n key} for releases with a known problem; still run, with that advisory.
   local advisory=type(policy.revoked)=='table' and policy.revoked[s.installed]
   s.update={installed=s.installed,recommended=policy.recommended,url=recommended.url,altUrl=recommended.altUrl,approved=ok or nil,advisory=type(advisory)=='string' and advisory or nil,required=known.installApi~=1 or nil}
  end
 end
 for _,check in ipairs(checks) do
  local key,path,search,feature=unpack(check)
  local expected=files[key=='workerRuntime' and 'runtime' or key]
  local actual,err=reader(path,search,expected.size)
  s.files[key]={relative=path,search=search,expected=selected and expected or nil,actual=actual,error=err}
  if not actual then warn(s,err=='missing' and 'missing' or 'unreadable',key,err=='missing' and L('install.error.file_missing',{path=path}) or L('install.error.file_unreadable',{path=path}),feature,{cause=true})
  elseif selected and (actual.size~=expected.size or actual.sha256~=expected.sha256) then
   local other
   for id,release in pairs(releases) do local f=release.files[key=='workerRuntime' and 'runtime' or key] if f and f.sha256==actual.sha256 and f.size==actual.size then other=id break end end
   warn(s,other and 'mixed_installation' or 'damaged_or_unrecognized',key,other and L('install.error.file_mixed',{path=path,release=other,required=selected.release}) or L('install.error.file_unrecognized',{path=path}),feature,{identity=true,detail=actual.size..':'..actual.sha256})
  end
 end
 -- The package unpacked into the wrong folder: found there while the game folder lacks its
 -- files, that is why nothing loads, so it comes first (after loading fails it is the problem
 -- every player sees). A copy left there beside a working installation says nothing.
 if s.files[role].error=='missing' or s.files.runtime.error=='missing' then
  for _,root in ipairs(misplacedRoots(env.find)) do
   local found
   for _,path in ipairs({root..'garrysmod/lua/bin/'..files[role].name,root..'bin/win64/'..runtime.name}) do if not found and reader(path,'BASE_PATH') then found=path end end
   if found then
    table.insert(s.issues,1,{code='misplaced_package',component='installation',feature='core',warning=true,cause=true,detail=found,found=found,message=L('install.warning.misplaced',{found=(found:gsub('/','\\'))})})
    break
   end
  end
 end
 -- The module and the runtime it links share C++ types: from two builds they may fail to
 -- load or crash Garry's Mod, so CheckInstallation prints this before loading them. Each
 -- binary carries its build ID; files this addon does not know are fine while they agree.
 local module,linked=s.files[role].actual,s.files.runtime.actual
 if module and linked and type(module.build)=='string' and type(linked.build)=='string' and module.build~=linked.build then
  warn(s,'mixed_builds','runtime',L('install.warning.mixed_builds',{module=s.files[role].relative,build=module.build,runtime=s.files.runtime.relative,runtimeBuild=linked.build}),nil,{identity=true,cause=true,detail=module.build..'/'..linked.build})
 end
 -- The renderer (bin/win64/d3d9.dll) is reported, never required: the Vulkan
 -- package ships the patched DXVK there, the no-Vulkan package leaves Source's
 -- Direct3D 9, and RTX Remix or other tools may own the file.
 if not env.server then
  local actual,err=reader('bin/win64/d3d9.dll','BASE_PATH',selected and selected.renderer and selected.renderer.size)
  local function same(record) return type(record)=='table' and record.size==actual.size and record.sha256==actual.sha256 end
  if not actual then s.renderer={kind=err=='missing' and 'd3d9' or 'other',error=err~='missing' and err or nil}
  elseif selected and same(selected.renderer) then s.renderer={kind='dxvk',release=selected.release,current=true}
  else
   s.renderer={kind='other'}
   for id,release in pairs(releases) do if same(release.renderer) then s.renderer={kind='dxvk',release=id} break end end
  end
 end
 return s
end

local policy=include('mmdhl/native_policy.lua') or {}
local status
local rawNative
local loadedIdentity
local loggedIssues={}
local lastFingerprint
-- The build ID every Model Hotloader binary carries (<commit>-YYYYMMDDTHHMMSSZ), read
-- before loading it; nil for other files.
local function buildOf(path,bytes)
 if not path:find('mmdhl',1,true) or path:sub(-4)~='.dll' then return nil end
 return bytes:match('%x%x%x%x%x%x%x%x%x%x%x%x%-%d%d%d%d%d%d%d%dT%d%d%d%d%d%dZ')
end
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
    else local bytes=f:Read(size) if not bytes or #bytes~=size then err='short_read' else value={size=size,sha256=util.SHA256(bytes),path=search..'/'..path} value.build=buildOf(path,bytes) end end
    f:Close()
   end
  end
  cache[key]={value,err} return value,err
 end
end
function M.GetInstallationStatus() return status end
function M.FeatureAvailable(feature)
 if not status or not status.features.core then return false,(M.loadError or L'install.unavailable_repair') end
 if status.features[feature]==false then
  -- Warnings disable nothing, so they are never the reason.
  for _,v in ipairs(status.issues) do if not v.warning and (v.feature==feature or v.feature=='core') then return false,v.message end end
  return false,L('install.checking_feature',{feature=featureName(feature)})
 end
 return true
end
local function publicStatus()
 return {schema=1,realm='server',recommended=status.recommended,installed=status.installed,features=status.features,issues=status.issues,
  fingerprint=status.fingerprint,unverified=status.unverified,blocked=status.blocked,update=status.update}
end
-- Whether release a (build ID ab) comes before release b (bb) in the update reminder's
-- order; a build of the same label never does.
local function releaseBefore(a,ab,b,bb)
 if a:gsub('%+.*$','')==b:gsub('%+.*$','') then return false end
 return olderBuild({release=a,build=ab},{release=b,build=bb})
end
-- Whether this realm's native module is release label or newer, in the update reminder's
-- order: the release its files match, else (files this addon does not know) the loaded
-- module's own.
function M.NativeReleaseAtLeast(label)
 local releases=policy.releases or {}
 local loaded=status and status.loaded and status.loaded.module
 local record=status and status.installed and releases[status.installed]
 local current,build=record and status.installed or loaded and loaded.release,record and record.build or loaded and loaded.build
 if type(label)~='string' or type(current)~='string' then return false end
 return not releaseBefore(current,build,label,releases[label] and releases[label].build)
end
-- A loaded module this addon may not drive correctly (too old to check, another
-- interface): the update reminder offers the download, worded as needed, when it is older
-- than the recommended release (nil: too old to say which). installed is the module's own
-- label (the files on disk may say otherwise).
local function requireUpdate(installed,build)
 local recommended=(policy.releases or {})[policy.recommended] or {}
 if installed~=nil and not (type(installed)=='string' and type(policy.recommended)=='string' and releaseBefore(installed,build,policy.recommended,recommended.build)) then return end
 status.update={installed=installed,recommended=policy.recommended,url=recommended.url,altUrl=recommended.altUrl,required=true}
end
-- Files this addon does not know, from a release older than the recommended one: the
-- ordinary reminder.
local function offerUpdate(installed,build)
 local recommended=(policy.releases or {})[policy.recommended]
 if status.update or not recommended or type(installed)~='string' or type(policy.recommended)~='string' then return end
 if releaseBefore(installed,build,policy.recommended,recommended.build) then status.update={installed=installed,recommended=policy.recommended,url=recommended.url,altUrl=recommended.altUrl} end
end
local updateLogged
-- Every issue once per session in the console, warnings too.
local function logIssues()
 for _,v in ipairs(status.issues) do local key=v.code..'/'..v.component..'/'..v.message if not loggedIssues[key] then loggedIssues[key]=true MsgN('[Model Hotloader / '..status.realm..'] '..M.Localize(v.message)) end end
end
local function notifyChanged()
 -- The problem that stops the addon: warnings never do, nor a problem of one feature (a
 -- missing worker or CoACD file that a failed load made a problem cannot explain it).
 local blocking
 for _,v in ipairs(status.issues) do if not v.warning and v.feature=='core' then blocking=v.message break end end
 M.loadError=not status.features.core and (blocking or L'install.unavailable') or nil
 local encoded=util.TableToJSON(publicStatus())
 if encoded~=lastFingerprint then
  lastFingerprint=encoded
  logIssues()
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
 -- their initialization hooks when this server's native module does not load.
 -- Clients do not check the server's status first, so a spawn request gets this
 -- server's problem as its answer instead of waiting for the client's timeout.
 local spawnReplies={mmdhl_action='mmdhl_spawn_status',mmdhl_prop_action='mmdhl_prop_status'}
 -- The physics editor's reply channel: physics_editor.lua, which pools it too, does not load
 -- when the module does not, and net.Start refuses a name that was never pooled.
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
-- Whether Garry's Mod loaded the files this check read. On a mismatch, also returns
-- whether the loaded files share this addon's interface (installation API, native API,
-- platform): other bytes, another build or another location do; an unknown or other
-- interface does not.
local function identitiesMatch(info)
 for _,key in ipairs({'module','runtime'}) do
  local record=status.files[key=='module' and status.realm or 'runtime']
  local found=info and info[key]
  local message=key=='module' and L'install.error.loaded_module_mismatch' or L'install.error.loaded_runtime_mismatch'
  if not (found and found.installApi==1 and found.api==(status.expected and status.expected.api or 1) and found.platform=='win64') then return false,message,false end
  local valid=false
  for _,path in ipairs(expectedPaths(key,info)) do if normalize(found.path)==path then valid=true end end
  -- Files this addon does not know are identified by their bytes on disk, not by a release.
  if valid and not status.installed then valid=record~=nil and record.actual~=nil and found.sha256==record.actual.sha256
  elseif valid then valid=record~=nil and found.release==status.expected.release and found.build==status.expected.build and found.sha256==record.expected.sha256 end
  if not valid then return false,message,true end
 end
 return true
end
-- A loaded file that is not the checked one (or that this addon cannot check) is a warning
-- about files this addon does not know (identity): Model Hotloader uses what Garry's Mod
-- loaded. Another interface also asks for the update when the module is older.
local function checkLoaded(info)
 local ok,message,sameInterface=identitiesMatch(info)
 if ok then return true end
 local module=type(info)=='table' and type(info.module)=='table' and info.module
 if not sameInterface and module and type(module.release)=='string' then requireUpdate(module.release,module.build) end
 warn(status,'loaded_mismatch','module',message,nil,{identity=true,detail=module and tostring(module.sha256) or 'unknown'})
 return true
end
local function removeIssues(feature)
 for i=#status.issues,1,-1 do if status.issues[i].feature==feature then table.remove(status.issues,i) end end
end
function M.RefreshGameCompatibility()
 if not rawNative or not status.features.core then return end
 local feature=SERVER and 'physics' or 'rendering'
 removeIssues(feature)
 -- A module from before this addon checked game builds relies on its own guards.
 if type(rawNative.CheckCompatibility)~='function' then
  status.features[feature]=true
  warn(status,'game_unchecked','game',L('install.warning.game_unchecked',{recommended=tostring(policy.recommended)}),feature)
  notifyChanged()
  return
 end
 local ok,value,err=pcall(rawNative.CheckCompatibility)
 local report=ok and decode(value,err)
 -- Only a library the game has not loaded yet keeps the feature off for now (the binary's
 -- renderer would keep that absence for the session). A game build the binary's interface,
 -- slot or class checks reject is a warning: Model Hotloader tries anyway, and the same
 -- checks still guard every engine call the binary makes (a refused call fails cleanly).
 status.features[feature]=not (report and report.pending)
 status.compatibility=report
 if not report then warn(status,'game_check_failed','game',L('install.warning.game_incompatible',{message=tostring(err or value)}),feature)
 else
  for _,v in ipairs(report.issues or {}) do warn(status,v.code,v.component,L('install.warning.game_incompatible',{message=tostring(v.message)}),feature) end
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
 -- A module from before the self-test: imports run unchecked.
 if type(rawNative.StartInstallationProbe)~='function' then return end
 local coacd=status.features.detailedCollision
 status.features.imports=false status.probePending=true
 local function failed(why)
  promote(status,'imports') issue(status,'worker_failed','worker',why,'imports').probe=true
 end
 local started,err=rawNative.StartInstallationProbe(coacd)
 if not started then status.probePending=false failed(tostring(err)) notifyChanged() return end
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
  if not istable(result) then failed(why or L'install.error.worker_failed')
  else
   local identity,runtime=istable(result.identity) and result.identity or {},istable(result.runtime) and result.runtime or {}
   -- The worker and its runtime copy share C++ types: they must come from one build (or
   -- imports may fail in the worker). Files this addon does not know are fine while they
   -- agree with each other and with the runtime copy read from disk.
   local mismatch=identity.build~=runtime.build
   if not mismatch and status.installed then mismatch=identity.release~=status.expected.release or identity.build~=status.expected.build or runtime.sha256~=status.expected.files.runtime.sha256
   elseif not mismatch then local copy=status.files.workerRuntime and status.files.workerRuntime.actual mismatch=not copy or runtime.sha256~=copy.sha256 end
   if mismatch then warn(status,'mixed_installation','worker',L'install.error.worker_mismatch','imports',{identity=true,probe=true,detail=tostring(identity.build)..'/'..tostring(runtime.build)}) end
   status.features.imports=true
   if coacd and not result.coacd then promote(status,'detailedCollision') issue(status,'dependency_failed','coacd',result.coacdError or L'install.error.coacd_failed','detailedCollision').probe=true end
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
-- What loading found out, kept for Recheck: the policy the module would not take at all
-- (its own checks then decide, as for another ABI family), or the libraries it took
-- without.
local configureError,compatibilityFallback
-- What the loaded module says about itself, on the first load and on every Recheck.
local function assessLoaded()
 status.loaded=loadedIdentity
 if type(rawNative.GetInstallationInfo)=='function' then checkLoaded(loadedIdentity) end
 if type(rawNative.GetInstallationInfo)~='function' or type(rawNative.ConfigureCompatibility)~='function' or type(rawNative.CheckCompatibility)~='function' then
  -- From before installation verification: it runs, with the update offered as needed. A
  -- release the policy records without verification (installApi 0) has its warning already.
  local ok,capabilities=pcall(function() return decode(rawNative.GetCapabilities()) end)
  local listed=false for _,v in ipairs(status.issues) do if v.code=='outdated' then listed=true end end
  if not listed then warn(status,'outdated','module',L('install.error.no_verification',{recommended=tostring(policy.recommended)})) end
  requireUpdate(ok and istable(capabilities) and isstring(capabilities.version) and capabilities.version or nil)
 elseif not status.installed then
  local module=istable(loadedIdentity) and istable(loadedIdentity.module) and loadedIdentity.module
  if module then offerUpdate(module.release,module.build) end
 end
 if configureError then warn(status,'policy_invalid','compatibility',configureError) end
 if compatibilityFallback then warn(status,'compatibility_fallback','compatibility',compatibilityFallback) end
end
function M.CheckInstallation(recheck)
 if recheck and status and status.probePending then return status end
 local previous=status
 local function find(pattern) if not file.Find then return {} end local _,folders=file.Find(pattern,'BASE_PATH') return folders end
 status=M.EvaluateInstallation(policy,readerForSession(),{server=SERVER,windows=system.IsWindows(),arch=jit.arch,dedicated=SERVER and game.IsDedicated(),find=find})
 if recheck then
  -- Garry's Mod never loads a DLL twice: what did not load needs a restart.
  if not rawNative then promote(status) issue(status,'restart_required','module',L'install.error.restart_to_load') notifyChanged() return status end
  assessLoaded()
  if previous then
   -- Runtime failures are carried over below; valid files alone do not mean a restart repairs them.
   -- A failure that missing or unreadable files explained (promoted when it failed) is theirs:
   -- while they still fail they explain it again; repaired, a restart may repair it.
   local repaired={}
   for _,v in ipairs(previous.issues) do if v.cause and not v.warning then repaired[v.feature]=true end end
   for _,v in ipairs(status.issues) do if v.cause and repaired[v.feature]~=nil then repaired[v.feature]=false end end
   local carried=function(v) return (v.probe and not repaired[v.feature]) or v.code=='game_incompatible' end
   local runtime={}
   for _,v in ipairs(previous.issues) do if carried(v) and not v.warning then runtime[v.feature]=true end end
   for key,allowed in pairs(previous.features) do
    if not allowed and status.features[key] and not runtime[key] then issue(status,'restart_required',key,L('install.error.restart_to_enable',{feature=featureName(key)}),key) end
    status.features[key]=status.features[key] and allowed
   end
   for _,v in ipairs(previous.issues) do if carried(v) then status.issues[#status.issues+1]=v end end
   for feature,fixed in pairs(repaired) do if not fixed then promote(status,feature) end end
  end
  M.RefreshGameCompatibility()
  notifyChanged() return status
 end
 -- Nothing loaded: the warnings that explain why become the problems, and everything is off.
 local function unloaded(problem)
  promote(status)
  if problem then issue(status,'loader_failed','module',problem) end
  status.blocked=true for key in pairs(status.features) do status.features[key]=false end
  notifyChanged() return false
 end
 -- The files are always tried. With no module file (or none for this platform) there is
 -- nothing to load. Warnings reach the console first, so a crash while loading leaves its
 -- reason in console.log.
 local own=status.files[status.realm]
 if not own or own.error=='missing' then return unloaded() end
 logIssues()
 local ok,err=pcall(require,'mmdhl')
 if not ok or not istable(mmdhl_native) then return unloaded(L('install.error.loader_failed',{reason=tostring(err)})) end
 rawNative=mmdhl_native
 if type(rawNative.GetInstallationInfo)=='function' then
  local identityOk,info=pcall(rawNative.GetInstallationInfo)
  loadedIdentity=identityOk and decode(info) or nil
 end
 if type(rawNative.ConfigureCompatibility)=='function' then
  local compat=include('mmdhl/compatibility_policy.lua')
  local configured,configError,without=configureCompatibility(compat)
  if not configured then configureError=tostring(configError)
  elseif without then compatibilityFallback=L('install.warning.compatibility_fallback',{libraries=without,recommended=tostring(policy.recommended)}) end
 end
 assessLoaded()
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
   elseif attempts==20 then
    -- Still not loaded: tried anyway, as any other game check.
    local feature=SERVER and 'physics' or 'rendering'
    warn(status,'game_not_ready','game',L'install.error.game_not_ready',feature) status.features[feature]=true notifyChanged()
   end
  end)
 end
end)
concommand.Add('mmdhl_check_installation',function(p)
 if SERVER and IsValid(p) and not p:IsAdmin() then return end
 M.CheckInstallation(true)
end)
