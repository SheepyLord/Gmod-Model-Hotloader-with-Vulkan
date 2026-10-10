-- Only approved data is shared. DLLs, Lua and arbitrary filesystem paths are
-- never part of this protocol. Lossless packets use a bounded sliding window.
-- Keep headroom below Source's reliable buffer for gameplay and other addons.
local CHUNK,WINDOW=48*1024,144*1024
local native=mmdhl.native
local L=mmdhl.L
local incoming,outgoing={},{}
local peerKey={} -- the remote server in a client realm
local serial=0
local sharedManifest -- server: memoized, rate-limited manifests
local matchesRequest,retryRequest -- client: the request an offer or refusal answers
local clock=SysTime or RealTime
local function releaseExport(state) if state and state.export then native.ReleaseSharedExport(state.export) state.export=nil end end
local function statistics(state,direction,finished,reason)
 local seconds=math.max(.001,clock()-state.startClock)
 return {protocol=2,asset=state.manifest.asset,started=state.startClock,sampledAt=clock(),direction=direction,finished=finished or false,error=mmdhl.Localize(reason),seconds=seconds,wireBytes=state.wireBytes or 0,rawBytes=state.rawBytes or 0,cachedBytes=state.cachedBytes or 0,generatedBytes=state.generatedBytes or 0,wireMiBps=(state.wireBytes or 0)/1048576/seconds,windowBytes=WINDOW,chunkBytes=CHUNK,phase=state.phase}
