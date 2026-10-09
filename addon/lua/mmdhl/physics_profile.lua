-- Ragdoll physics profiles: the editor's model of a carrier's .phy, shared by
-- both realms and free of vgui and net (tests run it outside the game).
-- Body i (0..17) is solid i and PhysObj i; joint i (1..17) is the constraint
-- whose child is body i. physicsOverrides is a flat diff of effective values
-- against the defaults; native canonicalises it again before building
-- (native/physics_profile.cpp) and P.Canonical mirrors those rules.
mmdhl=mmdhl or {}
local P={} mmdhl.physics=P
P.SCHEMA=1
P.IDS={'pelvis','spine1','spine4','head1','l_clavicle','l_upperarm','l_forearm','l_hand','r_clavicle','r_upperarm','r_forearm','r_hand','l_thigh','l_calf','l_foot','r_thigh','r_calf','r_foot'}
P.BODIES={} for i,short in ipairs({'Pelvis','Spine1','Spine4','Head1','L_Clavicle','L_UpperArm','L_Forearm','L_Hand','R_Clavicle','R_UpperArm','R_Forearm','R_Hand','L_Thigh','L_Calf','L_Foot','R_Thigh','R_Calf','R_Foot'}) do P.BODIES[i]='ValveBiped.Bip01_'..short end
P.PARENT={[0]=-1,0,1,2,2,4,5,6,2,8,9,10,0,12,13,0,15,16}
P.INDEX={} for i,name in ipairs(P.BODIES) do P.INDEX[name]=i-1 end
P.GROUPS={torso={0,1,2,3},left_arm={4,5,6,7},right_arm={8,9,10,11},left_leg={12,13,14},right_leg={15,16,17}}
P.MIRROR={[4]=8,[5]=9,[6]=10,[7]=11,[12]=15,[13]=16,[14]=17,[8]=4,[9]=5,[10]=6,[11]=7,[15]=12,[16]=13,[17]=14}
P.MIRROR_NEGATE={[12]={y=true},[15]={y=true}}
P.HINGES={[6]=true,[10]=true,[13]=true,[16]=true}
-- Axes whose sign was measured in game (elbow and knee bend). Promote others only after test G4.
P.VERIFIED={[6]={z=true},[10]={z=true},[13]={z=true},[16]={z=true}}
P.AXES={'x','y','z'}
-- SCMI's defaults (native/scmi_data.hpp): what an unedited carrier writes.
local function lim(x0,x1,y0,y1,z0,z1) return {x={x0,x1},y={y0,y1},z={z0,z1}} end
P.DEFAULTS={[0]={massBias=1,rotdamping=3},
 {massBias=8,rotdamping=5,limits=lim(-10,10,-16,16,-19,19)},{massBias=9,rotdamping=5,limits=lim(-10,10,-10,10,-20,20)},{massBias=4,rotdamping=3,limits=lim(-50,50,-20,20,-26,30)},
 {massBias=4,rotdamping=6,limits=lim(-10,10,-5,5,0,15)},{massBias=5,rotdamping=3,limits=lim(-15,20,-40,32,-80,25)},{massBias=4,rotdamping=4,limits=lim(-40,15,0,0,-120,10)},{massBias=1,rotdamping=1,limits=lim(-25,25,-35,35,-50,50)},
 {massBias=4,rotdamping=6,limits=lim(-10,10,-5,5,0,15)},{massBias=5,rotdamping=3,limits=lim(-15,20,-40,32,-80,25)},{massBias=4,rotdamping=4,limits=lim(-40,15,0,0,-120,10)},{massBias=1,rotdamping=1,limits=lim(-25,25,-35,35,-50,50)},
 {massBias=7,rotdamping=7,limits=lim(-30,30,-30,60,-100,30)},{massBias=4,rotdamping=5,limits=lim(-15,15,-5,5,-10,125)},{massBias=1,rotdamping=9,limits=lim(-15,15,-15,15,-18,25)},
 {massBias=7,rotdamping=7,limits=lim(-30,30,-60,30,-100,30)},{massBias=4,rotdamping=5,limits=lim(-15,15,-5,5,-10,125)},{massBias=1,rotdamping=9,limits=lim(-15,15,-15,15,-18,25)}}
P.DAMPING,P.INERTIA,P.MASS,P.BIAS_TOTAL=0.8,12,70,74
-- Anatomical caps: stop-derived ranges never grow past them (and explicit ones warn).
local function cap(x,y,z) return {x=x,y=y,z=z} end
local S45,S30,S80={-45,45},{-30,30},{-80,80}
P.CAPS={cap(S45,S45,S45),cap(S45,S45,S45),cap({-80,80},S45,{-60,60}),cap(S30,S30,S30),cap({-90,90},{-90,90},{-135,90}),cap({-90,45},{-10,10},{-150,10}),cap(S80,S80,S80),
 cap(S30,S30,S30),cap({-90,90},{-90,90},{-135,90}),cap({-90,45},{-10,10},{-150,10}),cap(S80,S80,S80),cap({-60,60},{-90,90},{-130,60}),cap(S30,{-15,15},{-10,160}),cap(S45,S45,S45),
 cap({-60,60},{-90,90},{-130,60}),cap(S30,{-15,15},{-10,160}),cap(S45,S45,S45)}
-- Simple-mode stops (§8). Friction is in .phy units (QC friction / 5).
P.STIFF={[-2]={rd=.35,f=0},[-1]={rd=.6,f=0},[0]={rd=1,f=0},[1]={rd=2,f=.6},[2]={rd=4,f=2}}
P.STIFF_IDS={[-2]='floppy',[-1]='loose',[0]='normal',[1]='firm',[2]='stiff'}
P.RANGE={[-3]=0,[-2]=.5,[-1]=.75,[0]=1,[1]=1.25,[2]=1.5}
P.RANGE_IDS={[-3]='locked',[-2]='very_tight',[-1]='tight',[0]='normal',[1]='loose',[2]='very_loose'}
P.FLOAT={[1]=2,[2]=5}
P.FLOAT_IDS={[0]='normal',[1]='slow_fall',[2]='floaty'}
P.PRESETS={default={0,0,0},less_floppy={1,-1,0},posing_doll={2,0,0},relaxed={-1,1,0},extra_floppy={-2,2,0},statue={0,-3,0}}
P.PRESET_ORDER={'default','less_floppy','posing_doll','relaxed','extra_floppy','statue'}
P.PRESET_IDS={default=true,less_floppy=true,posing_doll=true,relaxed=true,extra_floppy=true,statue=true,template=true,custom=true}
P.SURFACES={'flesh','bloodyflesh','zombieflesh','alienflesh','armorflesh','rubber','plastic','wood','metal','solidmetal','carpet','ice','default'}
P.STYLES={fitted=true,box=true,capsule=true}
-- Hard ranges and quanta (decimals) of physics schema 1.
P.LIMITS={angle={-180,180,1},friction={0,100,3},massBias={.01,100,3},damping={0,10,3},rotdamping={0,100,3},inertia={.1,100,3},drag={0,100,3},density={10,20000,1},animfriction={0,1000,0},animtime={0,10,2},mass={1,500,1},shape=4}
P.ANIMATED_DEFAULT={min=80,max=600,timeIn=.15,timeHold=1.75,timeOut=.25}
local DISTAL_CHILD={[4]=5,[5]=6,[6]=7,[8]=9,[9]=10,[10]=11,[12]=13,[13]=14,[15]=16,[16]=17}
local SCMI_SCALE=3.23656

local function copy(t) if type(t)~='table' then return t end local c={} for k,v in pairs(t) do c[k]=copy(v) end return c end
P.Copy=copy
local function clamp(v,lo,hi) if v<lo then return lo end if v>hi then return hi end return v end
local function finite(v) return type(v)=='number' and v==v and v~=math.huge and v~=-math.huge end
local function isint(v) return finite(v) and math.floor(v)==v end
-- Half away from zero, as native's std::round; -0 becomes 0.
function P.Quantize(v,decimals)
 local p=10^(decimals or 0) local n=v*p
 n=n>=0 and math.floor(n+.5) or -math.floor(-n+.5)
 local r=n/p if r==0 then return 0 end return r
