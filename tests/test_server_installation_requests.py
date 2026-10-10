"""Players on a server are never refused by the status the server reported: the
client sends the request and the server checks its own installation. A server
whose native module did not load answers a character or prop spawn (and the
physics editor) at once with its problem, worded as the server's ("Server: …"),
instead of letting the client wait for its timeout; a server whose physics is
unavailable words its refusal the same way. A server always tries the module file it
has: one this addon does not know whose runtime is missing is still required, and when
Windows refuses it the answer names the missing runtime (the problem that explains the
failure), never the module's warning; a consistent build this addon does not know
(other hashes, one build ID in module and runtime) loads with every server feature,
its warnings reach the server console before require, and the pooled handlers stay
silent. Runs client.lua's and library.lua's request functions, installation.lua as a
dedicated server (without native files, then with them), and server.lua's spawn check."""
from pathlib import Path
import sys
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'scripts'))
from compatibility_profiles import lua_policy
source = lambda name: (root / 'addon/lua/mmdhl' / name).read_text(encoding='utf-8')

# The client: a server status saying the installation is unavailable stops nothing.
client = LuaRuntime(unpack_returned_tuples=True)
client.execute(r'''
CLIENT=true SERVER=false
mmdhl={library={},spawnRequests={}}
mmdhl.serverInstallation={schema=1,realm='server',features={core=false,physics=false},issues={{code='missing',component='runtime',message='x',feature='core'}}}
SENT={} NOTIFIED={}
net={Start=function(name) SENT[#SENT+1]={name=name,values={}} end,WriteString=function(v) table.insert(SENT[#SENT].values,v) end,
 WriteUInt=function(v) table.insert(SENT[#SENT].values,v) end,SendToServer=function() SENT[#SENT].sent=true end}
notification={AddLegacy=function(text) NOTIFIED[#NOTIFIED+1]=text end}
util={NetworkStringToID=function() return 7 end,TableToJSON=function(t) return 'request='..tostring(t.request) end}
IsValid=function(e) return e~=nil end istable=function(v) return type(v)=='table' end isstring=function(v) return type(v)=='string' end
RealTime=function() return 0 end
mmdhl.WithSpawnDefaults=function(_,options) local copy={} for k,v in pairs(options or {}) do copy[k]=v end return copy end
ID=string.rep('a',64)
''')
attach(client)
# The client's installation.lua, so anything it offers the request functions is there.
client.execute(r'''
local saved={net=net,util=util}
unpack=table.unpack include=function() return {} end
net={Receive=function() end} util={} hook={Add=function() end,Run=function() end} concommand={Add=function() end}
''' + source('installation.lua') + r'''
net,util=saved.net,saved.util
mmdhl.serverInstallation={schema=1,realm='server',features={core=false,physics=false},issues={{code='missing',component='runtime',message='x',feature='core'}}}
''')
client.execute(definition(client, source('client.lua'), 'function mmdhl.Action('))
client.execute('local L,library=mmdhl.L,mmdhl.library local function validId(id) return isstring(id) and #id==64 and not id:find("[^a-f0-9]") end '
               + definition(client, source('library.lua'), 'function mmdhl.RequestSpawn('))
client.execute(r'''
assert(mmdhl.RequestSpawn(ID,{role='ragdoll'})==true,'the spawn request was refused by the reported server status')
assert(#SENT==1 and SENT[1].name=='mmdhl_action' and SENT[1].values[1]=='spawn' and SENT[1].values[2]==ID and SENT[1].sent,'the spawn request was not sent')
assert(#NOTIFIED==0,'a notification refused the request: '..tostring(NOTIFIED[1]))
mmdhl.Action('remove',nil,{EntIndex=function() return 42 end})
assert(#SENT==2 and SENT[2].values[1]=='remove' and SENT[2].values[3]==42 and SENT[2].sent,'an action was not sent')
''')

policy = lua_policy(root / 'addon/lua/mmdhl/native_policy.lua')


