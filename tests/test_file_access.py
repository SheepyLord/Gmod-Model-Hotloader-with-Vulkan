"""File access for other addons (file_access.lua) without the game: the server realm has no
API and no net receivers; an old binary module reports "needs update" and still calls back;
every callback runs exactly once, from the Think poll and never during the call; the
deny-only hook refuses before native and cannot grant; the MMDHL.RequestUserFile hook
forwards to the picker or RequestPath; requester labels and the reporting script are
sanitized; native refusal codes read as real phrases; MMDHL.FileAccessChanged runs after
the callback and outside the poll; a choice native could not save is reported; the
management window lists, revokes and switches through native. The installation check's
verdict on mmdhl_worker.exe (which shows the dialogs) reaches addons and the window, and
the switch still turns file access off then."""
import json
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'addon/lua/mmdhl/file_access.lua').read_text(encoding='utf-8')

STUBS = r'''
istable=function(v) return type(v)=='table' end
isstring=function(v) return type(v)=='string' end
isfunction=function(v) return type(v)=='function' end
isnumber=function(v) return type(v)=='number' end
-- GMod's IsValid: a table counts only through its own IsValid method.
IsValid=function(v) if not v then return false end local f=v.IsValid if not f then return false end return f(v) and true or false end
NOW=0 RealTime=function() return NOW end
TIMERS={} timer={Simple=function(_,f) TIMERS[#TIMERS+1]=f end}
NOTICES={} notification={AddProgress=function(id,text) NOTICES[#NOTICES+1]={kind='progress',id=id,text=text} end,Kill=function(id) NOTICES[#NOTICES+1]={kind='kill',id=id} end,AddLegacy=function(text) NOTICES[#NOTICES+1]={kind='legacy',text=text} end}
NOTIFY_HINT=1 NOTIFY_ERROR=2
surface={PlaySound=function() end}
WINDOWED=true system={IsWindowed=function() return WINDOWED end}
SINGLE=true HOST=false game={SinglePlayer=function() return SINGLE end}
LocalPlayer=function() return {IsValid=function() return true end,IsListenServerHost=function() return HOST end} end
COMMANDS={} concommand={Add=function(name,f) COMMANDS[name]=f end}
ERRORS={} ErrorNoHalt=function(m) ERRORS[#ERRORS+1]=m end
NETS={} net={Receive=function(name) NETS[#NETS+1]=name end}
Color=function(...) return {...} end ScrW=function() return 1920 end ScrH=function() return 1080 end
'''
# GMod's hook library, named as in the game so the reporting script skips it.
HOOK = r'''
HOOKS={}
hook={Add=function(event,name,f) HOOKS[event]=HOOKS[event] or {} HOOKS[event][name]=f end,
 Remove=function(event,name) if HOOKS[event] then HOOKS[event][name]=nil end end,
 Run=function(event,...) for _,f in pairs(HOOKS[event] or {}) do local r=f(...) if r~=nil then return r end end end}
'''
# A fake native module: ids, states, reads and listings the test drives.
NATIVE = r'''
CALLS={} STATES={} READS={} LISTS={} INFO={version=1,available=true,reason='ok',enabled=true} GRANTS={enabled=true,grants={}} SEQ=0
local function record(name,...) CALLS[#CALLS+1]={name=name,args={...}} end
local function nextId() SEQ=SEQ+1 return SEQ end
function CALLED(name) local n=0 for _,c in ipairs(CALLS) do if c.name==name then n=n+1 end end return n end
function LAST(name) for i=#CALLS,1,-1 do if CALLS[i].name==name then return CALLS[i].args end end end
function FAKE_NATIVE()
 local n={}
 n.FileAccessInfo=function() record('Info') return util.TableToJSON(INFO) end
 local function ask(name) return function(j) record(name,j) if REFUSE then local r=REFUSE REFUSE=nil return nil,r[1],r[2] end local id=nextId() STATES[id]={state='pending',dialog=true} return id end end
 n.FileAccessPick=ask('Pick') n.FileAccessRequest=ask('Request')
 n.FileAccessPoll=function(id) record('Poll',id) local s=STATES[id] if not s then return nil,'Unknown file request','unknown_request' end if s.state~='pending' then STATES[id]=nil end return util.TableToJSON(s) end
 n.FileAccessRead=function(h,j) record('Read',h,j) local id=nextId() READS[id]=READ_NEXT or 'pending' return id end
 n.FileAccessPollRead=function(id) local r=READS[id] if r=='pending' then return false end READS[id]=nil if r.code then return nil,r.error,r.code end return r.data,util.TableToJSON(r.info) end
 n.FileAccessList=function(h,j) record('List',h,j) local id=nextId() LISTS[id]=LIST_NEXT or 'pending' return id end
 n.FileAccessPollList=function(id) local r=LISTS[id] if r=='pending' then return false end LISTS[id]=nil return util.TableToJSON(r) end
 n.FileAccessRelease=function(h) record('Release',h) return true end
 n.FileAccessCancel=function(id) record('Cancel',id) return true end
 n.FileAccessGrants=function() record('Grants') return util.TableToJSON(GRANTS) end
 n.FileAccessRevoke=function(id) record('Revoke',id) return REVOKE_SAVED~=false end
 n.FileAccessSetEnabled=function(on,language) record('SetEnabled',on,language) if not on then GRANTS.enabled=false return util.TableToJSON({enabled=false,notSaved=OFF_NOT_SAVED}) end local id=nextId() STATES[id]={state='pending',dialog=true} return util.TableToJSON({request=id}) end
 return n
end
function THINK() local f=HOOKS.Think and HOOKS.Think['MMDHL.FileAccess'] if f then f() end NOW=NOW+1 end
-- Every callback: how often it ran and with what.
function CALLBACK() local c={runs=0} c.fn=function(...) c.runs=c.runs+1 c.args={...} c.n=select('#',...) end return c end
'''