end
local q=P.Quantize
local function same(a,b,decimals) return q(a,decimals)==q(b,decimals) end
local function emptyTable(t) return type(t)~='table' or next(t)==nil end
local function count(t) local n=0 for _ in pairs(t or {}) do n=n+1 end return n end
function P.Adjacent(a,b) return P.PARENT[a]==b or P.PARENT[b]==a end
-- Every non-adjacent pair (the default rule set: 136 pairs), sorted.
function P.AllPairs()
 local out={} for a=0,17 do for b=a+1,17 do if not P.Adjacent(a,b) then out[#out+1]={a,b} end end end return out
end
local pairKey=function(a,b) if a>b then a,b=b,a end return a*18+b end
local function sortPairs(list) table.sort(list,function(x,y) if x[1]~=y[1] then return x[1]<y[1] end return x[2]<y[2] end) return list end
function P.NormalizePairs(list)
 local seen,out={},{}
 for _,p in ipairs(list or {}) do local a,b=tonumber(p[1]),tonumber(p[2])
  if isint(a) and isint(b) and a>=0 and b<=17 and b>=0 and a<=17 and a~=b and not P.Adjacent(a,b) then
   if a>b then a,b=b,a end local k=pairKey(a,b) if not seen[k] then seen[k]=true out[#out+1]={a,b} end
  end
 end
 return sortPairs(out)
end
-- The non-adjacent pairs touching no body in the set: "these parts pass through the body".
function P.PassThroughPairs(set)
 local out={} for _,p in ipairs(P.AllPairs()) do if not (set[p[1]] or set[p[2]]) then out[#out+1]=p end end return out
end

-- Shape unit and scale multiplier of a rig.json (§3.2).
local function bonePosition(rig,name)
 for _,b in ipairs(rig and rig.bones or {}) do if b.name==name and type(b.position)=='table' then return b.position end end
end
function P.Unit(rig)
 local m=(tonumber(rig and rig.scale) or SCMI_SCALE)/SCMI_SCALE
 local h,l,r=bonePosition(rig,P.BODIES[4]),bonePosition(rig,P.BODIES[15]),bonePosition(rig,P.BODIES[18])
 if not (h and l and r) then return m*72/60,m end
 local d=0 for k=1,3 do local x=h[k]-(l[k]+r[k])/2 d=d+x*x end
 if d<1e-6 then return m*72/60,m end
 return math.sqrt(d)/60,m
end

function P.NewDraft()
 local d={feel={preset='default',stiffness=0,range=0,floatiness=0},joints={},explicit={},
  model={surfaceprop='flesh',massMode='bias',automass=false,density=1000,collisions={mode='all',pairs={}}},mass=P.MASS,shapes={},excludedMaterials={}}
 return d
end
local function joint(d,i) return d.joints[i] or {} end
-- Effective per-body values of a draft (§5.10): stops, then per-joint stops, then explicit numbers.
function P.Effective(d)
 local feel=d.feel or {} local model=d.model or {} local out={}
 local fs=clamp(tonumber(feel.stiffness) or 0,-2,2) local fl=clamp(tonumber(feel.floatiness) or 0,0,2)
 for i=0,17 do
  local def=P.DEFAULTS[i] local j=joint(d,i)
  local s=i==0 and fs or (j.stiffness or fs) local stiff=P.STIFF[s] or P.STIFF[0]
  local b={massBias=def.massBias,rotdamping=q(def.rotdamping*stiff.rd,3),damping=fl>0 and math.max(P.DAMPING,P.FLOAT[fl]) or P.DAMPING,inertia=P.INERTIA,surfaceprop=model.surfaceprop or 'flesh'}
  if i>0 then
   local r=j.locked and -3 or (j.range or tonumber(feel.range) or 0) local k=P.RANGE[r] or 1 b.limits={}
   for _,axis in ipairs(P.AXES) do
    local L,H=def.limits[axis][1],def.limits[axis][2] local lo,hi=L*k,H*k
    if k>1 then local c=P.CAPS[i][axis] lo=math.max(lo,math.min(L,c[1])) hi=math.min(hi,math.max(H,c[2])) end
    lo,hi=q(lo,1),q(hi,1)
    if j.hinge and P.HINGES[i] and axis~='z' then lo,hi=0,0 end
    b.limits[axis]={lo,hi,(lo==0 and hi==0) and 0 or stiff.f}
   end
  end
  local e=d.explicit and d.explicit[i]
  if e then
   if e.limits and i>0 then for axis,t in pairs(e.limits) do if b.limits[axis] then b.limits[axis]={t[1],t[2],t[3]} end end end
   for _,f in ipairs({'massBias','rotdamping','damping','inertia','drag','surfaceprop'}) do if e[f]~=nil then b[f]=e[f] end end
  end
  out[i]=b
 end
 return out,model
end
-- The SCMI values as effective bodies (the canonical diff's base).
function P.DefaultBodies(surface)
 local out={} for i=0,17 do local def=P.DEFAULTS[i] local b={massBias=def.massBias,rotdamping=def.rotdamping,damping=P.DAMPING,inertia=P.INERTIA,surfaceprop=surface or 'flesh'}
  if i>0 then b.limits={} for _,axis in ipairs(P.AXES) do b.limits[axis]={def.limits[axis][1],def.limits[axis][2],0} end end out[i]=b end
 return out
end
-- Classifies one limit triple like native: free (-360..360), fixed (0..0, no friction), or a limit.
local function canonicalAxis(t)
 local lo,hi,f=q(tonumber(t[1]) or 0,1),q(tonumber(t[2]) or 0,1),q(tonumber(t[3]) or 0,3)
 if not (lo==-360 and hi==360) and lo==0 and hi==0 then f=0 end
 return {lo,hi,f}
end
-- physicsOverrides for effective bodies and model fields, or {} when everything is default (§5.4).
function P.Canonical(bodies,model)
 model=model or {} local out={schema=P.SCHEMA}
 local surface=model.surfaceprop or 'flesh'
 if surface~='flesh' then out.surfaceprop=surface end
 if model.massMode=='volume' then out.massMode='volume' end
 if model.automass then out.automass={density=q(tonumber(model.density) or 1000,1)} end
 local c=model.collisions or {}
 if c.mode=='none' then out.collisions={mode='none'}
 elseif c.mode=='custom' then local list=P.NormalizePairs(c.pairs)
  if #list==0 then out.collisions={mode='none'} elseif #list<136 then out.collisions={mode='custom',pairs=list} end
 end
 local a=model.animatedFriction
 if a then out.animatedFriction={min=math.floor(tonumber(a.min) or 0),max=math.floor(tonumber(a.max) or 0),timeIn=q(tonumber(a.timeIn) or 0,2),timeOut=q(tonumber(a.timeOut) or 0,2),timeHold=q(tonumber(a.timeHold) or 0,2)} end
 local list={}
 for i=0,17 do
  local b,def=bodies[i],P.DEFAULTS[i] local o={}
  if b then
   if i>0 and b.limits then local limits={}
    for _,axis in ipairs(P.AXES) do local t=b.limits[axis] if t then t=canonicalAxis(t)
     if not (t[1]==def.limits[axis][1] and t[2]==def.limits[axis][2] and t[3]==0) then limits[axis]=t end end end
    if next(limits) then o.limits=limits end
   end
   if b.massBias and not same(b.massBias,def.massBias,3) then o.massBias=q(b.massBias,3) end
   if b.rotdamping and not same(b.rotdamping,def.rotdamping,3) then o.rotdamping=q(b.rotdamping,3) end
   if b.damping and not same(b.damping,P.DAMPING,3) then o.damping=q(b.damping,3) end
   if b.inertia and not same(b.inertia,P.INERTIA,3) then o.inertia=q(b.inertia,3) end
   if b.drag then o.drag=q(b.drag,3) end
   if b.surfaceprop and b.surfaceprop~=surface then o.surfaceprop=b.surfaceprop end
  end
  if next(o) then list[P.BODIES[i+1]]=o end
 end
 if next(list) then out.bodies=list end
 if count(out)==1 then return {} end
 return out
end
-- Effective bodies and model fields of a canonical physicsOverrides (defaults ⊕ diff).
function P.Expand(canonical)
 canonical=type(canonical)=='table' and canonical or {}
 local c=canonical.collisions or {}
 local model={surfaceprop=canonical.surfaceprop or 'flesh',massMode=canonical.massMode or 'bias',automass=canonical.automass~=nil,density=canonical.automass and canonical.automass.density or 1000,
  collisions={mode=c.mode or 'all',pairs=copy(c.pairs or {})},animatedFriction=copy(canonical.animatedFriction)}
 local bodies=P.DefaultBodies(model.surfaceprop)
 for name,o in pairs(canonical.bodies or {}) do local i=P.INDEX[name]
  if i and type(o)=='table' then local b=bodies[i]
   if i>0 and type(o.limits)=='table' then for axis,t in pairs(o.limits) do if b.limits[axis] and type(t)=='table' then b.limits[axis]={t[1],t[2],t[3]} end end end
   for _,f in ipairs({'massBias','rotdamping','damping','inertia','drag','surfaceprop'}) do if o[f]~=nil then b[f]=o[f] end end
  end
 end
 return bodies,model
end

-- Draft <-> request. Explicit numbers hold only what the stops do not already give.
local function sameTriple(a,b) return a and b and q(a[1],1)==q(b[1],1) and q(a[2],1)==q(b[2],1) and q(a[3],3)==q(b[3],3) end
local function sameField(f,a,b)
 if f=='surfaceprop' then return a==b end
 if a==nil or b==nil then return a==b end
 return same(a,b,3)
end
-- Makes the draft produce these effective bodies and model by writing explicit
-- values wherever its stops differ (QC, templates and reopened drafts share it).
function P.AdoptEffective(d,bodies,model)
 if model then d.model=copy(model) end
 local stops=P.Effective({feel=d.feel,joints=d.joints,explicit={},model=d.model})
 d.explicit={}
 for i=0,17 do local cur,stop=bodies[i],stops[i] local e={}
  if cur then
   if i>0 and cur.limits then for _,axis in ipairs(P.AXES) do local t=cur.limits[axis] if t and not sameTriple(canonicalAxis(t),stop.limits[axis]) then e.limits=e.limits or {} e.limits[axis]=canonicalAxis(t) end end end
   for _,f in ipairs({'massBias','rotdamping','damping','inertia','drag','surfaceprop'}) do
    if cur[f]~=nil and not sameField(f,cur[f],stop[f]) then if f=='surfaceprop' then e[f]=cur[f] else e[f]=q(cur[f],3) end end
   end
  end
  if next(e) then d.explicit[i]=e end
 end
 return d
end
local function physicsEditorOf(d)
 local feel=d.feel or {} local e={schema=1,feel={preset=feel.preset or 'custom',stiffness=feel.stiffness or 0,range=feel.range or 0,floatiness=feel.floatiness or 0}}
 local joints={}
 for i=1,17 do local j=d.joints and d.joints[i]
  if j and (j.stiffness~=nil or j.range~=nil or j.hinge or j.locked) then joints[P.BODIES[i+1]]={stiffness=j.stiffness,range=j.range,hinge=j.hinge==true,locked=j.locked==true} end
 end
 if next(joints) then e.joints=joints end
 return e
end
function P.Resolve(d,rig)
 local overrides={}
 for i=0,17 do local s=d.shapes and d.shapes[i]
  if s then local o={center={q(s.center[1],4),q(s.center[2],4),q(s.center[3],4)},extent={q(s.extent[1],4),q(s.extent[2],4),q(s.extent[3],4)}}
   if s.style and s.style~='fitted' then o.style=s.style end overrides[P.BODIES[i+1]]=o end
 end
 local excluded={} for _,v in ipairs(d.excludedMaterials or {}) do excluded[#excluded+1]=v end table.sort(excluded)
 local bodies,model=P.Effective(d)
 return {collisionOverrides=overrides,collisionOverrideScale=tonumber(rig and rig.scale) or nil,excludedMaterials=excluded,mass=q(tonumber(d.mass) or P.MASS,1),
  physicsOverrides=P.Canonical(bodies,model),physicsEditor=physicsEditorOf(d)}
end
-- Only well-formed UI state survives; it is never hashed (§5.9).
function P.SanitizeEditor(e)
 if type(e)~='table' or e.schema~=1 or type(e.feel)~='table' then return nil end
 local f=e.feel local function stop(v,lo,hi) return isint(v) and v>=lo and v<=hi end
 if not (P.PRESET_IDS[f.preset] and stop(f.stiffness,-2,2) and stop(f.range,-3,2) and stop(f.floatiness,0,2)) then return nil end
 local out={schema=1,feel={preset=f.preset,stiffness=f.stiffness,range=f.range,floatiness=f.floatiness}}
 if type(e.joints)=='table' then local joints={}
  for name,j in pairs(e.joints) do local i=P.INDEX[name]
   if i and i>0 and type(j)=='table' and (j.stiffness==nil or stop(j.stiffness,-2,2)) and (j.range==nil or stop(j.range,-2,2)) then
    joints[name]={stiffness=j.stiffness,range=j.range,hinge=j.hinge==true and P.HINGES[i]==true or nil,locked=j.locked==true or nil}
   end
  end
  if next(joints) then out.joints=joints end
 end
 return out
end
local function vec3(v) return type(v)=='table' and tonumber(v[1]) and tonumber(v[2]) and tonumber(v[3]) and {tonumber(v[1]),tonumber(v[2]),tonumber(v[3])} or nil end
-- Rebuilds a draft from the applied options (§5.10): Resolve(DraftFromState(x)) is x.
function P.DraftFromState(state,rig)
 local applied=state and state.applied or {} local d=P.NewDraft()
 local editor=P.SanitizeEditor(applied.physicsEditor)
 if editor then
  d.feel=copy(editor.feel)
  for name,j in pairs(editor.joints or {}) do d.joints[P.INDEX[name]]={stiffness=j.stiffness,range=j.range,hinge=j.hinge==true,locked=j.locked==true} end
 end
 d.mass=q(tonumber(applied.mass) or P.MASS,1)
 for _,v in ipairs(applied.excludedMaterials or {}) do if isint(v) then d.excludedMaterials[#d.excludedMaterials+1]=v end end
 local ratio=1 local scale=tonumber(rig and rig.scale) local from=tonumber(applied.collisionOverrideScale)
 if scale and from and from>0 then ratio=scale/from end
 for name,o in pairs(type(applied.collisionOverrides)=='table' and applied.collisionOverrides or {}) do local i=P.INDEX[name]
  if i and type(o)=='table' then
   local base=rig and rig.bodies and rig.bodies[i+1] local c,e=vec3(o.center) or vec3(base and base.center),vec3(o.extent) or vec3(base and base.extent)
   if c and e then for k=1,3 do c[k]=q(c[k]*ratio,4) e[k]=q(e[k]*ratio,4) end d.shapes[i]={center=c,extent=e,style=P.STYLES[o.style] and o.style or 'fitted'} end
  end
 end
 local bodies,model=P.Expand(applied.physicsOverrides)
 return P.AdoptEffective(d,bodies,model)
end
-- Changes between two requests: drives "Apply (n)".
function P.Diff(a,b)
 local out={} a=a or {} b=b or {}
 local ab,am=P.Expand(a.physicsOverrides) local bb,bm=P.Expand(b.physicsOverrides)
 local function add(body,field,axis,from,to) out[#out+1]={body=body,field=field,axis=axis,from=from,to=to} end
 for i=0,17 do local x,y=ab[i],bb[i]
  if i>0 then for _,axis in ipairs(P.AXES) do if not sameTriple(canonicalAxis(x.limits[axis]),canonicalAxis(y.limits[axis])) then add(i,'limits',axis,x.limits[axis],y.limits[axis]) end end end
  for _,f in ipairs({'massBias','rotdamping','damping','inertia','drag','surfaceprop'}) do if not sameField(f,x[f],y[f]) then add(i,f,nil,x[f],y[f]) end end
  local name=P.BODIES[i+1] local sa=(a.collisionOverrides or {})[name] local sb=(b.collisionOverrides or {})[name]
  local function shape(s) local c,e=type(s)=='table' and vec3(s.center),type(s)=='table' and vec3(s.extent) if not (c and e) then return '' end return string.format('%.4f %.4f %.4f %.4f %.4f %.4f %s',c[1],c[2],c[3],e[1],e[2],e[3],s.style or 'fitted') end
  if shape(sa)~=shape(sb) then add(i,'shape',nil,sa,sb) end
 end
 for _,f in ipairs({'surfaceprop','massMode'}) do if am[f]~=bm[f] then add(nil,f,nil,am[f],bm[f]) end end
 if am.automass~=bm.automass or (am.automass and q(am.density,1)~=q(bm.density,1)) then add(nil,'automass',nil,am.automass and am.density,bm.automass and bm.density) end
 local function rules(m) local c=m.collisions if c.mode=='custom' then local list={} for _,p in ipairs(P.NormalizePairs(c.pairs)) do list[#list+1]=p[1]..','..p[2] end return table.concat(list,' ') end return c.mode end
 if rules(am)~=rules(bm) then add(nil,'collisions',nil,am.collisions,bm.collisions) end
 local function animated(m) local x=m.animatedFriction if not x then return '' end return string.format('%d %d %.2f %.2f %.2f',x.min,x.max,x.timeIn,x.timeOut,x.timeHold) end
 if animated(am)~=animated(bm) then add(nil,'animatedFriction',nil,am.animatedFriction,bm.animatedFriction) end
 if q(tonumber(a.mass) or P.MASS,1)~=q(tonumber(b.mass) or P.MASS,1) then add(nil,'mass',nil,a.mass,b.mass) end
 local function set(list) local s={} for _,v in ipairs(list or {}) do s[#s+1]=tostring(v) end table.sort(s) return table.concat(s,',') end
 if set(a.excludedMaterials)~=set(b.excludedMaterials) then add(nil,'excludedMaterials',nil,a.excludedMaterials,b.excludedMaterials) end
 return out
end

-- Strict check of a physicsOverrides object, mirroring native canonicalPhysics (codes of §5.4).
local function validSurface(s) return type(s)=='string' and #s>=1 and #s<=32 and not s:find('[^a-z0-9_]') end
P.ValidSurface=validSurface
function P.CheckPhysics(raw)
 local errors={} local function fail(code,path,detail,lo,hi) errors[#errors+1]={code=code,path=path,detail=detail,min=lo,max=hi} end
 if raw==nil or (type(raw)=='table' and next(raw)==nil) then return errors end
 if type(raw)~='table' then fail('not_object','','expected an object') return errors end
 if raw.schema~=1 then fail('schema_unsupported','schema','this module reads physics schema 1') return errors end
 local function known(t,keys,path) for k in pairs(t) do if not keys[k] then fail('unknown_field',path=='' and tostring(k) or path..'.'..tostring(k),'not part of physics schema 1') end end end
 local function object(v,path) if type(v)=='table' then return v end fail('not_object',path,'expected an object') end
 local function number(v,path,decimals) if not finite(v) then fail('not_finite',path,'expected a number') return nil end return q(v,decimals) end
 local function integer(v,path) local n=number(v,path,6) if n and math.floor(n)~=n then fail('not_integer',path,'expected a whole number') return nil end return n end
 local function range(v,lo,hi,code,path) if v and (v<lo or v>hi) then fail(code,path,'expected '..lo..' to '..hi,lo,hi) return false end return v~=nil end
 known(raw,{schema=true,surfaceprop=true,massMode=true,automass=true,collisions=true,animatedFriction=true,bodies=true},'')
 local model='flesh'
 if raw.surfaceprop~=nil then if validSurface(raw.surfaceprop) then model=raw.surfaceprop else fail('surfaceprop','surfaceprop','expected 1 to 32 of a-z, 0-9 and _') end end
 if raw.massMode~=nil and raw.massMode~='bias' and raw.massMode~='volume' then fail('mass_mode','massMode','expected bias or volume') end
 if raw.automass~=nil then local o=object(raw.automass,'automass') if o then known(o,{density=true},'automass')
  if o.density==nil then fail('density_range','automass.density','missing') else range(number(o.density,'automass.density',1),10,20000,'density_range','automass.density') end end end
 if raw.collisions~=nil then local o=object(raw.collisions,'collisions') if o then known(o,{mode=true,pairs=true},'collisions')
  if o.mode=='custom' then
   if o.pairs~=nil and type(o.pairs)~='table' then fail('collision_mode','collisions.pairs','expected a list of pairs')
   else for n,p in ipairs(o.pairs or {}) do local path='collisions.pairs.'..(n-1)
    if type(p)~='table' or #p~=2 then fail('pair_index',path,'expected [a, b]') else
     local a,b=integer(p[1],path),integer(p[2],path)
     if a and b then if a<0 or a>17 or b<0 or b>17 or a==b then fail('pair_index',path,'expected two different bodies 0 to 17') elseif P.Adjacent(a,b) then fail('pair_adjacent',path,'joined bodies never collide') end end
    end end end
  elseif o.mode=='none' or o.mode=='all' then if o.pairs~=nil and next(o.pairs)~=nil then fail('collision_mode','collisions.pairs','pairs need mode custom') end
  else fail('collision_mode','collisions.mode','expected all, none or custom') end
 end end
 if raw.animatedFriction~=nil then local o=object(raw.animatedFriction,'animatedFriction') if o then known(o,{min=true,max=true,timeIn=true,timeOut=true,timeHold=true},'animatedFriction') local ok=true local v={}
  for _,k in ipairs({'min','max','timeIn','timeOut','timeHold'}) do local path='animatedFriction.'..k local whole=k=='min' or k=='max'
   if o[k]==nil then fail('animated_friction_range',path,'missing') ok=false else
    local n if whole then n=integer(o[k],path) else n=number(o[k],path,2) end
    if n==nil then ok=false elseif not range(n,0,whole and 1000 or 10,'animated_friction_range',path) then ok=false else v[k]=n end end
  end
  if ok and v.min>v.max then fail('animated_friction_range','animatedFriction.min','the minimum is above the maximum') end
 end end
 if raw.bodies~=nil then local bodies=object(raw.bodies,'bodies') if bodies then
  for name,b in pairs(bodies) do local path='bodies.'..tostring(name) local i=P.INDEX[name]
   if not i then fail('unknown_body',path,'not one of the 18 carrier bodies')
   elseif object(b,path) then
    known(b,{limits=true,massBias=true,damping=true,rotdamping=true,inertia=true,drag=true,surfaceprop=true},path)
    if b.limits~=nil then
     if i==0 then fail('root_joint',path..'.limits','the pelvis is the root and has no joint')
     elseif object(b.limits,path..'.limits') then known(b.limits,{x=true,y=true,z=true},path..'.limits')
      for _,axis in ipairs(P.AXES) do local t=b.limits[axis] local ap=path..'.limits.'..axis
       if t~=nil then
        if type(t)~='table' or #t~=3 then fail('limit_range',ap,'expected [min, max, friction]') else
         local lo,hi,f=number(t[1],ap,1),number(t[2],ap,1),number(t[3],ap,3)
         if lo and hi and f then
          if f<0 or f>100 then fail('friction_range',ap,'expected 0 to 100',0,100)
          elseif not (lo==-360 and hi==360) then if lo<-180 or hi>180 then fail('limit_range',ap,'expected -180 to 180',-180,180) elseif lo>hi then fail('limit_order',ap,'the minimum is above the maximum') end end
         end
        end
       end
      end
     end
    end
    local limits={massBias={.01,100,'mass_bias_range'},damping={0,10,'damping_range'},rotdamping={0,100,'rotdamping_range'},inertia={.1,100,'inertia_range'},drag={0,100,'drag_range'}}
    for _,f in ipairs({'massBias','damping','rotdamping','inertia','drag'}) do if b[f]~=nil then local r=limits[f] range(number(b[f],path..'.'..f,3),r[1],r[2],r[3],path..'.'..f) end end
    if b.surfaceprop~=nil and not validSurface(b.surfaceprop) then fail('surfaceprop',path..'.surfaceprop','expected 1 to 32 of a-z, 0-9 and _') end
   end
  end
 end end
 return errors
end
-- A request from the editor (client before sending, server again on receipt).
-- ctx = {unit=, level=, materialCount=, surfaceKnown=function(name)}. Returns ok, errors ({field, reason, code}).
function P.Validate(request,ctx)
 ctx=ctx or {} local errors={}
 local function fail(code,field,reason) errors[#errors+1]={code=code,field=field,reason=reason or code} end
 if type(request)~='table' then fail('invalid','request') return false,errors end
 local u=tonumber(ctx.unit) or 1
 if request.collisionOverrides~=nil then
  if type(request.collisionOverrides)~='table' then fail('shape_range','collisionOverrides','not_object') else
   for name,o in pairs(request.collisionOverrides) do
    if not P.INDEX[name] then fail('unknown_body','collisionOverrides.'..tostring(name))
    elseif type(o)~='table' then fail('shape_range','collisionOverrides.'..name,'not_object')
    else
     for k in pairs(o) do if k~='center' and k~='extent' and k~='style' then fail('unknown_field','collisionOverrides.'..name..'.'..tostring(k)) end end
     local c,e=vec3(o.center),vec3(o.extent)
     if (o.center~=nil and not c) or (o.extent~=nil and not e) then fail('shape_range','collisionOverrides.'..name,'not_finite')
     else
      for k=1,3 do if c and not (finite(c[k]) and math.abs(c[k])<=72*u) then fail('shape_range','collisionOverrides.'..name..'.center') break end end
      for k=1,3 do if e and not (finite(e[k]) and e[k]>=.01*u and e[k]<=36*u) then fail('shape_range','collisionOverrides.'..name..'.extent') break end end
     end
     if o.style~=nil and not P.STYLES[o.style] then fail('shape_range','collisionOverrides.'..name..'.style','style') end
    end
   end
  end
 end
 if request.collisionOverrideScale~=nil and not (finite(request.collisionOverrideScale) and request.collisionOverrideScale>0) then fail('range','collisionOverrideScale') end
 if request.mass~=nil and not (finite(request.mass) and request.mass>=1 and request.mass<=500) then fail('range','mass','1-500') end
 if request.excludedMaterials~=nil then
  if type(request.excludedMaterials)~='table' or #request.excludedMaterials>1024 then fail('range','excludedMaterials') else
   for _,v in pairs(request.excludedMaterials) do if not isint(v) or v<0 or (ctx.materialCount and v>=ctx.materialCount) then fail('range','excludedMaterials',tostring(v)) break end end
  end
 end
 for _,e in ipairs(P.CheckPhysics(request.physicsOverrides)) do fail(e.code,e.path,e.code) end
 if ctx.surfaceKnown and type(request.physicsOverrides)=='table' then
  local function known(name,field) if validSurface(name) and not ctx.surfaceKnown(name) then fail('surfaceprop_unknown',field,name) end end
  if request.physicsOverrides.surfaceprop then known(request.physicsOverrides.surfaceprop,'surfaceprop') end
  for name,b in pairs(type(request.physicsOverrides.bodies)=='table' and request.physicsOverrides.bodies or {}) do if type(b)=='table' and b.surfaceprop then known(b.surfaceprop,'bodies.'..tostring(name)..'.surfaceprop') end end
 end
 return #errors==0,errors
end
-- Whether spawn options carry a physics profile or a box/capsule shape: what a
-- saved default or dupe may fail on with another module, and is retried without.
function P.HasPhysicsEdits(options)
 if type(options)~='table' then return false end
 if type(options.physicsOverrides)=='table' and next(options.physicsOverrides)~=nil then return true end
 for _,o in pairs(type(options.collisionOverrides)=='table' and options.collisionOverrides or {}) do if type(o)=='table' and o.style~=nil and o.style~='fitted' then return true end end
 return false
end
-- The options with automatic physics ({} never loads a saved profile again) and fitted shapes.
function P.WithoutPhysics(options)
 local out=copy(options or {}) out.physicsOverrides={} out.physicsEditor=nil
 for _,o in pairs(type(out.collisionOverrides)=='table' and out.collisionOverrides or {}) do if type(o)=='table' then o.style=nil end end
 return out
end
-- Strips what an older server module cannot build: physics and shape styles (level 0).
function P.ShapesOnly(request)
 local out=copy(request or {}) out.physicsOverrides=nil out.physicsEditor=nil
 for _,o in pairs(type(out.collisionOverrides)=='table' and out.collisionOverrides or {}) do if type(o)=='table' then o.style=nil end end
 return out
end

-- Masses as native writes them (solidMasses): weight shares, or studiomdl's volume weighting.
function P.Masses(bodies,model,mass,volumes,edited)
 mass=tonumber(mass) or P.MASS local out={} local bias=0
 for i=0,17 do bias=bias+bodies[i].massBias end
 if model and model.massMode=='volume' and volumes then local weighted=0
  for i=0,17 do weighted=weighted+(volumes[i] or 0)*bodies[i].massBias end
  if weighted>0 then local total=0 for i=0,17 do out[i]=math.max(1,mass*(volumes[i] or 0)*bodies[i].massBias/weighted) total=total+out[i] end return out,total end
 end
 local total=0 for i=0,17 do local m=mass*bodies[i].massBias/bias if edited~=false then m=math.max(.1,m) end out[i]=m total=total+m end
 return out,total
end

-- Simple-mode edits. Each returns the draft; mirror applies the same change to P.MIRROR[i].
local function partEdited(d) if d.feel.preset~='template' then d.feel.preset='custom' end end
function P.FeelMatches(d,id)
 local p=P.PRESETS[id] if not p then return false end
 return d.feel.stiffness==p[1] and d.feel.range==p[2] and d.feel.floatiness==p[3] and next(d.joints)==nil and next(d.explicit)==nil
end
function P.ApplyPreset(d,id,clear)
 local p=P.PRESETS[id] if not p then return d end
 d.feel={preset=id,stiffness=p[1],range=p[2],floatiness=p[3]}
 if clear then d.joints={} d.explicit={} end
 return d
end
function P.SetFeel(d,field,value)
 d.feel[field]=value
 local matched for _,id in ipairs(P.PRESET_ORDER) do local p=P.PRESETS[id] if d.feel.stiffness==p[1] and d.feel.range==p[2] and d.feel.floatiness==p[3] then matched=id end end
 if next(d.joints)==nil and next(d.explicit)==nil and matched then d.feel.preset=matched elseif d.feel.preset~='template' then d.feel.preset='custom' end
 return d
end
local function targets(i,mirror) local list={i} if mirror and P.MIRROR[i] then list[2]=P.MIRROR[i] end return list end
function P.SetJointSimple(d,i,field,value,mirror)
 for _,k in ipairs(targets(i,mirror)) do
  if k>0 and (field~='hinge' or P.HINGES[k]) then
   local j=d.joints[k] or {} j[field]=value
   if j.stiffness==nil and j.range==nil and not j.hinge and not j.locked then d.joints[k]=nil else d.joints[k]=j end
  end
 end
 partEdited(d) return d
end
function P.MirrorLimits(i,triple,axis)
 if P.MIRROR_NEGATE[i] and P.MIRROR_NEGATE[i][axis] and not (triple[1]==-360 and triple[2]==360) then return {-triple[2],-triple[1],triple[3]} end
 return {triple[1],triple[2],triple[3]}
end
-- An Advanced joint preset (§8.6) as explicit limits on all three axes.
function P.ApplyJointPreset(d,i,id,mirror)
 if i<1 then return d end
 local current=P.Effective(d)
 for n,k in ipairs(targets(i,mirror)) do
  local def,capk=P.DEFAULTS[k].limits,P.CAPS[k] local e=d.explicit[k] or {}
  if id=='default' then e.limits=nil d.joints[k]=nil
  else
   local limits={}
   for _,axis in ipairs(P.AXES) do local L,H=def[axis][1],def[axis][2] local f=current[k].limits[axis][3]
    if id=='tighter' then limits[axis]={q(L*.75,1),q(H*.75,1),f}
    elseif id=='looser' then limits[axis]={q(math.max(L*1.25,math.min(L,capk[axis][1])),1),q(math.min(H*1.25,math.max(H,capk[axis][2])),1),f}
    elseif id=='hinge' then limits[axis]=(P.HINGES[k] and axis~='z') and {0,0,0} or {L,H,f}
    elseif id=='locked' then limits[axis]={0,0,0}
    elseif id=='free' then limits[axis]={-360,360,f} end
    if limits[axis] then local t=limits[axis] if t[1]==0 and t[2]==0 then t[3]=0 end end
   end
   e.limits=limits
  end
  d.explicit[k]=next(e) and e or nil
 end
 partEdited(d) return d
end
-- An Advanced value: limits[axis] (a triple), or a body field; nil returns it to the stop value.
function P.SetExplicit(d,i,field,value,axis,mirror)
 for n,k in ipairs(targets(i,mirror)) do
  local e=d.explicit[k] or {}
  if field=='limits' then
   if k>0 then e.limits=e.limits or {} e.limits[axis]=value and (n==1 and {value[1],value[2],value[3]} or P.MirrorLimits(i,value,axis)) or nil if next(e.limits)==nil then e.limits=nil end end
  else e[field]=value end
  d.explicit[k]=next(e) and e or nil
 end
 partEdited(d) return d
end

-- Shapes. A body's base shape: the draft entry, the latest exact preview, then rig.json.
function P.BaseShape(d,i,rig,preview)
 local s=d.shapes[i] if s then return {center={s.center[1],s.center[2],s.center[3]},extent={s.extent[1],s.extent[2],s.extent[3]},style=s.style or 'fitted'} end
 local b=preview and preview.bodies and preview.bodies[i+1] or rig and rig.bodies and rig.bodies[i+1]
 if not b then return nil end
 return {center={b.center[1],b.center[2],b.center[3]},extent={b.extent[1],b.extent[2],b.extent[3]},style=b.style or 'fitted'}
end
local function rotate(qt,v)
 local x,y,z,w=qt[1],qt[2],qt[3],qt[4]
 local tx,ty,tz=2*(y*v[3]-z*v[2]),2*(z*v[1]-x*v[3]),2*(x*v[2]-y*v[1])
 return {v[1]+w*tx+(y*tz-z*ty),v[2]+w*ty+(z*tx-x*tz),v[3]+w*tz+(x*ty-y*tx)}
end
local function boneOf(rig,i) local b=rig and rig.bodies and rig.bodies[i+1] return b and rig.bones and rig.bones[b.bone+1] end
-- +1 when the body's local +X points toward its child joint, -1 when away, 0 otherwise.
function P.Distal(rig,i)
 local child=DISTAL_CHILD[i] if not child then return 0 end
 local a,b=boneOf(rig,i),boneOf(rig,child) if not (a and b and a.rotation and a.position and b.position) then return 0 end
 local d={b.position[1]-a.position[1],b.position[2]-a.position[2],b.position[3]-a.position[3]} local len=math.sqrt(d[1]^2+d[2]^2+d[3]^2)
 if len<1e-6 then return 0 end local x=rotate(a.rotation,{1,0,0}) local s=(x[1]*d[1]+x[2]*d[2]+x[3]*d[3])/len
 return s>.5 and 1 or s<-.5 and -1 or 0
end
-- Rounded first, then held inside bounds rounded inwards: a clamped value never rounds back out of range.
local function clampShape(s,u)
 local lo,hi,c=math.ceil(.01*u*1e4)/1e4,math.floor(36*u*1e4)/1e4,math.floor(72*u*1e4)/1e4
 for k=1,3 do s.extent[k]=clamp(q(s.extent[k],4),lo,hi) s.center[k]=clamp(q(s.center[k],4),-c,c) end return s
end
-- Mirrors a shape onto the other side through the rest frames (§3.3); returns the shape and whether its centre was mirrored.
function P.MirrorShape(rig,i,shape)
 local j=P.MIRROR[i] local out={center={shape.center[1],shape.center[2],shape.center[3]},extent={shape.extent[1],shape.extent[2],shape.extent[3]},style=shape.style}
 local src,dst=boneOf(rig,i),boneOf(rig,j) local lt,rt=bonePosition(rig,P.BODIES[13]),bonePosition(rig,P.BODIES[16])
 if not (j and src and dst and src.rotation and dst.rotation and lt and rt) then return out,false end
 local n={lt[1]-rt[1],lt[2]-rt[2],lt[3]-rt[3]} local len=math.sqrt(n[1]^2+n[2]^2+n[3]^2) if len<1e-6 then return out,false end
 for k=1,3 do n[k]=n[k]/len end
 local function reflect(v) local d=v[1]*n[1]+v[2]*n[2]+v[3]*n[3] return {v[1]-2*d*n[1],v[2]-2*d*n[2],v[3]-2*d*n[3]} end
 local center=out.center
 for k=1,3 do
  local e={0,0,0} e[k]=1 local axis=reflect(rotate(src.rotation,e))
  local de={0,0,0} de[k]=1 local target=rotate(dst.rotation,de)
  local m=axis[1]*target[1]+axis[2]*target[2]+axis[3]*target[3]
  if math.abs(m)<=.95 then out.center=nil return out,false end
  center[k]=(m>0 and 1 or -1)*shape.center[k]
 end
 return out,true
end
local function setShape(d,i,s,rig,preview,mirror,mirrored)
 local u=P.Unit(rig) d.shapes[i]=clampShape(s,u)
 return d
end
-- Parts tab buttons (§8.7): longer/shorter keep the proximal end, thicker/thinner scale Y and Z.
function P.ShapeAction(d,i,action,rig,mirror,preview)
 local u=P.Unit(rig)
 for _,k in ipairs(targets(i,mirror)) do
  if action=='reset' then d.shapes[k]=nil
  else
   local s=P.BaseShape(d,k,rig,preview)
   if s then
    if action=='longer' or action=='shorter' then local old=s.extent[1] s.extent[1]=action=='longer' and old*1.1 or old/1.1 s.extent[1]=clamp(s.extent[1],.01*u,36*u) s.center[1]=s.center[1]+P.Distal(rig,k)*(s.extent[1]-old)
    elseif action=='thicker' or action=='thinner' then for a=2,3 do s.extent[a]=action=='thicker' and s.extent[a]*1.1 or s.extent[a]/1.1 end end
    d.shapes[k]=clampShape(s,u)
   end
  end
 end
 return d
end
function P.AllThickness(d,factor,rig,preview)
 local u=P.Unit(rig)
 for i=0,17 do local s=P.BaseShape(d,i,rig,preview) if s then for a=2,3 do s.extent[a]=s.extent[a]*factor end d.shapes[i]=clampShape(s,u) end end
 return d
end
-- Sets one shape value from the Numbers tab; mirroring writes the reflected shape to the other side.
function P.SetShape(d,i,shape,rig,mirror)
 local u=P.Unit(rig) d.shapes[i]=clampShape({center={shape.center[1],shape.center[2],shape.center[3]},extent={shape.extent[1],shape.extent[2],shape.extent[3]},style=shape.style or 'fitted'},u)
 local skipped=false
 if mirror and P.MIRROR[i] then local m,centered=P.MirrorShape(rig,i,d.shapes[i]) skipped=not centered
  if not centered then local base=P.BaseShape(d,P.MIRROR[i],rig) if base then m.center=base.center end end
  d.shapes[P.MIRROR[i]]=clampShape(m,u)
 end
 return d,skipped
end

-- Validation and advice (§10). preview is the exact native preview (or nil),
-- baseline the preview taken when the editor opened. ctx={rig=, level=, surfaceKnown=, approximate=, base=, mirrorSkipped=}.
local function issue(list,severity,code,params,body,axis,fixes) list[#list+1]={severity=severity,code=code,params=params or {},body=body,axis=axis,fixes=fixes or {}} end
function P.Check(d,preview,baseline,ctx)
 ctx=ctx or {} local list={} local bodies,model=P.Effective(d)
 local u,m=P.Unit(ctx.rig) local request=P.Resolve(d,ctx.rig)
 for _,e in ipairs(P.CheckPhysics(request.physicsOverrides)) do
  -- Body names contain dots themselves.
  local i=e.path:match('^bodies%.(ValveBiped%.Bip01_[%w_]+)') i=i and P.INDEX[i]
  local axis=e.path:match('limits%.(%a)$')
  if e.code=='limit_order' then issue(list,'error','limit_order',{},i,axis,{'swap'})
  elseif e.code=='root_joint' then issue(list,'error','root_joint',{},0,nil,{'remove'})
  elseif e.code=='pair_adjacent' then issue(list,'error','pair_adjacent',{},nil,nil,{'remove'})
  else issue(list,'error','range',{field=e.path,min=e.min or '',max=e.max or ''},i,axis,axis and {'clamp'} or {}) end
 end
 for i=0,17 do local s=d.shapes[i] if s then local bad=false
  for k=1,3 do if math.abs(s.center[k])>72*u+1e-6 or s.extent[k]<.01*u-1e-6 or s.extent[k]>36*u+1e-6 then bad=true end end
  if bad then issue(list,'error','shape_range',{},i,nil,{'reset_shape'}) end
  for k=1,3 do if s.extent[k]<.25*m then issue(list,'warning','thin_shape',{},i,nil,{'thicker'}) break end end
 end end
 if ctx.surfaceKnown then
  local function known(name,i) if validSurface(name) and not ctx.surfaceKnown(name) then issue(list,'error','surfaceprop_unknown',{name=name},i,nil,{'use_model_material'}) end end
  known(model.surfaceprop or 'flesh',nil) for i=0,17 do if bodies[i].surfaceprop~=(model.surfaceprop or 'flesh') then known(bodies[i].surfaceprop,i) end end
 end
 for i=1,17 do local b=bodies[i]
  local verifiedNoted=false
  for _,axis in ipairs(P.AXES) do local t=b.limits[axis] local free=t[1]==-360 and t[2]==360
   if free then issue(list,'warning','free_axis',{axis=axis},i,axis,{'natural_range'})
   elseif t[1]<=t[2] and (t[1]>0 or t[2]<0) then issue(list,'warning','rest_outside',{axis=axis,min=t[1],max=t[2]},i,axis,{'include_rest'}) end
   local explicit=d.explicit[i] and d.explicit[i].limits and d.explicit[i].limits[axis]
   local c=P.CAPS[i][axis]
   if explicit and not free and (t[1]<c[1] or t[2]>c[2]) then issue(list,'warning','wide_limit',{axis=axis,cap=c[1]..'…'..c[2]},i,axis,{'natural_range'}) end
   if t[3]>10 then issue(list,'warning','high_friction',{axis=axis},i,axis) end
   -- Simple stops scale about 0 and do not depend on the axis sign; typed numbers do.
   if explicit and not verifiedNoted and not (P.VERIFIED[i] and P.VERIFIED[i][axis]) then verifiedNoted=true issue(list,'note','axis_unverified',{axis=axis},i,axis) end
  end
 end
 for i=0,17 do local b=bodies[i]
  if b.damping>5 or b.rotdamping>50 then issue(list,'warning','high_damping',{},i,nil,{'reset'}) end
  if b.inertia<1 then issue(list,'warning','low_inertia',{},i,nil,{'reset'}) end
 end
 local edited=next(request.physicsOverrides)~=nil
 local volumes if preview and preview.bodies then volumes={} for i=0,17 do volumes[i]=preview.bodies[i+1] and preview.bodies[i+1].volume or 0 end end
 local masses
 if preview and preview.bodies then masses={} for i=0,17 do masses[i]=preview.bodies[i+1].mass end else masses=P.Masses(bodies,model,request.mass,nil,edited) end
 local raw=edited and P.Masses(bodies,{massMode=model.massMode},model.automass and preview and preview.mass or request.mass,volumes,false)
 for i=0,17 do
  if masses[i] and masses[i]<.25 then issue(list,'warning','mass_light',{kg=masses[i]},i,nil,{'raise_weight'}) end
  if raw and raw[i] and masses[i] and raw[i]<masses[i]-1e-4 then issue(list,'note','mass_floor',{kg=raw[i],floor=masses[i]},i) end
 end
 for i=1,17 do local p=P.PARENT[i] local a,b=masses[i],masses[p]
  if a and b and a>0 and b>0 and math.max(a,b)/math.min(a,b)>12 then local light,heavy=a<b and i or p,a<b and p or i issue(list,'warning','mass_ratio',{ratio=math.max(a,b)/math.min(a,b),light=light,heavy=heavy},light,nil,{'even_weights'}) end
 end
 local total=0 for i=0,17 do total=total+(masses[i] or 0) end
 if total>300 then issue(list,'note','heavy',{},nil) end
 if preview and preview.penetrations then
  local base={} for _,p in ipairs(baseline and baseline.penetrations or {}) do base[pairKey(p.a,p.b)]=p.depth end
  for _,p in ipairs(preview.penetrations) do local before=base[pairKey(p.a,p.b)] or 0
   if p.depth>.5*m and p.depth>before+.05*m then issue(list,'error','penetration_severe',{a=p.a,b=p.b,cm=p.depth*2.54},p.a,nil,{'pass_through','shrink'})
   elseif p.depth>math.max(.12*m,before+.05*m) then issue(list,'warning','penetration',{a=p.a,b=p.b,cm=p.depth*2.54},p.a,nil,{'pass_through','shrink'}) end
  end
 end
 if model.animatedFriction then issue(list,'note','animfriction_ragdoll',{}) end
 for i=0,17 do if bodies[i].drag then issue(list,'note','drag_approximate',{}) break end end
 if model.collisions and (model.collisions.mode=='none' or (model.collisions.mode=='custom' and #P.NormalizePairs(model.collisions.pairs)==0)) then issue(list,'note','no_self_collision',{}) end
 if model.automass then issue(list,'note','automass_estimate',{}) end
 if ctx.mirrorSkipped then issue(list,'note','mirror_center_skipped',{}) end
 if ctx.approximate then issue(list,'note','approx_preview',{}) end
 if preview and ctx.base and preview.key==ctx.base then issue(list,'note','unchanged',{}) end
 local order={error=1,warning=2,note=3}
 table.sort(list,function(x,y) if order[x.severity]~=order[y.severity] then return order[x.severity]<order[y.severity] end return (x.body or 99)<(y.body or 99) end)
 return list
end
function P.HasErrors(list) for _,v in ipairs(list or {}) do if v.severity=='error' then return true end end return false end
-- One click of a Checks fix button (§10). "shrink" is one 0.94 step; the editor repeats it after each preview.
function P.ApplyFix(d,item,fix,rig,preview)
 local i,axis=item.body,item.axis local bodies,model=P.Effective(d)
 local function setAxis(t) P.SetExplicit(d,i,'limits',t,axis) end
 if fix=='swap' and i and axis then local t=bodies[i].limits[axis] setAxis({t[2],t[1],t[3]})
 elseif fix=='clamp' and i and axis then local t=bodies[i].limits[axis] setAxis({clamp(t[1],-180,180),clamp(t[2],-180,180),clamp(t[3],0,100)})
 elseif fix=='include_rest' and i and axis then local t=bodies[i].limits[axis] setAxis({math.min(t[1],0),math.max(t[2],0),t[3]})
 elseif fix=='natural_range' and i and axis then local c=P.CAPS[i][axis] local t=bodies[i].limits[axis] setAxis({c[1],c[2],t[3]})
 elseif fix=='remove' and i==0 and d.explicit[0] then d.explicit[0].limits=nil
 elseif fix=='remove' then d.model.collisions={mode='custom',pairs=P.NormalizePairs(d.model.collisions.mode=='custom' and d.model.collisions.pairs or P.AllPairs())}
 elseif fix=='reset_shape' and i then d.shapes[i]=nil
 elseif fix=='thicker' and i then local s=P.BaseShape(d,i,rig,preview) local _,m=P.Unit(rig) if s then for k=1,3 do s.extent[k]=math.max(s.extent[k],.25*m) end P.SetShape(d,i,s,rig) end
 elseif fix=='reset' and i then local e=d.explicit[i] if e then e.damping=nil e.rotdamping=nil e.inertia=nil if next(e)==nil then d.explicit[i]=nil end end
 elseif fix=='use_model_material' then if i then local e=d.explicit[i] if e then e.surfaceprop=nil end else d.model.surfaceprop='flesh' end
 elseif fix=='pass_through' and item.params.a then local list=d.model.collisions.mode=='custom' and P.NormalizePairs(d.model.collisions.pairs) or (d.model.collisions.mode=='all' and P.AllPairs() or {})
  local out={} for _,p in ipairs(list) do if pairKey(p[1],p[2])~=pairKey(item.params.a,item.params.b) then out[#out+1]=p end end d.model.collisions={mode='custom',pairs=out}
 elseif fix=='shrink' and item.params.a then for _,k in ipairs({item.params.a,item.params.b}) do local s=P.BaseShape(d,k,rig,preview) if s then s.extent[2]=s.extent[2]*.94 s.extent[3]=s.extent[3]*.94 P.SetShape(d,k,s,rig) end end
 elseif fix=='even_weights' and item.params.light then local light,heavy=item.params.light,item.params.heavy
  P.SetExplicit(d,light,'massBias',q(clamp(bodies[heavy].massBias/8,.01,100),3))
 elseif fix=='raise_weight' then
  local masses=P.Masses(bodies,model,1,nil,false) local smallest=math.huge for k=0,17 do smallest=math.min(smallest,masses[k]) end
  if smallest>0 then d.mass=q(math.min(500,math.max(d.mass,math.ceil(.25/smallest*10)/10)),1) end
 end
 return d
end

-- QC: tokens, import (§11.2) and export (§11.3). Arguments come only from the command's own line.
function P.Tokenize(text)
 local tokens={} local line,i,n=1,1,#text
 while i<=n do
  local c=text:sub(i,i)
  if c=='\n' then line=line+1 i=i+1
  elseif c:match('%s') then i=i+1
  elseif text:sub(i,i+1)=='//' then local e=text:find('\n',i,true) i=e or n+1
  elseif text:sub(i,i+1)=='/*' then local e=text:find('*/',i+2,true) local stop=e and e+1 or n
   for _ in text:sub(i,stop):gmatch('\n') do line=line+1 end i=stop+1
  elseif c=='"' then local e=text:find('"',i+1,true) local stop=e or n+1 local value=text:sub(i+1,stop-1)
   tokens[#tokens+1]={text=value,line=line,quoted=true} for _ in value:gmatch('\n') do line=line+1 end i=stop+1
  elseif c=='{' or c=='}' then tokens[#tokens+1]={text=c,line=line} i=i+1
  else local s,e=text:find('^[^%s"{}]+',i) local word=text:sub(s,e) local cut=word:find('//',1,true) or word:find('/*',1,true)
   if cut and cut>1 then word=word:sub(1,cut-1) e=s+cut-2 end tokens[#tokens+1]={text=word,line=line} i=e+1 end
 end
 return tokens
end
local function carrierBone(name)
 if type(name)~='string' then return nil end local lower=name:lower()
 for i,full in ipairs(P.BODIES) do if lower==full:lower() then return i-1,false end end
 for i,full in ipairs(P.BODIES) do if 'valvebiped.'..lower==full:lower() then return i-1,true end end
end
local unsupported={['$jointskip']=true,['$jointmerge']=true,['$concave']=true,['$concaveperjoint']=true,['$maxconvexpieces']=true,['$masscenter']=true,['$remove2d']=true,['$weldposition']=true,['$weldnormal']=true,['$assumeworldspace']=true,['$rollingdrag']=true}
-- The studiomdl base: what a bare $collisionjoints compiles to before any joint line.
function P.StudiomdlBase()
 local bodies={} for i=0,17 do bodies[i]={massBias=1,rotdamping=0,damping=0,inertia=1,surfaceprop='flesh'} if i>0 then bodies[i].limits={x={0,0,0},y={0,0,0},z={0,0,0}} end end
 return bodies
end
-- Parses QC text into the draft (a copy is returned) with a per-line report. opts={base='studiomdl'|'merge', volumeWeighting=bool}.
function P.ParseQC(text,opts,draft)
 opts=opts or {} local d=copy(draft or P.NewDraft()) local report={rows={},applied=0,ignored=0,errors=0}
 local totals={applied='applied',ignored='ignored',error='errors'}
 local function row(line,status,command,reason,params) report.rows[#report.rows+1]={line=line,status=status,command=command,reason=reason,params=params or {}} report[totals[status]]=report[totals[status]]+1 end
 local tokens=P.Tokenize(tostring(text or ''))
 local commands={} local k=1
 local function skipBlock(at) if tokens[at] and tokens[at].text=='{' then local depth=0 repeat local t=tokens[at] if t.text=='{' then depth=depth+1 elseif t.text=='}' then depth=depth-1 end at=at+1 until depth==0 or not tokens[at] end return at end
 while k<=#tokens do
  local t=tokens[k]
  if not t.quoted and t.text:sub(1,1)=='$' then
   local name=t.text:lower() local args={} local j=k+1
   while tokens[j] and tokens[j].line==t.line and tokens[j].text~='{' and tokens[j].text~='}' and not (not tokens[j].quoted and tokens[j].text:sub(1,1)=='$') do args[#args+1]=tokens[j].text j=j+1 end
   commands[#commands+1]={name=name,args=args,line=t.line}
   if name=='$collisiontext' then j=skipBlock(j)
   elseif name~='$collisionjoints' and name~='$collisionmodel' and tokens[j] and tokens[j].text=='{' and not name:find('^%$joint') and name~='$mass' then
    local known={['$automass']=1,['$inertia']=1,['$damping']=1,['$rotdamping']=1,['$drag']=1,['$noselfcollisions']=1,['$animatedfriction']=1,['$rootbone']=1,['$surfaceprop']=1}
    if not known[name] and not unsupported[name] then j=skipBlock(j) end
   end
   k=j
  else k=k+1 end
 end
 local bodies,model
 if opts.base=='merge' then bodies,model=P.Effective(d) model=copy(model)
 else bodies=P.StudiomdlBase() model={surfaceprop='flesh',massMode=opts.volumeWeighting and 'volume' or 'bias',automass=false,density=d.model.density or 1000,collisions={mode='all',pairs={}}} end
 if opts.volumeWeighting then model.massMode='volume' end
 local defaults,constrained,list,noself,oldSurface={},{},{},false,model.surfaceprop or 'flesh'
 -- Model-level values apply before per-joint ones whatever their order (studiomdl's defaults).
 for _,c in ipairs(commands) do local v=tonumber(c.args[1])
  if c.name=='$inertia' or c.name=='$damping' or c.name=='$rotdamping' or c.name=='$drag' then
   local field=({['$inertia']='inertia',['$damping']='damping',['$rotdamping']='rotdamping',['$drag']='drag'})[c.name] local r=P.LIMITS[field]
   if not v or #c.args~=1 then row(c.line,'error',c.name,'qc.reason.args') elseif v<r[1] or v>r[2] then row(c.line,'error',c.name,'qc.reason.range',{min=r[1],max=r[2]}) else defaults[field]=q(v,3) row(c.line,'applied',c.name) end
  elseif c.name=='$surfaceprop' then
   if validSurface(c.args[1]) then model.surfaceprop=c.args[1] row(c.line,'applied',c.name) else row(c.line,'error',c.name,'qc.reason.args') end
  end
 end
 for field,v in pairs(defaults) do for i=0,17 do bodies[i][field]=v end end
 for i=0,17 do if opts.base~='merge' or not bodies[i].surfaceprop or bodies[i].surfaceprop==oldSurface then bodies[i].surfaceprop=model.surfaceprop end end
 for _,c in ipairs(commands) do local n=c.name local a=c.args
  if n=='$collisionjoints' then row(c.line,'applied',n)
  elseif n=='$collisionmodel' then row(c.line,'error',n,'qc.reason.not_jointed')
  elseif n=='$collisiontext' then row(c.line,'ignored',n,'qc.reason.collisiontext')
  elseif n=='$mass' then local v=tonumber(a[1]) if not v or #a~=1 then row(c.line,'error',n,'qc.reason.args') elseif v<1 or v>500 then row(c.line,'error',n,'qc.reason.range',{min=1,max=500}) else d.mass=q(v,1) model.automass=false row(c.line,'applied',n) end
  elseif n=='$automass' then model.automass=true model.density=model.density or 1000 row(c.line,'applied',n)
  elseif n=='$jointinertia' or n=='$jointdamping' or n=='$jointrotdamping' or n=='$jointmassbias' then
   local field=({['$jointinertia']='inertia',['$jointdamping']='damping',['$jointrotdamping']='rotdamping',['$jointmassbias']='massBias'})[n] local r=P.LIMITS[field]
   local i,prefixed=carrierBone(a[1]) local v=tonumber(a[2])
   if #a~=2 or not v then row(c.line,'error',n,'qc.reason.args')
   elseif not i then row(c.line,'ignored',n,'qc.reason.not_carrier_body',{bone=a[1]})
   elseif v<r[1] or v>r[2] then row(c.line,'error',n,'qc.reason.range',{min=r[1],max=r[2]})
   else bodies[i][field]=q(v,3) row(c.line,'applied',n,prefixed and 'qc.reason.prefix' or nil) end
  elseif n=='$jointconstrain' then
   local i,prefixed=carrierBone(a[1]) local axis=a[2] and a[2]:lower() local kind=a[3] and a[3]:lower()
   if not i then row(c.line,'ignored',n,'qc.reason.not_carrier_body',{bone=a[1]})
   elseif i==0 then row(c.line,'error',n,'qc.reason.root')
   elseif not (axis=='x' or axis=='y' or axis=='z') or not (kind=='limit' or kind=='fixed' or kind=='free') then row(c.line,'error',n,'qc.reason.args')
   elseif constrained[i..axis] then row(c.line,'ignored',n,'qc.reason.first_wins')
   else
    local lo,hi,f=tonumber(a[4]),tonumber(a[5]),tonumber(a[6])
    if kind=='limit' and (not lo or not hi) then row(c.line,'error',n,'qc.reason.args')
    elseif kind=='limit' and lo>hi then row(c.line,'error',n,'qc.reason.order')
    elseif kind=='limit' and (lo<-180 or hi>180) then row(c.line,'error',n,'qc.reason.range',{min=-180,max=180})
    else
     -- studiomdl stores friction / 5; an omitted friction is QC 1.0.
     local fq=f or (kind=='fixed' and 0 or 1) local phy=q(fq/5,3)
     if phy<0 or phy>100 then row(c.line,'error',n,'qc.reason.range',{min=0,max=500})
     else
      constrained[i..axis]=true
      bodies[i].limits[axis]=kind=='fixed' and {0,0,0} or kind=='free' and {-360,360,phy} or canonicalAxis({lo,hi,phy})
      row(c.line,'applied',n,prefixed and 'qc.reason.prefix' or nil)
     end
    end
   end
  elseif n=='$noselfcollisions' then noself=true row(c.line,'applied',n)
  elseif n=='$jointcollide' then local x,y=carrierBone(a[1]),carrierBone(a[2])
   if not x or not y then row(c.line,'ignored',n,'qc.reason.not_carrier_body',{bone=not x and a[1] or a[2]})
   elseif x==y or P.Adjacent(x,y) then row(c.line,'ignored',n,'qc.reason.joined',{a=x,b=y})
   else list[#list+1]={x,y} row(c.line,'applied',n) end
  elseif n=='$animatedfriction' then
   if #a~=5 or not (tonumber(a[1]) and tonumber(a[2]) and tonumber(a[3]) and tonumber(a[4]) and tonumber(a[5])) then row(c.line,'ignored',n,'qc.reason.args')
   else local mn,mx=math.floor(tonumber(a[1])),math.floor(tonumber(a[2]))
    -- studiomdl's order: min max timeIn timeHold timeOut.
    local ti,th,to=q(tonumber(a[3]),2),q(tonumber(a[4]),2),q(tonumber(a[5]),2)
    if mn>mx then row(c.line,'error',n,'qc.reason.order')
    elseif mn<0 or mx>1000 or ti<0 or ti>10 or th<0 or th>10 or to<0 or to>10 then row(c.line,'error',n,'qc.reason.range',{min=0,max=1000})
    else model.animatedFriction={min=mn,max=mx,timeIn=ti,timeHold=th,timeOut=to} row(c.line,'applied',n) end
   end
  elseif n=='$rootbone' then local i=carrierBone(a[1]) if i==0 then row(c.line,'applied',n) else row(c.line,'ignored',n,'qc.reason.root') end
  elseif n=='$jointsurfaceprop' then local i=carrierBone(a[1])
   if not i then row(c.line,'ignored',n,'qc.reason.not_carrier_body',{bone=a[1]}) elseif not validSurface(a[2]) then row(c.line,'error',n,'qc.reason.args') else bodies[i].surfaceprop=a[2] row(c.line,'applied',n) end
  elseif unsupported[n] then row(c.line,'ignored',n,'qc.reason.unsupported',{command=n})
  end
 end
 if noself then model.collisions={mode='none',pairs={}} for _,r in ipairs(report.rows) do if r.command=='$jointcollide' and r.status=='applied' then r.status='ignored' r.reason='qc.reason.no_self' report.applied=report.applied-1 report.ignored=report.ignored+1 end end
 elseif #list>0 then model.collisions={mode='custom',pairs=P.NormalizePairs(list)} end
 if d.feel.preset~='template' then d.feel.preset='custom' end
 P.AdoptEffective(d,bodies,model)
 return d,report
end
local function num(v) local s=string.format('%.4f',v):gsub('0+$',''):gsub('%.$','') if s=='-0' then s='0' end return s end
P.FormatNumber=num
local function mostCommon(bodies,field)
 local counts,best,bestN={},nil,-1
 for i=0,17 do local v=num(bodies[i][field] or 0) counts[v]=(counts[v] or 0)+1 if counts[v]>bestN or (counts[v]==bestN and best and v<best) then best,bestN=v,counts[v] end end
 return best
end
-- The draft as a $collisionjoints block for the clipboard. opts={name=, key=, version=}.
function P.EmitQC(d,opts)
 opts=opts or {} local bodies,model=P.Effective(d) local out={}
 local function add(s) out[#out+1]=s end
 add('// Physics for "'..tostring(opts.name or 'model')..'" exported by Model Hotloader '..tostring(opts.version or '')..' (carrier '..tostring(opts.key or '')..')')
 add('// 18 bodies / 17 joints; angles in degrees in each child bone\'s frame.')
 add('// Friction is in QC units: studiomdl writes one fifth of each value into the .phy.')
 add('// Weight distribution here: '..(model.massMode=='volume' and 'by shape volume x weight share (studiomdl)' or 'by weight shares'))
 add('$collisionjoints "phymodel.smd"') add('{')
 add(model.automass and '\t$automass' or '\t$mass '..num(d.mass or P.MASS))
 local common={} for _,f in ipairs({'inertia','damping','rotdamping'}) do common[f]=mostCommon(bodies,f) add('\t$'..f..' '..common[f]) end
 local drag,uniform=bodies[0].drag,true for i=1,17 do if bodies[i].drag~=drag then uniform=false end end
 if uniform and drag then add('\t$drag '..num(drag)) end
 add('\t$rootbone "'..P.BODIES[1]..'"')
 local c=model.collisions or {} local list=c.mode=='custom' and P.NormalizePairs(c.pairs) or {}
 if c.mode=='none' or (c.mode=='custom' and #list==0) then add('\t$noselfcollisions') end
 local a=model.animatedFriction if a then add('\t$animatedfriction '..num(a.min)..' '..num(a.max)..' '..num(a.timeIn)..' '..num(a.timeHold)..' '..num(a.timeOut)) end
 for i=0,17 do if num(bodies[i].massBias)~='1' then add('\t$jointmassbias "'..P.BODIES[i+1]..'" '..num(bodies[i].massBias)) end end
 for _,f in ipairs({'inertia','damping','rotdamping'}) do for i=0,17 do if num(bodies[i][f])~=common[f] then add('\t$joint'..f..' "'..P.BODIES[i+1]..'" '..num(bodies[i][f])) end end end
 for i=1,17 do for _,axis in ipairs(P.AXES) do local t=bodies[i].limits[axis]
  if t[1]==-360 and t[2]==360 then add('\t$jointconstrain "'..P.BODIES[i+1]..'" '..axis..' free -360 360 '..num(t[3]*5))
  else add('\t$jointconstrain "'..P.BODIES[i+1]..'" '..axis..' limit '..num(t[1])..' '..num(t[2])..' '..num(t[3]*5)) end
 end end
 if c.mode=='custom' then for _,p in ipairs(list) do add('\t$jointcollide "'..P.BODIES[p[1]+1]..'" "'..P.BODIES[p[2]+1]..'"') end end
 add('}')
 add('$surfaceprop "'..(model.surfaceprop or 'flesh')..'"')
 for i=0,17 do if bodies[i].surfaceprop~=(model.surfaceprop or 'flesh') then add('$jointsurfaceprop "'..P.BODIES[i+1]..'" "'..bodies[i].surfaceprop..'"') end end
 if not uniform then local parts={} for i=0,17 do if bodies[i].drag then parts[#parts+1]=P.BODIES[i+1]..' '..num(bodies[i].drag) end end
  if #parts>0 then add('// Not expressible in QC: per-part air drag ('..table.concat(parts,', ')..')') end end
 return table.concat(out,'\n')..'\n'
end

-- .phy text (an installed model's KeyValues) for templates (§11.1).
function P.ParsePhyText(text)
 if type(text)~='string' or text=='' then return nil,'template.missing' end
 local tokens=P.Tokenize(text) local phy={solids={},constraints={},rules=nil,animated=nil,editparams={}}
 local i=1
 while i<=#tokens do
  local name=tokens[i].text:lower()
  if tokens[i+1] and tokens[i+1].text=='{' then
   -- One block of "key" "value" pairs; a nested block (none in .phy text) is skipped whole.
   local block={} local j=i+2
   while tokens[j] and tokens[j].text~='}' do
    if tokens[j].text=='{' then local depth=0
     repeat if tokens[j].text=='{' then depth=depth+1 elseif tokens[j].text=='}' then depth=depth-1 end j=j+1 until depth==0 or not tokens[j]
    elseif tokens[j+1] and tokens[j+1].text~='{' and tokens[j+1].text~='}' then
     local key,value=tokens[j].text:lower(),tokens[j+1].text
     if name=='collisionrules' and key=='collisionpair' then local a,b=value:match('^%s*(%d+)%s*,%s*(%d+)') if a then block.pairs=block.pairs or {} block.pairs[#block.pairs+1]={tonumber(a),tonumber(b)} end
     else block[key]=value end
     j=j+2
    else j=j+1 end
   end
   if name=='solid' then local index=tonumber(block.index) if index then block.index=index phy.solids[#phy.solids+1]=block end
   elseif name=='ragdollconstraint' then phy.constraints[#phy.constraints+1]=block
   elseif name=='collisionrules' then phy.rules={selfcollisions=block.selfcollisions~=nil,pairs=block.pairs or {}}
   elseif name=='animatedfriction' then phy.animated=block
   elseif name=='editparams' then phy.editparams=block end
   i=j+1
  else i=i+1 end
 end
 if #phy.solids==0 then return nil,'template.missing' end
 table.sort(phy.solids,function(a,b) return a.index<b.index end)
 return phy
end
-- The text section of a binary .phy beside an installed .mdl, when KeyValues lack it.
function P.ReadPhyFile(mdlPath)
 if type(mdlPath)~='string' or not file or not file.Open then return nil end
 local f=file.Open(mdlPath:gsub('%.mdl$','.phy'),'rb','GAME') if not f then return nil end
 local ok,text=pcall(function()
  local size=f:ReadLong() f:ReadLong() local solids=f:ReadLong() f:ReadLong()
  if size~=16 or solids<1 or solids>64 then return nil end
  for _=1,solids do local n=f:ReadLong() if not n or n<0 or n>4194304 then return nil end f:Skip(n) end
  local rest=f:Read(f:Size()-f:Tell()) or ''
  return rest:match('^([^%z]*)')
 end)
 f:Close()
 return ok and text~='' and text or nil
end
-- Maps a template's solids to the carrier bodies; Spine1 and Spine4 may share one solid (HL2's Spine2).
local ALIASES={[0]={'pelvis'},{'spine1','spine','spine2'},{'spine4','spine3','spine2'},{'head1','head'}}
local function short(name) return (tostring(name or ''):lower():gsub('^valvebiped%.bip01_','')) end
function P.MapTemplate(phy)
 local byName={} for _,s in ipairs(phy.solids) do byName[short(s.name)]=s end
 local map={}
 for i=0,17 do local list=ALIASES[i] or {short(P.BODIES[i+1])}
  for n,candidate in ipairs(list) do if byName[candidate] then map[i]={solid=byName[candidate],kind=n==1 and 'exact' or 'alias'} break end end
 end
 if map[1] and map[2] and map[1].solid==map[2].solid then map[1].kind='split' map[2].kind='split' end
 return map
end
-- Copies a template's physics values into the draft (a copy is returned). opts={limits,friction,body,mass,total,collisions,animated}.
function P.FromTemplate(phy,opts,draft)
 opts=opts or {} local d=copy(draft or P.NewDraft()) local map=P.MapTemplate(phy)
 local bodies,model=P.Effective(d) model=copy(model)
 local constraintOf={} for _,c in ipairs(phy.constraints) do local child=tonumber(c.child) if child then constraintOf[child]=c end end
 local report={rows={},bodies=0,joints=0,solids=#phy.solids,kept={},split=map[1] and map[1].kind=='split'}
 local massTotal,mappedShares=0,0
 for i=0,17 do if map[i] then local s=map[i].solid if map[i].kind~='split' or i==1 then massTotal=massTotal+(tonumber(s.mass) or 0) end mappedShares=mappedShares+P.DEFAULTS[i].massBias end end
 for i=0,17 do local m=map[i] local b=bodies[i]
  if not m then report.kept[#report.kept+1]=i report.rows[#report.rows+1]={body=i,kind='kept'}
  else
   report.bodies=report.bodies+1 report.rows[#report.rows+1]={body=i,kind=m.kind,solid=m.solid.name}
   local s=m.solid local c=constraintOf[s.index]
   if i>0 and c and (opts.limits~=false or opts.friction~=false) then report.joints=report.joints+1
    for _,axis in ipairs(P.AXES) do local lo,hi,f=tonumber(c[axis..'min']),tonumber(c[axis..'max']),tonumber(c[axis..'friction'])
     local t=b.limits[axis]
     if opts.limits~=false and lo and hi then
      if m.kind=='split' and not (lo==-360 and hi==360) then lo,hi=q(lo/2,1),q(hi/2,1) end
      if not (lo==-360 and hi==360) then lo,hi=clamp(q(lo,1),-180,180),clamp(q(hi,1),-180,180) if lo>hi then lo,hi=hi,lo end end
      t={lo,hi,t[3]}
     end
     if opts.friction~=false and f then t={t[1],t[2],clamp(q(f,3),0,100)} end
     b.limits[axis]=canonicalAxis(t)
    end
   end
   if opts.body~=false then
    for key,field in pairs({damping='damping',rotdamping='rotdamping',inertia='inertia'}) do local v=tonumber(s[key]) local r=P.LIMITS[field] if v then b[field]=q(clamp(v,r[1],r[2]),3) end end
    if s.drag and tonumber(s.drag) and tonumber(s.drag)>=0 then b.drag=q(clamp(tonumber(s.drag),0,100),3) end
    if validSurface(s.surfaceprop) and (not opts.surfaceKnown or opts.surfaceKnown(s.surfaceprop)) then b.surfaceprop=s.surfaceprop end
   end
   if opts.mass~=false and massTotal>0 then local kg=tonumber(s.mass) or 0
    -- Mapped bodies share what the default gives them, so unmapped ones keep their default weight.
    if m.kind=='split' then kg=kg*(i==1 and 8 or 9)/17 end
    b.massBias=q(clamp(kg*mappedShares/massTotal,.01,100),3)
   end
  end
 end
 if opts.total then local total=tonumber(phy.editparams.totalmass) or massTotal if total and total>0 then d.mass=q(clamp(total,1,500),1) model.automass=false end end
 if opts.collisions and phy.rules then
  if phy.rules.selfcollisions then model.collisions={mode='none',pairs={}}
  else local solidTo={} for i=0,17 do if map[i] then local idx=map[i].solid.index solidTo[idx]=solidTo[idx] or {} table.insert(solidTo[idx],i) end end
   local list,dropped={},0
   for _,p in ipairs(phy.rules.pairs) do local xs,ys=solidTo[p[1]],solidTo[p[2]]
    if xs and ys then for _,x in ipairs(xs) do for _,y in ipairs(ys) do if x~=y and not P.Adjacent(x,y) then list[#list+1]={x,y} else dropped=dropped+1 end end end else dropped=dropped+1 end
   end
   model.collisions={mode='custom',pairs=P.NormalizePairs(list)} report.droppedPairs=dropped
  end
 end
 if opts.animated and phy.animated then local a=phy.animated
  local mn,mx=math.floor(tonumber(a.animfrictionmin) or 0),math.floor(tonumber(a.animfrictionmax) or 0)
  if mn<=mx then model.animatedFriction={min=clamp(mn,0,1000),max=clamp(mx,0,1000),timeIn=q(clamp(tonumber(a.animfrictiontimein) or 0,0,10),2),timeOut=q(clamp(tonumber(a.animfrictiontimeout) or 0,0,10),2),timeHold=q(clamp(tonumber(a.animfrictiontimehold) or 0,0,10),2)} end
 end
 d.feel.preset='template'
 P.AdoptEffective(d,bodies,model)
 return d,report
end
return P