def dedicated():
    """A dedicated server (a Lua session of its own) with installation.lua loaded, as autorun
    loads it; its files (FILES) start empty."""
    server = LuaRuntime(unpack_returned_tuples=True)
    server.execute(r'''
SERVER=true CLIENT=false
unpack=table.unpack jit={arch='x64'}
math.Clamp=function(v,lo,hi) return math.min(math.max(v,lo),hi) end
istable=function(v) return type(v)=='table' end
IsValid=function(e) return e~=nil end
mmdhl={}
''')
    attach(server)
    def convert(x):
        if isinstance(x, dict): return server.table_from({k: convert(v) for k, v in x.items()})
        if isinstance(x, list): return server.table_from([convert(v) for v in x])
        return x
    server.globals().POLICY = convert(policy)
    server.execute(PRELUDE)
    server.execute(source('installation.lua'))
    return server


PRELUDE = r'''
include=function(path) if path=='mmdhl/native_policy.lua' then return POLICY end return {} end
isstring=function(v) return type(v)=='string' end
-- The server's files (none at first), by their bytes. A Model Hotloader binary carries its build ID among them.
FILES={}
file={Exists=function(path,search) return FILES[search..'/'..path]~=nil end,
 Open=function(path,_,search) local bytes=FILES[search..'/'..path] return {Size=function() return #bytes end,Read=function() return bytes end,Close=function() end} end,
 Read=function() end,CreateDir=function() end,Write=function() end}
system={IsWindows=function() return true end,IsLinux=function() return false end} game={IsDedicated=function() return true end}
-- As in GMod: net.Start refuses a name util.AddNetworkString never pooled.
POOLED={}
util={AddNetworkString=function(name) POOLED[name]=true end,TableToJSON=function() return '{}' end,SHA256=function(bytes) return string.rep('5',64) end,
 JSONToTable=function(s) local n=tostring(s):match('"request":(%d+)') if n then return {request=tonumber(n)} end end}
HANDLERS={} READ={} SENT={}
net={Receive=function(name,f) HANDLERS[name]=f end,ReadString=function() return table.remove(READ,1) end,ReadUInt=function() return table.remove(READ,1) end,
 Start=function(name) if not POOLED[name] then error('Calling net.Start with unpooled message name "'..name..'"',2) end SENT[#SENT+1]={name=name,values={}} end,WriteString=function(v) table.insert(SENT[#SENT].values,v) end,
 WriteUInt=function(v) table.insert(SENT[#SENT].values,v) end,Send=function(p) SENT[#SENT].to=p end,Broadcast=function() SENT[#SENT].broadcast=true end}
concommand={Add=function() end} hook={Add=function() end,Run=function() end} timer={Create=function() end,Remove=function() end}
-- What happened, in order: the server console's lines and each require of the native module,
-- which Windows refuses with REQUIRE_ERROR or which leaves NATIVE as mmdhl_native.
EVENTS={} REQUIRED=0 REQUIRE_ERROR=nil NATIVE=nil
NOW=100 CurTime=function() return NOW end MsgN=function(text) EVENTS[#EVENTS+1]='console: '..text end
function require(name) assert(name=='mmdhl') REQUIRED=REQUIRED+1 EVENTS[#EVENTS+1]='require' if REQUIRE_ERROR then error(REQUIRE_ERROR,0) end mmdhl_native=NATIVE end
function INDEX(text) for i,v in ipairs(EVENTS) do if v==text then return i end end end
PLAYER={}
'''

