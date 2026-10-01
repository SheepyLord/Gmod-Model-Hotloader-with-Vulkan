-- Library deletion removes generated files and only unreferenced textures; source models are never deleted.
local native=mmdhl.native
local L=mmdhl.L
mmdhl.library=mmdhl.library or {entries={}}
local library=mmdhl.library
local function validId(id) return isstring(id) and #id==64 and not id:find('[^a-f0-9]') end
-- Folders organize the local library only; immutable model files never move.
local folderFile='mmd_hotloader/library/folders.json'
local function folderName(value)
 value=string.Trim(tostring(value or ''))
 if value=='' or value=='.' or value=='..' or value:find('[/\\%z\1-\31\127]') then return nil,L'library.folder.invalid_name' end
 if utf8.len(value)>80 then return nil,L'library.folder.name_too_long' end
 return value
end
local function inFolder(path,root) return path==root or path:sub(1,#root+1)==root..'/' end
function library.Folders()
 local folders=util.JSONToTable(file.Read(folderFile,'DATA') or '') or {}
 local all={}
 local function add(path)
  local current=''
  for segment in tostring(path or ''):gmatch('[^/]+') do
   if not folderName(segment) then return end
   current=current=='' and segment or current..'/'..segment all[current]=true
  end
 end
 for _,path in ipairs(folders) do add(path) end
 for _,entry in pairs(library.entries) do add(entry.settings.folder) end
 local paths=table.GetKeys(all) table.sort(paths) return paths
end
local function saveFolders(paths)
 file.CreateDir('mmd_hotloader/library')
 local json=util.TableToJSON(paths,true) file.Write(folderFile,json)
 return file.Read(folderFile,'DATA')==json
end
local function writeSettings(id,settings)
 file.CreateDir('mmd_hotloader/library') settings.version=1
 local path='mmd_hotloader/library/'..id..'.json' local json=util.TableToJSON(settings,true)
 file.Write(path,json) return file.Read(path,'DATA')==json
end
function library.CreateFolder(name,parent)
 local clean,err=folderName(name) if not clean then return nil,err end
 local paths=library.Folders() local parentExists=parent==nil or parent==''
 for _,p in ipairs(paths) do if p==parent then parentExists=true end end
 if not parentExists then return nil,L'library.folder.parent_missing' end
 local path=parent and parent~='' and parent..'/'..clean or clean
 for _,p in ipairs(paths) do if p:lower()==path:lower() then return nil,L'library.folder.exists' end end
 paths[#paths+1]=path
 if not saveFolders(paths) then return nil,L'library.folder.save_failed_space' end
 library.Refresh() return path
end
function library.MoveToFolder(ids,path)
 local exists=path=='' for _,p in ipairs(library.Folders()) do if p==path then exists=true end end
 if not exists then return false,L'library.folder.missing' end
 for _,id in ipairs(ids) do if not validId(id) or not library.entries[id] then return false,L'library.folder.model_missing' end end
 for _,id in ipairs(ids) do local settings=table.Copy(library.entries[id].settings) settings.folder=path
  if not writeSettings(id,settings) then library.Refresh() return false,L'library.folder.move_failed' end
 end
 library.Refresh() return true
end
function library.RenameFolder(path,name)
 local clean,err=folderName(name) if not clean then return nil,err end
 local parent=path:match('^(.*)/[^/]+$') local target=parent and parent..'/'..clean or clean
 local paths=library.Folders() local found=false
 for _,p in ipairs(paths) do if p==path then found=true elseif p:lower()==target:lower() then return nil,L'library.folder.exists' end end
 if not found then return nil,L'library.folder.missing' end
 for id,entry in pairs(library.entries) do local folder=entry.settings.folder or '' if inFolder(folder,path) then
  local settings=table.Copy(entry.settings) settings.folder=target..folder:sub(#path+1)
  if not writeSettings(id,settings) then library.Refresh() return nil,L'library.folder.model_move_failed' end
 end end
 for i,p in ipairs(paths) do if inFolder(p,path) then paths[i]=target..p:sub(#path+1) end end
 if not saveFolders(paths) then library.Refresh() return nil,L'library.folder.save_failed' end
 library.Refresh() return target
end
function library.RemoveFolder(path)
 local paths={} local ids={}
 for _,p in ipairs(library.Folders()) do if not inFolder(p,path) then paths[#paths+1]=p end end
 for id,entry in pairs(library.entries) do if inFolder(entry.settings.folder or '',path) then ids[#ids+1]=id end end
 local ok,err=library.MoveToFolder(ids,'') if not ok then return false,err end
 if not saveFolders(paths) then return false,L'library.folder.save_failed' end
 library.Refresh() return true
end
function library.Refresh()
 local entries={} local deleting={}
 for _,path in ipairs(util.JSONToTable(file.Read('mmd_hotloader/cleanup.json','DATA') or '') or {}) do local id=path:match('^assets/([a-f0-9]+)') if validId(id) then deleting[id]=true end end
 local sources=util.JSONToTable(file.Read('mmd_hotloader/sources.local.json','DATA') or '') or {}
 local _,dirs=file.Find('mmd_hotloader/assets/*','DATA')
 for _,id in ipairs(dirs or {}) do
  if validId(id) and not deleting[id] then
   local info=util.JSONToTable(file.Read('mmd_hotloader/assets/'..id..'/manifest.json','DATA') or '')
   if info then
    local settings=util.JSONToTable(file.Read('mmd_hotloader/library/'..id..'.json','DATA') or '') or {}
    -- Workshop models the player deleted stay hidden until restored.
    local W=mmdhl.workshop
    if not settings.deleted and not (W and W.IsHidden('character',id)) then entries[id]={id=id,info=info,settings=settings,name=settings.name or info.name or id:sub(1,12),source=(sources[id] or {}).source or '',workshop=W and W.Origin('character',id)} end
   end
  end
 end
 -- A single-player catalog refers to this same cache. It must never resurrect
 -- deleted files as empty "server approved" placeholders.
 if not game.SinglePlayer() then
  for id,entry in pairs(mmdhl.sharedAssets or {}) do if not entries[id] and not deleting[id] and not (mmdhl.workshop and mmdhl.workshop.IsHidden('character',id)) then
   entries[id]=table.Copy(entry) entries[id].source=L'library.server_approved'
   entries[id].settings=util.JSONToTable(file.Read('mmd_hotloader/library/'..id..'.json','DATA') or '') or {}
   entries[id].name=entries[id].settings.name or entries[id].name
  end end
 end
 library.entries=entries library.revision=(library.revision or 0)+1
 hook.Run('MMDHL.LibraryChanged')
 return entries
end
function library.Update(id,changes)
 if not validId(id) or not library.entries[id] then return false,L'library.error.not_in_cache' end
 local settings=table.Copy(library.entries[id].settings)
 for k,v in pairs(changes) do settings[k]=v end settings.version=1
 if not writeSettings(id,settings) then return false,L'library.error.save_failed' end
 library.Refresh() return true
end
function library.Delete(ids,done)
 done=done or function() end
 if library.job or mmdhl.pendingSpawn or library.deleting then done(false,L'library.delete.busy') return end
 local wanted={} for _,id in ipairs(ids) do if not validId(id) or not library.entries[id] then done(false,L'library.delete.not_in_library') return end wanted[id]=true end
 library.deleting=true
 local owner=mmdhl.previewOwner
 if IsValid(owner) and wanted[owner.selected] then native.ClearPreview() owner.previewHandle=nil owner.previewAsset=nil owner.selected=nil mmdhl.previewOwner=nil end
 for _,ent in pairs(mmdhl.editorPreviews or {}) do if IsValid(ent) and wanted[mmdhl.GetAsset(ent)] then ent:Remove() end end
 -- Client-only corpses have no server entity to remove; release them here.
 -- Another addon's copy of a player model only lets go of the model.
 for _,ent in ipairs(mmdhl.Entities()) do if wanted[mmdhl.GetAsset(ent)] then
  if ent.MMDHLCopyOf then mmdhl.ForgetPlayerCopy(ent) elseif not mmdhl.RemoveClientRagdoll(ent) then mmdhl.Action('remove',nil,ent) end
 end end
 local started=RealTime()
 timer.Create('MMDHL.DeleteModels',.1,0,function()
  for _,ent in ipairs(mmdhl.Entities()) do if wanted[mmdhl.GetAsset(ent)] then
   if RealTime()-started>5 then timer.Remove('MMDHL.DeleteModels') library.deleting=nil done(false,L'library.delete.ragdoll_stuck') end
   return
  end end
  timer.Remove('MMDHL.DeleteModels')
  local result,err=mmdhl.Decode(native.DeleteAssets(util.TableToJSON(ids))) library.deleting=nil
  if not result then library.Refresh() done(false,err) return end
  for _,id in ipairs(ids) do
   mmdhl.assets[id]=nil
   if mmdhl.sharedAssets then mmdhl.sharedAssets[id]=nil end
   if game.SinglePlayer() and mmdhl.UnregisterAsset then mmdhl.UnregisterAsset(id) end
   for _,cache in ipairs({'sourceMaterials','depthMaterials','sourceBlendMaterials','shadowMaterials','materials'}) do if mmdhl[cache] then mmdhl[cache][id]=nil end end
  end
  for key,rig in pairs(mmdhl.rigs) do if wanted[rig.asset] then mmdhl.rigs[key]=nil end end
  net.Start('mmdhl_forget_assets') net.WriteString(util.TableToJSON(ids)) net.SendToServer()
  if mmdhl.terms then mmdhl.terms.Forget(ids) end
  if mmdhl.names then mmdhl.names.Forget(ids) end
  library.Refresh()
  local message=L'library.delete.done'
  if result.pendingFiles>0 then message=L('library.delete.done_pending',{count=result.pendingFiles}) end
  done(true,message)
 end)
end
-- Complete physical deletion of any archives Windows held open last session.
local cleanup,cleanupError=mmdhl.Decode(native.DeleteAssets('[]'))
if cleanupError then ErrorNoHalt('[Model Hotloader cleanup] '..cleanupError..'\n') end
-- kind is nil for characters and 'static' for props; both share one worker.
function library.StartImport(handle,err,kind)
 if not handle then library.status=tostring(err or L'library.import.start_failed') hook.Run('MMDHL.ImportChanged') return false end
 library.job=handle library.jobKind=kind library.progress=nil library.filename=nil library.status=L'library.import.opening_picker' hook.Run('MMDHL.ImportChanged') return true
end
-- The model picker is a separate window. A full-screen game covers it and a
-- windowed one may hide it behind other windows, so say that it opened.
local PickerNotice='MMDHL.Picker'
local function pickerNotice(show)
 if not show then if library.pickerNoticed then notification.Kill(PickerNotice) library.pickerNoticed=nil end return end
 if library.pickerNoticed then return end library.pickerNoticed=true
 local text=(system.IsWindowed and not system.IsWindowed()) and L'library.picker.fullscreen' or L'library.picker.windowed'
 notification.AddProgress(PickerNotice,text)
 chat.AddText(Color(120,200,255),'[Model Hotloader] ',color_white,text)
 surface.PlaySound('garrysmod/content_downloaded.wav')
end
function library.BrowseImport(kind)
 -- One model at a time: a pending terms-of-use question comes first.
 if mmdhl.terms and IsValid(mmdhl.terms.pending) then mmdhl.terms.pending:MakePopup() return false end
 library.reimportOf=nil
 local handle,err if kind then handle,err=native.Browse(kind) else handle,err=native.Browse() end
 local ok=library.StartImport(handle,err,kind)
 if ok then pickerNotice(true) end
 return ok
end
-- Reimport a static prop from its recorded source with the current options.
function library.ReimportProp(id)
 if library.job then return false,L'library.import.busy' end
 local handle,err=native.PropReload(id,util.TableToJSON(mmdhl.props.ImportOptions()))
 if not library.StartImport(handle,err,'static') then return false,err end
 library.reimportOf=id library.status=L'library.import.reimporting_prop' return true
end
function library.CancelImport()
 pickerNotice(false)
 if library.job then native.CancelJob(library.job) library.job=nil library.status=L'library.import.cancelled' hook.Run('MMDHL.ImportChanged') end
end
-- Import feedback: explain failures and catch models imported on the wrong side.
local function hints() return {
 {'vrm avatar',L'library.hint.vrm_avatar'},
 {'belong in static props',L'library.hint.static_file'},
 {'humanoid map',L'library.hint.vrm_humanoid'},
 {'external buffer',L'library.hint.vrm_external_buffer'},
 {'vrm file',L'library.hint.vrm_damaged'},
 {'cannot open file',L'library.hint.cannot_open'},
 {'model parser',L'library.hint.parser'},
 {'not a blender file',L'library.hint.not_blend'},
 {'no triangle geometry',L'library.hint.no_triangles'},
 {'selected objects',L'library.hint.blend_objects_changed'},
 {'zstandard',L'library.hint.blend_compressed'},
 {'gzip',L'library.hint.blend_compressed'},
 {'truncated',L'library.hint.truncated'},
 {'blend',L'library.hint.blend_unsupported'},
 {'material limit',L'library.hint.material_limit'},
 {'byte limit',L'library.hint.byte_limit'},
 {'budget',L'library.hint.geometry_budget'},
 {'pmx',L'library.hint.pmx'},
 {'too small',L'library.hint.too_small'},
 {'too large',L'library.hint.too_large'},
 {'recenter',L'library.hint.recenter'},
 {'exited',L'library.hint.importer_exited'},
 {'five minutes',L'library.hint.timeout'},
 {'choose obj',L'library.hint.unsupported_type'},
 {'bone',L'library.hint.skeleton'},
} end
-- Import messages that need no action are notes, not warnings: joints MMD
-- itself ignores (a body linked to itself or two world anchors), large
-- textures (scaled down to 4096), broken numbers the loader repaired, and
-- the generic notices that Source shading approximates glTF or PMX
-- materials. Sorting happens here so models imported earlier follow suit.
local function isNote(text)
 text=tostring(text or '')
 if text:sub(1,14)=='Skipped joint ' then return text:find(': identical body references',1,true)~=nil or text:find(': both endpoints are world anchors',1,true)~=nil end
 return text:sub(1,14)=='Large texture ' or text:sub(1,9)=='Repaired ' or text:sub(1,43)=='Source shading approximates glTF materials;'
  or text:find(': PMX sphere/toon shading is approximated',1,true)~=nil
  or text:sub(1,31)=='Source shading approximates VRM' or text:sub(1,18)=='VRM approximation:'
end
function mmdhl.SplitWarnings(list)
 local warnings,notes={},{}
 for _,text in ipairs(list or {}) do if isNote(text) then notes[#notes+1]=text else warnings[#warnings+1]=text end end
 return warnings,notes
end
function mmdhl.ShowWarnings(name,list)
 local UI=mmdhl.UI local s,f=UI.metrics()
 local warnings,notes=mmdhl.SplitWarnings(list)
 local lines={}
 if #warnings>0 then lines[#lines+1]=L'library.warnings.heading' for _,w in ipairs(warnings) do lines[#lines+1]='• '..w end end
 if #notes>0 then if #lines>0 then lines[#lines+1]='' end lines[#lines+1]=L'library.warnings.notes_heading' for _,n in ipairs(notes) do lines[#lines+1]='• '..n end end
 local frame=vgui.Create('DFrame') frame:SetTitle('') frame:SetSize(math.min(ScrW()-s(40),s(640)),math.Clamp(s(170)+#lines*s(20),s(260),ScrH()-s(80))) frame:Center() frame:MakePopup() frame:DockPadding(s(16),s(14),s(16),s(14))
 frame.btnMinim:SetVisible(false) frame.btnMaxim:SetVisible(false)
 local accent=#warnings>0 and Color(191,120,22) or UI.colors.muted
 frame.Paint=function(_,w,h) draw.RoundedBox(6,0,0,w,h,Color(246,248,251)) draw.RoundedBoxEx(6,0,0,w,s(6),accent,true,true,false,false) end
 local modelName=tostring(name or L'library.warnings.unnamed')
 local title=UI.label(frame,#warnings>0 and L('library.warnings.title_warnings',{name=modelName}) or L('library.warnings.title_notes',{name=modelName}),f.Title,s(34)) title:Dock(TOP)
 local hint=UI.label(frame,#warnings>0 and L'library.warnings.hint_warnings' or L'library.warnings.hint_notes',f.Body,s(24)) hint:Dock(TOP) hint:SetWrap(true) hint:SetAutoStretchVertical(true) hint:SetTextColor(UI.colors.muted)
 local close=UI.button(frame,L'common.close',function() frame:Close() end,s(34),f.Body) close:Dock(BOTTOM) close:DockMargin(0,s(10),0,0)
 local text=frame:Add('DTextEntry') text:Dock(FILL) text:SetMultiline(true) text:SetEditable(false) text:SetVerticalScrollbarEnabled(true) text:SetFont(f.Small) text:SetText(table.concat(lines,'\n')) text:DockMargin(0,s(6),0,0)
 UI.ownScale(frame)
 return frame
end
-- VRM avatars carry machine-readable licences; summarise them for the library
-- and for the sharing prompt. Returns a short and a long description and
-- whether redistribution is allowed (true, false or nil when not stated).
function mmdhl.VrmSummary(info)
 local vrm=istable(info) and info.vrm if not istable(vrm) then return nil end
 local meta=istable(vrm.meta) and vrm.meta or {}
 local vrmLicences={Redistribution_Prohibited=L'library.vrm.redistribution_prohibited',CC0='CC0',CC_BY='CC BY',CC_BY_NC='CC BY-NC',CC_BY_SA='CC BY-SA',CC_BY_NC_SA='CC BY-NC-SA',CC_BY_ND='CC BY-ND',CC_BY_NC_ND='CC BY-NC-ND',Other=L'library.vrm.other_licence'}
 -- The converter writes an absent VRM 0.x licence name as an empty string.
 local licenseName=meta.licenseName~='' and meta.licenseName or nil
 local licence=vrm.version=='0.x' and (vrmLicences[licenseName or ''] or (licenseName and tostring(licenseName)) or L'library.vrm.no_licence') or (meta.allowRedistribution==true and L'library.vrm.redistribution_allowed' or L'library.vrm.redistribution_prohibited')
 local authors=istable(meta.authors) and table.concat(meta.authors,', ') or ''
 local springs=istable(vrm.springBone) and istable(vrm.springBone.joints) and #vrm.springBone.joints or 0
 local colliders=istable(vrm.springBone) and istable(vrm.springBone.colliders) and #vrm.springBone.colliders or 0
 local version=tostring(vrm.version)
 local short='VRM '..version..' · '..licence
 local url=meta.licenseUrl and meta.licenseUrl~='' and meta.licenseUrl
 local long={authors~='' and L('library.vrm.avatar_by',{version=version,authors=authors}) or L('library.vrm.avatar',{version=version}),
  (url and L('library.vrm.licence_url',{licence=licence,url=url}) or L('library.vrm.licence',{licence=licence}))..(meta.otherLicenseUrl and meta.otherLicenseUrl~='' and (' · '..meta.otherLicenseUrl) or ''),
  L('library.vrm.spring_bones',{joints=springs,colliders=colliders})}
 return short,table.concat(long,'\n'),meta.allowRedistribution
end
function mmdhl.ImportHint(err)
 local lower=tostring(err or ''):lower()
 for _,hint in ipairs(hints()) do if lower:find(hint[1],1,true) then return hint[2] end end
 return L'library.hint.default'
end
-- A .blend holds a whole scene: list its meshes first, then import the chosen ones.
local function isBlend(source) return tostring(source or ''):lower():sub(-6)=='.blend' end
function library.StartStaticImport(source,objects)
 local options=mmdhl.props.ImportOptions()
 if objects then options.objects=objects elseif isBlend(source) then options.kind='blend_scene' end
 local handle,err=native.BeginImport(source,util.TableToJSON(options))
 if not library.StartImport(handle,err,'static') then return false,err end
 library.status=options.kind=='blend_scene' and L'library.import.reading_blend' or L'library.import.importing_prop' return true
end
local function startImport(kind,source)
 if kind=='static' then return library.StartStaticImport(source) end
 return library.StartImport(native.BeginImport(source,'{}'))
end
function mmdhl.ShowImportFailure(status,kind)
 local UI=mmdhl.UI if not UI then Derma_Message(tostring(status.error),L'library.failure.title_short',L'common.close') return end
 local s,f=UI.metrics()
 local file=tostring(status.filename or (status.source and string.GetFileFromFilename(status.source)) or L'library.failure.selected_file')
 local hint=status.hint or mmdhl.ImportHint(status.error)
 local unknown=L'library.failure.unknown'
 local details=table.concat({L('library.failure.detail_file',{file=file}),L('library.failure.detail_source',{source=tostring(status.source or unknown)}),kind=='static' and L'library.failure.detail_type_static' or L'library.failure.detail_type_character',L('library.failure.detail_step',{step=tostring(status.stage or unknown)}),L('library.failure.detail_error',{error=tostring(status.error or unknown)}),L('library.failure.detail_time',{time=os.date('%Y-%m-%d %H:%M:%S')})},'\n')
 local frame=vgui.Create('DFrame') frame:SetTitle('') frame:SetSize(s(640),s(380)) frame:Center() frame:MakePopup() frame:DockPadding(s(16),s(16),s(16),s(14)) frame.btnMinim:SetVisible(false) frame.btnMaxim:SetVisible(false)
 frame.Paint=function(_,w,h) draw.RoundedBox(6,0,0,w,h,Color(246,248,251)) draw.RoundedBoxEx(6,0,0,w,s(6),Color(196,62,62),true,true,false,false) end
 local title=UI.label(frame,L('library.failure.title',{file=file}),f.Title,s(34)) title:Dock(TOP) title:SetTextColor(Color(166,38,38))
 local stage=UI.label(frame,L('library.failure.while',{step=tostring(status.stage or L'library.failure.importing'):lower(),error=tostring(status.error or L'library.import.unknown_error')}),f.Body,s(48)) stage:Dock(TOP) stage:SetWrap(true)
 local try=UI.label(frame,L('library.failure.what_to_try',{hint=hint}),f.Body,s(48)) try:Dock(TOP) try:SetWrap(true) try:SetTextColor(UI.colors.muted)
 local buttons=frame:Add('DPanel') buttons:Dock(BOTTOM) buttons:SetTall(s(34)) buttons:SetPaintBackground(false) buttons:DockMargin(0,s(10),0,0)
 local close=UI.button(buttons,L'common.close',function() frame:Close() end,s(34),f.Body) close:Dock(RIGHT) close:SetWide(s(100))
 local copy=UI.button(buttons,L'library.failure.copy_details',function() SetClipboardText(details) notification.AddLegacy(L'library.failure.copied',NOTIFY_GENERIC,4) end,s(34),f.Body) copy:Dock(RIGHT) copy:SetWide(s(130)) copy:DockMargin(0,0,s(8),0)
 if status.source and status.source~='' then
  local extension=status.source:lower():sub(-4)
  -- The worker also recognises VRM avatars saved as .glb; its (English) error names them.
  local pmd=extension=='.pmd' or extension=='.vrm' or tostring(status.error or ''):lower():find('vrm avatar',1,true)~=nil
  local retryKind=(kind=='static' and pmd) and 'library' or kind
  local retry=UI.button(buttons,pmd and kind=='static' and L'library.failure.import_as_character' or L'library.failure.retry',function() frame:Close() if not library.job then startImport(retryKind,status.source) end end,s(34),f.Strong,true)
  retry:Dock(LEFT) retry:SetWide(s(170))
 end
 local text=frame:Add('DTextEntry') text:Dock(FILL) text:SetMultiline(true) text:SetEditable(false) text:SetFont(f.Small) text:SetText(details) text:DockMargin(0,s(6),0,0)
end
-- Object picker for .blend files with several meshes. The ticked meshes become
-- one prop and keep their placement from Blender.
function mmdhl.ShowBlendPicker(status)
 local UI=mmdhl.UI local s,f=UI.metrics()
 if IsValid(library.blendPicker) then library.blendPicker:Remove() end
 local objects=table.Copy((status.scene or {}).objects or {})
 table.sort(objects,function(a,b) local av,bv=not a.hidden and a.in_scene~=false,not b.hidden and b.in_scene~=false if av~=bv then return av end return tostring(a.name)<tostring(b.name) end)
 local file=tostring(status.filename or L'library.blend.default_file')
 local frame=vgui.Create('DFrame') library.blendPicker=frame
 -- Sized to its rows: chrome (title, hint, tools, summary, buttons) plus 54 px per object.
 frame:SetTitle('') frame:SetSize(math.min(ScrW()-s(40),s(640)),math.Clamp(s(262)+#objects*s(54),s(360),ScrH()-s(80))) frame:Center() frame:MakePopup() frame:DockPadding(s(18),s(16),s(18),s(14))
 frame.btnMinim:SetVisible(false) frame.btnMaxim:SetVisible(false)
 frame.Paint=function(_,w,h) draw.RoundedBox(6,0,0,w,h,Color(246,248,251)) draw.RoundedBoxEx(6,0,0,w,s(6),UI.colors.accent,true,true,false,false) end
 local title=UI.label(frame,L'library.blend.title',f.Title,s(34)) title:Dock(TOP)
 local hint=UI.label(frame,L('library.blend.hint',{file=file,count=#objects}),f.Body,s(44)) hint:Dock(TOP) hint:SetWrap(true) hint:SetTextColor(UI.colors.muted)
 local tools=frame:Add('DPanel') tools:Dock(TOP) tools:SetTall(s(30)) tools:SetPaintBackground(false) tools:DockMargin(0,s(8),0,s(8))
 local buttons=frame:Add('DPanel') buttons:Dock(BOTTOM) buttons:SetTall(s(36)) buttons:SetPaintBackground(false) buttons:DockMargin(0,s(10),0,0)
 local summary=UI.label(frame,'',f.Small,s(22)) summary:Dock(BOTTOM) summary:SetTextColor(UI.colors.muted) summary:DockMargin(0,s(8),0,0)
 local list=frame:Add('DScrollPanel') list:Dock(FILL)
 list.Paint=function(_,w,h) draw.RoundedBox(4,0,0,w,h,color_white) end
 list:GetCanvas():DockPadding(s(10),s(8),s(10),s(8))
 local checks,chosen={},false
 local import
 local function update()
  local count,tris=0,0
  for _,c in ipairs(checks) do if c:GetChecked() then count=count+1 tris=tris+(c.Object.triangles or 0) end end
  summary:SetText(L('library.blend.summary',{count=count,total=#checks,triangles=string.Comma(math.floor(tris))}))
  import:SetEnabled(count>0) import:SetText(count>1 and L('library.blend.import_many',{count=count}) or L'library.blend.import_one')
 end
 for _,object in ipairs(objects) do
  local row=list:Add('DPanel') row:Dock(TOP) row:SetTall(s(50)) row:SetPaintBackground(false) row:DockMargin(0,0,0,s(4))
  local check=UI.checkbox(row,tostring(object.name),nil,f.Strong,s(24)) check:Dock(TOP) check.Object=object
  local visible=not object.hidden and object.in_scene~=false
  check:SetValue(visible) check.OnChange=function() update() end
  local details={L('library.blend.triangles',{count=string.Comma(math.floor(object.triangles or 0))})}
  local materials=object.materials or {}
  if #materials>0 then local names=table.concat(materials,', ',1,math.min(#materials,3))..(#materials>3 and '…' or '') details[#details+1]=#materials==1 and L('library.blend.material_one',{name=names}) or L('library.blend.materials',{count=#materials,names=names}) end
  if object.parent and object.parent~='' then details[#details+1]=L('library.blend.child_of',{name=object.parent}) end
  if object.hidden then details[#details+1]=L'library.blend.hidden' elseif object.in_scene==false then details[#details+1]=L'library.blend.not_in_scene' end
  if (object.modifiers or 0)>0 then details[#details+1]=object.modifiers==1 and L('library.blend.modifier_one',{count=object.modifiers}) or L('library.blend.modifiers',{count=object.modifiers}) end
  local detail=UI.label(row,table.concat(details,'  ·  '),f.Small,s(20)) detail:Dock(TOP) detail:DockMargin(s(24),0,0,0) detail:SetTextColor(UI.colors.muted)
  checks[#checks+1]=check
 end
 local function setAll(test) for _,c in ipairs(checks) do c:SetValue(test(c.Object)) end update() end
 local all=UI.button(tools,L'library.blend.select_all',function() setAll(function() return true end) end,s(30),f.Body) all:Dock(LEFT) all:SetWide(s(110))
 local shown=UI.button(tools,L'library.blend.visible',function() setAll(function(o) return not o.hidden and o.in_scene~=false end) end,s(30),f.Body) shown:Dock(LEFT) shown:SetWide(s(160)) shown:DockMargin(s(6),0,0,0)
 local none=UI.button(tools,L'library.blend.none',function() setAll(function() return false end) end,s(30),f.Body) none:Dock(LEFT) none:SetWide(s(80)) none:DockMargin(s(6),0,0,0)
 local cancel=UI.button(buttons,L'common.cancel',function() frame:Close() end,s(36),f.Body) cancel:Dock(RIGHT) cancel:SetWide(s(110))
 import=UI.button(buttons,L'library.blend.import',function()
  local names={} for _,c in ipairs(checks) do if c:GetChecked() then names[#names+1]=c.Object.name end end
  if #names==0 then return end
  if library.job then notification.AddLegacy(L'library.import.busy',NOTIFY_HINT,5) return end
  chosen=true frame:Close() library.StartStaticImport(status.source,names)
 end,s(36),f.Strong,true) import:Dock(FILL) import:DockMargin(0,0,s(8),0)
 frame.OnClose=function() if not chosen and not library.job then library.status=L'library.import.cancelled' hook.Run('MMDHL.ImportChanged') end end
 UI.ownScale(frame) update()
 return frame
end
-- Character imports: a model without a humanoid skeleton cannot fit a ragdoll.
local landmarks={{'頭','head'},{'首','neck'},{'上半身','upper body','upperbody','spine'},{'下半身','lower body','lowerbody','hips','pelvis'},
 {'左腕','arm_l','left arm','leftarm'},{'右腕','arm_r','right arm','rightarm'},{'左ひじ','elbow_l','left elbow'},{'右ひじ','elbow_r','right elbow'},
 {'左足','leg_l','left leg','leftleg'},{'右足','leg_r','right leg','rightleg'},{'左ひざ','knee_l','left knee'},{'右ひざ','knee_r','right knee'}}
function mmdhl.HumanoidLandmarks(info)
 local found=0
 for _,group in ipairs(landmarks) do
  local hit=false
  for _,bone in ipairs(info.boneList or {}) do
   local english=tostring(bone.english or ''):lower()
   for _,name in ipairs(group) do if bone.name==name or english==name then hit=true break end end
   if hit then break end
  end
  if hit then found=found+1 end
 end
 return found,#(info.boneList or {})
end
local function promptStaticInstead(status)
 local info=status.info or {} local found,bones=mmdhl.HumanoidLandmarks(info)
 if found>=6 and bones>=15 then return end
 local name=tostring(info.name or status.filename or L'library.prompt.this_model')
 local bodies=info.rigidBodies or 0
 Derma_Query(bodies==1 and L('library.static_instead.text_one',{name=name,bones=bones,bodies=bodies,found=found}) or L('library.static_instead.text_many',{name=name,bones=bones,bodies=bodies,found=found}),
  L'library.static_instead.title',L'library.static_instead.import',function()
   if not status.source then return end
   library.Delete({status.asset},function() if not library.job then startImport('static',status.source) end end)
  end,L'library.static_instead.keep')
end
local function promptCharacterInstead(status)
 local info=status.info or {} local class=info.classification
 local name=tostring(info.name or status.filename or L'library.prompt.this_model')
 if class and class.kind=='character' then
  Derma_Query(L('library.character_instead.text',{name=name,bones=class.bones or 0,bodies=class.rigidBodies or 0}),
   L'library.character_instead.title',L'library.character_instead.import',function()
    if not status.source then return end
    mmdhl.props.library.Delete({status.asset},function() if not library.job then startImport('library',status.source) end end)
   end,L'library.character_instead.keep')
 elseif info.skeleton and info.skeleton.humanoid then
  Derma_Message(L('library.rigged_prop.text',{name=name,bones=info.skeleton.bones or 0}),L'library.rigged_prop.title',L'common.ok')
 end
end
mmdhl.PromptStaticInstead=promptStaticInstead
mmdhl.PromptCharacterInstead=promptCharacterInstead
hook.Add('Think','MMDHL.LibraryImport',function()
 if not library.job or (library.nextPoll or 0)>RealTime() then return end library.nextPoll=RealTime()+.1
 local status,err=mmdhl.Decode(native.PollJob(library.job))
 if not status then library.job=nil library.status=tostring(err) pickerNotice(false) hook.Run('MMDHL.ImportChanged') return end
 if status.state~='running' or status.stage~='Select model' then pickerNotice(false) end
 library.status=status.state=='failed' and L('library.import.failed',{error=tostring(status.error or L'library.import.unknown_error')}) or status.stage or status.error or status.state
 if status.warning then library.status=library.status..' — '..status.warning end
 if isnumber(status.progress) then library.progress=math.max(library.progress or 0,math.Clamp(status.progress,0,1)) end
 library.filename=status.filename or library.filename
 if status.state=='selected' then
  local source=status.path or status.source local kind=status.kind
  library.job=nil
  local function proceed()
   if library.job then library.status=L'library.import.busy' hook.Run('MMDHL.ImportChanged') return end
   if kind=='static' then library.StartStaticImport(source)
   else library.StartImport(native.BeginImport(source,'{}')) library.status=L'library.import.importing_model' end
   hook.Run('MMDHL.ImportChanged')
  end
  if mmdhl.terms then
   library.status=L'terms.waiting'
   mmdhl.terms.BeforeImport(source,kind,proceed,function() library.reimportOf=nil library.status=L'library.import.cancelled' hook.Run('MMDHL.ImportChanged') end)
  else proceed() end
 elseif status.state=='complete' and status.kind=='blend_scene' then
  library.job=nil library.progress=nil
  local objects=(status.scene or {}).objects or {}
  if #objects==1 then library.StartStaticImport(status.source,{objects[1].name})
  elseif #objects==0 then
   local kinds={} for _,o in ipairs((status.scene or {}).other or {}) do if not table.HasValue(kinds,o.type) then kinds[#kinds+1]=o.type end end
   local failure={state='failed',kind='static',stage=L'library.blend.stage_listing',filename=status.filename,source=status.source,hint=L'library.hint.no_triangles',
    error=#kinds>0 and L('library.blend.no_meshes_kinds',{kinds=table.concat(kinds,', ')}) or L'library.blend.no_meshes'}
   library.status=L('library.import.failed',{error=failure.error}) notification.AddLegacy(L('library.blend.no_meshes_notice',{file=tostring(status.filename)}),NOTIFY_ERROR,8)
   timer.Simple(0,function() mmdhl.ShowImportFailure(failure,'static') end)
  else
   library.status=L('library.blend.choose',{file=tostring(status.filename)})
   timer.Simple(0,function() mmdhl.ShowBlendPicker(status) end)
  end
 elseif status.state=='complete' and status.kind=='static' then
  library.job=nil library.progress=1
  if mmdhl.terms then mmdhl.terms.Imported('static',status.asset,status.source,library.reimportOf) end
  if mmdhl.names then mmdhl.names.Imported(status.asset,status.source,library.reimportOf) end
  local message,warnings=mmdhl.props.library.OnImported(status,library.reimportOf) library.reimportOf=nil
  library.status=message library.lastImportedKind='static' library.lastImported=status.asset
  notification.AddLegacy(message,warnings>0 and NOTIFY_HINT or NOTIFY_GENERIC,8)
  hook.Run('MMDHL.PropImported',status.asset)
  timer.Simple(0,function() promptCharacterInstead(status) end)
 elseif status.state=='complete' then
  library.job=nil
  local preference='mmd_hotloader/library/'..status.asset..'.json' local saved=util.JSONToTable(file.Read(preference,'DATA') or '')
  if saved and saved.deleted then saved.deleted=nil file.Write(preference,util.TableToJSON(saved,true)) end
  if mmdhl.terms then mmdhl.terms.Imported('character',status.asset,status.source) end
  if mmdhl.names then mmdhl.names.Imported(status.asset,status.source) end
  library.Refresh()
  local name=(status.info or {}).name
  library.progress=1 library.lastImportedKind=nil library.lastImported=status.asset library.status=name and L('library.import.imported',{name=name}) or L'library.import.imported_unnamed'
  local warnings=mmdhl.SplitWarnings((status.info or {}).warnings)
  if #warnings>0 then library.status=L('library.import.with_warnings',{message=library.status,count=#warnings}) end
  notification.AddLegacy(library.status,#warnings>0 and NOTIFY_HINT or NOTIFY_GENERIC,8)
  hook.Run('MMDHL.Imported',status.asset)
  timer.Simple(0,function() promptStaticInstead(status) end)
 elseif status.state=='failed' or status.state=='cancelled' then
  library.job=nil library.reimportOf=nil notification.AddLegacy(library.status,status.state=='failed' and NOTIFY_ERROR or NOTIFY_HINT,8)
  local kind=(status.kind=='static' or library.jobKind=='static') and 'static' or 'library'
  if status.state=='failed' then timer.Simple(0,function() mmdhl.ShowImportFailure(status,kind) end) end
 end
 hook.Run('MMDHL.ImportChanged')
end)

-- Placement has a request/acknowledgement lifecycle. Keep errors actionable.
mmdhl.spawnRequests=mmdhl.spawnRequests or {}
function mmdhl.RequestSpawn(id,options,callback)
 if mmdhl.ServerFeatureAvailable then local ok,err=mmdhl.ServerFeatureAvailable('physics') if not ok then err=mmdhl.Localize(err) notification.AddLegacy(err,NOTIFY_ERROR,8) return false,err end end
 if library.deleting then return false,L'library.spawn.deleting' end
 if not validId(id) then return false,L'library.spawn.no_model' end
 if mmdhl.pendingSpawn then return false,L'library.spawn.busy' end

 if util.NetworkStringToID('mmdhl_spawn_status')==0 then return false,L'library.spawn.server_not_ready' end
 mmdhl.spawnSequence=((mmdhl.spawnSequence or 0)+1)%4294967295
 local request=mmdhl.spawnSequence local settings=mmdhl.WithSpawnDefaults(nil,options) settings.request=request
 mmdhl.spawnRequests[request]={callback=callback,started=RealTime(),asset=id}
 mmdhl.pendingSpawn=request
 local ok,err=pcall(mmdhl.Action,'spawn',id,nil,settings)
 if not ok then mmdhl.spawnRequests[request]=nil mmdhl.pendingSpawn=nil return false,tostring(err) end
 return true
end
local function spawnStatus(request,state,message,index)
 local pending=mmdhl.spawnRequests[request]
 mmdhl.lastSpawnStatus={request=request,state=state,message=message,entity=index,asset=pending and pending.asset}
 if state~='loading' then mmdhl.spawnRequests[request]=nil if mmdhl.pendingSpawn==request then mmdhl.pendingSpawn=nil end end
 if pending and pending.callback then pending.callback(state,message,index) end
 if state=='ready' then notification.AddLegacy(message,NOTIFY_GENERIC,5)
 elseif state=='error' then notification.AddLegacy(message,NOTIFY_ERROR,8) end
 hook.Run('MMDHL.SpawnStatus',state,message,index)
end
net.Receive('mmdhl_spawn_status',function() spawnStatus(net.ReadUInt(32),net.ReadString(),mmdhl.Localize(net.ReadString()),net.ReadUInt(16)) end)
hook.Add('Think','MMDHL.SpawnTimeout',function()
 for request,pending in pairs(mmdhl.spawnRequests) do
  if RealTime()-pending.started>75 then spawnStatus(request,'error',L'library.spawn.timeout',0) end
 end
end)
library.Refresh()

-- A persistent HUD indication remains when Q/the library is closed.
hook.Add('HUDPaint','MMDHL.ImportProgress',function()
 if not library.job then return end
 local w,h=math.min(580,ScrW()-40),94 local x,y=20,ScrH()-h-30
 draw.RoundedBox(6,x,y,w,h,Color(23,35,50,238))
 draw.SimpleText(library.filename and L('library.hud.importing_file',{file=library.filename}) or library.jobKind=='static' and L'library.hud.importing_prop' or L'library.hud.importing_model','DermaDefaultBold',x+14,y+12,color_white)
 draw.SimpleText(library.status or L'library.hud.working','DermaDefault',x+14,y+37,Color(208,222,238))
 surface.SetDrawColor(57,73,91) surface.DrawRect(x+14,y+67,w-28,10)
 surface.SetDrawColor(67,164,244)
 if library.progress then surface.DrawRect(x+14,y+67,(w-28)*library.progress,10)
 else surface.DrawRect(x+14+(w-108)*(RealTime()%1),y+67,80,10) end
end)
