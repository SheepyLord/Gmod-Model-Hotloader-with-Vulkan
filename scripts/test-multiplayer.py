"""Owned dedicated server + two real clients: transport and actor rollout gates.

Start the three peers with multiplayer-session.ps1 first. This never launches
bots, changes firewall rules, or writes to the user's installed game cache.
"""
import argparse,json,pathlib,time
from gamectl import ROOT,execute,read

ASSET='bac307fdcefad2b6331a95e3b77644ad1562a1aba9983c0830e260326f2ee4c6'
BASE=ROOT/'validation/multiplayer'
REPORT=ROOT/'validation/actors-multiplayer.json'
report=read(REPORT) or {'asset':ASSET,'checks':{},'stages':{}}

def lua(peer,code,timeout=30):
    session=read(BASE/f'{peer}-session.json')
    assert session and session.get('ownedMultiplayer'), 'Only owned peers may be tested'
    response=execute('server' if peer=='server' else 'client',code,timeout,BASE/f'{peer}-session.json')
    if not response['ok']:raise RuntimeError(response.get('error'))
    return response.get('value')

def save():REPORT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
def check(name,condition,evidence=None):
    report['checks'][name]={'passed':bool(condition),'evidence':evidence};save()
    if not condition:raise AssertionError(name)
    print('PASS',name,flush=True)

def wait(peer,code,predicate,seconds=90):
    deadline=time.monotonic()+seconds;last=None
    while time.monotonic()<deadline:
        last=lua(peer,code)
        if predicate(last):return last
        time.sleep(1)
    raise TimeoutError(f'{peer}: {last}')

def transport():
    peers=lua('server',"local out={} for _,p in ipairs(player.GetHumans()) do out[#out+1]={id=p:EntIndex(),bot=p:IsBot(),admin=p:IsAdmin(),address=p:IPAddress()} end return out")
    check('two_real_clients',len(peers)==2 and all(not p['bot'] for p in peers),peers)
    check('separate_privileges',peers[0]['admin'] and not peers[1]['admin'])
    for peer in ('client1','client2'):
        environment=lua(peer,"return {singleplayer=game.SinglePlayer(),width=ScrW(),height=ScrH(),addons=#engine.GetAddons(),native=mmdhl.Decode(mmdhl.native.GetCapabilities())}")
        check(peer+'_environment',not environment['singleplayer'] and environment['width']==2560 and environment['height']==1440 and environment['addons']>0,environment)
    approved=wait('server',f"return mmdhl.approved.assets['{ASSET}']",bool)
    check('admin_publication',bool(approved),approved)
    denied=lua('client2',f"local ok,err=mmdhl.PublishAsset('{ASSET}'); return {{ok=ok,error=err}}")
    check('nonadmin_publication_denied',not denied['ok'],denied)
    lua('client2',f"MMDHLMPDownload=nil; mmdhl.RequestSharedRig('{ASSET}',function(ok,err) MMDHLMPDownload={{ok=ok,error=err}} end); return true")
    result=wait('client2','return MMDHLMPDownload',bool,300)
    check('verified_download',result['ok'],result)
    manifest=lua('server',f"return mmdhl.Decode(mmdhl.native.GetSharedManifest('{ASSET}','[]',false))")
    mismatch=lua('client2','local bad={} for _,f in ipairs(util.JSONToTable('+json.dumps(json.dumps(manifest['files']))+')) do local ok,err=mmdhl.native.SharedFileMatches(f.path,f.size,f.sha256) if not ok then bad[#bad+1]={path=f.path,error=err} end end return bad')
    check('download_hashes',len(mismatch)==0,{'files':len(manifest['files']),'bytes':manifest['size'],'mismatches':mismatch})

