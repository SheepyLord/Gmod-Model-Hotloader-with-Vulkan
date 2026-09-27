"""A map cleanup cancels spawns that are still loading: character spawns (model
load, then collision fitting) and prop placements end with one failure, create no
entity or undo entry on the cleaned map, and clear the player's pending flag.
Runs the addon's server.lua and props/server.lua on a simulated server."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach

root = Path(__file__).resolve().parents[1]
common = r'''
SERVER=true CLIENT=false NOW=0 SysTime=function() return NOW end RealTime=SysTime CurTime=SysTime
IsValid=function(v) return v~=nil and not v.removed end isstring=function(v) return type(v)=='string' end istable=function(v) return type(v)=='table' end
math.Clamp=function(v,low,high) return math.min(math.max(v,low),high) end
table.Copy=function(t) local c={} for k,v in pairs(t) do c[k]=v end return c end
local V={} V.__index=V
function Vector(x,y,z) return setmetatable({x=x or 0,y=y or 0,z=z or 0},V) end
V.__add=function(a,b) return Vector(a.x+b.x,a.y+b.y,a.z+b.z) end
V.__mul=function(a,s) return Vector(a.x*s,a.y*s,a.z*s) end
function V:DistToSqr(o) return (self.x-o.x)^2+(self.y-o.y)^2+(self.z-o.z)^2 end
function V:Length() return math.sqrt(self:DistToSqr(Vector())) end
function V:Unpack() return self.x,self.y,self.z end
Angle=function() return {Unpack=function() return 0,0,0 end} end
Entity=function() return nil end
HOOKS={} hook={Add=function(event,name,f) HOOKS[event]=HOOKS[event] or {} HOOKS[event][name]=f end,Run=function() end}
TIMERS={} timer={Create=function(name,_,_,f) TIMERS[name]=f end,Remove=function(name) TIMERS[name]=nil end}
function tick() NOW=NOW+.05 local names={} for name in pairs(TIMERS) do names[#names+1]=name end for _,name in ipairs(names) do if TIMERS[name] then TIMERS[name]() end end end
-- Net messages: reads from INBOX; each sent message records its name and fields.
INBOX={} SENT={} RECEIVERS={} local writing
local function read() return table.remove(INBOX,1) end
local function write(v) writing.fields[#writing.fields+1]=v end
net={Receive=function(name,f) RECEIVERS[name]=f end,ReadString=read,ReadUInt=read,ReadInt=read,ReadEntity=read,ReadVector=read,ReadAngle=read,ReadFloat=read,
 Start=function(name) writing={name=name,fields={}} end,WriteString=write,WriteUInt=write,Send=function() SENT[#SENT+1]=writing end}
function receive(name,p,...) INBOX={...} RECEIVERS[name](0,p) end
function last(name) for i=#SENT,1,-1 do if SENT[i].name==name then return SENT[i].fields end end end
util={AddNetworkString=function() end,TableToJSON=function(t) return t end,JSONToTable=function(s) if s=='' then return nil end return {} end}
file={Read=function() end} duplicator={RegisterEntityClass=function() end,RegisterConstraint=function() end}
undo={Create=function() UNDO=(UNDO or 0)+1 end,AddEntity=function() end,SetPlayer=function() end,Finish=function() end,SetCustomUndoText=function() end}
function player() local p={} function p:EyePos() return Vector(0,0,64) end
 function p:GetEyeTrace() return {Hit=true,HitSky=false,StartSolid=false,HitPos=Vector(100,0,0),HitNormal=Vector(0,0,1)} end
 function p:GetActiveWeapon() return nil end function p:IsAdmin() return true end return p end
ID=string.rep('a',64)
'''

server = LuaRuntime(unpack_returned_tuples=True)
server.execute(common + r'''
mmdhl={} include=function() end cleanup={Register=function() end}
gamemode={Call=function() return true end}
GetConVar=function() return {GetBool=function() return true end} end
LOADED={} FIT_READY=false CREATED=0 CLEARED=0
mmdhl.native={RequestAsset=function() return true end,AssetInfo=function(id) return LOADED[id] and {name='A'} or nil end,
 RequestCarrierFit=function() return FIT_READY end,Clear=function() CLEARED=CLEARED+1 end}
mmdhl.Decode=function(v,err) return v,err end
mmdhl.FeatureAvailable=function() return true end
mmdhl.WithSpawnDefaults=function(_,o) return table.Copy(o or {}) end
mmdhl.FacingPlayerAngles=function() return Angle() end
mmdhl.CanUseAsset=function() return true end
mmdhl.ActorOptions=function(o) return o end
mmdhl.NPCWeapon=function() return 'none' end mmdhl.CleanBodygroups=function(v) return v end mmdhl.ValidSecondaryBackend=function() return 'cpu_mt_v2' end
mmdhl.SpawnNative=function(p,id,options,done) CREATED=CREATED+1 local ent={EntIndex=function() return 42 end} done(ent) return ent end
''')
attach(server)
server.execute((root / 'addon/lua/mmdhl/server.lua').read_text(encoding='utf-8'))
server.execute(r'''
local cancelled=mmdhl.I18n.Token('server.error.map_cleanup')
local function spawn(p) receive('mmdhl_action',p,'spawn',ID,0,'{}') end
local function cleanupMap() HOOKS.PostCleanupMap['MMDHL.Cleanup']() end
local function status() local f=last('mmdhl_spawn_status') return f[2],f[3] end
local p=player()
-- Cleaned while the model loads: one failure, nothing created, the player can spawn again.
spawn(p) assert(p.MMDHLSpawnPending and status()=='loading')
cleanupMap() LOADED[ID]=true tick()
local state,message=status()
assert(state=='error' and message==cancelled,'a spawn loading during a cleanup was not cancelled')
assert(CREATED==0 and not p.MMDHLSpawnPending and next(TIMERS)==nil and CLEARED==1)
-- Cleaned while the collision shape is fitted.
spawn(p) tick() assert(next(TIMERS)~=nil and status()=='loading','the fit did not start')
cleanupMap() FIT_READY=true tick()
state,message=status()
assert(state=='error' and message==cancelled and CREATED==0 and not p.MMDHLSpawnPending and next(TIMERS)==nil,'a spawn fitting during a cleanup was not cancelled')
-- Without a cleanup the character is created once.
spawn(p) tick() tick()
assert(status()=='ready' and CREATED==1 and not p.MMDHLSpawnPending)
-- Spawns without a completion callback (collision-fit copies) report it as a notice.
local sent=#SENT FIT_READY=false
mmdhl.Spawn(p,ID,{role='ragdoll',position={0,0,0},angles={0,0,0},backend='source'}) tick()
cleanupMap() FIT_READY=true tick()
assert(CREATED==1 and last('mmdhl_notice')[1]==cancelled,'a fit copy was placed on the cleaned map')
''')
print('PASS: character spawns loading or fitting during a map cleanup end with one failure and create nothing')

props = LuaRuntime(unpack_returned_tuples=True)
props.execute(common + r'''
mmdhl={cleanupGeneration=0,approved={props={}}} P={native={},SupportDistance=function() return 0 end}
mmdhl.props=P CreateConVar=function() end game={SinglePlayer=function() return false end}
GetConVar=function() return {GetInt=function() return 256 end} end
LOADS={} CREATED=0
P.ValidID=function(id) return #id==64 end P.CanonicalScale=function(v) return v end P.CheckScale=function() return true end
P.SpawnPose=function() return Vector(),Angle() end P.Name=function(info) return info.name end
P.Load=function(id,cb) LOADS[#LOADS+1]=cb end
P.SetCollision=function() end P.CollisionModeIds={} P.DefaultCollision='world'
''')
attach(props)
props.execute((root / 'addon/lua/mmdhl/props/server.lua').read_text(encoding='utf-8'))
props.execute(r'''
P.CreateProp=function() CREATED=CREATED+1 return {GetPhysicsObject=function() return nil end,EntIndex=function() return 7 end} end
mmdhl.approved.props[ID]={name='Chair'}
local cancelled=mmdhl.I18n.Token('server.error.map_cleanup')
local function place(p) receive('mmdhl_prop_action',p,'spawn',ID,0,'{}') end
local function status() local f=last('mmdhl_prop_status') return f[2],f[3] end
local p=player()
-- Cleaned while the prop loads: one failure, nothing placed, the player can place again.
place(p) assert(p.MMDHLPropPending and status()=='loading')
mmdhl.cleanupGeneration=mmdhl.cleanupGeneration+1 LOADS[1]({name='Chair'})
local state,message=status()
assert(state=='error' and message==cancelled and CREATED==0 and UNDO==nil and not p.MMDHLPropPending,'a placement loading during a cleanup was not cancelled')
place(p) LOADS[2]({name='Chair'})
assert(status()=='ready' and CREATED==1 and UNDO==1 and not p.MMDHLPropPending)
-- An attachment to a player (players survive cleanups) is cancelled too.
local target={} function target:IsWorld() return false end function target:IsPlayer() return true end function target:GetBoneCount() return 10 end
function target:WorldSpaceCenter() return Vector() end
receive('mmdhl_prop_attach',p,'attach',target,nil,ID,0,Vector(1,0,0),Angle(),1)
mmdhl.cleanupGeneration=mmdhl.cleanupGeneration+1 LOADS[3]({name='Chair'})
assert(CREATED==1 and UNDO==1 and last('mmdhl_notice')[1]==cancelled,'an attachment was placed on the cleaned map')
''')
print('PASS: prop placements and attachments loading during a map cleanup end with one failure and create nothing')
