"""A native module never stops the addon: installation.lua loads it against a simulated
game while the policy recommends a newer release. An approved older release loads without
issues and gets the update reminder (status.update); one no longer approved loads with a
warning that disables nothing. A build this addon does not know (other hashes, one build ID,
on client and server) loads with warnings only, every feature on, and its own label decides
NativeReleaseAtLeast and the ordinary (skippable) reminder. A compatibility policy newer than
the binary (a library it does not know) is offered without that library, then without any,
so the module still configures; a policy it rejects entirely (another ABI family, every
variant) is a warning, and CheckCompatibility's own guards decide. Modules this addon cannot
check run too: a release without verification (installApi 0) or a module without
GetInstallationInfo, ConfigureCompatibility or CheckCompatibility (an outdated warning, game
builds unchecked, a required update), one whose identity is unavailable, and one with
another interface (a warning; a required update only when it is older than the recommended
release). Also the server's console line and status, and publish-native-release.py's
evaluate() with a newer recommended release."""
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


class Refusing(Binary):
    """A binary that takes no variant of the policy at all."""
    def configure(self, text):
        self.calls += 1
        return None, 'Invalid compatibility guard'


def session(policy, release=CURRENT, server=False, loaded=None, compat=COMPAT, binary=None, native=None, disk=None, builds=None, probe=None):
    """Runs CheckInstallation with release's files on disk (or disk; builds: the build ID a file
    carries among its bytes) and a module reporting loaded (default: that release). probe: the
    worker self-test's answer (default: the worker does not start)."""
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
    g.PY_BUILDS = lua.table_from(builds or {})
    g.PY_PROBE = json.dumps(probe) if probe else None
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
function file.Exists(p,s) if s=='DATA' then return false end return PY_DISK[s..'/'..p]~=nil end
function file.Open(p,m,s) local f,id=PY_DISK[s..'/'..p],PY_BUILDS[s..'/'..p] return {Size=function() return f.size end,Read=function(_,n) if id then return id..string.rep('x',n-#id) end return string.rep('x',n) end,Close=function() end} end
function file.Read() return nil end
function file.Write() end
function file.CreateDir() end
SENT={} PRINTED={}
net={Start=function(name) SENT[#SENT+1]={name=name} end,WriteString=function(v) SENT[#SENT].value=v end,Broadcast=function() end,Send=function() end,Receive=function() end,SendToServer=function() end}
hook={Add=function() end,Run=function() end}
TIMERS={} timer={Create=function(name,_,_,f) TIMERS[name]=f end,Remove=function(name) TIMERS[name]=nil end,Simple=function() end}
concommand={Add=function() end}
system={IsWindows=function() return true end} jit={arch='x64'}
game={IsDedicated=function() return true end,SinglePlayer=function() return false end}
CurTime=function() return 0 end RealTime=function() return 0 end MsgN=function(text) PRINTED[#PRINTED+1]=text end
istable=function(v) return type(v)=='table' end isstring=function(v) return type(v)=='string' end
function include(p) if p=='mmdhl/native_policy.lua' then return util.JSONToTable(PY_POLICY) elseif p=='mmdhl/compatibility_policy.lua' then return util.JSONToTable(PY_COMPAT) end return {} end
CHECKS=0
function require() mmdhl_native=PY_NATIVE or {GetInstallationInfo=function() return PY_INFO end,ConfigureCompatibility=function(text) return PY_CONFIGURE(text) end,
 CheckCompatibility=function() CHECKS=CHECKS+1 return PY_REPORT end,StartInstallationProbe=function() if PY_PROBE then return true end return false,'no worker in this test' end,
 PollInstallationProbe=function() return PY_PROBE end} end
''')
    if native: lua.execute(native)
    g.PY_POLICY = json.dumps(policy)
    source = (ROOT / 'addon/lua/mmdhl/installation.lua').read_text(encoding='utf8')
    hashed = "value={size=size,sha256=util.SHA256(bytes),path=search..'/'..path}"
    assert hashed in source
    lua.execute(source.replace(hashed, "value={size=size,sha256=PY_DISK[search..'/'..path].sha256,path=search..'/'..path}"))
    loaded_ok = g.mmdhl.CheckInstallation()
    if probe and g.TIMERS['MMDHL.InstallationWorker']: lua.execute("TIMERS['MMDHL.InstallationWorker']()")
    return lua, g.mmdhl, g.mmdhl.GetInstallationStatus(), loaded_ok


codes = lambda s: [s.issues[i].code for i in range(1, len(s.issues) + 1)]
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
print('PASS: older approved and no longer approved releases load (client and server) with the update reminder; release order for NativeReleaseAtLeast')

# A build this addon does not know (a fresh GitHub Actions build of a newer release whose hashes
# native_policy.lua does not list yet), consistent: every file with other hashes, one build ID in
# the module and the runtimes. It runs with warnings only, every feature on, and is compared by
# the loaded module's own label and build.
FRESH = 'feedfacecafe-20261009T000000Z'
custom = {key: dict(f, sha256=c * 64) for (key, f), c in zip(POLICY['releases'][CURRENT]['files'].items(), 'abcde')}
def carried(build, server=False):
    """The build ID in the realm module and both runtime copies."""
    return {'MOD/lua/bin/' + custom['server' if server else 'client']['name']: build, 'BASE_PATH/bin/win64/' + custom['runtime']['name']: build, 'MOD/lua/bin/' + custom['runtime']['name']: build}
worker = {'identity': {'release': '2.3.0', 'build': FRESH}, 'runtime': {'build': FRESH, 'sha256': custom['runtime']['sha256']}, 'coacd': True}
lua, M, s, ok = session(POLICY, disk=custom, builds=carried(FRESH), loaded={'release': '2.3.0', 'build': FRESH}, probe=worker)
assert ok is True and M.native is not None and M.loadError is None and not s.blocked and not s.probePending, codes(s)
assert s.features.core and s.features.imports and s.features.detailedCollision and s.features.rendering
assert all(M.FeatureAvailable(f) is True for f in ('core', 'imports', 'detailedCollision', 'rendering'))
assert s.installed is None and s.unverified and s.update is None and s.loaded.module.release == '2.3.0'
assert codes(s) == ['damaged_or_unrecognized'] * 5 and all(s.issues[i].warning and s.issues[i].identity for i in range(1, 6)), codes(s)
assert lua.globals().CHECKS == 1, 'CheckCompatibility did not decide the game build'
# No loaded_mismatch (the loaded bytes are the files on disk), no mixed builds, its own label.
assert M.NativeReleaseAtLeast('2.3.0') and M.NativeReleaseAtLeast(CURRENT) and not M.NativeReleaseAtLeast('2.4.0')
# The server realm alike: physics on, its own label.
lua, M, s, ok = session(POLICY, server=True, disk=custom, builds=carried(FRESH, True), loaded={'release': '2.3.0', 'build': FRESH})
assert ok is True and M.native is not None and s.features.core and s.features.physics and codes(s) == ['damaged_or_unrecognized'] * 2 and s.unverified and s.installed is None
assert M.FeatureAvailable('physics') is True and M.NativeReleaseAtLeast('2.3.0') and not M.NativeReleaseAtLeast('2.4.0') and lua.globals().CHECKS == 1
# An older build this addon does not know: the ordinary, skippable reminder (never required).
_, M, s, ok = session(POLICY, disk=custom, loaded={'release': '2.1.0-native.12', 'build': '0123456789ab-20260928T200000Z'})
assert ok is True and s.features.core and s.update.installed == '2.1.0-native.12' and s.update.recommended == CURRENT and s.update.required is None
assert s.update.url == POLICY['releases'][CURRENT]['url'] and s.update.approved is None and 'loaded_mismatch' not in codes(s)
assert M.NativeReleaseAtLeast('2.1.0-native.12') and not M.NativeReleaseAtLeast(CURRENT)
# Its own label counts even when the published build of that release is later (a local build of it).
published = deepcopy(POLICY); published['releases']['2.3.0'] = dict(deepcopy(POLICY['releases'][CURRENT]), release='2.3.0', build='0123456789ab-20991231T000000Z')
_, M, s, ok = session(published, disk=custom, loaded={'release': '2.3.0', 'build': 'feedfacecafe-20261009T000000Z'})
assert ok is True and s.update is None and M.NativeReleaseAtLeast('2.3.0') and M.NativeReleaseAtLeast('2.3.0+local') and not M.NativeReleaseAtLeast('2.3.1')
print('PASS: a build this addon does not know loads on client and server with every feature and warnings only; its own label decides NativeReleaseAtLeast and the ordinary reminder')

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
# A policy the binary takes in no variant (another ABI family, or every variant refused): a warning;
# the module still runs and CheckCompatibility, with the binary's own guards, decides the game build.
libraries = len({l['name'] for l in COMPAT['libraries']})
for compat, binary, why in ((dict(COMPAT, family='source-win64-v2'), Binary(), 'Unsupported compatibility profile schema or ABI family'), (COMPAT, Refusing(), 'Invalid compatibility guard')):
    lua, M, s, ok = session(POLICY, compat=compat, binary=binary)
    invalid = [v for v in s.issues.values() if v.code == 'policy_invalid']
    assert ok is True and M.native is not None and s.features.core and s.features.rendering and M.FeatureAvailable('rendering') is True and M.loadError is None, codes(s)
    assert len(invalid) == 1 and invalid[0].warning and invalid[0].component == 'compatibility' and invalid[0].message == why and codes(s) == ['policy_invalid', 'worker_failed'], codes(s)
    assert binary.calls == libraries + 2 and binary.configured is None and lua.globals().CHECKS == 1, (binary.calls, lua.globals().CHECKS)
print('PASS: a newer compatibility policy configures without what the binary rejects, every map alike, with a warning; one it takes in no variant is a warning too')

# Modules this addon cannot check run too. Without installation verification (no
# GetInstallationInfo, ConfigureCompatibility or CheckCompatibility): one outdated warning, the game
# build unchecked (its own guards), imports unchecked, and the update it needs, worded as required.
LEGACY = "PY_NATIVE={GetCapabilities=function() return '{\"version\":\"2.0.0-preview\"}' end}"
for server in (False, True):
    # A release the policy records without verification (installApi 0), as old installations have.
    lua, M, s, ok = session(POLICY, release='legacy-actors-preview', server=server, native=LEGACY)
    feature = 'physics' if server else 'rendering'
    assert ok is True and M.native is not None and s.features.core and s.features[feature] and M.loadError is None and not s.blocked, codes(s)
    assert codes(s) == ['outdated', 'game_unchecked'] and s.issues[1].warning and s.issues[2].warning and s.issues[2].feature == feature, codes(s)
    assert M.Localize(s.issues[1].message) == M.Localize(M.L('install.error.no_verification', lua.table_from({'recommended': CURRENT})))
    assert M.Localize(s.issues[2].message) == M.Localize(M.L('install.warning.game_unchecked', lua.table_from({'recommended': CURRENT})))
    assert s.installed == 'legacy-actors-preview' and s.update.required and s.update.installed == '2.0.0-preview' and s.update.recommended == CURRENT and s.update.url == POLICY['releases'][CURRENT]['url']
    assert M.FeatureAvailable(feature) is True and (server or (s.features.imports and M.FeatureAvailable('imports') is True))
_, M, s, ok = session(newer_policy(), native=LEGACY)
assert ok is True and M.native is not None and s.features.core and s.features.rendering and s.features.imports, codes(s)
assert codes(s) == ['outdated', 'game_unchecked'] and s.update.required and s.update.installed == '2.0.0-preview' and s.update.recommended == '9.9.9' and s.update.url.endswith('/tag/9.9.9')
_, M, s, ok = session(POLICY, native='PY_NATIVE={}')
assert ok is True and M.native is not None and s.features.core and codes(s) == ['outdated', 'game_unchecked'] and s.update.required and s.update.installed is None and s.update.recommended == CURRENT
# A module that cannot say what it is (GetInstallationInfo answers nil, err): a warning, it runs.
_, M, s, ok = session(POLICY, native="PY_NATIVE={GetInstallationInfo=function() return nil,'Installation identity unavailable' end,ConfigureCompatibility=function(text) return PY_CONFIGURE(text) end,CheckCompatibility=function() CHECKS=CHECKS+1 return PY_REPORT end}")
assert ok is True and s.features.core and s.features.rendering and s.features.imports and codes(s) == ['loaded_mismatch'] and s.issues[1].warning and s.issues[1].detail == 'unknown' and s.update is None and s.loaded is None
# A loaded module with another interface: older than the recommended release, the update it needs.
_, M, s, ok = session(newer_policy(), loaded={'installApi': 0})
assert ok is True and s.features.core and M.native is not None and codes(s) == ['loaded_mismatch', 'worker_failed'] and s.issues[1].warning
assert s.update.required and s.update.installed == CURRENT and s.update.recommended == '9.9.9' and M.FeatureAvailable('rendering') is True
# Newer (it may need a newer addon), or the recommended release itself (any build of it): no
# download of the same or an older package, only the warning.
for loaded in ({'api': 2, 'release': '3.0.0'}, {'api': 2}, {'installApi': 2, 'build': '0123456789ab-20000101T000000Z'}):
    _, M, s, ok = session(POLICY, loaded=loaded)
    assert ok is True and codes(s) == ['loaded_mismatch', 'worker_failed'] and s.issues[1].warning and s.update is None and s.features.core and s.features.rendering and M.loadError is None, loaded
print('PASS: modules without verification, without an identity or with another interface run with a warning; older ones get the update they need')

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
