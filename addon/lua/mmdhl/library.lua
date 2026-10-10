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
-- The fit at import or spawn: {ok=false, missing={keys}} shows the "Needs bones" badge;
-- {ok=true} clears it.
function library.SetFitStatus(id,fit)
 local entry=validId(id) and library.entries[id] if not entry then return false end
 local ok=not istable(fit) or fit.ok~=false
 if ok and entry.settings.fit==nil then return true end
 local settings=table.Copy(entry.settings)
 settings.fit=not ok and {ok=false,missing=istable(fit.missing) and fit.missing or {}} or nil
 if not writeSettings(id,settings) then return false end
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
  -- The game's own server (single player, or the listen server this player
  -- hosts) serves this same cache: it forgets the models too (mmdhl_forget_assets),
  -- or their player models stay in the selector, empty.
  local hostsServer=game.SinglePlayer() or (IsValid(LocalPlayer()) and LocalPlayer():IsListenServerHost())
  for _,id in ipairs(ids) do
   mmdhl.assets[id]=nil
   if mmdhl.sharedAssets then mmdhl.sharedAssets[id]=nil end
   if hostsServer and mmdhl.UnregisterAsset then mmdhl.UnregisterAsset(id) end
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
-- Refusals the native says only in English before a job starts (natives after 2.3.0): a
-- code gives them the player's language in the failure window and the status line.
local startRefusals={{'only models chosen in its file window','start.not_picked'}}
local function startCode(err)
 local lower=tostring(err or ''):lower()
 for _,refusal in ipairs(startRefusals) do if lower:find(refusal[1],1,true) then return refusal[2] end end
end
-- Why an import did not start, in the player's language where the sentence is known, and its code.
function library.StartError(err)
 local code=startCode(err)
 if code then return mmdhl.ImportCause({errorCode=code,error=tostring(err)}),code end
 return tostring(err or L'library.import.start_failed')
end
-- kind is nil for characters and 'static' for props; both share one worker.
function library.StartImport(handle,err,kind)
 if not handle then
  local text,code=library.StartError(err)
  library.status=text hook.Run('MMDHL.ImportChanged')
  -- The importer did not start (missing, blocked, the installation check): say why in the
  -- failure window. A busy importer only needs the status line.
  if err and mmdhl.ShowImportFailure and not tostring(err):find('already running',1,true) then
   local failure={state='failed',error=tostring(err),errorCode=code,stageCode='start',kind=kind}
   timer.Simple(0,function() mmdhl.ShowImportFailure(failure,kind=='static' and 'static' or 'library') end)
  end
  return false
 end
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
 -- One model at a time: an open bone window and a pending terms-of-use question come first.
 local BM=mmdhl.boneMapper
 if BM and IsValid(BM.frame) then BM.frame:MakePopup() library.status=L'bonemap.window_open' hook.Run('MMDHL.ImportChanged') return false end
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
 if not library.StartImport(handle,err,'static') then return false,(library.StartError(err)) end
 library.reimportOf=id library.status=L'library.import.reimporting_prop' return true
