"""Game builds without a matching profile: installation.lua runs against a simulated
game and a native module whose compatibility report marks libraries "unverified"
(native releases from 2.1.0-native.6 report them instead of refusing). They keep
running: rendering and physics stay on, with one warning that disables nothing and
that Dismiss hides. A module from before these checks (no CheckCompatibility) relies on
its own guards: rendering and physics stay on with one game_unchecked warning, also after
the map's InitPostEntity refresh. A warning (an unverified build, a runtime loaded from
elsewhere) is never given as the reason a feature is off: only a real failure is."""
from pathlib import Path
import json, sys
from lupa import LuaRuntime
from lua_i18n import attach

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from compatibility_profiles import lua_policy

policy = lua_policy(ROOT / 'addon/lua/mmdhl/native_policy.lua')
release = policy['releases'][policy['recommended']]


def session(server, libraries, loaded_path=None, ready=True, issues=(), check_compatibility=True):
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
    disk = {'MOD/lua/bin/' + f['name']: f for f in release['files'].values()}
    disk['BASE_PATH/bin/win64/' + release['files']['runtime']['name']] = release['files']['runtime']
    g.PY_DISK = lua.table_from({k: lua.table_from({'size': v['size'], 'sha256': v['sha256']}) for k, v in disk.items()})
    game = 'C:\\game\\'
    role = 'server' if server else 'client'
    module = dict(release=release['release'], build=release['build'], installApi=1, api=1, platform='win64', size=release['files'][role]['size'], sha256=release['files'][role]['sha256'],
                  path=game + 'garrysmod\\lua\\bin\\' + release['files'][role]['name'], expectedPath=game + 'garrysmod\\lua\\bin\\' + release['files'][role]['name'])
    runtime = dict(module, size=release['files']['runtime']['size'], sha256=release['files']['runtime']['sha256'],
                   path=loaded_path or game + 'bin\\win64\\mmdhl_runtime_win64.dll', expectedPath=game + 'bin\\win64\\mmdhl_runtime_win64.dll')
    g.PY_INFO = json.dumps(dict(module=module, runtime=runtime))
    g.PY_REPORT = json.dumps(dict(ready=ready, pending=False, issues=list(issues), libraries=[dict(name=n, sha256='0' * 64, match=m) for n, m in libraries]))
    lua.execute(r'''
util={JSONToTable=function(s) return PY_DECODE(s) end,TableToJSON=function(t) return PY_ENCODE(t) end,AddNetworkString=function() end}
file={}
function file.Exists(p,s) if s=='DATA' then return false end return PY_DISK[s..'/'..p]~=nil end
function file.Open(p,m,s) local f=PY_DISK[s..'/'..p] return {Size=function() return f.size end,Read=function(_,n) return string.rep('x',n) end,Close=function() end} end
function file.Read() return nil end
function file.Write() end
function file.CreateDir() end
net={Start=function() end,WriteString=function() end,Broadcast=function() end,Receive=function() end,SendToServer=function() end}
HOOKS={} hook={Add=function(event,name,f) HOOKS[event]=HOOKS[event] or {} HOOKS[event][name]=f end,Run=function() end}
timer={Create=function() end,Remove=function() end,Simple=function() end}
concommand={Add=function() end}
system={IsWindows=function() return true end} jit={arch='x64'}
game={IsDedicated=function() return false end,SinglePlayer=function() return true end}
CurTime=function() return 0 end RealTime=function() return 0 end MsgN=function() end
istable=function(v) return type(v)=='table' end isstring=function(v) return type(v)=='string' end
function include(p) if p=='mmdhl/native_policy.lua' then return util.JSONToTable(PY_POLICY) end return {} end
function require() mmdhl_native={GetInstallationInfo=function() return PY_INFO end,ConfigureCompatibility=function() return '{"configured":true}' end,
 CheckCompatibility=PY_CHECKS and function() return PY_REPORT end or nil,GetCapabilities=function() return '{"version":"2.1.0-native.5"}' end,
 StartInstallationProbe=function() return false,'no worker in this test' end} end
''')
    g.PY_POLICY = json.dumps(policy)
    g.PY_CHECKS = check_compatibility
    source = (ROOT / 'addon/lua/mmdhl/installation.lua').read_text(encoding='utf8')
    hashed = "value={size=size,sha256=util.SHA256(bytes),path=search..'/'..path}"
    assert hashed in source
    lua.execute(source.replace(hashed, "value={size=size,sha256=PY_DISK[search..'/'..path].sha256,path=search..'/'..path}"))
    g.mmdhl.CheckInstallation()
    return lua, g.mmdhl, g.mmdhl.GetInstallationStatus()