def runtime(server=False, native=True):
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(STUBS)
    lua.globals().HOOK_SOURCE = HOOK
    lua.execute("assert(load(HOOK_SOURCE,'@lua/includes/modules/hook.lua'))()")
    lua.execute('mmdhl={}')
    attach(lua)

    def to_py(value):
        if lua.eval('function(v) return type(v)=="table" end')(value):
            keys = list(value.keys())
            if keys and all(isinstance(k, int) for k in keys) and sorted(keys) == list(range(1, len(keys) + 1)):
                return [to_py(value[k]) for k in sorted(keys)]
            return {k: to_py(v) for k, v in value.items()}
        return value
    lua.globals().JSON_ENCODE = lambda t: json.dumps(to_py(t))
    lua.globals().JSON_DECODE = lambda s: lua.table_from(json.loads(s), recursive=True)
    lua.execute('util={TableToJSON=function(t) return JSON_ENCODE(t) end,JSONToTable=function(s) local ok,r=pcall(JSON_DECODE,s) if ok then return r end end}')
    lua.execute(NATIVE)
    lua.execute(f'SERVER={"true" if server else "false"} CLIENT={"false" if server else "true"}')
    lua.execute('mmdhl.native=' + ('FAKE_NATIVE()' if native else '{}'))
    lua.globals().SOURCE = SOURCE
    lua.execute("assert(load(SOURCE,'@lua/mmdhl/file_access.lua'))()")
    # Another addon's file: the reporting script the dialogs show.
    lua.execute("OTHER=assert(load('return function(f,...) local a,b,c=f(...) return a,b,c end','@lua/autorun/client/other_addon.lua'))()")
    return lua


# ---- The server realm has nothing ----
lua = runtime(server=True)
assert lua.eval('mmdhl.FileAccess==nil and #NETS==0 and next(COMMANDS)==nil and next(HOOKS)==nil'), 'the server realm got file access'
print('PASS: the server realm has no file access API, hooks, commands or net receivers')

