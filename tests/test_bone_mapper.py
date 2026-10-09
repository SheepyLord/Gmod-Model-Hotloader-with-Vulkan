"""The bone assignment window's rules (bone_mapper_rules.lua) against the native
catalogue and rule fixtures, its state logic (assign, undo, mirror, swap,
suggestions, swinging parts, memory, request and pins), the library flow
(library.lua's Think hook branch, routing, rescue) and the server save handler,
all without the game."""
import json
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition

ROOT = Path(__file__).resolve().parents[1]
FIX = ROOT / 'tests/fixtures/bonemap'
RULES = (ROOT / 'addon/lua/mmdhl/bone_mapper_rules.lua').read_text(encoding='utf-8')
VB = 'ValveBiped.Bip01_'

lua = LuaRuntime(unpack_returned_tuples=True)
lua.globals().SAY_PY = print
lua.execute(r'''
mmdhl={}
istable=function(v) return type(v)=='table' end
isstring=function(v) return type(v)=='string' end
isnumber=function(v) return type(v)=='number' end
isfunction=function(v) return type(v)=='function' end
DISK={}
file={Read=function(p) return DISK[p] end,Write=function(p,v) DISK[p]=v end,CreateDir=function() end,Delete=function(p) DISK[p]=nil end,
 Find=function(pattern) local dir=pattern:match('^(.*)/%*') local out={} for p in pairs(DISK) do local d,n=p:match('^(.*)/([^/]+)$') if d==dir and n:sub(-5)=='.json' then out[#out+1]=n end end table.sort(out) return out,{} end}
PRINTED={} print=function(...) PRINTED[#PRINTED+1]=table.concat({...},' ') end
SAY=SAY_PY
''')
attach(lua)
lua.globals().JSON_DECODE = lambda s: lua.table_from(json.loads(s), recursive=True) if s is not None else None
lua.execute(r'''
util={TableToJSON=function(t) return JSON_ENCODE(t) end,JSONToTable=function(s) local ok,r=pcall(JSON_DECODE,s) if ok then return r end end}
''')


def to_py(value):
    """A Lua table as Python data (arrays become lists)."""
    if lua.eval('function(v) return type(v)=="table" end')(value):
        keys = list(value.keys())
        if keys and all(isinstance(k, int) for k in keys) and sorted(keys) == list(range(1, len(keys) + 1)):
            return [to_py(value[k]) for k in sorted(keys)]
        if not keys:
            return {}
        return {k: to_py(v) for k, v in value.items()}
    return value


def encode(t):
    return json.dumps(to_py(t))


lua.globals().JSON_ENCODE = encode
lua.execute(RULES)
BM = lua.eval('mmdhl.boneMapper')

# ---- the catalogue equals native/humanoid_slots.hpp ----
slots = []
for s in to_py(BM.Slots):
    slots.append({k: s[k] for k in ('key', 'id', 'group', 'family', 'side', 'segment', 'required', 'physical', 'recommended', 'convertOnly', 'anchors', 'partner', 'mmdJp', 'mmdEn')})
    if slots[-1]['anchors'] == {}:
        slots[-1]['anchors'] = []
native = json.loads((FIX / 'slots.json').read_text(encoding='utf-8'))
assert slots == native, [(a, b) for a, b in zip(slots, native) if a != b][:2]
assert sum(1 for s in slots if not s['convertOnly']) == 52 and len(slots) == 54
print('PASS: the slot catalogue matches the native one (54 parts, 52 assignable)')

# ---- the structural rules match native checkBoneMap ----
lua.execute(r'''
function STATE_FROM(skeleton,values,roots)
 local probe={skeleton={bones=skeleton.bones,points={}},auto={slots={},eyes={}}}
 local s=mmdhl.boneMapper.NewState('convert',{probe=probe,filename='test'})
 for _,slot in ipairs(mmdhl.boneMapper.Slots) do if not slot.convertOnly then s.slots[slot.key]={bone=-1,origin='created',confidence=0} end end
 for key,b in pairs(values) do if key=='Eye_L' or key=='Eye_R' then s.eyes[key:sub(5)]={bone=b,origin='user'} else s.slots[key]={bone=b,origin='user',confidence=1} end end
 s.chains={} for _,r in ipairs(roots) do s.chains[#s.chains+1]={root=r,kind='hair',enabled=true} end
 s.groups={{kind='hair',enabled=true,swing='normal'}}
 return s
end
''')
rules = json.loads((FIX / 'rules.json').read_text(encoding='utf-8'))
mismatch = []
for vector in rules['vectors']:
    skeleton = lua.table_from(rules['skeletons'][vector['skeleton']], recursive=True)
    state = lua.globals().STATE_FROM(skeleton, lua.table_from(vector['values']), lua.table_from(vector['chainRoots']))
    got = sorted((i['code'], i['slot'], i['severity']) for i in to_py(BM.Validate(state, lua.table_from({'structural': True}))) or [])
    want = sorted((e['code'], e['slot'], e['severity']) for e in vector['expect'])
    if got != want:
        mismatch.append((vector['name'], got, want))
assert not mismatch, mismatch
print(f'PASS: {len(rules["vectors"])} shared rule vectors give the native problems')

# ---- a probe-like skeleton for the state tests ----
lua.execute(r'''
local VB='ValveBiped.Bip01_'
function BONES()
 local list={} local function add(name,parent,x,y,z,w,flags,meaning,side,segment) list[#list+1]={name=name,parent=parent,position={x,y,z},weighted=w,flags=flags or {},meaning=meaning or '',side=side or '',segment=segment or 0,mirror=-1} return #list-1 end
 local hips=add('Hips',-1,0,.95,0,100,{},'pelvis') local spine=add('Spine',hips,0,1.05,0,100,{},'spine') local chest=add('Chest',spine,0,1.3,0,100,{},'spine')
 local neck=add('Neck',chest,0,1.45,0,50,{},'neck') local head=add('Head',neck,0,1.55,0,200,{},'head')
 local ids={}
 for _,s in ipairs({{'Left',1,'L'},{'Right',-1,'R'}}) do local n,x,side=s[1],s[2],s[3]
  local clav=add(n..'Shoulder',chest,.06*x,1.4,0,30,{},'clavicle',side) local arm=add(n..'Arm',clav,.17*x,1.4,0,100,{},'upperarm',side)
  local fore=add(n..'ForeArm',arm,.43*x,1.4,0,100,{},'forearm',side) add(n..'ForeArmTwist',fore,.5*x,1.4,0,10,{'twist','helper'},'helper',side) local hand=add(n..'Hand',fore,.68*x,1.4,0,80,{},'hand',side)
  local f=hand for k=1,3 do f=add(n..'HandIndex'..k,f,(.7+k*.03)*x,1.4,0,5,{},'index',side,k) end
  local thigh=add(n..'UpLeg',hips,.09*x,.9,0,150,{},'thigh',side) local calf=add(n..'Leg',thigh,.09*x,.5,0,120,{},'calf',side)
  local foot=add(n..'Foot',calf,.09*x,.08,0,60,{},'foot',side) add(n..'ToeBase',foot,.09*x,.02,.1,20,{},'toe',side)
  ids[side]={clav=clav,arm=arm,fore=fore,hand=hand,thigh=thigh,calf=calf,foot=foot}
 end
 local h1=add('Hair_01',head,0,1.66,-.08,10,{},'hair') local h2=add('Hair_02',h1,0,1.56,-.12,10,{},'hair') add('Hair_end',h2,0,1.46,-.14,0,{'helper'},'helper')
 local skirt=add('Skirt',hips,0,.9,0,0,{},'skirt')
 for _,n in ipairs({'F','B'}) do local a=add('Skirt_'..n..'_01',skirt,0,.85,n=='F' and .12 or -.12,10,{},'skirt') add('Skirt_'..n..'_02',a,0,.7,n=='F' and .14 or -.14,10,{},'skirt') end
 -- mirror partners as the native classifier gives them
 local byName={} for i,b in ipairs(list) do byName[b.name]=i-1 end
 for i,b in ipairs(list) do local other=b.name:gsub('^Left','Right') if other==b.name then other=b.name:gsub('^Right','Left') end if other~=b.name and byName[other] then b.mirror=byName[other] end end
 return list,byName
end
function PROBE(confident)
 local bones,byName=BONES()
 local slots={}
 local function put(key,name,c) slots[VB..key]={bone=name and byName[name] or -1,confidence=c or 1,method='name'} end
 put('Pelvis','Hips') put('Spine1','Spine',.95) put('Spine2',nil) put('Spine4','Chest',.95) put('Neck1','Neck') put('Head1','Head')
 for _,s in ipairs({{'L_','Left'},{'R_','Right'}}) do local k,n=s[1],s[2]
  put(k..'Clavicle',n..'Shoulder') put(k..'UpperArm',n..'Arm') put(k..'Forearm',n..'ForeArm') put(k..'Hand',n..'Hand',confident and 1 or .7)
  put(k..'Thigh',n..'UpLeg') put(k..'Calf',n..'Leg') put(k..'Foot',n..'Foot') put(k..'Toe0',n..'ToeBase')
  put(k..'Finger1',n..'HandIndex1') put(k..'Finger11',n..'HandIndex2') put(k..'Finger12',n..'HandIndex3')
 end
 for _,s in ipairs(mmdhl.boneMapper.Slots) do if not s.convertOnly and not slots[s.key] then slots[s.key]={bone=-1,confidence=0,method=''} end end
 return {version=1,format='fbx',sourceSha256=string.rep('a',64),skeleton={signature=string.rep('b',64),height=1.7,bones=bones,points={0,0,0,0, 0,1.7,0,4}},
  auto={humanoid=true,slots=slots,eyes={L={bone=-1,confidence=0},R={bone=-1,confidence=0}}}}
end
''')
lua.execute(r'''
local BM,VB=mmdhl.boneMapper,'ValveBiped.Bip01_'
local s=BM.NewState('convert',{probe=PROBE(true),filename='hero.fbx',source='C:/m/hero.fbx'})
BM.Validate(s)
local sum=BM.Summary(s)
assert(sum.headline=='ok',sum.headline) assert(BM.StatusOf(s,VB..'L_Hand')=='ok')
assert(BM.StatusOf(s,VB..'L_Finger0')=='created' and BM.StatusOf(s,'Eye_L')=='created')
-- An unsure guess must be checked.
local g=BM.NewState('convert',{probe=PROBE(false),filename='hero.fbx'}) BM.Validate(g)
assert(BM.Summary(g).headline=='check' and BM.StatusOf(g,VB..'L_Hand')=='check')
assert(BM.NextSlot(g,nil,1,true)==VB..'L_Hand')
BM.AcceptGuess(g,VB..'L_Hand') BM.Validate(g) assert(BM.StatusOf(g,VB..'L_Hand')=='ok' and BM.NextSlot(g,VB..'L_Hand',1,true)==VB..'R_Hand')
-- Assigning a used bone moves it and empties the other part; undo restores both.
local hand=s.slots[VB..'L_Hand'].bone
local moved=BM.Assign(s,VB..'R_Hand',hand,'user') BM.Validate(s)
assert(moved==VB..'L_Hand' and BM.Value(s,VB..'L_Hand')==-1 and BM.StatusOf(s,VB..'L_Hand')=='missing')
assert(BM.Summary(s).headline=='missing' and BM.NextSlot(s,nil)==VB..'L_Hand')
assert(BM.Undo(s) and BM.Value(s,VB..'L_Hand')==hand and BM.CanRedo(s)) assert(BM.Redo(s) and BM.Value(s,VB..'L_Hand')==-1)
BM.Undo(s)
-- The next missing part of the same chain comes first.
BM.Clear(s,VB..'L_Forearm') BM.Clear(s,VB..'L_Hand') BM.Clear(s,VB..'R_Thigh') BM.Validate(s)
assert(BM.NextSlot(s,VB..'L_Forearm')==VB..'L_Hand' and BM.NextSlot(s,VB..'L_Hand')==VB..'R_Thigh')
-- Fifty undo steps.
for i=1,60 do BM.Assign(s,VB..'Spine2',i%2==0 and -1 or s.byName['Chest'],'user') end
local steps=0 while BM.Undo(s) do steps=steps+1 end assert(steps==50,steps)
SAY('PASS: statuses, summary headlines, guesses, moving a used bone, the next part to assign, 50-step undo')
''')

