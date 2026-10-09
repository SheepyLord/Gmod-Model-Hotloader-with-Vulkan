-- Bone assignment window: the rules, without any panel (shared; the server checks
-- saves with them, tests run them offline). Bone indices are 0-based as in the
-- native data; state.bones[i+1] is bone i. A slot is a carrier body part keyed by
-- its ValveBiped name; its value is a bone index or -1 (created automatically).
-- native/humanoid_slots.hpp holds the same catalogue (tests/fixtures/bonemap/slots.json).
mmdhl.boneMapper=mmdhl.boneMapper or {}
local BM=mmdhl.boneMapper
local VB='ValveBiped.Bip01_'
-- Figure positions in a 380x500 design space, left-side values (the right mirrors x).
local slots={
 {'Pelvis','hips','torso','pelvis','',0,true,true,false,false,{},'','下半身','lower body',{190,252}},
 {'Spine1','spine','torso','spine','',0,true,true,false,false,{},'','上半身','upper body',{190,214}},
 {'Spine2','middle_spine','torso','spine','',0,false,false,false,false,{'Spine1'},'','上半身2','upper body2',{190,178}},
 {'Spine4','chest','torso','spine','',0,false,true,true,false,{'Spine2','Spine1'},'','上半身3','upper body3',{190,140}},
 {'Neck1','neck','torso','neck','',0,false,false,true,false,{'Spine4','Spine1'},'','首','neck',{190,98}},
 {'Head1','head','torso','head','',0,true,true,false,false,{'Neck1','Spine4','Spine1'},'','頭','head',{190,52}},
 {'Eye_L','left_eye','eyes','eye','L',0,false,false,false,true,{'Head1'},'Eye_R','左目','eye_L',{204,46}},
 {'Eye_R','right_eye','eyes','eye','R',0,false,false,false,true,{'Head1'},'Eye_L','右目','eye_R',{204,46}},
}
local function side(prefix,id,group,s,j,e)
 local partner=s=='L' and 'R' or 'L'
 local pid=s=='L' and 'left' or 'right'
 local list={
  {prefix..'Clavicle',pid..'_shoulder',pid..'_arm','clavicle',s,0,false,true,true,false,{'Spine4','Spine1'},partner..'_Clavicle',j..'肩','shoulder_'..e,{220,118}},
  {prefix..'UpperArm',pid..'_upper_arm',pid..'_arm','upperarm',s,0,true,true,false,false,{prefix..'Clavicle','Spine4','Spine1'},partner..'_UpperArm',j..'腕','arm_'..e,{252,126}},
  {prefix..'Forearm',pid..'_forearm',pid..'_arm','forearm',s,0,true,true,false,false,{prefix..'UpperArm'},partner..'_Forearm',j..'ひじ','elbow_'..e,{274,200}},
  {prefix..'Hand',pid..'_hand',pid..'_arm','hand',s,0,true,true,false,false,{prefix..'Forearm'},partner..'_Hand',j..'手首','wrist_'..e,{290,270}},
 }
 return list
end
local function leg(prefix,s,j,e)
 local partner=s=='L' and 'R' or 'L'
 local pid=s=='L' and 'left' or 'right'
 return {
  {prefix..'Thigh',pid..'_thigh',pid..'_leg','thigh',s,0,true,true,false,false,{'Pelvis'},partner..'_Thigh',j..'足','leg_'..e,{214,268}},
  {prefix..'Calf',pid..'_lower_leg',pid..'_leg','calf',s,0,true,true,false,false,{prefix..'Thigh'},partner..'_Calf',j..'ひざ','knee_'..e,{220,372}},
  {prefix..'Foot',pid..'_foot',pid..'_leg','foot',s,0,true,true,false,false,{prefix..'Calf'},partner..'_Foot',j..'足首','ankle_'..e,{224,462}},
  {prefix..'Toe0',pid..'_toes',pid..'_leg','toe',s,0,false,false,true,false,{prefix..'Foot'},partner..'_Toe0',j..'つま先','toe_'..e,{234,486}},
 }
