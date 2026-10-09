"""Ragdoll physics editor (physics_profile.lua, physics_editor.lua): the profile
model, its canonical form against native's, QC and .phy interchange, the
server's permissions, rate limits, saved defaults and replace-in-place, and
the editor model on clients with older or missing native modules. Runs the
addon's Lua against simulated games in Lua 5.1 (GMod's dialect)."""
import json
import math
import random
import re
import struct
from pathlib import Path
from lupa import lua51, LuaRuntime
from lua_i18n import attach
from lua_source import definition

root = Path(__file__).resolve().parents[1]
read = lambda relative: (root / relative).read_text(encoding='utf-8')
PROFILE = read('addon/lua/mmdhl/physics_profile.lua')


def runtime():
    lua = lua51.LuaRuntime(unpack_returned_tuples=True)
    lua.execute('mmdhl={} unpack=unpack or table.unpack')
    lua.execute(PROFILE)
    return lua


def to_lua(lua, value):
    if isinstance(value, dict): return lua.table_from({k: to_lua(lua, v) for k, v in value.items()})
    if isinstance(value, (list, tuple)): return lua.table_from([to_lua(lua, v) for v in value])
    return value


def to_py(value):
    """A Lua table as a list (1..n keys) or dict; empty tables become {}."""
    if hasattr(value, 'keys') and hasattr(value, 'values'):
        keys = list(value.keys())
        if keys and all(isinstance(k, (int, float)) and k == int(k) for k in keys) and sorted(int(k) for k in keys) == list(range(1, len(keys) + 1)):
            return [to_py(value[k]) for k in range(1, len(keys) + 1)]
        return {(int(k) if isinstance(k, float) and k == int(k) else k): to_py(v) for k, v in value.items()}
    return value


def numbers_equal(a, b):
    if isinstance(a, dict) and isinstance(b, dict): return a.keys() == b.keys() and all(numbers_equal(a[k], b[k]) for k in a)
    if isinstance(a, list) and isinstance(b, list): return len(a) == len(b) and all(numbers_equal(x, y) for x, y in zip(a, b))
    if isinstance(a, (int, float)) and isinstance(b, (int, float)) and not isinstance(a, bool) and not isinstance(b, bool): return a == b
    return a == b


lua = runtime()
P = lua.eval('mmdhl.physics')
B = lambda s: 'ValveBiped.Bip01_' + s

# ---- L1: the defaults are SCMI's (native/scmi_data.hpp), UpperArm rotdamping 3 ----
scmi = read('native/scmi_data.hpp')
start = scmi.index('"limits":{') + len('"limits":')
depth, end = 0, start
for end in range(start, len(scmi)):
    depth += {'{': 1, '}': -1}.get(scmi[end], 0)
    if depth == 0: break
limits = json.loads(scmi[start:end + 1])
names = [P.BODIES[i] for i in range(1, 19)]
assert names[3] == B('Head1') and names[17] == B('R_Foot') and len(limits) >= 18
for i, name in enumerate(names):
    expect = {'massBias': 1, 'rotdamping': 3, 'limits': {}}
    for line in limits[name]:
        parts = line.split()
        if parts[0] == '$jointmassbias': expect['massBias'] = float(parts[2])
        elif parts[0] == '$jointrotdamping': expect['rotdamping'] = float(parts[2])
        elif parts[0] == '$jointconstrain': expect['limits'][parts[2]] = [float(parts[4]), float(parts[5])]
    d = P.DEFAULTS[i]
    assert d.massBias == expect['massBias'] and d.rotdamping == expect['rotdamping'], name
    for axis, (lo, hi) in expect['limits'].items():
        assert d.limits[axis][1] == lo and d.limits[axis][2] == hi, (name, axis)
    assert (i == 0) == (d.limits is None), name
assert P.DEFAULTS[5].rotdamping == 3 and P.DEFAULTS[9].rotdamping == 3
print('PASS: P.DEFAULTS equal SCMI\'s joint table in native/scmi_data.hpp (UpperArm rotdamping stays 3)')

# ---- L2: stops, caps, per-joint stops, explicit numbers, hinge and lock ----
STIFF = {-2: (.35, 0), -1: (.6, 0), 0: (1, 0), 1: (2, .6), 2: (4, 2)}
RANGE = {-3: 0, -2: .5, -1: .75, 0: 1, 1: 1.25, 2: 1.5}
def q(v, d):
    p = 10 ** d; n = v * p
    n = math.floor(n + .5) if n >= 0 else -math.floor(-n + .5)
    r = n / p
    return 0 if r == 0 else r
CAPS = {i: {a: [P.CAPS[i][a][1], P.CAPS[i][a][2]] for a in 'xyz'} for i in range(1, 18)}
def expected(i, stiffness, rng, floatiness=0, hinge=False):
    d = P.DEFAULTS[i]; rd, f = STIFF[stiffness]; k = RANGE[rng]
    out = {'rotdamping': q(d.rotdamping * rd, 3), 'damping': {0: .8, 1: 2, 2: 5}[floatiness]}
    if i > 0:
        out['limits'] = {}
        for a in 'xyz':
            L, H = d.limits[a][1], d.limits[a][2]; lo, hi = L * k, H * k
            if k > 1: lo = max(lo, min(L, CAPS[i][a][0])); hi = min(hi, max(H, CAPS[i][a][1]))
            lo, hi = q(lo, 1), q(hi, 1)
            if hinge and i in (6, 10, 13, 16) and a != 'z': lo, hi = 0, 0
            out['limits'][a] = [lo, hi, 0 if lo == 0 and hi == 0 else f]
    return out
for s in range(-2, 3):
    for r in range(-3, 3):
        for fl in range(3):
            d = lua.eval('function(s,r,f) local d=mmdhl.physics.NewDraft() d.feel={preset="custom",stiffness=s,range=r,floatiness=f} return (mmdhl.physics.Effective(d)) end')(s, r, fl)
            for i in range(18):
                e = expected(i, s, r, fl); b = d[i]
                assert q(b.rotdamping, 3) == e['rotdamping'] and b.damping == e['damping'], (s, r, fl, i)
                if i:
                    for a in 'xyz': assert [b.limits[a][1], b.limits[a][2], b.limits[a][3]] == e['limits'][a], (s, r, i, a, list(b.limits[a].values()), e['limits'][a])
# Spot values from the spec's tables (§8.1, §8.2).
eff = lua.eval('function(s,r) local d=mmdhl.physics.NewDraft() d.feel.stiffness=s d.feel.range=r return (mmdhl.physics.Effective(d)) end')
assert list(eff(0, 1)[6].limits.x.values()) == [-50, 18.8, 0] and list(eff(0, 1)[6].limits.z.values()) == [-150, 10, 0]
assert list(eff(0, 2)[12].limits.y.values()) == [-45, 90, 0] and list(eff(0, 2)[15].limits.y.values()) == [-90, 45, 0] and list(eff(0, 2)[13].limits.z.values()) == [-10, 160, 0]
assert list(eff(0, -1)[1].limits.z.values()) == [-14.3, 14.3, 0] and list(eff(0, -2)[4].limits.z.values()) == [0, 7.5, 0]
assert eff(2, 0)[0].rotdamping == 12 and eff(-2, 0)[7].rotdamping == .35 and eff(1, 0)[17].rotdamping == 18 and eff(-2, 0)[14].rotdamping == 3.15
assert list(eff(1, 0)[6].limits.y.values()) == [0, 0, 0] and list(eff(1, 0)[6].limits.z.values()) == [-120, 10, .6]
mixed = lua.eval('''function()
 local P=mmdhl.physics local d=P.NewDraft() d.feel.stiffness=1
 P.SetJointSimple(d,6,'stiffness',-2) P.SetJointSimple(d,13,'hinge',true) P.SetJointSimple(d,7,'locked',true) P.SetJointSimple(d,7,'range',2)
 P.SetExplicit(d,6,'limits',{-30,11.3,.6},'x') P.SetExplicit(d,5,'massBias',3)
 return (P.Effective(d)),d end''')
b, d = mixed()
assert b[6].rotdamping == 1.4 and list(b[6].limits.z.values()) == [-120, 10, 0] and list(b[6].limits.x.values()) == [-30, 11.3, .6], 'a joint stop or explicit value lost to the global stop'
assert list(b[13].limits.x.values()) == [0, 0, 0] and list(b[13].limits.z.values()) == [-10, 125, .6], 'hinge only must fix X and Y only'
assert all(list(b[7].limits[a].values()) == [0, 0, 0] for a in 'xyz'), 'a locked joint wins over its bend range'
assert b[5].massBias == 3 and d.feel.preset == 'custom'
print('PASS: P.Effective matches the stop tables for every stop and joint; caps, joint stops, explicit values, hinge and lock')

# ---- L4: the canonical form agrees with native on the shared cases ----
cases = json.loads(read('tests/fixtures/physics/canonical_cases.json'))
assert len(cases) >= 40
canonical_of = lua.eval('function(raw) local P=mmdhl.physics local b,m=P.Expand(raw) return P.Canonical(b,m) end')
check_of = lua.eval('function(raw) local e=mmdhl.physics.CheckPhysics(raw) return e[1] and e[1].code end')
for c in cases:
    raw = to_lua(lua, c.get('input'))
    code = check_of(raw)
    if 'error' in c:
        assert code == c['error'], (c['label'], code)
    else:
        assert code is None, (c['label'], code)
        got = to_py(canonical_of(raw))
        assert numbers_equal(got, c['canonical']), (c['label'], got, c['canonical'])
print(f'PASS: P.Canonical and P.CheckPhysics agree with native on {len(cases)} shared cases')

# ---- L3: Resolve(DraftFromState(s)) is s ----
rig = {'scale': 3.23656, 'bodies': [], 'bones': []}
for i in range(18): rig['bodies'].append({'bone': i, 'center': [1 + i * .1, 0, 0], 'extent': [2, 1, 1], 'name': names[i]})
for i in range(18): rig['bones'].append({'name': names[i], 'position': {3: [0, 0, 60], 14: [0, 5, -6], 17: [0, -5, -6]}.get(i, [0, 0, 0]), 'rotation': [0, 0, 0, 1]})
resolve = lua.eval('function(s,rig) local P=mmdhl.physics return P.Resolve(P.DraftFromState(s,rig),rig) end')
draft_of = lua.eval('function(s,rig) return mmdhl.physics.DraftFromState(s,rig) end')
make = lua.eval('''function(kind,rig)
 local P=mmdhl.physics local d=P.NewDraft()
 if kind=='card' then P.ApplyPreset(d,'less_floppy')
 elseif kind=='statue' then P.ApplyPreset(d,'statue')
 elseif kind=='floaty' then P.SetFeel(d,'floatiness',2)
 elseif kind=='joint' then P.SetJointSimple(d,6,'range',-2,true) P.SetJointSimple(d,13,'hinge',true)
 elseif kind=='explicit' then P.SetExplicit(d,3,'drag',1.5) P.SetExplicit(d,3,'surfaceprop','metal') P.SetExplicit(d,12,'limits',{-20,40,1},'y',true)
 elseif kind=='template' then d.feel.preset='template' P.SetExplicit(d,1,'limits',{-5,5,.2},'x') P.SetExplicit(d,2,'massBias',4.5)
 elseif kind=='qc' then d.model.massMode='volume' d.model.collisions={mode='custom',pairs={{3,7},{3,11}}} d.model.animatedFriction={min=80,max=600,timeIn=.15,timeOut=.25,timeHold=1.75}
 elseif kind=='automass' then d.model.automass=true d.model.density=985 d.model.surfaceprop='wood'
 elseif kind=='mixed' then P.ApplyPreset(d,'relaxed') P.SetExplicit(d,6,'limits',{-360,360,.4},'x') d.mass=82.5 d.excludedMaterials={2,7}
 elseif kind=='shapes' then P.ShapeAction(d,6,'longer',rig,true) d.shapes[3]={center={0,0,1},extent={1,1,1},style='capsule'}
 elseif kind=='none' then d.model.collisions={mode='none',pairs={}} end
 return P.Resolve(d,rig)
end''')
lrig = to_lua(lua, rig)
for kind in ['default', 'card', 'statue', 'floaty', 'joint', 'explicit', 'template', 'qc', 'automass', 'mixed', 'shapes', 'none']:
    request = make(kind, lrig)
    state = lua.table_from({'applied': request})
    again = resolve(state, lrig)
    a, b = to_py(request), to_py(again)
    assert numbers_equal(a.get('physicsOverrides') or {}, b.get('physicsOverrides') or {}), (kind, a.get('physicsOverrides'), b.get('physicsOverrides'))
    for key in ('collisionOverrides', 'excludedMaterials', 'mass', 'physicsEditor'):
        assert numbers_equal(a.get(key) or {}, b.get(key) or {}), (kind, key, a.get(key), b.get(key))
# A scale change rescales stored shapes.
scaled = lua.eval('''function(rig)
 local P=mmdhl.physics local s={applied={collisionOverrides={['ValveBiped.Bip01_Head1']={center={1,2,3},extent={1,1,2}}},collisionOverrideScale=rig.scale/2}}
 return P.DraftFromState(s,rig).shapes[3] end''')(lrig)
assert list(scaled.center.values()) == [2, 4, 6] and list(scaled.extent.values()) == [2, 2, 4]
print('PASS: Resolve(DraftFromState(s)) reproduces 12 applied states (cards, joints, explicit, template, QC-like, shapes) and rescales shapes')
for kind in ['card', 'statue', 'floaty', 'joint', 'explicit', 'template', 'qc', 'automass', 'mixed', 'none']:
    assert to_py(make(kind, lrig)).get('physicsOverrides'), kind + ' produced no physics change'
assert len(to_py(make('shapes', lrig))['collisionOverrides']) == 3

# ---- L5: mirroring ----
mirror = lua.eval('function(i,t,axis) return mmdhl.physics.MirrorLimits(i,t,axis) end')
for left in (4, 5, 6, 7, 12, 13, 14):
    right = P.MIRROR[left]
    assert P.MIRROR[right] == left
    for a in 'xyz':
        d = P.DEFAULTS[left].limits[a]
        m = mirror(left, lua.table_from([d[1], d[2], 0]), a)
        r = P.DEFAULTS[right].limits[a]
        assert [m[1], m[2]] == [r[1], r[2]], (left, a, list(m.values()), list(r.values()))
assert list(mirror(12, lua.table_from([-360, 360, .5]), 'y').values()) == [-360, 360, .5]
# Shape centres follow the rest frames: a fixture rig whose right arm frame is the left one reflected.
def quat(axis, angle):
    s = math.sin(angle / 2); return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(angle / 2)]
frames = {name: [0, 0, 0, 1] for name in names}
frames[B('R_Forearm')] = quat([0, 0, 1], math.pi)  # X reversed and Y reversed: a mirror about the lateral (Y) plane flips Y only
positions = {name: [0, 0, 0] for name in names}
positions[B('L_Thigh')] = [0, 5, 0]; positions[B('R_Thigh')] = [0, -5, 0]; positions[B('Head1')] = [0, 0, 60]; positions[B('L_Foot')] = [0, 5, -6]; positions[B('R_Foot')] = [0, -5, -6]
mrig = {'scale': 3.23656, 'bodies': [{'bone': i, 'center': [1, 0, 0], 'extent': [2, 1, 1]} for i in range(18)],
        'bones': [{'name': n, 'position': positions[n], 'rotation': frames[n]} for n in names]}
shape = lua.eval('function(rig) return mmdhl.physics.MirrorShape(rig,6,{center={3,1,.5},extent={4,1,2},style="capsule"}) end')
out, centred = shape(to_lua(lua, mrig))
assert centred and list(out.center.values()) == [-3, 1, .5] and list(out.extent.values()) == [4, 1, 2] and out.style == 'capsule', list(out.center.values())
frames[B('R_Forearm')] = quat([0, 0, 1], math.pi / 4)
mrig['bones'] = [{'name': n, 'position': positions[n], 'rotation': frames[n]} for n in names]
out, centred = shape(to_lua(lua, mrig))
assert not centred and out.center is None and list(out.extent.values()) == [4, 1, 2]
checks = lua.eval('''function(rig) local P=mmdhl.physics local d=P.NewDraft() local _,skipped=P.SetShape(d,6,{center={3,1,.5},extent={4,1,2}},rig,true) return skipped,P.Check(d,nil,nil,{rig=rig,mirrorSkipped=skipped}),d end''')
skipped, issues, d = checks(to_lua(lua, mrig))
assert skipped and any(v.code == 'mirror_center_skipped' for v in issues.values()) and list(d.shapes[10].center.values()) == [1, 0, 0]
pairs = to_py(lua.eval('function() local s={} s[7]=true return mmdhl.physics.PassThroughPairs(s) end')())
assert len(pairs) == 120 and all(7 not in p for p in pairs)
print('PASS: left defaults mirror to the right ones (thigh Y negated), pass-through pairs, and shape centres mirror through the rest frames or are skipped')

# ---- L6: request validation (client before sending, server on receipt) ----
validate = lua.eval('function(r,ctx) local ok,e=mmdhl.physics.Validate(r,ctx) return ok,e[1] and e[1].code,e[1] and e[1].field end')
ctx = to_lua(lua, {'unit': 1, 'level': 1, 'materialCount': 5})
good = {'collisionOverrides': {B('L_Hand'): {'center': [1, 0, 0], 'extent': [1, 1, 1], 'style': 'box'}}, 'collisionOverrideScale': 3.2, 'mass': 70, 'excludedMaterials': [0, 4],
        'physicsOverrides': {'schema': 1, 'bodies': {B('L_Hand'): {'damping': 2}}}}
