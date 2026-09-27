"""Export bounded, family-split collision dimension priors from an audited report."""
import json,pathlib,numpy as np
ROOT=pathlib.Path(__file__).resolve().parents[1]
report=json.loads((ROOT/'validation/fitting-corpus.json').read_text(encoding='utf-8'))
names=list(report['calibration']);factors={};counts={}
for name in names:
 suffix=name.split('Bip01_')[1];base=suffix[2:] if suffix.startswith(('L_','R_')) else suffix
 values=[m['ratio'] for r in report['pairs'] if r.get('split')=='calibration' for m in r['measurements'] if m['confidence']>=.5 and m['bone'].split('Bip01_')[1] in [base,'L_'+base,'R_'+base]]
 if len(values)<10:raise ValueError('Insufficient verified calibration samples: '+name)
 factors[name]=np.clip(np.median(values,axis=0),.65,1.65).tolist();counts[name]=len(values)
metrics={}
for split in ['calibration','evaluation','acceptance_holdout']:
 before=[];after=[]
 for r in report['pairs']:
  if r.get('split')!=split:continue
  for m in r['measurements']:
   if m['confidence']<.5:continue
   ratios=np.array(m['ratio']);before.extend(abs(np.log(ratios)));after.extend(abs(np.log(ratios/factors[m['bone']])))
 metrics[split]={'dimensions':len(before),'medianAbsoluteLogErrorBefore':float(np.median(before)),'medianAbsoluteLogErrorAfter':float(np.median(after)),'p95AbsoluteLogErrorAfter':float(np.quantile(after,.95))}
encoded=json.dumps({'version':1,'factors':factors,'samples':counts},separators=(',',':'))
(ROOT/'native/fitter_calibration.hpp').write_text('// Aggregate dimension priors from 160 calibration pairs, split by character family; no model geometry.\n#pragma once\nnamespace mmd { inline constexpr char FitterCalibration[]=R"FIT('+encoded+')FIT"; }\n',encoding='utf-8')
(ROOT/'validation/fitter-evaluation.json').write_text(json.dumps(metrics,indent=2),encoding='utf-8')
print(json.dumps(metrics,indent=2))
