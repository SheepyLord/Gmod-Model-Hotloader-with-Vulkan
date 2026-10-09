"""An older native module never stops the addon: installation.lua loads it against a
simulated game while the policy recommends a newer release. An approved older release
loads without issues and gets the update reminder (status.update); one no longer
approved loads with a warning that disables nothing. A compatibility policy newer than
the binary (a library it does not know) is offered without that library, then without
any, so the module still configures. Modules this Lua cannot drive stay off, worded as
a required update when they are older than the recommended release (a newer module, or
the recommended one with another interface, is a problem without a download). Also:
mmdhl.NativeReleaseAtLeast, the server's console line and
status, and publish-native-release.py's evaluate() with a newer recommended release."""
from pathlib import Path
from copy import deepcopy
import importlib.util, json, sys
from lupa import LuaRuntime
from lua_i18n import attach

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from compatibility_profiles import lua_policy

POLICY = lua_policy(ROOT / 'addon/lua/mmdhl/native_policy.lua')
COMPAT = lua_policy(ROOT / 'addon/lua/mmdhl/compatibility_policy.lua')
CURRENT = POLICY['recommended']
KNOWN = {'engine.dll', 'client.dll', 'vphysics.dll', 'materialsystem.dll', 'shaderapidx9.dll', 'stdshader_dx9.dll', 'stdshader_dx6.dll'}


def newer_policy(approve_current=True):
    """The shipped policy plus a newer recommended release 9.9.9 (other hashes, a later build)."""
    p = deepcopy(POLICY)
    record = deepcopy(p['releases'][CURRENT]); record.update(release='9.9.9', build='0123456789ab-20991231T000000Z', url='https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/releases/tag/9.9.9')
    for f in record['files'].values(): f['sha256'] = '9' * 64
    p['releases']['9.9.9'] = record; p['approved'].append('9.9.9'); p['recommended'] = '9.9.9'
    if not approve_current: p['approved'].remove(CURRENT)
    return p


class Binary:
    """The native compatibility configuration of one game process: validated whole, set once."""
    def __init__(self, known=KNOWN): self.known, self.configured, self.calls = known, None, 0
    def configure(self, text):
        self.calls += 1
        policy = json.loads(text)
        if policy.get('schema') != 1 or policy.get('family') != 'source-win64-v1' or not isinstance(policy.get('libraries'), list): return None, 'Unsupported compatibility profile schema or ABI family'
        if any(p['name'] not in self.known for p in policy['libraries']): return None, 'Invalid compatibility library profile'
        if self.configured is not None and self.configured != policy: return None, "Compatibility policy changed; restart Garry's Mod"
        self.configured = policy
        return '{"configured":true,"family":"source-win64-v1"}'


