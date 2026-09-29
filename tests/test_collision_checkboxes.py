"""Hair and clothing collision checkboxes: the collision level of earlier releases
carries over once to mmdhl_collide_with, each checkbox sets and clears its own bit
and follows console changes, and the choice reaches every character (client
settings.lua with secondary_collision.lua)."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach

root = Path(__file__).resolve().parents[1]
collision = (root / 'addon/lua/mmdhl/secondary_collision.lua').read_text(encoding='utf-8')
settings = (root / 'addon/lua/mmdhl/settings.lua').read_text(encoding='utf-8')


def client(saved):
    """A client whose archived convars hold `saved`; returns the runtime after load."""
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute('''
SERVER=false CLIENT=true
bit={band=function(a,b) return a&b end,bor=function(a,b) return a|b end,bnot=function(a) return ~a end}
CONVARS={} CALLBACKS={} TIMERS={}
local function convar(name,default)
 local c={name=name}
 function c:GetString() return CONVARS[name] end
 function c:GetInt() return math.floor(tonumber(CONVARS[name]) or 0) end
 function c:SetInt(v) self:SetString(tostring(v)) end
 function c:SetString(v) local old=CONVARS[name] CONVARS[name]=v if old~=v then for _,f in pairs(CALLBACKS[name] or {}) do f(name,old,v) end end end
 return c
end
CreateClientConVar=function(name,default) if CONVARS[name]==nil then CONVARS[name]=default end return convar(name) end
GetConVar=function(name) if CONVARS[name]~=nil then return convar(name) end end
cvars={AddChangeCallback=function(name,f,id) CALLBACKS[name]=CALLBACKS[name] or {} CALLBACKS[name][id]=f end}
timer={Create=function(name,delay,reps,f) TIMERS[name]=f end}
hook={Add=function() end}
mmdhl={native={},IsMMD=function() return true end,GetInstance=function(e) return e.handle end,ValidSecondaryBackend=function(v) return v end,SetSecondaryBackend=function() return true end}
''')
    attach(lua)
    for name, value in saved.items():
        lua.globals().CONVARS[name] = value
    lua.execute(collision)
    lua.execute(settings)
    return lua


# An earlier release's level carries over once; the old default takes the new one.
for level, flags in (('0', '2'), ('1', '3'), ('2', '6'), (None, '6')):
    lua = client({} if level is None else {'mmdhl_secondary_collision': level})
    assert lua.eval("CONVARS.mmdhl_collide_with") == flags, (level, lua.eval("CONVARS.mmdhl_collide_with"))
    assert lua.eval("CONVARS.mmdhl_collision_settings_version") == '1'
# Once migrated, a later choice is kept, whatever the old variable says.
lua = client({'mmdhl_secondary_collision': '0', 'mmdhl_collision_settings_version': '1', 'mmdhl_collide_with': '21'})
assert lua.eval("CONVARS.mmdhl_collide_with") == '21'
assert lua.eval("mmdhl.GetGlobalSettings().collisionFlags") == 21
assert lua.eval("mmdhl.GetGlobalSettings().secondaryCollision") == 2, 'Native modules before 2.2 get the nearest level'
print('PASS: the earlier collision level carries over once; the old default becomes character and objects')

lua = client({})
lua.execute('''
local function box()
 local b={checked=false}
 function b:SetChecked(v) self.checked=v end
 function b:GetChecked() return self.checked end
 return b
end
local boxes={}
for _,target in ipairs(mmdhl.CollisionTargets) do boxes[target.key]=mmdhl.BindCollisionCheckbox(box(),target.flag) end
assert(not boxes.world.checked and boxes.character.checked and boxes.objects.checked and not boxes.players.checked and not boxes.npcs.checked,'Character and objects are checked by default')
boxes.world:OnChange(true) assert(CONVARS.mmdhl_collide_with=='7','World adds its bit')
boxes.npcs:OnChange(true) boxes.character:OnChange(false) assert(CONVARS.mmdhl_collide_with=='21','Checkboxes set and clear only their own bits')
boxes.npcs:OnChange(true) assert(CONVARS.mmdhl_collide_with=='21','Checking a checked box changes nothing')
-- A console change reaches the checkboxes.
CONVARS.mmdhl_collide_with='8'
for _,b in pairs(boxes) do b:Think() end
assert(boxes.players.checked and not boxes.world.checked and not boxes.objects.checked and not boxes.npcs.checked and not boxes.character.checked)
-- The choice reaches every character once the settings timer runs.
local a,b={handle=1},{handle=0}
mmdhl.Entities=function() return {a,b} end
mmdhl.native.SetSecondaryCollisionFlags=function(handle,flags) APPLIED=(APPLIED or '')..handle..'='..flags..';' return true end
boxes.world:OnChange(true) TIMERS['MMDHL.GlobalSettings']()
assert(APPLIED=='1=9;','The new flags were not applied to the character with a native world: '..tostring(APPLIED))
assert(a.MMDHLClientCollisionFlags==9 and b.MMDHLClientCollisionFlags==nil)
''')
print('PASS: each checkbox sets its own bit of mmdhl_collide_with, follows console changes and updates the characters')
