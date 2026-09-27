"""Owned-session integration checks for shared settings and opt-in telemetry."""
import json,time
from gamectl import ROOT,execute,read

def lua(realm,code):
    result=execute(realm,code,60)
    if not result['ok']:raise RuntimeError(result)
    return result.get('value')

def check(condition,message):
    if not condition:raise AssertionError(message)

def main():
    output={}
    output['initial']=lua('client',"local saved={} for _,key in ipairs({'mmdhl_spawn_frozen','mmdhl_secondary_collision','mmdhl_secondary_backend','mmdhl_debug_overlay','mmdhl_debug_print'}) do saved[key]=GetConVar(key):GetString() end return {convars=saved,resolution={ScrW(),ScrH()},addons=engine.GetAddons(),folders=select(2,file.Find('addons/*','MOD')),polls=mmdhl.performance.polls}")
    check(output['initial']['resolution']==[2560,1440],'Wrong game resolution')
    session=read(ROOT/'validation/session.json')
    from pathlib import Path
    models={}
    for path in (Path(session['cache'])/'assets').glob('*/manifest.json'):
        info=read(path)
        if info:models[info['name']]=path.parent.name
    ids=[models[n] for n in ['xin','星穹铁道—昔涟','Marionette']]
    def spawn(asset):
        lua('server',"MMDHL_QOL_RESULT=nil local p=player.GetHumans()[1] local n=#mmdhl.Entities() local pos=p:GetPos()+p:GetForward()*180+p:GetRight()*(n-1)*85 mmdhl.Spawn(p,"+json.dumps(asset)+",{position={pos.x,pos.y,pos.z+10}},function(e,err) MMDHL_QOL_RESULT={entity=IsValid(e) and e:EntIndex(),error=err} end) return true")
        for _ in range(100):
            value=lua('server','return MMDHL_QOL_RESULT')
            if value:
                check(value.get('entity'),f'Spawn failed: {value}')
                return value['entity']
            time.sleep(.2)
        raise TimeoutError('Spawn did not finish')
    try:
        lua('client',"mmdhl.SetGlobalSetting('frozen',true) mmdhl.SetGlobalSetting('secondaryCollision',0) mmdhl.SetGlobalSetting('secondaryBackend','reference') return true")
        time.sleep(.5)
        entities=[spawn(ids[0]),spawn(ids[1])]
        lua('server',"MMDHL_QOL_BEFORE={} for _,e in ipairs(mmdhl.Entities()) do local poses={} for i=0,17 do local p=e:GetPhysicsObjectNum(i) assert(not p:IsMotionEnabled()) poses[i+1]={position=p:GetPos(),angles=p:GetAngles()} end MMDHL_QOL_BEFORE[e:EntIndex()]=poses end return true")
        lua('client',"mmdhl.Open() local p=mmdhl.window.Library p.Backend:ChooseOptionID(2) p.CollisionMode:ChooseOptionID(2) return true")
        time.sleep(1)
        output['liveChange']=lua('server',"local result={} for _,e in ipairs(mmdhl.Entities()) do assert(e:GetPhysicsObjectCount()==18) assert(e:GetNW2String('MMDHLSecondaryBackend')=='cpu_mt') assert(mmdhl.GetSecondaryCollisionMode(e)==1) local d=mmdhl.GetDiagnostics(e,false) assert(d.secondaryBackend=='cpu_mt') for i=0,17 do local p=e:GetPhysicsObjectNum(i) local before=MMDHL_QOL_BEFORE[e:EntIndex()][i+1] assert(not p:IsMotionEnabled()) assert(p:GetPos():Distance(before.position)<.01) assert(p:GetAngles()==before.angles) end result[#result+1]={entity=e:EntIndex(),nativeBodies=18,bodies=d.bodies,joints=d.joints,backend=d.secondaryBackend,iterations=d.solverIterations} end return result")
        output['selection']=lua('client',"local p=mmdhl.window.Library local ids=util.JSONToTable("+json.dumps(json.dumps(ids))+ ") for _,id in ipairs(ids) do local entry=mmdhl.library.entries[id] assert(entry) local old=entry.settings.spawn entry.settings.spawn={frozen=false,secondaryCollision=2,secondaryBackend='gpu_opencl',scaleMultiplier=1.15} p:SelectAsset(id) entry.settings.spawn=old assert(mmdhl.GetGlobalSettings().secondaryBackend=='cpu_mt') assert(mmdhl.GetGlobalSettings().secondaryCollision==1) assert(mmdhl.GetGlobalSettings().frozen) end mmdhl.window:Close() mmdhl.Open() return mmdhl.GetGlobalSettings()")
        lua('client',"mmdhl.CloseLibrary() mmdhl.SetGlobalSetting('frozen',false) return true")
        time.sleep(.5)
        entities.append(spawn(ids[2]))
        output['spawnDefaults']=lua('server',"local newest=Entity("+str(entities[-1])+") assert(not newest.MMDOptions.frozen) for i=0,17 do assert(newest:GetPhysicsObjectNum(i):IsMotionEnabled()) end for id in pairs(MMDHL_QOL_BEFORE) do local e=Entity(id) for i=0,17 do assert(not e:GetPhysicsObjectNum(i):IsMotionEnabled()) end end return {newFrozen=newest.MMDOptions.frozen,backend=newest.MMDOptions.secondaryBackend,collision=newest.MMDOptions.secondaryCollision,explicitOptions=mmdhl.WithSpawnDefaults(player.GetHumans()[1],{frozen=true,secondaryCollision=0,secondaryBackend='reference'})}")
        output['idlePolls']=lua('client','return mmdhl.performance.polls')
        check(output['idlePolls']==output['initial']['polls'],'Disabled overlay polled diagnostics')
        lua('client',"mmdhl.SetGlobalSetting('secondaryCollision',2) GetConVar('mmdhl_debug_overlay'):SetBool(true) GetConVar('mmdhl_debug_print'):SetBool(true) gui.HideGameUI() return true")
        time.sleep(12)
        output['overlay']=lua('client',"assert(mmdhl.performance.summary) local s=table.Copy(mmdhl.performance.summary) s.graph=nil assert(s.models==3) assert(s.nativeBodies==54) assert(s.bodies>1500) assert(s.frames>0 and s.p95>0) RunConsoleCommand('mmdhl_debug_capture','global-settings-overlay') RunConsoleCommand('mmdhl_debug_report') return {summary=s,polls=mmdhl.performance.polls}")
        lua('client',"GetConVar('mmdhl_debug_overlay'):SetBool(false) GetConVar('mmdhl_debug_print'):SetBool(false) return true")
        before=lua('client','return mmdhl.performance.polls')
        time.sleep(1)
        check(lua('client','return mmdhl.performance.polls')==before,'Polling continued while disabled')
        lua('client',"mmdhl.Open() return true")
        time.sleep(.5)
        lua('client',"RunConsoleCommand('mmdhl_debug_capture','global-settings-library','ui') return true")
        time.sleep(.5)
        output['passed']=True
    finally:
        lua('client',"mmdhl.CloseLibrary() local values=util.JSONToTable("+json.dumps(json.dumps(output['initial']['convars']))+") for key,value in pairs(values) do GetConVar(key):SetString(value) end return true")
        lua('server',"for _,e in ipairs(mmdhl.Entities()) do e:Remove() end MMDHL_QOL_RESULT=nil MMDHL_QOL_BEFORE=nil return true")
        (ROOT/'validation/global-settings.json').write_text(json.dumps(output,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in output.items() if k!='initial'},ensure_ascii=False,indent=2))

if __name__=='__main__':main()
