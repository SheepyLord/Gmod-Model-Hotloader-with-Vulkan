"""Workshop model packages: validation, installation through the shared-file
transfer, Workshop origins, deleting/restoring, demo packs, unsubscribing and
server approvals, running the addon's workshop.lua against a simulated game."""
from pathlib import Path
import hashlib, json, re
from lupa import LuaRuntime
from lua_i18n import attach

ROOT = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)


def to_lua(value):
    if isinstance(value, dict): return lua.table_from({k: to_lua(v) for k, v in value.items()})
    if isinstance(value, list): return lua.table_from([to_lua(v) for v in value])
    return value


def to_py(value):
    if lupa_table(value):
        keys = list(value.keys())
        if keys and all(isinstance(k, int) for k in keys) and sorted(keys) == list(range(1, len(keys) + 1)):
            return [to_py(value[k]) for k in sorted(keys)]
        if not keys: return {}
        return {str(k): to_py(v) for k, v in value.items()}
    return value


def lupa_table(value): return type(value).__name__ == '_LuaTable'


def decode(text):
    try: return to_lua(json.loads(text))
    except Exception: return None


lua.globals().py_decode = decode
lua.globals().py_encode = lambda v: json.dumps(to_py(v))
lua.execute(r'''
mmdhl={library={entries={}},props={library={entries={}}}}
isstring=function(v) return type(v)=='string' end
istable=function(v) return type(v)=='table' end
isnumber=function(v) return type(v)=='number' end
isfunction=function(v) return type(v)=='function' end
IsValid=function(v) return v~=nil end
string.Trim=function(s) return (s:gsub('^%s+',''):gsub('%s+$','')) end
string.GetPathFromFilename=function(p) return p:match('^(.*[/\\])') or '' end
table.HasValue=function(t,v) for _,x in pairs(t) do if x==v then return true end end return false end
table.Count=function(t) local n=0 for _ in pairs(t) do n=n+1 end return n end
table.GetKeys=function(t) local k={} for key in pairs(t) do k[#k+1]=key end return k end
math.Clamp=function(v,a,b) return math.min(math.max(v,a),b) end
util={JSONToTable=function(s) if type(s)~='string' then return nil end return py_decode(s) end,TableToJSON=function(t) return py_encode(t) end}
SysTime=os.clock CurTime=os.clock
ERRORS={} ErrorNoHalt=function(m) ERRORS[#ERRORS+1]=m end
NOTICES={} notification={AddProgress=function() end,Kill=function() end,AddLegacy=function(m) NOTICES[#NOTICES+1]=m end}
NOTIFY_ERROR=1 NOTIFY_GENERIC=0
HOOKS={} hook={Add=function(event,name,fn) HOOKS[event]=HOOKS[event] or {} HOOKS[event][name]=fn end,Run=function(event,...) for _,fn in pairs(HOOKS[event] or {}) do fn(...) end end,Remove=function(event,name) if HOOKS[event] then HOOKS[event][name]=nil end end}
timer={Simple=function(_,fn) fn() end,Create=function(_,_,_,fn) fn() end,Remove=function() end}
game={SinglePlayer=function() return SINGLE end,IsDedicated=function() return DEDICATED end}
SINGLE=true DEDICATED=false CLIENT=true SERVER=false
LocalPlayer=function() return {IsListenServerHost=function() return false end} end
net={Start=function() end,SendToServer=function() end,Receive=function() end}
ents={FindByClass=function() return {} end}
mmdhl.Entities=function() return {} end
mmdhl.GetAsset=function() return '' end
-- Simulated game filesystem: GAME search path, addon title paths, DATA. Files
-- the test writes directly keep time 1 unless MTIME gives them another.
FS={} ADDONS={} MTIME={} NOW=100
local function key(path,id) return id..'|'..path end
function TOUCH(k) NOW=NOW+1 MTIME[k]=NOW end
file={
 Read=function(path,id) return FS[key(path,id or 'DATA')] end,
 Write=function(path,value) FS[key(path,'DATA')]=value TOUCH(key(path,'DATA')) end,
 Exists=function(path,id) return FS[key(path,id or 'DATA')]~=nil end,
 Size=function(path,id) local v=FS[key(path,id or 'DATA')] return v and #v or -1 end,
 Time=function(path,id) local k=key(path,id or 'DATA') return FS[k] and (MTIME[k] or 1) or 0 end,
 CreateDir=function() end,
 Find=function(pattern,id)
  local prefix=pattern:gsub('%*%.json$','') local out={}
  for k in pairs(FS) do local pid,path=k:match('^(.-)|(.*)$') if pid==id and path:sub(1,#prefix)==prefix and path:sub(-5)=='.json' and not path:sub(#prefix+1):find('/') then out[#out+1]=path:sub(#prefix+1) end end
  table.sort(out) return out,{}
 end,
 Open=function(path,mode,id)
  local data=FS[key(path,id)] if not data then return nil end
  local at=0
  return {Size=function() return #data end,Read=function(_,n) local s=data:sub(at+1,at+n) at=at+#s return s end,Close=function() end}
 end,
}
engine={GetAddons=function() return ADDONS end}
-- The shared-file transfer, as the native module validates it.
CALLS={} local handles,nextHandle={},1
native={
 -- Installed bytes are all 'x'; other bytes of the same size fail the hash. A
 -- hash costs HASH_COST seconds of the test clock when that is set.
 SharedFileMatches=function(path,size,sha) HASHED=(HASHED or 0)+1 if HASH_COST then CLOCK=CLOCK+HASH_COST end
  local v=FS[key('mmd_hotloader/'..path,'DATA')] return v~=nil and #v==size and not v:find('[^x]') end,
 BeginSharedFile=function(path,size,sha,packed) CALLS[#CALLS+1]={'begin',path,size,sha,packed} if FAIL_PATH==path then return nil,'refused' end local h=nextHandle nextHandle=h+1 handles[h]={path=path,size=size,packed=packed,got=''} return h end,
 AppendSharedChunk=function(h,offset,bytes) local t=handles[h] assert(offset==#t.got,'out of order') assert(#bytes<=48*1024,'chunk too large') t.got=t.got..bytes return true end,
 CommitSharedFile=function(h) local t=handles[h] assert(#t.got==t.packed,'incomplete') return true end,
 PollSharedCommit=function(h) local t=handles[h] local k=key('mmd_hotloader/'..t.path,'DATA') FS[k]=string.rep('x',t.size) TOUCH(k) handles[h]=nil return true end,
 CancelSharedFile=function(h) handles[h]=nil end,
 RequestAsset=function() return true end,AssetInfo=function() return {} end,
}
mmdhl.native=native
local function remove(prefix,ids) for _,id in ipairs(ids) do for k in pairs(FS) do if k:find(id,1,true) and k:sub(1,5)=='DATA|' then FS[k]=nil end end end end
DELETED={}
mmdhl.library.Refresh=function() REFRESHED=(REFRESHED or 0)+1 end
mmdhl.library.Delete=function(ids,done) for _,id in ipairs(ids) do DELETED[#DELETED+1]=id end remove('assets',ids) done(true,'Deleted.') end
mmdhl.props.library.Refresh=function() end
mmdhl.props.library.Delete=function(ids,done) for _,id in ipairs(ids) do DELETED[#DELETED+1]=id end remove('static',ids) done(true,'Deleted.') end
''')
attach(lua)
# The shared arms-choice cleaner lives in materials.lua.
materials = (ROOT / 'addon/lua/mmdhl/materials.lua').read_text(encoding='utf-8')
lua.execute(materials[materials.index('function mmdhl.CleanArmsParts('):materials.index('function mmdhl.ApplyBodygroupState(')])
lua.execute((ROOT / 'addon/lua/mmdhl/workshop.lua').read_text(encoding='utf-8'))
W = lua.eval('mmdhl.workshop')
G = lua.globals()