# ---- An old binary module: unavailable, explained, callbacks still once ----
lua = runtime(native=False)
lua.execute(r'''
local FA=mmdhl.FileAccess
assert(FA.Version==1)
local ok,text,code=FA.IsAvailable()
assert(ok==false and code=='needs_update' and text==mmdhl.L'file_access.needs_update',tostring(text))
assert(not text:find('mmdhl.',1,true))
local cb=CALLBACK()
assert(OTHER(FA.Pick,{addon='Old'},cb.fn)==false)
assert(cb.runs==0,'called back during the call')
THINK() THINK()
assert(cb.runs==1 and cb.args[1]==false and cb.args[3]=='needs_update' and cb.args[2]==mmdhl.L'file_access.needs_update')
local legacy=0 for _,n in ipairs(NOTICES) do if n.kind=='legacy' then legacy=legacy+1 assert(n.text==mmdhl.L'file_access.needs_update') end end
assert(legacy==1,'no fallback notice')
-- The update reminder, when the addon has it, is used once per session instead.
local shown={} mmdhl.ShowNativeUpdateNeeded=function(feature,release) shown[#shown+1]={feature,release} end FA.oldBinaryNoted=nil
local second=CALLBACK() FA.RequestPath('C:\\a.json',{addon='Old'},second.fn) FA.Pick({addon='Old'},CALLBACK().fn)
THINK()
assert(second.runs==1 and #shown==1 and shown[1][1]==mmdhl.L'file_access.feature' and shown[1][2]=='2.3.0')
-- Read on an old binary: no native, one answer.
local read=CALLBACK() FA.Read({handle=string.rep('a',32)},{},read.fn) THINK() assert(read.runs==1 and read.args[3]=='needs_update')
-- The soft-dependency hook still answers (the addon is installed, the binary is old).
local viaHook=CALLBACK() assert(hook.Run('MMDHL.RequestUserFile',{addon='Soft'},viaHook.fn)==true) THINK() assert(viaHook.runs==1 and viaHook.args[3]=='needs_update')
''')
print('PASS: an old binary module reports that file access needs 2.3.0, through the update reminder when present, and still calls back once')

