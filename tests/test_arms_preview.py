"""First-person arms preview requests always get an answer: the server replies to
an unapproved model and to a request within its one-second cooldown with a
reason, and the arms editor stops waiting after 30 seconds or when it is closed.
Runs actors.lua's request handler on a simulated server and first_person.lua on a
simulated client."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition

root = Path(__file__).resolve().parents[1]
actors = (root / 'addon/lua/mmdhl/actors.lua').read_text(encoding='utf-8')
server = LuaRuntime(unpack_returned_tuples=True)
server.execute(r'''
SERVER=true CLIENT=false NOW=0 RealTime=function() return NOW end
IsValid=function(v) return v~=nil end
mmdhl={}
local reading,at REPLIES={}
net={Receive=function(_,f) RECEIVE=f end,ReadUInt=function() at=at+1 return reading[at] end,Start=function() REPLIES[#REPLIES+1]={} end,
 WriteUInt=function(v) table.insert(REPLIES[#REPLIES],v) end,WriteString=function(v) table.insert(REPLIES[#REPLIES],v) end,Send=function() end}
net.ReadString=net.ReadUInt
function request(p,token,id) local before=#REPLIES reading={token,id,'female','{}'} at=0 RECEIVE(0,p) return #REPLIES>before and REPLIES[#REPLIES] or nil end
''')
attach(server)
server.execute(r'''
APPROVED,KEY=string.rep('a',64),string.rep('b',32) PUBLISHED={}
util={JSONToTable=function() return {} end,TableToJSON=function(t) return t end}
mmdhl.CanUseAsset=function(p,id) return id==APPROVED end
mmdhl.CleanArmsParts=function(t) return t or {} end
mmdhl.LoadAsset=function(id,cb) cb({name='A'}) end
mmdhl.ActorOptions=function(o) return o end
mmdhl.Decode=function(v) return v end
mmdhl.PublishRig=function(rig) PUBLISHED[#PUBLISHED+1]=rig.key end
mmdhl.native={PrepareCarrier=function() return {key=KEY} end}
''')
server.execute('local native,L=mmdhl.native,mmdhl.L ' + definition(server, actors, " net.Receive('mmdhl_arms_preview',"))
server.execute(r'''
local p={}
local r=request(p,7,string.rep('c',64))
assert(r and r[1]==7 and r[2]=='' and r[3]==mmdhl.I18n.Token('share.error.model_not_approved'),'an unapproved model was not answered')
r=request(p,8,APPROVED) assert(r[1]==8 and r[2]==KEY and r[3]=='' and #PUBLISHED==1)
r=request(p,9,APPROVED)
assert(r and r[1]==9 and r[2]=='' and r[3]==mmdhl.I18n.Token('actors.error.arms_preview_wait') and #PUBLISHED==1,'a request within the cooldown was not answered')
NOW=NOW+1.01 r=request(p,10,APPROVED) assert(r[1]==10 and r[2]==KEY and #PUBLISHED==2)
''')
print('PASS: the server answers every arms preview request, with a reason when it refuses')

client = LuaRuntime(unpack_returned_tuples=True)
client.execute(r'''
CLIENT=true SERVER=false mmdhl={}
isstring=function(v) return type(v)=='string' end istable=function(v) return type(v)=='table' end
util={JSONToTable=function() return nil end,TableToJSON=function() return '{}' end}
file={Read=function() end,Write=function() end,CreateDir=function() end}
CreateClientConVar=function() return {GetInt=function() return 2 end,SetInt=function() end} end
hook={Add=function() end}
TIMERS={} timer={Simple=function(delay,f) TIMERS[#TIMERS+1]={delay=delay,f=f} end}
local reading={} SENT={}
net={Receive=function(_,f) RECEIVE=f end,Start=function() SENT[#SENT+1]={} end,WriteUInt=function(v) table.insert(SENT[#SENT],v) end,WriteString=function(v) table.insert(SENT[#SENT],v) end,SendToServer=function() end,
 ReadUInt=function() return table.remove(reading,1) end,ReadString=function() return table.remove(reading,1) end}
function answer(token,key,err) reading={token,key,err or ''} RECEIVE() end
mmdhl.CleanArmsParts=function(t) return t or {} end
mmdhl.MountPackage=function() return true end
REQUESTED={} mmdhl.RequestSharedRig=function(key,cb) REQUESTED[#REQUESTED+1]=key end
-- The editor's widgets: labels keep their text, buttons whether they are enabled.
local function widget()
 local w={enabled=true}
 for _,name in ipairs({'Dock','DockMargin','SetSize','Center','SetTitle','MakePopup','SetTall','SetWide','SetTextColor','SetFOV','SetCamPos','SetLookAt','SetModel','SetValue','AddChoice'}) do w[name]=function() end end
 function w:SetText(t) self.text=t end function w:SetEnabled(v) self.enabled=v end
 function w:Add(class) local child=widget() WIDGETS[#WIDGETS+1]=child return child end
 function w:Remove() self.removed=true if self.OnRemove then self:OnRemove() end end
 return w
end
vgui={Create=function() FRAME=widget() return FRAME end}
IsValid=function(v) return v~=nil and not v.removed end Color=function() return {} end Vector=function() return {} end
-- Opens the editor, which asks for the arms at once; returns the request token.
function open() WIDGETS={} TIMERS={} mmdhl.OpenArmsEditor(string.rep('a',64),'female') STATUS,APPLY=WIDGETS[1],WIDGETS[2] return SENT[#SENT][1] end
''')
attach(client)
client.execute((root / 'addon/lua/mmdhl/first_person.lua').read_text(encoding='utf-8'))
client.execute(r'''
local L=mmdhl.L
-- No answer: after 30 seconds the editor stops waiting and can rebuild again.
local token=open()
assert(STATUS.text==L'first_person.extracting' and not APPLY.enabled and #TIMERS==1 and TIMERS[1].delay==30,'no timeout for the request')
TIMERS[1].f()
assert(STATUS.text==L'first_person.timed_out' and APPLY.enabled,'the editor still waits for an answer')
answer(token,string.rep('b',32)) assert(STATUS.text==L'first_person.timed_out' and #REQUESTED==0,'a late answer was used')
-- A refusal shows its reason; the timeout then does nothing.
token=open() answer(token,'',mmdhl.I18n.Token('actors.error.arms_preview_wait'))
assert(STATUS.text==L'actors.error.arms_preview_wait' and APPLY.enabled)
TIMERS[1].f() assert(STATUS.text==L'actors.error.arms_preview_wait','the timeout fired after the answer')
-- Closing the editor drops its request: a later answer does nothing.
token=open() FRAME:Remove()
answer(token,string.rep('b',32)) TIMERS[1].f()
assert(#REQUESTED==0,'a closed editor still handled its answer')
''')
print('PASS: the arms editor stops waiting after 30 seconds or when it is closed')
