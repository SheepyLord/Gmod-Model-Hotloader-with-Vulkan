"""Execute installation policy against failure fixtures without loading a DLL."""
from pathlib import Path
from copy import deepcopy
import json, re, sys
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

def convert(x):
    if isinstance(x,dict): return lua.table_from({k:convert(v) for k,v in x.items()})
    if isinstance(x,list): return lua.table_from([convert(v) for v in x])
    return x

policy=lua_policy(ROOT/'addon/lua/mmdhl/native_policy.lua')
release=policy['releases'][policy['recommended']]
def installed(server=False,dedicated=False):
    result={}
    for key,record in release['files'].items():
        result['MOD/lua/bin/'+record['name']]=dict(size=record['size'],sha256=record['sha256'],path='fixture/'+record['name'])
    result['BASE_PATH/'+('' if dedicated else 'bin/win64/')+release['files']['runtime']['name']]=deepcopy(result['MOD/lua/bin/'+release['files']['runtime']['name']])
    return result

count=0
def evaluate(files=None,server=False,dedicated=False,windows=True,arch='x64',p=None,accepted=None):
    global count
    count+=1; reads=[]; files=installed(server,dedicated) if files is None else files
    def reader(path,search,*_):
        key=search+'/'+path; reads.append(key)
        found=files.get(key)
        if isinstance(found,str): return None,found
        return (convert(found),None) if found else (None,'missing')
    s=lua.globals().mmdhl.EvaluateInstallation(convert(p or policy),reader,convert(dict(server=server,dedicated=dedicated,windows=windows,arch=arch,accepted=accepted)))
    return s,reads

s,_=evaluate();assert s.features.core and s.features.imports and s.features.detailedCollision and s.update is None
for key in ('client','runtime','worker','coacd'):
    path=('BASE_PATH/bin/win64/' if key=='runtime' else 'MOD/lua/bin/')+release['files'][key]['name']
    feature='core' if key in ('client','runtime') else 'imports' if key=='worker' else 'detailedCollision'
    for failure in ('missing','unreadable','truncate','modified'):
        files=installed()
        if failure=='missing': del files[path]
        elif failure=='unreadable': files[path]='unreadable'
        elif failure=='truncate': files[path]['size']-=1
        else: files[path]['sha256']='0'*64
        s,_=evaluate(files);assert not s.features[feature],(key,failure)
        if key in ('worker','coacd'): assert s.features.core
files=installed();files['MOD/lua/bin/'+release['files']['runtime']['name']]['sha256']='0'*64
s,_=evaluate(files);assert s.features.core and not s.features.imports
for windows,arch in ((False,'x64'),(True,'x86'),(True,'arm64')):
    s,reads=evaluate(windows=windows,arch=arch);assert s.issues[1].code=='unsupported_platform' and not reads
for dedicated in (False,True):
    s,reads=evaluate(server=True,dedicated=dedicated);assert s.features.core and not s.features.imports
    assert not any('worker' in x or 'coacd' in x or 'gmcl' in x for x in reads)
# A dedicated server loads the runtime beside srcds_win64.exe, else from bin/win64,
# where the native package puts it; the check verifies the copy it will load.
name=release['files']['runtime']['name'];beside='BASE_PATH/'+name;packaged='BASE_PATH/bin/win64/'+name
package=installed(True,True);package[packaged]=package.pop(beside)
s,_=evaluate(package,server=True,dedicated=True);assert s.features.core and s.files.runtime.relative=='bin/win64/'+name
both=deepcopy(package);both[beside]=dict(package[packaged],sha256='7'*64)
s,_=evaluate(both,server=True,dedicated=True)
assert not s.features.core and s.files.runtime.relative==name and s.issues[1].code=='damaged_or_unrecognized'
both[beside]='unreadable'
s,_=evaluate(both,server=True,dedicated=True);assert not s.features.core and s.files.runtime.relative==name and s.issues[1].code=='unreadable'
neither=installed(True,True);del neither[beside]
s,_=evaluate(neither,server=True,dedicated=True);assert not s.features.core and s.files.runtime.relative==name and s.issues[1].code=='missing'
# Clients and listen servers never take the runtime from beside the executable.
for server in (False,True):
    beside_only=installed(server,True)
    s,_=evaluate(beside_only,server=server);assert not s.features.core and s.files.runtime.relative=='bin/win64/'+name
