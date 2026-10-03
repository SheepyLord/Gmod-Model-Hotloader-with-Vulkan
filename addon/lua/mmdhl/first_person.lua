local L=mmdhl.L
local mode=CreateClientConVar('mmdhl_first_person_body','2',true,false,'Character model first-person body: 0 Auto, 1 On, 2 Off',0,2)
-- Retire the earlier first-person body option. Keep its archived variable for
-- old configs, but body rendering is now always disabled in first person.
local preferenceVersion=CreateClientConVar('mmdhl_first_person_default_version','0',true,false)
if preferenceVersion:GetInt()<2 then mode:SetInt(2) preferenceVersion:SetInt(2) end
local mainView
hook.Add('RenderScene','MMDHL.MainCamera',function(origin) mainView=Vector(origin) end)
function mmdhl.IsLocalFirstPerson(ent,origin)
 if ent~=LocalPlayer() or not ent:Alive() or GetViewEntity()~=ent then return false end
 -- Camera/body addons may request ShouldDrawLocalPlayer while the camera is
 -- still inside the head. Actual camera placement determines first person.
 origin=origin or mainView
 if origin then return origin:DistToSqr(ent:EyePos())<=32^2 end
 return not ent:ShouldDrawLocalPlayer()
end
-- The player's own character, hidden in first person, needs no physics unless
-- another view drew it in the last few frames: a mirror, water or a camera.
function mmdhl.HiddenFirstPerson(ent)
 if not mmdhl.IsLocalFirstPerson(ent) then return false end
 local instance=mmdhl.GetInstance(ent)
 return not (instance>0 and mmdhl.native.GetDrawAge and mmdhl.native.GetDrawAge(instance)<=3)
end
-- Nil means this is another view (third person, a camera or a reflection).
-- Shadow/depth cameras are at the light, so classify them using the player's
-- main camera. A hidden first-person body must not cast a detached shadow.
-- Other views go by the camera of the pass being drawn: EyePos(). During the
-- engine's mirror and water reflections (func_reflective_glass) EyePos() is the
-- mirrored camera while render.GetViewSetup() still reports the main view, which
-- hid the player from mirrors.
function mmdhl.FirstPersonView(ent,depth)
 -- Another addon's first-person body (player_copies.lua). Those addons move the
 -- head and arms of a standard model out of view; in the player's own view the
 -- native first-person mask leaves out the character's head, hair and arms.
 local owner=ent.MMDHLCopyOf
 if owner then
  if depth or owner~=LocalPlayer() then return nil end
  local origin=EyePos()
  if not mmdhl.IsLocalFirstPerson(owner,origin) then return nil end
  -- With no offset (First-Person Body in vehicles, or a forward distance near 0)
  -- the camera is inside the chest: also clip right below it, as the player's
  -- own first-person body did, or collars and capes fill the view.
  local chest=mmdhl.CopyChest and mmdhl.CopyChest(ent)
  if chest and (chest.x-origin.x)^2+(chest.y-origin.y)^2<10^2 then return true end
  return 'mask'
 end
 if depth then
  if mmdhl.IsLocalFirstPerson(ent) then return false end
  return nil
 end
 if mmdhl.IsLocalFirstPerson(ent,EyePos()) then return false end
 return nil
end
function mmdhl.GetArmsParts(id)
 return mmdhl.CleanArmsParts(util.JSONToTable(file.Read('mmd_hotloader/arms/'..id..'.json','DATA') or ''))