def sha(b): return hashlib.sha256(b).hexdigest()


def package(pid, title, install, items, files):
    return {'format': 'mmdhl-package', 'version': 1, 'id': pid, 'title': title, 'author': 'Tester', 'install': install, 'created': 1790000000,
            'items': items, 'files': files}


# Payloads: a character (manifest, model, one texture) and a static prop.
character, prop = 'c' * 64, None
texture = b'png-bytes' * 30000
texture_sha = sha(texture)
prop_bytes = b'gmdl' * 20000
prop = sha(prop_bytes)
payload = {f'assets/{character}/manifest.json': b'{"id":"x"}', f'assets/{character}/model.bin': b'm' * 150000, f'textures/{texture_sha}.png': texture,
           f'static/assets/{prop}.gmdl': prop_bytes}
files = {path: {'size': len(b), 'sha256': prop if path.startswith('static/') else sha(b) if path.startswith('textures/') else sha(b), 'packed': len(b) + 80 + 8}
         for path, b in payload.items()}
# The stored .dat is the packed stream; its length is what matters to the installer.
blobs = {files[p]['sha256']: b'P' * files[p]['packed'] for p in payload}
char_item = {'kind': 'character', 'asset': character, 'name': 'Workshop Girl', 'files': [f'assets/{character}/manifest.json', f'assets/{character}/model.bin', f'textures/{texture_sha}.png'],
             'settings': {'spawn': {'scaleMultiplier': 9}, 'bodygroups': {'presets': {'Swim': {'visible': [True, False, 'x']}, '\u0001bad': {'visible': []}}, 'default': 'Swim'}},
             'arms': {'0': 1, '3': -1, 'x': 5}, 'fit': {'version': 3, 'generator': 18, 'bodies': [{'bone': 1}], 'scale': 1}}
