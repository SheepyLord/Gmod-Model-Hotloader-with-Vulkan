"""Import the supplied acceptance assets through the same worker used by the menu."""
import argparse,json,pathlib,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--models',type=pathlib.Path,default=pathlib.Path.home()/'Desktop'/'test_models');p.add_argument('--game',type=pathlib.Path,default=pathlib.Path('H:/SteamLibrary/steamapps/common/GarrysMod'));a=p.parse_args()
    names={'心.pmx':'Xin','星穹铁道—昔涟5.pmx':'Cyrene','桑多涅.pmx':'Sandrone'};out={}
    for source in a.models.rglob('*.pmx'):
        if source.name not in names:continue
        name=names[source.name];directory=ROOT/'validation'/'acceptance-imports'/name;directory.mkdir(parents=True,exist_ok=True)
        request=directory/'request.json';request.write_text(json.dumps({'source':str(source),'cache':str(a.game/'garrysmod/data/mmd_hotloader'),'options':{}}),encoding='utf-8')
        subprocess.run([str(ROOT/'build/bin/Release/mmdhl_worker.exe'),'--request',str(request)],check=True,timeout=300)
        result=json.loads((directory/'status.json').read_text(encoding='utf-8'));out[name]=result;print(name,result.get('asset',result.get('id',result.get('state'))),flush=True)
    if len(out)!=3:raise RuntimeError('Expected exactly three acceptance characters')
    (ROOT/'validation/acceptance-models.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
if __name__=='__main__':main()