# A dedicated server without its native files: nothing to load, require is never called.
server = dedicated()
server.execute(r'''
assert(mmdhl.CheckInstallation()==false and mmdhl.loadError,'the server without native files loaded')
assert(REQUIRED==0,'the server required a module it does not have')
local function request(channel,...) READ={...} SENT={} HANDLERS[channel](0,PLAYER) local out={} for _,m in ipairs(SENT) do out[m.name]=m end return out end
local expected='Server: Native runtime: Missing lua/bin/gmsv_mmdhl_win64.dll. Reinstall the native package and restart. The server administrator must resolve this.'
local sent=request('mmdhl_action','spawn',string.rep('a',64),0,'{"request":7,"role":"ragdoll"}')
local reply=sent.mmdhl_spawn_status
assert(reply and reply.to==PLAYER,'a character spawn got no answer')
assert(reply.values[1]==7 and reply.values[2]=='error' and reply.values[4]==0,'the answer is not this request\'s failure')
assert(mmdhl.Localize(reply.values[3])==expected,mmdhl.Localize(reply.values[3]))
assert(sent.mmdhl_install_status and sent.mmdhl_install_status.to==PLAYER,'the server status was not sent with it')
-- Within five seconds: the spawn is still answered; the status is not sent again.
NOW=102 sent=request('mmdhl_prop_action','spawn',string.rep('b',64),0,'{"request":9}')
reply=sent.mmdhl_prop_status
assert(reply and reply.values[1]==9 and reply.values[2]=='error' and mmdhl.Localize(reply.values[3])==expected,'a prop spawn got no answer')
assert(not sent.mmdhl_install_status,'the status was sent again within five seconds')
-- Other requests get the status only, at most every five seconds.
NOW=106 sent=request('mmdhl_action','remove','',3,'')
assert(not sent.mmdhl_spawn_status and sent.mmdhl_install_status,'a removal was answered as a spawn, or the status was not sent')
NOW=107 sent=request('mmdhl_share','spawn')
assert(next(sent)==nil,'another channel was answered within five seconds')
-- The physics editor's request is answered for itself, within the five seconds too; closing needs no answer.
NOW=108 sent=request('mmdhl_physics',1,77,'open',{},0)
reply=sent.mmdhl_physics_status
assert(reply and reply.to==PLAYER and reply.values[1]==77 and reply.values[2]=='error' and reply.values[4]==0 and reply.values[5]==0,'the physics editor waited for its timeout')
assert(mmdhl.Localize(reply.values[3])==mmdhl.Localize(mmdhl.L'physics_editor.error.server_core'),mmdhl.Localize(reply.values[3]))
NOW=120 sent=request('mmdhl_physics',1,78,'close',{},0) assert(not sent.mmdhl_physics_status,'closing the editor was answered')
''')

# A server module this addon does not know (other bytes) without its runtime: the module is
# still required (its warning reaches the console first), and Windows refuses it. The answer
# names the missing runtime, the problem that explains the failure, not the module's warning
# listed before it.
server.execute(r'''
FILES['MOD/lua/bin/gmsv_mmdhl_win64.dll']='modified module'
EVENTS={} REQUIRE_ERROR='The specified module could not be found.'
assert(mmdhl.CheckInstallation()==false,'the server without its runtime loaded')
assert(REQUIRED==1,'the server did not try the module file it has')
local s=mmdhl.GetInstallationStatus()
local issues=s.issues
assert(#issues==3 and issues[1].code=='damaged_or_unrecognized' and issues[1].component=='server' and issues[1].warning==true and issues[1].identity==true,'expected the module\'s warning first')
assert(issues[2].code=='missing' and issues[2].component=='runtime' and issues[2].warning==nil and issues[2].cause==true,'the missing runtime did not become the problem')
assert(issues[3].code=='loader_failed' and issues[3].warning==nil and mmdhl.Localize(issues[3].message):find('The specified module could not be found.',1,true))
assert(not s.features.core and not s.features.physics and s.blocked and mmdhl.native==nil)
local warned=INDEX('console: [Model Hotloader / server] '..mmdhl.Localize(issues[1].message))
assert(warned and warned<INDEX('require'),'the module\'s warning did not reach the console before require')
READ={'spawn',string.rep('a',64),0,'{"request":8}'} SENT={} NOW=200 HANDLERS.mmdhl_action(0,PLAYER)
local text=mmdhl.Localize(SENT[1].values[3])
assert(SENT[1].name=='mmdhl_spawn_status' and text=='Server: Native runtime: Missing mmdhl_runtime_win64.dll. Reinstall the native package and restart. The server administrator must resolve this.',text)
''')

