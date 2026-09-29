-- What a placed static prop collides with, its gravity and what it is made of.
-- Both realms run the same rules (the mode is networked) so player movement
-- prediction agrees with the server. New placements default to the world only.
local P=mmdhl.props
local L=mmdhl.L
local lazy=mmdhl.I18n.Lazy
P.CollisionModes={
 lazy({id='none'},{label=function() return L'props.collision.none' end}),
 lazy({id='world'},{label=function() return L'props.collision.world' end}),
 lazy({id='noactors'},{label=function() return L'props.collision.noactors' end}),
 lazy({id='noplayers'},{label=function() return L'props.collision.noplayers' end}),
 lazy({id='all'},{label=function() return L'props.collision.all' end}),
}
P.CollisionModeIds={} for _,m in ipairs(P.CollisionModes) do P.CollisionModeIds[m.id]=m end
P.DefaultCollision='world'
-- Surface materials (the game's surface properties) a new prop can be made of:
-- its impact sounds, bullet marks, friction and bounce. Placements accept these.
-- i18n-keys: props.surface.default props.surface.wood props.surface.metal props.surface.metal_bouncy props.surface.concrete props.surface.glass props.surface.plastic props.surface.rubber props.surface.flesh props.surface.ice
-- i18n-keys: props.surface.paper props.surface.dirt props.surface.gravel props.surface.foliage props.surface.cardboard props.surface.porcelain props.surface.carpet props.surface.gmod_ice props.surface.gmod_bouncy props.surface.gmod_silent
P.SurfaceMaterials={}
for _,id in ipairs({'default','wood','metal','metal_bouncy','concrete','glass','plastic','rubber','flesh','ice','paper','dirt','gravel','foliage','cardboard','porcelain','carpet','gmod_ice','gmod_bouncy','gmod_silent'}) do
 P.SurfaceMaterials[#P.SurfaceMaterials+1]=lazy({id=id},{label=function() return L('props.surface.'..id) end})
end
P.SurfaceMaterialIds={} for _,m in ipairs(P.SurfaceMaterials) do P.SurfaceMaterialIds[m.id]=m end
P.DefaultSurface='default'
-- Props placed before modes existed report what their collision group does.
function P.CollisionMode(ent)
 local mode=ent:GetNW2String('MMDHLCollide','')
 if P.CollisionModeIds[mode] then return mode end
 return ent:GetCollisionGroup()==COLLISION_GROUP_WORLD and 'world' or 'all'
end
function P.GravityWanted(ent) return ent:GetNW2Bool('MMDHLGravity',true) end
-- A prop that collides with nothing would fall out of the map.
function P.GravityOn(ent) return P.CollisionMode(ent)~='none' and P.GravityWanted(ent) end
local function custom(mode) return mode=='noactors' or mode=='noplayers' end
-- The engine's own traces from a player (the Physics Gun's grab, bullets, the
-- gravity gun) ask this hook too: a prop that players pass through was out of
-- their reach. While a player attacks, the prop they aim at is solid to them;
-- a Physics Gun that holds something passes through again.
local function reaching(ply,prop)
 if not (ply:KeyDown(IN_ATTACK) or ply:KeyDown(IN_ATTACK2)) or ply:GetNW2Bool('MMDHLPhysgunHolding',false) then return false end
 return util.IntersectRayWithOBB(ply:EyePos(),ply:GetAimVector()*32768,prop:GetPos(),prop:GetAngles(),prop:OBBMins(),prop:OBBMaxs())~=nil
end
local function passes(prop,other)
 if prop:GetClass()~='mmdhl_prop' then return false end
 local mode=prop:GetNW2String('MMDHLCollide','')
 if not custom(mode) then return false end
 if other:IsPlayer() then return not reaching(other,prop) end
 return mode=='noactors' and (other:IsNPC() or other:IsNextBot())
end
hook.Add('ShouldCollide','MMDHL.PropCollision',function(a,b)
 if passes(a,b) or passes(b,a) then return false end
end)
if SERVER then
 function P.ApplyCollision(ent)
  if not IsValid(ent) or ent.MMDHLAttach then return end
  local mode=P.CollisionMode(ent)
  ent:SetCustomCollisionCheck(custom(mode))
  ent:SetCollisionGroup((mode=='world' or mode=='none') and COLLISION_GROUP_WORLD or COLLISION_GROUP_NONE)
  local phys=ent:GetPhysicsObject()
  if IsValid(phys) then
   phys:EnableCollisions(mode~='none')
   phys:EnableGravity(P.GravityOn(ent))
   -- Nothing stops a thrown prop that collides with nothing; damp it to rest.
   if not ent.MMDHLDamping then ent.MMDHLDamping={phys:GetDamping()} end
   if mode=='none' then phys:SetDamping(2,4) else phys:SetDamping(ent.MMDHLDamping[1],ent.MMDHLDamping[2]) end
   phys:Wake()
  end
  ent:CollisionRulesChanged()
 end
 function P.SetCollision(ent,mode,gravity)
  if not P.CollisionModeIds[mode] then mode=P.DefaultCollision end
  if gravity==nil then gravity=P.GravityWanted(ent) end
  ent:SetNW2String('MMDHLCollide',mode) ent:SetNW2Bool('MMDHLGravity',gravity==true)
  P.ApplyCollision(ent)
 end
 -- Reaching starts and ends with the attack buttons, the grab and the drop. The
 -- physics engine keeps its pair decisions until told the rules changed.
 local function attack(key) return key==IN_ATTACK or key==IN_ATTACK2 end
 hook.Add('KeyPress','MMDHL.PropReach',function(ply,key) if attack(key) then ply:CollisionRulesChanged() end end)
 hook.Add('KeyRelease','MMDHL.PropReach',function(ply,key) if attack(key) then ply:CollisionRulesChanged() end end)
 hook.Add('OnPhysgunPickup','MMDHL.PropReach',function(ply) ply:SetNW2Bool('MMDHLPhysgunHolding',true) ply:CollisionRulesChanged() end)
 hook.Add('PhysgunDrop','MMDHL.PropReach',function(ply) if IsValid(ply) then ply:SetNW2Bool('MMDHLPhysgunHolding',false) ply:CollisionRulesChanged() end end)
else
 -- The custom check must be enabled in both realms.
 function P.WatchCollision(ent)
  local function apply(mode) if IsValid(ent) then ent:SetCustomCollisionCheck(custom(mode)) ent:CollisionRulesChanged() end end
  ent:SetNW2VarProxy('MMDHLCollide',function(_,_,_,new) timer.Simple(0,function() apply(new) end) end)
  apply(ent:GetNW2String('MMDHLCollide',''))
 end
end
-- Context menu (hold C, right-click the prop).
local function editable(ent,ply,name)
 return IsValid(ent) and ent:GetClass()=='mmdhl_prop' and not ent:GetNW2Bool('MMDHLAttached',false) and gamemode.Call('CanProperty',ply,name,ent)~=false
end
-- Context menu labels are #phrases the game translates (registered in i18n.lua).
properties.Add('mmdhl_prop_collision',{
 MenuLabel='#mmdhl.props.menu.collides_with',Order=1500,MenuIcon='icon16/shape_group.png',
 Filter=function(self,ent,ply) return editable(ent,ply,'mmdhl_prop_collision') end,
 MenuOpen=function(self,option,ent)
  local menu=option:AddSubMenu() local current=P.CollisionMode(ent)
  for _,m in ipairs(P.CollisionModes) do
   local item=menu:AddOption(m.label,function() self:MsgStart() net.WriteEntity(ent) net.WriteString(m.id) self:MsgEnd() end)
   if m.id==current then item:SetIcon('icon16/tick.png') end
  end
 end,
 Action=function() end,
 Receive=function(self,_,ply)
  local ent,mode=net.ReadEntity(),net.ReadString()
  if not properties.CanBeTargeted(ent,ply) or not self:Filter(ent,ply) or not P.Owns(ply,ent) or not P.CollisionModeIds[mode] then return end
  P.SetCollision(ent,mode)
 end
})
properties.Add('mmdhl_prop_gravity',{
 MenuLabel='#mmdhl.props.menu.gravity',Type='toggle',Order=1501,
 Filter=function(self,ent,ply) return editable(ent,ply,'mmdhl_prop_gravity') and P.CollisionMode(ent)~='none' end,
 Checked=function(self,ent) return P.GravityWanted(ent) end,
 Action=function(self,ent) self:MsgStart() net.WriteEntity(ent) self:MsgEnd() end,
 Receive=function(self,_,ply)
  local ent=net.ReadEntity()
  if not properties.CanBeTargeted(ent,ply) or not self:Filter(ent,ply) or not P.Owns(ply,ent) then return end
  P.SetCollision(ent,P.CollisionMode(ent),not P.GravityWanted(ent))
 end
})
-- Sandbox's own Collision and Gravity toggles would bypass these rules on
-- static props, so they give way to the entries above there.
local function yield()
 for _,name in ipairs({'collision_off','collision_on','gravity'}) do
  local entry=properties.List and properties.List[name]
  if entry and not entry.MMDHLYields then
   local filter=entry.Filter
   entry.Filter=function(self,ent,ply) if IsValid(ent) and ent:GetClass()=='mmdhl_prop' then return false end return filter(self,ent,ply) end
   entry.MMDHLYields=true
  end
 end
end
yield() hook.Add('Initialize','MMDHL.PropProperties',yield)
