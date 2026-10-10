"""The installation check (installation.lua) without loading a DLL. Model Hotloader always
tries its native files: the pure evaluator never turns a feature off, it lists warnings
(another platform, a broken policy, files this addon does not know, missing or unreadable
files, a module and runtime from two builds, a release without verification), and
CheckInstallation skips require only when there is nothing to load (no module file for
this realm, another platform). Warnings reach the console before require; when loading
fails, the warnings that explain it become its problems (else the loader's error is the
reason), and when the worker's self-test fails (CoACD in it), only its feature's do. Also the
loaded-identity check (both 64-bit layouts, a dedicated server, a broken policy), Recheck,
every approved release evaluating without issues (publish-native-release.py's evaluate()),
that nothing of accepting unverified files is left in the addon, and all addon Lua syntax."""
from pathlib import Path
from copy import deepcopy
import importlib.util, json, re, sys
from lupa import LuaRuntime
from lua_i18n import attach

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from compatibility_profiles import lua_policy

lua=LuaRuntime(unpack_returned_tuples=True)
lua.execute('unpack=table.unpack; mmdhl={}; util={}; function include() return {} end')
attach(lua)
lua.execute('L=mmdhl.L')
source=(ROOT/'addon/lua/mmdhl/installation.lua').read_text(encoding='utf8')
lua.execute(source[:source.index("local policy=include")])
for p in (ROOT/'addon/lua').rglob('*.lua'):
    result=lua.eval('function(s) return load(s) end')(re.sub(r'\bcontinue\b','break',p.read_text(encoding='utf8')))
    if isinstance(result,tuple): raise AssertionError(f'{p}: {result[1]}')

def convert(x,runtime=lua):
    if isinstance(x,dict): return runtime.table_from({k:convert(v,runtime) for k,v in x.items()})
    if isinstance(x,list): return runtime.table_from([convert(v,runtime) for v in x])
    return x
def L(key,**vars): return lua.globals().mmdhl.L(key,convert(vars) if vars else None)
def listed(t): return [t[i] for i in range(1,len(t)+1)]
def codes(s): return [v.code for v in listed(s.issues)]
def features(s): return (s.features.core,s.features.imports,s.features.detailedCollision,s.features.rendering,s.features.physics)
CLIENT_ON,SERVER_ON=(True,True,True,True,False),(True,False,False,False,True)

policy=lua_policy(ROOT/'addon/lua/mmdhl/native_policy.lua')
release=policy['releases'][policy['recommended']]
NAMES={key:record['name'] for key,record in release['files'].items()}
CLIENT,SERVER='MOD/lua/bin/'+NAMES['client'],'MOD/lua/bin/'+NAMES['server']
RUNTIME,COPY='BASE_PATH/bin/win64/'+NAMES['runtime'],'MOD/lua/bin/'+NAMES['runtime']
WORKER,COACD='MOD/lua/bin/'+NAMES['worker'],'MOD/lua/bin/'+NAMES['coacd']
def installed(server=False,dedicated=False):
    result={}
    for key,record in release['files'].items():
        result['MOD/lua/bin/'+record['name']]=dict(size=record['size'],sha256=record['sha256'],path='fixture/'+record['name'])
    result['BASE_PATH/'+('' if dedicated else 'bin/win64/')+release['files']['runtime']['name']]=deepcopy(result['MOD/lua/bin/'+release['files']['runtime']['name']])
    return result

count=0
def evaluate(files=None,server=False,dedicated=False,windows=True,arch='x64',p=None):
    global count
    count+=1; reads=[]; files=installed(server,dedicated) if files is None else files
    def reader(path,search,*_):
        key=search+'/'+path; reads.append(key)
        found=files.get(key)
        if isinstance(found,str): return None,found
        return (convert(found),None) if found else (None,'missing')
    s=lua.globals().mmdhl.EvaluateInstallation(convert(p or policy),reader,convert(dict(server=server,dedicated=dedicated,windows=windows,arch=arch)))
    return s,reads

# The recommended release: every feature, no issue.
s,_=evaluate();assert features(s)==CLIENT_ON and s.update is None and codes(s)==[] and s.installed==policy['recommended'] and not s.unverified and not s.blocked
# A missing, unreadable, truncated or modified file never turns a feature off here: it is a
# warning. Missing and unreadable files explain a failure (cause), and become its problem
# when loading (or the worker's self-test) fails; other bytes are files this addon does not
# know (identity), with their size and hash as the detail Dismiss tells apart.
for key in ('client','runtime','worker','coacd'):
    path=('BASE_PATH/bin/win64/' if key=='runtime' else 'MOD/lua/bin/')+release['files'][key]['name']
    feature='core' if key in ('client','runtime') else 'imports' if key=='worker' else 'detailedCollision'
    for failure in ('missing','unreadable','truncate','modified'):
        files=installed()
        if failure=='missing': del files[path]
        elif failure=='unreadable': files[path]='unreadable'
        elif failure=='truncate': files[path]['size']-=1
        else: files[path]['sha256']='0'*64
        s,_=evaluate(files);assert features(s)==CLIENT_ON and not s.blocked and codes(s)==(['missing'] if failure=='missing' else ['unreadable'] if failure=='unreadable' else ['damaged_or_unrecognized']),(key,failure,codes(s))
        v=s.issues[1];assert v.warning is True and v.feature==feature and v.component==key,(key,failure)
        relative=path.split('/',1)[1]
        if failure in ('missing','unreadable'):
            assert v.cause is True and v.identity is None and not s.unverified and v.message==L('install.error.file_'+failure,path=relative),(key,failure)
        else:
            assert v.identity is True and v.cause is None and s.unverified and v.detail==f"{files[path]['size']}:{files[path]['sha256']}" and v.message==L('install.error.file_unrecognized',path=relative),(key,failure)
        assert s.installed==(None if key=='client' else policy['recommended']),(key,failure)
files=installed();files[COPY]['sha256']='0'*64
s,_=evaluate(files);assert features(s)==CLIENT_ON and codes(s)==['damaged_or_unrecognized'] and s.issues[1].component=='workerRuntime' and s.issues[1].feature=='imports' and s.issues[1].warning
# Another platform: no binary exists for it, so nothing is read; CheckInstallation keeps everything off (below).
for windows,arch in ((False,'x64'),(True,'x86'),(True,'arm64')):
    s,reads=evaluate(windows=windows,arch=arch);v=s.issues[1]
    assert codes(s)==['unsupported_platform'] and v.warning is True and v.cause is True and v.feature=='core' and not reads and next(iter(s.files.keys()),None) is None
