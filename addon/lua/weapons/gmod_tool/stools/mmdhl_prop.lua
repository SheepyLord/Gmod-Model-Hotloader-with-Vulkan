-- Static Prop tool: place imported props and attach them to bones.
local L=mmdhl.L
TOOL.Category='Construction'
TOOL.Name='#tool.mmdhl_prop.name'
TOOL.Command=nil
TOOL.ConfigName=''
TOOL.ClientConVar={asset='',scale='1',yaw='0',frozen='0',collide='world',gravity='1',physprop='default',r='255',g='255',b='255'}
TOOL.Information={{name='left'},{name='right'},{name='reload'}}
if CLIENT then
 language.Add('tool.mmdhl_prop.name',L'tool.prop.name')
 language.Add('tool.mmdhl_prop.desc',L'tool.prop.desc')
 language.Add('tool.mmdhl_prop.left',L'tool.prop.left')
 language.Add('tool.mmdhl_prop.right',L'tool.prop.right')
 language.Add('tool.mmdhl_prop.reload',L'tool.prop.reload')
end
local function settings(tool)
 return {scale=math.Clamp(tool:GetClientNumber('scale',1),.01,100),yaw=math.Clamp(tool:GetClientNumber('yaw',0),-180,180),frozen=tool:GetClientNumber('frozen',0)==1,
  collide=tool:GetClientInfo('collide'),gravity=tool:GetClientNumber('gravity',1)==1,physprop=tool:GetClientInfo('physprop'),color={tool:GetClientNumber('r',255),tool:GetClientNumber('g',255),tool:GetClientNumber('b',255)}}
end
local function ready(tool)
 if not mmdhl or not mmdhl.FeatureAvailable or not mmdhl.FeatureAvailable('physics') then
  local p=tool:GetOwner() if SERVER and IsValid(p) and mmdhl and mmdhl.ChatPrint then mmdhl.ChatPrint(p,L'common.unavailable') end return
 end
 local P=mmdhl and mmdhl.props local p=tool:GetOwner() local id=tool:GetClientInfo('asset')
 if not P or not P.ValidID(id) then if P then P.Notice(p,L'tool.prop.select_first') end return end
 if not P.CanUse(p,id) then P.Notice(p,L'tool.prop.not_shared') return end
 return P,p,id
end
-- Tool clicks are not predicted in single-player; the server owns every action.
function TOOL:LeftClick(trace)
 if CLIENT then return true end
 local P,p,id=ready(self) if not P then return false end
 if (p.MMDHLNextToolPlace or 0)>CurTime() then return false end p.MMDHLNextToolPlace=CurTime()+.3
 P.PlaceAt(p,id,trace,settings(self),function(ent,result) if not IsValid(ent) then P.Notice(p,result) end end)
 return true
end
function TOOL:RightClick(trace)
 if CLIENT then return true end
 local P=mmdhl and mmdhl.props local ent=trace.Entity local p=self:GetOwner()
 if not P or not IsValid(ent) or ent:IsWorld() then return false end
 if ent:GetClass()=='mmdhl_prop' and ent:GetNW2Bool('MMDHLAttached') then
  if not P.Owns(p,ent) then P.Notice(p,L'props.error.not_owner') return false end
  P.OpenAttach(p,ent:GetParent(),ent) return true
 end
 if not ready(self) then return false end
 P.OpenAttach(p,ent,NULL) return true
end
function TOOL:Reload(trace)
 if CLIENT then return true end
 local P=mmdhl and mmdhl.props local ent=trace.Entity local p=self:GetOwner()
 if not P or not IsValid(ent) or ent:GetClass()~='mmdhl_prop' then return false end
 if ent:GetNW2Bool('MMDHLAttached') then
  if not P.Owns(p,ent) then P.Notice(p,L'props.error.not_owner') return false end
  local ok,err=P.Detach(ent) P.Notice(p,ok and L'props.detached' or err) return true
 end
 p:ConCommand('mmdhl_prop_asset '..ent:GetAssetID()) p:ConCommand('mmdhl_prop_scale '..P.ScaleOf(ent))
 P.Notice(p,L('tool.prop.copied',{name=P.Name(P.Info[ent:GetAssetID()])}))
 return true
end
if CLIENT then
 function TOOL.BuildCPanel(panel)
  local P=mmdhl and mmdhl.props
  panel:Help(L'tool.prop.help')
  panel:Button(L'tool.prop.open_library','mmdhl_open_props')
  local search=vgui.Create('DTextEntry') search:SetPlaceholderText(L'tool.prop.search') panel:AddItem(search)
  local list=vgui.Create('DListView') list:SetTall(260) list:SetMultiSelect(false) list:AddColumn(L'tool.prop.column_prop') list:AddColumn(L'tool.prop.column_triangles'):SetFixedWidth(70) panel:AddItem(list)
  local function fill()
   if not IsValid(list) or not P then return end
   list:Clear() local query=string.Trim(search:GetValue()):lower() local current=GetConVar('mmdhl_prop_asset'):GetString()
   local rows={} for id,entry in pairs(P.library.entries) do
    local shown=mmdhl.names and mmdhl.names.EntryName('static',entry) or entry.name
    if query=='' or (shown..' '..entry.name):lower():find(query,1,true) then rows[#rows+1]={id=id,entry=entry,name=shown} end
   end
   table.sort(rows,function(a,b) return a.name:lower()<b.name:lower() end)
   for _,r in ipairs(rows) do local line=list:AddLine(r.name,string.Comma(r.entry.info.triangles or 0)) line.asset=r.id if r.id==current then list:SelectItem(line) end end
  end
  search.OnChange=fill
  list.OnRowSelected=function(_,_,line) if P and line.asset then P.SelectForTool(line.asset,nil,true) end end
  hook.Add('MMDHL.PropLibraryChanged',list,fill) hook.Add('MMDHL.NamesTranslated',list,fill) hook.Add('MMDHL.NamesChanged',list,fill) fill()
  panel:NumSlider(L'tool.prop.size','mmdhl_prop_scale',.05,20,2)
  panel:NumSlider(L'tool.prop.turn','mmdhl_prop_yaw',-180,180,0)
  panel:CheckBox(L'tool.prop.freeze','mmdhl_prop_frozen')
  local collide=panel:ComboBox(L'tool.prop.collides_with','mmdhl_prop_collide')
  for _,m in ipairs(P and P.CollisionModes or {}) do collide:AddChoice(m.label,m.id,GetConVar('mmdhl_prop_collide'):GetString()==m.id) end
  panel:CheckBox(L'tool.prop.gravity','mmdhl_prop_gravity')
  panel:ControlHelp(L'tool.prop.collision_help')
  local material=panel:ComboBox(L'tool.prop.physics_material','mmdhl_prop_physprop')
  for _,m in ipairs(P and P.SurfaceMaterials or {}) do material:AddChoice(m.label,m.id,GetConVar('mmdhl_prop_physprop'):GetString()==m.id) end
  local mixer=vgui.Create('DColorMixer') mixer:SetPalette(true) mixer:SetAlphaBar(false) mixer:SetWangs(true) mixer:SetTall(150)
  mixer:SetConVarR('mmdhl_prop_r') mixer:SetConVarG('mmdhl_prop_g') mixer:SetConVarB('mmdhl_prop_b') panel:AddItem(mixer)
  panel:Help(L'tool.prop.attach_help')
 end
end
