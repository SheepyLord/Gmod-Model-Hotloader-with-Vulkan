"""Values from clients, saves and peers that are no valid data never reach the engine or leave
an entity half changed: static-prop transforms that are no finite numbers (NaN, infinities,
"1e309"), a detach whose new body cannot be made (the prop stays attached as it was), entity
creation that returns NULL near the networked-edict limit (ragdolls, props, duplication),
damaged face presets and shared-model manifests whose file list holds no records. Runs
props/server.lua, server.lua, carrier.lua's SpawnNative, faceposer.lua and sharing.lua's accept
against simulated realms (Lua 5.4 through lupa; GMod's checks are stubbed as GMod has them)."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition

root = Path(__file__).resolve().parents[1]
read = lambda relative: (root / relative).read_text(encoding='utf-8')
STUBS = r'''
SERVER=true CLIENT=false NOW=0 CurTime=function() return NOW end RealTime=CurTime SysTime=CurTime unpack=unpack or table.unpack
isstring=function(v) return type(v)=='string' end istable=function(v) return type(v)=='table' end
isnumber=function(v) return type(v)=='number' end isbool=function(v) return type(v)=='boolean' end
isfunction=function(v) return type(v)=='function' end
-- GMod's NULL entity: not valid, and every method a no-op.
NULL=setmetatable({},{__index=function() return function() end end})
IsValid=function(v) return v~=nil and v~=NULL and not (type(v)=='table' and v.removed) end
math.Clamp=function(v,low,high) return math.min(math.max(v,low),high) end
math.NormalizeAngle=function(a) return (a+180)%360-180 end
Color=function(r,g,b,a) return {r=r,g=g,b=b,a=a or 255} end
function table.Copy(t) local c={} for k,v in pairs(t) do c[k]=type(v)=='table' and table.Copy(v) or v end return setmetatable(c,getmetatable(t)) end
local V={} V.__index=V
function Vector(x,y,z) if type(x)=='table' then return setmetatable({x=x.x,y=x.y,z=x.z},V) end return setmetatable({x=x or 0,y=y or 0,z=z or 0},V) end
V.__add=function(a,b) return Vector(a.x+b.x,a.y+b.y,a.z+b.z) end
V.__sub=function(a,b) return Vector(a.x-b.x,a.y-b.y,a.z-b.z) end
V.__mul=function(a,s) return Vector(a.x*s,a.y*s,a.z*s) end
V.__eq=function(a,b) return a.x==b.x and a.y==b.y and a.z==b.z end
function V:Length() return math.sqrt(self.x^2+self.y^2+self.z^2) end
function V:DistToSqr(o) return (self.x-o.x)^2+(self.y-o.y)^2+(self.z-o.z)^2 end
function V:Unpack() return self.x,self.y,self.z end
local A={} A.__index=A
function Angle(p,y,r) if type(p)=='table' then return setmetatable({p=p.p,y=p.y,r=p.r},A) end return setmetatable({p=p or 0,y=y or 0,r=r or 0},A) end
A.__eq=function(a,b) return a.p==b.p and a.y==b.y and a.r==b.r end
function A:Unpack() return self.p,self.y,self.r end
isvector=function(v) return getmetatable(v)==V end isangle=function(v) return getmetatable(v)==A end
angle_zero=Angle() vector_origin=Vector()
HOOKS={} hook={Add=function(event,name,f) HOOKS[event]=HOOKS[event] or {} HOOKS[event][name]=f end,Run=function() end}
TIMERS={} timer={Create=function(name,_,_,f) TIMERS[name]=f end,Remove=function(name) TIMERS[name]=nil end,Simple=function(_,f) f() end}
function tick() NOW=NOW+.05 local names={} for name in pairs(TIMERS) do names[#names+1]=name end for _,name in ipairs(names) do if TIMERS[name] then TIMERS[name]() end end end
-- Net messages: reads come from INBOX; each sent message records its name and fields.
INBOX={} SENT={} RECEIVERS={} local writing
local function read() return table.remove(INBOX,1) end
local function write(v) writing.fields[#writing.fields+1]=v end
net={Receive=function(name,f) RECEIVERS[name]=f end,ReadString=read,ReadUInt=read,ReadInt=read,ReadEntity=read,ReadVector=read,ReadAngle=read,ReadFloat=read,ReadBool=read,
 Start=function(name) writing={name=name,fields={}} end,WriteString=write,WriteUInt=write,WriteBool=write,WriteData=write,Send=function() SENT[#SENT+1]=writing end,Broadcast=function() SENT[#SENT+1]=writing end}
function receive(name,p,...) INBOX={...} RECEIVERS[name](0,p) end
function last(name) for i=#SENT,1,-1 do if SENT[i].name==name then return SENT[i].fields end end end
util={AddNetworkString=function() end,TableToJSON=function(t) return t end,JSONToTable=function(s) return s end,GetSurfaceIndex=function() return 0 end}
file={Read=function() end,Write=function() end,CreateDir=function() end}
undo={Create=function() end,AddEntity=function() end,SetPlayer=function() end,Finish=function() end,SetCustomUndoText=function() end}
CONSTRAINTS={} constraint={AddConstraintTable=function(a,link,b) CONSTRAINTS[#CONSTRAINTS+1]=link end,GetTable=function() return {} end,RemoveAll=function() end}
DUPE={} DUPE_CONSTRAINTS={} duplicator={RegisterEntityClass=function(class,f) DUPE[class]=f end,RegisterConstraint=function(name,f) DUPE_CONSTRAINTS[name]=f end,DoGeneric=function() end}
properties={Add=function() end,List={}} gamemode={Call=function() end} cleanup={Register=function() end}
CreateConVar=function() end game={SinglePlayer=function() return true end}
COLLISION_GROUP_NONE,COLLISION_GROUP_DEBRIS,SOLID_OBB,SOLID_VPHYSICS,MOVETYPE_NONE,MOVETYPE_VPHYSICS,EF_FOLLOWBONE=0,1,2,6,0,6,1
function player() local p={} function p:IsAdmin() return true end function p:EyePos() return Vector(0,0,64) end function p:EyeAngles() return Angle() end
 function p:GetEyeTrace() return {Hit=true,HitPos=Vector(100,0,0),HitNormal=Vector(0,0,1)} end function p:GetActiveWeapon() return nil end function p:AddCleanup() end
 function p:SteamID64() return '7656' end return p end
ID=string.rep('a',64)
'''
# A prop entity and its physics object, recording what the engine would hold.
ENTITIES = r'''
function physics()
 local p={material='default',mass=10,motion=true}
 function p:GetMaterial() return self.material end function p:SetMaterial(m) self.material=m end
 function p:SetMass(m) self.mass=m end function p:GetMass() return self.mass end function p:GetVolume() return 1000 end
 function p:EnableMotion(b) self.motion=b end function p:IsMotionEnabled() return self.motion end function p:Wake() end
 function p:IsGravityEnabled() return true end function p:EnableGravity() end function p:GetVelocity() return Vector() end
 function p:GetAngleVelocity() return Vector() end function p:AddAngleVelocity() end function p:SetVelocity() end function p:IsValid() return true end
 return p
end
E={} E.__index=E
function E:GetClass() return self.class end
function E:SetPos(v) self.pos=v end function E:GetPos() return self.pos or Vector() end
function E:SetAngles(a) self.ang=a end function E:GetAngles() return self.ang or Angle() end
for _,name in ipairs({'Spawn','Activate','SetModel','SetSolid','SetMoveType','EnableCustomCollisions','SetCollisionBounds','SetNoDraw','SetTable','RemoveEffects','DeleteOnRemove','SetColor'}) do E[name]=function() end end
function E:SetCreator(p) self.creator=p end function E:GetCreator() return self.creator end
function E:Remove() self.removed=true end
function E:SetParent(p) self.parent=p~=NULL and p or nil end function E:FollowBone(p,bone) self.parent=p self.followed=bone end function E:GetParent() return self.parent or NULL end
function E:SetLocalPos(v) self.localPos=v end function E:SetLocalAngles(a) self.localAng=a end
function E:SetCollisionGroup(g) self.group=g end function E:GetCollisionGroup() return self.group or COLLISION_GROUP_NONE end
function E:PhysicsInitMultiConvex() if FAIL=='init' or (FAIL_FOR==self and FAIL_STAGE=='init') then return false end self.phys=physics() return true end
function E:PhysicsDestroy() self.phys=nil end
function E:GetPhysicsObject() if FAIL=='object' then return nil end return self.phys end
function E:SetAssetID(id) self.asset=id end function E:GetAssetID() return self.asset end
function E:SetPropScale(s) self.scale=s end function E:GetPropScale() return self.scale or 1 end
for _,kind in ipairs({'Bool','String','Int','Vector','Angle'}) do
 E['SetNW2'..kind]=function(self,k,v) self.nw[k]=v end
 E['GetNW2'..kind]=function(self,k,d) local v=self.nw[k] if v==nil then return d end return v end
end
function E:GetBoneCount() return 10 end
function E:IsWorld() return false end function E:IsPlayer() return false end function E:WorldSpaceCenter() return Vector() end function E:GetBoneName(i) return 'bone'..i end
CREATE_FAILS=false
ents={Create=function(class) if CREATE_FAILS then return NULL end return setmetatable({class=class,nw={}},E) end,FindByClass=function() return {} end}
'''

# ---- Static props: transforms, detaching, NULL probes ----
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(STUBS + ENTITIES + 'mmdhl={props={},approved={props={}},cleanupGeneration=0}')
attach(lua)
shared = read('addon/lua/mmdhl/props/shared.lua')
lua.execute('local P=mmdhl.props P.MinScale,P.MaxScale=.01,100')
for header in ('function P.ValidScale(', 'function P.CanonicalScale(', 'function P.Finite(', 'function P.FiniteVector(', 'function P.FiniteAngle(', 'function P.SpawnPose('):
    lua.execute('local P=mmdhl.props ' + definition(lua, shared, header))
lua.execute(read('addon/lua/mmdhl/props/collision.lua'))
lua.execute(r'''
local P=mmdhl.props
HULLS={{}} P.native={PropHulls=function() if FAIL=='hulls' then return nil,'no hulls' end return HULLS end,PropHas=function() return true end}
P.Info={[ID]={name='Chair',mins={0,0,0},maxs={1,1,1}}}
LOADS=0 P.Load=function(id,cb) LOADS=LOADS+1 cb(P.Info[id]) end
P.LoadNow=function(id) if FAIL=='bundle' then return nil end return P.Info[id] end
P.ValidID=function(id) return isstring(id) and #id==64 end P.CheckScale=function(v) return P.ValidScale(v),'size' end
P.ScaleOf=function(ent) return ent:GetPropScale() end P.Vector=function(v) return Vector(v[1],v[2],v[3]) end
P.Name=function(info) return info.name end P.SupportDistance=function() return 0 end P.ApplyCollision=function() end
P.SetCollision=function() end P.CollisionModeIds={} P.DefaultCollision='world'
''')
lua.execute(read('addon/lua/mmdhl/props/server.lua'))
lua.execute(r'''
local P=mmdhl.props local L=mmdhl.L
local invalid=mmdhl.I18n.Token('props.error.invalid_transform')
local p=player() local nan,inf=0/0,1/0
-- Placement: a turn or size that is no finite number is refused before anything loads.
for _,settings in ipairs({{yaw='1e309'},{yaw=nan},{yaw=-inf},{scale=nan},{scale='1e309'}}) do
 local placed,why local loads=LOADS P.PlaceAt(p,ID,p:GetEyeTrace(),settings,function(ent,e) placed,why=ent,e end)
 assert(placed==nil and why~=nil and LOADS==loads,'a non-finite placement ('..tostring(settings.yaw or settings.scale)..') was loaded or placed')
end
local placed P.PlaceAt(p,ID,p:GetEyeTrace(),{yaw=45,color={nan,300,'x'}},function(ent) placed=ent end)
assert(IsValid(placed),'a finite placement was refused')
-- The spawn pose of a turn that overflows is the plain one (the tool's ghost uses it too).
local _,ang=P.SpawnPose(p,P.Info[ID],1,p:GetEyeTrace(),'1e309') assert(ang.y==ang.y and ang.y==-90,'an overflowing turn reached the spawn angle')
-- Copies: a NaN position or mass never reaches the engine.
assert(not P.CreateProp(p,ID,Vector(nan,0,0),Angle(),1,{}) and not P.CreateProp(p,ID,Vector(),Angle(0,inf,0),1,{}),'a prop was created at a non-finite transform')
local heavy=P.CreateProp(p,ID,Vector(),Angle(),1,{mass=nan}) assert(heavy:GetPhysicsObject():GetMass()==P.DefaultMass(heavy:GetPhysicsObject()),'a NaN mass reached the physics object')
-- Attaching: a NaN offset passes a length test; it is refused before loading or changing anything.
local target=setmetatable({class='prop_physics',nw={}},E)
local prop=P.CreateProp(p,ID,Vector(),Angle(),1,{frozen=true})
local function attach(pos,ang,scale) local loads=LOADS receive('mmdhl_prop_attach',p,'attach',target,prop,'',2,pos,ang,scale or 1) return LOADS>loads end
for _,t in ipairs({{Vector(nan,0,0),Angle()},{Vector(),Angle(nan,0,0)},{Vector(inf,0,0),Angle()},{Vector(),Angle(),nan}}) do
 p.MMDHLNextAttach=nil assert(not attach(t[1],t[2],t[3]) and prop.parent==nil and prop.phys~=nil,'a non-finite attachment was loaded or applied')
end
assert(last('mmdhl_notice')[1]==mmdhl.I18n.Token('props.error.size_range'))
p.MMDHLNextAttach=nil local before=#SENT receive('mmdhl_prop_attach',p,'attach',target,prop,'',2,Vector(nan,0,0),Angle(),1)
assert(last('mmdhl_notice')[1]==invalid,'a NaN offset was not refused as an invalid number')
-- A duplicated attachment with a NaN offset attaches at the bone itself.
DUPE_CONSTRAINTS.MMDHLAttach(target,prop,2,Vector(nan,0,0),Angle(0,inf,0))
assert(prop.parent==target and prop.localPos==Vector() and prop.localAng==Angle(),'a copied non-finite offset reached the engine')

-- Detaching: when the new body cannot be made (the bundle, its hulls, the physics object), the prop
-- stays attached exactly as it was: parent, bone, offset, constraint link, collision and NW values.
P.Attach(prop,target,3,Vector(1,2,3),Angle(0,90,0))
local function snapshot(e) return {parent=e.parent,bone=e.followed,pos=e.localPos,ang=e.localAng,link=e.MMDHLAttachLink,group=e.group,phys=e.phys,
 attached=e.nw.MMDHLAttached,nwBone=e.nw.MMDHLAttachBone,nwPos=e.nw.MMDHLAttachPos,nwAng=e.nw.MMDHLAttachAng,collide=e:GetNW2String('MMDHLCollide',''),record=e.MMDHLAttach} end
local function same(a,b) for k,v in pairs(a) do if b[k]~=v then return false,k end end for k in pairs(b) do if a[k]==nil and b[k]~=nil then return false,k end end return true end
local expected=snapshot(prop)
assert(expected.parent==target and expected.bone==3 and expected.attached==true and IsValid(expected.link))
for _,stage in ipairs({'bundle','hulls','init','object'}) do
 -- A bundle that does not load: neither loaded before nor now.
 local info=P.Info[ID] if stage=='bundle' then P.Info[ID]=nil end
 FAIL=stage local ok,why=P.Detach(prop) FAIL=nil P.Info[ID]=info
 local kept,field=same(expected,snapshot(prop))
 assert(not ok and why~=nil and kept and not expected.link.removed,'a detach that failed at '..stage..' changed '..tostring(field))
end
-- Should the prop itself fail where the stand-in did not, it goes back to what it followed.
FAIL_FOR,FAIL_STAGE=prop,'init' local ok=P.Detach(prop) FAIL_FOR=nil
local back=snapshot(prop)
assert(not ok and back.parent==target and back.bone==3 and back.pos==Vector(1,2,3) and back.attached==true and IsValid(back.link) and back.collide==expected.collide,'a prop that failed to detach was not attached again')
assert(P.Detach(prop) and prop.parent==nil and prop.nw.MMDHLAttached==false and prop.phys~=nil,'a valid detach failed')
-- Near the networked-edict limit ents.Create returns NULL: no stand-in, nothing changed.
P.Attach(prop,target,3,Vector(1,2,3),Angle()) expected=snapshot(prop)
CREATE_FAILS=true
local ok2,why2=P.Detach(prop) assert(not ok2 and why2==mmdhl.I18n.Token('props.error.create_failed') and same(expected,snapshot(prop)),'a NULL stand-in changed the attached prop')
local free=setmetatable({class='mmdhl_prop',nw={},asset=ID,scale=1,phys=physics()},E)
local ok3,why3=P.Rebuild(free,ID,2) assert(not ok3 and why3==mmdhl.I18n.Token('props.error.create_failed') and free.scale==1,'a NULL resize probe was used')
local none,why4=P.CreateProp(p,ID,Vector(),Angle(),1,{}) assert(none==nil and why4==mmdhl.I18n.Token('props.error.create_failed'))
CREATE_FAILS=false
''')
print('PASS: static props refuse non-finite placements, copies and attachments; a failed detach keeps the attachment; NULL entities are handled')

# ---- Characters: ents.Create returns NULL (legacy spawn and copies, native ragdolls) ----
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(STUBS + ENTITIES + r'''
mmdhl={} include=function() end
CARRIER=false GetConVar=function() return {GetBool=function() return CARRIER end} end
HANDLES=0 DESTROYED={}
mmdhl.native={RequestAsset=function() return true end,AssetInfo=function() return {name='A'} end,CreateInstance=function() HANDLES=HANDLES+1 return HANDLES end,
 DestroyInstance=function(h) DESTROYED[#DESTROYED+1]=h end,PrepareCarrier=function() return {model='models/mmd/x/a.mdl',gma='data/mmd_hotloader/rigs/x/carrier.gma',key='x'} end}
mmdhl.Decode=function(v,err) return v,err end
mmdhl.FeatureAvailable=function() return true end
mmdhl.WithSpawnDefaults=function(_,o) return table.Copy(o or {}) end
mmdhl.FacingPlayerAngles=function() return Angle() end mmdhl.CanUseAsset=function() return true end mmdhl.ActorOptions=function(o) return o end
mmdhl.MountPackage=function() return true end
''')
attach(lua)
lua.execute(read('addon/lua/mmdhl/server.lua'))
carrier = read('addon/lua/mmdhl/carrier.lua')
lua.execute('local L,native=mmdhl.L,mmdhl.native ' + definition(lua, carrier, ' function mmdhl.SpawnNative('))
lua.execute(r'''
CREATE_FAILS=true
local p=player() local results={}
mmdhl.Spawn(p,ID,{backend='legacy',position={0,0,0}},function(ent,err) results[#results+1]={ent,err} end) tick() tick()
assert(#results==1 and results[1][1]==nil and results[1][2]~=nil,'a legacy spawn without an entity did not end with one failure')
assert(#DESTROYED==1 and DESTROYED[1]==HANDLES,'the native instance made for it was not destroyed')
local copy=DUPE.mmdhl_ragdoll(p,{Pos=Vector(),EntityMods={MMDHL={asset=ID,options={}}}})
assert(copy==nil and #DESTROYED==2 and DESTROYED[2]==HANDLES,'a legacy copy without an entity leaked its instance')
local native={} mmdhl.SpawnNative(p,ID,{position={0,0,0}},function(ent,err) native[#native+1]={ent,err} end)
assert(#native==1 and native[1][1]==nil and native[1][2]==mmdhl.I18n.Token('server.error.native_ragdoll_failed'),'a native ragdoll without an entity did not fail cleanly')
CREATE_FAILS=false
''')
print('PASS: ragdoll spawns and copies that get no entity (ents.Create returns NULL) fail once and release what they made')

# ---- Face presets: a preset without morph values loads nothing; damaged values count as 0 ----
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(STUBS + r'''
SERVER=false CLIENT=false mmdhl={}
WEIGHTS=nil MORPHS={{name='smile',native=-1,mmd=0},{name='blink',native=-1,mmd=1},{name='frown',native=-1,mmd=2},{name='wink',native=-1,mmd=3}}
mmdhl.GetMorphs=function() return MORPHS end mmdhl.SetMorphWeights=function(_,w) WEIGHTS=w end mmdhl.GetMorphWeight=function() return 0 end
weapons={GetStored=function() end}
''')
attach(lua)
lua.execute(read('addon/lua/mmdhl/faceposer.lua'))
lua.execute(r'''
assert(mmdhl.ApplyMorphPose({},nil)==false and WEIGHTS==nil,'a preset without morph values was applied')
assert(mmdhl.ApplyMorphPose({},5)==false and WEIGHTS==nil)
mmdhl.ApplyMorphPose({},{smile='x',blink=0/0,frown=1/0,wink=.5,unknown=1})
assert(WEIGHTS[1]==0 and WEIGHTS[2]==0 and WEIGHTS[3]==0 and WEIGHTS[4]==.5 and #WEIGHTS==4,'damaged morph values were applied')
''')
print('PASS: face presets without a morph table load nothing; values that are no finite number count as 0')

# ---- Shared models: the manifest's file list holds records only ----
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(STUBS + r'''
mmdhl={}
incoming={} STARTED=nil
function key() return 'peer' end function clock() return 0 end function nextFile(p,state) STARTED=state end
''')
attach(lua)
sharing = read('addon/lua/mmdhl/sharing.lua')
lua.execute('local L=mmdhl.L ' + definition(lua, sharing, 'local function accept(').replace('local function accept(', 'function accept(', 1))
lua.execute(r'''
local L=mmdhl.L local A=string.rep('a',64) local T=string.rep('b',64)
local function manifest(files) return {transport=2,version=1,asset=A,name='Model',files=files} end
local good={path='assets/'..A..'/manifest.json',size=10,sha256=T} local model={path='assets/'..A..'/model.bin',size=20,sha256=T}
for _,files in ipairs({{1},{good,'x'},{good,model,[4]=model},{good,model,extra=model},{good,model,{path='textures/'..T..'.png',size=1,sha256=T,derive={}}}}) do
 STARTED=nil local ok,why=accept({},1,manifest(files))
 assert(ok==false and why~=nil and STARTED==nil,'a manifest whose file list holds no records was accepted')
end
local ok=accept({},2,manifest({good,model}))
assert(ok==true and STARTED and STARTED.manifest.files[1]~=good and STARTED.manifest.files[1].path==good.path and #STARTED.manifest.files==2,'a valid manifest was refused, or its records were not copied')
incoming={} STARTED=nil local nameless=manifest({good,model}) nameless.name={} accept({},3,nameless)
assert(STARTED.manifest.name==A:sub(1,12),'a manifest without a text name kept it')
''')
print('PASS: shared-model manifests whose file list holds scalars, holes or other keys are refused without a Lua error; valid ones are used as clean copies')
