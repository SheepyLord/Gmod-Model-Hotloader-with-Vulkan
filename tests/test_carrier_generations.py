"""2.2's carriers (rig generator 30) under 2.3.0's fit (generator 31): which torso bones a
fit picks changed, the carrier and rig.json format did not. Saves and dupes of 2.2
ragdolls, NPCs and player models restore with their own carrier instead of failing as
outdated, through Sandbox's duplicator, the SetModel binding and the entity modifier;
published NPC and player-model registrations come back after a restart. A carrier the
module cannot load (generator 29, or one newer than the module) is still refused. New
fits (a 2.2 ragdoll pasted as an NPC) use PrepareCarrier. An older module without
GetCapabilities().rigGeneratorMin loads its own generator only. Once the character is
spawned again from the library, its 2.3.0 NPC or player model replaces the 2.2 entry of
the same role and gender in the clients' NPC tab and player-model selector (one entry,
not two of the same name, also after a restart); the server keeps the 2.2 one for saves,
dupes and players who chose it. Runs actors.lua's rig checks and registrations,
persistence.lua and sharing.lua's RestorePublishedActors and catalog against a simulated
server and client."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition

root = Path(__file__).resolve().parents[1]
read = lambda name: (root / 'addon/lua/mmdhl' / name).read_text(encoding='utf-8')
actors, persistence, sharing = read('actors.lua'), read('persistence.lua'), read('sharing.lua')

lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute('SERVER=true CLIENT=false mmdhl={}')
attach(lua)
# actors.lua's header (the module's capabilities and the rig checks) with the registrations
# (UnregisterActor to RegisterActor).
lua.globals().ACTORS = (actors[:actors.index('mmdhl.actorProfiles=')] + actors[actors.index('function mmdhl.UnregisterActor('):actors.index('function mmdhl.RegisterActor(')]
                        + definition(lua, actors, 'function mmdhl.RegisterActor('))
lua.globals().PERSISTENCE = persistence
lua.globals().RESTORE = definition(lua, sharing, " hook.Add('InitPostEntity','MMDHL.RestorePublishedActors',")
# The catalog a joining client gets: a local of sharing.lua's server block, run as a global.
lua.globals().CATALOG = definition(lua, sharing, ' local function catalog(p)').replace('local function', 'function', 1)
lua.execute(r'''
isstring=function(v) return type(v)=='string' end istable=function(v) return type(v)=='table' end isnumber=function(v) return type(v)=='number' end
-- GMod's IsValid: a table is valid only through its own IsValid method.
IsValid=function(v) if not v then return false end local f=v.IsValid if not f then return false end return f(v) end
-- GMod's table.Copy: nil stays nil, anything but a table fails in pairs.
function table.Copy(t) if t==nil then return nil end if type(t)~='table' then error("bad argument #1 to 'pairs' (table expected, got "..type(t)..")") end
 local c={} for k,v in pairs(t) do c[k]=type(v)=='table' and table.Copy(v) or v end return c end
local encoded,serial={},0
function encode(t) serial=serial+1 local s='json'..serial encoded[s]=table.Copy(t) return s end
function decode(s) local t=encoded[s] return t and table.Copy(t) end
util={TableToJSON=encode,JSONToTable=decode}
FILES={} file={Read=function(path) return FILES[path] end,Exists=function(path) return FILES[path]~=nil end}
HOOKS={} hook={Add=function(e,n,f) HOOKS[e]=HOOKS[e] or {} HOOKS[e][n]=f end,Run=function() end}
TIMERS={} timer={Simple=function(_,f) TIMERS[#TIMERS+1]=f end}
function runTimers() local due=TIMERS TIMERS={} for _,f in ipairs(due) do f() end end
REPORTS={} ErrorNoHalt=function(s) REPORTS[#REPORTS+1]=s end MsgN=function() end
-- GMod's list (GetForEdit is the list itself) and player_manager (the player-model selector lists AllValidModels).
LISTS={NPC={},PlayerOptionsModel={}} list={Set=function(name,key,value) LISTS[name][key]=value end,GetForEdit=function(name) return LISTS[name] end}
PLAYERMODELS={} HANDS={} player_manager={AddValidModel=function(key,model) PLAYERMODELS[key]=model end,AddValidHands=function(key,model) HANDS[key]=model end,
 RemoveValidModel=function(key) PLAYERMODELS[key]=nil end,AllValidModels=function() return PLAYERMODELS end}

-- Entities: the engine's SetModel, which persistence.lua wraps.
ENTITY={}
FindMetaTable=function(name) assert(name=='Entity') return ENTITY end
function ENTITY:SetModel(path) self.model=path self.engineModels[#self.engineModels+1]=path end
function ENTITY:SetNoDraw(v) self.nodraw=v end
function ENTITY:GetModel() return self.model end function ENTITY:GetClass() return self.class end
function ENTITY:IsValid() return not self.removed end
function ENTITY:GetNW2String(k,d) local v=self.nw[k] if v==nil then return d end return v end
ENTITY.GetNW2Int=ENTITY.GetNW2String
function ENTITY:SetNW2String(k,v) self.nw[k]=v end ENTITY.SetNW2Int=ENTITY.SetNW2String
function ENTITY:GetPhysicsObjectCount() return 18 end function ENTITY:IsRagdoll() return self.class=='prop_ragdoll' end
function ENTITY:GetFlexScale() return 1 end function ENTITY:SetFlexScale() end
function entity(class) return setmetatable({class=class,nw={},engineModels={}},{__index=ENTITY}) end

-- Sandbox's duplicator: a generic entity is created and DoGeneric sets its model.
MODIFIERS={}
duplicator={CopyEntTable=function(e) return {Class=e.class,Model=e.model} end,
 CreateEntityFromTable=function(p,data) local e=entity(data.Class) duplicator.DoGeneric(e,data) return e end,
 DoGeneric=function(ent,data) ent:SetModel(data.Model) end,
 RegisterEntityModifier=function(name,f) MODIFIERS[name]=f end,
 ClearEntityModifier=function() end,StoreEntityModifier=function(e,name,state) e.stored=state end}

-- The module: the rigs on disk by model path; PrepareCarrier fits a new carrier.
A=string.rep('a',64)
function rig(generator,key,role)
 return {version=3,generator=generator,key=key,asset=A,role=role,name='Ganyu',scale=3.2,mass=70,morphs={},
  model='models/mmd/'..key:sub(1,16)..'/ganyu.mdl',materialGma='data/mmd_hotloader/assets/'..A..'/materials-v5.gma',animation={profile='citizen_female'}}
end
OLD=rig(30,string.rep('3',32),'ragdoll')        -- a 2.2 ragdoll
OLD_NPC=rig(30,string.rep('4',32),'citizen')    -- a 2.2 NPC
OLD_PLAYER=rig(30,string.rep('7',32),'player')  -- a 2.2 player model and its c_arms
OLD_ARMS=rig(30,string.rep('8',32),'arms')
OLDER=rig(29,string.rep('2',32),'ragdoll')      -- before 2.1.0-native.5
OLDER_NPC=rig(29,string.rep('1',32),'combine')
FUTURE=rig(32,string.rep('5',32),'ragdoll')     -- from a newer module
FRESH=rig(31,string.rep('6',32),'citizen')      -- what PrepareCarrier fits now
ALL={OLD,OLD_NPC,OLD_PLAYER,OLD_ARMS,OLDER,OLDER_NPC,FUTURE,FRESH}
for _,r in ipairs(ALL) do FILES['mmd_hotloader/rigs/'..r.key..'/rig.json']=encode(r) FILES['mmd_hotloader/rigs/'..r.key..'/carrier.gma']='GMAD' end
CAPS={api=1,rigVersion=3,rigGenerator=31,rigGeneratorMin=30}
PREPARED={}
mmdhl.native={GetCapabilities=function() return encode(CAPS) end,PrepareCarrier=function(id,options) PREPARED[#PREPARED+1]=decode(options) return encode(FRESH) end}
mmdhl.Decode=function(v,err) if v==nil then return nil,err end return decode(v) end
mmdhl.actorRegistrations={} mmdhl.rigs={}
mmdhl.GetRigForModel=function(model) for _,r in ipairs(ALL) do if r.model==model then return table.Copy(r) end end end
mmdhl.CanUseAsset=function() return true end
MOUNTED={} mmdhl.MountPackage=function(path) MOUNTED[path]=true return true end
ATTACHED={}
mmdhl.AttachNative=function(ent,id,options)
 local r=options.rigManifest ATTACHED[#ATTACHED+1]={ent=ent,rig=r}
 ent:SetNW2String('MMDHLAsset',id) ent:SetNW2String('MMDHLRig',r.key) mmdhl.rigs[r.key]=r return r
end
mmdhl.GetRig=function(ent) return mmdhl.rigs[ent:GetNW2String('MMDHLRig','')] end
mmdhl.IsMMD=function(ent) return ent:GetNW2String('MMDHLRig','')~='' end
mmdhl.GetMorphWeight=function() return 0 end mmdhl.CaptureMaterialState=function() return {} end
mmdhl.ActorOptions=function(o) return o end mmdhl.PublishRig=function() end
function loadActors(caps) CAPS=caps assert(load(ACTORS,'actors'))() end
''')

lua.execute(r'''
-- 2.3.0's module: generators 30 and 31 load; only 31 is the fit made now.
loadActors({api=1,rigVersion=3,rigGenerator=31,rigGeneratorMin=30})
assert(mmdhl.IsLoadableRig(OLD) and mmdhl.IsLoadableRig(FRESH),'a 2.2 or a current carrier is not loadable')
assert(not mmdhl.IsLoadableRig(OLDER) and not mmdhl.IsLoadableRig(FUTURE),'a generator-29 or a newer carrier is loadable')
local other=table.Copy(OLD) other.version=2 assert(not mmdhl.IsLoadableRig(other),'another rig version is loadable')
other=table.Copy(OLD) other.generator=nil assert(not mmdhl.IsLoadableRig(other) and not mmdhl.IsLoadableRig(nil) and not mmdhl.IsLoadableRig('x'))
assert(mmdhl.IsCurrentRig(FRESH) and not mmdhl.IsCurrentRig(OLD),'IsCurrentRig is not the fit made now')
-- 2.2.0's module has no rigGeneratorMin: its own generator only.
loadActors({api=1,rigVersion=3,rigGenerator=30})
assert(mmdhl.IsLoadableRig(OLD) and not mmdhl.IsLoadableRig(FRESH) and not mmdhl.IsLoadableRig(OLDER),'an older module loads other generators')
-- A module whose capabilities cannot be read loads nothing.
mmdhl.native.GetCapabilities=function() return nil end assert(load(ACTORS,'actors'))()
assert(not mmdhl.IsLoadableRig(OLD) and not mmdhl.IsLoadableRig(FRESH))
mmdhl.native.GetCapabilities=function() return encode(CAPS) end
loadActors({api=1,rigVersion=3,rigGenerator=31,rigGeneratorMin=30})
''')
print('PASS: a 2.3.0 module loads generators 30 and 31 and makes 31; an older module loads its own generator only')

lua.execute(r'''
assert(load(PERSISTENCE,'persistence'))()
local p={}
local function attachedTo(ent) for i=#ATTACHED,1,-1 do if ATTACHED[i].ent==ent then return ATTACHED[i].rig end end end
local function outdated(f) local ok,err=pcall(f) return not ok and tostring(err):find('Cached model carrier is missing or outdated',1,true)~=nil end

-- A 2.2 dupe or save of a ragdoll: pasted with its own carrier, nothing fitted.
local ragdoll=duplicator.CreateEntityFromTable(p,{Class='prop_ragdoll',Model=OLD.model})
runTimers()
assert(ragdoll.model==OLD.model and attachedTo(ragdoll).key==OLD.key and #PREPARED==0,'a 2.2 ragdoll dupe did not keep its carrier')
assert(MOUNTED['data/mmd_hotloader/rigs/'..OLD.key..'/carrier.gma'] and MOUNTED[OLD.materialGma] and #REPORTS==0,'the 2.2 carrier was not mounted, or an error was reported')
-- Its appearance modifier binds it too (a save restores it after the paste).
local bare=entity('prop_ragdoll') ENTITY.SetModel(bare,OLD.model) bare.nw={}
MODIFIERS.MMDHLNative(p,bare,{version=4,asset=A,rigKey=OLD.key,model=OLD.model,options={frozen=true}})
assert(attachedTo(bare).key==OLD.key and bare.stored and bare.stored.rigKey==OLD.key and #REPORTS==0,'the 2.2 appearance did not bind its carrier')

-- A 2.2 NPC keeps its NPC carrier; a 2.2 ragdoll pasted as an NPC gets a new fit.
local npc=duplicator.CreateEntityFromTable(p,{Class='npc_citizen',Model=OLD_NPC.model})
assert(npc.model==OLD_NPC.model and attachedTo(npc).key==OLD_NPC.key and #PREPARED==0,'a 2.2 NPC dupe did not keep its carrier')
local converted=duplicator.CreateEntityFromTable(p,{Class='npc_citizen',Model=OLD.model})
assert(#PREPARED==1 and PREPARED[1].role=='citizen' and PREPARED[1].rigManifest==nil,'a 2.2 ragdoll pasted as an NPC was not fitted again')
assert(converted.model==FRESH.model and attachedTo(converted).key==FRESH.key,'the NPC did not take the new fit')

-- A model that becomes a 2.2 carrier (a player model selection, a tool) binds it.
local player=entity('player') player:SetModel(OLD_PLAYER.model) runTimers()
assert(player.model==OLD_PLAYER.model and attachedTo(player).key==OLD_PLAYER.key and #REPORTS==0,'a 2.2 player model did not bind')

-- Carriers the module cannot load are still refused, as before.
for _,r in ipairs({OLDER,FUTURE}) do
 assert(outdated(function() duplicator.CreateEntityFromTable(p,{Class='prop_ragdoll',Model=r.model}) end),'a generator-'..r.generator..' dupe was pasted')
 local e=entity('prop_ragdoll') local reports=#REPORTS e:SetModel(r.model) runTimers()
 assert(attachedTo(e)==nil and #REPORTS>reports and REPORTS[#REPORTS]:find('outdated',1,true),'a generator-'..r.generator..' model was bound or not reported')
end
''')
print('PASS: dupes, saves and model selections of 2.2 ragdolls, NPCs and player models keep their carrier; an NPC made from a 2.2 ragdoll is fitted again; generators 29 and 32 are refused')

lua.execute(r'''
-- After a restart: the published 2.2 NPC and player model (with its c_arms) are registered again.
mmdhl.actorRegistrations={} LISTS={NPC={},PlayerOptionsModel={}} PLAYERMODELS={} HANDS={}
approved={assets={[A]={name='Ganyu'}},rigs={[OLD_NPC.key]={asset=A,role='citizen'},[OLD_PLAYER.key]={asset=A,role='player',arms=OLD_ARMS.key},
 [OLD_ARMS.key]={asset=A,role='arms'},[OLD.key]={asset=A,role='ragdoll'},[OLDER_NPC.key]={asset=A,role='combine'}}}
MOUNTED={}
assert(load(RESTORE,'sharing'))()
HOOKS.InitPostEntity['MMDHL.RestorePublishedActors']()
local npc,playerKey='mmd_'..OLD_NPC.key:sub(1,16),'mmd_'..OLD_PLAYER.key:sub(1,16)
assert(LISTS.NPC[npc] and LISTS.NPC[npc].Model==OLD_NPC.model and LISTS.NPC[npc].MMDHLRig==OLD_NPC.key,'the published 2.2 NPC was not registered')
assert(PLAYERMODELS[playerKey]==OLD_PLAYER.model and HANDS[playerKey]==OLD_ARMS.model and LISTS.PlayerOptionsModel[playerKey]==OLD_PLAYER.model,'the published 2.2 player model was not registered')
assert(mmdhl.actorRegistrations[OLD_PLAYER.model].arms.key==OLD_ARMS.key and MOUNTED['data/mmd_hotloader/rigs/'..OLD_PLAYER.key..'/carrier.gma'] and MOUNTED['data/mmd_hotloader/rigs/'..OLD_ARMS.key..'/carrier.gma'],'the 2.2 player model or its c_arms were not mounted')
assert(LISTS.NPC['mmd_'..OLDER_NPC.key:sub(1,16)]==nil and mmdhl.actorRegistrations[OLDER_NPC.model]==nil,'a generator-29 NPC was registered')
''')
print('PASS: published 2.2 NPC and player-model registrations come back after a restart; a generator-29 one does not')

lua.execute(r'''
-- The character spawned again from the library (mmdhl.SpawnNPC, SetPlayerModel): PublishActor
-- adds its 2.3.0 NPC (FRESH) and player model to approved.json beside the 2.2 ones. A 2.2
-- male NPC and a 2.2 Combine of the same character have no 2.3.0 counterpart yet.
NEW_PLAYER=rig(31,string.rep('9',32),'player') NEW_ARMS=rig(31,string.rep('b',32),'arms')
MALE_NPC=rig(30,string.rep('c',32),'citizen') MALE_NPC.animation={profile='citizen_male'}
OLD_COMBINE=rig(30,string.rep('d',32),'combine')
for _,r in ipairs({NEW_PLAYER,NEW_ARMS,MALE_NPC,OLD_COMBINE}) do FILES['mmd_hotloader/rigs/'..r.key..'/rig.json']=encode(r) FILES['mmd_hotloader/rigs/'..r.key..'/carrier.gma']='GMAD' end
for _,r in ipairs({FRESH,MALE_NPC,OLD_COMBINE}) do approved.rigs[r.key]={asset=A,role=r.role} end
approved.rigs[NEW_PLAYER.key]={asset=A,role='player',arms=NEW_ARMS.key} approved.rigs[NEW_ARMS.key]={asset=A,role='arms'}
local function key(r) return 'mmd_'..r.key:sub(1,16) end

-- After a restart the server registers both generations: 2.2 saves, dupes, spawn-menu
-- entries and player-model selections that name the 2.2 entry keep its carrier.
mmdhl.actorRegistrations={} LISTS={NPC={},PlayerOptionsModel={}} PLAYERMODELS={} HANDS={}
HOOKS.InitPostEntity['MMDHL.RestorePublishedActors']()
for _,r in ipairs({OLD_NPC,FRESH,MALE_NPC,OLD_COMBINE}) do assert(LISTS.NPC[key(r)] and mmdhl.actorRegistrations[r.model],'the server lost NPC '..r.key) end
assert(PLAYERMODELS[key(OLD_PLAYER)]==OLD_PLAYER.model and PLAYERMODELS[key(NEW_PLAYER)]==NEW_PLAYER.model and HANDS[key(NEW_PLAYER)]==NEW_ARMS.model,'the server lost a player model')
-- A joining client is sent one entry per character, role and gender: the newest generation.
net={Start=function() end,WriteString=function() end,Send=function() end,Broadcast=function() end}
SENT={} mmdhl.SendActorRegistration=function(r,a) SENT[r.key]=a and a.key or true end
assert(load(CATALOG,'catalog'))() catalog({})
assert(SENT[FRESH.key] and SENT[NEW_PLAYER.key]==NEW_ARMS.key and SENT[MALE_NPC.key] and SENT[OLD_COMBINE.key],'the catalog left out a current entry')
assert(SENT[OLD_NPC.key]==nil and SENT[OLD_PLAYER.key]==nil,'the catalog sent the 2.2 NPC or player model a 2.3.0 one replaces')
''')
print('PASS: after a library spawn and a restart the server keeps both generations; joining clients are sent the 2.3.0 NPC and player model only')

lua.execute(r'''
SERVER=false CLIENT=true
local function key(r) return 'mmd_'..r.key:sub(1,16) end
local function restart(caps) mmdhl.actorRegistrations={} LISTS={NPC={},PlayerOptionsModel={}} PLAYERMODELS={} HANDS={} loadActors(caps) end
local function npcs(model) local n=0 for _,entry in pairs(LISTS.NPC) do if entry.Model==model then n=n+1 end end return n end
local function citizens() local n=0 for _,entry in pairs(LISTS.NPC) do if entry.MMDHLAsset==A and entry.Class=='npc_citizen' then n=n+1 end end return n end
local function players() local n=0 for _ in pairs(player_manager.AllValidModels()) do n=n+1 end return n end
local function oneEach()
 assert(LISTS.NPC[key(OLD_NPC)]==nil and mmdhl.actorRegistrations[OLD_NPC.model]==nil,'the 2.2 NPC is still in the NPC tab')
 assert(npcs(FRESH.model)==1 and LISTS.NPC[key(FRESH)].MMDHLRig==FRESH.key,'the 2.3.0 NPC is not in the NPC tab')
 assert(LISTS.NPC[key(MALE_NPC)] and LISTS.NPC[key(OLD_COMBINE)] and citizens()==2,'another gender or role of the character was replaced')
 assert(PLAYERMODELS[key(OLD_PLAYER)]==nil and LISTS.PlayerOptionsModel[key(OLD_PLAYER)]==nil and mmdhl.actorRegistrations[OLD_PLAYER.model]==nil,'the 2.2 player model is still in the selector')
 assert(PLAYERMODELS[key(NEW_PLAYER)]==NEW_PLAYER.model and HANDS[key(NEW_PLAYER)]==NEW_ARMS.model and players()==1,'the selector does not show the 2.3.0 player model alone')
end
-- A client connected before the library spawn has the 2.2 entries from the catalog;
-- PublishActor's broadcast of the new ones replaces them.
restart(CAPS)
mmdhl.RegisterActor(OLD_NPC) mmdhl.RegisterActor(OLD_PLAYER,OLD_ARMS) mmdhl.RegisterActor(MALE_NPC) mmdhl.RegisterActor(OLD_COMBINE)
assert(npcs(OLD_NPC.model)==1 and PLAYERMODELS[key(OLD_PLAYER)]==OLD_PLAYER.model,'the 2.2 entries were not listed')
mmdhl.RegisterActor(FRESH) mmdhl.RegisterActor(NEW_PLAYER,NEW_ARMS)
oneEach()
-- A 2.2 entry whose download finishes later stays out; a language change keeps one each.
mmdhl.RegisterActor(OLD_NPC) mmdhl.RegisterActor(OLD_PLAYER,OLD_ARMS) oneEach()
for _,entry in pairs(mmdhl.actorRegistrations) do mmdhl.RegisterActor(entry.rig,entry.arms) end oneEach()
-- The other order (new entries first) gives the same menus.
restart(CAPS)
mmdhl.RegisterActor(FRESH) mmdhl.RegisterActor(NEW_PLAYER,NEW_ARMS) mmdhl.RegisterActor(MALE_NPC) mmdhl.RegisterActor(OLD_COMBINE)
mmdhl.RegisterActor(OLD_NPC) mmdhl.RegisterActor(OLD_PLAYER,OLD_ARMS) oneEach()
-- A client whose module loads only generator 30 (2.2.0's) cannot use the new ones and keeps the 2.2 entries.
restart({api=1,rigVersion=3,rigGenerator=30})
mmdhl.RegisterActor(OLD_NPC) mmdhl.RegisterActor(OLD_PLAYER,OLD_ARMS) mmdhl.RegisterActor(FRESH) mmdhl.RegisterActor(NEW_PLAYER,NEW_ARMS)
assert(npcs(OLD_NPC.model)==1 and LISTS.NPC[key(FRESH)]==nil and PLAYERMODELS[key(OLD_PLAYER)]==OLD_PLAYER.model and PLAYERMODELS[key(NEW_PLAYER)]==nil,'a 2.2.0 module lost its 2.2 entries')
''')
print('PASS: a client lists one NPC and one player model per character, role and gender: the 2.3.0 ones replace the 2.2 ones in either order; other genders and roles stay; a 2.2.0 module keeps its own')