# ---- The full flow against a fake native module ----
lua = runtime()
lua.execute(r'''
local FA=mmdhl.FileAccess
assert(FA.IsAvailable()==true)
-- Ready runs after every autorun file had its chance to add a hook.
local ready assert(#TIMERS>=1) hook.Add('MMDHL.FileAccessReady','test',function(api) ready=api end) for _,f in ipairs(TIMERS) do f() end assert(ready==FA)
-- Remote server and a local session whose server part failed read differently.
INFO={available=false,reason='no_local_server'} SINGLE=false HOST=false
local ok,text,code=FA.IsAvailable() assert(not ok and code=='unavailable_remote' and text==mmdhl.L'file_access.unavailable_remote')
SINGLE=true ok,text,code=FA.IsAvailable() assert(text==mmdhl.L'file_access.unavailable_server_realm')
SINGLE=false HOST=true ok,text,code=FA.IsAvailable() assert(text==mmdhl.L'file_access.unavailable_server_realm','the listen host read as a remote server') SINGLE=true HOST=false
INFO={available=false,reason='disabled'} ok,text,code=FA.IsAvailable() assert(code=='disabled' and text==mmdhl.L'file_access.unavailable_disabled')
INFO={available=true,reason='ok'}
-- A pick: sanitized label, the other addon's script, nothing during the call.
local cb=CALLBACK()
assert(OTHER(FA.Pick,{addon='  My\tAddon\n\1 v2  ',purpose='Load\na preset',filters={{'JSON files','*.json'},{1,2},'bad'},multiple=true},cb.fn)==true)
assert(cb.runs==0)
local sent=util.JSONToTable(LAST('Pick')[1])
assert(sent.requester=='My Addon v2' and sent.purpose=='Load a preset' and sent.script=='lua/autorun/client/other_addon.lua' and sent.language=='en' and sent.multiple==true,util.TableToJSON(sent))
assert(#sent.filters==1 and sent.filters[1][1]=='JSON files' and sent.filters[1][2]=='*.json')
assert(sent.path==nil and sent.addon==nil,'Lua-only fields reached native')
-- While the dialog waits: one notice naming the addon; windowed and full-screen wording.
THINK()
local notice=NOTICES[#NOTICES] assert(notice.kind=='progress' and notice.text==mmdhl.L('file_access.waiting_windowed',{addon='My Addon v2'}),notice.text)
local count=#NOTICES THINK() assert(#NOTICES==count,'the notice repeated')
local id=LAST('Pick') STATES[SEQ]={state='granted',items={{handle=string.rep('b',32),name='preset.json',size=12,folder=false,displayPath='~\\Documents\\preset.json'}}}
THINK()
assert(cb.runs==1 and cb.args[1]==true and #cb.args[2]==1,'the grant did not arrive once')
local item=cb.args[2][1] assert(item.handle==string.rep('b',32) and item.name=='preset.json' and item.size==12 and item.folder==false and isfunction(item.Read))
assert(NOTICES[#NOTICES].kind=='kill','the notice stayed')
for i=1,5 do THINK() end assert(cb.runs==1,'a callback ran twice')
-- Read and list through the item; text mode and limits pass through.
READ_NEXT={data='{"a":1}',info={size=7,offset=0,read=7,eof=true,encoding='ascii'}}
local read=CALLBACK() assert(item:Read({mode='text',maxBytes=4096},read.fn)==true) assert(read.runs==0)
local args=LAST('Read') local options=util.JSONToTable(args[2]) assert(args[1]==item.handle and options.mode=='text' and options.length==4096 and options.offset==nil)
THINK() assert(read.runs==1 and read.args[1]==true and read.args[2]=='{"a":1}' and read.args[3].encoding=='ascii')
READ_NEXT={code='outside',error='The file now leads outside what the player allowed'}
local escaped=CALLBACK() FA.Read(item.handle,{relative='x/y.txt'},escaped.fn) THINK()
assert(escaped.runs==1 and escaped.args[1]==false and escaped.args[3]=='outside' and escaped.args[2]==mmdhl.L'file_access.error.link')
READ_NEXT=nil
LIST_NEXT={entries={{name='a.json',folder=false,size=3,modified=1}},truncated=false}
local listed=CALLBACK() FA.List(item,listed.fn) THINK() assert(listed.runs==1 and listed.args[2][1].name=='a.json' and listed.args[3].truncated==false)
item:Release() assert(LAST('Release')[1]==item.handle)
local gone=CALLBACK() FA.Read({},{},gone.fn) THINK() assert(gone.runs==1 and gone.args[3]=='released')
-- A refused submission: the code and a real phrase, still on the next Think.
REFUSE={'Paths to other computers and devices are never read','network'}
local network=CALLBACK() assert(FA.RequestPath('\\\\host\\share\\a.json',{addon='Net'},network.fn)==false) assert(network.runs==0)
THINK() assert(network.runs==1 and network.args[3]=='network' and network.args[2]==mmdhl.L'file_access.error.network')
REFUSE={'Option filters must be text','invalid_options'}
local invalid=CALLBACK() FA.Pick({addon='Bad'},invalid.fn) THINK() assert(invalid.args[2]==mmdhl.L('file_access.error.invalid_options',{error='Option filters must be text'}))
-- No addon name: refused in Lua, native never asked.
local before=CALLED('Pick') local anonymous=CALLBACK() FA.Pick({},anonymous.fn) THINK()
assert(anonymous.runs==1 and anonymous.args[3]=='invalid_options' and CALLED('Pick')==before)
-- Labels keep whole characters (64 of them).
FA.Pick({addon=string.rep('\195\169',100)},CALLBACK().fn) assert(util.JSONToTable(LAST('Pick')[1]).requester==string.rep('\195\169',64))
assert(FA.Clip('a\0b\127c',64)=='a b c')
-- A failing callback does not stop the others.
local bad=CALLBACK() local good=CALLBACK()
FA.RequestPath('C:\\x.json',{addon='Boom'},function() error('boom') end) FA.RequestPath('C:\\y.json',{addon='Fine'},good.fn)
STATES[SEQ-1]={state='denied',code='denied',error='The player denied the request'} STATES[SEQ]={state='denied',code='auto_denied',error='x'}
THINK() assert(good.runs==1 and good.args[3]=='auto_denied' and good.args[2]==mmdhl.L'file_access.error.auto_denied' and #ERRORS==1)
-- A remembered grant tells the management window.
local changed=0 hook.Add('MMDHL.FileAccessChanged','test',function() changed=changed+1 end)
local remembered=CALLBACK() FA.RequestPath('C:\\Games\\a.json',{addon='Keep',folder=false},remembered.fn)
STATES[SEQ]={state='granted',changed=true,items={{handle=string.rep('c',32),name='a.json',remembered=true}}}
THINK() assert(remembered.runs==1 and remembered.args[2][1].remembered==true and changed==1)
-- The hook runs after the callback, from the queue: a listener that fails cannot lose the answer...
local order={}
hook.Add('MMDHL.FileAccessChanged','broken',function() order[#order+1]='changed' error('listener failed') end)
local errors=#ERRORS
local first=CALLBACK() FA.RequestPath('C:\\Games\\b.json',{addon='Keep'},function(...) order[#order+1]='callback' first.fn(...) end)
STATES[SEQ]={state='granted',changed=true,items={{handle=string.rep('e',32),name='b.json',remembered=true}}}
THINK() assert(first.runs==1 and first.args[1]==true and #ERRORS==errors+1 and order[1]=='callback' and order[2]=='changed','a failing listener lost the answer: '..table.concat(order,','))
hook.Remove('MMDHL.FileAccessChanged','broken')
-- ...and one that asks again does not change the requests while the poll walks them.
local again=CALLBACK()
hook.Add('MMDHL.FileAccessChanged','asks again',function() hook.Remove('MMDHL.FileAccessChanged','asks again') FA.RequestPath('C:\\Games\\c.json',{addon='Keep'},again.fn) end)
local second,third=CALLBACK(),CALLBACK()
FA.RequestPath('C:\\Games\\b.json',{addon='Keep'},second.fn) FA.RequestPath('C:\\Games\\d.json',{addon='Keep'},third.fn)
STATES[SEQ-1]={state='granted',changed=true,items={{handle=string.rep('f',32),name='b.json'}}}
errors=#ERRORS THINK()
assert(second.runs==1 and third.runs==0 and again.runs==0 and #ERRORS==errors and util.JSONToTable(LAST('Request')[1]).path=='C:\\Games\\c.json','the listener could not ask again')
STATES[SEQ]={state='denied',code='denied'} STATES[SEQ-1]={state='denied',code='denied'} THINK() assert(again.runs==1 and third.runs==1)
-- A choice native could not save still applies; the player hears about it once.
local notices=#NOTICES local kept=CALLBACK() FA.RequestPath('C:\\Games\\e.json',{addon='Keep'},kept.fn)
STATES[SEQ]={state='granted',changed=true,notSaved=true,items={{handle=string.rep('a',32),name='e.json',remembered=true}}}
THINK() local told=0 for i=notices+1,#NOTICES do if NOTICES[i].kind=='legacy' and NOTICES[i].text==mmdhl.L'file_access.not_saved' then told=told+1 end end
assert(kept.runs==1 and kept.args[1]==true and told==1,'the unsaved choice was not reported')
-- A request whose answer vanished (the module restarted) still calls back.
local lost=CALLBACK() FA.RequestPath('C:\\z.json',{addon='Lost'},lost.fn) STATES[SEQ]=nil THINK() assert(lost.runs==1 and lost.args[3]=='unknown_request')
''')
print('PASS: requests, reads and listings call back once from Think with real phrases; labels and scripts are sanitized; failing callbacks are contained')