def session(policy, release=CURRENT, server=False, loaded=None, compat=COMPAT, binary=None, native=None, disk=None, accepted=None):
    """Runs CheckInstallation with release's files on disk and a module reporting loaded (default: that release)."""
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(f'unpack=table.unpack; mmdhl={{}}; SERVER={"true" if server else "false"} CLIENT=not SERVER')
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
    record = policy['releases'][release]
    files = disk or {key: dict(f) for key, f in record['files'].items()}
    on_disk = {'MOD/lua/bin/' + f['name']: f for f in files.values()}
    on_disk['BASE_PATH/bin/win64/' + files['runtime']['name']] = files['runtime']
    g.PY_DISK = lua.table_from({k: lua.table_from({'size': v['size'], 'sha256': v['sha256']}) for k, v in on_disk.items()})
    g.PY_DATA = lua.table_from({'mmd_hotloader/unverified_native.json': accepted} if accepted else {})
    game, role = 'C:\\game\\', 'server' if server else 'client'
    identity = dict(dict(release=record['release'], build=record['build'], installApi=1, api=1, platform='win64'), **(loaded or {}))
    module = dict(identity, size=files[role]['size'], sha256=files[role]['sha256'], path=game + 'garrysmod\\lua\\bin\\' + files[role]['name'], expectedPath=game + 'garrysmod\\lua\\bin\\' + files[role]['name'])
    runtime = dict(identity, size=files['runtime']['size'], sha256=files['runtime']['sha256'], path=game + 'bin\\win64\\' + files['runtime']['name'], expectedPath=game + 'bin\\win64\\' + files['runtime']['name'])
    g.PY_INFO = json.dumps(dict(module=module, runtime=runtime))
    g.PY_REPORT = json.dumps(dict(ready=True, pending=False, issues=[], libraries=[dict(name='engine.dll', sha256='0' * 64, match='tested')]))
    g.PY_CONFIGURE = (binary or Binary()).configure
    g.PY_COMPAT = json.dumps(compat)
    lua.execute(r'''
util={JSONToTable=function(s) return PY_DECODE(s) end,TableToJSON=function(t) return PY_ENCODE(t) end,AddNetworkString=function() end}
file={}
function file.Exists(p,s) if s=='DATA' then return PY_DATA[p]~=nil end return PY_DISK[s..'/'..p]~=nil end
function file.Open(p,m,s) local f=PY_DISK[s..'/'..p] return {Size=function() return f.size end,Read=function(_,n) return string.rep('x',n) end,Close=function() end} end
function file.Read(p,s) return PY_DATA[p] end
function file.Write(p,c) PY_DATA[p]=c end
function file.CreateDir() end
SENT={} PRINTED={}
net={Start=function(name) SENT[#SENT+1]={name=name} end,WriteString=function(v) SENT[#SENT].value=v end,Broadcast=function() end,Send=function() end,Receive=function() end,SendToServer=function() end}
hook={Add=function() end,Run=function() end}
timer={Create=function() end,Remove=function() end,Simple=function() end}
concommand={Add=function() end}
system={IsWindows=function() return true end} jit={arch='x64'}
game={IsDedicated=function() return true end,SinglePlayer=function() return false end}
CurTime=function() return 0 end RealTime=function() return 0 end MsgN=function(text) PRINTED[#PRINTED+1]=text end
istable=function(v) return type(v)=='table' end isstring=function(v) return type(v)=='string' end
function include(p) if p=='mmdhl/native_policy.lua' then return util.JSONToTable(PY_POLICY) elseif p=='mmdhl/compatibility_policy.lua' then return util.JSONToTable(PY_COMPAT) end return {} end
function require() mmdhl_native=PY_NATIVE or {GetInstallationInfo=function() return PY_INFO end,ConfigureCompatibility=function(text) return PY_CONFIGURE(text) end,
 CheckCompatibility=function() return PY_REPORT end,StartInstallationProbe=function() return false,'no worker in this test' end} end
''')
    if native: lua.execute(native)
    g.PY_POLICY = json.dumps(policy)
    source = (ROOT / 'addon/lua/mmdhl/installation.lua').read_text(encoding='utf8')
    hashed = "value={size=size,sha256=util.SHA256(bytes),path=search..'/'..path}"
    assert hashed in source
    lua.execute(source.replace(hashed, "value={size=size,sha256=PY_DISK[search..'/'..path].sha256,path=search..'/'..path}"))
    loaded_ok = g.mmdhl.CheckInstallation()
    return lua, g.mmdhl, g.mmdhl.GetInstallationStatus(), loaded_ok


codes = lambda s: [v.code for v in s.issues.values()]
# The release this addon recommends: no reminder.
_, M, s, ok = session(POLICY)
assert ok is True and s.update is None and codes(s) == ['worker_failed'] and M.NativeReleaseAtLeast(CURRENT) and not M.NativeReleaseAtLeast('9.9.9')
# An approved older release (a newer one is recommended): loads, no issue, the reminder.
for server in (False, True):
    lua, M, s, ok = session(newer_policy(), server=server)
    assert ok is True and s.features.core and M.native is not None and codes(s) == ([] if server else ['worker_failed']), codes(s)
    assert s.update.installed == CURRENT and s.update.recommended == '9.9.9' and s.update.approved and s.update.url.endswith('/tag/9.9.9')
    assert M.FeatureAvailable('physics' if server else 'rendering') is True