PROBE="""
local out={actors={},scene=mmdhl.remoteSceneDiagnostics,sceneError=mmdhl.sceneError,renderError=mmdhl.renderError}
for _,e in ipairs(mmdhl.Entities()) do
 local h=mmdhl.GetInstance(e) local d=h>0 and mmdhl.Decode(mmdhl.native.GetDiagnostics(h,false)) or nil
 out.actors[#out.actors+1]={entity=e:EntIndex(),class=e:GetClass(),model=e:GetModel(),instance=h,role=e:GetNW2String('MMDHLRole'),rig=e:GetNW2String('MMDHLRig'),error=e.MMDHLAttachError,materials=#e:GetMaterials(),groups=e:GetNumBodyGroups(),sequences=e:GetSequenceCount(),bones=e:GetBoneCount(),physics=e:GetPhysicsObjectCount(),diagnostics=d,lod=e.MMDPhysicsLOD,collision=mmdhl.GetSecondaryCollisionMode(e)}
end return out
"""

def actors():
    lua('server',"MMDHLMPActors={} for _,p in ipairs(player.GetHumans()) do p:GodEnable() p:SetPos(Vector(850,200+(p:EntIndex()-1)*100,-140)) p:SetEyeAngles(Angle(10,180,0)) end return true")
    for role,y in [('ragdoll',100),('citizen',250),('combine',-150)]:
        lua('server',f"mmdhl.Spawn(Entity(1),'{ASSET}',{{role='{role}',backend='source',position={{620,{y},-138}},angles={{0,0,0}},frozen=true}},function(e,err) if IsValid(e) then e.MMDHLMPTest=true if e:IsNPC() then e:SetHealth(100000) end end MMDHLMPActors['{role}']={{entity=IsValid(e) and e:EntIndex() or 0,error=err}} end); return true")
        value=wait('server',f"return MMDHLMPActors['{role}']",bool)
        check(role+'_spawn',value['entity']>0,value)
    lua('client1',f"MMDHLMPPlayer=nil; return mmdhl.RequestSpawn('{ASSET}',{{role='player'}},function(state,message,index) if state~='loading' then MMDHLMPPlayer={{state=state,message=message,entity=index}} end end)")
    selected=wait('client1','return MMDHLMPPlayer',bool)
    check('player_menu_request',selected['state']=='ready',selected)
    for peer in ('client1','client2'):
        result=wait(peer,PROBE,lambda r:len(r['actors'])>=4 and all(e['instance']>0 and not e.get('error') for e in r['actors']),240)
        report['stages'][peer+'_actors']=result;save()
        check(peer+'_native_actors',{'player','npc_citizen','npc_combine_s','prop_ragdoll'}.issubset({e['class'] for e in result['actors']}),result)
    native=lua('server',"local p=Entity(1); local h=p:GetHands(); local ids={} for role,r in pairs(MMDHLMPActors) do local e=Entity(r.entity);ids[role]={class=e:GetClass(),physics=e:GetPhysicsObjectCount(),sequences=e:GetSequenceCount(),reference=e:LookupSequence('Reference'),referencef=e:LookupSequence('Referencef'),materials=#e:GetMaterials()} end return {actors=ids,playerModel=p:GetModel(),hands=IsValid(h) and h:GetModel()} ")
    check('native_ragdoll_18',native['actors']['ragdoll']['physics']==18,native)
    check('native_sequences',all(native['actors'][r]['reference']>=0 and native['actors'][r]['referencef']>=0 and native['actors'][r]['sequences']>20 for r in ('citizen','combine')))
    check('native_hands',native.get('hands','').startswith('models/mmd/'))

def snapshot():
    for peer in ('client1','client2'):report['stages'][peer+'_latest']=lua(peer,PROBE)
    save()

