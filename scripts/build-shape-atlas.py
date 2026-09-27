"""Development-only corpus audit. Ships normalized anatomy, never source paths/art.

Requires numpy/scipy (build/python-deps is supported). Corpus inputs stay local.
Rigid registration checks landmarks in addition to the older length-only audit.
Acceptance character families never enter the reference atlas.
"""
import concurrent.futures, hashlib, importlib.util, json, pathlib, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'build/python-deps'))
import numpy as np
from scipy.spatial import ConvexHull, HalfspaceIntersection
from scipy.optimize import linprog
spec=importlib.util.spec_from_file_location('corpus',ROOT/'scripts/calibrate-fitter.py')
corpus=importlib.util.module_from_spec(spec);spec.loader.exec_module(corpus)

def rotation(q):
 x,y,z,w=q
 return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
 [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
 [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])

def reduce(points,cap=64):
 points=np.unique(np.round(points,6),axis=0)
 points=points[ConvexHull(points).vertices]
 if len(points)>cap:
  chosen=[int(np.argmax(np.linalg.norm(points-points.mean(0),axis=1)))];distance=np.full(len(points),np.inf)
  while len(chosen)<cap:
   distance=np.minimum(distance,((points-points[chosen[-1]])**2).sum(1));chosen.append(int(np.argmax(distance)))
  points=points[chosen]
 return points

def plane_vertices(equations):
 solve=linprog([0,0,0,-1],A_ub=np.column_stack([equations[:,:3],np.linalg.norm(equations[:,:3],axis=1)]),b_ub=-equations[:,3],bounds=[(None,None)]*3+[(0,None)],method='highs')
 if not solve.success or solve.x[3]<1e-8:return None
 return HalfspaceIntersection(equations,solve.x[:3]).intersections

def intersection_score(a,b):
 ha,hb=ConvexHull(a),ConvexHull(b);points=plane_vertices(np.concatenate([ha.equations,hb.equations]))
 if points is None:return 0.
 volume=ConvexHull(points).volume
 return float(volume/(ha.volume+hb.volume-volume))

def audit(pair):
 out={'folder':pair['folder'],'split':pair.get('split'),'accepted':False}
 try:
  path=pathlib.Path(pair['path']);ref,clouds=corpus.smd(pathlib.Path(pair['folder'])/'5_propo/Physics.smd')
  # Cache diagnostics by content and executable: useful for repeatable development runs.
  rig=corpus.fit(path);bones={b['name']:b for b in rig['bones']}
  # SCMI deliberately relocates the three torso pivots. Register unchanged head/limb landmarks;
  # measure torso hulls relative to the original PMX pivots after that registration.
  names=[n for n in corpus.PRIMARY if bones[n]['mmd']>=0 and not any(n.endswith(x) for x in ('Pelvis','Spine1','Spine4'))]
  source=np.array([bones[n]['position'] for n in names]);target=np.array([ref[n][:3,3] for n in names])
  a=source-source.mean(0);b=target-target.mean(0);u,d,vt=np.linalg.svd(a.T@b);r=u@vt
  if np.linalg.det(r)<0:u[:,-1]*=-1;r=u@vt
  scale=float((a@r*b).sum()/(a*a).sum());translation=target.mean(0)-source.mean(0)@r*scale
  errors=np.linalg.norm(source@r*scale+translation-target,axis=1)
  height=float(np.linalg.norm(np.array(bones['ValveBiped.Bip01_Head1']['position'])-np.mean([bones['ValveBiped.Bip01_'+s+'_Foot']['position'] for s in ('L','R')],axis=0)))
  out['registrationError']=float(errors.max()/max(height*scale,.01))
  if out['registrationError']>.025:raise ValueError('PMX and compiled landmarks disagree after rigid registration')
  bodies={}
  for name in corpus.PRIMARY:
   local=((clouds[name]-translation)@r.T/scale-np.array(bones[name]['position']))@rotation(bones[name]['rotation'])/height
   points=reduce(local);hull=ConvexHull(points);lo,hi=points.min(0),points.max(0);center=(lo+hi)/2;extent=(hi-lo)/2
   if extent.min()<.0005 or extent.max()>.32 or np.linalg.norm(center)>.4:raise ValueError('Degenerate or non-anatomical primary hull')
   bodies[name]={'center':center.tolist(),'extent':extent.tolist(),'shape':((points-center)/extent).tolist(),'volume':float(hull.volume)}
  out.update(accepted=True,bodies=bodies,features={b['name']:b.get('features') for b in rig['bodies']},family=pair.get('family',pair['folder']),sourceHash=hashlib.sha256(path.read_bytes()).hexdigest())
 except Exception as e:out['error']=str(e)
 return out

