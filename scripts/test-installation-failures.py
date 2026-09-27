"""Fault injection only in the disposable client1-isolated game root."""
import json,os,shutil,struct,subprocess,time
from gamectl import execute,ROOT,read
from compatibility_profiles import lua_policy,write_policy

PWSH=shutil.which('pwsh') or str(__import__('pathlib').Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe')
game=(ROOT/'validation/multiplayer/client1-isolated').resolve()
assert game.parent==(ROOT/'validation/multiplayer').resolve()
report={}
def ps(script,*args):
    result=subprocess.run([PWSH,'-NoProfile','-File',str(ROOT/'scripts'/script),*map(str,args)],capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
def stop():
    if (ROOT/'validation/session.json').exists() and __import__('pathlib').Path(read(ROOT/'validation/session.json')['gameRoot']).resolve()==game:ps('game-stop.ps1')
def install():ps('install.ps1','-GameRoot',game,'-InstallAddon')
def start():ps('game-start.ps1','-GameRoot',game,'-SkipInstall','-NoWorkshop','-Map','gm_construct','-Width',1920,'-Height',1080)
def call(realm,code):
    r=execute(realm,code,30)
    assert r['ok'],r
    return r.get('value')
def state(realm):return call(realm,'return mmdhl.GetInstallationStatus()')
def settled():
    end=time.monotonic()+20
    while time.monotonic()<end:
        s=state('client')
        if s['features']['imports'] or not s['features']['core']:return s
        time.sleep(.2)
    raise TimeoutError('worker startup')
engine=game/'bin/win64/engine.dll'
original=engine.read_bytes()
def replace(path,data):
    # Atomic replacement breaks the fixture's hardlink; never modify Steam's inode.
    temp=path.with_suffix(path.suffix+'.installation-test-tmp');temp.write_bytes(data)
    for attempt in range(50):
        try:os.replace(temp,path);return
        except PermissionError:
            if attempt==49:raise
            time.sleep(.1)
try:
    stop();install()
    altered=bytearray(original);nt=struct.unpack_from('<I',altered,0x3c)[0];altered[nt+8]^=1;replace(engine,altered)
    replace(game/'garrysmod/lua/bin/lib_coacd.dll',b'damaged optional fixture')
    start();s=settled();assert s['features']['core'] and s['features']['imports'] and not s['features']['detailedCollision'],s
    assert any(x['name']=='engine.dll' and x['match']=='abi-evidence' for x in s['compatibility']['libraries'])
    call('client',"mmdhl.Open() return true")
    call('client',"RunConsoleCommand('mmdhl_debug_capture','installation-warning','ui') return true")
    time.sleep(.5)
    session=read(ROOT/'validation/session.json')
    report['warningCapture']=str(__import__('pathlib').Path(session['cache'])/'debug'/session['token']/'installation-warning.png')
    report['newGameHashAndOptionalFailure']=s
    source=(ROOT/'tests/fixtures/props/cube.obj').as_posix()
    job=call('client',f"local h,e=mmdhl.native.BeginImport([[{source}]],'{{\"kind\":\"static\",\"collision\":\"balanced\"}}') assert(h,e) return h")
    for _ in range(100):
        result=call('client',f'return mmdhl.Decode(mmdhl.native.PollJob({job}))')
        if result['state']!='running':break
        time.sleep(.1)
    assert result['state']=='complete' and result['info']['collision_method']!='coacd',result
    report['simpleHullFallback']=result['info']['collision_method']
    stop();replace(engine,original);install()
    (game/'garrysmod/lua/bin/gmsv_mmdhl_win64.dll').unlink()
    start();s=settled();server=state('server');assert s['features']['core'] and not server['features']['core']
    remote=None
    for _ in range(100):
        remote=call('client','return mmdhl.serverInstallation')
        if remote:break
        time.sleep(.1)
    assert remote and not remote['features']['core'],remote
    report['missingServer']={'client':s['features'],'server':server,'received':remote}
    stop();install();(game/'garrysmod/lua/bin/gmcl_mmdhl_win64.dll').unlink()
    start();s=state('client');assert not s['features']['core'] and any(x['code']=='missing' for x in s['issues'])
    assert call('client',"RunConsoleCommand('mmdhl_open') return mmdhl.native==nil")
    report['missingClient']=s
    stop();install()
    path=game/'garrysmod/addons/mmd_hotloader/lua/mmdhl/compatibility_policy.lua';policy=lua_policy(path)
    for p in policy['libraries']:
        if p['name']=='engine.dll':p['evidence']['sections'][0]['sha256']='0'*64
    # A game build no profile describes (every game update) is unverified: it keeps
    # rendering behind the runtime interface, slot and class checks, without a warning.
    write_policy(path,policy);start();s=settled()
    assert s['features']['core'] and s['features']['imports'] and s['features']['rendering'],s
    assert not any(x['code']=='game_incompatible' for x in s['issues']),s['issues']
    assert any(x['name']=='engine.dll' and x['match']=='unverified' for x in s['compatibility']['libraries'])
    assert call('client',"local before=GetConVar('mat_queue_mode'):GetInt() local ok=mmdhl.RenderAvailable() assert(ok) return before==GetConVar('mat_queue_mode'):GetInt()")
    report['abiUnverified']=s
    print('PASS: actual game accepts new DLL hash with matching evidence; optional corruption uses simple hull; missing server is communicated; missing client retains UI; an unverified game build keeps rendering without changing queue mode')
finally:
    stop();replace(engine,original);install()
    (ROOT/'validation/installation-game-failures.json').write_text(json.dumps(report,indent=2),encoding='utf8')