# ---- validation rules beyond the structural ones ----
lua.execute(r'''
local BM,VB=mmdhl.boneMapper,'ValveBiped.Bip01_'
local function fresh() local s=BM.NewState('convert',{probe=PROBE(true),filename='x'}) return s end
local function has(s,code,slot) for _,i in ipairs(BM.Validate(s)) do if i.code==code and (slot==nil or i.slot==slot) then return i end end end
-- Swapping left and right with the picture keeps every side relation and turns the
-- character around: a correct one then faces backwards.
local s=fresh() BM.SwapSides(s) assert(s.turn==180)
assert(not has(s,'sides_swapped') and not has(s,'side') and has(s,'facing'),'the turned picture keeps the sides consistent')
-- Parts assigned to the other side: one problem, fixed by a relabel without turning.
s=fresh() for _,slot in ipairs(BM.Slots) do if slot.side=='L' and not slot.convertOnly then local l,r=s.slots[slot.key],s.slots[slot.partner] s.slots[slot.key],s.slots[slot.partner]=r,l end end
assert(has(s,'sides_swapped') and not has(s,'side',VB..'L_Hand') and not has(s,'facing'))
BM.SwapSides(s,false) assert(not has(s,'sides_swapped') and not has(s,'side') and s.turn==0)
-- A mirrored export (left names on the right, toes backwards): swapping with the picture fixes it.
s=fresh() for _,b in ipairs(s.bones) do b.pos[3]=-b.pos[3] end
assert(has(s,'facing') and not has(s,'sides_swapped'))
BM.SwapSides(s) assert(not has(s,'facing') and not has(s,'sides_swapped') and not has(s,'side') and s.turn==180)
-- One part on the wrong side.
s=fresh() local l,r=s.slots[VB..'L_Clavicle'],s.slots[VB..'R_Clavicle'] s.slots[VB..'L_Clavicle'],s.slots[VB..'R_Clavicle']=r,l
local i=has(s,'side',VB..'L_Clavicle') assert(i and i.severity=='warning' and i.mirror==l.bone)
-- Heights, lengths, twist and weight.
s=fresh() BM.Assign(s,VB..'L_Hand',s.byName['LeftForeArmTwist'],'user')
assert(has(s,'twist',VB..'L_Hand') and has(s,'length',VB..'L_Forearm'))
s=fresh() BM.Assign(s,VB..'Neck1',s.byName['Skirt'],'user') assert(has(s,'order',VB..'Neck1').severity=='warning' and has(s,'order',VB..'Head1').severity=='error' and has(s,'height',VB..'Neck1'))
s=fresh() BM.Assign(s,VB..'L_Calf',s.byName['Skirt'],'user') local u=has(s,'unweighted',VB..'L_Calf') assert(u==nil,'a part keeps the weight of the unassigned bones below it')
s=fresh() local strand=s.byName['Skirt_F_01'] BM.Assign(s,VB..'Spine2',strand,'user') assert(has(s,'order',VB..'Spine2').severity=='warning')
-- Optional parts left empty are listed once.
s=fresh() BM.Clear(s,VB..'Neck1') BM.Clear(s,VB..'L_Toe0') local rec=has(s,'recommended') assert(rec and #rec.args.parts==2)
s=fresh() BM.Clear(s,VB..'L_Finger12') assert(has(s,'finger_partial'))
-- A picture turned the wrong way shows the sides swapped and the toes backwards.
s=fresh() s.turn=180 assert(has(s,'facing') and has(s,'sides_swapped'))
SAY('PASS: side, swapped sides, facing, height, length, twist, weight, recommended and finger rules')
''')

# ---- suggestions, regions, mirror ----
lua.execute(r'''
local BM,VB=mmdhl.boneMapper,'ValveBiped.Bip01_'
local s=BM.NewState('convert',{probe=PROBE(true),filename='x'})
BM.Clear(s,VB..'L_Forearm') BM.Validate(s)
local list=BM.Candidates(s,VB..'L_Forearm',3)
assert(#list>=1 and #list<=3 and list[1].bone==s.byName['LeftForeArm'] and BM.BestMatch(list),'the forearm is the best match')
local reasons={} for _,r in ipairs(list[1].reasons) do reasons[r.key]=true end assert(reasons['bonemap.reason.named'] and reasons['bonemap.reason.child'])
for _,c in ipairs(list) do assert(c.bone~=s.byName['Hair_end'],'helpers without weight are never suggested') end
local twistRank for i,c in ipairs(BM.Candidates(s,VB..'L_Forearm',10)) do if c.bone==s.byName['LeftForeArmTwist'] then twistRank=i end end
assert(twistRank==nil or twistRank>1)
s.bones[s.byName['LeftForeArm']+1].flags.secondary=true local again=BM.Candidates(s,VB..'L_Forearm',3) local physics=false for _,r in ipairs(again[1].reasons) do physics=physics or r.key=='bonemap.reason.physics' end
assert(again[1].score==list[1].score-50 and physics,'a bone moved by physics loses 50 points and says why')
s.bones[s.byName['LeftForeArm']+1].flags.secondary=nil
-- Regions follow the nearest assigned ancestor.
BM.Assign(s,VB..'L_Forearm',s.byName['LeftForeArm'],'user')
local regions=BM.Regions(s) assert(regions[s.byName['LeftForeArmTwist']]=='left' and regions[s.byName['Hair_01']]=='body' and regions[s.byName['RightHand']]=='right')
-- Mirror copies through the bones' partners, keeping parts set by hand.
BM.Clear(s,VB..'R_Hand') BM.Clear(s,VB..'R_Forearm') BM.Assign(s,VB..'R_Calf',s.byName['RightLeg'],'user')
local copied,unmatched,kept=BM.Mirror(s,'L')
assert(copied==2 and BM.Value(s,VB..'R_Hand')==s.byName['RightHand'] and #kept==1 and kept[1]==VB..'R_Calf')
-- Effective weight counts unassigned bones below a part.
assert(BM.EffectiveWeight(s,VB..'L_Forearm')==110)
SAY('PASS: ranked suggestions with reasons, regions, mirror (keeping hand-set parts), effective weight')
''')

# ---- swinging parts ----
lua.execute(r'''
local BM,VB=mmdhl.boneMapper,'ValveBiped.Bip01_'
local s=BM.NewState('convert',{probe=PROBE(true),filename='x'})
BM.RefreshChains(s)
local roots={} for _,c in ipairs(s.chains) do roots[s.bones[c.root+1].name]=c end
assert(roots.Hair_01 and roots.Hair_01.kind=='hair' and roots.Hair_01.enabled,'hair is found by name')
assert(roots.Skirt_F_01 and roots.Skirt_B_01 and not roots.Skirt,'an unweighted skirt node splits into strands')
local g=BM.GroupOf(s,'hair') assert(g and g.enabled and g.swing=='normal' and g.collide)
local v=BM.GroupValues({kind='hair',swing='less'}) assert(v.stiffness==1.5 and math.abs(v.dragForce-.55)<1e-6)
v=BM.GroupValues({kind='skirt',swing='more'}) assert(math.abs(v.stiffness-.48)<1e-6 and math.abs(v.dragForce-.35)<1e-6 and math.abs(v.gravityPower-.39)<1e-6)
-- A recompute keeps the player's choices.
roots.Hair_01.enabled=false BM.Assign(s,VB..'Spine2',-1,'user') BM.RefreshChains(s)
for _,c in ipairs(s.chains) do if s.bones[c.root+1].name=='Hair_01' then assert(not c.enabled) end end
-- Body bones never swing.
local ok,why=BM.AddChain(s,s.byName['LeftArm']) assert(not ok and why=='bonemap.jiggle.refused')
local ok2,why2=BM.AddChain(s,s.byName['Hair_02']) assert(ok2) -- a part of a disabled chain may be added on its own
local okc,whyc=BM.AddChain(s,s.byName['Skirt_F_02']) assert(not okc and whyc=='bonemap.jiggle.included')
-- The request holds every part by name and the swinging parts with their values.
local r=BM.Request(s) local n=0 for k,name in pairs(r.boneMap) do n=n+1 end
assert(n==52 and r.boneMap[VB..'Pelvis']=='Hips' and r.boneMap[VB..'Spine2']=='' and r.kind=='character' and r.requestVersion==1)
local hairGroup for _,grp in ipairs(r.jiggle.groups) do if grp.kind=='hair' then hairGroup=grp end end
assert(hairGroup and hairGroup.values.stiffness==1 and #hairGroup.chains>=1)
SAY('PASS: swinging part suggestions, strands, presets, recompute, refusals and the import request')
''')