# ---- Hooks: deny-only rules and the soft-dependency entry ----
lua.execute(r'''
local FA=mmdhl.FileAccess
local seen
hook.Add('MMDHLCanAccessUserFile','policy',function(addon,request) seen={addon=addon,kind=request.kind,path=request.path,script=request.script} request.path='C:\\evil' if addon=='Blocked' then return false end end)
local before=CALLED('Request')
local blocked=CALLBACK() assert(OTHER(FA.RequestPath,'C:\\a.json',{addon='Blocked'},blocked.fn)==false)
THINK() assert(blocked.runs==1 and blocked.args[3]=='denied_by_hook' and blocked.args[2]==mmdhl.L'file_access.error.denied_by_hook' and CALLED('Request')==before)
assert(seen.addon=='Blocked' and seen.kind=='path' and seen.path=='C:\\a.json' and seen.script=='lua/autorun/client/other_addon.lua')
-- Returning true never grants: native still decides, and the hook cannot change the path.
hook.Add('MMDHLCanAccessUserFile','policy',function() return true end)
local asked=CALLBACK() FA.RequestPath('C:\\a.json',{addon='Allowed'},asked.fn) THINK()
assert(CALLED('Request')==before+1 and util.JSONToTable(LAST('Request')[1]).path=='C:\\a.json' and asked.runs==0,'a hook granted or rewrote the request')
STATES[SEQ]={state='denied',code='denied'} THINK() assert(asked.runs==1 and asked.args[1]==false)
hook.Remove('MMDHLCanAccessUserFile','policy')
-- The entry other addons can call without depending on Model Hotloader.
local picks,requests=CALLED('Pick'),CALLED('Request')
local soft=CALLBACK() assert(OTHER(hook.Run,'MMDHL.RequestUserFile',{addon='Soft',filters={{'JSON','*.json'}}},soft.fn)==true)
assert(CALLED('Pick')==picks+1 and util.JSONToTable(LAST('Pick')[1]).script=='lua/autorun/client/other_addon.lua','the hook did not open the picker for the calling addon')
assert(hook.Run('MMDHL.RequestUserFile',{addon='Soft',path='D:\\Data\\a.json',purpose='Import'},CALLBACK().fn)==true and CALLED('Request')==requests+1)
assert(util.JSONToTable(LAST('Request')[1]).path=='D:\\Data\\a.json')
assert(hook.Run('MMDHL.RequestUserFile','nonsense')==nil,'a malformed request was claimed')
STATES[SEQ-1]={state='granted',items={{handle=string.rep('d',32),name='x.json'}}} THINK() assert(soft.runs==1 and soft.args[1]==true)
''')
print('PASS: MMDHLCanAccessUserFile only denies, and MMDHL.RequestUserFile forwards to the picker or RequestPath')

