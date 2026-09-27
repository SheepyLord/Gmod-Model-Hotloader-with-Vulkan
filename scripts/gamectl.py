"""Token/sequence scoped local game inspection. Native edits require a restart;
Lua and scenarios can be exercised repeatedly in the same running test game.
"""
import argparse,json,pathlib,time,sys,os
if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf-8')
ROOT=pathlib.Path(__file__).resolve().parents[1]
def atomic(path,value):
    temp=path.with_suffix('.tmp');temp.write_text(json.dumps(value),encoding='utf-8')
    # Source's filesystem occasionally holds the destination without DELETE sharing.
    for attempt in range(100):
        try:os.replace(temp,path);return
        except PermissionError:
            if attempt==99:raise
            time.sleep(.01)
def read(path):
    try:return json.loads(path.read_text(encoding='utf-8-sig'))
    except (OSError,ValueError):return None
def execute(realm,code,timeout=30,session_path=None):
    session=read(pathlib.Path(session_path) if session_path else ROOT/'validation/session.json')
    if not session:raise RuntimeError('Start a test game with scripts/game-start.ps1')
    directory=pathlib.Path(session['cache'])/'debug'/session['token'];directory.mkdir(parents=True,exist_ok=True)
    sequence=time.time_ns()//1000000
    request={'token':session['token'],'sequence':sequence,'code':code}
    atomic(directory/f'{realm}-request.json',request)
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        response=read(directory/f'{realm}-response.json')
        if response and response.get('token')==session['token'] and response.get('sequence')==sequence:
            prefix=(session.get('peer','')+'-') if session.get('ownedMultiplayer') else ''
            (ROOT/'validation'/f'{prefix}{realm}-{sequence}.json').write_text(json.dumps(response,ensure_ascii=False,indent=2),encoding='utf-8')
            return response
        time.sleep(.1)
    raise TimeoutError(f'{realm} did not respond in {timeout}s; inspect game log and realm ready reports')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('realm',choices=['server','client']);p.add_argument('code',nargs='?');p.add_argument('--file',type=pathlib.Path);p.add_argument('--timeout',type=float,default=30);p.add_argument('--session',type=pathlib.Path)
    args=p.parse_args()
    try:
        result=execute(args.realm,args.file.read_text(encoding='utf-8') if args.file else args.code,args.timeout,args.session)
        print(json.dumps(result,ensure_ascii=False,indent=2));sys.exit(0 if result['ok'] else 1)
    except Exception as error:print(str(error),file=sys.stderr);sys.exit(2)
