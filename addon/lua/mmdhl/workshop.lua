-- Workshop model packages. A package is a .gma exported from the library: a
-- manifest in data_static/mmdhl/packages/<id>.json and compressed cache files in
-- data_static/mmdhl/files/<sha256>.dat. Every realm that keeps a cache installs
-- the models of mounted packages with the verified shared-file transfer. Clients
-- label Workshop models, hide deleted ones until restored, and drop models whose
-- addon is no longer mounted. Packages marked "user" (demo models) are installed
-- once and then belong to the player like their own imports.
local native=mmdhl.native
local L=mmdhl.L
local W=mmdhl.workshop or {}
mmdhl.workshop=W
local PackageDir,FileDir='data_static/mmdhl/packages/','data_static/mmdhl/files/'
local StatePath='mmd_hotloader/workshop/state.json'
local Chunk=48*1024
local MaxPackageBytes=4*1048576
local ChunkBudget,VerifyBudget=.004,.003
W.packages=W.packages or {}
W.origins=W.origins or {}
W.provided=W.provided or {}
W.failures=W.failures or {}
-- Models whose assembled files passed the native check this session (step), by key.
W.checked=W.checked or {}
local function hex(s,n) return isstring(s) and #s==n and not s:find('[^0-9a-f]') end
local function integer(v,low,high) return isnumber(v) and v%1==0 and v>=low and v<=high end
-- Keeps UTF-8 whole: never cut inside a multi-byte character.
local function clip(s,bytes)
 if #s<=bytes then return s end
 local cut=bytes while cut>0 and s:byte(cut+1)>=128 and s:byte(cut+1)<192 do cut=cut-1 end
 return s:sub(1,cut)
end
function W.CleanText(s,bytes,multiline)
 if not isstring(s) then return '' end
 if utf8.force then s=utf8.force(s) end
 s=s:gsub(multiline and '[%z\1-\8\11\12\14-\31\127]' or '[%z\1-\31\127]',multiline and '' or ' ')
 return clip(string.Trim(s),bytes)
end
function W.Key(kind,id) return (kind=='static' and 'static' or 'character')..':'..id end
local function split(key) return key:match('^(%a+):(%x+)$') end
-- The largest file a package may list, by kind (props as in sharing); nil: not allowed.
local function fileLimit(path)
 local id,name=path:match('^assets/(%x+)/([%w%.]+)$')
 if hex(id,64) then return name=='manifest.json' and 64*1048576 or name=='model.bin' and 1073741824 or nil end
 if hex(path:match('^textures/(%x+)%.png$'),64) then return 256*1048576 end
 if hex(path:match('^static/assets/(%x+)%.gmdl$'),64) then return 268435456 end
