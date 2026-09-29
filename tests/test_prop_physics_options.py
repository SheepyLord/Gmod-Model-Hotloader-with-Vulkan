"""Static prop physics options: the Physics Gun reaches props that players pass
through (Everything except players / except players and NPCs), a placed prop's
surface material survives copies, attaching and resizing, and deleting props from
the library runs (a call chain there once made it "attempt to call a nil value").
Runs props/collision.lua, props/server.lua, the prop entity's copy and ui.lua's
Delete on simulated realms."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition

root = Path(__file__).resolve().parents[1]
collision = (root / 'addon/lua/mmdhl/props/collision.lua').read_text(encoding='utf-8')

# --- Collision rules: players pass through, except the prop a player aims at while attacking.
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
SERVER=true CLIENT=false IN_ATTACK,IN_ATTACK2,IN_JUMP=1,2048,2
IsValid=function(v) return v~=nil and not v.removed end
HOOKS={} hook={Add=function(event,name,f) HOOKS[event]=HOOKS[event] or {} HOOKS[event][name]=f end}
properties={Add=function() end,List={}} gamemode={Call=function() end}
mmdhl={props={}}
-- The player's line of sight: they aim at AIMED.
AIM=setmetatable({},{__mul=function(v) return v end})
util={IntersectRayWithOBB=function(origin,ray,pos) if pos==AIMED then return pos end end}
function entity(class,fields)
 local e={class=class,nw={},changes=0} for k,v in pairs(fields or {}) do e[k]=v end
 function e:GetClass() return self.class end
 function e:GetNW2String(k,d) local v=self.nw[k] if v==nil then return d end return v end
 function e:GetNW2Bool(k,d) local v=self.nw[k] if v==nil then return d end return v end
 function e:SetNW2Bool(k,v) self.nw[k]=v end
 function e:IsPlayer() return self.class=='player' end
 function e:IsNPC() return self.class=='npc_citizen' end
 function e:IsNextBot() return false end
 function e:KeyDown(k) return self.keys~=nil and self.keys[k]==true end
 function e:GetActiveWeapon() return self.weapon end
 function e:CollisionRulesChanged() self.changes=self.changes+1 end
 function e:EyePos() return self end function e:GetAimVector() return AIM end
 function e:GetPos() return self end function e:GetAngles() end function e:OBBMins() end function e:OBBMaxs() end
 return e
end
''')
attach(lua)
lua.execute(collision)
lua.execute(r'''
local P=mmdhl.props
local should=HOOKS.ShouldCollide['MMDHL.PropCollision']
local physgun,pistol=entity('weapon_physgun'),entity('weapon_pistol')
local prop=entity('mmdhl_prop') prop.nw.MMDHLCollide='noplayers'
local ply=entity('player',{weapon=physgun,keys={}}) local npc=entity('npc_citizen')
assert(should(prop,ply)==false and should(ply,prop)==false,'players pass through a prop that lets players through')
assert(should(prop,npc)==nil,'NPCs meet an Everything-except-players prop')
ply.keys[IN_ATTACK]=true AIMED=prop
assert(should(prop,ply)==nil and should(ply,prop)==nil,'the Physics Gun reaches the prop its player aims at to grab')
AIMED=entity('mmdhl_prop') assert(should(prop,ply)==false,'props the gun does not point at still let the player through')
AIMED=prop
ply.weapon=pistol assert(should(prop,ply)==nil,'bullets reach the prop their player aims at')
ply.keys[IN_ATTACK]=nil ply.keys[IN_ATTACK2]=true assert(should(prop,ply)==nil,'so does a secondary attack (the gravity gun pulls with it)')
ply.keys[IN_ATTACK2]=nil assert(should(prop,ply)==false,'without an attack the aim does not matter')
ply.keys[IN_ATTACK]=true
ply.weapon=physgun ply.nw.MMDHLPhysgunHolding=true
assert(should(prop,ply)==false,'a player carrying something passes through again')
ply.nw.MMDHLPhysgunHolding=false prop.nw.MMDHLCollide='noactors'
assert(should(prop,ply)==nil and should(prop,npc)==false,'the reach works without letting NPCs collide')
ply.keys[IN_ATTACK]=nil assert(should(prop,ply)==false)
for _,mode in ipairs({'none','world','all'}) do prop.nw.MMDHLCollide=mode assert(should(prop,ply)==nil,mode..' leaves the decision to the engine') end
-- Grabbing, dropping and the attack buttons re-evaluate the player's pairs; other keys do not.
HOOKS.OnPhysgunPickup['MMDHL.PropReach'](ply,prop) assert(ply.nw.MMDHLPhysgunHolding==true and ply.changes==1)
HOOKS.PhysgunDrop['MMDHL.PropReach'](ply,prop) assert(ply.nw.MMDHLPhysgunHolding==false and ply.changes==2)
HOOKS.KeyPress['MMDHL.PropReach'](ply,IN_ATTACK) HOOKS.KeyRelease['MMDHL.PropReach'](ply,IN_ATTACK) assert(ply.changes==4)
HOOKS.KeyPress['MMDHL.PropReach'](ply,IN_ATTACK2) assert(ply.changes==5)
HOOKS.KeyPress['MMDHL.PropReach'](ply,IN_JUMP) HOOKS.KeyRelease['MMDHL.PropReach'](ply,IN_JUMP) assert(ply.changes==5,'other keys change nothing')
-- The surface materials offered by the Static Props tab and the tool.
assert(P.SurfaceMaterials[1].id=='default' and P.DefaultSurface=='default' and #P.SurfaceMaterials==20)
assert(P.SurfaceMaterialIds.metal and P.SurfaceMaterialIds.gmod_silent and not P.SurfaceMaterialIds.lava)
assert(mmdhl.Localize(P.SurfaceMaterialIds.metal.label)=='Metal' and mmdhl.Localize(P.SurfaceMaterialIds.gmod_ice.label)=='Frictionless ice')
''')
print('PASS: a player attacking reaches the prop they aim at when players pass through it (Physics Gun, bullets); other props and carried props still pass')

# --- Surface material: placement, copies, attaching and resizing keep it.
server = LuaRuntime(unpack_returned_tuples=True)
server.execute(r'''
SERVER=true CLIENT=false IN_ATTACK=1 angle_zero={}
IsValid=function(v) return v~=nil and not v.removed end isstring=function(v) return type(v)=='string' end istable=function(v) return type(v)=='table' end
isvector=function() return true end isangle=function() return true end
math.Clamp=function(v,low,high) return math.min(math.max(v,low),high) end
local V={} V.__index=V
function Vector(x,y,z) return setmetatable({x=x or 0,y=y or 0,z=z or 0},V) end
V.__mul=function(a,s) return Vector(a.x*s,a.y*s,a.z*s) end
V.__sub=function(a,b) return Vector(a.x-b.x,a.y-b.y,a.z-b.z) end
function V:DistToSqr(o) return (self.x-o.x)^2+(self.y-o.y)^2+(self.z-o.z)^2 end
Angle=function() return {} end
HOOKS={} hook={Add=function(event,name,f) HOOKS[event]=HOOKS[event] or {} HOOKS[event][name]=f end,Run=function() end}
timer={Create=function() end,Simple=function() end} CurTime=function() return 0 end RealTime=CurTime
properties={Add=function() end,List={}} gamemode={Call=function() end}
net={Receive=function() end} CreateConVar=function() end game={SinglePlayer=function() return true end}
util={AddNetworkString=function() end,GetSurfaceIndex=function(name) return ({default=0,metal=1,metal_box=2,wood=3})[name] or -1 end}
file={CreateDir=function() end,Write=function() end}
undo={Create=function() end,AddEntity=function() end,SetPlayer=function() end,Finish=function() end,SetCustomUndoText=function() end}
constraint={AddConstraintTable=function() end,GetTable=function() return {} end,RemoveAll=function() end}
DUPE={} duplicator={RegisterEntityClass=function(class,f) DUPE[class]=f end,RegisterConstraint=function() end,DoGeneric=function() end}
function physics()
 local p={material='default',mass=10,motion=true}
 function p:GetMaterial() return self.material end function p:SetMaterial(m) self.material=m end
 function p:SetMass(m) self.mass=m end function p:GetMass() return self.mass end function p:GetVolume() return 1000 end
 function p:EnableMotion(b) self.motion=b end function p:IsMotionEnabled() return self.motion end function p:Wake() end
 function p:IsGravityEnabled() return true end function p:EnableGravity() end function p:GetVelocity() return Vector() end
 function p:GetAngleVelocity() return Vector() end function p:AddAngleVelocity() end function p:SetVelocity() end
 return p
end
local E={} E.__index=E
function E:GetClass() return self.class end
function E:SetPos(v) self.pos=v end function E:GetPos() return self.pos or Vector() end
function E:SetAngles(a) self.ang=a end function E:GetAngles() return self.ang or Angle() end
for _,name in ipairs({'Spawn','Activate','SetModel','SetSolid','SetMoveType','EnableCustomCollisions','SetCollisionBounds','SetCreator','SetCollisionGroup',
 'FollowBone','SetParent','SetLocalPos','SetLocalAngles','SetNW2Int','SetNW2Vector','SetNW2Angle','DeleteOnRemove','RemoveEffects','SetNoDraw','SetTable'}) do E[name]=function() end end
function E:Remove() self.removed=true end
function E:PhysicsInitMultiConvex() self.phys=physics() return true end
function E:PhysicsDestroy() self.phys=nil end
function E:GetPhysicsObject() return self.phys end
function E:SetAssetID(id) self.asset=id end function E:GetAssetID() return self.asset end
function E:SetPropScale(s) self.scale=s end function E:GetPropScale() return self.scale or 1 end
function E:SetNW2Bool(k,v) self.nw[k]=v end function E:SetNW2String(k,v) self.nw[k]=v end
function E:GetNW2String(k,d) local v=self.nw[k] if v==nil then return d end return v end
function E:GetNW2Bool(k,d) local v=self.nw[k] if v==nil then return d end return v end
function E:GetBoneCount() return 10 end
ents={Create=function(class) return setmetatable({class=class,nw={}},E) end,FindByClass=function() return {} end}
PLAYER={} function PLAYER:IsAdmin() return true end function PLAYER:EyePos() return Vector() end function PLAYER:AddCleanup() end
ID=string.rep('a',64)
mmdhl={props={},approved={props={}},cleanupGeneration=0}
''')
attach(server)
server.execute(collision)
server.execute(r'''
local P=mmdhl.props
P.native={PropHulls=function() return {{}} end,PropHas=function() return true end}
P.Info={[ID]={name='Chair',mins={0,0,0},maxs={1,1,1}}}
P.Load=function(id,cb) cb(P.Info[id]) end P.LoadNow=function(id) return P.Info[id] end
P.ValidID=function(id) return isstring(id) and #id==64 end P.CanonicalScale=function(v) return v end P.CheckScale=function() return true end
P.ScaleOf=function(ent) return ent:GetPropScale() end P.Vector=function(v) return Vector(v[1],v[2],v[3]) end
P.SpawnPose=function() return Vector(),Angle() end P.Name=function(info) return info.name end P.SupportDistance=function() return 0 end
''')
server.execute((root / 'addon/lua/mmdhl/props/server.lua').read_text(encoding='utf-8'))
server.execute('ENT={} AddCSLuaFile=function() end include=function() end')
server.execute((root / 'addon/lua/entities/mmdhl_prop/init.lua').read_text(encoding='utf-8'))
server.execute(r'''
local P=mmdhl.props
-- Collision is not what this checks; keep the prop's physics untouched by it.
P.ApplyCollision=function() end
local function place(settings) local placed P.PlaceAt(PLAYER,ID,{Hit=true,HitPos=Vector()},settings,function(ent) placed=ent end) return placed end
local metal=place({physprop='metal',collide='all'})
assert(metal and metal:GetPhysicsObject():GetMaterial()=='metal' and metal.MMDHLSurface=='metal','Spawn Prop places a metal prop')
assert(place({physprop='lava'}):GetPhysicsObject():GetMaterial()=='default','an unknown surface places the default one')
assert(place({}):GetPhysicsObject():GetMaterial()=='default')
-- A copy (duplicator, saves) carries the material; pasting rebuilds it.
local function copy(ent) local data={} ENT.OnEntityCopyTableFinish(ent,data) return data end
local data=copy(metal) assert(data.MMDHLProp.surface=='metal','the copy records the material')
data.Pos,data.Angle=Vector(),Angle()
local pasted=DUPE.mmdhl_prop(PLAYER,data) assert(pasted:GetPhysicsObject():GetMaterial()=='metal','a pasted copy is metal again')
-- The Physical Properties tool offers more surfaces than the tab: copies keep any the game knows.
pasted:GetPhysicsObject():SetMaterial('metal_box') data=copy(pasted) data.Pos,data.Angle=Vector(),Angle()
assert(DUPE.mmdhl_prop(PLAYER,data):GetPhysicsObject():GetMaterial()=='metal_box')
data.MMDHLProp.surface='no such surface' assert(DUPE.mmdhl_prop(PLAYER,data):GetPhysicsObject():GetMaterial()=='default','unknown surfaces in copies are dropped')
-- Attaching drops the body; detaching builds a new one of the same material.
local target=setmetatable({class='prop_physics',nw={}},getmetatable(metal))
P.Attach(metal,target,-1,Vector(),Angle()) assert(metal:GetPhysicsObject()==nil)
assert(copy(metal).MMDHLProp.surface=='metal','an attached prop copies its material')
assert(P.Detach(metal) and metal:GetPhysicsObject():GetMaterial()=='metal','a detached prop is still metal')
-- Resizing rebuilds the body too.
assert(P.Rebuild(metal,ID,2) and metal:GetPhysicsObject():GetMaterial()=='metal' and metal:GetPropScale()==2,'a resized prop is still metal')
''')
print('PASS: the surface material survives placement, copies, attaching, detaching and resizing')

# --- Library Delete on the Static Props and Character Models tabs.
ui = (root / 'addon/lua/mmdhl/ui.lua').read_text(encoding='utf-8')
client = LuaRuntime(unpack_returned_tuples=True)
client.execute(r'''
CLIENT=true SERVER=false PANEL={}
IsValid=function(v) return v~=nil end
timer={Simple=function() end}
QUERIES={} Derma_Query=function(question,title,yes,apply) QUERIES[#QUERIES+1]=apply end
CALLS={}
mmdhl={library={Delete=function(ids,done) CALLS[#CALLS+1]='character:'..ids[1] done(true,'deleted') end},
 props={library={Delete=function(ids,done) CALLS[#CALLS+1]='static:'..ids[1] done(true,'deleted') end}}}
function panel(mode) return setmetatable({mode=mode,Models={GetSelected=function() return {{asset=string.rep('b',64)}} end},
 Refresh=function() end,SetStatus=function(self,m,err) self.status=m self.statusError=err end},{__index=PANEL}) end
''')
attach(client)
client.execute('local library,L=mmdhl.library,mmdhl.L local DELETED="\\2deleted" '
               'local function releasePreview(owner) owner.released=true end '
               'local function kindOf(mode) return mode=="static" and "static" or "character" end '
               + definition(client, ui, 'function PANEL:DeleteSelected('))
client.execute(r'''
local id=string.rep('b',64)
-- Without the Workshop module, each tab deletes through its own library.
local props=panel('static') props:DeleteSelected() QUERIES[#QUERIES]()
assert(CALLS[1]=='static:'..id and props.released and props.status=='deleted' and not props.statusError,'deleting a prop runs')
local models=panel('library') models:DeleteSelected() QUERIES[#QUERIES]()
assert(CALLS[2]=='character:'..id and models.status=='deleted','deleting a model runs')
-- With it, Workshop items are hidden instead: the tab's kind goes along.
mmdhl.workshop={Provided=function() return false end,Delete=function(kind,ids,done) CALLS[#CALLS+1]='workshop '..kind..':'..ids[1] done(true,'hidden') end}
props=panel('static') props:DeleteSelected() QUERIES[#QUERIES]()
assert(CALLS[3]=='workshop static:'..id and props.status=='hidden','a prop is deleted through the Workshop module')
models=panel('library') models:DeleteSelected() QUERIES[#QUERIES]()
assert(CALLS[4]=='workshop character:'..id and models.status=='hidden')
''')
print('PASS: deleting props and models from the library runs on both tabs, with and without the Workshop module')
