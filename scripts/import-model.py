"""Import a local PMX/PMD through the real in-game isolated worker."""
import argparse,json,pathlib,time
from gamectl import execute,ROOT
p=argparse.ArgumentParser();p.add_argument('model',type=pathlib.Path);p.add_argument('--save-test',action='store_true');a=p.parse_args()
source=json.dumps(str(a.model.resolve()),ensure_ascii=False)
r=execute('client',f"local id,e=mmdhl.native.BeginImport({source},'{{}}') if e then error(e) end return id")
if not r['ok']:raise RuntimeError(r)
while True:
 result=execute('client',f"return mmdhl.Decode(mmdhl.native.PollJob({r['value']}))")
 if not result['ok']:raise RuntimeError(result)
 value=result['value']
 if value['state']!='running':break
 time.sleep(.2)
if value['state']!='complete':raise RuntimeError(value)
out={'id':value['asset'],'name':value['info']['name'],'vertices':value['info']['vertices'],'warnings':value['info']['warnings']}
(ROOT/'validation/last-import.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
if a.save_test:(ROOT/'validation/test-asset.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(out,ensure_ascii=False,indent=2))