for dedicated in (False,True):
    s,reads=evaluate(server=True,dedicated=dedicated);assert features(s)==SERVER_ON and codes(s)==[]
    assert not any('worker' in x or 'coacd' in x or 'gmcl' in x for x in reads)
# A dedicated server loads the runtime beside srcds_win64.exe, else from bin/win64,
# where the native package puts it; the check reads the copy it will load.
name=release['files']['runtime']['name'];beside='BASE_PATH/'+name;packaged='BASE_PATH/bin/win64/'+name
package=installed(True,True);package[packaged]=package.pop(beside)
s,_=evaluate(package,server=True,dedicated=True);assert features(s)==SERVER_ON and codes(s)==[] and s.files.runtime.relative=='bin/win64/'+name
both=deepcopy(package);both[beside]=dict(package[packaged],sha256='7'*64)
s,_=evaluate(both,server=True,dedicated=True)
assert features(s)==SERVER_ON and s.files.runtime.relative==name and codes(s)==['damaged_or_unrecognized'] and s.issues[1].identity and s.issues[1].component=='runtime'
both[beside]='unreadable'
s,_=evaluate(both,server=True,dedicated=True);assert features(s)==SERVER_ON and s.files.runtime.relative==name and codes(s)==['unreadable'] and s.issues[1].cause
neither=installed(True,True);del neither[beside]
s,_=evaluate(neither,server=True,dedicated=True);assert features(s)==SERVER_ON and s.files.runtime.relative==name and codes(s)==['missing'] and s.issues[1].cause
# Clients and listen servers never take the runtime from beside the executable.
for server in (False,True):
    beside_only=installed(server,True)
    s,_=evaluate(beside_only,server=server);assert s.features.core and codes(s)==['missing'] and s.files.runtime.relative=='bin/win64/'+name
old=deepcopy(release);old['release']='1.0.0';old['build']='old'
for f in old['files'].values():f['sha256']='1'*64
p=deepcopy(policy);p['releases']['1.0.0']=old
files=installed()
for f in files.values():f['sha256']='1'*64
# An older release no longer approved still runs: one warning that disables nothing,
# and the update reminder with the recommended release's links.
s,_=evaluate(files,p=p)
assert features(s)==CLIENT_ON and not s.unverified and not s.blocked and s.installed=='1.0.0'
assert codes(s)==['outdated_release'] and s.issues[1].warning and not s.issues[1].identity and s.issues[1].feature=='core'
u=s.update
assert u.installed=='1.0.0' and u.recommended==policy['recommended'] and u.url==release['url'] and u.altUrl==release.get('altUrl') and u.approved is None and u.required is None and u.advisory is None
# Approved, it runs without any issue (publish-native-release.py requires that) and still gets the reminder.
p['approved'].append('1.0.0');s,_=evaluate(files,p=p);assert s.features.core and s.installed=='1.0.0' and len(s.issues)==0 and s.update.approved is True and s.update.installed=='1.0.0'
# A file of another release: a warning naming that release.
files=installed();files[RUNTIME]['sha256']='1'*64
s,_=evaluate(files,p=p);v=s.issues[1]
assert features(s)==CLIENT_ON and codes(s)==['mixed_installation'] and v.warning and v.identity and v.detail==f"{files[RUNTIME]['size']}:{'1'*64}" and s.unverified
assert v.message==L('install.error.file_mixed',path='bin/win64/'+name,release='1.0.0',required=release['release'])
# A broken policy: the files are read by their usual names, never compared; nothing is turned off.
for broken in ({'schema':1},dict(policy,schema=2),dict(policy,recommended='0.0.0'),dict(policy,releases='x')):
    s,reads=evaluate(p=broken)
    assert codes(s)==['policy_invalid'] and s.issues[1].warning and s.issues[1].component=='addon' and s.issues[1].feature=='core' and features(s)==CLIENT_ON,broken.keys()
    assert s.update is None and s.installed is None and s.expected is None and not s.unverified and CLIENT in reads and RUNTIME in reads and WORKER in reads
    assert s.files.client.actual.sha256==release['files']['client']['sha256'] and s.files.client.expected is None
files=installed();del files[RUNTIME]
s,_=evaluate(files,p={'schema':1});assert codes(s)==['policy_invalid','missing'] and s.issues[2].cause and s.features.core
future=deepcopy(old);future['release']='9.0.0';future['build']='future'
p=deepcopy(policy);p['releases']['9.0.0']=future
files=installed()
for f in files.values():f['sha256']='1'*64
# A newer release this addon records but does not approve: it runs, with a warning about it.
s,_=evaluate(files,p=p);v=s.issues[1]
assert features(s)==CLIENT_ON and codes(s)==['unapproved_release'] and v.warning and v.identity and v.detail=='9.0.0' and s.unverified and s.installed=='9.0.0' and s.update is None
# A release with a known problem (policy.revoked: label -> phrase) still runs, with that advisory.
p=deepcopy(policy);p['releases']['1.0.0']=old;p['revoked']={'1.0.0':'install.advisory.test'}
s,_=evaluate(files,p=p);assert s.features.core and s.update.advisory=='install.advisory.test' and s.issues[1].code=='outdated_release'
p['approved'].append('1.0.0');s,_=evaluate(files,p=p);assert s.features.core and len(s.issues)==0 and s.update.advisory=='install.advisory.test'
p['revoked']={'1.0.0':7};s,_=evaluate(files,p=p);assert s.features.core and s.update.advisory is None
# Order: the build time ending the build ID wins over the label (a re-tagged or
# pre-release label), and labels alone order a pre-release before its release.
def synthetic(p,label,build,sha):
    record=deepcopy(release);record['release']=label;record['build']=build
    for f in record['files'].values():f['sha256']=sha
    p['releases'][label]=record
def disk(sha):
    files=installed()
    for f in files.values():f['sha256']=sha
    return files