assert validate(to_lua(lua, good), ctx)[0]
bad = [
 ({'collisionOverrides': {B('L_Hand'): {'center': [73, 0, 0], 'extent': [1, 1, 1]}}}, 'shape_range'),
 ({'collisionOverrides': {B('L_Hand'): {'center': [0, 0, 0], 'extent': [.005, 1, 1]}}}, 'shape_range'),
 ({'collisionOverrides': {B('L_Hand'): {'center': [0, 0, 0], 'extent': [37, 1, 1]}}}, 'shape_range'),
 ({'collisionOverrides': {B('L_Hand'): {'center': [0, 0, 0], 'extent': [1, 1, 1], 'style': 'sphere'}}}, 'shape_range'),
 ({'collisionOverrides': {B('Spine2'): {'center': [0, 0, 0], 'extent': [1, 1, 1]}}}, 'unknown_body'),
 ({'mass': .5}, 'range'), ({'mass': 501}, 'range'), ({'excludedMaterials': [5]}, 'range'), ({'excludedMaterials': [1.5]}, 'range'),
]
for c in cases:
    if 'error' in c: bad.append(({'physicsOverrides': c['input']}, c['error']))
for request, code in bad:
    ok, got, field = validate(to_lua(lua, request), ctx)
    assert not ok and got == code, (request, got, code)
known = lua.eval('function(r) return mmdhl.physics.Validate(r,{unit=1,surfaceKnown=function(name) return name~="fakeprop" end}) end')
assert not known(to_lua(lua, {'physicsOverrides': {'schema': 1, 'surfaceprop': 'fakeprop'}}))[0]
assert known(to_lua(lua, {'physicsOverrides': {'schema': 1, 'surfaceprop': 'metal'}}))[0]
print(f'PASS: P.Validate refuses {len(bad)} malformed requests (every native code, shape bounds, mass, material slots) and unknown surface materials')

# ---- L6b: a physics error names its part (bone names contain dots); clamped shapes stay in range ----
lua.execute(r'''
local P=mmdhl.physics
local d=P.NewDraft() P.SetExplicit(d,6,'limits',{50,30,0},'z')
local order for _,v in ipairs(P.Check(d,nil,nil,{})) do if v.code=='limit_order' then order=v end end
assert(order and order.body==6 and order.axis=='z' and order.fixes[1]=='swap','a minimum above the maximum lost its part')
P.ApplyFix(d,order,'swap') local z=P.Effective(d)[6].limits.z assert(z[1]==30 and z[2]==50,'Swap changed nothing')
d=P.NewDraft() P.SetExplicit(d,10,'limits',{-200,30,0},'x')
local range for _,v in ipairs(P.Check(d,nil,nil,{})) do if v.code=='range' then range=v end end
assert(range and range.body==10 and range.axis=='x' and range.fixes[1]=='clamp','a range error lost its part')
P.ApplyFix(d,range,'clamp') assert(P.Effective(d)[10].limits.x[1]==-180 and not P.HasErrors(P.Check(d,nil,nil,{})),'Clamp changed nothing')
-- Shapes typed or stepped past a bound land on the nearest value that P.Check and the server accept.
local rig={scale=3.23656,bones={},bodies={}} for i,n in ipairs(P.BODIES) do rig.bones[i]={name=n,position=({[4]={0,0,68},[15]={0,5,0},[18]={0,-5,0}})[i] or {0,0,0},rotation={0,0,0,1}} rig.bodies[i]={bone=i-1,center={1,0,0},extent={2,1,1}} end
local u=P.Unit(rig)
local function inRange(d) for _,v in ipairs(P.Check(d,nil,nil,{rig=rig})) do if v.code=='shape_range' then return false end end return P.Validate(P.Resolve(d,rig),{unit=u}) end
for _,shape in ipairs({{center={0,0,0},extent={0,1,1}},{center={0,0,0},extent={1e6,1,1}},{center={-1e6,1e6,0},extent={1,1,1}},{center={0,0,0},extent={.0113,.01133,.011334}}}) do
 d=P.NewDraft() P.SetShape(d,6,shape,rig) assert(inRange(d),'a clamped shape is out of range: '..d.shapes[6].extent[1]..' '..d.shapes[6].center[1])
end
d=P.NewDraft() for _=1,80 do P.ShapeAction(d,6,'thinner',rig) P.ShapeAction(d,6,'shorter',rig) end assert(inRange(d),'Thinner and Shorter stepped out of range')
d=P.NewDraft() for _=1,80 do P.AllThickness(d,1/1.05,rig) end assert(inRange(d),'the thickness buttons stepped out of range')
''')
print('PASS: physics errors name their part, so Swap, Clamp and Go to work; shapes clamped at a bound stay valid for Checks and the server')

# Lua helpers of the QC and .phy file checks below.
ROUNDTRIP = r'''function(seed)
 local P=mmdhl.physics math.randomseed(seed) local failures={}
 local function pick(t) return t[math.random(#t)] end
 for n=1,100 do
  local d=P.NewDraft()
  P.ApplyPreset(d,pick(P.PRESET_ORDER))
  if math.random()<.5 then P.SetFeel(d,'floatiness',math.random(0,2)) end
  for k=1,math.random(0,6) do local i=math.random(1,17) P.SetJointSimple(d,i,pick({'stiffness','range'}),math.random(-2,2)) end
  for k=1,math.random(0,6) do local i=math.random(0,17)
   local f=pick({'massBias','damping','rotdamping','inertia','surfaceprop','limits'})
   if f=='limits' and i>0 then local lo=math.random(-180,0)+math.random(0,9)/10 local hi=math.random(0,180) local kind=math.random(3)
    if lo<-180 then lo=-180 end
    P.SetExplicit(d,i,'limits',kind==1 and {0,0,0} or kind==2 and {-360,360,math.random(0,500)/1000} or {lo,hi,math.random(0,2000)/1000},pick({'x','y','z'}))
   elseif f=='surfaceprop' then P.SetExplicit(d,i,'surfaceprop',pick({'metal','wood','flesh'}))
   elseif f~='limits' then P.SetExplicit(d,i,f,math.random(1,4000)/1000) end
  end
  if math.random()<.3 then local drag=math.random(0,500)/100 for i=0,17 do P.SetExplicit(d,i,'drag',drag) end end
  d.model.surfaceprop=pick({'flesh','metal','wood'}) d.model.massMode=pick({'bias','volume'})
  local c=math.random(3) if c==1 then d.model.collisions={mode='none',pairs={}} elseif c==2 then local pairs={} local all=P.AllPairs() for k=1,8 do pairs[#pairs+1]=all[math.random(#all)] end d.model.collisions={mode='custom',pairs=pairs} end
  if math.random()<.4 then d.model.animatedFriction={min=math.random(0,200),max=math.random(300,900),timeIn=math.random(0,100)/100,timeOut=math.random(0,100)/100,timeHold=math.random(0,300)/100} end
  if math.random()<.3 then d.model.automass=true d.model.density=985 else d.mass=math.random(100,5000)/10 end
  local text=P.EmitQC(d,{name='Test',key='0123',version='2.3.0'})
  local back=P.ParseQC(text,{base='studiomdl',volumeWeighting=d.model.massMode=='volume'},d)
  local a,am=P.Effective(d) local b,bm=P.Effective(back)
  local function differ(what) failures[#failures+1]=n..': '..what end
  for i=0,17 do
   for _,f in ipairs({'massBias','damping','rotdamping','inertia'}) do if P.Quantize(a[i][f],3)~=P.Quantize(b[i][f],3) then differ(i..' '..f..' '..tostring(a[i][f])..' '..tostring(b[i][f])) end end
   if a[i].surfaceprop~=b[i].surfaceprop then differ(i..' surfaceprop') end
   if i>0 then for _,axis in ipairs(P.AXES) do local x,y=a[i].limits[axis],b[i].limits[axis] if x[1]~=y[1] or x[2]~=y[2] or P.Quantize(x[3],3)~=P.Quantize(y[3],3) then differ(i..axis..' '..x[1]..','..x[2]..','..x[3]..' vs '..y[1]..','..y[2]..','..y[3]) end end end
  end
  local drag=a[0].drag local uniform=true for i=0,17 do if a[i].drag~=drag then uniform=false end end
  if uniform then for i=0,17 do if a[i].drag~=b[i].drag then differ('drag '..i) end end end
  local ca,cb=P.Canonical(a,am),P.Canonical(b,bm)
  if tostring(ca.surfaceprop)~=tostring(cb.surfaceprop) or tostring(ca.massMode)~=tostring(cb.massMode) or (ca.automass==nil)~=(cb.automass==nil) then differ('model') end
  local function rules(c) local r=c.collisions if not r then return 'all' end if r.mode=='none' then return 'none' end local s={} for _,p in ipairs(r.pairs) do s[#s+1]=p[1]..','..p[2] end return table.concat(s,' ') end
  if rules(ca)~=rules(cb) then differ('collisions '..rules(ca)..' / '..rules(cb)) end
  local x,y=ca.animatedFriction,cb.animatedFriction
  if (x==nil)~=(y==nil) or (x and (x.min~=y.min or x.max~=y.max or x.timeIn~=y.timeIn or x.timeOut~=y.timeOut or x.timeHold~=y.timeHold)) then differ('animated friction') end
  if not d.model.automass and back.mass~=d.mass then differ('mass') end
 end
 return table.concat(failures,'\n'),#failures
end'''
FILE_MOCK = r'''
file={Open=function(path,mode,where)
 OPENED=path..'|'..mode..'|'..where
 local data=PHY local at=0
 return {ReadLong=function() local a,b,c,d=data:byte(at+1,at+4) at=at+4 local v=a+b*256+c*65536+d*16777216 if v>=2147483648 then v=v-4294967296 end return v end,
  Skip=function(_,n) at=at+n end,Read=function(_,n) local s=data:sub(at+1,at+n) at=at+n return s end,Size=function() return #data end,Tell=function() return at end,Close=function() CLOSED=true end}
end}
'''

# ---- L7: QC import and export ----
parse = lua.eval('function(text,base,volume) local d,r=mmdhl.physics.ParseQC(text,{base=base,volumeWeighting=volume},mmdhl.physics.NewDraft()) return (mmdhl.physics.Effective(d)),d,r end')
def rows(report): return [(r.line, r.status, r.command, r.reason) for r in report.rows.values()]
qc = '''$collisionjoints "phymodel.smd"
{
 $mass 62.5
 $damping 0.3 // a comment
 $jointconstrain "ValveBiped.Bip01_L_Forearm" z limit -100 10 3
 $jointconstrain "ValveBiped.Bip01_L_Forearm" x limit -20 20
 $jointconstrain "ValveBiped.Bip01_L_Forearm" z limit -50 50 1
 $jointconstrain "ValveBiped.Bip01_L_Hand" y limit 30 -30 0
 $jointconstrain "ValveBiped.Bip01_L_Hand" x limit
   -10 10
 $jointconstrain "ValveBiped.Bip01_Spine2" x limit -10 10 0
 $jointconstrain "hair_01" x limit -10 10 0
 $jointconstrain "Bip01_R_Hand" z free -360 360 2
 $jointrotdamping "ValveBiped.Bip01_Head1" 6
 $noselfcollisions
 $jointcollide "ValveBiped.Bip01_Head1" "ValveBiped.Bip01_L_Hand"
 $animatedfriction 80 600 0.15 1.75 0.25
 /* $jointmassbias "ValveBiped.Bip01_Pelvis" 50 */
 $jointskip "ValveBiped.Bip01_L_Clavicle"
}
$collisionmodel "prop.smd"
$surfaceprop "metal"
'''
b, d, report = parse(qc, 'studiomdl', False)
r = rows(report)
assert list(b[6].limits.z.values()) == [-100, 10, .6], 'friction is QC / 5'
assert list(b[6].limits.x.values()) == [-20, 20, .2], 'an omitted friction is QC 1.0'
assert (7, 'ignored', '$jointconstrain', 'qc.reason.first_wins') in r and list(b[6].limits.z.values())[0] == -100
assert (8, 'error', '$jointconstrain', 'qc.reason.order') in r
assert (9, 'error', '$jointconstrain', 'qc.reason.args') in r and list(b[7].limits.x.values()) == [0, 0, 0], 'arguments on the next line were consumed'
assert any(x[0] == 11 and x[3] == 'qc.reason.not_carrier_body' for x in r) and any(x[0] == 12 and x[3] == 'qc.reason.not_carrier_body' for x in r)
assert list(b[11].limits.z.values()) == [-360, 360, .4] and (13, 'applied', '$jointconstrain', 'qc.reason.prefix') in r
assert list(b[6].limits.y.values()) == [0, 0, 0] and list(b[1].limits.x.values()) == [0, 0, 0], 'studiomdl fixes the axes a QC leaves out'
assert b[3].rotdamping == 6 and b[0].damping == .3 and b[0].rotdamping == 0 and b[0].inertia == 1 and b[0].surfaceprop == 'metal'
assert d.model.collisions.mode == 'none' and (16, 'ignored', '$jointcollide', 'qc.reason.no_self') in r
a = d.model.animatedFriction
assert (a.min, a.max, a.timeIn, a.timeHold, a.timeOut) == (80, 600, .15, 1.75, .25), 'studiomdl reads min max timein timehold timeout'
assert b[0].massBias == 1 and d.mass == 62.5 and (19, 'ignored', '$jointskip', 'qc.reason.unsupported') in r and (21, 'error', '$collisionmodel', 'qc.reason.not_jointed') in r
b2, d2, report2 = parse('$animatedfriction 80 600 0.15 1.75\n$jointconstrain "ValveBiped.Bip01_L_Hand" z limit -5 5 0\n', 'merge', True)
assert (1, 'ignored', '$animatedfriction', 'qc.reason.args') in rows(report2) and d2.model.animatedFriction is None
assert list(b2[7].limits.x.values()) == [-25, 25, 0] and list(b2[7].limits.z.values()) == [-5, 5, 0] and d2.model.massMode == 'volume', 'merge keeps the axes a QC leaves out'
# Round trip: every QC-expressible value survives EmitQC then ParseQC from studiomdl's base.
roundtrip = lua.eval(ROUNDTRIP)
failures, n = roundtrip(5)
assert n == 0, failures[:2000]
text = lua.eval('function() local P=mmdhl.physics local d=P.NewDraft() P.SetExplicit(d,3,"drag",1) return P.EmitQC(d,{name="A",key="k",version="v"}) end')()
assert '$jointconstrain "ValveBiped.Bip01_L_Forearm" y limit 0 0 0' in text and '// Not expressible in QC: per-part air drag (ValveBiped.Bip01_Head1 1)' in text and '$mass 70' in text and '-0' not in text
print('PASS: QC import (friction / 5, omitted friction, first wins, studiomdl and merge bases, same-line arguments, self-collision, animated friction order, cloth bones, $collisionmodel) and 100 seeded export/import round trips')

# ---- L8: a template from an installed model's .phy text ----
template = lua.eval('''function(text) local P=mmdhl.physics local phy=P.ParsePhyText(text)
 local d,report=P.FromTemplate(phy,{limits=true,friction=true,body=true,mass=true,total=true,collisions=true,animated=true},P.NewDraft())
 return phy,d,report,(P.Effective(d)) end''')
phy, d, report, b = template(read('tests/fixtures/physics/hl2_citizen_phy.txt'))
assert len(phy.solids) == 15 and len(phy.constraints) == 14 and phy.rules.selfcollisions
assert report.solids == 15 and report.bodies == 16 and report.joints == 15 and report.split and sorted(report.kept.values()) == [4, 8]
assert list(b[1].limits.x.values()) == [-10, 10, .4] and list(b[2].limits.z.values()) == [-20, 18, .4], 'the shared Spine2 range is split between Spine1 and Spine4'
assert abs(b[1].massBias / b[2].massBias - 8 / 9) < 1e-3, "Spine1 and Spine4 share Spine2's weight 8:9"
assert b[4].massBias == 4 and b[8].massBias == 4 and list(b[4].limits.z.values()) == [0, 15, 0], 'unmapped clavicles keep their defaults'
assert abs(sum(b[i].massBias for i in range(18)) - 74) < .01, 'mapped bodies share what the defaults gave them'
assert d.model.collisions.mode == 'none' and d.mass == 90 and d.model.animatedFriction.timeHold == 1.5 and d.feel.preset == 'template'
assert list(b[6].limits.z.values()) == [-140, 5, .2] and list(b[6].limits.y.values()) == [0, 0, 0] and b[6].rotdamping == 4 and b[6].damping == 0 and b[6].inertia == 10
missing = lua.eval('function() return mmdhl.physics.ParsePhyText("") end')()
assert missing == (None, 'template.missing')
print('PASS: a 15-solid HL2 template maps 16 of 18 parts and 15 joints, splits Spine2 8:9 with half ranges, keeps the clavicles and copies the rules')

