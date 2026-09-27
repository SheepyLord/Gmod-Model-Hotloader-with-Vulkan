"""Capture a short, timestamped full-rig motion sequence in the owned game."""
import argparse,json,math,pathlib,subprocess,time
from gamectl import execute,ROOT,read

def lua(realm,code):
 result=execute(realm,code,45)
 if not result['ok']:raise RuntimeError(result.get('error'))
 return result.get('value')

def main():
 p=argparse.ArgumentParser();p.add_argument('model',choices=['Xin','Cyrene','Sandrone']);p.add_argument('--backend',choices=['reference','cpu_mt','gpu_opencl'],default='reference');p.add_argument('--origin',help='Source x,y,z; otherwise trace the current map ground');p.add_argument('--frames',type=int,default=12);p.add_argument('--interval',type=float,default=.2);a=p.parse_args()
 assert 2<=a.frames<=60 and .1<=a.interval<=2
 asset=read(ROOT/'validation/acceptance-models.json')[a.model]['asset'];origin=[float(v) for v in a.origin.split(',')] if a.origin else None
 if origin is not None and (len(origin)!=3 or not all(math.isfinite(v) for v in origin)):p.error('Origin must contain three finite coordinates')
 session=read(ROOT/'validation/session.json');prefix='motion-'+a.model.lower()+'-'+a.backend;records=[]
 # Focus the owned game through the computer-use tool before calling this probe.
 try:
  position='Vector('+','.join(str(v) for v in origin)+')' if origin else "util.TraceLine({start=Vector(0,0,2048),endpos=Vector(0,0,-30000),mask=MASK_SOLID_BRUSHONLY}).HitPos+Vector(0,0,12)"
  entity=lua('server',"for _,e in ipairs(mmdhl.Entities()) do e:Remove() end local p=player.GetHumans()[1] local origin="+position+" MMDHL_MOTION_PLAYER={p=p,position=p:GetPos(),angles=p:EyeAngles(),move=p:GetMoveType()} p:SetMoveType(MOVETYPE_NOCLIP) p:SetPos(origin+Vector(-290,40,100)) local e=mmdhl.SpawnNative(p,"+json.dumps(asset)+",{scaleMultiplier=1,position={origin:Unpack()},frozen=true,secondaryBackend="+json.dumps(a.backend)+"}) MMDHL_MOTION=e return e:EntIndex()")
  deadline=time.monotonic()+15
  while not lua('client',f"local e=Entity({entity}) if not IsValid(e) then return false end e:SetupBones() local h=e:LookupBone('ValveBiped.Bip01_Head1') local f=e:LookupBone('ValveBiped.Bip01_L_Foot') return h and f and e:GetBoneMatrix(h)~=nil and e:GetBoneMatrix(f)~=nil"):
   if time.monotonic()>=deadline:raise RuntimeError('Motion model did not replicate with a valid skeleton')
   time.sleep(.2)
  lua('client',f"""gui.HideGameUI() if IsValid(g_SpawnMenu) then g_SpawnMenu:Close() end
 for _,name in ipairs({{'MMDHL.TestCamera','MMDHL.VisualCamera','MMDHL.StressCamera'}}) do hook.Remove('CalcView',name) end
 local e=Entity({entity}) e:SetupBones() local head=e:GetBoneMatrix(e:LookupBone('ValveBiped.Bip01_Head1')):GetTranslation() local foot=e:GetBoneMatrix(e:LookupBone('ValveBiped.Bip01_L_Foot')):GetTranslation() local center=LerpVector(.5,foot,head)+Vector(0,0,12) local offset=Vector(-290,40,45)
 hook.Add('CalcView','MMDHL.MotionCamera',function() return {{origin=center+offset,angles=(-offset):Angle(),fov=40,drawviewer=false}} end) return true""")
  lua('server',"""local e=MMDHL_MOTION local body=e:GetPhysicsObjectNum(0) local anchor=body:GetPos() local angles=body:GetAngles() local start=CurTime()
 for i=1,17 do local b=e:GetPhysicsObjectNum(i) b:EnableMotion(true) b:Wake() end
 hook.Add('Tick','MMDHL.MotionCapture',function() if not IsValid(e) then return end local t=CurTime()-start
 body:SetPos(anchor+Vector(math.sin(t*2)*16,math.sin(t*2.3)*20,20+math.sin(t*2.7)*10)) body:SetAngles(angles+Angle(math.sin(t*2)*10,math.sin(t*1.7)*20,0))
 for i=1,17 do e:GetPhysicsObjectNum(i):Wake() end end) return true""")
  time.sleep(2)
  for frame in range(a.frames):
   name=f'{prefix}-{frame:03d}'
   sample=lua('server',"local e=MMDHL_MOTION local poses={} for i=0,17 do local b=e:GetPhysicsObjectNum(i) poses[i+1]={position={b:GetPos():Unpack()},angles={b:GetAngles():Unpack()}} end local d=mmdhl.GetDiagnostics(e) d.bodyList=nil return {time=SysTime(),poses=poses,diagnostics=d}")
   sample['capture']=lua('client',"RunConsoleCommand('mmdhl_debug_capture',"+json.dumps(name)+") return {time=SysTime(),resolution={ScrW(),ScrH()},error=mmdhl.renderError}");sample['image']=name+'.png';records.append(sample)
   time.sleep(a.interval)
  directory=pathlib.Path(session['cache'])/'debug'/session['token']
  for r in records:assert (directory/r['image']).exists(),r['image']
  report={'model':a.model,'backend':a.backend,'session':session['token'],'directory':str(directory),'frames':records}
  (ROOT/f'validation/{prefix}.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
  print(f'{a.model}: {len(records)} motion frames saved to {directory}',flush=True)
 finally:
  lua('server',"hook.Remove('Tick','MMDHL.MotionCapture') if IsValid(MMDHL_MOTION) then MMDHL_MOTION:Remove() end local s=MMDHL_MOTION_PLAYER if s and IsValid(s.p) then s.p:SetMoveType(s.move) s.p:SetPos(s.position) s.p:SetEyeAngles(s.angles) end MMDHL_MOTION_PLAYER=nil return true")
  lua('client',"hook.Remove('CalcView','MMDHL.MotionCamera') return true")

if __name__=='__main__':main()
