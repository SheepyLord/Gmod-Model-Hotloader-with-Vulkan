-- Bone assignment window: characters in other formats (FBX, glTF, DAE) before they
-- are converted, and PMX, PMD or VRM models the fitter cannot map (their choices are
-- saved as fitter pins in fit_overrides). The rules are shared, so the server checks
-- a save with them; the window itself is client-only (bone_mapper_ui.lua).
-- autorun sends bone_mapper_rules.lua and bone_mapper_ui.lua before its installation
-- check: a client whose server stopped there still gets them. Without the rules this
-- file stops quietly, and the library imports without the window.
mmdhl.boneMapper=mmdhl.boneMapper or {}
local BM=mmdhl.boneMapper
if SERVER then
 util.AddNetworkString('mmdhl_bonemap_result')
 util.AddNetworkString('mmdhl_bonemap_pins')
end
include('mmdhl/bone_mapper_rules.lua')
if not istable(BM.SlotByKey) or not isfunction(BM.CleanPins) then return end
-- A part's name for messages: a token the client renders in its language.
-- i18n-keys: bonemap.finger.left_thumb bonemap.finger.left_index bonemap.finger.left_middle bonemap.finger.left_ring bonemap.finger.left_little
-- i18n-keys: bonemap.finger.right_thumb bonemap.finger.right_index bonemap.finger.right_middle bonemap.finger.right_ring bonemap.finger.right_little
-- i18n-keys: bonemap.segment.1 bonemap.segment.2 bonemap.segment.3
-- i18n-keys: bonemap.slot.hips bonemap.slot.spine bonemap.slot.middle_spine bonemap.slot.chest bonemap.slot.neck bonemap.slot.head bonemap.slot.left_eye bonemap.slot.right_eye
-- i18n-keys: bonemap.slot.left_shoulder bonemap.slot.left_upper_arm bonemap.slot.left_forearm bonemap.slot.left_hand bonemap.slot.right_shoulder bonemap.slot.right_upper_arm bonemap.slot.right_forearm bonemap.slot.right_hand
-- i18n-keys: bonemap.slot.left_thigh bonemap.slot.left_lower_leg bonemap.slot.left_foot bonemap.slot.left_toes bonemap.slot.right_thigh bonemap.slot.right_lower_leg bonemap.slot.right_foot bonemap.slot.right_toes
function BM.PartLabel(key)
 local s=BM.SlotByKey[key] if not s then return tostring(key or '') end
 if s.segment>0 then return mmdhl.L('bonemap.finger_slot',{finger=mmdhl.L('bonemap.finger.'..s.id:gsub('_%d$','')),segment=mmdhl.L('bonemap.segment.'..s.segment)}) end
 return mmdhl.L('bonemap.slot.'..s.id)