# ---- L9: the .phy file fallback ----
text_section = 'solid {\n"index" "0"\n"name" "ValveBiped.Bip01_Pelvis"\n"mass" "10"\n}\n'
binary = struct.pack('<iiii', 16, 0, 2, 1234) + struct.pack('<i', 8) + b'\x01' * 8 + struct.pack('<i', 4) + b'\x02' * 4 + text_section.encode() + b'\x00' + b'tail'
reader = runtime()
reader.globals().PHY = binary
reader.execute(FILE_MOCK)
assert reader.eval('mmdhl.physics.ReadPhyFile("models/test/model.mdl")') == text_section
assert reader.eval('OPENED') == 'models/test/model.phy|rb|GAME' and reader.eval('CLOSED')
reader.globals().PHY = struct.pack('<iiii', 20, 0, 2, 0)
assert reader.eval('mmdhl.physics.ReadPhyFile("models/test/model.mdl")') is None
print('PASS: ReadPhyFile skips the binary solids of a .phy and returns its text section; a bad header gives nil')


# ---- The server: permissions, limits, operations, saved defaults, replace in place ----
EDITOR = read('addon/lua/mmdhl/physics_editor.lua')
SERVER_LUA = read('addon/lua/mmdhl/server.lua')
CHECK = LuaRuntime()  # definition() compiles with the newer load(string)
# server.lua's saved pins and the bone window's rules (which pins the shapes depend on),
# which the editor uses.
RULES = read('addon/lua/mmdhl/bone_mapper_rules.lua')
PINS = definition(CHECK, SERVER_LUA, 'function mmdhl.SavedBoneMap') + chr(10) + RULES


def json_bridge(rt):
    def encode(value):
        def conv(v):
            if hasattr(v, 'keys') and hasattr(v, 'values'):
                keys = list(v.keys())
                if not keys: return []
                if all(isinstance(k, (int, float)) and k == int(k) for k in keys) and sorted(int(k) for k in keys) == list(range(1, len(keys) + 1)):
                    return [conv(v[k]) for k in range(1, len(keys) + 1)]
                return {str(int(k)) if isinstance(k, float) and k == int(k) else str(k): conv(x) for k, x in v.items()}
            return v
        return json.dumps(conv(value))
    def decode(text):
        try: data = json.loads(text)
        except Exception: return None
        def back(v):
            if isinstance(v, dict): return rt.table_from({k: back(x) for k, x in v.items()})
            if isinstance(v, list): return rt.table_from([back(x) for x in v])
            return v
        return back(data)
    rt.globals().py_encode = encode
    rt.globals().py_decode = decode


SERVER_MOCKS = r'''
SERVER=true CLIENT=false NOW=100 CurTime=function() return NOW end SysTime=CurTime
-- As in GMod: a table is valid only through its own IsValid method.
IsValid=function(v) if type(v)~='table' then return false end local f=v.IsValid if not f then return false end return f(v) end
isstring=function(v) return type(v)=='string' end istable=function(v) return type(v)=='table' end isfunction=function(v) return type(v)=='function' end isnumber=function(v) return type(v)=='number' end
local function deep(t) if type(t)~='table' then return t end local c={} for k,v in pairs(t) do c[k]=deep(v) end return setmetatable(c,getmetatable(t)) end
-- As in GMod: table.Copy takes a table (or nil) and errors on anything else.
table.Copy=function(t) if t==nil then return nil end if type(t)~='table' then error("bad argument #1 to 'pairs' (table expected, got "..type(t)..")",2) end return deep(t) end table.Count=function(t) local n=0 for _ in pairs(t or {}) do n=n+1 end return n end
math.Clamp=function(v,lo,hi) return math.min(math.max(v,lo),hi) end
ERRORS={} ErrorNoHalt=function(s) ERRORS[#ERRORS+1]=tostring(s) end MsgN=function(s) LOGGED=(LOGGED or '')..tostring(s)..'\n' end
FCVAR_ARCHIVE=1 FCVAR_REPLICATED=2 FCVAR_NOTIFY=4
CONVARS={} CreateConVar=function(name,default) local c={value=default} function c:GetInt() return math.floor(tonumber(self.value) or 0) end function c:GetFloat() return tonumber(self.value) or 0 end function c:GetBool() return (tonumber(self.value) or 0)~=0 end CONVARS[name]=c return c end
GetConVar=function(name) return CONVARS[name] end
GLOBALS={} SetGlobal2Int=function(k,v) GLOBALS[k]=v end
HOOKS={} hook={Add=function(e,n,f) HOOKS[e]=HOOKS[e] or {} HOOKS[e][n]=f end,Remove=function(e,n) if HOOKS[e] then HOOKS[e][n]=nil end end,
 Run=function(e,...) for _,f in pairs(HOOKS[e] or {}) do local r=f(...) if r~=nil then return r end end end}
SINGLE=false DEDICATED=true game={SinglePlayer=function() return SINGLE end,IsDedicated=function() return DEDICATED end}
-- file.Write takes only some extensions; WRITE_FAIL makes writes to matching paths fail.
FILES={} REFUSED={} file={Read=function(p) return FILES[p] end,Write=function(p,c) if not (p:match('%.txt$') or p:match('%.json$') or p:match('%.dat$')) then REFUSED[#REFUSED+1]=p return end if WRITE_FAIL and p:find(WRITE_FAIL,1,true) then return end FILES[p]=c end,Delete=function(p) FILES[p]=nil end,Exists=function(p) return FILES[p]~=nil end,CreateDir=function() end,
 Rename=function(a,b) if FILES[b]~=nil or FILES[a]==nil then return false end FILES[b]=FILES[a] FILES[a]=nil return true end}
TIMERS={} timer={Create=function(name,_,_,f) TIMERS[name]=f end,Remove=function(name) TIMERS[name]=nil end,Simple=function(_,f) f() end}
function tick(n) for _=1,n or 1 do NOW=NOW+.05 local list={} for name,f in pairs(TIMERS) do list[#list+1]=f end for _,f in ipairs(list) do f() end end end
KNOWN_SURFACES={flesh=true,metal=true,wood=true,rubber=true}
util={AddNetworkString=function() end,TableToJSON=function(t) return py_encode(t) end,JSONToTable=function(s) if type(s)~='string' then return nil end return py_decode(s) end,
 Compress=function(s) return 'Z'..s end,Decompress=function(s,max) if type(s)=='string' and s:sub(1,1)=='Z' and (not max or #s-1<=max) then return s:sub(2) end end,
 GetSurfaceIndex=function(n) return KNOWN_SURFACES[n] and 1 or -1 end}
RECEIVERS={} SENT={} local inbox={} local writing
function feed(list) inbox=list end
local function read() return table.remove(inbox,1) end
net={Receive=function(n,f) RECEIVERS[n]=f end,Start=function(n) writing={name=n,fields={}} end,ReadUInt=read,ReadString=read,ReadEntity=read,ReadData=function() return read() end,
 Send=function(p) writing.to=p SENT[#SENT+1]=writing end,Broadcast=function() SENT[#SENT+1]=writing end}
for _,name in ipairs({'WriteUInt','WriteString','WriteEntity','WriteData','WriteBool','WriteFloat'}) do net[name]=function(v) writing.fields[#writing.fields+1]=v end end
local V={} V.__index=V
function Vector(x,y,z) return setmetatable({x=x or 0,y=y or 0,z=z or 0},V) end
V.__add=function(a,b) return Vector(a.x+b.x,a.y+b.y,a.z+b.z) end V.__sub=function(a,b) return Vector(a.x-b.x,a.y-b.y,a.z-b.z) end V.__mul=function(a,s) return Vector(a.x*s,a.y*s,a.z*s) end
V.__eq=function(a,b) return a.x==b.x and a.y==b.y and a.z==b.z end
function V:Unpack() return self.x,self.y,self.z end
function V:DistToSqr(o) return (self.x-o.x)^2+(self.y-o.y)^2+(self.z-o.z)^2 end
local A={} A.__index=A
function Angle(p,y,r) return setmetatable({p=p or 0,y=y or 0,r=r or 0},A) end
function A:Unpack() return self.p,self.y,self.r end
function A:Right() local y=math.rad(self.y) return Vector(math.sin(y),-math.cos(y),0) end
constraint={GetTable=function(e) return e.constraints or {} end}
MADE={} duplicator={CreateConstraintFromTable=function(c,map,p) if c.fail then error('constraint failed') end MADE[#MADE+1]={c=c,map=map} return {} end}
UNDO={} undo={ReplaceEntity=function(a,b) UNDO[#UNDO+1]={a,b} end} CLEAN={} cleanup={ReplaceEntity=function(a,b) CLEAN[#CLEAN+1]={a,b} end}
GAMEMODE={} CALLED={} gamemode={Call=function(name,...) CALLED[#CALLED+1]=name local f=GAMEMODE[name] if f then return f(...) end end}
ENTS={} Entity=function(i) return ENTS[i] or NULL end NULL={removed=true,IsValid=function() return false end}
local nextIndex=10
function physobj(i)
 local o={pos=Vector(i,0,0),ang=Angle(0,i,0),motion=true,vel=Vector(0,0,i),angvel=Vector(i,0,0),asleep=false}
 function o:IsValid() return true end
 function o:GetPos() return self.pos end function o:GetAngles() return self.ang end function o:SetPos(v) self.pos=v end function o:SetAngles(a) self.ang=a end
 function o:IsMotionEnabled() return self.motion end function o:EnableMotion(v) self.motion=v end function o:GetVelocity() return self.vel end function o:SetVelocity(v) self.vel=v end
 function o:GetAngleVelocity() return self.angvel end function o:AddAngleVelocity(v) self.angvel=self.angvel+v end function o:IsAsleep() return self.asleep end function o:Sleep() self.asleep=true end function o:Wake() self.asleep=false end
 return o
end
local E={} E.__index=function(t,k) local f=rawget(E,k) if f then return f end if type(k)=='string' and k:match('^%u') and not k:match('^MMD') then return function(self,...) self.calls=self.calls or {} self.calls[#self.calls+1]=k end end end
function E:IsValid() return not self.removed end function E:GetClass() return self.class end function E:GetPhysicsObjectCount() return 18 end function E:GetPhysicsObjectNum(i) return self.objs[i] end
function E:GetNW2String(k,d) local v=self.nw[k] if v==nil then return d end return v end E.SetNW2String=function(self,k,v) self.nw[k]=v end
E.GetNW2Bool=E.GetNW2String E.SetNW2Bool=E.SetNW2String
function E:GetPos() return self.pos end function E:GetAngles() return self.ang end function E:BoundingRadius() return 40 end function E:EntIndex() return self.index end
function E:GetCreator() return self.creator end function E:SetCreator(p) self.creator=p end function E:Remove() self.removed=true end
function E:GetBodyGroups() return {{id=1},{id=2}} end function E:GetBodygroup(id) return self.groups[id] or 0 end function E:SetBodygroup(id,v) self.groups[id]=v end
function E:GetModel() return 'models/mmd/'..self.nw.MMDHLRig:sub(1,16)..'/m.mdl' end function E:GetSkin() return 2 end
RIGS={}
function ragdoll(key,options,owner)
 nextIndex=nextIndex+1 local e=setmetatable({class='prop_ragdoll',nw={MMDHLRig=key},pos=Vector(1,2,3),ang=Angle(0,45,0),objs={},creator=owner,MMDOptions=options or {},asset=string.rep('a',64),index=nextIndex,groups={[1]=1}},E)
 for i=0,17 do e.objs[i]=physobj(i) end ENTS[nextIndex]=e return e
end
function player(admin,super,host,name)
 local p={admin=admin,super=super,host=host,name=name or 'P'}
 function p:IsValid() return not self.removed end function p:IsAdmin() return self.admin==true or self.super==true end function p:IsSuperAdmin() return self.super==true end function p:IsListenServerHost() return self.host==true end
 function p:SteamID64() return '7656119800000000'..(self.name=='Admin' and '1' or '2') end function p:Nick() return self.name end function p:EyeAngles() return Angle(10,90,0) end
 return p
end
mmdhl={native={GetCapabilities=function() return py_encode({physicsEditor=LEVEL}) end}}
LEVEL=1
function mmdhl.Decode(v,err) if v==nil then return nil,err end if type(v)=='string' then return util.JSONToTable(v) end return v end
mmdhl.IsMMD=function(e) return type(e)=='table' and (e.class=='prop_ragdoll') end
mmdhl.GetAsset=function(e) return e.asset end
mmdhl.GetRig=function(e) return RIGS[e:GetNW2String('MMDHLRig','')] end
EDITABLE=function() return true end mmdhl.CanEdit=function(p,e,prop) return EDITABLE(p,e,prop) end
BOUND={} mmdhl.CaptureNativeState=function(e) return {asset=e.asset,from=e.index} end mmdhl.BindEntity=function(e,s) BOUND[#BOUND+1]={e,s} return true end mmdhl.StoreNativeState=function() end
SPAWNS={} SPAWN_FAIL=nil
mmdhl.Spawn=function(p,asset,o,done,progress,flags)
 -- As server.lua's: every fit takes the saved pins, whatever the options carry.
 o.boneMap=mmdhl.SavedBoneMap and mmdhl.SavedBoneMap(asset) or nil
 SPAWNS[#SPAWNS+1]={p=p,asset=asset,options=o,flags=flags}
 if SPAWN_FAIL then done(nil,SPAWN_FAIL) return end
 local n=#SPAWNS
 -- DEFER holds the build until DEFERRED() runs.
 local function made() local key=string.format('%032x',n+4096) RIGS[key]={scale=3.23656,materialCount=4,physicsOverrides=o.physicsOverrides,bones=BONES,bodies={}}
  local e=ragdoll(key,deep(o),p) LAST_SPAWNED=e done(e) end
 if DEFER then DEFERRED=made else made() end
end
'''
SERVER_SETUP = r'''
BONES={} for i,name in ipairs(mmdhl.physics.BODIES) do BONES[i]={name=name,position=({[4]={0,0,60},[15]={0,5,-6},[18]={0,-5,-6}})[i] or {0,0,0},rotation={0,0,0,1}} end
KEY=string.rep('b',32) RIGS[KEY]={scale=3.23656,materialCount=4,bones=BONES,bodies={}}
function request(p,op,ent,payload,protocol,n)
 local data=payload and ('Z'..py_encode(payload)) or ''
 feed({protocol or 1,#SENT+1,op,ent,n or #data,data}) RECEIVERS.mmdhl_physics(0,p)
end
function replies(p) local out={} for _,m in ipairs(SENT) do if m.name=='mmdhl_physics_status' and m.to==p then local f=m.fields out[#out+1]={state=f[2],message=f[3],ent=f[4],data=f[6] and util.JSONToTable(f[6]:sub(2)) or nil} end end return out end
function last(p) local r=replies(p) return r[#r] end
function token(key) return mmdhl.I18n.Token(key) end
function says(reply,key) return reply and reply.message:find(key,1,true)~=nil end
'''


def server_runtime():
    rt = lua51.LuaRuntime(unpack_returned_tuples=True)
    json_bridge(rt)
    rt.execute(SERVER_MOCKS)
    attach(rt)
    rt.execute('AddCSLuaFile=function() end include=function() end')
    rt.execute(PINS)
    rt.execute(PROFILE)
    rt.execute(EDITOR)
    rt.execute(SERVER_SETUP)
    return rt


# L10: who may do what.
s = server_runtime()
s.execute(r'''
local P=mmdhl.physics local owner,other,admin,host=player(false,false,false,'Owner'),player(false,false,false,'Other'),player(true,false,false,'Admin'),player(false,false,true,'Host')
local ent=ragdoll(KEY,{},owner)
EDITABLE=function(p,e) return p~=other end
local function can(p,op) local ok,why=P.Can(p,ent,op) return ok==true,why end
assert(GLOBALS.MMDHLPhysicsEditor==1,'the server level is not published')
-- Dedicated servers default to admins only.
assert(CONVARS.mmdhl_physics_editor.value=='1')
assert(select(2,can(owner,'apply'))=='physics_editor.error.admin_only' and can(admin,'apply'),'mode 1 must allow admins only')
CONVARS.mmdhl_physics_editor.value='2'
assert(can(owner,'apply') and can(owner,'test') and select(2,can(other,'apply'))=='physics_editor.error.not_allowed','mode 2 follows who may edit the ragdoll')
assert(select(2,can(owner,'save_default'))=='physics_editor.error.admin_only' and can(admin,'save_default') and can(host,'save_default'),'saving defaults needs an admin or the listen host')
assert(can(other,'open') and can(other,'close'),'everyone may look')
SINGLE=true assert(can(owner,'save_default'),'single player saves') SINGLE=false
hook.Add('MMDHLCanSavePhysicsDefault','test',function() return true end) assert(can(owner,'save_default'),'the save hook can allow')
hook.Add('MMDHLCanSavePhysicsDefault','test',function() return false end) assert(not can(admin,'save_default'),'the save hook can deny') hook.Remove('MMDHLCanSavePhysicsDefault','test')
hook.Add('MMDHLCanEditPhysics','test',function(p,e,op) if op=='apply' then return false end end) assert(select(2,can(admin,'apply'))=='physics_editor.error.not_allowed' and can(admin,'test')) hook.Remove('MMDHLCanEditPhysics','test')
CONVARS.mmdhl_physics_editor.value='0' assert(select(2,can(admin,'apply'))=='physics_editor.error.disabled' and can(admin,'open')) CONVARS.mmdhl_physics_editor.value='2'
-- A non-admin's forced save is refused on the wire too.
request(owner,'save_default',ent,{base=KEY}) assert(last(owner).state=='error' and says(last(owner),'physics_editor.error.admin_only') and FILES[P.SavedPath(ent.asset)]==nil)
-- Open answers everyone, with what they may do.
request(other,'open',ent,{}) local st=last(other) assert(st.state=='state' and st.data.base==KEY and st.data.canEdit==false and st.data.canSave==false and st.data.level==1)
request(admin,'open',ent,{}) assert(last(admin).data.canEdit==true and last(admin).data.canSave==true)
''')
print('PASS: permissions follow mmdhl_physics_editor (0/1/2), prop protection, the edit and save hooks; saving defaults needs an admin, the listen host or single player')