prop_item = {'kind': 'static', 'asset': prop, 'name': 'Demo Chair', 'files': [f'static/assets/{prop}.gmdl'], 'settings': {'spawn': {'scale': 50}}}
workshop_id, demo_id = 'a' * 32, 'b' * 32
workshop_files = {p: files[p] for p in char_item['files']}
demo_files = {p: files[p] for p in prop_item['files']}
workshop_pkg = package(workshop_id, 'Anime Pack', 'workshop', [char_item], workshop_files)
demo_pkg = package(demo_id, 'Demo Pack', 'user', [prop_item], demo_files)
demo_pkg['folder'] = 'Demo models'


def mount(pkg, title, path_ids):
    text = json.dumps(pkg)
    for pid in path_ids: G.FS[pid + '|data_static/mmdhl/packages/' + pkg['id'] + '.json'] = text
    for f in pkg['files'].values():
        for pid in path_ids: G.FS[pid + '|data_static/mmdhl/files/' + f['sha256'] + '.dat'] = blobs[f['sha256']].decode()


def unmount(pkg):
    for k in list(G.FS.keys()):
        if pkg['id'] in k or any(f['sha256'] + '.dat' in k for f in pkg['files'].values()):
            if not k.startswith('DATA|'): del G.FS[k]


def run(frames=400):
    for _ in range(frames):
        for fn in list(G.HOOKS['Think'].values()): fn()


# --- Validation: nothing in a package is trusted.
def validate(pid, text):
    result = W.ValidatePackage(pid, text)
    return result[0] if isinstance(result, tuple) else result


assert validate(workshop_id, json.dumps(workshop_pkg))
for broken, why in [
    (dict(workshop_pkg, id='z' * 32), 'id must match the file name'),
    (dict(workshop_pkg, format='other'), 'format'),
    (dict(workshop_pkg, install='everyone'), 'install mode'),
    (dict(workshop_pkg, items=[dict(char_item, files=char_item['files'][:1])]), 'character without model data'),
    (dict(workshop_pkg, items=[dict(char_item, files=char_item['files'] + [f'rigs/{"d" * 32}/carrier.gma'])], files=dict(workshop_files, **{f'rigs/{"d" * 32}/carrier.gma': files[f'assets/{character}/model.bin']})), 'generated archives are rebuilt locally'),
    (dict(workshop_pkg, files=dict(workshop_files, **{f'textures/{texture_sha}.png': dict(files[f'textures/{texture_sha}.png'], sha256='e' * 64)})), 'texture must match its hash'),
    (dict(workshop_pkg, files=dict(workshop_files, **{f'assets/{character}/model.bin': dict(files[f'assets/{character}/model.bin'], packed=10 ** 9)})), 'packet over the transfer bound'),
    (dict(demo_pkg, files={f'static/assets/{prop}.gmdl': dict(files[f'static/assets/{prop}.gmdl'], sha256='f' * 64)}), 'prop hash is its id'),
]:
    assert validate(broken['id'] if broken.get('id') in (workshop_id, demo_id) else workshop_id, json.dumps(broken)) is None, why
assert validate(workshop_id, 'x' * (4 * 1048576 + 1)) is None
# The exporter (native/packages.hpp) refuses exactly what installers refuse.
base, shift = map(int, re.search(r'PackageManifestLimit=(\d+)u<<(\d+);', (ROOT / 'native/packages.hpp').read_text(encoding='utf-8')).groups())
limit = base << shift


def padded(size):
    pkg = json.loads(json.dumps(workshop_pkg)); pkg['items'][0]['fit'] = dict(char_item['fit'], pad='')
    pkg['items'][0]['fit']['pad'] = 'x' * (size - len(json.dumps(pkg))); return json.dumps(pkg)


assert len(padded(limit)) == limit and validate(workshop_id, padded(limit)), 'installers accept the largest manifest an export can have'
assert validate(workshop_id, padded(limit + 1)) is None, 'and refuse one byte more'
settings = W.ItemSettings(validate(workshop_id, json.dumps(workshop_pkg))['items'][1])
assert settings['spawn']['scaleMultiplier'] == 4 and settings['name'] == 'Workshop Girl'
assert set(settings['bodygroups']['presets'].keys()) == {'Swim', 'bad'}, 'control characters removed from preset names'
assert settings['bodygroups']['default'] == 'Swim' and settings['bodygroups']['presets']['Swim']['visible'][2] is False
arms = W.ItemArms(validate(workshop_id, json.dumps(workshop_pkg))['items'][1])
assert dict(arms.items()) == {'0': 1, '3': -1}, 'arms choices keep string slot keys (a 1..n table would save as a JSON array)'
print('PASS: package validation and settings sanitizing')

# --- Every subscriber inflates and writes what a package declares: each file is
# bounded by its kind and by what its packed bytes can expand to, and so is the whole.
MiB, GiB = 1 << 20, 1 << 30


def reason(files):
    pkg = json.loads(json.dumps(workshop_pkg)); pkg['files'].update(files)
    result = W.ValidatePackage(workshop_id, json.dumps(pkg))
    return result[1] if isinstance(result, tuple) else None