# ---- memory ----
lua.execute(r'''
local BM,VB=mmdhl.boneMapper,'ValveBiped.Bip01_'
DISK={}
local s=BM.NewState('convert',{probe=PROBE(true),filename='hero.fbx'})
BM.Assign(s,VB..'Spine2',s.byName['Chest'],'user') BM.Assign(s,VB..'Spine4',s.byName['Neck'],'user')
BM.SaveMemory(s)
assert(DISK['mmd_hotloader/bone_maps/sources/'..string.rep('a',64)..'.json'] and DISK['mmd_hotloader/bone_maps/skeletons/'..string.rep('b',64)..'.json'])
local again=BM.NewState('convert',{probe=PROBE(true),filename='hero.fbx'})
local applied,notice=BM.ApplyMemory(again,BM.LoadMemory(PROBE(true)))
assert(applied>40 and notice=='saved_file' and BM.Value(again,VB..'Spine2')==s.byName['Chest'] and again.slots[VB..'Spine2'].origin=='saved')
-- The same skeleton in another file: remembered; bones that do not exist are skipped.
local other=PROBE(true) other.sourceSha256=string.rep('c',64)
local data=util.JSONToTable(DISK['mmd_hotloader/bone_maps/skeletons/'..string.rep('b',64)..'.json']) data.boneMap[VB..'L_Toe0']='NoSuchBone'
DISK['mmd_hotloader/bone_maps/skeletons/'..string.rep('b',64)..'.json']=util.TableToJSON(data)
local r=BM.NewState('convert',{probe=other,filename='copy.fbx'}) local n2,notice2=BM.ApplyMemory(r,BM.LoadMemory(other))
assert(notice2=='same_skeleton' and r.slots[VB..'L_Toe0'].origin=='auto' and r.slots[VB..'Spine2'].origin=='remembered' and r.slots[VB..'Neck1'].bone==-1)
-- Damaged and newer files are ignored, with one console line.
DISK['mmd_hotloader/bone_maps/sources/'..string.rep('a',64)..'.json']='{not json'
DISK['mmd_hotloader/bone_maps/skeletons/'..string.rep('b',64)..'.json']=util.TableToJSON({version=2,boneMap={}})
local m=BM.LoadMemory(PROBE(true)) assert(m.source==nil and m.skeleton==nil and #PRINTED>=2)
-- At most 256 remembered skeletons, the oldest go first.
DISK={} for i=1,258 do DISK[string.format('mmd_hotloader/bone_maps/skeletons/%064x.json',i)]=util.TableToJSON({version=1,savedAt=i,boneMap={}}) end
BM.SaveMemory(s) local count=0 for p in pairs(DISK) do if p:find('skeletons/') then count=count+1 end end
assert(count==256 and DISK[string.format('mmd_hotloader/bone_maps/skeletons/%064x.json',1)]==nil)
SAY('PASS: memory per file and per skeleton, missing bones skipped, damaged and newer files ignored, 256-file cap')
''')

# ---- fit mode: pins are the difference from the fitter's own choice ----
lua.execute(r'''
local BM,VB=mmdhl.boneMapper,'ValveBiped.Bip01_'
local bones=BONES()
local proposal={bones={}} for _,s in ipairs(BM.Slots) do if not s.convertOnly then proposal.bones[#proposal.bones+1]={name=s.key,mmd=-1,provenance='synthesized'} end end
local idx={} for i,b in ipairs(bones) do idx[b.name]=i-1 end
local function setp(key,n) for _,b in ipairs(proposal.bones) do if b.name==VB..key then b.mmd=idx[n] b.provenance='name' end end end
setp('Pelvis','Hips') setp('Spine1','Spine') setp('Spine4','Chest') setp('Head1','Head') setp('L_UpperArm','LeftArm') setp('L_Forearm','LeftForeArm') setp('L_Hand','LeftHand')
setp('R_UpperArm','RightArm') setp('R_Forearm','RightForeArm') setp('R_Hand','RightHand') setp('R_Thigh','RightUpLeg') setp('R_Calf','RightLeg') setp('R_Foot','RightFoot') setp('L_Foot','LeftFoot')
local inspect={slots={[VB..'L_Thigh']={bone=idx['LeftUpLeg'],confidence=.8},[VB..'L_Calf']={bone=idx['LeftLeg'],confidence=.9}}}
local s=BM.NewState('fit',{asset=string.rep('d',64),name='Hero',skeleton={bones=bones,points={}},auto=inspect,proposal=proposal,current=proposal,pins={},reason='rescue'})
BM.Validate(s)
assert(s.slots[VB..'L_Thigh'].origin=='guess' and BM.StatusOf(s,VB..'L_Thigh')=='check' and BM.Summary(s).headline=='check')
assert(BM.StatusOf(s,'Eye_L')=='disabled' or s.slots['Eye_L']==nil)
local pins=BM.Pins(s) assert(pins[VB..'L_Thigh']==idx['LeftUpLeg'] and pins[VB..'L_Calf']==idx['LeftLeg'] and pins['Eye_L']==nil)
local count=0 for _ in pairs(pins) do count=count+1 end assert(count==2)
BM.UseAuto(s,VB..'L_Thigh') assert(BM.Value(s,VB..'L_Thigh')==idx['LeftUpLeg'])
assert(BM.SamePins({a=1},{a=1}) and not BM.SamePins({a=1},{a=2}))
SAY('PASS: fit mode starts from the fitter, guesses the missing parts, and saves only pins')
''')

# ---- projection, picking, keys ----
lua.execute(r'''
local BM=mmdhl.boneMapper
local v={mode='front',turn=0,cx=100,cy=100,mx=0,my=1,mz=0,scale=50}
local x,y=BM.Project(v,.2,1.5,0) assert(x==110 and y==75)
v.turn=180 x=BM.Project(v,.2,1.5,0) assert(x==90)
v.mode='side' v.turn=0 x=BM.Project(v,0,1,.5) assert(x==75)
local best,list=BM.HitTest({{bone=1,x=10,y=10,weight=5},{bone=2,x=12,y=10,weight=9},{bone=3,x=40,y=40}},11,10,8,6,{})
assert((best==1 or best==2) and list and list[1]==2 and list[2]==1)
local only=BM.HitTest({{bone=3,x=40,y=40}},41,40,8,6) assert(only==3)
assert(BM.KeyAction('z',true,false,false)=='undo' and BM.KeyAction('z',true,true,false)=='redo' and BM.KeyAction('y',true,false,false)=='redo')
assert(BM.KeyAction('tab',false,false,false)=='next_slot' and BM.KeyAction('tab',false,true,false)=='prev_slot' and BM.KeyAction('enter',false,false,false)=='enter')
assert(BM.KeyAction('delete',false,false,false)=='clear' and BM.KeyAction('f',true,false,false)=='search' and BM.KeyAction('f',false,false,false)=='fit')
assert(BM.KeyAction('1',false,false,false)=='view_front' and BM.KeyAction('z',true,false,true)==nil and BM.KeyAction('escape',false,false,false)==nil)
SAY('PASS: projection (front, side, turned), picking with the overlap list, keyboard actions')
''')