end
function BM.PartList(keys,limit)
 local names={} for i,k in ipairs(keys) do if limit and i>limit then names[#names+1]='…' break end names[#names+1]=BM.PartLabel(k) end
 return table.concat(names,', ')
end
if SERVER then
 local L=mmdhl.L
 local function valid(id) return isstring(id) and #id==64 and not id:find('[^a-f0-9]') end
 -- Who may change how a model is fitted on this server: single player, the listen
 -- host and admins; other addons can refuse with MMDHLCanEditBoneMap.
 function BM.CanEdit(p,id)
  if not (game.SinglePlayer() or (IsValid(p) and (p:IsListenServerHost() or p:IsAdmin()))) then return false end
  return hook.Run('MMDHLCanEditBoneMap',p,id)~=false
 end
 -- Saves the player's bone pins for a model in fit_overrides/<id>.json, next to its
 -- collision corrections, after the server's own fitter accepted them.
 function BM.HandleSave(p,id,value)
  local function reply(ok,token)
   if not IsValid(p) then return end
   net.Start('mmdhl_bonemap_result') net.WriteString(isstring(id) and id or '') net.WriteBool(ok) net.WriteString(tostring(token or '')) net.Send(p)
  end
  local function invalid(reason) reply(false,L('server.error.bonemap_invalid',{reason=reason})) end
  if not BM.CanEdit(p,id) then reply(false,L'server.error.bonemap_permission') return end
  local now=SysTime()
  if (p.MMDHLBoneMapAt or -10)>now-1 then invalid(L'server.error.bonemap_request') return end
  p.MMDHLBoneMapAt=now
  if not valid(id) or not isstring(value) or #value>16384 then invalid(L'server.error.bonemap_request') return end
  local payload=util.JSONToTable(value)
  if not istable(payload) or tonumber(payload.version)~=1 or not istable(payload.boneMap) then invalid(L'server.error.bonemap_request') return end
  local pins=BM.CleanPins(payload.boneMap)
  if not pins then invalid(L'server.error.bonemap_request') return end
  local native=mmdhl.native
  if next(pins) and not isfunction(native.GetBoneMapProposal) then reply(false,L'server.error.bonemap_update') return end
  mmdhl.LoadAsset(id,function(info,err)
   if not IsValid(p) then return end
   if not info then reply(false,L'server.error.bonemap_not_on_server') return end
   if next(pins) then
    local result,e=mmdhl.Decode(native.GetBoneMapProposal(id,BM.IndexJSON('boneMap',pins)))
    if not result then invalid(tostring(e or '')) return end
    -- A bad pin first: a pin that took a required part's bone also leaves that part missing.
    for _,i in ipairs(result.issues or {}) do if istable(i) and i.severity=='error' then invalid(tostring(i.text or i.code or '')) return end end
    if istable(result.missing) and #result.missing>0 then invalid(BM.PartList(result.missing)) return end
    -- The structural rules on what the window showed and checked: the fitter's own
    -- choice with the pins on top (the fitter's torso repairs are its own business).
    if isfunction(native.InspectBoneMap) then
     local base=mmdhl.Decode(native.GetBoneMapProposal(id,'{}'))
     local values={} for _,b in ipairs(istable(base) and base.bones or {}) do if istable(b) and BM.Mapped(b.name) then values[b.name]=tonumber(b.mmd) or -1 end end
     for key,v in pairs(pins) do values[key]=v end
     local check=mmdhl.Decode(native.InspectBoneMap(id,BM.IndexJSON('values',values)))
     for _,i in ipairs(check and check.issues or {}) do if i.severity=='error' then invalid(tostring(i.text or i.code or '')) return end end
    end
   end
   local path='mmd_hotloader/fit_overrides/'..id..'.json'
   local saved=util.JSONToTable(file.Read(path,'DATA') or '') or {}
   if not istable(saved) then saved={} end
   -- Collision corrections were made for the old bones: the server compares them itself.
   local drop=payload.dropCollision==true local before=istable(saved.boneMap) and saved.boneMap or {}
   for _,slot in ipairs(BM.Slots) do if slot.physical and tonumber(before[slot.key])~=pins[slot.key] then drop=true end end
   saved.version=saved.version or 3 saved.generator=saved.generator or 18
   if next(pins) then saved.boneMap=pins saved.boneMapVersion=1 saved.boneMapSavedAt=os.time()
   else saved.boneMap=nil saved.boneMapVersion=nil saved.boneMapSavedAt=nil end
   if drop then saved.bodies=nil saved.scale=nil saved.collisionOverrideScale=nil end
   file.CreateDir('mmd_hotloader/fit_overrides') file.Write(path,util.TableToJSON(saved,true))
   reply(true,L('server.notice.bonemap_saved',{name=tostring(info.name or id:sub(1,12))}))
  end)
 end
 -- The pins a model has on this server and whether it has collision corrections: the
 -- bone window's starting point for an admin on a dedicated server, whose own DATA
 -- folder is not the server's. Pins are not secret; a few answers per second.
 function BM.HandleQuery(p,id)
  if not IsValid(p) or not valid(id) then return end
  local second=math.floor(SysTime()) if p.MMDHLBoneMapQuerySecond~=second then p.MMDHLBoneMapQuerySecond=second p.MMDHLBoneMapQueries=0 end
  p.MMDHLBoneMapQueries=p.MMDHLBoneMapQueries+1 if p.MMDHLBoneMapQueries>8 then return end
  local saved=util.JSONToTable(file.Read('mmd_hotloader/fit_overrides/'..id..'.json','DATA') or '')
  if not istable(saved) then saved={} end
  net.Start('mmdhl_bonemap_pins') net.WriteString(id) net.WriteString(BM.IndexJSON('boneMap',saved.boneMap)) net.WriteBool(istable(saved.bodies) and next(saved.bodies)~=nil) net.Send(p)
 end
end
if CLIENT then include('mmdhl/bone_mapper_ui.lua') end
