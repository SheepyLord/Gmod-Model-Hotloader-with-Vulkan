"""Exercise a real old cached carrier through Sandbox duplication and migration."""
import json,time,sys
from pathlib import Path
from gamectl import execute,ROOT,read
sys.path.insert(0,str(ROOT/'build/python-deps'))
import numpy as np
from scipy.spatial.transform import Rotation
session=read(ROOT/'validation/session.json');cache=Path(session['cache']);models=read(ROOT/'validation/acceptance-models.json')

def lua(code):
 r=execute('server',code,45)
 if not r['ok']:raise RuntimeError(r.get('error'))
 return r['value']

def transform(position,rotation=None,angles=None):
 t=np.eye(4);t[:3,3]=position
 t[:3,:3]=Rotation.from_quat(rotation).as_matrix() if rotation else Rotation.from_euler('ZYX',[angles[1],angles[0],angles[2]],degrees=True).as_matrix()
 return t
report={}
for name,model in models.items():
 candidates=[read(p) for p in (cache/'rigs').glob('*/rig.json')]
 old=next((r for r in candidates if r and r.get('asset')==model['asset'] and r.get('generator')==9),None)
 if not old:raise RuntimeError('Missing generator-9 fixture for '+name)
 key=old['key']
 result=lua(f"""for _,e in ipairs(mmdhl.Entities()) do e:Remove() end
local r=util.JSONToTable(file.Read('mmd_hotloader/rigs/{key}/rig.json','DATA')) assert(game.MountGMA('data/mmd_hotloader/rigs/{key}/carrier.gma'))
local e=ents.Create('prop_ragdoll') e:SetModel(r.model) e:SetPos(Vector(-425,-700,100)) e:SetAngles(Angle(0,27,0)) e:Spawn()
local before={{}} for i=0,17 do local p=e:GetPhysicsObjectNum(i) p:EnableMotion(false) before[i+1]={{pos={{p:GetPos():Unpack()}},ang={{p:GetAngles():Unpack()}}}} end
local finger=e:LookupBone('ValveBiped.Bip01_L_Finger1') e:ManipulateBoneAngles(finger,Angle(25,0,0))
-- Old version-1 duplicates had no embedded rig: recover immutable metadata by model path.
duplicator.StoreEntityModifier(e,'MMDHLNative',{{version=1,asset='{model['asset']}',options={{height=72,frozen=true}},scale=1}})
local copied=duplicator.Copy(e) local pasted=duplicator.Paste(player.GetHumans()[1],copied.Entities,copied.Constraints) local clone=pasted[e:EntIndex()]
assert(IsValid(clone) and mmdhl.IsMMD(clone),'Migration did not attach')
local rig=mmdhl.GetRig(clone) local after={{}} for i=0,17 do local p=clone:GetPhysicsObjectNum(i) after[i+1]={{pos={{p:GetPos():Unpack()}},ang={{p:GetAngles():Unpack()}},frozen=not p:IsMotionEnabled()}} end
local result={{before=before,after=after,rig=rig,bodies=clone:GetPhysicsObjectCount(),finger=clone:GetManipulateBoneAngles(clone:LookupBone('ValveBiped.Bip01_L_Finger1')).p}}
e:Remove() clone:Remove() return result""")
 new=result['rig'];errors=[]
 for i,(b,a) in enumerate(zip(result['before'],result['after'])):
  ob=old['bones'][old['bodies'][i]['bone']];nb=new['bones'][new['bodies'][i]['bone']]
  before=transform(b['pos'],angles=b['ang'])@np.linalg.inv(transform(ob['position'],rotation=ob['rotation']))
  after=transform(a['pos'],angles=a['ang'])@np.linalg.inv(transform(nb['position'],rotation=nb['rotation']))
  errors.append(float(np.abs(before-after).max()))
 assert max(errors)<.01,(name,errors)
 assert result['bodies']==18 and all(a['frozen'] for a in result['after']) and abs(result['finger']-25)<.001,result
 assert abs(new['scale']-old['scale'])<1e-5,(new['scale'],old['scale'])
 report[name]={'oldGenerator':9,'newGenerator':new['generator'],'scale':new['scale'],'maxBindDeltaError':max(errors),'bodies':18,'frozen':True,'finger':result['finger']}
 (ROOT/'validation/legacy-native-duplicates.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 print(name,'PASS: old scale, 18 bodies, pose, freeze and fingers survived migration',flush=True)
 time.sleep(.2)