def entry(size, packed=None): return {'size': size, 'sha256': 'e' * 64, 'packed': max(80, size) if packed is None else packed}


other = 'f' * 64
for path, limit in [(f'assets/{other}/manifest.json', 64 * MiB), (f'assets/{other}/model.bin', GiB), (f'textures/{other}.png', 256 * MiB), (f'static/assets/{other}.gmdl', 256 * MiB)]:
    assert reason({path: entry(limit)}) is None, path + ' at its limit'
    assert reason({path: entry(limit + 1)}) == 'file too large for its kind', path + ' over its limit'
for path in [f'assets/{other}/materials-v5.gma', f'rigs/{"d" * 32}/carrier.gma', 'lua/autorun/x.lua', f'textures/{other.upper()}.png', f'assets/{other}/model.bin/x', 'textures/abc.png']:
    assert reason({path: entry(100)}) == 'package lists a forbidden file', path
# 256:1 at most, or anything up to 1 MiB.
assert reason({f'assets/{other}/model.bin': entry(256 * MiB, MiB)}) is None
assert reason({f'assets/{other}/model.bin': entry(256 * MiB + 1, MiB)}) == 'file expands too far beyond its packed size'
assert reason({f'assets/{other}/model.bin': entry(MiB, 80)}) is None and reason({f'assets/{other}/model.bin': entry(MiB + 1, 80)}) == 'file expands too far beyond its packed size'
# 16 GiB per package, counting the character's own files.
listed = sum(f['size'] for f in workshop_files.values())
models = {f'assets/{i:064x}/model.bin': entry(GiB) for i in range(1, 16)}
models[f'assets/{16:064x}/model.bin'] = entry(GiB - listed)
assert reason(models) is None, 'exactly 16 GiB'
models[f'assets/{16:064x}/model.bin'] = entry(GiB - listed + 1)
assert reason(models) == 'package too large', 'one byte over 16 GiB'
print('PASS: per-kind file caps, forbidden paths, the expansion bound and the package cap')

# --- A Workshop package (mapped by title) and a demo pack in a renamed item (found by the native scan).
G.ADDONS = to_lua([{'title': 'Anime Pack', 'wsid': '123456', 'mounted': True, 'file': 'content/4000/123456/anime.gma'},
                   {'title': 'Renamed Demo', 'wsid': '777', 'mounted': True, 'file': 'content/4000/777/demo.gma'}])
mount(workshop_pkg, 'Anime Pack', ['GAME', 'Anime Pack', 'WORKSHOP'])
mount(demo_pkg, 'Demo Pack', ['GAME', 'Demo Pack', 'WORKSHOP'])
lua.execute('''
native.StartAddonPackageScan=function(list) SCANNED=py_decode(list) return 1 end
native.PollAddonPackageScan=function() return py_encode({{},{DEMO_ID}}) end
''')
G.DEMO_ID = demo_id
W.Scan(); run()
assert G.CALLS and all(c[1] == 'begin' for c in G.CALLS.values())
begun = {c[2]: c for c in G.CALLS.values()}
assert set(begun) == set(payload), 'every file installed once'
assert begun[f'static/assets/{prop}.gmdl'][5] == files[f'static/assets/{prop}.gmdl']['packed']
origin = W.Origin('character', character)
assert origin['wsid'] == '123456' and origin['title'] == 'Anime Pack' and origin['workshop'] and origin['author'] == 'Tester'
assert W.Origin('static', prop) is None, 'demo models belong to the player'
demo_origin = W.origins[demo_id]
assert demo_origin['wsid'] == '777' and demo_origin['title'] == 'Renamed Demo' and demo_origin['pathId'] == 'Demo Pack'
state = W.state.assets
assert state['character:' + character]['installed'] and not state['character:' + character]['adopted']
assert state['static:' + prop]['adopted']
lib = json.loads(G.FS['DATA|mmd_hotloader/library/' + character + '.json'])
assert lib['name'] == 'Workshop Girl' and lib['spawn']['scaleMultiplier'] == 4 and 'folder' not in lib
assert json.loads(G.FS['DATA|mmd_hotloader/static/library/' + prop + '.json'])['folder'] == 'Demo models'
assert json.loads(G.FS['DATA|mmd_hotloader/fit_overrides/' + character + '.json'])['generator'] == 18
assert 'DATA|mmd_hotloader/arms/' + character + '.json' in G.FS
assert any('Installed 2' in n for n in G.NOTICES.values())
print('PASS: install through the transfer, title and archive-index origins, demo adoption, settings and extras')

# --- Existing settings are never overwritten; nothing reinstalls when present.
G.FS['DATA|mmd_hotloader/library/' + character + '.json'] = json.dumps({'name': 'My rename'})
calls = len(G.CALLS); W.Scan(); run()
assert len(G.CALLS) == calls and json.loads(G.FS['DATA|mmd_hotloader/library/' + character + '.json'])['name'] == 'My rename'

