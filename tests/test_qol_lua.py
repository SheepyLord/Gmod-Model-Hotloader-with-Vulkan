"""Executable policy/appearance regressions; syntax-check GLua with continue normalized."""
from pathlib import Path
import re
from lupa import LuaRuntime
from lua_i18n import attach

root = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)
compile_lua = lua.eval('function(s,n) return load(s,n) end')
for path in (root / 'addon/lua/mmdhl').glob('*.lua'):
    code = re.sub(r'\bcontinue\b', 'break', path.read_text(encoding='utf-8'))
    result = compile_lua(code, str(path))
    if isinstance(result, tuple):
        raise AssertionError(result[1])
lua.execute('mmdhl={PhysicsSettingDefaults={}}; function CreateClientConVar() end')
lua.execute((root/'addon/lua/mmdhl/physics_lod.lua').read_text(encoding='utf-8'))
lua.execute('''
local s={enabled=true,near=300,full=1000,middle=2000,cutoff=4000,hiddenSeconds=2}
local function check(distance,visible,hidden,fast,protected,divisor,paused,previous)
 local d,p=mmdhl.PhysicsLODPolicy({distance=distance,visible=visible,hiddenFor=hidden,fast=fast,protected=protected},s,previous)
 assert(d==divisor and p==paused, 'LOD policy mismatch at '..distance)
end
check(250,false,20,false,false,1,false)
check(999,true,0,false,false,1,false)
check(1500,true,0,false,false,2,false)
check(3000,true,0,false,false,4,false)
check(4001,true,0,false,false,4,true)
check(500,false,1.99,false,false,2,false)
check(500,false,2,false,false,4,true)
check(5000,false,20,true,false,1,false)
check(5000,false,20,false,true,1,false)
check(1030,true,0,false,false,1,false,1)
check(1060,true,0,false,false,2,false,1)
s.enabled=false check(9000,false,30,false,false,1,false)
''')
lua.execute('''
SERVER=true CLIENT=false
isnumber=function(v)return type(v)=='number'end
isstring=function(v)return type(v)=='string'end
istable=function(v)return type(v)=='table'end
IsValid=function(v)return v~=nil end
util={AddNetworkString=function()end,TableToJSON=function(v)return v end}
net={Receive=function()end}
local meta={}
FindMetaTable=function()return meta end
meta.GetBodygroup=function(e,i)return e.groups[i] or 0 end
meta.SetBodygroup=function(e,i,v)e.groups[i]=v end
meta.GetSubMaterial=function(e,i)return e.subs[i] or '' end
meta.SetSubMaterial=function(e,i,v)if i then e.subs[i]=v end end
meta.GetNW2Int=function(e,k,v)return e.nw[k] or v end
meta.SetNW2Int=function(e,k,v)e.nw[k]=v end
meta.GetNW2String=meta.GetNW2Int meta.SetNW2String=meta.SetNW2Int
meta.EntIndex=function(e)return e.index end
for _,name in ipairs({'GetMaterials','GetBodyGroups','GetNumBodyGroups','GetBodygroupCount','GetBodygroupName','FindBodygroupByName','SetBodyGroups'})do meta[name]=function()end end
local rig={materials={}}
for i=1,140 do rig.materials[i]={name='part '..i,path='part_'..i,defaultHidden=i==2 or i==35,authoredAlpha=(i==2 or i==35) and 0 or 1}end
mmdhl.IsMMD=function()return true end mmdhl.GetRig=function()return rig end
mmdhl.native={SetMaterialVisibility=function(_,state) materialState=state return true end}
-- A networked entity: a stable positive index (SyncMaterialState staggers re-syncs by it).
entity=setmetatable({index=37,groups={},subs={},nw={}},{__index=meta})
''')
lua.execute((root/'addon/lua/mmdhl/materials.lua').read_text(encoding='utf-8'))
lua.execute('''
assert(entity:GetBodygroupName(2)=='Show part 2')
assert(entity:GetBodygroupName(1)=='Hide part 1')
for _,slot in ipairs({1,34})do
 assert(not mmdhl.IsMaterialVisible(entity,slot))
 mmdhl.SetMaterialVisible(entity,slot,true) assert(entity:GetBodygroup(slot+1)==1)
 assert(mmdhl.IsMaterialVisible(entity,slot))
end
mmdhl.SetMaterialVisible(entity,0,false) assert(entity:GetBodygroup(1)==1)
mmdhl.SyncMaterialState(entity,1)
assert(not materialState.visible[1] and materialState.visible[2] and materialState.forceOpaque[2] and materialState.forceOpaque[35])
entity:SetSubMaterial(135,'test/material')
local saved=mmdhl.CaptureMaterialState(entity)
mmdhl.ApplyMaterialState(entity,{}) assert(not mmdhl.IsMaterialVisible(entity,1)) assert(mmdhl.IsMaterialVisible(entity,0))
mmdhl.ApplyMaterialState(entity,saved)
assert(entity:GetSubMaterial(135)=='test/material' and mmdhl.IsMaterialVisible(entity,34))
-- Disk saves deserialize numeric-looking JSON keys as numbers.
mmdhl.ApplyMaterialState(entity,{overrides={[135]='disk/material'},groups={[34]=1}})
assert(entity:GetSubMaterial(135)=='disk/material' and mmdhl.IsMaterialVisible(entity,34))
CLIENT=true SERVER=false
entity.MMDHLEditorPreview=true
mmdhl.SetMaterialVisible(entity,34,false) assert(not mmdhl.IsMaterialVisible(entity,34))
entity:SetSubMaterial(135,'client/preview') assert(entity:GetSubMaterial(135)=='client/preview')
CLIENT=false SERVER=true
entity:SetBodygroup(35,1) entity:SetSubMaterial(135)
CLIENT=true SERVER=false
assert(mmdhl.IsMaterialVisible(entity,34),'Server bodygroup update lost behind client preview')
assert(entity:GetSubMaterial(135)=='','Server reset resurrected stale client material')
''')
scene = LuaRuntime(unpack_returned_tuples=True)
scene.execute('''
SERVER=true CLIENT=false
local null=setmetatable({}, {__eq=function()error('NULL physics object compared')end})
local actor={MMDSceneBodies={}}
function actor:GetPhysicsObjectCount()return 0 end
function actor:GetPhysicsObjectNum()return null end
function actor:EntIndex()return 1 end
function actor:IsPlayer()return true end
function actor:Alive()return true end
local body={valid=true}
local corpse={}
function corpse:GetPhysicsObjectCount()return 18 end
function corpse:GetPhysicsObjectNum()return body end
function corpse:EntIndex()return 2 end
function corpse:IsPlayer()return false end
function corpse:IsNPC()return false end
function corpse:IsNextBot()return false end
function actor:GetNW2Int(_,fallback)return fallback end
function corpse:GetNW2Int(_,fallback)return fallback end
IsValid=function(v)return v and v.valid end
CurTime=function()return 1 end
hook={callbacks={},Add=function(event,name,f)hook.callbacks[name]=f end}
util={AddNetworkString=function()end}
net={Receive=function()end}
ents={GetAll=function()return {actor,corpse}end}
-- The scene hook classifies every player, then the entities near the consumers' regions.
player={GetAll=function() local out={} for _,e in ipairs(ents.GetAll()) do if e:IsPlayer() then out[#out+1]=e end end return out end}
game={SinglePlayer=function()return true end}
mmdhl={Entities=function()return {actor,corpse}end,Decode=function(v)return v end,native={CaptureSecondaryScene=function(owners,_,excluded)
 assert(owners[1]==nil and #owners[2]==18 and #excluded==0)
 sceneCaptures=(sceneCaptures or 0)+1 return {}
end}}
''')
# secondary_collision.lua labels its mode list with mmdhl.L and mmdhl.I18n.Lazy at load.
attach(scene)
scene.execute((root/'addon/lua/mmdhl/secondary_collision.lua').read_text(encoding='utf-8'))
scene.execute("for i=1,3 do hook.callbacks['MMDHL.SecondaryScene']() end assert(sceneCaptures==3)")
scene.execute('''
local original=ents.GetAll()
local function stock(index,kind,alive)
 local b={valid=true}
 return {EntIndex=function()return index end,GetPhysicsObjectCount=function()return 1 end,GetPhysicsObjectNum=function()return b end,
 IsPlayer=function()return kind=='player' end,Alive=function()return alive end,IsNPC=function()return kind=='npc' end,IsNextBot=function()return kind=='nextbot' end,Health=function()return alive and 100 or 0 end}
end
local p,n,b,dead=stock(3,'player',true),stock(4,'npc',true),stock(5,'nextbot',true),stock(6,'npc',false)
ents.GetAll=function()return {original[1],original[2],p,n,b,dead} end
mmdhl.native.CaptureSecondaryScene=function(owners,_,excluded)
 assert(#excluded==3,'Single-player stock actors were omitted from exclusions')
 for _,e in ipairs({p,n,b})do assert(owners[e:EntIndex()][1]==e:GetPhysicsObjectNum(0))end
 for _,body in ipairs(excluded)do assert(body~=dead:GetPhysicsObjectNum(0),'Dead actors must not be excluded as living')end
 return {}
end
hook.callbacks['MMDHL.SecondaryScene']()
''')
# Once a consumer registers a region, only entities near it (and every player)
# are classified; the map is not scanned.
scene.execute('''
local function living(index,kind)
 local b={valid=true}
 return {EntIndex=function()return index end,GetPhysicsObjectCount=function()return 1 end,GetPhysicsObjectNum=function()return b end,
 IsPlayer=function()return kind=='player' end,Alive=function()return true end,IsNPC=function()return kind=='npc' end,IsNextBot=function()return false end,Health=function()return 100 end}
end
local near,remote=living(8,'npc'),living(9,'player')
Vector=function(x,y,z)return {x=x,y=y,z=z} end
local boxes={}
ents.GetAll=function() error('A registered region must not scan every entity') end
ents.FindInBox=function(minimum,maximum) boxes[#boxes+1]={minimum,maximum} return {near} end
player.GetAll=function() return {remote} end
mmdhl.native.SceneInterest=function() return {0,0,0,10,20,30} end
mmdhl.native.CaptureSecondaryScene=function(owners,_,excluded)
 assert(#excluded==2 and owners[8][1]==near:GetPhysicsObjectNum(0) and owners[9][1]==remote:GetPhysicsObjectNum(0),'Region or player actors were not excluded')
 return {}
end
hook.callbacks['MMDHL.SecondaryScene']()
assert(#boxes==1 and boxes[1][1].x==-64 and boxes[1][2].z==94,'Regions are searched with a 64-unit margin')
''')
scene.execute("""
CLIENT=true SERVER=false
mmdhl.IsMMD=function()return true end
mmdhl.GetInstance=function(e)return e.handle or 0 end
mmdhl.GetGlobalSettings=function()return {secondaryCollision=0} end
mmdhl.native.SetSecondaryCollisionMode=function(_,mode) appliedMode=mode return true end
local ent={GetNW2Int=function()return 2 end}
assert(mmdhl.GetSecondaryCollisionMode(ent)==0,'Client must not use server collision settings')
assert(mmdhl.SetSecondaryCollisionMode(ent,1))
assert(mmdhl.GetSecondaryCollisionMode(ent)==1)
ent.handle=10
assert(mmdhl.SetSecondaryCollisionMode(ent,2) and appliedMode==2)
assert(not mmdhl.SetSecondaryCollisionMode(ent,3))
mmdhl.native.SetSecondaryCollisionMode=function()return nil,'failure' end
assert(not mmdhl.SetSecondaryCollisionMode(ent,0))
assert(mmdhl.GetSecondaryCollisionMode(ent)==2,'Failed changes must not publish a new mode')
""")
corpse=LuaRuntime(unpack_returned_tuples=True)
corpse.execute('''
hook={Add=function()end,Remove=function()end}
mmdhl={GetRig=function(e)return e.rig or {key='rig',asset='asset'}end,native={RebindSourceEntity=function() rebinds=rebinds+1 return false end}}
rebinds=0
source={MMDHLClientInstance=4,generation=2,alive=true}
function source:IsPlayer()return true end
function source:Alive()return self.alive end
function source:GetNW2Int()return self.generation end
body={generation=1}
function body:GetNW2Int()return self.generation end
function body:EntIndex()return 10 end
''')
corpse.execute((root/'addon/lua/mmdhl/instances.lua').read_text(encoding='utf-8'))
corpse.execute('''
mmdhl.TransferPresentation(source,body)
assert(rebinds==0,'A retained corpse stole a living player instance')
source.alive=false
mmdhl.TransferPresentation(source,body)
assert(rebinds==0,'An older corpse stole a later death instance')
body.generation=2
mmdhl.TransferPresentation(source,body)
assert(rebinds==1,'Same-generation death did not reach the native handoff')
body.MMDHLFormerGeneration=1
mmdhl.TransferPresentation(source,body)
assert(rebinds==1,'Client-only corpse ignored its captured generation')
''')
print('GLua syntax, LOD, materials, actor scene capture and corpse generation isolation passed')
