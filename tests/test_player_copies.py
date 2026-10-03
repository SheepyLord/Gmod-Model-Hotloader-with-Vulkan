"""Another addon's clientside copy of an MMD player's model (First-Person Body:
a prop posed in its own bone callback and drawn with DrawModel) shows that
player's character: it is adopted when it is drawn, joins the entity list, draws
through the native renderer instead of the empty carrier, counts as shown only
while its addon draws it, mirrors the player's expressions, takes the
first-person mask in the player's own view and never submits a degenerate bone
matrix. Networked entities, panel previews, corpses and copies of models no MMD
player wears stay as they were. Runs player_copies.lua with carrier.lua,
first_person.lua, native_render.lua and instances.lua code."""
from pathlib import Path
from lupa import LuaRuntime
from lua_source import definition

root = Path(__file__).resolve().parents[1]
read = lambda name: (root / 'addon/lua/mmdhl' / name).read_text(encoding='utf-8')
carrier, copies, first, render, instances = (read(n) for n in ('carrier.lua', 'player_copies.lua', 'first_person.lua', 'native_render.lua', 'instances.lua'))
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
SERVER=false CLIENT=true EF_NODRAW=32
NOW=100 RealTime=function() return NOW end FRAME=1 FrameNumber=function() return FRAME end
mmdhl={rigs={}} IsValid=function(e) return type(e)=='table' and not e.removed end
isstring=function(v) return type(v)=='string' end
util={JSONToTable=function() return nil end,TableToJSON=function(t) return t end} file={Read=function() return nil end}
HOOKS={} hook={Add=function(e,n,f) HOOKS[e]=HOOKS[e] or {} HOOKS[e][n]=f end}
TIMERS={} timer={Create=function(n,d,r,f) TIMERS[n]=f end,Simple=function(d,f) end}
ENTITY={} ALL={}
ents={GetAll=function() return ALL end}
FindMetaTable=function(name) assert(name=='Entity') return ENTITY end
ENGINE={} function ENTITY.DrawModel(e,flags) ENGINE[#ENGINE+1]=e end
function ENTITY:EntIndex() return self.index end function ENTITY:GetModel() return self.model end
function ENTITY:GetClass() return self.class or 'prop_physics' end
function ENTITY:GetNoDraw() return self.nodraw==true end function ENTITY:IsEffectActive() return false end
function ENTITY:IsDormant() return false end
function ENTITY:GetNW2String(k,d) local v=self.nw[k] if v==nil then return d end return v end
ENTITY.GetNW2Bool=ENTITY.GetNW2String ENTITY.GetNW2Int=ENTITY.GetNW2String ENTITY.GetNW2Float=ENTITY.GetNW2String
function ENTITY:CallOnRemove(n,f) self.removers[n]=f end
function ENTITY:GetFlexScale() return self.flexScale or 1 end
function ENTITY:GetFlexWeight(i) return self.flexes and self.flexes[i] or 0 end
function ENTITY:GetPos() return Vector(7,8,9) end
function ENTITY:Alive() return true end
function ENTITY:EyePos() return Vector(0,0,64) end
function ENTITY:LookupBone(name) return self.bones and self.bones[name] end
function ENTITY:GetBoneParent(i) return self.parents and self.parents[i] or -1 end
function entity(index,model,fields)
 local e=fields or {} e.index=index e.model=model e.nw=e.nw or {} e.removers={}
 setmetatable(e,{__index=ENTITY}) ALL[#ALL+1]=e return e
end
function remove(e) e.removed=true for _,f in pairs(e.removers) do f(e) end end
-- Vectors and matrices, enough for the bone sanitizer.
local V={} V.__index=V
function Vector(x,y,z) return setmetatable({x=x or 0,y=y or 0,z=z or 0},V) end
function Matrix(scale,position) local m={scale=scale or Vector(1,1,1),position=position or Vector()}
 function m:GetScale() return self.scale end function m:GetTranslation() return self.position end
 function m:SetTranslation(v) self.position=v end
 function m:Set(o) self.scale=o.scale self.position=o.position end return m end
''')
# The identity, entity list and presentation rules of carrier.lua, and its carrier-path test.
lua.execute(carrier[carrier.index('local function networkedAsset('):carrier.index('function mmdhl.GetMetadata(')])
lua.execute(definition(lua, read('persistence.lua'), 'local function carrierPath(').replace('local function carrierPath(', 'function mmdhl.IsCarrierModel(', 1))
lua.execute(r'''
RELEASED={} mmdhl.ReleasePresentation=function(e) RELEASED[#RELEASED+1]=e e.MMDHLClientInstance=nil end
DRAWN={} mmdhl.DrawCarrier=function(e,translucent,flags) DRAWN[#DRAWN+1]={e=e,translucent=translucent,flags=flags} end
-- The DModelPanel control: its DrawModel draws its entity.
PANEL={DrawModel=function(self) self.Entity:DrawModel() end}
vgui={GetControlTable=function(name) return name=='DModelPanel' and PANEL or nil end}
PLAYERS={} player={GetAll=function() return PLAYERS end} LocalPlayer=function() return PLAYERS[1] end
''')
lua.execute(copies)
lua.execute(definition(lua, first, 'function mmdhl.IsLocalFirstPerson(') + definition(lua, first, 'function mmdhl.FirstPersonView('))
lua.execute(render[render.index('local function finite('):render.index("-- Source's client bone palette")])
lua.execute(definition(lua, instances, 'function mmdhl.SyncActorMorphs('))

lua.execute(r'''
local A,B=string.rep('a',64),string.rep('b',64)
local model,other='models/mmd/aaaaaaaaaaaaaaaa/player.mdl','models/mmd/bbbbbbbbbbbbbbbb/player.mdl'
mmdhl.rigs.ka={key='ka',asset=A,model=model,morphs={{native=0,mmd=0},{native=-1,mmd=1}}}
mmdhl.rigs.kb={key='kb',asset=B,model=other,morphs={}}
local me=entity(1,model,{nw={MMDHLAsset=A,MMDHLRig='ka',MMDHLMorph1=.75},flexes={[0]=.25}}) PLAYERS[1]=me
-- What the addon captured: ENTITY.DrawModel after this addon loaded.
local DrawModel=ENTITY.DrawModel
-- Each draw is a frame of its own: networked values are read once per frame.
local function draws(e,flags) FRAME=FRAME+1 ENGINE={} DRAWN={} DrawModel(e,flags) return #ENGINE,#DRAWN end
local function listed(e) mmdhl.InvalidateEntityList() for _,x in ipairs(mmdhl.Entities()) do if x==e then return true end end return false end

-- First-Person Body's body: a client prop of the player's model, drawn by its RenderOverride.
local body=entity(-1,model)
local engine,drawn=draws(body,1)
assert(body.MMDHLCopyOf==me and body.MMDHLLocalAsset==A and body.MMDHLLocalRig==mmdhl.rigs.ka,'the copy was not adopted')
assert(engine==0 and drawn==0,'the copy drew the carrier, or drew before it has a presentation')
assert(listed(body),'the copy is missing from the entity list')
body.MMDHLClientInstance=5
engine,drawn=draws(body,1)
assert(engine==0 and drawn==2 and DRAWN[1].e==body and DRAWN[1].translucent==false and DRAWN[2].translucent==true and DRAWN[1].flags==1,'the copy did not draw the character')

-- Shown while its addon draws it, whatever its no-draw flag; released 5 s after the last draw.
assert(not mmdhl.PresentationSuppressed(body))
body.nodraw=true assert(not mmdhl.PresentationSuppressed(body),'a hand-drawn no-draw copy was suppressed') body.nodraw=nil
NOW=NOW+1.5 assert(mmdhl.PresentationSuppressed(body) and not mmdhl.PresentationReleased(body),'an undrawn copy was not suppressed, or was released at once')
NOW=NOW+4 assert(mmdhl.PresentationReleased(body),'an undrawn copy kept its presentation')
draws(body,1) assert(not mmdhl.PresentationSuppressed(body))

-- Its companion that is never drawn (Body_NoDraw) stays a plain prop.
local idle=entity(-1,model,{nodraw=true}) assert(idle.MMDHLCopyOf==nil and not listed(idle))

-- Unchanged: networked entities, other models, panel previews, corpses.
local networked=entity(12,model)
engine,drawn=draws(networked) assert(engine==1 and drawn==0 and networked.MMDHLCopyOf==nil,'a networked entity was adopted')
local prop=entity(-1,'models/props_c17/oildrum001.mdl')
engine=draws(prop) assert(engine==1 and prop.MMDHLCopyOf==nil)
local npc=entity(-1,other)
engine=draws(npc) assert(engine==1 and npc.MMDHLCopyOf==nil,'a copy of a model no player wears was adopted')
local preview=entity(-1,model) PANEL.Entity=preview ENGINE={} PANEL:DrawModel()
assert(#ENGINE==1 and preview.MMDHLCopyOf==nil,'a panel preview was adopted')
local corpse=entity(-1,model,{MMDHLLocalAsset=A,MMDHLLocalRigKey='ka',MMDHLCorpse=true})
engine=draws(corpse) assert(engine==1 and corpse.MMDHLCopyOf==nil,'a corpse was taken for a copy')

-- A player who later wears that model: the copy is adopted at the next check (1 s).
local friend=entity(2,other,{nw={MMDHLAsset=B,MMDHLRig='kb'}}) PLAYERS[2]=friend
engine=draws(npc) assert(engine==1 and npc.MMDHLCopyOf==nil,'checked again before a second passed')
NOW=NOW+1.1 engine=draws(npc) assert(engine==0 and npc.MMDHLCopyOf==friend,'the copy of the second player was not adopted')

-- The player takes another model: the drawn copy lets go at once, an undrawn one within half a second.
me.model='models/player/kleiner.mdl' me.nw={}
engine=draws(body) assert(engine==1 and body.MMDHLCopyOf==nil and body.MMDHLLocalAsset==nil and not listed(body),'the copy kept the old character')
friend.model=model friend.nw={MMDHLAsset=A,MMDHLRig='ka'} NOW=NOW+1.1 draws(body) assert(body.MMDHLCopyOf==friend)
friend.model='models/player/alyx.mdl' FRAME=FRAME+1 TIMERS['MMDHL.PlayerCopies']() assert(body.MMDHLCopyOf==nil,'the sweep kept an undrawn copy')
remove(npc) TIMERS['MMDHL.PlayerCopies']() assert(mmdhl.playerCopies[npc]==nil)
''')
print('PASS: a drawn copy of an MMD player model is adopted and draws the character; panels, corpses, networked and unworn models are untouched; copies let go with the model')

lua.execute(r'''
local model='models/mmd/aaaaaaaaaaaaaaaa/player.mdl'
local me=entity(1,model,{nw={MMDHLAsset=string.rep('a',64),MMDHLRig='ka',MMDHLMorph1=.75},flexes={[0]=.25},flexScale=1}) PLAYERS={me}
local body=entity(-1,model) ENTITY.DrawModel(body) assert(body.MMDHLCopyOf==me)
-- The player's own first-person view: the mask, without our clip plane; shadows and other views keep the body.
function Vector(x,y,z) local v={x=x or 0,y=y or 0,z=z or 0} function v:DistToSqr(o) return (self.x-o.x)^2+(self.y-o.y)^2+(self.z-o.z)^2 end return v end
-- VIEW is the camera of the pass being drawn (EyePos).
GetViewEntity=function() return me end EyePos=function() return VIEW.origin end
VIEW={origin=Vector(1,0,64)} render={GetViewSetup=function() return VIEW end}
-- First-Person Body sets the body 14 units back: its chest is behind the camera.
body.bones={['ValveBiped.Bip01_Spine4']=3} body.MMDRenderPose={[4]=Matrix(Vector(1,1,1),Vector(0,-14,50))}
assert(mmdhl.FirstPersonView(body,false)=='mask','no first-person mask in the player\'s own view')
-- No offset (in vehicles): the camera is inside the chest, so our clip below the camera as well.
body.MMDRenderPose[4]=Matrix(Vector(1,1,1),Vector(2,3,50))
assert(mmdhl.FirstPersonView(body,false)==true,'a camera inside the copy\'s chest was not clipped')
body.MMDRenderPose[4]=Matrix(Vector(1,1,1),Vector(0,-14,50))
assert(mmdhl.FirstPersonView(body,true)==nil,'a shadow lost the head')
VIEW={origin=Vector(120,0,64)} assert(mmdhl.FirstPersonView(body,false)==nil,'a third-person view masked the copy')
VIEW={origin=Vector(1,0,64)} assert(mmdhl.FirstPersonView(me,false)==false,'the player\'s own body is drawn in first person again')
-- Expressions: flex and networked morphs come from the player.
SENT=nil native={SetMorphs=function(h,w) SENT=w end,SetBonePose=function() end}
mmdhl.GetMorphs=function(e) return mmdhl.rigs.ka.morphs end mmdhl.GetInstance=function() return 5 end
mmdhl.GetAsset=function() return string.rep('a',64) end mmdhl.assets={[string.rep('a',64)]={bones=0}}
mmdhl.SyncActorMorphs(body)
assert(SENT and SENT[1]==.25 and SENT[2]==.75,'the copy does not show the player\'s expressions')
''')
print('PASS: the player\'s own first-person view masks the copy\'s head and arms, and clips below a camera inside its chest; shadows and other views keep them; expressions follow the player')

lua.execute(r'''
local V={} V.__index=V function Vector(x,y,z) return setmetatable({x=x or 0,y=y or 0,z=z or 0},V) end
local body=entity(-1,'x')
local good=Matrix(Vector(1,1,1),Vector(1,2,3)) assert(mmdhl.SaneBoneMatrix(good,body)==good,'a valid bone matrix was replaced')
local flat=Matrix(Vector(0,0,0),Vector(4,5,6)) local fixed=mmdhl.SaneBoneMatrix(flat,body)
assert(fixed~=flat and fixed:GetScale().x==1 and fixed:GetTranslation().x==4 and fixed:GetTranslation().z==6,'a zero-scale bone kept no scale or lost its position')
local lost=Matrix(Vector(1,1,1),Vector(0/0,0,0)) fixed=mmdhl.SaneBoneMatrix(lost,body)
assert(fixed:GetTranslation().x==7 and fixed:GetTranslation().z==9,'a bone without a position did not take the copy\'s')
local far=Matrix(Vector(1,1,1),Vector(1e12,0,0)) assert(mmdhl.SaneBoneMatrix(far,body)~=far)
-- In vehicles First-Person Body sends the head 10000 units away: the head and its
-- children stay at the neck; bones of the body stay where they are.
function V.DistToSqr(a,b) return (a.x-b.x)^2+(a.y-b.y)^2+(a.z-b.z)^2 end
local root,neck=Matrix(Vector(1,1,1),Vector(0,0,40)),Matrix(Vector(1,1,1),Vector(0,0,60))
local head,eye,hand=Matrix(Vector(1,1,1),Vector(0,10000,60)),Matrix(Vector(1,1,1),Vector(0,10000,62)),Matrix(Vector(1,1,1),Vector(20,0,45))
body.parents={[1]=0,[2]=1,[3]=2,[4]=1}
local palette={root,neck,head,eye,hand}
mmdhl.GatherCopyBones(body,palette)
assert(palette[1]==root and palette[2]==neck and palette[5]==hand,'a bone near the skeleton was moved')
assert(palette[3]:GetTranslation().z==60 and palette[3]:GetTranslation().y==0 and palette[4]:GetTranslation().y==0,'a far head (or its child) was not gathered to the neck')
''')
print('PASS: zero-scale, non-finite and far-away copy bones are replaced before they reach the native batch')
