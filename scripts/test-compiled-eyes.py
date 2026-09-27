"""Compare native eye displacement with an installed SCMI-compiled reference."""
import json,runpy,time
from gamectl import ROOT,read
c=runpy.run_path(str(ROOT/'scripts/test-compatibility.py'));lua,spawn=c['lua'],c['spawn']
asset=read(ROOT/'validation/acceptance-models.json')['Cyrene']['asset']
e=spawn(asset)
ref=lua('server',"local model='models/sheepylord/honkai_star_rail/cyrene.mdl' assert(util.IsValidModel(model)) local e=ents.Create('prop_ragdoll') e:SetModel(model) e:SetPos(mmdhl.testEnt:GetPos()+Vector(0,140,0)) e:Spawn() for i=0,e:GetPhysicsObjectCount()-1 do e:GetPhysicsObjectNum(i):EnableMotion(false) end assert(e:LookupBone('Eye_L')) MMDHL_COMPILED_EYES=e return e:EntIndex()")
saved=lua('server',"local t={} for _,n in ipairs({'sv_rpe_eye_track_bone_pos_ud','sv_rpe_eye_track_bone_pos_lr','sv_rpe_eye_track_use_bone','sv_rpe_eye_track_use_flex','sv_rpe_eye_track_enable'}) do t[n]=GetConVar(n):GetString() end return t")
report={'reference':'models/sheepylord/honkai_star_rail/cyrene.mdl','poses':{}}
try:
 # Keep both models on the same explicit driving input instead of tracking
 # different nearby-player angles between the server and client probes.
 lua('server',"GetConVar('sv_rpe_eye_track_enable'):SetBool(false) return true")
 for scale in [0,.5,1]:
  lua('server',f"GetConVar('sv_rpe_eye_track_bone_pos_ud'):SetFloat({scale}) GetConVar('sv_rpe_eye_track_bone_pos_lr'):SetFloat({scale}) GetConVar('sv_rpe_eye_track_use_bone'):SetBool(true) GetConVar('sv_rpe_eye_track_use_flex'):SetBool(false) for _,e in ipairs({{mmdhl.testEnt,MMDHL_COMPILED_EYES}}) do apply_eye_tracking(e,nil,{{overrideControlActive=true,overrideControlU=.4,overrideControlL=.5,overrideControlB=0,curU=.4,curL=.5,curB=0,last=CurTime()}},CurTime()) end return true")
  time.sleep(.15)
  report['poses'][str(scale)]=lua('client',f"local out={{}} for _,id in ipairs({{{e},{ref}}}) do local e=Entity(id) e:InvalidateBoneCache() e:SetupBones() local h=e:GetBoneMatrix(e:LookupBone('ValveBiped.Bip01_Head1')) local b=e:GetBoneMatrix(e:LookupBone('Eye_L')) local rel=h:GetInverse()*b out[#out+1]={{pos={{rel:GetTranslation():Unpack()}},manip={{e:GetManipulateBonePosition(e:LookupBone('Eye_L')):Unpack()}}}} end return out")
 result=[]
 zero=report['poses']['0']
 for scale in [.5,1]:
  pair=report['poses'][str(scale)]
  delta=[[pair[j]['pos'][k]-zero[j]['pos'][k] for k in range(3)] for j in range(2)]
  error=sum((delta[0][k]-delta[1][k])**2 for k in range(3))**.5
  result.append({'scale':scale,'headLocalDisplacement':delta,'error':error})
 report['comparison']=result
 assert all(r['error']<.02 for r in result),result
 print('PASS eye displacement agrees with compiled SCMI reference',result,flush=True)
finally:
 lua('server','for n,v in pairs(util.JSONToTable('+json.dumps(json.dumps(saved))+')) do GetConVar(n):SetString(v) end if IsValid(MMDHL_COMPILED_EYES) then MMDHL_COMPILED_EYES:Remove() end for _,e in ipairs(mmdhl.Entities()) do e:Remove() end return true')
 (ROOT/'validation/compatibility-compiled-eyes.json').write_text(json.dumps(report,indent=2),encoding='utf8')
