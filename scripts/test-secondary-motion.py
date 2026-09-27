"""Ten-second full-rig motion probes in an owned, addons-enabled GMod session."""
import argparse,json,time,pathlib
from gamectl import ROOT,execute,read
def lua(realm,code):
    r=execute(realm,code)
    if not r['ok']:raise RuntimeError(r.get('error'))
    return r.get('value')
def main():
    p=argparse.ArgumentParser();p.add_argument('label');p.add_argument('--scenario',choices=['sandwich','pelvis','carry','sleeve'],default='sandwich');p.add_argument('--backend',default='cpu_mt_v2');p.add_argument('--model',default='Sandrone');p.add_argument('--speed',type=float,default=240);p.add_argument('--stretch',choices=['0','1']);a=p.parse_args()
    assert a.label.replace('-','').isalnum()
    model=next(m for m in json.loads((ROOT/'tests/v2-assets.json').read_text(encoding='utf-8')) if m['label']==a.model)
    session=read(ROOT/'validation/session.json');directory=pathlib.Path(session['cache'])/'debug'/session['token']
    config=dict(label=a.label,asset=model['asset'],backend=a.backend,scenario=a.scenario,speed=a.speed)
    source=(ROOT/'tests/game/secondary-motion.lua').read_text(encoding='utf-8')
    previous={}
    try:
        settings={'mmdhl_secondary_iterations':'10','mmdhl_secondary_gravity':'1','mmdhl_secondary_damping':'1','mmdhl_secondary_stretch':'0'}
        if a.stretch is not None:
            settings['mmdhl_secondary_stretch']=a.stretch
            config['stretch']=a.stretch=='1'
        for name,value in settings.items():
            previous[name]=lua('client',"return GetConVar("+json.dumps(name)+"):GetString()")
            lua('client',"GetConVar("+json.dumps(name)+"):SetString("+json.dumps(value)+") return true")
        config['tuning']=settings
        config.update(lua('server','MMDHL_MOTION_CONFIG=util.JSONToTable('+json.dumps(json.dumps(config))+')\n'+source))
        lua('client','MMDHL_MOTION_CONFIG=util.JSONToTable('+json.dumps(json.dumps(config))+')\n'+source)
        started=time.monotonic()
        while time.monotonic()-started<30:
            data=read(directory/(a.label+'.json'))
            if data and data.get('finished'):break
            time.sleep(.3)
        else:raise TimeoutError('Motion probe stopped responding')
        data.update(config=config,session=session,model=model)
        for image in directory.glob(a.label+'-*.jpg'):(ROOT/'validation'/image.name).write_bytes(image.read_bytes())
        samples=data['samples'];tail=[d for d in samples if d['sampleTime']>=8]
        data['fullRigsVerified']=all(d['bodies']==model['rigidBodies'] and d['authoredJoints']==model['joints'] and d['solverIterations']==10 for d in samples)
        data['stretchVerified']=a.stretch!='1' or samples[-1]['stretchCorrections']>0
        data['clockPassed']=samples[0]['resets']==samples[-1]['resets'] and samples[0]['droppedTime']==samples[-1]['droppedTime']
        data['functionalPassed']=data['fullRigsVerified'] and data['stretchVerified'] and data['clockPassed'] and all(not d.get('sourceError') and not d.get('asyncError') for d in samples) and all(d['belowFloor']==0 for d in tail)
        data['headIsolationPassed']=a.scenario!='pelvis' or (all(d.get('headBodies',0)>0 for d in samples) and max(d['headPresentationError'] for d in tail)<.1)
        data['visibleFrames']=sum(f['drawn'] for f in data['frames'])
        data['visiblePassed']=data['visibleFrames']>=.95*len(data['frames'])
        data['focusedPassed']=all(f['focus'] for f in data['frames'])
        moving=[d for d in samples if 2<=d['sampleTime']<4.8]
        data['attachmentLeadMovingSource']=sum(d['attachmentLead'] for d in moving)/len(moving)
        data['attachmentLeadStoppedSource']=sum(d['attachmentLead'] for d in tail)/len(tail)
        out=ROOT/'validation'/(a.label+'.json');out.write_text(json.dumps(data,indent=2),encoding='utf-8')
        print(json.dumps(dict(label=a.label,minHeight=min(d['minHeight'] for d in samples),endBelow=max(d['belowFloor'] for d in tail),endJointError=max(d['maxLinearLimitError'] for d in tail),contacts=max(d['worldContacts'] for d in samples),corrections=max(d.get('surfaceCorrections',0) for d in samples),unfocused=sum(not f['focus'] for f in data['frames']),visible=data['visiblePassed'],attachmentLeadMoving=data['attachmentLeadMovingSource'],attachmentLeadStopped=data['attachmentLeadStoppedSource'],headIsolationPassed=data['headIsolationPassed'],functionalPassed=data['functionalPassed']),indent=2))
        if not all(data[k] for k in ['functionalPassed','headIsolationPassed','visiblePassed','focusedPassed']):raise RuntimeError('Motion regression failed; inspect '+str(out))
    finally:
        for name,value in previous.items():lua('client',"GetConVar("+json.dumps(name)+"):SetString("+json.dumps(value)+") return true")
        lua('server',"hook.Remove('Tick','MMDHL.MotionBugProbe') if MMDHL_MOTION and IsValid(MMDHL_MOTION.ent) then MMDHL_MOTION.ent:Remove() end return true")
        lua('client',"hook.Remove('CalcView','MMDHL.MotionBugCamera') hook.Remove('PostRender','MMDHL.MotionBugSample') hook.Remove('PreDrawViewModel','MMDHL.MotionBugHideGun') hook.Remove('HUDShouldDraw','MMDHL.MotionBugHideHUD') if MMDHL_MOTION_ORIGINAL_DRAW then mmdhl.DrawCarrier=MMDHL_MOTION_ORIGINAL_DRAW MMDHL_MOTION_ORIGINAL_DRAW=nil end return true")
if __name__=='__main__':main()