# ---- Every refusal code reads as a real phrase ----
lua.execute(r'''
for _,code in ipairs({'denied','auto_denied','auto_denied_session','busy','not_found','network','remote_drive','relative','parent','stream','device','invalid_path','denied_location','link','outside','hidden','too_large',
 'offset_too_large','not_a_file','not_a_folder','released','unknown_request','unreadable','dialog_failed','too_many_items','denied_by_hook','disabled','unavailable_remote','no_local_server','worker_missing','worker_unavailable','needs_update','invalid_options','something_new'}) do
 local text=mmdhl.FileAccess.Message(code,'detail')
 assert(isstring(text) and text~='' and not text:find('mmdhl.file_access',1,true),code..': '..tostring(text))
end
assert(mmdhl.FileAccess.Message('something_new','detail')==mmdhl.L('file_access.error.other',{error='detail'}))
assert(mmdhl.FileAccess.Message('auto_denied_session')==mmdhl.L'file_access.error.auto_denied_session' and mmdhl.FileAccess.Message('auto_denied_session')~=mmdhl.FileAccess.Message('auto_denied'))
''')
print('PASS: every native refusal code has a localized explanation')

# ---- The management window ----
lua.execute(r'''
-- Answer what earlier checks left waiting: the real module shows one dialog at a time.
for id,state in pairs(STATES) do if state.state=='pending' then STATES[id]={state='denied',code='denied'} end end THINK() THINK()
PANELS={}
local function panel(kind)
 local p={kind=kind,children={},lines={},removed=false}
 PANELS[#PANELS+1]=p
 return setmetatable(p,{__index=function(t,k)
  if k=='Add' then return function(self,child) local c=panel(child) self.children[#self.children+1]=c return c end end
  if k=='AddLine' then return function(self,...) local line=panel('line') line.values={...} self.lines[#self.lines+1]=line return line end end
  if k=='Clear' then return function(self) self.lines={} end end
  if k=='GetSelectedLine' then return function(self) return 1,self.lines[1] end end
  if k=='SetChecked' then return function(self,v) self.checked=v end end
  if k=='SetText' then return function(self,v) self.text=v end end
  if k=='SetEnabled' then return function(self,v) self.enabled=v end end
  if k=='SetVisible' then return function(self,v) self.visible=v end end
  return function(self) return panel('result') end
 end})
end
vgui={Create=function(kind) local p=panel(kind) if kind=='DFrame' then rawset(p,'btnMinim',panel('DButton')) rawset(p,'btnMaxim',panel('DButton')) end return p end}
local function find(kind,text) for _,p in ipairs(PANELS) do if p.kind==kind and (text==nil or p.text==text) then return p end end end
FIND=find
mmdhl.UI={metrics=function() return function(n) return n end,{Body='b',Small='s',Strong='t',Title='T'} end,colors={muted={},ink={}},
 label=function(parent,text) local p=parent:Add('DLabel') p.text=text return p end,
 button=function(parent,text,cb) local p=parent:Add('DButton') p.text=text p.DoClick=cb return p end,
 checkbox=function(parent,text) local p=parent:Add('DCheckBoxLabel') p.text=text return p end}
QUERIES={} Derma_Query=function(text,title,yes,accept) QUERIES[#QUERIES+1]=text accept() end
GRANTS={enabled=true,grants={{id='0123456789abcdef',requester='HUD Maker',folder='~\\Documents\\HUD',created=1,used=1700000000}}}
assert(COMMANDS.mmdhl_file_access,'no console command')
COMMANDS.mmdhl_file_access()
local frame=mmdhl.FileAccess.manager assert(frame and frame.kind=='DFrame')
local list=find('DListView') assert(#list.lines==1 and list.lines[1].values[1]=='HUD Maker' and list.lines[1].values[2]=='~\\Documents\\HUD' and list.lines[1].grant=='0123456789abcdef')
local box=find('DCheckBoxLabel',mmdhl.L'file_access.manage.enabled') assert(box.checked==true)
find('DButton',mmdhl.L'file_access.manage.revoke').DoClick() assert(LAST('Revoke')[1]=='0123456789abcdef')
find('DButton',mmdhl.L'file_access.manage.revoke_all').DoClick() assert(LAST('Revoke')[1]=='all' and QUERIES[1]==mmdhl.L'file_access.manage.revoke_all_confirm')
-- A revoke or a switch native could not save is reported (it applies until the map changes).
local function unsaved() local n=0 for _,v in ipairs(NOTICES) do if v.kind=='legacy' and v.text==mmdhl.L'file_access.not_saved' then n=n+1 end end return n end
local before=unsaved() REVOKE_SAVED=false find('DButton',mmdhl.L'file_access.manage.revoke').DoClick() REVOKE_SAVED=nil assert(unsaved()==before+1,'an unsaved revoke was not reported')
-- Off at once; on asks natively and shows the confirmation notice meanwhile.
OFF_NOT_SAVED=true box.OnChange(box,false) OFF_NOT_SAVED=nil assert(LAST('SetEnabled')[1]==false and unsaved()==before+2,'an unsaved switch was not reported')
for _,f in ipairs(TIMERS) do f() end
box.OnChange(box,true) local args=LAST('SetEnabled') assert(args[1]==true and args[2]=='en')
THINK() assert(NOTICES[#NOTICES].text==mmdhl.L'file_access.waiting_confirm')
STATES[SEQ]={state='granted',enabled=true,changed=true} GRANTS.enabled=true THINK() THINK()
assert(NOTICES[#NOTICES].kind=='kill' and box.checked==true)
''')
print('PASS: the management window lists remembered folders, revokes them and switches file access through native')