# ---- the library flow (bone_mapper_ui.lua, client) ----
UI = (ROOT / 'addon/lua/mmdhl/bone_mapper_ui.lua').read_text(encoding='utf-8')
LIBRARY = (ROOT / 'addon/lua/mmdhl/library.lua').read_text(encoding='utf-8')
lua.execute(r'''
CLIENT=true SERVER=false
Color=function(r,g,b,a) return {r=r,g=g,b=b,a=a or 255} end
AUTOSKIP=false
CreateClientConVar=function() return {GetBool=function() return AUTOSKIP end} end
NET={} net={Receive=function(name,fn) NET[name]=fn end}
TIMERS={} timer={Simple=function(_,fn) TIMERS[#TIMERS+1]=fn end,Create=function() end,Remove=function() end}
function RUN_TIMERS() local list=TIMERS TIMERS={} for _,fn in ipairs(list) do fn() end end
NOTES={} notification={AddLegacy=function(text) NOTES[#NOTES+1]=text end}
HOOKS={} HOOKED={} hook={Add=function(event,name,fn) HOOKS[event..'/'..name]=fn end,Run=function(name,...) HOOKED[#HOOKED+1]={name,...} end}
string.GetFileFromFilename=function(p) return (p:match('[^/\\]+$')) end
math.Clamp=function(v,a,b) return math.max(a,math.min(b,v)) end
IsValid=function(v) return v~=nil and v~=false and not (type(v)=='table' and v.removed) end
game={SinglePlayer=function() return true end}
NOW=0 RealTime=function() NOW=NOW+1 return NOW end
mmdhl.Decode=function(v,err) if v==nil then return nil,err end if type(v)=='string' then return util.JSONToTable(v) end return v end
mmdhl.HumanoidLandmarks=function(info) return info.found or 0,info.bones or 0 end
CALLS={}
mmdhl.native={
 GetCapabilities=function() return util.TableToJSON({characterImport={version=1,probeVersion=1,requestVersion=1,formats={'fbx','glb','gltf','dae'}}}) end,
 BeginImport=function(source,json) CALLS[#CALLS+1]={source=source,options=util.JSONToTable(json)} return #CALLS end}
mmdhl.library={entries={}}
''')
lua.execute('local native,L=mmdhl.native,mmdhl.L local library=mmdhl.library ' + definition(lua, LIBRARY, 'function library.StartImport'))
lua.execute(UI)
lua.execute(r'''
DISK={}
local BM,VB,library=mmdhl.boneMapper,'ValveBiped.Bip01_',mmdhl.library
assert(BM.Available('convert') and BM.Convertible('C:/m/Hero.FBX') and BM.Convertible('a.glb') and not BM.Convertible('a.pmx') and not BM.Convertible('a.obj'))
assert(not BM.Available('fit'),'fit mode needs the fitter pins (GetBoneMapProposal) and InspectBoneMap')
local src='C:/m/hero.fbx'
-- The probe job.
assert(BM.Probe(src) and library.job==1 and library.jobKind=='character_probe' and CALLS[1].options.kind=='character_probe')
assert(library.status==mmdhl.L'library.import.reading_skeleton')
assert(BM.OnJobStatus({state='running',stage='Reading the skeleton',stageCode='probe',progress=.5,filename='hero.fbx'}) and library.progress==.5)
assert(BM.OnJobStatus({state='running',stage='Converting the character',stageCode='convert_character'}) and library.status==mmdhl.L'bonemap.stage.converting')
assert(not BM.OnJobStatus({state='running',stage='Writing',stageCode='write'}))
-- A finished probe opens the window (one tick later) with the automatic map.
local shown={} BM.ShowWindow=function(state,opts) shown[#shown+1]={state=state,opts=opts or {}} end
local status={state='complete',kind='bone_map',source=src,filename='hero.fbx',probe=PROBE(true)}
assert(BM.OnJobStatus(status) and library.job==nil and library.status==mmdhl.L('bonemap.status.waiting',{file='hero.fbx'}))
assert(#shown==0) RUN_TIMERS() assert(#shown==1 and shown[1].state.mode=='convert' and shown[1].state.source==src and BM.sessions[src]==shown[1].state)
-- Cancel keeps the session: picking the same file again restores it.
local session=BM.sessions[src] BM.Assign(session,VB..'Spine2',session.byName['Chest'],'user')
BM.OnJobStatus(status) RUN_TIMERS() assert(shown[2].state==session and BM.Value(session,VB..'Spine2')==session.byName['Chest'])
-- A probe newer than this Lua is refused.
local newer=PROBE(true) newer.version=2
assert(BM.OnJobStatus({state='complete',kind='bone_map',source='C:/m/new.fbx',probe=newer}) and #TIMERS==0 and NOTES[#NOTES]==mmdhl.L'bonemap.too_new')
-- The converter rejects the map: the window reopens on the slot with the native sentence.
library.job=5
assert(BM.OnJobStatus({state='failed',kind='character',source=src,filename='hero.fbx',error='Bone X is used twice.',errorCode='character.bone_map',errorDetails={slot=VB..'L_Hand',reason='duplicate'}}))
assert(library.job==nil) RUN_TIMERS()
local last=shown[#shown] assert(last.state==session and last.opts.select==VB..'L_Hand' and last.opts.native.code=='native' and last.opts.native.args.message=='Bone X is used twice.')
-- A bone that is gone (the file changed, or Reload): probe again, then reopen with the notice.
local before=#CALLS
assert(BM.OnJobStatus({state='failed',kind='character',source=src,errorCode='character.bone_map',errorDetails={slot=VB..'L_Hand',reason='missing'}}))
assert(#CALLS==before+1 and CALLS[#CALLS].options.kind=='character_probe' and BM.pending[src].notice=='file_changed')
library.job=nil
local changed=PROBE(true) changed.sourceSha256=string.rep('e',64)
BM.OnJobStatus({state='complete',kind='bone_map',source=src,probe=changed}) RUN_TIMERS()
last=shown[#shown] assert(last.state~=session and last.state.notice.key=='file_changed' and last.opts.select==VB..'L_Hand' and BM.pending[src]==nil)
-- The import finished: the session goes and the closed hook says imported; the library's own branch runs too.
HOOKED={}
assert(not BM.OnJobStatus({state='complete',source=src,asset=string.rep('f',64),info={}}))
assert(BM.sessions[src]==nil and HOOKED[1][1]=='MMDHL.BoneMapClosed' and HOOKED[1][3]=='imported')
-- Other failures go to the library's failure dialog.
assert(not BM.OnJobStatus({state='failed',kind='character_probe',source=src,errorCode='character.parse'}))
-- Skip option: a probe with nothing to check imports at once with the automatic map.
AUTOSKIP=true before=#CALLS
assert(BM.OnJobStatus({state='complete',kind='bone_map',source='C:/m/auto.fbx',filename='auto.fbx',probe=PROBE(true)}))
assert(#TIMERS==0 and #CALLS==before+1 and CALLS[#CALLS].options.kind=='character' and CALLS[#CALLS].options.boneMap[VB..'Pelvis']=='Hips' and NOTES[#NOTES]==mmdhl.L'bonemap.auto_imported')
library.job=nil
-- ...but a guess still opens the window.
local guess=PROBE(false) guess.sourceSha256=string.rep('d',64) guess.skeleton.signature=string.rep('d',64)
assert(BM.OnJobStatus({state='complete',kind='bone_map',source='C:/m/guess.fbx',probe=guess})) assert(#TIMERS==1) RUN_TIMERS()
AUTOSKIP=false
-- The public entry: a probe status opens the convert window; fit mode needs the newer native.
assert(mmdhl.OpenBoneMapper({probe={probe=PROBE(true),source='C:/m/api.fbx',filename='api.fbx'}}) and shown[#shown].state.source=='C:/m/api.fbx')
NOTES={} assert(mmdhl.OpenBoneMapper({asset=string.rep('a',64)})==false and NOTES[1]==mmdhl.L'bonemap.update_needed')
-- A probe without its file (the conversion reads the file again) does not open, and raises nothing.
local count=#shown
assert(mmdhl.OpenBoneMapper({probe=PROBE(true)})==false and mmdhl.OpenBoneMapper({probe={state='complete',source='C:/m/x.fbx'}})==false and #shown==count)
assert(BM.OpenConvert({probe=PROBE(true)},{})==false and #shown==count)
-- A window the player is working in (fit mode opened while the probe ran) is never replaced:
-- the finished probe waits and opens when that window closes.
BM.frame={} NOTES={}
assert(BM.OnJobStatus({state='complete',kind='bone_map',source='C:/m/wait.fbx',filename='wait.fbx',probe=PROBE(true)})) RUN_TIMERS()
assert(#shown==count and BM.queued~=nil and NOTES[#NOTES]==mmdhl.L('bonemap.status.waiting',{file='wait.fbx'}))
BM.frame=nil BM.RunQueued() assert(BM.queued==nil) RUN_TIMERS()
assert(#shown==count+1 and shown[#shown].state.source=='C:/m/wait.fbx')
-- The same for a converter rejection that reopens a session.
BM.frame={} library.job=6
assert(BM.OnJobStatus({state='failed',kind='character',source='C:/m/wait.fbx',filename='wait.fbx',error='Bone X is used twice.',errorCode='character.bone_map',errorDetails={slot=VB..'L_Hand',reason='duplicate'}}))
RUN_TIMERS() assert(#shown==count+1 and BM.queued~=nil)
BM.frame=nil BM.RunQueued() RUN_TIMERS() assert(#shown==count+2 and shown[#shown].opts.select==VB..'L_Hand')
SAY('PASS: probe job, window on a finished probe, kept session, newer probes, converter rejections, file changes, skip option, public entry, no window replaced')
''')

# ---- failures with a better hint: an older native, and a DAE without a skeleton ----
lua.execute('local library=mmdhl.library ' + definition(lua, LIBRARY, 'local staticTypes=') + definition(lua, LIBRARY, 'function library.StaticImportable'))
lua.execute(r'''
local library=mmdhl.library
assert(library.StaticImportable('C:/m/Box.FBX') and library.StaticImportable('a.blend') and not library.StaticImportable('C:/m/box.dae') and not library.StaticImportable('noext'))
CAPS=mmdhl.native.GetCapabilities mmdhl.native.GetCapabilities=nil
''')
lua.execute(UI)
lua.execute(r'''
local BM,library=mmdhl.boneMapper,mmdhl.library
assert(not BM.Available('convert'))
local old='This file is not a PMX, PMD or VRM character. Static 3D models (OBJ, FBX, glTF, BLEND) belong in Static Props.'
-- An FBX picked through "All files" with natives older than 2.3.0: the update reminder and hint.
library.jobKind='library' NOTES={}
local status={state='failed',source='C:/m/hero.fbx',filename='hero.fbx',error=old}
assert(not BM.OnJobStatus(status) and status.hint==mmdhl.L'library.hint.character_update' and NOTES[1]==mmdhl.L'library.hint.character_update')
local reminded mmdhl.ShowNativeUpdateNeeded=function(feature,version) reminded={feature,version} end
status={state='failed',source='C:/m/hero.glb',error=old} BM.OnJobStatus(status)
assert(reminded[1]==mmdhl.L'bonemap.feature_convert' and reminded[2]=='2.3.0' and status.hint)
mmdhl.ShowNativeUpdateNeeded=nil
-- Not for a PMX, nor for a static prop import.
status={state='failed',source='C:/m/hero.pmx',error=old} BM.OnJobStatus(status) assert(status.hint==nil)
library.jobKind='static' status={state='failed',source='C:/m/hero.fbx',error=old} BM.OnJobStatus(status) assert(status.hint==nil)
library.jobKind='character_probe'
mmdhl.native.GetCapabilities=CAPS
''')
lua.execute(UI)
lua.execute(r'''
local BM,library=mmdhl.boneMapper,mmdhl.library
assert(BM.Available('convert'))
-- A DAE without a skeleton cannot become a static prop either: export it first.
local status={state='failed',kind='character_probe',source='C:/m/box.dae',errorCode='character.no_skeleton'}
assert(not BM.OnJobStatus(status) and status.hint==mmdhl.L'library.hint.character_no_skeleton_export')
status={state='failed',kind='character_probe',source='C:/m/box.fbx',errorCode='character.no_skeleton'} BM.OnJobStatus(status) assert(status.hint==nil)
library.jobKind=nil
SAY('PASS: older natives get the update reminder for FBX, glTF and DAE characters; a DAE without a skeleton is told to export first')
''')