end
-- Reload a character from its recorded source (the entity editor's Reload). The new
-- revision keeps the terms record of the one it replaces when the source's readmes cannot
-- be read again (on a server this game does not host, only beside a file picked in this
-- session). Keyed by source: a changed file goes through the bone window first.
library.reloadOf=library.reloadOf or {}
function library.ReloadCharacter(id)
 local handle,err=native.Reload(id)
 if not library.StartImport(handle,err) then return false,(library.StartError(err)) end
 local entry=istable(library.entries) and library.entries[id]
 if entry and isstring(entry.source) and entry.source~='' then library.reloadOf[entry.source]=id end
 return true
end
function library.CancelImport()
 pickerNotice(false)
 if library.job then native.CancelJob(library.job) library.job=nil library.status=L'library.import.cancelled' hook.Run('MMDHL.ImportChanged') end
end
-- Import feedback: explain failures and catch models imported on the wrong side.
-- Natives before 2.3.0 send no error code: the first phrase the English message
-- contains picks the hint, so specific phrases come before general ones.
local function hints() return {
 -- Natives after 2.3.0, on another player's server: a file the player did not choose in the file window.
 {'only models chosen in its file window',L'library.hint.remote_picked'},
 {'vrm avatar',L'library.hint.vrm_avatar'},
 {'spring bone',L'library.hint.vrm_spring'},
 {'invalid/truncated pmx',L'library.hint.pmx'},
 {'import worker missing',L'library.hint.worker_missing'},
 {'cannot start import worker',L'library.hint.worker_start'},
 {'cannot isolate worker',L'library.hint.worker_start'},
 {'belong in static props',istable(mmdhl.boneMapper) and isfunction(mmdhl.boneMapper.Available) and mmdhl.boneMapper.Available('convert') and L'library.hint.character_format' or L'library.hint.static_file'},
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
-- Structured worker errors (errorCode, 2.3.0 natives) choose their hint and their sentence
-- before the message text does; a code of a known family with no entry gets the family's.
local codeHints={['character.format']='library.hint.character_format',['character.blend']='library.hint.character_blend',['character.parse']='library.hint.character_parse',
 ['character.no_skeleton']='library.hint.character_no_skeleton',['character.too_few_bones']='library.hint.character_no_skeleton',['character.too_many_bones']='library.hint.character_too_complex',
 ['character.too_complex']='library.hint.character_too_complex',['character.bone_map']='library.hint.character_bone_map',['character.jiggle']='library.hint.character_jiggle',
 ['character.orientation']='library.hint.character_orientation',['character.needs_mapping']='library.hint.character_needs_mapping',['character.request_version']='library.hint.character_needs_mapping',
 ['pmx.truncated']='library.hint.pmx_truncated',['pmx.section_corrupt']='library.hint.pmx_damaged',['pmx.text']='library.hint.pmx_damaged',['pmx.version']='library.hint.pmx_version',
 ['pmx.materials']='library.hint.pmx_materials',['pmx.number']='library.hint.pmx_number',['pmx.reference']='library.hint.pmx_reference',
 ['format.archive']='library.hint.format_archive',['format.motion']='library.hint.format_motion',['format.image']='library.hint.format_image',['format.renamed']='library.hint.format_renamed',['format.unknown']='library.hint.format_unknown',
 ['vrm.truncated']='library.hint.truncated',['vrm.humanoid']='library.hint.vrm_humanoid',['vrm.external']='library.hint.vrm_external_buffer',['spring.data']='library.hint.vrm_spring',
 ['io.missing']='library.hint.io_missing',['io.locked']='library.hint.io_locked',['io.denied']='library.hint.io_denied',['io.device']='library.hint.io_device',['io.read']='library.hint.cannot_open',['io.empty']='library.hint.io_empty',
 ['io.write']='library.hint.io_write',['io.filesystem']='library.hint.io_write',['io.disk_full']='library.hint.io_disk_full',['memory']='library.hint.memory',['worker.crash']='library.hint.worker_crash',['texture.derivative']='library.hint.texture_derivative',
 ['start.not_picked']='library.hint.remote_picked'}
local familyHints={pmx='library.hint.pmx',vrm='library.hint.vrm_damaged',spring='library.hint.vrm_spring',io='library.hint.cannot_open',format='library.hint.format_unknown',character='library.hint.character_parse'}
-- i18n-keys: library.hint.character_format library.hint.character_blend library.hint.character_parse library.hint.character_no_skeleton library.hint.character_too_complex
-- i18n-keys: library.hint.character_bone_map library.hint.character_jiggle library.hint.character_orientation library.hint.character_needs_mapping
-- i18n-keys: library.hint.pmx_truncated library.hint.pmx_damaged library.hint.pmx_version library.hint.pmx_materials library.hint.pmx_number library.hint.pmx_reference library.hint.pmx
-- i18n-keys: library.hint.format_archive library.hint.format_motion library.hint.format_image library.hint.format_renamed library.hint.format_unknown
-- i18n-keys: library.hint.truncated library.hint.vrm_humanoid library.hint.vrm_external_buffer library.hint.vrm_spring library.hint.vrm_damaged
-- i18n-keys: library.hint.io_missing library.hint.io_locked library.hint.io_denied library.hint.io_device library.hint.cannot_open library.hint.io_empty library.hint.io_write library.hint.io_disk_full
-- i18n-keys: library.hint.memory library.hint.worker_crash library.hint.texture_derivative library.hint.remote_picked
local function codeEntry(map,families,code)
 if not isstring(code) then return nil end
 return map[code] or (families and families[code:match('^([%w_]+)%.') or ''])
end
-- details: the error's errorDetails; their extension fills the renamed-file hint. stage: the
-- stageCode; a crash in the file picker or before the first step is not the file's fault.
local crashHints={pick='library.hint.picker_crash',start='library.hint.worker_start'}
-- i18n-keys: library.hint.picker_crash library.hint.worker_start
function mmdhl.ImportHint(err,code,details,stage)
 if code=='worker.crash' and crashHints[stage] then return L(crashHints[stage]) end
 local key=codeEntry(codeHints,familyHints,code)
 if key then return L(key,{extension=istable(details) and isstring(details.extension) and details.extension or '.fbx'}) end
 local lower=tostring(err or ''):lower()
 for _,hint in ipairs(hints()) do if lower:find(hint[1],1,true) then return hint[2] end end
 return L'library.hint.default'
end
-- The failure in one sentence in the player's language; the importer's own (English)
-- sentence, which names the exact element, stays in the details.
local codeCauses={['pmx.truncated']='library.cause.truncated',['vrm.truncated']='library.cause.truncated',['pmx.section_corrupt']='library.cause.pmx_damaged',['pmx.text']='library.cause.pmx_text',
 ['pmx.version']='library.cause.pmx_version',['pmx.materials']='library.cause.pmx_materials',['pmx.number']='library.cause.pmx_number',['pmx.reference']='library.cause.pmx_reference',
 ['format.archive']='library.cause.format_archive',['format.motion']='library.cause.format_motion',['format.image']='library.cause.format_image',['format.renamed']='library.cause.format_renamed',['format.unknown']='library.cause.format_unknown',
 ['vrm.json']='library.cause.vrm_damaged',['vrm.data']='library.cause.vrm_damaged',['vrm.container']='library.cause.vrm_damaged',['vrm.image']='library.cause.vrm_damaged',['vrm.no_skeleton']='library.cause.vrm_damaged',
 ['vrm.no_geometry']='library.cause.vrm_damaged',['vrm.error']='library.cause.vrm_damaged',['vrm.humanoid']='library.cause.vrm_humanoid',['vrm.external']='library.cause.vrm_external',['spring.data']='library.cause.spring',
 ['io.missing']='library.cause.io_missing',['io.locked']='library.cause.io_locked',['io.denied']='library.cause.io_denied',['io.device']='library.cause.io_device',['io.read']='library.cause.io_read',['io.empty']='library.cause.io_empty',
 ['io.write']='library.cause.io_write',['io.filesystem']='library.cause.io_write',['io.disk_full']='library.cause.io_disk_full',['memory']='library.cause.memory',['worker.crash']='library.cause.worker_crash',['texture.derivative']='library.cause.texture',
 ['start.not_picked']='library.cause.not_picked'}
-- i18n-keys: library.cause.truncated library.cause.pmx_damaged library.cause.pmx_text library.cause.pmx_version library.cause.pmx_materials library.cause.pmx_number library.cause.pmx_reference
-- i18n-keys: library.cause.format_archive library.cause.format_motion library.cause.format_image library.cause.format_renamed library.cause.format_unknown
-- i18n-keys: library.cause.vrm_damaged library.cause.vrm_humanoid library.cause.vrm_external library.cause.spring
-- i18n-keys: library.cause.io_missing library.cause.io_locked library.cause.io_denied library.cause.io_device library.cause.io_read library.cause.io_empty library.cause.io_write library.cause.io_disk_full
-- i18n-keys: library.cause.memory library.cause.worker_crash library.cause.texture library.cause.not_picked
function mmdhl.ImportCause(status)
 local key=codeEntry(codeCauses,nil,status.errorCode)
 if not key then return tostring(status.error or L'library.import.unknown_error') end
 local d=istable(status.errorDetails) and status.errorDetails or {}
 return L(key,{bone=tostring(d.bone or '?'),code=tostring(d.exitCodeHex or status.exitCode or '?')})
end
-- The step, for "While {step}: …": by stageCode in the player's language, else the
-- importer's own name for it (older natives, static props).
local stages={read=true,parse=true,convert_vrm=true,convert_character=true,probe=true,textures=true,cache=true,materials=true,fit=true,start=true,worker=true,pick=true}
-- i18n-keys: library.stage.read library.stage.parse library.stage.convert_vrm library.stage.convert_character library.stage.probe library.stage.textures
-- i18n-keys: library.stage.cache library.stage.materials library.stage.fit library.stage.start library.stage.worker library.stage.pick
function mmdhl.ImportStage(status)
 if stages[status.stageCode] then return L('library.stage.'..status.stageCode) end
 return tostring(status.stage or L'library.failure.importing'):lower()
end
-- Where it went wrong: the places the importer names (bone 12 “左足” › …), the value, the
-- byte where reading stopped; else its English context, else its last progress line.
local kinds={vertex=true,triangle=true,material=true,texture=true,bone=true,ik=true,morph=true,display_frame=true,rigid_body=true,joint=true,soft_body=true,header=true,text=true,
 mesh=true,primitive=true,accessor=true,buffer_view=true,buffer=true,node=true,skin=true,image=true,humanoid_bone=true,spring=true,spring_joint=true,collider=true,collider_group=true}
-- i18n-keys: library.element.vertex library.element.triangle library.element.material library.element.texture library.element.bone library.element.ik library.element.morph
-- i18n-keys: library.element.display_frame library.element.rigid_body library.element.joint library.element.soft_body library.element.header library.element.text
-- i18n-keys: library.element.mesh library.element.primitive library.element.accessor library.element.buffer_view library.element.buffer library.element.node library.element.skin
-- i18n-keys: library.element.image library.element.humanoid_bone library.element.spring library.element.spring_joint library.element.collider library.element.collider_group
local function placeText(p)
 local kind=tostring(p.kind or '') local noun=kinds[kind] and L('library.element.'..kind) or kind:gsub('_',' ')
 local name=isstring(p.name) and p.name~='' and p.name local index=tonumber(p.index)
 if index and name then return L('library.where.item_named',{kind=noun,index=index,name=name}) end
 if index then return L('library.where.item',{kind=noun,index=index}) end
 if name then return L('library.where.named',{kind=noun,name=name}) end
 return noun
end
function mmdhl.ImportWhere(status)
 local d=istable(status.errorDetails) and status.errorDetails or {} local parts={}
 if istable(d.where) and #d.where>0 then for _,p in ipairs(d.where) do if istable(p) then parts[#parts+1]=placeText(p) end end
 elseif istable(status.context) then for _,c in ipairs(status.context) do parts[#parts+1]=tostring(c) end end
 if istable(d.after) then parts[#parts+1]=L('library.where.after',{place=placeText(d.after)}) end
 if isstring(d.field) and d.field~='' then parts[#parts+1]=d.value~=nil and L('library.where.value',{field=d.field,value=tostring(d.value)}) or d.field end
 if tonumber(d.offset) and tonumber(d.size) then parts[#parts+1]=L('library.where.offset',{offset=string.Comma(tonumber(d.offset)),size=string.Comma(tonumber(d.size))}) end
 if isstring(d.path) and d.path~='' then parts[#parts+1]=d.path end
 if isstring(status.detail) and status.detail~='' and (#parts==0 or status.errorCode=='worker.crash' or status.errorCode=='memory') then parts[#parts+1]=L('library.where.progress',{detail=status.detail}) end
 if #parts==0 then return nil end
 return table.concat(parts,' › ')
end
-- Everything a problem report needs, as the Copy details button copies it.
-- i18n-keys: library.failure.detail_code library.failure.detail_context library.failure.detail_data library.failure.detail_file library.failure.detail_source
-- i18n-keys: library.failure.detail_step library.failure.detail_progress library.failure.detail_exit library.failure.detail_build library.failure.detail_elapsed library.failure.detail_time
function mmdhl.ImportFailureDetails(status,kind)
 local unknown=L'library.failure.unknown'
 local file=tostring(status.filename or (status.source and string.GetFileFromFilename(status.source)) or L'library.failure.selected_file')
 local lines={L('library.failure.detail_error',{error=tostring(status.error or unknown)})}
 local function add(key,vars) lines[#lines+1]=L(key,vars) end
 if isstring(status.errorCode) and status.errorCode~='' then add('library.failure.detail_code',{code=status.errorCode}) end
 if istable(status.context) and #status.context>0 then local c={} for _,v in ipairs(status.context) do c[#c+1]=tostring(v) end add('library.failure.detail_context',{context=table.concat(c,' › ')}) end
 if istable(status.errorDetails) and next(status.errorDetails)~=nil then add('library.failure.detail_data',{data=util.TableToJSON(status.errorDetails)}) end
 add('library.failure.detail_file',{file=file}) add('library.failure.detail_source',{source=tostring(status.source or unknown)})
 lines[#lines+1]=kind=='static' and L'library.failure.detail_type_static' or L'library.failure.detail_type_character'
 add('library.failure.detail_step',{step=tostring(status.stage or unknown)..(isstring(status.stageCode) and ' ['..status.stageCode..']' or '')})
 if isstring(status.detail) and status.detail~='' then add('library.failure.detail_progress',{detail=status.detail}) end
 if status.exitCode~=nil then local d=istable(status.errorDetails) and status.errorDetails or {}
  add('library.failure.detail_exit',{code=tostring(d.exitCodeHex or status.exitCode),cause=tostring(d.cause or status.exceptionType or unknown)}) end
 if istable(status.worker) then add('library.failure.detail_build',{release=tostring(status.worker.release or unknown),build=tostring(status.worker.build or unknown)}) end
 if tonumber(status.elapsed_ms) then add('library.failure.detail_elapsed',{seconds=string.format('%.1f',tonumber(status.elapsed_ms)/1000)}) end
 add('library.failure.detail_time',{time=os.date('%Y-%m-%d %H:%M:%S')})
 if isstring(status.log) and status.log~='' then lines[#lines+1]=L'library.failure.detail_log' lines[#lines+1]=status.log end
 return table.concat(lines,'\n')
end
-- A .blend holds a whole scene: list its meshes first, then import the chosen ones.
local function isBlend(source) return tostring(source or ''):lower():sub(-6)=='.blend' end
-- The file types the static prop importer reads (DAE characters are not among them).
local staticTypes={obj=true,fbx=true,glb=true,gltf=true,pmx=true,blend=true}
function library.StaticImportable(source) return staticTypes[tostring(source or ''):lower():match('%.(%w+)$') or '']==true end
function library.StartStaticImport(source,objects)
 local options=mmdhl.props.ImportOptions()
 if objects then options.objects=objects elseif isBlend(source) then options.kind='blend_scene' end
 local handle,err=native.BeginImport(source,util.TableToJSON(options))
 if not library.StartImport(handle,err,'static') then return false,err end
 library.status=options.kind=='blend_scene' and L'library.import.reading_blend' or L'library.import.importing_prop' return true
end
local function startImport(kind,source)
 if kind=='static' then return library.StartStaticImport(source) end
 -- The bone window may be missing or partly loaded (its files did not arrive): imports work without it.
 local BM=mmdhl.boneMapper
 if istable(BM) and isfunction(BM.Convertible) and isfunction(BM.Probe) and BM.Convertible(source) then return BM.Probe(source) end
 return library.StartImport(native.BeginImport(source,'{}'))
end
-- Wrapped text height in a label this wide: each line's measured width wraps, plus breaks.
local function textHeight(text,font,width)
 surface.SetFont(font) local total=0
 for line in (tostring(text)..'\n'):gmatch('([^\n]*)\n') do local w,h=surface.GetTextSize(line=='' and ' ' or line) total=total+h*math.max(1,math.ceil(w/math.max(1,width))) end
 return total
end
function mmdhl.ShowImportFailure(status,kind)
 local UI=mmdhl.UI if not UI then Derma_Message(tostring(status.error),L'library.failure.title_short',L'common.close') return end
 local s,f=UI.metrics()
 local file=tostring(status.filename or (status.source and string.GetFileFromFilename(status.source)) or L'library.failure.selected_file')
 local hint=status.hint or mmdhl.ImportHint(status.error,status.errorCode,status.errorDetails,status.stageCode)
 local details=mmdhl.ImportFailureDetails(status,kind)
 local where=mmdhl.ImportWhere(status)
 -- What failed (and while doing what), where in the file, and what to try.
 local lines={{L('library.failure.title',{file=file}),f.Title,Color(166,38,38)},{L('library.failure.while',{step=mmdhl.ImportStage(status),error=mmdhl.ImportCause(status)}),f.Body,UI.colors.ink}}
 if where then lines[#lines+1]={L('library.failure.where',{where=where}),f.Body,UI.colors.ink} end
 lines[#lines+1]={L('library.failure.what_to_try',{hint=hint}),f.Body,UI.colors.muted}
 -- The window grows with its text (long names, longer translations) and keeps room for the details.
 local wide=math.min(ScrW()-s(40),s(680)) local tall=s(16)+s(10)+s(34)+s(14)+s(150)
 for _,line in ipairs(lines) do line.tall=textHeight(line[1],line[2],wide-s(36)) tall=tall+line.tall+s(6) end
 local frame=vgui.Create('DFrame') frame:SetTitle('') frame:SetSize(wide,math.Clamp(tall,s(380),math.max(s(380),ScrH()-s(60)))) frame:Center() frame:MakePopup() frame:DockPadding(s(16),s(16),s(16),s(14)) frame.btnMinim:SetVisible(false) frame.btnMaxim:SetVisible(false)
 frame.Paint=function(_,w,h) draw.RoundedBox(6,0,0,w,h,Color(246,248,251)) draw.RoundedBoxEx(6,0,0,w,s(6),Color(196,62,62),true,true,false,false) end
 for _,line in ipairs(lines) do local label=UI.label(frame,line[1],line[2],line.tall) label:Dock(TOP) label:SetWrap(true) label:SetAutoStretchVertical(true) label:SetTextColor(line[3]) label:DockMargin(0,0,0,s(6)) end
 local buttons=frame:Add('DPanel') buttons:Dock(BOTTOM) buttons:SetTall(s(34)) buttons:SetPaintBackground(false) buttons:DockMargin(0,s(10),0,0)
 local close=UI.button(buttons,L'common.close',function() frame:Close() end,s(34),f.Body) close:Dock(RIGHT) close:SetWide(s(100))
 local copy=UI.button(buttons,L'library.failure.copy_details',function() SetClipboardText(details) notification.AddLegacy(L'library.failure.copied',NOTIFY_GENERIC,4) end,s(34),f.Body) copy:Dock(RIGHT) copy:SetWide(s(130)) copy:DockMargin(0,0,s(8),0)
 if status.source and status.source~='' then
  local extension=status.source:lower():sub(-4)
  -- The worker also recognises VRM avatars saved as .glb; its (English) error names them.
  local pmd=extension=='.pmd' or extension=='.vrm' or tostring(status.error or ''):lower():find('vrm avatar',1,true)~=nil
  local retryKind=(kind=='static' and pmd) and 'library' or kind
  -- A file without a usable skeleton can still be a static prop.
  local asProp=(status.errorCode=='character.no_skeleton' or status.errorCode=='character.too_few_bones') and library.StaticImportable(status.source)
  if asProp then retryKind='static' end
  local retry=UI.button(buttons,asProp and L'library.failure.import_as_prop' or pmd and kind=='static' and L'library.failure.import_as_character' or L'library.failure.retry',function() frame:Close() if not library.job then startImport(retryKind,status.source) end end,s(34),f.Strong,true)
  retry:Dock(LEFT) retry:SetWide(asProp and s(200) or s(170))
  -- A failed conversion goes back to the bone window with the player's choices.
  local BM=mmdhl.boneMapper local session=BM and BM.sessions and BM.sessions[status.source]
  if session then
   local back=UI.button(buttons,L'library.failure.back_to_bones',function() frame:Close() if not IsValid(BM.frame) and isfunction(BM.ShowWindow) then BM.ShowWindow(session,{}) end end,s(34),f.Body)
   back:Dock(LEFT) back:SetWide(s(200)) back:DockMargin(s(8),0,0,0)
  end
 end
 local text=frame:Add('DTextEntry') text:Dock(FILL) text:SetMultiline(true) text:SetEditable(false) text:SetFont(f.Small) text:SetText(details) text:DockMargin(0,s(6),0,0)
end
-- After a character import whose ragdoll fit failed (status.fit, 2.3.0 natives). The bone
-- window's rescue prompt explains missing parts when it can assign them; otherwise this
-- window lists them (or says why the fit failed), so the player learns it before spawning.
-- unchecked: the rescue prompt's own check (the cached model, the server's pins) failed,
-- so this window explains it after all.
function mmdhl.ExplainFit(status,unchecked)
 local fit=istable(status) and status.fit
 if not istable(fit) or fit.ok~=false or not isstring(status.asset) then return false end
 -- A model that does not look like a character gets the static-prop question instead.
 local found,bones=mmdhl.HumanoidLandmarks(status.info or {}) if found<6 or bones<15 then return false end
 local BM=mmdhl.boneMapper
 if not unchecked and fit.errorCode=='fit.landmarks' and BM and BM.AfterImport and BM.Available and BM.Available('fit') and isfunction(mmdhl.OpenBoneMapper) then return false end
 mmdhl.ShowFitFailure(status) return true
end
function mmdhl.ShowFitFailure(status)
 local UI=mmdhl.UI if not UI then return end local s,f=UI.metrics()
 local fit=status.fit local d=istable(fit.errorDetails) and fit.errorDetails or {}
 local missing=istable(fit.missing) and #fit.missing>0 and fit.missing or istable(d.missing) and d.missing or {}
 local name=tostring((status.info or {}).name or status.filename or L'library.prompt.this_model')
 local BM=mmdhl.boneMapper local parts={}
 for i,key in ipairs(missing) do parts[i]=BM and BM.PartLabel and BM.PartLabel(key) or tostring(key):gsub('^ValveBiped%.Bip01_','') end
 -- Assigning bones fixes missing parts only, and needs the bone window (feature-detected)
 -- with this binary's fit functions; without them it would only ask for an update.
 local assign=#missing>0 and isfunction(mmdhl.OpenBoneMapper) and (not BM or not BM.Available or BM.Available('fit'))
 local lines={{L'library.fit_failed.title',f.Title,UI.colors.ink},
  {#missing>0 and L('library.fit_failed.missing',{name=name,parts=table.concat(parts,', ')}) or L('library.fit_failed.reason',{name=name,reason=tostring(fit.error or L'library.import.unknown_error')}),f.Body,UI.colors.ink},
  {assign and L'library.fit_failed.assign_hint' or #missing>0 and L'library.fit_failed.rename_hint' or L'library.fit_failed.other_hint',f.Body,UI.colors.muted}}
 local wide=math.min(ScrW()-s(40),s(600)) local tall=s(16)+s(10)+s(36)+s(14)
 for _,line in ipairs(lines) do line.tall=textHeight(line[1],line[2],wide-s(36)) tall=tall+line.tall+s(8) end
 if IsValid(library.fitPrompt) then library.fitPrompt:Remove() end
 local frame=vgui.Create('DFrame') library.fitPrompt=frame
 frame:SetTitle('') frame:SetSize(wide,math.Clamp(tall,s(220),math.max(s(220),ScrH()-s(60)))) frame:Center() frame:MakePopup() frame:DockPadding(s(16),s(16),s(16),s(14))
 frame.btnMinim:SetVisible(false) frame.btnMaxim:SetVisible(false)
 frame.Paint=function(_,w,h) draw.RoundedBox(6,0,0,w,h,Color(246,248,251)) draw.RoundedBoxEx(6,0,0,w,s(6),Color(191,120,22),true,true,false,false) end
 local buttons=frame:Add('DPanel') buttons:Dock(BOTTOM) buttons:SetTall(s(36)) buttons:SetPaintBackground(false) buttons:DockMargin(0,s(10),0,0)
 local keep=UI.button(buttons,L'library.fit_failed.keep',function() frame:Close() end,s(36),f.Body) keep:Dock(RIGHT) surface.SetFont(f.Body) keep:SetWide(math.max(s(130),surface.GetTextSize(L'library.fit_failed.keep')+s(28)))
 if assign then
  local open=UI.button(buttons,L'library.fit_failed.assign',function() frame:Close() mmdhl.OpenBoneMapper({asset=status.asset,source=status.source,fit=fit}) end,s(36),f.Strong,true)
  open:Dock(RIGHT) surface.SetFont(f.Strong) open:SetWide(math.max(s(170),surface.GetTextSize(L'library.fit_failed.assign')+s(28))) open:DockMargin(0,0,s(8),0)
 end
 for _,line in ipairs(lines) do local label=UI.label(frame,line[1],line[2],line.tall) label:Dock(TOP) label:SetWrap(true) label:SetAutoStretchVertical(true) label:SetTextColor(line[3]) label:DockMargin(0,0,0,s(8)) end
 UI.ownScale(frame)
 return frame
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
  local BM=mmdhl.boneMapper
  if istable(BM) and isfunction(BM.Convertible) and status.source and BM.Convertible(status.source) then
   Derma_Query(L('library.rigged_prop.query',{name=name,bones=info.skeleton.bones or 0}),L'library.rigged_prop.title',L'library.rigged_prop.import',function()
    mmdhl.props.library.Delete({status.asset},function() if not library.job then startImport('library',status.source) end end)
   end,L'library.rigged_prop.keep')
  else Derma_Message(L('library.rigged_prop.text',{name=name,bones=info.skeleton.bones or 0}),L'library.rigged_prop.title',L'common.ok') end
 end
end
mmdhl.PromptStaticInstead=promptStaticInstead
mmdhl.PromptCharacterInstead=promptCharacterInstead
hook.Add('Think','MMDHL.LibraryImport',function()
 if not library.job or (library.nextPoll or 0)>RealTime() then return end library.nextPoll=RealTime()+.1
 local status,err=mmdhl.Decode(native.PollJob(library.job))
 if not status then
  local kind=library.jobKind=='static' and 'static' or 'library'
  library.job=nil library.status=tostring(err) pickerNotice(false) hook.Run('MMDHL.ImportChanged')
  -- The module lost the job or cannot read its result: say so like any failure.
  local failure={state='failed',error=tostring(err or L'library.import.unknown_error'),stageCode='worker',filename=library.filename}
  timer.Simple(0,function() mmdhl.ShowImportFailure(failure,kind) end) return
 end
 local BM=mmdhl.boneMapper
 if istable(BM) and isfunction(BM.OnJobStatus) and BM.OnJobStatus(status) then hook.Run('MMDHL.ImportChanged') return end
 if status.state~='running' or status.stage~='Select model' then pickerNotice(false) end
 library.status=status.state=='failed' and L('library.import.failed',{error=tostring(status.error or L'library.import.unknown_error')}) or status.stage or status.error or status.state
 if status.warning then library.status=library.status..' — '..status.warning end
 if isnumber(status.progress) then library.progress=math.max(library.progress or 0,math.Clamp(status.progress,0,1)) end
 library.filename=status.filename or library.filename
 -- The import kept a damaged list of source paths (Reload's) under this name and started a new one (natives after 2.3.0).
 if status.state=='complete' and isstring(status.registryBackup) and status.registryBackup~='' then
  local text=L('library.import.registry_damaged',{file='mmd_hotloader/'..(status.kind=='static' and 'static/' or '')..status.registryBackup})
  notification.AddLegacy(text,NOTIFY_ERROR,15) chat.AddText(Color(255,170,80),'[Model Hotloader] ',color_white,text)
 end
 if status.state=='selected' then
  local source=status.path or status.source local kind=status.kind
  library.job=nil
  local function proceed()
   if library.job then library.status=L'library.import.busy' hook.Run('MMDHL.ImportChanged') return end
   if kind=='static' then library.StartStaticImport(source)
   elseif istable(BM) and isfunction(BM.Convertible) and isfunction(BM.Probe) and BM.Convertible(source) then BM.Probe(source)
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
  local previous=isstring(status.source) and library.reloadOf[status.source] or nil
  if previous then library.reloadOf[status.source]=nil end
  if mmdhl.terms then mmdhl.terms.Imported('character',status.asset,status.source,previous) end
  -- A reload's new revision keeps the translation choice of the model it replaces.
  if mmdhl.names then mmdhl.names.Imported(status.asset,status.source,previous) end
  library.Refresh()
  local name=(status.info or {}).name
  library.progress=1 library.lastImportedKind=nil library.lastImported=status.asset library.status=name and L('library.import.imported',{name=name}) or L'library.import.imported_unnamed'
  local warnings=mmdhl.SplitWarnings((status.info or {}).warnings)
  if #warnings>0 then library.status=L('library.import.with_warnings',{message=library.status,count=#warnings}) end
  notification.AddLegacy(library.status,#warnings>0 and NOTIFY_HINT or NOTIFY_GENERIC,8)
  hook.Run('MMDHL.Imported',status.asset)
  -- A failed ragdoll fit: the bone window's rescue prompt once its check is done, or, when
  -- that check fails, ExplainFit's window after all.
  timer.Simple(0,function() promptStaticInstead(status)
   if mmdhl.boneMapper and mmdhl.boneMapper.AfterImport then mmdhl.boneMapper.AfterImport(status,function() if mmdhl.ExplainFit then mmdhl.ExplainFit(status,true) end end) end
   if mmdhl.ExplainFit then mmdhl.ExplainFit(status) end end)
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
 elseif state=='error' then notification.AddLegacy(message,NOTIFY_ERROR,8)
  if pending and pending.asset and mmdhl.boneMapper and mmdhl.boneMapper.CheckRescue then mmdhl.boneMapper.CheckRescue(pending.asset) end
 end
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
