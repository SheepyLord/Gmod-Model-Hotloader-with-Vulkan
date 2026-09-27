"""Audit Source/PMX corpus correspondences before using collision dimensions.
All paths, per-character geometry and reports stay in ignored validation/.
A family holdout prevents alternate costumes leaking into calibration.
"""
import argparse,concurrent.futures,hashlib,json,math,pathlib,re,subprocess
import numpy as np
from gamectl import ROOT
WORKER=ROOT/'build/bin/Release/mmdhl_worker.exe'
FIT_VERSION=hashlib.sha256((WORKER.parent/'mmdhl_runtime_win64.dll').read_bytes()).digest()
PRIMARY=['Pelvis','Spine1','Spine4','Head1']+[s+b for s in ('L_','R_') for b in ('Clavicle','UpperArm','Forearm','Hand')]+[s+b for s in ('L_','R_') for b in ('Thigh','Calf','Foot')]
PRIMARY=['ValveBiped.Bip01_'+x for x in PRIMARY]
EDGES=[(s+a,s+b) for s in ('L_','R_') for a,b in [('UpperArm','Forearm'),('Forearm','Hand'),('Thigh','Calf'),('Calf','Foot')]]
def smd(path):
 names={};parents={};local={};points={};state='';frame=0
 for line in path.read_text(encoding='utf-8-sig',errors='replace').splitlines():
  line=line.strip()
  if line in ('nodes','skeleton','triangles','end'):state=line;continue
  if state=='nodes':
   m=re.fullmatch(r'(\d+)\s+"([^"]+)"\s+(-?\d+)',line)
   if m:idx=int(m[1]);names[idx]=m[2];parents[idx]=int(m[3])
  elif state=='skeleton':
   v=line.split()
   if not v:continue
   if v[0]=='time':frame=int(v[1]);continue
   if frame or len(v)!=7:continue
   idx=int(v[0]);x,y,z=map(float,v[4:]);cx,sx=math.cos(x),math.sin(x);cy,sy=math.cos(y),math.sin(y);cz,sz=math.cos(z),math.sin(z)
   t=np.eye(4);t[:3,:3]=np.array([[cz,-sz,0],[sz,cz,0],[0,0,1]])@np.array([[cy,0,sy],[0,1,0],[-sy,0,cy]])@np.array([[1,0,0],[0,cx,-sx],[0,sx,cx]]);t[:3,3]=list(map(float,v[1:4]));local[idx]=t
  elif state=='triangles':
   v=line.split()
   if len(v)<9:continue
   idx=int(v[0]);point=list(map(float,v[1:4]));links=int(v[9]) if len(v)>9 else 0
   if links:
    weights=[(int(v[10+2*i]),float(v[11+2*i])) for i in range(links)];idx,weight=max(weights,key=lambda k:k[1])
    if weight<.95:raise ValueError('Collision mesh has blended weights')
   points.setdefault(idx,[]).append(point)
 global_={}
 def resolve(i):
  if i not in global_:global_[i]=(resolve(parents[i]) if parents[i]>=0 else np.eye(4))@local[i]
  return global_[i]
 for i in names:resolve(i)
 return {names[i]:t for i,t in global_.items()},{names[i]:np.array(p) for i,p in points.items()}
def fit(path):
 content=path.read_bytes();key=hashlib.sha256(content+FIT_VERSION).hexdigest();dest=ROOT/'validation/fit-cache'/f'{key}.json';dest.parent.mkdir(exist_ok=True)
 if dest.exists():return json.loads(dest.read_text(encoding='utf-8'))
 r=subprocess.run([str(WORKER),'--fit-raw',str(path)],capture_output=True,encoding='utf-8',errors='replace',timeout=45)
 if r.returncode:raise ValueError(r.stderr[:300])
 result=json.loads(r.stdout);dest.write_text(json.dumps(result),encoding='utf-8');return result
def family(name):
 value=re.sub(r'[^a-z\u4e00-\u9fff]','',name.lower())
 # Costume/version labels have many variants; preserve a broad family prefix.
 return value[:5]
