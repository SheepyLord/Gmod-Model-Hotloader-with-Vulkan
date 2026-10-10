-- The ragdoll physics editor window (client). The draft lives here; the server
-- builds what Apply or Test copy sends (physics_editor.lua) and the exact
-- preview comes from the client's native module (PreviewCarrierFit), with a Lua
-- approximation on older modules. physics_profile.lua is the model.
if SERVER then return end
local P=mmdhl.physics
local L=mmdhl.L
local E -- the open editor
local ink,muted=Color(31,43,58),Color(93,109,127)
local colors={default=Color(205,213,222),modified=Color(255,215,0),issue=Color(230,60,200),selected=Color(34,109,185),review=Color(255,150,30),
 hullUnchanged=Color(150,170,190),hullModified=Color(255,215,0),hullIssue=Color(230,60,200),actual=Color(30,220,220),penetration=Color(255,0,255),
 error=Color(196,43,43),warning=Color(191,120,22),note=Color(34,109,185),success=Color(33,138,84)}
local axisColors={x=Color(255,70,70),y=Color(70,255,70),z=Color(70,100,255)}
-- i18n-keys: physics_editor.part.pelvis physics_editor.part.spine1 physics_editor.part.spine4 physics_editor.part.head1 physics_editor.part.l_clavicle physics_editor.part.l_upperarm physics_editor.part.l_forearm physics_editor.part.l_hand physics_editor.part.r_clavicle physics_editor.part.r_upperarm physics_editor.part.r_forearm physics_editor.part.r_hand physics_editor.part.l_thigh physics_editor.part.l_calf physics_editor.part.l_foot physics_editor.part.r_thigh physics_editor.part.r_calf physics_editor.part.r_foot
local function partName(i) return L('physics_editor.part.'..P.IDS[i+1]) end
-- i18n-keys: physics_editor.joint.spine1 physics_editor.joint.spine4 physics_editor.joint.head1 physics_editor.joint.l_clavicle physics_editor.joint.l_upperarm physics_editor.joint.l_forearm physics_editor.joint.l_hand physics_editor.joint.r_clavicle physics_editor.joint.r_upperarm physics_editor.joint.r_forearm physics_editor.joint.r_hand physics_editor.joint.l_thigh physics_editor.joint.l_calf physics_editor.joint.l_foot physics_editor.joint.r_thigh physics_editor.joint.r_calf physics_editor.joint.r_foot
local function jointName(i) return L('physics_editor.joint.'..P.IDS[i+1]) end
-- i18n-keys: physics_editor.axis.x physics_editor.axis.y physics_editor.axis.z
local function axisName(axis) return L('physics_editor.axis.'..axis) end
local function fmt(v,decimals) if type(v)~='number' then return '—' end local s=string.format('%.'..(decimals or 2)..'f',v) if s:find('%.') then s=s:gsub('0+$',''):gsub('%.$','') end if s=='-0' then s='0' end return s end
local function vec(t) return Vector(t[1],t[2],t[3]) end
local function bodyLabel(i) return partName(i) end
-- The Advanced checkbox's own value first: its convar changes a frame later.
local function advanced() if E and E.advancedSet~=nil then return E.advancedSet end return GetConVar('mmdhl_physics_editor_advanced'):GetBool() end

