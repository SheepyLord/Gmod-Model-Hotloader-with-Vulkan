"""Owned-session LOD acceptance: identical eight full rigs, 10-second samples."""
import json, time
from gamectl import ROOT, execute, read

def lua(realm, code):
    reply=execute(realm,code,45)
    if not reply['ok']: raise RuntimeError(reply.get('error'))
    return reply.get('value')

session=read(ROOT/'validation/session.json')
from pathlib import Path
directory=Path(session['cache'])/'debug'/session['token']
assets=read(ROOT/'tests/v2-assets.json')
source=(ROOT/'tests/game/native-stress.lua').read_text(encoding='utf-8')
settings=['mmdhl_lod_enabled','mmdhl_secondary_backend','mmdhl_secondary_collision','mmdhl_debug_overlay']
saved=lua('client','local t={} for _,n in ipairs('+ '{'+','.join(json.dumps(n) for n in settings)+'}) do t[n]=GetConVar(n):GetString() end return t')
report={'session':session,'assets':assets,'scenes':{}}
def save(): (ROOT/'validation/adaptive-physics.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
def cleanup():
    lua('server',"hook.Remove('Tick','MMDHL.StressMotion') hook.Remove('Tick','MMDHL.StressTickCount') timer.Remove('MMDHL.StressHeartbeat') if MMDHL_STRESS then for _,w in ipairs(MMDHL_STRESS.wrapped or {}) do hook.Add(w[1],w[2],w[3]) end for _,e in ipairs(MMDHL_STRESS.entities or {}) do if IsValid(e) then e:Remove() end end end return true")
    lua('client',"for event,name in pairs({CalcView='MMDHL.StressCamera',HUDShouldDraw='MMDHL.StressHUD',PreDrawViewModel='MMDHL.StressViewModel',PostRender='MMDHL.StressFrames',HUDPaint='MMDHL.StressOverlay',OnLuaError='MMDHL.StressLuaErrors'}) do hook.Remove(event,name) end timer.Remove('MMDHL.StressClientHeartbeat') if MMDHL_ORIGINAL_DRAW then mmdhl.DrawCarrier=MMDHL_ORIGINAL_DRAW MMDHL_ORIGINAL_DRAW=nil end if MMDHL_STRESS_CLIENT then for _,w in ipairs(MMDHL_STRESS_CLIENT.wrapped or {}) do hook.Add(w[1],w[2],w[3]) end end return true")
try:
    assert lua('client','return system.HasFocus()'), 'Owned game must be foreground'
    assert not lua('client','for _,e in ipairs(mmdhl.Entities()) do if not e.MMDHLPreviewRig then return true end end return false'), 'Use an empty owned scene; never remove unrelated actors'
    lua('client',"RunConsoleCommand('mmdhl_secondary_backend','cpu_mt_v2'); RunConsoleCommand('mmdhl_secondary_collision','2'); RunConsoleCommand('mmdhl_debug_overlay','0'); return true")
    encoded=json.dumps(json.dumps([a['asset'] for a in assets]))
    lua('server','for _,id in ipairs(util.JSONToTable('+encoded+')) do mmdhl.native.RequestAsset(id) end return true')
    deadline=time.monotonic()+45
    while not lua('server','local ready=true for _,id in ipairs(util.JSONToTable('+encoded+')) do if not mmdhl.native.AssetInfo(id) then ready=false end end return ready'):
        if time.monotonic()>deadline: raise RuntimeError('Asset warm-up timed out')
        time.sleep(.5)
    # The last three scenes exercise suspension and recovery on the same rig
    # content. No physics bodies, joints, masks, timestep or mesh LOD changes.
    for name,distance,away,enabled,divisor,paused,held in [
        ('full',650,False,False,1,False,False),
        ('near',650,False,True,1,False,False),
        ('half',1450,False,True,2,False,False),
        ('quarter',2750,False,True,4,False,False),
        ('hidden',650,True,True,4,True,False),
        ('cutoff',5000,False,True,4,True,False),
        ('held',5000,True,True,1,False,True)]:
        config=dict(name='lod-'+name,mode='stress',grid=True,cameraCount=8,layout='spaced',motion='standing',assets=[a['asset'] for a in assets],warmup=8,duration=18,secondaryCollision=2,secondaryBackend='cpu_mt_v2',cameraDistance=distance,lookAway=away)
        for realm in ['server','client']: (directory/(config['name']+'-'+realm+'.json')).unlink(missing_ok=True)
        lua('client',f"RunConsoleCommand('mmdhl_lod_enabled','{int(enabled)}'); return true")
        config['origin']=lua('server','MMDHL_TEST_CONFIG=util.JSONToTable('+json.dumps(json.dumps(config))+')\n'+source)['origin']
        if held: lua('server',"for _,e in ipairs(MMDHL_STRESS.entities) do e:SetNW2Bool('MMDHLPhysgunHeld',true) end return true")
        lua('client','MMDHL_TEST_CONFIG=util.JSONToTable('+json.dumps(json.dumps(config))+')\n'+source)
        deadline=time.monotonic()+40
        while True:
            time.sleep(1)
            result=read(directory/(config['name']+'-client.json'))
            if result and result.get('finished'): break
            if time.monotonic()>deadline: raise RuntimeError(name+' timed out')
        states=result.get('physicsEnd') or {}; starts=result.get('physicsStart') or {}
        result['checks']={'focused':result['unfocused']==0 and result['foregroundSeconds']>=9.8,
            'finite':not result['errors'], 'rigs':len(states)==8,
            'tier':len(states)==8 and all(d.get('lod',{}).get('suspended')==paused and d['lod']['divisor']==divisor for d in states.values()),
            'ticksStopped':not paused or all(d['ticks']==starts[k]['ticks'] for k,d in states.items()),
            'noDebtOrReset':all(d['resets']==starts[k]['resets'] and d['dropped']==starts[k]['dropped'] for k,d in states.items())}
        expected={a['asset']:a for a in assets}
        result['checks']['rigs']=result['checks']['rigs'] and all(d['bodies']==expected[d['asset']]['rigidBodies'] and d['joints']==expected[d['asset']]['joints'] for d in states.values())
        result['environment']=read(directory/(config['name']+'-environment.json'))
        report['scenes'][name]=result; save()
        print(name,json.dumps(result['checks']),result['foregroundFrames'],flush=True)
        if paused:
            lua('server',"for _,e in ipairs(MMDHL_STRESS.entities) do e:SetNW2Bool('MMDHLPhysgunHeld',true) end return true")
            time.sleep(2)
            resumed=lua('client',"local out={} for _,e in ipairs(mmdhl.Entities()) do local h=mmdhl.GetInstance(e) if h>0 then local d=mmdhl.GetDiagnostics(e,false); out[tostring(h)]={ticks=d.ticks,resets=d.resets,debt=d.debtSeconds,finite=d.finite,dropped=d.droppedTime,lod=mmdhl.GetPhysicsLOD(e)} end end return out")
            result['resume']=resumed
            result['checks']['sameEntitiesResume']=len(resumed)==8 and set(resumed)==set(states) and all(not d['lod']['suspended'] and d['lod']['divisor']==1 and d['ticks']>states[k]['ticks'] and d['resets']==states[k]['resets'] and d['dropped']==states[k]['dropped'] for k,d in resumed.items())
            save()
        cleanup()
    report['passed']=all(all(s['checks'].values()) for s in report['scenes'].values()); save()
finally:
    cleanup()
    lua('client','for k,v in pairs(util.JSONToTable('+json.dumps(json.dumps(saved))+')) do RunConsoleCommand(k,v) end return true')
    save()