# --- Self-repair of installed models. Every scan compares file sizes: a resized or
# missing file clears the completion record and installs just that file again.
model_path, texture_path = f'assets/{character}/model.bin', f'textures/{texture_sha}.png'
G.FS['DATA|mmd_hotloader/' + model_path] = 'x' * 10
del G.FS['DATA|mmd_hotloader/' + texture_path]
calls = len(G.CALLS); W.Scan(); run()
assert [G.CALLS[i][2] for i in range(calls + 1, len(G.CALLS) + 1)] == [model_path, texture_path], 'resized and missing installed files are installed again'
assert W.state.assets['character:' + character]['installed'] and W.Origin('character', character)
# A file changed in place (same size) is found by hashing, once per session after
# the scan, the installed files modified since they were installed or last hashed:
# one 5 ms hash per frame here, so startup does not wait.
workshop_source = (ROOT / 'addon/lua/mmdhl/workshop.lua').read_text(encoding='utf-8')
def new_session():
    global W
    lua.execute('mmdhl.workshop=nil')
    lua.execute(workshop_source)
    W = lua.eval('mmdhl.workshop')
def touch(path): lua.eval('TOUCH')('DATA|mmd_hotloader/' + path)
def verify_frames():
    hashes = []
    while len(hashes) < 100:
        before = G.HASHED
        for fn in list(G.HOOKS['Think'].values()): fn()
        hashes.append(G.HASHED - before)
        if len(G.CALLS) > calls: break
    return hashes
new_session()
manifest_path = f'assets/{character}/manifest.json'
G.FS['DATA|mmd_hotloader/' + texture_path] = 'y' * files[texture_path]['size']
for path in (manifest_path, model_path, texture_path): touch(path)
lua.execute('CLOCK=0 SysTime=function() CLOCK=CLOCK+.0001 return CLOCK end HASH_COST=.005 HASHED=0')
calls = len(G.CALLS); W.Scan()
hashes = verify_frames()
assert len(G.CALLS) > calls, 'a changed installed file was not found'
assert max(hashes) <= 1 and sum(hashes) == 3, f'hashing is spread over frames, at most one hash each: {hashes}'
run()
assert [G.CALLS[i][2] for i in range(calls + 1, len(G.CALLS) + 1)] == [texture_path], 'only the changed file is installed again'
assert W.state.assets['character:' + character]['installed']
hashed = G.HASHED; W.Scan(); run()
assert G.HASHED == hashed, 'installed models are checked once per session'
# An unchanged cache costs no hashing in the next session; a file touched without a
# change is hashed once, then remembered.
new_session(); hashed = G.HASHED; calls = len(G.CALLS); W.Scan(); run()
assert G.HASHED == hashed and len(G.CALLS) == calls, 'unchanged installed files are hashed again in a new session'
touch(model_path)
new_session(); hashed = G.HASHED; W.Scan(); run()
assert G.HASHED == hashed + 1 and len(G.CALLS) == calls, 'a touched file is hashed alone, and it still matches'
new_session(); hashed = G.HASHED; W.Scan(); run()
assert G.HASHED == hashed, 'a file that matched is remembered with its new time'
lua.execute('SysTime=os.clock HASH_COST=nil')
# The player's own import of a packaged model (no install record) is left alone,
# even when a file's size differs (another importer version's manifest layout).
manifest_path, key = f'assets/{character}/manifest.json', 'character:' + character
record = W.state.assets[key]
W.state.assets[key] = None
G.FS['DATA|mmd_hotloader/' + manifest_path] = 'x' * (files[manifest_path]['size'] + 5)
calls = len(G.CALLS); W.Scan(); run()
assert len(G.CALLS) == calls and W.state.assets[key] is None, "a player's own import was taken over as a Workshop install"
G.FS['DATA|mmd_hotloader/' + manifest_path] = 'x' * files[manifest_path]['size']
W.state.assets[key] = record
print('PASS: installed models are repaired: sizes at every scan, files modified since their last check hashed once per session, a frame at a time')

# --- Deleting provided models records the choice; rescans respect it; Restore installs again.
done = {}
lua.globals().DONE = lua.table()
W.Delete('character', to_lua([character]), lua.eval('function(ok,m) DONE.ok=ok DONE.m=m end'))
assert G.DONE.ok and 'Deleted Workshop models' in G.DONE.m
assert W.IsHidden('character', character) and [d['id'] for d in to_py(W.Deleted('character'))] == [character]
W.Scan(); run()
assert not W.InCache('character', character), 'deleted Workshop model stays deleted'
ok, message = W.Restore('character', to_lua([character])); run()
assert ok and W.InCache('character', character) and not W.IsHidden('character', character)
W.Delete('static', to_lua([prop]), lua.eval('function() end'))
assert W.IsHidden('static', prop), 'deleted demo models are not reinstalled either'
W.Restore('static', to_lua([prop])); run()
assert W.InCache('static', prop) and W.state.assets['static:' + prop]['adopted']
print('PASS: delete to Deleted Workshop models, rescans, restore, demo deletion')

