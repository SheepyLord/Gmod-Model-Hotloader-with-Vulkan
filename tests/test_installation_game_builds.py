"""Game builds without a matching profile: installation.lua runs against a simulated
game and a native module whose compatibility report marks libraries "unverified"
(2.1.0-native.6 reports them instead of refusing). Rendering and physics stay off
for them, since the default branch's 64-bit build of 2026-09-17 shifted the
IMaterialSystem/IPhysics slots and the model preview hung; the reason a feature is
off is never a warning the player accepted."""
from pathlib import Path
import json, sys
from lupa import LuaRuntime
from lua_i18n import attach

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from compatibility_profiles import lua_policy

policy = lua_policy(ROOT / 'addon/lua/mmdhl/native_policy.lua')
release = policy['releases'][policy['recommended']]


def session(server, libraries, accepted=None, loaded_path=None):
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
    g.PY_DATA = lua.table_from({'mmd_hotloader/unverified_native.json': accepted} if accepted else {})
    game = 'C:\\game\\'
    role = 'server' if server else 'client'
    module = dict(release=release['release'], build=release['build'], installApi=1, api=1, platform='win64', size=release['files'][role]['size'], sha256=release['files'][role]['sha256'],
                  path=game + 'garrysmod\\lua\\bin\\' + release['files'][role]['name'], expectedPath=game + 'garrysmod\\lua\\bin\\' + release['files'][role]['name'])
    runtime = dict(module, size=release['files']['runtime']['size'], sha256=release['files']['runtime']['sha256'],
                   path=loaded_path or game + 'bin\\win64\\mmdhl_runtime_win64.dll', expectedPath=game + 'bin\\win64\\mmdhl_runtime_win64.dll')
    g.PY_INFO = json.dumps(dict(module=module, runtime=runtime))
    g.PY_REPORT = json.dumps(dict(ready=True, pending=False, issues=[], libraries=[dict(name=n, sha256='0' * 64, match=m) for n, m in libraries]))
    lua.execute(r'''
util={JSONToTable=function(s) return PY_DECODE(s) end,TableToJSON=function(t) return PY_ENCODE(t) end,AddNetworkString=function() end}
file={}
function file.Exists(p,s) if s=='DATA' then return PY_DATA[p]~=nil end return PY_DISK[s..'/'..p]~=nil end
function file.Open(p,m,s) local f=PY_DISK[s..'/'..p] return {Size=function() return f.size end,Read=function(_,n) return string.rep('x',n) end,Close=function() end} end
function file.Read(p,s) return PY_DATA[p] end
function file.Write(p,c) PY_DATA[p]=c end
function file.CreateDir() end
net={Start=function() end,WriteString=function() end,Broadcast=function() end,Receive=function() end,SendToServer=function() end}
hook={Add=function() end,Run=function() end}
timer={Create=function() end,Remove=function() end,Simple=function() end}
concommand={Add=function() end}
system={IsWindows=function() return true end} jit={arch='x64'}
game={IsDedicated=function() return false end,SinglePlayer=function() return true end}
CurTime=function() return 0 end RealTime=function() return 0 end MsgN=function() end
istable=function(v) return type(v)=='table' end
function include(p) if p=='mmdhl/native_policy.lua' then return util.JSONToTable(PY_POLICY) end return {} end
function require() mmdhl_native={GetInstallationInfo=function() return PY_INFO end,ConfigureCompatibility=function() return '{"configured":true}' end,
 CheckCompatibility=function() return PY_REPORT end,StartInstallationProbe=function() return false,'no worker in this test' end} end
''')
    g.PY_POLICY = json.dumps(policy)
    source = (ROOT / 'addon/lua/mmdhl/installation.lua').read_text(encoding='utf8')
    hashed = "value={size=size,sha256=util.SHA256(bytes),path=search..'/'..path}"
    assert hashed in source
    lua.execute(source.replace(hashed, "value={size=size,sha256=PY_DISK[search..'/'..path].sha256,path=search..'/'..path}"))
    g.mmdhl.CheckInstallation()
    return lua, g.mmdhl, g.mmdhl.GetInstallationStatus()


client = ['engine.dll', 'client.dll', 'materialsystem.dll', 'shaderapidx9.dll', 'stdshader_dx9.dll']
# Audited builds: rendering on, no game issue.
_, M, s = session(False, [(n, 'tested') for n in client])
assert s.features.core and s.features.rendering and not any(v.code == 'game_incompatible' for v in s.issues.values())
# One unverified library: rendering off with one message naming it; the core and imports stay on.
_, M, s = session(False, [(n, 'unverified' if n == 'materialsystem.dll' else 'abi-evidence') for n in client])
issues = [v for v in s.issues.values() if v.code == 'game_incompatible']
assert s.features.core and not s.features.rendering and len(issues) == 1 and issues[0].feature == 'rendering'
assert 'materialsystem.dll' in M.Localize(issues[0].message) and 'engine.dll' not in M.Localize(issues[0].message)
ok, why = M.FeatureAvailable('rendering')
assert not ok and why == issues[0].message
# The server: an unverified vphysics.dll keeps physics (spawning) off.
_, M, s = session(True, [('vphysics.dll', 'unverified')])
assert s.features.core and not s.features.physics and any(v.code == 'game_incompatible' and v.feature == 'physics' for v in s.issues.values())
# A warning the player accepted (a runtime loaded elsewhere) is never given as the reason.
_, M, first = session(False, [(n, 'unverified') for n in client], loaded_path='C:\\game\\mmdhl_runtime_win64.dll')
fingerprint = first.fingerprint
_, M, s = session(False, [(n, 'unverified') for n in client], accepted=json.dumps({'client': fingerprint}), loaded_path='C:\\game\\mmdhl_runtime_win64.dll')
assert s.features.core and any(v.code == 'loaded_mismatch' and v.accepted for v in s.issues.values())
ok, why = M.FeatureAvailable('rendering')
assert not ok and 'engine.dll' in M.Localize(why), M.Localize(why)
print('PASS: unverified game builds keep rendering and physics off with one message; audited builds render; accepted warnings are never the reason')
