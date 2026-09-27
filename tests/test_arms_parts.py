"""First-person arms choices survive saving and reopening the editor. GMod's
util.JSONToTable turns numeric-looking object keys into numbers and
util.TableToJSON writes a table keyed 1..n as an array; the editor reads string
keys and the carrier builder rejects arrays. Runs first_person.lua and the shared
cleaner from materials.lua against a simulated client with GMod's JSON rules."""
from pathlib import Path
import json
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition

root = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)


def lupa_table(value): return type(value).__name__ == '_LuaTable'


def to_lua(value):
    # util.JSONToTable: arrays become sequences and numeric-looking keys numbers.
    if isinstance(value, dict):
        out = {}
        for k, v in value.items():
            try: key = int(k) if str(int(k)) == k else float(k)
            except ValueError: key = k
            out[key] = to_lua(v)
        return lua.table_from(out)
    if isinstance(value, list): return lua.table_from([to_lua(v) for v in value])
    return value


def to_py(value):
    # util.TableToJSON: a table keyed exactly 1..n is an array, anything else an object.
    if lupa_table(value):
        keys = list(value.keys())
        if keys and all(isinstance(k, int) for k in keys) and sorted(keys) == list(range(1, len(keys) + 1)):
            return [to_py(value[k]) for k in sorted(keys)]
        return {str(k): to_py(v) for k, v in value.items()}
    return value


lua.globals().py_decode = lambda text: to_lua(json.loads(text)) if text else None
lua.globals().py_encode = lambda value: json.dumps(to_py(value))
lua.execute(r'''
CLIENT=true mmdhl={}
isstring=function(v) return type(v)=='string' end istable=function(v) return type(v)=='table' end
util={JSONToTable=function(s) if type(s)~='string' or s=='' then return nil end return py_decode(s) end,TableToJSON=function(t) return py_encode(t) end}
FS={} file={Read=function(p) return FS[p] end,Write=function(p,v) FS[p]=v end,CreateDir=function() end}
CreateClientConVar=function() return {GetInt=function() return 2 end,SetInt=function() end} end
hook={Add=function() end} timer={Simple=function() end}
-- The arms preview request and its answer.
local reading={} SENT={} RECEIVERS={}
net={Receive=function(name,f) RECEIVERS[name]=f end,Start=function() SENT[#SENT+1]={} end,
 WriteUInt=function(v) table.insert(SENT[#SENT],v) end,WriteString=function(v) table.insert(SENT[#SENT],v) end,SendToServer=function() end,
 ReadUInt=function() return table.remove(reading,1) end,ReadString=function() return table.remove(reading,1) end}
function answer(key) local request=SENT[#SENT] reading={request[1],key,''} RECEIVERS.mmdhl_arms_preview() end
mmdhl.MountPackage=function() return true end mmdhl.RequestSharedRig=function() end
-- Editor widgets: a combo box shows the label of its current choice.
COMBOS={} BUTTONS={}
local function widget()
 local w={}
 for _,name in ipairs({'Dock','DockMargin','SetSize','Center','SetTitle','MakePopup','SetTall','SetWide','SetText','SetTextColor','SetFOV','SetCamPos','SetLookAt','SetModel','SetEnabled'}) do w[name]=function() end end
 function w:Add(class) local child=widget() if class=='DButton' then BUTTONS[#BUTTONS+1]=child end if class=='DComboBox' then child.choices={} function child:SetValue(v) self.shown=v end function child:AddChoice(label,value) self.choices[#self.choices+1]={label,value} end COMBOS[#COMBOS+1]=child end return child end
 return w
end
vgui={Create=function() return widget() end}
IsValid=function(v) return v~=nil end Color=function() return {} end Vector=function() return {} end
''')
attach(lua)
materials = (root / 'addon/lua/mmdhl/materials.lua').read_text(encoding='utf-8')
lua.execute(definition(lua, materials, 'function mmdhl.CleanArmsParts('))
lua.execute((root / 'addon/lua/mmdhl/first_person.lua').read_text(encoding='utf-8'))
lua.execute(r'''
local L=mmdhl.L
ID=string.rep('a',64) KEY=string.rep('b',32)
FS['mmd_hotloader/rigs/'..KEY..'/rig.json']=util.TableToJSON({model='models/mmd/arms.mdl',materials={{name='Body',slot=0},{name='Sleeve',slot=1},{name='Glove',slot=2}}})
local labels={[0]=L'first_person.part_auto',[1]=L'first_person.part_include',[-1]=L'first_person.part_exclude'}
local function open() COMBOS={} BUTTONS={} mmdhl.OpenArmsEditor(ID,'female') answer(KEY) local shown={} for i,c in ipairs(COMBOS) do shown[i-1]=c.shown end return shown end
-- Saved choices: slot 1 included, slot 2 excluded (keys 1..2 decode as numbers).
FS['mmd_hotloader/arms/'..ID..'.json']='{"1":1,"2":-1}'
local shown=open()
assert(shown[0]==labels[0] and shown[1]==labels[1] and shown[2]==labels[-1],'saved Include/Exclude choices reopen as saved, not as Auto')
-- Edit slot 0 and save again: one string key per slot, stored as a JSON object.
local function select(slot,value) for _,choice in ipairs(COMBOS[slot+1].choices) do if choice[2]==value then COMBOS[slot+1]:OnSelect(1,choice[1],value) end end end
select(0,1) select(2,0)
BUTTONS[1].DoClick() -- Rebuild: saves the choices and asks the server for the arms
local saved=FS['mmd_hotloader/arms/'..ID..'.json']
assert(saved:sub(1,1)=='{','arms choices are saved as an object')
local parts=util.JSONToTable(saved)
assert(parts[0]==1 and parts[1]==1 and parts[2]==0,'each slot is saved once with its edited choice')
assert(SENT[#SENT][4]:sub(1,1)=='{','the rebuild request carries an object, which the carrier builder accepts')
shown=open()
assert(shown[0]==labels[1] and shown[1]==labels[1] and shown[2]==labels[0],'the edited choices reopen as saved')
''')
print('PASS: arms choices keep string slot keys through save, reopen, edit and save')