# L11: busy, stale, held, rate limits, payload size, unknown operations.
s = server_runtime()
s.execute(r'''
CONVARS.mmdhl_physics_editor.value='2'
local p=player(false,false,false,'Owner') local ent=ragdoll(KEY,{mass=70},p)
local req={collisionOverrides={},mass=80,physicsOverrides={schema=1,bodies={['ValveBiped.Bip01_L_Hand']={damping=2}}},physicsEditor={schema=1,feel={preset='custom',stiffness=0,range=0,floatiness=0}}}
request(p,'apply',ent,{base=string.rep('c',32),request=req}) assert(says(last(p),'physics_editor.error.stale'),'a stale base was built')
ent.MMDHLPhysicsBusy=true request(p,'apply',ent,{base=KEY,request=req}) assert(says(last(p),'physics_editor.error.busy')) ent.MMDHLPhysicsBusy=nil
p.MMDHLPhysicsBusy=true request(p,'test',ent,{base=KEY,request=req}) assert(says(last(p),'physics_editor.error.busy')) p.MMDHLPhysicsBusy=nil
ent:SetNW2Bool('MMDHLPhysgunHeld',true) request(p,'apply',ent,{base=KEY,request=req}) assert(says(last(p),'physics_editor.error.held')) ent:SetNW2Bool('MMDHLPhysgunHeld',false)
request(p,'explode',ent,{}) assert(says(last(p),'physics_editor.error.invalid'),'an unknown operation was not refused')
request(p,'apply',ent,{base=KEY,request=req},2) assert(says(last(p),'physics_editor.error.invalid'),'an unknown protocol was not refused')
request(p,'apply',ent,{base=KEY,request=req},1,60001) assert(says(last(p),'physics_editor.error.too_large'))
assert(#SPAWNS==0,'a refused request built something')
-- One build: building, then ready with the new ragdoll's state; flags cleared.
request(p,'apply',ent,{base=KEY,request=req})
local r=replies(p) assert(r[#r-1].state=='building' and r[#r].state=='ready' and says(r[#r],'physics_editor.notice.applied'),'apply did not answer building then ready')
local new=ENTS[r[#r].ent] assert(new and new~=ent and ent.removed and not new.removed,'the ragdoll was not replaced')
assert(r[#r].data.base==new:GetNW2String('MMDHLRig') and r[#r].data.hasPrevious==true,'the reply does not describe the new ragdoll')
assert(not p.MMDHLPhysicsBusy and not new.MMDHLPhysicsBusy)
local o=SPAWNS[1].options assert(o.mass==80 and o.physicsOverrides.bodies['ValveBiped.Bip01_L_Hand'].damping==2 and o.frozen==true and o.role=='ragdoll' and o.backend=='source' and o.rigManifest==nil and SPAWNS[1].flags.replace==true)
-- The cooldown, then the budget (superadmins are exempt).
local base=new:GetNW2String('MMDHLRig')
request(p,'apply',new,{base=base,request=req}) assert(says(last(p),'physics_editor.error.rate_limited') and last(p).data.seconds==3,'the cooldown did not hold')
local count=#SPAWNS
for k=1,30 do NOW=NOW+3.01 request(p,'test',new,{base=base,request=req}) end
local built=#SPAWNS-count
assert(built==19,'the budget allowed '..built..' builds in 10 minutes')
assert(says(last(p),'physics_editor.error.rate_limited'))
NOW=NOW+601 request(p,'test',new,{base=base,request=req}) assert(last(p).state=='ready','the budget window did not move on')
local boss=player(true,true,false,'Boss') for k=1,25 do NOW=NOW+3.01 request(boss,'test',new,{base=base,request=req}) end
assert(last(boss).state=='ready','superadmins are exempt from the budget')
-- Test copies: one per player; the next replaces it; closing removes it.
local copies={} for _,m in ipairs(replies(boss)) do if m.state=='ready' then copies[#copies+1]=ENTS[m.ent] end end
for k=1,#copies-1 do assert(copies[k].removed,'an older test copy stayed') end
assert(not copies[#copies].removed and copies[#copies]:GetNW2Bool('MMDHLPhysicsTestCopy'))
request(boss,'close',new,{}) assert(copies[#copies].removed,'closing the editor kept the test copy')
NOW=NOW+10 request(boss,'test',new,{base=base,request=req}) local copy=ENTS[last(boss).ent] HOOKS.PlayerDisconnected['MMDHL.PhysicsTestCopy'](boss) assert(copy.removed,'a disconnect kept the test copy')
-- The ragdoll limit applies to a first test copy.
GAMEMODE.PlayerSpawnRagdoll=function() return false end NOW=NOW+10
local q=player(false,false,false,'Limited') request(q,'test',new,{base=base,request=req}) assert(says(last(q),'physics_editor.error.spawn_limit'))
GAMEMODE.PlayerSpawnRagdoll=nil
-- A test copy that finishes building after its editor closed is removed at once; the next one stays.
local tester=player(true,true,false,'Tester') DEFER=true request(tester,'test',new,{base=base,request=req}) assert(last(tester).state=='building')
request(tester,'close',new,{}) DEFER=false DEFERRED()
assert(LAST_SPAWNED.removed and tester.MMDHLPhysicsTestCopy==nil and not tester.MMDHLPhysicsBusy and not new.MMDHLPhysicsBusy,'a test copy built after closing the editor stayed')
NOW=NOW+10 request(tester,'test',new,{base=base,request=req}) assert(last(tester).state=='ready' and not ENTS[last(tester).ent].removed,'the next test copy was removed too')
-- Opening is answered at most four times a second per player.
local viewer=player(false,false,false,'Viewer')
for k=1,4 do request(viewer,'open',new,{}) assert(last(viewer).state=='state','open '..k..' was refused') end
request(viewer,'open',new,{}) assert(says(last(viewer),'physics_editor.error.rate_limited') and last(viewer).data.seconds==1,'a fifth open within a second was answered')
NOW=NOW+1.01 request(viewer,'open',new,{}) assert(last(viewer).state=='state','opening stayed refused')
''')
print('PASS: stale, busy, held, unknown operations and protocols, payloads over 60000 bytes; 3 s cooldown and 20 builds per 10 minutes (superadmins exempt); one test copy per player, none after its editor closed; at most 4 opens a second')

# L12: an older server module builds shapes only.
s = server_runtime()
s.execute(r'''
CONVARS.mmdhl_physics_editor.value='2'
local p=player(false,false,false,'Owner') local saved={schema=1,massMode='volume'} local ent=ragdoll(KEY,{physicsOverrides=saved},p)
local req={collisionOverrides={['ValveBiped.Bip01_L_Hand']={center={1,0,0},extent={1,1,1},style='capsule'}},mass=70,physicsOverrides={schema=1,bodies={['ValveBiped.Bip01_L_Hand']={damping=2}}},physicsEditor={schema=1,feel={preset='custom',stiffness=0,range=0,floatiness=0}}}
LEVEL=0 request(p,'apply',ent,{base=KEY,request=req})
local o=SPAWNS[1].options local r=last(p)
assert(r.state=='ready' and o.collisionOverrides['ValveBiped.Bip01_L_Hand'].style==nil and o.collisionOverrides['ValveBiped.Bip01_L_Hand'].center[1]==1,'level 0 must keep the shape and drop its style')
assert(o.physicsOverrides==saved or (o.physicsOverrides and o.physicsOverrides.massMode=='volume'),'level 0 must keep the ragdoll\'s own profile for a later update')
local warned=false for _,w in ipairs(r.data.warnings) do warned=warned or w:find('physics_editor.notice.server_shapes_only',1,true)~=nil end assert(warned,'no shapes-only warning')
LEVEL=1 NOW=NOW+10 local new=ENTS[r.ent] request(p,'apply',new,{base=new:GetNW2String('MMDHLRig'),request=req})
o=SPAWNS[2].options assert(o.collisionOverrides['ValveBiped.Bip01_L_Hand'].style=='capsule' and o.physicsOverrides.bodies['ValveBiped.Bip01_L_Hand'].damping==2,'level 1 must pass physics and styles through')
-- The server validates what the client sent.
NOW=NOW+10 new=ENTS[last(p).ent]
local bad={collisionOverrides={},mass=70,physicsOverrides={schema=1,bodies={['ValveBiped.Bip01_L_Hand']={surfaceprop='plasma'}}}}
request(p,'apply',new,{base=new:GetNW2String('MMDHLRig'),request=bad}) assert(says(last(p),'physics_editor.issue.surfaceprop_unknown'),'an unknown surface material was built')
bad.physicsOverrides={schema=1,bodies={['ValveBiped.Bip01_L_Hand']={damping=200}}}
request(p,'apply',new,{base=new:GetNW2String('MMDHLRig'),request=bad}) assert(says(last(p),'physics_editor.error.invalid') and last(p).data.reason=='damping_range')
assert(#SPAWNS==2)
''')
print('PASS: an older server module gets shapes only (styles and physics kept out, with a warning); a current one gets them; the server validates every request')

# L16: replace in place.
s = server_runtime()
s.execute(r'''
CONVARS.mmdhl_physics_editor.value='2'
local owner,editor=player(false,false,false,'Owner'),player(false,false,false,'Friend')
local ent=ragdoll(KEY,{mass=70,collisionOverrides={},physicsOverrides={}},owner)
ent.objs[3].motion=false ent.objs[5].asleep=true
local prop={index=900} ENTS[900]=prop
ent.constraints={{Type='Weld',Entity={{Index=ent.index,Entity=ent,Bone=5},{Index=900,Entity=prop,Bone=0}}},{Type='Rope',Entity={{Index=ent.index,Entity=ent},{Index=0,Entity={world=true},World=true}}},{Type='Bad',fail=true,Entity={{Index=ent.index,Entity=ent}}}}
local req={collisionOverrides={},mass=90,physicsOverrides={},physicsEditor=nil}
request(editor,'apply',ent,{base=KEY,request=req})
local r=last(editor) local new=ENTS[r.ent]
assert(r.state=='ready' and ent.removed and not new.removed)
for i=0,17 do local a,b=ent.objs[i],new.objs[i] assert(a.pos==b.pos and a.ang.y==b.ang.y and a.motion==b.motion,'the pose of body '..i..' was lost') end
assert(new.objs[3].motion==false and new.objs[5].asleep==true and new.objs[7].vel==ent.objs[7].vel and new.objs[7].angvel==ent.objs[7].angvel,'motion state was lost')
assert(new.groups[1]==1 and BOUND[1][1]==new and BOUND[1][2].from==ent.index,'appearance was not carried over')
assert(#MADE==2 and MADE[1].map[ent.index]==new and MADE[1].map[900]==prop and MADE[2].map[ent.index]==new,'constraints were not rebuilt onto the new ragdoll')
local lost=false for _,w in ipairs(r.data.warnings) do lost=lost or w:find('physics_editor.notice.constraints_lost',1,true)~=nil end assert(lost,'a lost constraint was not reported')
assert(new.creator==owner,'the owner changed to the editor') assert(UNDO[1][1]==ent and UNDO[1][2]==new and CLEAN[1][1]==ent and CLEAN[1][2]==new)
local counted=false for _,c in ipairs(CALLED) do counted=counted or c=='PlayerSpawnedRagdoll' end assert(counted)
assert(#new.MMDHLPhysicsHistory==1 and new.MMDHLPhysicsHistory[1].mass==70,'the previous version was not kept')
-- Previous version restores the old options and pops the history.
NOW=NOW+10 request(editor,'previous',new,{base=new:GetNW2String('MMDHLRig')})
local back=ENTS[last(editor).ent] assert(back and SPAWNS[2].options.mass==70 and #back.MMDHLPhysicsHistory==0 and new.removed,'Previous version did not restore')
NOW=NOW+10 request(editor,'previous',back,{base=back:GetNW2String('MMDHLRig')}) assert(says(last(editor),'physics_editor.error.no_previous'))
-- Reset gives the factory options: no overrides and a real 70 kg.
NOW=NOW+10 back.MMDOptions.mass=55 back.MMDOptions.physicsOverrides={schema=1,massMode='volume'} back.MMDOptions.collisionOverrides={['ValveBiped.Bip01_Head1']={center={0,0,0},extent={1,1,1}}}
request(editor,'reset',back,{base=back:GetNW2String('MMDHLRig')}) local o=SPAWNS[3].options
assert(o.mass==70 and next(o.physicsOverrides)==nil and next(o.collisionOverrides)==nil and next(o.excludedMaterials)==nil and o.physicsEditor==nil)
-- A failure before the swap leaves the old ragdoll alone.
local current=ENTS[last(editor).ent] NOW=NOW+10
local made=0 mmdhl.BindEntity=function() error('bind failed') end
request(editor,'apply',current,{base=current:GetNW2String('MMDHLRig'),request=req})
local failed=last(editor) assert(failed.state=='error' and not current.removed,'the old ragdoll was removed after a failed swap')
assert(LAST_SPAWNED~=current and LAST_SPAWNED.removed,'the half-made ragdoll stayed')
-- A build failure is translated for the player.
mmdhl.BindEntity=function() return true end NOW=NOW+10 SPAWN_FAIL='VPhysics distorted body 7 by 0.5 Source units'
request(editor,'apply',current,{base=current:GetNW2String('MMDHLRig'),request=req}) assert(says(last(editor),'physics_editor.error.hull_rejected') and last(editor).message:find('physics_editor.part.l_hand',1,true))
NOW=NOW+10 SPAWN_FAIL='Invalid physics settings: damping_range bodies.ValveBiped.Bip01_L_Hand.damping expected 0 to 10'
request(editor,'apply',current,{base=current:GetNW2String('MMDHLRig'),request=req}) assert(says(last(editor),'physics_editor.error.invalid'))
SPAWN_FAIL=nil assert(not current.MMDHLPhysicsBusy and not editor.MMDHLPhysicsBusy)
''')
s.execute(r'''
local P=mmdhl.physics
local sub=P.Subset({mass=60,collisionOverrideScale=3.23656,collisionOverrides={a={center={1,2,3}}},physicsOverrides={schema=1}})
assert(sub.mass==60 and sub.collisionOverrideScale==3.23656 and sub.collisionOverrides.a.center[2]==2 and sub.physicsOverrides.schema==1,'history keeps number options')
local empty=P.Subset(nil) assert(empty.mass==70 and next(empty.collisionOverrides)==nil)
''')
print('PASS: Apply replaces the ragdoll in place: pose and motion, look, constraints, owner, undo and cleanup move over; Previous version and Reset; failures leave the old ragdoll and are translated')

# L13: saved per-model defaults and spawns.
s = server_runtime()
s.execute(r'''
local P=mmdhl.physics CONVARS.mmdhl_physics_editor.value='2'
local admin=player(true,false,false,'Admin')
local canonical={schema=1,bodies={['ValveBiped.Bip01_L_Forearm']={limits={z={-90,7.5,.6}}}}}
RIGS[KEY].physicsOverrides=canonical
local ent=ragdoll(KEY,{collisionOverrides={['ValveBiped.Bip01_Head1']={center={0,0,1},extent={2,2,2},style='box'}},collisionOverrideScale=3.23656,excludedMaterials={2},mass=62,physicsOverrides=canonical,
 physicsEditor={schema=1,feel={preset='less_floppy',stiffness=1,range=-1,floatiness=0}}},admin)
-- The client's payload is ignored: the file is what the ragdoll has.
request(admin,'save_default',ent,{base=KEY,request={mass=5,physicsOverrides={schema=1,massMode='volume'}}})
assert(last(admin).state=='ready' and says(last(admin),'physics_editor.notice.saved'))
local fit=util.JSONToTable(FILES[P.SavedPath(ent.asset)])
assert(fit.version==3 and fit.generator==18 and fit.mass==62 and fit.physics.bodies['ValveBiped.Bip01_L_Forearm'].limits.z[2]==7.5 and fit.physics.massMode==nil,'the saved file is not the ragdoll\'s physics')
assert(fit.bodies['ValveBiped.Bip01_Head1'].style=='box' and fit.excludedMaterials[1]==2 and fit.scale==3.23656)
assert(fit.editor.ui.feel.preset=='less_floppy' and fit.editor.savedByName=='Admin' and fit.editor.savedBy=='76561198000000001' and fit.editor.savedAt)
assert(#REFUSED==0,'file.Write was given an extension GMod refuses: '..tostring(REFUSED[1]))
assert(FILES['mmd_hotloader/fit_overrides/'..ent.asset..'.new.txt']==nil,'the temporary file stayed')
-- A failed write keeps the previous default and says so.
local before=FILES[P.SavedPath(ent.asset)] WRITE_FAIL='.new.txt'
request(admin,'save_default',ent,{base=KEY}) assert(last(admin).state=='error' and says(last(admin),'physics_editor.error.save_failed') and FILES[P.SavedPath(ent.asset)]==before,'a failed save lost the previous default')
WRITE_FAIL=nil
-- LoadSavedFit gives a spawn what it can use.
local saved,why=mmdhl.LoadSavedFit(ent.asset) assert(saved.mass==62 and saved.physics and saved.editor and saved.bodies and not why)
fit.physics={schema=1,bodies={['ValveBiped.Bip01_L_Forearm']={damping=50}}} fit.mass=900 FILES[P.SavedPath(ent.asset)]=util.TableToJSON(fit)
saved,why=mmdhl.LoadSavedFit(ent.asset) assert(saved.physics==nil and saved.mass==nil and saved.bodies and why:find('physics_editor.notice.saved_physics_invalid',1,true),'invalid saved physics and mass were used')
fit.physics=canonical fit.mass=62 FILES[P.SavedPath(ent.asset)]=util.TableToJSON(fit)
LEVEL=0 saved,why=mmdhl.LoadSavedFit(ent.asset) assert(saved.physics and why:find('physics_editor.notice.saved_physics_needs_update',1,true),'an old module must keep the profile and say so')
saved,why=mmdhl.LoadSavedFit(ent.asset) assert(saved.physics and why==nil,'the old-module notice repeats on every spawn') LEVEL=1
FILES[P.SavedPath(ent.asset)]=util.TableToJSON({version=3,generator=12,bodies={}})
saved,why=mmdhl.LoadSavedFit(ent.asset) assert(saved==nil and why:find('server.notice.old_fit_corrections',1,true))
-- Forget deletes it (admins only).
FILES[P.SavedPath(ent.asset)]=util.TableToJSON(fit)
request(admin,'clear_default',ent,{}) assert(last(admin).state=='ready' and FILES[P.SavedPath(ent.asset)]==nil)
''')
print('PASS: Save for new spawns writes the ragdoll\'s own options and canonical physics (not the client\'s), with who and when; saved files are validated; Forget deletes them')

