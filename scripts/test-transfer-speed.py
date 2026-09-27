"""Measure protocol-v2 sharing on an owned server and two real clients.
Run after a cold-cache connection. It records that first transfer, verifies all
files, then tests cached requests and simultaneous transfers with cancel/retry.
"""
import concurrent.futures,hashlib,json,pathlib,time
from gamectl import ROOT,execute,read
ASSET='bac307fdcefad2b6331a95e3b77644ad1562a1aba9983c0830e260326f2ee4c6'
BASE=ROOT/'validation/multiplayer'
REPORT=ROOT/'validation/transfer-speed.json'
report={'asset':ASSET,'checks':{},'protocol':2}
def call(peer,code):
 session=BASE/f'{peer}-session.json';assert read(session)['ownedMultiplayer']
 r=execute('server' if peer=='server' else 'client',code,30,session)
 if not r['ok']:raise RuntimeError(r.get('error'))
 return r.get('value')
def save():REPORT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
def check(name,ok,data=None):
 report['checks'][name]={'passed':bool(ok),'evidence':data};save();assert ok,name;print('PASS',name,flush=True)
def wait(peer,code,predicate,timeout=90):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  value=call(peer,code)
  if predicate(value):return value
  time.sleep(.15)
 raise TimeoutError((peer,value))
def begin(peer):
 return call(peer,f"MMDHLTransferMeasure=nil;local start=SysTime();mmdhl.RequestSharedRig('{ASSET}',function(ok,err) MMDHLTransferMeasure={{ok=ok,error=err,seconds=SysTime()-start,transfer=mmdhl.GetTransferDiagnostics()}} end);return true")
players=call('server',"local o={} for _,p in ipairs(player.GetHumans())do o[#o+1]={id=p:EntIndex(),bot=p:IsBot(),admin=p:IsAdmin(),address=p:IPAddress()}end return o")
check('two_real_clients',len(players)==2 and all(not p['bot'] for p in players) and sum(p['admin'] for p in players)==1,players)
history=call('client2','return mmdhl.transferHistory') or []
report['coldHistory']=history
cold=[h for h in history if h['finished'] and h['rawBytes']>0]
check('cold_download_and_local_material_build',bool(cold) and sum(x.get('generatedBytes',0) for x in cold)>100000000,{'seconds':max(x['sampledAt'] for x in cold)-min(x['started'] for x in cold),'wireBytes':sum(x['wireBytes'] for x in cold),'networkRawBytes':sum(x['rawBytes'] for x in cold),'generatedBytes':sum(x.get('generatedBytes',0) for x in cold)})
manifest=call('server',f"local rigs={{}} for id,r in pairs(mmdhl.approved.rigs)do if r.asset=='{ASSET}' then rigs[#rigs+1]=id end end return mmdhl.Decode(mmdhl.native.GetSharedManifest('{ASSET}',util.TableToJSON(rigs),false))")
code='local bad={} for _,f in ipairs(util.JSONToTable('+json.dumps(json.dumps(manifest['files']))+'))do local ok,err=mmdhl.native.SharedFileMatches(f.path,f.size,f.sha256);if not ok then bad[#bad+1]={path=f.path,error=err}end end return bad'
# The ragdoll carrier may not be requested by initial actor registration.
rigs=call('server',f"local o={{}} for id,r in pairs(mmdhl.approved.rigs)do if r.asset=='{ASSET}' then o[#o+1]=id end end return o")
for rig in rigs:
 call('client2',f"MMDHLRigCheck=nil;mmdhl.RequestSharedRig('{rig}',function(ok,err)MMDHLRigCheck={{ok=ok,error=err}}end);return true")
 result=wait('client2','return MMDHLRigCheck',bool);assert result['ok'],result
bad=call('client2',code);check('all_generated_and_received_sha256',not bad,{'files':len(manifest['files']),'bytes':manifest['size'],'bad':bad})
begin('client2');result=wait('client2','return MMDHLTransferMeasure',bool)
check('cached_request_sends_no_payload',result['ok'] and result['transfer']['wireBytes']==0,result)
# Move just one known PNG from each disposable cache. Never touch the user cache.
item=max((x for x in manifest['files'] if x['path'].startswith('textures/')),key=lambda x:x['size'])
for peer in ('client1','client2'):
 cache=pathlib.Path(read(BASE/f'{peer}-session.json')['cache']).resolve()
 src=(cache/item['path']).resolve();dst=(ROOT/'validation/transfer-concurrency-hold'/peer/item['path']).resolve()
 assert cache.is_relative_to(BASE.resolve()) and src.is_relative_to(cache) and dst.is_relative_to((ROOT/'validation').resolve())
 assert not dst.exists() and hashlib.sha256(src.read_bytes()).hexdigest()==item['sha256']
 dst.parent.mkdir(parents=True,exist_ok=True);src.replace(dst)
with concurrent.futures.ThreadPoolExecutor(2) as pool:list(pool.map(begin,('client1','client2')))
partial=wait('client2','return mmdhl.GetTransferDiagnostics()',lambda d:d and not d['finished'] and d['wireBytes']>100000)
call('client2',"mmdhl.CancelSharedTransfer();return true");begin('client2')
report['cancelledAt']=partial
for peer in ('client1','client2'):
 result=wait(peer,'return MMDHLTransferMeasure',bool)
 check(peer+'_concurrent_retry',result['ok'],result)
 bad=call(peer,code);check(peer+'_intact_after_retry',not bad,bad)
check('peers_stay_connected',call('server','return #player.GetHumans()')==2)
call('client2',f"MMDHLTransferSpawn=nil;mmdhl.RequestSpawn('{ASSET}',{{role='ragdoll',frozen=true}},function(state,message,index)if state~='loading' then MMDHLTransferSpawn={{state=state,message=message,entity=index}}end end);return true")
spawn=wait('client2','return MMDHLTransferSpawn',bool);assert spawn['state']=='ready',spawn
ent=spawn['entity']
pose=wait('client2',f"local e=Entity({ent});return {{handle=mmdhl.GetInstance(e),bones=e:GetBoneCount(),error=e.MMDHLAttachError}}",lambda d:d['handle']>0 and d['bones']>=58 and not d.get('error'))
native=call('server',f"local e=Entity({ent});return {{class=e:GetClass(),physics=e:GetPhysicsObjectCount()}}")
check('downloaded_model_spawns',native['class']=='prop_ragdoll' and native['physics']==18,{'native':native,'client':pose})
call('server',f"local e=Entity({ent});if IsValid(e) and mmdhl.IsMMD(e) then e:Remove() end return true")
report['finished']=time.time();save()