def appearance():
    rid=lua('server',"return MMDHLMPActors.ragdoll.entity")
    zero=lua('server',f"local e=Entity({rid}); for i,m in ipairs(mmdhl.GetMaterials(e)) do if m.defaultHidden then return i-1 end end")
    assert zero is not None
    lua('server',f"local e=Entity({rid}); MMDHLMPAppearance=mmdhl.CaptureMaterialState(e); e:SetSubMaterial(0,'models/debug/debugwhite'); e:SetColor(Color(120,180,220,255)); mmdhl.SetMaterialVisible(e,{zero},true); mmdhl.SetMorphWeight(e,0,.4); return true")
    query=f"local e=Entity({rid}); return {{override=e:GetSubMaterial(0),visible=mmdhl.IsMaterialVisible(e,{zero}),group=e:GetBodygroup({zero+1}),color=e:GetColor().r,morph=mmdhl.GetMorphWeight(e,0)}}"
    for peer in ('client1','client2'):
        value=wait(peer,query,lambda v:v['visible'] and v['override']=='models/debug/debugwhite' and v['color']==120 and abs(v['morph']-.4)<.01)
        check(peer+'_appearance_replication',True,value)
    # Exercise the actual client-to-server property route, including a denial.
    lua('server',f"hook.Add('MMDHLCanEdit','MMDHL.MPTestPermission',function(p,e) if p:EntIndex()==2 and e:EntIndex()=={rid} then return false end end); return true")
    lua('client2',f"return mmdhl.SetMaterialVisible(Entity({rid}),{zero},false)")
    time.sleep(.4)
    check('property_permission_denial',lua('server',f"return mmdhl.IsMaterialVisible(Entity({rid}),{zero})"))
    lua('server',"hook.Remove('MMDHLCanEdit','MMDHL.MPTestPermission'); return true")
    lua('client1',f"return mmdhl.SetMaterialVisible(Entity({rid}),{zero},false)")
    for peer in ('client1','client2'):
        wait(peer,query,lambda v:not v['visible'])
        check(peer+'_property_replication',True)
    lua('server',f"local e=Entity({rid});mmdhl.ApplyMaterialState(e,MMDHLMPAppearance);e:SetColor(color_white);mmdhl.SetMorphWeight(e,0,0);return true")

def settings():
    rid=lua('server',"return MMDHLMPActors.ragdoll.entity")
    names=['mmdhl_secondary_backend','mmdhl_secondary_collision','mmdhl_lod_enabled']
    saved={peer:lua(peer,'local out={} for _,n in ipairs('+ '{'+','.join(json.dumps(n) for n in names)+'}' +') do out[n]=GetConVar(n):GetString() end return out') for peer in ('client1','client2')}
    report['savedClientSettings']=saved;save()
    try:
        lua('client1',"GetConVar('mmdhl_secondary_backend'):SetString('reference');GetConVar('mmdhl_secondary_collision'):SetString('0');GetConVar('mmdhl_lod_enabled'):SetString('1');return true")
        lua('client2',"GetConVar('mmdhl_secondary_backend'):SetString('cpu_mt_v2');GetConVar('mmdhl_secondary_collision'):SetString('2');GetConVar('mmdhl_lod_enabled'):SetString('1');return true")
        lua('client1',f"hook.Add('CalcView','MMDHL.MPTestView',function() local e=Entity({rid});local target=e:GetPos()+Vector(0,0,35);local origin=target+Vector(5000,0,50); return {{origin=origin,angles=(target-origin):Angle(),fov=75,drawviewer=true}} end);return true")
        lua('client2',f"hook.Add('CalcView','MMDHL.MPTestView',function() local e=Entity({rid});local target=e:GetPos()+Vector(0,0,35);local origin=target+Vector(180,-80,35); return {{origin=origin,angles=(target-origin):Angle(),fov=75,drawviewer=true}} end);return true")
        query=f"local e=Entity({rid});local d=mmdhl.GetDiagnostics(e,false);return {{handle=mmdhl.GetInstance(e),backend=d.secondaryBackendRequested,collision=mmdhl.GetSecondaryCollisionMode(e),ticks=d.ticks,quality=d.quality,lod=e.MMDPhysicsLOD and {{suspended=e.MMDPhysicsLOD.suspended,reason=e.MMDPhysicsLOD.reason,distance=e.MMDPhysicsLOD.distance}}}}"
        far=wait('client1',query,lambda v:v.get('quality',{}).get('suspended') and v['backend']=='reference' and v['collision']==0)
        near=wait('client2',query,lambda v:v.get('quality',{}).get('suspended') is False and v['backend']=='cpu_mt_v2' and v['collision']==2)
        time.sleep(1)
        after=lua('client1',query)
        check('independent_client_physics',far['quality']['suspended'] and not near['quality']['suspended'],{'far':far,'near':near})
        check('suspended_client_stops_steps',after['ticks']==far['ticks'],{'before':far['ticks'],'after':after['ticks']})
    finally:
        for peer in ('client1','client2'):
            lua(peer,'hook.Remove("CalcView","MMDHL.MPTestView");for k,v in pairs(util.JSONToTable('+json.dumps(json.dumps(saved[peer]))+')) do GetConVar(k):SetString(v) end return true')
    resumed=wait('client1',query,lambda v:v.get('quality',{}).get('suspended') is False)
    check('resume_keeps_instance',resumed['handle']==far['handle'],resumed)