# L13b: the bone window's pins share the saved file; the editor keeps them, previews and builds with them.
s = server_runtime()
s.execute(r'''
local P=mmdhl.physics CONVARS.mmdhl_physics_editor.value='2'
local admin=player(true,true,false,'Admin')
local calf,chest='ValveBiped.Bip01_L_Calf','ValveBiped.Bip01_Spine4'
local pins={[calf]=7}
local ent=ragdoll(KEY,{boneMap=pins,mass=62,collisionOverrides={}},admin)
local path=P.SavedPath(ent.asset)
FILES[path]=util.TableToJSON({version=3,generator=18,boneMap=pins,boneMapVersion=1,boneMapSavedAt=5})
-- A file with only pins is no saved physics: nothing to forget or restore. The preview fits with the pins.
request(admin,'open',ent,{}) local st=last(admin).data
assert(st.savedDefault.exists==false,'a file with only the bone window\'s pins is offered as saved physics')
assert(st.fitOptions.boneMap and st.fitOptions.boneMap[calf]==7,'the preview is fitted without the pins every build uses')
request(admin,'restore_saved',ent,{base=KEY}) assert(says(last(admin),'physics_editor.error.no_saved') and #SPAWNS==0,'Restore saved rebuilt from a file with only pins')
-- Save for new spawns replaces the default and keeps the pins.
request(admin,'save_default',ent,{base=KEY}) assert(last(admin).state=='ready')
local fit=util.JSONToTable(FILES[path])
assert(fit.boneMap and fit.boneMap[calf]==7 and fit.boneMapVersion==1 and fit.boneMapSavedAt==5,'Save for new spawns deleted the bone window\'s pins')
assert(fit.version==3 and fit.generator==18 and fit.mass==62 and fit.editor.savedByName=='Admin' and mmdhl.SavedBoneMap(ent.asset)[calf]==7)
NOW=NOW+1 request(admin,'open',ent,{}) assert(last(admin).data.savedDefault.exists==true)
-- Saving again replaces the whole default (a profile the ragdoll no longer has goes), still with the pins.
fit.physics={schema=1,massMode='volume'} FILES[path]=util.TableToJSON(fit)
request(admin,'save_default',ent,{base=KEY}) fit=util.JSONToTable(FILES[path]) assert(fit.physics==nil and fit.boneMap[calf]==7)
-- Forget removes the default and keeps the pins.
request(admin,'clear_default',ent,{}) fit=util.JSONToTable(FILES[path])
assert(last(admin).state=='ready' and fit,'Forget deleted the file with the pins')
assert(fit.boneMap[calf]==7 and fit.boneMapSavedAt==5 and fit.bodies==nil and fit.scale==nil and fit.excludedMaterials==nil and fit.mass==nil and fit.physics==nil and fit.editor==nil,'Forget kept the default or lost the pins')
-- An older file's corrections no longer load and are replaced; its pins stay.
FILES[path]=util.TableToJSON({version=2,generator=9,bodies={old=1},mass=10,boneMap=pins})
request(admin,'save_default',ent,{base=KEY}) fit=util.JSONToTable(FILES[path])
assert(fit.version==3 and fit.generator==18 and fit.boneMap[calf]==7 and fit.bodies.old==nil and fit.mass==62)
assert(#REFUSED==0 and FILES['mmd_hotloader/fit_overrides/'..ent.asset..'.new.txt']==nil)

-- The bones were assigned again after this ragdoll was placed: its shapes, its draft's and its
-- earlier versions' were made for the old bones. Builds that use them and saving them are refused.
local owner=player(false,false,false,'Owner')
local old,new={[chest]=5},{[chest]=6}
FILES[path]=util.TableToJSON({version=3,generator=18,boneMap=new,boneMapVersion=1})
local placed=ragdoll(KEY,{boneMap=old,collisionOverrides={[chest]={center={0,0,1},extent={2,2,2}}}},owner)
local draft={collisionOverrides={[chest]={center={0,0,1},extent={3,3,3}}},mass=70}
for _,op in ipairs({'test','apply','save_default'}) do NOW=NOW+10 request(admin,op,placed,{base=KEY,request=draft}) assert(says(last(admin),'physics_editor.error.bones_changed'),op..' used shapes made for other bones') end
assert(#SPAWNS==0 and util.JSONToTable(FILES[path]).bodies==nil,'shapes for the old bones were built or saved')
-- Reset uses none of them: it rebuilds with the saved pins, and its earlier version (old bones) cannot come back.
NOW=NOW+10 request(admin,'reset',placed,{base=KEY}) local r=last(admin)
assert(r.state=='ready' and SPAWNS[1].options.boneMap[chest]==6 and next(SPAWNS[1].options.collisionOverrides)==nil,'Reset did not rebuild with the saved pins')
local rebuilt=ENTS[r.ent] assert(rebuilt.MMDHLPhysicsHistory[1].boneMap[chest]==5,'a version does not remember its pins')
-- That version is not offered; asked anyway, the answer says why (no respawn helps).
assert(r.data.hasPrevious==false,'Previous version is offered for a version made for the old bones')
NOW=NOW+10 request(admin,'previous',rebuilt,{base=rebuilt:GetNW2String('MMDHLRig')}) assert(says(last(admin),'physics_editor.error.previous_bones_changed') and #SPAWNS==1,'Previous version brought back shapes for the old bones')
-- The rebuilt ragdoll has the current pins: it is edited, tested and saved as usual.
NOW=NOW+10 request(admin,'apply',rebuilt,{base=rebuilt:GetNW2String('MMDHLRig'),request=draft}) r=last(admin)
assert(r.state=='ready' and SPAWNS[2].options.boneMap[chest]==6 and SPAWNS[2].options.collisionOverrides[chest].extent[1]==3)
local current=ENTS[r.ent] assert(current.MMDHLPhysicsHistory[1].boneMap[chest]==6 and r.data.hasPrevious==true)
NOW=NOW+10 request(admin,'previous',current,{base=current:GetNW2String('MMDHLRig')}) assert(last(admin).state=='ready','Previous version refused a version made for the current bones')
-- Restore saved builds the saved shapes with the saved pins, whatever the ragdoll had.
FILES[path]=util.TableToJSON({version=3,generator=18,boneMap=new,bodies={[chest]={center={0,0,2},extent={1,1,1}}},mass=50})
local stale=ragdoll(KEY,{boneMap=old},owner)
NOW=NOW+10 request(admin,'restore_saved',stale,{base=KEY}) local o=SPAWNS[#SPAWNS].options
assert(last(admin).state=='ready' and o.mass==50 and o.collisionOverrides[chest].center[3]==2 and o.boneMap[chest]==6,'Restore saved did not rebuild the saved shapes with the saved pins')
-- Only the body parts' pins count, as when the bone window saves (it keeps the saved
-- corrections then): a finger or the middle spine pinned since leaves the shapes valid.
local finger,spine2='ValveBiped.Bip01_L_Finger1','ValveBiped.Bip01_Spine2'
FILES[path]=util.TableToJSON({version=3,generator=18,boneMap={[chest]=6,[finger]=40,[spine2]=3},boneMapVersion=1})
local before=ragdoll(KEY,{boneMap={[chest]=6},collisionOverrides={[chest]={center={0,0,1},extent={2,2,2}}}},owner)
NOW=NOW+10 request(admin,'test',before,{base=KEY,request=draft}) assert(last(admin).state=='ready','a finger pinned since refused the test copy')
NOW=NOW+10 request(admin,'apply',before,{base=KEY,request=draft}) r=last(admin)
assert(r.state=='ready' and SPAWNS[#SPAWNS].options.boneMap[finger]==40 and SPAWNS[#SPAWNS].options.collisionOverrides[chest].extent[1]==3,'a finger pinned since refused Apply')
local applied=ENTS[r.ent] assert(r.data.hasPrevious==true,'a version made before a finger was pinned is not offered')
NOW=NOW+10 request(admin,'previous',applied,{base=applied:GetNW2String('MMDHLRig')}) assert(last(admin).state=='ready','a finger pinned since refused Previous version')
local placedBefore=ragdoll(KEY,{boneMap={[chest]=6},collisionOverrides={[chest]={center={0,0,1},extent={2,2,2}}}},owner)
NOW=NOW+10 request(admin,'save_default',placedBefore,{base=KEY}) assert(last(admin).state=='ready' and util.JSONToTable(FILES[path]).bodies[chest],'a finger pinned since refused Save for new spawns')
-- Pins compare as numbers; none and empty are the same; only the 18 body parts count.
local BM=mmdhl.boneMapper
assert(BM.SamePhysicalPins({[chest]=6},{[chest]='6'}) and BM.SamePhysicalPins(nil,{}) and not BM.SamePhysicalPins({[chest]=6},nil) and not BM.SamePhysicalPins({},{[chest]=6}) and not BM.SamePhysicalPins({[chest]=6},{[chest]=-1}))
assert(BM.SamePhysicalPins({[finger]=40},nil) and BM.SamePhysicalPins({[chest]=6,[spine2]=3},{[chest]=6,['ValveBiped.Bip01_Neck1']=2}) and not BM.SamePhysicalPins({['ValveBiped.Bip01_R_Foot']=9},{}))
assert(#ERRORS==0,table.concat(ERRORS,'\n'))
''')
print('PASS: Save for new spawns and Forget keep the bone window\'s pins (a file with only pins is no saved physics); the editor\'s preview fits with the saved pins; shapes made for older body-part pins are neither built, saved nor offered as Previous version (other pins do not count), Reset and Restore saved rebuild with the current ones')

