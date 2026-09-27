-- Client-wide tuning is applied at the next complete physics step. No model
-- reimport, pose reset, or change to the fixed 60 Hz clock is required.
local native=mmdhl.native
local L=mmdhl.L
local workers=mmdhl.Decode(native.GetWorkerCapabilities())
local maximumWorkers=workers and workers.maximum or 32
local definitions={
 {'mmdhl_secondary_iterations','10','Physics accuracy: -1 off, 0 lightweight jiggle, 1-100 full simulation',-1,100},
 {'mmdhl_secondary_gravity','1','Multiplier for character model physics gravity',0,4},
 {'mmdhl_secondary_damping','1','Multiplier for authored body damping',0,4},
 {'mmdhl_secondary_stretch','0','Contact-aware stretch correction',0,1},
 {'mmdhl_vrm_relative_damping','1','VRM spring bones damp motion relative to the character, not the world',0,1},
 {'mmdhl_secondary_stretch_tolerance','1','Allowed stretch',.25,4},
 {'mmdhl_workers','0','Model Hotloader worker count: 0 selects the automatic count',0,maximumWorkers}
}
mmdhl.PhysicsSettingDefaults={mmdhl_first_person_body="2",
 mmdhl_secondary_backend='cpu_mt_v2',mmdhl_secondary_collision='2',mmdhl_spawn_frozen='0',
 mmdhl_secondary_sleep='1',mmdhl_secondary_sleep_linear='0.5',mmdhl_secondary_sleep_angular='0.35',
 mmdhl_secondary_sleep_seconds='1.5',mmdhl_secondary_wake_drift='0.02',mmdhl_secondary_wait_ms='0',
 mmdhl_vertex_cache='1',mmdhl_compact_vertices='1',mmdhl_gpu_skinning='1',mmdhl_update_lod='1',mmdhl_smooth_stepped_poses='1',mmdhl_flashlight_overlap_fix='1',mmdhl_debug_overlay='0',mmdhl_debug_print='0'
}
for _,d in ipairs(definitions) do
 CreateClientConVar(d[1],d[2],true,false,d[3],d[4],d[5])
 mmdhl.PhysicsSettingDefaults[d[1]]=d[2]
end
local lastWorkers
local function apply()
 if not native.SetSecondaryTuning then mmdhl.physicsSettingsError=L'physics.error.restart_required' return end
 local function n(name) return GetConVar('mmdhl_'..name):GetFloat() end
 local ok,err=native.SetSecondaryTuning(math.Round(n('secondary_iterations')),n('secondary_gravity'),n('secondary_damping'),n('secondary_stretch')~=0,n('secondary_stretch_tolerance'))
 if not ok then mmdhl.physicsSettingsError=err and tostring(err) or L'physics.error.apply_failed' return end
 local workers=math.Round(n('workers'))
 if workers~=lastWorkers then
  -- The native entry point drains existing jobs before replacing the pool.
  local result,workerError=native.SetWorkers(workers)
  if not result then mmdhl.physicsSettingsError=workerError and tostring(workerError) or L'physics.error.workers_failed' return end
  lastWorkers=workers
 end
 if native.SetSpringRelativeDamping then native.SetSpringRelativeDamping(n('vrm_relative_damping')~=0) end
 mmdhl.physicsSettingsError=nil
end
local function changed() timer.Create('MMDHL.PhysicsSettings',.2,1,apply) end
for _,d in ipairs(definitions) do cvars.AddChangeCallback(d[1],changed,'MMDHL.PhysicsSettings') end
hook.Add('InitPostEntity','MMDHL.PhysicsSettings',changed)
changed()
function mmdhl.RestorePhysicsDefaults()
 for name,value in pairs(mmdhl.PhysicsSettingDefaults) do local c=GetConVar(name) if c then c:SetString(value) end end
 changed()
end