# ---- after an import or a failed spawn: the fitter (with saved pins) decides ----
lua.execute(r'''
local BM,VB,library=mmdhl.boneMapper,'ValveBiped.Bip01_',mmdhl.library
local id=string.rep('9',64)
local FIT={} library.SetFitStatus=function(asset,fit) FIT[#FIT+1]={asset,fit} end
local prompts={} BM.ShowRescuePrompt=function(asset,name,missing) prompts[#prompts+1]={asset,name,missing} end
-- Without the fitter pins only the badge is set from the import's fit block.
BM.AfterImport({asset=id,info={name='Hero',found=8,bones=40},fit={ok=false,errorCode='fit.landmarks',missing={VB..'L_Thigh'}}})
assert(FIT[1][2].ok==false and FIT[1][2].missing[1]==VB..'L_Thigh' and #prompts==0)
-- Models that do not look like characters keep the static-prop question.
FIT={} BM.AfterImport({asset=id,info={name='Box',found=1,bones=3},fit={ok=false,errorCode='fit.landmarks',missing={VB..'L_Thigh'}}}) assert(#FIT==0)
-- With the fitter pins the saved pins are applied first.
local proposals={}
RAW={} mmdhl.native.GetBoneMapProposal=function(asset,json) RAW[#RAW+1]=json proposals[#proposals+1]=util.JSONToTable(json) return util.TableToJSON({missing=MISSING or {}}) end
mmdhl.native.InspectBoneMap=function() return '{}' end
mmdhl.native.RequestAsset=function() return true end mmdhl.native.AssetInfo=function() return util.TableToJSON({name='Hero',found=8,bones=40}) end
local queued={} timer.Create=function(name,_,_,fn) queued[#queued+1]=fn end
assert(BM.Available('fit'))
DISK['mmd_hotloader/fit_overrides/'..id..'.json']='{"version":3,"generator":18,"boneMap":{"ValveBiped.Bip01_L_Thigh":12.0}}'
MISSING={VB..'L_Calf'} FIT={}
BM.AfterImport({asset=id,info={name='Hero',found=8,bones=40},fit={ok=false,errorCode='fit.landmarks',missing={VB..'L_Thigh'}}},function() error('the check worked') end)
for _,fn in ipairs(queued) do fn() end queued={}
assert(proposals[1].boneMap[VB..'L_Thigh']==12 and FIT[1][2].missing[1]==VB..'L_Calf' and prompts[1][1]==id and prompts[1][3][1]==VB..'L_Calf')
assert(RAW[1]=='{"boneMap":{"ValveBiped.Bip01_L_Thigh":12}}','pins reach the native as whole numbers')
-- On a dedicated server the pins are the server's: the client asks for them.
game={SinglePlayer=function() return false end} LocalPlayer=function() return {IsListenServerHost=function() return false end} end
local asked={} mmdhl.Action=function(action,asset) asked[#asked+1]={action,asset} end
local got BM.ServerPins(id,function(pins,collision) got={pins,collision} end) BM.ServerPins(id,function() end)
assert(#asked==1 and asked[1][1]=='bonemap_pins' and asked[1][2]==id and got==nil)
local reads={id,'{"boneMap":{"ValveBiped.Bip01_L_Calf":7.0,"Nonsense":3}}',true}
net.ReadString=function() return table.remove(reads,1) end net.ReadBool=function() return table.remove(reads,1) end
NET['mmdhl_bonemap_pins']() assert(got[1][VB..'L_Calf']==7 and got[1].Nonsense==nil and got[2]==true and BM.pinQueries[id]==nil)
-- No answer: the callback learns it.
queued={} got=false BM.ServerPins(id,function(pins) got=pins end) for _,fn in ipairs(queued) do fn() end queued={} assert(got==nil and BM.pinQueries[id]==nil)
game={SinglePlayer=function() return true end}
-- The saved pins fixed it: the badge goes, no prompt.
MISSING={} FIT={} prompts={}
BM.CheckRescue(id) for _,fn in ipairs(queued) do fn() end queued={}
assert(FIT[1][2].ok==true and #prompts==0)
-- The check itself fails (the model does not load): the import's caller learns it, to explain the fit itself.
local failures=0 mmdhl.native.RequestAsset=function() return false,'gone' end FIT={} prompts={}
BM.AfterImport({asset=id,info={name='Hero',found=8,bones=40},fit={ok=false,errorCode='fit.landmarks',missing={VB..'L_Thigh'}}},function() failures=failures+1 end)
assert(failures==1 and #FIT==0 and #prompts==0)
mmdhl.native.RequestAsset=function() return true end
-- A fit that worked clears the badge.
FIT={} BM.AfterImport({asset=id,info={name='Hero',found=8,bones=40},fit={ok=true}}) assert(FIT[1][2].ok==true)
mmdhl.native.GetBoneMapProposal=nil mmdhl.native.InspectBoneMap=nil
SAY('PASS: after an import or a failed spawn the fitter with the saved pins decides the badge and the rescue prompt')
''')

# ---- library.lua: routing, the Think hook branch and the bone window in front ----
lua.execute('PICKER={} pickerNotice=function(v) PICKER[#PICKER+1]=v end promptStaticInstead=function() end promptCharacterInstead=function() end '
            'table.Copy=function(t) local c={} for k,v in pairs(t) do c[k]=type(v)=="table" and table.Copy(v) or v end return c end')
for header in ("hook.Add('Think','MMDHL.LibraryImport'", 'function library.BrowseImport', 'local function startImport', 'function library.SetFitStatus'):
    chunk = definition(lua, LIBRARY, header)
    if header.startswith('local function'):
        chunk += '\nSTART_IMPORT=startImport'
    lua.execute('local native,L=mmdhl.native,mmdhl.L local library=mmdhl.library '
                'local function validId(id) return type(id)=="string" and #id==64 and not id:find("[^a-f0-9]") end '
                'local function writeSettings(id,settings) WROTE={id,settings} return true end ' + chunk)
lua.execute(r'''
local BM,library=mmdhl.boneMapper,mmdhl.library
local think=HOOKS['Think/MMDHL.LibraryImport']
-- A picked FBX goes to the probe; a PMX imports as before.
CALLS={} library.job=9 mmdhl.native.PollJob=function() return {state='selected',source='C:/m/pick.fbx'} end
think() assert(CALLS[1].source=='C:/m/pick.fbx' and CALLS[1].options.kind=='character_probe' and library.status==mmdhl.L'library.import.reading_skeleton')
mmdhl.native.PollJob=function() return {state='selected',source='C:/m/pick.pmx'} end library.job=9 library.nextPoll=0
think() assert(CALLS[2].source=='C:/m/pick.pmx' and next(CALLS[2].options)==nil)
-- The bone window's statuses never reach the generic branches.
library.job=9 library.nextPoll=0 BM.ShowWindow=function() end
mmdhl.native.PollJob=function() return {state='complete',kind='bone_map',source='C:/m/x.fbx',probe=PROBE(true)} end
think() assert(library.job==nil and library.status==mmdhl.L('bonemap.status.waiting',{file='x.fbx'})) RUN_TIMERS()
-- Retry and "import as character" route through the probe too.
CALLS={} START_IMPORT('library','C:/m/again.glb') assert(CALLS[1].options.kind=='character_probe')
CALLS={} library.job=nil START_IMPORT('library','C:/m/again.vrm') assert(next(CALLS[1].options)==nil)
-- Import while the bone window is open brings it to the front.
library.job=nil local popped=false BM.frame={MakePopup=function() popped=true end}
mmdhl.native.Browse=function() error('the picker must not open') end
assert(library.BrowseImport()==false and popped and library.status==mmdhl.L'bonemap.window_open')
BM.frame=nil
-- The badge state: set on a failure, removed on success, nothing written when already clear.
local id=string.rep('8',64) library.entries[id]={id=id,settings={folder='x'}} library.Refresh=function() end
WROTE=nil assert(library.SetFitStatus(id,{ok=true}) and WROTE==nil)
assert(library.SetFitStatus(id,{ok=false,missing={'a'}}) and WROTE[2].fit.ok==false and WROTE[2].fit.missing[1]=='a' and WROTE[2].folder=='x')
library.entries[id].settings=WROTE[2] assert(library.SetFitStatus(id,{ok=true}) and WROTE[2].fit==nil and WROTE[2].folder=='x')
SAY('PASS: picked FBX files are probed, PMX files import as before, retries route through the probe, Import brings an open bone window to the front, fit badge state')
''')

