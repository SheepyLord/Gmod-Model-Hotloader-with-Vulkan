"""File access shows its dialogs in mmdhl_worker.exe, so installation.lua's worker gate covers
it as it covers imports: while the installation check does not allow the worker (its self-test
still running, or failed: a worker that does not start or answer, or whose file is missing, which
is then the reason), FileAccessPick and FileAccessSetEnabled(true) are refused with the check's
reason and the code "worker_unavailable", and native is not called. FileAccessRequest reaches
native marked noDialog: a folder the player always allowed still answers (that needs no window),
anything else is refused with the check's reason. Turning file access off and the calls that
start no worker (info, polls, reads, grants, revoke) always pass. Once the self-test passes,
everything reaches native unchanged, without the import options rewrite. A worker this addon
does not know (other bytes, or a self-test reporting another build or release) is only a
warning: file access and imports reach native as for a passing self-test.

With file_access.lua loaded on top (the game's order): while the self-test runs, IsAvailable()
says true when MMDHL.FileAccessReady runs and requests that need a window wait for its verdict
(remembered folders answer at once); after it, they go to native (a worker from another build
too, without MMDHL.FileAccessChanged) or, when it failed, are refused once, and an addon hears
MMDHL.FileAccessChanged when the verdict changed what IsAvailable() says."""
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
REFUSED = {'FileAccessPick', 'FileAccessSetEnabled'}
OTHER_RELEASE = json.dumps(dict(identity=dict(release='2.2.0', build='old'), runtime=dict(build='old', sha256='0' * 64), coacd=True))
# A worker and its runtime copy that report two builds.
TWO_BUILDS = json.dumps(dict(identity=dict(release=release['release'], build='aaaaaaaaaaaa-20261001T000000Z'), runtime=dict(build='bbbbbbbbbbbb-20261002T000000Z', sha256=release['files']['runtime']['sha256']), coacd=True))