# A consistent build this addon does not know (a fresh GitHub Actions build: other hashes, one
# build ID in the module and the runtime) on a server that starts with it: it loads with every
# server feature and warnings only, which reach the server console before require. The pooled
# handlers stay silent: server.lua answers the requests.
dedicated().execute(r'''
local BUILD='feedfacecafe-20261009T120000Z'
FILES['MOD/lua/bin/gmsv_mmdhl_win64.dll']='server module '..BUILD
FILES['BASE_PATH/mmdhl_runtime_win64.dll']='runtime '..BUILD
local function loaded(path,bytes) return {release='2.3.0',build=BUILD,installApi=1,api=1,platform='win64',size=#bytes,sha256=util.SHA256(bytes),path=path,expectedPath=path} end
local INFO={module=loaded('D:\\GMod Server\\garrysmod\\lua\\bin\\gmsv_mmdhl_win64.dll',FILES['MOD/lua/bin/gmsv_mmdhl_win64.dll']),runtime=loaded('D:\\GMod Server\\mmdhl_runtime_win64.dll',FILES['BASE_PATH/mmdhl_runtime_win64.dll'])}
CHECKS=0
NATIVE={GetInstallationInfo=function() return INFO end,ConfigureCompatibility=function() return {configured=true} end,
 CheckCompatibility=function() CHECKS=CHECKS+1 return {ready=true,pending=false,issues={},libraries={}} end}
assert(mmdhl.CheckInstallation()==true,'a server module this addon does not know did not load')
local s=mmdhl.GetInstallationStatus()
assert(REQUIRED==1 and mmdhl.native~=nil and mmdhl.loadError==nil and CHECKS==1,'the module was not used')
assert(s.features.core and s.features.physics and not s.blocked and s.unverified and s.installed==nil and s.update==nil)
assert(#s.issues==2 and s.issues[1].component=='server' and s.issues[2].component=='runtime','expected one warning per file, no loaded_mismatch and no mixed builds')
assert(s.files.server.actual.build==BUILD and s.files.runtime.actual.build==BUILD,'the build IDs were not read: nothing told the builds agree')
for _,v in ipairs(s.issues) do
 assert(v.code=='damaged_or_unrecognized' and v.warning==true and v.identity==true,v.code)
 local printed=INDEX('console: [Model Hotloader / server] '..mmdhl.Localize(v.message))
 assert(printed and printed<INDEX('require'),'a warning did not reach the server console before require')
end
assert(mmdhl.FeatureAvailable('physics')==true and mmdhl.FeatureAvailable('core')==true)
assert(mmdhl.NativeReleaseAtLeast('2.3.0') and mmdhl.NativeReleaseAtLeast(POLICY.recommended) and not mmdhl.NativeReleaseAtLeast('2.4.0'),'the loaded module\'s own label does not decide')
for _,channel in ipairs({'mmdhl_action','mmdhl_prop_action','mmdhl_physics','mmdhl_share'}) do
 NOW=NOW+10 READ={'spawn',string.rep('a',64),0,'{"request":5}'} SENT={}
 HANDLERS[channel](0,PLAYER)
 assert(#SENT==0 and #READ==4,channel..' was answered by the installation check of a server whose module loaded')
end
''')

# A server whose native module loaded but whose physics is unavailable refuses the spawn itself.
server.execute('local L=mmdhl.L ' + definition(server, source('server.lua'), 'function mmdhl.Spawn('))
server.execute(r'''
mmdhl.FeatureAvailable=function(feature) assert(feature=='physics') return false,'Game interface slot replaced or outside expected library' end
local result,why
mmdhl.Spawn(PLAYER,string.rep('a',64),{},function(ent,err) result,why=ent,err end)
assert(result==nil and mmdhl.Localize(why)=='Server: Native character physics: Game interface slot replaced or outside expected library The server administrator must resolve this.',mmdhl.Localize(why))
''')
print('PASS: the client sends spawns and actions whatever the server reported; a server without native files answers spawns at once with its problem; '
      'a server module this addon does not know is always tried (a missing runtime, not its warning, explains a failure; a consistent build loads '
      'and the pooled handlers stay silent); a physics refusal is worded as the server\'s')