# --- Unsubscribing: Workshop models leave, the player's demo models stay.
unmount(workshop_pkg); unmount(demo_pkg); G.ADDONS = to_lua([])
del_before = len(G.DELETED)
W.Scan(); run()
removed = [G.DELETED[i] for i in range(del_before + 1, len(G.DELETED) + 1)]
assert removed == [character], removed
assert W.InCache('static', prop) and W.state.assets['character:' + character] is None
print('PASS: unsubscribed Workshop models are removed; adopted demo models remain')

# --- A model's files arrive before its textures. An install that stops there (a
# texture fails, the game closes) is not complete: a rescan transfers only the
# missing file, and so does the next start without a completion record.
texture_path = f'textures/{texture_sha}.png'
G.ADDONS = to_lua([{'title': 'Anime Pack', 'wsid': '123456', 'mounted': True, 'file': 'content/4000/123456/anime.gma'}])
mount(workshop_pkg, 'Anime Pack', ['GAME', 'Anime Pack', 'WORKSHOP'])
G.FS['DATA|mmd_hotloader/' + texture_path] = None  # uninstalling kept the texture; start without it
G.FAIL_PATH = texture_path
W.Scan(); run()
assert W.InCache('character', character) and 'DATA|mmd_hotloader/' + texture_path not in G.FS
assert W.state.assets['character:' + character] is None, 'a failed install leaves no completion record'
G.FAIL_PATH = None
calls = len(G.CALLS); W.Scan(); run()
assert [G.CALLS[i][2] for i in range(calls + 1, len(G.CALLS) + 1)] == [texture_path], 'a rescan transfers only the missing texture'
assert 'DATA|mmd_hotloader/' + texture_path in G.FS and W.state.assets['character:' + character]['installed']
del G.FS['DATA|mmd_hotloader/' + texture_path]
stored = json.loads(G.FS['DATA|mmd_hotloader/workshop/state.json']); del stored['assets']['character:' + character]
G.FS['DATA|mmd_hotloader/workshop/state.json'] = json.dumps(stored)
lua.execute('mmdhl.workshop=nil')
lua.execute((ROOT / 'addon/lua/mmdhl/workshop.lua').read_text(encoding='utf-8'))
W = lua.eval('mmdhl.workshop')
calls = len(G.CALLS); W.Scan(); run()
assert [G.CALLS[i][2] for i in range(calls + 1, len(G.CALLS) + 1)] == [texture_path], 'the next start resumes the interrupted install'
unmount(workshop_pkg); G.ADDONS = to_lua([]); W.Scan(); run()
assert not W.InCache('character', character)
print('PASS: an interrupted install resumes on rescan and after a restart')

# --- A package file that cannot be installed is reported and skipped.
mount(workshop_pkg, 'Anime Pack', ['GAME', 'Anime Pack'])
G.ADDONS = to_lua([{'title': 'Anime Pack', 'wsid': '123456', 'mounted': True, 'file': 'x.gma'}])
G.FAIL_PATH = f'assets/{character}/model.bin'
W.Scan(); run()
assert not W.InCache('character', character) and any('refused' in e for e in G.ERRORS.values())
G.FAIL_PATH = None

# --- Dedicated servers install and approve; approvals end with the package.
lua.execute('''
CLIENT=false SERVER=true SINGLE=false DEDICATED=true
APPROVED={} FORGOTTEN={}
mmdhl.approved={assets={['%s']={approvedBy='workshop'}},props={}}
mmdhl.ApproveWorkshopAsset=function(id,name,package) APPROVED[#APPROVED+1]=id..'|'..name..'|'..package end
mmdhl.ForgetPublishedAssets=function(ids) for _,id in ipairs(ids) do FORGOTTEN[#FORGOTTEN+1]=id end end
util.AddNetworkString=function() end
''' % ('9' * 64))
for k in [k for k in G.FS.keys() if k.startswith('DATA|mmd_hotloader/')]: del G.FS[k]
lua.execute((ROOT / 'addon/lua/mmdhl/workshop.lua').read_text(encoding='utf-8'))
W = lua.eval('mmdhl.workshop')
W.Scan(); run()
assert W.InCache('character', character) and list(G.APPROVED.values()) == [character + '|Workshop Girl|' + workshop_id]
assert list(G.FORGOTTEN.values()) == ['9' * 64], 'a Workshop approval without its package is withdrawn'
# A dedicated server repairs its own cache too, and drops prepared download manifests.
lua.execute('INVALIDATED=0 mmdhl.InvalidateSharedManifests=function() INVALIDATED=INVALIDATED+1 end')
G.FS['DATA|mmd_hotloader/' + model_path] = 'y' * files[model_path]['size']; touch(model_path)
calls = len(G.CALLS); W.Scan(); run()
assert [G.CALLS[i][2] for i in range(calls + 1, len(G.CALLS) + 1)] == [model_path], 'a dedicated server repairs a changed file'
assert G.INVALIDATED == 1 and not G.FS['DATA|mmd_hotloader/' + model_path].strip('x')
print('PASS: failed files are reported; dedicated servers install, approve, repair and withdraw Workshop models')