# ---- The installation check's verdict on the worker that shows the dialogs ----
lua.execute(r'''
local FA=mmdhl.FileAccess
-- installation.lua's guard refuses the calls that start mmdhl_worker.exe with the check's reason
-- (tests/test_file_access_worker_gate.py); its sentence reaches the addon inside the file access one.
local why='lua/bin/mmdhl_worker.exe belongs to release 2.2.0; this addon needs 2.3.0.'
assert(FA.Message('worker_unavailable',why)==mmdhl.L('file_access.worker_unavailable',{reason=why}))
REFUSE={why,'worker_unavailable'}
local refused=CALLBACK() assert(FA.Pick({addon='Gated'},refused.fn)==false) THINK()
assert(refused.runs==1 and refused.args[1]==false and refused.args[3]=='worker_unavailable' and refused.args[2]==mmdhl.L('file_access.worker_unavailable',{reason=why}),tostring(refused.args[2]))
-- IsAvailable says so whether file access is on or merely off; a remote server stays the reason there.
local checked={} mmdhl.FeatureAvailable=function(feature) checked[#checked+1]=feature if WORKER_OK then return true end return false,why end
INFO={available=true,reason='ok',enabled=true}
local ok,text,code=FA.IsAvailable() assert(not ok and code=='worker_unavailable' and text==mmdhl.L('file_access.worker_unavailable',{reason=why}) and checked[#checked]=='imports')
INFO={available=false,reason='disabled',enabled=false} ok,text,code=FA.IsAvailable() assert(not ok and code=='worker_unavailable')
INFO={available=false,reason='no_local_server'} SINGLE=false ok,text,code=FA.IsAvailable() assert(code=='unavailable_remote') SINGLE=true
-- The window: on, the switch still turns file access off (that needs no window)...
INFO={available=true,reason='ok',enabled=true} GRANTS={enabled=true,grants={}}
local box=FIND('DCheckBoxLabel',mmdhl.L'file_access.manage.enabled')
hook.Run('MMDHL.InstallationChanged')
local status=FIND('DLabel',mmdhl.L('file_access.worker_unavailable',{reason=why}))
assert(status and status.visible and box.enabled==true and box.checked==true,'the switch cannot turn file access off while the worker is not allowed')
box.OnChange(box,false) assert(LAST('SetEnabled')[1]==false)
INFO={available=false,reason='disabled',enabled=false} for _,f in ipairs(TIMERS) do f() end THINK()
assert(box.checked==false and box.enabled==false,'file access could be turned on without a worker the installation check allows')
-- ...and once the check allows the worker (its self-test ended), the window follows.
WORKER_OK=true hook.Run('MMDHL.InstallationChanged') assert(box.enabled==true and status.text==mmdhl.L'file_access.unavailable_disabled')
mmdhl.FeatureAvailable=nil
''')
print('PASS: the installation check\'s verdict on the worker reaches addons and the window; the switch still turns file access off')

