-- Bone assignment window (client): one window, three columns. The body chart and
-- the model's own skeleton cross-highlight; the inspector always says what to do
-- next. Convert mode prepares an FBX/glTF/DAE import; fit mode saves fitter pins
-- for a cached character. All logic lives in bone_mapper_rules.lua.
local BM=mmdhl.boneMapper
local L=mmdhl.L
local native=mmdhl.native
local VB='ValveBiped.Bip01_'
BM.sessions=BM.sessions or {}
BM.pending=BM.pending or {}
local autoskip=CreateClientConVar('mmdhl_bonemap_autoskip','0',true,false,'Import FBX, glTF and DAE characters without the bone window when every body part is found automatically (1).')
-- i18n-keys: bonemap.group.torso bonemap.group.left_arm bonemap.group.right_arm bonemap.group.left_leg bonemap.group.right_leg bonemap.group.fingers bonemap.group.eyes
-- i18n-keys: bonemap.origin.auto bonemap.origin.guess bonemap.origin.user bonemap.origin.saved bonemap.origin.remembered bonemap.origin.fit_saved bonemap.origin.conversion bonemap.origin.created
-- i18n-keys: bonemap.legend.ok bonemap.legend.check bonemap.legend.missing bonemap.legend.created
-- i18n-keys: bonemap.help.hips bonemap.help.spine bonemap.help.middle_spine bonemap.help.chest bonemap.help.neck bonemap.help.head bonemap.help.eye bonemap.help.shoulder bonemap.help.upper_arm
-- i18n-keys: bonemap.help.forearm bonemap.help.hand bonemap.help.thigh bonemap.help.lower_leg bonemap.help.foot bonemap.help.toes bonemap.help.finger
-- i18n-keys: bonemap.reason.named bonemap.reason.child bonemap.reason.below bonemap.reason.side_left bonemap.reason.side_right bonemap.reason.middle bonemap.reason.height
-- i18n-keys: bonemap.reason.moves bonemap.reason.moves_none bonemap.reason.used bonemap.reason.helper bonemap.reason.physics
-- i18n-keys: bonemap.issue.required bonemap.issue.duplicate bonemap.issue.order bonemap.issue.leg_on_spine bonemap.issue.sides_swapped bonemap.issue.side bonemap.issue.facing
-- i18n-keys: bonemap.issue.height bonemap.issue.length bonemap.issue.unweighted bonemap.issue.twist bonemap.issue.physics bonemap.issue.physics_optional bonemap.issue.recommended
-- i18n-keys: bonemap.issue.finger_partial bonemap.issue.jiggle_body bonemap.issue.jiggle_too_many bonemap.issue.range bonemap.issue.band bonemap.issue.native
-- i18n-keys: bonemap.jiggle.kind.hair bonemap.jiggle.kind.skirt bonemap.jiggle.kind.chest bonemap.jiggle.kind.tail bonemap.jiggle.kind.accessory
-- i18n-keys: bonemap.jiggle.swing.less bonemap.jiggle.swing.normal bonemap.jiggle.swing.more
-- i18n-keys: bonemap.notice.saved_file bonemap.notice.same_skeleton bonemap.notice.file_changed bonemap.notice.saved_fit bonemap.notice.resized
-- i18n-keys: bonemap.summary.ok bonemap.summary.check_one bonemap.summary.check_many bonemap.summary.missing_one bonemap.summary.missing_many bonemap.summary.not_humanoid
-- i18n-keys: bonemap.side.left bonemap.side.right bonemap.hint.convert bonemap.hint.rescue bonemap.hint.edit
local Help={hips='hips',spine='spine',middle_spine='middle_spine',chest='chest',neck='neck',head='head',left_eye='eye',right_eye='eye',left_shoulder='shoulder',right_shoulder='shoulder',
 left_upper_arm='upper_arm',right_upper_arm='upper_arm',left_forearm='forearm',right_forearm='forearm',left_hand='hand',right_hand='hand',left_thigh='thigh',right_thigh='thigh',
 left_lower_leg='lower_leg',right_lower_leg='lower_leg',left_foot='foot',right_foot='foot',left_toes='toes',right_toes='toes'}
-- Bone names the help texts show (not translated: they are names in files).
BM.Examples={hips='Hips, pelvis, 下半身',spine='Spine, spine_01, 上半身',middle_spine='Spine1, spine_02, 上半身2',chest='Spine2, spine_03, UpperChest, 上半身3',neck='Neck, neck_01, 首',head='Head, 頭',
 left_eye='LeftEye, eye_L, 左目',left_shoulder='LeftShoulder, clavicle_l, 左肩',left_upper_arm='LeftArm, upperarm_l, 左腕',left_forearm='LeftForeArm, lowerarm_l, 左ひじ',left_hand='LeftHand, hand_l, 左手首',
 left_thigh='LeftUpLeg, thigh_l, 左足',left_lower_leg='LeftLeg, calf_l, 左ひざ',left_foot='LeftFoot, foot_l, 左足首',left_toes='LeftToeBase, ball_l, 左つま先'}
function BM.ExamplesFor(s)
 local id=s.id:gsub('^right_','left_') local text=BM.Examples[id] or ''
 if s.side=='R' then text=text:gsub('Left','Right'):gsub('_l%f[%W]','_r'):gsub('_L%f[%W]','_R'):gsub('左','右') end
 return text
end
local Colors={ink=Color(31,43,58),muted=Color(93,109,127),accent=Color(34,109,185),window=Color(246,248,251),line=Color(226,230,235),
 ok=Color(33,138,84),check=Color(191,120,22),missing=Color(196,62,62),created=Color(138,151,166),disabled=Color(184,194,205),
 left=Color(47,111,222),right=Color(198,106,18),body=Color(88,98,112),swing=Color(142,68,173),unassigned=Color(190,198,207),hover=Color(255,210,74),
 hair=Color(142,92,194),skirt=Color(31,158,143),chest=Color(208,88,126),tail=Color(199,122,30),accessory=Color(79,124,201),
 silhouette=Color(230,235,241),outline=Color(201,210,220),link=Color(174,185,197)}
BM.Colors=Colors

-- ---- capabilities ----
local caps
local function capabilities()
 if caps==nil then caps=false local c=isfunction(native.GetCapabilities) and mmdhl.Decode(native.GetCapabilities()) if istable(c) then caps=c end end
 return caps or {}
end
function BM.Available(mode)
 if mode=='convert' then local c=capabilities().characterImport return istable(c) and (tonumber(c.version) or 0)>=1 end
 if mode=='fit' then
  -- The fitter's pins (GetBoneMapProposal) and the skeleton reader (InspectBoneMap).
  local c=capabilities().boneMap
  return isfunction(native.GetBoneMapProposal) and isfunction(native.InspectBoneMap) and not (istable(c) and c.fit==false)
 end
 return false
end
function BM.Convertible(path)
 if not BM.Available('convert') or not isstring(path) then return false end
 local extension=path:lower():match('%.([%w]+)$') if not extension then return false end
 for _,f in ipairs(capabilities().characterImport.formats or {}) do if f==extension then return true end end
 return false
end
function BM.CanSave() local p=LocalPlayer() return game.SinglePlayer() or (IsValid(p) and (p:IsListenServerHost() or p:IsAdmin())) end
-- A native function this binary lacks: the update reminder, or a plain notice.
-- i18n-keys: bonemap.feature bonemap.update_needed bonemap.feature_convert library.hint.character_update
local function updateNeeded(feature,text)
 if mmdhl.ShowNativeUpdateNeeded then mmdhl.ShowNativeUpdateNeeded(L(feature or 'bonemap.feature'),'2.3.0') return end
 notification.AddLegacy(L(text or 'bonemap.update_needed'),NOTIFY_HINT,8)
