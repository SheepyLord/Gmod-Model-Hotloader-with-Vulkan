local ent
for _,e in ipairs(ents.FindByClass('prop_ragdoll')) do if e:GetModel():find('models/mmd/',1,true) and e:GetNW2String('MMDHLRig','')~='' then ent=e break end end
assert(IsValid(ent),'No native carrier')
local center=ent:WorldSpaceCenter()
local key=ent:GetNW2String('MMDHLRig','')
local rig=util.JSONToTable(file.Read('mmd_hotloader/rigs/'..key..'/rig.json','DATA'))
hook.Add('CalcView','MMDHL.CarrierProbeCamera',function() local pos=center+Vector(95,90,25) return {origin=pos,angles=(center-pos):Angle(),fov=40,drawviewer=true} end)
hook.Add('PostDrawTranslucentRenderables','MMDHL.CarrierProbeOverlay',function(depth,sky)
 if sky or not IsValid(ent) then return end
 render.SetColorMaterial()
 ent:SetupBones()
 for i,body in ipairs(rig.bodies) do
  local matrix=ent:GetBoneMatrix(body.bone) if not matrix then continue end
  local pos,ang=matrix:GetTranslation(),matrix:GetAngles() local color=HSVToColor((i-1)*360/18,.8,1)
  local c,ext=Vector(unpack(body.center)),Vector(unpack(body.extent))
  render.DrawWireframeBox(pos,ang,c-ext,c+ext,color,true)
  render.DrawLine(pos,pos+ang:Forward()*3,Color(255,0,0),true)
  render.DrawLine(pos,pos+ang:Right()*3,Color(0,255,0),true)
  render.DrawLine(pos,pos+ang:Up()*3,Color(0,100,255),true)
 end
end)
RunConsoleCommand('gameui_hide')
RunConsoleCommand('mmdhl_debug_capture','carrier-collision')
return {entity=ent:EntIndex(),objects=#rig.bodies,resolution={ScrW(),ScrH()}}