def contacts():
    rid=lua('server',"return MMDHLMPActors.ragdoll.entity")
    setup=f"""
local e=Entity({rid});MMDHLMPBodies={{}};MMDHLMPProps={{}}
for i=0,17 do local b=e:GetPhysicsObjectNum(i);MMDHLMPBodies[i]={{pos=b:GetPos(),ang=b:GetAngles()}};b:SetPos(b:GetPos()-Vector(0,0,30));b:Wake() end
for _,name in ipairs({{'ValveBiped.Bip01_L_Forearm','ValveBiped.Bip01_R_Forearm'}}) do
 local pos=e:GetBonePosition(e:LookupBone(name));local p=ents.Create('prop_physics');p:SetModel('models/hunter/blocks/cube025x025x025.mdl');p:SetPos(pos-Vector(0,0,30));p:Spawn();p.MMDHLMPTest=true;p:GetPhysicsObject():EnableMotion(false);p.MMDHLMPOrigin=p:GetPos();MMDHLMPProps[#MMDHLMPProps+1]=p
end
hook.Add('Tick','MMDHL.MPContacts',function() for i,p in ipairs(MMDHLMPProps) do if IsValid(p) then local b=p:GetPhysicsObject();b:SetPos(p.MMDHLMPOrigin+Vector(math.sin(CurTime()*2+i)*5,0,0));b:Wake() end end end)
return true
"""
    lua('server',setup);time.sleep(3)
    lua('client2',f"""
MMDHLMPContacts={{frames={{}},world=0,objects=0,finite=true}};local stop=RealTime()+10
hook.Add('PreRender','MMDHL.MPContactSamples',function()
 local e=Entity({rid});local d=mmdhl.GetDiagnostics(e,false);local t=MMDHLMPContacts
 t.world=math.max(t.world,d.worldContacts or 0);t.objects=math.max(t.objects,d.objectContacts or 0);t.finite=t.finite and d.sourceError=='' and d.asyncError=='';t.frames[#t.frames+1]=RealFrameTime()*1000
 if RealTime()>=stop then hook.Remove('PreRender','MMDHL.MPContactSamples');table.sort(t.frames);t.p95=t.frames[math.ceil(#t.frames*.95)];t.count=#t.frames;t.frames=nil;t.finished=true end
end);return true
""")
    try:
        result=wait('client2','return MMDHLMPContacts',lambda r:r and r.get('finished'),30)
        check('remote_world_and_prop_contacts',result['world']>0 and result['objects']>0 and result['finite'],result)
        scene=lua('client2','return mmdhl.remoteSceneDiagnostics')
        check('remote_scene_current',scene['pending']==0 and scene['age']<1,scene)
        native=lua('server',"local out={} for _,p in ipairs(MMDHLMPProps) do out[#out+1]={velocity=p:GetPhysicsObject():GetVelocity():Length(),motion=p:GetPhysicsObject():IsMotionEnabled()} end return out")
        check('one_way_contacts_keep_props_kinematic',all(not p['motion'] and p['velocity']<.01 for p in native),native)
    finally:
        lua('server',f"hook.Remove('Tick','MMDHL.MPContacts');for _,p in ipairs(MMDHLMPProps or {{}}) do if IsValid(p) then p:Remove() end end;local e=Entity({rid});for i,b in pairs(MMDHLMPBodies) do local p=e:GetPhysicsObjectNum(i);p:SetPos(b.pos);p:SetAngles(b.ang);p:Wake() end;return true")

