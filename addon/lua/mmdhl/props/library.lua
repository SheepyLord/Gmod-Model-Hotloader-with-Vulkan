-- Client library of imported static props. It mirrors the character library's
-- interface (entries, folders, rename, favorites, delete) so the same panel can
-- show either list. Bundles are immutable; only these settings files change.
local P=mmdhl.props
local native=P.native
P.library=P.library or {entries={},approved={}}
local Lib=P.library
local L=mmdhl.L
local root='mmd_hotloader/static/'
local folderFile=root..'library/folders.json'
local function folderName(value)
 value=string.Trim(tostring(value or ''))
 if value=='' or value=='.' or value=='..' or value:find('[/\\%z\1-\31\127]') then return nil,L'props.library.folder_invalid' end
 if utf8.len(value)>80 then return nil,L'props.library.folder_too_long' end
 return value
end
local function inFolder(path,base) return path==base or path:sub(1,#base+1)==base..'/' end
local function writeSettings(id,settings)
 file.CreateDir(root..'library') settings.version=1
 local path=root..'library/'..id..'.json' local json=util.TableToJSON(settings,true)
 file.Write(path,json) return file.Read(path,'DATA')==json
end
local function readSettings(id) return util.JSONToTable(file.Read(root..'library/'..id..'.json','DATA') or '') or {} end
local function saveFolders(paths)
 file.CreateDir(root..'library')
 local json=util.TableToJSON(paths,true) file.Write(folderFile,json)
 return file.Read(folderFile,'DATA')==json
end
function Lib.Folders()
 local all={}
 local function add(path)
  local current=''
  for segment in tostring(path or ''):gmatch('[^/]+') do
   if not folderName(segment) then return end
   current=current=='' and segment or current..'/'..segment all[current]=true
  end
 end
 for _,path in ipairs(util.JSONToTable(file.Read(folderFile,'DATA') or '') or {}) do add(path) end
 for _,entry in pairs(Lib.entries) do add(entry.settings.folder) end
 local paths=table.GetKeys(all) table.sort(paths) return paths
end
function Lib.Refresh()
 local entries={}
 local catalog=util.JSONToTable(native.PropCatalog() or '') or {}
 local sources=util.JSONToTable(file.Read(root..'sources.local.json','DATA') or '') or {}
 for id,info in pairs(catalog) do
  if P.ValidID(id) then
   local settings=readSettings(id)
   local W=mmdhl.workshop
   if not settings.deleted and not (W and W.IsHidden('static',id)) then entries[id]={id=id,info=info,settings=settings,name=settings.name or info.name or id:sub(1,12),source=(sources[id] or {}).source or '',workshop=W and W.Origin('static',id)} end
  end
 end
 -- Server-approved props this client has not downloaded yet.
 if not game.SinglePlayer() then
  for id,shared in pairs(Lib.approved) do if not entries[id] and not (mmdhl.workshop and mmdhl.workshop.IsHidden('static',id)) then
   local settings=readSettings(id)
   entries[id]={id=id,info={name=shared.name,triangles=shared.triangles,warnings={},shared=true},settings=settings,name=settings.name or shared.name,source=L'props.library.server_approved',shared=true}
  end end
 end
 Lib.entries=entries Lib.revision=(Lib.revision or 0)+1
 hook.Run('MMDHL.PropLibraryChanged')
 return entries
end
function Lib.Update(id,changes)
 if not P.ValidID(id) or not Lib.entries[id] then return false,L'props.library.missing_refresh' end
 local settings=table.Copy(Lib.entries[id].settings)
 for k,v in pairs(changes) do settings[k]=v end
 if not writeSettings(id,settings) then return false,L'props.library.save_failed' end
 Lib.Refresh() return true
end
function Lib.CreateFolder(name,parent)
 local clean,err=folderName(name) if not clean then return nil,err end
 local paths=Lib.Folders() local parentExists=parent==nil or parent==''
 for _,p in ipairs(paths) do if p==parent then parentExists=true end end
 if not parentExists then return nil,L'props.library.parent_missing' end
 local path=parent and parent~='' and parent..'/'..clean or clean
 for _,p in ipairs(paths) do if p:lower()==path:lower() then return nil,L'props.library.folder_exists' end end
 paths[#paths+1]=path
 if not saveFolders(paths) then return nil,L'props.library.folders_save_failed_disk' end
 Lib.Refresh() return path
end
function Lib.MoveToFolder(ids,path)
 local exists=path=='' for _,p in ipairs(Lib.Folders()) do if p==path then exists=true end end
 if not exists then return false,L'props.library.folder_missing' end
 for _,id in ipairs(ids) do if not P.ValidID(id) or not Lib.entries[id] then return false,L'props.library.selected_missing' end end
 for _,id in ipairs(ids) do local settings=table.Copy(Lib.entries[id].settings) settings.folder=path
  if not writeSettings(id,settings) then Lib.Refresh() return false,L'props.library.folder_change_failed' end
 end
 Lib.Refresh() return true
end
function Lib.RenameFolder(path,name)
 local clean,err=folderName(name) if not clean then return nil,err end
 local parent=path:match('^(.*)/[^/]+$') local target=parent and parent..'/'..clean or clean
 local paths=Lib.Folders() local found=false
 for _,p in ipairs(paths) do if p==path then found=true elseif p:lower()==target:lower() then return nil,L'props.library.folder_exists' end end
 if not found then return nil,L'props.library.folder_missing' end
 for id,entry in pairs(Lib.entries) do local folder=entry.settings.folder or '' if inFolder(folder,path) then
  local settings=table.Copy(entry.settings) settings.folder=target..folder:sub(#path+1)
  if not writeSettings(id,settings) then Lib.Refresh() return nil,L'props.library.prop_folder_change_failed' end
 end end
 for i,p in ipairs(paths) do if inFolder(p,path) then paths[i]=target..p:sub(#path+1) end end
 if not saveFolders(paths) then Lib.Refresh() return nil,L'props.library.folders_save_failed' end
 Lib.Refresh() return target
end
function Lib.RemoveFolder(path)
 local paths={} local ids={}
 for _,p in ipairs(Lib.Folders()) do if not inFolder(p,path) then paths[#paths+1]=p end end
 for id,entry in pairs(Lib.entries) do if inFolder(entry.settings.folder or '',path) then ids[#ids+1]=id end end
 local ok,err=Lib.MoveToFolder(ids,'') if not ok then return false,err end
 if not saveFolders(paths) then return false,L'props.library.folders_save_failed' end
 Lib.Refresh() return true
end
local function propsUsing(wanted)
 local found={} for _,ent in ipairs(ents.FindByClass('mmdhl_prop')) do if wanted[ent:GetAssetID()] then found[#found+1]=ent end end return found
end
function Lib.Delete(ids,done)
 done=done or function() end
 if mmdhl.library.job or Lib.deleting then done(false,L'props.library.busy') return end
 local wanted={} for _,id in ipairs(ids) do if not P.ValidID(id) or not Lib.entries[id] then done(false,L'props.library.missing') return end wanted[id]=true end
 Lib.deleting=true
 for id in pairs(wanted) do P.Action('remove_asset',id) end
 local started=RealTime()
 timer.Create('MMDHL.DeleteProps',.1,0,function()
  -- In multiplayer, props owned by others stay in the map; only this cache is cleaned.
  if #propsUsing(wanted)>0 and game.SinglePlayer() then
   if RealTime()-started>5 then timer.Remove('MMDHL.DeleteProps') Lib.deleting=nil done(false,L'props.library.remove_from_map_failed') end
   return
  end
  timer.Remove('MMDHL.DeleteProps')
  for id in pairs(wanted) do if P.PreviewAsset==id then P.ReleasePreview() end P.DestroyRender(id) end
  local result,err=mmdhl.Decode(native.PropDelete(util.TableToJSON(ids))) Lib.deleting=nil
  if result and mmdhl.terms then mmdhl.terms.Forget(ids) end
  if result and mmdhl.names then mmdhl.names.Forget(ids) end
  if game.SinglePlayer() then net.Start('mmdhl_prop_forget') net.WriteString(util.TableToJSON(ids)) net.SendToServer() end
  Lib.Refresh()
  if not result then done(false,err) return end
  local message=L'props.library.deleted'
  if (result.pendingFiles or 0)>0 then message=L('props.library.deleted_files_in_use',{count=result.pendingFiles}) end
  done(true,message)
 end)
end
-- Import options apply to the next import and to Reimport.
local options={
 axis=CreateClientConVar('mmdhl_prop_import_axis','auto',true,false,'Up axis for static prop imports: auto, y_up or z_up'),
 collision=CreateClientConVar('mmdhl_prop_import_collision','hull',true,false,'Static prop collision: hull (fast single hull) or balanced (multiple hulls)'),
 scale=CreateClientConVar('mmdhl_prop_import_scale','1',true,false,'Extra scale applied when importing static props',.01,100),
 yaw=CreateClientConVar('mmdhl_prop_import_yaw','0',true,false,'Turn static props when importing (degrees)',-180,180),
}
CreateClientConVar('mmdhl_prop_spawn_frozen','0',true,false,'Freeze newly placed static props',0,1)
-- Shared with the Static Prop tool (its ClientConVars collide, gravity and
-- physprop), so they are user info the server can read.
CreateClientConVar('mmdhl_prop_collide','world',true,true,'What new static props collide with: none, world, noactors, noplayers or all')
CreateClientConVar('mmdhl_prop_gravity','1',true,true,'Gravity for new static props (always off when they collide with nothing)',0,1)
CreateClientConVar('mmdhl_prop_physprop','default',true,true,'Surface material of new static props: default, wood, metal, plastic, rubber, glass, …')
function P.PlacementCollision()
 local mode=GetConVar('mmdhl_prop_collide'):GetString()
 return P.CollisionModeIds[mode] and mode or P.DefaultCollision,GetConVar('mmdhl_prop_gravity'):GetBool()
end
function P.PlacementSurface()
 local surface=GetConVar('mmdhl_prop_physprop'):GetString()
 return P.SurfaceMaterialIds[surface] and surface or P.DefaultSurface
end
function P.ImportOptions()
 local axis=options.axis:GetString() if axis~='y_up' and axis~='z_up' then axis='auto' end
 local collision=options.collision:GetString()=='balanced' and 'balanced' or 'hull'
 local scale=math.Clamp(tonumber(options.scale:GetString()) or 1,.01,100)
 local yaw=math.Clamp(math.Round(tonumber(options.yaw:GetString()) or 0),-180,180)
 return {kind='static',axis=axis,collision=collision,scale=scale,rotation={0,yaw,0}}
end
-- Called by the character library's import poll for completed prop jobs.
function Lib.OnImported(status,previous)
 local id=status.asset
 local saved=readSettings(id)
 if previous and previous~=id and Lib.entries[previous] then
  -- A reimport keeps its library name, folder and favorite.
  local old=Lib.entries[previous].settings
  for _,key in ipairs({'name','folder','favorite','spawn'}) do if saved[key]==nil then saved[key]=old[key] end end
  saved.deleted=nil writeSettings(id,saved)
  local hide=table.Copy(old) hide.deleted=true hide.replacedBy=id writeSettings(previous,hide)
  if game.SinglePlayer() then
   P.Action('replace_asset',previous,nil,{target=id})
   -- Once the map's props use the new import, the old bundle is unreferenced.
   timer.Simple(3,function()
    if #propsUsing({[previous]=true})>0 then return end
    if P.PreviewAsset==previous then P.ReleasePreview() end P.DestroyRender(previous)
    native.PropDelete(util.TableToJSON({previous}))
    net.Start('mmdhl_prop_forget') net.WriteString(util.TableToJSON({previous})) net.SendToServer()
   end)
  end
 elseif saved.deleted then saved.deleted=nil writeSettings(id,saved) end
 -- Part presets remember their original and the selection that made them.
 local derived=(status.info or {}).derived_from
 if derived then
  local pending=Lib.pendingPreset Lib.pendingPreset=nil
  local parent=Lib.entries[derived]
  saved=readSettings(id) saved.parent=derived saved.preset=(status.info.preset or {}).name or (pending and pending.spec.name)
  saved.presetSpec=pending and pending.parent==derived and pending.spec or saved.presetSpec
  if saved.folder==nil and parent then saved.folder=parent.settings.folder end
  if saved.name==nil and parent then saved.name=parent.name..' · '..tostring(saved.preset) end
  saved.deleted=nil writeSettings(id,saved)
 end
 P.RefreshRender(id)
 Lib.Refresh()
 local info=status.info or {}
 local warnings=mmdhl.SplitWarnings(info.warnings)
 local seconds=(status.elapsed_ms or 0)/1000
 local took=seconds<.1 and L'props.import.time_under' or L('props.import.time_seconds',{seconds=string.format('%.1f',seconds)})
 local name,triangles,hulls=tostring(info.name or L'props.import.unnamed'),string.Comma(info.triangles or 0),info.collision_hulls or 1
 local message
 if derived then message=hulls==1 and L('props.import.saved_preset_one_hull',{name=name,triangles=triangles,hulls=hulls,time=took}) or L('props.import.saved_preset',{name=name,triangles=triangles,hulls=hulls,time=took})
 else message=hulls==1 and L('props.import.imported_one_hull',{name=name,triangles=triangles,hulls=hulls,time=took}) or L('props.import.imported',{name=name,triangles=triangles,hulls=hulls,time=took}) end
 if #warnings>0 then message=L('props.import.with_warnings',{message=message,count=#warnings}) end
 return message,#warnings
end
net.Receive('mmdhl_prop_catalog',function()
 for _=1,net.ReadUInt(6) do
  local id=net.ReadString() local name=mmdhl.Localize(net.ReadString()) local triangles=net.ReadUInt(32) local bytes=net.ReadUInt(32)
  if P.ValidID(id) then Lib.approved[id]={name=name,triangles=triangles,bytes=bytes} hook.Run('MMDHL.PropApproved',id) end
 end
 Lib.Refresh()
end)
-- The server withdrew these approvals (sharing.lua receives the message).
function Lib.Withdraw(ids)
 for _,id in ipairs(ids) do Lib.approved[id]=nil end
 Lib.Refresh()
end
-- Placement mirrors the character request/acknowledgement lifecycle.
P.spawnRequests=P.spawnRequests or {}
function P.Action(action,id,ent,settings)
 net.Start('mmdhl_prop_action') net.WriteString(action) net.WriteString(id or '') net.WriteUInt(IsValid(ent) and ent:EntIndex() or 0,16) net.WriteString(util.TableToJSON(settings or {})) net.SendToServer()
end
local function status(request,state,message,index)
 local pending=P.spawnRequests[request]
 if state~='loading' then P.spawnRequests[request]=nil if P.pendingSpawn==request then P.pendingSpawn=nil end end
 if pending and pending.callback then pending.callback(state,message,index) end
 if state=='ready' then notification.AddLegacy(message,NOTIFY_GENERIC,5)
 elseif state=='error' then notification.AddLegacy(message,NOTIFY_ERROR,8) end
 hook.Run('MMDHL.PropSpawnStatus',state,message,index)
end
net.Receive('mmdhl_prop_status',function() status(net.ReadUInt(32),net.ReadString(),mmdhl.Localize(net.ReadString()),net.ReadUInt(16)) end)
function P.RequestSpawn(id,settings,callback)
 if Lib.deleting then return false,L'props.spawn.deleting' end
 if not P.ValidID(id) then return false,L'props.error.select_prop' end
 if P.pendingSpawn then return false,L'props.spawn.busy' end
 if util.NetworkStringToID('mmdhl_prop_status')==0 then return false,L'props.spawn.server_not_ready' end
 P.spawnSequence=((P.spawnSequence or 0)+1)%4294967295
 local request=P.spawnSequence settings=table.Copy(settings or {}) settings.request=request
 P.spawnRequests[request]={callback=callback,started=RealTime(),asset=id}
 P.pendingSpawn=request
 local function send() P.Action('spawn',id,nil,settings) end
 if game.SinglePlayer() or Lib.approved[id] then send() return true end
 -- Multiplayer: an administrator's first placement shares the prop, then places it.
 if not LocalPlayer():IsAdmin() then P.spawnRequests[request]=nil P.pendingSpawn=nil return false,L'props.error.not_shared' end
 local ok,err=mmdhl.PublishProp(id)
 if not ok then P.spawnRequests[request]=nil P.pendingSpawn=nil return false,err end
 P.spawnRequests[request].awaitingApproval=true
 if callback then callback('loading',L'props.spawn.sharing') end
 hook.Add('MMDHL.PropApproved','MMDHL.SpawnAfterShare'..request,function(approvedId)
  if approvedId~=id then return end
  P.CancelSpawnWait(request)
  if P.spawnRequests[request] then P.spawnRequests[request].awaitingApproval=nil P.spawnRequests[request].started=RealTime() send() end
 end)
 -- The server refused the upload, or it was cancelled: nothing will be approved.
 hook.Add('MMDHL.ShareUploadFailed','MMDHL.SpawnAfterShare'..request,function(failedId,why)
  if failedId~=id then return end
  P.CancelSpawnWait(request) status(request,'error',why or L'share.cancelled',0)
 end)
 return true
end
function P.CancelSpawnWait(request)
 hook.Remove('MMDHL.PropApproved','MMDHL.SpawnAfterShare'..request)
 hook.Remove('MMDHL.ShareUploadFailed','MMDHL.SpawnAfterShare'..request)
end
hook.Add('Think','MMDHL.PropSpawnTimeout',function()
 for request,pending in pairs(P.spawnRequests) do
  if RealTime()-pending.started>(pending.awaitingApproval and 600 or 75) then P.CancelSpawnWait(request) status(request,'error',L'props.spawn.timeout',0) end
 end
end)
function P.ResizeAimed(scale)
 local target=LocalPlayer():GetEyeTraceNoCursor().Entity
 if not IsValid(target) or target:GetClass()~='mmdhl_prop' then return false,L'props.error.aim_at_prop' end
 local allowed,why=P.CheckScale(scale,P.Info[target:GetAssetID()]) if not allowed then return false,why end
 P.Action('resize',target:GetAssetID(),target,{scale=scale})
 return true
end
Lib.Refresh()
-- Save a part preset of an imported prop through the worker (see the editor).
function Lib.SavePreset(parent,spec,replaces)
 local library=mmdhl.library
 if library.job then return false,L'props.library.import_busy' end
 local handle,err=native.PropDerive(parent,util.TableToJSON(spec))
 if not library.StartImport(handle,err,'static') then return false,err end
 Lib.pendingPreset={parent=parent,spec=spec} library.reimportOf=replaces library.status=L('props.library.saving_preset',{name=spec.name})
 return true
end
function Lib.Presets(parent)
 local out={} for id,entry in pairs(Lib.entries) do if entry.settings.parent==parent then out[#out+1]=entry end end
 table.sort(out,function(a,b) return tostring(a.settings.preset):lower()<tostring(b.settings.preset):lower() end)
 return out
end
-- One preview at a time keeps its render entry alive and prepares it first.
function P.SetPreview(id)
 if P.PreviewAsset==id then return end
 if P.PreviewAsset then P.ReleaseRender(P.PreviewAsset) end
 P.PreviewAsset=id
 if id then P.AcquireRender(id) end
end
function P.ReleasePreview() P.SetPreview(nil) end