end
-- A package comes from any addon; nothing in it is trusted. Returns the package or nil and why.
function W.ValidatePackage(id,raw)
 if not hex(id,32) then return nil,'invalid package name' end
 if not isstring(raw) or #raw==0 or #raw>MaxPackageBytes then return nil,'package manifest missing or too large' end
 local p=util.JSONToTable(raw)
 if not istable(p) or p.format~='mmdhl-package' or p.version~=1 or p.id~=id then return nil,'not a model package of a supported version' end
 if p.install~='workshop' and p.install~='user' then return nil,'unknown install mode' end
 if not istable(p.items) or not istable(p.files) or #p.items==0 or #p.items>1000 then return nil,'package lists no models' end
 local package={id=id,install=p.install,title=W.CleanText(p.title,128),author=W.CleanText(p.author,128),description=W.CleanText(p.description,8000,true),
  created=integer(p.created,0,2^40) and p.created or 0,folder=W.CleanText(p.folder,80),items={},files={}}
 if package.title=='' then package.title=id:sub(1,8) end
 -- Every subscriber inflates and writes what a package declares: bound each file
 -- by its kind and by what its packed bytes can expand to, and the whole package.
 local total=0
 for path,f in pairs(p.files) do
  if not isstring(path) or not istable(f) or not integer(f.size,0,4294967295) or not hex(f.sha256,64) or not integer(f.packed,80,4294967295)
   or f.packed>f.size+80+8*math.ceil(f.size/1048576) then return nil,'invalid file entry' end
  local limit=fileLimit(path)
  if not limit then return nil,'package lists a forbidden file' end
  if f.size>limit then return nil,'file too large for its kind' end
  if f.size>math.max(1048576,f.packed*256) then return nil,'file expands too far beyond its packed size' end
  total=total+f.size
  package.files[path]={size=f.size,sha256=f.sha256,packed=f.packed}
 end
 if total>16*1073741824 then return nil,'package too large' end
 local seen={}
 for _,item in ipairs(p.items) do
  if not istable(item) or (item.kind~='character' and item.kind~='static') or not hex(item.asset,64) or not istable(item.files) then return nil,'invalid model entry' end
  local key=W.Key(item.kind,item.asset)
  if not seen[key] then
   seen[key]=true
   local files,found={},{}
   for _,path in ipairs(item.files) do
    if not isstring(path) or not package.files[path] or found[path] then return nil,'model lists an unknown file' end
    found[path]=true files[#files+1]=path
   end
   -- Only the raw imported data. Generated archives (which the game mounts) are rebuilt locally.
   if item.kind=='character' then
    local manifest,model='assets/'..item.asset..'/manifest.json','assets/'..item.asset..'/model.bin'
    if not found[manifest] or not found[model] then return nil,'character without model data' end
    for _,path in ipairs(files) do if path~=manifest and path~=model and not path:match('^textures/%x+%.png$') then return nil,'character lists a forbidden file' end end
   else
    local bundle='static/assets/'..item.asset..'.gmdl'
    if #files~=1 or files[1]~=bundle or package.files[bundle].sha256~=item.asset then return nil,'invalid static prop entry' end
   end
   for _,path in ipairs(files) do if path:match('^textures/') and package.files[path].sha256..'.png'~=path:match('([^/]+)$') then return nil,'texture does not match its hash' end end
   package.items[#package.items+1]={kind=item.kind,asset=item.asset,name=W.CleanText(item.name,100),files=files,settings=istable(item.settings) and item.settings or nil,
    arms=istable(item.arms) and item.arms or nil,fit=istable(item.fit) and item.fit or nil,terms=istable(item.terms) and item.terms or nil}
  end
 end
 return package
end
-- Per-model settings from a package, reduced to known keys and ranges.
function W.ItemSettings(item)
 local s=istable(item.settings) and item.settings or {}
 local out={name=item.name~='' and item.name or nil}
 if item.kind=='character' then
  local scale=istable(s.spawn) and tonumber(s.spawn.scaleMultiplier)
  if scale then out.spawn={scaleMultiplier=math.Clamp(scale,.1,4)} end
  local b=istable(s.bodygroups) and s.bodygroups
  if b and istable(b.presets) then
   local presets,count={},0
   for key,preset in pairs(b.presets) do
    local name=W.CleanText(tostring(key),64)
    if name~='' and istable(preset) and istable(preset.visible) and count<64 then
     local visible={}
     for index,v in pairs(preset.visible) do local i=tonumber(index) if i and i>=1 and i<=512 and i%1==0 then visible[i]=v==true end end
     presets[name]={visible=visible} count=count+1
    end
   end
   if count>0 then out.bodygroups={presets=presets,default=isstring(b.default) and presets[W.CleanText(b.default,64)] and W.CleanText(b.default,64) or nil} end
  end
 else
  local scale=istable(s.spawn) and tonumber(s.spawn.scale)
  if scale then out.spawn={scale=math.Clamp(scale,.05,20)} end
 end
 return out
end
function W.ItemArms(item)
 if item.kind~='character' or not istable(item.arms) then return nil end
 local out=mmdhl.CleanArmsParts(item.arms)
 return next(out)~=nil and out or nil
end
-- A model's saved fit (fit_overrides/<id>.json): collision corrections (bodies), the bone
-- window's pins (boneMap) and a physics default (mass, physics). Each part is optional:
-- pins are often saved alone, and new pins drop the corrections made for the old bones.
-- A part that is there must have its type, pins must name assignable parts with whole
-- bone numbers (the bone window's rules), and the fit must hold at least one part.
function W.ItemFit(item)
 local f=item.kind=='character' and istable(item.fit) and item.fit
 if not f or f.version~=3 or (f.generator~=14 and f.generator~=15 and f.generator~=18) or #util.TableToJSON(f)>512*1024 then return nil end
 for _,key in ipairs({'bodies','excludedMaterials','physics','editor'}) do if f[key]~=nil and not istable(f[key]) then return nil end end
 if f.mass~=nil and not isnumber(f.mass) then return nil end
 if f.boneMap~=nil then
  local BM=mmdhl.boneMapper
  if not (istable(BM) and isfunction(BM.CleanPins) and BM.CleanPins(f.boneMap)) then return nil end
  for _,bone in pairs(f.boneMap) do if not isnumber(bone) then return nil end end
 end
 local pinned=istable(f.boneMap) and next(f.boneMap)~=nil
 if not (f.bodies or pinned or f.physics or f.mass or (f.excludedMaterials and next(f.excludedMaterials)~=nil)) then return nil end
 return f
end
local function inCache(kind,id)
 if kind=='static' then return file.Exists('mmd_hotloader/static/assets/'..id..'.gmdl','DATA') end
 return file.Exists('mmd_hotloader/assets/'..id..'/manifest.json','DATA') and file.Exists('mmd_hotloader/assets/'..id..'/model.bin','DATA')
end
W.InCache=inCache
local function loadState()
 local s=util.JSONToTable(file.Read(StatePath,'DATA') or '')
 if not istable(s) or s.version~=1 or not istable(s.assets) then s={version=1,assets={}} end
 return s
end
W.state=W.state or loadState()
local function saveState() file.CreateDir('mmd_hotloader/workshop') file.Write(StatePath,util.TableToJSON(W.state)) end
local function itemFor(package,kind,id) for _,item in ipairs(package and package.items or {}) do if item.kind==kind and item.asset==id then return item end end end
-- Installed means every file the package lists. A model file arrives before its
-- textures, so an install that stopped part way (a failed texture, a closed
-- game) looks cached but has no completion record: it resumes on the next scan.
-- Files of a model this addon installed must also keep their sizes (verify hashes
-- those modified since); the player's own import of the model only needs every file.
local function complete(item,package,installed)
 if not item or not inCache(item.kind,item.asset) then return false end
 for _,path in ipairs(item.files) do
  local name='mmd_hotloader/'..path
  if installed then if (file.Size(name,'DATA') or -1)~=package.files[path].size then return false end
  elseif not file.Exists(name,'DATA') then return false end
 end
 return true
end
-- The package a model is shown under: prefer one that installs as Workshop content.
local function provider(key)
 local list=W.provided[key] if not list then return end
 for _,id in ipairs(list) do if W.packages[id].install=='workshop' then return W.packages[id] end end
 return W.packages[list[1]]
end
function W.Origin(kind,id)
 local key=W.Key(kind,id) local rec=W.state.assets[key] local package=provider(key)
 if not package or not rec or not rec.installed or rec.adopted then return nil end
 local origin=W.origins[package.id] or {}
 return {package=package.id,title=origin.title or package.title,packageTitle=package.title,author=package.author,wsid=origin.wsid or '0',workshop=origin.workshop}
end
function W.IsHidden(kind,id) local rec=W.state.assets[W.Key(kind,id)] return rec~=nil and rec.hidden==true end
-- Deleted models whose package is still mounted, for the Deleted Workshop models view.
function W.Deleted(kind)
 local out={}
 for key,rec in pairs(W.state.assets) do
  local k,id=split(key)
  if rec.hidden and k==(kind=='static' and 'static' or 'character') and W.provided[key] then
   local package=provider(key) local item=itemFor(package,k,id) local origin=W.origins[package.id] or {}
   out[#out+1]={id=id,name=item and item.name~='' and item.name or rec.name or id:sub(1,12),title=origin.title or package.title,package=package.id,wsid=origin.wsid or '0',workshop=origin.workshop,author=package.author}
  end
 end
 table.sort(out,function(a,b) return a.name:lower()<b.name:lower() end)
 return out
end
W.revision=W.revision or 0
local function changed() W.revision=W.revision+1 hook.Run('MMDHL.WorkshopChanged') end
-- Installation: one model at a time, one file at a time, bounded work per frame.
local queue,queued,current={}, {},nil
W.installed=0
local function progress(done)
 if SERVER then return end
 local total=W.installed+#queue+(current and 1 or 0)
 if done or total==0 then notification.Kill('MMDHL.Workshop') return end
 local name=current and current.item.name~='' and current.item.name or ''
 notification.AddProgress('MMDHL.Workshop',L('workshop.installing',{done=W.installed,total=total,name=name}),total>0 and W.installed/total or nil)
end
function W.Busy() return current~=nil or #queue>0 end
-- changed: the index of a file known to differ; the ones before it were just verified.
local function enqueue(package,item,changed)
 local key=W.Key(item.kind,item.asset)
 if queued[key] then return end
 local files={} for _,path in ipairs(item.files) do local f=package.files[path] files[#files+1]={path=path,size=f.size,sha256=f.sha256,packed=f.packed} end
 queued[key]=true queue[#queue+1]={key=key,package=package,item=item,files=files,origin=W.origins[package.id] or {},changed=changed}
end
-- The modification time of each file of a model when it was last known good:
-- installed, or hashed by verify. Only the realm that installs records them (a
-- dedicated server keeps records that hold nothing else).
local function stamps(item)
 local out={} for _,path in ipairs(item.files) do out[path]=file.Time('mmd_hotloader/'..path,'DATA') end return out
end
local function remember(key,item)
 local rec=W.state.assets[key] or {} rec.times=stamps(item) W.state.assets[key]=rec saveState()
end
-- Once per session, installed files whose modification time changed since then
-- are hashed again, a few milliseconds of work per frame while nothing installs.
-- An unchanged cache costs no reads. A file that no longer matches clears the
-- completion record and installs the model again from that file.
local checks,checked={},{}
local function verify(package,item)
 local key=W.Key(item.kind,item.asset)
 if checked[key] then return end
 local rec=W.state.assets[key] local times=rec and rec.times or {} local files={}
 for index,path in ipairs(item.files) do if times[path]~=file.Time('mmd_hotloader/'..path,'DATA') then files[#files+1]=index end end
 if #files==0 then checked[key]=true return end
 for _,check in ipairs(checks) do if check.key==key then if check.package~=package then check.package,check.item,check.files,check.at=package,item,files,1 end return end end
 checks[#checks+1]={key=key,package=package,item=item,files=files,at=1}
end
hook.Add('Think','MMDHL.WorkshopVerify',function()
 if not checks[1] or current or #queue>0 then return end
 local deadline=SysTime()+VerifyBudget
 while checks[1] and SysTime()<deadline do
  local check=checks[1] local index=check.files[check.at] local path=index and check.item.files[index]
  -- A later scan that no longer lists the model (package gone, model deleted) drops it.
  if W.packages[check.package.id]~=check.package then table.remove(checks,1)
  elseif not path then table.remove(checks,1) checked[check.key]=true remember(check.key,check.item)
  else
   local f=check.package.files[path]
   if native.SharedFileMatches(path,f.size,f.sha256) then check.at=check.at+1
   else
    -- The install replaces this file and checks the ones after it.
    table.remove(checks,1) checked[check.key]=true
    local rec=W.state.assets[check.key] if rec and rec.installed then rec.installed=nil saveState() end
    enqueue(check.package,check.item,index) return
   end
  end
 end
end)
local function writeIfMissing(path,value)
 if file.Exists(path,'DATA') then return end
 file.CreateDir(string.GetPathFromFilename(path):sub(1,-2)) file.Write(path,util.TableToJSON(value,true))
end
local refreshPending
local function refreshLibraries()
 if SERVER or refreshPending then return end refreshPending=true
 timer.Simple(.25,function()
  refreshPending=nil
  if mmdhl.library then mmdhl.library.Refresh() end
  if mmdhl.props and mmdhl.props.library then mmdhl.props.library.Refresh() end
 end)
end
local installedHandlers={}
function W.OnInstalled(handler) installedHandlers[#installedHandlers+1]=handler end
local function installed(job)
 local item,package=job.item,job.package
 if CLIENT then
  local settings=W.ItemSettings(item)
  local user=package.install=='user'
  if user and package.folder~='' and not package.folder:find('[/\\]') then settings.folder=package.folder end
  if item.kind=='static' then writeIfMissing('mmd_hotloader/static/library/'..item.asset..'.json',settings)
  else
   writeIfMissing('mmd_hotloader/library/'..item.asset..'.json',settings)
   local arms=W.ItemArms(item) if arms then writeIfMissing('mmd_hotloader/arms/'..item.asset..'.json',arms) end
   local fit=W.ItemFit(item) if fit then writeIfMissing('mmd_hotloader/fit_overrides/'..item.asset..'.json',fit) end
  end
  if mmdhl.terms and item.terms then mmdhl.terms.SaveFromPackage(item.kind,item.asset,item.terms,package.title) end
  local rec=W.state.assets[job.key] or {}
  rec.installed=true rec.adopted=user or rec.adopted or nil rec.hidden=nil rec.name=item.name rec.package=package.id rec.times=stamps(item)
  W.state.assets[job.key]=rec saveState()
  refreshLibraries()
  -- A model deleted earlier this session can still be queued for disk cleanup,
  -- which hides it. Loading it (and building its Source materials) dequeues it.
  local pending=item.kind=='character' and util.JSONToTable(file.Read('mmd_hotloader/cleanup.json','DATA') or '')
  if istable(pending) and table.HasValue(pending,'assets/'..item.asset) then
   native.RequestAsset(item.asset)
   local name,tries='MMDHL.WorkshopLoad.'..item.asset,0
   timer.Create(name,.5,60,function()
    tries=tries+1 local info,err=native.AssetInfo(item.asset)
    if info or err or tries>=60 then timer.Remove(name) refreshLibraries() end
   end)
  end
 -- Only a dedicated server installs in the server realm; it also needs the
 -- package's saved collision and physics to use them for its spawns.
 else
  remember(job.key,item)
  local fit=item.kind=='character' and game.IsDedicated() and W.ItemFit(item) if fit then writeIfMissing('mmd_hotloader/fit_overrides/'..item.asset..'.json',fit) end
 end
 for _,handler in ipairs(installedHandlers) do handler(item,package) end
end
local function fail(job,err)
 err=mmdhl.Localize(tostring(err or '?'))
 W.failures[job.key]=err
 ErrorNoHalt('[Model Hotloader workshop] '..(job.item.name~='' and job.item.name or job.item.asset)..': '..err..'\n')
 if CLIENT then notification.AddLegacy(L('workshop.install_failed',{name=job.item.name~='' and job.item.name or job.item.asset:sub(1,12),error=err}),NOTIFY_ERROR,8) end
end
local function close(job) if job.stream then job.stream:Close() job.stream=nil end if job.handle then native.CancelSharedFile(job.handle) job.handle=nil end end
-- Advances the current file; returns true when the job finished (either way).
local function step(job,deadline)
 while SysTime()<deadline do
  local f=job.files[job.index]
  if not f then
   -- Every file matches its hash. Together they must also make up the model the package
   -- names (natives after 2.3.0 load it as using it would) before it counts as installed.
   if not native.StartAssetCheck then return true,nil end
   if job.phase~='validate' then
    local handle,err=native.StartAssetCheck(job.item.kind,job.item.asset) if not handle then return true,err end
    job.check,job.phase=handle,'validate'
   end
   local valid,err,code=native.PollAssetCheck(job.check)
   if valid==false then return false end
   job.check=nil
   if valid==true then W.checked[job.key]=true return true,nil end
   job.invalid=code=='invalid' return true,L('workshop.error.invalid_model',{reason=mmdhl.Localize(tostring(err or '?'))})
  end
  if job.phase=='check' then
   local matches,err=native.SharedFileMatches(f.path,f.size,f.sha256)
   if err then return true,err end
   if matches then job.index=job.index+1 else job.phase='open' end
  elseif job.phase=='open' then
   local source=FileDir..f.sha256..'.dat'
   local stream=job.origin.pathId and job.origin.pathId~='GAME' and file.Open(source,'rb',job.origin.pathId) or nil
   stream=stream or file.Open(source,'rb','GAME')
   if not stream then return true,L'workshop.error.file_missing' end
   if stream:Size()~=f.packed then stream:Close() return true,L'workshop.error.file_size' end
   local handle,err=native.BeginSharedFile(f.path,f.size,f.sha256,f.packed)
   if not handle then stream:Close() return true,err end
   job.stream,job.handle,job.offset,job.phase=stream,handle,0,'stream'
  elseif job.phase=='stream' then
   local bytes=job.stream:Read(math.min(Chunk,f.packed-job.offset))
   if not bytes or #bytes==0 then return true,L'workshop.error.file_size' end
   local ok,err=native.AppendSharedChunk(job.handle,job.offset,bytes)
   if not ok then return true,err end
   job.offset=job.offset+#bytes
   if job.offset>=f.packed then
    job.stream:Close() job.stream=nil
    local started,reason=native.CommitSharedFile(job.handle)
    if not started then return true,reason end
    job.phase='commit'
   end
  elseif job.phase=='commit' then
   local done,err=native.PollSharedCommit(job.handle)
   if err then return true,err end
   if not done then return false end
   job.handle=nil job.index=job.index+1 job.phase='check'
  end
 end
 return false
end
hook.Add('Think','MMDHL.WorkshopInstall',function()
 if not current then
  current=table.remove(queue,1)
  if not current then return end
  current.index,current.phase=current.changed or 1,current.changed and 'open' or 'check' progress()
 end
 local finished,err=step(current,SysTime()+ChunkBudget)
 if not finished then return end
 local job=current current=nil queued[job.key]=nil close(job)
 -- Files that make up no valid model are not left for the library to list: their manifest (a
 -- prop: its bundle) goes. Shared textures stay.
 if job.invalid then file.Delete(job.item.kind=='static' and 'mmd_hotloader/static/assets/'..job.item.asset..'.gmdl' or 'mmd_hotloader/assets/'..job.item.asset..'/manifest.json') end
 if err then fail(job,err) else W.failures[job.key]=nil W.installed=W.installed+1 installed(job) end
 if #queue==0 then
  if CLIENT and W.installed>0 then notification.AddLegacy(L('workshop.installed',{count=W.installed}),NOTIFY_GENERIC,6) end
  W.installed=0 progress(true)
  if CLIENT and mmdhl.workshop.AfterInstalls then mmdhl.workshop.AfterInstalls() end
 else progress() end
end)
-- Discovery runs as a coroutine over a few frames: hundreds of addons may be mounted.
local scanning,rescan
local function discover()
 local found={}
 for _,name in ipairs((file.Find(PackageDir..'*.json','GAME'))) do
  local id=name:match('^(%x+)%.json$')
  if id and #id==32 then
   local package,err=W.ValidatePackage(id,file.Read(PackageDir..name,'GAME'))
   if package then found[id]=package elseif not W.failures['package:'..id] then W.failures['package:'..id]=err ErrorNoHalt('[Model Hotloader workshop] Ignored package '..id..': '..tostring(err)..'\n') end
  end
 end
 local origins={}
 if next(found) then
  local addons=engine.GetAddons and engine.GetAddons() or {}
  local function assign(id,a,pathId)
   local wsid=tostring(a.wsid or '0')
   if not origins[id] or (origins[id].wsid=='0' and wsid~='0') then origins[id]={pathId=pathId or a.title,title=a.title,wsid=wsid,workshop=wsid~='0'} end
  end
  -- An exported GMA carries the package title, and GMod mounts it under that
  -- title: an exact match unless the Workshop item was renamed since.
  local byTitle={}
  for _,a in ipairs(addons) do if a.mounted and isstring(a.title) and a.title~='' then byTitle[a.title]=byTitle[a.title] or {} table.insert(byTitle[a.title],a) end end
  for id,package in pairs(found) do for _,a in ipairs(byTitle[package.title] or {}) do if file.Exists(PackageDir..id..'.json',a.title) then assign(id,a) end end end
  local missing=false for id in pairs(found) do if not origins[id] then missing=true end end
  if missing and native.StartAddonPackageScan then
   -- Renamed items: the native module reads the archive indexes off the main thread.
   local list,files={},{}
   for _,a in ipairs(addons) do if a.mounted and isstring(a.file) and a.file:lower():sub(-4)=='.gma' then list[#list+1]=a files[#files+1]=a.file end end
   local handle=native.StartAddonPackageScan(util.TableToJSON(files))
   local result=false
   while handle and result==false do coroutine.yield() result=native.PollAddonPackageScan(handle) end
   for i,ids in ipairs(util.JSONToTable(result or '') or {}) do
    for _,id in ipairs(istable(ids) and ids or {}) do
     if found[id] and not origins[id] then assign(id,list[i],file.Exists(PackageDir..id..'.json',found[id].title) and found[id].title or 'GAME') end
    end
   end
  elseif missing then
   -- Older native modules: search each addon, a couple of milliseconds per frame.
   local budget=SysTime()
   for _,a in ipairs(addons) do
    if a.mounted and isstring(a.title) and a.title~='' then
     for _,name in ipairs((file.Find(PackageDir..'*.json',a.title))) do local id=name:sub(1,32) if found[id] then assign(id,a) end end
    end
    if SysTime()-budget>.002 then coroutine.yield() budget=SysTime() end
   end
  end
  for id,package in pairs(found) do
   if not origins[id] then origins[id]={pathId='GAME',title=package.title,wsid='0',workshop=file.Exists(PackageDir..id..'.json','WORKSHOP')} end
  end
 end
 local provided={}
 for id,package in pairs(found) do for _,item in ipairs(package.items) do local key=W.Key(item.kind,item.asset) provided[key]=provided[key] or {} table.insert(provided[key],id) end end
 for _,list in pairs(provided) do table.sort(list) end
 W.packages,W.origins,W.provided=found,origins,provided
end
-- What the libraries show depends only on the mounted packages and their origins.
local function signature()
 local parts={}
 for key,list in pairs(W.provided) do parts[#parts+1]=key..'='..table.concat(list,',') end
 for id,o in pairs(W.origins) do parts[#parts+1]=id..'@'..tostring(o.title)..'#'..tostring(o.wsid) end
 table.sort(parts) return table.concat(parts,'\n')
end
function W.Scan()
 if scanning then rescan=true return end
 scanning=coroutine.create(function()
  discover()
  local sig=signature() local fresh=sig~=(W.signature or '') W.signature=sig
  W.Reconcile(fresh)
  if fresh then changed() end
 end)
end
hook.Add('Think','MMDHL.WorkshopScan',function()
 if not scanning then return end
 local ok,err=coroutine.resume(scanning)
 if not ok then ErrorNoHalt('[Model Hotloader workshop] '..tostring(err)..'\n') scanning=nil return end
 if coroutine.status(scanning)=='dead' then scanning=nil if rescan then rescan=nil W.Scan() end end
end)
-- Addons can be mounted while playing (subscriptions, server content); coalesce bursts.
if CLIENT then hook.Add('GameContentChanged','MMDHL.Workshop',function() timer.Create('MMDHL.WorkshopRescan',2,1,W.Scan) end) end
hook.Add('InitPostEntity','MMDHL.Workshop',function() timer.Simple(1,W.Scan) end)
if CLIENT then
 -- Models from packages that are gone, unless the player owns them (own imports, demo models).
 local removing
 local function removeOrphans()
  if removing or W.Busy() then return end
  local characters,props,used={},{},{}
  for _,ent in ipairs(mmdhl.Entities()) do used[mmdhl.GetAsset(ent)]=true end
  for _,ent in ipairs(ents.FindByClass('mmdhl_prop')) do if ent.GetAssetID then used[ent:GetAssetID()]=true end end
  for key,rec in pairs(W.state.assets) do
   local kind,id=split(key)
   if not W.provided[key] and rec.installed and not rec.adopted then
    if not inCache(kind,id) then W.state.assets[key]=nil
    elseif not used[id] then if kind=='static' then props[#props+1]=id else characters[#characters+1]=id end end
   end
  end
  saveState()
  local function forget(kind,ids) for _,id in ipairs(ids) do W.state.assets[W.Key(kind,id)]=nil end saveState() end
  local function run(lib,kind,ids,nextStep)
   if #ids==0 or not lib then nextStep() return end
   removing=true
   lib.Delete(ids,function(ok,message)
    removing=nil
    if ok then forget(kind,ids) else ErrorNoHalt('[Model Hotloader workshop] '..tostring(message)..'\n') end
    nextStep()
   end)
  end
  run(mmdhl.library,'character',characters,function() run(mmdhl.props and mmdhl.props.library,'static',props,function() changed() end) end)
 end
 function W.Reconcile(fresh)
  local dirty=false
  for key,list in pairs(W.provided) do
   local kind,id=split(key) local rec=W.state.assets[key] local package=provider(key) local item=itemFor(package,kind,id)
   local user=false for _,pid in ipairs(list) do if W.packages[pid].install=='user' then user=true end end
   if rec and rec.installed and user and not rec.adopted then rec.adopted=true dirty=true end
   if not (rec and rec.hidden) then
    if not complete(item,package,rec and rec.installed) then
     -- A missing or resized file: not installed until the package repairs it.
     if rec and rec.installed then rec.installed=nil dirty=true end
     enqueue(package,item)
    -- Only this addon's installs; a player's own import of the model is theirs.
    elseif rec and rec.installed then verify(package,item) end
   end
  end
  if dirty then saveState() end
  W.AfterInstalls=removeOrphans
  if not W.Busy() then removeOrphans() end
  -- Re-reading every manifest is costly; only when the packages changed.
  if fresh~=false or dirty then refreshLibraries() end
 end
 -- Deleting a model a mounted package provides records the choice, so it is not
 -- installed again, and lists it under Deleted Workshop models for restoring.
 function W.Delete(kind,ids,done)
  kind=kind=='static' and 'static' or 'character'
  local lib=kind=='static' and mmdhl.props.library or mmdhl.library
  if W.Busy() then done(false,L'workshop.error.busy') return end
  local names={} for _,id in ipairs(ids) do local entry=lib.entries[id] names[id]=entry and entry.name end
  lib.Delete(ids,function(ok,message)
   if ok then
    local tombstones=0
    for _,id in ipairs(ids) do
     local key=W.Key(kind,id)
     if W.provided[key] then local rec=W.state.assets[key] or {} rec.hidden=true rec.name=names[id] or rec.name W.state.assets[key]=rec tombstones=tombstones+1 end
    end
    if tombstones>0 then saveState() changed() message=message..' '..L('workshop.deleted_note',{count=tombstones}) end
   end
   done(ok,message)
  end)
 end
 function W.Provided(kind,id) return W.provided[W.Key(kind,id)]~=nil end
 function W.Restore(kind,ids)
  local count=0
  for _,id in ipairs(ids) do local rec=W.state.assets[W.Key(kind,id)] if rec and rec.hidden then rec.hidden=nil count=count+1 end end
  if count==0 then return false,L'workshop.error.nothing_to_restore' end
  saveState() W.Reconcile() changed()
  return true,L('workshop.restoring',{count=count})
 end
 -- Listen servers approve Workshop models once their host installed them.
 W.OnInstalled(function()
  if not game.SinglePlayer() and IsValid(LocalPlayer()) and LocalPlayer():IsListenServerHost() then
   timer.Create('MMDHL.WorkshopReady',1,1,function() net.Start('mmdhl_workshop_ready') net.SendToServer() end)
  end
 end)
else
 util.AddNetworkString('mmdhl_workshop_ready')
 -- A dedicated server keeps its own cache; a listen server shares its host's.
 local installs=game.IsDedicated()
 function W.Reconcile()
  if game.SinglePlayer() then return end
  for key,list in pairs(W.provided) do
   local kind,id=split(key) local package=provider(key) local item=itemFor(package,kind,id)
   if complete(item,package,installs) then W.Approve(item,package) if installs then verify(package,item) end
   elseif installs then enqueue(package,item) end
  end
  W.ForgetMissing()
 end
 local function approve(item,package)
  if item.kind=='static' then if mmdhl.props and mmdhl.props.ApproveWorkshop then mmdhl.props.ApproveWorkshop(item.asset,item.name,package.id) end
  elseif mmdhl.ApproveWorkshopAsset then mmdhl.ApproveWorkshopAsset(item.asset,item.name,package.id) end
 end
 -- A model is approved once its assembled files passed the native check: in this server's own
 -- install (step), or here for one installed earlier or, on a listen server, by the host's game.
 -- Checks run one at a time; a model approved before (or by an administrator) needs none.
 local approvals,approving,waiting={},nil,{}
 function W.Approve(item,package)
  local key=W.Key(item.kind,item.asset) local list=mmdhl.approved and (item.kind=='static' and mmdhl.approved.props or mmdhl.approved.assets) or {}
  local entry=list[item.asset]
  if entry and (entry.approvedBy~='workshop' or entry.package==package.id) then return end
  if W.checked[key] or not native.StartAssetCheck then approve(item,package) return end
  if waiting[key] then waiting[key].item,waiting[key].package=item,package return end
  waiting[key]={key=key,item=item,package=package} approvals[#approvals+1]=waiting[key]
 end
 hook.Add('Think','MMDHL.WorkshopApprove',function()
  if not approving then
   approving=table.remove(approvals,1) if not approving then return end
   local handle,err=native.StartAssetCheck(approving.item.kind,approving.item.asset)
   if not handle then waiting[approving.key]=nil ErrorNoHalt('[Model Hotloader workshop] '..approving.item.asset..': '..tostring(err)..'\n') approving=nil return end
   approving.handle=handle
  end
  local valid,err=native.PollAssetCheck(approving.handle)
  if valid==false then return end
  local done=approving approving=nil waiting[done.key]=nil
  if valid==true then W.checked[done.key]=true approve(done.item,done.package)
  else
   W.failures[done.key]=mmdhl.Localize(L('workshop.error.invalid_model',{reason=tostring(err or '?')}))
   ErrorNoHalt('[Model Hotloader workshop] '..(done.item.name~='' and done.item.name or done.item.asset)..': '..W.failures[done.key]..'\n')
  end
 end)
 W.OnInstalled(function(item,package)
  -- A repaired file must not be offered from a manifest made before.
  if mmdhl.InvalidateSharedManifests then mmdhl.InvalidateSharedManifests() end
  if not game.SinglePlayer() then W.Approve(item,package) end
 end)
 -- Approvals granted because a package was mounted end with the package.
 function W.ForgetMissing()
  local approved=mmdhl.approved or {}
  local characters,props={},{}
  for id,entry in pairs(approved.assets or {}) do if entry.approvedBy=='workshop' and not W.provided[W.Key('character',id)] then characters[#characters+1]=id end end
  for id,entry in pairs(approved.props or {}) do if entry.approvedBy=='workshop' and not W.provided[W.Key('static',id)] then props[#props+1]=id end end
  if #characters>0 and mmdhl.ForgetPublishedAssets then mmdhl.ForgetPublishedAssets(characters) end
  if #props>0 and mmdhl.props and mmdhl.props.ForgetApproved then mmdhl.props.ForgetApproved(props) end
 end
 net.Receive('mmdhl_workshop_ready',function(_,p)
  if not IsValid(p) or not p:IsListenServerHost() or (p.MMDHLWorkshopReady or 0)>CurTime() then return end
  p.MMDHLWorkshopReady=CurTime()+2 W.Scan()
 end)
end
