"""Bodygroup presets with numeric names ("1", "2024") are table keys that
util.JSONToTable reads back as numbers. Mixed with other names they broke the
sorted preset lists (the editor's Save: "attempt to compare string with
number"), and the default preset was not found for spawns. Runs bodygroups.lua's
preset functions and the library panel's RefreshBodygroupChoices (ui.lua)."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition

root = Path(__file__).resolve().parents[1]
bodygroups = (root / 'addon/lua/mmdhl/bodygroups.lua').read_text(encoding='utf-8')
ui = (root / 'addon/lua/mmdhl/ui.lua').read_text(encoding='utf-8')
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
mmdhl={library={entries={}}}
table.GetKeys=function(t) local out={} for k in pairs(t) do out[#out+1]=k end return out end
IsValid=function(v) return v~=nil end
-- What a library entry holds after util.JSONToTable: numeric names became numbers.
ID=string.rep('a',64)
mmdhl.library.entries[ID]={id=ID,settings={bodygroups={presets={[1]={visible={true,false}},[2024]={visible={false,true}},Dress={visible={true,true}}},default='1'}}}
''')
attach(lua)
lua.execute('L=mmdhl.L')
lua.execute('local library=mmdhl.library local function store(id) local entry=library.entries[id] return entry and entry.settings.bodygroups or {} end '
            + definition(lua, bodygroups, 'function mmdhl.BodygroupPresets(') + definition(lua, bodygroups, 'function mmdhl.BodygroupDefault(')
            + definition(lua, bodygroups, 'function mmdhl.BodygroupSpawnState('))
lua.execute('PANEL={} local library=mmdhl.library ' + definition(lua, ui, 'function PANEL:RefreshBodygroupChoices('))
lua.execute(r'''
local presets=mmdhl.BodygroupPresets(ID)
for name in pairs(presets) do assert(type(name)=='string','a preset name is a '..type(name)) end
local names=table.GetKeys(presets) table.sort(names)
assert(table.concat(names,',')=='1,2024,Dress',table.concat(names,','))
assert(mmdhl.BodygroupDefault(ID)=='1')
local state=mmdhl.BodygroupSpawnState(ID,'1')
assert(state and state[1]==true and state[2]==false,'the preset named 1 is not found for spawns')
-- A default saved as a number is a name too.
mmdhl.library.entries[ID].settings.bodygroups.default=2024 assert(mmdhl.BodygroupDefault(ID)=='2024')
-- The library panel lists them sorted, with the default chosen (here one saved as a number).
local choices={}
local combo={Clear=function() choices={} end,AddChoice=function(_,label,data,selected) choices[#choices+1]={label=label,data=data,selected=selected} end}
local panel={BodygroupPreset=combo}
PANEL.RefreshBodygroupChoices(panel,mmdhl.library.entries[ID])
assert(#choices==4 and choices[3].data=='2024' and choices[3].selected and not choices[2].selected,'the default preset is not chosen')
assert(panel.bodygroupPreset=='2024')
''')
print('PASS: numeric preset names read back as numbers are names: sorted lists, the default and spawns find them')