def main():
 pairs=json.loads((ROOT/'validation/fitting-corpus.json').read_text(encoding='utf-8'))['pairs']
 pairs=[p for p in pairs if p.get('paired')];results=[]
 if '--reuse-audit' in sys.argv:
  results=json.loads((ROOT/'validation/shape-atlas-audit.json').read_text(encoding='utf-8'))['results']
 else:
  with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
   for r in pool.map(audit,pairs):
    results.append(r)
    if len(results)%25==0:print(f'{len(results)}/{len(pairs)} audited',flush=True)
 training=[r for r in results if r['accepted'] and r['split']=='calibration']
 directions=np.array([[x,y,z] for x in (-1,0,1) for y in (-1,0,1) for z in (-1,0,1) if x or y or z],dtype=float)
 directions/=np.linalg.norm(directions,axis=1)[:,None]
 atlas={'version':2,'method':'PMX-registered supporting planes / family holdout','directions':directions.tolist(),'bodies':{}}
 for name in corpus.PRIMARY:
  samples=[r['bodies'][name] for r in training]
  centers=np.array([v['center'] for v in samples]);extents=np.array([v['extent'] for v in samples])
  features=np.concatenate([centers,np.log(extents)],axis=1);normalized=(features-features.mean(0))/np.maximum(features.std(0),.01)
  chosen=[int(np.argmin((normalized**2).sum(1)))];distance=np.full(len(samples),np.inf)
  while len(chosen)<min(8,len(samples)):
   distance=np.minimum(distance,((normalized-normalized[chosen[-1]])**2).sum(1));chosen.append(int(np.argmax(distance)))
  atlas['bodies'][name]={'count':len(samples),'center':np.median(centers,0).tolist(),'extent':np.median(extents,0).tolist(),
   'centerLow':np.quantile(centers,.05,axis=0).tolist(),'centerHigh':np.quantile(centers,.95,axis=0).tolist(),
   'extentLow':np.quantile(extents,.05,axis=0).tolist(),'extentHigh':np.quantile(extents,.95,axis=0).tolist(),
   'templates':[samples[i] for i in chosen]}
  # Predict support planes from surface quantiles, with ridge strength selected
  # by family folds inside calibration only. No acceptance/evaluation targets.
  supportScale=np.median(extents,0)
  atlas['bodies'][name]['supportScale']=supportScale.tolist()
  x=np.array([r['features'][name] for r in training]);y=np.array([(((np.array(s['shape'])*s['extent']+s['center'])/supportScale)@directions.T).max(0) for s in samples])
  folds=np.array([int(hashlib.sha256(r['family'].encode()).hexdigest()[:8],16)%5 for r in training])
  def coefficients(a,b,alpha):
   mean=a.mean(0);std=np.maximum(a.std(0),.002);z=(a-mean)/std;offset=b.mean(0)
   return mean,std,offset,np.linalg.solve(z.T@z+np.eye(z.shape[1])*alpha,z.T@(b-offset))
  scores=[]
  for alpha in (1.,3.,10.,30.,100.):
   errors=[]
   for fold in range(5):
    keep=folds!=fold;test=~keep
    if not test.any():continue
    mean,std,offset,c=coefficients(x[keep],y[keep],alpha)
    errors.extend(np.mean((((x[test]-mean)/std)@c+offset-y[test])**2,axis=1))
   scores.append((np.mean(errors),alpha))
  error,alpha=min(scores);mean,std,offset,c=coefficients(x,y,alpha)
  span=np.maximum(np.quantile(y,.99,axis=0)-np.quantile(y,.01,axis=0),.005)
  atlas['bodies'][name]['supportRegression']={'mean':mean.tolist(),'std':std.tolist(),'intercept':offset.tolist(),'coefficients':c.T.tolist(),
   'low':(np.quantile(y,.01,axis=0)-span*.15).tolist(),'high':(np.quantile(y,.99,axis=0)+span*.15).tolist(),'alpha':alpha,'familyCV_RMSE':float(np.sqrt(error))}
  # Choose between a support-plane envelope and a real anatomical convex.
  # All geometry and algorithm selection remain inside calibration family folds.
  # Templates preserve the tapered profiles that a 26-plane envelope rounds out.
  shapes=[np.array(v['shape']) for v in samples]
  golds=[shape*sample['extent']+sample['center'] for shape,sample in zip(shapes,samples)]
  comparison=[];reg=atlas['bodies'][name]['supportRegression']
  for fold in range(5):
   keep=folds!=fold;test=~keep
   if not test.any():continue
   fm,fs,fo,fc=coefficients(x[keep],y[keep],alpha)
   predictions=((x[test]-fm)/fs)@fc+fo
   for prediction,idx in zip(predictions,np.flatnonzero(test)):
    if idx%5:continue
    prediction=np.clip(prediction,reg['low'],reg['high'])
    poly=plane_vertices(np.column_stack([directions/supportScale,-prediction]))
    if poly is None:continue
    lo,hi=poly.min(0),poly.max(0);center=(lo+hi)/2;extent=(hi-lo)/2
    candidates=[shapes[k]*extent+center for k in np.flatnonzero(keep)]
    errors=[np.mean(((candidate/supportScale@directions.T).max(0)-prediction)**2) for candidate in candidates]
    best=candidates[int(np.argmin(errors))]
    comparison.append([intersection_score(poly,golds[idx]),intersection_score(best,golds[idx])])
  measured=np.array(comparison);median=np.median(measured,0);tail=np.quantile(measured,.1,axis=0)
  useTemplate=bool(median[1]>median[0]+.03 and tail[1]>=tail[0]-.02)
  atlas['bodies'][name]['surfaceSelection']={'samples':len(comparison),'medianIoU':median.tolist(),'p10IoU':tail.tolist(),'useTemplate':useTemplate}
  if useTemplate:atlas['bodies'][name]['surfaceTemplates']=[shape.tolist() for shape in shapes]
 encoded=json.dumps(atlas,separators=(',',':'))
 # Split literals to remain below MSVC's per-string limit.
 chunks=[encoded[i:i+12000] for i in range(0,len(encoded),12000)]
 header='#pragma once\nnamespace mmd { inline constexpr char ShapeAtlas[]=\n'+''.join('R"ATLAS('+v+')ATLAS"\n' for v in chunks)+'; }\n'
 (ROOT/'native/shape_atlas.hpp').write_text(header,encoding='utf-8')
 (ROOT/'validation/shape-atlas-audit.json').write_text(json.dumps({'training':len(training),'results':results},ensure_ascii=False),encoding='utf-8')
 print(f'Atlas: {len(training)} registered training pairs; {len(encoded)} bytes; held-out geometry excluded')
if __name__=='__main__':main()