# ---- server: saving the bones (bone_mapper.lua) ----
SHARED = (ROOT / 'addon/lua/mmdhl/bone_mapper.lua').read_text(encoding='utf-8')
lua.execute(r'''
CLIENT=false SERVER=true
AddCSLuaFile=function() end util.AddNetworkString=function() end include=function() end
''')
lua.execute(SHARED)
lua.execute(r'''
local BM,VB=mmdhl.boneMapper,'ValveBiped.Bip01_'
local id=string.rep('a',64) local path='mmd_hotloader/fit_overrides/'..id..'.json'
REPLIES={} local sending
net={Start=function() sending={} end,WriteString=function(v) sending[#sending+1]=v end,WriteBool=function(v) sending[#sending+1]=v end,Send=function(p) REPLIES[#REPLIES+1]=sending end}
CLOCK=100 SysTime=function() return CLOCK end
local admin=false local single=false
game={SinglePlayer=function() return single end}
local p={IsListenServerHost=function() return false end,IsAdmin=function() return admin end}
IsValid=function(v) return v~=nil end
hook={Run=function() return nil end}
local proposal={missing={},issues={},bones={{name=VB..'L_Thigh',mmd=12},{name=VB..'Pelvis',mmd=1}}}
local inspected
-- The fitter's own choice (no pins) and its resolved map with the pins (a torso repair put two parts on bone 1).
local base={missing={},issues={},bones={{name=VB..'L_Thigh',mmd=-1},{name=VB..'Pelvis',mmd=1},{name=VB..'Spine1',mmd=2}}}
proposal.bones[#proposal.bones+1]={name=VB..'Spine1',mmd=1}
PROPOSED={} RAWPROPOSED={}
mmdhl.native={GetBoneMapProposal=function(asset,json) RAWPROPOSED[#RAWPROPOSED+1]=json PROPOSED[#PROPOSED+1]=util.JSONToTable(json) return util.TableToJSON(json=='{}' and base or proposal) end,
 InspectBoneMap=function(asset,json) inspected=util.JSONToTable(json) return util.TableToJSON({issues={}}) end}
mmdhl.LoadAsset=function(asset,cb) cb({name='Hero'}) end
local function save(value) CLOCK=CLOCK+2 REPLIES={} BM.HandleSave(p,id,type(value)=='string' and value or util.TableToJSON(value)) return REPLIES[1] end
local function says(reply,key) return reply~=nil and reply[3]:find(key,1,true)~=nil end
-- Only single player, the listen host and admins; other addons can refuse.
local r=save({version=1,boneMap={[VB..'L_Thigh']=12}}) assert(r[1]==id and r[2]==false and says(r,'server.error.bonemap_permission'))
admin=true hook={Run=function(name) if name=='MMDHLCanEditBoneMap' then return false end end}
r=save({version=1,boneMap={}}) assert(r[2]==false and says(r,'bonemap_permission')) hook={Run=function() end}
-- One request per second.
save({version=1,boneMap={}}) REPLIES={} BM.HandleSave(p,id,util.TableToJSON({version=1,boneMap={}})) assert(says(REPLIES[1],'bonemap_request'))
-- Malformed requests.
assert(says(save('{"version":1,"boneMap":{}'),'bonemap_request'))
assert(says(save({version=2,boneMap={}}),'bonemap_request'))
assert(says(save({version=1,boneMap={Eye_L=3}}),'bonemap_request'),'eyes are convert-only')
assert(says(save({version=1,boneMap={[VB..'L_Thigh']=1.5}}),'bonemap_request'))
assert(says(save({version=1,boneMap={[VB..'L_Thigh']=-2}}),'bonemap_request'))
assert(says(save({version=1,boneMap={Nonsense=1}}),'bonemap_request'))
assert(says(save('{"version":1,"boneMap":{},"pad":"'..string.rep('x',17000)..'"}'),'bonemap_request'))
CLOCK=CLOCK+2 REPLIES={} BM.HandleSave(p,'../'..id,util.TableToJSON({version=1,boneMap={}})) assert(says(REPLIES[1],'bonemap_request'))
-- The server's own fitter must accept the pins.
proposal.missing={VB..'L_Calf'} r=save({version=1,boneMap={[VB..'L_Thigh']=12}}) assert(r[2]==false and says(r,'bonemap_invalid') and says(r,'bonemap.slot.left_lower_leg'))
proposal.missing={} proposal.issues={{code='duplicate',severity='error',text='Bone 12 is used twice'}}
r=save({version=1,boneMap={[VB..'L_Thigh']=12}}) assert(r[2]==false and says(r,'Bone 12 is used twice'))
proposal.issues={{code='band',severity='warning',text='aliased'}}
-- Saving merges into the collision corrections and checks the structure of what the window
-- showed: the fitter's own choice with the pins on top, not the fitter's repaired torso.
DISK[path]=util.TableToJSON({version=3,generator=18,bodies={a=1},scale=3.2,excludedMaterials={'m'}})
PROPOSED={} RAWPROPOSED={} r=save('{"version":1,"boneMap":{"ValveBiped.Bip01_Spine2":5.0},"dropCollision":false}')
assert(r[2]==true and says(r,'server.notice.bonemap_saved') and PROPOSED[1].boneMap[VB..'Spine2']==5 and RAWPROPOSED[2]=='{}')
assert(RAWPROPOSED[1]=='{"boneMap":{"ValveBiped.Bip01_Spine2":5}}','pins reach the fitter as whole numbers')
assert(inspected.values[VB..'Spine2']==5 and inspected.values[VB..'Spine1']==2 and inspected.values[VB..'Pelvis']==1 and inspected.values[VB..'L_Thigh']==-1)
local saved=util.JSONToTable(DISK[path]) assert(saved.boneMap[VB..'Spine2']==5 and saved.boneMapVersion==1 and saved.bodies.a==1 and saved.scale==3.2 and saved.version==3 and saved.generator==18)
-- A changed part with a body drops the collision corrections made for the old bones, even
-- when the client did not ask (its baseline may be stale); excluded materials stay.
r=save({version=1,boneMap={[VB..'L_Thigh']=12},dropCollision=false}) saved=util.JSONToTable(DISK[path])
assert(r[2]==true and saved.bodies==nil and saved.scale==nil and saved.excludedMaterials[1]=='m' and inspected.values[VB..'L_Thigh']==12)
DISK[path]=util.TableToJSON({version=3,generator=18,bodies={a=1},boneMap={[VB..'L_Thigh']=12}})
r=save({version=1,boneMap={[VB..'L_Thigh']=12,[VB..'Spine2']=5}}) saved=util.JSONToTable(DISK[path]) assert(saved.bodies.a==1,'unchanged bodies keep their corrections')
r=save({version=1,boneMap={[VB..'L_Thigh']=12},dropCollision=true}) saved=util.JSONToTable(DISK[path]) assert(saved.bodies==nil,'the client may still ask')
-- An empty map removes the pins without asking the fitter.
PROPOSED={} r=save({version=1,boneMap={}}) saved=util.JSONToTable(DISK[path])
assert(r[2]==true and #PROPOSED==0 and saved.boneMap==nil and saved.boneMapVersion==nil and saved.version==3)
-- A model the server cannot load, and a server without the fitter pins.
mmdhl.LoadAsset=function(asset,cb) cb(nil,'missing') end assert(says(save({version=1,boneMap={}}),'bonemap_not_on_server'))
mmdhl.LoadAsset=function(asset,cb) cb({name='Hero'}) end mmdhl.native.GetBoneMapProposal=nil
assert(says(save({version=1,boneMap={[VB..'L_Thigh']=12}}),'bonemap_update'))
-- The pins a player on a dedicated server starts from, a few answers per second.
DISK[path]=util.TableToJSON({version=3,generator=18,bodies={a=1},boneMap={[VB..'L_Calf']=7}})
REPLIES={} CLOCK=500 for k=1,10 do BM.HandleQuery(p,id) end BM.HandleQuery(p,'../x')
assert(#REPLIES==8 and REPLIES[1][1]==id and REPLIES[1][2]=='{"boneMap":{"ValveBiped.Bip01_L_Calf":7}}' and REPLIES[1][3]==true)
CLOCK=502 REPLIES={} DISK[path]=nil BM.HandleQuery(p,id) assert(REPLIES[1][2]=='{"boneMap":{}}' and REPLIES[1][3]==false)
SAY('PASS: the save handler checks permission, rate, size, keys and values, asks the server fitter, validates the window\'s map, drops stale corrections, merges the file, removes pins, and answers pin queries')
''')