end
local function record(state,direction,finished,reason)
 local value=statistics(state,direction,finished,reason) mmdhl.lastTransfer=value
 mmdhl.transferHistory=mmdhl.transferHistory or {} local history=mmdhl.transferHistory history[#history+1]=value
 if #history>32 then table.remove(history,1) end
end
local function key(p) return SERVER and p or peerKey end
local function send(p) if SERVER then net.Send(p) else net.SendToServer() end end
local function message(p,command,token)
 net.Start('mmdhl_share') net.WriteString(command) net.WriteUInt(token or 0,32)
end
local function progress(text,fraction)
 if CLIENT then mmdhl.shareStatus={text=text,progress=fraction,time=RealTime()} hook.Run('MMDHL.ShareProgress',mmdhl.shareStatus) end
end
local function cancel(p,why,notify)
 local k=key(p) local state=incoming[k] or outgoing[k]
 local upload=CLIENT and outgoing[k] and outgoing[k].upload and outgoing[k].manifest.asset
 if incoming[k] and incoming[k].handle then native.CancelSharedFile(incoming[k].handle) end
 if incoming[k] and incoming[k].generating then native.ReleaseSharedExport(incoming[k].generating) end
 if state then record(state,incoming[k] and 'receive' or 'send',false,why) end
 releaseExport(outgoing[k])
 incoming[k]=nil outgoing[k]=nil
 if CLIENT then progress(why or L'share.cancelled',nil) if mmdhl.FinishShareRequest then mmdhl.FinishShareRequest(false,why) end end
 if upload then hook.Run('MMDHL.ShareUploadFailed',upload,why) end
 if notify and state then message(p,'cancel',state.token) net.WriteString(why or L'share.cancelled_short') send(p) end
end
local function offer(p,manifest,upload)
 local k=key(p) if outgoing[k] or incoming[k] then return false,L'share.error.busy' end
 manifest.transport=2
 local bytes=util.Compress(util.TableToJSON(manifest))
 if not bytes or #bytes>60000 then return false,L'share.error.manifest_too_large' end
 serial=(serial+1)%4294967295
 outgoing[k]={token=serial,manifest=manifest,time=RealTime(),started=RealTime(),startClock=clock(),upload=upload,wireBytes=0,rawBytes=0,phase='offered'}
 message(p,upload and 'upload' or 'offer',serial) net.WriteUInt(#bytes,16) net.WriteData(bytes,#bytes) send(p)
 return true
end
local nextFile
local function requestFile(p,state,index)
 state.index=index state.phase='preparing' state.offset=0 state.wireSize=nil
 message(p,'file',state.token) net.WriteUInt(index,16) send(p)
end
local function finish(p,state)
 record(state,'receive',true)
 incoming[key(p)]=nil
 local static=state.manifest.kind=='static'
 if SERVER then
  -- An upload is done once the server has loaded and approved it. The uploader
  -- keeps its transfer until then; checking tells it to wait for the verdict.
  local asset,token=state.manifest.asset,state.token
  local function verdict(err) if not IsValid(p) then return end if err then message(p,'error',token) net.WriteString(err) else message(p,'done',token) end send(p) end
  message(p,'checking',token) send(p)
  if static then mmdhl.props.AcceptUpload(p,asset,function(ok,err) verdict(not ok and (err or L'share.error.invalid_prop') or nil) end) return end
  mmdhl.LoadAsset(asset,function(info,err)
   if not info then verdict(err or L'share.error.invalid_model')
   elseif not mmdhl.ApproveAsset(p,asset,info.name) then verdict(L'share.error.admin_publish_models')
   else verdict() end
  end)
 else
  message(p,'done',state.token) send(p)
  progress(static and L'share.progress.prop_verified' or L'share.progress.model_verified',1)
  if static then mmdhl.props.library.Refresh() mmdhl.props.RefreshRender(state.manifest.asset)
  elseif mmdhl.library then mmdhl.library.Refresh() end
  mmdhl.FinishShareRequest(true)
 end
end
nextFile=function(p,state)
 if state.derivedFallback then finish(p,state) return end
 local files=state.manifest.files
 while state.index<=#files do
  local item=files[state.index]
  local matches,err=native.SharedFileMatches(item.path,item.size,item.sha256)
  if err then cancel(p,err,true) return end
  if matches then state.completed=state.completed+item.size state.cachedBytes=state.cachedBytes+item.size state.index=state.index+1
  elseif CLIENT and item.derive=='source_materials_v5' then state.derivedIndex=state.index state.index=state.index+1
  else requestFile(p,state,state.index) return end
 end
 if state.derivedIndex then
  local handle,err=native.StartSharedMaterialBuild(state.manifest.asset)
  if not handle then state.derivedFallback=true requestFile(p,state,state.derivedIndex) return end
  state.generating=handle state.phase='generating' progress(L'share.progress.preparing_materials',state.completed/math.max(1,state.total))
 else finish(p,state) end
end
local function accept(p,token,manifest)
 if not istable(manifest) or manifest.transport~=2 then return false,L'share.error.version_mismatch' end
 if manifest.version~=1 or not isstring(manifest.asset) or #manifest.asset~=64 or manifest.asset:find('[^a-f0-9]') or not istable(manifest.files) or #manifest.files>65535 then return false,L'share.error.invalid_manifest' end
 -- The file list is a list of records: anything else (a number, holes, extra keys) is refused
 -- before a field is read, and the transfer works on clean copies of them.
 local count=0 for _ in pairs(manifest.files) do count=count+1 end
 if count~=#manifest.files then return false,L'share.error.invalid_manifest' end
 local files={}
 for i=1,count do
  local item=manifest.files[i]
  if not istable(item) or (item.derive~=nil and not isstring(item.derive)) then return false,L'share.error.invalid_file_metadata' end
  files[i]={path=item.path,size=item.size,sha256=item.sha256,derive=item.derive}
 end
 manifest.files=files manifest.name=isstring(manifest.name) and manifest.name or manifest.asset:sub(1,12)
 if manifest.kind=='static' then
  local item=manifest.files[1]
  if #manifest.files~=1 or not istable(item) or item.path~='static/assets/'..manifest.asset..'.gmdl' or item.sha256~=manifest.asset or not isnumber(item.size) or item.size<24 or item.size>268435456 or item.size%1~=0 or item.derive then return false,L'share.error.invalid_prop_manifest' end
  if SERVER and item.size>GetConVar('mmdhl_prop_max_package_mb'):GetInt()*1048576 then return false,L'share.error.prop_too_large' end
  local state={token=token,manifest=manifest,index=1,completed=0,total=item.size,time=RealTime(),started=RealTime(),startClock=clock(),wireBytes=0,rawBytes=0,cachedBytes=0}
  incoming[key(p)]=state nextFile(p,state) return true
 elseif manifest.kind~=nil then return false,L'share.error.unknown_kind' end
 local paths,total={},0
 for _,item in ipairs(manifest.files) do
  if not isstring(item.path) or paths[item.path] or not isnumber(item.size) or item.size<0 or item.size>4294967295 or item.size%1~=0 or not isstring(item.sha256) or #item.sha256~=64 or item.sha256:find('[^a-f0-9]') then return false,L'share.error.invalid_file_metadata' end
  if item.derive and (SERVER or item.derive~='source_materials_v5' or item.path~='assets/'..manifest.asset..'/materials-v5.gma') then return false,L'share.error.invalid_derived_metadata' end
  -- Uploads are raw imported assets; the server generates all carrier binaries.
  if SERVER and not (item.path=='assets/'..manifest.asset..'/manifest.json' or item.path=='assets/'..manifest.asset..'/model.bin' or item.path:match('^textures/[a-f0-9]+%.png$')) then return false,L'share.error.upload_contents' end
  paths[item.path]=true total=total+item.size
 end
 if not paths['assets/'..manifest.asset..'/manifest.json'] or not paths['assets/'..manifest.asset..'/model.bin'] then return false,L'share.error.no_model_data' end
 local state={token=token,manifest=manifest,index=1,completed=0,total=total,time=RealTime(),started=RealTime(),startClock=clock(),wireBytes=0,rawBytes=0,cachedBytes=0}
 incoming[key(p)]=state nextFile(p,state) return true
end
if SERVER then
 util.AddNetworkString('mmdhl_share') util.AddNetworkString('mmdhl_catalog') util.AddNetworkString('mmdhl_catalog_forget')
 -- approved.json: keep every well-formed approval of a damaged or partial file
 -- (props/server.lua keeps props here too) and a copy of the original beside it.
 local function loadApproved()
  local raw=file.Read('mmd_hotloader/approved.json','DATA') local data=raw and util.JSONToTable(raw)
  local out,damaged={version=1,assets={},rigs={},props={}},raw~=nil and not istable(data)
  local function id(v,n) return isstring(v) and #v==n and not v:find('[^a-f0-9]') end
  local function text(v) return v==nil or isstring(v) end
  local function count(v) return v==nil or (isnumber(v) and v>=0 and v<=4294967295 and v%1==0) end
  local valid={assets=function(k,e) return id(k,64) and text(e.name) end,
   rigs=function(k,e) return id(k,32) and id(e.asset,64) and isstring(e.role) and (e.arms==nil or id(e.arms,32)) end,
   props=function(k,e) return id(k,64) and text(e.name) and count(e.triangles) and count(e.bytes) end}
  if istable(data) then
   if data.version~=1 then damaged=true end
   for name,check in pairs(valid) do
    local list=data[name]
    if list~=nil and not istable(list) then damaged=true
    elseif list then for k,e in pairs(list) do if istable(e) and check(k,e) then out[name][k]=e else damaged=true end end end
   end
  end
  if damaged then file.CreateDir('mmd_hotloader') file.Write('mmd_hotloader/approved.invalid.json',raw) end
  return out
 end
 mmdhl.approved=loadApproved()
 local approved=mmdhl.approved
 -- Manifests hash every file they list on this thread. One is kept a minute; a
 -- new one waits a second per player and at most four are made each second.
 local manifests,computed={},{}
 function mmdhl.InvalidateSharedManifests() manifests={} end
 local function unchanged(manifest)
  for _,item in ipairs(manifest.files) do if (file.Size('mmd_hotloader/'..item.path,'DATA') or -1)~=item.size then return false end end
  return true
 end
 sharedManifest=function(p,id,build)
  local now,memo=RealTime(),manifests[id]
  if memo and now<memo.expires and unchanged(memo.manifest) then return memo.manifest end
  while computed[1] and computed[1]<=now-1 do table.remove(computed,1) end
  if now<(p.MMDHLManifestAt or 0) or #computed>=4 then return nil,L'share.error.throttled' end
  p.MMDHLManifestAt=now+1 computed[#computed+1]=now
  local manifest,err=build()
  if istable(manifest) and istable(manifest.files) then manifests[id]={manifest=manifest,expires=now+60} end
  return manifest,err
 end
 -- Clients only add catalog entries; withdrawn approvals are sent separately.
 function mmdhl.SendCatalogWithdrawal(static,ids)
  if game.SinglePlayer() then return end
  for first=1,#ids,128 do
   net.Start('mmdhl_catalog_forget') net.WriteBool(static) net.WriteUInt(math.min(128,#ids-first+1),8)
   for i=first,math.min(first+127,#ids) do net.WriteString(ids[i]) end
   net.Broadcast()
  end
 end
 function mmdhl.CanUseAsset(p,id) return game.SinglePlayer() or approved.assets[id]~=nil end
 function mmdhl.CanEdit(p,ent,property)
  if not IsValid(p) or not mmdhl.IsMMD(ent) then return false end
  if ent:IsPlayer() and ent~=p and not p:IsAdmin() then return false end
  if hook.Run('MMDHLCanEdit',p,ent,property)==false then return false end
  return gamemode.Call('CanProperty',p,property or 'bodygroups',ent)~=false
 end
 local function save() file.CreateDir('mmd_hotloader') file.Write('mmd_hotloader/approved.json',util.TableToJSON(approved,true)) end
 function mmdhl.ForgetPublishedAssets(ids)
  for _,id in ipairs(ids) do
   approved.assets[id]=nil
   for key,entry in pairs(approved.rigs) do if entry.asset==id then approved.rigs[key]=nil end end
   mmdhl.UnregisterAsset(id)
  end
  save() mmdhl.InvalidateSharedManifests() mmdhl.SendCatalogWithdrawal(false,ids)
 end
 -- Clean entries left by earlier deletions in single player or by a listen
 -- server's host, whose library is this cache. Dedicated servers own their
 -- published library independently of a client's local cache.
 if game.SinglePlayer() or not game.IsDedicated() then
  local missing={}
  for id in pairs(approved.assets) do if not file.Exists('mmd_hotloader/assets/'..id..'/manifest.json','DATA') then missing[#missing+1]=id end end
  mmdhl.ForgetPublishedAssets(missing)
 end
 -- Sends what is approved; registers and saves nothing. An NPC or player model replaced
 -- by a newer generator's (mmdhl.NewerActor) stays on the server, out of clients' menus.
 local function catalog(p)
  for id,entry in pairs(approved.assets) do net.Start('mmdhl_catalog') net.WriteString(id) net.WriteString(entry.name or id) if p then net.Send(p) else net.Broadcast() end end
  for _,entry in pairs(mmdhl.actorRegistrations) do if not mmdhl.NewerActor(entry.rig) then mmdhl.SendActorRegistration(entry.rig,entry.arms,p) end end
  if mmdhl.props and mmdhl.props.Catalog then mmdhl.props.Catalog(p) end
 end
 function mmdhl.ApproveAsset(p,id,name)
  if not game.SinglePlayer() and (not IsValid(p) or not p:IsAdmin()) then return false end
  approved.assets[id]={name=name,approvedBy=IsValid(p) and p:SteamID64() or 'console',approvedAt=os.time()}
  save() mmdhl.InvalidateSharedManifests() catalog() return true
 end
 -- Models of a mounted Workshop package are approved by the server owner's choice of content.
 function mmdhl.ApproveWorkshopAsset(id,name,package)
  local entry=approved.assets[id]
  if entry and entry.approvedBy~='workshop' then return end
  if entry and entry.package==package then return end
  approved.assets[id]={name=name~='' and name or nil,approvedBy='workshop',package=package,approvedAt=os.time()}
  save() mmdhl.InvalidateSharedManifests() catalog()
 end
 function mmdhl.PublishRig(rig,arms)
  if not approved.assets[rig.asset] then if not game.SinglePlayer() then return end approved.assets[rig.asset]={name=rig.name,approvedBy='singleplayer',approvedAt=os.time()} end
  local old=approved.rigs[rig.key]
  approved.rigs[rig.key]={asset=rig.asset,role=rig.role,arms=arms and arms.key or (old and old.arms)}
  if arms then approved.rigs[arms.key]={asset=arms.asset,role='arms'} end save()
 end
 mmdhl.SendSharedCatalog=catalog
 hook.Add('InitPostEntity','MMDHL.RestorePublishedActors',function()
  for id,entry in pairs(approved.rigs) do if entry.role~='ragdoll' and entry.role~='arms' and approved.assets[entry.asset] then
   local r=util.JSONToTable(file.Read('mmd_hotloader/rigs/'..id..'/rig.json','DATA') or '')
   local a=entry.arms and util.JSONToTable(file.Read('mmd_hotloader/rigs/'..entry.arms..'/rig.json','DATA') or '')
   if mmdhl.IsLoadableRig(r) and (not entry.arms or mmdhl.IsLoadableRig(a)) and file.Exists('mmd_hotloader/rigs/'..id..'/carrier.gma','DATA') then
    mmdhl.MountPackage('data/mmd_hotloader/rigs/'..id..'/carrier.gma') if a then mmdhl.MountPackage('data/mmd_hotloader/rigs/'..a.key..'/carrier.gma') end
    mmdhl.RegisterActor(r,a)
   end
  end end
 end)
else
 mmdhl.sharedAssets=mmdhl.sharedAssets or {}
 local queue,requests={},{} local current
 function mmdhl.FinishShareRequest(ok,err)
  local request=current current=nil
  if request then requests[request.kind=='static' and 'prop:'..request.id or request.id]=nil for _,cb in ipairs(request.callbacks) do cb(ok,err) end end
 end
 function mmdhl.RequestSharedProp(id,callback)
  if not isstring(id) or #id~=64 or id:find('[^a-f0-9]') then if callback then callback(false,L'share.error.invalid_prop_id') end return end
  if game.SinglePlayer() then if callback then callback(true) end return end
  local r=requests['prop:'..id]
  if not r then r={id=id,kind='static',callbacks={}} requests['prop:'..id]=r queue[#queue+1]=r end
  if callback then r.callbacks[#r.callbacks+1]=callback end
 end
 function mmdhl.PublishProp(id)
  if not IsValid(LocalPlayer()) or not LocalPlayer():IsAdmin() then return false,L'share.error.admin_share_props' end
  local manifest,err=mmdhl.Decode(native.PropSharedManifest(id)) if not manifest then return false,err end
  progress(L('share.progress.sharing',{name=manifest.name}),0) return offer(nil,manifest,true)
 end
 function mmdhl.RequestSharedRig(id,callback)
  if not isstring(id) or (#id~=32 and #id~=64) or id:find('[^a-f0-9]') then if callback then callback(false,L'share.error.invalid_asset_id') end return end
  if game.SinglePlayer() then if callback then callback(true) end return end
  local r=requests[id]
  if not r then r={id=id,callbacks={}} requests[id]=r queue[#queue+1]=r end
  if callback then r.callbacks[#r.callbacks+1]=callback end
 end
 function mmdhl.PublishAsset(id)
  if not IsValid(LocalPlayer()) or not LocalPlayer():IsAdmin() then return false,L'share.error.admin_publish_models' end
  local manifest,err=mmdhl.Decode(native.GetSharedManifest(id,'[]',true)) if not manifest then return false,err end
  progress(L('share.progress.publishing',{name=manifest.name}),0) return offer(nil,manifest,true)
 end
 function mmdhl.CancelSharedTransfer() cancel(nil,L'share.cancelled',true) end
 -- An offer answers the current request only when it carries what was asked
 -- for; any other is the late answer to a request cancelled before it arrived.
 matchesRequest=function(manifest)
  local request=current
  if not request or not istable(manifest) then return false end
  if request.kind=='static' then return manifest.kind=='static' and manifest.asset==request.id end
  if manifest.kind~=nil then return false end
  if #request.id==64 then return manifest.asset==request.id end
  if not istable(manifest.files) then return false end
  for _,item in ipairs(manifest.files) do if istable(item) and item.path=='rigs/'..request.id..'/rig.json' then return true end end
  return false
 end
 -- A request the server refused while busy or rate limited is asked again shortly.
 local transient={[mmdhl.I18n.Token('share.error.busy')]=true,[mmdhl.I18n.Token('share.error.throttled')]=true}
 retryRequest=function(reason)
  local request=current
  if not request or not transient[reason] or (request.retries or 0)>=10 then return false end
  request.retries=(request.retries or 0)+1 request.retryAt=RealTime()+1 current=nil table.insert(queue,1,request)
  return true
 end
 hook.Add('Think','MMDHL.SharedQueue',function()
  if current and RealTime()-current.time>60 and not incoming[peerKey] then cancel(nil,L'share.error.no_package',true) end
  if current or incoming[peerKey] or outgoing[peerKey] or #queue==0 or RealTime()<(queue[1].retryAt or 0) then return end
  current=table.remove(queue,1) current.time=RealTime()
  message(nil,current.kind=='static' and 'request_prop' or 'request',0) net.WriteString(current.id) send() progress(current.kind=='static' and L'share.progress.requesting_prop' or L'share.progress.requesting_model',0)
 end)
 hook.Add('InitPostEntity','MMDHL.SharedCatalog',function() message(nil,'catalog',0) send() end)
 net.Receive('mmdhl_catalog',function()
  local id,name=net.ReadString(),net.ReadString() if #id~=64 or id:find('[^a-f0-9]') then return end
  mmdhl.sharedAssets[id]={id=id,name=name,settings={},shared=true,info={vertices=0}}
  if mmdhl.library then mmdhl.library.Refresh() end
 end)
 -- Approvals the server withdrew (a Workshop package gone, a model forgotten).
 net.Receive('mmdhl_catalog_forget',function()
  local static,ids=net.ReadBool(),{}
  for i=1,net.ReadUInt(8) do ids[i]=net.ReadString() end
  if static then if mmdhl.props and mmdhl.props.library then mmdhl.props.library.Withdraw(ids) end return end
  for _,id in ipairs(ids) do mmdhl.sharedAssets[id]=nil if mmdhl.UnregisterAsset then mmdhl.UnregisterAsset(id) end end
  if mmdhl.library then mmdhl.library.Refresh() end
 end)
end
net.Receive('mmdhl_share',function(_,p)
 local command,token=net.ReadString(),net.ReadUInt(32) local k=key(p)
 local function refuse(why) message(p,'error',0) net.WriteString(why) send(p) end
 if command=='catalog' and SERVER then
  if RealTime()>=(p.MMDHLCatalogAt or 0) then p.MMDHLCatalogAt=RealTime()+5 mmdhl.SendSharedCatalog(p) end return
 end
 -- Requests are refused while this peer has a transfer, before any file is hashed.
 if command=='request' and SERVER then
  local id=net.ReadString() local asset,rigs=id,{}
  if #id==32 then local r=mmdhl.approved.rigs[id] if not r then refuse(L'share.error.rig_not_published') return end asset=r.asset rigs={id} if r.arms then rigs[#rigs+1]=r.arms end end
  if not mmdhl.CanUseAsset(p,asset) then refuse(L'share.error.model_not_approved') return end
  if outgoing[k] or incoming[k] then refuse(L'share.error.busy') return end
  local manifest,err=sharedManifest(p,asset..':'..table.concat(rigs,','),function() return mmdhl.Decode(native.GetSharedManifest(asset,util.TableToJSON(rigs),false)) end)
  if not manifest then refuse(err or L'share.error.model_unavailable') return end
  local ok,e=offer(p,manifest,false) if not ok then refuse(e) end return
 end
 if command=='request_prop' and SERVER then
  local id=net.ReadString() local P=mmdhl.props
  if not P.ValidID(id) or not P.CanUse(p,id) or not native.PropHas(id) then refuse(L'share.error.prop_not_shared') return end
  if outgoing[k] or incoming[k] then refuse(L'share.error.busy') return end
  local manifest,err=sharedManifest(p,'prop:'..id,function() return mmdhl.Decode(native.PropSharedManifest(id)) end)
  if not manifest then refuse(err or L'share.error.prop_unavailable') return end
  P.Load(id,function(info) if info and IsValid(p) then P.SendCollision(p,id) end end)
  local ok,e=offer(p,manifest,false) if not ok then refuse(e) end return
 end
 if command=='upload' or command=='offer' then
  if (SERVER and (command~='upload' or not IsValid(p) or not p:IsAdmin())) or (CLIENT and command~='offer') then return end
  if SERVER and incoming[k] then cancel(p,L'share.error.queue_busy',true) return end
  local size=net.ReadUInt(16) if size>60000 then return end
  local bytes=util.Decompress(net.ReadData(size) or '',16*1024*1024) local manifest=bytes and util.JSONToTable(bytes)
  -- Decline a late offer (its request was cancelled) without touching a transfer.
  if CLIENT and not matchesRequest(manifest) then message(p,'cancel',token) net.WriteString(L'share.cancelled_short') send(p) return end
  if incoming[k] then cancel(p,L'share.error.queue_busy',true) return end
  local ok,err=accept(p,token,manifest) if not ok then message(p,'error',token) net.WriteString(err) send(p) end return
 end
 if command=='file' then
  local state=outgoing[k] if not state or state.token~=token then return end
  local index=net.ReadUInt(16) local item=state.manifest.files[index]
  state.requested=state.requested or {}
  if not item or state.requested[index] or (state.wireSize and state.acked~=state.wireSize) then cancel(p,L'share.error.invalid_file_request',true) return end
  state.requested[index]=true
  releaseExport(state)
  local handle,err=native.StartSharedExport(item.path,item.size,item.sha256)
  if not handle then cancel(p,err,true) return end
  state.export=handle state.index=index state.sent=0 state.acked=0 state.wireSize=nil state.phase='packing' state.time=RealTime() return
 end
 if command=='fileinfo' then
  local state=incoming[k] if not state or state.token~=token then return end
  local index,size=net.ReadUInt(16),net.ReadUInt(32) local item=state.manifest.files[state.index]
  if index~=state.index or state.handle or not item or size<80 or size>item.size+80+8*math.ceil(item.size/1048576) then cancel(p,L'share.error.invalid_compressed_metadata',true) return end
  local handle,err=native.BeginSharedFile(item.path,item.size,item.sha256,size)
  if not handle then cancel(p,err,true) return end
  state.handle=handle state.wireSize=size state.offset=0 state.phase='receiving' state.time=RealTime() return
 end
 if command=='ack' then
  local state=outgoing[k] if not state or state.token~=token then return end
  local index,offset=net.ReadUInt(16),net.ReadUInt(32)
  if index~=state.index then return end
  if offset<state.acked or offset>state.sent then cancel(p,L'share.error.invalid_ack',true) return end
  state.acked=offset state.time=RealTime() return
 end
 if command=='chunk' then
  local state=incoming[k] if not state or state.token~=token then return end
  local index,offset,size=net.ReadUInt(16),net.ReadUInt(32),net.ReadUInt(16)
  if not state.handle or state.committing or index~=state.index or offset~=state.offset or size==0 or size>CHUNK or size>state.wireSize-offset then cancel(p,L'share.error.out_of_order',true) return end
  local bytes=net.ReadData(size)
  if not bytes or #bytes~=size then cancel(p,L'share.error.truncated_chunk',true) return end
  local ok,err=native.AppendSharedChunk(state.handle,offset,bytes)
  if not ok then cancel(p,err,true) return end
  state.offset=offset+size state.time=RealTime() state.wireBytes=state.wireBytes+size local item=state.manifest.files[index]
  message(p,'ack',token) net.WriteUInt(index,16) net.WriteUInt(state.offset,32) send(p)
  local seconds=math.max(.1,RealTime()-state.started)
  progress(L('share.progress.downloading',{name=state.manifest.name,speed=string.format('%.1f',state.wireBytes/1048576/seconds)}),math.min(1,(state.completed+item.size*state.offset/state.wireSize)/math.max(1,state.total)))
  if state.offset==state.wireSize then
   local started,reason=native.CommitSharedFile(state.handle) if not started then cancel(p,reason,true) return end
   state.committing=true state.phase='verifying' progress(L('share.progress.verifying',{name=state.manifest.name}),math.min(1,(state.completed+item.size)/math.max(1,state.total)))
  end return
 end
 if command=='checking' then
  local state=outgoing[k] if CLIENT and state and state.token==token and state.upload then state.phase='checking' state.time=RealTime() progress(L'share.progress.upload_verified',1) end return
 end
 if command=='done' then
  local state=outgoing[k] if state and state.token==token then record(state,'send',true) releaseExport(state) outgoing[k]=nil if CLIENT then progress(L('share.progress.upload_approved',{name=state.manifest.name}),1) end end return
 end
 if command=='cancel' or command=='error' then
  local state=incoming[k] or outgoing[k] local reason=net.ReadString()
  if (state and state.token==token) or (token==0 and not state) then
   if CLIENT and token==0 and retryRequest(reason) then return end
   cancel(p,mmdhl.Localize(reason),false)
  end
 end
end)
-- Compression/decompression runs off-thread. Only short reads, bounded net
-- messages and completion publication run on the engine thread.
hook.Add('Think','MMDHL.TransferPump',function()
 for k,state in pairs(outgoing) do
  local p=SERVER and k or nil
  if state.export then
   if not state.wireSize then
    local size,err=native.GetSharedExportSize(state.export)
    if err then cancel(p,err,true)
    elseif size and size>0 then
     state.wireSize=size state.phase='sending' state.rawBytes=state.rawBytes+state.manifest.files[state.index].size
     message(p,'fileinfo',state.token) net.WriteUInt(state.index,16) net.WriteUInt(size,32) send(p)
    end
   end
   if outgoing[k]==state and state.wireSize then
    for _=1,3 do
     local count=math.min(CHUNK,state.wireSize-state.sent)
     if count<=0 or state.sent-state.acked+count>WINDOW then break end
     local bytes,err=native.ReadSharedExport(state.export,state.sent,count)
     if not bytes then cancel(p,err,true) break end
     message(p,'chunk',state.token) net.WriteUInt(state.index,16) net.WriteUInt(state.sent,32) net.WriteUInt(#bytes,16) net.WriteData(bytes,#bytes) send(p)
     state.sent=state.sent+#bytes state.wireBytes=state.wireBytes+#bytes state.time=RealTime()
    end
    if CLIENT then progress(L('share.progress.publishing',{name=state.manifest.name}),math.min(1,state.sent/math.max(1,state.wireSize))) end
   end
  end
 end
 for k,state in pairs(incoming) do
  local p=SERVER and k or nil
  if state.generating then
   local size,err=native.GetSharedExportSize(state.generating)
   if err or (size and size>0) then
    native.ReleaseSharedExport(state.generating) state.generating=nil
    local item=state.manifest.files[state.derivedIndex]
    local matches=not err and native.SharedFileMatches(item.path,item.size,item.sha256)
    if matches then state.generatedBytes=item.size state.completed=state.completed+item.size finish(p,state)
    else state.derivedFallback=true requestFile(p,state,state.derivedIndex) end
   end
  elseif state.committing then
  local p=SERVER and k or nil
  local done,err=native.PollSharedCommit(state.handle)
  if err then cancel(p,err,true)
  elseif done then
   state.handle=nil state.committing=nil local item=state.manifest.files[state.index]
   state.completed=state.completed+item.size state.rawBytes=state.rawBytes+item.size state.index=state.index+1 state.time=RealTime() nextFile(p,state)
  end
 end end
end)
function mmdhl.GetTransferDiagnostics(p)
 local k=key(p) local state=incoming[k] or outgoing[k]
 return state and statistics(state,incoming[k] and 'receive' or 'send',false) or mmdhl.lastTransfer
end
if CLIENT then
 concommand.Add('mmdhl_share_cancel',mmdhl.CancelSharedTransfer)
 hook.Add('MMDHL.ShareProgress','MMDHL.ShareBanner',function(status)
  if not IsValid(mmdhl.shareBanner) then
   local panel=vgui.Create('DPanel') mmdhl.shareBanner=panel panel:SetSize(560,104)
   panel:ParentToHUD() panel:SetMouseInputEnabled(true)
   panel.Paint=function(self,w,h)
    local s=mmdhl.shareStatus or {} draw.RoundedBox(6,0,0,w,h,Color(23,35,50,238))
    draw.SimpleText(L'share.banner.title','DermaDefaultBold',14,12,color_white)
    draw.SimpleText(s.text or L'share.banner.waiting','DermaDefault',14,37,color_white)
    surface.SetDrawColor(57,73,91) surface.DrawRect(14,72,w-28,10)
    surface.SetDrawColor(67,164,244) surface.DrawRect(14,72,(w-28)*math.Clamp(s.progress or 0,0,1),10)
   end
   local cancel=vgui.Create('DButton',panel) cancel:SetText(L'common.cancel') cancel:SetSize(70,25) cancel:SetPos(476,7) cancel.DoClick=function() mmdhl.CancelSharedTransfer() panel:Remove() end
   panel.Think=function(self) self:SetPos(20,ScrH()-self:GetTall()-30) if RealTime()-(mmdhl.shareStatus.time or 0)>6 and not incoming[peerKey] and not outgoing[peerKey] then self:Remove() end end
  end
 end)
end
hook.Add('Think','MMDHL.TransferTimeout',function()
 for p,state in pairs(incoming) do if RealTime()-state.time>60 then cancel(SERVER and p or nil,L'share.error.timed_out',true) end end
 -- The server may load an upload for up to a minute before it answers.
 for p,state in pairs(outgoing) do if RealTime()-state.time>(state.phase=='checking' and 180 or 60) then cancel(SERVER and p or nil,L'share.error.timed_out',true) end end
end)
if SERVER then hook.Add('PlayerDisconnected','MMDHL.TransferDisconnect',function(p) cancel(p,'Disconnected',false) end) end