def session(probe_starts=True, worker_sha=None, file_access=False, missing_worker=False):
    """A client whose installation check ran; the worker's self-test waits for PROBE() (or FAIL()).
    With file_access, a native file access module that answers like the real one, and file_access.lua."""
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
    def decode(s):
        # GMod's util.JSONToTable: nil for anything that is not JSON.
        try: return to_lua(json.loads(s)) if s else None
        except (ValueError, TypeError): return None
    # GMod's util.TableToJSON writes every number as a float (12 -> 12.0).
    floats = lambda x: {k: floats(v) for k, v in x.items()} if isinstance(x, dict) else [floats(v) for v in x] if isinstance(x, list) else float(x) if isinstance(x, int) and not isinstance(x, bool) else x
    g.PY_DECODE = decode
    g.PY_ENCODE = lambda t: json.dumps(floats(from_lua(t)))
    g.NATIVE_JSON = lambda t: json.dumps(from_lua(t))
    disk = {'MOD/lua/bin/' + f['name']: dict(f) for f in release['files'].values()}
    disk['BASE_PATH/bin/win64/' + release['files']['runtime']['name']] = dict(release['files']['runtime'])
    if worker_sha:
        disk['MOD/lua/bin/' + release['files']['worker']['name']]['sha256'] = worker_sha
    if missing_worker:
        del disk['MOD/lua/bin/' + release['files']['worker']['name']]
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
    g.PY_FILE_ACCESS = file_access
    lua.execute(r'''
util={JSONToTable=function(s) return PY_DECODE(s) end,TableToJSON=function(t) return PY_ENCODE(t) end,AddNetworkString=function() end}
file={}
function file.Exists(p,s) if s=='DATA' then return false end return PY_DISK[s..'/'..p]~=nil end
function file.Open(p,m,s) local f=PY_DISK[s..'/'..p] return {Size=function() return f.size end,Read=function(_,n) return string.rep('x',n) end,Close=function() end} end
function file.Read() return nil end
function file.Write() end
function file.CreateDir() end
net={Start=function() end,WriteString=function() end,Broadcast=function() end,Receive=function() end,SendToServer=function() end}
-- GMod's hook library: a hook that returns a value stops the others.
HOOKS={}
hook={Add=function(event,name,f) HOOKS[event]=HOOKS[event] or {} HOOKS[event][name]=f end,
 Remove=function(event,name) if HOOKS[event] then HOOKS[event][name]=nil end end,
 Run=function(event,...) for _,f in pairs(HOOKS[event] or {}) do local r=f(...) if r~=nil then return r end end end}
TIMERS={} SIMPLE={} timer={Create=function(name,_,_,f) TIMERS[name]=f end,Remove=function(name) TIMERS[name]=nil end,Simple=function(_,f) SIMPLE[#SIMPLE+1]=f end}
concommand={Add=function() end}
system={IsWindows=function() return true end,IsLinux=function() return false end,IsWindowed=function() return true end} jit={arch='x64'}
game={IsDedicated=function() return false end,SinglePlayer=function() return true end}
NOW=0 CurTime=function() return NOW end RealTime=function() return NOW end MsgN=function() end
istable=function(v) return type(v)=='table' end
isstring=function(v) return type(v)=='string' end
isfunction=function(v) return type(v)=='function' end
isnumber=function(v) return type(v)=='number' end
-- GMod's IsValid: a table counts only through its own IsValid method.
IsValid=function(v) if not v then return false end local f=v.IsValid if not f then return false end return f(v) and true or false end
LocalPlayer=function() return {IsValid=function() return true end,IsListenServerHost=function() return false end} end
ERRORS={} ErrorNoHalt=function(m) ERRORS[#ERRORS+1]=m end
NOTICES={} notification={AddProgress=function(id,text) NOTICES[#NOTICES+1]=text end,Kill=function() end,AddLegacy=function(text) NOTICES[#NOTICES+1]=text end}
NOTIFY_HINT=1 NOTIFY_ERROR=2 surface={PlaySound=function() end}
function include(p) if p=='mmdhl/native_policy.lua' then return util.JSONToTable(PY_POLICY) end return {} end
-- The native module: what reached it, and a probe that answers (or fails) when the test says.
CALLS={} PROBE_RESULT=nil PROBE_ERROR=nil
local function record(name,...) CALLS[#CALLS+1]={name=name,args={...}} end
-- ANSWER[name], when set, is what that native function answers instead.
ANSWER={}
local function called(name) return function(...) record(name,...) if ANSWER[name] then return ANSWER[name](...) end return 'native '..name end end
-- With file access: answers like file_access.cpp (a folder "Saved" always allowed holds D:\Saved;
-- noDialog lets only it answer; anything else waits in a dialog).
STATES={} SEQ=0
local function dialog() SEQ=SEQ+1 STATES[SEQ]={state='pending',dialog=true} return SEQ end
REAL={
 FileAccessInfo=function() record('FileAccessInfo') return '{"version":1,"available":true,"reason":"ok","enabled":true}' end,
 FileAccessPick=function(j) record('FileAccessPick',j) return dialog() end,
 FileAccessRequest=function(j) record('FileAccessRequest',j) local r=util.JSONToTable(j)
  if r.requester=='Saved' and r.path:sub(1,9)=='D:\\Saved\\' then SEQ=SEQ+1 STATES[SEQ]={state='granted',items={{handle=string.rep('a',32),name='a.json',remembered=true}}} return SEQ end
  if r.noDialog then return nil,"Model Hotloader's installation check does not allow the worker that shows file access windows",'worker_unavailable' end
  return dialog() end,
 FileAccessPoll=function(id) record('FileAccessPoll',id) local s=STATES[id] if not s then return nil,'Unknown file request','unknown_request' end if s.state~='pending' then STATES[id]=nil end return NATIVE_JSON(s) end,
 FileAccessSetEnabled=function(on,language) record('FileAccessSetEnabled',on,language) if not on then return '{"enabled":false}' end return NATIVE_JSON({request=dialog()}) end,
}
function require()
 mmdhl_native={GetInstallationInfo=function() return PY_INFO end,ConfigureCompatibility=function() return '{"configured":true}' end,CheckCompatibility=function() return PY_REPORT end,
  StartInstallationProbe=function() if PY_PROBE_STARTS then return true end return false,'The worker could not be started.' end,
  PollInstallationProbe=function() if PROBE_ERROR then return nil,PROBE_ERROR end if PROBE_RESULT then return PROBE_RESULT end return '{"pending":true}' end,
  Browse=called('Browse'),BeginImport=called('BeginImport')}
 for _,name in ipairs(FILE_ACCESS) do mmdhl_native[name]=PY_FILE_ACCESS and REAL[name] or called(name) end
end
function PROBE(result) PROBE_RESULT=result TIMERS['MMDHL.InstallationWorker']() end
function FAIL(why) PROBE_ERROR=why TIMERS['MMDHL.InstallationWorker']() end
function LAST() return CALLS[#CALLS] end
function COUNT(name) local n=0 for _,c in ipairs(CALLS) do if c.name==name then n=n+1 end end return n end
function THINK() local f=HOOKS.Think and HOOKS.Think['MMDHL.FileAccess'] if f then f() end NOW=NOW+0.2 end
function CALLBACK() local c={runs=0} c.fn=function(...) c.runs=c.runs+1 c.args={...} end return c end
''')
    g.FILE_ACCESS = lua.table_from(FILE_ACCESS)
    source = (ROOT / 'addon/lua/mmdhl/installation.lua').read_text(encoding='utf8')
    hashed = "value={size=size,sha256=util.SHA256(bytes),path=search..'/'..path}"
    assert hashed in source
    lua.execute(source.replace(hashed, "value={size=size,sha256=PY_DISK[search..'/'..path].sha256,path=search..'/'..path}"))
    assert g.mmdhl.CheckInstallation() is True
    if file_access:
        # autorun/mmdhl.lua includes file_access.lua after the installation check.
        g.FA_SOURCE = (ROOT / 'addon/lua/mmdhl/file_access.lua').read_text(encoding='utf8')
        lua.execute("assert(load(FA_SOURCE,'@lua/mmdhl/file_access.lua'))()")
    return lua, g


