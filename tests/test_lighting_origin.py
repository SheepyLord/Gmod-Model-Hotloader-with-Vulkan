"""Client lighting uses open map space and never substitutes player brightness."""
from pathlib import Path
from lupa import LuaRuntime
r=Path(__file__).resolve().parents[1];lua=LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
mmdhl={}CONTENTS_SOLID=1 frame=1 FrameNumber=function()return frame end
bit={band=function(a,b)return a&b end}
local V={}V.__index=V
function Vector(x,y,z)return setmetatable({x=x,y=y or 0,z=z or 0},V)end
function V:LengthSqr()return self.x*self.x+self.y*self.y+self.z*self.z end
function V:DistToSqr(b)return (self.x-b.x)^2+(self.y-b.y)^2+(self.z-b.z)^2 end
util={PointContents=function(p)return p.x>0 and 1 or 0 end}
local bones={[0]=Vector(-4),[1]=Vector(-2)}
center=Vector(-10)entity={WorldSpaceCenter=function()return center end,GetBoneMatrix=function(_,i)if not bones[i]then return end return {GetTranslation=function()return bones[i]end}end}
mmdhl.GetRig=function()return {bodies={{bone=0},{bone=1}}}end
moveBone=function(i,p)bones[i]=p end
''')
s=(r/'addon/lua/mmdhl/native_render.lua').read_text(encoding='utf8');start=s.index('function mmdhl.LightingOrigin');stop=s.index('local remixPreview',start);lua.execute(s[start:stop])
lua.execute(r'''
assert(mmdhl.LightingOrigin(entity).x==-10)
frame=2 center=Vector(2)assert(mmdhl.LightingOrigin(entity).x==-2)
frame=3 moveBone(0,Vector(-.1))assert(mmdhl.LightingOrigin(entity).x==-2,'Fallback switched between valid bones')
frame=4 moveBone(1,Vector(4))assert(mmdhl.LightingOrigin(entity).x==-.1)
frame=5 moveBone(0,Vector(3))assert(mmdhl.LightingOrigin(entity).x==-.1,'Last nearby exposed point lost')
frame=6 center=Vector(-15)assert(mmdhl.LightingOrigin(entity).x==-15 and entity.MMDHLLightBone==nil)
''')
print('PASS: exposed center, solid ledge fallback, stable bone choice, last valid point, return to normal sampling')
