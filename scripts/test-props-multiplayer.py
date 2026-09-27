"""Static props on an owned dedicated server with two real clients.

Start the peers first (server, then client1, then client2) with
multiplayer-session.ps1; client1 joins first and is the administrator.
Client1 imports a prop and spawns it: its first placement shares the prop,
the server validates and approves it, and client2 downloads and draws it.
"""
import argparse,json,pathlib,time
from gamectl import ROOT,execute,read

BASE=ROOT/'validation/multiplayer'
REPORT=ROOT/'validation/props-multiplayer.json'
report={'checks':{},'started':time.strftime('%Y-%m-%dT%H:%M:%S')}

def lua(peer,code,timeout=30):
    session=read(BASE/f'{peer}-session.json')
    assert session and session.get('ownedMultiplayer'),'Only owned peers may be tested'
    response=execute('server' if peer=='server' else 'client',code,timeout,BASE/f'{peer}-session.json')
    if not response['ok']:raise RuntimeError(response.get('error'))
    return response.get('value')
def save():REPORT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
def check(name,condition,evidence=None):
    report['checks'][name]={'passed':bool(condition),'evidence':evidence};save()
    if not condition:raise AssertionError(f'{name}: {evidence}')
    print('PASS',name,flush=True)
def wait(peer,code,predicate,seconds=120):
    deadline=time.monotonic()+seconds;last=None
    while time.monotonic()<deadline:
        last=lua(peer,code)
        if predicate(last):return last
        time.sleep(1)
    raise TimeoutError(f'{peer}: {last}')

parser=argparse.ArgumentParser()
parser.add_argument('source',help='Model file to import on client1')
args=parser.parse_args()
source=str(pathlib.Path(args.source).resolve())

humans=lua('server',"local out={} for _,p in ipairs(player.GetHumans()) do out[#out+1]={index=p:EntIndex(),admin=p:IsAdmin(),bot=p:IsBot()} end return out")
check('two_real_clients',len(humans)==2 and not any(h['bot'] for h in humans),humans)
check('client1_admin_client2_player',humans[0]['admin'] and not humans[1]['admin'],humans)

started=time.monotonic()
lua('client1',"local L=mmdhl.library return L.StartImport(mmdhl.native.BeginImport([==["+source+"]==],util.TableToJSON(mmdhl.props.ImportOptions())),nil,'static')")
status=wait('client1',"local L=mmdhl.library return {job=L.job~=nil,asset=L.lastImported,kind=L.lastImportedKind,status=L.status}",lambda v:not v['job'],180)
asset=status['asset']
check('client1_import',status['kind']=='static' and asset and len(asset)==64,status)
report['importSeconds']=round(time.monotonic()-started,2)

# Aim client1 at open ground in front of it, then place through the normal request.
lua('server',f"local p=Entity({humans[0]['index']}) p:SetPos(Vector(704,300,-143)) p:SetEyeAngles(Angle(35,90,0)) return true")
time.sleep(1)
started=time.monotonic()
lua('client1',"_G.MMDHLPropEvents={} local ok,err=mmdhl.props.RequestSpawn('"+asset+"',{scale=4},function(state,msg,index) table.insert(_G.MMDHLPropEvents,{state=state,msg=msg,index=index}) end) return {ok=ok,err=err}")
events=wait('client1',"return _G.MMDHLPropEvents",lambda v:any(e['state'] in ('ready','error') for e in (v or [])),240)
report['shareAndSpawnSeconds']=round(time.monotonic()-started,2)
check('admin_first_spawn_shares_and_places',any(e['state']=='ready' for e in events),events)

server=lua('server',"local P=mmdhl.props local id='"+asset+"' local out={approved=mmdhl.approved.props[id],has=P.native.PropHas(id),props={}} for _,e in ipairs(ents.FindByClass('mmdhl_prop')) do if e:GetAssetID()==id then local ph=e:GetPhysicsObject() out.props[#out.props+1]={scale=e:GetPropScale(),valid=IsValid(ph),mass=IsValid(ph) and ph:GetMass(),convexes=IsValid(ph) and #(ph:GetMeshConvexes() or {})} end end return out")
check('server_approved_and_cached',server['approved'] is not None and server['has'],server)
check('server_prop_physics',len(server['props'])==1 and server['props'][0]['valid'] and server['props'][0]['scale']==4,server['props'])

started=time.monotonic()
remote=wait('client2',"local P=mmdhl.props local id='"+asset+"' local c=P.RenderCache[id] return {has=P.native.PropHas(id),state=c and c.state,err=c and c.error,pieces=c and c.pieces and #c.pieces,approved=P.library.approved[id]~=nil,listed=P.library.entries[id]~=nil}",lambda v:v['state'] in ('ready','failed'),240)
report['client2DownloadSeconds']=round(time.monotonic()-started,2)
check('client2_downloads_and_draws',remote['state']=='ready' and remote['has'] and remote['pieces'],remote)
check('client2_lists_shared_prop',remote['approved'] and remote['listed'],remote)
trace=lua('client2',"local id='"+asset+"' for _,e in ipairs(ents.FindByClass('mmdhl_prop')) do if e:GetAssetID()==id then local c=e:WorldSpaceCenter() local tr=util.TraceLine({start=c+Vector(0,0,200),endpos=c}) return {hit=tr.Entity==e} end end return {hit=false}")
check('client2_traces_hit_prop',trace['hit'],trace)

# A non-administrator may place an approved prop.
lua('server',f"local p=Entity({humans[1]['index']}) p:SetPos(Vector(560,300,-143)) p:SetEyeAngles(Angle(35,90,0)) return true")
time.sleep(1)
lua('client2',"_G.MMDHLPropEvents={} local ok,err=mmdhl.props.RequestSpawn('"+asset+"',{scale=2},function(state,msg,index) table.insert(_G.MMDHLPropEvents,{state=state,msg=msg,index=index}) end) return {ok=ok,err=err}")
events=wait('client2',"return _G.MMDHLPropEvents",lambda v:any(e['state'] in ('ready','error') for e in (v or [])),120)
check('player_spawns_approved_prop',any(e['state']=='ready' for e in events),events)
report['finished']=time.strftime('%Y-%m-%dT%H:%M:%S');save()
print(json.dumps(report,indent=1))
