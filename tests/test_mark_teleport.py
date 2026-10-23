"""mmdhl.MarkTeleport: passes a clamped window to the native module, also from the
MMDVMDNPCPoseCut hook, and stays inert without an MMD instance or native support."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
root=Path(__file__).resolve().parents[1]
lua=LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
SERVER=false CLIENT=true KEY_F8=99
marks={}
mmdhl={native={MarkTeleport=function(instance,seconds) marks[#marks+1]={instance,seconds} return true end}}
function CreateClientConVar(_,value) return {GetInt=function() return tonumber(value) end,SetInt=function() end} end
GetConVar=function(n) return CreateClientConVar(n,'0') end
concommand={Add=function() end}
hook={callbacks={},Add=function(name,id,f) hook.callbacks[name..'/'..id]=f end}
input={IsKeyDown=function() return false end}
IsValid=function(v) return type(v)=='table' end
isfunction=function(v) return type(v)=='function' end
math.Clamp=function(v,lo,hi) return math.min(math.max(v,lo),hi) end
net={Receive=function() end} properties={Add=function() end} notification={AddLegacy=function() end}
NOTIFY_GENERIC=0 NOTIFY_HINT=1
mmdhl.IsMMD=function(ent) return IsValid(ent) and ent.mmd==true end
mmdhl.GetInstance=function(ent) return ent.instance or 0 end
mmdhl.Entities=function() return {} end
''')
attach(lua)
lua.execute((root/'addon/lua/mmdhl/physics_reset.lua').read_text(encoding='utf8'))
lua.execute(r'''
local dancer={mmd=true,instance=7}
local cut=hook.callbacks['MMDVMDNPCPoseCut/MMDHL.MarkTeleport']
assert(mmdhl.MarkTeleport(dancer)==true and marks[1][1]==7 and marks[1][2]==.1,'default window')
assert(mmdhl.MarkTeleport(dancer,5)==true and marks[2][2]==1,'window not clamped to one second')
cut(dancer,.25) assert(#marks==3 and marks[3][1]==7 and marks[3][2]==.25,'pose-cut hook did not mark the dancer')
assert(mmdhl.MarkTeleport({mmd=false,instance=3})==false and #marks==3,'marked an entity that is no MMD character')
assert(mmdhl.MarkTeleport({mmd=true,instance=0})==false and #marks==3,'marked a character without an instance')
cut(nil) assert(#marks==3,'hook with no entity marked something')
mmdhl.native={} assert(mmdhl.MarkTeleport(dancer)==false,'native module without MarkTeleport')
mmdhl.native=nil assert(mmdhl.MarkTeleport(dancer)==false,'no native module')
''')
print('PASS: MarkTeleport default and clamped windows, pose-cut hook, inert without an MMD instance or native support')