assert lua.eval('#PRINTED') >= 1 and any('older than the recommended 9.9.9' in lua.globals().PRINTED[i] for i in range(1, lua.eval('#PRINTED') + 1)), 'the server console did not say so'
status = json.loads([lua.globals().SENT[i].value for i in range(1, lua.eval('#SENT') + 1) if lua.globals().SENT[i].name == 'mmdhl_install_status'][-1])
assert status['update']['installed'] == CURRENT and status['update']['recommended'] == '9.9.9', 'the server status does not carry the update'
# An older release no longer approved: loads too, with one warning that disables nothing.
_, M, s, ok = session(newer_policy(approve_current=False))
assert ok is True and s.features.core and s.features.rendering and not s.unverified and not s.blocked and M.FeatureAvailable('rendering') is True
assert codes(s)[0] == 'outdated_release' and s.issues[1].warning and s.update.approved is None and M.loadError is None
# The update reminder's order: the release the files match, by build time, then by label.
_, M, s, _ = session(newer_policy())
for label, expected in ((CURRENT, True), ('2.1.0-native.12', True), ('2.1.0-native.5+d4a199ab', True), ('9.9.9', False), ('2.3.0', False), ('2.2.0-rc.1', True), ('2.2.1', False), (7, False)):
    assert M.NativeReleaseAtLeast(label) is expected, label
_, M, s, _ = session(newer_policy(), release='2.1.0-native.12')
assert s.update.installed == '2.1.0-native.12' and M.NativeReleaseAtLeast('2.1.0-native.11') and not M.NativeReleaseAtLeast(CURRENT)
# Accepted unverified files (a custom build) are compared by the loaded module's own label and build.
custom = {k: dict(f) for k, f in POLICY['releases'][CURRENT]['files'].items()}
custom['client']['sha256'] = 'c' * 64; custom['runtime']['sha256'] = 'd' * 64
_, M, first, ok = session(POLICY, disk=custom)
assert ok is False and first.unverified and not M.NativeReleaseAtLeast('1.0.0')
_, M, s, ok = session(POLICY, disk=custom, accepted=json.dumps({'client': first.fingerprint}), loaded={'release': '2.3.0', 'build': 'feedfacecafe-20261009T000000Z'})
assert ok is True and s.installed is None and M.NativeReleaseAtLeast('2.3.0') and M.NativeReleaseAtLeast(CURRENT) and not M.NativeReleaseAtLeast('2.4.0')
# Its own label counts even when the published build of that release is later (a local build of it).
published = deepcopy(POLICY); published['releases']['2.3.0'] = dict(deepcopy(POLICY['releases'][CURRENT]), release='2.3.0', build='0123456789ab-20991231T000000Z')
_, M, s, ok = session(published, disk=custom, accepted=json.dumps({'client': first.fingerprint}), loaded={'release': '2.3.0', 'build': 'feedfacecafe-20261009T000000Z'})
assert ok is True and M.NativeReleaseAtLeast('2.3.0') and M.NativeReleaseAtLeast('2.3.0+local') and not M.NativeReleaseAtLeast('2.3.1')
print('PASS: older approved and no longer approved releases load (client and server) with the update reminder; release order for NativeReleaseAtLeast')