client = ['engine.dll', 'client.dll', 'materialsystem.dll', 'shaderapidx9.dll', 'stdshader_dx9.dll']
warnings = lambda s: [v for v in s.issues.values() if v.code == 'game_unverified']
# Audited builds: rendering on, no warning.
_, M, s = session(False, [(n, 'tested') for n in client])
assert s.features.core and s.features.rendering and not warnings(s)
# An unverified library: rendering stays on, with one warning naming it.
_, M, s = session(False, [(n, 'unverified' if n == 'materialsystem.dll' else 'abi-evidence') for n in client])
w = warnings(s)
assert s.features.core and s.features.rendering and len(w) == 1 and w[0].warning and not s.blocked and not s.unverified
assert 'materialsystem.dll' in M.Localize(w[0].message) and 'engine.dll' not in M.Localize(w[0].message)
assert M.FeatureAvailable('rendering') is True
# The server: an unverified vphysics.dll keeps physics (spawning) on.
_, M, s = session(True, [('vphysics.dll', 'unverified')])
assert s.features.core and s.features.physics and len(warnings(s)) == 1 and M.FeatureAvailable('physics') is True
# A real failure (a missing interface) still names itself, never the warning beside it.
problem = dict(code='game_incompatible', component='client.dll', message='Required game interface unavailable: VClientEntityList003')
_, M, s = session(False, [(n, 'unverified') for n in client], ready=False, issues=[problem])
ok, why = M.FeatureAvailable('rendering')
assert not s.features.rendering and not ok and why == problem['message'], why
# Nor is a loaded runtime that is not the checked file (loaded from elsewhere): a warning beside it.
elsewhere = r'C:\game\mmdhl_runtime_win64.dll'
_, M, s = session(False, [(n, 'tested') for n in client], loaded_path=elsewhere)
mismatch = [v for v in s.issues.values() if v.code == 'loaded_mismatch']
assert s.features.core and s.features.rendering and len(mismatch) == 1 and mismatch[0].warning and M.FeatureAvailable('rendering') is True
_, M, s = session(False, [(n, 'tested') for n in client], loaded_path=elsewhere, ready=False, issues=[problem])
assert any(v.code == 'loaded_mismatch' and v.warning for v in s.issues.values()) and s.features.core
ok, why = M.FeatureAvailable('rendering')
assert not ok and why == problem['message'], why
print('PASS: unverified game builds keep running with one dismissible warning; real failures, not warnings, are the reason a feature is off')

# A module from before game build checks (no CheckCompatibility): its own guards decide; rendering
# (physics on the server) stays on with one game_unchecked warning, also after InitPostEntity.
for server, feature in ((False, 'rendering'), (True, 'physics')):
    lua, M, s = session(server, [], check_compatibility=False)
    unchecked = [v for v in s.issues.values() if v.code == 'game_unchecked']
    assert s.features.core and s.features[feature] and M.FeatureAvailable(feature) is True and M.native is not None, [v.code for v in s.issues.values()]
    assert len(unchecked) == 1 and unchecked[0].warning and unchecked[0].feature == feature and unchecked[0].component == 'game' and not s.blocked
    assert M.Localize(unchecked[0].message) == M.Localize(M.L('install.warning.game_unchecked', lua.table_from({'recommended': policy['recommended']})))
    # It also lacks installation verification: an outdated warning, the update it needs.
    assert [v.code for v in s.issues.values() if v.code == 'outdated'] == ['outdated'] and s.update.required
    lua.eval('HOOKS.InitPostEntity')['MMDHL.InstallationCompatibility']()
    assert [v.code for v in s.issues.values()].count('game_unchecked') == 1 and s.features[feature], 'the map refresh repeated or dropped the warning'
print('PASS: a module without game build checks keeps rendering and physics on with one game_unchecked warning')
