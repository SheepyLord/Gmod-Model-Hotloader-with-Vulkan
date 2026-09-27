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

s,_=evaluate();assert s.features.core and s.features.imports and s.features.detailedCollision
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
old=deepcopy(release);old['release']='1.0.0';old['build']='old'
for f in old['files'].values():f['sha256']='1'*64
p=deepcopy(policy);p['releases']['1.0.0']=old
files=installed()
for f in files.values():f['sha256']='1'*64
s,_=evaluate(files,p=p);assert not s.features.core and s.issues[1].code=='outdated'
p['approved'].append('1.0.0');s,_=evaluate(files,p=p);assert s.features.core and s.installed=='1.0.0'
files=installed();files['BASE_PATH/bin/win64/'+release['files']['runtime']['name']]['sha256']='1'*64
s,_=evaluate(files,p=p);assert not s.features.core and any(v.code=='mixed_installation' for v in s.issues.values())
s,_=evaluate(p={'schema':1});assert s.issues[1].code=='policy_invalid'
future=deepcopy(old);future['release']='9.0.0';future['build']='future'
p=deepcopy(policy);p['releases']['9.0.0']=future
files=installed()
for f in files.values():f['sha256']='1'*64
s,_=evaluate(files,p=p);assert not s.features.core and s.issues[1].code=='unapproved_release'

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
s,_=evaluate(files,p=p);assert s.issues[1].code=='outdated' and s.blocked
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
print(f'PASS: {count} installation policy scenarios (including accepted unverified files), nine loaded-identity checks and all addon Lua syntax')