# --- A model's saved fit travels with it: collision corrections, bone pins (often saved alone:
# new pins drop the corrections made for the old bones) and a physics default, each optional and
# each checked when present. The pins follow the bone window's own rules (bone_mapper_rules.lua).
VB = 'ValveBiped.Bip01_'


def item_fit(fit, kind='character'):
    return to_py(W.ItemFit(to_lua({'kind': kind, 'asset': character, 'fit': fit})))


base_fit = {'version': 3, 'generator': 18}
pins_only = dict(base_fit, boneMap={VB + 'L_Thigh': 12.0, VB + 'Spine2': -1}, boneMapVersion=1, boneMapSavedAt=1790000000)
physics_only = dict(base_fit, mass=55, physics={'schema': 1, 'bodies': {}}, editor={'schema': 1, 'savedAt': 1790000000}, excludedMaterials=['skin'])
assert item_fit(pins_only) is None, 'without the pin rules a package\'s pins cannot be checked'
lua.execute((ROOT / 'addon/lua/mmdhl/bone_mapper_rules.lua').read_text(encoding='utf-8'))
assert item_fit(dict(base_fit, bodies=[{'bone': 1}], scale=1))['bodies'] == [{'bone': 1}], 'collision corrections alone, as before'
assert item_fit(dict(base_fit, generator=14, bodies=[]))['generator'] == 14
got = item_fit(pins_only)
assert got['boneMap'] == {VB + 'L_Thigh': 12, VB + 'Spine2': -1} and got['boneMapSavedAt'] == 1790000000 and 'bodies' not in got, got
got = item_fit(physics_only)
assert got['mass'] == 55 and got['physics']['schema'] == 1 and got['excludedMaterials'] == ['skin'] and 'boneMap' not in got, got
assert item_fit(dict(base_fit, excludedMaterials=['skin']))['excludedMaterials'] == ['skin'], 'excluded materials survive dropped corrections'
assert item_fit(dict(base_fit, bodies=[], boneMap={})) is not None, 'an empty pin set (no pins) beside corrections'
for broken, why in [
    (dict(pins_only, version=2), 'version'),
    (dict(pins_only, generator=9), 'generator'),
    (dict(base_fit), 'nothing to install'),
    (dict(base_fit, boneMap={}), 'nothing but an empty pin set'),
    (dict(base_fit, excludedMaterials=[]), 'nothing but an empty list'),
    (dict(base_fit, bodies='x'), 'bodies of the wrong type'),
    (dict(pins_only, physics='x'), 'physics of the wrong type'),
    (dict(pins_only, mass='heavy'), 'mass of the wrong type'),
    (dict(pins_only, excludedMaterials='skin'), 'excluded materials of the wrong type'),
    (dict(pins_only, editor=1), 'editor of the wrong type'),
    (dict(pins_only, boneMap=[1, 2]), 'pins without part names'),
    (dict(pins_only, boneMap={'Eye_L': 3}), 'eyes are pinned only when converting'),
    (dict(pins_only, boneMap={'Nonsense': 3}), 'an unknown part'),
    (dict(pins_only, boneMap={VB + 'L_Thigh': 1.5}), 'a fractional bone'),
    (dict(pins_only, boneMap={VB + 'L_Thigh': -2}), 'a bone below -1'),
    (dict(pins_only, boneMap={VB + 'L_Thigh': 'twelve'}), 'a bone that is no number'),
    (dict(pins_only, boneMap={VB + 'L_Thigh': '12'}), 'a bone number as text (the native reads numbers)'),
    (dict(pins_only, boneMap=12), 'pins of the wrong type'),
    (dict(pins_only, pad='x' * (512 * 1024)), 'over 512 KiB'),
]:
    assert item_fit(broken) is None, why
assert item_fit(pins_only, 'static') is None, 'props have no fit'
print('PASS: a saved fit installs with collision corrections, bone pins or physics alone, each checked, within 512 KiB')

# Installed on a dedicated server and on clients: the pins and the physics default reach
# fit_overrides; a fit with a wrong pin is left out while its model installs.
pins_id, physics_id, bad_id = '1' * 64, '2' * 64, '3' * 64
pins_pkg_id = 'c' * 32
pins_items, pins_files = [], {}
for asset, fit in ((pins_id, pins_only), (physics_id, physics_only), (bad_id, dict(pins_only, boneMap={VB + 'L_Thigh': 1.5}))):
    paths = [f'assets/{asset}/manifest.json', f'assets/{asset}/model.bin']
    pins_items.append({'kind': 'character', 'asset': asset, 'name': 'Model ' + asset[0], 'files': paths, 'fit': fit})
    pins_files.update({paths[0]: files[f'assets/{character}/manifest.json'], paths[1]: files[f'assets/{character}/model.bin']})
