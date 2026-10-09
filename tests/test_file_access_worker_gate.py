"""File access shows its dialogs in mmdhl_worker.exe, so installation.lua's worker gate covers
it as it covers imports: while the installation check does not allow the worker (its self-test
still running or failed, a worker from another release), FileAccessPick, FileAccessRequest and
FileAccessSetEnabled(true) are refused with the check's reason and the code
"worker_unavailable", and native is not called. Turning file access off and the calls that start
no worker (info, polls, reads, grants, revoke) always pass. Once the self-test passes, everything
reaches native, without the import options rewrite."""
from pathlib import Path
import json, sys
from lupa import LuaRuntime
from lua_i18n import attach

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from compatibility_profiles import lua_policy

policy = lua_policy(ROOT / 'addon/lua/mmdhl/native_policy.lua')
release = policy['releases'][policy['recommended']]
FILE_ACCESS = ['FileAccessInfo', 'FileAccessPick', 'FileAccessRequest', 'FileAccessPoll', 'FileAccessRead', 'FileAccessPollRead', 'FileAccessList',
               'FileAccessPollList', 'FileAccessRelease', 'FileAccessCancel', 'FileAccessGrants', 'FileAccessRevoke', 'FileAccessSetEnabled']
STARTS_WORKER = {'FileAccessPick', 'FileAccessRequest', 'FileAccessSetEnabled'}


def session(probe_starts=True, worker_sha=None):
    """A client whose installation check ran; the worker's self-test waits for PROBE()."""
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute('unpack=table.unpack; mmdhl={}; SERVER=false CLIENT=true')
    attach(lua); lua.execute('L=mmdhl.L')
    g = lua.globals()
    to_lua = lambda x: lua.table_from({k: to_lua(v) for k, v in x.items()}) if isinstance(x, dict) else lua.table_from([to_lua(v) for v in x]) if isinstance(x, list) else x
    def from_lua(t):
        if hasattr(t, 'items'):
            d = dict(t.items())
            return [from_lua(d[i]) for i in sorted(d)] if d and all(isinstance(k, int) for k in d) else {k: from_lua(v) for k, v in d.items()}
        return t
    g.PY_DECODE = lambda s: to_lua(json.loads(s)) if s else None
    g.PY_ENCODE = lambda t: json.dumps(from_lua(t))
    disk = {'MOD/lua/bin/' + f['name']: dict(f) for f in release['files'].values()}
    disk['BASE_PATH/bin/win64/' + release['files']['runtime']['name']] = dict(release['files']['runtime'])
    if worker_sha:
        disk['MOD/lua/bin/' + release['files']['worker']['name']]['sha256'] = worker_sha
    g.PY_DISK = lua.table_from({k: lua.table_from({'size': v['size'], 'sha256': v['sha256']}) for k, v in disk.items()})
    game = 'C:\\game\\'
    module = dict(release=release['release'], build=release['build'], installApi=1, api=1, platform='win64', size=release['files']['client']['size'], sha256=release['files']['client']['sha256'],
                  path=game + 'garrysmod\\lua\\bin\\' + release['files']['client']['name'], expectedPath=game + 'garrysmod\\lua\\bin\\' + release['files']['client']['name'])
    runtime = dict(module, size=release['files']['runtime']['size'], sha256=release['files']['runtime']['sha256'],
                   path=game + 'bin\\win64\\mmdhl_runtime_win64.dll', expectedPath=game + 'bin\\win64\\mmdhl_runtime_win64.dll')
    g.PY_INFO = json.dumps(dict(module=module, runtime=runtime))
    g.PY_REPORT = json.dumps(dict(ready=True, pending=False, issues=[], libraries=[]))
    g.PY_PROBE_STARTS = probe_starts
    g.PY_IDENTITY = json.dumps(dict(identity=dict(release=release['release'], build=release['build']), runtime=dict(build=release['build'], sha256=release['files']['runtime']['sha256']), coacd=True))
    g.PY_POLICY = json.dumps(policy)
    lua.execute(r'''
util={JSONToTable=function(s) return PY_DECODE(s) end,TableToJSON=function(t) return PY_ENCODE(t) end,AddNetworkString=function() end}
file={}
function file.Exists(p,s) if s=='DATA' then return false end return PY_DISK[s..'/'..p]~=nil end
function file.Open(p,m,s) local f=PY_DISK[s..'/'..p] return {Size=function() return f.size end,Read=function(_,n) return string.rep('x',n) end,Close=function() end} end
function file.Read() return nil end
function file.Write() end
function file.CreateDir() end
net={Start=function() end,WriteString=function() end,Broadcast=function() end,Receive=function() end,SendToServer=function() end}
hook={Add=function() end,Run=function() end}
TIMERS={} timer={Create=function(name,_,_,f) TIMERS[name]=f end,Remove=function(name) TIMERS[name]=nil end,Simple=function() end}
concommand={Add=function() end}
system={IsWindows=function() return true end} jit={arch='x64'}
game={IsDedicated=function() return false end,SinglePlayer=function() return true end}
CurTime=function() return 0 end RealTime=function() return 0 end MsgN=function() end
istable=function(v) return type(v)=='table' end
function include(p) if p=='mmdhl/native_policy.lua' then return util.JSONToTable(PY_POLICY) end return {} end
-- The native module: what reached it, and a probe that answers when the test says.
CALLS={} PROBE_RESULT=nil
local function called(name) return function(...) CALLS[#CALLS+1]={name=name,args={...}} return 'native '..name end end
function require()
 mmdhl_native={GetInstallationInfo=function() return PY_INFO end,ConfigureCompatibility=function() return '{"configured":true}' end,CheckCompatibility=function() return PY_REPORT end,
  StartInstallationProbe=function() if PY_PROBE_STARTS then return true end return false,'The worker could not be started.' end,
  PollInstallationProbe=function() if PROBE_RESULT then return PROBE_RESULT end return '{"pending":true}' end,
  Browse=called('Browse'),BeginImport=called('BeginImport')}
 for _,name in ipairs(FILE_ACCESS) do mmdhl_native[name]=called(name) end
end
function PROBE(result) PROBE_RESULT=result TIMERS['MMDHL.InstallationWorker']() end
function LAST() return CALLS[#CALLS] end
''')
    g.FILE_ACCESS = lua.table_from(FILE_ACCESS)
    source = (ROOT / 'addon/lua/mmdhl/installation.lua').read_text(encoding='utf8')
    hashed = "value={size=size,sha256=util.SHA256(bytes),path=search..'/'..path}"
    assert hashed in source
    lua.execute(source.replace(hashed, "value={size=size,sha256=PY_DISK[search..'/'..path].sha256,path=search..'/'..path}"))
    assert g.mmdhl.CheckInstallation() is True
    return lua, g