end
-- Finger joints in the 182x150 hand box of the Hands view (left hand; the right mirrors x).
local hand={thumb={{128,110},{146,88},{160,68}},index={{114,76},{118,50},{121,26}},middle={{95,72},{95,44},{95,18}},ring={{76,76},{72,50},{69,28}},little={{58,86},{50,66},{44,48}}}
local function fingers(s,j,e)
 local partner=s=='L' and 'R' or 'L'
 local pid=s=='L' and 'left' or 'right'
 local list={}
 local names={{'thumb','親指',{'０','１','２'},'thumb',{0,1,2}},{'index','人指',{'１','２','３'},'fore',{1,2,3}},{'middle','中指',{'１','２','３'},'middle',{1,2,3}},{'ring','薬指',{'１','２','３'},'third',{1,2,3}},{'little','小指',{'１','２','３'},'little',{1,2,3}}}
 for f,info in ipairs(names) do
  for k=1,3 do
   local key=s..'_Finger'..(f-1)..(k>1 and tostring(k-1) or '')
   local anchor=k==1 and s..'_Hand' or s..'_Finger'..(f-1)..(k>2 and tostring(k-2) or '')
   list[#list+1]={key,pid..'_'..info[1]..'_'..k,pid..'_fingers',info[1],s,k,false,false,false,false,{anchor},partner..key:sub(2),j..info[2]..info[3][k],info[4]..info[5][k]..'_'..e,nil,hand[info[1]][k]}
  end
 end
 return list
end
for _,list in ipairs({side('L_','left','left_arm','L','左','L'),side('R_','right','right_arm','R','右','R'),leg('L_','L','左','L'),leg('R_','R','右','R'),fingers('L','左','L'),fingers('R','右','R')}) do
 for _,row in ipairs(list) do slots[#slots+1]=row end
end
BM.Slots={} BM.SlotByKey={}
local function full(k) if k=='' or k:sub(1,4)=='Eye_' then return k end return VB..k end
for i,row in ipairs(slots) do
 local anchors={} for _,a in ipairs(row[11]) do anchors[#anchors+1]=full(a) end
 local pos=row[15] and {row[15][1],row[15][2]}
 if pos and row[5]=='R' then pos[1]=380-pos[1] end
 local h=row[16] and {row[16][1],row[16][2]}
 if h and row[5]=='R' then h[1]=182-h[1] end
 local s={index=i,key=full(row[1]),id=row[2],group=row[3],family=row[4],side=row[5],segment=row[6],required=row[7],physical=row[8],recommended=row[9],convertOnly=row[10],anchors=anchors,partner=full(row[12]),mmdJp=row[13],mmdEn=row[14],pos=pos,hand=h}
 if s.key=='Eye_R' then s.pos={176,46} end
 BM.Slots[i]=s BM.SlotByKey[s.key]=s
end
BM.GroupOrder={'torso','left_arm','right_arm','left_leg','right_leg','fingers','eyes'}
BM.Kinds={'hair','skirt','chest','tail','accessory'}
BM.MaxChains=64 BM.MaxJoints=256
-- Families a bone's meaning can stand for (native classifyBones).
local chainKind={hair=true,skirt=true,chest=true,tail=true,accessory=true}
local function groupOf(s) if s.group=='left_fingers' or s.group=='right_fingers' then return 'fingers' end return s.group end
BM.SummaryGroup=groupOf
function BM.Mapped(key) local s=BM.SlotByKey[key] return s~=nil and not s.convertOnly end

-- ---- state ----
local function copy(t) if type(t)~='table' then return t end local c={} for k,v in pairs(t) do c[k]=copy(v) end return c end
BM.Copy=copy
local function flagSet(list) local set={} for _,f in ipairs(list or {}) do set[f]=true end return set end
-- Bones in parent-first order, depths and an Euler tour for O(1) ancestry tests.
local function index(state)
 local bones=state.bones local n=#bones
 for i=1,n do bones[i].children={} end
 local roots={}
 for i=1,n do local b=bones[i] local p=b.parent
  if p and p>=0 and p<n and p~=i-1 then table.insert(bones[p+1].children,i-1) else b.parent=-1 roots[#roots+1]=i-1 end
 end
 local order,clock,seen={},0,{}
 for _,r in ipairs(roots) do
  local stack={{r,0}} bones[r+1].depth=1 bones[r+1].tin=clock clock=clock+1 order[#order+1]=r seen[r]=true
  while #stack>0 do
   local top=stack[#stack] local b=bones[top[1]+1] top[2]=top[2]+1
   local c=b.children[top[2]]
   if c==nil then b.tout=clock table.remove(stack)
   elseif not seen[c] then seen[c]=true bones[c+1].depth=b.depth+1 bones[c+1].tin=clock clock=clock+1 order[#order+1]=c stack[#stack+1]={c,0} end
  end
 end
 -- A bone in a parent cycle never reaches a root: it stands alone.
 for i=1,n do if not seen[i-1] then local b=bones[i] b.parent=-1 b.depth=1 b.tin=clock b.tout=clock+1 clock=clock+1 order[#order+1]=i-1 end end
 state.order=order
 for k=#order,1,-1 do local b=bones[order[k]+1] b.subtree=(b.subtree or 0)+b.weighted if b.parent>=0 then local p=bones[b.parent+1] p.subtree=(p.subtree or 0)+b.subtree end end
 local depth=0 for i=1,n do if bones[i].depth>depth then depth=bones[i].depth end end
 state.maxDepth=depth
end
-- a strictly below b
function BM.Below(state,a,b)
 if not a or not b or a<0 or b<0 or a==b then return false end
 local x,y=state.bones[a+1],state.bones[b+1]
 return x~=nil and y~=nil and y.tin<x.tin and x.tin<y.tout
end
local below=BM.Below
local function auto(slot,value,confidence,method)
 return {bone=value or -1,confidence=confidence or 0,method=method or ''}
end
-- A slot's aliases from the fitter (GetBoneMapProposal), without the slot's own bone: a
-- pinned Spine2/Spine4 outside its band comes back as an alias of a synthesized pivot,
-- and the window shows it as the slot's bone, never as moving with itself.
function BM.SlotAliases(aliases,value)
 local list={} for _,a in ipairs(istable(aliases) and aliases or {}) do if a~=value then list[#list+1]=a end end
 return list
end
-- A fitter issue (GetBoneMapProposal) as a window issue: band, range and chest have
-- their own texts; everything else shows the native sentence.
function BM.NativeIssue(i)
 local code=(i.code=='band' or i.code=='range' or i.code=='chest') and i.code or 'native'
 return {code=code,severity=i.severity or 'warning',slot=i.slot or '',args={message=tostring(i.text or '')}}
end
local function originFor(g)
 if g.bone<0 then return 'created' end
 if (g.confidence or 0)<.85 then return 'guess' end
 return 'auto'
end
-- data (convert): {probe=status.probe, source=, filename=}
-- data (fit): {asset=, name=, skeleton=, auto= (InspectBoneMap auto), proposal= (fitter's choice
-- without pins), current= (with the saved pins), pins=, hasCollision=, reason=}
function BM.NewState(mode,data)
 local skeleton=mode=='convert' and (data.probe or {}).skeleton or data.skeleton
 local s={mode=mode,reason=data.reason or (mode=='convert' and 'import' or 'edit'),source=data.source,sha=mode=='convert' and (data.probe or {}).sourceSha256 or nil,asset=data.asset,
  name=data.name or data.filename or '',filename=data.filename or '',format=mode=='convert' and (data.probe or {}).format or 'pmx',turn=0,tab='body',view='picture',
  history={},cursor=0,dirty=false,issues={},slots={},auto={},eyes={L={bone=-1,origin='created'},R={bone=-1,origin='created'}},groups={},chains={},mapVersion=0,chainsVersion=-1,
  aliases={},nativeIssues={},savedPins=data.pins or {},hasCollision=data.hasCollision,torso=data.torso,probe=data.probe,bones={},byName={}}
 skeleton=skeleton or {bones={},points={}}
 for i,b in ipairs(skeleton.bones or {}) do
  local p=b.position or {0,0,0}
  s.bones[i]={name=tostring(b.name or ('bone_'..(i-1))),english=b.english,parent=tonumber(b.parent) or -1,pos={tonumber(p[1]) or 0,tonumber(p[2]) or 0,tonumber(p[3]) or 0},
   weighted=tonumber(b.weighted) or 0,flags=flagSet(b.flags),meaning=b.meaning or '',side=b.side or '',segment=tonumber(b.segment) or 0,mirror=tonumber(b.mirror) or -1,nameIssue=b.nameIssue or ''}
  if s.byName[s.bones[i].name]==nil then s.byName[s.bones[i].name]=i-1 end
 end
 index(s)
 s.points=skeleton.points or {}
 s.signature=skeleton.signature
 local lo,hi
 for i=2,#s.points,4 do local y=s.points[i] if not lo or y<lo then lo=y end if not hi or y>hi then hi=y end end
 if not lo then for _,b in ipairs(s.bones) do local y=b.pos[2] if not lo or y<lo then lo=y end if not hi or y>hi then hi=y end end end
 s.floor=lo or 0 s.height=tonumber(skeleton.height) or ((hi or 1)-(lo or 0))
 if s.height<=0 then s.height=1 end
 if mode=='convert' then
  local a=(data.probe or {}).auto or {}
  s.humanoid=a.humanoid~=false
  for _,slot in ipairs(BM.Slots) do
   if not slot.convertOnly then local g=(a.slots or {})[slot.key] or {} s.auto[slot.key]=auto(slot,tonumber(g.bone),tonumber(g.confidence),g.method) end
  end
  for _,k in ipairs({'L','R'}) do local g=((a.eyes or {})[k]) or {} s.auto['Eye_'..k]=auto(nil,tonumber(g.bone),tonumber(g.confidence),g.method) end
  for key,g in pairs(s.auto) do
   if key:sub(1,4)=='Eye_' then s.eyes[key:sub(5)]={bone=g.bone,origin=originFor(g)}
   else s.slots[key]={bone=g.bone,origin=originFor(g),confidence=g.confidence} end
  end
 else
  -- Fit mode: the fitter's own choice is automatic; the engine only suggests bones
  -- for required parts it left empty.
  local proposal,current,inspect=data.proposal or {},data.current or data.proposal or {},data.auto or {}
  local function values(p) local v={} for _,b in ipairs(p.bones or {}) do if BM.Mapped(b.name) then v[b.name]=b end end return v end
  local base,now=values(proposal),values(current)
  s.humanoid=true
  for _,slot in ipairs(BM.Slots) do if not slot.convertOnly then
   local b=base[slot.key] or {} local value=tonumber(b.mmd) or -1
   local g=auto(slot,value,value>=0 and 1 or 0,b.provenance or '')
   local suggestion=((inspect.slots or {})[slot.key]) or {}
   g.suggested=-1
   if value<0 and slot.required and (tonumber(suggestion.bone) or -1)>=0 and (tonumber(suggestion.confidence) or 0)>=.5 then g.suggested=suggestion.bone g.suggestedConfidence=suggestion.confidence end
   s.auto[slot.key]=g
   local c=now[slot.key] or b local cv=tonumber(c.mmd) or -1
   -- A saved torso pin outside its band moves with a synthesized pivot (the fitter lists it
   -- as an alias): it stays the player's choice, so saving again keeps it.
   local pin,aliases=tonumber(s.savedPins[slot.key]),c.aliases or {}
   for _,a in ipairs(aliases) do if pin and pin>=0 and a==pin then cv=pin end end
   local origin=(b.provenance=='conversion' and 'conversion') or 'auto'
   if s.savedPins[slot.key]~=nil then origin='fit_saved' end
   if cv<0 and slot.required then
    if g.suggested>=0 then s.slots[slot.key]={bone=g.suggested,origin='guess',confidence=g.suggestedConfidence or .5}
    else s.slots[slot.key]={bone=-1,origin=origin,confidence=0} end
   else s.slots[slot.key]={bone=cv,origin=cv<0 and (s.savedPins[slot.key]~=nil and 'fit_saved' or 'created') or origin,confidence=1} end
   s.aliases[slot.key]=BM.SlotAliases(aliases,cv)
  end end
  local used={} for _,v in pairs(s.slots) do if v.bone>=0 then used[v.bone]=(used[v.bone] or 0)+1 end end
  -- A suggestion never takes a bone another part already uses.
  for key,v in pairs(s.slots) do if v.origin=='guess' and (used[v.bone] or 0)>1 then v.bone=-1 v.origin='auto' end end
 end
 BM.Snapshot(s,'open')
 return s
end

-- ---- history ----
function BM.Snapshot(state,label)
 for i=#state.history,state.cursor+1,-1 do table.remove(state.history,i) end
 state.history[#state.history+1]={label=label,slots=copy(state.slots),eyes=copy(state.eyes),groups=copy(state.groups),chains=copy(state.chains),turn=state.turn}
 while #state.history>51 do table.remove(state.history,1) end
 state.cursor=#state.history
end
local function restore(state,snap)
 state.slots=copy(snap.slots) state.eyes=copy(snap.eyes) state.groups=copy(snap.groups) state.chains=copy(snap.chains) state.turn=snap.turn
 state.mapVersion=state.mapVersion+1 state.dirty=state.cursor>1
end
function BM.CanUndo(state) return state.cursor>1 end
function BM.CanRedo(state) return state.cursor<#state.history end
function BM.Undo(state) if state.cursor<=1 then return false end state.cursor=state.cursor-1 restore(state,state.history[state.cursor]) return true end
function BM.Redo(state) if state.cursor>=#state.history then return false end state.cursor=state.cursor+1 restore(state,state.history[state.cursor]) return true end
-- Every change pushes one snapshot after it is made.
local function changed(state,label) state.dirty=true state.mapVersion=state.mapVersion+1 BM.Snapshot(state,label) end
BM.Changed=changed

-- ---- values ----
function BM.Value(state,key)
 if key=='Eye_L' or key=='Eye_R' then return state.eyes[key:sub(5)].bone end
 local v=state.slots[key] return v and v.bone or -1
end
function BM.Values(state,withEyes)
 local v={} for key,s in pairs(state.slots) do v[key]=s.bone end
 if withEyes and state.mode=='convert' then v.Eye_L=state.eyes.L.bone v.Eye_R=state.eyes.R.bone end
 return v
end
function BM.SlotUsing(state,bone)
 if bone==nil or bone<0 then return nil end
 for _,s in ipairs(BM.Slots) do if not (s.convertOnly and state.mode~='convert') and BM.Value(state,s.key)==bone then return s.key end end
end
local function set(state,key,bone,origin,confidence)
 if key=='Eye_L' or key=='Eye_R' then state.eyes[key:sub(5)]={bone=bone,origin=origin} return end
 state.slots[key]={bone=bone,origin=origin,confidence=confidence or 1}
end
-- Assigns bone (or -1) to a part. A bone another part uses moves here; that part
-- becomes empty. Returns the part the bone was taken from.
function BM.Assign(state,key,bone,origin)
 origin=origin or 'user' local moved
 if bone>=0 then
  for _,s in ipairs(BM.Slots) do if s.key~=key and BM.Value(state,s.key)==bone and not (s.convertOnly and state.mode~='convert') then moved=s.key set(state,s.key,-1,'created',0) end end
 end
 set(state,key,bone,bone<0 and 'created' or origin,1)
 changed(state,key)
 return moved
end
function BM.Clear(state,key) return BM.Assign(state,key,-1,'created') end
function BM.UseAuto(state,key)
 local g=state.auto[key] if not g then return end
 local origin=originFor(g)
 if state.mode=='fit' and g.bone<0 and (g.suggested or -1)>=0 then BM.Assign(state,key,g.suggested,'guess') return end
 BM.Assign(state,key,g.bone,origin)
 if key:sub(1,4)~='Eye_' then state.slots[key].confidence=g.confidence end
end
function BM.AcceptGuess(state,key) local v=state.slots[key] if v and v.origin=='guess' then v.origin='user' v.confidence=1 changed(state,key) end end
function BM.SetEye(state,which,bone,origin) BM.Assign(state,'Eye_'..which,bone,origin) end

-- ---- geometry helpers ----
local function midline(state)
 local pelvis=BM.Value(state,VB..'Pelvis')
 if pelvis>=0 then return state.bones[pelvis+1].pos[1] end
 local xs={} for _,b in ipairs(state.bones) do if b.weighted>0 then xs[#xs+1]=b.pos[1] end end
 table.sort(xs) if #xs==0 then return 0 end
 if #xs%2==1 then return xs[(#xs+1)/2] end return (xs[#xs/2]+xs[#xs/2+1])/2
end
-- Lateral position in the current frame: positive is the character's left.
function BM.Lateral(state,bone)
 local b=state.bones[bone+1] if not b then return 0 end
 local x=b.pos[1]-midline(state) if state.turn==180 then x=-x end return x
end
local function forward(state,bone) local z=state.bones[bone+1].pos[3] if state.turn==180 then z=-z end return z end
local function distance(a,b) local x,y,z=a.pos[1]-b.pos[1],a.pos[2]-b.pos[2],a.pos[3]-b.pos[3] return math.sqrt(x*x+y*y+z*z) end
function BM.AnchorOf(state,slot)
 for _,a in ipairs(slot.anchors) do local b=BM.Value(state,a) if b>=0 then return b,a end end
 return -1
end

-- ---- validation ----
local SoftOrder={middle_spine=true,chest=true,neck=true}
local LongPairs={'UpperArm','Forearm','Hand','Thigh','Calf','Foot'}
local function issue(list,code,severity,slot,args,extra)
 local i={code=code,severity=severity,slot=slot,args=args or {}} for k,v in pairs(extra or {}) do i[k]=v end list[#list+1]=i return i
end
local function name(state,b) local bone=state.bones[(b or -1)+1] return bone and bone.name or '' end
-- Bones a swinging part brings: its subtree without unweighted leaves (their parent's tail).
function BM.ChainBones(state,root)
 local list={} local stack={root}
 while #stack>0 do local b=table.remove(stack) local bone=state.bones[b+1]
  if bone then list[#list+1]=b for _,c in ipairs(bone.children) do stack[#stack+1]=c end end
 end
 return list
end
function BM.JointCount(state,root)
 local n=0 for _,b in ipairs(BM.ChainBones(state,root)) do local bone=state.bones[b+1] if not (#bone.children==0 and bone.weighted==0) then n=n+1 end end return n
end
-- The swinging parts that will be imported: enabled chains of enabled groups.
function BM.ImportedChains(state)
 local list={} if state.mode~='convert' then return list end
 local on={} for _,g in ipairs(state.groups) do on[g.kind]=g.enabled end
 for _,c in ipairs(state.chains) do if c.enabled and on[c.kind] then list[#list+1]=c end end
 return list
end
-- Problems of the current assignment: {code, severity, slot, args, ...}, errors first.
-- The rules marked structural in the spec match native checkBoneMap (rules.json).
function BM.Validate(state,options)
 options=options or {}
 local out={} local H=state.height local values=BM.Values(state,true) local flat=state.maxDepth<=3
 local owner={}
 local spine=values[VB..'Spine1'] or -1
 for _,s in ipairs(BM.Slots) do
  local b=values[s.key]
  if b~=nil and not (s.convertOnly and state.mode~='convert') then
   if b<0 then
    if s.required then issue(out,'required','error',s.key,{part=s.key}) end
   elseif owner[b] then issue(out,'duplicate','error',s.key,{bone=name(state,b),first=owner[b],second=s.key},{bone=b,other=owner[b]})
   else
    owner[b]=s.key
    local anchor,anchorKey=BM.AnchorOf(state,s)
    if anchor>=0 and not below(state,b,anchor) then
     issue(out,'order',(flat or SoftOrder[s.id]) and 'warning' or 'error',s.key,{part=s.key,parent=anchorKey,bone=name(state,b),parentBone=name(state,anchor)},{bone=b,other=anchor})
    end
    local bone=state.bones[b+1]
    if bone.flags.secondary then issue(out,'physics',s.required and 'error' or 'warning',s.key,{bone=bone.name,part=s.key},{bone=b}) end
    if s.family=='thigh' and spine>=0 and below(state,b,spine) then issue(out,'leg_on_spine','error',s.key,{part=s.key,bone=bone.name},{bone=b}) end
   end
  end
 end
 -- Swinging parts never hold a body part.
 local chains=BM.ImportedChains(state) local joints=0
 for _,c in ipairs(chains) do
  joints=joints+BM.JointCount(state,c.root)
  local found
  for b,key in pairs(owner) do if (b==c.root or below(state,b,c.root)) and (not found or b<found) then found=b end end
  if found then issue(out,'jiggle_body','error',owner[found],{chain=name(state,c.root),part=owner[found],bone=name(state,found)},{chain=c.root,bone=found}) end
 end
 if #chains>BM.MaxChains or joints>BM.MaxJoints then issue(out,'jiggle_too_many','error','',{count=joints,max=BM.MaxJoints}) end
 if options.structural then return out end
 -- Sides: most long bones reversed is one problem with one fix.
 local reversed=0
 for _,part in ipairs(LongPairs) do local l,r=values[VB..'L_'..part],values[VB..'R_'..part]
  if l and r and l>=0 and r>=0 and BM.Lateral(state,l)<BM.Lateral(state,r) then reversed=reversed+1 end end
 if reversed>=4 then issue(out,'sides_swapped','error','',{})
 else
  for _,s in ipairs(BM.Slots) do local b=values[s.key]
   if b and b>=0 and s.side~='' and not (s.convertOnly and state.mode~='convert') then
    local lat=BM.Lateral(state,b) local wrong=(s.side=='L' and lat<-.03*H) or (s.side=='R' and lat>.03*H)
    if wrong then
     local long=s.family=='upperarm' or s.family=='forearm' or s.family=='hand' or s.family=='thigh' or s.family=='calf' or s.family=='foot'
     issue(out,'side',long and 'error' or 'warning',s.key,{bone=name(state,b),side=lat>0 and 'left' or 'right',part=s.key},{bone=b,mirror=state.bones[b+1].mirror})
    end
   end
  end
 end
 if state.mode=='convert' then
  local lt,rt,lf,rf=values[VB..'L_Toe0'],values[VB..'R_Toe0'],values[VB..'L_Foot'],values[VB..'R_Foot']
  if lt>=0 and rt>=0 and lf>=0 and rf>=0 then
   local mean=((forward(state,lt)-forward(state,lf))+(forward(state,rt)-forward(state,rf)))/2
   if mean<-.01 then issue(out,'facing','warning','',{}) end
  end
 end
 local function y(key) local b=values[VB..key] return b and b>=0 and state.bones[b+1].pos[2] or nil end
 for _,pair in ipairs({{'Head1','Neck1'},{'Neck1','Spine1'},{'Spine4','Spine1'}}) do
  local a,b=y(pair[1]),y(pair[2]) if a and b and a<b-.01*H then issue(out,'height','warning',VB..pair[1],{part=VB..pair[1],other=VB..pair[2]}) end
 end
 for _,s in ipairs({'L_','R_'}) do
  for _,pair in ipairs({{'Calf','Thigh'},{'Foot','Calf'}}) do local a,b=y(s..pair[1]),y(s..pair[2]) if a and b and a>b+.01*H then issue(out,'height','warning',VB..s..pair[1],{part=VB..s..pair[1],other=VB..s..pair[2]}) end end
  local function len(a,b) local x,z=values[VB..s..a],values[VB..s..b] if x and z and x>=0 and z>=0 then return distance(state.bones[x+1],state.bones[z+1]) end end
  local arm,fore=len('UpperArm','Forearm'),len('Forearm','Hand')
  if arm and fore and arm>1e-6 and (fore/arm<.45 or fore/arm>2.2) then issue(out,'length','warning',VB..s..'Forearm',{part=VB..s..'Forearm',other=VB..s..'UpperArm'}) end
  local thigh,calf=len('Thigh','Calf'),len('Calf','Foot')
  if thigh and calf and thigh>1e-6 and (calf/thigh<.5 or calf/thigh>2) then issue(out,'length','warning',VB..s..'Calf',{part=VB..s..'Calf',other=VB..s..'Thigh'}) end
 end
 for _,s in ipairs(BM.Slots) do local b=values[s.key]
  if b and b>=0 and not (s.convertOnly and state.mode~='convert') then local bone=state.bones[b+1]
   if s.physical and BM.EffectiveWeight(state,s.key)==0 then issue(out,'unweighted','warning',s.key,{bone=bone.name,part=s.key},{bone=b}) end
   if bone.flags.twist and s.group~='left_fingers' and s.group~='right_fingers' then issue(out,'twist','warning',s.key,{bone=bone.name,part=s.key},{bone=b,parentBone=bone.parent}) end
  end
 end
 local recommended={}
 for _,s in ipairs(BM.Slots) do if s.recommended and values[s.key]==-1 then recommended[#recommended+1]=s.key end end
 if #recommended>0 then issue(out,'recommended','info','',{parts=recommended}) end
 local partial=false
 for _,s in ipairs(BM.Slots) do if s.segment==1 then local n=0
  for k=0,2 do local key=k==0 and s.key or s.key..tostring(k) if values[key] and values[key]>=0 then n=n+1 end end
  if n==1 or n==2 then partial=true end end end
 if partial then issue(out,'finger_partial','info','',{}) end
 if state.mode=='fit' and istable(state.torso) then
  for _,r in ipairs(state.torso.repairs or {}) do
   if type(r)=='string' then r={code='note',text=r} end
   issue(out,'torso','info','',{text=r.text,code=r.code},{repair=r})
  end
 end
 for _,n in ipairs(state.nativeIssues or {}) do out[#out+1]=n end
 local rank={error=1,warning=2,info=3}
 for i,v in ipairs(out) do v.order=i end
 table.sort(out,function(a,b) if a.severity~=b.severity then return rank[a.severity]<rank[b.severity] end local sa,sb=BM.SlotByKey[a.slot],BM.SlotByKey[b.slot] local ia,ib=sa and sa.index or 0,sb and sb.index or 0 if ia~=ib then return ia<ib end return a.order<b.order end)
 state.issues=out
 return out
end
function BM.IssuesOf(state,key) local list={} for _,i in ipairs(state.issues) do if i.slot==key then list[#list+1]=i end end return list end
-- ok, check, missing, created or disabled.
function BM.StatusOf(state,key)
 local s=BM.SlotByKey[key] if not s then return 'disabled' end
 local function need(k) return BM.Value(state,k)<0 end
 if s.family=='eye' and need(VB..'Head1') then return 'disabled' end
 if s.family=='toe' and need(VB..s.side..'_Foot') then return 'disabled' end
 if s.segment>0 and need(VB..s.side..'_Hand') then return 'disabled' end
 local value=BM.Value(state,key)
 local worst
 for _,i in ipairs(state.issues) do if i.slot==key then if i.severity=='error' then worst='error' elseif i.severity=='warning' and worst~='error' then worst='warning' end end end
 if worst=='error' then return 'missing' end
 if value<0 then return s.required and 'missing' or 'created' end
 if worst=='warning' then return 'check' end
 local origin=key:sub(1,4)=='Eye_' and state.eyes[key:sub(5)].origin or state.slots[key].origin
 if origin=='guess' then return 'check' end
 return 'ok'
end
local function shown(state,s) return not (s.convertOnly and state.mode~='convert') end
function BM.Summary(state)
 local r={missing={},check={},groups={},errors=0,warnings=0,required=0,assigned=0}
 for _,i in ipairs(state.issues) do if i.severity=='error' then r.errors=r.errors+1 elseif i.severity=='warning' then r.warnings=r.warnings+1 end end
 local rank={missing=4,check=3,created=2,ok=1,disabled=0}
 for _,s in ipairs(BM.Slots) do if shown(state,s) then
  local st=BM.StatusOf(state,s.key) local g=groupOf(s)
  local entry=r.groups[g] or {done=0,total=0,worst='ok'} r.groups[g]=entry
  entry.total=entry.total+1 if st=='ok' or st=='created' or st=='check' then entry.done=entry.done+1 end
  if rank[st]>rank[entry.worst] then entry.worst=st end
  if s.required then r.required=r.required+1 if BM.Value(state,s.key)>=0 then r.assigned=r.assigned+1 end end
  if st=='missing' then r.missing[#r.missing+1]=s.key elseif st=='check' then r.check[#r.check+1]=s.key end
 end end
 if state.humanoid==false and r.assigned<6 then r.headline='not_humanoid'
 elseif #r.missing>0 or r.errors>0 then r.headline='missing'
 elseif #r.check>0 then r.headline='check'
 else r.headline='ok' end
 return r
end
-- The next part to assign after `from`: in a check run the next part to check;
-- otherwise the next missing part of the same chain, then the next missing one.
local Chains={}
for _,c in ipairs({{'Pelvis','Spine1','Spine2','Spine4','Neck1','Head1'}}) do Chains[#Chains+1]=c end
for _,s in ipairs({'L_','R_'}) do Chains[#Chains+1]={s..'Clavicle',s..'UpperArm',s..'Forearm',s..'Hand'} Chains[#Chains+1]={s..'Thigh',s..'Calf',s..'Foot',s..'Toe0'}
 for f=0,4 do Chains[#Chains+1]={s..'Finger'..f,s..'Finger'..f..'1',s..'Finger'..f..'2'} end end
local chainOf={} for _,c in ipairs(Chains) do for i,k in ipairs(c) do chainOf[VB..k]={c,i} end end
function BM.NextSlot(state,from,dir,onlyCheck)
 dir=dir or 1
 local list={} for _,s in ipairs(BM.Slots) do if shown(state,s) and BM.StatusOf(state,s.key)~='disabled' then list[#list+1]=s.key end end
 local at=0 for i,k in ipairs(list) do if k==from then at=i end end
 if onlyCheck then
  for step=1,#list do local k=list[(at-1+step*dir)%#list+1] if BM.StatusOf(state,k)=='check' then return k end end
  return nil
 end
 if dir==1 and from and chainOf[from] then local c,i=chainOf[from][1],chainOf[from][2]
  for j=i+1,#c do local k=VB..c[j] if BM.StatusOf(state,k)=='missing' then return k end end end
 for step=1,#list do local k=list[(at-1+step*dir)%#list+1] if k~=from and BM.StatusOf(state,k)=='missing' then return k end end
 return nil
end
-- Tab order: every shown, enabled part.
function BM.TabSlot(state,from,dir)
 local list={} for _,s in ipairs(BM.Slots) do if shown(state,s) and BM.StatusOf(state,s.key)~='disabled' then list[#list+1]=s.key end end
 if #list==0 then return nil end
 local at=0 for i,k in ipairs(list) do if k==from then at=i end end
 if at==0 then return dir==-1 and list[#list] or list[1] end
 return list[(at-1+dir)%#list+1]
end

-- ---- suggestions ----
local Expected={pelvis=.52,spine={Spine1=.58,Spine2=.64,Spine4=.72},neck=.83,head=.88,eye=.92,clavicle=.81,upperarm=.80,thigh=.50,calf=.28,foot=.05,toe=.02}
local function better(state,a,b)
 local x,y=state.bones[a+1],state.bones[b+1]
 if x.weighted~=y.weighted then return x.weighted>y.weighted end
 if x.subtree~=y.subtree then return x.subtree>y.subtree end
 if x.depth~=y.depth then return x.depth<y.depth end
 return a<b
end
BM.Better=better
-- Ranked bones for a part: {bone, score, reasons={{key, args, points}}}.
function BM.Candidates(state,key,limit)
 limit=limit or 3
 local s=BM.SlotByKey[key] if not s then return {} end
 local H=state.height local out={}
 local anchor,anchorKey=BM.AnchorOf(state,s)
 local upper=BM.Value(state,VB..s.side..'_UpperArm')
 for i,bone in ipairs(state.bones) do local b=i-1
  if not ((bone.flags.helper or bone.flags.ik) and bone.weighted==0) then
   local score,reasons=0,{}
   local function add(points,reason,args) score=score+points if reason then reasons[#reasons+1]={key=reason,args=args or {},points=points} end end
   local segmentOk=s.segment==0 or bone.segment==s.segment
   if bone.meaning==s.family and (s.side=='' or bone.side==s.side) and segmentOk then add(60,'bonemap.reason.named',{part=key}) end
   if anchor>=0 then
    if bone.parent==anchor then add(35,'bonemap.reason.child',{bone=name(state,anchor)})
    elseif below(state,b,anchor) then add(25,'bonemap.reason.below',{bone=name(state,anchor)})
    else add(-20) end
   end
   local lat=BM.Lateral(state,b)
   if (s.side=='L' and lat>.02*H) then add(15,'bonemap.reason.side_left')
   elseif (s.side=='R' and lat<-.02*H) then add(15,'bonemap.reason.side_right')
   elseif s.side=='' and math.abs(lat)<.05*H then add(15,'bonemap.reason.middle') end
   local e=Expected[s.family] if type(e)=='table' then e=e[key:sub(#VB+1)] end
   if s.family=='forearm' or s.family=='hand' then
    if upper>=0 then local want=(s.family=='forearm' and .17 or .33)*H local d=distance(bone,state.bones[upper+1])
     local h=15*(1-math.min(1,math.abs(d-want)/(.08*H))) add(h,h>=10 and 'bonemap.reason.height' or nil) end
   elseif e and s.segment==0 then local y=(bone.pos[2]-state.floor)/H local h=15*(1-math.min(1,math.abs(y-e)/.15)) add(h,h>=10 and 'bonemap.reason.height' or nil) end
   local weight=BM.BoneWeight(state,b)
   if weight>0 then add(10,'bonemap.reason.moves',{count=weight}) else add(-15,'bonemap.reason.moves_none') end
   local using=BM.SlotUsing(state,b) if using and using~=key then add(-40,'bonemap.reason.used',{part=using}) end
   if bone.flags.twist or chainKind[bone.meaning] then add(-30,'bonemap.reason.helper') end
   if bone.flags.secondary then add(-50,'bonemap.reason.physics') end
   if score>=20 then
    table.sort(reasons,function(x,y) return math.abs(x.points)>math.abs(y.points) end)
    while #reasons>3 do table.remove(reasons) end
    out[#out+1]={bone=b,score=score,reasons=reasons}
   end
  end
 end
 table.sort(out,function(x,y) if x.score~=y.score then return x.score>y.score end return better(state,x.bone,y.bone) end)
 while #out>limit do table.remove(out) end
 return out
end
-- "best match": a clear winner.
function BM.BestMatch(list) return list[1]~=nil and list[1].score>=60 and (list[2]==nil or list[1].score-list[2].score>=15) end
function BM.BoneWeight(state,b) local bone=state.bones[b+1] return bone and bone.weighted or 0 end
-- The vertices a part moves: its bone, the bones moving with it (fit mode) and
-- the unassigned bones below it up to the next assigned one (MMD legs weight 足D).
function BM.EffectiveWeight(state,key)
 local b=BM.Value(state,key) if b<0 then return 0 end
 local used={} for _,v in pairs(BM.Values(state,true)) do if v>=0 then used[v]=true end end
 local total=state.bones[b+1].weighted
 for _,a in ipairs(state.aliases[key] or {}) do local bone=state.bones[a+1] if bone then total=total+bone.weighted end end
 local stack={} for _,c in ipairs(state.bones[b+1].children) do stack[#stack+1]=c end
 while #stack>0 do local c=table.remove(stack) if not used[c] then local bone=state.bones[c+1] total=total+bone.weighted for _,k in ipairs(bone.children) do stack[#stack+1]=k end end end
 return total
end
-- Region of every bone, parents first: the part using it, a swinging part, or its parent's.
function BM.Regions(state)
 local slotRegion={}
 for _,s in ipairs(BM.Slots) do local b=BM.Value(state,s.key)
  if b>=0 and shown(state,s) then slotRegion[b]=s.side=='L' and 'left' or s.side=='R' and 'right' or 'body' end end
 local swings={} for _,c in ipairs(BM.ImportedChains(state)) do swings[c.root]=c.kind end
 local out={}
 for _,b in ipairs(state.order) do local bone=state.bones[b+1]
  if slotRegion[b] then out[b]=slotRegion[b]
  elseif swings[b] then out[b]='swing'
  elseif bone.parent>=0 and out[bone.parent] then out[b]=out[bone.parent]
  else out[b]='unassigned' end
 end
 return out
end

-- ---- mirror and swap ----
local function partnerKey(key) local s=BM.SlotByKey[key] return s and s.partner or '' end
-- Copies one side's assignment to the other through the bones' mirror partners.
-- Returns copied, unmatched parts and the parts set by hand that were kept
-- (replaceUser replaces those too).
function BM.Mirror(state,from,replaceUser)
 local copied,unmatched,kept=0,{},{}
 for _,s in ipairs(BM.Slots) do if s.side==from and shown(state,s) then
  local b=BM.Value(state,s.key)
  if b>=0 then
   local target=partnerKey(s.key) local m=state.bones[b+1].mirror
   local origin=target:sub(1,4)=='Eye_' and state.eyes[target:sub(5)].origin or (state.slots[target] or {}).origin
   if m<0 then unmatched[#unmatched+1]=s.key
   elseif origin=='user' and not replaceUser then kept[#kept+1]=target
   elseif BM.Value(state,target)~=m then set(state,target,m,'user',1) copied=copied+1 end
  end
 end end
 if copied>0 then changed(state,'mirror') end
 return copied,unmatched,kept
end
-- Exchanges every left and right part. With turn (the default when converting) the
-- picture turns too: the converter orients the character by its left and right
-- parts, so the relabelled character faces the other way. That keeps every side
-- relation and fixes toes pointing backwards (a mirrored export); a relabel
-- without turning fixes parts assigned to the wrong sides. Fit mode never turns.
function BM.SwapSides(state,turn)
 if turn==nil then turn=true end
 for _,s in ipairs(BM.Slots) do if s.side=='L' and shown(state,s) then
  local p=s.partner
  if s.key:sub(1,4)=='Eye_' then state.eyes.L,state.eyes.R=state.eyes.R,state.eyes.L
  else state.slots[s.key],state.slots[p]=state.slots[p],state.slots[s.key] end
 end end
 if turn and state.mode=='convert' then state.turn=(state.turn+180)%360 end
 changed(state,'swap')
end

-- ---- swinging parts (convert mode) ----
local Presets={
 hair={stiffness=1,dragForce=.4,gravityPower=0,hitRadius=.02},
 skirt={stiffness=.8,dragForce=.5,gravityPower=.3,hitRadius=.03},
 chest={stiffness=2,dragForce=.6,gravityPower=0,hitRadius=0},
 tail={stiffness=.7,dragForce=.35,gravityPower=.15,hitRadius=.03},
 accessory={stiffness=2,dragForce=.55,gravityPower=.05,hitRadius=.015}}
BM.Presets=Presets
BM.Ranges={stiffness={0,4,.05},dragForce={0,1,.05},gravityPower={0,2,.05},hitRadius={0,.1,.005}}
local function round(v,step) return math.floor(v/step+.5)*step end
function BM.GroupValues(group)
 if group.custom and istable(group.values) then return copy(group.values) end
 local p=Presets[group.kind] or Presets.accessory local v=copy(p)
 if group.swing=='less' then v.stiffness=v.stiffness*1.5 v.dragForce=math.min(.95,v.dragForce+.15) v.gravityPower=v.gravityPower*.7
 elseif group.swing=='more' then v.stiffness=v.stiffness*.6 v.dragForce=math.max(.05,v.dragForce-.15) v.gravityPower=v.gravityPower*1.3 end
 v.stiffness=round(v.stiffness,.01) v.dragForce=round(v.dragForce,.01) v.gravityPower=round(v.gravityPower,.01) v.hitRadius=round(v.hitRadius,.001)
 return v
end
local function bodySet(state)
 local body={} for _,v in pairs(BM.Values(state,true)) do if v>=0 then body[v]=true end end
 local contains={}
 for k=#state.order,1,-1 do local b=state.order[k] local bone=state.bones[b+1]
  if body[b] then contains[b]=true end
  if contains[b] and bone.parent>=0 then contains[bone.parent]=true end
 end
 return body,contains
end
local function parentSlot(state,b,body)
 local p=state.bones[b+1].parent
 while p>=0 do if body[p] then return BM.SlotUsing(state,p) end p=state.bones[p+1].parent end
end
-- The kind of a swinging part, by name (two levels down) or by where it hangs.
local function chainKindOf(state,root,body)
 local hint=false local found
 local level={root}
 for depth=0,2 do local nextLevel={}
  for _,b in ipairs(level) do local bone=state.bones[b+1]
   if bone.flags.secondaryHint then hint=true end
   if not found and chainKind[bone.meaning] then found=bone.meaning end
   for _,c in ipairs(bone.children) do nextLevel[#nextLevel+1]=c end
  end
  level=nextLevel
 end
 if found then return found,hint and .9 or .8 end
 local H=state.height local slot=parentSlot(state,root,body) or ''
 local bones=BM.ChainBones(state,root) local rootBone=state.bones[root+1]
 local last=bones[#bones] for _,b in ipairs(bones) do if #state.bones[b+1].children==0 then last=b end end
 local lastBone=state.bones[last+1]
 local lo,hi=rootBone.pos[2],rootBone.pos[2] for _,b in ipairs(bones) do local y=state.bones[b+1].pos[2] lo=math.min(lo,y) hi=math.max(hi,y) end
 local dx,dy,dz=lastBone.pos[1]-rootBone.pos[1],lastBone.pos[2]-rootBone.pos[2],lastBone.pos[3]-rootBone.pos[3]
 local len=math.sqrt(dx*dx+dy*dy+dz*dz) if len>1e-6 then dz=dz/len else dz=0 end
 if state.turn==180 then dz=-dz end
 local short=slot:sub(#VB+1)
 if (short=='Head1' or short=='Neck1') and hi-lo>=.05*H then return 'hair',.5 end
 if (short=='Pelvis' or short=='L_Thigh' or short=='R_Thigh') and rootBone.pos[2]-lastBone.pos[2]>.05*H then return 'skirt',.5 end
 if (short=='Spine1' or short=='Spine2' or short=='Spine4') and #bones<=3 and dz>.5 then return 'chest',.5 end
 if (short=='Pelvis' or short=='Spine1') and dz<-.6 then return 'tail',.5 end
 if hint then return 'accessory',.8 end
 return 'accessory',.4
end
-- Branches off the body that can swing: one per strand of an unweighted group node.
function BM.SuggestChains(state)
 local body,contains=bodySet(state)
 local roots={}
 local function size(b) local n,w=0,0 for _,c in ipairs(BM.ChainBones(state,b)) do n=n+1 if state.bones[c+1].weighted>0 then w=w+1 end end return n,w end
 local function consider(r)
  local bone=state.bones[r+1]
  if contains[r] or bone.flags.ik then return end
  local n,w=size(r) if n<2 or w<1 then return end
  if bone.weighted==0 and #bone.children>=2 then for _,c in ipairs(bone.children) do consider(c) end return end
  roots[#roots+1]={root=r,size=n}
 end
 for i,bone in ipairs(state.bones) do local b=i-1
  local p=bone.parent
  if p>=0 and not contains[b] and (body[p] or contains[p]) then consider(b) end
 end
 table.sort(roots,function(a,b) if a.size~=b.size then return a.size>b.size end return a.root<b.root end)
 local chains={}
 for i=1,math.min(#roots,BM.MaxChains) do local r=roots[i].root local kind,confidence=chainKindOf(state,r,body)
  chains[#chains+1]={root=r,kind=kind,confidence=confidence,enabled=kind~='chest' and confidence>=.8 or kind=='chest',user=false}
 end
 table.sort(chains,function(a,b) return a.root<b.root end)
 return chains
end
function BM.DefaultGroups(state,chains)
 local groups={}
 for _,kind in ipairs(BM.Kinds) do local any,sure=false,false
  for _,c in ipairs(chains) do if c.kind==kind then any=true if c.confidence>=.8 then sure=true end end end
  if any then groups[#groups+1]={kind=kind,enabled=kind~='chest' and sure,swing='normal',custom=false,collide=kind~='chest'} end
 end
 return groups
end
function BM.GroupOf(state,kind) for _,g in ipairs(state.groups) do if g.kind==kind then return g end end end
-- Recomputes suggestions after the body changed, keeping the player's choices by
-- chain root name and group kind.
function BM.RefreshChains(state)
 if state.mode~='convert' or state.chainsVersion==state.mapVersion then return end
 local before={} for _,c in ipairs(state.chains) do before[name(state,c.root)]=c end
 local groupsBefore={} for _,g in ipairs(state.groups) do groupsBefore[g.kind]=g end
 local chains=BM.SuggestChains(state)
 local listed={} for _,c in ipairs(chains) do local old=before[name(state,c.root)] if old then c.kind=old.kind c.enabled=old.enabled c.user=old.user end listed[c.root]=true end
 for _,c in pairs(before) do if c.user and not listed[c.root] then chains[#chains+1]=c end end
 state.chains=chains
 local groups=BM.DefaultGroups(state,chains)
 for i,g in ipairs(groups) do local old=groupsBefore[g.kind] if old then groups[i]=old end end
 for kind,old in pairs(groupsBefore) do local found=false for _,g in ipairs(groups) do if g.kind==kind then found=true end end
  local used=false for _,c in ipairs(chains) do if c.kind==kind then used=true end end
  if not found and used then groups[#groups+1]=old end end
 table.sort(groups,function(a,b) local ia,ib=0,0 for i,k in ipairs(BM.Kinds) do if k==a.kind then ia=i end if k==b.kind then ib=i end end return ia<ib end)
 state.groups=groups state.chainsVersion=state.mapVersion
end
-- A part the player adds: refused when it is, or holds, a body part.
function BM.AddChain(state,bone)
 local body,contains=bodySet(state)
 if contains[bone] then return false,'bonemap.jiggle.refused' end
 for _,c in ipairs(BM.ImportedChains(state)) do if c.root==bone or below(state,bone,c.root) then return false,'bonemap.jiggle.included',c end end
 local kind=chainKindOf(state,bone,body)
 local existing
 for _,c in ipairs(state.chains) do if c.root==bone then existing=c end end
 if existing then existing.enabled=true else state.chains[#state.chains+1]={root=bone,kind=kind,confidence=1,enabled=true,user=true} existing=state.chains[#state.chains] end
 local g=BM.GroupOf(state,existing.kind)
 if not g then g={kind=existing.kind,enabled=true,swing='normal',custom=false,collide=existing.kind~='chest'} state.groups[#state.groups+1]=g end
 g.enabled=true
 changed(state,'chain')
 return true,nil,existing
end
-- Moves a group to another kind (merging with that kind's group) with its preset.
function BM.SetGroupKind(state,group,kind)
 if group.kind==kind then return end
 for _,c in ipairs(state.chains) do if c.kind==group.kind then c.kind=kind end end
 local target=BM.GroupOf(state,kind)
 if target then for i,g in ipairs(state.groups) do if g==group then table.remove(state.groups,i) break end end target.enabled=target.enabled or group.enabled
 else group.kind=kind group.custom=false group.values=nil group.collide=kind~='chest' end
 changed(state,'group')
end

-- ---- output ----
-- The import request (spec 9.3): bones by name, "" for none.
function BM.Request(state)
 local map={}
 for _,s in ipairs(BM.Slots) do if not s.convertOnly then local b=BM.Value(state,s.key) map[s.key]=b>=0 and name(state,b) or '' end end
 local eyes={L=state.eyes.L.bone>=0 and name(state,state.eyes.L.bone) or '',R=state.eyes.R.bone>=0 and name(state,state.eyes.R.bone) or ''}
 local groups={}
 for _,g in ipairs(state.groups) do local chains={}
  for _,c in ipairs(state.chains) do if c.kind==g.kind then chains[#chains+1]={root=name(state,c.root),enabled=c.enabled==true} end end
  if #chains>0 then groups[#groups+1]={kind=g.kind,enabled=g.enabled==true,swing=g.swing or 'normal',custom=g.custom==true,collide=g.collide~=false and g.kind~='chest',values=BM.GroupValues(g),chains=chains} end
 end
 return {kind='character',requestVersion=1,boneMap=map,eyes=eyes,jiggle={version=1,groups=groups}}
end
-- The fitter pins (spec 9.7): every part that differs from the fitter's own choice.
function BM.Pins(state)
 local pins={}
 for _,s in ipairs(BM.Slots) do if not s.convertOnly then
  local b=BM.Value(state,s.key) local a=(state.auto[s.key] or {}).bone or -1
  if b~=a then pins[s.key]=b end
 end end
 return pins
end
function BM.SamePins(a,b)
 for k,v in pairs(a or {}) do if (b or {})[k]~=v then return false end end
 for k,v in pairs(b or {}) do if (a or {})[k]~=v then return false end end
 return true
end
-- Whether two pin sets (nil is none; values compare as numbers) give the ragdoll's body
-- parts the same bones. Collision corrections sit in those bones' frames: they stay valid
-- when only other pins change (fingers, toes, neck, middle spine). Saving bones
-- (bone_mapper.lua) drops the saved corrections by this rule, and the collision and
-- physics editors refuse a ragdoll's own corrections by it.
function BM.SamePhysicalPins(a,b)
 a,b=type(a)=='table' and a or {},type(b)=='table' and b or {}
 for _,s in ipairs(BM.Slots) do if s.physical and tonumber(a[s.key])~=tonumber(b[s.key]) then return false end end
 return true
end
-- Pins from outside (a save request, a Workshop package's fit): {part: bone index, or -1
-- for none} for assignable parts only. The pins as numbers, or nil when any entry is wrong.
function BM.CleanPins(map)
 if not istable(map) then return nil end
 local pins,count={},0
 for key,v in pairs(map) do
  count=count+1 local slot=BM.SlotByKey[key] local n=tonumber(v)
  if count>52 or not slot or slot.convertOnly or not n or n~=math.floor(n) or n<-1 or n>=2^31 then return nil end
  pins[key]=n
 end
 return pins
end
-- {"<field>":{"<part>":<bone>}} for the native readers, with whole numbers written as
-- such: util.TableToJSON may write 12 as 12.0. Unknown parts are left out.
function BM.IndexJSON(field,map)
 local parts={}
 for key,v in pairs(istable(map) and map or {}) do local n=tonumber(v) if BM.SlotByKey[key] and n and math.abs(n)<2^31 then parts[#parts+1]='"'..key..'":'..string.format('%d',math.floor(n)) end end
 table.sort(parts)
 return '{"'..field..'":{'..table.concat(parts,',')..'}}'
end

-- ---- memory: the last assignment of a file, or of any file with the same skeleton ----
local MemoryRoot='mmd_hotloader/bone_maps/'
local function validHex(s) return isstring(s) and #s==64 and not s:find('[^0-9a-f]') end
local function readMemory(path)
 local text=file.Read(path,'DATA') if not text then return nil end
 local ok,data=pcall(util.JSONToTable,text)
 if not ok or not istable(data) or tonumber(data.version)~=1 or not istable(data.boneMap) then print('[Model Hotloader] Ignoring damaged bone map '..path) return nil end
 return data
end
function BM.LoadMemory(probe)
 local memory={}
 if istable(probe) then
  if validHex(probe.sourceSha256) then memory.source=readMemory(MemoryRoot..'sources/'..probe.sourceSha256..'.json') end
  local sig=istable(probe.skeleton) and probe.skeleton.signature
  if validHex(sig) then memory.skeleton=readMemory(MemoryRoot..'skeletons/'..sig..'.json') end
 end
 return memory
end
-- Applies saved names where the bones still exist. Returns how many parts, and the notice.
function BM.ApplyMemory(state,memory)
 local data,origin,notice=memory.source,'saved','saved_file'
 if not data then data,origin,notice=memory.skeleton,'remembered','same_skeleton' end
 if not data then return 0 end
 local applied,from=0,{}
 for key,boneName in pairs(data.boneMap) do
  if BM.Mapped(key) and isstring(boneName) then
   local b=boneName=='' and -1 or state.byName[boneName]
   if b~=nil then
    if b<0 then state.slots[key]={bone=-1,origin='created',confidence=1} else state.slots[key]={bone=b,origin=origin,confidence=1} end
    applied=applied+1 from[key]=true
   end
  end
 end
 for _,k in ipairs({'L','R'}) do local n=istable(data.eyes) and data.eyes[k]
  if isstring(n) then local b=n=='' and -1 or state.byName[n] if b~=nil then state.eyes[k]={bone=b,origin=b<0 and 'created' or origin} end end end
 -- Saved swinging parts, by root name.
 if istable(data.jiggle) and istable(data.jiggle.groups) and tonumber(data.jiggle.version)==1 then
  state.memoryJiggle=data.jiggle
 end
 -- A bone a restored part took leaves the automatic part that had it (a name the
 -- file no longer has keeps its automatic bone, which may be that one).
 local owner={}
 for _,s in ipairs(BM.Slots) do if not s.convertOnly and from[s.key] then local v=state.slots[s.key] if v.bone>=0 then if owner[v.bone] then state.slots[s.key]={bone=-1,origin='created',confidence=0} else owner[v.bone]=s.key end end end end
 for _,s in ipairs(BM.Slots) do if not s.convertOnly and not from[s.key] then local v=state.slots[s.key] if v.bone>=0 then if owner[v.bone] then state.slots[s.key]={bone=-1,origin='created',confidence=0} else owner[v.bone]=s.key end end end end
 if applied>0 then state.notice={key=notice,args={file=data.filename or ''},link='use_auto'} state.mapVersion=state.mapVersion+1 BM.Snapshot(state,'memory') end
 return applied,notice
end
-- Chains and groups restored from memory once the suggestions exist.
function BM.ApplyMemoryJiggle(state)
 local j=state.memoryJiggle if not j then return end state.memoryJiggle=nil
 for _,g in ipairs(j.groups) do if istable(g) and isstring(g.kind) then
  local group=BM.GroupOf(state,g.kind)
  if not group then group={kind=g.kind} state.groups[#state.groups+1]=group end
  group.enabled=g.enabled==true group.swing=g.swing or 'normal' group.custom=g.custom==true group.collide=g.collide~=false group.values=istable(g.values) and copy(g.values) or nil
  for _,c in ipairs(g.chains or {}) do local b=isstring(c.root) and state.byName[c.root]
   if b then local found=false for _,existing in ipairs(state.chains) do if existing.root==b then existing.kind=g.kind existing.enabled=c.enabled==true found=true end end
    if not found then state.chains[#state.chains+1]={root=b,kind=g.kind,confidence=1,enabled=c.enabled==true,user=true} end end
  end
 end end
end
function BM.SaveMemory(state)
 if state.mode~='convert' then return end
 local request=BM.Request(state)
 local data={version=1,kind='source',savedAt=os.time(),filename=state.filename,format=state.format,signature=state.signature or '',boneMap=request.boneMap,eyes=request.eyes,jiggle=request.jiggle}
 file.CreateDir(MemoryRoot..'sources') file.CreateDir(MemoryRoot..'skeletons')
 if validHex(state.sha) then file.Write(MemoryRoot..'sources/'..state.sha..'.json',util.TableToJSON(data)) end
 if validHex(state.signature) then data.kind='skeleton' file.Write(MemoryRoot..'skeletons/'..state.signature..'.json',util.TableToJSON(data))
  -- At most 256 remembered skeletons: the oldest go first.
  local files=file.Find(MemoryRoot..'skeletons/*.json','DATA') or {}
  if #files>256 then local list={}
   for _,f in ipairs(files) do local d=readMemory(MemoryRoot..'skeletons/'..f) list[#list+1]={f=f,t=d and tonumber(d.savedAt) or 0} end
   table.sort(list,function(a,b) if a.t~=b.t then return a.t<b.t end return a.f<b.f end)
   for i=1,#list-256 do file.Delete(MemoryRoot..'skeletons/'..list[i].f) end
  end
 end
end

-- ---- the model view: projection and picking ----
-- view: {mode='front'|'side', turn=0|180, cx, cy, mx, my, mz, scale}
function BM.Project(view,x,y,z)
 if view.turn==180 then x,z=-x,-z end
 local sx
 if view.mode=='side' then sx=view.cx-(z-view.mz)*view.scale else sx=view.cx+(x-view.mx)*view.scale end
 return sx,view.cy-(y-view.my)*view.scale
end
-- projected: list of {bone, x, y, rank?, weight}. Returns the nearest bone within
-- radius and, when several lie within overlap of the point, all of them ordered
-- by candidate rank, then weight, then name.
function BM.HitTest(projected,sx,sy,radius,overlap,names)
 local best,bestD local near={}
 for _,p in ipairs(projected) do local dx,dy=p.x-sx,p.y-sy local d=math.sqrt(dx*dx+dy*dy)
  if d<=radius and (not bestD or d<bestD) then best,bestD=p.bone,d end
  if d<=overlap then near[#near+1]=p end
 end
 if #near<2 then return best,nil end
 table.sort(near,function(a,b)
  local ra,rb=a.rank or 99,b.rank or 99 if ra~=rb then return ra<rb end
  if (a.weight or 0)~=(b.weight or 0) then return (a.weight or 0)>(b.weight or 0) end
  local na,nb=names and names[a.bone] or '',names and names[b.bone] or '' if na~=nb then return na<nb end return a.bone<b.bone end)
 local list={} for _,p in ipairs(near) do list[#list+1]=p.bone end
 return best,list
end
-- Keyboard: key names, Ctrl and Shift, and whether a text box has focus.
local Keys={z={ctrl='undo',ctrlShift='redo'},y={ctrl='redo'},tab={plain='next_slot',shift='prev_slot'},enter={plain='enter'},delete={plain='clear'},backspace={plain='clear'},
 f={ctrl='search',plain='fit'},['1']={plain='view_front'},['2']={plain='view_side'}}
function BM.KeyAction(key,ctrl,shift,textFocus)
 local k=Keys[key] if not k then return nil end
 if textFocus then return nil end
 if ctrl and shift then return k.ctrlShift end
 if ctrl then return k.ctrl end
 if shift then return k.shift end
 return k.plain
end
