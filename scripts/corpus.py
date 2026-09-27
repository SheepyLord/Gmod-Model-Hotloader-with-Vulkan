"""Bounded worker-process corpus inspection; stores paths only in ignored validation output."""
import argparse,concurrent.futures,json,pathlib,subprocess,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
def inspect(path):
 start=time.perf_counter()
 try:
  r=subprocess.run([str(ROOT/'build/bin/Release/mmdhl_worker.exe'),'--inspect',str(path)],capture_output=True,timeout=40,encoding='utf-8',errors='replace')
  j=json.loads(r.stdout if r.returncode==0 else r.stderr)
  return {'path':str(path),'ok':r.returncode==0,'seconds':time.perf_counter()-start,**{k:j.get(k) for k in ['vertices','triangles','bones','rigidBodies','joints','softBodies','error']}}
 except Exception as e:return {'path':str(path),'ok':False,'error':str(e),'seconds':time.perf_counter()-start}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('directory',type=pathlib.Path);a=p.parse_args()
 paths=sorted(a.directory.glob('*/1_PMX/**/*.pmx'))
 results=[]
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
  for r in pool.map(inspect,paths):
   results.append(r)
   if len(results)%50==0:print(len(results),'/',len(paths),flush=True)
 report={'count':len(results),'passed':sum(r['ok'] for r in results),'failed':sum(not r['ok'] for r in results),'models':results}
 (ROOT/'validation/corpus.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
 print({k:v for k,v in report.items() if k!='models'})
