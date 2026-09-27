"""Run the actual GLua transfer state machine across simulated delayed peers.
Native compression/integrity have CTest coverage; this exercises flow control,
cancellation/retry, stale packets, permissions, caching and upload direction.
"""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
root=Path(__file__).resolve().parents[1]
lua=LuaRuntime(unpack_returned_tuples=True)
attach(lua)
lua.globals().sharing_code=(root/'addon/lua/mmdhl/sharing.lua').read_text(encoding='utf-8')
lua.execute(r'''
local encoded,serial={},0
local function encode(t) serial=serial+1 local s='json'..serial encoded[s]=t return s end
local asset=string.rep('a',64)
local paths={'assets/'..asset..'/manifest.json','assets/'..asset..'/model.bin','textures/'..string.rep('b',64)..'.png'}
local source={[paths[1]]=string.rep('m',1234),[paths[2]]=string.rep('x',1024*1024+123),[paths[3]]=string.rep('p',200123)}
local function scenario(upload,cancelFirst,corrupt,deriveFails)
 local paths={paths[1],paths[2],paths[3]}
 local original=source local source={} for p,b in pairs(original)do source[p]=b end
 local material='assets/'..asset..'/materials-v5.gma'
 if deriveFails~=nil then table.insert(paths,2,material);source[material]=string.rep('g',400123)end
 local now,mail,peers,peak=0,{},{},0
 local peer={IsAdmin=function()return true end,SteamID64=function()return 'test-admin'end}
 local function environment(server)
  local e={SERVER=server,CLIENT=not server,FCVAR_NONE=0,_hooks={},_files={},_exports={},_imports={},_pending=0,_peak=0,_seq=0}
  setmetatable(e,{__index=_G})
  e.RealTime=function()return now end
  e.IsValid=function(p)return p~=nil end
  e.istable=function(t)return type(t)=='table'end e.isnumber=function(t)return type(t)=='number'end e.isstring=function(t)return type(t)=='string'end
  e.LocalPlayer=function()return peer end
  e.game={SinglePlayer=function()return false end}
  e.gamemode={Call=function()return true end}
  e.file={Read=function()end,Exists=function()return false end,CreateDir=function()end,Write=function()end,
   Size=function(path)local bytes=e._files[(path:gsub('^mmd_hotloader/',''))] return bytes and #bytes or -1 end}
  e.util={AddNetworkString=function()end,Compress=function(s)return s end,Decompress=function(s)return s end,TableToJSON=encode,JSONToTable=function(s)return encoded[s]end}
  e.hook={Add=function(n,id,f)e._hooks[n]=e._hooks[n] or {};e._hooks[n][id]=f end,Run=function()end}
  e.concommand={Add=function()end}
  -- Server text is a token; Localize renders it as the receiving client would.
  e.mmdhl={L=server and mmdhl.I18n.Token or mmdhl.L,Localize=mmdhl.Localize,I18n=mmdhl.I18n,actorRegistrations={},Decode=function(s,err)return encoded[s],err end,LoadAsset=function(id,cb)cb({name='Fixture'})end,PublishActor=function()end,IsCurrentRig=function()return true end}
  local native={} e.mmdhl.native=native
  function native.GetSharedManifest()
   local files,total={},0
   for i,p in ipairs(paths)do files[i]={path=p,size=#source[p],sha256=string.rep(tostring(i),64)};if p==material then files[i].derive='source_materials_v5'end;total=total+#source[p]end
   return encode({version=1,asset=asset,name='Fixture',files=files,size=total})
  end
  function native.SharedFileMatches(path,size,digest)return e._files[path]==source[path] and #e._files[path]==size end
  function native.StartSharedExport(path,size,digest)
   assert(e._files[path] and #e._files[path]==size)
   e._seq=e._seq+1 e._exports[e._seq]={bytes=string.rep('h',80)..e._files[path],ready=now+.04}
   return e._seq
  end
  function native.StartSharedMaterialBuild(id)
   assert(id==asset);e._seq=e._seq+1;e._exports[e._seq]={bytes=source[material],ready=now+.05,material=true};return e._seq
  end
  function native.GetSharedExportSize(id)
   local x=assert(e._exports[id]);if now<x.ready then return 0 end
   if x.material and not deriveFails then e._files[material]=source[material]end
   return #x.bytes
  end
  function native.ReadSharedExport(id,offset,count)assert(count<=49152);return e._exports[id].bytes:sub(offset+1,offset+count)end
  function native.ReleaseSharedExport(id)e._exports[id]=nil end
  function native.BeginSharedFile(path,size,digest,wire)
   e._seq=e._seq+1 e._imports[e._seq]={path=path,size=size,digest=digest,wire=wire,bytes=''}return e._seq
  end
  function native.AppendSharedChunk(id,offset,bytes)
   local x=assert(e._imports[id]);assert(offset==#x.bytes and #bytes<=49152 and #x.bytes+#bytes<=x.wire);x.bytes=x.bytes..bytes;return true
  end
  function native.CommitSharedFile(id)local x=e._imports[id];assert(#x.bytes==x.wire);x.ready=now+.03;return true end
  function native.PollSharedCommit(id)
   local x=assert(e._imports[id]);if now<x.ready then return false end
   local raw=x.bytes:sub(81)
   if raw~=source[x.path] or #raw~=x.size then return nil,'SHA256 mismatch' end
   e._files[x.path]=raw e._imports[id]=nil return true
  end
  function native.CancelSharedFile(id)e._imports[id]=nil end
  local written,reading,at
  local net={} e.net=net
  function net.Start(name)written={name=name,fields={},size=20}end
  local function write(v)written.fields[#written.fields+1]=v end
  net.WriteString=function(v)write(v);written.size=written.size+#v end
  net.WriteUInt=function(v,b)write(v);written.size=written.size+math.ceil(b/8)end
  net.WriteData=function(v,n)assert(n==#v);write(v);written.size=written.size+n end
  local function pop()local v=reading[at];at=at+1;return v end
  net.ReadString=pop net.ReadUInt=pop net.ReadData=pop
  function net.Receive(name,f)e._receive=f end
  local corrupted=false
  local function send()
   if corrupt and server and not corrupted and written.fields[1]=='chunk' and written.fields[3]==2 then
    local n=#written.fields;local bytes=written.fields[n];written.fields[n]=bytes:sub(1,#bytes-1)..'z';corrupted=true
   end
   e._pending=e._pending+written.size;e._peak=math.max(e._peak,e._pending)
   mail[#mail+1]={time=now+.04,from=e,to=server and peers.client or peers.server,msg=written}
   written=nil
  end
  net.Send=send net.SendToServer=send net.Broadcast=function()end
  function e.deliver(m)reading=m.fields at=1;e._receive(m.size*8,server and peer or nil)end
  assert(load(sharing_code,'sharing','t',e))()
  if server then e.mmdhl.approved.assets[asset]={name='Fixture'}end
  return e
 end
 peers.server=environment(true);peers.client=environment(false)
 local sender=upload and peers.client or peers.server
 for p,b in pairs(source)do sender._files[p]=b end
 local receiver=upload and peers.server or peers.client
 local function step()
  now=now+.015
  for _,e in ipairs({peers.server,peers.client})do for _,fn in pairs(e._hooks.Think or {})do fn()end end
  local due={};for i=#mail,1,-1 do if mail[i].time<=now then table.insert(due,1,table.remove(mail,i))end end
  for _,m in ipairs(due)do m.from._pending=m.from._pending-m.msg.size;m.to.deliver(m.msg)end
 end
 local done,success=false,nil
 local function start()
  done=false success=nil
  if upload then assert(peers.client.mmdhl.PublishAsset(asset))
  else peers.client.mmdhl.RequestSharedRig(asset,function(ok)done=true success=ok end)end
 end
 start()
 local canceled=false
 for _=1,1000 do
  step()
  if cancelFirst and not canceled then
   local d=peers.client.mmdhl.GetTransferDiagnostics()
   if d and d.wireBytes>49152 then peers.client.mmdhl.CancelSharedTransfer();canceled=true;start()end
  end
  if upload then local d=peers.server.mmdhl.lastTransfer;if d and d.finished then done=true;success=true end end
  if done then break end
 end
 assert(done,'Transfer did not complete')
 if corrupt then assert(success==false and receiver._files[paths[2]]==nil,'Corrupt bytes were published')
 else
  assert(success,'Transfer failed')
  for p,b in pairs(source)do assert(receiver._files[p]==b,'Missing or changed file')end
  if cancelFirst then assert(canceled,'Cancel path was not exercised')end
 end
 if deriveFails~=nil then
  local d=peers.client.mmdhl.lastTransfer
  assert(d.generatedBytes==(deriveFails and 0 or #source[material]),'Derived archive path/fallback was not exercised')
 end
 for _=1,20 do step()end
 assert(not next(peers.server._exports) and not next(peers.client._exports),'Leaked outgoing file')
 assert(not next(peers.server._imports) and not next(peers.client._imports),'Leaked staging file')
 assert(peers.server._peak<160000 and peers.client._peak<160000,'Exceeded bounded reliable window')
 if not upload and not corrupt then
  start();for _=1,50 do step();if done then break end end
  assert(done and success)
  local d=peers.client.mmdhl.lastTransfer;assert(d.wireBytes==0 and d.rawBytes==0 and d.cachedBytes>1000000,'Cached model transferred again')
 end
end
scenario(false,false,false)
scenario(true,false,false)
scenario(false,true,false)
scenario(false,false,true)
scenario(false,false,false,false)
scenario(false,false,false,true)
''')
print('Delayed upload/download, window bounds, cache reuse, cancellation/retry, stale packets, corruption rejection and verified local generation/fallback passed')
