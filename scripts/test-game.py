"""Repeatable single-player integration suite. Start game-start.ps1 first.
Exercises real native modules, worker, entities, Source physics and rendering.
No keyboard timing or console-window automation is needed.
"""
import argparse,json,statistics,time,pathlib
from gamectl import execute,ROOT,read
p=argparse.ArgumentParser();p.add_argument('--asset',help='Imported asset hash (defaults to validation/test-asset.json)');args=p.parse_args()
asset=args.asset or read(ROOT/'validation/test-asset.json')['id']
report={'asset':asset,'checks':[],'benchmarks':[]}
def lua(realm,code):
 r=execute(realm,code)
 if not r['ok']:raise RuntimeError(r)
 return r.get('value')
def check(condition,name,data=None):
 report['checks'].append({'name':name,'passed':bool(condition),'data':data});print(('PASS ' if condition else 'FAIL ')+name,flush=True)
 (ROOT/'validation/game-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 if not condition:raise AssertionError(name)
def wait(realm,code,timeout=60):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  r=lua(realm,code)
  if r:return r
  time.sleep(.1)
 raise TimeoutError(code)
def load(id):
 for realm in ('server','client'):
  lua(realm,f"return mmdhl.native.RequestAsset('{id}')")
  wait(realm,f"local v,e=mmdhl.Decode(mmdhl.native.AssetInfo('{id}')) if e then error(e) end return v~=nil")
def job(code):
 id=lua('client',code)
 return wait('client',f"local s,e=mmdhl.Decode(mmdhl.native.PollJob({id})) if e then error(e) end if s.state~='running' then return s end",120)
def capture(name,ui=False):
 lua('client',f"RunConsoleCommand('mmdhl_debug_capture','{name}','{'ui' if ui else ''}') return true")
 session=read(ROOT/'validation/session.json');path=pathlib.Path(session['cache'])/'debug'/session['token']/f'{name}.png'
 end=time.monotonic()+5
 while not path.exists() and time.monotonic()<end:time.sleep(.1)
 check(path.exists(),'screenshot '+name)
 return str(path)
def clean():
 lua('server',"for _,e in ipairs(ents.FindByClass('mmdhl_ragdoll')) do e:Remove() end if IsValid(mmdhl.testProp) then mmdhl.testProp:Remove() end mmdhl.native.Clear() mmdhl.testEnt=nil return true")
def spawn(id,offset=0,frozen=True):
 lua('server',f"mmdhl.Spawn(Entity(1),'{id}',{{position={{64,{-384+offset},-12260}},frozen={str(frozen).lower()}}},function(e,err) mmdhl.testEnt=e mmdhl.testErr=err end) return true")
 return wait('server',"if mmdhl.testErr then error(mmdhl.testErr) end return IsValid(mmdhl.testEnt) and mmdhl.testEnt:GetInstance()")
load(asset);clean()
lua('client',"hook.Remove('PostDrawTranslucentRenderables','MMDHL.DebugDraw') RunConsoleCommand('mat_motion_blur_enabled','0') hook.Add('CalcView','MMDHL.DebugView',function() return {origin=Vector(-86,-384,-12215),angles=Angle(0,0,0),fov=60,drawviewer=true} end) return true")
handle=spawn(asset)
wait('client',"return #ents.FindByClass('mmdhl_ragdoll')==1")
check(lua('client',"return mmdhl.renderError==nil and mmdhl.native.RenderStatus():find('Native')~=nil"),'native render path without errors')
report['screenshot']=capture('integration-model')
time.sleep(.3)
cached=lua('client',"return mmdhl.Decode(mmdhl.native.RenderStats())")
time.sleep(.4)
reused=lua('client',"return mmdhl.Decode(mmdhl.native.RenderStats())")
check(cached['cachedBuffers']>0 and cached['builds']==reused['builds'],'frozen mesh is reused across frames')
lua('server',"mmdhl.native.SetFrozen(mmdhl.testEnt:GetInstance(),false) mmdhl.testEnt:SetFrozen(false) return true")
time.sleep(2)
bounds=lua('server',"return mmdhl.Decode(mmdhl.native.GetBounds(mmdhl.testEnt:GetInstance()))")
check(-12320<bounds['center'][2]<-12200,'ragdoll collides with gm_flatgrass',bounds)
# Frozen imported collision body must stop and rebound an ordinary Source prop.
lua('server',(ROOT/'tests/game/prop-impact.lua').read_text())
time.sleep(1.2)
impact=lua('server',"return {feedback=mmdhl.feedbackCount,first=mmdhl.samples[1],last=mmdhl.samples[#mmdhl.samples],error=mmdhl.simulationError}")
check(impact['feedback']>0 and impact['last']['velocity']<0 and not impact.get('error'),'two-way Source prop collision',impact)
lua('server',(ROOT/'tests/game/impulse-units.lua').read_text());time.sleep(.15)
units=lua('server',"return mmdhl.unitResult")
check(abs(units['velocity']-100)<.01 and abs(units['angular']-10)<.01,'Source linear and angular impulse units',units)
# Exercise the grab constraint directly, without input timing.
grab=lua('server',"local id=mmdhl.testEnt:GetInstance() mmdhl.native.ResetPhysics(id) mmdhl.native.SetFrozen(id,false) local d=mmdhl.Decode(mmdhl.native.GetDiagnostics(id)) for i,b in ipairs(d.bodyList) do if b.generated then local p=Vector(unpack(b.position)) local _,e=mmdhl.native.BeginGrab(id,i-1,p) if e then error(e) end mmdhl.native.UpdateGrab(p+Vector(0,0,80)) mmdhl.grabTest={index=i,start=p.z} return mmdhl.grabTest end end error('No generated body to grab')")
time.sleep(.5)
height=lua('server',"local d=mmdhl.Decode(mmdhl.native.GetDiagnostics(mmdhl.testEnt:GetInstance())) mmdhl.native.EndGrab() return d.bodyList[mmdhl.grabTest.index].position[3]")
check(height>grab['start']+10,'grab constraint lifts ragdoll',{'start':grab['start'],'end':height})
lua('server',"mmdhl.native.ResetPhysics(mmdhl.testEnt:GetInstance()) mmdhl.native.SetFrozen(mmdhl.testEnt:GetInstance(),true) mmdhl.testEnt:SetFrozen(true) return true")
before=lua('server',"return mmdhl.Decode(mmdhl.native.GetBounds(mmdhl.testEnt:GetInstance())).center")
reloaded=job(f"return mmdhl.native.Reload('{asset}')")
check(reloaded['state']=='complete','isolated worker reload')
lua('client',f"mmdhl.Action('replace','{reloaded['asset']}',ents.FindByClass('mmdhl_ragdoll')[1]) return true")
wait('server',f"return mmdhl.testEnt:GetInstance()~={handle}")
after=lua('server',"return mmdhl.Decode(mmdhl.native.GetBounds(mmdhl.testEnt:GetInstance())).center")
check(sum((a-b)**2 for a,b in zip(before,after))<.1,'reload preserves model center',{'before':before,'after':after})
pose=lua('server',"local e=mmdhl.testEnt local id=e:GetInstance() local _,err=mmdhl.native.SetBonePose(id,0,util.TableToJSON({translation={0,.5,0}})) if err then error(err) end local info=mmdhl.Decode(mmdhl.native.AssetInfo(e:GetAsset())) if #info.morphs>1 then mmdhl.native.SetMorph(id,1,.25) end return mmdhl.Decode(mmdhl.native.GetState(id)).manual[1].translation[2]")
check(abs(pose-.5)<.001,'bone posing persists in native state')
dupe=lua('server',"local e=mmdhl.testEnt e:PreEntityCopy() local data=duplicator.CopyEntTable(e) data.Pos=e:GetPos()+Vector(0,100,0) local copy=duplicator.CreateEntityFromTable(Entity(1),data) if not IsValid(copy) then error('Duplication failed') end local state=mmdhl.Decode(mmdhl.native.GetState(copy:GetInstance())) copy:Remove() return {manual=state.manual[1].translation[2],morph=state.morphs[2]}")
check(abs(dupe['manual']-.5)<.001 and abs(dupe.get('morph',0)-.25)<.001,'duplicator preserves native pose and morphs',dupe)
badpath=json.dumps(str(ROOT/'tests/fixtures/truncated.pmx'),ensure_ascii=False)
failed=job(f"return mmdhl.native.BeginImport({badpath},'{{}}')")
check(failed['state']=='failed' and lua('server',"return IsValid(mmdhl.testEnt)"),'failed import preserves live ragdoll')
# Soft-body fixture uses a material starting at global vertex 3 and nonzero anchor index.
path=json.dumps(str(ROOT/'tests/fixtures/cloth21.pmx'),ensure_ascii=False)
cloth=job(f"return mmdhl.native.BeginImport({path},'{{}}')")
check(cloth['state']=='complete','PMX 2.1 fixture imports')
load(cloth['asset']);clean();spawn(cloth['asset'],frozen=False);time.sleep(.6)
soft=lua('server',"return {rig=mmdhl.Decode(mmdhl.native.GetDiagnostics(mmdhl.testEnt:GetInstance())),bounds=mmdhl.Decode(mmdhl.native.GetBounds(mmdhl.testEnt:GetInstance())),error=mmdhl.simulationError}")
check(soft['rig']['softBodies']==1 and not soft.get('error') and all(abs(x)<50000 for x in soft['bounds']['center']),'soft-body fixture simulates in game',soft['rig'])
check(lua('server',"local e=mmdhl.testEnt mmdhl.native.SetFrozen(e:GetInstance(),true) e:SetFrozen(true) e:PreEntityCopy() local data=duplicator.CopyEntTable(e) data.Pos=e:GetPos()+Vector(0,100,0) local copy=duplicator.CreateEntityFromTable(Entity(1),data) if not IsValid(copy) then return false end local state=mmdhl.Decode(mmdhl.native.GetState(copy:GetInstance())) copy:Remove() return #state.soft==1 and #state.soft[1]==25"),'duplicator preserves soft node state')
capture('integration-cloth')
for count in (1,5):
 clean()
 for i in range(count):
  lua('server','mmdhl.testEnt=nil return true');spawn(asset,offset=i*80,frozen=False)
 time.sleep(.5)
 lua('server',"mmdhl.benchmark={} hook.Add('Tick','MMDHL.Benchmark',function() local d=mmdhl.Decode(mmdhl.native.GetDiagnostics()) table.insert(mmdhl.benchmark,d.stepMs) if #mmdhl.benchmark>=120 then hook.Remove('Tick','MMDHL.Benchmark') end end) return true")
 samples=wait('server',"if #mmdhl.benchmark>=120 then return mmdhl.benchmark end")
 samples.sort();data={'models':count,'samples':len(samples),'medianStepMs':statistics.median(samples),'p95StepMs':samples[int(len(samples)*.95)-1]};report['benchmarks'].append(data)
 check(lua('server','return mmdhl.simulationError==nil'),'simulation benchmark '+str(count)+' models',data)
clean();spawn(asset)
wait('client',"return #ents.FindByClass('mmdhl_ragdoll')==1")
check(lua('server',"local p=Entity(1) p:SetMoveType(MOVETYPE_NOCLIP) p:SetPos(Vector(-80,-384,-12279)) p:SetEyeAngles(Angle(0,0,0)) local weapon=p:GetWeapon('weapon_mmdhl') if not IsValid(weapon) then weapon=p:Give('weapon_mmdhl') end p:SelectWeapon('weapon_mmdhl') weapon:PrimaryAttack() local grabbed=weapon.Grabbing==true weapon:Release() return grabbed"),'MMD grabber weapon acquires imported body')
lua('server',"mmdhl.native.ResetPhysics(mmdhl.testEnt:GetInstance()) mmdhl.native.SetFrozen(mmdhl.testEnt:GetInstance(),true) mmdhl.testEnt:SetFrozen(true) return true")
lua('client',"mmdhl.Open() return true")
lua('client',f"local list=mmdhl.window.MMDHLLibrary for _,row in ipairs(list:GetLines()) do if row.asset=='{asset}' then list:SelectItem(row) end end return true")
time.sleep(.4)
check(lua('client',"return mmdhl.renderError==nil"),'library preview renders')
capture('integration-ui',True)
lua('client',"if IsValid(mmdhl.window) then mmdhl.window:Close() end return true")
lua('server',"Entity(1):ConCommand('gmod_undo') return true")
wait('server',"return not IsValid(mmdhl.testEnt)")
check(True,'Sandbox undo removes imported entity')
clean()
check(lua('server',"local d=mmdhl.Decode(mmdhl.native.GetDiagnostics()) return d.instances==0"),'entity removal releases native instances')
time.sleep(.3)
check(lua('client',"mmdhl.native.PruneRenderCache(false) return mmdhl.Decode(mmdhl.native.RenderStats()).cachedBuffers==0"),'entity and preview removal release GPU caches')
report['passed']=True;(ROOT/'validation/game-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('Integration suite complete.',flush=True)