def gated(lua, g, reason):
    """Calls that start the worker are refused with the reason, before native; a request reaches
    native marked noDialog and its refusal carries the reason; the rest pass unchanged."""
    M = g.mmdhl
    for name in FILE_ACCESS:
        calls = len(g.CALLS)
        args = (True, 'en') if name == 'FileAccessSetEnabled' else ('{"requester":"Gate","path":"C:\\\\a.json"}',)
        result = getattr(M.native, name)(*args)
        if name in REFUSED:
            assert result[0] is None and result[2] == 'worker_unavailable' and M.Localize(result[1]) == M.Localize(reason) and len(g.CALLS) == calls, (name, result)
        elif name == 'FileAccessRequest':
            sent = json.loads(g.LAST().args[1])
            assert len(g.CALLS) == calls + 1 and sent == {'requester': 'Gate', 'path': 'C:\\a.json', 'noDialog': True}, sent
            # The fake answers 'native ...': an id passes through as native gave it.
            assert (result[0] if isinstance(result, tuple) else result) == 'native FileAccessRequest', result
        else:
            assert result == 'native ' + name and len(g.CALLS) == calls + 1, (name, result)
    # Native's refusal of a request without a window reads as the installation check's reason;
    # its other refusals (file access turned off, a malformed path) stay native's.
    for code in ('worker_unavailable', 'disabled', 'invalid_path'):
        lua.execute(f"ANSWER.FileAccessRequest=function() return nil,'native sentence','{code}' end")
        result = M.native.FileAccessRequest('{"requester":"Gate","path":"C:\\\\a.json"}')
        want = M.Localize(reason) if code == 'worker_unavailable' else 'native sentence'
        assert result[0] is None and result[2] == code and M.Localize(result[1]) == want, (code, result)
    lua.execute('ANSWER.FileAccessRequest=nil')
    # Payloads the guard cannot read are refused, never sent with a window.
    calls = len(g.CALLS)
    for bad in ('not json', None):
        result = M.native.FileAccessRequest(bad)
        assert result[0] is None and result[2] == 'worker_unavailable' and len(g.CALLS) == calls, (bad, result)
    # Turning file access off needs no window.
    assert M.native.FileAccessSetEnabled(False, 'en') == 'native FileAccessSetEnabled' and g.LAST().args[1] is False


