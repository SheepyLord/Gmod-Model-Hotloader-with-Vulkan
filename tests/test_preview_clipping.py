"""Menu preview paint must relinquish render clipping on success and failure."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition
root=Path(__file__).resolve().parents[1]
lua=LuaRuntime(unpack_returned_tuples=True)
lua.execute('''
PANEL={}Color=function(...)return {...}end
TEXT_ALIGN_CENTER=1
local mt={};mt.__index=mt
local function vec()return setmetatable({},mt)end
mt.__add=vec;mt.__mul=vec;mt.__unm=vec
function mt:Angle()return Angle()end
Angle=function()return {Forward=vec}end
camCount=0;rtCount=0;clipped=false
cam={Start3D=function()camCount=camCount+1 end,End3D=function()camCount=camCount-1 end}
render={ClearDepth=function()end,Clear=function()end,
 SetScissorRect=function(_,_,_,_,enabled)clipped=enabled end,
 PushRenderTarget=function()rtCount=rtCount+1 end,PopRenderTarget=function()rtCount=rtCount-1 end}
surface={GetScissorRect=function()error('VGUI clip must not become a render override')end,
 SetDrawColor=function()end,SetMaterial=function()end,DrawTexturedRectUV=function()end}
draw={RoundedBox=function()end,SimpleText=function()assert(not clipped,'Popup/text rendering inherited preview clipping')end}
previewTarget=function()return {target={},material={},w=128,h=128}end
releasePreview=function(p)p.released=true end
mmdhl={ImmediateRendering=function()return true end,UsesRemixPreview=function()return remix end,
 DrawLibraryPreview=function()if fail then error('test draw failure')end return true end}
-- A registered panel reaches its PANEL methods through its metatable, as vgui.Register arranges.
panel=setmetatable({previewHandle=1,previewInfo={},pitch=0,yaw=0,distance=1,target=vec(),
 Fonts={Small='font'},S=function(x)return x end,SetStatus=function(self,e)self.error=e end},{__index=PANEL})
mmdhl.previewOwner=panel
child={LocalToScreen=function()return 10,20 end}
''')
attach(lua)
source=(root/'addon/lua/mmdhl/ui.lua').read_text(encoding='utf8')
# Painting first applies any camera drag (PANEL:DragCamera; none is in progress here) and
# captions the preview through ui.lua's file-level L, declared on the chunk's first line.
lua.execute('local L=mmdhl.L '+definition(lua,source,'function PANEL:DragCamera(')
 +source[source.index('function PANEL:PaintPreview'):source.index('function PANEL:OnRemove')])
lua.execute('''
for _,mode in ipairs({false,true})do
 remix=mode
 for _,failure in ipairs({false,true})do
  fail=failure;panel.released=nil;panel.error=nil
  PANEL.PaintPreview(panel,child,100,80)
  assert(not clipped and camCount==0 and rtCount==0)
  assert((panel.released==true)==failure)
 end
end
''')
print('PASS: Source/RTX previews clear render clipping and balance camera/target stacks')