spawn = lua51.LuaRuntime(unpack_returned_tuples=True)
json_bridge(spawn)
spawn.execute(SERVER_MOCKS)
attach(spawn)
spawn.execute(r'''
AddCSLuaFile=function() end include=function() end cleanup.Register=function() end duplicator.RegisterEntityClass=function() end
CONVARS.mmdhl_native_carrier={GetBool=function() return true end}
ASSET=string.rep('d',64)
mmdhl.FeatureAvailable=function() return true end mmdhl.WithSpawnDefaults=function(p,o) return table.Copy(o or {}) end mmdhl.CanUseAsset=function() return true end
mmdhl.GetInstance=function() return 0 end mmdhl.ActorOptions=function(o) return o end mmdhl.FacingPlayerAngles=function() return Angle(0,0,0) end mmdhl.CleanBodygroups=function(v) return v end mmdhl.NPCWeapon=function() end
mmdhl.ValidSecondaryBackend=function(v) return v end mmdhl.ValidCollisionFlags=function(v) return v end mmdhl.CollideDefault=1
mmdhl.native.RequestAsset=function() return true end mmdhl.native.AssetInfo=function() return py_encode({name='A'}) end
FITS={} FIT_FAIL=nil
mmdhl.native.RequestCarrierFit=function(id,json) local o=util.JSONToTable(json) FITS[#FITS+1]=o if FIT_FAIL and FIT_FAIL(o) then return nil,'Invalid physics settings: schema_unsupported schema this module reads physics schema 1' end return true end
NATIVE={} mmdhl.SpawnNative=function(p,id,o,done,flags) NATIVE[#NATIVE+1]={o=o,flags=flags} local e=ragdoll(KEY,o,p) e.asset=id if done then done(e) end return e end
''')
spawn.execute(read('addon/lua/mmdhl/server.lua'))
spawn.execute(RULES)
spawn.execute(PROFILE)
spawn.execute(EDITOR)
spawn.execute(SERVER_SETUP)
spawn.execute(r'''
local P=mmdhl.physics local p=player(false,false,false,'Owner')
local function notices() local out={} for _,m in ipairs(SENT) do if m.name=='mmdhl_notice' then out[#out+1]=m.fields[1] end end return out end
local function spawnWith(o) local made,err mmdhl.Spawn(p,ASSET,o,function(e,why) made,err=e,why end) tick(5) return made,err end
-- No saved file: nothing is added; native keeps 70 kg.
local made=spawnWith({backend='source',position={0,0,0},angles={0,0,0}})
assert(made and FITS[#FITS].mass==nil and FITS[#FITS].physicsOverrides==nil,'a spawn without a request mass must leave it to native')
-- A saved default: shapes, physics and mass; a request's own values win.
local canonical={schema=1,bodies={['ValveBiped.Bip01_L_Hand']={damping=2}}}
FILES[P.SavedPath(ASSET)]=util.TableToJSON({version=3,generator=18,bodies={['ValveBiped.Bip01_Head1']={center={0,0,1},extent={2,2,2},style='capsule'}},scale=3.23656,excludedMaterials={},mass=62,physics=canonical,editor={schema=1,ui={schema=1,feel={preset='custom',stiffness=0,range=0,floatiness=0}}}})
made=spawnWith({backend='source',position={0,0,0},angles={0,0,0}})
local o=FITS[#FITS] assert(o.mass==62 and o.physicsOverrides.bodies['ValveBiped.Bip01_L_Hand'].damping==2 and o.collisionOverrides['ValveBiped.Bip01_Head1'].style=='capsule' and o.physicsEditor.feel.preset=='custom')
made=spawnWith({backend='source',position={0,0,0},angles={0,0,0},mass=80,physicsOverrides={}})
o=FITS[#FITS] assert(o.mass==80 and next(o.physicsOverrides)==nil and o.collisionOverrides['ValveBiped.Bip01_Head1'],'the request\'s mass and explicit automatic physics must win')
-- Saved physics that no longer build: one retry with automatic physics and fitted shapes, and the player is told.
FIT_FAIL=function(o) return o.physicsOverrides and next(o.physicsOverrides)~=nil end
local before=#FITS made=spawnWith({backend='source',position={0,0,0},angles={0,0,0}})
assert(made and #FITS==before+2 and next(FITS[#FITS].physicsOverrides)==nil and FITS[#FITS].collisionOverrides['ValveBiped.Bip01_Head1'].style==nil and FITS[#FITS].collisionOverrides['ValveBiped.Bip01_Head1'].center[3]==1,'the fallback did not drop physics and styles only')
local told=false for _,n in ipairs(notices()) do told=told or n:find('physics_editor.notice.saved_failed',1,true)~=nil end assert(told,'the fallback was silent')
-- A request's own physics are never dropped: the retry removes only the saved styles, and still fails.
before=#FITS local failed,err=spawnWith({backend='source',position={0,0,0},angles={0,0,0},physicsOverrides={schema=1,massMode='volume'}})
assert(not failed and #FITS==before+2 and FITS[#FITS].physicsOverrides.massMode=='volume' and err:find('Invalid physics settings',1,true),'a requested profile was silently dropped')
FIT_FAIL=nil
-- The spawn menu sends no mass: none reaches the native options.
local captured local real=mmdhl.Spawn mmdhl.Spawn=function(_,_,o) captured=o end
function p:GetEyeTrace() return {Hit=true,HitPos=Vector(0,0,0),HitNormal=Vector(0,0,1)} end function p:EyePos() return Vector(0,0,64) end
feed({'spawn',ASSET,0,util.TableToJSON({request=1,role='ragdoll'})}) RECEIVERS.mmdhl_action(0,p)
assert(captured and captured.mass==nil,'the spawn menu forced 70 kg over a saved mass')
feed({'spawn',ASSET,0,util.TableToJSON({request=2,role='ragdoll',mass=900})}) p.MMDHLSpawnPending=nil RECEIVERS.mmdhl_action(0,p) assert(captured.mass==500)
mmdhl.Spawn=real
-- The collision editor's corrected copy ('fit') follows the physics editor's rules: the
-- mmdhl_physics_editor setting, the edit hook, value checks, the ragdoll limit, one build at
-- a time, the cooldown and the budget. Its Save keeps the saved mass, physics and pins, and
-- is for those who may save defaults.
local hand,chest='ValveBiped.Bip01_L_Hand','ValveBiped.Bip01_Spine4'
local ent=ragdoll(KEY,{mass=62,collisionOverrides={}},p) ent.asset=ASSET RIGS[KEY].scale=3.23656
local function fit(by,data) local count=#NATIVE SENT={} feed({'fit','',ent.index,type(data)=='string' and data or util.TableToJSON(data)}) RECEIVERS.mmdhl_action(0,by) tick(5) return #NATIVE>count,notices() end
local function said(list,key) for _,n in ipairs(list) do if n:find(key,1,true) then return true end end return false end
local good={bodies={[hand]={center={1,0,0},extent={1,1,1}}},excludedMaterials={}}
SINGLE=false
-- Dedicated servers default to admins only; 0 turns it off for everyone.
local made,told=fit(p,good) assert(not made and said(told,'physics_editor.error.admin_only'),'a player who may not change physics spawned a corrected copy')
CONVARS.mmdhl_physics_editor.value='0' made,told=fit(player(true,true,false,'Boss'),good) assert(not made and said(told,'physics_editor.error.disabled'),'the corrected copy ignores mmdhl_physics_editor 0')
CONVARS.mmdhl_physics_editor.value='2'
hook.Add('MMDHLCanEditPhysics','test',function(_,_,op) if op=='test' then return false end end)
made,told=fit(p,good) assert(not made and said(told,'physics_editor.error.not_allowed'),'the edit hook does not decide about the corrected copy') hook.Remove('MMDHLCanEditPhysics','test')
-- The values are checked as the editor's are.
made,told=fit(p,{bodies={[hand]={center={1,0,0},extent={1,1,1e9}}}}) assert(not made and said(told,'physics_editor.error.invalid'),'an out-of-range shape was built')
made,told=fit(p,{bodies={Nonsense={center={0,0,0},extent={1,1,1}}}}) assert(not made and said(told,'physics_editor.error.invalid'),'an unknown body was built')
made,told=fit(p,{bodies={[hand]={center={1,0,0},extent={1,1,1},mass=5}}}) assert(not made and said(told,'physics_editor.error.invalid'),'an unknown shape field was built')
made,told=fit(p,{bodies={},excludedMaterials={9}}) assert(not made and said(told,'physics_editor.error.invalid'),'a material slot the model does not have was accepted')
made,told=fit(p,'{"bodies":{},"pad":"'..string.rep('x',60001)..'"}') assert(not made and said(told,'physics_editor.error.invalid'),'an oversized request was read')
-- The extra ragdoll counts toward the gamemode's limit.
GAMEMODE.PlayerSpawnRagdoll=function() return false end made,told=fit(p,good) GAMEMODE.PlayerSpawnRagdoll=nil
assert(not made and said(told,'server.error.spawn_forbidden'),'the corrected copy ignores the ragdoll limit')
p.MMDHLPhysicsBusy=true made,told=fit(p,good) p.MMDHLPhysicsBusy=nil assert(not made and said(told,'physics_editor.error.busy'),'a second build ran at once')
-- A player who may not save defaults gets the copy, and the default stays.
made,told=fit(p,good)
local file=util.JSONToTable(FILES[P.SavedPath(ASSET)]) assert(made and file.bodies['ValveBiped.Bip01_Head1'] and not file.bodies[hand],'a player who may not save defaults changed them')
assert(said(told,'physics_editor.notice.fit_not_saved') and not p.MMDHLPhysicsBusy)
made,told=fit(p,good) assert(not made and said(told,'physics_editor.error.rate_limited'),'the corrected copy has no cooldown')
local before=#NATIVE for k=1,25 do NOW=NOW+3.01 fit(p,good) end
assert(#NATIVE-before==19,'the budget allowed '..(#NATIVE-before+1)..' corrected copies in 10 minutes')
-- An older server module builds shapes without their style.
NOW=NOW+601 LEVEL=0 made=fit(p,{bodies={[hand]={center={1,0,0},extent={1,1,1},style='capsule'}}}) LEVEL=1
assert(made and FITS[#FITS].collisionOverrides[hand].style==nil and FITS[#FITS].collisionOverrides[hand].center[1]==1,'an older server module was sent a shape style')
-- Saving keeps the saved mass, physics, editor state and the bone window's pins.
local saved=util.JSONToTable(FILES[P.SavedPath(ASSET)]) saved.boneMap={[chest]=5} saved.boneMapVersion=1 saved.boneMapSavedAt=9 FILES[P.SavedPath(ASSET)]=util.TableToJSON(saved)
ent.MMDOptions.boneMap={[chest]=5}
SINGLE=true NOW=NOW+10
made,told=fit(p,{bodies={[hand]={center={1,0,0},extent={1,1,1}}},excludedMaterials={3}})
file=util.JSONToTable(FILES[P.SavedPath(ASSET)])
assert(made and said(told,'server.notice.fit_saved') and FITS[#FITS].boneMap[chest]==5)
assert(file.bodies[hand] and not file.bodies['ValveBiped.Bip01_Head1'] and file.excludedMaterials[1]==3 and file.mass==62 and file.physics.bodies[hand].damping==2 and file.editor,'the collision editor dropped the saved mass, physics or editor state')
assert(file.boneMap[chest]==5 and file.boneMapVersion==1 and file.boneMapSavedAt==9,'the collision editor dropped the bone window\'s pins')
-- The bones were assigned again after this ragdoll was placed: its corrections would not match.
saved=util.JSONToTable(FILES[P.SavedPath(ASSET)]) saved.boneMap={[chest]=6} FILES[P.SavedPath(ASSET)]=util.TableToJSON(saved)
NOW=NOW+10 local fits=#FITS made,told=fit(p,good)
assert(not made and #FITS==fits and said(told,'server.error.fit_bones_changed') and util.JSONToTable(FILES[P.SavedPath(ASSET)]).boneMap[chest]==6,'corrections for a carrier fitted with older pins were built')
-- An older file's corrections are replaced; its pins stay.
FILES[P.SavedPath(ASSET)]=util.TableToJSON({version=2,generator=9,bodies={old=1},boneMap={[chest]=6}}) ent.MMDOptions.boneMap={[chest]=6}
NOW=NOW+10 made=fit(p,good) file=util.JSONToTable(FILES[P.SavedPath(ASSET)])
assert(made and file.version==3 and file.generator==18 and file.bodies[hand] and file.bodies.old==nil and file.boneMap[chest]==6,'the collision editor deleted pins kept in an older file')
-- A failed build frees the player for the next one.
FIT_FAIL=function() return true end NOW=NOW+10 made,told=fit(p,good) FIT_FAIL=nil
assert(not made and said(told,'Invalid physics settings') and not p.MMDHLPhysicsBusy)
-- Pins of parts without a body (a finger) leave the corrections valid, as when saving bones.
saved=util.JSONToTable(FILES[P.SavedPath(ASSET)]) saved.boneMap['ValveBiped.Bip01_L_Finger1']=40 FILES[P.SavedPath(ASSET)]=util.TableToJSON(saved)
NOW=NOW+10 made,told=fit(p,good) assert(made and said(told,'server.notice.fit_saved'),'a finger pinned since refused the corrected copy')
assert(#ERRORS==0,table.concat(ERRORS,'\n'))
''')
print('PASS: spawns take the saved shapes, physics and mass unless the request sets them; saved physics that fail are retried without them; the spawn menu sends no mass; the collision editor\'s corrected copy follows the physics editor\'s permission, checks and limits, keeps saved physics and pins, refuses older pins and needs save rights to save')

# L14: Workshop packages carry the extended file; dedicated servers install it; exports drop who saved it.
w = lua51.LuaRuntime(unpack_returned_tuples=True)
json_bridge(w)
w.execute(r'''
SERVER=true CLIENT=false istable=function(v) return type(v)=='table' end
util={TableToJSON=function(t) return py_encode(t) end,JSONToTable=function(s) return py_decode(s) end}
W={} game={IsDedicated=function() return DEDICATED end} DEDICATED=true
WRITTEN={} function writeIfMissing(path,value) WRITTEN[path]=value end function remember(key,item) REMEMBERED=key end installedHandlers={}
FILES={} file={Read=function(p) return FILES[p] end}
mmdhl={}
''')
workshop = read('addon/lua/mmdhl/workshop.lua')
w.execute(definition(CHECK, workshop, 'function W.ItemFit('))
w.execute(definition(CHECK, workshop, 'local function installed(job)').replace('local function installed(job)', 'function installed(job)'))
export = read('addon/lua/mmdhl/workshop_export.lua')
w.execute(definition(CHECK, export, 'local function characterItem(').replace('local function characterItem(', 'function characterItem('))
w.execute(r'''
local fit={version=3,generator=18,bodies={['ValveBiped.Bip01_Head1']={center={0,0,1},extent={1,1,1},style='box'}},scale=3.2,excludedMaterials={},mass=62,physics={schema=1,massMode='volume'},editor={schema=1,ui={},savedAt=1,savedBy='7656',savedByName='Admin'}}
local item={kind='character',asset='a',fit=fit}
assert(W.ItemFit(item)==fit,'the extended fit file is refused by packages')
installed({item=item,package={},key='character:a'})
assert(WRITTEN['mmd_hotloader/fit_overrides/a.json']==fit and REMEMBERED=='character:a','a dedicated server does not install the package\'s fit file')
WRITTEN={} DEDICATED=false installed({item=item,package={},key='character:a'}) assert(next(WRITTEN)==nil)
FILES['mmd_hotloader/fit_overrides/b.json']=util.TableToJSON(fit)
local exported=characterItem('b',{name='B',settings={}})
assert(exported.fit.physics.massMode=='volume' and exported.fit.editor.savedBy==nil and exported.fit.editor.savedByName==nil and exported.fit.editor.savedAt==1,'exports keep who saved the physics')
''')
print('PASS: packages accept fit files with physics, dedicated servers install them, and exports drop who saved them')

