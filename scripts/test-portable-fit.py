"""Exercise the release worker from a clean directory containing only binaries/PMX.

No corpus files, SCMI installation, textures or fitting cache are copied.
Private acceptance geometry/results remain in ignored validation/.
"""
import json, pathlib, shutil, subprocess, time
ROOT=pathlib.Path(__file__).resolve().parents[1]
folder=ROOT/'validation/portable-runtime';folder.mkdir(parents=True,exist_ok=True)
for name in ('mmdhl_worker.exe','mmdhl_runtime_win64.dll'):
    shutil.copyfile(ROOT/'build/bin/Release'/name,folder/name)
session=json.loads((ROOT/'validation/session.json').read_text(encoding='utf-8-sig'))
cache=pathlib.Path(session['cache'])
models=json.loads((ROOT/'validation/acceptance-models.json').read_text(encoding='utf-8'))
report={}
for name,model in models.items():
    path=folder/(name+'.pmx');shutil.copyfile(cache/'assets'/model['asset']/'model.bin',path)
    started=time.perf_counter()
    process=subprocess.run([str(folder/'mmdhl_worker.exe'),'--fit',str(path)],cwd=folder,capture_output=True,encoding='utf-8',check=True,timeout=30)
    elapsed=time.perf_counter()-started;rig=json.loads(process.stdout)
    assert abs(rig['scale']-3.23656)<1e-5
    assert len(rig['bodies'])==18 and len(rig['bones'])==56
    assert all(4<=len(b['hull'])<=64 for b in rig['bodies'])
    report[name]={'coldFitSeconds':elapsed,'generator':rig['generator'],'scale':rig['scale'],'hullVertices':[len(b['hull']) for b in rig['bodies']],
                  'needsReview':[b['name'] for b in rig['bodies'] if b['needsReview']],'maxInitialPenetration':rig['maxInitialPenetration']}
    print(name,round(elapsed,3),'seconds, 18 bodies, portable fit passed',flush=True)
(ROOT/'validation/portable-fit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