-- Previews ------------------------------------------------------------------
-- The Lua stand-in for PreviewCarrierFit on older modules: the collision editor's
-- affine rescale of the fitted hull, weight shares without volumes, no overlaps.
local function approximatePreview(rig,request,draft)
 local out={status='approximate',bodies={},penetrations={},canonical=request.physicsOverrides}
 local bodies,model=P.Effective(draft)
 local masses=P.Masses(bodies,model,request.mass,nil,next(request.physicsOverrides or {})~=nil)
 for i=0,17 do
  local b=rig.bodies[i+1] local s=(request.collisionOverrides or {})[P.BODIES[i+1]]
  local c,e=s and s.center or b.center,s and s.extent or b.extent
  local hull,faces=b.hull,b.faces
  if s and s.style and s.style~='fitted' then
   hull={} for x=-1,1,2 do for y=-1,1,2 do for z=-1,1,2 do hull[#hull+1]={c[1]+x*e[1],c[2]+y*e[2],c[3]+z*e[3]} end end end
   faces={{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}}
  elseif s then
   hull={} for j,v in ipairs(b.hull) do local p={} for k=1,3 do p[k]=c[k]+(v[k]-b.center[k])*e[k]/math.max(b.extent[k],1e-6) end hull[j]=p end
  end
  out.bodies[i+1]={index=i,name=P.BODIES[i+1],parent=b.parent,bone=b.bone,center=c,extent=e,hull=hull,faces=faces,style=s and s.style or 'fitted',mass=masses[i],needsReview=b.needsReview,confidence=b.confidence,
   lower={bodies[i].limits and bodies[i].limits.x[1] or 0,bodies[i].limits and bodies[i].limits.y[1] or 0,bodies[i].limits and bodies[i].limits.z[1] or 0},
   upper={bodies[i].limits and bodies[i].limits.x[2] or 0,bodies[i].limits and bodies[i].limits.y[2] or 0,bodies[i].limits and bodies[i].limits.z[2] or 0}}
 end
 local total=0 for i=0,17 do total=total+masses[i] end out.mass=total
 return out
end
local edgeCache=setmetatable({},{__mode='k'})
local function edges(body)
 if edgeCache[body] then return edgeCache[body] end local list,seen={},{}
 for _,face in ipairs(body.faces or {}) do for k,a in ipairs(face) do local b=face[k%#face+1] local key=math.min(a,b)..':'..math.max(a,b) if not seen[key] then seen[key]=true list[#list+1]={a+1,b+1} end end end
 edgeCache[body]=list return list
end

-- The editor object ------------------------------------------------------------
local Editor={} Editor.__index=Editor
function Editor:Valid() return IsValid(self.frame) end
-- Editing continues into the draft while a build runs; only the server operations wait for it.
function Editor:CanEdit() return self.state~=nil and self.state.canEdit==true and not self.removed end
-- Simple and physics controls are off on a server whose module builds shapes only.
function Editor:PhysicsEnabled() return self:CanEdit() and self.level>=1 end
function Editor:Request() return P.Resolve(self.draft,self.rig) end
function Editor:Dirty() return self.diff and #self.diff>0 end
function Editor:Snapshot(merge)
 if merge and self.merging==merge then return end self.merging=merge
 self.undo[#self.undo+1]=P.Copy(self.draft) if #self.undo>50 then table.remove(self.undo,1) end self.redo={}
end
-- Every draft edit goes through here: one undo step (a slider drag merges into one), then refresh.
function Editor:Edit(fn,merge)
 if not self:CanEdit() then return end
 self:Snapshot(merge) fn(self.draft) self:Changed()
end
function Editor:Undo() local d=table.remove(self.undo) if d then self.redo[#self.redo+1]=P.Copy(self.draft) self.draft=d self.merging=nil self:Changed() end end
function Editor:Redo() local d=table.remove(self.redo) if d then self.undo[#self.undo+1]=P.Copy(self.draft) self.draft=d self.merging=nil self:Changed() end end
function Editor:Changed()
 self.request=self:Request() self.diff=P.Diff(self.appliedRequest,self.request)
 self.previewAt=RealTime()+.15 self:RunChecks() self:Sync()
end
function Editor:Sync() if self.syncing then return end self.syncing=true for _,f in ipairs(self.syncs) do local ok,err=pcall(f) if not ok then ErrorNoHalt('[Model Hotloader physics editor] '..tostring(err)..'\n') end end self.syncing=false end
function Editor:OnSync(f) self.syncs[#self.syncs+1]=f return f end
function Editor:RunChecks()
 -- While a refit is pending the last preview belongs to an older draft: its masses and overlaps wait.
 self.issues=P.Check(self.draft,not self.pending and self.preview and self.preview.status=='ready' and self.preview or nil,self.baseline,{rig=self.rig,level=self.level,surfaceKnown=function(name) return util.GetSurfaceIndex(name)>=0 end,
  approximate=not P.ClientPreview(),base=not self.state.approximateKey and self.state.base or nil,mirrorSkipped=self.mirrorSkipped})
 if self.previewError then table.insert(self.issues,1,{severity='error',code='preview_failed',params={reason=self.previewError},fixes={}}) end
 local size=#(util.Compress(util.TableToJSON({base=self.state.base,request=self.request})) or '')
 if size>60000 then table.insert(self.issues,1,{severity='error',code='too_large',params={},fixes={}}) end
end
function Editor:HasErrors() return P.HasErrors(self.issues) end
function Editor:BodyIssue(i) for _,v in ipairs(self.issues or {}) do if v.body==i and v.severity~='note' then return true end end end
function Editor:BodyModified(i)
 local d=self.draft if d and (d.shapes[i] or d.explicit[i] or d.joints[i]) then return true end
 for _,d in ipairs(self.diff or {}) do if d.body==i then return true end end
end
function Editor:Body(i) local p=self.preview and self.preview.bodies and self.preview.bodies[i+1] return p or self.rig.bodies[i+1] end
function Editor:RunPreview()
 self.previewAt=nil
 local request=self.level<1 and P.ShapesOnly(self.request) or self.request
 if not P.ClientPreview() then self.preview=approximatePreview(self.rig,request,self.draft) self:RunChecks() self:Sync() return end
 local result,err=self:NativePreview(request)
 if not result then
  -- The asset is not loaded on this computer yet: ask for it and approximate meanwhile.
  if mmdhl.native.RequestAsset then mmdhl.native.RequestAsset(self.asset) end
  self.preview=approximatePreview(self.rig,request,self.draft) self.previewAt=RealTime()+1 self.pending=false
 elseif result.status=='pending' then self.pollAt=RealTime()+.1 self.pending=true
 elseif result.status=='error' then local e=result.errors and result.errors[1] or {} self.previewError=(e.detail and e.detail~='' and e.detail or e.code or tostring(err)) self.pending=false
 else
  self.preview=result self.previewError=nil self.pending=false
  -- Overlaps the fit already had when the editor opened do not warn (§10 "baseline"). While its
  -- own refit runs, this preview's overlaps stand in and the poll asks again.
  if not self.baseline or self.baseline.standIn then local base=self:NativePreview(self.level<1 and P.ShapesOnly(self.appliedRequest) or self.appliedRequest)
   if base and base.status=='pending' then self.baseline={standIn=true,penetrations=result.penetrations} self.pollAt=RealTime()+.1 else self.baseline=base and base.status=='ready' and base or result end end
 end
 self:RunChecks() self:Sync()
 if self.shrinking and result and result.status=='ready' then local step=self.shrinking self.shrinking=nil step() end
end
-- PreviewCarrierFit with the ragdoll's own fit options: the server builds from the same ones.
function Editor:NativePreview(request)
 local options=table.Copy(self.state.fitOptions or {}) for k,v in pairs(request) do options[k]=v end options.physicsEditor=nil
 local raw,err=mmdhl.native.PreviewCarrierFit(self.asset,util.TableToJSON(options))
 return raw and util.JSONToTable(raw),err
end
function Editor:Think()
 if not IsValid(self.ent) and not self.removed and not self.building then self.removed=true self:Sync() end
 if self.previewAt and RealTime()>=self.previewAt then self:RunPreview() end
 if self.pollAt and RealTime()>=self.pollAt then self.pollAt=nil self:RunPreview() end
 if IsValid(self.ent) and self.state and not self.building and self.ent:GetNW2String('MMDHLRig','')~=self.state.base then if not self.stale then self.stale=true self:Sync() end end
 if mmdhl.RequestCollisionMesh and GetConVar('mmdhl_physics_editor_actual'):GetBool() and IsValid(self.ent) then mmdhl.RequestCollisionMesh(self.ent) end
end
-- Reopens with the server's state; edits made meanwhile stay in the draft.
function Editor:Load(state)
 self.state=state self.level=tonumber(state.level) or 0 self.stale=false
 -- The spawn menu's preview ragdoll: each build is saved for every new spawn of the model.
 self.modelPreview=state.modelPreview==true
 if IsValid(self.frame) then self.frame:SetTitle(L(self.modelPreview and 'physics_editor.model.title' or 'physics_editor.title',{name=self.name})) end
 self.rig=mmdhl.GetRig(self.ent) or self.rig
 self.applied=P.DraftFromState(state,self.rig) self.appliedRequest=P.Resolve(self.applied,self.rig)
 -- Dirty against the state just loaded, not the one before it.
 self.diff=self.draft and P.Diff(self.appliedRequest,self:Request()) or nil
 if not self.draft or not self:Dirty() then self.draft=P.Copy(self.applied) self.undo={} self.redo={} end
 self.baseline=nil self.preview=nil self:Changed() self:RunPreview()
 if not P.ClientPreview() then P.NoteOldBinary() end
end

-- Server operations ----------------------------------------------------------------
-- i18n-keys: physics_editor.status.unchanged physics_editor.status.unsaved physics_editor.status.building physics_editor.status.applied physics_editor.status.problem physics_editor.status.readonly
-- i18n-keys: physics_editor.status.saved_all physics_editor.title physics_editor.model.title physics_editor.apply physics_editor.apply_count physics_editor.model.apply physics_editor.model.apply_count
function Editor:Status()
 if self.building then return L'physics_editor.status.building',colors.note end
 if not self:CanEdit() then return L'physics_editor.status.readonly',muted end
 if self:HasErrors() then return L'physics_editor.status.problem',colors.error end
 if self:Dirty() then return L'physics_editor.status.unsaved',colors.warning end
 if self.appliedAt then return L(self.modelPreview and 'physics_editor.status.saved_all' or 'physics_editor.status.applied',{time=os.date('%H:%M',self.appliedAt)}),colors.success end
 return L'physics_editor.status.unchanged',muted
end
function Editor:Send(op,payload,after)
 if self.building and op~='close' then return end
 payload=payload or {} payload.base=self.state and self.state.base
 local replaces=op=='apply' or op=='previous' or op=='reset' or op=='restore_saved'
 if op~='save_default' and op~='clear_default' then self.building=true self:Sync() end
 mmdhl.PhysicsRequest(op,self.ent,payload,function(state,message,_,data,index)
  if not self:Valid() then return end
  if state=='building' then return end
  self.building=false
  if state=='error' then self:Toast(message,NOTIFY_ERROR) self:Sync() if after then after(false) end return end
  if op=='test' then self:Toast(message,NOTIFY_GENERIC) for _,w in ipairs(data and data.warnings or {}) do self:Toast(mmdhl.Localize(w),NOTIFY_HINT) end
  elseif replaces and istable(data) then self:Rebind(op,index,data,message,after) return
  -- The server knows the model by its hash only; the notice names it as this window does.
  elseif op=='save_default' or op=='clear_default' then self:Toast(L(op=='save_default' and 'physics_editor.notice.saved' or 'physics_editor.notice.forgotten',{name=self.name}),NOTIFY_GENERIC) self:Reopen()
  else self:Toast(message,NOTIFY_GENERIC) end
  self:Sync() if after then after(true) end
 end)
end
-- After a replace: wait for the new ragdoll and its carrier on this client, then edit it.
function Editor:Rebind(op,index,state,message,after)
 local started=RealTime() self.building=true self:Sync()
 local name='MMDHL.PhysicsEditorRebind'
 timer.Create(name,.1,0,function()
  if not self:Valid() then timer.Remove(name) return end
  local ent=Entity(index or 0)
  local rig=IsValid(ent) and mmdhl.GetRig(ent)
  if rig and rig.key==state.base or RealTime()-started>10 then
   timer.Remove(name) self.building=false
   if IsValid(ent) then
    if IsValid(self.ent) then self.ent.MMDHLFitOverlay=self.overlayBefore end
    self.ent=ent self.overlayBefore=ent.MMDHLFitOverlay ent.MMDHLFitOverlay=false ent.MMDHLActualCollision=nil
    self.rig=rig or self.rig self.appliedAt=os.time() self.removed=false
    -- Apply keeps the draft, with edits made while it built; Previous version, Reset and Restore show what the ragdoll has now.
    local draft,undo,redo=self.draft,self.undo,self.redo if op~='apply' then self.draft=nil end
    self:Load(state) if op=='apply' then self.draft,self.undo,self.redo=draft,undo,redo self:Changed() end
    -- The server knows the model by its hash only: on the preview, say what was saved by name.
    if self.modelPreview then message=L(op=='reset' and 'physics_editor.notice.forgotten' or 'physics_editor.notice.saved',{name=self.name}) end
    self:Toast(message,NOTIFY_GENERIC) for _,w in ipairs(state.warnings or {}) do self:Toast(mmdhl.Localize(w),NOTIFY_HINT) end
    if after then after(true) end
   else self.removed=true self:Sync() end
  end
 end)
end
function Editor:Reopen() mmdhl.PhysicsRequest('open',self.ent,{},function(state,message,_,data) if not self:Valid() then return end if state=='state' and istable(data) then self:Load(data) else self:Toast(message,NOTIFY_ERROR) end end) end
function Editor:Toast(text,kind) if text and text~='' then notification.AddLegacy(text,kind or NOTIFY_GENERIC,6) end end
function Editor:Apply() if self:CanEdit() and self:Dirty() and not self:HasErrors() and not self.pending then self:Send('apply',{request=self.request}) end end
function Editor:Close(force)
 if not force and self:Dirty() then Derma_Query(L'physics_editor.confirm.close',L'physics_editor.title_short',L'physics_editor.button.discard',function() self:Close(true) end,L'physics_editor.button.keep_editing',function() end) return end
 if IsValid(self.ent) then self.ent.MMDHLFitOverlay=self.overlayBefore end
 if self.ent then mmdhl.PhysicsRequest('close',self.ent,{}) end
 hook.Remove('Think','MMDHL.PhysicsEditor') hook.Remove('PostDrawTranslucentRenderables','MMDHL.PhysicsEditor') hook.Remove('CalcView','MMDHL.PhysicsEditorCamera') hook.Remove('HUDPaint','MMDHL.PhysicsEditor') hook.Remove('OnPauseMenuShow','MMDHL.PhysicsTemplatePick')
 timer.Remove('MMDHL.PhysicsEditorRebind')
 for _,p in ipairs({self.world,self.frame,self.dialog}) do if IsValid(p) then p:Remove() end end
 if E==self then E=nil end
end

-- Widgets ----------------------------------------------------------------------
local function wrapLabel(parent,text,font,height,color)
 local l=mmdhl.UI.label(parent,text,font,height) l:Dock(TOP) l:SetWrap(true) l:SetAutoStretchVertical(true) if color then l:SetTextColor(color) end return l
end
local function row(parent,height,s) local r=parent:Add('DPanel') r:Dock(TOP) r:SetTall(height) r:SetPaintBackground(false) r:DockMargin(0,0,0,s(4)) return r end
-- A slider that snaps to stops (§9.3): click or drag, arrows, Home and End.
local function stopSlider(parent,s,stops,get,set,enabled)
 local p=parent:Add('DPanel') p:SetTall(s(24)) p:SetKeyboardInputEnabled(true) p:SetCursor('hand')
 local function index() local v=get() for k,stop in ipairs(stops) do if stop==v then return k end end return 1 end
 local function at(x) local w=p:GetWide()-s(18) local k=math.Clamp(math.Round((x-s(9))/math.max(w,1)*(#stops-1))+1,1,#stops) return stops[k] end
 p.Paint=function(self,w,h)
  local on=enabled() local track=s(9) local span=w-s(18)
  draw.RoundedBox(2,track,h/2-s(2),span,s(4),on and Color(190,200,212) or Color(226,230,235))
  for k=1,#stops do local x=track+span*(k-1)/math.max(#stops-1,1) draw.RoundedBox(s(7),x-s(7),h/2-s(7),s(14),s(14),Color(232,237,242)) end
  local x=track+span*(index()-1)/math.max(#stops-1,1)
  draw.RoundedBox(s(9),x-s(9),h/2-s(9),s(18),s(18),on and colors.selected or muted)
  if self:HasFocus() then surface.SetDrawColor(colors.selected) surface.DrawOutlinedRect(0,0,w,h) end
 end
 p.OnMousePressed=function(self,code) if code~=MOUSE_LEFT or not enabled() then return end self:RequestFocus() self.dragging=true self:MouseCapture(true) set(at(self:CursorPos()),true) end
 p.OnCursorMoved=function(self,x) if self.dragging then set(at(x),true) end end
 p.OnMouseReleased=function(self) self.dragging=false self:MouseCapture(false) if E then E.merging=nil end end
 p.OnKeyCodePressed=function(self,code)
  if not enabled() then return end local k=index()
  if code==KEY_LEFT then k=k-1 elseif code==KEY_RIGHT then k=k+1 elseif code==KEY_HOME then k=1 elseif code==KEY_END then k=#stops else return end
  set(stops[math.Clamp(k,1,#stops)])
 end
 return p
end
-- A numeric field (§9.6): comma or point, Enter or blur commits what was typed, Esc reverts, arrows step (Shift ×10, Ctrl ×0.1).
local function numberField(parent,s,f,get,set,lo,hi,step,decimals,enabled)
 local t=parent:Add('DTextEntry') t:SetFont(f.Small) t:SetTall(s(24)) t:SetContentAlignment(6) t:SetUpdateOnType(false)
 -- t.shown is the text the field last displayed or committed. Leaving the field with it
 -- (a click in to read the value, Esc, the blur after Enter's commit, the field Enter moves
 -- on to) is no edit: setting a value pins it, mirrors it, makes a shape explicit and adds
 -- an undo step, and the displayed text is rounded (friction shows a fifth ×5).
 local function display(text) t.shown=text t:SetText(text) end
 local function show() if not t:HasFocus() then local v=get() display(type(v)=='number' and fmt(v,decimals) or '') end end
 -- value: an arrow key's step; otherwise the typed text.
 local function commit(value)
  local typed=value==nil and t:GetValue() or nil
  if typed~=nil and typed==t.shown then show() return end
  -- One value: gsub's count would be tonumber's base (an error).
  local v=tonumber((tostring(value or typed):gsub(',','.')))
  if not v then show() return end
  if v<lo or v>hi then t.flashUntil=RealTime()+3 t:SetTooltip(L('physics_editor.clamped',{min=fmt(lo,decimals),max=fmt(hi,decimals)})) v=math.Clamp(v,lo,hi) end
  -- The value the draft already has (typed again, or 1.0 for 1) is no edit either.
  v=P.Quantize(v,decimals) local current=get()
  if not (type(current)=='number' and P.Quantize(current,decimals)==v) then set(v) end
  if typed~=nil then t.shown=typed end
  show()
 end
 t.OnEnter=function() commit() end
 local base=vgui.GetControlTable('DTextEntry')
 t.OnLoseFocus=function(self) commit() if base and base.OnLoseFocus then base.OnLoseFocus(self) end end
 t.OnKeyCodeTyped=function(self,code)
  -- Back to the displayed text first: the blur that KillFocus causes then commits nothing.
  if code==KEY_ESCAPE then self:SetText(self.shown or '') self:KillFocus() show() return true end
  if code==KEY_UP or code==KEY_DOWN then local v=tonumber((self:GetValue():gsub(',','.'))) or get() or 0
   local k=step*((input.IsShiftDown() and 10) or (input.IsControlDown() and .1) or 1) commit((v+(code==KEY_UP and k or -k))) display(fmt(get(),decimals)) self:SetCaretPos(#self:GetText()) return true end
  -- Enter and every other key as DTextEntry handles them: its Enter calls OnEnter.
  if base and base.OnKeyCodeTyped then return base.OnKeyCodeTyped(self,code) end
 end
 t.AllowInput=function(_,char) return not char:find('[%d%.,%-]') end
 t.PaintOver=function(self,w,h) if (self.flashUntil or 0)>RealTime() then surface.SetDrawColor(colors.warning) surface.DrawOutlinedRect(0,0,w,h,2) end end
 t.Think=function(self) if base and base.Think then base.Think(self) end self:SetEnabled(enabled==nil or enabled()) end
 t.Show=show show()
 return t
end

-- Tabs -----------------------------------------------------------------------
local Tabs={}
-- i18n-keys: physics_editor.stiffness physics_editor.stiffness.help physics_editor.range physics_editor.range.help physics_editor.floatiness physics_editor.floatiness.help physics_editor.shorter physics_editor.longer physics_editor.thinner physics_editor.thicker
-- i18n-keys: physics_editor.self.all physics_editor.self.none physics_editor.self.custom physics_editor.center physics_editor.half_size physics_editor.column.axis physics_editor.column.mode physics_editor.column.min physics_editor.column.max physics_editor.column.friction
-- i18n-keys: physics_editor.mass_share physics_editor.damping physics_editor.rotdamping physics_editor.inertia physics_editor.animated.min physics_editor.animated.max physics_editor.animated.time_in physics_editor.animated.time_hold physics_editor.animated.time_out
-- i18n-keys: physics_editor.preset.default physics_editor.preset.less_floppy physics_editor.preset.posing_doll physics_editor.preset.relaxed physics_editor.preset.extra_floppy physics_editor.preset.statue physics_editor.preset.template physics_editor.preset.custom
-- i18n-keys: physics_editor.preset.default.hint physics_editor.preset.less_floppy.hint physics_editor.preset.posing_doll.hint physics_editor.preset.relaxed.hint physics_editor.preset.extra_floppy.hint physics_editor.preset.statue.hint
-- i18n-keys: physics_editor.stiffness.floppy physics_editor.stiffness.loose physics_editor.stiffness.normal physics_editor.stiffness.firm physics_editor.stiffness.stiff
-- i18n-keys: physics_editor.range.locked physics_editor.range.very_tight physics_editor.range.tight physics_editor.range.normal physics_editor.range.loose physics_editor.range.very_loose
-- i18n-keys: physics_editor.floatiness.normal physics_editor.floatiness.slow_fall physics_editor.floatiness.floaty
-- i18n-keys: physics_editor.material.flesh physics_editor.material.bloodyflesh physics_editor.material.zombieflesh physics_editor.material.alienflesh physics_editor.material.armorflesh physics_editor.material.rubber physics_editor.material.plastic physics_editor.material.wood physics_editor.material.metal physics_editor.material.solidmetal physics_editor.material.carpet physics_editor.material.ice physics_editor.material.default
local function materialName(id) for _,known in ipairs(P.SURFACES) do if known==id then return L('physics_editor.material.'..id) end end return L('physics_editor.material.other',{name=id}) end
local function materialCombo(e,parent,s,f,get,set,enabled,editable)
 local combo=parent:Add('DComboBox') combo:SetTall(s(28)) mmdhl.UI.styleChoices(combo,s,f.Body) combo:SetSortItems(false)
 local function fill() local current=get() if combo.filledFor==current then return end combo.filledFor=current combo:Clear()
  for _,id in ipairs(P.SURFACES) do if util.GetSurfaceIndex(id)>=0 then combo:AddChoice(materialName(id),id,id==current) end end
  local listed=false for _,id in ipairs(P.SURFACES) do if id==current then listed=true end end
  if not listed and current then combo:AddChoice(materialName(current),current,true) end
 end
 combo.OnSelect=function(_,_,_,id) if not e.syncing then set(id) end end
 if editable then
  -- Advanced: any surface material the game knows, typed in.
  local entry=parent:Add('DTextEntry') entry:SetTall(s(28)) entry:SetFont(f.Small) entry:SetPlaceholderText('surfaceprop')
  entry.OnEnter=function(self) local id=self:GetValue():lower() if P.ValidSurface(id) and util.GetSurfaceIndex(id)>=0 then set(id) self:SetText('') else self.flashUntil=RealTime()+3 end end
  entry.PaintOver=function(self,w,h) if (self.flashUntil or 0)>RealTime() then surface.SetDrawColor(colors.error) surface.DrawOutlinedRect(0,0,w,h,2) end end
  combo.Entry=entry
 end
 e:OnSync(function() fill() combo:SetEnabled(enabled()) if combo.Entry then combo.Entry:SetEnabled(enabled()) end end)
 return combo
end
function Tabs.feel(e,page,s,f)
 local UI=mmdhl.UI
 UI.label(page,L'physics_editor.starting_points',f.Strong,s(20)):Dock(TOP)
 local grid=page:Add('DPanel') grid:Dock(TOP) grid:SetTall(s(136)) grid:SetPaintBackground(false) grid:DockMargin(0,s(4),0,s(6))
 local cards={}
 for n,id in ipairs(P.PRESET_ORDER) do
  local card=grid:Add('DButton') card:SetText('') cards[n]=card
  card.Paint=function(self,w,h)
   local on=P.FeelMatches(e.draft,id) local enabled=e:PhysicsEnabled()
   draw.RoundedBox(4,0,0,w,h,on and Color(220,234,250) or (self:IsHovered() and enabled) and Color(240,244,248) or color_white)
   surface.SetDrawColor(on and colors.selected or Color(205,213,222)) surface.DrawOutlinedRect(0,0,w,h,on and 2 or 1)
   draw.SimpleText(L('physics_editor.preset.'..id)..(on and '  ✓' or ''),f.Strong,s(8),s(8),enabled and ink or muted)
   draw.SimpleText(L('physics_editor.preset.'..id..'.hint'),f.Small,s(8),s(34),muted)
  end
  card.DoClick=function()
   if not e:PhysicsEnabled() then return end
   local function pick(clear) e:Edit(function(d) P.ApplyPreset(d,id,clear) end) end
   local custom=table.Count(e.draft.joints)+table.Count(e.draft.explicit)
   if custom>0 then Derma_Query(L('physics_editor.keep_changes',{count=custom}),L'physics_editor.keep_changes.title',L'physics_editor.button.keep',function() pick(false) end,L'physics_editor.button.clear',function() pick(true) end,L'physics_editor.button.cancel',function() end)
   else pick(false) end
  end
 end
 grid.PerformLayout=function(_,w) local cw=math.floor((w-s(16))/3) for n,card in ipairs(cards) do card:SetPos(((n-1)%3)*(cw+s(8)),math.floor((n-1)/3)*s(72)) card:SetSize(cw,s(64)) end end
 local copy=UI.button(page,L'physics_editor.copy_from_model',function() if e:PhysicsEnabled() then e:TemplateDialog() end end,s(32),f.Body) copy:Dock(TOP) copy:DockMargin(0,0,0,s(10))
 e:OnSync(function() copy:SetEnabled(e:PhysicsEnabled()) end)
 local function stops(title,help,field,list,ids)
  local head=row(page,s(20),s) local label=UI.label(head,'',f.Strong,s(20)) label:Dock(FILL)
  local reset=UI.button(head,'↺',function() e:Edit(function(d) P.SetFeel(d,field,0) end) end,s(20),f.Small) reset:Dock(RIGHT) reset:SetWide(s(28)) reset:SetTooltip(L'physics_editor.reset_field')
  local slider=stopSlider(page,s,list,function() return e.draft.feel[field] or 0 end,function(v,drag) if v~=e.draft.feel[field] then e:Edit(function(d) P.SetFeel(d,field,v) end,drag and ('feel.'..field) or nil) end end,function() return e:PhysicsEnabled() end)
  slider:Dock(TOP) slider:DockMargin(s(4),s(2),s(4),s(2))
  local ends=row(page,s(16),s) local left=UI.label(ends,L('physics_editor.'..field..'.'..ids[list[1]]),f.Small,s(16)) left:Dock(LEFT) left:SizeToContentsX() left:SetTextColor(muted)
  local right=UI.label(ends,L('physics_editor.'..field..'.'..ids[list[#list]]),f.Small,s(16)) right:Dock(RIGHT) right:SizeToContentsX() right:SetTextColor(muted)
  wrapLabel(page,L(help),f.Small,s(16),muted):DockMargin(0,0,0,s(8))
  e:OnSync(function() label:SetText(L(title,{stop=L('physics_editor.'..field..'.'..ids[e.draft.feel[field] or 0])})) reset:SetEnabled(e:PhysicsEnabled() and (e.draft.feel[field] or 0)~=0) end)
 end
 stops('physics_editor.stiffness','physics_editor.stiffness.help','stiffness',{-2,-1,0,1,2},P.STIFF_IDS)
 stops('physics_editor.range','physics_editor.range.help','range',{-3,-2,-1,0,1,2},P.RANGE_IDS)
 stops('physics_editor.floatiness','physics_editor.floatiness.help','floatiness',{0,1,2},P.FLOAT_IDS)
 local head=row(page,s(20),s) UI.label(head,L'physics_editor.weight',f.Strong,s(20)):Dock(FILL)
 local resetMass=UI.button(head,'↺',function() e:Edit(function(d) d.mass=70 end) end,s(20),f.Small) resetMass:Dock(RIGHT) resetMass:SetWide(s(28))
 local weight=row(page,s(28),s)
 local entry=numberField(weight,s,f,function() return e.draft.mass end,function(v) e:Edit(function(d) d.mass=v end) end,1,500,1,1,function() return e:PhysicsEnabled() and not e.draft.model.automass end)
 entry:Dock(LEFT) entry:SetWide(s(64))
 local unit=UI.label(weight,' '..L'physics_editor.unit.kg',f.Body,s(28)) unit:Dock(LEFT) unit:SetWide(s(30))
 local quick={}
 for n,kg in ipairs({100,70,45}) do local b=UI.button(weight,tostring(kg),function() e:Edit(function(d) d.mass=kg end) end,s(28),f.Small) b:Dock(RIGHT) b:SetWide(s(40)) b:DockMargin(s(4),0,0,0) quick[n]=b end
 local slider=weight:Add('DSlider') slider:Dock(FILL) slider:DockMargin(s(6),s(6),s(6),s(6)) slider:SetLockY(.5) slider:SetTrapInside(true)
 slider.TranslateValues=function(self,x,y) if not e.syncing then local adv=advanced() local lo,hi=adv and 1 or 10,adv and 500 or 300 local v=math.Round(lo+x*(hi-lo)) if v~=e.draft.mass then e:Edit(function(d) d.mass=v end,'mass') end end return x,y end
 local mlabel=UI.label(page,L'physics_editor.material',f.Body,s(20)) mlabel:Dock(TOP) mlabel:DockMargin(0,s(8),0,0)
 local material=materialCombo(e,page,s,f,function() return e.draft.model.surfaceprop end,function(id) e:Edit(function(d) d.model.surfaceprop=id end) end,function() return e:PhysicsEnabled() end)
 material:Dock(TOP)
 e:OnSync(function()
  local on=e:PhysicsEnabled() and not e.draft.model.automass resetMass:SetEnabled(on and e.draft.mass~=70) for _,b in ipairs(quick) do b:SetEnabled(on) end slider:SetEnabled(on) entry:Show()
  local adv=advanced() local lo,hi=adv and 1 or 10,adv and 500 or 300 slider:SetSlideX(math.Clamp((e.draft.mass-lo)/(hi-lo),0,1))
 end)
end
-- The body diagram (§9.4): front view, the character's right on the viewer's left.
local Diagram={[3]={66,8,48,52},[2]={52,66,76,58},[1]={56,126,68,44},[0]={52,172,76,40},[8]={22,66,28,22},[4]={130,66,28,22},[9]={14,90,24,70},[5]={142,90,24,70},
 [10]={10,162,22,66},[6]={148,162,22,66},[11]={6,230,24,28},[7]={150,230,24,28},[15]={56,214,30,84},[12]={94,214,30,84},[16]={58,300,26,62},[13]={96,300,26,62},[17]={50,364,34,14},[14]={96,364,34,14}}
local function diagram(e,parent,s,f)
 local p=parent:Add('DPanel') p:SetSize(s(180),s(380)) p:SetKeyboardInputEnabled(true)
 local function hit(x,y) for i=0,17 do local r=Diagram[i] if x>=s(r[1]) and y>=s(r[2]) and x<=s(r[1]+r[3]) and y<=s(r[2]+r[4]) then return i end end end
 p.Paint=function(self,w,h)
  draw.RoundedBox(4,0,0,w,h,Color(246,248,251))
  for i=0,17 do local r=Diagram[i] local x,y,bw,bh=s(r[1]),s(r[2]),s(r[3]),s(r[4])
   local issue,modified=e:BodyIssue(i),e:BodyModified(i)
   draw.RoundedBox(i==3 and s(20) or s(6),x,y,bw,bh,issue and colors.issue or modified and colors.modified or colors.default)
   local body=e.rig.bodies[i+1]
   if body and body.needsReview then surface.SetDrawColor(colors.review) for k=0,bw,s(6) do surface.DrawLine(x+k,y,x+math.min(k+s(3),bw),y) surface.DrawLine(x+k,y+bh-1,x+math.min(k+s(3),bw),y+bh-1) end end
   if e.selected==i then surface.SetDrawColor(colors.selected) surface.DrawOutlinedRect(x-s(2),y-s(2),bw+s(4),bh+s(4),2) end
   local glyph=issue and '!' or modified and '✎' or nil
   if glyph then draw.SimpleText(glyph,f.Small,x+bw/2,y+bh/2,ink,TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER) end
  end
  draw.SimpleText('R',f.Small,s(4),h-s(16),muted) draw.SimpleText('L',f.Small,w-s(12),h-s(16),muted)
 end
 p.OnMousePressed=function(self,code) local i=hit(self:CursorPos()) if i then e:Select(i) self:RequestFocus() end end
 p.Think=function(self) local i=hit(self:CursorPos()) if i~=self.hover then self.hover=i self:SetTooltip(i and (partName(i)..(advanced() and ('  ('..P.BODIES[i+1]..', '..i..')') or '')) or false) end end
 p.OnKeyCodePressed=function(self,code)
  local r=Diagram[e.selected or 0] local cx,cy=r[1]+r[3]/2,r[2]+r[4]/2 local best,bestD
  local dx,dy=(code==KEY_LEFT and -1) or (code==KEY_RIGHT and 1) or 0,(code==KEY_UP and -1) or (code==KEY_DOWN and 1) or 0
  if dx==0 and dy==0 then return end
  for i=0,17 do local q=Diagram[i] local ox,oy=q[1]+q[3]/2-cx,q[2]+q[4]/2-cy
   if (dx~=0 and ox*dx>4) or (dy~=0 and oy*dy>4) then local d=ox*ox+oy*oy+((dx~=0 and oy*oy) or ox*ox)*3 if not bestD or d<bestD then best,bestD=i,d end end end
  if best then e:Select(best) end
 end
 return p
end
-- i18n-keys: physics_editor.bend_range physics_editor.joint_stiffness
function Tabs.parts(e,page,s,f)
 local UI=mmdhl.UI
 local body=page:Add('DPanel') body:Dock(TOP) body:SetTall(s(400)) body:SetPaintBackground(false)
 local left=body:Add('DPanel') left:Dock(LEFT) left:SetWide(s(190)) left:SetPaintBackground(false)
 UI.label(left,L'physics_editor.part.front_view',f.Small,s(16)):Dock(TOP)
 diagram(e,left,s,f):Dock(TOP)
 local right=body:Add('DPanel') right:Dock(FILL) right:SetPaintBackground(false) right:DockMargin(s(10),0,0,0)
 local title=UI.label(right,'',f.Strong,s(28)) title:Dock(TOP)
 local info=UI.label(right,'',f.Small,s(20)) info:Dock(TOP) info:SetTextColor(muted)
 UI.label(right,L'physics_editor.shape',f.Strong,s(20)):Dock(TOP)
 local function shapeButtons(a,b)
  local r=row(right,s(32),s)
  local x=UI.button(r,L('physics_editor.'..a),function() e:Edit(function(d) P.ShapeAction(d,e.selected,a,e.rig,e:Mirror(),e.preview) end) end,s(32),f.Body)
  local y=UI.button(r,L('physics_editor.'..b),function() e:Edit(function(d) P.ShapeAction(d,e.selected,b,e.rig,e:Mirror(),e.preview) end) end,s(32),f.Body)
  r.PerformLayout=function(_,w,h) local half=math.floor((w-s(6))/2) x:SetPos(0,0) x:SetSize(half,h) y:SetPos(half+s(6),0) y:SetSize(w-half-s(6),h) end
  e:OnSync(function() x:SetEnabled(e:CanEdit()) y:SetEnabled(e:CanEdit()) end)
 end
 shapeButtons('shorter','longer') shapeButtons('thinner','thicker')
 local mirror=UI.checkbox(right,L'physics_editor.mirror_edits','mmdhl_physics_editor_mirror',f.Body,s(24)) mirror:Dock(TOP)
 local resetShape=UI.button(right,L'physics_editor.reset_shape',function() e:Edit(function(d) P.ShapeAction(d,e.selected,'reset',e.rig,e:Mirror()) end) end,s(28),f.Body) resetShape:Dock(TOP) resetShape:DockMargin(0,s(4),0,s(8))
 local jointTitle=UI.label(right,'',f.Strong,s(20)) jointTitle:Dock(TOP)
 local root=wrapLabel(right,L'physics_editor.part.root',f.Small,s(32),muted)
 local function stopCombo(label,field,ids,list)
  local r=row(right,s(28),s) local l=UI.label(r,L(label),f.Body,s(28)) l:Dock(LEFT) l:SetWide(s(96))
  local combo=r:Add('DComboBox') combo:Dock(FILL) UI.styleChoices(combo,s,f.Small)
  combo:AddChoice(L'physics_editor.same_as_body','none') for _,v in ipairs(list) do combo:AddChoice(L('physics_editor.'..(field=='range' and 'range' or 'stiffness')..'.'..ids[v]),v) end
  combo.OnSelect=function(_,_,_,v) if e.syncing then return end e:Edit(function(d) P.SetJointSimple(d,e.selected,field,v~='none' and v or nil,e:Mirror()) end) end
  e:OnSync(function() local j=e.draft.joints[e.selected] or {} local v=j[field] local id=1 for k,stop in ipairs(list) do if stop==v then id=k+1 end end combo:ChooseOptionID(id) combo:SetEnabled(e:PhysicsEnabled() and e.selected>0 and not (field=='range' and j.locked)) r:SetVisible(e.selected>0) end)
 end
 stopCombo('physics_editor.bend_range','range',P.RANGE_IDS,{-2,-1,0,1,2})
 stopCombo('physics_editor.joint_stiffness','stiffness',P.STIFF_IDS,{-2,-1,0,1,2})
 local hinge=UI.checkbox(right,L'physics_editor.hinge_only',nil,f.Body,s(24)) hinge:Dock(TOP)
 hinge.OnChange=function(_,v) if not e.syncing then e:Edit(function(d) P.SetJointSimple(d,e.selected,'hinge',v or nil,e:Mirror()) end) end end
 local lock=UI.checkbox(right,L'physics_editor.lock_joint',nil,f.Body,s(24)) lock:Dock(TOP)
 lock.OnChange=function(_,v) if not e.syncing then e:Edit(function(d) P.SetJointSimple(d,e.selected,'locked',v or nil,e:Mirror()) end) end end
 e:OnSync(function()
  local i=e.selected local b=e:Body(i)
  title:SetText(partName(i)..(e:BodyModified(i) and ('   ✎ '..L'physics_editor.badge.modified') or ''))
  local masses=e.preview and e.preview.bodies and e.preview.bodies[i+1] and e.preview.bodies[i+1].mass
  local shape=e.draft.shapes[i] local extent=shape and shape.extent or (b and b.extent) or {0,0,0}
  info:SetText(L('physics_editor.part.info',{length=fmt(2*extent[1]*2.54,0),kg=fmt(masses,2)}))
  mirror:SetVisible(i>3) resetShape:SetEnabled(e:CanEdit() and (e.draft.shapes[i]~=nil or (e:Mirror() and P.MIRROR[i] and e.draft.shapes[P.MIRROR[i]]~=nil)))
  jointTitle:SetText(i>0 and L('physics_editor.joint_title',{joint=jointName(i)}) or '') root:SetVisible(i==0)
  local j=e.draft.joints[i] or {}
  hinge:SetVisible(P.HINGES[i]==true) hinge:SetChecked(j.hinge==true) hinge:SetEnabled(e:PhysicsEnabled())
  lock:SetVisible(i>0) lock:SetChecked(j.locked==true) lock:SetEnabled(e:PhysicsEnabled())
  right:InvalidateLayout()
 end)
end
function Tabs.collisions(e,page,s,f)
 local UI=mmdhl.UI
 UI.label(page,L'physics_editor.self.title',f.Strong,s(20)):Dock(TOP)
 local radios={}
 local function mode() local c=e.draft.model.collisions if c.mode~='custom' then return c.mode end
  -- Custom pairs of the "these parts pass through" form show the part list; others were made in the Model tab.
  local set={} for i=0,17 do local touched=false for _,p in ipairs(P.NormalizePairs(c.pairs)) do if p[1]==i or p[2]==i then touched=true end end if not touched then set[i]=true end end
  local same=#P.NormalizePairs(c.pairs)==#P.PassThroughPairs(set) return same and 'parts' or 'custom',set end
 for _,id in ipairs({'all','none','parts'}) do
  local key=id=='parts' and 'physics_editor.self.custom' or 'physics_editor.self.'..id
  local r=UI.checkbox(page,L(key),nil,f.Body,s(24)) r:Dock(TOP) radios[id]=r
  r.OnChange=function(_,v)
   if e.syncing then return end
   if not v then e:Sync() return end
   e:Edit(function(d) if id=='parts' then local _,set=mode() d.model.collisions={mode='custom',pairs=P.PassThroughPairs(set or {})} else d.model.collisions={mode=id,pairs={}} end end)
  end
 end
 local customNote=UI.label(page,L'physics_editor.self.custom_pairs',f.Small,s(20)) customNote:Dock(TOP) customNote:SetTextColor(muted)
 local grid=page:Add('DPanel') grid:Dock(TOP) grid:SetTall(s(9*22+8)) grid:SetPaintBackground(false) grid:DockMargin(s(18),0,0,s(6))
 local checks={}
 for i=0,17 do
  local c=UI.checkbox(grid,partName(i),nil,f.Small,s(22)) checks[i]=c
  c.OnChange=function(_,v) if e.syncing then return end local _,set=mode() set=set or {} set[i]=v or nil e:Edit(function(d) d.model.collisions={mode='custom',pairs=P.PassThroughPairs(set)} end) end
 end
 local order={3,0,2,1,4,8,5,9,6,10,7,11,12,15,13,16,14,17}
 grid.PerformLayout=function(_,w) for n,i in ipairs(order) do local c=checks[i] c:SetPos(((n-1)%2)*math.floor(w/2),math.floor((n-1)/2)*s(22)) c:SetWide(math.floor(w/2)-s(4)) end end
 local thickness=row(page,s(28),s) thickness:DockMargin(0,s(8),0,s(2))
 UI.label(thickness,L'physics_editor.thickness.title',f.Body,s(28)):Dock(FILL)
 local thicker=UI.button(thickness,L'physics_editor.thickness.thicker',function() e:Edit(function(d) P.AllThickness(d,1.05,e.rig,e.preview) end) end,s(28),f.Small) thicker:Dock(RIGHT) thicker:SetWide(s(110))
 local thinner=UI.button(thickness,L'physics_editor.thickness.thinner',function() e:Edit(function(d) P.AllThickness(d,1/1.05,e.rig,e.preview) end) end,s(28),f.Small) thinner:Dock(RIGHT) thinner:SetWide(s(110)) thinner:DockMargin(0,0,s(4),0)
 wrapLabel(page,L'physics_editor.thickness.help',f.Small,s(16),muted)
 local overlaps=row(page,s(28),s) overlaps:DockMargin(0,s(8),0,s(4))
 local overlapText=UI.label(overlaps,'',f.Body,s(28)) overlapText:Dock(FILL)
 local fix=UI.button(overlaps,L'physics_editor.fix_overlaps',function() e:FixOverlaps() end,s(28),f.Small) fix:Dock(RIGHT) fix:SetWide(s(130))
 local holder,content=UI.expander(page,L'physics_editor.fit_parts.title',s,f)
 wrapLabel(content,L'physics_editor.fit_parts.help',f.Small,s(32),muted)
 local pending=UI.label(content,L'physics_editor.fit_parts.pending',f.Small,s(20)) pending:Dock(TOP) pending:SetTextColor(colors.warning)
 local excluded={} for _,v in ipairs(e.draft.excludedMaterials) do excluded[v]=true end
 local list,boxes=mmdhl.MaterialRegionList(content,e.ent,excluded,function(sorted) e:Edit(function(d) d.excludedMaterials=sorted end) end) list:SetTall(s(160))
 holder:Resize()
 e:OnSync(function()
  -- Undo, Discard, Previous version and Reset change the parts too: the set the boxes edit
  -- (in place) and the boxes follow the draft. SetChecked does not call OnChange.
  for k in pairs(excluded) do excluded[k]=nil end for _,v in ipairs(e.draft.excludedMaterials or {}) do excluded[v]=true end
  for slot,box in pairs(boxes or {}) do box:SetChecked(not excluded[slot]) box:SetEnabled(e:CanEdit()) end
  local current=mode() local on=e:PhysicsEnabled()
  for id,r in pairs(radios) do r:SetChecked(current==id) r:SetEnabled(on) end
  customNote:SetVisible(current=='custom') grid:SetVisible(current=='parts')
  local _,set=mode() for i,c in pairs(checks) do c:SetChecked(set and set[i]==true) c:SetEnabled(on) end
  thicker:SetEnabled(e:CanEdit()) thinner:SetEnabled(e:CanEdit())
  local n=0 for _,v in ipairs(e.issues or {}) do if v.code=='penetration' or v.code=='penetration_severe' then n=n+1 end end
  overlapText:SetText(L'physics_editor.overlaps.title'..' '..(e.pending and L'physics_editor.fit_parts.pending' or n>0 and L('physics_editor.overlaps.count',{count=n}) or L'physics_editor.overlaps.none')) fix:SetEnabled(e:CanEdit() and n>0 and P.ClientPreview() and not e.pending)
  pending:SetVisible(e.pending==true)
  page:InvalidateLayout()
 end)
end
-- i18n-keys: physics_editor.style.fitted physics_editor.style.box physics_editor.style.capsule physics_editor.mode.limit physics_editor.mode.locked physics_editor.mode.free
-- i18n-keys: physics_editor.joint_preset.default physics_editor.joint_preset.tighter physics_editor.joint_preset.looser physics_editor.joint_preset.hinge physics_editor.joint_preset.locked physics_editor.joint_preset.free
function Tabs.numbers(e,page,s,f)
 local UI=mmdhl.UI
 local pick=row(page,s(28),s) local pl=UI.label(pick,L'physics_editor.numbers.part',f.Body,s(28)) pl:Dock(LEFT) pl:SetWide(s(60))
 local part=pick:Add('DComboBox') part:Dock(FILL) UI.styleChoices(part,s,f.Small)
 for i=0,17 do part:AddChoice(string.format('%02d  %s  (%s)',i,partName(i),P.BODIES[i+1]),i) end
 part.OnSelect=function(_,_,_,i) if not e.syncing then e:Select(i) end end
 local function shapeOf(i) return P.BaseShape(e.draft,i,e.rig,e.preview) or {center={0,0,0},extent={1,1,1},style='fitted'} end
 local function setShape(i,fn) e:Edit(function(d) local shape=shapeOf(i) fn(shape) local _,skipped=P.SetShape(d,i,shape,e.rig,e:Mirror()) e.mirrorSkipped=skipped end) end
 local head=row(page,s(20),s) UI.label(head,L'physics_editor.shape',f.Strong,s(20)):Dock(LEFT)
 for _,axis in ipairs({'Z','Y','X'}) do local l=UI.label(head,axis,f.Small,s(20)) l:Dock(RIGHT) l:SetWide(s(76)) l:SetContentAlignment(5) end
 local u=function() return (P.Unit(e.rig)) end
 for _,kind in ipairs({'center','extent'}) do
  local r=row(page,s(26),s) local l=UI.label(r,L(kind=='center' and 'physics_editor.center' or 'physics_editor.half_size'),f.Body,s(26)) l:Dock(FILL)
  local reset=UI.button(r,'↺',function() e:Edit(function(d) P.ShapeAction(d,e.selected,'reset',e.rig,e:Mirror()) end) end,s(24),f.Small) reset:Dock(RIGHT) reset:SetWide(s(24)) reset:SetTooltip(L'physics_editor.reset_field')
  for k=3,1,-1 do
   local field=numberField(r,s,f,function() return shapeOf(e.selected)[kind][k] end,function(v) setShape(e.selected,function(shape) shape[kind][k]=v end) end,kind=='center' and -72*u() or .01*u(),kind=='center' and 72*u() or 36*u(),.05,4,function() return e:CanEdit() end)
   field:Dock(RIGHT) field:SetWide(s(72)) field:DockMargin(s(4),0,0,0) e:OnSync(function() field:Show() end)
  end
  e:OnSync(function() reset:SetEnabled(e:CanEdit() and e.draft.shapes[e.selected]~=nil) end)
 end
 local styleRow=row(page,s(28),s) local sl=UI.label(styleRow,L'physics_editor.style',f.Body,s(28)) sl:Dock(LEFT) sl:SetWide(s(60))
 local style=styleRow:Add('DComboBox') style:Dock(LEFT) style:SetWide(s(170)) UI.styleChoices(style,s,f.Small)
 for _,id in ipairs({'fitted','box','capsule'}) do style:AddChoice(L('physics_editor.style.'..id),id) end
 style.OnSelect=function(_,_,_,id) if not e.syncing then setShape(e.selected,function(shape) shape.style=id end) end end
 local stats=UI.label(styleRow,'',f.Small,s(28)) stats:Dock(FILL) stats:SetTextColor(muted) stats:DockMargin(s(8),0,0,0)
 e:OnSync(function()
  local shape=shapeOf(e.selected) style:ChooseOptionID(shape.style=='box' and 2 or shape.style=='capsule' and 3 or 1) style:SetEnabled(e:CanEdit() and e.level>=1)
  local b=e:Body(e.selected) stats:SetText(b and b.volume and L('physics_editor.shape_stats',{volume=fmt(b.volume,1),percent=fmt((b.confidence or 0)*100,0)}) or '')
 end)
 local jointHead=row(page,s(28),s) jointHead:DockMargin(0,s(8),0,0)
 local jointTitle=UI.label(jointHead,'',f.Strong,s(28)) jointTitle:Dock(FILL)
 local presets=jointHead:Add('DComboBox') presets:Dock(RIGHT) presets:SetWide(s(130)) UI.styleChoices(presets,s,f.Small) presets:SetValue(L'physics_editor.joint_preset')
 for _,id in ipairs({'default','tighter','looser','hinge','locked','free'}) do presets:AddChoice(L('physics_editor.joint_preset.'..id),id) end
 presets.OnSelect=function(self,_,_,id) if e.syncing then return end e:Edit(function(d) P.ApplyJointPreset(d,e.selected,id,e:Mirror()) end) self:SetValue(L'physics_editor.joint_preset') end
 local note=wrapLabel(page,L'physics_editor.frame_note',f.Small,s(16),muted)
 local columns=row(page,s(20),s)
 for n,key in ipairs({'axis','mode','min','max','friction'}) do local l=UI.label(columns,L('physics_editor.column.'..key),f.Small,s(20)) l:Dock(LEFT) l:SetWide(s(({90,84,64,64,90})[n])) l:SetTextColor(muted) end
 local lastLimit={}
 local axisRows={}
 for _,axis in ipairs(P.AXES) do
  local r=row(page,s(26),s) axisRows[#axisRows+1]=r
  local name=UI.label(r,'',f.Body,s(26)) name:Dock(LEFT) name:SetWide(s(90))
  local modeCombo=r:Add('DComboBox') modeCombo:Dock(LEFT) modeCombo:SetWide(s(84)) UI.styleChoices(modeCombo,s,f.Small)
  for _,id in ipairs({'limit','locked','free'}) do modeCombo:AddChoice(L('physics_editor.mode.'..id),id) end
  local function triple() local b=P.Effective(e.draft)[e.selected] return b.limits and b.limits[axis] or {0,0,0} end
  local function set(t) lastLimit[e.selected..axis]=(t[1]==-360 and t[2]==360) and lastLimit[e.selected..axis] or (t[1]==0 and t[2]==0) and lastLimit[e.selected..axis] or {t[1],t[2],t[3]} e:Edit(function(d) P.SetExplicit(d,e.selected,'limits',t,axis,e:Mirror()) end) end
  modeCombo.OnSelect=function(_,_,_,id)
   if e.syncing then return end local t=triple()
   if id=='locked' then set({0,0,0}) elseif id=='free' then set({-360,360,t[3]})
   else local back=lastLimit[e.selected..axis] or {P.DEFAULTS[e.selected].limits[axis][1],P.DEFAULTS[e.selected].limits[axis][2],t[3]} set({back[1],back[2],back[3]}) end
  end
  local lo=numberField(r,s,f,function() return triple()[1] end,function(v) local t=triple() set({v,t[2],t[3]}) end,-180,180,1,1,function() local t=triple() return e:PhysicsEnabled() and not (t[1]==-360 and t[2]==360) and not (t[1]==0 and t[2]==0) end)
  lo:Dock(LEFT) lo:SetWide(s(64))
  local hi=numberField(r,s,f,function() return triple()[2] end,function(v) local t=triple() set({t[1],v,t[3]}) end,-180,180,1,1,function() local t=triple() return e:PhysicsEnabled() and not (t[1]==-360 and t[2]==360) and not (t[1]==0 and t[2]==0) end)
  hi:Dock(LEFT) hi:SetWide(s(64)) hi:DockMargin(s(4),0,0,0)
  -- Friction shows QC units (×5, as in $jointconstrain); the .phy stores a fifth.
  local fr=numberField(r,s,f,function() return triple()[3]*5 end,function(v) local t=triple() set({t[1],t[2],P.Quantize(v/5,3)}) end,0,500,.5,2,function() local t=triple() return e:PhysicsEnabled() and not (t[1]==0 and t[2]==0) end)
  fr:Dock(LEFT) fr:SetWide(s(64)) fr:DockMargin(s(4),0,0,0)
  local reset=UI.button(r,'↺',function() e:Edit(function(d) P.SetExplicit(d,e.selected,'limits',nil,axis,e:Mirror()) end) end,s(24),f.Small) reset:Dock(LEFT) reset:SetWide(s(24)) reset:DockMargin(s(4),0,0,0) reset:SetTooltip(L'physics_editor.reset_field')
  e:OnSync(function()
   local verified=P.VERIFIED[e.selected] and P.VERIFIED[e.selected][axis]
   name:SetText(axisName(axis)..(verified and '' or ' '..L'physics_editor.unverified')) name:SetTooltip(not verified and L'physics_editor.unverified.tooltip' or false)
   local t=triple() modeCombo:ChooseOptionID((t[1]==-360 and t[2]==360) and 3 or (t[1]==0 and t[2]==0) and 2 or 1) modeCombo:SetEnabled(e:PhysicsEnabled())
   lo:Show() hi:Show() fr:Show() fr:SetTooltip(L('physics_editor.friction.tooltip',{phy=fmt(t[3],3)}))
   local explicit=e.draft.explicit[e.selected] and e.draft.explicit[e.selected].limits and e.draft.explicit[e.selected].limits[axis]
   for _,field in ipairs({lo,hi,fr}) do field:SetTextColor(explicit and ink or muted) end
   reset:SetEnabled(e:PhysicsEnabled() and explicit~=nil) r:SetVisible(e.selected>0)
  end)
 end
 e:OnSync(function() jointTitle:SetText(e.selected>0 and L('physics_editor.joint_to',{joint=jointName(e.selected),parent=partName(P.PARENT[e.selected]),child=partName(e.selected)}) or L'physics_editor.part.root') presets:SetVisible(e.selected>0) presets:SetEnabled(e:PhysicsEnabled()) note:SetVisible(e.selected>0) columns:SetVisible(e.selected>0) end)
 UI.label(page,L'physics_editor.body',f.Strong,s(20)):Dock(TOP)
 local function bodyField(key,field,lo,hi,step,decimals,readout)
  local r=row(page,s(26),s) local l=UI.label(r,L(key),f.Body,s(26)) l:Dock(LEFT) l:SetWide(s(250))
  local value=numberField(r,s,f,function() return P.Effective(e.draft)[e.selected][field] end,function(v) e:Edit(function(d) P.SetExplicit(d,e.selected,field,v,nil,e:Mirror()) end) end,lo,hi,step,decimals,function() return e:PhysicsEnabled() end)
  value:Dock(LEFT) value:SetWide(s(72))
  local extra=UI.label(r,'',f.Small,s(26)) extra:Dock(FILL) extra:DockMargin(s(6),0,0,0) extra:SetTextColor(muted)
  local reset=UI.button(r,'↺',function() e:Edit(function(d) P.SetExplicit(d,e.selected,field,nil,nil,e:Mirror()) end) end,s(24),f.Small) reset:Dock(RIGHT) reset:SetWide(s(24)) reset:SetTooltip(L'physics_editor.reset_field')
  e:OnSync(function() value:Show() local explicit=e.draft.explicit[e.selected] and e.draft.explicit[e.selected][field]~=nil value:SetTextColor(explicit and ink or muted) reset:SetEnabled(e:PhysicsEnabled() and explicit) extra:SetText(readout and readout() or '') end)
  return r,value,extra
 end
 bodyField('physics_editor.mass_share','massBias',.01,100,.1,3,function() local b=e:Body(e.selected) return b and b.mass and L('physics_editor.mass_share.readout',{kg=fmt(b.mass,2)}) or '' end)
 bodyField('physics_editor.damping','damping',0,10,.05,3)
 bodyField('physics_editor.rotdamping','rotdamping',0,100,.5,3)
 bodyField('physics_editor.inertia','inertia',.1,100,1,3)
 local dragRow=row(page,s(26),s) local dl=UI.label(dragRow,L'physics_editor.drag',f.Body,s(26)) dl:Dock(LEFT) dl:SetWide(s(170))
 local dragSet=UI.checkbox(dragRow,L'physics_editor.drag.set',nil,f.Small,s(26)) dragSet:Dock(LEFT) dragSet:SetWide(s(76))
 dragSet.OnChange=function(_,v) if not e.syncing then e:Edit(function(d) P.SetExplicit(d,e.selected,'drag',v and 1 or nil,nil,e:Mirror()) end) end end
 local drag=numberField(dragRow,s,f,function() return P.Effective(e.draft)[e.selected].drag or 0 end,function(v) e:Edit(function(d) P.SetExplicit(d,e.selected,'drag',v,nil,e:Mirror()) end) end,0,100,.1,3,function() return e:PhysicsEnabled() and P.Effective(e.draft)[e.selected].drag~=nil end)
 drag:Dock(LEFT) drag:SetWide(s(72))
 local approx=UI.label(dragRow,' '..L'physics_editor.drag.note',f.Small,s(26)) approx:Dock(FILL) approx:SetTextColor(muted)
 e:OnSync(function() local set=P.Effective(e.draft)[e.selected].drag~=nil dragSet:SetChecked(set) dragSet:SetEnabled(e:PhysicsEnabled()) drag:Show() end)
 local surfaceRow=row(page,s(28),s) local sfl=UI.label(surfaceRow,L'physics_editor.surface',f.Body,s(28)) sfl:Dock(LEFT) sfl:SetWide(s(130))
 local surfaceCombo=materialCombo(e,surfaceRow,s,f,function() return P.Effective(e.draft)[e.selected].surfaceprop end,function(id) e:Edit(function(d) P.SetExplicit(d,e.selected,'surfaceprop',id~=(d.model.surfaceprop or 'flesh') and id or nil,nil,e:Mirror()) end) end,function() return e:PhysicsEnabled() end,true)
 surfaceCombo:Dock(LEFT) surfaceCombo:SetWide(s(190)) surfaceCombo.Entry:Dock(FILL) surfaceCombo.Entry:DockMargin(s(4),0,0,0)
 local actions=row(page,s(28),s) actions:DockMargin(0,s(6),0,0)
 local other=UI.button(actions,L'physics_editor.copy_other_side',function() e:CopyToOtherSide() end,s(28),f.Body) other:Dock(LEFT) other:SetWide(s(180))
 local resetPart=UI.button(actions,L'physics_editor.reset_part',function() e:ResetPart() end,s(28),f.Body) resetPart:Dock(LEFT) resetPart:SetWide(s(140)) resetPart:DockMargin(s(6),0,0,0)
 e:OnSync(function() part:ChooseOptionID(e.selected+1) other:SetEnabled(e:CanEdit() and P.MIRROR[e.selected]~=nil) resetPart:SetEnabled(e:CanEdit() and e:BodyModified(e.selected)) page:InvalidateLayout() end)
end
-- The 18×18 self-collision grid (§9.7): click or drag to toggle a pair; joined pairs never collide.
local function pairGrid(e,parent,s,f)
 local cell,label=s(14),s(24)
 local p=parent:Add('DPanel') p:SetSize(label+18*cell+2,label+18*cell+2) p:SetKeyboardInputEnabled(true)
 local function enabledSet() local c=e.draft.model.collisions local list=c.mode=='all' and P.AllPairs() or c.mode=='custom' and P.NormalizePairs(c.pairs) or {} local set={} for _,q in ipairs(list) do set[q[1]*18+q[2]]=true end return set end
 local function at(x,y) local a,b=math.floor((y-label)/cell),math.floor((x-label)/cell) if a>=0 and b>=0 and a<18 and b<18 then return math.min(a,b),math.max(a,b) end end
 local function toggle(a,b,value)
  if not a or a==b or P.Adjacent(a,b) or not e:PhysicsEnabled() then return end
  local set=enabledSet() if value==nil then value=not set[a*18+b] end if (set[a*18+b]==true)==value then return end
  e:Edit(function(d) set[a*18+b]=value or nil local list={} for k in pairs(set) do list[#list+1]={math.floor(k/18),k%18} end d.model.collisions={mode=#list==0 and 'none' or 'custom',pairs=list} end,'pairs')
 end
 p.Paint=function(self,w,h)
  local set=enabledSet() draw.RoundedBox(2,0,0,w,h,color_white)
  for i=0,17 do draw.SimpleText(tostring(i),f.Small,label+i*cell+cell/2,label/2,muted,TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER) draw.SimpleText(tostring(i),f.Small,label/2,label+i*cell+cell/2,muted,TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER) end
  for a=0,17 do for b=a+1,17 do local x,y=label+b*cell,label+a*cell
   if P.Adjacent(a,b) then surface.SetDrawColor(210,214,220) surface.DrawRect(x+1,y+1,cell-2,cell-2) surface.SetDrawColor(170,176,186) surface.DrawLine(x+1,y+cell-2,x+cell-2,y+1)
   else surface.SetDrawColor(set[a*18+b] and colors.selected or Color(232,237,242)) surface.DrawRect(x+1,y+1,cell-2,cell-2) end
   if self.focusA==a and self.focusB==b and self:HasFocus() then surface.SetDrawColor(colors.warning) surface.DrawOutlinedRect(x,y,cell,cell,2) end
  end end
 end
 p.OnMousePressed=function(self,code) if code~=MOUSE_LEFT then return end self:RequestFocus() local a,b=at(self:CursorPos()) if not a then return end local set=enabledSet() self.paint=not set[a*18+b] self:MouseCapture(true) self.focusA,self.focusB=a,b e.merging=nil toggle(a,b,self.paint) end
 p.OnCursorMoved=function(self,x,y) local a,b=at(x,y) if a and b and a~=b then if self.paint~=nil then toggle(a,b,self.paint) end local set=enabledSet() self:SetTooltip(L('physics_editor.pair.tooltip',{a=partName(a),b=partName(b),state=P.Adjacent(a,b) and L'physics_editor.pair.joined' or set[a*18+b] and L'physics_editor.pair.collide' or L'physics_editor.pair.pass'})) end end
 p.OnMouseReleased=function(self) self.paint=nil self:MouseCapture(false) e.merging=nil end
 p.OnKeyCodePressed=function(self,code)
  local a,b=self.focusA or 0,self.focusB or 2
  if code==KEY_SPACE then toggle(a,b) return end
  if code==KEY_LEFT then b=b-1 elseif code==KEY_RIGHT then b=b+1 elseif code==KEY_UP then a=a-1 elseif code==KEY_DOWN then a=a+1 else return end
  a=math.Clamp(a,0,16) b=math.Clamp(b,a+1,17) self.focusA,self.focusB=a,b
 end
 return p
end
function Tabs.model(e,page,s,f)
 local UI=mmdhl.UI
 local presetRow=row(page,s(28),s) local pl=UI.label(presetRow,L'physics_editor.preset.whole_model',f.Body,s(28)) pl:Dock(LEFT) pl:SetWide(s(170))
 local preset=presetRow:Add('DComboBox') preset:Dock(FILL) UI.styleChoices(preset,s,f.Small)
 for _,id in ipairs(P.PRESET_ORDER) do preset:AddChoice(L('physics_editor.preset.'..id),id) end
 preset.OnSelect=function(_,_,_,id) if not e.syncing then e:Edit(function(d) P.ApplyPreset(d,id,false) end) end end
 UI.label(page,L'physics_editor.weight',f.Strong,s(20)):Dock(TOP)
 local total=row(page,s(28),s) local tl=UI.label(total,L'physics_editor.weight.total',f.Body,s(28)) tl:Dock(LEFT) tl:SetWide(s(110))
 local mass=numberField(total,s,f,function() return e.draft.mass end,function(v) e:Edit(function(d) d.mass=v end) end,1,500,1,1,function() return e:PhysicsEnabled() and not e.draft.model.automass end) mass:Dock(LEFT) mass:SetWide(s(64))
 local kg=UI.label(total,' '..L'physics_editor.unit.kg',f.Body,s(28)) kg:Dock(LEFT) kg:SetWide(s(30))
 local automass=UI.checkbox(total,L'physics_editor.automass',nil,f.Small,s(28)) automass:Dock(FILL) automass:DockMargin(s(8),0,0,0)
 automass.OnChange=function(_,v) if e.syncing then return end e:Edit(function(d) d.model.automass=v
  if v and not d.model.densitySet then local index=util.GetSurfaceIndex(d.model.surfaceprop or 'flesh') local data=index>=0 and util.GetSurfaceData(index) d.model.density=P.Quantize(math.Clamp(data and data.density or 1000,10,20000),1) end end) end
 local density=row(page,s(26),s) local dl=UI.label(density,L'physics_editor.density',f.Body,s(26)) dl:Dock(LEFT) dl:SetWide(s(150))
 local dens=numberField(density,s,f,function() return e.draft.model.density end,function(v) e:Edit(function(d) d.model.density=v d.model.densitySet=true end) end,10,20000,10,1,function() return e:PhysicsEnabled() and e.draft.model.automass end) dens:Dock(LEFT) dens:SetWide(s(72))
 local distribution=UI.label(page,L'physics_editor.distribution',f.Body,s(22)) distribution:Dock(TOP)
 local modes={}
 for _,id in ipairs({'bias','volume'}) do local r=UI.checkbox(page,L('physics_editor.distribution.'..id),nil,f.Small,s(24)) r:Dock(TOP) r:DockMargin(s(18),0,0,0) modes[id]=r -- i18n-keys: physics_editor.distribution.bias physics_editor.distribution.volume
  r.OnChange=function(_,v) if e.syncing then return end if v then e:Edit(function(d) d.model.massMode=id end) else e:Sync() end end end
 local summary=UI.label(page,'',f.Small,s(20)) summary:Dock(TOP) summary:SetTextColor(muted)
 local mrow=row(page,s(28),s) local ml=UI.label(mrow,L'physics_editor.model_material',f.Body,s(28)) ml:Dock(LEFT) ml:SetWide(s(210))
 local material=materialCombo(e,mrow,s,f,function() return e.draft.model.surfaceprop end,function(id) e:Edit(function(d) d.model.surfaceprop=id end) end,function() return e:PhysicsEnabled() end) material:Dock(FILL)
 local pairsHead=row(page,s(24),s) pairsHead:DockMargin(0,s(8),0,0) UI.label(pairsHead,L'physics_editor.pairs.title',f.Strong,s(24)):Dock(FILL)
 for _,id in ipairs({'default','all_off','all_on'}) do -- i18n-keys: physics_editor.pairs.default physics_editor.pairs.all_off physics_editor.pairs.all_on
  local b=UI.button(pairsHead,L('physics_editor.pairs.'..id),function() e:Edit(function(d) d.model.collisions={mode=id=='all_off' and 'none' or 'all',pairs={}} end) end,s(24),f.Small) b:Dock(RIGHT) b:SetWide(s(70)) b:DockMargin(s(4),0,0,0)
  e:OnSync(function() b:SetEnabled(e:PhysicsEnabled()) end)
 end
 local gridRow=row(page,s(24)+18*s(14)+2,s) pairGrid(e,gridRow,s,f):Dock(LEFT)
 UI.label(page,L'physics_editor.animated.title',f.Strong,s(22)):Dock(TOP)
 local animated=UI.checkbox(page,L'physics_editor.animated.enable',nil,f.Body,s(24)) animated:Dock(TOP)
 animated.OnChange=function(_,v) if not e.syncing then e:Edit(function(d) d.model.animatedFriction=v and P.Copy(P.ANIMATED_DEFAULT) or nil end) end end
 local function animField(parent,key,field,lo,hi,step,decimals)
  local l=UI.label(parent,L(key),f.Small,s(26)) l:Dock(LEFT) l:SizeToContentsX(s(6))
  local t=numberField(parent,s,f,function() return e.draft.model.animatedFriction and e.draft.model.animatedFriction[field] end,function(v) e:Edit(function(d) if d.model.animatedFriction then d.model.animatedFriction[field]=v end end) end,lo,hi,step,decimals,function() return e:PhysicsEnabled() and e.draft.model.animatedFriction~=nil end)
  t:Dock(LEFT) t:SetWide(s(56)) t:DockMargin(0,0,s(10),0) e:OnSync(function() t:Show() end)
 end
 local a1=row(page,s(26),s) a1:DockMargin(s(18),0,0,s(2)) animField(a1,'physics_editor.animated.min','min',0,1000,10,0) animField(a1,'physics_editor.animated.max','max',0,1000,10,0)
 local a2=row(page,s(26),s) a2:DockMargin(s(18),0,0,s(2)) animField(a2,'physics_editor.animated.time_in','timeIn',0,10,.05,2) animField(a2,'physics_editor.animated.time_hold','timeHold',0,10,.05,2) animField(a2,'physics_editor.animated.time_out','timeOut',0,10,.05,2)
 wrapLabel(page,L'physics_editor.animated.note',f.Small,s(32),muted):DockMargin(s(18),0,0,s(8))
 UI.label(page,L'physics_editor.interchange',f.Strong,s(22)):Dock(TOP)
 local i1=row(page,s(28),s) local i2=row(page,s(28),s)
 local template=UI.button(i1,L'physics_editor.copy_from_model',function() e:TemplateDialog() end,s(28),f.Small) template:Dock(FILL)
 local copyQC=UI.button(i2,L'physics_editor.qc.copy',function() e:CopyQC() end,s(28),f.Small) copyQC:Dock(LEFT) copyQC:SetWide(s(150))
 local pasteQC=UI.button(i2,L'physics_editor.qc.paste',function() e:PasteDialog() end,s(28),f.Small) pasteQC:Dock(LEFT) pasteQC:SetWide(s(150)) pasteQC:DockMargin(s(6),0,0,0)
 local phy=UI.button(i2,L'physics_editor.phy.view',function() e:PhyDialog() end,s(28),f.Small) phy:Dock(FILL) phy:DockMargin(s(6),0,0,0)
 local carrier=UI.button(page,'',function() if e.preview and e.preview.key then SetClipboardText(e.preview.key) end end,s(20),f.Small) carrier:Dock(TOP) carrier:DockMargin(0,s(8),0,0) carrier:SetContentAlignment(4) carrier.Paint=function() end
 local savedRow=row(page,s(28),s)
 local saved=UI.label(savedRow,'',f.Small,s(28)) saved:Dock(FILL) saved:SetWrap(true)
 local forget=UI.button(savedRow,L'physics_editor.saved.forget',function() e:ForgetDialog() end,s(28),f.Small) forget:Dock(RIGHT) forget:SetWide(s(170))
 e:OnSync(function()
  local on=e:PhysicsEnabled() local m=e.draft.model
  preset:SetEnabled(on) mass:Show() automass:SetChecked(m.automass==true) automass:SetEnabled(on) dens:Show()
  for id,r in pairs(modes) do r:SetChecked((m.massMode or 'bias')==id) r:SetEnabled(on) end
  local b=e.preview and e.preview.bodies
  if b then local heavy,light=0,0 for i=0,17 do if (b[i+1].mass or 0)>(b[heavy+1].mass or 0) then heavy=i end if (b[i+1].mass or 0)<(b[light+1].mass or 0) then light=i end end
   summary:SetText(L('physics_editor.mass_summary',{total=fmt(e.preview.mass,1),heavy=partName(heavy),heavykg=fmt(b[heavy+1].mass,2),light=partName(light),lightkg=fmt(b[light+1].mass,2)}))
  else summary:SetText('') end
  animated:SetChecked(m.animatedFriction~=nil) animated:SetEnabled(on)
  template:SetEnabled(on) pasteQC:SetEnabled(on) phy:SetEnabled(e.preview~=nil and e.preview.phyText~=nil)
  local current,predicted=(e.state.base or ''):sub(1,8),e.preview and e.preview.key and e.preview.key:sub(1,8) or '…'
  carrier:SetText(L('physics_editor.carrier.line',{current=current,predicted=predicted..(predicted==current and ' ('..L'physics_editor.carrier.same'..')' or ' '..L'physics_editor.carrier.new')}))
  local sd=e.state.savedDefault or {}
  saved:SetText(sd.exists and L('physics_editor.saved.info',{name=sd.savedByName or '?',date=sd.savedAt and os.date('%Y-%m-%d',sd.savedAt) or '?'}) or L'physics_editor.saved.none')
  forget:SetVisible(sd.exists==true and e.state.canSave==true)
 end)
end

-- Dialogs ----------------------------------------------------------------------
local function dialog(e,title,w,h)
 if IsValid(e.dialog) then e.dialog:Remove() end
 local s=e.s local d=vgui.Create('DFrame') e.dialog=d d:SetTitle(title) d:SetSize(math.min(s(w),ScrW()-s(40)),math.min(s(h),ScrH()-s(40))) d:Center() d:MakePopup()
 d:DockPadding(s(12),s(30),s(12),s(12))
 return d
end
local HL2={'models/humans/group01/female_01.mdl','models/humans/group01/female_02.mdl','models/humans/group01/female_03.mdl','models/humans/group01/female_04.mdl','models/humans/group01/female_06.mdl','models/humans/group01/female_07.mdl',
 'models/humans/group01/male_01.mdl','models/humans/group01/male_02.mdl','models/humans/group01/male_03.mdl','models/humans/group01/male_04.mdl','models/humans/group01/male_05.mdl','models/humans/group01/male_06.mdl','models/humans/group01/male_07.mdl','models/humans/group01/male_08.mdl','models/humans/group01/male_09.mdl',
 'models/alyx.mdl','models/barney.mdl','models/breen.mdl','models/eli.mdl','models/kleiner.mdl','models/monk.mdl','models/mossman.mdl','models/gman.mdl','models/police.mdl','models/combine_soldier.mdl','models/combine_super_soldier.mdl','models/zombie/classic.mdl'}
-- An installed model's .phy text: the .phy file itself (KeyValues stop after 4096 characters), else KeyValues.
function P.TemplateText(path)
 if not isstring(path) or not path:lower():match('^models/[%w_%-%./]+%.mdl$') or path:find('..',1,true) then return nil end
 local text=P.ReadPhyFile(path)
 if not text then local info=util.GetModelInfo(path) text=info and info.KeyValues end
 return isstring(text) and text~='' and text or nil
end
function Editor:TemplateDialog()
 local UI=mmdhl.UI local s,f=self.s,self.f
 local d=dialog(self,L'physics_editor.template.title',640,600)
 local tabs=row(d,s(28),s) local list=d:Add('DListView') local loaded,phy,path
 local search=row(d,s(28),s) local sl=UI.label(search,L'physics_editor.template.search',f.Body,s(28)) sl:Dock(LEFT) sl:SetWide(s(70))
 local query=search:Add('DTextEntry') query:Dock(FILL) query:SetFont(f.Body)
 list:Dock(TOP) list:SetTall(s(240)) list:SetMultiSelect(false) list:AddColumn(L'physics_editor.template.col_name'):SetWidth(s(240)) list:AddColumn(L'physics_editor.template.col_model'):SetWidth(s(300)) list:AddColumn(L'physics_editor.template.col_parts'):SetWidth(s(60))
 local pathRow=row(d,s(28),s) pathRow:DockMargin(0,s(6),0,s(4)) local pal=UI.label(pathRow,L'physics_editor.template.path',f.Body,s(28)) pal:Dock(LEFT) pal:SetWide(s(90))
 local entry=pathRow:Add('DTextEntry') entry:Dock(FILL) entry:SetFont(f.Small)
 local report=wrapLabel(d,'',f.Small,s(40)) local kept=wrapLabel(d,'',f.Small,s(20),muted)
 local opts={limits=true,friction=true,body=true,mass=true,total=false,collisions=false,animated=false}
 local optRows={row(d,s(24),s),row(d,s(24),s)}
 local boxes={}
 for n,key in ipairs({'limits','friction','body','mass','total','collisions','animated'}) do -- i18n-keys: physics_editor.template.opt_limits physics_editor.template.opt_friction physics_editor.template.opt_body physics_editor.template.opt_mass physics_editor.template.opt_total physics_editor.template.opt_collisions physics_editor.template.opt_animated
  local c=UI.checkbox(optRows[n<=3 and 1 or 2],L('physics_editor.template.'..('opt_'..key),{kg='…'}),nil,f.Small,s(24)) c:Dock(LEFT) c:DockMargin(0,0,s(10),0) c:SetChecked(opts[key]) c.OnChange=function(_,v) opts[key]=v end boxes[key]=c
 end
 wrapLabel(d,L'physics_editor.template.approximate',f.Small,s(32),colors.warning)
 local buttons=row(d,s(32),s) buttons:Dock(BOTTOM)
 local copy=UI.button(buttons,L'physics_editor.template.copy',function()
  if not phy then return end
  self:Edit(function(draft) local result=P.FromTemplate(phy,{limits=opts.limits,friction=opts.friction,body=opts.body,mass=opts.mass,total=opts.total,collisions=opts.collisions,animated=opts.animated,surfaceKnown=function(n) return util.GetSurfaceIndex(n)>=0 end},draft)
   for k in pairs(draft) do draft[k]=nil end for k,v in pairs(result) do draft[k]=v end end)
  d:Close()
 end,s(32),f.Body,true) copy:Dock(RIGHT) copy:SetWide(s(170)) copy:SetEnabled(false)
 local cancel=UI.button(buttons,L'physics_editor.button.cancel',function() d:Close() end,s(32),f.Body) cancel:Dock(RIGHT) cancel:SetWide(s(110)) cancel:DockMargin(0,0,s(8),0)
 local function load(p)
  path=p entry:SetText(p or '') phy=nil copy:SetEnabled(false)
  local text=P.TemplateText(p) local parsed,err
  if text then parsed,err=P.ParsePhyText(text) else err='template.missing' end
  if parsed and #parsed.solids<2 then parsed,err=nil,'template.not_ragdoll' end
  if not parsed then report:SetText(L('physics_editor.'..(err or 'template.missing'),{path=tostring(p),count=1})) kept:SetText('') return end -- i18n-keys: physics_editor.template.missing physics_editor.template.not_ragdoll
  phy=parsed local _,r=P.FromTemplate(phy,{},P.NewDraft())
  report:SetText(L('physics_editor.template.report',{solids=r.solids,bodies=r.bodies,joints=r.joints})..(r.split and ('  '..L('physics_editor.template.split',{a=partName(1),b=partName(2)})) or ''))
  local names={} for _,i in ipairs(r.kept) do names[#names+1]=partName(i) end kept:SetText(#names>0 and L('physics_editor.template.kept',{parts=table.concat(names,', ')}) or '')
  boxes.total:SetText(L('physics_editor.template.opt_total',{kg=fmt(tonumber(phy.editparams.totalmass) or 0,0)}))
  copy:SetEnabled(true)
  for _,line in ipairs(list:GetLines()) do if line.Path==p then line:SetColumnText(3,tostring(#phy.solids)) end end
 end
 local sources={}
 local function show(kind)
  list:Clear() local q=query:GetValue():lower()
  for _,item in ipairs(sources[kind] or {}) do if q=='' or item[1]:lower():find(q,1,true) or item[2]:lower():find(q,1,true) then local line=list:AddLine(item[1],item[2],'') line.Path=item[2] end end
 end
 sources.players={} for name,model in SortedPairs(player_manager.AllValidModels()) do sources.players[#sources.players+1]={name,model} end
 sources.hl2={} for _,model in ipairs(HL2) do if util.IsValidModel(model) then sources.hl2[#sources.hl2+1]={model:match('([^/]+)%.mdl$'),model} end end
 local current='players'
 for n,id in ipairs({'players','hl2','world'}) do -- i18n-keys: physics_editor.template.players physics_editor.template.hl2 physics_editor.template.world
  local b=UI.button(tabs,L('physics_editor.template.'..id),function()
   if id=='world' then
    -- The next click picks the model of the ragdoll under it; Esc, the right button or a click beside one cancels.
    self.pickingTemplate=function(model) if IsValid(d) then d:Show() d:MakePopup() if model then load(model) end end end
    hook.Add('OnPauseMenuShow','MMDHL.PhysicsTemplatePick',function() if E and E.pickingTemplate then E:EndPick() return false end end)
    d:Hide() notification.AddLegacy(L'physics_editor.template.world_hint',NOTIFY_HINT,5) return
   end
   current=id show(id)
  end,s(28),f.Small,'tab') b:Dock(LEFT) b:SetWide(s(150)) b:DockMargin(0,0,s(4),0)
 end
 query.OnChange=function() show(current) end
 list.OnRowSelected=function(_,_,line) load(line.Path) end
 entry.OnEnter=function(self2) load(self2:GetValue()) end
 local loadButton=UI.button(pathRow,L'physics_editor.template.load',function() load(entry:GetValue()) end,s(28),f.Small) loadButton:Dock(RIGHT) loadButton:SetWide(s(70))
 show('players') UI.ownScale(d)
end
function Editor:CopyQC()
 local version=mmdhl.native and mmdhl.native.GetCapabilities and (mmdhl.Decode(mmdhl.native.GetCapabilities()) or {}).version or ''
 SetClipboardText(P.EmitQC(self.draft,{name=self.name,key=self.preview and self.preview.key or self.state.base,version=version}))
 notification.AddLegacy(L'physics_editor.qc.copied',NOTIFY_GENERIC,4)
end
-- i18n-keys: physics_editor.qc.row.applied physics_editor.qc.row.ignored physics_editor.qc.row.error
-- i18n-keys: physics_editor.qc.reason.not_carrier_body physics_editor.qc.reason.unsupported physics_editor.qc.reason.first_wins physics_editor.qc.reason.order physics_editor.qc.reason.args physics_editor.qc.reason.range physics_editor.qc.reason.not_jointed physics_editor.qc.reason.collisiontext physics_editor.qc.reason.no_self physics_editor.qc.reason.joined physics_editor.qc.reason.prefix physics_editor.qc.reason.root
function Editor:PasteDialog()
 local UI=mmdhl.UI local s,f=self.s,self.f
 local d=dialog(self,L'physics_editor.qc.title',720,640)
 wrapLabel(d,L'physics_editor.qc.help',f.Small,s(20),muted)
 local text=d:Add('DTextEntry') text:Dock(TOP) text:SetTall(s(260)) text:SetMultiline(true) text:SetFont(self.mono) text:DockMargin(0,s(4),0,s(6))
 local base=row(d,s(24),s) local bl=UI.label(base,L'physics_editor.qc.base',f.Body,s(24)) bl:Dock(LEFT) bl:SetWide(s(90))
 local studiomdl=UI.checkbox(base,L'physics_editor.qc.base_studiomdl',nil,f.Small,s(24)) studiomdl:Dock(LEFT) studiomdl:DockMargin(0,0,s(12),0)
 local merge=UI.checkbox(base,L'physics_editor.qc.base_merge',nil,f.Small,s(24)) merge:Dock(LEFT)
 local chosen,setting=false,false
 local function choose(useStudiomdl) setting=true studiomdl:SetChecked(useStudiomdl) merge:SetChecked(not useStudiomdl) setting=false end
 studiomdl.OnChange=function(_,v) if not setting then chosen=true choose(v) end end merge.OnChange=function(_,v) if not setting then chosen=true choose(not v) end end choose(false)
 local volume=UI.checkbox(d,L'physics_editor.qc.volume',nil,f.Small,s(24)) volume:Dock(TOP)
 local results=d:Add('DListView') results:Dock(TOP) results:SetTall(s(150)) results:DockMargin(0,s(6),0,s(4))
 results:AddColumn(L'physics_editor.qc.column.line'):SetWidth(s(50)) results:AddColumn(L'physics_editor.qc.column.status'):SetWidth(s(70)) results:AddColumn(L'physics_editor.qc.column.command'):SetWidth(s(200)) results:AddColumn(L'physics_editor.qc.column.effect')
 local summary=UI.label(d,'',f.Small,s(20)) summary:Dock(TOP)
 local buttons=row(d,s(32),s) buttons:Dock(BOTTOM)
 local parsed
 local function check()
  results:Clear()
  local draft,report=P.ParseQC(text:GetValue(),{base=studiomdl:GetChecked() and 'studiomdl' or 'merge',volumeWeighting=volume:GetChecked()},self.draft)
  for _,r in ipairs(report.rows) do
   local params={} for k,v in pairs(r.params or {}) do params[k]=(k=='a' or k=='b') and partName(v) or v end
   results:AddLine(r.line,L('physics_editor.qc.row.'..r.status),r.command,r.reason and L('physics_editor.'..r.reason,params) or '')
  end
  summary:SetText(L('physics_editor.qc.result',{applied=report.applied,ignored=report.ignored,errors=report.errors}))
  parsed={draft=draft,report=report}
  return parsed
 end
 local function import() local result=parsed or check() self:Edit(function(draft) for k in pairs(draft) do draft[k]=nil end for k,v in pairs(result.draft) do draft[k]=v end end) d:Close() end
 local skip=UI.button(buttons,L'physics_editor.button.import_skip',import,s(32),f.Small) skip:Dock(RIGHT) skip:SetWide(s(190))
 local ok=UI.button(buttons,L'physics_editor.button.import',import,s(32),f.Body,true) ok:Dock(RIGHT) ok:SetWide(s(110)) ok:DockMargin(0,0,s(8),0)
 local checkButton=UI.button(buttons,L'physics_editor.button.check',function() check() end,s(32),f.Body) checkButton:Dock(RIGHT) checkButton:SetWide(s(100)) checkButton:DockMargin(0,0,s(8),0)
 local cancel=UI.button(buttons,L'physics_editor.button.cancel',function() d:Close() end,s(32),f.Body) cancel:Dock(LEFT) cancel:SetWide(s(110))
 -- A whole $collisionjoints block compiles from studiomdl's defaults, unless the player chose otherwise.
 text.OnChange=function() parsed=nil if not chosen then choose(text:GetValue():find('$collisionjoints',1,true)~=nil) end end
 -- DFrame's own Think moves the dialog while its title bar is dragged.
 local frameThink=d.Think
 d.Think=function(self) if frameThink then frameThink(self) end local r=parsed and parsed.report ok:SetEnabled(r~=nil and r.errors==0) skip:SetEnabled(r~=nil) end
 UI.ownScale(d)
end
function Editor:PhyDialog()
 local UI=mmdhl.UI local s,f=self.s,self.f
 local d=dialog(self,L'physics_editor.phy.title',720,640)
 local text=d:Add('DTextEntry') text:Dock(FILL) text:SetMultiline(true) text:SetFont(self.mono) text:SetEditable(false) text:SetVerticalScrollbarEnabled(true)
 text:SetText(self.preview and self.preview.phyText or L'physics_editor.phy.unavailable')
 local buttons=row(d,s(32),s) buttons:Dock(BOTTOM) buttons:DockMargin(0,s(8),0,0)
 local close=UI.button(buttons,L'physics_editor.button.close',function() d:Close() end,s(32),f.Body) close:Dock(RIGHT) close:SetWide(s(110))
 local copy=UI.button(buttons,L'physics_editor.button.copy',function() SetClipboardText(text:GetValue()) end,s(32),f.Body) copy:Dock(RIGHT) copy:SetWide(s(110)) copy:DockMargin(0,0,s(8),0)
 copy:SetEnabled(self.preview~=nil and self.preview.phyText~=nil)
 UI.ownScale(d)
end
function Editor:SaveDialog()
 local UI=mmdhl.UI local s,f=self.s,self.f
 local d=dialog(self,L'physics_editor.save.title',520,260)
 wrapLabel(d,L('physics_editor.save.body',{name=self.name}),f.Body,s(80))
 if self:Dirty() then wrapLabel(d,L'physics_editor.save.dirty',f.Small,s(20),colors.warning) end
 local buttons=row(d,s(32),s) buttons:Dock(BOTTOM)
 local save=UI.button(buttons,L'physics_editor.save.save',function() d:Close() self:Send('save_default',{}) end,s(32),f.Body,not self:Dirty()) save:Dock(RIGHT) save:SetWide(s(110))
 if self:Dirty() then
  local both=UI.button(buttons,L'physics_editor.save.apply_and_save',function() d:Close() if not self:HasErrors() and not self.pending then self:Send('apply',{request=self.request},function(ok) if ok then self:Send('save_default',{}) end end) end end,s(32),f.Body,true)
  both:Dock(RIGHT) both:SetWide(s(160)) both:DockMargin(0,0,s(8),0) both:SetEnabled(not self:HasErrors() and not self.pending)
 end
 local cancel=UI.button(buttons,L'physics_editor.button.cancel',function() d:Close() end,s(32),f.Body) cancel:Dock(LEFT) cancel:SetWide(s(110))
 UI.ownScale(d)
end
function Editor:ForgetDialog()
 Derma_Query(L('physics_editor.forget.body',{name=self.name}),L'physics_editor.forget.title',L'physics_editor.saved.forget',function() self:Send('clear_default',{}) end,L'physics_editor.button.cancel',function() end)
end

-- Selection, mirroring, part actions -------------------------------------------------
function Editor:Mirror() return GetConVar('mmdhl_physics_editor_mirror'):GetBool() end
function Editor:Select(i) if i==nil or i<0 or i>17 then return end self.selected=i self:Sync() end
function Editor:CopyToOtherSide()
 local i=self.selected local j=P.MIRROR[i] if not j then return end
 self:Edit(function(d)
  local e=d.explicit[i] d.explicit[j]=nil
  if e then local copy=P.Copy(e) if copy.limits then for axis,t in pairs(copy.limits) do copy.limits[axis]=P.MirrorLimits(i,t,axis) end end d.explicit[j]=copy end
  d.joints[j]=P.Copy(d.joints[i])
  if d.shapes[i] then local m,centered=P.MirrorShape(self.rig,i,d.shapes[i]) self.mirrorSkipped=not centered if not centered then local base=P.BaseShape(d,j,self.rig,self.preview) m.center=base and base.center or {0,0,0} end d.shapes[j]=m else d.shapes[j]=nil end
 end)
end
function Editor:ResetPart()
 self:Edit(function(d) for _,k in ipairs(self:Mirror() and P.MIRROR[self.selected] and {self.selected,P.MIRROR[self.selected]} or {self.selected}) do d.shapes[k]=nil d.explicit[k]=nil d.joints[k]=nil end end)
end
-- "Shrink them" and Fix overlaps: shrink both bodies 6% per preview until the overlap is a warning no more (8 steps at most).
function Editor:FixOverlaps(item)
 if not P.ClientPreview() then return end
 local steps=0
 local function step()
  local list={} for _,v in ipairs(self.issues or {}) do if (v.code=='penetration' or v.code=='penetration_severe') and (not item or (v.params.a==item.params.a and v.params.b==item.params.b)) then list[#list+1]=v end end
  if #list==0 or steps>=8 then self.shrinking=nil return end
  steps=steps+1 self:Edit(function(d) for _,v in ipairs(list) do P.ApplyFix(d,v,'shrink',self.rig,self.preview) end end,'shrink')
  self.shrinking=step
 end
 self.merging=nil step()
end

-- The world: overlay, camera and picking ----------------------------------------------
-- The shapes sit inside the character's mesh: draw them through it, and always
-- restore depth testing so an error cannot leave the rest of the frame without it.
function Editor:DrawWorld()
 if not IsValid(self.ent) or not self.rig or not self.draft then return end
 cam.IgnoreZ(true) local ok,err=pcall(self.DrawShapes,self) cam.IgnoreZ(false)
 if not ok then error(err,0) end
end
function Editor:DrawShapes()
 local ent=self.ent
 ent:InvalidateBoneCache() ent:SetupBones() render.SetColorMaterial()
 local m=(tonumber(self.rig.scale) or 3.23656)/3.23656
 local actual=GetConVar('mmdhl_physics_editor_actual'):GetBool() and ent.MMDHLActualCollision
 for i=0,17 do
  local body=self:Body(i) local rb=self.rig.bodies[i+1] local matrix=rb and ent:GetBoneMatrix(rb.bone)
  if matrix and body and body.hull then
   local pos,ang=matrix:GetTranslation(),matrix:GetAngles()
   local color=self:BodyIssue(i) and colors.hullIssue or self:BodyModified(i) and colors.hullModified or colors.hullUnchanged
   local points={} for k,v in ipairs(body.hull) do points[k]=LocalToWorld(Vector(v[1],v[2],v[3]),angle_zero,pos,ang) end
   for _,edge in ipairs(edges(body)) do local a,b=points[edge[1]],points[edge[2]] if a and b then
    if self.selected==i then render.DrawBeam(a,b,.12*m,0,1,color_white) else render.DrawLine(a,b,color,true) end end end
   local engine=actual and actual[i+1]
   if engine then local pts={} for k,v in ipairs(engine.vertices) do pts[k]=LocalToWorld(Vector(v[1],v[2],v[3]),angle_zero,pos,ang) end
    for _,edge in ipairs(engine.edges) do if pts[edge[1]] and pts[edge[2]] then render.DrawLine(pts[edge[1]],pts[edge[2]],colors.actual,true) end end end
  end
 end
 local i=self.selected
 if i and i>0 then
  local b=P.Effective(self.draft)[i] local lower,upper,dashed={},{},{}
  for k,axis in ipairs(P.AXES) do lower[k]=b.limits[axis][1] upper[k]=b.limits[axis][2] dashed[k]=not (P.VERIFIED[i] and P.VERIFIED[i][axis]) end
  if mmdhl.DrawLimitArcs then self.arcEnds=mmdhl.DrawLimitArcs(ent,self.rig,i,lower,upper,dashed,5*m) self.arcLimits={lower,upper} end
 else self.arcEnds=nil end
 self.overlapLabels={}
 for _,v in ipairs(self.issues or {}) do
  if v.code=='penetration' or v.code=='penetration_severe' then
   local a,b=self.rig.bodies[v.params.a+1],self.rig.bodies[v.params.b+1] local ma,mb=a and ent:GetBoneMatrix(a.bone),b and ent:GetBoneMatrix(b.bone)
   local ca,cb=self:Body(v.params.a),self:Body(v.params.b)
   if ma and mb and ca and cb then local pa=LocalToWorld(vec(ca.center),angle_zero,ma:GetTranslation(),ma:GetAngles()) local pb=LocalToWorld(vec(cb.center),angle_zero,mb:GetTranslation(),mb:GetAngles())
    render.DrawLine(pa,pb,colors.penetration,false) self.overlapLabels[#self.overlapLabels+1]={pos=(pa+pb)/2,text=fmt(v.params.cm,1)..' '..L'physics_editor.unit.cm'} end
  end
 end
end
function Editor:DrawLabels()
 for k,ends in pairs(self.arcEnds or {}) do
  local lower,upper=self.arcLimits[1][k],self.arcLimits[2][k]
  local axis=P.AXES[k] local color=axisColors[axis]
  if ends[1] then local p=ends[1]:ToScreen() if p.visible then draw.SimpleTextOutlined(axis:upper()..' '..fmt(lower,1),'DermaDefault',p.x,p.y,color,TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER,1,color_black) end end
  if ends[2] then local p=ends[2]:ToScreen() if p.visible then draw.SimpleTextOutlined(axis:upper()..' +'..fmt(upper,1),'DermaDefault',p.x,p.y,color,TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER,1,color_black) end end
 end
 for _,l in ipairs(self.overlapLabels or {}) do local p=l.pos:ToScreen() if p.visible then draw.SimpleTextOutlined(l.text,'DermaDefault',p.x,p.y,colors.penetration,TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER,1,color_black) end end
 if self.pending then local left=IsValid(self.frame) and self.frame:GetPos() or ScrW() draw.SimpleTextOutlined(L'physics_editor.fit_parts.pending','DermaLarge',left/2,self.s(60),colors.hullModified,TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER,2,color_black) end
end
function Editor:View()
 if not GetConVar('mmdhl_physics_editor_camera'):GetBool() or not IsValid(self.ent) then return end
 local cam=self.camera local target=cam.target or self.ent:WorldSpaceCenter()
 local ang=Angle(cam.pitch,cam.yaw,0) return target-ang:Forward()*cam.distance,ang
end
function Editor:ResetCamera()
 local ent=self.ent if not IsValid(ent) then return end
 local mins,maxs=ent:OBBMins(),ent:OBBMaxs()
 self.camera={yaw=LocalPlayer():EyeAngles().y,pitch=10,distance=math.Clamp(2.2*(maxs.z-mins.z),60,400)}
end
function Editor:Focus()
 local i=self.selected local rb=self.rig.bodies[i+1] local matrix=rb and IsValid(self.ent) and self.ent:GetBoneMatrix(rb.bone) local b=self:Body(i)
 if not matrix or not b then return end
 self.camera.target=LocalToWorld(vec(b.center),angle_zero,matrix:GetTranslation(),matrix:GetAngles())
 self.camera.distance=math.Clamp(6*math.max(b.extent[1],b.extent[2],b.extent[3]),20,200)
end
-- Ends "pick from world" and shows the template dialog again, with the picked model if any.
function Editor:EndPick(model)
 local pick=self.pickingTemplate self.pickingTemplate=nil hook.Remove('OnPauseMenuShow','MMDHL.PhysicsTemplatePick')
 if pick then pick(model) end
end
-- The body under the cursor: ray against each body's oriented box; clicking the selected one again cycles.
function Editor:Pick(x,y)
 local origin,ang=self:View() local dir
 if origin then dir=util.AimVector(ang,self.fov or LocalPlayer():GetFOV(),x,y,ScrW(),ScrH()) else origin=EyePos() dir=gui.ScreenToVector(x,y) end
 if self.pickingTemplate then
  local tr=util.TraceLine({start=origin,endpos=origin+dir*10000,filter=LocalPlayer()})
  self:EndPick(IsValid(tr.Entity) and tr.Entity:GetClass()=='prop_ragdoll' and tr.Entity:GetModel() or nil) return
 end
 local hits={}
 for i=0,17 do local rb=self.rig.bodies[i+1] local b=self:Body(i) local matrix=rb and self.ent:GetBoneMatrix(rb.bone)
  if matrix and b then local c,e=vec(b.center),vec(b.extent) local hit=util.IntersectRayWithOBB(origin,dir*10000,matrix:GetTranslation(),matrix:GetAngles(),c-e,c+e)
   if hit then hits[#hits+1]={i=i,d=hit:DistToSqr(origin)} end end
 end
 table.sort(hits,function(a,b) return a.d<b.d end)
 if #hits==0 then return end
 local pick=hits[1].i for n,h in ipairs(hits) do if h.i==self.selected and hits[n+1] then pick=hits[n+1].i end end
 self:Select(pick)
end

-- The window -----------------------------------------------------------------------
-- i18n-keys: physics_editor.tab.feel physics_editor.tab.parts physics_editor.tab.collisions physics_editor.tab.numbers physics_editor.tab.model
-- i18n-keys: physics_editor.banner.server_core physics_editor.banner.disabled physics_editor.banner.readonly physics_editor.banner.server_shapes_only physics_editor.banner.client_approximate physics_editor.banner.stale physics_editor.banner.removed physics_editor.banner.model
function Editor:Banner()
 if self.serverCore then return 'server_core' end
 if not self.state then return nil end
 if self.removed then return 'removed' end
 if self.stale then return 'stale' end
 if not self.state.canEdit then return (GetConVar('mmdhl_physics_editor') and GetConVar('mmdhl_physics_editor'):GetInt()==0) and 'disabled' or 'readonly' end
 if self.level<1 then return 'server_shapes_only' end
 if not P.ClientPreview() then return 'client_approximate' end
 if self.modelPreview then return 'model' end
end
function Editor:ShowTab(id)
 local adv=advanced()
 if (id=='numbers' or id=='model') and not adv then id='feel' end
 self.tab=id
 for key,page in pairs(self.pages) do page:SetVisible(key==id) end
 for key,b in pairs(self.tabButtons) do b.Selected=key==id b:SetVisible(adv or (key~='numbers' and key~='model')) end
 self.tabBar:InvalidateLayout()
end
-- i18n-keys: physics_editor.issue.range physics_editor.issue.limit_order physics_editor.issue.root_joint physics_editor.issue.pair_adjacent physics_editor.issue.surfaceprop_unknown physics_editor.issue.shape_range physics_editor.issue.penetration_severe physics_editor.issue.too_large physics_editor.issue.penetration physics_editor.issue.mass_ratio physics_editor.issue.mass_light physics_editor.issue.mass_floor physics_editor.issue.rest_outside physics_editor.issue.free_axis physics_editor.issue.wide_limit physics_editor.issue.high_friction physics_editor.issue.high_damping physics_editor.issue.low_inertia physics_editor.issue.thin_shape physics_editor.issue.heavy physics_editor.issue.axis_unverified physics_editor.issue.animfriction_ragdoll physics_editor.issue.drag_approximate physics_editor.issue.no_self_collision physics_editor.issue.automass_estimate physics_editor.issue.mirror_center_skipped physics_editor.issue.approx_preview physics_editor.issue.unchanged physics_editor.issue.preview_failed
-- i18n-keys: physics_editor.fix.clamp physics_editor.fix.swap physics_editor.fix.remove physics_editor.fix.use_model_material physics_editor.fix.reset_shape physics_editor.fix.pass_through physics_editor.fix.shrink physics_editor.fix.even_weights physics_editor.fix.raise_weight physics_editor.fix.include_rest physics_editor.fix.natural_range physics_editor.fix.reset physics_editor.fix.thicker
function Editor:IssueText(v)
 local params={} for k,value in pairs(v.params or {}) do params[k]=value end
 params.part=v.body and partName(v.body) or '' params.axis=v.axis and axisName(v.axis) or (params.axis and axisName(params.axis)) or ''
 for _,k in ipairs({'a','b','light','heavy'}) do if type(params[k])=='number' then params[k]=partName(params[k]) end end
 for _,k in ipairs({'cm','kg','floor','ratio'}) do if type(params[k])=='number' then params[k]=fmt(params[k],k=='ratio' and 1 or 2) end end
 for _,k in ipairs({'min','max'}) do if type(params[k])=='number' then params[k]=fmt(params[k],1) end end
 return L('physics_editor.issue.'..v.code,params)
end
function Editor:BuildChecks(parent)
 local UI=mmdhl.UI local s,f=self.s,self.f
 local head=UI.label(parent,L'physics_editor.checks.title',f.Strong,s(18)) head:Dock(TOP)
 local list=parent:Add('DScrollPanel') list:Dock(TOP) list:SetTall(s(76))
 local shown
 self:OnSync(function()
  local key={} for _,v in ipairs(self.issues or {}) do key[#key+1]=v.code..(v.body or '')..(v.axis or '')..(v.params.a or '')..(v.params.b or '') end key=table.concat(key,'|')
  if key==shown then return end shown=key list:Clear()
  if #(self.issues or {})==0 then local none=UI.label(list,L'physics_editor.checks.none',f.Small,s(22)) none:Dock(TOP) none:SetTextColor(muted) return end
  for _,v in ipairs(self.issues) do
   local r=list:Add('DPanel') r:Dock(TOP) r:SetTall(s(24)) r:SetPaintBackground(false)
   local glyph=v.severity=='error' and '✖' or v.severity=='warning' and '▲' or '●'
   local icon=UI.label(r,glyph,f.Small,s(24)) icon:Dock(LEFT) icon:SetWide(s(18)) icon:SetTextColor(colors[v.severity])
   for n=#v.fixes,1,-1 do local fix=v.fixes[n]
    if not (fix=='shrink' and not P.ClientPreview()) then
     local b=UI.button(r,L('physics_editor.fix.'..fix),function() if fix=='shrink' then self:FixOverlaps(v) else self:Edit(function(d) P.ApplyFix(d,v,fix,self.rig,self.preview) end) end end,s(22),f.Small)
     b:Dock(RIGHT) surface.SetFont(f.Small) b:SetWide(surface.GetTextSize(b:GetText())+s(16)) b:DockMargin(s(4),s(1),0,s(1)) b:SetEnabled(self:CanEdit())
    end
   end
   if v.body then local go=UI.button(r,L'physics_editor.button.go_to',function() self:Select(v.body) if v.axis or v.code=='range' then self:ShowTab(advanced() and 'numbers' or 'parts') else self:ShowTab('parts') end end,s(22),f.Small)
    go:Dock(RIGHT) surface.SetFont(f.Small) go:SetWide(surface.GetTextSize(go:GetText())+s(16)) go:DockMargin(s(4),s(1),0,s(1)) end
   local text=UI.label(r,self:IssueText(v),f.Small,s(24)) text:Dock(FILL) text:SetTooltip(self:IssueText(v))
  end
 end)
end
function Editor:BuildFooter(parent)
 local UI=mmdhl.UI local s,f=self.s,self.f
 local r1=row(parent,s(40),s)
 local test=UI.button(r1,L'physics_editor.test_copy',function() if not self:HasErrors() and not self.pending then self:Send('test',{request=self.request}) end end,s(40),f.Body) test:Dock(LEFT) test:SetWide(s(140))
 local apply=UI.button(r1,L'physics_editor.apply',function() self:Apply() end,s(40),f.Strong,true) apply:Dock(RIGHT) apply:SetWide(s(200))
 local r2=row(parent,s(32),s)
 local previous=UI.button(r2,L'physics_editor.previous',function()
  local function go() self:Send('previous',{}) end
  if self:Dirty() then Derma_Query(L'physics_editor.confirm.previous',L'physics_editor.title_short',L'physics_editor.previous',go,L'physics_editor.button.cancel',function() end) else go() end
 end,s(32),f.Small) previous:Dock(LEFT) previous:SetWide(s(150))
 local discard=UI.button(r2,L'physics_editor.discard',function() self:Edit(function(d) for k in pairs(d) do d[k]=nil end for k,v in pairs(P.Copy(self.applied)) do d[k]=v end end) end,s(32),f.Small) discard:Dock(LEFT) discard:SetWide(s(150)) discard:DockMargin(s(6),0,0,0)
 local reset=UI.button(r2,L'physics_editor.reset'..' ▾',function(button)
  local menu=DermaMenu()
  local function confirm(choice,op) Derma_Query(L('physics_editor.confirm.reset',{choice=choice}),L'physics_editor.title_short',L'physics_editor.reset',function() self:Send(op,{}) end,L'physics_editor.button.cancel',function() end) end
  -- On the spawn menu's preview, automatic settings also forget the model's saved physics; what
  -- the preview has is the saved version already.
  if self.modelPreview then menu:AddOption(L'physics_editor.reset.automatic',function() Derma_Query(L('physics_editor.model.confirm_reset',{name=self.name}),L'physics_editor.title_short',L'physics_editor.reset',function() self:Send('reset',{}) end,L'physics_editor.button.cancel',function() end) end)
  else
   menu:AddOption(L'physics_editor.reset.automatic',function() confirm(L'physics_editor.reset.automatic','reset') end)
   if self.state.savedDefault and self.state.savedDefault.exists then menu:AddOption(L'physics_editor.reset.saved',function() confirm(L'physics_editor.reset.saved','restore_saved') end) end
  end
  menu:AddOption(L'physics_editor.reset.part',function() self:ResetPart() end)
  menu:Open()
 end,s(32),f.Small) reset:Dock(FILL) reset:DockMargin(s(6),0,0,0)
 local r3=row(parent,s(28),s)
 local save=UI.button(r3,L'physics_editor.save_new_spawns',function() self:SaveDialog() end,s(28),f.Small) save:Dock(LEFT) save:SetWide(s(200))
 local status=UI.label(r3,'',f.Small,s(28)) status:Dock(FILL) status:SetContentAlignment(6)
 self:OnSync(function()
  local edit=self:CanEdit() local errors=self:HasErrors()
  test:SetEnabled(edit and not errors and not self.pending) apply:SetEnabled(edit and self:Dirty() and not errors and not self.pending)
  local model=self.modelPreview
  apply:SetText(self.building and L'physics_editor.status.building' or (self:Dirty() and L(model and 'physics_editor.model.apply_count' or 'physics_editor.apply_count',{count=#self.diff}) or L(model and 'physics_editor.model.apply' or 'physics_editor.apply')))
  previous:SetEnabled(edit and self.state.hasPrevious==true) discard:SetEnabled(not self.building and self:Dirty()) reset:SetEnabled(edit)
  save:SetVisible(self.state.canSave==true and not model) save:SetEnabled(not self.building)
  local text,color=self:Status() status:SetText(text) status:SetTextColor(color)
 end)
end
-- The open editor (scripts, the in-game probes); nil when none is open.
function mmdhl.GetPhysicsEditor() return E end
function mmdhl.OpenPhysicsEditor(ent)
 if not P.Editable(ent) then return end
 if E and E:Valid() then
  if E.ent==ent then E.frame:MakePopup() return end
  if E:Dirty() then Derma_Query(L'physics_editor.confirm.close',L'physics_editor.title_short',L'physics_editor.button.discard',function() E:Close(true) mmdhl.OpenPhysicsEditor(ent) end,L'physics_editor.button.keep_editing',function() end) return end
  E:Close(true)
 end
 if mmdhl.CloseLibrary then mmdhl.CloseLibrary() end
 local UI=mmdhl.UI local s,f=UI.metrics()
 local e=setmetatable({ent=ent,asset=mmdhl.GetAsset(ent),rig=mmdhl.GetRig(ent),selected=6,syncs={},undo={},redo={},issues={},pages={},tabButtons={},s=s,f=f,level=P.Level()},Editor) E=e
 if not e.rig then notification.AddLegacy(L'physics_editor.error.not_ragdoll',NOTIFY_ERROR,5) E=nil return end
 local entry=mmdhl.library and mmdhl.library.entries and mmdhl.library.entries[e.asset]
 e.name=entry and (mmdhl.names and mmdhl.names.EntryName('character',entry) or entry.name) or e.asset:sub(1,12)
 local scale=math.Clamp(ScrH()/1080,1,2) e.mono='MMDHL.PhysicsMono.'..math.Round(scale,2)
 surface.CreateFont(e.mono,{font='Consolas',size=math.Round(14*scale),weight=500,extended=true})
 -- The world layer: clicks select bodies, the right button orbits, the wheel zooms.
 local world=vgui.Create('EditablePanel') e.world=world world:SetPos(0,0) world:SetSize(ScrW(),ScrH()) world:MakePopup() world:SetKeyboardInputEnabled(false) world.Paint=function() end
 world.OnMousePressed=function(self,code)
  local x,y=self:CursorPos()
  if code==MOUSE_RIGHT and e.pickingTemplate then e:EndPick() elseif code==MOUSE_LEFT then e:Pick(x,y) elseif code==MOUSE_RIGHT then self.orbit={x,y} self:MouseCapture(true) end
  if IsValid(e.frame) then e.frame:MoveToFront() end
 end
 world.OnCursorMoved=function(self,x,y) if self.orbit then local c=e.camera c.yaw=c.yaw-(x-self.orbit[1])*.3 c.pitch=math.Clamp(c.pitch+(y-self.orbit[2])*.3,-80,80) self.orbit={x,y} end end
 world.OnMouseReleased=function(self,code) if code==MOUSE_RIGHT then self.orbit=nil self:MouseCapture(false) end end
 world.OnMouseWheeled=function(_,delta) local c=e.camera c.distance=math.Clamp(c.distance*(delta>0 and .9 or 1.1),20,600) end
 local h=math.min(s(900),ScrH()-s(120)) local w=s(520)
 local frame=vgui.Create('DFrame') e.frame=frame frame:SetSize(w,h) frame:SetPos(ScrW()-w-s(24),(ScrH()-h)/2) frame:SetTitle(L('physics_editor.title',{name=e.name})) frame:SetSizable(false) frame:SetDraggable(true) frame:MakePopup()
 frame.btnMinim:SetVisible(false) frame.btnMaxim:SetVisible(false) frame:DockPadding(s(12),s(30),s(12),s(12))
 frame.Paint=function(_,pw,ph) draw.RoundedBox(6,0,0,pw,ph,Color(246,248,251)) draw.RoundedBoxEx(6,0,0,pw,s(24),Color(31,43,58),true,true,false,false) end
 frame.lblTitle:SetTextColor(color_white)
 frame.OnClose=function() e:Close(true) end
 frame.btnClose.DoClick=function() e:Close() end
 frame.OnKeyCodePressed=function(_,code) e:Key(code) end
 local loading=UI.label(frame,L'physics_editor.loading',f.Title,s(40)) loading:Dock(TOP)
 e.overlayBefore=ent.MMDHLFitOverlay ent.MMDHLFitOverlay=false
 e:ResetCamera()
 hook.Add('Think','MMDHL.PhysicsEditor',function() if E then if not E:Valid() then E:Close(true) else E:Think() end end end)
 hook.Add('PostDrawTranslucentRenderables','MMDHL.PhysicsEditor',function(depth,sky) if depth or sky or not E then return end E:DrawWorld() end)
 hook.Add('HUDPaint','MMDHL.PhysicsEditor',function() if E then E:DrawLabels() end end)
 hook.Add('CalcView','MMDHL.PhysicsEditorCamera',function(_,_,_,fov) if not E then return end local origin,ang=E:View() if origin then E.fov=fov return {origin=origin,angles=ang,fov=fov,drawviewer=true} end end)
 mmdhl.PhysicsRequest('open',ent,{},function(state,message,_,data)
  if not e:Valid() then return end
  if state~='state' or not istable(data) then
   loading:SetText(message~='' and message or L'physics_editor.error.no_answer') loading:SetWrap(true) loading:SetFont(f.Body) loading:SetTall(s(60))
   if message==mmdhl.Localize(L'physics_editor.error.server_core') then e.serverCore=true end
   return
  end
  loading:Remove() e:Load(data) e:Build() e:Sync()
 end)
end
function Editor:Key(code)
 local ctrl=input.IsControlDown()
 if ctrl and code==KEY_Z then self:Undo() elseif ctrl and code==KEY_Y then self:Redo()
 elseif ctrl and code==KEY_ENTER then self:Apply()
 elseif ctrl and code==KEY_TAB then local order={'feel','parts','collisions','numbers','model'} local at=1 for n,id in ipairs(order) do if id==self.tab then at=n end end
  local step=input.IsShiftDown() and -1 or 1 repeat at=(at-1+step)%#order+1 until advanced() or at<=3 self:ShowTab(order[at])
 elseif code==KEY_F then self:Focus() elseif code==KEY_HOME then self:ResetCamera()
 elseif code==KEY_ESCAPE then if self.pickingTemplate then self:EndPick() else self:Close() end end
end
function Editor:Build()
 local UI=mmdhl.UI local s,f=self.s,self.f local frame=self.frame
 local header=row(frame,s(40),s)
 local name=UI.label(header,self.name,f.Title,s(40)) name:Dock(FILL)
 local advanced=UI.checkbox(header,L'physics_editor.advanced','mmdhl_physics_editor_advanced',f.Body,s(40)) advanced:Dock(RIGHT)
 local chip=header:Add('DPanel') chip:Dock(RIGHT) chip:SetWide(s(140)) chip:DockMargin(0,s(8),s(8),s(8))
 chip.Paint=function(_,w,h) local text,color=self:Status() draw.RoundedBox(4,0,0,w,h,ColorAlpha(color,40)) draw.SimpleText(text,f.Small,w/2,h/2,color,TEXT_ALIGN_CENTER,TEXT_ALIGN_CENTER) end
 advanced.OnChange=function(_,v) self.advancedSet=v==true self:ShowTab(self.tab or 'feel') self:Sync() end
 local banner=frame:Add('DPanel') banner:Dock(TOP) banner:SetTall(s(40)) banner:DockMargin(0,0,0,s(6))
 local bannerText=UI.label(banner,'',f.Small,s(40)) bannerText:Dock(FILL) bannerText:SetWrap(true) bannerText:DockMargin(s(8),0,s(8),0)
 local reload=UI.button(banner,L'physics_editor.button.reload',function() self:Reopen() end,s(28),f.Small) reload:Dock(RIGHT) reload:SetWide(s(90)) reload:DockMargin(0,s(6),s(6),s(6))
 banner.Paint=function(_,w,h) draw.RoundedBox(4,0,0,w,h,Color(255,244,214)) end
 self:OnSync(function() local id=self:Banner() banner:SetVisible(id~=nil) if id then bannerText:SetText(L('physics_editor.banner.'..id)) end reload:SetVisible(id=='stale') frame:InvalidateLayout() end)
 local footer=frame:Add('DPanel') footer:Dock(BOTTOM) footer:SetPaintBackground(false) footer:SetTall(s(40+32+28+16)) self:BuildFooter(footer)
 local checks=frame:Add('DPanel') checks:Dock(BOTTOM) checks:SetPaintBackground(false) checks:SetTall(s(100)) checks:DockMargin(0,s(6),0,s(6)) self:BuildChecks(checks)
 local tabBar=frame:Add('DPanel') tabBar:Dock(TOP) tabBar:SetTall(s(32)) tabBar:SetPaintBackground(false) tabBar:DockMargin(0,0,0,s(6)) self.tabBar=tabBar
 for _,id in ipairs({'feel','parts','collisions','numbers','model'}) do
  local b=UI.button(tabBar,L('physics_editor.tab.'..id),function() self:ShowTab(id) end,s(32),f.Body,'tab') self.tabButtons[id]=b
  local page=frame:Add('DScrollPanel') page:Dock(FILL) page:SetVisible(false) self.pages[id]=page
  local ok,err=pcall(Tabs[id],self,page,s,f) if not ok then ErrorNoHalt('[Model Hotloader physics editor] '..tostring(err)..'\n') end
 end
 tabBar.PerformLayout=function(_,w,h) local shown={} for _,id in ipairs({'feel','parts','collisions','numbers','model'}) do if self.tabButtons[id]:IsVisible() then shown[#shown+1]=self.tabButtons[id] end end
  local bw=math.floor((w-s(4)*(#shown-1))/math.max(#shown,1)) for n,b in ipairs(shown) do b:SetPos((n-1)*(bw+s(4)),0) b:SetSize(bw,h) end end
 self:ShowTab('feel')
 -- First use: what the shapes are and how to look around.
 if not GetConVar('mmdhl_physics_editor_coached'):GetBool() then
  local tip=frame:Add('DPanel') tip:SetSize(frame:GetWide()-s(24),s(84)) tip:SetPos(s(12),frame:GetTall()-s(84)-s(220)) tip.Paint=function(_,w,h) draw.RoundedBox(6,0,0,w,h,Color(31,43,58,235)) end
  local text=UI.label(tip,L'physics_editor.coach',f.Small,s(60)) text:SetPos(s(10),s(6)) text:SetSize(tip:GetWide()-s(110),s(72)) text:SetWrap(true) text:SetTextColor(color_white)
  local ok=UI.button(tip,L'physics_editor.coach_ok',function() RunConsoleCommand('mmdhl_physics_editor_coached','1') tip:Remove() end,s(32),f.Body,true) ok:SetPos(tip:GetWide()-s(96),s(26)) ok:SetSize(s(86),s(32))
 end
 UI.ownScale(frame)
end
