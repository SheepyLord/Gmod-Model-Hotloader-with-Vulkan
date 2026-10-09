"""Fault injection only in the disposable client1-isolated game root.

Native files the policy does not know, damaged or from another build only add
warnings: the module is always loaded. Problems that turn features off come from
failures: a missing module, a CoACD the worker self-test cannot load, a game build
that fails its checks."""
import json,os,re,shutil,struct,subprocess,time
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
def problems(status):return [x for x in status['issues'] if not x.get('warning')]
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
    # The damaged file itself is only a warning; the worker self-test still loads it (in
    # the worker), and that failure is the problem that turns detailed collision off.
    coacd=[x for x in s['issues'] if x.get('component')=='coacd']
    assert any(x['code']=='damaged_or_unrecognized' and x.get('warning') for x in coacd),s
    assert any(x['code']=='dependency_failed' and not x.get('warning') and x.get('feature')=='detailedCollision' for x in coacd),s
    assert all(x.get('feature')=='detailedCollision' for x in problems(s)),s
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
    # Nothing to load: the missing module's warning became the server's problem.
    assert any(x['code']=='missing' and not x.get('warning') for x in server['issues']),server
    remote=None
    for _ in range(100):
        remote=call('client','return mmdhl.serverInstallation')
        if remote:break
        time.sleep(.1)
    assert remote and not remote['features']['core'],remote
    report['missingServer']={'client':s['features'],'server':server,'received':remote}
    stop();install();(game/'garrysmod/lua/bin/gmcl_mmdhl_win64.dll').unlink()
    start();s=state('client');assert not s['features']['core'] and any(x['code']=='missing' and not x.get('warning') for x in s['issues'])
    assert call('client',"RunConsoleCommand('mmdhl_open') return mmdhl.native==nil")
    report['missingClient']=s
    stop();install()
    # A client module the policy does not know (a test build, a newer release): bytes after
    # its last section change its hash, not what Windows loads. It loads with one warning,
    # everything stays on, and the banner shows the module's own label and build.
    module=game/'garrysmod/lua/bin/gmcl_mmdhl_win64.dll'
    replace(module,module.read_bytes()+b'\0'*512+b'unrecognized build fixture')
    start();s=settled()
    assert all(s['features'][key] for key in ('core','imports','detailedCollision')) and not problems(s),s
    assert s.get('installed') is None and s.get('unverified'),s
    assert any(x['code']=='damaged_or_unrecognized' and x.get('component')=='client' and x.get('identity') for x in s['issues']),s
    summary=call('client',"local text,versions=mmdhl.InstallationSummary() return {text=text,versions=versions,line=mmdhl.L'install.binary_unrecognized'}")
    loaded=s['loaded']['module']
    assert summary['line'] in summary['text'] and loaded['release'] in summary['versions'] and loaded['build'] in summary['versions'],summary
    report['unknownClientModule']={'status':s,'summary':summary}
    stop();install()
    # A runtime from another build than the module: the build ID inside the copy the game
    # loads is changed (same length). Both realms still load, and the warning reached the
    # console before the module loaded.
    build=json.loads((ROOT/'build/bin/Release/native-release.json').read_text(encoding='utf8'))['build']
    assert re.fullmatch(r'[0-9a-f]{12}-\d{8}T\d{6}Z',build),build
    other=('0'*12 if build[:12]!='0'*12 else '1'*12)+build[12:]
    runtime=game/'bin/win64/mmdhl_runtime_win64.dll';data=runtime.read_bytes()
    assert build.encode() in data,'no build ID in the runtime'
    replace(runtime,data.replace(build.encode(),other.encode()))
    log=game/'garrysmod/console.log';logged=log.stat().st_size if log.exists() else 0
    start();s=settled();server=state('server')
    for realm in (s,server):
        assert realm['features']['core'] and not problems(realm),realm
        assert any(x['code']=='mixed_builds' and x.get('identity') for x in realm['issues']),realm
    assert s['features']['imports'],s
    message=next(x['message'] for x in s['issues'] if x['code']=='mixed_builds')
    with open(log,'rb') as f:
        f.seek(logged if log.stat().st_size>=logged else 0);console=f.read().decode('utf8','replace')
    assert '[Model Hotloader / client] '+message in console,console[-4000:]
    report['runtimeFromAnotherBuild']={'client':s,'server':server}
    stop();install()
    path=game/'garrysmod/addons/mmd_hotloader/lua/mmdhl/compatibility_policy.lua';policy=lua_policy(path)
    for p in policy['libraries']:
        if p['name']=='engine.dll':p['evidence']['sections'][0]['sha256']='0'*64
    # A game build no profile describes is unverified: it keeps rendering, with one
    # warning that disables nothing.
    write_policy(path,policy);start();s=settled()
    assert s['features']['core'] and s['features']['imports'] and s['features']['rendering'],s
    assert any(x['code']=='game_unverified' and x.get('warning') for x in s['issues'])
    assert any(x['name']=='engine.dll' and x['match']=='unverified' for x in s['compatibility']['libraries'])
    assert call('client',"local before=GetConVar('mat_queue_mode'):GetInt() local ok=mmdhl.RenderAvailable() assert(ok) return before==GetConVar('mat_queue_mode'):GetInt()")
    report['abiUnverified']=s
    print('PASS: actual game accepts new DLL hash with matching evidence; a damaged CoACD is a warning, fails the worker self-test and falls back to simple hulls; missing server is communicated; missing client retains UI; a client module the policy does not know and a runtime from another build load with warnings only (the latter printed before loading); an unverified game build keeps rendering with a warning, without changing queue mode')
finally:
    stop();replace(engine,original);install()
    (ROOT/'validation/installation-game-failures.json').write_text(json.dumps(report,indent=2),encoding='utf8')
