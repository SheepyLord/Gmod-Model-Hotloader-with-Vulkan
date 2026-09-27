"""A rejected renderer stays cheap and reports its cause without forcing queue mode."""
from pathlib import Path
from lupa import LuaRuntime

root = Path(__file__).resolve().parents[1]
source = (root/'addon/lua/mmdhl/client.lua').read_text(encoding='utf8')
lua = LuaRuntime()
lua.execute('''
calls=0 commands=0
mmdhl={native={CheckRenderer=function()calls=calls+1 return nil,'Unsupported engine fixture' end}}
GetConVar=function()error('Rejected renderer must not touch render queue settings')end
RunConsoleCommand=function()commands=commands+1 end
''')
lua.execute(source[:source.index('local function restoreRendering()')])
lua.execute('''
for i=1,1000 do
 local ok,err=mmdhl.RenderAvailable() assert(not ok and err=='Unsupported engine fixture')
 assert(not mmdhl.ImmediateRendering())
end
assert(calls==1 and commands==0)
assert(mmdhl.renderInitError=='Unsupported engine fixture')
''')

ui = (root/'addon/lua/mmdhl/ui.lua').read_text(encoding='utf8')
status = ui[ui.index('function PANEL:SetStatus'):ui.index('function PANEL:ResetCamera')]
lua.execute('PANEL={}; function Color(...) return {...} end')
lua.execute(status)
lua.execute('''
local text,tooltip
local panel={Status={SetTooltip=function(_,v)tooltip=v end,SetText=function(_,v)text=v end,SetTextColor=function()end}}
PANEL.SetStatus(panel,'addons/mmd_hotloader/lua/mmdhl/ui.lua:303: Unsupported fixture\\nstack traceback:\\n[C]: error',true)
assert(text=='Unsupported fixture' and tooltip:find('stack traceback',1,true))
PANEL.SetStatus(panel,'Ready',false) assert(text=='Ready' and tooltip==nil)
''')
print('PASS: unsupported renderer is checked once; UI preserves the error cause and full details')