-- The convar remains an iteration count for console users and saved settings.
-- Only the menu uses compact, approximately logarithmic positions.
local accuracySteps={[-1]=-1,[0]=0,1,2,5,10,20,50,100}
function mmdhl.BindAccuracySlider(p)
 local cvar=GetConVar('mmdhl_secondary_iterations')
 local syncing=false
 local lastValue
 local function sync()
  local value=cvar:GetInt()
  local step=math.Clamp(value,-1,0)
  if value>0 then
   local distance=math.huge
   for i=1,7 do
    local d=math.abs(math.log(value/accuracySteps[i]))
    if d<distance then step=i distance=d end
   end
  end
  syncing=true p:SetValue(step) syncing=false
  lastValue=value
  p:SetText(value<0 and L'physics.accuracy.off' or value==0 and L'physics.accuracy.jiggle' or L('physics.accuracy.iterations',{count=value}))
 end
 p:SetMinMax(-1,7) p:SetDecimals(0) p:SetDefaultValue(4)
 p.OnValueChanged=function(_,value)
  if syncing then return end
  cvar:SetInt(accuracySteps[math.Clamp(math.Round(value),-1,7)])
  sync()
 end
 local think=p.Think
 p.Think=function(self) if think then think(self) end if cvar:GetInt()~=lastValue then sync() end end
 sync()
end