# ---- server.lua: spawns read the pins; collision corrections keep them ----
SERVER_LUA = (ROOT / 'addon/lua/mmdhl/server.lua').read_text(encoding='utf-8')
lua.execute('NOTICES={} notice=function(p,text) NOTICES[#NOTICES+1]=text end')
lua.execute('local native,L=mmdhl.native,mmdhl.L ' + definition(lua, SERVER_LUA, 'if not mmdhl.LoadSavedFit then') + definition(lua, SERVER_LUA, 'function mmdhl.SavedBoneMap') + definition(lua, SERVER_LUA, 'function mmdhl.Spawn'))
lua.execute(r'''
local VB='ValveBiped.Bip01_'
local id=string.rep('b',64) local path='mmd_hotloader/fit_overrides/'..id..'.json'
mmdhl.cleanupGeneration=0
mmdhl.FeatureAvailable=function() return true end mmdhl.WithSpawnDefaults=function(p,o) return o end
mmdhl.CanUseAsset=function() return true end mmdhl.LoadAsset=function() end
local seen mmdhl.ActorOptions=function(o) seen=o return o end
local function spawn(options) seen=nil NOTICES={} mmdhl.Spawn({},id,options or {},function() end) return seen end
DISK[path]=util.TableToJSON({version=3,generator=18,bodies={x=1},scale=2,boneMap={[VB..'L_Thigh']=12},boneMapVersion=1})
local o=spawn() assert(o.boneMap[VB..'L_Thigh']==12 and o.collisionOverrides.x==1 and #NOTICES==0)
o=spawn({boneMap={[VB..'L_Thigh']=7}}) assert(o.boneMap[VB..'L_Thigh']==12,'pins copied from an older spawn never win over the saved ones')
DISK[path]=util.TableToJSON({version=3,generator=18,boneMap={[VB..'L_Calf']=3}})
o=spawn() assert(o.boneMap[VB..'L_Calf']==3 and o.collisionOverrides==nil and #NOTICES==0,'a file with only pins is valid')
DISK[path]=util.TableToJSON({version=2,generator=9,bodies={x=1}})
o=spawn({boneMap={[VB..'L_Thigh']=7}}) assert(o.boneMap==nil and o.collisionOverrides==nil and #NOTICES==1,'old corrections still give their notice; reset pins are reset')
assert(mmdhl.SavedBoneMap('../'..string.rep('b',62))==nil and mmdhl.SavedBoneMap(id)==nil)
DISK[path]=util.TableToJSON({version=3,generator=18,bodies={x=1},boneMap={}})
o=spawn() assert(o.boneMap==nil)
SAY('PASS: spawns read the saved pins next to the collision corrections')
''')
# The mmdhl_action receiver: bonemap saves go to the handler; collision corrections keep the pins.
lua.execute(r'''
RECEIVERS={} net={Receive=function(name,fn) RECEIVERS[name]=fn end}
''')
lua.execute('local native,L=mmdhl.native,mmdhl.L ' + definition(lua, SERVER_LUA, 'local function samePins') + definition(lua, SERVER_LUA, "net.Receive('mmdhl_action'"))
lua.execute(r'''
local VB='ValveBiped.Bip01_'
local id=string.rep('c',64) local path='mmd_hotloader/fit_overrides/'..id..'.json'
local queue
net.ReadString=function() return table.remove(queue,1) end net.ReadUInt=function() return 3 end
local ent={GetClass=function() return 'prop_ragdoll' end,MMDOptions={boneMap={[VB..'L_Thigh']=12}},GetPos=function() return {Unpack=function() return 1,2,3 end} end}
Entity=function() return ent end
local handled mmdhl.boneMapper.HandleSave=function(p,asset,value) handled={asset,value} end
queue={'bonemap',id,'{"version":1}'} RECEIVERS.mmdhl_action(0,{}) assert(handled[1]==id and handled[2]=='{"version":1}')
mmdhl.CanEdit=function() return true end mmdhl.IsMMD=function() return true end mmdhl.GetInstance=function() return 1 end
mmdhl.GetRig=function() return {scale=3} end mmdhl.GetAsset=function() return id end
local respawned mmdhl.Spawn=function(p,asset,options,cb) respawned=options cb({}) end
DISK[path]=util.TableToJSON({version=3,generator=18,bodies={old=1},boneMap={[VB..'L_Thigh']=12},boneMapVersion=1,boneMapSavedAt=5})
queue={'fit','',util.TableToJSON({bodies={new=1},excludedMaterials={}})} RECEIVERS.mmdhl_action(0,{})
local saved=util.JSONToTable(DISK[path])
assert(respawned.boneMap[VB..'L_Thigh']==12 and saved.bodies.new==1 and saved.bodies.old==nil and saved.scale==3 and saved.boneMap[VB..'L_Thigh']==12 and saved.boneMapSavedAt==5)
-- The bones were assigned again after this ragdoll was placed: its corrections would not match.
DISK[path]=util.TableToJSON({version=3,generator=18,boneMap={[VB..'L_Thigh']=13}}) respawned=nil NOTICES={}
queue={'fit','',util.TableToJSON({bodies={new=2},excludedMaterials={}})} RECEIVERS.mmdhl_action(0,{})
assert(respawned==nil and NOTICES[1]==mmdhl.L'server.error.fit_bones_changed' and util.JSONToTable(DISK[path]).bodies==nil)
-- Pin queries go to their handler.
local queried mmdhl.boneMapper.HandleQuery=function(p,asset) queried=asset end
queue={'bonemap_pins',id,''} RECEIVERS.mmdhl_action(0,{}) assert(queried==id)
SAY('PASS: the action receiver routes bone saves and queries; collision corrections keep the saved pins and refuse a carrier fitted with older ones')

''')

# ---- actors.lua: the first-person arms preview is fitted with the same pins ----
ACTORS_LUA = (ROOT / 'addon/lua/mmdhl/actors.lua').read_text(encoding='utf-8')
lua.execute('local native,L=mmdhl.native,mmdhl.L ' + definition(lua, ACTORS_LUA, "net.Receive('mmdhl_arms_preview'"))
lua.execute(r'''
local VB='ValveBiped.Bip01_'
local id=string.rep('d',64) DISK['mmd_hotloader/fit_overrides/'..id..'.json']=util.TableToJSON({version=3,generator=18,boneMap={[VB..'L_Calf']=3}})
local queue={1,id,'female','{}'} net.ReadUInt=function() return table.remove(queue,1) end net.ReadString=function() return table.remove(queue,1) end
local sent={} net.Start=function() end net.WriteUInt=function() end net.WriteString=function(v) sent[#sent+1]=v end net.Send=function() end
mmdhl.CanUseAsset=function() return true end RealTime=function() return 10 end mmdhl.CleanArmsParts=function(t) return t end
mmdhl.LoadAsset=function(asset,cb) cb({name='Hero'}) end mmdhl.ActorOptions=function(o) return o end mmdhl.PublishRig=function() end
local prepared mmdhl.native.PrepareCarrier=function(asset,json) prepared=util.JSONToTable(json) return util.TableToJSON({key='rig'}) end
RECEIVERS.mmdhl_arms_preview(0,{})
assert(prepared.role=='arms' and prepared.boneMap[VB..'L_Calf']==3 and sent[1]=='rig','the arms preview is fitted with the saved pins')
SAY('PASS: the first-person arms preview takes the saved pins')
''')