old=deepcopy(release);old['release']='1.0.0';old['build']='old'
for f in old['files'].values():f['sha256']='1'*64
p=deepcopy(policy);p['releases']['1.0.0']=old
files=installed()
for f in files.values():f['sha256']='1'*64
# An older release no longer approved still runs: one warning that disables nothing,
# and the update reminder with the recommended release's links.
s,_=evaluate(files,p=p)
assert s.features.core and s.features.imports and s.features.rendering and not s.unverified and not s.blocked and s.installed=='1.0.0'
assert len(s.issues)==1 and s.issues[1].code=='outdated_release' and s.issues[1].warning and not s.issues[1].unverified and s.issues[1].feature=='core'
u=s.update
assert u.installed=='1.0.0' and u.recommended==policy['recommended'] and u.url==release['url'] and u.altUrl==release.get('altUrl') and u.approved is None and u.required is None and u.advisory is None
# Approved, it runs without any issue (publish-native-release.py requires that) and still gets the reminder.
p['approved'].append('1.0.0');s,_=evaluate(files,p=p);assert s.features.core and s.installed=='1.0.0' and len(s.issues)==0 and s.update.approved is True and s.update.installed=='1.0.0'
files=installed();files['BASE_PATH/bin/win64/'+release['files']['runtime']['name']]['sha256']='1'*64
s,_=evaluate(files,p=p);assert not s.features.core and any(v.code=='mixed_installation' for v in s.issues.values())
s,_=evaluate(p={'schema':1});assert s.issues[1].code=='policy_invalid' and s.update is None
future=deepcopy(old);future['release']='9.0.0';future['build']='future'
p=deepcopy(policy);p['releases']['9.0.0']=future
files=installed()
for f in files.values():f['sha256']='1'*64
s,_=evaluate(files,p=p);assert not s.features.core and s.issues[1].code=='unapproved_release' and s.unverified and s.update is None
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
s,_=evaluate(disk('6'*64),p=p);assert not s.features.core and s.issues[1].code=='unapproved_release' and s.update is None
p=deepcopy(policy);p['releases']['2.3.0']=deepcopy(release);p['releases']['2.3.0'].update(release='2.3.0',build='local');p['recommended']='2.3.0'
synthetic(p,'2.3.0-rc.1','local','7'*64);synthetic(p,'2.3.0+hotfix','local','8'*64)
s,_=evaluate(disk('7'*64),p=p);assert s.features.core and s.update.installed=='2.3.0-rc.1' and s.update.recommended=='2.3.0'
s,_=evaluate(disk('8'*64),p=p);assert s.issues[1].code=='unapproved_release' and s.update is None
for label,older in (('2.1.0-native.12',True),('2.2.0',True),('2.3.1',False),('10.0.0',False),('legacy-preview',True)):
    synthetic(p,label,'local','9'*64);s,_=evaluate(disk('9'*64),p=p);del p['releases'][label]
    assert (s.update is not None)==older,label