p=deepcopy(policy);p['releases'][policy['recommended']]['build']='aaaaaaaaaaaa-20261001T120000Z'
synthetic(p,'9.9.9','bbbbbbbbbbbb-20260101T000000Z','5'*64);synthetic(p,'0.0.1','cccccccccccc-20261231T000000Z','6'*64)
s,_=evaluate(disk('5'*64),p=p);assert s.features.core and s.update.installed=='9.9.9' and s.issues[1].code=='outdated_release'
s,_=evaluate(disk('6'*64),p=p);assert features(s)==CLIENT_ON and codes(s)==['unapproved_release'] and s.issues[1].warning and s.update is None
p=deepcopy(policy);p['releases']['2.3.0']=deepcopy(release);p['releases']['2.3.0'].update(release='2.3.0',build='local');p['recommended']='2.3.0'
synthetic(p,'2.3.0-rc.1','local','7'*64);synthetic(p,'2.3.0+hotfix','local','8'*64)
s,_=evaluate(disk('7'*64),p=p);assert s.features.core and s.update.installed=='2.3.0-rc.1' and s.update.recommended=='2.3.0'
s,_=evaluate(disk('8'*64),p=p);assert s.issues[1].code=='unapproved_release' and s.update is None
for label,older in (('2.1.0-native.12',True),('2.2.0',True),('2.3.1',False),('10.0.0',False),('legacy-preview',True)):
    synthetic(p,label,'local','9'*64);s,_=evaluate(disk('9'*64),p=p);del p['releases'][label]
    assert (s.update is not None)==older,label

# Files this addon does not know (a fresh GitHub Actions build: every client file with other
# hashes, one build ID in the module and the runtime): every feature stays on, one identity
# warning per file, no release is installed and no update is offered (the loaded module's own
# label decides that, see test_native_update_session.py).
FRESH='feedfacecafe-20261009T120000Z'
SHAS={CLIENT:'a'*64,SERVER:'b'*64,RUNTIME:'c'*64,COPY:'c'*64,WORKER:'d'*64,COACD:'e'*64}
def fresh(server=False,builds=None):
    files=installed(server)
    for key,f in files.items():
        f['sha256']=SHAS[key]
        if key in (CLIENT,SERVER,RUNTIME,COPY): f['build']=(builds or {}).get(key,FRESH)
    return files
for server in (False,True):
    s,_=evaluate(fresh(server),server=server)
    keys=['server','runtime'] if server else ['client','runtime','worker','workerRuntime','coacd']
    assert features(s)==(SERVER_ON if server else CLIENT_ON) and not s.blocked and s.unverified and s.installed is None and s.update is None,server
    assert codes(s)==['damaged_or_unrecognized']*len(keys) and [v.component for v in listed(s.issues)]==keys,codes(s)
    assert all(v.warning is True and v.identity is True and v.cause is None and v.detail==f'{s.files[v.component].actual.size}:{s.files[v.component].actual.sha256}' for v in listed(s.issues))
    assert s.expected.release==release['release'] and s.files[keys[0]].actual.build==FRESH
# A module and a runtime from two builds share C++ types without a handshake: still loaded, with
# a warning that names both builds and explains a failure to load (cause).
OTHER='0123456789ab-20261001T000000Z'
for server in (False,True):
    role,module=('server',SERVER) if server else ('client',CLIENT)
    s,_=evaluate(fresh(server,{RUNTIME:OTHER}),server=server)
    mixed=[v for v in listed(s.issues) if v.code=='mixed_builds']
    assert features(s)==(SERVER_ON if server else CLIENT_ON) and len(mixed)==1,codes(s)
    v=mixed[0];assert v.warning is True and v.identity is True and v.cause is True and v.component=='runtime' and v.feature=='core' and v.detail==FRESH+'/'+OTHER
    assert v.message==L('install.warning.mixed_builds',module=module.split('/',1)[1],build=FRESH,runtime='bin/win64/'+name,runtimeBuild=OTHER)
# The approved module with a runtime of another build: the same warning beside the file's own.
files=installed();files[CLIENT]['build']=release['build'];files[RUNTIME].update(sha256='c'*64,build=OTHER)
s,_=evaluate(files);assert s.installed==release['release'] and codes(s)==['damaged_or_unrecognized','mixed_builds'] and s.features.core
# Without a build ID on either side (another file, or a binary from before build IDs) nothing tells.
files=fresh(builds={RUNTIME:None});s,_=evaluate(files);assert 'mixed_builds' not in codes(s)
files=fresh(builds={COPY:OTHER});s,_=evaluate(files);assert 'mixed_builds' not in codes(s),'the worker side is the self-test\'s to judge'
# A release without installation verification (installApi 0) runs too, worded as an update it needs.
legacy=deepcopy(future);legacy['installApi']=0;p=deepcopy(policy);p['releases']['legacy']=legacy
files=installed()
for f in files.values():f['sha256']='1'*64
s,_=evaluate(files,p=p);assert codes(s)==['outdated'] and s.issues[1].warning and not s.issues[1].identity and not s.blocked and features(s)==CLIENT_ON
assert s.issues[1].message==L('install.error.no_verification',recommended=policy['recommended'])
assert s.update.required and s.update.installed=='legacy' and s.update.url==release['url'] and s.update.recommended==policy['recommended']
# Nothing records an acceptance any more.
assert all(v.accepted is None for v in listed(s.issues)) and s.unverifiedAccepted is None and s.acceptedIssues is None

# The renderer (bin/win64/d3d9.dll) is reported on the client and never gates a feature:
# the bundled DXVK of this or another release, Source's Direct3D 9, or another file (RTX Remix).
dxvk=dict(name='d3d9.dll',size=4042752,sha256='d'*64,kind='dxvk',dxvk='v3.1.1',patches=['0001-shared-compute-queues.patch'])
p=deepcopy(policy);p['releases'][policy['recommended']]['renderer']=dxvk
older=deepcopy(old);older['renderer']=dict(dxvk,sha256='e'*64);p['releases']['1.0.0']=older
d3d9='BASE_PATH/bin/win64/d3d9.dll'
s,reads=evaluate(p=p);assert s.renderer.kind=='d3d9' and d3d9 in reads and s.features.core and s.features.rendering
s,_=evaluate();assert s.renderer.kind=='d3d9' # A policy without renderer records.
for sha,kind,current,owner in (('d'*64,'dxvk',True,release['release']),('e'*64,'dxvk',None,'1.0.0'),('f'*64,'other',None,None)):
    files=installed();files[d3d9]=dict(size=dxvk['size'],sha256=sha,path='fixture/d3d9.dll')
    s,_=evaluate(files,p=p)
    assert s.renderer.kind==kind and s.renderer.current==current and s.renderer.release==owner,(sha,kind)
    assert s.features.core and s.features.rendering and not s.unverified and not s.blocked and len(s.issues)==0