# ---- the window itself: every panel builds and paints (Derma replaced by recording stubs) ----
UI_HELPERS = (ROOT / 'addon/lua/mmdhl/ui.lua').read_text(encoding='utf-8')
import re
# Derma methods the window calls; other names on a panel are its own fields (nil until set).
lua.globals().DERMA_METHODS = lua.table_from({name: True for name in set(re.findall(r':([A-Z]\w*)\(', UI + UI_HELPERS)) | {'Think', 'PerformLayout', 'SetExpanded'}})
lua.execute(r'''
CLIENT=true SERVER=false
ALL={} OPTIONS={}
local PanelMT={}
local special={btnMinim=true,btnMaxim=true,btnClose=true,Label=true,Header=true,m_Image=true}
function NEW_PANEL(class) local p=setmetatable({_class=class,_children={},_text=''},PanelMT) ALL[#ALL+1]=p return p end
local function child(self,class) local c=NEW_PANEL(class) table.insert(self._children,c) c._parent=self return c end
local methods={
 GetSize=function() return 400,300 end, GetWide=function() return 400 end, GetTall=function() return 300 end,
 CursorPos=function() return 200,150 end, LocalToScreen=function(_,x,y) return x or 0,y or 0 end, ScreenToLocal=function(_,x,y) return x or 0,y or 0 end,
 GetText=function(self) return self._text end, SetText=function(self,v) self._text=tostring(v) end, GetValue=function(self) return self._value end, SetValue=function(self,v) self._value=v end,
 GetChecked=function(self) return self._checked==true end, SetChecked=function(self,v) self._checked=v end,
 IsHovered=function() return false end, IsChildHovered=function() return false end, HasFocus=function() return false end,
 IsEnabled=function(self) return self._enabled~=false end, SetEnabled=function(self,v) self._enabled=v end,
 IsVisible=function(self) return self._visible~=false end, SetVisible=function(self,v) self._visible=v end,
 GetSelected=function() return 'Hips','ValveBiped.Bip01_Pelvis' end, GetClassName=function() return 'Panel' end,
 GetChildren=function(self) return self._children end, GetParent=function(self) return self._parent end, HasParent=function() return false end,
 GetScroll=function() return 0 end, GetFont=function(self) return self._font end, SetFont=function(self,v) self._font=v end,
 Remove=function(self) self.removed=true if rawget(self,'OnRemove') then self:OnRemove() end end, Clear=function(self) for _,c in ipairs(self._children) do c.removed=true end self._children={} end,
 GetDockMargin=function() return 0,0,0,0 end,
}
for _,name in ipairs({'Add','AddNode','AddColumn','AddLine','AddSubMenu','GetCanvas','GetVBar'}) do methods[name]=function(self,...) local c=child(self,name) c._args={...} return c end end
methods.AddOption=function(self,text,fn) local c=child(self,'option') OPTIONS[#OPTIONS+1]={text=text,fn=fn} return c end
methods.AddSubMenu=function(self) local c=child(self,'submenu') return c,c end
methods.AddChoice=function(self,text,data) self._choices=self._choices or {} self._choices[#self._choices+1]={text,data} end
PanelMT.__index=function(t,k)
 if special[k] then local c=child(t,k) rawset(t,k,c) return c end
 if k=='Columns' then return t._children end
 if methods[k] then return methods[k] end
 if DERMA_METHODS[k] then return function() end end
end
vgui={Create=function(class,parent) local p=NEW_PANEL(class) if parent then table.insert(parent._children,p) p._parent=parent end return p end,
 GetKeyboardFocus=function() return nil end, GetHoveredPanel=function() return nil end}
DermaMenu=function() return NEW_PANEL('menu') end
QUERIES={} Derma_Query=function(text,title,a,fa,b,fb) QUERIES[#QUERIES+1]={text=text,a=fa,b=fb} end
Derma_Message=function() end
draw={RoundedBox=function() end,RoundedBoxEx=function() end,SimpleText=function(text) assert(type(text)=='string','draw.SimpleText needs text') end,NoTexture=function() end}
surface={SetDrawColor=function() end,DrawRect=function() end,DrawLine=function() end,DrawCircle=function() end,DrawPoly=function(p) assert(#p>=3) end,
 SetFont=function() end,GetTextSize=function(text) assert(type(text)=='string') return #text*7,14 end,CreateFont=function() end}
input={IsKeyDown=function() return false end,IsMouseDown=function() return false end}
gui={IsGameUIVisible=function() return false end}
ScrW=function() return 1920 end ScrH=function() return 1080 end
color_white=Color(255,255,255) ColorAlpha=function(c,a) return Color(c.r,c.g,c.b,a) end
string.Comma=function(n) return tostring(n) end string.Trim=function(s) return (s:gsub('^%s+',''):gsub('%s+$','')) end
math.Round=function(v) return math.floor(v+.5) end
for i,k in ipairs({'KEY_Z','KEY_Y','KEY_TAB','KEY_ENTER','KEY_DELETE','KEY_BACKSPACE','KEY_F','KEY_1','KEY_2','KEY_LCONTROL','KEY_RCONTROL','KEY_LSHIFT','KEY_RSHIFT'}) do _G[k]=i end
MOUSE_LEFT=107 MOUSE_RIGHT=108 MOUSE_MIDDLE=109 TEXT_ALIGN_LEFT=0 TEXT_ALIGN_CENTER=1 TEXT_ALIGN_RIGHT=2 TEXT_ALIGN_BOTTOM=4 NOTIFY_ERROR=1 NOTIFY_GENERIC=0 NOTIFY_HINT=3
GetConVar=function() return nil end
LocalPlayer=function() return {IsListenServerHost=function() return false end,IsAdmin=function() return false end} end
IsValid=function(v) return v~=nil and v~=false and not (type(v)=='table' and v.removed) end
TIMERS={} timer={Simple=function(_,fn) TIMERS[#TIMERS+1]=fn end,Create=function(name,_,_,fn) TIMERS[#TIMERS+1]=fn end,Remove=function() end}
ACTIONS={} mmdhl.Action=function(action,id,ent,value) ACTIONS[#ACTIONS+1]={action,id,value} end
''')
lua.execute(UI_HELPERS[:UI_HELPERS.index('\nlocal PANEL={}')])
lua.execute(r'''
-- Calls every painter, layout and cursor handler the window installed.
function PAINT_ALL()
 local n=0
 for _,p in ipairs(ALL) do if not p.removed then
  for _,name in ipairs({'PerformLayout','Paint','PaintOver'}) do local fn=rawget(p,name) if type(fn)=='function' then fn(p,400,300) n=n+1 end end
  local moved=rawget(p,'OnCursorMoved') if type(moved)=='function' then moved(p,200,150) moved(p,5,5) end
 end end
 return n
end
function CLICK_ALL(code)
 for _,p in ipairs(ALL) do if not p.removed then local fn=rawget(p,'OnMousePressed') if type(fn)=='function' then fn(p,code) end end end
end
''')
lua.execute(UI)
lua.execute(r'''
local BM,VB,library=mmdhl.boneMapper,'ValveBiped.Bip01_',mmdhl.library
library.job=nil DISK={}
CALLS={} mmdhl.native.BeginImport=function(source,json) CALLS[#CALLS+1]={source=source,options=util.JSONToTable(json)} return 77 end
-- Convert mode: picture view, summary, slot card, bone card, list view, picker, jiggle tab.
local state=BM.NewState('convert',{probe=PROBE(false),source='C:/m/smoke.fbx',filename='smoke.fbx'})
BM.RefreshChains(state)
local win=BM.ShowWindow(state,{})
assert(win and BM.frame and IsValid(BM.frame) and win.summary.headline=='check')
assert(PAINT_ALL()>20)
win:Arm(VB..'L_Hand') assert(win.armed==VB..'L_Hand') PAINT_ALL() CLICK_ALL(MOUSE_LEFT)
win:Arm(VB..'L_Finger1') assert(win.figure=='hands') PAINT_ALL()
win:Arm(nil) win.selected=state.byName['Hair_01'] win:Refresh() PAINT_ALL()
win.selected=nil
win:Key('next_slot') assert(win.armed~=nil) win:Key('enter') win:Key('undo') win:Key('redo') win:Key('view_side') PAINT_ALL() win:Key('fit')
state.view='list' win:BuildContent() win:Changed() PAINT_ALL()
BM.OpenPicker(win,VB..'Spine2',NEW_PANEL('anchor')) PAINT_ALL()
win:SetTab('jiggle') win.selectedGroup='hair' win:Refresh() PAINT_ALL() CLICK_ALL(MOUSE_RIGHT)
win.addMode=true PAINT_ALL()
win:SetTab('body') state.view='picture' win:BuildContent() win:Changed()
-- The More menu: every option runs.
OPTIONS={} win:MoreMenu() assert(#OPTIONS==7)
for _,o in ipairs(OPTIONS) do if o.fn then o.fn() end end
for _,q in ipairs(QUERIES) do if q.a then q.a() end end QUERIES={}
PAINT_ALL()
-- Notices, problems and the swap fix.
BM.SwapSides(state,false) win:Changed() PAINT_ALL()
-- Import: the window closes and the conversion starts with the request.
BM.Undo(state) while BM.Undo(state) do end BM.AcceptGuess(state,VB..'L_Hand') BM.AcceptGuess(state,VB..'R_Hand') win:Changed()
assert(win.summary.headline=='ok',win.summary.headline)
win:Primary() assert(BM.frame==nil and CALLS[#CALLS].options.kind=='character' and BM.sessions['C:/m/smoke.fbx']==state)
-- Closing with changes asks first.
local again=BM.ShowWindow(state,{}) BM.Assign(state,VB..'Spine2',state.byName['Chest'],'user')
BM.frame:Close() assert(#QUERIES==1 and QUERIES[1].text==mmdhl.L'bonemap.discard_text_convert' and BM.frame~=nil) QUERIES[1].a() assert(BM.frame==nil and library.status==mmdhl.L'library.import.cancelled')
SAY('PASS: convert window builds and paints every view, runs the More menu, imports and asks before discarding')
''')
lua.execute(r'''
local BM,VB=mmdhl.boneMapper,'ValveBiped.Bip01_'
-- Fit mode: the window loads the cached model, then saves pins through the bonemap action.
local id=string.rep('7',64)
local bones=BONES()
local proposal={bones={},missing={VB..'L_Thigh'},issues={{code='band',severity='warning',slot=VB..'Spine4',text='aliased'}},torso={repairs={'Spine bones were reordered'}}}
for _,s in ipairs(BM.Slots) do if not s.convertOnly then proposal.bones[#proposal.bones+1]={name=s.key,mmd=-1,provenance='synthesized',aliases={}} end end
local idx={} for i,b in ipairs(bones) do idx[b.name]=i-1 end
for _,b in ipairs(proposal.bones) do local short=b.name:sub(#VB+1) local map={Pelvis='Hips',Spine1='Spine',Spine4='Chest',Neck1='Neck',Head1='Head',L_UpperArm='LeftArm',L_Forearm='LeftForeArm',L_Hand='LeftHand',
 R_UpperArm='RightArm',R_Forearm='RightForeArm',R_Hand='RightHand',R_Thigh='RightUpLeg',R_Calf='RightLeg',R_Foot='RightFoot',L_Calf='LeftLeg',L_Foot='LeftFoot'}
 if map[short] then b.mmd=idx[map[short]] b.provenance='name' end end
mmdhl.native.RequestAsset=function() return true end
mmdhl.native.AssetInfo=function() return util.TableToJSON({name='Hero'}) end
mmdhl.native.InspectBoneMap=function() return util.TableToJSON({skeleton={bones=bones,points={0,0,0,0},signature=string.rep('b',64),height=1.7},auto={slots={[VB..'L_Thigh']={bone=idx['LeftUpLeg'],confidence=.9}}}}) end
mmdhl.native.GetBoneMapProposal=function(asset,json) return util.TableToJSON(proposal) end
game={SinglePlayer=function() return true end}
assert(BM.Available('fit'))
mmdhl.library.entries[id]={id=id,name='Hero',source='C:/m/hero.pmx',settings={}}
TIMERS={} assert(BM.OpenFit(id,'rescue','Hero'))
assert(BM.frame and BM.state.loading) PAINT_ALL()
for _,fn in ipairs(TIMERS) do fn() end TIMERS={}
local s=BM.state assert(s.mode=='fit' and not s.loading and s.slots[VB..'L_Thigh'].origin=='guess' and s.format=='pmx')
PAINT_ALL()
local win=BM.frame.Window win:RefreshProposal() PAINT_ALL()
BM.AcceptGuess(s,VB..'L_Thigh') win:Changed() for _,fn in ipairs(TIMERS) do fn() end TIMERS={}
ACTIONS={} win:Primary()
assert(ACTIONS[1][1]=='bonemap' and ACTIONS[1][2]==id and ACTIONS[1][3].version==1 and ACTIONS[1][3].boneMap[VB..'L_Thigh']==idx['LeftUpLeg'] and win.saving)
-- The server's answer closes the window and clears the badge.
local FIT={} mmdhl.library.SetFitStatus=function(asset,fit) FIT[#FIT+1]=fit end
local reads={id,true,'saved'} net.ReadString=function() return table.remove(reads,1) end net.ReadBool=function() return table.remove(reads,1) end
NOTES={} NET['mmdhl_bonemap_result']()
assert(BM.frame==nil and FIT[1].ok==true and NOTES[1]==mmdhl.L('bonemap.rescued',{name='Hero'}))
-- A load failure shows Try again.
mmdhl.native.AssetInfo=function() return nil,'Model was deleted; import it again' end
TIMERS={} BM.OpenFit(id,'edit','Hero') for _,fn in ipairs(TIMERS) do fn() end TIMERS={} PAINT_ALL()
BM.frame.Window.finish('cancelled')
-- An admin on a dedicated server starts from the server's pins, not from his own DATA folder.
mmdhl.native.AssetInfo=function() return util.TableToJSON({name='Hero'}) end
game={SinglePlayer=function() return false end} DISK['mmd_hotloader/fit_overrides/'..id..'.json']=util.TableToJSON({version=3,boneMap={[VB..'Spine2']=1}})
ACTIONS={} TIMERS={} BM.OpenFit(id,'edit','Hero') local list=TIMERS TIMERS={} for _,fn in ipairs(list) do fn() end
assert(ACTIONS[1][1]=='bonemap_pins' and BM.state.loading)
local answer={id,'{"boneMap":{"ValveBiped.Bip01_L_Thigh":'..idx['LeftUpLeg']..'}}',true} net.ReadString=function() return table.remove(answer,1) end net.ReadBool=function() return table.remove(answer,1) end
NET['mmdhl_bonemap_pins']()
s=BM.state assert(not s.loading and s.savedPins[VB..'L_Thigh']==idx['LeftUpLeg'] and s.savedPins[VB..'Spine2']==nil and s.hasCollision==true and s.notice.key=='saved_fit')
PAINT_ALL() BM.frame.Window.finish('cancelled')
game={SinglePlayer=function() return true end}
-- The rescue prompt.
local prompt=BM.ShowRescuePrompt(id,'Hero',{VB..'L_Thigh',VB..'L_Calf'}) PAINT_ALL()
SAY('PASS: fit window loads the cached model, shows guesses and fitter notes, saves pins and closes on the answer; rescue prompt paints')
''')