def gated(lua, g, reason):
    """Calls that start the worker are refused with the reason, before native; the rest pass."""
    M = g.mmdhl
    for name in FILE_ACCESS:
        calls = len(g.CALLS)
        args = (True, 'en') if name == 'FileAccessSetEnabled' else ('{"requester":"Gate"}',)
        result = getattr(M.native, name)(*args)
        if name in STARTS_WORKER:
            assert result[0] is None and result[2] == 'worker_unavailable' and M.Localize(result[1]) == M.Localize(reason) and len(g.CALLS) == calls, (name, result)
        else:
            assert result == 'native ' + name and len(g.CALLS) == calls + 1, (name, result)
    # Turning file access off needs no window.
    assert M.native.FileAccessSetEnabled(False, 'en') == 'native FileAccessSetEnabled' and g.LAST().args[1] is False


def passes(lua, g):
    M = g.mmdhl
    for name in STARTS_WORKER:
        payload = '{"requester":"Gate","options":{"collision":"detailed"}}'
        args = (True, 'fr') if name == 'FileAccessSetEnabled' else (payload,)
        assert getattr(M.native, name)(*args) == 'native ' + name, name
        sent = g.LAST().args
        assert sent[1] == args[0] and (len(args) < 2 or sent[2] == args[1]), (name, dict(sent.items()))


# While the worker's self-test runs, nothing starts the worker; once it passes, file access reaches native.
lua, g = session()
M = g.mmdhl
ok, checking = M.FeatureAvailable('imports')
assert ok is False and g.mmdhl.GetInstallationStatus().probePending
gated(lua, g, checking)
g.PROBE(g.PY_IDENTITY)
assert M.FeatureAvailable('imports') is True
passes(lua, g)
print('PASS: file access waits for the worker self-test like imports, then reaches native unchanged')

# A worker from another release (the self-test reports another identity): refused with that reason.
lua, g = session()
M = g.mmdhl
g.PROBE(json.dumps(dict(identity=dict(release='2.2.0', build='old'), runtime=dict(build='old', sha256='0' * 64), coacd=True)))
ok, why = M.FeatureAvailable('imports')
assert ok is False and M.Localize(why) == M.Localize(M.L('install.error.worker_mismatch'))
gated(lua, g, why)
# A worker that cannot run at all, and one whose file is not the release's.
for kwargs in (dict(probe_starts=False), dict(worker_sha='7' * 64)):
    lua, g = session(**kwargs)
    ok, why = g.mmdhl.FeatureAvailable('imports')
    assert ok is False, kwargs
    gated(lua, g, why)
print('PASS: a worker the installation check does not allow never shows a file access dialog; turning file access off still works')
