import argparse,json,time,subprocess
from gamectl import execute,ROOT
def lua(realm,code):
 r=execute(realm,code,45)
 if not r['ok']:raise RuntimeError(r.get('error'))
 return r.get('value')
def main():
 p=argparse.ArgumentParser();p.add_argument('model',choices=['Xin','Cyrene','Sandrone']);p.add_argument('--height',type=float,default=72);p.add_argument('--keep',action='store_true');a=p.parse_args();data=json.loads((ROOT/'validation/acceptance-models.json').read_text(encoding='utf-8'))[a.model]
 subprocess.run(['powershell','-NoProfile','-File',str(ROOT/'scripts/game-focus.ps1')],check=True)
 lua('server',('' if a.keep else 'for _,e in ipairs(mmdhl.Entities()) do e:Remove() end ')+f"""local p=player.GetHumans()[1]; local pos=p:GetPos()+Vector(100,#mmdhl.Entities()*130,12); MMDHL_SPAWN_RESULT=nil; mmdhl.Spawn(p,"{data['asset']}",{{backend="source",position={{pos:Unpack()}},angles={{0,0,0}},height={a.height},frozen=true}},function(e,err) MMDHL_SPAWN_RESULT={{entity=IsValid(e) and e:EntIndex(),error=err}} end);return true""")
 for attempt in range(100):
  result=lua('server','return MMDHL_SPAWN_RESULT')
  if result:break
  time.sleep(.1)
 if not result or not result.get('entity'):raise RuntimeError(result)
 entity=result['entity'];time.sleep(1)
 lua('client',f"""gui.HideGameUI() local e=Entity({entity}); e:SetupBones(); local head=e:GetBoneMatrix(e:LookupBone('ValveBiped.Bip01_Head1')):GetTranslation(); local foot=e:GetBoneMatrix(e:LookupBone('ValveBiped.Bip01_L_Foot')):GetTranslation();local center=LerpVector(.55,foot,head);local height=head.z-foot.z; hook.Add('CalcView','MMDHL.TestCamera',function() local pos=center+Vector(-height*3.8,height*.18,height*.15) return {{origin=pos,angles=(center-pos):Angle(),fov=40,drawviewer=false}} end) return true""")
 time.sleep(2);print(lua('client',"RunConsoleCommand('mmdhl_debug_capture','"+a.model.lower()+"-native');return {resolution={ScrW(),ScrH()},focus=system.HasFocus(),error=mmdhl.renderError,deform=mmdhl.deformMs,fps=1/RealFrameTime()}"))
 result=lua('server',f'local d=mmdhl.GetDiagnostics(Entity({entity}));d.bodyList=nil;return d');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
