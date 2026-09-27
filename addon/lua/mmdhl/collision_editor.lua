if SERVER then return end
local L=mmdhl.L
function mmdhl.CollisionEditor(panel,ent)
 local rig=mmdhl.GetRig(ent) if not rig then return end
 local enabled=panel:Add('DCheckBoxLabel') enabled:Dock(TOP) enabled:SetTall(28) enabled:SetText(L'collision_editor.inspect')
 enabled.OnChange=function(_,value) ent.MMDHLFitOverlay=value end
 local actual=panel:Add('DCheckBoxLabel') actual:Dock(TOP) actual:SetTall(26) actual:SetText(L'collision_editor.show_actual') actual:SetValue(1)
 actual.OnChange=function(_,value) ent.MMDHLShowActual=value end ent.MMDHLShowActual=true
 local secondary=panel:Add('DCheckBoxLabel') secondary:Dock(TOP) secondary:SetTall(26) secondary:SetText(L'collision_editor.show_secondary')
 secondary.OnChange=function(_,value) ent.MMDHLSecondaryOverlay=value ent.MMDHLFitOverlay=ent.MMDHLFitOverlay or value end
 local select=panel:Add('DComboBox') select:Dock(TOP) select:SetValue(L'collision_editor.select_body')
 for i,b in ipairs(rig.bodies) do select:AddChoice(L('collision_editor.body_choice',{index=string.format('%02d',i-1),name=b.name,percent=string.format('%.0f',b.confidence*100)}),i) end
 local values=panel:Add('DPanel') values:Dock(TOP) values:SetTall(190) local controls={}
 local current
 for _,kind in ipairs({'center','extent'}) do for axis=1,3 do
  local letter=({'X','Y','Z'})[axis] local slider=values:Add('DNumSlider') slider:Dock(TOP) slider:SetText(kind=='center' and L('collision_editor.center_axis',{axis=letter}) or L('collision_editor.extent_axis',{axis=letter})) slider:SetMinMax(kind=='extent' and .01*rig.scale/3.23656 or -36*rig.scale/3.23656,36*rig.scale/3.23656) slider:SetDecimals(2)
  slider.OnValueChanged=function(_,value) if not current or controls.loading then return end ent.MMDHLFitOverrides[current][kind][axis]=value end
  controls[kind..axis]=slider
 end end
 ent.MMDHLFitOverrides=table.Copy(ent.MMDHLFitOverrides or {})
 ent.MMDHLExcludedMaterials=table.Copy(ent.MMDHLExcludedMaterials or rig.excludedMaterials or {})
 select.OnSelect=function(_,_,_,index)
  local b=rig.bodies[index] current=b.name ent.MMDHLFitSelected=index
  ent.MMDHLFitOverrides[current]=ent.MMDHLFitOverrides[current] or {center=table.Copy(b.center),extent=table.Copy(b.extent)}
  controls.loading=true for _,kind in ipairs({'center','extent'}) do for a=1,3 do controls[kind..a]:SetValue(ent.MMDHLFitOverrides[current][kind][a]) end end controls.loading=false
 end
 local regionTitle=panel:Add('DLabel') regionTitle:Dock(TOP) regionTitle:SetTall(25) regionTitle:SetText(L'collision_editor.regions')
 local regions=panel:Add('DScrollPanel') regions:Dock(TOP) regions:SetTall(145)
 local metadata=mmdhl.GetMetadata(ent) local excluded={} for _,i in ipairs(ent.MMDHLExcludedMaterials) do excluded[i]=true end
 for i,material in ipairs(metadata.model and metadata.model.materials or {}) do
  local function label(name) return material.alpha<.01 and L('collision_editor.region_hidden',{index=i-1,name=name}) or (i-1)..'  '..name end
  local check=regions:Add('DCheckBoxLabel') check:Dock(TOP) check:SetTall(24) check:SetText(label(material.name)) check:SetValue(not excluded[i-1])
  if mmdhl.names and isstring(material.name) then mmdhl.names.Bind(check,function(p) mmdhl.names.SetCheckboxText(p,label(mmdhl.names.Both(material.name,mmdhl.GetAsset(ent)))) end) end
  check.OnChange=function(_,include) excluded[i-1]=not include ent.MMDHLExcludedMaterials={} for index,value in pairs(excluded) do if value then table.insert(ent.MMDHLExcludedMaterials,index) end end table.sort(ent.MMDHLExcludedMaterials) end
 end
 local apply=panel:Add('DButton') apply:Dock(TOP) apply:SetTall(28) apply:SetText(L'collision_editor.save')
 apply.DoClick=function() if IsValid(ent) then mmdhl.Action('fit',nil,ent,{bodies=ent.MMDHLFitOverrides,excludedMaterials=ent.MMDHLExcludedMaterials}) end end
 local reset=panel:Add('DButton') reset:Dock(TOP) reset:SetTall(25) reset:SetText(L'collision_editor.reset')
 reset.DoClick=function() ent.MMDHLFitOverrides={} current=nil end
