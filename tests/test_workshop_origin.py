"""The library's Workshop origin banner: its button opens the Workshop page, or
restores a deleted Workshop model. Builds the banner with ui.lua's own code and
clicks the button in both states."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition

root = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
PANEL={} TOP,RIGHT,FILL=1,2,3
VIEWED={} RESTORED={}
steamworks={ViewFile=function(id) VIEWED[#VIEWED+1]=id end}
mmdhl={workshop={Restore=function(kind,ids) RESTORED[#RESTORED+1]=kind..':'..ids[1] return true,'restored' end}}
surface={SetFont=function() end,GetTextSize=function(t) return #t*7,12 end}
draw={RoundedBox=function() end} Color=function() return {} end
kindOf=function(mode) return mode=='static' and 'static' or 'character' end
local function widget()
 local w={visible=true,values={}}
 for _,name in ipairs({'Dock','DockMargin','SetTall','SetWide','SetText','SetTooltip'}) do w[name]=function(self,v) self.values[name]=v end end
 function w:SetVisible(v) self.visible=v end
 function w:Add() return widget() end
 return w
end
-- ui.lua's local helpers: a button runs its callback when clicked.
button=function(parent,text,callback) local b=widget() b.click=callback return b end
label=function() return widget() end
panel=setmetatable({Right=widget(),Fonts={Small='small'},S=function(v) return v end,mode='library',
 SetStatus=function(self,m,err) self.status=m self.statusError=err end,Refresh=function() end,InvalidateLayout=function() end},{__index=PANEL})
''')
attach(lua)
lua.execute('L=mmdhl.L')
ui = (root / 'addon/lua/mmdhl/ui.lua').read_text(encoding='utf-8')
# The banner is built inside the library panel's layout code: run those lines as written.
start = ui.index(" self.Origin=self.Right:Add('DPanel')")
end = ui.index('\n', ui.index(' self.OriginText=label(self.Origin')) + 1
lua.execute('function buildOrigin(self,s,f)\n' + ui[start:end] + 'end')
for header in ['function PANEL:ShowOrigin(', 'function PANEL:OriginAction(']: lua.execute(definition(lua, ui, header))
lua.execute(r'''
buildOrigin(panel,function(v) return v end,{Small='small'})
local banner=nil
for _,value in pairs(panel) do if type(value)=='table' and value.click then banner=value end end
assert(banner,'the banner has a button')
panel:ShowOrigin({title='Anime Pack',author='Tester',wsid='123456',workshop=true},false)
assert(banner.visible,'a Workshop item offers its page')
banner.click()
assert(VIEWED[1]=='123456' and #RESTORED==0,'the button opens the Workshop page')
panel.deletedRow={asset=string.rep('c',64)}
panel:ShowOrigin({title='Anime Pack',wsid='123456',workshop=true},true)
banner.click()
assert(RESTORED[1]=='character:'..string.rep('c',64) and #VIEWED==1 and panel.status=='restored','the button restores a deleted Workshop model')
''')
print('PASS: the Workshop origin button opens the Workshop page and restores deleted models')