# L15: the editor on clients with a current, an older or no native module.
UI_HEAD = read('addon/lua/mmdhl/ui.lua')
UI_HEAD = UI_HEAD[:UI_HEAD.index('\n', UI_HEAD.index('mmdhl.UI={')) + 1]
CLIENT_MOCKS = r'''
SERVER=false CLIENT=true NOW=10 RealTime=function() return NOW end CurTime=RealTime FRAME=1 FrameNumber=function() return FRAME end
-- As in GMod: a table is valid only through its own IsValid method.
IsValid=function(v) if type(v)~='table' then return false end local f=v.IsValid if not f then return false end return f(v) end
isstring=function(v) return type(v)=='string' end istable=function(v) return type(v)=='table' end isfunction=function(v) return type(v)=='function' end isnumber=function(v) return type(v)=='number' end
local function deep(t) if type(t)~='table' then return t end local c={} for k,v in pairs(t) do c[k]=deep(v) end return setmetatable(c,getmetatable(t)) end
-- As in GMod: table.Copy takes a table (or nil) and errors on anything else.
table.Copy=function(t) if t==nil then return nil end if type(t)~='table' then error("bad argument #1 to 'pairs' (table expected, got "..type(t)..")",2) end return deep(t) end table.Count=function(t) local n=0 for _ in pairs(t or {}) do n=n+1 end return n end
math.Clamp=function(v,lo,hi) return math.min(math.max(v,lo),hi) end math.Round=function(v,d) local p=10^(d or 0) return math.floor(v*p+.5)/p end
Lerp=function(t,a,b) return a+(b-a)*t end
ERRORS={} ErrorNoHalt=function(s) ERRORS[#ERRORS+1]=tostring(s) end
CONVARS={} local function convar(name,default) local c={value=tostring(default)} function c:GetBool() return (tonumber(self.value) or 0)~=0 end function c:GetInt() return math.floor(tonumber(self.value) or 0) end function c:GetFloat() return tonumber(self.value) or 0 end function c:GetString() return self.value end function c:SetInt(v) self.value=tostring(v) end CONVARS[name]=c return c end
CreateClientConVar=function(name,default) return CONVARS[name] or convar(name,default) end CreateConVar=function(name,default) return CONVARS[name] or convar(name,default) end
GetConVar=function(name) return CONVARS[name] end RunConsoleCommand=function(name,value) (CONVARS[name] or convar(name,0)).value=tostring(value) end
FCVAR_ARCHIVE=1 FCVAR_REPLICATED=2 FCVAR_NOTIFY=4
game={IsDedicated=function() return false end,SinglePlayer=function() return true end} ents={FindByClass=function() FOUND_BY_CLASS=(FOUND_BY_CLASS or 0)+1 return {} end}
GLOBALS={MMDHLPhysicsEditor=1} GetGlobal2Int=function(k,d) local v=GLOBALS[k] if v==nil then return d end return v end
HOOKS={} hook={Add=function(e,n,f) HOOKS[e]=HOOKS[e] or {} HOOKS[e][n]=f end,Remove=function(e,n) if HOOKS[e] then HOOKS[e][n]=nil end end,Run=function() end}
function fire(e,...) for _,f in pairs(HOOKS[e] or {}) do f(...) end end
TIMERS={} timer={Create=function(n,_,_,f) TIMERS[n]=f end,Remove=function(n) TIMERS[n]=nil end,Simple=function(_,f) f() end}
function tick() NOW=NOW+.2 local list={} for _,f in pairs(TIMERS) do list[#list+1]=f end for _,f in ipairs(list) do f() end fire('Think') end
NOTES={} notification={AddLegacy=function(t) NOTES[#NOTES+1]=t end} NOTIFY_GENERIC=0 NOTIFY_ERROR=1 NOTIFY_HINT=3
language={Add=function(k,v) PHRASES=PHRASES or {} PHRASES[k]=v end}
properties={Add=function(n,t) PROPERTIES=PROPERTIES or {} PROPERTIES[n]=t end} concommand={Add=function(n,f) COMMANDS=COMMANDS or {} COMMANDS[n]=f end}
KEY_Z=1 KEY_Y=2 KEY_ENTER=3 KEY_TAB=4 KEY_F=5 KEY_HOME=6 KEY_ESCAPE=7 KEY_LEFT=8 KEY_RIGHT=9 KEY_UP=10 KEY_DOWN=11 KEY_END=12 KEY_SPACE=13 MOUSE_LEFT=107 MOUSE_RIGHT=108
CTRL=false SHIFT=false input={IsControlDown=function() return CTRL end,IsShiftDown=function() return SHIFT end}
ScrW=function() return 1920 end ScrH=function() return 1080 end
QUERIES={} Derma_Query=function(text,title,...) QUERIES[#QUERIES+1]={text=text,title=title,args={...}} end
CLIPBOARD=nil SetClipboardText=function(t) CLIPBOARD=t end
SortedPairs=function(t) local keys={} for k in pairs(t) do keys[#keys+1]=k end table.sort(keys) local i=0 return function() i=i+1 local k=keys[i] if k~=nil then return k,t[k] end end end
player_manager={AllValidModels=function() return {alyx='models/player/alyx.mdl'} end}
local V={} V.__index=V
function Vector(x,y,z) return setmetatable({x=x or 0,y=y or 0,z=z or 0},V) end
V.__add=function(a,b) return Vector(a.x+b.x,a.y+b.y,a.z+b.z) end V.__sub=function(a,b) return Vector(a.x-b.x,a.y-b.y,a.z-b.z) end V.__mul=function(a,s) if type(a)=='number' then a,s=s,a end return Vector(a.x*s,a.y*s,a.z*s) end V.__div=function(a,s) return Vector(a.x/s,a.y/s,a.z/s) end
function V:Unpack() return self.x,self.y,self.z end function V:ToScreen() return {x=1,y=1,visible=true} end function V:DistToSqr(o) return (self.x-o.x)^2+(self.y-o.y)^2+(self.z-o.z)^2 end
local A={} A.__index=A function Angle(p,y,r) return setmetatable({p=p or 0,y=y or 0,r=r or 0},A) end function A:Forward() return Vector(1,0,0) end function A:RotateAroundAxis() end function A:Up() return Vector(0,0,1) end
angle_zero=Angle() vector_origin=Vector()
LocalToWorld=function(p,a,o,oa) return p+o,oa end WorldToLocal=function(p,a,o,oa) return p-o,a end
Color=function(r,g,b,a) return {r=r,g=g,b=b,a=a or 255} end ColorAlpha=function(c,a) return Color(c.r,c.g,c.b,a) end color_white=Color(255,255,255) color_black=Color(0,0,0)
TEXT_ALIGN_CENTER=1 TEXT_ALIGN_LEFT=0
local function noop() end
draw={RoundedBox=noop,RoundedBoxEx=noop,SimpleText=noop,SimpleTextOutlined=noop,NoTexture=noop}
surface={SetFont=noop,GetTextSize=function(t) return #tostring(t or '')*7,14 end,CreateFont=noop,SetDrawColor=noop,DrawRect=noop,DrawLine=noop,DrawOutlinedRect=noop}
render={DrawLine=noop,DrawBeam=noop,SetColorMaterial=noop,DrawWireframeBox=noop} cam={Start3D2D=function() LABELS=(LABELS or 0)+1 end,End3D2D=noop,IgnoreZ=function(on) IGNOREZ=on end}
util={AddNetworkString=noop,TableToJSON=function(t) return py_encode(t) end,JSONToTable=function(s) if type(s)~='string' then return nil end return py_decode(s) end,Compress=function(s) return 'Z'..s end,Decompress=function(s) if type(s)=='string' and s:sub(1,1)=='Z' then return s:sub(2) end end,
 GetSurfaceIndex=function(n) return ({flesh=1,metal=2,wood=3,ice=4})[n] or -1 end,GetSurfaceData=function() return {density=1000} end,IsValidModel=function(m) return m=='models/alyx.mdl' end,GetModelInfo=function() return {KeyValues=TEMPLATE} end,
 AimVector=function() return Vector(1,0,0) end,IntersectRayWithOBB=function(o) return o+Vector(1,0,0) end,TraceLine=function() return {} end}
gui={ScreenToVector=function() return Vector(1,0,0) end,MouseX=function() return MOUSE_X or 0 end,MouseY=function() return MOUSE_Y or 0 end} EyePos=function() return Vector() end EyeAngles=function() return Angle() end
SENT={} local inbox={} local writing function feed(list) inbox=list end local function read() return table.remove(inbox,1) end
RECEIVERS={} net={Receive=function(n,f) RECEIVERS[n]=f end,Start=function(n) writing={name=n,fields={}} end,SendToServer=function() SENT[#SENT+1]=writing end,ReadUInt=read,ReadString=read,ReadData=read,ReadEntity=read}
for _,name in ipairs({'WriteUInt','WriteString','WriteEntity','WriteData','WriteFloat','WriteBool'}) do net[name]=function(v) writing.fields[#writing.fields+1]=v end end
-- Panels: every method the editor calls, recorded loosely; ALL lists them for the walk below.
-- CONTROLS holds the built-in methods a class's instances inherit, as GMod's vgui files do.
ALL={} local Panel={}
CONTROLS={
 -- dtextentry.lua: Enter in a single-line entry moves the focus on, then calls OnEnter.
 DTextEntry={Think=function(self) self.convarThinks=(self.convarThinks or 0)+1 end,OnLoseFocus=function() end,OnGetFocus=function() end,
  OnKeyCodeTyped=function(self,code) if code==KEY_ENTER and not self.multiline then self:FocusNext() if self.OnEnter then self:OnEnter(self:GetText()) end end end},
 -- dframe.lua: Think moves the frame while its title bar is dragged.
 DFrame={Think=function(self) if self.Dragging then self:SetPos(gui.MouseX()-self.Dragging[1],gui.MouseY()-self.Dragging[2]) end end}}
local function panel(class,parent)
 local p=setmetatable({class=class,children={},w=100,h=20,visible=true,enabled=true,text='',parent=parent,choices={}},Panel) ALL[#ALL+1]=p
 if parent then parent.children[#parent.children+1]=p end
 if class=='DFrame' then p.btnClose=panel('DButton',p) p.btnMinim=panel('DButton',p) p.btnMaxim=panel('DButton',p) p.lblTitle=panel('DLabel',p) end
 if class=='DCheckBoxLabel' then p.Label=panel('DLabel',p) end
 return p
end
local loose={'^Set','^Dock','^Make','^SizeTo','^Center','^Invalidate','^Request','^Kill','^Mouse','^Move','^Hide','^Show$','^Open$','^PerformLayout$','^Paint','^Think$'}
Panel.__index=function(t,k) local f=rawget(Panel,k) if f then return f end local control=CONTROLS[rawget(t,'class')] if control and control[k] then return control[k] end if type(k)=='string' then for _,pattern in ipairs(loose) do if k:match(pattern) then return function() end end end end end
function Panel:IsValid() return rawget(self,'removed')~=true end
function Panel:SetMultiline(v) self.multiline=v end
-- Keyboard focus, one panel at a time. RequestFocus takes it (the panel that had it loses it);
-- KillFocus and FocusNext (DTextEntry's Enter) give it up. Losing it calls OnLoseFocus at once,
-- or with LATE_BLUR only at blur(): the game may change focus later than the call.
FOCUSED=nil LATE_BLUR=false LATE={}
local function loseFocus(p) if FOCUSED~=p then return end FOCUSED=nil if p.OnLoseFocus then p:OnLoseFocus() end end
function blur() local list=LATE LATE={} for _,p in ipairs(list) do loseFocus(p) end end
function Panel:RequestFocus() if FOCUSED==self then return end local had=FOCUSED if had then loseFocus(had) end FOCUSED=self if self.OnGetFocus then self:OnGetFocus() end end
function Panel:KillFocus() if LATE_BLUR then LATE[#LATE+1]=self else loseFocus(self) end end
Panel.FocusNext=Panel.KillFocus
function Panel:HasFocus() return FOCUSED==self end
function Panel:Add(class) return panel(class,self) end function Panel:SetParent(p) self.parent=p end function Panel:GetChildren() return self.children end
function Panel:SetTall(h) self.h=h end function Panel:GetTall() return self.h end function Panel:SetWide(w) self.w=w end function Panel:GetWide() return self.w end
function Panel:SetSize(w,h) self.w,self.h=w,h end function Panel:GetSize() return self.w,self.h end function Panel:SetPos(x,y) self.x,self.y=x,y end function Panel:GetPos() return self.x or 0,self.y or 0 end
function Panel:SetVisible(v) self.visible=v end function Panel:IsVisible() return self.visible end function Panel:SetEnabled(v) self.enabled=v end function Panel:IsEnabled() return self.enabled end
function Panel:SetText(t) self.text=t end function Panel:GetText() return self.text end function Panel:GetValue() return self.text end
function Panel:SetValue(v) if self.class=='DCheckBoxLabel' then self.checked=v==true or v==1 if self.OnChange then self:OnChange(self.checked) end else self.text=v end end
function Panel:SetFont(f) self.font=f end function Panel:GetFont() return self.font end function Panel:Remove() self.removed=true end function Panel:Clear() for _,c in ipairs(self.children) do c.removed=true end self.children={} end
function Panel:SetChecked(v) self.checked=v end function Panel:GetChecked() return self.checked==true end function Panel:CursorPos() return 0,0 end function Panel:IsHovered() return false end
function Panel:AddChoice(text,data,selected) self.choices[#self.choices+1]={text,data} end
function Panel:ChooseOptionID(i) local c=self.choices[i] if c then self.text=c[1] if self.OnSelect then self:OnSelect(i,c[1],c[2]) end end end
function Panel:AddColumn() return panel('DListView_Column',self) end function Panel:AddLine(...) local l=panel('DListView_Line',self) l.columns={...} self.lines=self.lines or {} self.lines[#self.lines+1]=l return l end function Panel:GetLines() return self.lines or {} end
function Panel:Close() if self.OnClose then self:OnClose() end self.removed=true end
vgui={Create=function(class) return panel(class) end,GetControlTable=function(class) return CONTROLS[class] end}
DermaMenu=function() local m={options={}} function m:AddOption(t,f) self.options[#self.options+1]={t,f} return m end function m:Open() MENU=self end return m end
function walk(root) for _,p in ipairs(ALL) do if not p.removed and p.visible~=false then
 if p.PerformLayout then p:PerformLayout(p.w,p.h) end if p.Paint then p:Paint(p.w,p.h) end if p.PaintOver then p:PaintOver(p.w,p.h) end if p.Think and p.class~='DFrame' then p:Think() end end end end
function find(text,class) for i=#ALL,1,-1 do local p=ALL[i] if not p.removed and p.text==text and (not class or p.class==class) then return p end end end
-- The edited ragdoll and its rig.json.
RIG={key=string.rep('b',32),scale=3.23656,materialCount=3,bodies={},bones={}}
for i=0,17 do local names=mmdhl_body_names RIG.bones[i+1]={name=names[i+1],position={0,0,({[3]=60,[14]=-6,[17]=-6})[i] or i},rotation={0,0,0,1},parent=-1}
 RIG.bodies[i+1]={bone=i,parent=({[0]=-1,0,1,2,2,4,5,6,2,8,9,10,0,12,13,0,15,16})[i],name=names[i+1],center={1,0,0},extent={2,1,1},confidence=.9,needsReview=i==5,
  hull={{-1,-1,-1},{3,-1,-1},{-1,1,-1},{3,1,-1},{-1,-1,1},{3,-1,1},{-1,1,1},{3,1,1}},faces={{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}}} end
local M={} M.__index=M function M:GetTranslation() return Vector(0,0,0) end function M:GetAngles() return Angle() end
local Ent={} Ent.__index=function(t,k) local f=rawget(Ent,k) if f then return f end if type(k)=='string' and k:match('^%u') and not k:match('^MMD') then return function() end end end
function Ent:IsValid() return rawget(self,'removed')~=true end function Ent:GetClass() return 'prop_ragdoll' end function Ent:GetNW2Int(k,d) return k=='MMDHLNativeBodyCount' and 18 or d end function Ent:GetNW2String(k,d) return k=='MMDHLRig' and self.key or d end
function Ent:GetBoneMatrix() return setmetatable({},M) end function Ent:OBBMins() return Vector(-10,-10,0) end function Ent:OBBMaxs() return Vector(10,10,70) end function Ent:WorldSpaceCenter() return Vector(0,0,35) end
function Ent:EntIndex() return self.index end function Ent:GetModel() return 'models/mmd/x/m.mdl' end
RAGDOLL=setmetatable({key=RIG.key,index=33},Ent) ENTS={[33]=RAGDOLL} NULL={IsValid=function() return false end} Entity=function(i) return ENTS[i] or NULL end
LocalPlayer=function() return {EyeAngles=function() return Angle() end,GetFOV=function() return 75 end,GetEyeTrace=function() return {Entity=RAGDOLL} end} end
'''
CLIENT_SETUP = r'''
mmdhl.library={entries={}} mmdhl.IsMMD=function() return true end mmdhl.GetAsset=function() return string.rep('e',64) end mmdhl.GetRig=function(e) return e==RAGDOLL and RIG or NEWRIG end
mmdhl.GetMetadata=function() return {model={materials={{name='skin',alpha=1},{name='skirt',alpha=1},{name='hidden',alpha=0}}}} end
mmdhl.Decode=function(v) if type(v)=='string' then return util.JSONToTable(v) end return v end
function answer(state,message,index,data) local blob=data and ('Z'..py_encode(data)) or '' feed({REQ_ID,state,message or '',index or 0,#blob,blob}) RECEIVERS.mmdhl_physics_status() end
function lastSent() return SENT[#SENT] end
STATE={base=RIG.key,level=1,canEdit=true,canSave=true,hasPrevious=true,fitOptions={scaleMultiplier=1},materialCount=3,
 applied={collisionOverrides={},collisionOverrideScale=3.23656,excludedMaterials={},mass=70,physicsOverrides={schema=1,bodies={['ValveBiped.Bip01_L_Hand']={damping=2}}}},savedDefault={exists=true,hasPhysics=true,savedAt=1,savedByName='Admin'}}
function openEditor()
 SENT={} mmdhl.OpenPhysicsEditor(RAGDOLL)
 local m=lastSent() assert(m and m.name=='mmdhl_physics' and m.fields[3]=='open','the editor did not ask the server')
 REQ_ID=m.fields[2] answer('state','',33,STATE)
 local e=mmdhl.GetPhysicsEditor() assert(e and e.draft and e.draft.explicit[7].damping==2,'the applied physics did not reach the draft')
 return e
end
function exercise(e)
 walk()
 -- The preset cards are the empty-text buttons of the Feel tab.
 local cards={} for _,p in ipairs(ALL) do if p.class=='DButton' and p.text=='' and rawget(p,'DoClick') and not p.removed and p.parent and #p.parent.children==6 then cards[#cards+1]=p end end
 assert(#cards>=6,'no preset cards') cards[2]:DoClick()
 assert(#QUERIES==1,'a card over per-part settings must ask first') QUERIES[1].args[4]() QUERIES={}
 assert(e.draft.feel.preset=='less_floppy' and next(e.draft.explicit)==nil and e:Dirty(),'Clear them did not apply the card')
 local longer=find('Longer','DButton') longer:DoClick() assert(e.draft.shapes[6],'Longer changed nothing')
 RunConsoleCommand('mmdhl_physics_editor_advanced','1') e:ShowTab('numbers') e:Sync() walk()
 local fields=0 for _,p in ipairs(ALL) do if p.class=='DTextEntry' and p.OnEnter and not p.removed and p.Show then fields=fields+1 end end assert(fields>20,'the Numbers tab has no fields')
 e:Select(13) e:Sync() walk() e:ShowTab('model') e:Sync() walk() e:ShowTab('collisions') walk() e:ShowTab('parts') walk()
 tick() tick() walk()
 CTRL=true e:Key(KEY_Z) assert(e.draft.shapes[6]==nil,'Undo did not undo') e:Key(KEY_Y) assert(e.draft.shapes[6],'Redo did not redo') CTRL=false
 e:TemplateDialog() walk() e:PasteDialog() walk() e:PhyDialog() walk() e:SaveDialog() walk() e:CopyQC() assert(CLIPBOARD:find('$collisionjoints',1,true))
 e:PasteDialog() walk()
 for _,v in ipairs(e.issues) do e:IssueText(v) end
 SENT={} e:Apply() local m=lastSent() assert(m and m.fields[3]=='apply','Apply sent nothing')
 local payload=util.JSONToTable(m.fields[6]:sub(2)) assert(payload.base==RIG.key and payload.request.physicsOverrides.bodies,'Apply sent no request')
 assert(e.building) REQ_ID=m.fields[2] answer('building','',33) assert(e.building)
 answer('error',mmdhl.I18n.Token('physics_editor.error.busy'),0) assert(not e.building and NOTES[#NOTES]==mmdhl.L'physics_editor.error.busy','a server error was not shown')
 return payload
end
'''


def client_runtime(native_lua):
    c = lua51.LuaRuntime(unpack_returned_tuples=True)
    json_bridge(c)
    c.globals().mmdhl_body_names = c.table_from(names)
    c.execute(CLIENT_MOCKS)
    attach(c)
    c.execute('mmdhl.I18n.FontFace=function() return "x" end mmdhl.I18n.FontData=function() return {} end')
    c.execute(native_lua)
    c.execute('AddCSLuaFile=function() end include=function(path) if path=="mmdhl/physics_editor_ui.lua" then UI_FILE() end end')
    c.globals().UI_FILE = lambda: c.execute(read('addon/lua/mmdhl/physics_editor_ui.lua'))
    c.execute(UI_HEAD)
    collision = read('addon/lua/mmdhl/collision_editor.lua')  # its overlay uses GLua's continue: only the shared helpers run here
    c.execute(chr(10).join(definition(CHECK, collision, h) for h in ('local L=mmdhl.L', 'local function restAngle(', 'local axisColors=', 'function mmdhl.DrawLimitArcs(', 'function mmdhl.MaterialRegionList(')))
    c.execute(PROFILE)
    c.execute(CLIENT_SETUP)
    c.execute(EDITOR)
    c.globals().TEMPLATE = read('tests/fixtures/physics/hl2_citizen_phy.txt')
    return c


# autorun sends the editor's files before its installation check: a client whose server has no
# working module still runs physics_editor.lua, and that stops quietly without the profile.
autorun = read('addon/lua/autorun/mmdhl.lua')
gate = autorun.index('if not mmdhl.CheckInstallation()')
for f in ('physics_editor.lua', 'physics_profile.lua', 'physics_editor_ui.lua'):
    assert 0 <= autorun.find(f"AddCSLuaFile('mmdhl/{f}')") < gate, f
bare = lua51.LuaRuntime(unpack_returned_tuples=True)
json_bridge(bare)
bare.globals().mmdhl_body_names = bare.table_from(names)
bare.execute(CLIENT_MOCKS)
attach(bare)
bare.execute('mmdhl.native=nil AddCSLuaFile=function() end include=function() end')
bare.execute(EDITOR)
assert bare.eval('mmdhl.PhysicsRequest==nil and #ERRORS==0')
print('PASS: autorun sends the editor\'s files before its installation check, and physics_editor.lua stops quietly without its profile')

# An older module (no PreviewCarrierFit), then none at all: approximate previews, nothing breaks.
for label, native_lua in (('older module', 'mmdhl.native={GetCapabilities=function() return "{}" end}'), ('no module', 'mmdhl.native=nil')):
    c = client_runtime(native_lua)
    c.execute(r'''
assert(PROPERTIES.mmdhl_physics_editor.Filter(nil,RAGDOLL) and PHRASES['mmdhl.physics_editor.menu']=='Ragdoll physics…')
assert(COMMANDS.mmdhl_physics_editor_open and COMMANDS.mmdhl_physics_editor==nil and CONVARS.mmdhl_physics_editor,'the console command shares the server setting\'s name')
local e=openEditor()
assert(e:Banner()=='client_approximate','the older client is not told')
local noted=false for _,n in ipairs(NOTES) do noted=noted or n==mmdhl.L'physics_editor.banner.client_approximate' end assert(noted,'the update reminder fallback was not shown')
local payload=exercise(e)
assert(e.preview and e.preview.status=='approximate' and #e.preview.bodies==18,'no approximate preview')
local approx=false for _,v in ipairs(e.issues) do approx=approx or v.code=='approx_preview' end assert(approx)
assert(#ERRORS==0,table.concat(ERRORS,'\n'))
e:Close(true) assert(mmdhl.GetPhysicsEditor()==nil and HOOKS.CalcView['MMDHL.PhysicsEditorCamera']==nil,'closing left the editor hooks')
''')
    print(f'PASS: with {label} on the client the editor opens, previews approximately, edits, undoes and applies without errors')