end
local function hullEdges(body)
 if body.edges then return body.edges end local edges={} local seen={}
 for _,face in ipairs(body.faces or {}) do for i,a in ipairs(face) do local b=face[i%#face+1] local key=math.min(a,b)..':'..math.max(a,b)
  if not seen[key] then seen[key]=true edges[#edges+1]={a+1,b+1} end
 end end
 body.edges=edges return edges
end

local function restAngle(bone)
 if bone.restAngle then return bone.restAngle end
 local q=bone.rotation local x,y,z,w=q[1],q[2],q[3],q[4]
 bone.restAngle=Angle(math.deg(math.asin(math.Clamp(2*(w*y-z*x),-1,1))),math.deg(math.atan2(2*(w*z+x*y),1-2*(y*y+z*z))),math.deg(math.atan2(2*(w*x+y*z),1-2*(x*x+y*y))))
 return bone.restAngle
end
local axisColors={Color(255,70,70),Color(70,255,70),Color(70,100,255)}
local function limitArcs(ent,rig,body,pos)
 if body.parent<0 then return end
 local child=rig.bones[body.bone+1] local parent=rig.bones[rig.bodies[body.parent+1].bone+1]
 local live=ent:GetBoneMatrix(rig.bodies[body.parent+1].bone) if not live then return end
 local offset,rotation=WorldToLocal(Vector(unpack(child.position)),restAngle(child),Vector(unpack(parent.position)),restAngle(parent))
 local _,frame=LocalToWorld(offset,rotation,live:GetTranslation(),live:GetAngles())
 for axis,color in ipairs(axisColors) do
  local from,to=body.lower[axis],body.upper[axis] local previous
  for step=0,24 do local a=math.rad(Lerp(step/24,from,to)) local v=Vector()
   v[axis%3+1]=math.cos(a)*5 v[(axis+1)%3+1]=math.sin(a)*5
   local p=LocalToWorld(v,angle_zero,pos,frame)
   if previous then render.DrawLine(previous,p,color,true) end
   if step==0 or step==24 then render.DrawLine(pos,p,color,true) end previous=p
  end
 end
end
hook.Add('Think','MMDHL.FitDiagnostics',function()
 for _,ent in ipairs(mmdhl.Entities()) do if ent.MMDHLFitOverlay and (ent.MMDHLProbeAt or 0)<RealTime() then
  ent.MMDHLProbeAt=RealTime()+.2 ent.MMDHLProbe=mmdhl.GetDiagnostics(ent)
  mmdhl.RequestCollisionMesh(ent)
 end end
end)
hook.Add('PostDrawTranslucentRenderables','MMDHL.CollisionFit',function(depth,sky)
 if depth or sky then return end
 for _,ent in ipairs(mmdhl.Entities()) do
  if not ent.MMDHLFitOverlay then continue end local rig=mmdhl.GetRig(ent) if not rig then continue end
  -- The carrier has no studio mesh, so it may never receive a normal DrawModel
  -- bone setup. Refresh it explicitly before displaying physics/limit frames.
  ent:InvalidateBoneCache() ent:SetupBones()
  for i,body in ipairs(rig.bodies) do
   local bone=ent:GetBoneMatrix(body.bone) if not bone then continue end
   local correction=ent.MMDHLFitOverrides and ent.MMDHLFitOverrides[body.name] or body
   local center,extent=Vector(unpack(correction.center)),Vector(unpack(correction.extent))
   local pos,angle=bone:GetTranslation(),bone:GetAngles()
   local color=ent.MMDHLFitSelected==i and Color(255,255,255) or body.needsReview and Color(255,180,30) or Color(30,220,180)
   local vertices={} for j,v in ipairs(body.hull) do local p=Vector(unpack(v))-Vector(unpack(body.center)) for axis=1,3 do p[axis]=p[axis]*extent[axis]/body.extent[axis] end vertices[j]=LocalToWorld(center+p,angle_zero,pos,angle) end
   local actual=ent.MMDHLActualCollision and ent.MMDHLActualCollision[i]
   if actual and ent.MMDHLShowActual~=false then
    local points={} for j,v in ipairs(actual.vertices) do points[j]=LocalToWorld(Vector(unpack(v)),angle_zero,pos,angle) end
    for _,edge in ipairs(actual.edges) do render.DrawLine(points[edge[1]],points[edge[2]],Color(30,220,220),true) end
   end
   if not actual or ent.MMDHLShowActual==false or correction~=body then
    for _,edge in ipairs(hullEdges(body)) do render.DrawLine(vertices[edge[1]],vertices[edge[2]],correction~=body and Color(255,220,70) or color,true) end
   end
   for axis,color in ipairs({Color(255,70,70),Color(70,255,70),Color(70,100,255)}) do local v=Vector() v[axis]=4 render.DrawLine(pos,LocalToWorld(v,angle_zero,pos,angle),color,true) end
   if ent.MMDHLFitSelected==i then limitArcs(ent,rig,body,pos) end
   if body.parent>=0 then local parent=ent:GetBoneMatrix(rig.bodies[body.parent+1].bone) if parent then render.DrawLine(parent:GetTranslation(),pos,Color(220,220,220),true) end end
  end
  if ent.MMDHLSecondaryOverlay and ent.MMDHLProbe then for _,b in ipairs(ent.MMDHLProbe.bodyList or {}) do if b.worldPosition then
   local color=b.follower and Color(255,210,40) or Color(235,70,200) local extent=Vector(unpack(b.extent))
   render.DrawWireframeBox(Vector(unpack(b.worldPosition)),Angle(unpack(b.worldAngles)),-extent,extent,color,true)
  end end end
 end
end)
hook.Add('HUDPaint','MMDHL.FitProbeText',function()
 local y=80
 for _,ent in ipairs(mmdhl.Entities()) do if ent.MMDHLFitOverlay then
  local rig=mmdhl.GetRig(ent) local d=ent.MMDHLProbe
  local tr=LocalPlayer():GetEyeTrace() local selected=tr.Entity==ent and tr.PhysicsBone+1 or ent.MMDHLFitSelected
  local function line(text) draw.SimpleText(text,'DermaDefaultBold',20,y,color_white) y=y+19 end
  line(L('collision_editor.probe_title',{index=ent:EntIndex()}))
  if selected and rig and rig.bodies[selected] then local b=rig.bodies[selected]
   line(L('collision_editor.probe_bone',{index=selected-1,name=b.name,percent=string.format('%.0f',b.confidence*100),method=b.needsReview and L'collision_editor.needs_review' or b.method or 'legacy'}))
   local function deg(v) return string.format('%.0f',v) end
   line(L('collision_editor.probe_limits',{x_min=deg(b.lower[1]),x_max=deg(b.upper[1]),y_min=deg(b.lower[2]),y_max=deg(b.upper[2]),z_min=deg(b.lower[3]),z_max=deg(b.upper[3])}))
   line(L'collision_editor.probe_axes')
   local binding=rig.bones[b.bone+1] local info=mmdhl.assets[mmdhl.GetAsset(ent)] local original=info and info.boneList and info.boneList[binding.mmd+1]
   if original then line(L('collision_editor.probe_pivot',{index=binding.mmd,name=original.name})) end
  end
  if d then line(string.format('nanoem: %d bodies / %d joints, %d anchors · %.2f ms · dropped %.3f s',d.bodies,d.joints,d.followers or 0,d.stepMs or 0,d.droppedTime or 0)) line(string.format('Largest linear-limit error %.4f PMX units · deformation %.2f ms · rendering %.2f ms',d.maxLinearLimitError or 0,mmdhl.deformMs or 0,mmdhl.renderMs or 0)) end
  if d and d.ticks then line(string.format('60 Hz ticks %d · this frame %d · interpolation %.2f · debt %.4f s · resets %d (%s)',d.ticks,d.lastSteps or 0,d.interpolation or 0,d.debtSeconds or 0,d.resets or 0,d.resetReason or '')) end
  if mmdhl.RequestFrameProfile then mmdhl.RequestFrameProfile(1) end
  local profile=mmdhl.frameProfile
  if profile then line(string.format('Worker barrier %.2f ms · physics work %.2f ms · %d workers',profile.prepareWallMs or 0,profile.physicsWorkMs or 0,profile.workers or 1)) end
 end end
end)