def passes(lua, g):
    M = g.mmdhl
    for name in ('FileAccessPick', 'FileAccessRequest', 'FileAccessSetEnabled'):
        payload = '{"requester":"Gate","options":{"collision":"detailed"}}'
        args = (True, 'fr') if name == 'FileAccessSetEnabled' else (payload,)
        assert getattr(M.native, name)(*args) == 'native ' + name, name
        sent = g.LAST().args
        assert sent[1] == args[0] and (len(args) < 2 or sent[2] == args[1]), (name, dict(sent.items()))


# ---- The guard (installation.lua alone) ----
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

# A worker from another release or build (the self-test reports another identity): a warning that
# disables nothing. File access and imports reach native as they are.
for answer, detail in ((OTHER_RELEASE, 'old/old'), (TWO_BUILDS, 'aaaaaaaaaaaa-20261001T000000Z/bbbbbbbbbbbb-20261002T000000Z')):
    lua, g = session()
    M = g.mmdhl
    g.PROBE(answer)
    s = M.GetInstallationStatus()
    warned = [v for v in s.issues.values() if v.code == 'mixed_installation']
    assert M.FeatureAvailable('imports') is True and s.features.imports and not s.probePending and M.loadError is None, [v.code for v in s.issues.values()]
    assert len(warned) == 1 and warned[0].warning and warned[0].identity and warned[0].probe and warned[0].component == 'worker' and warned[0].feature == 'imports' and warned[0].detail == detail
    assert M.Localize(warned[0].message) == M.Localize(M.L('install.error.worker_mismatch')) and s.unverified
    passes(lua, g)
    assert M.native.BeginImport('C:\\model.pmx', '{}') == 'native BeginImport' and g.LAST().name == 'BeginImport'
# A worker file with other bytes: a warning; the self-test still decides, and passes.
lua, g = session(worker_sha='7' * 64)
M = g.mmdhl
s = M.GetInstallationStatus()
assert s.probePending and [v.code for v in s.issues.values()] == ['damaged_or_unrecognized'] and s.issues[1].warning and s.issues[1].identity and s.issues[1].component == 'worker'
ok, checking = M.FeatureAvailable('imports')
assert ok is False and checking == M.L('install.checking_feature', lua.table_from({'feature': M.L('install.feature.imports')}))
gated(lua, g, checking)
g.PROBE(g.PY_IDENTITY)
assert M.FeatureAvailable('imports') is True
passes(lua, g)
print('PASS: a worker this addon does not know (other bytes, another build or release) is a warning: file access and imports reach native')

# A worker that does not start, one whose self-test fails, and one whose file is missing: the failed
# self-test (worker_failed) keeps imports off, with its reason; a missing file is that reason.
for kwargs, fail, reason in ((dict(probe_starts=False), None, 'The worker could not be started.'), ({}, 'Worker self-test failed: exit code 3', 'Worker self-test failed: exit code 3'),
                             (dict(probe_starts=False, missing_worker=True), None, None)):
    lua, g = session(**kwargs)
    M = g.mmdhl
    if fail: g.FAIL(fail)
    reason = reason or M.L('install.error.file_missing', lua.table_from({'path': 'lua/bin/' + release['files']['worker']['name']}))
    s = M.GetInstallationStatus()
    failed = [v for v in s.issues.values() if v.code == 'worker_failed']
    ok, why = M.FeatureAvailable('imports')
    assert ok is False and why == reason and not s.features.imports and s.features.core and not s.probePending, (kwargs, why)
    assert len(failed) == 1 and failed[0].warning is None and failed[0].probe and failed[0].feature == 'imports', kwargs
    if kwargs.get('missing_worker'): assert s.issues[1].code == 'missing' and s.issues[1].warning is None and s.issues[1].cause
    gated(lua, g, why)
    assert M.native.BeginImport('C:\\model.pmx', '{}') == (None, why) and g.COUNT('BeginImport') == 0
