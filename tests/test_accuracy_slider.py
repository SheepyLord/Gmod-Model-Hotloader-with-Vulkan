"""Menu positions map to iteration counts without rewriting saved custom values."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach

root = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute('''
mmdhl={} value=10 writes=0
math.Clamp=function(v,a,b)return math.max(a,math.min(b,v))end
math.Round=function(v)return math.floor(v+.5)end
GetConVar=function()return {
 GetInt=function()return value end,
 SetInt=function(_,v)value=v writes=writes+1 end
}end
p={SetMinMax=function(self,a,b)self.min=a self.max=b end,
 SetDecimals=function()end,SetDefaultValue=function(self,v)self.default=v end,
 SetValue=function(self,v)self.position=v if self.OnValueChanged then self:OnValueChanged(v)end end,
 SetText=function(self,v)self.text=v end,
 Think=function(self)self.originalThink=true end}
''')
attach(lua)
lua.execute('L=mmdhl.L')
source = (root/'addon/lua/mmdhl/physics_settings.lua').read_text(encoding='utf8')
lua.execute(source[source.index('local accuracySteps='):source.index('function mmdhl.BuildPhysicsSettings')])
lua.execute('''
mmdhl.BindAccuracySlider(p)
assert(p.min==-1 and p.max==7 and p.default==4 and p.position==4)
assert(value==10 and writes==0)
local expected={[-1]=-1,[0]=0,1,2,5,10,20,50,100}
for step=-1,7 do p:OnValueChanged(step) assert(value==expected[step] and p.position==step) end
p:OnValueChanged(3.6) assert(value==10 and p.position==4)
p:OnValueChanged(-100) assert(value==-1)
p:OnValueChanged(100) assert(value==100)
value=30 local before=writes p:Think()
assert(value==30 and writes==before and p.originalThink and p.text:find('30 iterations',1,true))
value=10 p:Think() assert(p.position==4 and writes==before)
value=0 p:Think() assert(p.position==0 and writes==before)
value=-1 p:Think() assert(p.position==-1 and writes==before)
''')
print('PASS: accuracy levels, rounding, Off/Jiggle, defaults and external/custom values')