# Accepting the warning: hash and release mismatches run anyway, for exactly the
# accepted bytes; missing files and releases without verification never do.
client='MOD/lua/bin/'+release['files']['client']['name'];runtime='BASE_PATH/bin/win64/'+release['files']['runtime']['name']
files=installed();files[client]['sha256']='2'*64
s,_=evaluate(files);assert not s.features.core and s.unverified and not s.blocked and not s.unverifiedAccepted
fingerprint=s.fingerprint
s,_=evaluate(files,accepted=fingerprint)
assert s.features.core and s.features.imports and s.unverifiedAccepted and not s.unverified and s.installed is None
assert all(v.accepted for v in s.issues.values()) and s.issues[1].code=='damaged_or_unrecognized'
changed=deepcopy(files);changed[client]['sha256']='3'*64
s,_=evaluate(changed,accepted=fingerprint);assert not s.features.core and s.unverified
s,_=evaluate(files,server=True,accepted=fingerprint);assert s.fingerprint!=fingerprint
mixed=installed();mixed[runtime]['sha256']='1'*64;p=deepcopy(policy);p['releases']['1.0.0']=old
s,_=evaluate(mixed,p=p);assert s.issues[1].code=='mixed_installation' and not s.features.core
s,_=evaluate(mixed,p=p,accepted=s.fingerprint);assert s.features.core
worker=installed();worker['MOD/lua/bin/'+release['files']['worker']['name']]['sha256']='2'*64
s,_=evaluate(worker);assert s.features.core and not s.features.imports and s.unverified
s,_=evaluate(worker,accepted=s.fingerprint);assert s.features.imports
broken=deepcopy(files);del broken[runtime]
s,_=evaluate(broken);assert s.unverified and s.blocked
s,_=evaluate(broken,accepted=s.fingerprint);assert not s.features.core and any(v.code=='missing' and not v.accepted for v in s.issues.values())
p=deepcopy(policy);p['releases']['9.0.0']=future
files=installed()
for f in files.values():f['sha256']='1'*64
s,_=evaluate(files,p=p);s,_=evaluate(files,p=p,accepted=s.fingerprint)
assert s.features.core and s.installed=='9.0.0' and s.issues[1].code=='unapproved_release' and s.issues[1].accepted
legacy=deepcopy(future);legacy['installApi']=0;p=deepcopy(policy);p['releases']['legacy']=legacy
# A release without installation verification stays off, worded as a required update with the same links.
s,_=evaluate(files,p=p);assert s.issues[1].code=='outdated' and s.blocked and not s.features.core
assert s.update.required and s.update.installed=='legacy' and s.update.url==release['url'] and s.update.recommended==policy['recommended']
s,_=evaluate(files,p=p,accepted=s.fingerprint);assert not s.features.core

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
# Accepted unverified files: the loaded bytes must equal the files on disk; release and build may differ.
files=installed();files[client]['sha256']='2'*64;files[runtime]['sha256']='4'*64
s,_=evaluate(files);accepted,_=evaluate(files,accepted=s.fingerprint)
lua.globals().TEST_STATUS=accepted
validator=lua.execute('local status=TEST_STATUS\n'+part+'\nreturn identitiesMatch')
unverified=deepcopy(info);unverified['module'].update(build='custom',release='custom',sha256='2'*64);unverified['runtime'].update(build='custom',sha256='4'*64)
assert validator(convert(unverified)) is True
for key,field,value in [('module','sha256','5'*64),('runtime','api',99),('module','path','C:/shadow/module.dll')]:
    broken=deepcopy(unverified);broken[key][field]=value
    assert validator(convert(broken))[0] is False
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
# from bin/win64. Loaded from there while the check verified a copy beside
# srcds_win64.exe, it is a file the administrator may load anyway.
srcds='D:\\GMod Server\\'
dedicated=deepcopy(info)
dedicated['module'].update(sha256=release['files']['server']['sha256'],path=srcds+'garrysmod\\lua\\bin\\'+release['files']['server']['name'],expectedPath=srcds+'garrysmod\\lua\\bin\\'+release['files']['server']['name'])
dedicated['runtime'].update(path=srcds+'bin\\win64\\'+name,expectedPath=srcds+name)
for files,verified in ((package,True),(installed(True,True),False)):
    lua.globals().TEST_STATUS,_=evaluate(files,server=True,dedicated=True)
    result=lua.execute('local status=TEST_STATUS\n'+part+'\nreturn identitiesMatch')(convert(dedicated))
    assert result is True if verified else (result[0] is False and result[2] is True),files.keys()
# Elsewhere, or other bytes, the player may load anyway; another interface never loads.
for key,field,value,overridable in [('runtime','path',game+'shadow\\'+release['files']['runtime']['name'],True),('runtime','sha256','6'*64,True),
                                     ('module','build','other',True),('module','api',99,False),('runtime','installApi',0,False)]:
    broken=deepcopy(layouts);broken[key][field]=value
    ok,message,allowed=validator(convert(broken))
    assert ok is False and allowed is overridable and 'does not match' in message,(key,field)
# Loading anyway extends the accepted fingerprint; it still accepts the checked files.
files=installed();files[client]['sha256']='2'*64
s,_=evaluate(files);s,_=evaluate(files,accepted=s.fingerprint+';loaded:module=1:x,runtime=2:y')
assert s.features.core and s.unverifiedAccepted and all(v.accepted for v in s.issues.values())
s,_=evaluate(files,accepted=s.fingerprint+';other');assert not s.features.core and not s.unverifiedAccepted
print(f'PASS: {count} installation policy scenarios (including accepted unverified files and where a dedicated server finds its runtime), eighteen loaded-identity checks (both 64-bit layouts and a dedicated server) and all addon Lua syntax')