# A current module: the exact preview drives checks, overlaps and the .phy view; Apply rebinds to the new ragdoll.
CURRENT_NATIVE = r'''
mmdhl.native={GetCapabilities=function() return py_encode({physicsEditor=1,version='2.3.0'}) end,RequestAsset=function() return true end}
PREVIEWS=0
function mmdhl.native.PreviewCarrierFit(asset,json)
 PREVIEWS=PREVIEWS+1 local o=util.JSONToTable(json) LAST_PREVIEW=o
 if PENDING then return py_encode({status='pending'}) end
 local bodies={} for i=0,17 do local b=RIG.bodies[i+1] bodies[i+1]={index=i,name=b.name,parent=b.parent,bone=b.bone,center=b.center,extent=b.extent,hull=b.hull,faces=b.faces,style='fitted',volume=16,mass=70/18,massBias=1,damping=.8,rotdamping=3,inertia=12,surfaceprop='flesh',lower={0,0,0},upper={0,0,0},friction={0,0,0},confidence=.9,needsReview=false} end
 local c=o.physicsOverrides and o.physicsOverrides.collisions local on=not c or c.mode=='all'
 if c and c.mode=='custom' then for _,pair in ipairs(c.pairs) do on=on or (pair[1]==3 and pair[2]==4) end end
 local overlap=on and o.collisionOverrides and o.collisionOverrides['ValveBiped.Bip01_Head1'] and {{a=3,b=4,depth=1}} or {}
 return py_encode({status='ready',key=PREVIEWS==1 and RIG.key or string.rep('f',32),scale=3.23656,m=1,unit=1.1,mass=70,canonical=o.physicsOverrides or {},bodies=bodies,pairs={mode='all',count=136},penetrations=overlap,phyText='solid {\n}\n'})
end
'''
c = client_runtime(CURRENT_NATIVE)
c.execute(r'''
-- Until the server's state arrives (or when it never does) the overlay draws nothing and raises nothing.
SENT={} mmdhl.OpenPhysicsEditor(RAGDOLL) local waiting=mmdhl.GetPhysicsEditor()
fire('PostDrawTranslucentRenderables',false,false) fire('HUDPaint') fire('Think')
REQ_ID=lastSent().fields[2] answer('error',mmdhl.I18n.Token('physics_editor.error.not_ragdoll'),0)
fire('PostDrawTranslucentRenderables',false,false) fire('HUDPaint') walk()
assert(#ERRORS==0,table.concat(ERRORS,'\n')) waiting:Close(true)
local e=openEditor()
assert(e:Banner()==nil and e.preview.status=='ready' and e.baseline,'no exact preview')
assert(LAST_PREVIEW.scaleMultiplier==1 and LAST_PREVIEW.physicsEditor==nil,'the preview did not use the ragdoll\'s fit options')
local payload=exercise(e)
-- An enlarged head overlapping the shoulder is an error with fixes, and the overlap is drawn.
e:Edit(function(d) d.shapes[3]={center={0,0,1},extent={6,6,6},style='fitted'} end) tick() tick()
local severe for _,v in ipairs(e.issues) do if v.code=='penetration_severe' then severe=v end end
assert(severe and severe.fixes[1]=='pass_through' and e:HasErrors(),'a severe overlap was not reported')
fire('PostDrawTranslucentRenderables',false,false) fire('HUDPaint') assert(#e.overlapLabels==1,'the overlap is not drawn')
e:Edit(function(d) P=mmdhl.physics P.ApplyFix(d,severe,'pass_through',RIG,e.preview) end) tick()
assert(e.draft.model.collisions.mode=='custom' and #e.draft.model.collisions.pairs==135)
e:PhyDialog() local shown=false for _,p in ipairs(ALL) do shown=shown or (p.class=='DTextEntry' and p.text=='solid {\n}\n') end assert(shown,'the .phy text is not shown')
-- Apply answers with the new ragdoll; the editor follows it.
NEWRIG={key=string.rep('9',32),scale=3.23656,bodies=RIG.bodies,bones=RIG.bones,materialCount=3}
local new=setmetatable({key=NEWRIG.key,index=44},getmetatable(RAGDOLL)) ENTS[44]=new
local state=table.Copy(STATE) state.base=NEWRIG.key state.applied.physicsOverrides=payload.request.physicsOverrides
SENT={} e:Apply() local m=lastSent() REQ_ID=m.fields[2]
answer('ready',mmdhl.I18n.Token('physics_editor.notice.applied'),44,state) tick()
assert(e.ent==new and not e.building and e.state.base==NEWRIG.key and e.appliedAt,'the editor did not rebind to the new ragdoll')
assert(#e.undo>0,'Apply dropped the undo history')
assert(#ERRORS==0,table.concat(ERRORS,'\n'))
-- Reset (and Previous version, Restore saved): the editor shows what the ragdoll has now, clean.
e:Edit(function(d) d.mass=90 d.feel.stiffness=2 end) assert(e:Dirty())
NEWRIG={key=string.rep('8',32),scale=3.23656,bodies=RIG.bodies,bones=RIG.bones,materialCount=3}
local reset=setmetatable({key=NEWRIG.key,index=45},getmetatable(RAGDOLL)) ENTS[45]=reset
local factory=table.Copy(STATE) factory.base=NEWRIG.key factory.applied={collisionOverrides={},collisionOverrideScale=3.23656,excludedMaterials={},mass=70,physicsOverrides={}}
SENT={} e:Send('reset',{}) REQ_ID=lastSent().fields[2] answer('ready',mmdhl.I18n.Token('physics_editor.notice.applied'),45,factory) tick() walk()
assert(e.ent==reset and not e:Dirty() and e.draft.mass==70 and e.draft.feel.stiffness==0 and next(e.draft.explicit)==nil and #e.undo==0,'Reset left the old draft as unsaved changes')
assert(not find(mmdhl.L('physics_editor.apply_count',{count=1}),'DButton') and e:Status()==mmdhl.L('physics_editor.status.applied',{time=os.date('%H:%M',e.appliedAt)}),'the footer still offers to apply the old draft')
-- A refit that is still running: Checks wait for it, Apply and Test copy too, and the editor says so.
PENDING=true e:Edit(function(d) d.excludedMaterials={1} d.shapes[3]={center={0,0,1},extent={6,6,6},style='fitted'} end) tick() walk()
local label=find(mmdhl.L'physics_editor.fit_parts.pending','DLabel')
assert(e.pending and label and label.visible and not find(mmdhl.L('physics_editor.apply_count',{count=2}),'DButton').enabled,'a pending refit did not show or did not hold Apply')
for _,v in ipairs(e.issues) do assert(v.code~='penetration' and v.code~='penetration_severe','a pending refit reported the previous preview\'s overlaps') end
fire('HUDPaint') SENT={} e:Apply() assert(#SENT==0,'Apply sent a draft whose refit is pending')
PENDING=false tick() walk() assert(not e.pending and not label.visible,'the refit never finished')
local overlapping=false for _,v in ipairs(e.issues) do overlapping=overlapping or v.code=='penetration_severe' end assert(overlapping,'the finished refit\'s overlaps did not come back')
e:Edit(function(d) d.excludedMaterials={} d.shapes[3]=nil end) tick()
-- "Pick from world" in the template dialog: Esc, the right button, the pause menu and a miss all cancel it.
e:TemplateDialog() walk() local world=find(mmdhl.L'physics_editor.template.world','DButton')
world:DoClick() assert(e.pickingTemplate and HOOKS.OnPauseMenuShow['MMDHL.PhysicsTemplatePick']) e:Key(KEY_ESCAPE)
assert(not e.pickingTemplate and mmdhl.GetPhysicsEditor()==e and not HOOKS.OnPauseMenuShow['MMDHL.PhysicsTemplatePick'],'Esc closed the editor instead of cancelling the pick')
world:DoClick() e.world:OnMousePressed(MOUSE_RIGHT) assert(not e.pickingTemplate and not e.world.orbit,'the right button did not cancel the pick')
world:DoClick() assert(HOOKS.OnPauseMenuShow['MMDHL.PhysicsTemplatePick']()==false and not e.pickingTemplate,'the pause menu did not cancel the pick')
world:DoClick() e:Pick(1,1) assert(not e.pickingTemplate,'a click beside any ragdoll kept picking')
local trace=util.TraceLine util.TraceLine=function() return {Entity=RAGDOLL} end world:DoClick() e:Pick(1,1) util.TraceLine=trace assert(not e.pickingTemplate)
-- Save and Forget name the model as the window does, not by its hash; a failed write is not a failed build.
e.name='Miku' SENT={} e:Send('save_default',{}) REQ_ID=lastSent().fields[2] answer('ready',mmdhl.I18n.Token('physics_editor.notice.saved',{name='eeeeeeeeeeee'}),33)
assert(NOTES[#NOTES]==mmdhl.L('physics_editor.notice.saved',{name='Miku'}),NOTES[#NOTES])
SENT={} e:Send('clear_default',{}) REQ_ID=lastSent().fields[2] answer('ready',mmdhl.I18n.Token('physics_editor.notice.forgotten',{name='eeeeeeeeeeee'}),33)
assert(NOTES[#NOTES]==mmdhl.L('physics_editor.notice.forgotten',{name='Miku'}),NOTES[#NOTES])
assert(#ERRORS==0,table.concat(ERRORS,'\n'))
-- Closing with unapplied edits asks first.
e:Edit(function(d) d.mass=90 end) e:Close() assert(#QUERIES==1 and mmdhl.GetPhysicsEditor()==e) QUERIES[1].args[2]() assert(mmdhl.GetPhysicsEditor()==nil)
''')
print('PASS: with a current module the exact preview drives overlap checks, fixes and the .phy view; Apply rebinds and keeps the draft, Reset shows the factory physics clean; pending refits hold Checks and Apply; picking from the world cancels; notices name the model')

# The editor's widgets keep GMod's built-in panel behaviour and follow the draft; the preview
# fits with the server's pins; test copies are found once a frame.
c = client_runtime(CURRENT_NATIVE)
c.execute(r'''
local calf='ValveBiped.Bip01_L_Calf'
STATE.fitOptions.boneMap={[calf]=7}
local e=openEditor() STATE.fitOptions.boneMap=nil
assert(LAST_PREVIEW.boneMap and LAST_PREVIEW.boneMap[calf]==7,'the preview was fitted without the pins the server builds with')
walk()
-- Enter in a number field commits it: DTextEntry's own Enter moves the focus on and calls OnEnter.
local weight for _,p in ipairs(ALL) do if not weight and p.class=='DTextEntry' and rawget(p,'Show') and not p.removed then weight=p end end
local steps=#e.undo
weight:RequestFocus() weight:SetText('82,5') weight:OnKeyCodeTyped(KEY_ENTER)
assert(e.draft.mass==82.5,'Enter in a number field did not commit it')
assert(#e.undo==steps+1 and weight:GetText()=='82.5' and not weight:HasFocus(),'Enter committed the value twice')
-- The game may move the focus after OnEnter: still one commit.
weight:RequestFocus() weight:SetText('84') LATE_BLUR=true weight:OnKeyCodeTyped(KEY_ENTER) LATE_BLUR=false blur()
assert(e.draft.mass==84 and #e.undo==steps+2 and weight:GetText()=='84','Enter before the blur committed the value twice')
weight:RequestFocus() weight:SetText('90') weight:KillFocus() assert(e.draft.mass==90 and #e.undo==steps+3,'leaving the field no longer commits')
weight:RequestFocus() weight:OnKeyCodeTyped(KEY_UP) SHIFT=true weight:OnKeyCodeTyped(KEY_UP) SHIFT=false assert(e.draft.mass==101 and #e.undo==steps+5,'the arrow keys do not step the value')
weight:KillFocus() assert(e.draft.mass==101 and #e.undo==steps+5 and weight:GetText()=='101','leaving the field after the arrows committed again')
-- Esc reverts what was typed, also through the blur KillFocus causes (at once or later).
weight:RequestFocus() weight:SetText('120') weight:OnKeyCodeTyped(KEY_ESCAPE)
assert(e.draft.mass==101 and #e.undo==steps+5 and weight:GetText()=='101' and not weight:HasFocus(),'Esc committed the typed value')
weight:RequestFocus() weight:SetText('130') LATE_BLUR=true weight:OnKeyCodeTyped(KEY_ESCAPE) LATE_BLUR=false blur()
assert(e.draft.mass==101 and #e.undo==steps+5 and weight:GetText()=='101','Esc committed the typed value when the blur came later')
-- The value the draft has, typed again in another form, is no edit.
weight:RequestFocus() weight:SetText('101,0') weight:KillFocus() assert(#e.undo==steps+5 and weight:GetText()=='101','an unchanged value made an undo step')
weight:Think() assert((rawget(weight,'convarThinks') or 0)>0,'the number field replaced DTextEntry:Think')
-- Clicking into a field to read it and out again changes nothing, in every field. Shape fields
-- would make the fitted shape explicit (and, mirrored, replace the other side's), part fields
-- would pin their value on both sides, friction would round 0.123 (shown ×5 as 0.61) to 0.122.
RunConsoleCommand('mmdhl_physics_editor_advanced','1') RunConsoleCommand('mmdhl_physics_editor_mirror','1')
e:Edit(function(d) d.explicit[6]={limits={x={-30,40,.123}}} end)
e:ShowTab('numbers') e:Select(6) e:Sync() walk()
local function same(a,b) if type(a)~='table' or type(b)~='table' then return a==b end for k,v in pairs(a) do if not same(v,b[k]) then return false end end for k in pairs(b) do if a[k]==nil then return false end end return true end
local draft,before=table.Copy(e.draft),#e.undo local read=0
for _,p in ipairs(ALL) do if p.class=='DTextEntry' and rawget(p,'Show') and not p.removed and p:IsEnabled() then
 p:RequestFocus() read=read+1 e.world:RequestFocus()
 assert(#e.undo==before and same(e.draft,draft),'leaving a number field unchanged (now showing '..tostring(p:GetText())..') changed the draft')
end end
assert(read>=15 and e.draft.shapes[6]==nil and e.draft.shapes[10]==nil and e.draft.explicit[6].limits.x[3]==.123 and e.draft.explicit[10]==nil,'reading the fields pinned, mirrored or rounded values')
-- Enter moves the focus on to the next field: leaving that one unchanged commits nothing either.
local fields={} for _,p in ipairs(ALL) do if p.class=='DTextEntry' and rawget(p,'Show') and not p.removed and p:IsEnabled() then fields[#fields+1]=p end end
fields[1]:RequestFocus() fields[1]:SetText('3') fields[1]:OnKeyCodeTyped(KEY_ENTER) fields[2]:RequestFocus() fields[2]:KillFocus()
assert(#e.undo==before+1,'the field Enter moved on to committed its unchanged value')
FOCUSED=nil
-- The QC paste dialog keeps DFrame's Think, which moves it while its title bar is dragged.
e:PasteDialog() local d=e.dialog
d.Dragging={10,20} MOUSE_X,MOUSE_Y=300,400 d:Think() d.Dragging=nil
assert(d.x==290 and d.y==380,'the QC paste dialog cannot be dragged')
d:Close()
-- Fit to model parts: the boxes and the set they edit follow Undo (and Discard, versions, Reset).
e:ShowTab('collisions') walk()
local skin,skirt=find('0  skin','DCheckBoxLabel'),find('1  skirt','DCheckBoxLabel')
assert(skin and skirt and skin:GetChecked() and skirt:GetChecked())
skirt:SetValue(false) assert(#e.draft.excludedMaterials==1 and e.draft.excludedMaterials[1]==1)
CTRL=true e:Key(KEY_Z) CTRL=false
assert(#e.draft.excludedMaterials==0 and skirt:GetChecked(),'Undo left the material box unticked')
skin:SetValue(false)
assert(#e.draft.excludedMaterials==1 and e.draft.excludedMaterials[1]==0,'an undone exclusion came back with the next box')
e:Close(true)
-- Test copies are labelled; the label hook looks them up once a frame among the addon's
-- characters, never by searching every ragdoll on each render pass.
NEWRIG=RIG
local copy=setmetatable({key=RIG.key,index=50},getmetatable(RAGDOLL)) ENTS[50]=copy
function copy:GetNW2Bool(k,d) if k=='MMDHLPhysicsTestCopy' then return true end return d end
local listed=0 mmdhl.Entities=function() listed=listed+1 return {RAGDOLL,copy} end
local label=HOOKS.PostDrawTranslucentRenderables['MMDHL.PhysicsTestCopy']
FOUND_BY_CLASS=nil LABELS=0 FRAME=FRAME+1
for pass=1,3 do label(false,false) end
assert(FOUND_BY_CLASS==nil,'every render pass searched all ragdolls')
assert(listed==1 and LABELS==3,'the test copy was not labelled on every pass from one lookup a frame')
label(true,false) label(false,true) assert(LABELS==3,'a depth or skybox pass was labelled')
FRAME=FRAME+1 copy.removed=true label(false,false) assert(listed==2 and LABELS==3,'a removed test copy is still labelled')
assert(#ERRORS==0,table.concat(ERRORS,'\n'))
''')
print('PASS: the preview fits with the server\'s pins; Enter commits a number field once, Esc and a field left unchanged commit nothing, its Think stays DTextEntry\'s; the QC paste dialog drags; the material boxes follow Undo; test copies are labelled from one lookup a frame')