print('PASS: a worker the installation check does not allow (it failed its self-test, or is missing) never shows a file access dialog; turning file access off still works')

# ---- With file_access.lua: the self-test window and the verdict ----
lua, g = session(file_access=True)
lua.execute(r'''
local FA=mmdhl.FileAccess
assert(mmdhl.GetInstallationStatus().probePending)
-- Ready runs at once: during the self-test file access counts as available.
local ready hook.Add('MMDHL.FileAccessReady','addon',function(api) ready={api.IsAvailable()} end) for _,f in ipairs(SIMPLE) do f() end
assert(ready and ready[1]==true,'IsAvailable said no while the worker self-test ran: '..tostring(ready and ready[2]))
local changed=0 hook.Add('MMDHL.FileAccessChanged','addon',function() changed=changed+1 end)
-- A remembered folder answers at once, without a window and without waiting.
local saved=CALLBACK() assert(FA.RequestPath('D:\\Saved\\a.json',{addon='Saved'},saved.fn)==true)
assert(util.JSONToTable(LAST().args[1]).noDialog==true) THINK()
assert(saved.runs==1 and saved.args[1]==true and saved.args[2][1].remembered==true,'a remembered folder waited for the self-test')
-- What needs a window waits for the verdict: nothing reaches native, nobody is called back.
local picks,requests=COUNT('FileAccessPick'),COUNT('FileAccessRequest')
local picked,asked,enabled=CALLBACK(),CALLBACK(),CALLBACK()
assert(FA.Pick({addon='Waits'},picked.fn)==true and FA.RequestPath('D:\\Other\\b.json',{addon='Waits'},asked.fn)==true)
FA.SetEnabled(true,enabled.fn)
for i=1,5 do THINK() end
assert(picked.runs==0 and asked.runs==0 and enabled.runs==0 and COUNT('FileAccessPick')==picks and COUNT('FileAccessSetEnabled')==0,'a request did not wait for the self-test')
-- The self-test passes: they go to native as they are, once.
PROBE(PY_IDENTITY) THINK()
assert(COUNT('FileAccessPick')==picks+1 and COUNT('FileAccessRequest')==requests+2 and COUNT('FileAccessSetEnabled')==1,'the waiting requests did not reach native once')
for _,c in ipairs(CALLS) do if c.name=='FileAccessRequest' and util.JSONToTable(c.args[1]).path=='D:\\Other\\b.json' and util.JSONToTable(c.args[1]).noDialog==nil then asked.native=true end end
assert(asked.native,'the waiting request reached native still marked noDialog')
assert(picked.runs==0 and asked.runs==0 and changed==0,'a request ended before its dialog was answered')
for id,s in pairs(STATES) do if s.state=='pending' then STATES[id]={state='denied',code='denied',error='x'} end end
THINK() THINK()
assert(picked.runs==1 and picked.args[3]=='denied' and asked.runs==1 and asked.args[3]=='denied' and enabled.runs==1 and enabled.args[1]==false)
assert(FA.IsAvailable()==true and changed==0,'IsAvailable changed although the worker was allowed')
''')
print('PASS: during the worker self-test file access counts as available, a remembered folder answers at once and other requests wait for the verdict')