files=installed();files[d3d9]='unreadable'
s,_=evaluate(files,p=p);assert s.renderer.kind=='other' and s.renderer.error=='unreadable' and s.features.rendering and len(s.issues)==0
for dedicated in (False,True):
    s,reads=evaluate(server=True,dedicated=dedicated,p=p);assert s.renderer is None and d3d9 not in reads
# Every approved release evaluates without any issue on client and server, as
# publish-native-release.py's evaluate() requires before it publishes.
spec=importlib.util.spec_from_file_location('publish_native_release',ROOT/'scripts/publish-native-release.py')
publish=importlib.util.module_from_spec(spec);spec.loader.exec_module(publish)
publish.evaluate(policy,policy['recommended'])
for label in policy['approved']:
    record=policy['releases'][label];approved_files={}
    for f in record['files'].values():approved_files['MOD/lua/bin/'+f['name']]=dict(size=f['size'],sha256=f['sha256'])
    approved_files['BASE_PATH/bin/win64/'+record['files']['runtime']['name']]=dict(size=record['files']['runtime']['size'],sha256=record['files']['runtime']['sha256'])
    for server in (False,True):
        s,_=evaluate(approved_files,server=server)
        assert codes(s)==[] and s.installed==label and not s.unverified and features(s)==(SERVER_ON if server else CLIENT_ON),(label,server,codes(s))

# Exercise the actual post-load validator with independently constructed reports.
valid,_=evaluate()
lua.globals().TEST_STATUS=valid
part=source[source.index('local function normalize'):source.index('local function removeIssues')]
validator=lua.execute('local status=TEST_STATUS\n'+part+'\nreturn identitiesMatch')
info={}
for key,file_key in [('module','client'),('runtime','runtime')]:
    info[key]=dict(release=release['release'],build=release['build'],installApi=1,api=release['api'],platform='win64',
                   sha256=release['files'][file_key]['sha256'],path='C:/game/'+release['files'][file_key]['name'],expectedPath='c:\\game\\'+release['files'][file_key]['name'])
assert validator(convert(info)) is True
for key,field,value in [('runtime','path','C:/shadow/runtime.dll'),('module','build','other'),('module','sha256','0'*64),('module','installApi',0)]:
    broken=deepcopy(info);broken[key][field]=value
    assert validator(convert(broken))[0] is False
# Files this addon does not know: the loaded bytes must equal the files on disk; release and build may differ.
client='MOD/lua/bin/'+release['files']['client']['name'];runtime='BASE_PATH/bin/win64/'+release['files']['runtime']['name']
files=installed();files[client]['sha256']='2'*64;files[runtime]['sha256']='4'*64
unknown,_=evaluate(files);assert unknown.installed is None and unknown.features.core
lua.globals().TEST_STATUS=unknown
validator=lua.execute('local status=TEST_STATUS\n'+part+'\nreturn identitiesMatch')
unverified=deepcopy(info);unverified['module'].update(build='custom',release='custom',sha256='2'*64);unverified['runtime'].update(build='custom',sha256='4'*64)
assert validator(convert(unverified)) is True
for key,field,value in [('module','sha256','5'*64),('runtime','api',99),('module','path','C:/shadow/module.dll')]:
    broken=deepcopy(unverified);broken[key][field]=value
    assert validator(convert(broken))[0] is False
# A broken policy (status.expected nil): the files on disk still identify the loaded ones, interface 1.
lua.globals().TEST_STATUS,_=evaluate(p={'schema':1})
assert lua.globals().TEST_STATUS.expected is None
validator=lua.execute('local status=TEST_STATUS\n'+part+'\nreturn identitiesMatch')
assert validator(convert(info)) is True
for key,field,value,same in [('runtime','sha256','6'*64,True),('module','api',2,False)]:
    broken=deepcopy(info);broken[key][field]=value
    ok,_,interface=validator(convert(broken));assert ok is False and interface is same,(key,field)
# Both 64-bit branches load the runtime from bin/win64. The main branch starts
# gmod_win64.exe in the game folder, where builds up to 2.1.0-native.5 expect it.
lua.globals().TEST_STATUS=valid
validator=lua.execute('local status=TEST_STATUS\n'+part+'\nreturn identitiesMatch')
game='H:\\SteamLibrary\\steamapps\\common\\GarrysMod\\'
layouts=deepcopy(info)
layouts['module'].update(path=game.lower()+'garrysmod\\lua\\bin\\'+release['files']['client']['name'],expectedPath=game+'garrysmod\\lua\\bin\\'+release['files']['client']['name'])
for expected in ('bin\\win64\\','',):  # x86-64 branch (bin\win64\gmod.exe), main branch (gmod_win64.exe)
    layouts['runtime'].update(path=game+'bin\\win64\\'+release['files']['runtime']['name'],expectedPath=game+expected+release['files']['runtime']['name'])
    assert validator(convert(layouts)) is True,expected
# A dedicated server (srcds_win64.exe in its folder) loading the packaged runtime
# from bin/win64. Loaded from there while the check read a copy beside srcds_win64.exe,
# it is another file with the same interface: a warning (loaded_mismatch), never a refusal.
srcds='D:\\GMod Server\\'
dedicated=deepcopy(info)
dedicated['module'].update(sha256=release['files']['server']['sha256'],path=srcds+'garrysmod\\lua\\bin\\'+release['files']['server']['name'],expectedPath=srcds+'garrysmod\\lua\\bin\\'+release['files']['server']['name'])
dedicated['runtime'].update(path=srcds+'bin\\win64\\'+name,expectedPath=srcds+name)
for files,verified in ((package,True),(installed(True,True),False)):
    lua.globals().TEST_STATUS,_=evaluate(files,server=True,dedicated=True)
    result=lua.execute('local status=TEST_STATUS\n'+part+'\nreturn identitiesMatch')(convert(dedicated))
    assert result is True if verified else (result[0] is False and result[2] is True),files.keys()
# Elsewhere or other bytes keep the interface; another interface does not (the update reminder
# then offers the download, see test_native_update_session.py). Both only warn.
for key,field,value,same in [('runtime','path',game+'shadow\\'+release['files']['runtime']['name'],True),('runtime','sha256','6'*64,True),
                             ('module','build','other',True),('module','api',99,False),('runtime','installApi',0,False)]:
    broken=deepcopy(layouts);broken[key][field]=value
    ok,message,interface=validator(convert(broken))
    assert ok is False and interface is same and message==L('install.error.loaded_module_mismatch' if key=='module' else 'install.error.loaded_runtime_mismatch'),(key,field)
