"""Development-only, family-held-out convex IoU against registered SCMI bodies."""
import concurrent.futures, importlib.util, json, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'build/python-deps'))
import numpy as np
from scipy.optimize import linprog
from scipy.spatial import ConvexHull, HalfspaceIntersection
spec=importlib.util.spec_from_file_location('atlas',ROOT/'scripts/build-shape-atlas.py')
atlas=importlib.util.module_from_spec(spec);spec.loader.exec_module(atlas)

def iou(a,b):
    ha,hb=ConvexHull(a),ConvexHull(b)
    equations=np.concatenate([ha.equations,hb.equations])
    # Chebyshev center supplies a strictly interior point for exact intersection.
    solve=linprog([0,0,0,-1],A_ub=np.column_stack([equations[:,:3],np.linalg.norm(equations[:,:3],axis=1)]),b_ub=-equations[:,3],bounds=[(None,None)]*3+[(0,None)],method='highs')
    if not solve.success or solve.x[3]<1e-8:return 0.
    intersection=ConvexHull(HalfspaceIntersection(equations,solve.x[:3]).intersections).volume
    return float(intersection/(ha.volume+hb.volume-intersection))

def evaluate(reference):
    path=next(p['path'] for p in pairs if p['folder']==reference['folder'])
    rig=atlas.corpus.fit(Path(path));bones={b['name']:b for b in rig['bones']}
    height=np.linalg.norm(np.array(bones['ValveBiped.Bip01_Head1']['position'])-np.mean([bones['ValveBiped.Bip01_'+s+'_Foot']['position'] for s in ('L','R')],axis=0))
    scores={}
    for body in rig['bodies']:
        gold=reference['bodies'][body['name']]
        actual=np.array(body['hull'])/height
        expected=np.array(gold['shape'])*gold['extent']+gold['center']
        scores[body['name']]=iou(actual,expected)
    return {'folder':reference['folder'],'split':reference['split'],'scores':scores,'median':float(np.median(list(scores.values())))}

if __name__=='__main__':
    pairs=json.loads((ROOT/'validation/fitting-corpus.json').read_text(encoding='utf-8'))['pairs']
    audit=json.loads((ROOT/'validation/shape-atlas-audit.json').read_text(encoding='utf-8'))
    split='calibration' if '--calibration' in sys.argv else 'holdout'
    references=[r for r in audit['results'] if r['accepted'] and ((r['split']=='calibration')==(split=='calibration'))]
    if split=='calibration':references=references[::5]
    results=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for r in pool.map(evaluate,references):
            results.append(r)
            print(f'{len(results)}/{len(references)} median {r["median"]:.3f}',flush=True)
    values=[v for r in results for v in r['scores'].values()]
    report={'split':split,'pairs':len(results),'bodies':len(values),'medianIoU':float(np.median(values)),'p10IoU':float(np.quantile(values,.1)),
        'byBody':{n:float(np.median([r['scores'][n] for r in results])) for n in atlas.corpus.PRIMARY},'results':results}
    (ROOT/f'validation/shape-quality-{split}.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='results'},indent=2))