end
local previewCallbacks={} local request=0
net.Receive('mmdhl_arms_preview',function()
 local id,key,err=net.ReadUInt(16),net.ReadString(),mmdhl.Localize(net.ReadString()) local done=previewCallbacks[id] previewCallbacks[id]=nil
 if not done then return end
 if key=='' then done(nil,err) return end
 local function ready()
  local rig=util.JSONToTable(file.Read('mmd_hotloader/rigs/'..key..'/rig.json','DATA') or '')
  if not rig then return false end
  if rig.materialGma then mmdhl.MountPackage(rig.materialGma) end
  if not mmdhl.MountPackage('data/mmd_hotloader/rigs/'..key..'/carrier.gma') then return false end
  done(rig) return true
 end
 if not ready() then mmdhl.RequestSharedRig(key,function(ok,error) if not ok then done(nil,error) elseif not ready() then done(nil,L'first_person.arms_incomplete') end end) end
end)
function mmdhl.OpenArmsEditor(id,gender)
 local parts=mmdhl.GetArmsParts(id)
 local frame=vgui.Create('DFrame') frame:SetSize(1000,700) frame:Center() frame:SetTitle(L'first_person.title') frame:MakePopup()
 local status=frame:Add('DLabel') status:Dock(BOTTOM) status:SetTall(32) status:SetText(L'first_person.preparing')
 local apply=frame:Add('DButton') apply:Dock(BOTTOM) apply:SetTall(34) apply:SetText(L'first_person.rebuild')
 local list=frame:Add('DScrollPanel') list:Dock(LEFT) list:SetWide(340)
 local preview=frame:Add('DModelPanel') preview:Dock(FILL) preview:SetFOV(40) preview:SetCamPos(Vector(65,-70,60)) preview:SetLookAt(Vector(0,0,40))
 preview.LayoutEntity=function() end
 local built=false local pending
 -- A closed editor no longer waits for its reply.
 frame.OnRemove=function() if pending then previewCallbacks[pending]=nil end end
 local function rebuild()
  status:SetText(L'first_person.extracting') apply:SetEnabled(false)
  file.CreateDir('mmd_hotloader/arms') file.Write('mmd_hotloader/arms/'..id..'.json',util.TableToJSON(parts,true))
  request=(request+1)%65536 local token=request pending=token
  local function done(rig,err)
   if not IsValid(frame) then return end apply:SetEnabled(true)
   if not rig then status:SetText(err or L'first_person.failed') return end
   preview:SetModel(rig.model)
   if IsValid(preview.Entity) then local lo,hi=preview.Entity:GetRenderBounds() preview:SetLookAt((lo+hi)*.5) preview:SetCamPos((lo+hi)*.5+Vector(1,-1,.5)*math.max(30,(hi-lo):Length())) local seq=preview.Entity:LookupSequence('Reference') if seq>=0 then preview.Entity:SetSequence(seq) end end
   if not built then
    for _,material in ipairs(rig.materials or {}) do
     local row=list:Add('DPanel') row:Dock(TOP) row:SetTall(58) row:DockMargin(0,0,6,5)
     local label=row:Add('DLabel') label:Dock(TOP) label:SetText(material.name) label:SetTextColor(Color(20,20,20))
     local choice=row:Add('DComboBox') choice:Dock(TOP) choice:SetValue(({[0]=L'first_person.part_auto',[1]=L'first_person.part_include',[-1]=L'first_person.part_exclude'})[parts[tostring(material.slot)] or 0])
     choice:AddChoice(L'first_person.part_auto',0) choice:AddChoice(L'first_person.part_include',1) choice:AddChoice(L'first_person.part_exclude',-1)
     choice.OnSelect=function(_,_,_,value) parts[tostring(material.slot)]=value end
    end built=true
   end
   status:SetText(L'first_person.saved')
  end
  previewCallbacks[token]=done
  -- The server may never answer (an older version, a lost message): stop waiting.
  timer.Simple(30,function() if previewCallbacks[token]==done then previewCallbacks[token]=nil done(nil,L'first_person.timed_out') end end)
  net.Start('mmdhl_arms_preview') net.WriteUInt(token,16) net.WriteString(id) net.WriteString(gender=='male' and 'male' or 'female') net.WriteString(util.TableToJSON(parts)) net.SendToServer()
 end
 apply.DoClick=rebuild rebuild()
end
