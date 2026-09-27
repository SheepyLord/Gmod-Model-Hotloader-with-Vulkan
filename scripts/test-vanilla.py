"""Exercise the actual Sandbox tools and engine physgun in an owned test game.
Use a humanoid asset with facial morphs and standard MMD finger bones.
The physgun test drives real user commands; it never calls pickup hooks itself.
"""
import argparse,atexit,json,time
from gamectl import execute,ROOT

parser=argparse.ArgumentParser()
parser.add_argument('--asset',required=True)
args=parser.parse_args()
report={'asset':args.asset,'checks':[]}

def lua(realm,code):
    r=execute(realm,code)
    if not r['ok']:raise RuntimeError(r)
    return r.get('value')

def wait(realm,code,timeout=45):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        value=lua(realm,code)
        if value:return value
        time.sleep(.2)
    raise TimeoutError(code)

def check(value,label,data=None):
    report['checks'].append({'name':label,'passed':bool(value),'data':data})
    (ROOT/'validation/vanilla-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(('PASS ' if value else 'FAIL ')+label,flush=True)
    if not value:raise AssertionError(label)

wait('server','return IsValid(Entity(1))')
saved=lua('client',"local out={} local names={'faceposer_scale','eyeposer_x','eyeposer_y','eyeposer_strabismus'} for i=0,95 do names[#names+1]='faceposer_flex'..i end for i=0,15 do names[#names+1]='finger_'..i end for _,name in ipairs(names) do local c=GetConVar(name) if c then out[name]=c:GetString() end end return out")
def restore_controls():
    try:
        encoded=json.dumps(json.dumps(saved))
        execute('client',f"for name,value in pairs(util.JSONToTable({encoded})) do RunConsoleCommand(name,value) end return true",timeout=3)
    except (RuntimeError,TimeoutError):pass
atexit.register(restore_controls)
lua('server',"for _,e in ipairs(ents.FindByClass('mmdhl_ragdoll')) do e:Remove() end mmdhl.testEnt=nil mmdhl.testErr=nil return true")
lua('server',f"mmdhl.Spawn(Entity(1),'{args.asset}',{{position={{64,-384,-12260}},frozen=true}},function(e,err) mmdhl.testEnt=e mmdhl.testErr=err end) return true")
wait('server','if mmdhl.testErr then error(mmdhl.testErr) end return IsValid(mmdhl.testEnt)')
wait('client','local e=ents.FindByClass("mmdhl_ragdoll")[1] return IsValid(e) and e:GetFlexNum()>0')
check(lua('client',"return spawnmenu.GetCreationTabs().MMD~=nil and IsValid(mmdhl.spawnPanel)"),'MMD creation tab exists and builds its library panel')
closed=lua('client',f"""g_SpawnMenu:Open() g_SpawnMenu:OpenCreationMenuTab('MMD')
 local function find(panel)
  if panel.GetLines and panel.AddColumn then return panel end
  for _,child in ipairs(panel:GetChildren()) do local match=find(child) if match then return match end end
 end
 local models=assert(find(mmdhl.spawnPanel))
 for i,row in ipairs(models:GetLines()) do if row.asset=='{args.asset}' then models:DoDoubleClick(i,row) return not g_SpawnMenu:IsVisible() end end
 error('Asset is missing from library')""")
wait('server',"return #ents.FindByClass('mmdhl_ragdoll')==2")
check(closed,'Q-menu library entry spawns a model and closes the menu')
lua('server',"for _,e in ipairs(ents.FindByClass('mmdhl_ragdoll')) do if e~=mmdhl.testEnt then e:Remove() end end return true")
rig=lua('server',"return mmdhl.Decode(mmdhl.native.GetDiagnostics(mmdhl.testEnt:GetInstance()))")
check(rig['anatomicalJoints']>=12,'humanoid has anatomical primary constraints',rig['jointLimits'])
check(lua('server',"local e=mmdhl.testEnt local c=e:GetPos() local tr=util.TraceLine({start=c-Vector(140,0,0),endpos=c+Vector(140,0,0),filter=Entity(1)}) return tr.Entity==e and IsValid(e:GetPhysicsObject()) and not e:GetPhysicsObject():IsCollisionEnabled()"),'ordinary Source trace hits ragdoll; handle is excluded from collision feedback')
check(lua('server',"local p=ents.Create('prop_physics') p:SetModel('models/props_junk/PopCan01a.mdl') p:Spawn() local ok=p:GetFlexNum()==0 and p:GetBoneCount()==1 p:Remove() return ok"),'normal Source entities keep their original bone and flex APIs')

lua('server',(ROOT/'tests/game/vanilla-physgun.lua').read_text(encoding='utf-8'))
result=wait('server','return mmdhl.vanillaGun and mmdhl.vanillaGun.done and mmdhl.vanillaGun',15)
check(result['pickups']>=1 and result['drops']>=1,'engine Physics Gun picks up and drops the ragdoll',result)
check(result['maxHeight']>result['startHeight']+25,'engine Physics Gun lifts the real humanoid by its head',result)
check(result['frozen'],'engine right-click freezes the ragdoll')
before=lua('server','return mmdhl.Decode(mmdhl.native.GetState(mmdhl.testEnt:GetInstance())).bodies')
lua('server',"local e=mmdhl.testEnt local p=Entity(1) p:Give('gmod_tool') p:SelectWeapon('gmod_tool') local face=p:GetWeapon('gmod_tool'):GetToolObject('faceposer') assert(face:RightClick({Entity=e,HitPos=e:GetPos()})) p:ConCommand('faceposer_flex0 0.7') p:ConCommand('faceposer_scale 1') return true")
time.sleep(1.2)
check(lua('server',"local e=mmdhl.testEnt local face=Entity(1):GetWeapon('gmod_tool'):GetToolObject('faceposer') face.FaceTimer=0 face:Think() return math.abs(e:GetFlexWeight(0)-.7)<.001"),'vanilla Face Poser Think applies a morph')
check(lua('client',"local e=ents.FindByClass('mmdhl_ragdoll')[1] local face=LocalPlayer():GetWeapon('gmod_tool'):GetToolObject('faceposer') face:RebuildControlPanel(e) return IsValid(controlpanel.Get('faceposer')) and e:GetFlexNum()>0 and math.abs(e:GetFlexWeight(0)-.7)<.001"),'vanilla facial panel and client weights match the model')
lua('server',"local e=mmdhl.testEnt local finger=Entity(1):GetWeapon('gmod_tool'):GetToolObject('finger') local hand=e:LookupBone('ValveBiped.Bip01_L_Hand') assert(hand and finger:RightClick({Entity=e,HitPos=e:GetBoneMatrix(hand):GetTranslation()})) return true")
time.sleep(.6)
lua('server',"Entity(1):ConCommand('finger_3 40 0') return true")
time.sleep(.3)
finger=lua('server',"local e=mmdhl.testEnt local tool=Entity(1):GetWeapon('gmod_tool'):GetToolObject('finger') tool:ApplyValues(e,0) local b=e:LookupBone('ValveBiped.Bip01_L_Finger1') local state=mmdhl.Decode(mmdhl.native.GetState(e:GetInstance())) return {angle=e:GetManipulateBoneAngles(b).p,q=state.manual[b+1].rotation,mapped=table.Count(e.FingerIndex)}")
check(finger['mapped']>=24 and abs(finger['angle']-40)<.01 and abs(finger['q'][3])<.99,'vanilla Finger Poser maps available joints and changes the native pose',finger)
lua('server',"local e=mmdhl.testEnt local p=Entity(1) local tool=p:GetWeapon('gmod_tool'):GetToolObject('eyeposer') assert(tool:RightClick({Entity=e,HitPos=e:GetPos()})) p:ConCommand('eyeposer_x .7') p:ConCommand('eyeposer_y .3') p:ConCommand('eyeposer_strabismus 0') return true")
time.sleep(.3)
check(lua('server',"local e=mmdhl.testEnt local t=Entity(1):GetWeapon('gmod_tool'):GetToolObject('eyeposer') t:Think() local s=mmdhl.Decode(mmdhl.native.GetState(e:GetInstance())) return e:LookupAttachment('eyes')==1 and math.abs(s.manual[e.MMDEyes[1]+1].rotation[4])<.999"),'vanilla Eye Poser changes MMD eye bones')
after=lua('server','return mmdhl.Decode(mmdhl.native.GetState(mmdhl.testEnt:GetInstance())).bodies')
check(all(before[i]==after[i] for i,b in enumerate(rig['bodyList']) if b['core']),'facial and finger edits preserve the frozen primary ragdoll pose')
dupe=lua('server',"local e=mmdhl.testEnt e:PreEntityCopy() local data=duplicator.CopyEntTable(e) data.Pos=e:GetPos()+Vector(0,120,0) local copy=duplicator.CreateEntityFromTable(Entity(1),data) assert(IsValid(copy)) local result={flex=copy:GetFlexWeight(0),angle=copy:GetManipulateBoneAngles(copy:LookupBone('ValveBiped.Bip01_L_Finger1')).p} copy:Remove() return result")
check(abs(dupe['flex']-.7)<.001 and abs(dupe['angle']-40)<.01,'Sandbox duplication preserves face and finger tool state',dupe)
lua('server',(ROOT/'tests/game/vanilla-unfreeze.lua').read_text(encoding='utf-8'))
check(wait('server','return mmdhl.vanillaUnfreezeDone',10) and lua('server','return not mmdhl.testEnt:GetFrozen()'),'engine Physics Gun reload unfreezes the ragdoll')
time.sleep(2)
data=lua('server',"local d=mmdhl.Decode(mmdhl.native.GetDiagnostics(mmdhl.testEnt:GetInstance())) local worst=0 local pivot=0 for _,j in ipairs(d.jointLimits) do for axis=1,3 do worst=math.max(worst,j.lower[axis]-j.angle[axis],j.angle[axis]-j.upper[axis]) end pivot=math.max(pivot,j.pivotError) end return {angularError=worst,pivotError=pivot,simulationError=mmdhl.simulationError}")
check(data['angularError']<12 and data['pivotError']<3 and not data.get('simulationError'),'joint limits remain bounded after manipulation and release',data)
check(lua('server',"local e=mmdhl.testEnt local tool=Entity(1):GetWeapon('gmod_tool'):GetToolObject('remover') return tool:LeftClick({Entity=e,HitPos=e:GetPos(),PhysicsBone=0})"),'vanilla Remover accepts the ragdoll')
wait('server','return not IsValid(mmdhl.testEnt)')
check(lua('server','return mmdhl.Decode(mmdhl.native.GetDiagnostics()).instances==0'),'removal cleans up native bodies and grab state')
print(f"{len(report['checks'])} vanilla integration checks passed")
