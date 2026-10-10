-- Chain the installed tool's panel/selection handlers; other entities retain
-- exactly the existing panel and tool behavior, including addon extensions.
local L=mmdhl.L
local function capture(ent)
 local pose={} for _,morph in ipairs(mmdhl.GetMorphs(ent)) do pose[morph.name]=mmdhl.GetMorphWeight(ent,morph.mmd) end return pose
end
-- pose: morph name -> weight. A value that is no finite number (a damaged preset) counts as 0;
-- names this model does not have are ignored.
local function apply(ent,pose)
 if not istable(pose) then return false end
 local weights={} for i,morph in ipairs(mmdhl.GetMorphs(ent)) do local value=pose[morph.name] if not (isnumber(value) and value==value and math.abs(value)~=math.huge) then value=0 end weights[i]=value
  if CLIENT and morph.native>=0 then RunConsoleCommand('faceposer_flex'..morph.native,tostring(value)) end
 end
 mmdhl.SetMorphWeights(ent,weights)
 if CLIENT then local panel=controlpanel.Get('faceposer') if IsValid(panel) then panel.MMDUpdating=true for index,slider in pairs(panel.MMDOverflowControls or {}) do if IsValid(slider) then slider:SetValue(weights[index+1] or 0) end end panel.MMDUpdating=false end end
end
mmdhl.CaptureMorphPose=capture
mmdhl.ApplyMorphPose=apply
local function install()
 local stored=weapons.GetStored('gmod_tool') local tool=stored and stored.Tool and stored.Tool.faceposer
 if not tool or tool.MMDHLOverflow then return end tool.MMDHLOverflow=true
 if SERVER then
  local right,left=tool.RightClick,tool.LeftClick
  tool.RightClick=function(self,tr,...) local result=right(self,tr,...); if mmdhl.IsMMD(tr.Entity) then self:GetOwner().MMDHLFaceCopy=capture(tr.Entity) end return result end
  tool.LeftClick=function(self,tr,...) local result=left(self,tr,...);local pose=self:GetOwner().MMDHLFaceCopy
   if mmdhl.IsMMD(tr.Entity) and pose then for _,m in ipairs(mmdhl.GetMorphs(tr.Entity)) do if m.native<0 and pose[m.name]~=nil then mmdhl.SetMorphWeight(tr.Entity,m.mmd,pose[m.name]) end end end return result end
  return
 end
 local build=tool.BuildCPanel
 tool.BuildCPanel=function(panel,ent,...)
  build(panel,ent,...)
  if not mmdhl.IsMMD(ent) then return end
  local rig=mmdhl.GetRig(ent) if not rig then return end
  -- Relabel native controls by their stable convar, not by guessed text.
  panel.MMDNativeControls={}
  local function relabel(root)
   for _,child in ipairs(root:GetChildren()) do
    if child.Label and child.Scratch then local convar=child.Scratch.m_strConVar local index=convar and tonumber(convar:match('^faceposer_flex(%d+)$'))
     if index then for _,m in ipairs(rig.morphs) do if m.native==index then child:SetText(m.displayName or m.name) if mmdhl.names then mmdhl.names.Bind(child,function(p) p:SetText((mmdhl.names.MorphLabel(m,rig.asset))) end) end child:SetTooltip(m.original) child.originalName=m.original.." "..m.name panel.MMDNativeControls[index]=child break end end end
    end
    relabel(child)
   end
  end
  relabel(panel)
  panel:Help(L'faceposer.help')
  local function button(label,fn) local b=vgui.Create('DButton',panel) b:SetText(label) b.DoClick=function() if IsValid(ent) then fn() end end panel:AddItem(b) end
  button(L'faceposer.copy',function() mmdhl.faceClipboard=capture(ent) end)
  button(L'faceposer.paste',function() if mmdhl.faceClipboard then apply(ent,mmdhl.faceClipboard) end end)
  button(L'faceposer.reset',function() apply(ent,{}) end)
  local preset=vgui.Create('DTextEntry',panel) preset:SetPlaceholderText(L'faceposer.preset_name') panel:AddItem(preset)
  local function path() local name=preset:GetValue():gsub('[^%w_-]',''):sub(1,64) if name=='' then return end return 'mmd_hotloader/face_presets/'..name..'.json' end
  button(L'faceposer.save',function() local p=path() if p then file.CreateDir('mmd_hotloader/face_presets') file.Write(p,util.TableToJSON({version=1,morphs=capture(ent)},true)) end end)
  button(L'faceposer.load',function() local p=path() local saved=p and util.JSONToTable(file.Read(p,'DATA') or '')
   if istable(saved) and saved.version==1 and istable(saved.morphs) then apply(ent,saved.morphs) elseif saved~=nil then notification.AddLegacy(L'faceposer.preset_damaged',NOTIFY_ERROR,5) end end)
  local overflow=0 for _,m in ipairs(rig.morphs) do if m.native<0 then overflow=overflow+1 end end
  if overflow==0 then return end
  panel.MMDOverflowCount=overflow panel.MMDOverflowControls={}
  panel:Help(L('faceposer.overflow',{count=overflow}))
  local filter=panel:TextEntry(L'faceposer.search') filter:SetUpdateOnType(true)
  filter.OnValueChange=function(_,query) query=query:lower() for _,m in ipairs(rig.morphs) do local c=panel.MMDOverflowControls[m.mmd] if IsValid(c) then c:SetVisible(query=='' or ((mmdhl.names and mmdhl.names.MorphLabel(m,rig.asset) or m.displayName or m.name)..' '..m.original..' '..m.name):lower():find(query,1,true)~=nil) c:InvalidateParent() end end panel:InvalidateChildren() end
  for _,m in ipairs(rig.morphs) do if m.native<0 then
   local slider=vgui.Create('DNumSlider',panel) slider:SetText(m.displayName or m.name) if mmdhl.names then mmdhl.names.Bind(slider,function(p) p:SetText((mmdhl.names.MorphLabel(m,rig.asset))) end) end slider:SetTooltip(m.original) slider:SetMinMax(0,1) slider:SetDecimals(3) slider:SetValue(mmdhl.GetMorphWeight(ent,m.mmd))
   panel.MMDOverflowControls[m.mmd]=slider
   slider.OnValueChanged=function(_,value) if IsValid(ent) and not panel.MMDUpdating then mmdhl.SetMorphWeight(ent,m.mmd,value) end end panel:AddItem(slider)
  end end
 end
end
hook.Add('InitPostEntity','MMDHL.FaceOverflow',install)
timer.Simple(0,install)