print(f'PASS: {count} installation policy scenarios (warnings only: files this addon does not know, two builds, a broken policy, missing files, '
      'where a dedicated server finds its runtime; every approved release without issues), twenty-one loaded-identity checks (both 64-bit layouts, '
      'a dedicated server, a broken policy) and all addon Lua syntax')

# ---- CheckInstallation: the files are always tried ----
HASHED="value={size=size,sha256=util.SHA256(bytes),path=search..'/'..path}"
assert HASHED in source,'test harnesses replace this line'
assert 'if not mmdhl.CheckInstallation()' in (ROOT/'addon/lua/autorun/mmdhl.lua').read_text(encoding='utf8')
GAME='C:\\game\\'
STUBS=r'''
util={JSONToTable=function(s) return PY_DECODE(s) end,TableToJSON=function(t) return PY_ENCODE(t) end,AddNetworkString=function() end}
-- What happened, in order: console lines and the require of the native module.
EVENTS={} LOOKED={}
file={}
function file.Exists(p,s) LOOKED[#LOOKED+1]=s..'/'..p return PY_DISK[s..'/'..p]~=nil end
-- A Model Hotloader binary carries its build ID (PY_BUILDS) among its bytes.
function file.Open(p,m,s)
 local f=PY_DISK[s..'/'..p] if f.unreadable then return nil end
 local id=PY_BUILDS[s..'/'..p]
 return {Size=function() return f.size end,Read=function(_,n) if id then return id..string.rep('x',n-#id) end return string.rep('x',n) end,Close=function() end}
end
function file.Read() return nil end function file.Write() end function file.CreateDir() end
net={Start=function() end,WriteString=function() end,Broadcast=function() end,Send=function() end,Receive=function() end,SendToServer=function() end}
hook={Add=function() end,Run=function() end}
TIMERS={} timer={Create=function(name,_,_,f) TIMERS[name]=f end,Remove=function(name) TIMERS[name]=nil end,Simple=function() end}
concommand={Add=function() end}
system={IsWindows=function() return PY_WINDOWS end,IsLinux=function() return false end} jit={arch=PY_ARCH}
game={IsDedicated=function() return false end,SinglePlayer=function() return true end}
CurTime=function() return 0 end RealTime=function() return 0 end
MsgN=function(text) EVENTS[#EVENTS+1]='console: '..text end
istable=function(v) return type(v)=='table' end isstring=function(v) return type(v)=='string' end
function include(p) if p=='mmdhl/native_policy.lua' then return PY_POLICY end return {schema=1,family='source-win64-v1',libraries={}} end
-- The native module reports INFO as the files Garry's Mod loaded. Its worker self-test answers
-- PROBE: nil while it runs, a result, or false (failed with PROBE_ERROR).
CHECKS=0 PROBE=nil PROBE_ERROR=nil
function NATIVE()
 return {GetInstallationInfo=function() return INFO end,ConfigureCompatibility=function() return {configured=true} end,
  CheckCompatibility=function() CHECKS=CHECKS+1 return {ready=true,pending=false,issues={},libraries={}} end,
  StartInstallationProbe=function() if PY_PROBE_STARTS then return true end return false,'The worker could not be started.' end,
  PollInstallationProbe=function() if PROBE==nil then return {pending=true} end if PROBE==false then return nil,PROBE_ERROR end return PROBE end,
  GetCapabilities=function() return {version=INFO.module.release} end}
end
function require(name)
 EVENTS[#EVENTS+1]='require '..name
 if PY_REQUIRE_ERROR then error(PY_REQUIRE_ERROR,0) end
 mmdhl_native=NATIVE()
end
function ANSWER(result,why) PROBE=result PROBE_ERROR=why TIMERS['MMDHL.InstallationWorker']() end
'''

def identity(module=None,runtime=None,label=None,build=None,server=False,runtime_at=None):
    """What the native module reports about itself: by default the recommended release, loaded from where the game loads it."""
    role='server' if server else 'client'
    base=dict(release=label or release['release'],build=build or release['build'],installApi=1,api=1,platform='win64')
    m=dict(release['files'][role],**(module or {}));r=dict(release['files']['runtime'],**(runtime or {}))
    mpath=GAME+'garrysmod\\lua\\bin\\'+m['name'];rpath=GAME+'bin\\win64\\'+r['name']
    return {'module':dict(base,size=m['size'],sha256=m['sha256'],path=mpath,expectedPath=mpath),
            'runtime':dict(base,size=r['size'],sha256=r['sha256'],path=runtime_at or rpath,expectedPath=rpath)}
def worker(build=None,runtime_sha=None,label=None):
    """The worker self-test's answer: the worker's identity and its runtime copy's."""
    return {'identity':{'release':label or release['release'],'build':build or release['build']},'runtime':{'build':build or release['build'],'sha256':runtime_sha or release['files']['runtime']['sha256']},'coacd':True}

def session(files=None,builds=None,p=None,server=False,windows=True,arch='x64',info=None,require_error=None,probe_starts=True):
    """CheckInstallation as autorun runs it, in a fresh Lua session."""
    lua=LuaRuntime(unpack_returned_tuples=True)
    lua.execute(f'unpack=table.unpack mmdhl={{}} SERVER={"true" if server else "false"} CLIENT=not SERVER')
    attach(lua)
    g=lua.globals()
    def from_lua(t):
        if hasattr(t,'items'):
            d=dict(t.items())
            return [from_lua(d[i]) for i in sorted(d)] if d and all(isinstance(k,int) for k in d) else {k:from_lua(v) for k,v in d.items()}
        return t
    g.PY_DECODE=lambda s: convert(json.loads(s),lua) if s else None
    g.PY_ENCODE=lambda t: json.dumps(from_lua(t))
    files=installed(server) if files is None else files
    g.PY_DISK=convert({k:({'unreadable':True} if v=='unreadable' else {'size':v['size'],'sha256':v['sha256']}) for k,v in files.items()},lua)
    g.PY_BUILDS=convert({k:v for k,v in (builds or {}).items() if v},lua)
    g.PY_POLICY=convert(p or policy,lua)
    g.PY_WINDOWS,g.PY_ARCH,g.PY_PROBE_STARTS,g.PY_REQUIRE_ERROR=windows,arch,probe_starts,require_error
    lua.execute(STUBS)
    g.INFO=convert(info or identity(server=server),lua)
    lua.execute(source.replace(HASHED,"value={size=size,sha256=PY_DISK[search..'/'..path].sha256,path=search..'/'..path}"))
    ok=g.mmdhl.CheckInstallation()
    return lua,g,g.mmdhl,ok

