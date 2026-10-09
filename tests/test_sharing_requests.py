"""Requests between a simulated server and client running the addon's sharing.lua:
a damaged or partial approved.json keeps its usable approvals; requests are refused
before any file is hashed while the peer has a transfer; manifests are kept a
minute and computed at most once a second per peer and four times a second in
all; the catalog is rate limited and changes nothing; a late offer for a cancelled
request is declined and a refused request asked again; an upload is done only
after the server checked it, and the uploader waits for that verdict; withdrawn
approvals reach clients. Also the props pieces of those fixes (props/server.lua,
props/library.lua)."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition

root = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)
attach(lua)
lua.globals().sharing_code = (root / 'addon/lua/mmdhl/sharing.lua').read_text(encoding='utf-8')
lua.execute(r'''
NOW=0
local encoded,serial={},0
function encode(t) serial=serial+1 local s='json'..serial encoded[s]=t return s end
function decode(s) return encoded[s] end
function token(key) return mmdhl.I18n.Token(key) end
A,B,C,U=string.rep('a',64),string.rep('b',64),string.rep('c',64),string.rep('f',64)
RIG=string.rep('d',32)
SOURCE={['rigs/'..RIG..'/rig.json']=string.rep('r',500)}
for _,id in ipairs({A,B,C,U}) do SOURCE['assets/'..id..'/manifest.json']=string.rep('m',1000) SOURCE['assets/'..id..'/model.bin']=string.rep(id:sub(1,1),70000) end
-- A server and a client realm; messages arrive in order 40 ms after sending.
-- Messages to players other than the client's are kept in w.others.
function world(options)
 options=options or {}
 local w={mail={},others={},loads={}}
 w.admin={IsAdmin=function() return true end,SteamID64=function() return 'admin' end}
 local function environment(server)
  local e={SERVER=server,CLIENT=not server,_hooks={},_receive={},_files={},_written={},_exports={},_imports={},_seq=0,
   manifests={},registered={},unregistered={},refreshed=0,withdrawnProps={},withdrawCalls=0,sentRegistrations=0}
  setmetatable(e,{__index=_G})
  e.RealTime=function() return NOW end e.SysTime=function() return NOW end
  e.IsValid=function(v) return v~=nil and not v.gone end
  e.istable=function(v) return type(v)=='table' end e.isnumber=function(v) return type(v)=='number' end e.isstring=function(v) return type(v)=='string' end
  -- A dedicated server, or with options.listen the listen server its host's client shares a cache with.
  e.game={SinglePlayer=function() return false end,IsDedicated=function() return options.listen~=true end}
  e.LocalPlayer=function() return w.admin end
  e.GetConVar=function() return {GetInt=function() return 256 end} end
  e.file={Read=function(path) return (options.files or {})[path] end,Exists=function(path) return (options.exists or {})[path]==true end,CreateDir=function() end,
   Write=function(path,value) e._written[#e._written+1]={path=path,value=value} end,
   Size=function(path) local bytes=e._files[(path:gsub('^mmd_hotloader/',''))] return bytes and #bytes or -1 end}
  e.util={AddNetworkString=function() end,Compress=function(s) return s end,Decompress=function(s,limit) return s end,TableToJSON=encode,JSONToTable=decode}
  -- The progress banner is a client panel; its hook is not run here.
  e.hook={Add=function(event,name,f) e._hooks[event]=e._hooks[event] or {} e._hooks[event][name]=f end,Remove=function(event,name) if e._hooks[event] then e._hooks[event][name]=nil end end,
   Run=function(event,...) if event=='MMDHL.ShareProgress' then return end for _,f in pairs(e._hooks[event] or {}) do f(...) end end}
  e.concommand={Add=function() end}
  e.mmdhl={L=server and mmdhl.I18n.Token or mmdhl.L,Localize=mmdhl.Localize,I18n=mmdhl.I18n,actorRegistrations={},
   Decode=function(s,err) if s==nil then return nil,err end return decode(s) end,
   IsLoadableRig=function() return false end,
   RegisterActor=function() e.registered[#e.registered+1]=true end,
   SendActorRegistration=function() e.sentRegistrations=e.sentRegistrations+1 end,
   UnregisterAsset=function(id) e.unregistered[#e.unregistered+1]=id end,
   LoadAsset=function(id,cb) if w.holdLoads then w.loads[#w.loads+1]=cb else cb({name='Loaded '..id:sub(1,1)}) end end,
   library={Refresh=function() e.refreshed=e.refreshed+1 end},
   props={ValidID=function(id) return #id==64 end,CanUse=function() return true end,Load=function() end,SendCollision=function() end,RefreshRender=function() end,
    library={Refresh=function() end,Withdraw=function(ids) e.withdrawCalls=e.withdrawCalls+1 for _,id in ipairs(ids) do e.withdrawnProps[#e.withdrawnProps+1]=id end end}}}
  local native={} e.mmdhl.native=native
  function native.GetSharedManifest(asset,rigs)
   e.manifests[#e.manifests+1]=asset
   local paths={'assets/'..asset..'/manifest.json','assets/'..asset..'/model.bin'}
   for _,key in ipairs(decode(rigs) or {}) do paths[#paths+1]='rigs/'..key..'/rig.json' end
   local files={} for i,path in ipairs(paths) do files[i]={path=path,size=#e._files[path],sha256=string.rep(tostring(i),64)} end
   return encode({version=1,asset=asset,name='Model '..asset:sub(1,1),files=files})
  end
  function native.PropHas() return true end
  function native.PropSharedManifest(id)
   e.manifests[#e.manifests+1]='prop:'..id
   return encode({version=1,kind='static',asset=id,name='Prop',files={{path='static/assets/'..id..'.gmdl',size=100,sha256=id}}})
  end
  function native.SharedFileMatches(path,size) return e._files[path]~=nil and #e._files[path]==size and e._files[path]==SOURCE[path] end
  function native.StartSharedExport(path,size) assert(e._files[path] and #e._files[path]==size,'export of '..path) e._seq=e._seq+1 e._exports[e._seq]={bytes=string.rep('h',80)..e._files[path],ready=NOW+.04} return e._seq end
  function native.GetSharedExportSize(id) local x=assert(e._exports[id]) if NOW<x.ready then return 0 end return #x.bytes end
  function native.ReadSharedExport(id,offset,count) return e._exports[id].bytes:sub(offset+1,offset+count) end
  function native.ReleaseSharedExport(id) e._exports[id]=nil end
  function native.BeginSharedFile(path,size,digest,wire) e._seq=e._seq+1 e._imports[e._seq]={path=path,bytes=''} return e._seq end
  function native.AppendSharedChunk(id,offset,bytes) local x=assert(e._imports[id]) assert(offset==#x.bytes) x.bytes=x.bytes..bytes return true end
  function native.CommitSharedFile(id) e._imports[id].ready=NOW+.03 return true end
  function native.PollSharedCommit(id) local x=assert(e._imports[id]) if NOW<x.ready then return false end e._files[x.path]=x.bytes:sub(81) e._imports[id]=nil return true end
  function native.CancelSharedFile(id) e._imports[id]=nil end
  local written,reading,at
  e.net={}
  function e.net.Start(name) written={name=name,fields={}} end
  local function write(v) written.fields[#written.fields+1]=v end
  e.net.WriteString=write e.net.WriteUInt=write e.net.WriteData=write e.net.WriteBool=write
  local function pop() local v=reading[at] at=at+1 return v end
  e.net.ReadString=pop e.net.ReadUInt=pop e.net.ReadData=pop e.net.ReadBool=pop
  function e.net.Receive(name,f) e._receive[name]=f end
  local function post(to) w.mail[#w.mail+1]={time=NOW+.04,to=to,msg=written} written=nil end
  e.net.Send=function(p) if p==w.admin then post(w.client) else w.others[#w.others+1]={to=p,msg=written} written=nil end end
  e.net.SendToServer=function() post(w.server) end
  e.net.Broadcast=function() post(w.client) end
  e.received={}
  function e.deliver(msg,from) e.received[#e.received+1]=msg reading=msg.fields at=1 local f=e._receive[msg.name] if f then f(0,server and (from or w.admin) or nil) end end
  assert(load(sharing_code,'sharing','t',e))()
  return e
 end
 w.server=environment(true) w.client=environment(false)
 function w.step()
  NOW=NOW+.015
  for _,e in ipairs({w.server,w.client}) do for _,f in pairs(e._hooks.Think or {}) do f() end end
  local due={} for i=#w.mail,1,-1 do if w.mail[i].time<=NOW then table.insert(due,1,table.remove(w.mail,i)) end end
  for _,m in ipairs(due) do m.to.deliver(m.msg) end
 end
 function w.run(seconds) local stop=NOW+seconds while NOW<stop do w.step() end end
 -- Another player's request, answered at once; an offer is cancelled again.
 function w.ask(p,command,id)
  local before=#w.others
  w.server.deliver({name='mmdhl_share',fields={command,0,id}},p)
  if #w.others==before then return nil end
  local reply=w.others[#w.others].msg.fields
  if reply[1]=='offer' then w.server.deliver({name='mmdhl_share',fields={'cancel',reply[2],'Cancelled'}},p) end
  return reply
 end
 function w.publish() for path,bytes in pairs(SOURCE) do if not path:find(U,1,true) then w.server._files[path]=bytes end end end
 return w
end
''')

lua.execute(r'''
local function loaded(content) local w=world({files={['mmd_hotloader/approved.json']=content}}) return w.server.mmdhl.approved,w.server._written,w end
-- A partial file: missing collections are empty; nothing is dropped or copied.
local a,written,w=loaded(encode({version=1,assets={[A]={name='A'}}}))
assert(a.assets[A].name=='A' and next(a.rigs)==nil and next(a.props)==nil and #written==0,'a partial approved.json')
w.server._hooks.InitPostEntity['MMDHL.RestorePublishedActors']()
-- Wrong types and malformed entries are dropped; the original is kept beside it.
local raw=encode({version=1,assets={[A]={name='A'},bad={name='x'},[B]=7,[C]={name=5}},
 rigs={[RIG]={asset=A,role='citizen'},short={asset=A,role='citizen'},[string.rep('e',32)]={asset='zz',role='citizen'},[string.rep('9',32)]={asset=A,role='citizen',arms='x'}},props='none'})
a,written,w=loaded(raw)
assert(a.assets[A] and not a.assets.bad and not a.assets[B] and not a.assets[C],'malformed model approvals are kept')
assert(a.rigs[RIG] and not a.rigs.short and not a.rigs[string.rep('e',32)] and not a.rigs[string.rep('9',32)] and next(a.props)==nil,'malformed rig or prop approvals are kept')
assert(#written==1 and written[1].path=='mmd_hotloader/approved.invalid.json' and written[1].value==raw,'the damaged original is not kept')
w.server._hooks.InitPostEntity['MMDHL.RestorePublishedActors']()
a,written=loaded(encode({assets={[A]={name='A'}},props={[B]={name='B',triangles=12,bytes=4096},[C]={triangles=-1}}}))
assert(a.assets[A] and a.props[B] and not a.props[C] and #written==1,'a file without a version is kept, with a copy')
a,written=loaded('not json')
assert(next(a.assets)==nil and next(a.rigs)==nil and next(a.props)==nil and #written==1 and written[1].value=='not json')
a,written=loaded(nil)
assert(next(a.assets)==nil and #written==0,'a server without approved.json wrote a copy')
''')
print('PASS: a partial or damaged approved.json keeps its well-formed approvals and a copy of the original')

lua.execute(r'''
local w=world() w.publish()
local approved=w.server.mmdhl.approved
approved.assets[A]={name='A'} approved.assets[B]={name='B'} approved.assets[C]={name='C'} approved.rigs[RIG]={asset=A,role='citizen'}
local busy,throttled=token('share.error.busy'),token('share.error.throttled')
local p1,p2,p3,p4,p5,p6={},{},{},{},{},{}
local function count() return #w.server.manifests end
NOW=100
assert(w.ask(p1,'request',A)[1]=='offer' and count()==1)
-- A manifest is kept a minute: asking again, even at once, hashes nothing.
assert(w.ask(p1,'request',A)[1]=='offer' and count()==1,'a kept manifest was computed again')
assert(w.ask(p2,'request',A)[1]=='offer' and count()==1)
-- A new manifest waits a second per peer.
local r=w.ask(p1,'request',RIG) assert(r[1]=='error' and r[3]==throttled and count()==1,'a peer computed manifests back to back')
NOW=NOW+1.01 r=w.ask(p1,'request',RIG) assert(r[1]=='offer' and count()==2 and w.server.manifests[2]==A)
-- At most four manifests a second for everyone.
NOW=NOW+5
for i,p in ipairs({p2,p3,p4,p5}) do assert(w.ask(p,'request_prop',string.rep(tostring(i),64))[1]=='offer') end
r=w.ask(p6,'request_prop',string.rep('9',64)) assert(r[1]=='error' and r[3]==throttled and count()==6,'a fifth manifest in one second')
NOW=NOW+1.01 assert(w.ask(p6,'request_prop',string.rep('9',64))[1]=='offer' and count()==7)
-- Kept manifests end after a minute, with an approval change or a resized file.
NOW=NOW+61 assert(w.ask(p1,'request',A)[1]=='offer' and count()==8,'a manifest was kept over a minute')
NOW=NOW+1.01 assert(w.ask(p1,'request',A)[1]=='offer' and count()==8)
w.server.mmdhl.ApproveAsset(w.admin,C,'C')
NOW=NOW+1.01 assert(w.ask(p1,'request',A)[1]=='offer' and count()==9,'a manifest outlived an approval change')
w.server._files['assets/'..A..'/model.bin']=string.rep('a',70001)
NOW=NOW+1.01 assert(w.ask(p1,'request',A)[1]=='offer' and count()==10,'a manifest outlived a resized file')
-- A peer with a transfer is refused before anything is hashed.
NOW=NOW+1.01 w.server.deliver({name='mmdhl_share',fields={'request',0,B}},p1)
assert(w.others[#w.others].msg.fields[1]=='offer' and count()==11)
NOW=NOW+2 r=w.ask(p1,'request',C) assert(r[1]=='error' and r[3]==busy and count()==11,'a busy peer made the server hash a manifest')
r=w.ask(p1,'request_prop',string.rep('8',64)) assert(r[1]=='error' and r[3]==busy and count()==11,'a busy peer made the server hash a prop')
''')
print('PASS: requests are refused before hashing while busy; manifests are kept a minute and rate limited per peer and in all')

lua.execute(r'''
local w=world()
w.server.mmdhl.approved.assets[A]={name='A'} w.server.mmdhl.approved.assets[B]={name='B'}
w.server.mmdhl.actorRegistrations={['models/mmd/a.mdl']={rig={key=RIG,asset=A}}}
local p={}
local function catalog() w.others={} w.server.deliver({name='mmdhl_share',fields={'catalog',0}},p) local n=0 for _,m in ipairs(w.others) do if m.msg.name=='mmdhl_catalog' then n=n+1 end end return n end
NOW=500
assert(catalog()==2 and w.server.sentRegistrations==1,'the catalog lists approved models and actors')
assert(#w.server.registered==0 and #w.server._written==0,'the catalog registered actors or rewrote approved.json')
assert(catalog()==0 and #w.others==0 and w.server.sentRegistrations==1,'a second catalog within 5 s')
NOW=NOW+5 assert(catalog()==2 and w.server.sentRegistrations==2)
-- An approval broadcasts the catalog without registering anything again.
w.server.mmdhl.ApproveAsset(w.admin,C,'C')
assert(#w.server.registered==0 and #w.server._written==1 and w.server.sentRegistrations==3)
''')
print('PASS: the catalog is rate limited per peer and only sends')

lua.execute(r'''
local w=world() w.publish()
w.server.mmdhl.approved.assets[A]={name='A'} w.server.mmdhl.approved.assets[B]={name='B'}
local results={}
NOW=1000
w.client.mmdhl.RequestSharedRig(A,function(ok) results[#results+1]={'A',ok} end)
w.step()
-- Cancelled before the server's offer arrives: the request fails at once...
w.client.mmdhl.CancelSharedTransfer()
assert(#results==1 and results[1][2]==false)
-- ...and the late offer must not complete the next request with A's files.
w.client.mmdhl.RequestSharedRig(B,function(ok)
 results[#results+1]={'B',ok,w.client._files['assets/'..B..'/model.bin']==SOURCE['assets/'..B..'/model.bin'],w.client._files['assets/'..A..'/model.bin']~=nil}
end)
w.run(10)
assert(#results==2 and results[2][1]=='B' and results[2][2],'the request for B did not complete')
assert(results[2][3],"the request for B completed without B's files")
assert(not results[2][4],'the late offer for A was accepted')
assert(not next(w.server._exports) and not next(w.client._imports),'a declined offer left a transfer behind')
-- The server refused B while A's offer was pending (busy); the client asked again.
local refused,requests=0,0
for _,m in ipairs(w.client.received) do if m.name=='mmdhl_share' and m.fields[1]=='error' and m.fields[2]==0 and m.fields[3]==token('share.error.busy') then refused=refused+1 end end
for _,m in ipairs(w.server.received) do if m.fields[1]=='request' and m.fields[3]==B then requests=requests+1 end end
assert(refused==1 and requests==2,'the busy refusal was not retried ('..refused..' refusals, '..requests..' requests)')
''')
print('PASS: a late offer for a cancelled request is declined; a request refused while busy is asked again')

lua.execute(r'''
local w=world()
for path,bytes in pairs(SOURCE) do if path:find(U,1,true) then w.client._files[path]=bytes end end
w.holdLoads=true
local failed={} w.client.hook.Add('MMDHL.ShareUploadFailed','test',function(asset,why) failed[#failed+1]={asset,why} end)
NOW=2000
assert(w.client.mmdhl.PublishAsset(U))
w.run(5)
-- The server has the files and loads the model; the uploader waits for its verdict.
assert(w.server._files['assets/'..U..'/model.bin']==SOURCE['assets/'..U..'/model.bin'] and #w.loads==1)
local d=w.client.mmdhl.GetTransferDiagnostics()
assert(d.direction=='send' and not d.finished and d.phase=='checking','the upload was done before the server checked it')
-- Longer than a transfer may go without data: the uploader keeps waiting.
w.run(120)
d=w.client.mmdhl.GetTransferDiagnostics()
assert(d.phase=='checking' and not d.finished and #failed==0,'the uploader gave up while the server checked its upload')
-- Refused: the uploader hears why and releases its transfer.
w.loads[1](nil,'Broken model') w.run(1)
d=w.client.mmdhl.GetTransferDiagnostics()
assert(not d.finished and d.error=='Broken model' and #failed==1 and failed[1][1]==U and failed[1][2]=='Broken model','the refusal did not reach the uploader')
assert(not next(w.client._exports) and not w.server.mmdhl.approved.assets[U])
-- Accepted (the server already has the files): done comes after the approval.
w.loads={} assert(w.client.mmdhl.PublishAsset(U)) w.run(5)
assert(#w.loads==1 and w.client.mmdhl.GetTransferDiagnostics().phase=='checking')
w.loads[1]({name='Uploaded'}) w.run(1)
d=w.client.mmdhl.GetTransferDiagnostics()
assert(d.finished and w.server.mmdhl.approved.assets[U].name=='Uploaded' and #failed==1)
assert(w.client.mmdhl.shareStatus.text==mmdhl.L('share.progress.upload_approved',{name='Model f'}))
-- A server that never answers: the uploader stops waiting after three minutes.
w.loads={} assert(w.client.mmdhl.PublishAsset(U)) w.run(170)
assert(w.client.mmdhl.GetTransferDiagnostics().phase=='checking')
w.run(15)
d=w.client.mmdhl.GetTransferDiagnostics()
assert(not d.finished and d.error==mmdhl.L'share.error.timed_out' and #failed==2)
''')
print('PASS: an upload is done only after the server loaded and approved it; refusals reach the uploader')

lua.execute(r'''
local w=world()
w.server.mmdhl.approved.assets[A]={name='A'} w.server.mmdhl.approved.assets[B]={name='B'}
NOW=3000
w.server.mmdhl.SendSharedCatalog() w.run(1)
assert(w.client.mmdhl.sharedAssets[A] and w.client.mmdhl.sharedAssets[B])
local refreshed=w.client.refreshed
w.server.mmdhl.ForgetPublishedAssets({A}) w.run(1)
assert(not w.client.mmdhl.sharedAssets[A] and w.client.mmdhl.sharedAssets[B],'a withdrawn model stays in the client library')
assert(w.client.unregistered[1]==A and #w.client.unregistered==1 and w.client.refreshed>refreshed,'the client did not refresh its library')
-- Props, at most 128 ids a message.
local ids={} for i=1,300 do ids[i]=string.format('%064x',i) end
w.server.mmdhl.SendCatalogWithdrawal(true,ids) w.run(1)
assert(w.client.withdrawCalls==3 and #w.client.withdrawnProps==300 and w.client.withdrawnProps[300]==ids[300])
''')
print('PASS: withdrawn model and prop approvals reach clients')

lua.execute(r'''
-- A listen server shares its host's cache: approvals of models deleted from that
-- library (no manifest any more) are dropped at start, as in single player, so
-- their player models do not come back empty.
local approved=encode({version=1,assets={[A]={name='A'},[B]={name='B'}},rigs={[RIG]={asset=A,role='player'}}})
local w=world({listen=true,files={['mmd_hotloader/approved.json']=approved},exists={['mmd_hotloader/assets/'..B..'/manifest.json']=true}})
local a=w.server.mmdhl.approved
assert(a.assets[A]==nil and a.rigs[RIG]==nil and a.assets[B],'a listen server kept the approval of a deleted model')
assert(w.server.unregistered[1]==A and #w.server.unregistered==1,'the deleted model stays registered')
-- A dedicated server's published library is its own.
local d=world({files={['mmd_hotloader/approved.json']=approved}})
assert(d.server.mmdhl.approved.assets[A] and d.server.mmdhl.approved.rigs[RIG] and #d.server.unregistered==0,'a dedicated server dropped approvals')
''')
print('PASS: a listen server drops approvals of models deleted from the library of its host at start; a dedicated server keeps its own')

# The props side of those fixes.
props_server = (root / 'addon/lua/mmdhl/props/server.lua').read_text(encoding='utf-8')
props_library = (root / 'addon/lua/mmdhl/props/library.lua').read_text(encoding='utf-8')
props = LuaRuntime(unpack_returned_tuples=True)
props.execute(r'''
mmdhl={} P={} approved={props={}} SAVED=0 INVALIDATED=0 WITHDRAWN={}
file={CreateDir=function() end,Write=function() SAVED=SAVED+1 end} util={TableToJSON=function() return '' end}
mmdhl.InvalidateSharedManifests=function() INVALIDATED=INVALIDATED+1 end
mmdhl.SendCatalogWithdrawal=function(static,ids) WITHDRAWN[#WITHDRAWN+1]={static,ids} end
''')
props.execute(definition(props, props_server, 'local function save(') + definition(props, props_server, 'function P.ForgetApproved('))
props.execute(r'''
local X,Y=string.rep('1',64),string.rep('2',64)
approved.props[X]={name='x'} approved.props[Y]={name='y'}
P.ForgetApproved({X})
assert(approved.props[X]==nil and approved.props[Y] and SAVED==1 and INVALIDATED==1,'a prop withdrawal is saved and drops prepared manifests')
assert(#WITHDRAWN==1 and WITHDRAWN[1][1]==true and WITHDRAWN[1][2][1]==X,'a prop withdrawal is not sent to clients')
''')
client = LuaRuntime(unpack_returned_tuples=True)
client.execute(r'''
CLIENT=true NOW=0 RealTime=function() return NOW end
HOOKS={} hook={Add=function(event,name,f) HOOKS[event]=HOOKS[event] or {} HOOKS[event][name]=f end,Remove=function(event,name) if HOOKS[event] then HOOKS[event][name]=nil end end,
 Run=function(event,...) for _,f in pairs(HOOKS[event] or {}) do f(...) end end}
notification={AddLegacy=function() end} NOTIFY_GENERIC,NOTIFY_ERROR=0,1
util={NetworkStringToID=function() return 1 end} table.Copy=function(t) local c={} for k,v in pairs(t) do c[k]=v end return c end
LocalPlayer=function() return {IsAdmin=function() return true end} end game={SinglePlayer=function() return false end}
mmdhl={PublishProp=function() return true end}
P={spawnRequests={},ValidID=function(id) return #id==64 end,Action=function() end}
Lib={approved={},entries={}} REFRESHED=0 Lib.Refresh=function() REFRESHED=REFRESHED+1 end
''')
attach(client)
client.execute('local L=mmdhl.L ' + definition(client, props_library, 'local function status(') + definition(client, props_library, 'function P.RequestSpawn(')
               + definition(client, props_library, 'function P.CancelSpawnWait(') + definition(client, props_library, 'function Lib.Withdraw('))
client.execute(r'''
local X,Y=string.rep('1',64),string.rep('2',64)
Lib.approved[X]={} Lib.approved[Y]={}
Lib.Withdraw({X})
assert(Lib.approved[X]==nil and Lib.approved[Y] and REFRESHED==1,'withdrawn prop approvals stay in the library')
-- An administrator's first placement shares the prop; a refused upload ends the wait.
local states={}
assert(P.RequestSpawn(X,{},function(state,message) states[#states+1]={state,message} end))
assert(P.pendingSpawn and states[1][1]=='loading')
hook.Run('MMDHL.ShareUploadFailed',Y,'other') assert(#states==1)
hook.Run('MMDHL.ShareUploadFailed',X,'Rejected by the server')
assert(states[2][1]=='error' and states[2][2]=='Rejected by the server' and not P.pendingSpawn and not next(P.spawnRequests),'a refused upload left the placement waiting')
assert(next(HOOKS['MMDHL.PropApproved'])==nil and next(HOOKS['MMDHL.ShareUploadFailed'])==nil)
''')
print('PASS: prop withdrawals are saved and sent; clients drop them; a refused prop upload ends its placement wait')