# A compatibility policy newer than the binary: the policy without the library it does not
# know, the same on every later map of that process, with a warning that disables nothing.
newer = deepcopy(COMPAT)
extra = deepcopy(newer['libraries'][0]); extra['name'] = 'datacache.dll'
newer['libraries'][1:1] = [extra, dict(extra, variant='other')]
binary = Binary()
for _ in range(2):
    _, M, s, ok = session(newer_policy(), compat=newer, binary=binary)
    assert ok is True and s.features.core and s.features.rendering and M.native is not None
    assert [l['name'] for l in binary.configured['libraries']] == [l['name'] for l in COMPAT['libraries']], 'the binary did not get the policy without the unknown library'
    warning = [v for v in s.issues.values() if v.code == 'compatibility_fallback']
    assert len(warning) == 1 and warning[0].warning and 'datacache.dll' in M.Localize(warning[0].message) and 'engine.dll' not in M.Localize(warning[0].message)
# A rejected guard in every library: no profile at all; the binary checks every library itself.
binary = Binary(known=set())
_, M, s, ok = session(POLICY, compat=newer, binary=binary)
assert ok is True and binary.configured == {'schema': 1, 'family': 'source-win64-v1', 'libraries': []} and 'vphysics.dll' in M.Localize([v for v in s.issues.values() if v.code == 'compatibility_fallback'][0].message)
# The shipped policy configures as it is, without a warning.
binary = Binary()
_, M, s, ok = session(POLICY, binary=binary)
assert ok is True and binary.calls == 1 and binary.configured == COMPAT and 'compatibility_fallback' not in codes(s)
# Another ABI family is never forced on the binary: it stays off as before.
_, M, s, ok = session(POLICY, compat=dict(COMPAT, family='source-win64-v2'))
assert ok is False and codes(s)[-1] == 'policy_invalid' and not s.features.core
print('PASS: a newer compatibility policy configures without what the binary rejects, every map alike, with a warning')

# Modules this Lua cannot drive stay off, worded as a required update with the download.
_, M, s, ok = session(newer_policy(), native="PY_NATIVE={GetCapabilities=function() return '{\"version\":\"2.0.0-preview\"}' end}")
assert ok is False and codes(s) == ['outdated'] and s.update.required and s.update.installed == '2.0.0-preview' and s.update.recommended == '9.9.9' and s.update.url.endswith('/tag/9.9.9')
_, M, s, ok = session(POLICY, native='PY_NATIVE={}')
assert ok is False and s.update.required and s.update.installed is None and s.update.recommended == CURRENT
# A loaded module with another interface: older than the recommended release, a required update.
_, M, s, ok = session(newer_policy(), loaded={'installApi': 0})
assert ok is False and codes(s) == ['restart_required'] and s.update.required and s.update.installed == CURRENT and s.update.recommended == '9.9.9'
# A newer one needs a newer addon, and the recommended release itself (any build of it) a restart
# or repair: no download of the same or an older package, the problem says what to do.
for loaded in ({'api': 2, 'release': '3.0.0'}, {'api': 2}, {'installApi': 2, 'build': '0123456789ab-20000101T000000Z'}):
    _, M, s, ok = session(POLICY, loaded=loaded)
    assert ok is False and codes(s) == ['restart_required'] and s.update is None and not s.features.core, loaded
print('PASS: modules without verification or with an older interface stay off as a required update; newer ones as a problem')

# publish-native-release.py: with a newer recommended release appended, every approved
# release still loads on client and server without issues.
spec = importlib.util.spec_from_file_location('publish_native_release', ROOT / 'scripts/publish-native-release.py')
publish = importlib.util.module_from_spec(spec); spec.loader.exec_module(publish)
record = deepcopy(POLICY['releases'][CURRENT]); record.pop('url'); record.pop('altUrl', None)
record.update(release='9.9.9', build='0123456789ab-20991231T000000Z')
for f in record['files'].values(): f['sha256'] = '9' * 64
updated, added = publish.add_release(POLICY, record, 'https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/releases/tag/9.9.9')
assert added and updated['recommended'] == '9.9.9' and CURRENT in updated['approved']
publish.evaluate(updated, '9.9.9')
print('PASS: publish-native-release.py evaluate(): approved releases older than a new recommended release load without issues')