def events(g): return listed(g.EVENTS)
def console(M,v): return 'console: [Model Hotloader / '+M.GetInstallationStatus().realm+'] '+M.Localize(v.message)
def issue(s,code): return [v for v in listed(s.issues) if v.code==code]
def same(M,a,b): return M.Localize(a)==M.Localize(b)

# The recommended release loads; imports wait for the worker's self-test.
lua_,g,M,ok=session();s=M.GetInstallationStatus()
assert ok is True and M.native is not None and codes(s)==[] and s.features.core and s.features.rendering and s.probePending and not s.features.imports
assert events(g)==['require mmdhl'] and g.CHECKS==1
g.ANSWER(convert(worker(),lua_));assert s.features.imports and s.features.detailedCollision and codes(s)==[] and M.FeatureAvailable('imports') is True and M.loadError is None

# A module and a runtime from two builds (files this addon does not know): the warning reaches the
# console before require, which is still tried; the module runs when it loads.
A,B='0123456789ab-20261001T120000Z','ba9876543210-20261008T120000Z'
two_builds=fresh(builds={CLIENT:A,RUNTIME:B,COPY:B})
mixed_info=identity({'sha256':SHAS[CLIENT]},{'sha256':SHAS[RUNTIME]},label='2.3.0',build=A)
lua_,g,M,ok=session(two_builds,{CLIENT:A,RUNTIME:B,COPY:B},info=mixed_info);s=M.GetInstallationStatus()
mixed=issue(s,'mixed_builds')
assert ok is True and M.native is not None and s.features.core and s.features.rendering and len(mixed)==1 and mixed[0].warning is True and M.loadError is None,codes(s)
assert same(M,mixed[0].message,L('install.warning.mixed_builds',module='lua/bin/'+NAMES['client'],build=A,runtime='bin/win64/'+NAMES['runtime'],runtimeBuild=B))
order=events(g)
assert order.index(console(M,mixed[0]))<order.index('require mmdhl') and order.count('require mmdhl')==1,order
assert all(order.index(console(M,v))<order.index('require mmdhl') for v in listed(s.issues)),'a warning reached the console after require'
assert 'loaded_mismatch' not in codes(s) and s.installed is None and s.unverified
# When require then fails, the mixed builds are the reason, beside the loader's error.
lua_,g,M,ok=session(two_builds,{CLIENT:A,RUNTIME:B,COPY:B},info=mixed_info,require_error="Couldn't load module library!");s=M.GetInstallationStatus()
mixed=issue(s,'mixed_builds')[0];order=events(g)
assert ok is False and M.native is None and not any(features(s)) and s.blocked and same(M,M.loadError,mixed.message) and mixed.warning is None,codes(s)
assert order.index(console(M,mixed))<order.index('require mmdhl')
loader=issue(s,'loader_failed');assert len(loader)==1 and loader[0].warning is None and same(M,loader[0].message,L('install.error.loader_failed',reason="Couldn't load module library!"))
assert all(v.warning is True for v in listed(s.issues) if v.code=='damaged_or_unrecognized'),'files this addon does not know became problems'
assert M.FeatureAvailable('core')==(False,M.loadError) and M.FeatureAvailable('imports')[0] is False
# One build this addon does not know (module and runtimes agree): it loads and runs everything, the
# worker's self-test judged by its runtime copy's bytes. When Windows refuses it (a missing Visual C++
# runtime), its files explain nothing: the loader's error is the reason and they stay warnings.
one_build={CLIENT:FRESH,RUNTIME:FRESH,COPY:FRESH}
fresh_info=identity({'sha256':SHAS[CLIENT]},{'sha256':SHAS[RUNTIME]},label='2.3.0',build=FRESH)
lua_,g,M,ok=session(fresh(builds=one_build),one_build,info=fresh_info);s=M.GetInstallationStatus()
assert ok is True and M.native is not None and s.features.core and s.features.rendering and s.probePending and codes(s)==['damaged_or_unrecognized']*5,codes(s)
g.ANSWER(convert(worker(build=FRESH,runtime_sha=SHAS[COPY],label='2.3.0'),lua_))
assert s.features.imports and s.features.detailedCollision and codes(s)==['damaged_or_unrecognized']*5 and M.FeatureAvailable('imports') is True and M.loadError is None,codes(s)
assert M.NativeReleaseAtLeast('2.3.0') is True and M.NativeReleaseAtLeast('2.4.0') is False and s.installed is None and s.update is None
lua_,g,M,ok=session(fresh(builds=one_build),one_build,info=fresh_info,require_error='The specified procedure could not be found.');s=M.GetInstallationStatus()
refused=L('install.error.loader_failed',reason='The specified procedure could not be found.')
assert ok is False and 'require mmdhl' in events(g) and codes(s)==['damaged_or_unrecognized']*5+['loader_failed'] and same(M,M.loadError,refused),codes(s)
assert all(v.warning is True for v in listed(s.issues)[:5]) and not any(features(s))

# No module file for this realm: nothing to load, require is never called.
files=installed();del files[CLIENT]
lua_,g,M,ok=session(files);s=M.GetInstallationStatus()
missing=L('install.error.file_missing',path='lua/bin/'+NAMES['client'])
assert ok is False and 'require mmdhl' not in events(g) and M.native is None and not any(features(s)) and s.blocked,events(g)
assert codes(s)==['missing'] and s.issues[1].warning is None and same(M,M.loadError,missing)
# A missing runtime beside a module: require is tried. The module runs when it loads (Windows found a
# runtime elsewhere), with the warning; when it does not load, the missing runtime is the reason.
files=installed();del files[RUNTIME]
elsewhere=identity(runtime_at='C:\\Windows\\System32\\'+NAMES['runtime'])
lua_,g,M,ok=session(files,info=elsewhere);s=M.GetInstallationStatus()
assert ok is True and M.native is not None and s.features.core and s.features.rendering and 'require mmdhl' in events(g),codes(s)
assert issue(s,'missing')[0].warning is True and issue(s,'missing')[0].component=='runtime' and issue(s,'loaded_mismatch')[0].warning is True and M.loadError is None
lua_,g,M,ok=session(files,require_error='The specified module could not be found.');s=M.GetInstallationStatus()
assert ok is False and 'require mmdhl' in events(g) and codes(s)==['missing','loader_failed'] and s.issues[1].warning is None
assert same(M,M.loadError,L('install.error.file_missing',path='bin/win64/'+NAMES['runtime'])) and not any(features(s))

