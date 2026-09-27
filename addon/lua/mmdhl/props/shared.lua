-- Static props: OBJ/FBX/glTF/PMX imported by the worker into content-addressed
-- bundles (data/mmd_hotloader/static). Both realms decode the same bundle; the
-- server owns physics and approval, clients draw it with engine-lit meshes.
mmdhl.props=mmdhl.props or {}
local P=mmdhl.props
local L=mmdhl.L
local native=mmdhl.native
P.native=native
P.Placeholder='models/hunter/blocks/cube025x025x025.mdl'
P.MinScale,P.MaxScale=.01,100
if SERVER then
 for _,name in ipairs({'mmdhl_prop_action','mmdhl_prop_status','mmdhl_prop_catalog','mmdhl_prop_collision','mmdhl_prop_forget'}) do util.AddNetworkString(name) end
end
function P.ValidID(id) return isstring(id) and #id==64 and not id:find('[^0-9a-f]') end
function P.Vector(v) return Vector(tonumber(v and v[1]) or 0,tonumber(v and v[2]) or 0,tonumber(v and v[3]) or 0) end
function P.ValidScale(value) return isnumber(value) and value==value and value>=P.MinScale and value<=P.MaxScale end
-- Float network fields round 0.01 slightly below the lower bound. Canonicalize
-- their precision before checking limits or passing the scale to native code.
function P.CanonicalScale(value)
 if not isnumber(value) or value~=value or value==math.huge or value==-math.huge then return value end
 return math.floor(value*1000000+.5)/1000000
end
function P.ScaleOf(ent)
 local value=P.CanonicalScale(IsValid(ent) and ent.GetPropScale and ent:GetPropScale() or 1)
 return P.ValidScale(value) and value or 1
end
function P.CheckScale(value,info)
 if not P.ValidScale(value) then return false,L'props.error.size_range' end
 if info then
  for i=1,3 do
   if math.max(math.abs(info.mins[i]),math.abs(info.maxs[i]))*value>32768 then return false,L'props.error.size_limit' end
  end
 end
 return true
end
-- Asynchronous bundle loading shared by both realms. Callbacks receive the
-- decoded render manifest (materials, parts, bounds) or an error.
P.Info=P.Info or {}
P.Loading=P.Loading or {}
P.LastUse=P.LastUse or {}
P.Pins=P.Pins or {}
function P.Load(id,callback)
 if not P.ValidID(id) then if callback then callback(nil,L'props.error.invalid_id') end return end
 P.LastUse[id]=RealTime()
 if P.Info[id] then if callback then callback(P.Info[id]) end return end
 if P.Loading[id] then if callback then table.insert(P.Loading[id],callback) end return end
 P.Loading[id]=callback and {callback} or {}
 local ok,err=native.PropRequest(id)
 if not ok then local pending=P.Loading[id] P.Loading[id]=nil for _,cb in ipairs(pending) do cb(nil,err) end end
end
-- Blocking variant for duplicator pastes and save restoration.
function P.LoadNow(id)
 if P.Info[id] then return P.Info[id] end
 local raw,err=native.PropInfo(id,true) local info=raw and util.JSONToTable(raw)
 if not info then return nil,err or L'props.error.invalid_metadata' end
 P.Info[id]=info P.LastUse[id]=RealTime()
 local pending=P.Loading[id] P.Loading[id]=nil
 for _,cb in ipairs(pending or {}) do cb(info) end
 return info
end
hook.Add('Think','MMDHL.PropLoads',function()
 for id,callbacks in pairs(P.Loading) do
  local raw,err=native.PropInfo(id)
  if raw or err then
   local info=raw and util.JSONToTable(raw)
   P.Info[id]=info P.Loading[id]=nil
   if not info then native.PropRelease(id) end
   for _,cb in ipairs(callbacks) do cb(info,err or (not info and L'props.error.invalid_metadata')) end
  end
 end
end)
function P.Forget(id)
 P.Info[id]=nil
 native.PropRelease(id)
end
function P.Name(info) return info and info.name and tostring(info.name) or L'props.imported_prop' end
-- Rotated bounds: distance from the prop origin to its surface along -normal,
-- so a prop placed on a wall or ceiling rests against it instead of through it.
function P.SupportDistance(info,scale,ang,normal)
 local mins,maxs=P.Vector(info.mins)*scale,P.Vector(info.maxs)*scale local best=-math.huge
 for x=0,1 do for y=0,1 do for z=0,1 do
  local corner=Vector(x==0 and mins.x or maxs.x,y==0 and mins.y or maxs.y,z==0 and mins.z or maxs.z) corner:Rotate(ang)
  best=math.max(best,-corner:Dot(normal))
 end end end
 return best
end
-- Placement pose shared by the server and the tool's ghost preview.
function P.SpawnPose(p,info,scale,tr,yaw)
 -- Imported fronts (glTF +Z, PMX -Z, FBX front axis) land on local -Y; face the player.
 local ang=Angle(0,math.NormalizeAngle(p:EyeAngles().y-90+(tonumber(yaw) or 0)),0)
 local normal=tr.HitNormal
 return tr.HitPos+normal*(P.SupportDistance(info,scale,ang,normal)+1),ang
end
