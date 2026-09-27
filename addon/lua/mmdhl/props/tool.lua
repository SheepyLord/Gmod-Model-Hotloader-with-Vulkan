-- Client side of the Static Prop tool: selecting a prop, the placement ghost
-- and the bone attachment editor (opened by the tool's right click).
local P=mmdhl.props
local L=mmdhl.L
local function toolActive()
 local ply=LocalPlayer() if not IsValid(ply) then return false end
 local weapon=ply:GetActiveWeapon()
 return IsValid(weapon) and weapon:GetClass()=='gmod_tool' and ply:GetInfo('gmod_toolmode')=='mmdhl_prop'
end
-- Put a library prop into the tool and switch to it. In multiplayer an
-- administrator's selection also shares the prop so placement can start.
function P.SelectForTool(id,scale,keepWeapon)
 local entry=P.library.entries[id]
 if not entry then return false,L'props.error.select_in_library' end
 RunConsoleCommand('mmdhl_prop_asset',id)
 if scale then RunConsoleCommand('mmdhl_prop_scale',tostring(math.Clamp(scale,.01,100))) end
 if not game.SinglePlayer() and not P.library.approved[id] then
  if not LocalPlayer():IsAdmin() then return false,L'props.error.not_shared' end
  local ok,err=mmdhl.PublishProp(id) if not ok then return false,err end
  notification.AddLegacy(L('props.tool.sharing',{name=entry.name}),NOTIFY_HINT,6)
 end
 if not keepWeapon then
  RunConsoleCommand('gmod_tool','mmdhl_prop') RunConsoleCommand('use','gmod_tool')
  notification.AddLegacy(L('props.tool.equipped',{name=entry.name}),NOTIFY_GENERIC,6)
 end
 return true
end
-- Placement ghost: the selected prop at the tool's aim, with the tool's size and turn.
local ghost
local function setGhost(id)
 if ghost==id then return end
 if ghost then P.ReleaseRender(ghost) end
 ghost=P.ValidID(id) and id or nil
 if ghost then P.AcquireRender(ghost) end
end
local ghostLight={}
hook.Add('PostDrawTranslucentRenderables','MMDHL.PropToolGhost',function(depth,sky)
 if depth or sky then return end
 if not toolActive() or IsValid(P.attachWindow) then setGhost(nil) return end
 local id=GetConVar('mmdhl_prop_asset'):GetString() setGhost(id)
 local entry=id and P.RenderCache[id] if not entry or entry.state~='ready' then return end
 local ply=LocalPlayer() local tr=ply:GetEyeTrace()
 if not tr.Hit or tr.HitSky or tr.HitPos:DistToSqr(ply:EyePos())>4096^2 then return end
 if IsValid(tr.Entity) and tr.Entity:GetClass()=='mmdhl_prop' and tr.Entity:GetNW2Bool('MMDHLAttached') then return end
 local scale=math.Clamp(GetConVar('mmdhl_prop_scale'):GetFloat(),.01,100)
 local pos,ang=P.SpawnPose(ply,entry.info,scale,tr,GetConVar('mmdhl_prop_yaw'):GetFloat())
 local tint=Color(GetConVar('mmdhl_prop_r'):GetInt(),GetConVar('mmdhl_prop_g'):GetInt(),GetConVar('mmdhl_prop_b'):GetInt(),120)
 P.DrawAsset(id,pos,ang,false,tint,scale,ghostLight) P.DrawAsset(id,pos,ang,true,tint,scale,ghostLight)
end)
-- Attachment editor: choose a bone and an offset; the ghost follows the bone live.
local function boneTransform(target,bone)
 if bone>=0 then
  target:SetupBones()
  local matrix=target:GetBoneMatrix(bone)
  if matrix then return matrix:GetTranslation(),matrix:GetAngles() end
 end
 return target:GetPos(),target:GetAngles()
end
function P.OpenAttachEditor(target,existing)
 local UI=mmdhl.UI if not UI or not IsValid(target) then return end
 if IsValid(P.attachWindow) then P.attachWindow:Close() end
 local id=IsValid(existing) and existing:GetAssetID() or GetConVar('mmdhl_prop_asset'):GetString()
 if not P.ValidID(id) then notification.AddLegacy(L'props.attach.select_prop',NOTIFY_ERROR,5) return end
 P.AcquireRender(id)
 local s,f=UI.metrics()
 local frame=vgui.Create('DFrame') P.attachWindow=frame
 frame:SetTitle('') frame:SetSize(s(430),math.min(ScrH()-s(80),s(760))) frame:SetPos(ScrW()-frame:GetWide()-s(24),s(40)) frame:MakePopup() frame:DockPadding(s(14),s(12),s(14),s(12)) frame.btnMinim:SetVisible(false) frame.btnMaxim:SetVisible(false)
 frame.Paint=function(_,w,h) draw.RoundedBox(6,0,0,w,h,Color(246,248,251,245)) end
 local entry=P.library.entries[id]
 local title=UI.label(frame,IsValid(existing) and L'props.attach.title_adjust' or L'props.attach.title_attach',f.Title,s(34)) title:Dock(TOP)
 local targetName=target:IsPlayer() and target:Nick() or (target.PrintName~='' and target.PrintName) or target:GetClass()
 local sub=UI.label(frame,(entry and entry.name or L'props.attach.unnamed')..'  →  '..tostring(targetName)..' #'..target:EntIndex(),f.Small,s(22)) sub:Dock(TOP) sub:SetTextColor(UI.colors.muted)
 local state={bone=-1,pos=Vector(),ang=Angle(),scale=math.Clamp(GetConVar('mmdhl_prop_scale'):GetFloat(),.05,20)}
 if IsValid(existing) then
  state.bone=existing:GetNW2Int('MMDHLAttachBone',-1) state.pos=existing:GetNW2Vector('MMDHLAttachPos',Vector()) state.ang=existing:GetNW2Angle('MMDHLAttachAng',Angle()) state.scale=P.ScaleOf(existing)
 else
  -- Start at the bone nearest to where the player aimed.
  local hit=LocalPlayer():GetEyeTrace().HitPos local best=math.huge
  target:SetupBones()
  for i=0,target:GetBoneCount()-1 do local m=target:GetBoneMatrix(i) local name=target:GetBoneName(i) if m and name and name~='__INVALIDBONE__' then local d=m:GetTranslation():DistToSqr(hit) if d<best then best=d state.bone=i end end end
 end
 -- Bone list with search.
 local search=frame:Add('DTextEntry') search:Dock(TOP) search:SetTall(s(30)) search:SetFont(f.Body) search:SetPlaceholderText(L'props.attach.search_bones') search:DockMargin(0,s(6),0,s(6))
 local bones=frame:Add('DListView') bones:Dock(TOP) bones:SetTall(s(210)) bones:SetMultiSelect(false) bones:AddColumn(L'props.attach.column_bone') bones:AddColumn('#'):SetFixedWidth(s(40)) bones:SetDataHeight(s(24))
 local function fillBones()
  bones:Clear() local query=string.Trim(search:GetValue()):lower()
  local line=bones:AddLine(L'props.attach.no_bone',-1) line.bone=-1 if state.bone==-1 then bones:SelectItem(line) end
  for i=0,target:GetBoneCount()-1 do
   local name=target:GetBoneName(i)
   if name and name~='' and name~='__INVALIDBONE__' and (query=='' or name:lower():find(query,1,true)) then
    local row=bones:AddLine(name,i) row.bone=i if i==state.bone then bones:SelectItem(row) end
   end
  end
 end
 search.OnChange=fillBones fillBones()
 bones.OnRowSelected=function(_,_,row) state.bone=row.bone end
 -- Offset, rotation and size. Offsets are in the bone's own axes.
 local radius=math.max(24,target:BoundingRadius())
 local sliders={}
 local function slider(text,min,max,decimals,value,apply)
  local sl=frame:Add('DNumSlider') sl:Dock(TOP) sl:SetTall(s(30)) sl:SetText(text) sl:SetMinMax(min,max) sl:SetDecimals(decimals) sl:SetValue(value) sl:SetDark(true) sl.Label:SetFont(f.Body)
  sl.OnValueChanged=function(_,v) apply(v) end sliders[#sliders+1]=sl return sl
 end
 local offsetLabel=UI.label(frame,L'props.attach.offset',f.Strong,s(26)) offsetLabel:Dock(TOP) offsetLabel:DockMargin(0,s(6),0,0)
 slider(L'props.attach.forward',-radius,radius,2,state.pos.x,function(v) state.pos.x=v end)
 slider(L'props.attach.left',-radius,radius,2,state.pos.y,function(v) state.pos.y=v end)
 slider(L'props.attach.up',-radius,radius,2,state.pos.z,function(v) state.pos.z=v end)
 local rotationLabel=UI.label(frame,L'props.attach.rotation',f.Strong,s(26)) rotationLabel:Dock(TOP) rotationLabel:DockMargin(0,s(6),0,0)
 slider(L'props.attach.pitch',-180,180,0,state.ang.p,function(v) state.ang.p=v end)
 slider(L'props.attach.yaw',-180,180,0,state.ang.y,function(v) state.ang.y=v end)
 slider(L'props.attach.roll',-180,180,0,state.ang.r,function(v) state.ang.r=v end)
 slider(L'props.attach.size',.05,20,2,state.scale,function(v) state.scale=v end)
 local buttons=frame:Add('DPanel') buttons:Dock(BOTTOM) buttons:SetTall(s(76)) buttons:SetPaintBackground(false)
 local function send(action)
  net.Start('mmdhl_prop_attach') net.WriteString(action) net.WriteEntity(target) net.WriteEntity(IsValid(existing) and existing or NULL) net.WriteString(id)
  net.WriteInt(state.bone,16) net.WriteVector(state.pos) net.WriteAngle(state.ang) net.WriteFloat(state.scale) net.SendToServer()
 end
 local apply=UI.button(buttons,IsValid(existing) and L'props.attach.save_changes' or L'props.attach.attach',function() send('attach') frame:Close() end,s(34),f.Strong,true)
 local reset=UI.button(buttons,L'props.attach.reset_offset',function() state.pos=Vector() state.ang=Angle() for i=1,6 do sliders[i]:SetValue(0) end end,s(34),f.Body)
 local detach=UI.button(buttons,L'props.attach.detach',function() send('detach') frame:Close() end,s(34),f.Body,'danger') detach:SetEnabled(IsValid(existing))
 local cancel=UI.button(buttons,L'common.cancel',function() frame:Close() end,s(34),f.Body)
 buttons.PerformLayout=function(_,w,h)
  local half=math.floor((w-s(6))/2) local row=s(34)
  apply:SetPos(0,0) apply:SetSize(half,row) reset:SetPos(half+s(6),0) reset:SetSize(w-half-s(6),row)
  detach:SetPos(0,row+s(8)) detach:SetSize(half,row) cancel:SetPos(half+s(6),row+s(8)) cancel:SetSize(w-half-s(6),row)
 end
 local lightState={}
 hook.Add('PostDrawTranslucentRenderables',frame,function(_,depth,sky)
  if depth or sky or not IsValid(target) then return end
  local bonePos,boneAng=boneTransform(target,state.bone)
  local pos,ang=LocalToWorld(state.pos,state.ang,bonePos,boneAng)
  P.DrawAsset(id,pos,ang,false,Color(160,220,255,150),state.scale,lightState) P.DrawAsset(id,pos,ang,true,Color(160,220,255,150),state.scale,lightState)
  render.SetColorMaterial() render.DrawSphere(bonePos,.8,10,10,Color(255,170,40,220))
 end)
 frame.OnRemove=function() P.ReleaseRender(id) end
 UI.ownScale(frame)
 return frame
end
net.Receive('mmdhl_prop_attach_open',function()
 local target,existing=net.ReadEntity(),net.ReadEntity()
 P.OpenAttachEditor(target,existing)
end)