def death():
    source=lua('server',"return {player=1,citizen=MMDHLMPActors.citizen.entity}")
    for peer in ('client1','client2'):
        lua(peer,"MMDHLMPDeath={};hook.Add('CreateClientsideRagdoll','MMDHL.MPDeath',function(s,c) if mmdhl.IsMMD(s) then timer.Simple(.5,function() MMDHLMPDeath[#MMDHLMPDeath+1]={source=s:EntIndex(),corpse=IsValid(c) and c:EntIndex(),mmd=IsValid(c) and mmdhl.IsMMD(c),handle=IsValid(c) and mmdhl.GetInstance(c)} end) end end);return true")
    lua('server',f"local e=Entity({source['citizen']});e:SetNPCState(NPC_STATE_IDLE);e:TakeDamage(200000,Entity(1),Entity(1));Entity(1):GodDisable();Entity(1):Kill();return true")
    def corpse_probe(peer):
        return lua(peer,"local out={} for _,e in ipairs(mmdhl.Entities()) do if e:GetNW2String('MMDHLRole')=='corpse' or e.MMDHLCorpse then out[#out+1]={entity=e:EntIndex(),class=e:GetClass(),handle=mmdhl.GetInstance(e),bones=e:GetBoneCount(),error=e.MMDHLAttachError} end end return {corpses=out,clientEvents=MMDHLMPDeath}")
    time.sleep(3)
    for peer in ('client1','client2'):
        result=corpse_probe(peer)
        check(peer+'_native_death_corpses',len(result['corpses'])>=2 and all(c['handle']>0 and c['bones']>=58 and not c.get('error') for c in result['corpses']),result)
    lua('server',"Entity(1):Spawn();Entity(1):GodEnable();return true")
    for peer in ('client1','client2'):
        value=wait(peer,"local p=Entity(1);return {alive=p:Alive(),mmd=mmdhl.IsMMD(p),handle=mmdhl.GetInstance(p),model=p:GetModel()}",lambda r:r['alive'] and r['mmd'] and r['handle']>0)
        check(peer+'_respawn',True,value)
        lua(peer,"hook.Remove('CreateClientsideRagdoll','MMDHL.MPDeath');return true")

def latejoin():
    query="""
local p=Entity(1);local out={alive=p:Alive(),player=mmdhl.GetInstance(p),generation=p:GetNW2Int('MMDHLGeneration'),corpses={},actors={}}
for _,e in ipairs(mmdhl.Entities()) do
 local value={entity=e:EntIndex(),class=e:GetClass(),handle=mmdhl.GetInstance(e),bones=e:GetBoneCount(),error=e.MMDHLAttachError,formerGeneration=e:GetNW2Int('MMDHLFormerGeneration',0)}
 out.actors[#out.actors+1]=value
 if e:GetNW2String('MMDHLRole')=='corpse' then out.corpses[#out.corpses+1]=value end
end return out
"""
    result=wait('client2',query,lambda r:r['alive'] and r['player']>0 and len(r['corpses'])>0 and all(a['handle']>0 and a['bones']>=58 and not a.get('error') for a in r['actors']))
    check('late_join_live_player_and_corpse',all(c['handle']!=result['player'] and c['formerGeneration']<result['generation'] for c in result['corpses']),result)
    catalog=lua('client2',"return {approved=table.Count(mmdhl.sharedAssets),registered=table.Count(mmdhl.actorRegistrations),admin=LocalPlayer():IsAdmin()}")
    check('late_join_catalog_and_permissions',catalog['approved']>0 and catalog['registered']>=3 and not catalog['admin'],catalog)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('phase',choices=['transport','actors','appearance','settings','contacts','death','latejoin','snapshot']);args=parser.parse_args()
    try:
        globals()[args.phase]();report['stages'][args.phase]={'passed':True,'time':time.time()};save()
    except Exception as error:
        report['stages'][args.phase]={'passed':False,'error':str(error),'time':time.time()};save();raise