# Another platform: nothing is read and nothing loads; the platform is the reason.
for windows,arch in ((False,'x64'),(True,'x86')):
    lua_,g,M,ok=session(windows=windows,arch=arch);s=M.GetInstallationStatus()
    assert ok is False and 'require mmdhl' not in events(g) and len(g.LOOKED)==0 and not any(features(s)) and codes(s)==['unsupported_platform'] and s.issues[1].warning is None
    assert same(M,M.loadError,L('install.error.unsupported_platform'))
# A broken policy: require is tried; the files on disk identify the loaded module (status.expected nil).
for broken in ({'schema':1},dict(policy,recommended='0.0.0')):
    lua_,g,M,ok=session(p=broken);s=M.GetInstallationStatus()
    assert ok is True and 'require mmdhl' in events(g) and M.native is not None and s.features.core and s.features.rendering and s.expected is None,codes(s)
    assert codes(s)==['policy_invalid'] and s.issues[1].warning is True and s.update is None and M.loadError is None
    assert M.NativeReleaseAtLeast(release['release']) is True and M.NativeReleaseAtLeast('99.0.0') is False
    # identitiesMatch ran: a runtime loaded elsewhere is told, another interface too, without an error.
    lua_,g,M,ok=session(p=broken,info=elsewhere);s=M.GetInstallationStatus()
    assert ok is True and codes(s)==['policy_invalid','loaded_mismatch'] and same(M,s.issues[2].message,L('install.error.loaded_runtime_mismatch'))
    newer=identity();newer['module']['api']=2
    lua_,g,M,ok=session(p=broken,info=newer);s=M.GetInstallationStatus()
    assert ok is True and codes(s)==['policy_invalid','loaded_mismatch'] and s.update is None and s.features.core
# CoACD is the worker's to load: whether the addon knows the file or not, the self-test decides. When it
# fails there, detailed collision is off (simple hulls remain; imports run): a missing file is then the
# reason, a file this addon does not know never is.
coacd_failed=dict(worker(),coacd=False,coacdError='lib_coacd.dll could not be loaded')
files=installed();del files[COACD]
lua_,g,M,ok=session(files);s=M.GetInstallationStatus()
assert ok is True and codes(s)==['missing'] and s.issues[1].warning is True and s.issues[1].feature=='detailedCollision' and s.features.detailedCollision
g.ANSWER(convert(coacd_failed,lua_))
assert codes(s)==['missing','dependency_failed'] and s.issues[1].warning is None and s.issues[2].warning is None and s.issues[2].probe,codes(s)
assert s.features.core and s.features.imports and not s.features.detailedCollision and M.FeatureAvailable('imports') is True
assert M.FeatureAvailable('detailedCollision')==(False,L('install.error.file_missing',path='lua/bin/'+NAMES['coacd']))
files=installed();files[COACD]['sha256']='e'*64
lua_,g,M,ok=session(files);s=M.GetInstallationStatus()
g.ANSWER(convert(coacd_failed,lua_))
assert codes(s)==['damaged_or_unrecognized','dependency_failed'] and s.issues[1].warning is True and not s.features.detailedCollision and s.features.imports,codes(s)
assert M.FeatureAvailable('detailedCollision')==(False,'lib_coacd.dll could not be loaded')
# Passing the self-test, a CoACD this addon does not know stays a warning: detailed collision on.
lua_,g,M,ok=session(files);s=M.GetInstallationStatus()
g.ANSWER(convert(worker(),lua_))
assert codes(s)==['damaged_or_unrecognized'] and s.features.detailedCollision and M.FeatureAvailable('detailedCollision') is True
# A failed self-test makes problems only of its own feature's causes: the runtime the loaded module
# found elsewhere stays a warning.
files=installed();del files[RUNTIME]
for result,code in ((coacd_failed,'dependency_failed'),(None,'worker_failed')):
    lua_,g,M,ok=session(files,info=elsewhere);s=M.GetInstallationStatus()
    if result: g.ANSWER(convert(result,lua_))
    else: g.ANSWER(False,'Worker self-test failed: exit code 3')
    failed=issue(s,code)
    assert len(failed)==1 and failed[0].warning is None and issue(s,'missing')[0].warning is True and s.features.core and M.loadError is None,codes(s)
print('PASS: CheckInstallation always tries the files (one or two builds this addon does not know, a missing runtime, a broken policy), prints every '
      'warning before require, and skips it only without a module file or on another platform; a failed load names the warning that explains it, '
      'else the loader\'s error; the self-test decides CoACD')

