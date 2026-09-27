"""Catalog deletion and Sandbox selection must use their owning state."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach

root=Path(__file__).resolve().parents[1]
lua=LuaRuntime(unpack_returned_tuples=True)
lua.execute("""
mmdhl={actorRegistrations={a={rig={asset='keep',key='aaaaaaaa'}},b={rig={asset='delete',key='bbbbbbbb'}}}}
list={tables={NPC={},PlayerOptionsModel={}},Get=function()return {{class='weapon_crossbow'}}end}
list.GetForEdit=function(key)return list.tables[key]end
player_manager={RemoveValidModel=function(name)removed=name end}
IsValid=function(v)return v~=nil end isstring=function(v)return type(v)=='string'end
""")
attach(lua)
actors=(root/'addon/lua/mmdhl/actors.lua').read_text(encoding='utf8')
lua.execute(actors[actors.index('function mmdhl.UnregisterAsset'):actors.index('function mmdhl.RegisterActor')])
lua.execute("""
local p={GetInfo=function()return choice end}
choice='none' assert(mmdhl.NPCWeapon(p,'citizen')=='none')
choice='' assert(mmdhl.NPCWeapon(p,'combine')=='weapon_ar2')
choice='weapon_crossbow' assert(mmdhl.NPCWeapon(p,'citizen')=='weapon_crossbow')
choice='arbitrary_entity' assert(mmdhl.NPCWeapon(p,'citizen')=='none')
assert(mmdhl.NPCWeapon(p,'combine','weapon_smg1')=='weapon_smg1')
mmdhl.UnregisterAsset('delete')
assert(mmdhl.actorRegistrations.a and not mmdhl.actorRegistrations.b and removed=='mmd_bbbbbbbb')
""")
library=(root/'addon/lua/mmdhl/library.lua').read_text(encoding='utf8')
lua.execute("""
local present=string.rep('a',64) local deleted=string.rep('b',64)
presentId=present deletedId=deleted
mmdhl.sharedAssets={[present]={id=present,info={vertices=0}},[deleted]={id=deleted,info={vertices=0}}}
table.Copy=function(t)local c={} for k,v in pairs(t)do c[k]=v end return c end
file={Read=function(path)
 if path=='mmd_hotloader/assets/'..present..'/manifest.json' then return {vertices=100,name='present'} end
end,Find=function()return {},{present,deleted}end}
util={JSONToTable=function(value)if type(value)=='table'then return value end end}
game={SinglePlayer=function()return single end}
hook={Run=function()end}
single=true
""")
lua.execute(library[:library.index('function library.Update')])
lua.execute("""
mmdhl.library.Refresh()
assert(mmdhl.library.entries[presentId].info.vertices==100)
assert(mmdhl.library.entries[deletedId]==nil,'Single-player catalog resurrected deleted cache')
single=false
mmdhl.library.Refresh()
assert(mmdhl.library.entries[deletedId].source=='Server approved model','Remote approval disappeared with a local cache')
""")
print('PASS: NPC weapon selection, scoped actor unregistration, deleted SP catalog rows, remote library availability')
