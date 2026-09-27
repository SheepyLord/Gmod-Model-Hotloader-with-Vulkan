"""Camera classification, shortcut edge triggering, F8 retirement and toggle restore."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
root=Path(__file__).resolve().parents[1]
lua=LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
SERVER=false CLIENT=true KEY_F8=99
mmdhl={native={}} local values={mmdhl_reset_all_key=99,mmdhl_secondary_iterations=10}
function CreateClientConVar(name,value)
 if values[name]==nil then values[name]=tonumber(value)end
 return {GetInt=function()return values[name]end,SetInt=function(_,v)values[name]=v end}
end
GetConVar=function(n)return CreateClientConVar(n,'0')end
concommand={commands={},Add=function(n,f)concommand.commands[n]=f end}
hook={callbacks={},Add=function(n,k,f)hook.callbacks[k]=f end,Run=function()end}
local pressed={}input={IsKeyDown=function(k)return pressed[k] or false end}
press=function(k,v)pressed[k]=v end
focus=true menu=false cursor=false keyboard=nil
system={HasFocus=function()return focus end}gui={IsGameUIVisible=function()return menu end}
vgui={CursorVisible=function()return cursor end,GetKeyboardFocus=function()return keyboard end}
IsValid=function(v)return type(v)=='table'end
net={Receive=function()end}properties={Add=function()end}notification={AddLegacy=function()end}
NOTIFY_GENERIC=0 NOTIFY_HINT=1
mmdhl.Entities=function()return {}end
''')
attach(lua)
lua.execute((root/'addon/lua/mmdhl/physics_reset.lua').read_text(encoding='utf8'))
lua.execute(r'''
assert(GetConVar('mmdhl_reset_all_key'):GetInt()==0,'F8 was not retired')
GetConVar('mmdhl_toggle_physics_key'):SetInt(50)
local tick=hook.callbacks['MMDHL.ResetShortcut']
press(50,true)tick()assert(GetConVar('mmdhl_secondary_iterations'):GetInt()==-1)
tick()assert(GetConVar('mmdhl_secondary_iterations'):GetInt()==-1,'Held key toggled repeatedly')
press(50,false)tick()press(50,true)tick()assert(GetConVar('mmdhl_secondary_iterations'):GetInt()==10)
GetConVar('mmdhl_secondary_iterations'):SetInt(0)
mmdhl.TogglePhysics()assert(GetConVar('mmdhl_secondary_iterations'):GetInt()==-1)
mmdhl.TogglePhysics()assert(GetConVar('mmdhl_secondary_iterations'):GetInt()==0,'Jiggle selection lost')
press(50,false)tick()menu=true press(50,true)tick()assert(GetConVar('mmdhl_secondary_iterations'):GetInt()==0)
menu=false press(50,false)tick()focus=false press(50,true)tick()assert(GetConVar('mmdhl_secondary_iterations'):GetInt()==0)
''')
lua.execute(r'''
function Vector(x)if type(x)=='table'then return x end return {DistToSqr=function(_,p)return (x-p.x)^2 end,x=x}end
p={Alive=function()return true end,EyePos=function()return Vector(100)end,ShouldDrawLocalPlayer=function()return drawLocal end}
LocalPlayer=function()return p end viewEntity=p GetViewEntity=function()return viewEntity end
eye=Vector(100)EyePos=function()return eye end render={GetViewSetup=function()return {origin=eye}end}
''')
s=(root/'addon/lua/mmdhl/first_person.lua').read_text(encoding='utf8')
lua.execute(s[:s.index('function mmdhl.GetArmsParts')])
lua.execute(r'''
drawLocal=true assert(mmdhl.FirstPersonView(p,false)==false,'External body addon bypassed first-person hide')
hook.callbacks['MMDHL.MainCamera'](eye)
assert(mmdhl.FirstPersonView(p,true)==false,'Hidden first-person body still casts a shadow')
eye=Vector(400)assert(mmdhl.FirstPersonView(p,false)==nil,'Third-person body was hidden')
hook.callbacks['MMDHL.MainCamera'](eye)
assert(mmdhl.FirstPersonView(p,true)==nil,'Third-person shadow was hidden')
eye=Vector(100)viewEntity={}assert(not mmdhl.IsLocalFirstPerson(p,eye),'Remote camera incorrectly suspended player')
''')
print('PASS: first/third-person and shadow classification, F8 retirement, key debounce, menu/focus guard, full/jiggle toggle restore')