# ---- While the worker's self-test runs: available, and what needs a window waits for the verdict ----
lua.execute(r'''
local FA=mmdhl.FileAccess
for _,f in ipairs(TIMERS) do f() end THINK() THINK()
local why=mmdhl.L('install.checking_feature',{feature='Model import'})
WORKER_OK=false CHECKING=true
mmdhl.FeatureAvailable=function() if WORKER_OK then return true end return false,why end
mmdhl.GetInstallationStatus=function() return {probePending=CHECKING} end
INFO={available=true,reason='ok',enabled=true}
local changed=0 hook.Add('MMDHL.FileAccessChanged','verdict',function() changed=changed+1 end)
hook.Run('MMDHL.InstallationChanged') THINK()
assert(FA.IsAvailable()==true and changed==0,'file access read as unavailable while the worker self-test ran')
-- The guard refuses what needs a window until the verdict: the request waits, nobody is called back.
local picks=CALLED('Pick') REFUSE={why,'worker_unavailable'}
local waited=CALLBACK() assert(FA.Pick({addon='Early'},waited.fn)==true)
for i=1,3 do THINK() end assert(waited.runs==0 and CALLED('Pick')==picks+1,'a request refused during the self-test was not held')
-- The self-test passes: asked again once, then answered as usual.
CHECKING=false WORKER_OK=true hook.Run('MMDHL.InstallationChanged') THINK()
assert(CALLED('Pick')==picks+2 and waited.runs==0 and changed==0,'the held request was not asked again once')
STATES[SEQ]={state='granted',items={{handle=string.rep('9',32),name='late.json'}}} THINK()
assert(waited.runs==1 and waited.args[1]==true)
-- A verdict that changes what IsAvailable says runs MMDHL.FileAccessChanged once (from Think); one that does not, never.
WORKER_OK=false hook.Run('MMDHL.InstallationChanged') assert(changed==0) THINK() assert(changed==1)
hook.Run('MMDHL.InstallationChanged') THINK() assert(changed==1)
WORKER_OK=true hook.Run('MMDHL.InstallationChanged') THINK() assert(changed==2)
hook.Remove('MMDHL.FileAccessChanged','verdict') mmdhl.FeatureAvailable=nil mmdhl.GetInstallationStatus=nil
''')
print('PASS: during the worker self-test file access counts as available and requests wait for the verdict; a changed verdict runs MMDHL.FileAccessChanged')

# ---- The realm count that gates file access is taken once a module opened, never before ----
# localServerRealm() counts the server modules that opened in this process; nothing releases the
# count of one that failed to open, which would leave file access offered on a remote server.
module = (ROOT / 'native/module.cpp').read_text(encoding='utf-8')
opening = module[module.index('GMOD_MODULE_OPEN(){'):module.index('GMOD_MODULE_CLOSE(){')]
body, failure = opening.split('}catch(const std::exception& e){')
assert body.count('acquireRuntimeRealm(') == 1 and 'acquireRuntimeRealm(' not in failure, 'the realm is counted more than once'
acquired = body.index('acquireRuntimeRealm(ServerRealm)')
assert all(acquired > body.rindex(step) for step in ('create_directories(', 'registerPropFunctions(', 'sweepJobFolders(')), 'the realm is counted before the module can still fail to open'
assert failure.index('context.reset()') < failure.index('ThrowError'), 'a module that failed to open keeps its state'
print('PASS: the server realm is counted only once its module opened')