def candidate(folder):
 result={'folder':str(folder),'family':family(folder.name),'paired':False}
 try:
  stage=folder/'5_propo';qc=next(iter(stage.glob('*.qc')),None)
  if qc is None:raise ValueError('No QC')
  text=qc.read_text(encoding='utf-8-sig',errors='replace');origin=re.search(r'\$origin\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)',text,re.I)
  result['origin']=list(map(float,origin.groups())) if origin else [0,0,0]
  if not re.search(r'\$collisionjoints\s+"?Physics.smd',text,re.I):raise ValueError('QC does not identify Physics.smd as collision input')
  ref,points=smd(stage/'Physics.smd')
  if not all(n in ref and n in points for n in PRIMARY):raise ValueError('Reference does not contain all 18 primary bodies')
  prop=stage/'anims/proportions.smd'
  if not prop.exists():raise ValueError('No proportion reference pose')
  pose,_=smd(prop);mismatch=max(np.linalg.norm(ref[n][:3,3]-pose[n][:3,3]) for n in PRIMARY if n in pose)
  result['proportionPoseDifference']=float(mismatch)
  if mismatch>1:raise ValueError('Physics bind pose and proportion pose disagree')
  paths=list((folder/'1_PMX').rglob('*.pmx'));result['pmxCandidates']=len(paths)
  if not paths or len(paths)>12:raise ValueError('Missing or ambiguous PMX source inventory')
  scores=[]
  for p in paths:
   try:
    rig=fit(p);bones={b['name']:np.array(b['position']) for b in rig['bones']};ratios=[]
    for a,b in EDGES:
     a='ValveBiped.Bip01_'+a;b='ValveBiped.Bip01_'+b
     ratios.append(np.linalg.norm(ref[a][:3,3]-ref[b][:3,3])/np.linalg.norm(bones[a]-bones[b]))
    scale=float(np.median(ratios));error=float(max(abs(np.array(ratios)/scale-1)));scores.append((error,str(p),scale,rig))
   except Exception:continue
  scores.sort(key=lambda x:x[0]);result['scores']=[{'error':s[0],'path':s[1]} for s in scores]
  if not scores or scores[0][0]>.15:raise ValueError('Limb proportions do not validate this pairing')
  if len(scores)>1 and scores[1][0]-scores[0][0]<.01:raise ValueError('Several PMX variants match equally; cannot choose a verified source')
  error,path,scale,rig=scores[0];result.update(paired=True,path=path,scale=scale,proportionError=error)
  measurements=[]
  for b in rig['bodies']:
   n=b['name'];world=points[n];local=(world-ref[n][:3,3])@ref[n][:3,:3];lo,hi=local.min(0),local.max(0)
   # Sorted extents avoid confusing a reference-frame rotation with a size error.
   expected=np.sort((hi-lo)*.5);predicted=np.sort(np.array(b['extent'])*scale)
   measurements.append({'bone':n,'ratio':(expected/np.maximum(predicted,.01)).tolist(),'confidence':b['confidence'],'samples':b['samples']})
  result['measurements']=measurements
  held=any(t in folder.name.lower() for t in ['cyrene','xin','sandrone','昔涟','心','桑多涅'])
  result['split']='acceptance_holdout' if held else 'evaluation' if int(hashlib.sha256(result['family'].encode()).hexdigest()[:8],16)%5==0 else 'calibration'
 except Exception as e:result['reason']=str(e)
 return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('directory',type=pathlib.Path);a=p.parse_args()
 folders=[x for x in a.directory.iterdir() if (x/'1_PMX').is_dir() and (x/'5_propo/Physics.smd').is_file()];out=[]
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
  for r in pool.map(candidate,folders):
   out.append(r)
   if len(out)%25==0:print(f'{len(out)}/{len(folders)} audited; {sum(x["paired"] for x in out)} verified',flush=True)
 report={'candidates':len(out),'verified':sum(x['paired'] for x in out),'pairs':out}
 calibration={}
 for n in PRIMARY:
  values=[m['ratio'] for r in out if r.get('split')=='calibration' for m in r['measurements'] if m['bone']==n and m['confidence']>=.5]
  if len(values)>=10:calibration[n]={'samples':len(values),'medianRatio':np.median(values,axis=0).tolist(),'p10':np.quantile(values,.1,axis=0).tolist(),'p90':np.quantile(values,.9,axis=0).tolist()}
 report['calibration']=calibration
 (ROOT/'validation/fitting-corpus.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
 print({k:v for k,v in report.items() if k not in ['pairs','calibration']})