function mmdhl.BuildPhysicsSettings(parent,s,fonts,styleChoices)
 local scroll=parent:Add('DScrollPanel') scroll:Dock(FILL)
 local ink,muted=Color(31,43,58),Color(93,109,127)
 local container=scroll
 local function row(kind,height)
  local p=container:Add(kind) p:Dock(TOP) p:SetTall(s(height)) p:DockMargin(s(8),0,s(16),s(6)) return p
 end
 local function text(value,title)
  local p=row('DLabel',title and 34 or 42) p:SetText(value) p:SetFont(title and fonts.Title or fonts.Small) p:SetTextColor(title and ink or muted) p:SetWrap(true) return p
 end
 local function check(cvar,value,hint)
  local p=row('DCheckBoxLabel',28) p:SetText(value) p:SetConVar(cvar) p:SetTextColor(ink) p.Label:SetFont(fonts.Body) p:SetTooltip(hint or '') return p
 end
 local function slider(cvar,value,min,max,decimals,hint)
  local p=row('DNumSlider',38) p:SetText(value) p:SetMinMax(min,max) p:SetDecimals(decimals) p:SetConVar(cvar) p:SetDark(true)
  p.Label:SetFont(fonts.Body) p.Label:SetWide(s(305)) p:SetTooltip(hint or '') p:SetDefaultValue(tonumber(mmdhl.PhysicsSettingDefaults[cvar])) return p
 end
 local function choice(key,value,choices,hint)
  text(value,false)
  local p=row('DComboBox',34) styleChoices(p,s,fonts.Body) mmdhl.BindGlobalChoice(p,key,choices) p:SetTooltip(hint or '') return p
 end
 -- A console variable with a few named values ({value, label} pairs).
 local function options(cvar,value,choices,hint)
  text(value,false)
  local p=row('DComboBox',34) styleChoices(p,s,fonts.Body) p:SetTooltip(hint or '')
  for _,c in ipairs(choices) do p:AddChoice(c[2],c[1]) end
  p.Think=function(panel) local v=GetConVar(cvar):GetString() if panel.shown~=v then panel.shown=v panel:SetValue(panel:GetOptionTextByData(v) or v) end end
  p.OnSelect=function(panel,_,_,data) panel.shown=tostring(data) RunConsoleCommand(cvar,tostring(data)) end
  return p
 end
 local function action(value,callback)
  local p=row('DButton',36) p:SetText(value) p:SetFont(fonts.Body) p.DoClick=callback return p
 end
 local function advanced(title)
  container=scroll
  local category=row('DCollapsibleCategory',28) category:SetLabel(title) category:SetExpanded(false)
  local content=vgui.Create('DPanel',category) content:SetPaintBackground(false)
  content.PerformLayout=function(p) p:SizeToChildren(false,true) end
  category:SetContents(content) container=content
 end
 text(L'physics.title',true)
 text(L'physics.intro')
 local status=text('',false) status:SetTall(s(32))
 status.Think=function(p)
  if mmdhl.RequestFrameProfile then mmdhl.RequestFrameProfile(2) end
  if (p.nextUpdate or 0)>RealTime() then return end p.nextUpdate=RealTime()+1
  local profile=mmdhl.frameProfile or {}
  local accuracy=GetConVar('mmdhl_secondary_iterations'):GetInt()
  local mode=accuracy<0 and L'physics.status.off' or accuracy==0 and L'physics.status.jiggle' or L('physics.status.full',{count=accuracy})
  p:SetText(mmdhl.physicsSettingsError or (mode..' · '..(profile.workers and L('physics.status.threads',{count=profile.workers}) or L'physics.status.threads_automatic')))
  p:SetTextColor(mmdhl.physicsSettingsError and Color(180,46,46) or muted)
 end
 choice('secondaryBackend',L'physics.backend.label',mmdhl.SecondaryBackends,L'physics.backend.tooltip')
 choice('secondaryCollision',L'physics.collision.label',mmdhl.SecondaryCollisionModes,L'physics.collision.tooltip')
 check('mmdhl_spawn_frozen',L'physics.spawn_frozen.label',L'physics.spawn_frozen.tooltip')
 local accuracy=row('DNumSlider',38) accuracy:SetDark(true)
 accuracy.Label:SetFont(fonts.Body) accuracy.Label:SetWide(s(305))
 accuracy:SetTooltip(L'physics.accuracy.tooltip')
 mmdhl.BindAccuracySlider(accuracy)
 text(L'physics.accuracy.help')
 slider('mmdhl_secondary_gravity',L'physics.gravity.label',0,4,2,L'physics.gravity.tooltip')
 slider('mmdhl_secondary_damping',L'physics.damping.label',0,4,2,L'physics.damping.tooltip')
 check('mmdhl_vrm_relative_damping',L'physics.vrm_damping.label',L'physics.vrm_damping.tooltip')
 text(L'lod.title',true)
 check('mmdhl_lod_enabled',L'lod.enabled.label',L'lod.enabled.tooltip')
 advanced(L'lod.advanced')
 slider('mmdhl_lod_near',L'lod.near.label',0,10000,0,L'lod.near.tooltip')
 slider('mmdhl_lod_full',L'lod.full.label',0,20000,0,L'lod.full.tooltip')
 slider('mmdhl_lod_middle',L'lod.middle.label',0,30000,0,L'lod.middle.tooltip')
 slider('mmdhl_lod_cutoff',L'lod.cutoff.label',1,50000,0,L'lod.cutoff.tooltip')
 slider('mmdhl_lod_hidden_seconds',L'lod.hidden.label',0,30,1,L'lod.hidden.tooltip')
 slider('mmdhl_lod_speed',L'lod.speed.label',1,2000,0,L'lod.speed.tooltip')
 slider('mmdhl_lod_angular_speed',L'lod.angular.label',1,1440,0,L'lod.angular.tooltip')
 container=scroll
 text(L'physics.stretch.title',true)
 check('mmdhl_secondary_stretch',L'physics.stretch.label',L'physics.stretch.tooltip')
 slider('mmdhl_secondary_stretch_tolerance',L'physics.stretch_tolerance.label',.25,4,2,L'physics.stretch_tolerance.tooltip')
 text(L'physics.stretch.help')
 -- Name translation shares model names with Google Translate, so "Restore
 -- defaults" leaves this choice alone.
 text(L'names.setting_title',true)
 check('mmdhl_translate_names',L'names.setting_label',L'names.setting_tooltip')
 local translation=text(L'names.setting_help')
 translation.Think=function(p)
  if (p.nextUpdate or 0)>RealTime() then return end p.nextUpdate=RealTime()+1
  local status=mmdhl.names and mmdhl.names.Status()
  local failing=status and status.enabled and status.failures>0
  local message=failing and L'names.status_unavailable' or L'names.setting_help'
  if p:GetText()~=message then p:SetText(message) p:SetTextColor(failing and Color(170,90,20) or muted) end
 end
 text(L'performance.title',true)
 slider('mmdhl_workers',L'performance.workers.label',0,maximumWorkers,0,L'performance.workers.tooltip')
 check('mmdhl_secondary_sleep',L'performance.sleep.label',L'performance.sleep.tooltip')
 advanced(L'performance.advanced')
 slider('mmdhl_secondary_wait_ms',L'performance.wait.label',0,16,1,L'performance.wait.tooltip')
 check('mmdhl_vertex_cache',L'performance.vertex_cache.label',L'performance.vertex_cache.tooltip')
 check('mmdhl_compact_vertices',L'performance.compact_vertices.label',L'performance.compact_vertices.tooltip')
 check('mmdhl_gpu_skinning',L'performance.gpu_skinning.label',L'performance.gpu_skinning.tooltip')
 check('mmdhl_smooth_stepped_poses',L'performance.smooth_poses.label',L'performance.smooth_poses.tooltip')
 options('mmdhl_update_lod',L'performance.update_lod.label',{{'0',L'performance.update_lod.every_frame'},{'1',L'performance.update_lod.unseen'},{'2',L'performance.update_lod.small'}},L'performance.update_lod.tooltip')
 check('mmdhl_flashlight_overlap_fix',L'performance.flashlight_fix.label',L'performance.flashlight_fix.tooltip')
 slider('mmdhl_secondary_sleep_linear',L'performance.sleep_linear.label',0,10,2,L'performance.sleep_linear.tooltip')
 slider('mmdhl_secondary_sleep_angular',L'performance.sleep_angular.label',0,10,2,L'performance.sleep_angular.tooltip')
 slider('mmdhl_secondary_sleep_seconds',L'performance.sleep_seconds.label',.1,60,1,L'performance.sleep_seconds.tooltip')
 slider('mmdhl_secondary_wake_drift',L'performance.wake_drift.label',0,10,3,L'performance.wake_drift.tooltip')
 container=scroll
 text(L'debug.section',true)
 check('mmdhl_debug_overlay',L'debug.overlay')
 check('mmdhl_debug_print',L'debug.print')
 action(L'debug.write_report',function() RunConsoleCommand('mmdhl_debug_report') end)
 action(L'physics.reset_all',function() RunConsoleCommand('mmdhl_reset_all_physics') end)
 action(L'physics.cleanup_ragdolls',function() RunConsoleCommand('mmdhl_cleanup_client_ragdolls') end)
 text(L'physics.cleanup_help')
 action(L'physics.toggle',function() RunConsoleCommand('mmdhl_toggle_physics') end)
 text(L'physics.toggle_shortcut')
 local toggle=row('DBinder',34) toggle:SetValue(GetConVar('mmdhl_toggle_physics_key'):GetInt())
 toggle.OnChange=function(_,key) RunConsoleCommand('mmdhl_toggle_physics_key',tostring(key)) end
 text(L'physics.reset_shortcut')
 local reset=row('DBinder',34) reset:SetValue(GetConVar('mmdhl_reset_all_key'):GetInt())
 reset.OnChange=function(p,key) if key==KEY_F8 then key=0 p:SetValue(0) end RunConsoleCommand('mmdhl_reset_all_key',tostring(key)) end
 action(L'physics.restore_defaults',function() mmdhl.RestorePhysicsDefaults() end)
 text(L'physics.restore_help')
 return scroll
end