# ---- Recheck (mmdhl_check_installation): files read again, never loaded again ----
# Nothing loaded: a restart loads the repaired files.
files=installed();del files[CLIENT]
lua_,g,M,ok=session(files);assert ok is False
g.PY_DISK[CLIENT]=convert({'size':release['files']['client']['size'],'sha256':release['files']['client']['sha256']},lua_)
s=M.CheckInstallation(True)
assert codes(s)==['restart_required'] and s.issues[1].warning is None and same(M,s.issues[1].message,L('install.error.restart_to_load')) and not any(features(s))
assert same(M,M.loadError,L('install.error.restart_to_load')) and 'require mmdhl' not in events(g) and M.native is None
# The self-test's warning carries over: imports stay on.
lua_,g,M,ok=session()
g.ANSWER(convert(worker(build='0123456789ab-20200101T000000Z'),lua_));s=M.GetInstallationStatus()
assert s.features.imports and codes(s)==['mixed_installation'] and s.issues[1].probe and s.issues[1].identity and s.issues[1].warning
s=M.CheckInstallation(True)
assert codes(s)==['mixed_installation'] and s.issues[1].probe and s.issues[1].warning and s.features.imports and M.FeatureAvailable('imports') is True and events(g).count('require mmdhl')==1
# A failed self-test carries over too: imports stay off with its reason, no restart is promised.
lua_,g,M,ok=session()
g.ANSWER(False,'Worker self-test failed: exit code 3')
s=M.CheckInstallation(True)
assert codes(s)==['worker_failed'] and not s.features.imports and M.FeatureAvailable('imports')==(False,'Worker self-test failed: exit code 3') and s.features.core
# A self-test that failed because its file is missing: the missing file stays the reason while it is
# missing; once it is back, a restart starts the repaired worker.
files=installed();del files[WORKER]
lua_,g,M,ok=session(files,probe_starts=False);s=M.GetInstallationStatus()
missing=L('install.error.file_missing',path='lua/bin/'+NAMES['worker'])
assert ok is True and not s.features.imports and s.features.core and codes(s)==['missing','worker_failed'] and s.issues[1].warning is None
assert M.FeatureAvailable('imports')==(False,missing)
s=M.CheckInstallation(True)
assert not s.features.imports and M.FeatureAvailable('imports')==(False,missing) and 'restart_required' not in codes(s),codes(s)
g.PY_DISK[WORKER]=convert({'size':release['files']['worker']['size'],'sha256':release['files']['worker']['sha256']},lua_)
s=M.CheckInstallation(True)
restart=L('install.error.restart_to_enable',feature=L('install.feature.imports'))
assert codes(s)==['restart_required'] and s.issues[1].feature=='imports' and not s.features.imports and M.FeatureAvailable('imports')==(False,restart),codes(s)
print('PASS: Recheck never loads again: restart_to_load when nothing loaded, the self-test\'s warnings and failures carry over, a repaired worker file asks for a restart')

# ---- Nothing of accepting unverified files is left in the addon ----
GONE=('SetUnverifiedAccepted','unverified_native.json','mmdhl_accept_unverified_native','mmdhl_revoke_unverified_native','accepted_tag','install.unverified',
      'unverifiedAccepted','acceptedIssues','CanAcceptUnverifiedNative','UsingUnverifiedNative','AcceptUnverifiedNative','RevokeUnverifiedNative',
      'ConfirmUnverifiedNative','install.button.use_anyway','install.button.stop_using','loadedFingerprint')
for p in sorted((ROOT/'addon').rglob('*')):
    if p.is_file() and p.suffix in ('.lua','.properties','.json','.txt'):
        text=p.read_text(encoding='utf-8')
        found=[word for word in GONE if word in text]
        assert not found,f'{p.relative_to(ROOT)} still mentions {found}'
print('PASS: no API, data file, console command, button or phrase for accepting unverified native files remains in addon/')

# ---- The package unpacked into the wrong folder (issue #6) ----
MODULE_FILE,RUNTIME_FILE=NAMES['client'],NAMES['runtime']
def wrong(files,find=None):
    def reader(path,search,*_):
        found=files.get(search+'/'+path)
        return (convert(found),None) if found else (None,'missing')
    env=convert(dict(server=False,dedicated=False,windows=True,arch='x64'))
    # file.Find in the game is a Lua function; a Python callable reaches Lua as userdata.
    if find: env.find=lua.eval('function(f) return function(p) return f(p) end end')(lambda pattern: lua.table_from(find(pattern)))
    return lua.globals().mmdhl.EvaluateInstallation(convert(policy),reader,env)
def stray(): return dict(size=1,sha256='e'*64)
M=lua.globals().mmdhl
# The contents of the package's GarrysMod folder copied into garrysmod: found, first, explained.
s=wrong({'BASE_PATH/garrysmod/garrysmod/lua/bin/'+MODULE_FILE:stray(),'BASE_PATH/garrysmod/bin/win64/'+RUNTIME_FILE:stray()})
first=listed(s.issues)[0]
assert first.code=='misplaced_package' and first.warning and first.cause and first.feature=='core' and first.detail=='garrysmod/garrysmod/lua/bin/'+MODULE_FILE,codes(s)
assert chr(92).join(['garrysmod','garrysmod','lua','bin',MODULE_FILE]) in M.Localize(first.message) and features(s)==CLIENT_ON
# The zip's GarrysMod folder dropped into garrysmod, or into the game folder.
for root in ('garrysmod/GarrysMod/','GarrysMod/','garrysmod/addons/'):
    s=wrong({'BASE_PATH/'+root+'bin/win64/'+RUNTIME_FILE:stray()})
    assert codes(s)[0]=='misplaced_package' and listed(s.issues)[0].detail==root+'bin/win64/'+RUNTIME_FILE,(root,codes(s))
# Only the runtime misplaced (the module is where it belongs).
files=installed();del files[RUNTIME];files['BASE_PATH/garrysmod/bin/win64/'+RUNTIME_FILE]=stray()
s=wrong(files);assert codes(s)[0]=='misplaced_package',codes(s)
# An unpacked release folder left where it was unpacked (found by name).
release_dir='Model-Hotloader-2.3.0-63b581c4-win64-vulkan'
s=wrong({'BASE_PATH/garrysmod/'+release_dir+'/GarrysMod/garrysmod/lua/bin/'+MODULE_FILE:stray()},find=lambda pattern: [release_dir] if pattern=='garrysmod/Model-Hotloader-*' else [])
assert codes(s)[0]=='misplaced_package' and listed(s.issues)[0].detail=='garrysmod/'+release_dir+'/GarrysMod/garrysmod/lua/bin/'+MODULE_FILE,codes(s)
# A copy left in the wrong folder beside a working installation says nothing.
files=installed();files['BASE_PATH/garrysmod/bin/win64/'+RUNTIME_FILE]=stray()
s=wrong(files);assert 'misplaced_package' not in codes(s),codes(s)
# Nothing loads then (no module where the game looks): require is never tried, and the
# misplaced folder, not the missing file, is the reason every player reads.
files={k:v for k,v in installed().items() if k not in (CLIENT,RUNTIME)}
files['BASE_PATH/garrysmod/garrysmod/lua/bin/'+MODULE_FILE]=stray()
lua_,g,M2,ok=session(files);s=M2.GetInstallationStatus()
assert ok is False and 'require mmdhl' not in events(g) and not s.features.core,events(g)
misplaced_issue=issue(s,'misplaced_package')[0]
assert not misplaced_issue.warning and M2.loadError==misplaced_issue.message,(M2.Localize(M2.loadError),codes(s))
print('PASS: a package unpacked into garrysmod, the addons folder, a nested GarrysMod folder or a release folder is found and named first; a stray copy beside a working installation is not')
