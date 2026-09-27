"""Virtual library folders preserve model identity and appearance preferences."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition
root=Path(__file__).resolve().parents[1]
lua=LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
mmdhl={native={},library={entries={}}}
isstring=function(v)return type(v)=='string'end
string.Trim=function(s)return s:match('^%s*(.-)%s*$')end
table.Copy=function(t)local c={} for k,v in pairs(t)do c[k]=type(v)=='table' and table.Copy(v) or v end return c end
table.GetKeys=function(t)local k={}for key in pairs(t)do k[#k+1]=key end return k end
local disk={}
file={Read=function(p)return disk[p]end,Write=function(p,v)disk[p]=v end,CreateDir=function()end,Find=function()return {},{string.rep('a',64),string.rep('b',64)}end}
util={TableToJSON=function(v)return table.Copy(v)end,JSONToTable=function(v)return type(v)=='table' and table.Copy(v)end}
-- Values are stored as tables; JSON decoding copies them like a disk reload.
hook={Run=function()end} game={SinglePlayer=function()return true end}
a=string.rep('a',64) b=string.rep('b',64)
disk['mmd_hotloader/assets/'..a..'/manifest.json']={vertices=30,name='one'}
disk['mmd_hotloader/assets/'..b..'/manifest.json']={vertices=60,name='two'}
disk['mmd_hotloader/library/'..a..'.json']={favorite=true,spawn={scaleMultiplier=1.2}}
''')
attach(lua)
source=(root/'addon/lua/mmdhl/library.lua').read_text(encoding='utf8')
lua.execute(source[:source.index('function library.Delete')])
lua.execute(r'''
local l=mmdhl.library l.Refresh()
assert(l.CreateFolder('Characters')=='Characters')
assert(l.CreateFolder('星穹铁道','Characters')=='Characters/星穹铁道')
assert(not l.CreateFolder('characters'))
assert(not l.CreateFolder('../escape')) assert(not l.CreateFolder('bad\\name'))
assert(l.MoveToFolder({a,b},'Characters/星穹铁道'))
l.Refresh() assert(l.entries[a].settings.folder=='Characters/星穹铁道')
assert(l.entries[a].settings.favorite and l.entries[a].settings.spawn.scaleMultiplier==1.2)
assert(l.RenameFolder('Characters','Favorites')=='Favorites')
assert(l.entries[b].settings.folder=='Favorites/星穹铁道')
assert(l.RemoveFolder('Favorites'))
assert(#l.Folders()==0 and l.entries[a].settings.folder=='' and l.entries[b].info.vertices==60)
assert(not l.MoveToFolder({a},'missing'))
assert(not l.MoveToFolder({string.rep('c',64)},''))
assert(l.entries[a].settings.favorite and l.entries[a].settings.spawn.scaleMultiplier==1.2)
''')
print('PASS: nested folders, rename, multi-move, reload, safe folder removal, metadata preservation')
ui=(root/'addon/lua/mmdhl/ui.lua').read_text(encoding='utf8')
lua.execute('PANEL={} library=mmdhl.library IsValid=function(p)return p~=nil end')
# The menu reads the tab's list through PANEL:Lib() and its text through ui.lua's file-level
# locals library and L; declare those as ui.lua does, on the chunk's first line.
lua.execute('local library,L=mmdhl.library,mmdhl.L '+definition(lua,ui,'function PANEL:Lib()')
 +ui[ui.index('function PANEL:PopulateMoveMenu'):ui.index('function PANEL:Refresh()')])
lua.execute('''
assert(library.CreateFolder('Destination')=='Destination')
local rows={{asset=a},{asset=b}}
local panel=setmetatable({Models={GetSelected=function()return rows end},
 Refresh=function(self)self.refreshed=true end,SetStatus=function(_,_,error)assert(not error)end},{__index=PANEL})
local menu={options={}}
function menu:AddOption(text,fn)self.options[#self.options+1]={text=text,click=fn}end
panel:PopulateMoveMenu(menu)
assert(menu.options[1].text=='Unfiled' and menu.options[2].text=='Destination')
menu.options[2].click()
assert(library.entries[a].settings.folder=='Destination' and library.entries[b].settings.folder=='Destination')
assert(panel.refreshed)
''')
print('PASS: destination submenu populates and moves the captured multi-selection')
