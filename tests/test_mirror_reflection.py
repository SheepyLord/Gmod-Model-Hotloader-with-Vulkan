"""Mirrors show the player's own character (user report: invisible in
gm_construct's mirror). In first person the character is hidden from the player's
own view and its physics wait. The engine draws a mirror's reflection from the
mirrored camera, which EyePos() reports, while render.GetViewSetup() keeps the main
view, as measured in game (func_reflective_glass, _rt_WaterReflection). The
reflection shows the character, and its hair and clothing move while a mirror shows
it. Runs first_person.lua's view rules and physics_lod.lua."""
from pathlib import Path
from lupa import LuaRuntime
from lua_source import definition

root = Path(__file__).resolve().parents[1]
first = (root / 'addon/lua/mmdhl/first_person.lua').read_text(encoding='utf-8')
lod = (root / 'addon/lua/mmdhl/physics_lod.lua').read_text(encoding='utf-8')
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
local V={} V.__index=V
function V.DistToSqr(a,b) return (a.x-b.x)^2+(a.y-b.y)^2+(a.z-b.z)^2 end
function Vector(x,y,z) return setmetatable({x=x or 0,y=y or 0,z=z or 0},V) end
EYE=Vector(-2100,-1982,-336)
me={alive=true} function me:Alive() return self.alive end function me:EyePos() return EYE end function me:ShouldDrawLocalPlayer() return false end
LocalPlayer=function() return me end GetViewEntity=function() return me end
-- The pass being drawn: EyePos() is its camera; render.GetViewSetup() stays on the main view.
PASS=EYE EyePos=function() return PASS end
render={GetViewSetup=function() return {origin=EYE} end}
mainView=EYE -- what the RenderScene hook records for the main camera
AGE=1e9 mmdhl={native={GetDrawAge=function() return AGE end},GetInstance=function() return 7 end}
''')
lua.execute(definition(lua, first, 'function mmdhl.IsLocalFirstPerson(') + definition(lua, first, 'function mmdhl.HiddenFirstPerson(')
            + definition(lua, first, 'function mmdhl.FirstPersonView('))
lua.execute(r'''
-- The player's own view: hidden, and no shadow detached from the hidden body.
assert(mmdhl.FirstPersonView(me,false)==false,'the body shows in first person')
assert(mmdhl.FirstPersonView(me,true)==false,'the hidden body casts a shadow')
-- The mirror's reflection pass: the mirrored camera, 170 units away.
PASS=Vector(-2100,-2152,-336)
assert(mmdhl.FirstPersonView(me,false)==nil,'the mirror does not show the player')
-- Its refraction pass is drawn from the eye again: still hidden.
PASS=EYE assert(mmdhl.FirstPersonView(me,false)==false)
-- A third-person camera or a dead player: always drawn.
PASS=Vector(-2100,-2100,-300) assert(mmdhl.FirstPersonView(me,false)==nil)
PASS=EYE me.alive=false assert(mmdhl.FirstPersonView(me,false)==nil) me.alive=true
-- Physics wait only while nothing drew the hidden body in the last frames.
AGE=1e9 assert(mmdhl.HiddenFirstPerson(me)==true)
AGE=2 assert(mmdhl.HiddenFirstPerson(me)==false,'a body a mirror shows keeps still')
AGE=4 assert(mmdhl.HiddenFirstPerson(me)==true)
mainView=Vector(0,0,0) assert(mmdhl.HiddenFirstPerson(me)==false,'a third-person body waits') mainView=EYE
''')
print('PASS: a mirror reflection (EyePos at the mirrored camera, view setup at the eye) shows the player; own view, refraction and shadows stay hidden')

lua.execute(r'''
CreateClientConVar=function() end mmdhl.PhysicsSettingDefaults={}
FrameNumber=function() return 1 end RealTime=function() return 10 end
ENABLED=0 function GetConVar(name) return {GetInt=function() return 10 end,GetFloat=function() if name=='mmdhl_lod_enabled' then return ENABLED end return 1 end} end
CALLS={} mmdhl.native.SetSecondaryQuality=function(h,divisor,paused) CALLS[#CALLS+1]=paused return true end
''')
lua.execute(lod)
lua.execute(r'''
-- In first person with nothing showing the body, its physics pause.
AGE=1e9 mmdhl.UpdatePhysicsLOD(me,{})
assert(CALLS[#CALLS]==true and me.MMDPhysicsLOD.reason=='first-person player','hidden first-person physics kept running')
-- A mirror drew it: hair and clothing move again.
AGE=0 mmdhl.UpdatePhysicsLOD(me,{})
assert(CALLS[#CALLS]==false and me.MMDPhysicsLOD.suspended==false,'the mirrored body has frozen physics')
''')
print('PASS: first-person physics pause only while no mirror or other view shows the body')