end
-- The pins a model has where it is fitted: single player and the listen host share the
-- server's DATA folder; other players ask the server. callback(pins, hasCollision), or
-- callback(nil) when the server does not answer.
BM.pinQueries=BM.pinQueries or {}
function BM.ServerPins(id,callback)
 local p=not game.SinglePlayer() and LocalPlayer()
 if not p or (IsValid(p) and p:IsListenServerHost()) then
  local saved=util.JSONToTable(file.Read('mmd_hotloader/fit_overrides/'..id..'.json','DATA') or '')
  if not istable(saved) then saved={} end
  callback(istable(saved.boneMap) and saved.boneMap or {},istable(saved.bodies) and next(saved.bodies)~=nil) return
 end
 local waiting=BM.pinQueries[id] if waiting then waiting[#waiting+1]=callback return end
 BM.pinQueries[id]={callback} mmdhl.Action('bonemap_pins',id)
 timer.Create('MMDHL.BoneMapPins.'..id,5,1,function() local list=BM.pinQueries[id] BM.pinQueries[id]=nil for _,cb in ipairs(list or {}) do cb(nil) end end)
end
net.Receive('mmdhl_bonemap_pins',function()
 local id=net.ReadString() local data=util.JSONToTable(net.ReadString()) local collision=net.ReadBool()
 local list=BM.pinQueries[id] BM.pinQueries[id]=nil timer.Remove('MMDHL.BoneMapPins.'..id)
 local pins={} for key,v in pairs(istable(data) and istable(data.boneMap) and data.boneMap or {}) do if BM.Mapped(key) and tonumber(v) then pins[key]=math.floor(tonumber(v)) end end
 for _,cb in ipairs(list or {}) do cb(pins,collision) end
end)

-- One bone window at a time: an open one comes to the front instead.
function BM.BringToFront()
 if not IsValid(BM.frame) then return false end
 BM.frame:MakePopup() BM.frame:MoveToFront()
 notification.AddLegacy(L'bonemap.window_open',NOTIFY_HINT,5)
 return true
end

-- ---- the library flow ----
-- A window the player is working in is never replaced: the next one opens when it closes.
function BM.Present(open,file)
 if not IsValid(BM.frame) then open() return true end
 BM.queued=open notification.AddLegacy(L('bonemap.status.waiting',{file=tostring(file or '')}),NOTIFY_HINT,8)
 return false
end
function BM.RunQueued() local open=BM.queued BM.queued=nil if open then timer.Simple(0,open) end end
function BM.Probe(source,opts)
 local library=mmdhl.library
 if library.job then library.status=L'library.import.busy' hook.Run('MMDHL.ImportChanged') return false end
 local handle,err=native.BeginImport(source,util.TableToJSON({kind='character_probe'}))
 if not library.StartImport(handle,err,'character_probe') then return false,err end
 BM.pending[source]=opts or {}
 library.status=L'library.import.reading_skeleton' hook.Run('MMDHL.ImportChanged')
 return true
end
local function startConversion(state)
 local library=mmdhl.library
 local handle,err=native.BeginImport(state.source,util.TableToJSON(BM.Request(state)))
 if not library.StartImport(handle,err) then return false,err end
 library.status=L'library.import.importing_model' hook.Run('MMDHL.ImportChanged') return true
end
-- Called first in the library's job poll. True when the bone window took the status.
function BM.OnJobStatus(status)
 local library=mmdhl.library
 if not istable(status) then return false end
 -- The worker names these steps in English; the library shows them in the player's language.
 if status.state=='running' and (status.stageCode=='probe' or status.stageCode=='convert_character') then
  library.status=status.stageCode=='probe' and L'library.import.reading_skeleton' or L'bonemap.stage.converting'
  if isnumber(status.progress) then library.progress=math.max(library.progress or 0,math.Clamp(status.progress,0,1)) end
  library.filename=status.filename or library.filename
  return true
 end
 if status.state~='running' and isstring(status.source) and status.kind~='bone_map' then BM.pending[status.source]=nil end
 if status.state=='complete' and status.kind=='bone_map' then
  library.job=nil library.progress=nil
  local opts=BM.pending[status.source] or {} BM.pending[status.source]=nil
  local probe=status.probe
  if not istable(probe) or (tonumber(probe.version) or 0)>1 then library.status=L'bonemap.too_new' notification.AddLegacy(library.status,NOTIFY_ERROR,8) return true end
  local file=tostring(status.filename or string.GetFileFromFilename(tostring(status.source or '')))
  library.status=L('bonemap.status.waiting',{file=file})
  -- Players who asked for it skip the window when nothing needs them.
  if autoskip:GetBool() and not opts.notice and not opts.select then
   local state=BM.NewState('convert',{probe=probe,source=status.source,filename=file})
   BM.ApplyMemory(state,BM.LoadMemory(probe)) BM.RefreshChains(state) BM.ApplyMemoryJiggle(state)
   BM.Validate(state)
   local sum=BM.Summary(state) local blocking=false for _,i in ipairs(state.issues) do if i.severity~='info' then blocking=true end end
   if sum.headline=='ok' and not blocking and not (probe.height or {}).normalized then
    BM.SaveMemory(state) BM.sessions[status.source]=state
    if startConversion(state) then notification.AddLegacy(L'bonemap.auto_imported',NOTIFY_GENERIC,10) end
    return true
   end
  end
  timer.Simple(0,function() BM.Present(function() BM.OpenConvert(status,opts) end,file) end)
  return true
 end
 if status.state=='failed' and (status.errorCode=='character.bone_map' or status.errorCode=='character.jiggle') and isstring(status.source) then
  library.job=nil
  local d=istable(status.errorDetails) and status.errorDetails or {}
  local session=BM.sessions[status.source]
  local nativeIssue={code='native',severity='error',slot=isstring(d.slot) and d.slot~='' and d.slot or '',args={message=tostring(status.error or '')}}
  if d.reason=='missing' or not session then
   -- The file changed since its bones were assigned (or this is a Reload): read it again.
   BM.Probe(status.source,{notice=d.reason=='missing' and 'file_changed' or nil,select=nativeIssue.slot,native=nativeIssue})
  else
   library.status=L('bonemap.status.waiting',{file=tostring(status.filename or '')})
   timer.Simple(0,function() BM.Present(function() BM.ShowWindow(session,{select=nativeIssue.slot,native=nativeIssue}) end,status.filename) end)
  end
  return true
 end
 if status.state=='complete' and not status.kind and isstring(status.source) and BM.sessions[status.source] then
  local s=BM.sessions[status.source] BM.sessions[status.source]=nil
  hook.Run('MMDHL.BoneMapClosed',s,'imported')
 end
 -- Failures the library's dialog shows, with a better hint.
 if status.state=='failed' and isstring(status.source) and status.kind~='static' and library.jobKind~='static' then
  local extension=status.source:lower():match('%.(%w+)$') or ''
  local static=library.StaticImportable
  -- An older native read an FBX, glTF or DAE character as a file of the wrong kind.
  if not BM.Available('convert') and ({fbx=true,glb=true,gltf=true,dae=true})[extension] and tostring(status.error or ''):lower():find('belong in static props',1,true) then
   status.hint=L'library.hint.character_update' updateNeeded('bonemap.feature_convert','library.hint.character_update')
  -- No skeleton, and the static prop importer cannot read this type either (DAE).
  elseif (status.errorCode=='character.no_skeleton' or status.errorCode=='character.too_few_bones') and static and not static(status.source) then
   status.hint=L'library.hint.character_no_skeleton_export'
  end
 end
 return false
end
-- Opens the convert window from a probe status (also usable from the console with stored JSON).
function BM.OpenConvert(status,opts)
 opts=opts or {}
 if not istable(status) or not istable(status.probe) or not isstring(status.source) or status.source=='' then return false end
 local probe=status.probe local source=status.source
 local file=tostring(status.filename or string.GetFileFromFilename(tostring(source or '')))
 local state=BM.sessions[source]
 if not state or state.sha~=probe.sourceSha256 then
  state=BM.NewState('convert',{probe=probe,source=source,filename=file})
  BM.ApplyMemory(state,BM.LoadMemory(probe))
  BM.RefreshChains(state) BM.ApplyMemoryJiggle(state)
  state.history={} state.cursor=0 BM.Snapshot(state,'open') state.dirty=false
 end
 if opts.notice then state.notice={key=opts.notice,args={},link=state.notice and state.notice.link} end
 if istable(probe.height) and probe.height.normalized and not (state.notice and state.notice.key=='file_changed') then
  state.notice={key='resized',args={height=string.format('%.2f',tonumber(probe.height.meters) or 0)},severity='warning'} end
 BM.sessions[source]=state
 BM.ShowWindow(state,opts)
end
-- Fit mode: the window opens at once and reads the cached model's skeleton.
function BM.OpenFit(id,reason,name)
 if not BM.Available('fit') then updateNeeded() return false end
 if BM.BringToFront() then return false end
 local entry=mmdhl.library.entries[id]
 local state={mode='fit',reason=reason or 'edit',loading=true,asset=id,name=name or (entry and entry.name) or id:sub(1,12),bones={},issues={},slots={},eyes={L={bone=-1},R={bone=-1}},history={},cursor=0}
 local window=BM.ShowWindow(state,{})
 local ok,err=native.RequestAsset(id)
 local started=RealTime()
 local function fail(message) if IsValid(window) then window:LoadFailed(message) end end
 if not ok then fail(err) return false end
 timer.Create('MMDHL.BoneMapLoad',.1,0,function()
  if not IsValid(window) then timer.Remove('MMDHL.BoneMapLoad') return end
  local info,e=mmdhl.Decode(native.AssetInfo(id))
  if not info and not e and RealTime()-started<60 then return end
  timer.Remove('MMDHL.BoneMapLoad')
  if not info then fail(e or L'server.error.asset_timeout') return end
  BM.ServerPins(id,function(pins,hasCollision) if IsValid(window) then BM.FitLoaded(window,state,info,pins,hasCollision) end end)
 end)
 return true
end
-- The cached model, the fitter's choice and the server's pins become the window's state.
function BM.FitLoaded(window,state,info,pins,hasCollision)
 local id,reason=state.asset,state.reason
 local entry=mmdhl.library.entries[id]
 if not pins then window:LoadFailed(L'bonemap.save_timeout') return end
 local inspect,ie=mmdhl.Decode(native.InspectBoneMap(id,util.TableToJSON({include={'skeleton'}})))
 local proposal,pe=mmdhl.Decode(native.GetBoneMapProposal(id,'{}'))
 if not inspect or not proposal then window:LoadFailed(ie or pe) return end
 local current=proposal
 if next(pins) then current=mmdhl.Decode(native.GetBoneMapProposal(id,BM.IndexJSON('boneMap',pins))) or proposal end
 local s=BM.NewState('fit',{asset=id,name=state.name,reason=reason,skeleton=inspect.skeleton,auto=inspect.auto,proposal=proposal,current=current,pins=pins,hasCollision=hasCollision==true,torso=current.torso})
 s.savedPins=pins
 s.format=entry and isstring(entry.source) and entry.source:lower():match('%.(%w+)$') or (istable(info.vrm) and 'vrm' or 'pmx')
 if next(pins) then s.notice={key='saved_fit',args={}} end
 if istable(info.conversion) then s.converted=true end
 for _,i in ipairs(current.issues or {}) do if istable(i) and i.severity then s.nativeIssues[#s.nativeIssues+1]={code=i.code=='band' and 'band' or i.code=='range' and 'range' or 'native',severity=i.severity,slot=i.slot or '',args={message=tostring(i.text or '')}} end end
 window:SetState(s)
end
-- Fit mode without a window: the parts the fitter misses for a model, with its
-- saved pins, or nil. callback(missing, error).
function BM.CheckFit(id,callback)
 if not isfunction(native.GetBoneMapProposal) then callback(nil,'unavailable') return end
 local ok,err=native.RequestAsset(id) if not ok then callback(nil,err) return end
 local started=RealTime() local name='MMDHL.BoneMapCheck.'..id
 timer.Create(name,.1,0,function()
  local info,e=mmdhl.Decode(native.AssetInfo(id))
  if not info and not e and RealTime()-started<60 then return end
  timer.Remove(name)
  if not info then callback(nil,e) return end
  BM.ServerPins(id,function(pins)
   if not pins then callback(nil,'timeout') return end
   local result,re=mmdhl.Decode(native.GetBoneMapProposal(id,next(pins) and BM.IndexJSON('boneMap',pins) or '{}'))
   if not result then callback(nil,re) return end
   callback(istable(result.missing) and result.missing or {},nil,info)
  end)
 end)
end
-- After a character import or a failed spawn: when the fitter (with the saved pins)
-- still misses body parts of a model that looks like a character, the model gets the
-- "Needs bones" badge and the rescue prompt. Models that do not look like characters
-- keep the library's "import it as a static prop" question.
local function looksHumanoid(info) local found,bones=mmdhl.HumanoidLandmarks(istable(info) and info or {}) return found>=6 and bones>=15 end
local function setFit(id,fit) local library=mmdhl.library if library.SetFitStatus then library.SetFitStatus(id,fit) end end
local function flag(id,name,missing)
 setFit(id,{ok=false,missing=missing})
 if BM.Available('fit') then BM.ShowRescuePrompt(id,name,missing) end
end
function BM.CheckRescue(id,name,info)
 if not BM.Available('fit') then return false end
 BM.CheckFit(id,function(missing,_,loaded)
  if not missing then return end
  if #missing==0 then setFit(id,{ok=true}) return end
  if not looksHumanoid(loaded or info) then return end
  local entry=mmdhl.library.entries[id]
  flag(id,name or (entry and entry.name) or (loaded and loaded.name) or id:sub(1,12),missing)
 end)
 return true
end
-- status: a character import's complete status, with the import-time fit when the
-- worker reports it ({ok=false, errorCode='fit.landmarks', missing={keys}}).
function BM.AfterImport(status)
 local info=status.info or {}
 if not isstring(status.asset) or not looksHumanoid(info) then return end
 local name=tostring(info.name or status.filename or '')
 local fit=status.fit
 if istable(fit) then
  if fit.ok~=false then setFit(status.asset,{ok=true}) return end
  if fit.errorCode~='fit.landmarks' then return end
  -- Saved pins may already fix it; without the fitter's pins only the badge is set.
  if BM.Available('fit') then BM.CheckRescue(status.asset,name,info)
  elseif istable(fit.missing) and #fit.missing>0 then flag(status.asset,name,fit.missing) end
  return
 end
 BM.CheckRescue(status.asset,name,info)
end

-- ---- drawing helpers ----
local function circle(x,y,r,color) draw.RoundedBox(r,x-r,y-r,r*2,r*2,color) end
local function ring(x,y,r,color,thick) surface.SetDrawColor(color) for k=0,(thick or 2)-1 do surface.DrawCircle(x,y,r-k,color.r,color.g,color.b,color.a) end end
local function thickLine(x1,y1,x2,y2,color,w)
 surface.SetDrawColor(color) local dx,dy=x2-x1,y2-y1 local len=math.sqrt(dx*dx+dy*dy) if len<.001 then return end
 local nx,ny=-dy/len,dx/len for k=0,(w or 2)-1 do local o=k-((w or 2)-1)/2 surface.DrawLine(x1+nx*o,y1+ny*o,x2+nx*o,y2+ny*o) end
end
-- The status glyphs, drawn so they look the same in every font.
function BM.DrawStatus(status,x,y,r)
 if status=='ok' then circle(x,y,r,Colors.ok) thickLine(x-r*.45,y,x-r*.1,y+r*.4,color_white,2) thickLine(x-r*.1,y+r*.4,x+r*.5,y-r*.35,color_white,2)
 elseif status=='check' then circle(x,y,r,Colors.check) surface.SetDrawColor(255,255,255) surface.DrawRect(x-1,y-r*.55,2,r*.65) surface.DrawRect(x-1,y+r*.25,2,2)
 elseif status=='missing' then circle(x,y,r,Colors.missing) circle(x,y,r-2,color_white) thickLine(x-r*.4,y-r*.4,x+r*.4,y+r*.4,Colors.missing,2) thickLine(x-r*.4,y+r*.4,x+r*.4,y-r*.4,Colors.missing,2)
 elseif status=='created' then circle(x,y,r,color_white) surface.SetDrawColor(Colors.created)
  for k=0,11 do local a=math.rad(k*30) local b=a+math.rad(15) surface.DrawLine(x+math.cos(a)*r,y+math.sin(a)*r,x+math.cos(b)*r,y+math.sin(b)*r) surface.DrawLine(x+math.cos(a)*(r-1),y+math.sin(a)*(r-1),x+math.cos(b)*(r-1),y+math.sin(b)*(r-1)) end
 else circle(x,y,r,ColorAlpha(Colors.disabled,100)) end
end
local function poly(points,color) surface.SetDrawColor(color) draw.NoTexture() surface.DrawPoly(points) end
local function circlePoly(x,y,r,n) local p={} for k=0,(n or 16)-1 do local a=k/(n or 16)*math.pi*2 p[#p+1]={x=x+math.cos(a)*r,y=y+math.sin(a)*r} end return p end
local function clockwise(points) local a=0 for i=1,#points do local p,q=points[i],points[i%#points+1] a=a+p.x*q.y-q.x*p.y end if a<0 then local r={} for i=#points,1,-1 do r[#r+1]=points[i] end return r end return points end
local function capsule(x1,y1,x2,y2,r,color)
 poly(circlePoly(x1,y1,r,12),color) poly(circlePoly(x2,y2,r,12),color)
 local dx,dy=x2-x1,y2-y1 local len=math.sqrt(dx*dx+dy*dy) if len<.001 then return end
 local nx,ny=-dy/len*r,dx/len*r
 poly(clockwise({{x=x1+nx,y=y1+ny},{x=x2+nx,y=y2+ny},{x=x2-nx,y=y2-ny},{x=x1-nx,y=y1-ny}}),color)
end
local function regionColor(region,kind) if region=='swing' then return Colors[kind] or Colors.swing end return Colors[region] or Colors.unassigned end

-- ---- the window ----
local function primaryLabel(state) return state.mode=='convert' and L'bonemap.import' or L'bonemap.save' end
local function originText(state,key)
 local origin=key:sub(1,4)=='Eye_' and state.eyes[key:sub(5)].origin or (state.slots[key] or {}).origin or 'auto'
 return L('bonemap.origin.'..origin)
end
local function issueText(state,i)
 local a={} for k,v in pairs(i.args or {}) do a[k]=v end
 for _,k in ipairs({'part','first','second','parent','other'}) do if isstring(a[k]) and BM.SlotByKey[a[k]] then a[k]=BM.PartLabel(a[k]) end end
 if istable(a.parts) then a.parts=BM.PartList(a.parts,6) end
 if a.side then a.side=L('bonemap.side.'..a.side) end
 if i.code=='physics' and i.severity=='warning' then return L('bonemap.issue.physics_optional',a) end
 if i.code=='torso' then
  local r=i.repair or {}
  if r.code=='reordered' and istable(r.bones) and #r.bones>=2 then return L('bonemap.torso_reordered',{lower=tostring((state.bones[r.bones[1]+1] or {}).name or ''),upper=tostring((state.bones[r.bones[2]+1] or {}).name or '')}) end
  if r.code=='merged' and istable(r.bones) and #r.bones>=2 then return L('bonemap.torso_merged',{bone=tostring((state.bones[r.bones[1]+1] or {}).name or ''),target=tostring((state.bones[r.bones[2]+1] or {}).name or '')}) end
  return tostring(r.text or '')
 end
 if i.code=='native' then return L('bonemap.issue.native',{message=tostring(a.message or '')}) end
 if i.code=='band' or i.code=='range' then return L('bonemap.issue.'..i.code) end
 return L('bonemap.issue.'..i.code,a)
end
BM.IssueText=issueText

function BM.ShowWindow(state,opts)
 opts=opts or {}
 -- A window still open (an edit started from the library while an import came back) closes.
 if IsValid(BM.frame) then local old=BM.frame.Window BM.frame.closing=true BM.frame.replaced=true BM.frame:Remove() if old then hook.Run('MMDHL.BoneMapClosed',old.state,'cancelled') end end
 local UI=mmdhl.UI local s,f=UI.metrics()
 local frame=vgui.Create('DFrame') BM.frame=frame BM.state=state
 local W,H=math.min(s(1400),ScrW()-s(40)),math.min(s(860),ScrH()-s(60))
 frame:SetTitle('') frame:SetSize(W,H) frame:Center() frame:MakePopup() frame:DockPadding(s(16),s(14),s(16),s(14))
 frame.btnMinim:SetVisible(false) frame.btnMaxim:SetVisible(false)
 local win={state=state,frame=frame,s=s,f=f,view={mode='front',zoom=1,panX=0,panY=0},armed=nil,selected=nil,hover=nil,figure='body',transient=nil,searchText=''}
 frame.Window=win
 local accent=function() return state.reason=='rescue' and Colors.check or Colors.accent end
 frame.Paint=function(_,w,h) draw.RoundedBox(6,0,0,w,h,Colors.window) draw.RoundedBoxEx(6,0,0,w,s(6),accent(),true,true,false,false) end
 -- Closing never discards silently.
 local function finishClose(outcome)
  frame.closing=true
  if state.mode=='convert' and outcome=='cancelled' then mmdhl.library.status=L'library.import.cancelled' hook.Run('MMDHL.ImportChanged') end
  hook.Run('MMDHL.BoneMapClosed',state,outcome) frame:Remove()
 end
 win.finish=finishClose
 local function askClose()
  if state.dirty and not state.loading then
   -- Convert mode keeps its choices for the game session; fit mode loses them.
   Derma_Query(state.mode=='convert' and L'bonemap.discard_text_convert' or L'bonemap.discard_text',L'bonemap.discard_title',L'bonemap.discard',function() if IsValid(frame) then finishClose('cancelled') end end,L'bonemap.keep_editing')
  else finishClose('cancelled') end
 end
 frame.Close=function() askClose() end
 frame.OnRemove=function() if BM.frame==frame then BM.frame=nil end timer.Remove('MMDHL.BoneMapRefresh') timer.Remove('MMDHL.BoneMapLoad') if IsValid(win.picker) then win.picker:Remove() end if not frame.replaced then BM.RunQueued() end end
 -- ---- layout ----
 local title=frame:Add('DPanel') title:Dock(TOP) title:SetTall(s(34)) title:SetPaintBackground(false)
 local titleLabel=UI.label(title,'',f.Title,s(34)) titleLabel:Dock(FILL)
 local facts=UI.label(title,'',f.Small,s(34)) facts:Dock(RIGHT) facts:SetTextColor(Colors.muted) facts:SetContentAlignment(6)
 local hint=UI.label(frame,'',f.Body,s(24)) hint:Dock(TOP) hint:SetWrap(true) hint:SetAutoStretchVertical(true) hint:SetTextColor(Colors.muted) hint:DockMargin(0,0,0,s(6))
 local notice=frame:Add('DPanel') notice:Dock(TOP) notice:SetTall(s(32)) notice:DockMargin(0,0,0,s(6)) notice:SetVisible(false)
 local noticeLink=UI.button(notice,L'bonemap.notice.use_auto',nil,s(26),f.Small) noticeLink:Dock(RIGHT) noticeLink:DockMargin(0,s(3),s(4),s(3))
 local noticeText=UI.label(notice,'',f.Small,s(32)) noticeText:Dock(FILL) noticeText:DockMargin(s(10),0,0,0)
 notice.Paint=function(_,w,h) local n=state.notice local tint=n and n.severity=='warning' and Color(252,241,224) or n and (n.key=='saved_file' or n.key=='same_skeleton' or n.key=='saved_fit') and Color(232,243,236) or Color(232,239,247) draw.RoundedBox(4,0,0,w,h,tint) end
 local tools=frame:Add('DPanel') tools:Dock(TOP) tools:SetTall(s(34)) tools:SetPaintBackground(false) tools:DockMargin(0,0,0,s(8))
 local footer=frame:Add('DPanel') footer:Dock(BOTTOM) footer:SetTall(s(64)) footer:SetPaintBackground(false) footer:DockMargin(0,s(10),0,0)
 local content=frame:Add('DPanel') content:Dock(FILL) content:SetPaintBackground(false)
 win.content=content
 local function natural(font,text,pad) surface.SetFont(font) return surface.GetTextSize(text)+(pad or s(28)) end
 -- Tool row: tabs on the left, editing tools on the right.
 local bodyTab=UI.button(tools,L'bonemap.tab.body',function() win:SetTab('body') end,s(34),f.Strong,'tab') bodyTab:Dock(LEFT) bodyTab:SetWide(math.max(s(90),natural(f.Strong,L'bonemap.tab.body'))) bodyTab:DockMargin(0,0,s(6),0)
 local jiggleTab=UI.button(tools,L'bonemap.tab.jiggle',function() win:SetTab('jiggle') end,s(34),f.Strong,'tab') jiggleTab:Dock(LEFT) jiggleTab:SetWide(natural(f.Strong,L'bonemap.tab.jiggle'))
 local listToggle=UI.button(tools,L'bonemap.view.list',function() state.view=state.view=='list' and 'picture' or 'list' win:BuildContent() end,s(34),f.Body) listToggle:Dock(RIGHT) listToggle:SetWide(math.max(s(100),natural(f.Body,L'bonemap.view.picture'))) listToggle:DockMargin(s(6),0,0,0)
 local more=UI.button(tools,L'bonemap.more'..' ▾',function() win:MoreMenu() end,s(34),f.Body) more:Dock(RIGHT) more:SetWide(math.max(s(90),natural(f.Body,L'bonemap.more'..' ▾'))) more:DockMargin(s(6),0,0,0)
 local swap=UI.button(tools,L'bonemap.swap',function() BM.SwapSides(state) win:Changed() end,s(34),f.Body) swap:Dock(RIGHT) swap:SetWide(natural(f.Body,L'bonemap.swap')) swap:DockMargin(s(6),0,0,0)
 local undo=UI.button(tools,L'bonemap.undo',function() if BM.Undo(state) then win:Changed(true) end end,s(34),f.Body) undo:Dock(RIGHT) undo:SetWide(math.max(s(80),natural(f.Body,L'bonemap.undo'))) undo:SetTooltip(L'bonemap.undo_tip')
 -- Footer: what to do now on the left; Cancel and the primary action on the right.
 local primary=UI.button(footer,primaryLabel(state),function() win:Primary() end,s(36),f.Strong,true) primary:Dock(RIGHT) primary:SetWide(math.max(s(220),natural(f.Strong,primaryLabel(state)))) primary:DockMargin(s(8),s(14),0,s(14))
 local cancel=UI.button(footer,L'common.cancel',function() askClose() end,s(36),f.Body) cancel:Dock(RIGHT) cancel:SetWide(math.max(s(110),natural(f.Body,L'common.cancel'))) cancel:DockMargin(s(8),s(14),0,s(14))
 local statusRow=footer:Add('DPanel') statusRow:Dock(TOP) statusRow:SetTall(s(40)) statusRow:SetPaintBackground(false)
 local showLink=UI.button(statusRow,L'bonemap.status.show',function() win:ShowFirst() end,s(26),f.Small) showLink:Dock(RIGHT) showLink:DockMargin(s(6),s(7),0,s(7))
 local statusText=UI.label(statusRow,'',f.Body,s(40)) statusText:Dock(FILL) statusText:SetWrap(true) statusText:DockMargin(s(26),0,0,0)
 statusRow.PaintOver=function() local sum=win.summary if sum then BM.DrawStatus(sum.headline=='ok' and 'ok' or sum.headline=='check' and 'check' or 'missing',s(10),s(20),s(9)) end end
 local below=footer:Add('DPanel') below:Dock(FILL) below:SetPaintBackground(false)
 local skip
 if state.mode=='convert' then skip=UI.checkbox(below,L'bonemap.skip_next','mmdhl_bonemap_autoskip',f.Small,s(22)) skip:Dock(LEFT) skip:DockMargin(s(26),0,0,0) end
 local collisionNote=UI.label(below,L'bonemap.collision_reset',f.Small,s(22)) collisionNote:Dock(FILL) collisionNote:SetTextColor(Colors.check) collisionNote:SetVisible(false) collisionNote:DockMargin(s(26),0,0,0)

 -- ---- state changes ----
 function win:Changed(keepArmed)
  if state.loading then return end
  -- The converter's complaint stands until the player changes something.
  if state.nativeAt and state.mapVersion~=state.nativeAt then state.nativeIssues={} state.nativeAt=nil end
  BM.RefreshChains(state) BM.Validate(state) win.summary=BM.Summary(state)
  win.regions=BM.Regions(state)
  if win.armed and BM.StatusOf(state,win.armed)=='disabled' then win.armed=nil end
  win:Refresh()
  if state.mode=='fit' then
   -- The fitter's own view of these pins, a moment after the last change.
   timer.Create('MMDHL.BoneMapRefresh',.25,1,function() if IsValid(frame) and BM.state==state then win:RefreshProposal() end end)
  end
 end
 function win:RefreshProposal()
  local result=mmdhl.Decode(native.GetBoneMapProposal(state.asset,BM.IndexJSON('boneMap',BM.Pins(state))))
  if not istable(result) then return end
  state.nativeIssues={}
  for _,i in ipairs(result.issues or {}) do if istable(i) then state.nativeIssues[#state.nativeIssues+1]={code=i.code=='band' and 'band' or i.code=='range' and 'range' or 'native',severity=i.severity or 'warning',slot=i.slot or '',args={message=tostring(i.text or '')}} end end
  for _,b in ipairs(result.bones or {}) do if istable(b) and BM.Mapped(b.name) then state.aliases[b.name]=b.aliases or {} end end
  state.torso=result.torso
  BM.Validate(state) win.summary=BM.Summary(state) win:Refresh()
 end
 function win:Refresh()
  if not IsValid(frame) then return end
  titleLabel:SetText(state.mode=='convert' and L('bonemap.title',{file=state.filename}) or L('bonemap.title_fit',{name=state.name}))
  facts:SetText(state.loading and '' or L('bonemap.facts',{format=string.upper(state.format or ''),bones=#state.bones,height=string.format('%.2f',state.height or 0)}))
  facts:SizeToContentsX(s(4))
  hint:SetText(state.mode=='convert' and L'bonemap.hint.convert' or state.reason=='rescue' and L('bonemap.hint.rescue',{name=state.name}) or L'bonemap.hint.edit')
  local n=state.notice
  notice:SetVisible(n~=nil)
  if n then noticeText:SetText(L('bonemap.notice.'..n.key,n.args or {})) noticeLink:SetVisible(n.link=='use_auto') noticeLink:SetWide(natural(f.Small,L'bonemap.notice.use_auto',s(16))) end
  bodyTab.Selected=state.tab=='body' jiggleTab.Selected=state.tab=='jiggle' jiggleTab:SetVisible(state.mode=='convert')
  listToggle:SetText(state.view=='list' and L'bonemap.view.picture' or L'bonemap.view.list') listToggle:SetVisible(state.tab=='body')
  undo:SetEnabled(BM.CanUndo(state))
  local sum=win.summary
  if win.transient and RealTime()<win.transient.expires then statusText:SetText(win.transient.text) showLink:SetText(L'bonemap.undo') showLink.Action='undo'
  elseif sum then
   local text
   if #sum.missing>0 then text=#sum.missing==1 and L('bonemap.status.missing_one',{parts=BM.PartList(sum.missing,4)}) or L('bonemap.status.missing_many',{count=#sum.missing,parts=BM.PartList(sum.missing,4)})
   elseif sum.errors>0 then text=sum.errors==1 and L'bonemap.status.problems_one' or L('bonemap.status.problems_many',{count=sum.errors})
   elseif #sum.check>0 then text=L('bonemap.status.check',{count=#sum.check,parts=BM.PartList(sum.check,4)})
   else text=L'bonemap.status.ok' end
   if state.mode=='fit' and not BM.CanSave() then text=L'bonemap.disabled_admin' end
   statusText:SetText(text) showLink:SetText(L'bonemap.status.show') showLink.Action='show'
   showLink:SetVisible(#sum.missing>0 or sum.errors>0 or #sum.check>0)
  end
  showLink:SetWide(natural(f.Small,showLink:GetText(),s(16)))
  local changedPhysical=false
  if state.mode=='fit' and state.hasCollision then for key,v in pairs(BM.Pins(state)) do local slot=BM.SlotByKey[key] if slot and slot.physical and state.savedPins[key]~=v then changedPhysical=true end end
   for key,v in pairs(state.savedPins or {}) do local slot=BM.SlotByKey[key] if slot and slot.physical and BM.Pins(state)[key]~=v then changedPhysical=true end end end
  collisionNote:SetVisible(changedPhysical) win.dropCollision=changedPhysical
  local blocked=sum==nil or sum.errors>0 or #sum.missing>0
  local busy=state.mode=='convert' and mmdhl.library.job~=nil
  local admin=state.mode=='fit' and not BM.CanSave()
  primary:SetEnabled(not state.loading and not blocked and not busy and not admin and not win.saving)
  primary:SetText(win.saving and L'bonemap.saving' or primaryLabel(state))
  primary:SetTooltip(blocked and L'bonemap.disabled_missing' or busy and L'bonemap.disabled_busy' or admin and L'bonemap.disabled_admin' or nil)
  if IsValid(win.inspector) then win.inspector:RebuildContent() end
  if IsValid(win.table) then win.table:RebuildContent() end
  if IsValid(win.jiggleList) then win.jiggleList:RebuildContent() end
  if IsValid(win.jiggleDetails) then win.jiggleDetails:RebuildContent() end
 end
 noticeLink.DoClick=function()
  -- One undo step back to the automatic assignment.
  for key,g in pairs(state.auto) do if key:sub(1,4)=='Eye_' then state.eyes[key:sub(5)]={bone=g.bone,origin=g.bone<0 and 'created' or 'auto'}
   elseif state.slots[key] then state.slots[key]={bone=g.bone,origin=g.bone<0 and 'created' or ((g.confidence or 0)<.85 and 'guess' or 'auto'),confidence=g.confidence} end end
  state.notice=nil BM.Changed(state,'auto') win:Changed()
 end
 showLink.DoClick=function() if showLink.Action=='undo' then win.transient=nil if BM.Undo(state) then win:Changed() end else win:ShowFirst() end end
 function win:Notify(text) win.transient={text=text,expires=RealTime()+6} win:Refresh() end
 function win:ShowFirst()
  local sum=win.summary if not sum then return end
  local key=sum.missing[1] for _,i in ipairs(state.issues) do if i.severity=='error' and i.slot~='' and not key then key=i.slot end end
  key=key or sum.check[1] if key then if state.tab~='body' then win:SetTab('body') end win:Arm(key) end
 end
 -- ---- arming and assigning ----
 function win:Arm(key)
  if key==nil then win.armed=nil win:Refresh() return end
  local slot=BM.SlotByKey[key] if not slot then return end
  win.armed=key win.selected=nil win.searchText=''
  -- The chart and the picture follow the part: fingers show the hand, the rest the body.
  if slot.segment>0 then win.figure='hands' win:ZoomToHand(slot.side)
  else win.figure='body' if win.view.focus then win:FitView() end end
  win:Refresh()
 end
 function win:AssignArmed(bone,origin)
  local key=win.armed if not key then return end
  local moved=BM.Assign(state,key,bone,origin or 'user')
  if moved then win.transient={text=L('bonemap.reassigned',{bone=state.bones[bone+1].name,part=BM.PartLabel(moved)}),expires=RealTime()+6} end
  BM.Validate(state)
  win.armed=BM.NextSlot(state,key,1,win.checkRun)
  if not win.armed then win.checkRun=nil end
  win:Changed()
 end
 function win:ClickBone(bone)
  if win.armed then win:AssignArmed(bone) return end
  local using=BM.SlotUsing(state,bone)
  if using then win:Arm(using) else win.selected=bone win:Refresh() end
 end
 function win:Primary()
  if not primary:IsEnabled() then win:ShowFirst() return end
  if state.mode=='convert' then
   -- The session stays until the import completes (MMDHL.BoneMapClosed 'imported') or comes back.
   BM.SaveMemory(state) BM.sessions[state.source]=state
   frame.closing=true frame:Remove()
   local ok,err=startConversion(state)
   if not ok then notification.AddLegacy(tostring(err or L'library.import.start_failed'),NOTIFY_ERROR,8) end
  else
   local pins=BM.Pins(state)
   if BM.SamePins(pins,state.savedPins) then finishClose('saved') return end
   win.saving=true win:Refresh()
   BM.awaiting={asset=state.asset,state=state,started=RealTime(),name=state.name}
   mmdhl.Action('bonemap',state.asset,nil,{version=1,boneMap=pins,dropCollision=win.dropCollision==true})
   timer.Create('MMDHL.BoneMapSave',10,1,function()
    if BM.awaiting and BM.awaiting.state==state then BM.awaiting=nil if IsValid(frame) then win.saving=false win:Notify(L'bonemap.save_timeout') end end
   end)
  end
 end
 function win:MoreMenu()
  local menu=DermaMenu()
  menu:AddOption(L'bonemap.more.auto_keep',function()
   for key,g in pairs(state.auto) do local v=key:sub(1,4)=='Eye_' and state.eyes[key:sub(5)] or state.slots[key]
    if v and v.origin~='user' and v.origin~='saved' and v.origin~='remembered' and v.origin~='fit_saved' then
     if key:sub(1,4)=='Eye_' then state.eyes[key:sub(5)]={bone=g.bone,origin=g.bone<0 and 'created' or 'auto'} else state.slots[key]={bone=g.bone,origin=g.bone<0 and 'created' or ((g.confidence or 0)<.85 and 'guess' or 'auto'),confidence=g.confidence} end end end
   BM.Changed(state,'auto') win:Changed() end):SetIcon('icon16/arrow_refresh.png')
  local allLabel=state.mode=='fit' and L'bonemap.more.reset_fit' or L'bonemap.more.auto_all'
  menu:AddOption(allLabel,function()
   local function apply() noticeLink.DoClick() end
   local own=0 for _,v in pairs(state.slots) do if v.origin=='user' then own=own+1 end end
   if own>0 then Derma_Query(L('bonemap.more.auto_all_confirm',{count=own}),allLabel,L'bonemap.confirm.continue',apply,L'common.cancel') else apply() end
  end):SetIcon('icon16/arrow_rotate_anticlockwise.png')
  menu:AddSpacer()
  for _,from in ipairs({'L','R'}) do
   menu:AddOption(from=='L' and L'bonemap.more.copy_lr' or L'bonemap.more.copy_rl',function()
    local function run(replace)
     local copied,unmatched=BM.Mirror(state,from,replace)
     local text=L('bonemap.mirror.done',{count=copied}) if #unmatched>0 then text=text..' '..L('bonemap.mirror.unmatched',{parts=BM.PartList(unmatched,4)}) end
     win:Changed() win:Notify(text)
    end
    local _,_,kept=BM.Mirror(BM.Copy(state),from,false)
    if #kept>0 then Derma_Query(L('bonemap.mirror.replace_user',{count=#kept}),L'bonemap.more',L'bonemap.mirror.replace',function() run(true) end,L'bonemap.mirror.keep',function() run(false) end) else run(false) end
   end)
  end
  menu:AddSpacer()
  menu:AddOption(L'bonemap.more.clear_fingers',function() for _,slot in ipairs(BM.Slots) do if slot.segment>0 then state.slots[slot.key]={bone=-1,origin='created',confidence=0} end end BM.Changed(state,'fingers') win:Changed() end)
  menu:AddOption(L'bonemap.more.clear_optional',function() for _,slot in ipairs(BM.Slots) do if not slot.required and not slot.convertOnly then state.slots[slot.key]={bone=-1,origin='created',confidence=0} end end
   if state.mode=='convert' then state.eyes={L={bone=-1,origin='created'},R={bone=-1,origin='created'}} end BM.Changed(state,'optional') win:Changed() end)
  menu:AddSpacer()
  local redo=menu:AddOption(L'bonemap.more.redo',function() if BM.Redo(state) then win:Changed() end end) redo:SetEnabled(BM.CanRedo(state))
  UI.ownScale(menu) menu:Open()
 end
 function win:SetTab(tab) state.tab=tab if tab=='jiggle' then BM.RefreshChains(state) end win:BuildContent() win:Changed() end
 function win:LoadFailed(message)
  content:Clear()
  local box=content:Add('DPanel') box:Dock(FILL) box:SetPaintBackground(false)
  local text=UI.label(box,L('bonemap.load_failed',{error=tostring(message or '')}),f.Body,s(60)) text:Dock(TOP) text:SetWrap(true) text:SetAutoStretchVertical(true) text:DockMargin(s(20),s(40),s(20),s(10))
  local again=UI.button(box,L'bonemap.try_again',function() local id,reason,name=state.asset,state.reason,state.name finishClose('cancelled') BM.OpenFit(id,reason,name) end,s(36),f.Strong,true) again:Dock(TOP) again:SetWide(s(200)) again:DockMargin(s(20),0,s(20),0)
 end
 function win:SetState(s2)
  state=s2 win.state=s2 BM.state=s2 frame.Window=win
  win:BuildContent() win:Changed()
  hook.Run('MMDHL.BoneMapOpened',state)
  if state.reason=='rescue' then win:ShowFirst() end
 end
 -- ---- content ----
 function win:BuildContent()
  content:Clear() win.figurePanel=nil win.viewPanel=nil win.inspector=nil win.table=nil win.jiggleList=nil win.jiggleDetails=nil
  if state.loading then local text=UI.label(content,L'bonemap.loading',f.Title,s(40)) text:Dock(TOP) text:SetContentAlignment(5) text:DockMargin(0,s(120),0,0) return end
  if state.tab=='jiggle' then
   win.jiggleList=BM.BuildJiggleList(win,content) win.jiggleList:Dock(LEFT) win.jiggleList:SetWide(s(320)) win.jiggleList:DockMargin(0,0,s(12),0)
   win.jiggleDetails=BM.BuildJiggleDetails(win,content) win.jiggleDetails:Dock(RIGHT) win.jiggleDetails:SetWide(s(380)) win.jiggleDetails:DockMargin(s(12),0,0,0)
   win.viewPanel=BM.BuildView(win,content) win.viewPanel:Dock(FILL)
  elseif state.view=='list' then
   win.viewPanel=BM.BuildView(win,content) win.viewPanel:Dock(RIGHT) win.viewPanel:SetWide(math.min(s(520),math.floor(.42*(W-s(32))))) win.viewPanel:DockMargin(s(12),0,0,0)
   win.table=BM.BuildTable(win,content) win.table:Dock(FILL)
  else
   win.figurePanel=BM.BuildFigure(win,content) win.figurePanel:Dock(LEFT) win.figurePanel:SetWide(s(320)) win.figurePanel:DockMargin(0,0,s(12),0)
   win.inspector=BM.BuildInspector(win,content) win.inspector:Dock(RIGHT) win.inspector:SetWide(s(380)) win.inspector:DockMargin(s(12),0,0,0)
   win.viewPanel=BM.BuildView(win,content) win.viewPanel:Dock(FILL)
  end
  UI.ownScale(content)
 end
 function win:FitView() win.view.zoom=1 win.view.panX=0 win.view.panY=0 win.view.focus=nil end
 function win:ZoomToHand(side)
  local hand=BM.Value(state,VB..side..'_Hand') if hand<0 then return end
  win.view.focus=BM.ChainBones(state,hand) win.view.zoom=1 win.view.panX=0 win.view.panY=0
 end
 -- Keyboard: edges only, never while typing.
 local keys={z=KEY_Z,y=KEY_Y,tab=KEY_TAB,enter=KEY_ENTER,delete=KEY_DELETE,backspace=KEY_BACKSPACE,f=KEY_F,['1']=KEY_1,['2']=KEY_2}
 local down={}
 local baseThink=frame.Think
 frame.Think=function(self)
  baseThink(self)
  local focus=vgui.GetKeyboardFocus() local text=IsValid(focus) and focus:GetClassName()=='TextEntry'
  -- Keys belong to another window (a question on top, the console) when it has the focus.
  local mine=not gui.IsGameUIVisible() and not (IsValid(focus) and focus~=frame and not focus:HasParent(frame))
  local ctrl=input.IsKeyDown(KEY_LCONTROL) or input.IsKeyDown(KEY_RCONTROL) local shift=input.IsKeyDown(KEY_LSHIFT) or input.IsKeyDown(KEY_RSHIFT)
  for name,code in pairs(keys) do local now=input.IsKeyDown(code)
   if now and not down[name] and mine and not state.loading then local action=BM.KeyAction(name,ctrl,shift,text) if action then win:Key(action) end end
   down[name]=now end
  if state.mode=='convert' then local busy=mmdhl.library.job~=nil if busy~=win.busy then win.busy=busy win:Refresh() end end
  if win.transient and RealTime()>=win.transient.expires then win.transient=nil win:Refresh() end
 end
 function win:Key(action)
  if action=='undo' then if BM.Undo(state) then win:Changed() end
  elseif action=='redo' then if BM.Redo(state) then win:Changed() end
  elseif action=='next_slot' or action=='prev_slot' then if state.tab=='body' then win:Arm(BM.TabSlot(state,win.armed,action=='next_slot' and 1 or -1)) end
  elseif action=='enter' then
   if win.armed then local c=BM.Candidates(state,win.armed,1)[1] if c then win:AssignArmed(c.bone) end
   elseif primary:IsEnabled() then win:Primary() end
  elseif action=='clear' then if win.armed then BM.Clear(state,win.armed) win:Changed() end
  elseif action=='search' then if IsValid(win.inspector) and IsValid(win.inspector.Search) then win.inspector.Search:RequestFocus() end
  elseif action=='view_front' then win.view.mode='front'
  elseif action=='view_side' then win.view.mode='side'
  elseif action=='fit' then win:FitView() end
 end
 if state.loading then win:BuildContent() win:Refresh()
 else
  if opts.native then state.nativeIssues={opts.native} state.nativeAt=state.mapVersion end
  win:BuildContent() win:Changed()
  if opts.select and opts.select~='' then win:Arm(opts.select) end
  hook.Run('MMDHL.BoneMapOpened',state)
 end
 UI.ownScale(frame)
 return win
end

-- ---- the body chart ----
function BM.BuildFigure(win,parent)
 local UI=mmdhl.UI local s,f,state=win.s,win.f,win.state
 local panel=parent:Add('DPanel') panel:SetPaintBackground(false)
 local head=panel:Add('DPanel') head:Dock(TOP) head:SetTall(s(28)) head:SetPaintBackground(false)
 local count=UI.label(head,'',f.Strong,s(28)) count:Dock(FILL)
 local hands=UI.button(head,L'bonemap.figure.hands',function() win.figure='hands' end,s(26),f.Small,'tab') hands:Dock(RIGHT) hands:SetWide(s(64))
 local body=UI.button(head,L'bonemap.figure.body',function() win.figure='body' end,s(26),f.Small,'tab') body:Dock(RIGHT) body:SetWide(s(64)) body:DockMargin(0,0,s(4),0)
 local legend=panel:Add('DPanel') legend:Dock(BOTTOM) legend:SetTall(s(40)) legend:SetPaintBackground(false)
 legend.Paint=function(_,w,h)
  local items={'ok','check','missing','created'} local x,y=s(8),s(10)
  for i,k in ipairs(items) do local col=(i-1)%2 local row=math.floor((i-1)/2) local px,py=s(8)+col*math.floor(w/2),s(10)+row*s(20)
   BM.DrawStatus(k,px+s(6),py,s(6)) draw.SimpleText(L('bonemap.legend.'..k),f.Small,px+s(18),py,Colors.muted,TEXT_ALIGN_LEFT,TEXT_ALIGN_CENTER) end
 end
 local facing=UI.label(panel,L'bonemap.figure.facing',f.Small,s(20)) facing:Dock(BOTTOM) facing:SetTextColor(Colors.muted)
 local canvas=panel:Add('DPanel') canvas:Dock(FILL) canvas:SetPaintBackground(false)
 local hovered
 local function dots()
  local w,h=canvas:GetSize() local list={}
  if win.figure=='hands' then
   local half=w/2 local k=math.min(half/182,(h-s(20))/150)
   for _,slot in ipairs(BM.Slots) do if slot.hand then
    local ox=slot.side=='R' and (half-182*k)/2 or half+(half-182*k)/2
    list[#list+1]={slot=slot,x=ox+slot.hand[1]*k,y=s(20)+slot.hand[2]*k,r=math.max(6*k,s(5)),hit=math.max(9*k,s(8))}
   end end
   return list,k
  end
  local k=math.min(w/380,h/500) local ox,oy=(w-380*k)/2,(h-500*k)/2
  for _,slot in ipairs(BM.Slots) do if slot.pos and not (slot.convertOnly and state.mode~='convert') then
   local r=slot.family=='eye' and 5 or slot.required and 9 or slot.recommended and 8 or 7
   list[#list+1]={slot=slot,x=ox+slot.pos[1]*k,y=oy+slot.pos[2]*k,r=r*k,hit=math.max(14*k,s(10))}
  end end
  return list,k,ox,oy
 end
 canvas.Paint=function(_,w,h)
  local list,k,ox,oy=dots()
  if win.figure=='body' then
   local function p(x,y) return ox+x*k,oy+y*k end
   local hx,hy=p(190,50) poly(circlePoly(hx,hy,32*k,24),Colors.silhouette)
   local ax,ay=p(190,80) local bx,by=p(190,104) capsule(ax,ay,bx,by,10*k,Colors.silhouette)
   local q1x,q1y=p(150,112) local q2x,q2y=p(230,112) local q3x,q3y=p(216,232) local q4x,q4y=p(164,232) poly(clockwise({{x=q1x,y=q1y},{x=q2x,y=q2y},{x=q3x,y=q3y},{x=q4x,y=q4y}}),Colors.silhouette)
   local r1x,r1y=p(164,232) local r2x,r2y=p(216,232) local r3x,r3y=p(226,268) local r4x,r4y=p(154,268) poly(clockwise({{x=r1x,y=r1y},{x=r2x,y=r2y},{x=r3x,y=r3y},{x=r4x,y=r4y}}),Colors.silhouette)
   for _,m in ipairs({1,-1}) do local function q(x,y) return p(m==1 and x or 380-x,y) end
    local sx,sy=q(252,126) local ex,ey=q(274,200) local wx,wy=q(290,270) capsule(sx,sy,ex,ey,13*k,Colors.silhouette) capsule(ex,ey,wx,wy,11*k,Colors.silhouette)
    local dx,dy=wx-ex,wy-ey local len=math.sqrt(dx*dx+dy*dy) if len>0 then poly(circlePoly(wx+dx/len*16*k,wy+dy/len*16*k,12*k,12),Colors.silhouette) end
    local tx,ty=q(214,268) local kx,ky=q(220,372) local fx,fy=q(224,462) local tox,toy=q(234,486) capsule(tx,ty,kx,ky,17*k,Colors.silhouette) capsule(kx,ky,fx,fy,13*k,Colors.silhouette) capsule(fx,fy,tox,toy,9*k,Colors.silhouette)
   end
   draw.SimpleText(L'bonemap.figure.mark_right',f.Strong,ox+14*k,oy+10*k,Colors.muted) draw.SimpleText(L'bonemap.figure.mark_left',f.Strong,ox+366*k,oy+10*k,Colors.muted,TEXT_ALIGN_RIGHT)
  else
   draw.SimpleText(L'bonemap.figure.right_hand',f.Small,w/4,s(2),Colors.muted,TEXT_ALIGN_CENTER) draw.SimpleText(L'bonemap.figure.left_hand',f.Small,w*3/4,s(2),Colors.muted,TEXT_ALIGN_CENTER)
   for _,side in ipairs({'L','R'}) do local half=w/2 local ox2=side=='R' and (half-182*k)/2 or half+(half-182*k)/2
    local function p(x,y) if side=='R' then x=182-x end return ox2+x*k,s(20)+y*k end
    local wx,wy=p(91,140) local a1x,a1y=p(69,140) local a2x,a2y=p(113,140) local b1x,b1y=p(50,84) local b2x,b2y=p(128,96)
    poly(clockwise({{x=a1x,y=a1y},{x=b1x,y=b1y},{x=b2x,y=b2y},{x=a2x,y=a2y}}),Colors.silhouette)
   end
  end
  for _,d in ipairs(list) do
   local status=BM.StatusOf(state,d.slot.key)
   if win.armed==d.slot.key then ring(d.x,d.y,d.r+4+3,Colors.accent,3) end
   if hovered==d.slot.key then circle(d.x,d.y,d.r+6,ColorAlpha(Colors.hover,170)) end
   if win.selected and status~='ok' then local anchor=BM.AnchorOf(state,d.slot) if d.slot.side=='' or (win.regions and win.regions[win.selected]==(d.slot.side=='L' and 'left' or 'right')) then if anchor<0 or BM.Below(state,win.selected,anchor) then ring(d.x,d.y,d.r+3,Colors.accent,2) end end end
   BM.DrawStatus(status,d.x,d.y,hovered==d.slot.key and d.r*1.25 or d.r)
  end
  local text
  if win.figure=='hands' then local l,r=0,0 for _,slot in ipairs(BM.Slots) do if slot.segment>0 and BM.Value(state,slot.key)>=0 then if slot.side=='L' then l=l+1 else r=r+1 end end end text=L('bonemap.fingers.count',{left=l,right=r})
  else local sum=win.summary or {assigned=0,required=15} text=L('bonemap.figure.count',{assigned=sum.assigned,required=sum.required}) end
  if count:GetText()~=text then count:SetText(text) end
  body.Selected=win.figure=='body' hands.Selected=win.figure=='hands'
 end
 canvas.OnCursorMoved=function(_,x,y)
  local list=dots() hovered=nil
  for _,d in ipairs(list) do local dx,dy=d.x-x,d.y-y if dx*dx+dy*dy<=d.hit*d.hit then hovered=d.slot.key end end
  win.hoverSlot=hovered
  canvas:SetTooltip(hovered and (BM.PartLabel(hovered)..'\n'..L('bonemap.tooltip.technical',{valve=hovered,mmd=BM.SlotByKey[hovered].mmdJp})) or false)
 end
 canvas.OnCursorExited=function() hovered=nil win.hoverSlot=nil end
 canvas.OnMousePressed=function(_,code)
  if code==MOUSE_RIGHT then win.selected=nil win:Arm(nil) return end
  if not hovered then return end
  if BM.StatusOf(state,hovered)=='disabled' then win:Notify(L('bonemap.fingers.need_hand',{hand=BM.PartLabel(VB..BM.SlotByKey[hovered].side..'_Hand')})) return end
  if win.selected then BM.Assign(state,hovered,win.selected,'user') win.selected=nil win:Changed() return end
  if win.armed==hovered then win:Arm(nil) else win:Arm(hovered) end
 end
 return panel
end

-- ---- the model view: the skeleton over its mesh points ----
function BM.BuildView(win,parent)
 local UI=mmdhl.UI local s,f,state=win.s,win.f,win.state
 local panel=parent:Add('DPanel') panel:SetPaintBackground(false)
 local bar=panel:Add('DPanel') bar:Dock(TOP) bar:SetTall(s(32)) bar:SetPaintBackground(false) bar:DockMargin(0,0,0,s(6))
 local front=UI.button(bar,L'bonemap.view.front',function() win.view.mode='front' end,s(30),f.Small,'tab') front:Dock(LEFT) front:SetWide(s(64))
 local side=UI.button(bar,L'bonemap.view.side',function() win.view.mode='side' end,s(30),f.Small,'tab') side:Dock(LEFT) side:SetWide(s(64)) side:DockMargin(s(4),0,0,0)
 local filter=bar:Add('DComboBox') filter:Dock(LEFT) filter:SetWide(s(170)) filter:DockMargin(s(8),0,0,0) UI.styleChoices(filter,s,f.Small)
 filter:AddChoice(L'bonemap.view.filter_body','body',not win.view.all) filter:AddChoice(L('bonemap.view.filter_all',{count=#state.bones}),'all',win.view.all==true)
 filter.OnSelect=function(_,_,_,data) win.view.all=data=='all' end
 local plus=UI.button(bar,'+',function() win.view.zoom=math.min(16,win.view.zoom*1.15) end,s(30),f.Strong) plus:Dock(RIGHT) plus:SetWide(s(32))
 surface.SetFont(f.Small)
 local fit=UI.button(bar,L'bonemap.view.fit',function() win:FitView() end,s(30),f.Small) fit:Dock(RIGHT) fit:SetWide(math.max(s(48),s(20)+(surface.GetTextSize(L'bonemap.view.fit')))) fit:DockMargin(s(4),0,s(4),0)
 local minus=UI.button(bar,'−',function() win.view.zoom=math.max(.5,win.view.zoom/1.15) end,s(30),f.Strong) minus:Dock(RIGHT) minus:SetWide(s(32))
 local canvas=panel:Add('DPanel') canvas:Dock(FILL) canvas:SetPaintBackground(false)
 local projected={} local hoverBone local dragging
 -- The part using each bone and the armed part's suggestions, kept until the assignment changes.
 local usingCache,candidateCache={},{}
 local function usingMap()
  if usingCache.version~=state.mapVersion then
   local map={}
   for _,slot in ipairs(BM.Slots) do if not (slot.convertOnly and state.mode~='convert') then local b=BM.Value(state,slot.key) if b>=0 and map[b]==nil then map[b]=slot.key end end end
   usingCache.version=state.mapVersion usingCache.map=map
  end
  return usingCache.map
 end
 local function suggestions()
  local key=tostring(win.armed)..':'..state.mapVersion..':'..tostring(state.turn)
  if candidateCache.key~=key then candidateCache.key=key candidateCache.list=win.armed and BM.Candidates(state,win.armed,3) or {} end
  return candidateCache.list
 end
 -- Which bones show: assigned, candidates and hovered bones always.
 local function visible(b,candidates)
  local bone=state.bones[b+1]
  if win.view.all then return true end
  if usingMap()[b] or candidates[b] or b==hoverBone or b==win.selected then return true end
  if bone.subtree==0 then return false end
  if bone.weighted==0 and (bone.flags.helper or bone.flags.ik) then return false end
  if state.mode=='fit' then local p=bone.parent while p>=0 do if state.bones[p+1].flags.secondary then return false end p=state.bones[p+1].parent end end
  return true
 end
 local function layout(w,h,candidates)
  local view=win.view local mode=view.mode local turn=state.turn or 0
  local minX,maxX,minY,maxY
  local function add(x,y,z) if turn==180 then x,z=-x,-z end local u=mode=='side' and -z or x if not minX then minX,maxX,minY,maxY=u,u,y,y end minX=math.min(minX,u) maxX=math.max(maxX,u) minY=math.min(minY,y) maxY=math.max(maxY,y) end
  if view.focus then for _,b in ipairs(view.focus) do local p=state.bones[b+1].pos add(p[1],p[2],p[3]) end
  else for i=1,#state.points,4 do add(state.points[i],state.points[i+1],state.points[i+2]) end
   for i,bone in ipairs(state.bones) do if visible(i-1,candidates) then add(bone.pos[1],bone.pos[2],bone.pos[3]) end end end
  if not minX then minX,maxX,minY,maxY=-1,1,0,2 end
  local margin=view.focus and .2 or 0
  local rx,ry=math.max(.05,(maxX-minX)*(1+margin)),math.max(.05,(maxY-minY)*(1+margin))
  local scale=math.min((w-s(56))/rx,(h-s(56))/ry)*view.zoom
  local cx,cy=w/2+view.panX,h/2+view.panY
  local mx,my=(minX+maxX)/2,(minY+maxY)/2
  return {mode=mode,turn=turn,cx=cx,cy=cy,mx=mode=='side' and 0 or mx,my=my,mz=mode=='side' and -mx or 0,scale=scale}
 end
 canvas.Paint=function(_,w,h)
  draw.RoundedBox(4,0,0,w,h,color_white)
  local candidates,ranks={},{}
  for rank,c in ipairs(suggestions()) do candidates[c.bone]=true ranks[c.bone]=rank end
  local using=usingMap()
  local view=layout(w,h,candidates) win.projection=view
  local regions=win.regions or {}
  local chainKind={} if state.tab=='jiggle' then for _,c in ipairs(state.chains) do for _,b in ipairs(BM.ChainBones(state,c.root)) do chainKind[b]=c end end end
  -- Points, coloured by the region of the bone they follow.
  local size=s(2)
  for i=1,#state.points,4 do
   local x,y=BM.Project(view,state.points[i],state.points[i+1],state.points[i+2]) local b=state.points[i+3]
   local col
   if state.tab=='jiggle' then local c=chainKind[b] col=c and (c.enabled and Colors[c.kind] or Colors.disabled) or Colors.unassigned
   else col=regionColor(b>=0 and regions[b] or 'unassigned') end
   if b>=0 and b==hoverBone then surface.SetDrawColor(Colors.hover.r,Colors.hover.g,Colors.hover.b,230) surface.DrawRect(x-1,y-1,s(3),s(3))
   else surface.SetDrawColor(col.r,col.g,col.b,140) surface.DrawRect(x,y,size,size) end
  end
  projected={}
  local positions={}
  for i,bone in ipairs(state.bones) do local b=i-1 if visible(b,candidates) then local x,y=BM.Project(view,bone.pos[1],bone.pos[2],bone.pos[3]) positions[b]={x,y} projected[#projected+1]={bone=b,x=x,y=y,rank=ranks[b],weight=bone.weighted} end end
  -- Links: the mapped body reads as a stick figure.
  for b,p in pairs(positions) do local bone=state.bones[b+1] local q=bone.parent>=0 and positions[bone.parent]
   if q then local key,parentKey=using[b],using[bone.parent]
    if key and parentKey and state.tab=='body' then local st=BM.StatusOf(state,key) thickLine(q[1],q[2],p[1],p[2],Colors[st] or Colors.link,3)
    else surface.SetDrawColor(Colors.link) surface.DrawLine(q[1],q[2],p[1],p[2]) end end end
  local anchor=-1 if win.armed then anchor=BM.AnchorOf(state,BM.SlotByKey[win.armed]) end
  for b,p in pairs(positions) do local bone=state.bones[b+1] local key=using[b]
   local alpha=(win.armed and anchor>=0 and not BM.Below(state,b,anchor)) and 70 or 255
   if b==hoverBone then circle(p[1],p[2],11,ColorAlpha(Colors.hover,170)) end
   if key then local slot=BM.SlotByKey[key] local col=regionColor(slot.side=='L' and 'left' or slot.side=='R' and 'right' or 'body')
    circle(p[1],p[2],6,ColorAlpha(color_white,alpha)) circle(p[1],p[2],5,ColorAlpha(col,alpha))
    if (slot.family=='hand' or slot.family=='foot') and state.tab=='body' then draw.SimpleText(slot.side=='L' and L'bonemap.figure.mark_left' or L'bonemap.figure.mark_right',f.Small,p[1]+8,p[2]-8,Colors.muted) end
   elseif bone.weighted>0 then circle(p[1],p[2],3,ColorAlpha(Colors.muted,alpha))
   else circle(p[1],p[2],2,ColorAlpha(Colors.disabled,alpha)) end
   if win.armed and BM.Value(state,win.armed)==b then ring(p[1],p[2],9,Colors.accent,2) end
   if ranks[b] then circle(p[1]+9,p[2]-9,8,Colors.accent) draw.SimpleText(tostring(ranks[b]),f.Small,p[1]+9,p[2]-9,color_white,TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER) end
   if win.selected==b then ring(p[1],p[2],9,Colors.accent,2) end
  end
  -- Markers and help.
  if view.mode=='front' then draw.SimpleText(L'bonemap.figure.mark_right',f.Strong,s(10),s(8),Colors.muted) draw.SimpleText(L'bonemap.figure.mark_left',f.Strong,w-s(10),s(8),Colors.muted,TEXT_ALIGN_RIGHT)
  else draw.SimpleText(L'bonemap.view.front_marker',f.Strong,s(10),s(8),Colors.muted) end
  draw.SimpleText(L('bonemap.view.height',{height=string.format('%.2f',state.height or 0)}),f.Small,s(8),h-s(6),Colors.muted,TEXT_ALIGN_LEFT,TEXT_ALIGN_BOTTOM)
  draw.SimpleText(L'bonemap.view.help',f.Small,w-s(8),h-s(6),Colors.muted,TEXT_ALIGN_RIGHT,TEXT_ALIGN_BOTTOM)
  -- The prompt says what a click does.
  local prompt=win.armed and (L('bonemap.prompt.pick',{part=BM.PartLabel(win.armed)})..' · '..L'bonemap.prompt.stop') or win.addMode and (L'bonemap.prompt.add_chain'..' · '..L'bonemap.prompt.stop')
  if prompt then surface.SetFont(f.Body) local tw=surface.GetTextSize(prompt) local pw=math.min(w-s(20),tw+s(30)) draw.RoundedBox(s(15),(w-pw)/2,s(8),pw,s(30),Colors.accent) draw.SimpleText(prompt,f.Body,w/2,s(23),color_white,TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER) end
  -- Hover details.
  if hoverBone and positions[hoverBone] then
   local bone=state.bones[hoverBone+1] local lines={bone.name}
   if bone.english and bone.english~='' and bone.english~=bone.name then lines[#lines+1]=bone.english end
   if using[hoverBone] then lines[#lines+1]=L('bonemap.tooltip.used',{part=BM.PartLabel(using[hoverBone])}) end
   lines[#lines+1]=bone.weighted>0 and L('bonemap.tooltip.moves',{count=string.Comma(bone.weighted)}) or L'bonemap.tooltip.moves_none'
   if bone.flags.secondary then lines[#lines+1]=L'bonemap.tooltip.physics' end
   if bone.nameIssue=='cp932' or bone.nameIssue=='cp936' then lines[#lines+1]=L'bonemap.tooltip.name_cp932' elseif bone.nameIssue=='replaced' then lines[#lines+1]=L'bonemap.tooltip.name_replaced' end
   surface.SetFont(f.Small) local tw=0 for _,line in ipairs(lines) do tw=math.max(tw,(surface.GetTextSize(line))) end
   local bw,bh=tw+s(16),#lines*s(18)+s(8) local p=positions[hoverBone] local bx,by=math.min(p[1]+s(14),w-bw-s(4)),math.max(s(4),math.min(p[2]-bh-s(6),h-bh-s(4)))
   draw.RoundedBox(4,bx,by,bw,bh,Color(31,43,58,235)) for i,line in ipairs(lines) do draw.SimpleText(line,f.Small,bx+s(8),by+s(4)+(i-1)*s(18),color_white) end
  end
  front.Selected=view.mode=='front' side.Selected=view.mode=='side'
 end
 canvas.OnCursorMoved=function(_,x,y)
  if dragging then win.view.panX=dragging.panX+(x-dragging.x) win.view.panY=dragging.panY+(y-dragging.y) return end
  hoverBone=BM.HitTest(projected,x,y,s(8),s(6))
  win.hoverBone=hoverBone
 end
 canvas.OnCursorExited=function() hoverBone=nil win.hoverBone=nil end
 canvas.OnMouseWheeled=function(_,delta) local factor=delta>0 and 1.15 or 1/1.15 local z=math.Clamp(win.view.zoom*factor,.5,16)
  local x,y=canvas:CursorPos() local w,h=canvas:GetSize() local cx,cy=w/2+win.view.panX,h/2+win.view.panY
  win.view.panX=win.view.panX+(x-cx)*(1-z/win.view.zoom) win.view.panY=win.view.panY+(y-cy)*(1-z/win.view.zoom) win.view.zoom=z return true end
 canvas.OnMousePressed=function(_,code)
  local x,y=canvas:CursorPos()
  if code==MOUSE_RIGHT or code==MOUSE_MIDDLE then
   if code==MOUSE_RIGHT and (win.armed or win.addMode or win.selected) then win.addMode=nil win.selected=nil win:Arm(nil) return end
   dragging={x=x,y=y,panX=win.view.panX,panY=win.view.panY} canvas:MouseCapture(true) return end
  local best,list=BM.HitTest(projected,x,y,s(8),s(6),(function() local n={} for _,p in ipairs(projected) do n[p.bone]=state.bones[p.bone+1].name end return n end)())
  local function pick(b)
   if win.addMode then local ok,why,chain=BM.AddChain(state,b) if not ok then win:Notify(why=='bonemap.jiggle.included' and L('bonemap.jiggle.included',{chain=state.bones[chain.root+1].name}) or L('bonemap.jiggle.refused',{bone=state.bones[b+1].name})) else win.addMode=nil win.selectedGroup=chain.kind win:Changed() end return end
   -- On the jiggle tab a click selects the swinging part's group.
   if state.tab=='jiggle' then for _,c in ipairs(state.chains) do if c.root==b or BM.Below(state,b,c.root) then win.selectedGroup=c.kind win:Refresh() return end end return end
   win:ClickBone(b)
  end
  if list then
   local menu=DermaMenu() menu:AddOption(L'bonemap.overlap.title'):SetEnabled(false) menu:AddSpacer()
   for _,b in ipairs(list) do local bone=state.bones[b+1] local o=menu:AddOption(L('bonemap.overlap.item',{name=bone.name,count=string.Comma(bone.weighted)}),function() pick(b) end) o.OnCursorEntered=function() hoverBone=b end end
   UI.ownScale(menu) menu:Open() return
  end
  if best then pick(best) end
 end
 canvas.OnMouseReleased=function() dragging=nil canvas:MouseCapture(false) end
 -- A double click on empty space fits the view.
 local lastClick=0
 local press=canvas.OnMousePressed
 canvas.OnMousePressed=function(self,code)
  if code==MOUSE_LEFT then
   local x,y=canvas:CursorPos()
   if RealTime()-lastClick<.3 and not BM.HitTest(projected,x,y,s(8),s(6)) then win:FitView() lastClick=0 return end
   lastClick=RealTime()
  end
  press(self,code)
 end
 return panel
end

-- ---- the inspector: summary, the armed part, or a selected bone ----
function BM.BuildInspector(win,parent)
 local UI=mmdhl.UI local s,f=win.s,win.f
 local panel=parent:Add('DScrollPanel') panel.Paint=function(_,w,h) draw.RoundedBox(4,0,0,w,h,color_white) end
 local canvas=panel:GetCanvas() canvas:DockPadding(s(10),s(8),s(10),s(8))
 local function separator() local line=canvas:Add('DPanel') line:Dock(TOP) line:SetTall(1) line:DockMargin(0,s(8),0,s(8)) line.Paint=function(_,w,h) surface.SetDrawColor(Colors.line) surface.DrawRect(0,0,w,h) end end
 local function text(value,font,color,tall) local l=UI.label(canvas,value,font,tall or s(20)) l:Dock(TOP) l:SetWrap(true) l:SetAutoStretchVertical(true) if color then l:SetTextColor(color) end return l end
 local function action(label,callback,style) local b=UI.button(canvas,label,callback,s(34),f.Body,style) b:Dock(TOP) b:DockMargin(0,0,0,s(6)) return b end
 local function boneList(key)
  local state=win.state
  local search=canvas:Add('DTextEntry') search:Dock(TOP) search:SetTall(s(30)) search:SetFont(f.Body) search:SetPlaceholderText(L'bonemap.card.search') search:DockMargin(0,0,0,s(6)) search:SetText(win.searchText or '')
  panel.Search=search
  local holder=canvas:Add('DPanel') holder:Dock(TOP) holder:SetTall(s(240)) holder:SetPaintBackground(false)
  local function fill()
   holder:Clear()
   local query=string.Trim(search:GetText()):lower()
   if query=='' then
    -- The hierarchy, opened along the armed part's current bone or its best candidate.
    local tree=holder:Add('DTree') tree:Dock(FILL)
    local open={} local target=key and BM.Value(state,key) or -1
    if target<0 and key then local c=BM.Candidates(state,key,1)[1] target=c and c.bone or -1 end
    local p=target while p>=0 do open[p]=true p=state.bones[p+1].parent end
    local function label(b) local bone=state.bones[b+1] local using=BM.SlotUsing(state,b) return bone.name..(using and ('  ·  '..BM.PartLabel(using)) or '')..'   '..string.Comma(bone.weighted) end
    local function add(node,b,depth)
     local bone=state.bones[b+1]
     local n=node:AddNode(label(b),#bone.children>0 and 'icon16/bullet_black.png' or 'icon16/bullet_white.png') n.Label:SetFont(f.Small)
     n.DoClick=function() if win.armed then win:AssignArmed(b) else win:ClickBone(b) end end
     n.OnCursorEntered=function() win.hoverBone=b end
     if #bone.children>0 then n:SetForceShowExpander(true)
      local filled=false
      local expand=n.SetExpanded
      n.SetExpanded=function(self,on,...) if on and not filled then filled=true for _,c in ipairs(bone.children) do add(self,c,depth+1) end end return expand(self,on,...) end
      if open[b] and depth<64 then n:SetExpanded(true) end
     end
     if b==target then tree:SetSelectedItem(n) end
    end
    for _,b in ipairs(state.order) do if state.bones[b+1].parent<0 then add(tree,b,0) end end
   else
    local list=holder:Add('DListView') list:Dock(FILL) list:SetMultiSelect(false) list:SetDataHeight(s(24)) list:SetHeaderHeight(s(24))
    list:AddColumn(L'bonemap.column.bone'):SetWidth(s(190)) list:AddColumn(L'bonemap.column.used'):SetWidth(s(100)) list:AddColumn(L'bonemap.column.moves'):SetFixedWidth(s(60))
    local shown=0 local more=0
    for i,bone in ipairs(state.bones) do local b=i-1
     local meaning=bone.meaning~='' and bone.meaning or ''
     if (bone.name:lower():find(query,1,true) or (bone.english or ''):lower():find(query,1,true) or meaning:find(query,1,true)) then
      if shown<200 then local using=BM.SlotUsing(state,b) local row=list:AddLine(bone.name,using and BM.PartLabel(using) or '',string.Comma(bone.weighted)) row.bone=b shown=shown+1 else more=more+1 end
     end
    end
    if more>0 then list:AddLine(L('bonemap.card.more_results',{count=more}),'','') end
    list.OnRowSelected=function(_,_,row) if row.bone then if win.armed then win:AssignArmed(row.bone) else win:ClickBone(row.bone) end end end
    for _,c in pairs(list.Columns) do c.Header:SetFont(f.Small) end
   end
  end
  search.OnChange=function() win.searchText=search:GetText() fill() end
  search.OnEnter=function() local query=string.Trim(search:GetText()):lower() if query=='' or not win.armed then return end
   for i,bone in ipairs(state.bones) do if bone.name:lower():find(query,1,true) then win:AssignArmed(i-1) return end end end
  fill()
 end
 -- Not Rebuild: DScrollPanel calls its own Rebuild on every layout pass.
 function panel:RebuildContent()
  local state=win.state
  local keepFocus=IsValid(panel.Search) and panel.Search:HasFocus()
  canvas:Clear() panel.Search=nil
  local sum=win.summary or BM.Summary(state)
  local key=win.armed
  if key then
   local slot=BM.SlotByKey[key] local status=BM.StatusOf(state,key)
   local head=canvas:Add('DPanel') head:Dock(TOP) head:SetTall(s(30)) head:SetPaintBackground(false)
   head.Paint=function(_,w,h) BM.DrawStatus(status,s(10),h/2,s(9)) draw.SimpleText(BM.PartLabel(key),f.Strong,s(26),h/2,Colors.ink,TEXT_ALIGN_LEFT,TEXT_ALIGN_CENTER) draw.SimpleText(L('bonemap.group.'..BM.SummaryGroup(slot)),f.Small,w,h/2,Colors.muted,TEXT_ALIGN_RIGHT,TEXT_ALIGN_CENTER) end
   if slot.segment>0 then text(L'bonemap.help.finger',f.Small,Colors.muted)
   else text(L('bonemap.help.'..(Help[slot.id] or slot.id),{examples=BM.ExamplesFor(slot)}),f.Small,Colors.muted) end
   separator()
   local value=BM.Value(state,key)
   local now=canvas:Add('DPanel') now:Dock(TOP) now:SetTall(s(30)) now:SetPaintBackground(false)
   local clear=UI.button(now,'✕',function() BM.Clear(state,key) win:Changed() end,s(26),f.Small) clear:Dock(RIGHT) clear:SetWide(s(26)) clear:SetTooltip(slot.required and L'bonemap.issue.required_tip' or L'bonemap.card.clear_tip') clear:SetEnabled(value>=0)
   now.Paint=function(_,w,h)
    draw.SimpleText(L'bonemap.card.now',f.Small,0,h/2,Colors.muted,TEXT_ALIGN_LEFT,TEXT_ALIGN_CENTER)
    local x=s(50)
    if value>=0 then local col=regionColor(slot.side=='L' and 'left' or slot.side=='R' and 'right' or 'body') circle(x+s(5),h/2,s(5),col)
     draw.SimpleText(state.bones[value+1].name,f.Strong,x+s(14),h/2,col,TEXT_ALIGN_LEFT,TEXT_ALIGN_CENTER)
     surface.SetFont(f.Strong) local tw=surface.GetTextSize(state.bones[value+1].name)
     draw.SimpleText(originText(state,key),f.Small,x+s(20)+tw,h/2,Colors.muted,TEXT_ALIGN_LEFT,TEXT_ALIGN_CENTER)
    else draw.SimpleText(slot.required and L'bonemap.card.none' or L'bonemap.card.created',f.Body,x,h/2,slot.required and Colors.missing or Colors.created,TEXT_ALIGN_LEFT,TEXT_ALIGN_CENTER) end
   end
   if state.mode=='fit' and #(state.aliases[key] or {})>0 then local names={} for _,a in ipairs(state.aliases[key]) do if state.bones[a+1] then names[#names+1]=state.bones[a+1].name end end
    text(L('bonemap.card.also_moves',{bones=table.concat(names,', ')}),f.Small,Colors.muted) end
   for _,i in ipairs(BM.IssuesOf(state,key)) do text(issueText(state,i),f.Small,i.severity=='error' and Colors.missing or Colors.check) end
   separator()
   text(L'bonemap.card.suggestions',f.Strong)
   local list=BM.Candidates(state,key,3)
   if #list==0 then text(L'bonemap.card.no_suggestions',f.Small,Colors.muted) end
   local best=BM.BestMatch(list)
   for rank,c in ipairs(list) do
    local bone=state.bones[c.bone+1]
    local reasons={} for _,r in ipairs(c.reasons) do local a={} for k,v in pairs(r.args or {}) do a[k]=v end if a.part and BM.SlotByKey[a.part] then a.part=BM.PartLabel(a.part) end if a.count then a.count=string.Comma(a.count) end reasons[#reasons+1]=L(r.key,a) end
    local row=canvas:Add('DButton') row:Dock(TOP) row:SetTall(s(44)) row:SetText('') row:DockMargin(0,0,0,s(4))
    row.Paint=function(self,w,h) draw.RoundedBox(4,0,0,w,h,self:IsHovered() and Color(232,241,251) or Color(244,246,249))
     circle(s(14),s(15),s(9),Colors.accent) draw.SimpleText(tostring(rank),f.Small,s(14),s(15),color_white,TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER)
     draw.SimpleText(bone.name,f.Strong,s(30),s(15),Colors.ink,TEXT_ALIGN_LEFT,TEXT_ALIGN_CENTER)
     if rank==1 and best then draw.SimpleText(L'bonemap.card.best',f.Small,w-s(8),s(15),Colors.ok,TEXT_ALIGN_RIGHT,TEXT_ALIGN_CENTER) end
     draw.SimpleText(table.concat(reasons,' · '),f.Small,s(30),s(33),Colors.muted,TEXT_ALIGN_LEFT,TEXT_ALIGN_CENTER) end
    row.DoClick=function() win:AssignArmed(c.bone) end
    row.OnCursorEntered=function() win.hoverBone=c.bone end
   end
   separator()
   boneList(key)
   separator()
   local origin=key:sub(1,4)=='Eye_' and state.eyes[key:sub(5)].origin or state.slots[key].origin
   if origin=='guess' and value>=0 then action(L('bonemap.card.keep',{bone=state.bones[value+1].name}),function() BM.AcceptGuess(state,key) BM.Validate(state) win.armed=BM.NextSlot(state,key,1,win.checkRun) win:Changed() end,true) end
   if not slot.required and value>=0 then action(L'bonemap.card.leave_empty',function() BM.Clear(state,key) BM.Validate(state) win.armed=BM.NextSlot(state,key,1,win.checkRun) win:Changed() end) end
   local a=state.auto[key] local autoBone=a and (a.bone>=0 and a.bone or (a.suggested or -1)) or -1
   if a and autoBone~=value then action(L'bonemap.card.use_auto',function() BM.UseAuto(state,key) win:Changed() end) end
   action(L'bonemap.card.done',function() win.checkRun=nil win:Arm(nil) end)
  elseif win.selected then
   local b=win.selected local bone=state.bones[b+1]
   text(bone.name,f.Strong)
   if bone.meaning~='' and bone.meaning~='helper' then local label for _,slot in ipairs(BM.Slots) do if slot.family==bone.meaning and (slot.side=='' or slot.side==bone.side) and (slot.segment==0 or slot.segment==bone.segment) then label=BM.PartLabel(slot.key) break end end
    if label then text(L('bonemap.tooltip.looks_like',{part=label}),f.Small,Colors.muted) end end
   text(L('bonemap.card.bone_info',{parent=bone.parent>=0 and state.bones[bone.parent+1].name or '—',children=#bone.children}),f.Small,Colors.muted)
   text(bone.weighted>0 and L('bonemap.tooltip.moves',{count=string.Comma(bone.weighted)}) or L'bonemap.tooltip.moves_none',f.Small,Colors.muted)
   separator()
   text(L'bonemap.card.use_for',f.Strong)
   local combo=canvas:Add('DComboBox') combo:Dock(TOP) combo:SetTall(s(30)) UI.styleChoices(combo,s,f.Body) combo:DockMargin(0,0,0,s(6))
   local region=win.regions and win.regions[b] local order={}
   for _,slot in ipairs(BM.Slots) do if not (slot.convertOnly and state.mode~='convert') then local st=BM.StatusOf(state,slot.key) local score=(slot.required and st~='ok') and 0 or ((slot.side=='L' and region=='left') or (slot.side=='R' and region=='right')) and 1 or 2 order[#order+1]={slot=slot,score=score} end end
   table.sort(order,function(x,y) if x.score~=y.score then return x.score<y.score end return x.slot.index<y.slot.index end)
   for _,o in ipairs(order) do combo:AddChoice(BM.PartLabel(o.slot.key),o.slot.key) end
   combo:ChooseOptionID(1)
   action(L'bonemap.card.assign',function() local _,key2=combo:GetSelected() if key2 then BM.Assign(state,key2,b,'user') win.selected=nil win:Changed() end end,true)
   separator() boneList(nil)
  else
   local head=canvas:Add('DPanel') head:Dock(TOP) head:SetTall(s(34)) head:SetPaintBackground(false)
   local headline=sum.headline
   local titleText=headline=='ok' and L'bonemap.summary.ok' or headline=='check' and (#sum.check==1 and L'bonemap.summary.check_one' or L('bonemap.summary.check_many',{count=#sum.check}))
    or headline=='missing' and (#sum.missing==0 and (sum.errors==1 and L'bonemap.status.problems_one' or L('bonemap.status.problems_many',{count=sum.errors}))
     or #sum.missing==1 and L'bonemap.summary.missing_one' or L('bonemap.summary.missing_many',{count=#sum.missing})) or L'bonemap.summary.not_humanoid'
   head.Paint=function(_,w,h) BM.DrawStatus(headline=='ok' and 'ok' or headline=='check' and 'check' or headline=='missing' and 'missing' or 'created',s(14),h/2,s(13)) end
   local t=UI.label(head,titleText,f.Title,s(34)) t:Dock(FILL) t:DockMargin(s(34),0,0,0)
   if headline=='ok' then text(L('bonemap.summary.ok_text',{button=primaryLabel(state)}),f.Body,Colors.muted)
   elseif headline=='check' then text(L('bonemap.summary.check_text',{parts=BM.PartList(sum.check,4)}),f.Body,Colors.muted)
    action(L'bonemap.summary.check_button',function() win.checkRun=true win:Arm(sum.check[1]) end,true)
   elseif headline=='missing' then if #sum.missing>0 then text(L('bonemap.summary.missing_text',{parts=BM.PartList(sum.missing,4)}),f.Body,Colors.muted) end
    action(L'bonemap.summary.start',function() win:ShowFirst() end,true)
   else text(L('bonemap.summary.not_humanoid_text',{found=sum.assigned,bones=#state.bones}),f.Body,Colors.muted)
    action(L'bonemap.summary.anyway',function() win:Arm(VB..'Pelvis') end,true)
    if state.mode=='convert' and (not mmdhl.library.StaticImportable or mmdhl.library.StaticImportable(state.source)) then action(L'bonemap.summary.as_prop',function() local source=state.source BM.sessions[source]=nil win.finish('cancelled') mmdhl.library.StartStaticImport(source) end) end
   end
   separator()
   text(L'bonemap.summary.parts',f.Strong)
   for _,g in ipairs(BM.GroupOrder) do local entry=sum.groups[g] if entry then
    local row=canvas:Add('DButton') row:Dock(TOP) row:SetTall(s(32)) row:SetText('')
    row.Paint=function(self,w,h) if self:IsHovered() then draw.RoundedBox(4,0,0,w,h,Color(238,243,249)) end BM.DrawStatus(entry.worst,s(12),h/2,s(8))
     draw.SimpleText(L('bonemap.group.'..g),f.Body,s(28),h/2,Colors.ink,TEXT_ALIGN_LEFT,TEXT_ALIGN_CENTER) draw.SimpleText(L('bonemap.group.count',{done=entry.done,total=entry.total}),f.Small,w-s(8),h/2,Colors.muted,TEXT_ALIGN_RIGHT,TEXT_ALIGN_CENTER) end
    row.DoClick=function() local first for _,slot in ipairs(BM.Slots) do if BM.SummaryGroup(slot)==g and not first and BM.StatusOf(state,slot.key)~='ok' and BM.StatusOf(state,slot.key)~='disabled' then first=slot.key end end
     if not first then for _,slot in ipairs(BM.Slots) do if BM.SummaryGroup(slot)==g and not first then first=slot.key end end end if first then win:Arm(first) end end
   end end
   local problems={} local notes={}
   for _,i in ipairs(state.issues) do if i.severity=='info' then notes[#notes+1]=i else problems[#problems+1]=i end end
   if #problems>0 then
    separator() text(L('bonemap.summary.problems',{count=#problems}),f.Strong)
    for _,i in ipairs(problems) do
     local row=canvas:Add('DPanel') row:Dock(TOP) row:SetPaintBackground(false) row:DockMargin(0,0,0,s(4))
     local fix
     if i.code=='sides_swapped' then fix=UI.button(row,L'bonemap.fix.swap',function() BM.SwapSides(state,false) win:Changed() end,s(26),f.Small)
     elseif i.code=='facing' then fix=UI.button(row,L'bonemap.fix.swap',function() BM.SwapSides(state,true) win:Changed() end,s(26),f.Small)
     elseif i.code=='side' and i.mirror and i.mirror>=0 then fix=UI.button(row,L('bonemap.fix.use',{bone=state.bones[i.mirror+1].name}),function() BM.Assign(state,i.slot,i.mirror,'user') win:Changed() end,s(26),f.Small)
     elseif i.code=='twist' and i.parentBone and i.parentBone>=0 then fix=UI.button(row,L('bonemap.fix.use',{bone=state.bones[i.parentBone+1].name}),function() BM.Assign(state,i.slot,i.parentBone,'user') win:Changed() end,s(26),f.Small)
     elseif i.code=='jiggle_body' then fix=UI.button(row,L'bonemap.fix.untick',function() for _,c in ipairs(state.chains) do if c.root==i.chain then c.enabled=false end end BM.Changed(state,'chain') win:Changed() end,s(26),f.Small)
     elseif i.code=='required' then local c=BM.Candidates(state,i.slot,1)[1] if c and c.score>=60 then fix=UI.button(row,L('bonemap.fix.use',{bone=state.bones[c.bone+1].name}),function() BM.Assign(state,i.slot,c.bone,'user') win:Changed() end,s(26),f.Small) end
     end
     if not fix and i.slot~='' then fix=UI.button(row,L'bonemap.fix.select',function() if state.tab~='body' then win:SetTab('body') end win:Arm(i.slot) end,s(26),f.Small) end
     if fix then surface.SetFont(f.Small) fix:Dock(RIGHT) fix:SetWide(math.min(s(150),(surface.GetTextSize(fix:GetText()))+s(20))) fix:DockMargin(s(6),0,0,0) end
     local label=UI.label(row,issueText(state,i),f.Small,s(20)) label:Dock(FILL) label:SetWrap(true) label:SetAutoStretchVertical(true) label:SetTextColor(i.severity=='error' and Colors.missing or Colors.check)
     row.PerformLayout=function() row:SetTall(math.max(s(28),label:GetTall())) end
    end
   end
   local probe=state.probe local extra={}
   for _,w in ipairs(istable(probe) and probe.warnings or {}) do extra[#extra+1]=tostring(w) end
   for _,i in ipairs(notes) do extra[#extra+1]=issueText(state,i) end
   if #extra>0 then
    separator()
    local holder,body=UI.expander(canvas,#extra==1 and L'bonemap.summary.notes_one' or L('bonemap.summary.notes_many',{count=#extra}),s,f,function() end)
    for _,line in ipairs(extra) do local l=UI.label(body,'• '..line,f.Small,s(20)) l:Dock(TOP) l:SetWrap(true) l:SetAutoStretchVertical(true) l:SetTextColor(Colors.muted) end
   end
  end
  canvas:InvalidateLayout(true) panel:InvalidateLayout(true) UI.ownScale(panel)
  if keepFocus and IsValid(panel.Search) then panel.Search:RequestFocus() panel.Search:SetCaretPos(#panel.Search:GetText()) end
 end
 panel:RebuildContent()
 return panel
end

-- ---- list view (experts) ----
function BM.BuildTable(win,parent)
 local UI=mmdhl.UI local s,f=win.s,win.f
 local panel=parent:Add('DScrollPanel') panel.Paint=function(_,w,h) draw.RoundedBox(4,0,0,w,h,color_white) end
 local canvas=panel:GetCanvas() canvas:DockPadding(s(6),s(6),s(6),s(6))
 local collapsed={fingers=true}
 function panel:RebuildContent()
  local state=win.state
  local y=panel:GetVBar():GetScroll()
  canvas:Clear()
  for _,g in ipairs(BM.GroupOrder) do local entry=(win.summary or BM.Summary(state)).groups[g] if entry then
   if entry.worst=='missing' then collapsed[g]=false end
   local head=UI.button(canvas,(collapsed[g] and '▸ ' or '▾ ')..L('bonemap.group.'..g)..'   '..L('bonemap.group.count',{done=entry.done,total=entry.total}),function() collapsed[g]=not collapsed[g] panel:RebuildContent() end,s(26),f.Strong)
   head:Dock(TOP) head:SetContentAlignment(4) head:SetTextInset(s(8),0) head:DockMargin(0,s(4),0,s(2))
   if not collapsed[g] then for _,slot in ipairs(BM.Slots) do if BM.SummaryGroup(slot)==g and not (slot.convertOnly and state.mode~='convert') then
    local key=slot.key local status=BM.StatusOf(state,key) local issues=BM.IssuesOf(state,key)
    local row=canvas:Add('DButton') row:Dock(TOP) row:SetTall(#issues>0 and s(46) or s(28)) row:SetText('')
    row.DoClick=function() win:Arm(key) end
    -- Columns: status and part | how it was set | the bone (opens the picker) | clear.
    local pickWide=s(230)
    row.Paint=function(self,w,h) if win.armed==key then draw.RoundedBox(0,0,0,w,h,Color(220,237,251)) elseif self:IsHovered() then draw.RoundedBox(0,0,0,w,h,Color(238,243,249)) end
     BM.DrawStatus(status,s(14),s(14),s(8)) draw.SimpleText(BM.PartLabel(key),f.Body,s(28),s(14),Colors.ink,TEXT_ALIGN_LEFT,TEXT_ALIGN_CENTER)
     draw.SimpleText(originText(state,key),f.Small,w-s(32)-pickWide-s(8),s(14),Colors.muted,TEXT_ALIGN_RIGHT,TEXT_ALIGN_CENTER)
     if #issues>0 then draw.SimpleText(issueText(state,issues[1]),f.Small,s(28),s(34),issues[1].severity=='error' and Colors.missing or Colors.check,TEXT_ALIGN_LEFT,TEXT_ALIGN_CENTER) end end
    row:SetTooltip(L('bonemap.tooltip.technical',{valve=key,mmd=slot.mmdJp}))
    local value=BM.Value(state,key)
    local clear=UI.button(row,'✕',function() BM.Clear(state,key) win:Changed() end,s(24),f.Small) clear:SetSize(s(24),s(24)) clear:SetEnabled(not slot.required and value>=0)
    clear:SetTooltip(slot.required and L'bonemap.issue.required_tip' or L'bonemap.card.clear_tip')
    local pick=UI.button(row,value>=0 and state.bones[value+1].name or slot.required and L'bonemap.card.none' or L'bonemap.card.created',function(btn) win:Arm(key) BM.OpenPicker(win,key,btn) end,s(24),f.Small)
    row.PerformLayout=function(_,w) pick:SetPos(w-s(30)-pickWide,s(2)) pick:SetSize(pickWide,s(24)) clear:SetPos(w-s(26),s(2)) end
   end end end
  end end
  panel:InvalidateLayout(true) panel:GetVBar():SetScroll(y) UI.ownScale(panel)
 end
 panel:RebuildContent()
 return panel
end
-- The bone picker popover of the list view.
function BM.OpenPicker(win,key,anchor)
 local UI=mmdhl.UI local s,f,state=win.s,win.f,win.state
 if IsValid(win.picker) then win.picker:Remove() end
 local frame=win.frame
 -- A popup of its own, below the button that opened it; it closes with the window.
 local pop=vgui.Create('EditablePanel') win.picker=pop
 local w,h=s(380),s(440) local ax,ay=anchor:LocalToScreen(0,anchor:GetTall())
 pop:SetSize(w,h) pop:SetPos(math.Clamp(ax,s(4),ScrW()-w-s(4)),math.Clamp(ay,s(4),ScrH()-h-s(4)))
 pop:DockPadding(s(8),s(8),s(8),s(8)) pop:MakePopup()
 pop.Paint=function(_,pw,ph) draw.RoundedBox(4,0,0,pw,ph,Color(200,208,216)) draw.RoundedBox(4,1,1,pw-2,ph-2,color_white) end
 local search=pop:Add('DTextEntry') search:Dock(TOP) search:SetTall(s(30)) search:SetFont(f.Body) search:SetPlaceholderText(L'bonemap.picker.search')
 local helpers=UI.checkbox(pop,L'bonemap.picker.helpers',nil,f.Small,s(24)) helpers:Dock(TOP) helpers:DockMargin(0,s(4),0,s(4))
 local bottom=pop:Add('DPanel') bottom:Dock(BOTTOM) bottom:SetTall(s(34)) bottom:SetPaintBackground(false) bottom:DockMargin(0,s(6),0,0)
 local close=UI.button(bottom,L'common.close',function() pop:Remove() end,s(34),f.Body) close:Dock(RIGHT) close:SetWide(s(100))
 local slot=BM.SlotByKey[key]
 if not slot.required then local empty=UI.button(bottom,L'bonemap.card.leave_empty',function() BM.Clear(state,key) pop:Remove() win:Changed() end,s(34),f.Small) empty:Dock(FILL) empty:DockMargin(0,0,s(6),0) end
 local list=pop:Add('DListView') list:Dock(FILL) list:SetMultiSelect(false) list:SetDataHeight(s(24)) list:SetHeaderHeight(s(24))
 list:AddColumn(L'bonemap.column.bone') list:AddColumn(L'bonemap.column.used'):SetFixedWidth(s(110)) list:AddColumn(L'bonemap.column.moves'):SetFixedWidth(s(60))
 for _,c in pairs(list.Columns) do c.Header:SetFont(f.Small) end
 local function fill()
  list:Clear() local query=string.Trim(search:GetText()):lower() local shown=0
  local ranked={} for rank,c in ipairs(BM.Candidates(state,key,5)) do ranked[#ranked+1]=c.bone end
  local function add(b) local bone=state.bones[b+1] local using=BM.SlotUsing(state,b) local row=list:AddLine(bone.name,using and BM.PartLabel(using) or '',string.Comma(bone.weighted)) row.bone=b shown=shown+1 end
  local seen={} for _,b in ipairs(ranked) do if query=='' or state.bones[b+1].name:lower():find(query,1,true) then add(b) seen[b]=true end end
  for _,b in ipairs(state.order) do local bone=state.bones[b+1]
   if not seen[b] and shown<200 and (helpers:GetChecked() or bone.subtree>0) and (query=='' or bone.name:lower():find(query,1,true) or (bone.english or ''):lower():find(query,1,true)) then add(b) end end
 end
 list.OnRowSelected=function(_,_,row) if row.bone then BM.Assign(state,key,row.bone,'user') pop:Remove() win.armed=key win:Changed() end end
 search.OnChange=fill helpers.OnChange=function() fill() end
 -- A click anywhere else closes it.
 pop.Think=function()
  if not IsValid(frame) then pop:Remove() return end
  if RealTime()-(pop.opened or 0)<.2 or not (input.IsMouseDown(MOUSE_LEFT) or input.IsMouseDown(MOUSE_RIGHT)) then return end
  local hovered=vgui.GetHoveredPanel()
  while IsValid(hovered) do if hovered==pop then return end hovered=hovered:GetParent() end
  pop:Remove()
 end
 pop.opened=RealTime()
 fill() UI.ownScale(pop) search:RequestFocus()
end

-- ---- swinging parts ----
function BM.BuildJiggleList(win,parent)
 local UI=mmdhl.UI local s,f=win.s,win.f
 local panel=parent:Add('DPanel') panel:SetPaintBackground(false)
 local hint=UI.label(panel,L'bonemap.jiggle.hint',f.Small,s(20)) hint:Dock(TOP) hint:SetWrap(true) hint:SetAutoStretchVertical(true) hint:SetTextColor(Colors.muted) hint:DockMargin(0,0,0,s(6))
 local total=UI.label(panel,'',f.Small,s(22)) total:Dock(BOTTOM)
 local add=UI.button(panel,L'bonemap.jiggle.add',function() win.addMode=true win.armed=nil end,s(32),f.Body) add:Dock(BOTTOM) add:DockMargin(0,s(6),0,s(4))
 local list=panel:Add('DScrollPanel') list:Dock(FILL) list.Paint=function(_,w,h) draw.RoundedBox(4,0,0,w,h,color_white) end
 local canvas=list:GetCanvas() canvas:DockPadding(s(6),s(6),s(6),s(6))
 function panel:RebuildContent()
  local state=win.state canvas:Clear()
  if #state.groups==0 then local none=UI.label(canvas,L'bonemap.jiggle.none',f.Small,s(20)) none:Dock(TOP) none:SetWrap(true) none:SetAutoStretchVertical(true) none:SetTextColor(Colors.muted) end
  for _,g in ipairs(state.groups) do
   local chains,bones=0,0 for _,c in ipairs(state.chains) do if c.kind==g.kind then chains=chains+1 bones=bones+BM.JointCount(state,c.root) end end
   local row=canvas:Add('DButton') row:Dock(TOP) row:SetTall(s(56)) row:SetText('') row:DockMargin(0,0,0,s(4))
   row.Paint=function(self,w,h) draw.RoundedBox(4,0,0,w,h,win.selectedGroup==g.kind and Color(220,237,251) or self:IsHovered() and Color(238,243,249) or Color(246,248,251))
    draw.RoundedBox(2,s(8),s(11),s(14),s(14),g.enabled and (Colors[g.kind] or Colors.swing) or Colors.disabled)
    draw.SimpleText(L('bonemap.jiggle.group_row',{chains=chains,bones=bones}),f.Small,w-s(8),s(18),Colors.muted,TEXT_ALIGN_RIGHT,TEXT_ALIGN_CENTER)
    draw.SimpleText(L('bonemap.jiggle.swing.'..(g.custom and 'normal' or g.swing or 'normal'))..((g.collide and g.kind~='chest') and (' · '..L'bonemap.jiggle.collide') or ''),f.Small,s(30),s(40),Colors.muted,TEXT_ALIGN_LEFT,TEXT_ALIGN_CENTER) end
   local check=UI.checkbox(row,L('bonemap.jiggle.kind.'..g.kind),nil,f.Strong,s(24)) check:SetPos(s(30),s(6)) check:SetChecked(g.enabled)
   check.OnChange=function(_,on) g.enabled=on
    -- Ticking a group whose parts are all off turns them on.
    if on then local any=false for _,c in ipairs(state.chains) do if c.kind==g.kind and c.enabled then any=true end end if not any then for _,c in ipairs(state.chains) do if c.kind==g.kind then c.enabled=true end end end end
    BM.Changed(state,'group') win:Changed() end
   row.DoClick=function() win.selectedGroup=g.kind win:Refresh() end
   row.OnCursorEntered=function() win.hoverGroup=g.kind end
  end
  local joints=0 for _,c in ipairs(BM.ImportedChains(state)) do joints=joints+BM.JointCount(state,c.root) end
  total:SetText(L('bonemap.jiggle.total',{bones=joints,max=BM.MaxJoints})) total:SetTextColor(joints>BM.MaxJoints and Colors.missing or Colors.muted)
  UI.ownScale(panel)
 end
 panel:RebuildContent()
 return panel
end
function BM.BuildJiggleDetails(win,parent)
 local UI=mmdhl.UI local s,f=win.s,win.f
 local panel=parent:Add('DScrollPanel') panel.Paint=function(_,w,h) draw.RoundedBox(4,0,0,w,h,color_white) end
 local canvas=panel:GetCanvas() canvas:DockPadding(s(10),s(8),s(10),s(8))
 function panel:RebuildContent()
  local state=win.state canvas:Clear()
  local g=win.selectedGroup and BM.GroupOf(state,win.selectedGroup)
  if not g then local l=UI.label(canvas,L'bonemap.jiggle.select_group',f.Body,s(20)) l:Dock(TOP) l:SetWrap(true) l:SetAutoStretchVertical(true) l:SetTextColor(Colors.muted) return end
  local function caption(text) local l=UI.label(canvas,text,f.Strong,s(24)) l:Dock(TOP) l:DockMargin(0,s(6),0,s(2)) end
  caption(L'bonemap.jiggle.type')
  local kind=canvas:Add('DComboBox') kind:Dock(TOP) kind:SetTall(s(30)) UI.styleChoices(kind,s,f.Body)
  for i,k in ipairs(BM.Kinds) do kind:AddChoice(L('bonemap.jiggle.kind.'..k),k,k==g.kind) end
  kind.OnSelect=function(_,_,_,data) BM.SetGroupKind(state,g,data) win.selectedGroup=data win:Changed() end
  caption(L'bonemap.jiggle.swing_question')
  local swing=canvas:Add('DPanel') swing:Dock(TOP) swing:SetTall(s(32)) swing:SetPaintBackground(false)
  for _,level in ipairs({'less','normal','more'}) do local b=UI.button(swing,L('bonemap.jiggle.swing.'..level),function() g.swing=level g.custom=false g.values=nil BM.Changed(state,'swing') win:Changed() end,s(30),f.Body,'tab')
   b:Dock(LEFT) b:SetWide(s(80)) b:DockMargin(0,0,s(4),0) b.Selected=not g.custom and g.swing==level end
  if g.kind~='chest' then local collide=UI.checkbox(canvas,L'bonemap.jiggle.collide',nil,f.Body,s(28)) collide:Dock(TOP) collide:SetChecked(g.collide~=false) collide:DockMargin(0,s(6),0,0)
   collide.OnChange=function(_,on) g.collide=on BM.Changed(state,'collide') win:Refresh() end end
  local chains={} for _,c in ipairs(state.chains) do if c.kind==g.kind then chains[#chains+1]=c end end
  caption(L('bonemap.jiggle.parts',{count=#chains}))
  for _,c in ipairs(chains) do
   local row=canvas:Add('DPanel') row:Dock(TOP) row:SetTall(s(28)) row:SetPaintBackground(false)
   local count=UI.label(row,L('bonemap.jiggle.part_row',{bones=BM.JointCount(state,c.root)}),f.Small,s(28)) count:Dock(RIGHT) count:SizeToContentsX(s(4)) count:SetTextColor(Colors.muted)
   local check=UI.checkbox(row,state.bones[c.root+1].name,nil,f.Body,s(28)) check:Dock(FILL) check:SetChecked(c.enabled)
   check.OnChange=function(_,on) c.enabled=on BM.Changed(state,'chain') win:Changed() end
   row.OnMousePressed=function(_,code) if code~=MOUSE_RIGHT then return end
    local menu=DermaMenu() local sub=menu:AddSubMenu(L'bonemap.jiggle.move_to')
    for _,k in ipairs(BM.Kinds) do if k~=c.kind then sub:AddOption(L('bonemap.jiggle.kind.'..k),function() c.kind=k if not BM.GroupOf(state,k) then state.groups[#state.groups+1]={kind=k,enabled=true,swing='normal',custom=false,collide=k~='chest'} end BM.Changed(state,'chain') win:Changed() end) end end
    if c.user then menu:AddOption(L'bonemap.jiggle.remove',function() for i,x in ipairs(state.chains) do if x==c then table.remove(state.chains,i) break end end BM.Changed(state,'chain') win:Changed() end) end
    UI.ownScale(menu) menu:Open() end
  end
  local holder,body=UI.expander(canvas,L'bonemap.jiggle.details',s,f,function() end)
  local values=BM.GroupValues(g)
  -- i18n-keys: bonemap.jiggle.stiffness bonemap.jiggle.drag bonemap.jiggle.gravity bonemap.jiggle.radius
  for _,spec in ipairs({{'stiffness','bonemap.jiggle.stiffness'},{'dragForce','bonemap.jiggle.drag'},{'gravityPower','bonemap.jiggle.gravity'},{'hitRadius','bonemap.jiggle.radius'}}) do
   if not (spec[1]=='hitRadius' and g.kind=='chest') then
    local range=BM.Ranges[spec[1]]
    local slider=body:Add('DNumSlider') slider:Dock(TOP) slider:SetTall(s(30)) slider:SetText(L(spec[2])) slider:SetMinMax(range[1],range[2]) slider:SetDecimals(spec[1]=='hitRadius' and 3 or 2) slider:SetDark(true) slider.Label:SetFont(f.Small)
    slider:SetValue(values[spec[1]])
    slider.OnValueChanged=function(_,v) local current=BM.GroupValues(g) current[spec[1]]=math.Clamp(math.Round(v/range[3])*range[3],range[1],range[2]) g.values=current g.custom=true state.dirty=true end
   end
  end
  local reset=UI.button(body,L'bonemap.jiggle.reset',function() g.custom=false g.values=nil BM.Changed(state,'swing') win:Changed() end,s(28),f.Small) reset:Dock(TOP) reset:DockMargin(0,s(4),0,0)
  holder:Resize() UI.ownScale(panel)
 end
 panel:RebuildContent()
 return panel
end

-- ---- the rescue prompt ----
function BM.ShowRescuePrompt(id,name,missing)
 local UI=mmdhl.UI if not UI then return end local s,f=UI.metrics()
 if IsValid(BM.prompt) then BM.prompt:Remove() end
 local frame=vgui.Create('DFrame') BM.prompt=frame
 frame:SetTitle('') frame:SetSize(math.min(ScrW()-s(40),s(600)),s(260)) frame:Center() frame:MakePopup() frame:DockPadding(s(16),s(16),s(16),s(14))
 frame.btnMinim:SetVisible(false) frame.btnMaxim:SetVisible(false)
 frame.Paint=function(_,w,h) draw.RoundedBox(6,0,0,w,h,Colors.window) draw.RoundedBoxEx(6,0,0,w,s(6),Colors.check,true,true,false,false) end
 local available=BM.Available('fit')
 local missingSet={} for _,k in ipairs(missing or {}) do missingSet[k]=true end
 local figure=frame:Add('DPanel') figure:Dock(LEFT) figure:SetWide(s(114)) figure:DockMargin(0,0,s(12),0)
 figure.Paint=function(_,w,h) local k=math.min(w/380,h/500) local ox,oy=(w-380*k)/2,(h-500*k)/2
  for _,slot in ipairs(BM.Slots) do if slot.pos and slot.required then BM.DrawStatus(missingSet[slot.key] and 'missing' or 'ok',ox+slot.pos[1]*k,oy+slot.pos[2]*k,math.max(3,9*k)) end end end
 local buttons=frame:Add('DPanel') buttons:Dock(BOTTOM) buttons:SetTall(s(36)) buttons:SetPaintBackground(false) buttons:DockMargin(0,s(10),0,0)
 if available then
  local assign=UI.button(buttons,L'bonemap.rescue.assign',function() frame:Close() BM.OpenFit(id,'rescue',name) end,s(36),f.Strong,true) assign:Dock(RIGHT) surface.SetFont(f.Strong) assign:SetWide(math.max(s(200),surface.GetTextSize(L'bonemap.rescue.assign')+s(28)))
  local later=UI.button(buttons,L'bonemap.rescue.later',function() frame:Close() end,s(36),f.Body) later:Dock(RIGHT) later:SetWide(s(110)) later:DockMargin(0,0,s(8),0)
 else
  local close=UI.button(buttons,L'common.close',function() frame:Close() end,s(36),f.Body) close:Dock(RIGHT) close:SetWide(s(110))
 end
 local title=UI.label(frame,L('bonemap.rescue.title',{name=tostring(name or '')}),f.Title,s(34)) title:Dock(TOP) title:SetWrap(true) title:SetAutoStretchVertical(true)
 local text=UI.label(frame,available and L('bonemap.rescue.text',{parts=BM.PartList(missing or {})}) or L'bonemap.update_needed',f.Body,s(48)) text:Dock(TOP) text:SetWrap(true) text:SetAutoStretchVertical(true) text:DockMargin(0,s(6),0,0)
 if available then local later=UI.label(frame,L'bonemap.rescue.later_hint',f.Small,s(20)) later:Dock(TOP) later:SetWrap(true) later:SetAutoStretchVertical(true) later:SetTextColor(Colors.muted) later:DockMargin(0,s(8),0,0) end
 UI.ownScale(frame)
 return frame
end
-- The public entry other features open the window with (an "Assign bones…" button):
-- {asset=id, fit=status.fit} for a cached model, or {probe=<probe status>} to convert.
function mmdhl.OpenBoneMapper(opts)
 opts=opts or {}
 if BM.BringToFront() then return false end
 if istable(opts.probe) then
  if not BM.Available('convert') then updateNeeded() return false end
  local status=istable(opts.probe.probe) and opts.probe or {probe=opts.probe,source=opts.source,filename=opts.filename}
  -- The conversion reads the file again, so a probe without its source cannot open.
  if not isstring(status.source) or status.source=='' or not istable(status.probe) then print('[Model Hotloader] OpenBoneMapper: a probe needs its source file') return false end
  return BM.OpenConvert(status,{})~=false
 end
 if isstring(opts.asset) then
  if not BM.Available('fit') then updateNeeded() return false end
  return BM.OpenFit(opts.asset,(istable(opts.fit) and opts.fit.ok==false) and 'rescue' or opts.reason or 'edit',opts.name)
 end
 -- A file path alone never starts an import here: imports begin from the player's picker.
 return false
end
-- The server's answer to a save.
net.Receive('mmdhl_bonemap_result',function()
 local id=net.ReadString() local ok=net.ReadBool() local message=mmdhl.Localize(net.ReadString())
 local waiting=BM.awaiting if not waiting or waiting.asset~=id then if message~='' then notification.AddLegacy(message,ok and NOTIFY_GENERIC or NOTIFY_ERROR,8) end return end
 BM.awaiting=nil timer.Remove('MMDHL.BoneMapSave')
 local frame=BM.frame local win=IsValid(frame) and frame.Window
 if ok then
  if mmdhl.library.SetFitStatus then mmdhl.library.SetFitStatus(id,{ok=true}) end
  notification.AddLegacy(waiting.state.reason=='rescue' and L('bonemap.rescued',{name=waiting.name}) or L('bonemap.saved',{name=waiting.name}),NOTIFY_GENERIC,8)
  if win and win.state==waiting.state then win.finish('saved') end
 elseif win and win.state==waiting.state then win.saving=false win:Notify(message) end
end)
