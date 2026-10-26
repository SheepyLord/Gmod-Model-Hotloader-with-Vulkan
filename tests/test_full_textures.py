"""Full-size textures for static props (props/library.lua P.ImportOptions) and the servers'
prop limits (props/server.lua): the option reaches the importer only through a binary module
that keeps such textures (GetCapabilities().propFullTextures, 2.3.1), an older one imports as
before and says why once, and the setting turns itself off there; servers keep 2.3.0's limits
by default and may raise them to what the binary module accepts."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition

ROOT = Path(__file__).resolve().parents[1]
LIBRARY = (ROOT / 'addon/lua/mmdhl/props/library.lua').read_text(encoding='utf-8')
SERVER = (ROOT / 'addon/lua/mmdhl/props/server.lua').read_text(encoding='utf-8')

lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
mmdhl={props={native={}}}
istable=function(v) return type(v)=='table' end isnumber=function(v) return type(v)=='number' end isfunction=function(v) return type(v)=='function' end
math.Clamp=function(v,a,b) return math.max(a,math.min(b,v)) end math.Round=function(v) return math.floor(v+.5) end
CONVARS={} CreateClientConVar=function(name,default) CONVARS[name]=CONVARS[name] or default
 return {GetString=function() return CONVARS[name] end,GetBool=function() return (tonumber(CONVARS[name]) or 0)~=0 end} end
CALLBACKS={} cvars={AddChangeCallback=function(name,fn) CALLBACKS[name]=fn end} RunConsoleCommand=function(name,value) CONVARS[name]=value end
CAPS={} mmdhl.props.native.GetCapabilities=function() return CAPS end
mmdhl.Decode=function(v) return v end
NOTICES={} mmdhl.ShowNativeUpdateNeeded=function(feature,release) NOTICES[#NOTICES+1]={feature,release} end
''')
attach(lua)
# The import options, their ConVars and the change callback: one block of props/library.lua.
end = "end,'MMDHL.PropFullTextures')"
block = LIBRARY[LIBRARY.index('local options={'):LIBRARY.index(end) + len(end)]
lua.execute('local P=mmdhl.props local native=P.native local L=mmdhl.L\n' + block)
lua.execute(r'''
local P,L=mmdhl.props,mmdhl.L
-- Off (the default): 2.3.0's options.
local o=P.ImportOptions() assert(o.full_textures==nil and o.kind=='static' and o.axis=='auto' and o.collision=='hull' and #NOTICES==0)
-- On, with a binary module that cannot: imported as before, the player told once (2.3.1 has them),
-- and turned on again, the setting turns itself off.
CONVARS.mmdhl_prop_import_full_textures='1'
o=P.ImportOptions() assert(o.full_textures==nil and #NOTICES==1 and NOTICES[1][1]==L'library.full_textures.feature' and NOTICES[1][2]=='2.3.1')
P.ImportOptions() assert(#NOTICES==1,'the notice repeats on every import')
CALLBACKS.mmdhl_prop_import_full_textures('mmdhl_prop_import_full_textures','0','1') assert(CONVARS.mmdhl_prop_import_full_textures=='0' and #NOTICES==2)
-- A build with full-size character textures only (fullTextures without propFullTextures) cannot either.
CAPS={fullTextures=16384} assert(P.FullTexturesEdge()==nil)
-- With one that keeps them: the import and Reimport ask for them.
CAPS={fullTextures=16384,propFullTextures=16384} CONVARS.mmdhl_prop_import_full_textures='1'
CALLBACKS.mmdhl_prop_import_full_textures('mmdhl_prop_import_full_textures','0','1') assert(CONVARS.mmdhl_prop_import_full_textures=='1' and #NOTICES==2)
o=P.ImportOptions() assert(o.full_textures==true and o.kind=='static' and P.FullTexturesEdge()==16384)
''')
print('PASS: full-size prop textures reach the importer only through a binary module that keeps them; an older one imports as before, says why once and turns the setting off')

# --- The servers' upload limits: 2.3.0's by default, up to what the binary module accepts.
lua.execute(r'''
SERVER_CONVARS={} FCVAR_ARCHIVE=128
CreateConVar=function(name,default,flags,help,min,max) SERVER_CONVARS[name]={default=default,flags=flags,min=min,max=max} end
''')
lua.execute(definition(lua, SERVER, 'for name,limit in pairs('))
lua.execute(r'''
local expected={mmdhl_prop_max_materials={'128',128},mmdhl_prop_max_package_mb={'256',2048},mmdhl_prop_max_texture_dimension={'4096',16384},mmdhl_prop_max_expanded_mb={'1024',4096}}
for name,e in pairs(expected) do local c=SERVER_CONVARS[name] assert(c and c.default==e[1] and c.flags==FCVAR_ARCHIVE and c.min==1 and c.max==e[2],name) end
''')
print("PASS: servers keep 2.3.0's static prop limits by default and may raise them to what the binary module accepts")