# The self-test finds a worker from another build: a warning; the waiting requests go to native as
# they are and IsAvailable never changes.
lua, g = session(file_access=True)
g.OTHER_RELEASE = OTHER_RELEASE
lua.execute(r'''
local FA=mmdhl.FileAccess
local changed=0 hook.Add('MMDHL.FileAccessChanged','addon',function() changed=changed+1 end)
local picked,asked=CALLBACK(),CALLBACK()
FA.Pick({addon='Waits'},picked.fn) FA.RequestPath('D:\\Other\\b.json',{addon='Waits'},asked.fn) THINK()
assert(picked.runs==0 and asked.runs==0 and COUNT('FileAccessPick')==0)
PROBE(OTHER_RELEASE) THINK()
assert(mmdhl.FeatureAvailable('imports')==true,'a worker from another build turned imports off')
assert(COUNT('FileAccessPick')==1,'the waiting pick did not reach native')
for _,c in ipairs(CALLS) do if c.name=='FileAccessRequest' and util.JSONToTable(c.args[1]).path=='D:\\Other\\b.json' and util.JSONToTable(c.args[1]).noDialog==nil then asked.native=true end end
assert(asked.native,'the waiting request did not reach native with its window')
assert(picked.runs==0 and asked.runs==0,'a request ended before its dialog was answered')
assert(FA.IsAvailable()==true and changed==0,'IsAvailable changed for a warning')
''')
print('PASS: a self-test that finds a worker from another build lets the waiting requests reach native; IsAvailable stays true')

lua, g = session(file_access=True)
lua.execute(r'''
local FA=mmdhl.FileAccess
local changed=0 hook.Add('MMDHL.FileAccessChanged','addon',function() changed=changed+1 end)
local picked,asked=CALLBACK(),CALLBACK()
FA.Pick({addon='Waits'},picked.fn) FA.RequestPath('D:\\Other\\b.json',{addon='Waits'},asked.fn) THINK()
assert(picked.runs==0 and asked.runs==0)
-- The self-test fails: the requests are refused once, with its reason.
FAIL('Worker self-test failed: exit code 3')
local ok,why=mmdhl.FeatureAvailable('imports') assert(not ok)
local expected=FA.Message('worker_unavailable',why)
THINK() THINK()
assert(COUNT('FileAccessPick')==0,'the worker the check refused was asked to show a window')
assert(picked.runs==1 and picked.args[1]==false and picked.args[3]=='worker_unavailable' and picked.args[2]==expected,tostring(picked.args[2]))
assert(asked.runs==1 and asked.args[3]=='worker_unavailable' and asked.args[2]==expected,tostring(asked.args[2]))
-- Addons hear that IsAvailable changed, once.
local ok2,text,code=FA.IsAvailable()
assert(changed==1 and ok2==false and code=='worker_unavailable' and text==expected,'no MMDHL.FileAccessChanged when the verdict changed IsAvailable: '..changed)
hook.Run('MMDHL.InstallationChanged') THINK() assert(changed==1,'MMDHL.FileAccessChanged without a change')
-- A remembered folder still answers: it needs no window.
local saved=CALLBACK() FA.RequestPath('D:\\Saved\\sub\\c.json',{addon='Saved'},saved.fn) THINK()
assert(saved.runs==1 and saved.args[1]==true,'a remembered folder was refused because of the worker')
-- And a new request that needs a window is refused at once (no waiting after the verdict).
local late=CALLBACK() assert(FA.Pick({addon='Late'},late.fn)==false) THINK() assert(late.runs==1 and late.args[3]=='worker_unavailable')
''')
# Turning file access on (the confirmation is a window) waits the same way, and is refused once.
lua, g = session(file_access=True, probe_starts=True)
lua.execute(r'''
local enabled=CALLBACK() mmdhl.FileAccess.SetEnabled(true,enabled.fn) THINK() THINK()
assert(enabled.runs==0 and COUNT('FileAccessSetEnabled')==0,'turning on did not wait for the self-test')
FAIL('Worker self-test failed: exit code 3') THINK() THINK()
assert(enabled.runs==1 and enabled.args[1]==false and COUNT('FileAccessSetEnabled')==0,'turning on after a failed self-test')
''')
print('PASS: after a failed worker self-test the waiting requests are refused once with its reason, addons hear MMDHL.FileAccessChanged, and remembered folders still answer')