pins_pkg = package(pins_pkg_id, 'Pins Pack', 'workshop', pins_items, pins_files)
mount(pins_pkg, 'Pins Pack', ['GAME', 'Pins Pack'])
G.ADDONS = to_lua([{'title': 'Anime Pack', 'wsid': '123456', 'mounted': True, 'file': 'x.gma'}, {'title': 'Pins Pack', 'wsid': '888', 'mounted': True, 'file': 'y.gma'}])


def installed_fits():
    for asset in (pins_id, physics_id, bad_id): assert W.InCache('character', asset), asset
    pins = json.loads(G.FS['DATA|mmd_hotloader/fit_overrides/' + pins_id + '.json'])
    physics = json.loads(G.FS['DATA|mmd_hotloader/fit_overrides/' + physics_id + '.json'])
    assert pins['boneMap'] == {VB + 'L_Thigh': 12, VB + 'Spine2': -1} and pins['version'] == 3, pins
    assert physics['mass'] == 55 and physics['physics']['schema'] == 1, physics
    assert 'DATA|mmd_hotloader/fit_overrides/' + bad_id + '.json' not in G.FS


W.Scan(); run()
installed_fits()
lua.execute('CLIENT=true SERVER=false SINGLE=true DEDICATED=false')
for k in [k for k in G.FS.keys() if k.startswith('DATA|mmd_hotloader/')]: del G.FS[k]
new_session(); W.Scan(); run()
installed_fits()
print('PASS: dedicated servers and clients install fits that hold only bone pins or only physics')

# --- Natives after 2.3.0 check the assembled model (StartAssetCheck: its manifest's identity, the
# checksums, the model itself) before it counts as installed, and a server approves only what passed:
# files whose hashes all match can still make up another model than the package names.
lua.execute('''
file.Delete=function(path,id) FS[(id or 'DATA')..'|'..path]=nil end
CHECKS={} VERDICT={}
native.StartAssetCheck=function(kind,id) CHECKS[#CHECKS+1]=kind..':'..id return #CHECKS end
native.PollAssetCheck=function(h) local v=VERDICT[CHECKS[h]:match(':(%x+)$')] if v==nil then return false end if v==true then return true end return nil,v,'invalid' end
function INSTALLED(key) local r=mmdhl.workshop.state.assets[key] return r~=nil and r.installed==true end
''')
# The pins pack shares the character's files: mount the Anime Pack again after removing it.
unmount(pins_pkg); mount(workshop_pkg, 'Anime Pack', ['GAME', 'Anime Pack'])
for k in [k for k in G.FS.keys() if k.startswith('DATA|mmd_hotloader/')]: del G.FS[k]
G.ADDONS = to_lua([{'title': 'Anime Pack', 'wsid': '123456', 'mounted': True, 'file': 'x.gma'}])
manifest_key = f'DATA|mmd_hotloader/assets/{character}/manifest.json'
new_session(); W.Scan(); run()
assert manifest_key in G.FS and not G.INSTALLED('character:' + character), ('a model counted as installed before its check', list(G.CHECKS.values()))
G.VERDICT[character] = 'Cache version or ID mismatch'; run()
assert not G.INSTALLED('character:' + character) and manifest_key not in G.FS, 'an incoherent model was installed or left for the library to list'
assert 'Cache version or ID mismatch' in W.failures['character:' + character]
G.VERDICT[character] = True; W.Scan(); run()
assert G.INSTALLED('character:' + character) and manifest_key in G.FS and W.checked['character:' + character], 'a valid model was not installed'
# A dedicated server approves a model installed in an earlier session once it passed the check.
lua.execute('''
CLIENT=false SERVER=true SINGLE=false DEDICATED=true APPROVED={}
mmdhl.approved={assets={},props={}}
mmdhl.ApproveWorkshopAsset=function(id,name,package) APPROVED[#APPROVED+1]=id end
''')
G.VERDICT[character] = None; new_session(); W.Scan(); run()
assert len(G.APPROVED) == 0, 'a server approved a model before its check'
G.VERDICT[character] = 'Cached model checksum mismatch'; run()
assert len(G.APPROVED) == 0 and 'checksum' in W.failures['character:' + character], 'a server approved an incoherent model'
G.VERDICT[character] = True; new_session(); W.Scan(); run()
assert list(G.APPROVED.values()) == [character], 'a server did not approve a valid model'
lua.execute('CLIENT=true SERVER=false SINGLE=true DEDICATED=false native.StartAssetCheck=nil native.PollAssetCheck=nil')
print('PASS: a Workshop model counts as installed, and is approved, only once its assembled files load as that model')
