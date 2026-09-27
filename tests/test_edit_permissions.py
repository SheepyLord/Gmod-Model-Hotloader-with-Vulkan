"""Edits that other players or withdrawn approvals must not reach, and edits that
must not leave the client:
- the grabber weapon (spawnable in multiplayer) grabs, freezes, resets and removes
  only characters its holder may edit; the owner still can;
- replacing a legacy character and restoring dupes of either backend create
  nothing for a model whose server approval was withdrawn;
- material and expression edits of a client-only corpse (entity index -1) apply
  locally instead of travelling to a server that cannot resolve the entity.
Runs weapon_mmdhl.lua, sharing.lua's CanEdit, server.lua, materials.lua and
carrier.lua against simulated games."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition

root = Path(__file__).resolve().parents[1]
read = lambda relative: (root / relative).read_text(encoding='utf-8')

# ---- the grabber weapon on a multiplayer server with prop protection ----
weapon = LuaRuntime(unpack_returned_tuples=True)
weapon.execute(r'''
SERVER=true CLIENT=false NOW=0 CurTime=function() return NOW end unpack=unpack or table.unpack
IsValid=function(v) return type(v)=='table' and not v.removed end
AddCSLuaFile=function() end IN_ATTACK=1 IN_SPEED=2
local V={} V.__index=V
function Vector(x,y,z) return setmetatable({x=x or 0,y=y or 0,z=z or 0},V) end
V.__add=function(a,b) return Vector(a.x+b.x,a.y+b.y,a.z+b.z) end
V.__mul=function(a,s) return Vector(a.x*s,a.y*s,a.z*s) end
function V:Distance(o) return math.sqrt((self.x-o.x)^2+(self.y-o.y)^2+(self.z-o.z)^2) end
concommand={Add=function() end} game={SinglePlayer=function() return false end} hook={Run=function() end}
-- Prop protection: CanProperty allows only the entity's creator.
gamemode={Call=function(name,p,property,ent) if name=='CanProperty' then return ent:GetCreator()==p end end}
CALLS={}
local function call(name) return function() CALLS[#CALLS+1]=name return true end end
mmdhl={L=function(k) return k end,Localize=function(v) return v end,FeatureAvailable=function() return true end,Decode=function(v) return v end,ChatPrint=function() end}
mmdhl.IsMMD=function(e) return e.class=='mmdhl_ragdoll' end
mmdhl.native={Raycast=function() return {instance=5,body=0,position={0,0,0}} end,SetFrozen=call('SetFrozen'),BeginGrab=call('BeginGrab'),
 UpdateGrab=call('UpdateGrab'),EndGrab=call('EndGrab'),ResetPhysics=call('ResetPhysics')}
function character(owner)
 local e={class='mmdhl_ragdoll',frozen=false,owner=owner}
 function e:GetFrozen() return self.frozen end function e:SetFrozen(v) self.frozen=v end
 function e:Remove() self.removed=true end function e:GetCreator() return self.owner end function e:IsPlayer() return false end
 return e
end
function player()
 local p={keys={}}
 function p:EyePos() return Vector(0,0,64) end function p:GetAimVector() return Vector(1,0,0) end
 function p:KeyDown(k) return self.keys[k]==true end function p:IsAdmin() return false end
 return p
end
SWEP={Primary={},Secondary={}}
''')
weapon.execute(read('addon/lua/weapons/weapon_mmdhl.lua'))
weapon.execute(definition(weapon, read('addon/lua/mmdhl/sharing.lua'), ' function mmdhl.CanEdit('))
weapon.execute(r'''
local function held(p) local w=setmetatable({},{__index=SWEP}) function w:GetOwner() return p end function w:SetNextPrimaryFire() end function w:SetNextSecondaryFire() end return w end
local owner,other=player(),player()
local ent=character(owner) mmdhl.EntityForInstance=function() return ent end
local function reload(w,p,remove) NOW=NOW+1 p.keys[IN_SPEED]=remove or nil w:Reload() end
-- Another player's legacy character: every action is refused before it changes anything.
local w=held(other)
w:PrimaryAttack() w:SecondaryAttack() reload(w,other,false) reload(w,other,true)
assert(#CALLS==0 and not w.Grabbing and not ent.frozen and not ent.removed,'the grabber changed another player\'s character: '..table.concat(CALLS,','))
-- The owner keeps every action.
local o=held(owner)
o:PrimaryAttack() assert(o.Grabbing and CALLS[#CALLS]=='BeginGrab','the owner could not grab')
o:Release() o:SecondaryAttack() assert(ent.frozen,'the owner could not freeze')
reload(o,owner,false) assert(CALLS[#CALLS]=='ResetPhysics','the owner could not reset')
reload(o,owner,true) assert(ent.removed,'the owner could not remove')
''')
print('PASS: the grabber refuses grab, freeze, reset and remove on another player\'s character and keeps them for its owner')

# ---- server.lua: legacy replacement and dupes of a withdrawn model ----
server = LuaRuntime(unpack_returned_tuples=True)
server.execute(r'''
SERVER=true CLIENT=false NOW=0 SysTime=function() return NOW end unpack=unpack or table.unpack
IsValid=function(v) return type(v)=='table' and not v.removed end isstring=function(v) return type(v)=='string' end
table.Copy=function(t) local c={} for k,v in pairs(t or {}) do c[k]=v end return c end
local V={} V.__index=V
function Vector(x,y,z) return setmetatable({x=x or 0,y=y or 0,z=z or 0},V) end
V.__add=function(a,b) return Vector(a.x+b.x,a.y+b.y,a.z+b.z) end V.__sub=function(a,b) return Vector(a.x-b.x,a.y-b.y,a.z-b.z) end
function V:Unpack() return self.x,self.y,self.z end
TIMERS={} timer={Create=function(name,_,_,f) TIMERS[name]=f end,Remove=function(name) TIMERS[name]=nil end}
function tick() NOW=NOW+.05 for name,f in pairs(TIMERS) do f() end end
hook={Add=function() end,Run=function() end} include=function() end cleanup={Register=function() end}
INBOX={} SENT={} RECEIVERS={} local writing
local function read() return table.remove(INBOX,1) end
net={Receive=function(name,f) RECEIVERS[name]=f end,ReadString=read,ReadUInt=read,Start=function(name) writing={name=name,fields={}} end,
 WriteString=function(v) writing.fields[#writing.fields+1]=v end,Send=function() SENT[#SENT+1]=writing end}
function last(name) for i=#SENT,1,-1 do if SENT[i].name==name then return SENT[i].fields end end end
util={AddNetworkString=function() end,TableToJSON=function(t) return t end,JSONToTable=function() return {} end}
file={Read=function() end}
DUPES={} duplicator={RegisterEntityClass=function(class,f) DUPES[class]=f end}
NATIVE_CARRIER=false GetConVar=function() return {GetBool=function() return NATIVE_CARRIER end} end
APPROVED={} CREATED=0 NATIVE_SPAWNS=0
mmdhl={cleanupGeneration=0}
mmdhl.native={RequestAsset=function() return true end,AssetInfo=function() return {name='A'} end,GetBounds=function() return {center={0,0,0}} end,
 CreateInstance=function() CREATED=CREATED+1 return 100+CREATED end,DestroyInstance=function() end,SetState=function() end}
mmdhl.Decode=function(v,err) return v,err end
mmdhl.CanUseAsset=function(p,id) return APPROVED[id]==true end
mmdhl.CanEdit=function() return true end mmdhl.IsMMD=function() return true end
mmdhl.SpawnNative=function() NATIVE_SPAWNS=NATIVE_SPAWNS+1 return {SetFlexScale=function() end} end
ents={Create=function(class)
 local e={class=class}
 for _,name in ipairs({'SetAsset','SetFrozen','SetPos','Spawn','SetCreator','SetNW2Float'}) do e[name]=function() end end
 function e:SetInstance(h) self.instance=h end return e
end}
''')
attach(server)
server.execute(read('addon/lua/mmdhl/server.lua'))
server.execute(r'''
local p={AddCleanup=function() end}
local OLD,NEW=string.rep('a',64),string.rep('b',64) APPROVED[OLD]=true
local ent={class='mmdhl_ragdoll',instance=7,MMDOptions={}}
function ent:GetClass() return self.class end function ent:GetInstance() return self.instance end function ent:SetInstance(h) self.instance=h end
function ent:SetAsset(id) self.asset=id end function ent:GetFrozen() return false end function ent:IsPlayer() return false end
Entity=function() return ent end
local function replace(id) INBOX={'replace',id,1,''} RECEIVERS.mmdhl_action(0,p) for _=1,3 do tick() end end
-- Replacing a legacy character with a model whose approval was withdrawn.
replace(NEW)
assert(CREATED==0 and ent.instance==7,'a withdrawn model replaced a legacy character')
assert(last('mmdhl_notice')[1]==mmdhl.I18n.Token('server.error.not_approved'))
APPROVED[NEW]=true replace(NEW)
assert(CREATED==1 and ent.instance==101 and ent.asset==NEW,'an approved replacement was refused')
-- Dupes and saves of either backend.
local function paste() return DUPES.mmdhl_ragdoll(p,{EntityMods={MMDHL={asset=OLD,options={position={0,0,0}}}},Pos=Vector(1,2,3)}) end
APPROVED[OLD]=nil
for _,native in ipairs({true,false}) do NATIVE_CARRIER=native
 assert(paste()==nil and NATIVE_SPAWNS==0 and CREATED==1,'a dupe of a withdrawn model was created (native carrier '..tostring(native)..')')
 assert(last('mmdhl_notice')[1]==mmdhl.I18n.Token('persistence.error.model_not_approved'))
end
APPROVED[OLD]=true
NATIVE_CARRIER=true assert(paste() and NATIVE_SPAWNS==1,'an approved native dupe was refused')
NATIVE_CARRIER=false assert(paste() and CREATED==2,'an approved legacy dupe was refused')
''')
print('PASS: legacy replacement and dupes of both backends create nothing for a withdrawn model; approved ones still work')

# ---- the client: a client-only corpse and a networked character ----
client = LuaRuntime(unpack_returned_tuples=True)
client.execute(r'''
SERVER=false CLIENT=true
isnumber=function(v) return type(v)=='number' end isstring=function(v) return type(v)=='string' end istable=function(v) return type(v)=='table' end
IsValid=function(v) return type(v)=='table' and not v.removed end
math.Clamp=function(v,low,high) return math.min(math.max(v,low),high) end
FrameNumber=function() return 1 end
util={AddNetworkString=function() end,TableToJSON=function(v) return v end}
SENT={} net={Receive=function() end,Start=function(name) SENT[#SENT+1]=name end,SendToServer=function() end}
for _,name in ipairs({'WriteEntity','WriteUInt','WriteBool','WriteFloat'}) do net[name]=function() end end
timer={Simple=function() end} hook={Add=function() end}
local meta={} FindMetaTable=function() return meta end
meta.GetBodygroup=function(e,i) return e.groups[i] or 0 end meta.SetBodygroup=function(e,i,v) e.groups[i]=v end
meta.GetSubMaterial=function(e,i) return e.subs[i] or '' end meta.SetSubMaterial=function(e,i,v) if i then e.subs[i]=v end end
meta.GetNW2Int=function(e,k,v) return e.nw[k] or v end meta.SetNW2Int=function(e,k,v) e.nw[k]=v end
meta.GetNW2String=meta.GetNW2Int meta.SetNW2String=meta.SetNW2Int meta.GetNW2Float=meta.GetNW2Int
meta.GetFlexWeight=function(e,i) return e.flex[i] or 0 end meta.SetFlexWeight=function(e,i,v) e.flex[i]=v end
meta.EntIndex=function(e) return e.index end
for _,name in ipairs({'GetMaterials','GetBodyGroups','GetNumBodyGroups','GetBodygroupCount','GetBodygroupName','FindBodygroupByName','SetBodyGroups'}) do meta[name]=function() end end
mmdhl={rigs={}}
local rig={materials={},morphs={{native=-1,mmd=0},{native=0,mmd=1}}}
for i=1,40 do rig.materials[i]={name='part '..i,path='part_'..i} end
mmdhl.IsMMD=function() return true end mmdhl.GetRig=function() return rig end
mmdhl.native={SetMaterialVisibility=function(_,state) STATE=state return true end}
function entity(index) return setmetatable({index=index,groups={},subs={},nw={},flex={}},{__index=meta}) end
''')
client.execute(read('addon/lua/mmdhl/materials.lua'))
carrier = read('addon/lua/mmdhl/carrier.lua')
for header in ['function mmdhl.IsClientOnly(', 'function mmdhl.GetMorphs(', 'function mmdhl.GetMorphWeight(', 'function mmdhl.SetMorphWeight(', 'function mmdhl.SetMorphWeights(']:
    client.execute(definition(client, carrier, header))
client.execute(r'''
local corpse=entity(-1)
-- A part below and one above Source's 32 bodygroups, then both expressions.
for _,slot in ipairs({0,34}) do
 assert(mmdhl.SetMaterialVisible(corpse,slot,false) and not mmdhl.IsMaterialVisible(corpse,slot),'hiding a corpse part changed nothing')
end
mmdhl.SyncMaterialState(corpse,1) assert(STATE.visible[1]==false and STATE.visible[35]==false and STATE.visible[2]==true,'the hidden parts did not reach the renderer')
assert(mmdhl.SetMorphWeight(corpse,0,.7) and mmdhl.GetMorphWeight(corpse,0)==.7,'a corpse expression without a flex controller changed nothing')
assert(mmdhl.SetMorphWeight(corpse,1,.4) and corpse.flex[0]==.4 and mmdhl.GetMorphWeight(corpse,1)==.4,'a corpse flex expression changed nothing')
assert(mmdhl.SetMorphWeights(corpse,{.25,.5}) and corpse.MMDHLLocalMorphs[1]==.25 and corpse.flex[0]==.5,'resetting corpse expressions changed nothing')
assert(#SENT==0,'a client-only corpse edit was sent to the server: '..table.concat(SENT,','))
-- A networked character still asks the server.
local networked=entity(37)
mmdhl.SetMaterialVisible(networked,0,false) mmdhl.SetMorphWeight(networked,0,.5) mmdhl.SetMorphWeights(networked,{0,0})
assert(table.concat(SENT,',')=='mmdhl_material_visibility,mmdhl_native_morph,mmdhl_native_morphs' and networked.groups[1]==nil)
''')
print('PASS: part and expression edits of a client-only corpse apply locally; networked characters still go through the server')
