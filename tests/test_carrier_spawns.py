"""Native carrier spawns on the server: Sandbox's completion hooks count every
ragdoll and NPC exactly once (so its limits hold and recover on removal, spawn-menu
conversions included), new NPCs take the player's NPC health setting, and an entity
that changes model drops the previous model's networked overflow parts, submaterials
and expressions. Runs the addon's carrier.lua, actors.lua and materials.lua code
against a simulated Sandbox server."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition

root = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
SERVER=true CLIENT=false
mmdhl={rigs={},actorRegistrations={},CollideDefault=6,ValidCollisionFlags=function(v) return tonumber(v) end}
IsValid=function(e) return type(e)=='table' and not e.removed end
isnumber=function(v) return type(v)=='number' end
istable=function(v) return type(v)=='table' end
table.Copy=function(t) local c={} for k,v in pairs(t) do c[k]=type(v)=='table' and table.Copy(v) or v end return c end
util={TableToJSON=function(t) return table.Copy(t) end,JSONToTable=function(v) return type(v)=='table' and table.Copy(v) or nil end}
Vector=function(...) return {...} end Angle=function(...) return {...} end unpack=table.unpack
undo={Create=function() end,AddEntity=function() end,SetPlayer=function() end,Finish=function() end}
bit={bor=function(a,b) return a|b end}
EF_NODRAW=32 SF_CITIZEN_NOT_COMMANDABLE=1048576 D_LI=3 D_HT=1
HOOKS={} hook={Add=function(e,n,f) HOOKS[e]=HOOKS[e] or {} HOOKS[e][n]=f end,
 Run=function(e,...) for _,f in pairs(HOOKS[e] or {}) do local r=f(...) if r~=nil then return r end end end}
-- Sandbox: spawn checks against sbox_max*, completion hooks that AddCount, and a
-- count that drops when the entity is removed (gamemodes/sandbox/gamemode).
LIMIT=1 COUNT={ragdolls=0,npcs=0}
local function allowed(kind) return COUNT[kind]<LIMIT end
local function add(kind,ent) COUNT[kind]=COUNT[kind]+1 ent:CallOnRemove('GetCountUpdate',function() COUNT[kind]=COUNT[kind]-1 end) end
GAMEMODE={PlayerSpawnRagdoll=function(_,p) return allowed('ragdolls') end,PlayerSpawnedRagdoll=function(_,p,model,ent) add('ragdolls',ent) end,
 PlayerSpawnNPC=function(_,p) return allowed('npcs') end,PlayerSpawnedNPC=function(_,p,ent) add('npcs',ent) end}
gamemode={Call=function(name,...) local r=hook.Run(name,...) if r~=nil then return r end if GAMEMODE[name] then return GAMEMODE[name](GAMEMODE,...) end end}
-- Entities; like the engine, Remove takes effect at the end of the frame.
REMOVING={}
function endFrame() for _,e in ipairs(REMOVING) do e.removed=true for _,f in pairs(e.removers) do f(e) end end REMOVING={} end
function entity(class)
 local e={class=class,nw={},removers={},model='',keys={}}
 function e:GetClass() return self.class end
 function e:SetModel(m) self.model=m end function e:GetModel() return self.model end
 function e:SetPos() end function e:SetAngles() end function e:Activate() end
 -- Spawn sets the class's health from skill.cfg, as the engine does.
 function e:Spawn() self.health=({npc_citizen=40,npc_combine_s=50})[self.class] or 0 self.maxHealth=self.health end
 function e:SetHealth(h) self.health=h end function e:Health() return self.health end
 function e:SetMaxHealth(h) self.maxHealth=h end function e:GetMaxHealth() return self.maxHealth end
 local zero={Unpack=function() return 0,0,0 end} function e:GetPos() return zero end function e:GetAngles() return zero end
 function e:SetKeyValue(k,v) self.keys[k]=v end function e:GetSpawnFlags() return 0 end
 function e:SetCreator(p) self.creator=p end
 function e:AddRelationship() end function e:AddEntityRelationship() end
 function e:GetPhysicsObjectCount() return self.class=='prop_ragdoll' and 18 or 1 end
 function e:GetPhysicsObjectNum() return {EnableMotion=function() end,Wake=function() end} end
 function e:GetNoDraw() return false end function e:IsEffectActive() return false end
 for _,kind in ipairs({'String','Int','Bool','Float'}) do
  e['GetNW2'..kind]=function(self,k,default) local v=self.nw[k] if v==nil then return default end return v end
  e['SetNW2'..kind]=function(self,k,v) self.nw[k]=v end
 end
 function e:CallOnRemove(name,f) self.removers[name]=f end
 function e:Remove() REMOVING[#REMOVING+1]=self end
 return e
end
CREATED={} ents={Create=function(class) local e=entity(class) CREATED[#CREATED+1]=e return e end}
player={GetAll=function() return {} end}
-- A player and the client settings the server reads as userinfo.
INFO={}
function PLAYER() return {AddCleanup=function() end,GetInfoNum=function(_,name,default) local v=INFO[name] if v==nil then return default end return v end} end
-- Carriers: one rig per asset and role.
local function rig(key,asset,role,materials,model)
 local r={key=key,asset=asset,role=role,model=model,scale=40,gma='data/mmd_hotloader/rigs/'..key..'/carrier.gma',materials={},morphs={{native=-1,mmd=0},{native=0,mmd=1},{native=-1,mmd=2}}}
 for i=1,materials do r.materials[i]={name='part '..i} end
 return r
end
A,B=string.rep('a',64),string.rep('b',64)
RIGS={[A..':ragdoll']=rig('ragdollA',A,'ragdoll',40,'models/mmd/aaaaaaaaaaaaaaaa/ragdoll.mdl'),
 [A..':citizen']=rig('citizenA',A,'citizen',40,'models/mmd/aaaaaaaaaaaaaaa1/citizen.mdl'),
 [A..':combine']=rig('combineA',A,'combine',40,'models/mmd/aaaaaaaaaaaaaaa2/combine.mdl'),
 [A..':player']=rig('playerA',A,'player',40,'models/mmd/aaaaaaaaaaaaaaa3/player.mdl'),
 [B..':player']=rig('playerB',B,'player',45,'models/mmd/bbbbbbbbbbbbbbbb/player.mdl')}
for _,r in pairs(RIGS) do mmdhl.rigs[r.key]=r end
native={PrepareCarrier=function(id,options) return table.Copy(RIGS[id..':'..(options.role or 'ragdoll')]) end}
mmdhl.WithSpawnDefaults=function(p,o) return o end
mmdhl.MountPackage=function() return true end
mmdhl.InvalidateEntityList=function() end mmdhl.PublishRig=function() end mmdhl.PublishActor=function() end
mmdhl.ApplyBodygroupState=function() end mmdhl.MakeHostileCitizen=function(e) e.hostile=true end
mmdhl.GetRig=function(ent) return mmdhl.rigs[ent:GetNW2String('MMDHLRig','')] end
mmdhl.NPCWeapon=function(p,role,weapon) return weapon or 'weapon_smg1' end
mmdhl.HostileActorRole=function(weapon) return weapon=='weapon_pistol' and 'citizen' or 'combine' end
friendlyCitizen=function(e) e.friendly=true end save=function() end mmdhl.ChatPrint=function(_,m) error(m) end
-- The server's spawn entry for actors: hostile options, then the native carrier.
mmdhl.Spawn=function(p,id,options,done)
 if options.role=='combine' then options.role=mmdhl.HostileActorRole(options.weapon) options.hostile=true end
 return mmdhl.SpawnActorNative(p,id,options,native.PrepareCarrier(id,options),done)
end
''')
attach(lua)
lua.execute('L=mmdhl.L')
carrier = (root / 'addon/lua/mmdhl/carrier.lua').read_text(encoding='utf-8')
actors = (root / 'addon/lua/mmdhl/actors.lua').read_text(encoding='utf-8')
materials = (root / 'addon/lua/mmdhl/materials.lua').read_text(encoding='utf-8')
lua.execute(definition(lua, materials, 'local function changed(') + definition(lua, materials, 'function mmdhl.ClearAssetState('))
for header in [' function mmdhl.AttachNative(', ' function mmdhl.SpawnNative(']: lua.execute(definition(lua, carrier, header))
lua.execute(definition(lua, actors, 'mmdhl.MaxNPCHealth=') + definition(lua, actors, 'function mmdhl.NPCHealth('))
# A local of the server block, like friendlyCitizen: run it as a global.
lua.execute(definition(lua, actors, ' local function setHealth(').replace('local function', 'function', 1))
for header in [' function mmdhl.ClearActorIdentity(', ' function mmdhl.SpawnActorNative(', " hook.Add('PlayerSpawnedNPC','MMDHL.AttachMenuNPC',"]:
    lua.execute(definition(lua, actors, header))

lua.execute(r'''
local p=PLAYER()
-- The ragdoll spawn action checks the limit first (server.lua), then spawns.
local function ragdoll() if gamemode.Call('PlayerSpawnRagdoll',p,A)==false then return nil end return mmdhl.SpawnNative(p,A,{role='ragdoll'}) end
local first=ragdoll()
assert(first and first.creator==p and COUNT.ragdolls==1,'a native ragdoll counts toward the Sandbox ragdoll limit')
assert(ragdoll()==nil and COUNT.ragdolls==1,'a second ragdoll is refused at a limit of one')
first:Remove() endFrame()
assert(COUNT.ragdolls==0 and ragdoll() and COUNT.ragdolls==1,'removing the ragdoll frees its place')
''')
print('PASS: native ragdolls count toward the Sandbox limit once and free it on removal')

lua.execute(r'''
local p=PLAYER()
local function npc(weapon) if gamemode.Call('PlayerSpawnNPC',p,'npc_citizen',weapon)==false then return nil end
 return mmdhl.Spawn(p,A,{role='citizen',weapon=weapon}) end
local first=npc('weapon_smg1')
assert(first and COUNT.npcs==1,'a native NPC counts toward the Sandbox NPC limit')
assert(first:GetNW2Int('MMDHLGeneration',0)==1 and first.friendly,'the NPC is attached once, not again by the spawn-menu handler')
assert(npc('weapon_smg1')==nil and COUNT.npcs==1,'a second NPC is refused at a limit of one')
first:Remove() endFrame()
assert(COUNT.npcs==0,'removing the NPC frees its place')
-- A spawn-menu Combine entry with a pistol: Sandbox created and counts the menu
-- NPC; the addon replaces it with a hostile citizen, which counts in its place.
for _,r in pairs(RIGS) do mmdhl.actorRegistrations[r.model]={rig=r} end
local menu=entity('npc_combine_s') menu:SetModel(RIGS[A..':combine'].model) menu.Equipment='weapon_pistol'
gamemode.Call('PlayerSpawnedNPC',p,menu) endFrame()
local replacement=CREATED[#CREATED]
assert(menu.removed and replacement.class=='npc_citizen' and COUNT.npcs==1,'a converted spawn-menu NPC is counted once, as its replacement')
assert(replacement.hostile and not replacement.friendly and replacement:GetNW2Int('MMDHLGeneration',0)==1,'the hostile replacement is attached once and stays hostile')
''')
print('PASS: native NPCs count once, spawn-menu conversions included, and free their place on removal')

lua.execute(r'''
LIMIT=100 local p=PLAYER()
local function npc(options) options.role=options.role or 'citizen' return mmdhl.Spawn(p,A,options) end
local function health(e) return e:Health()..'/'..e:GetMaxHealth() end
-- The setting's default (0) keeps the class's own health.
local citizen,combine=npc({}),npc({role='combine',weapon='weapon_ar2'})
assert(health(citizen)=='40/40' and combine.class=='npc_combine_s' and health(combine)=='50/50','with NPC health at 0 the class keeps its health')
-- The player's setting is the new NPCs' health and maximum health, friendly or hostile.
INFO.mmdhl_npc_health=250
assert(health(npc({}))=='250/250','a friendly NPC takes the NPC health setting')
local hostile=npc({role='combine',weapon='weapon_pistol'})
assert(hostile.class=='npc_citizen' and hostile.hostile and health(hostile)=='250/250','a hostile citizen takes it')
assert(health(npc({role='combine',weapon='weapon_ar2'}))=='250/250','a Combine Soldier takes it')
-- A spawn's own value wins, 0 included; it is whole and at most mmdhl.MaxNPCHealth.
assert(health(npc({npcHealth=0}))=='40/40' and health(npc({npcHealth=75}))=='75/75','an explicit npcHealth overrides the setting')
assert(health(npc({npcHealth=99999}))=='10000/10000' and health(npc({npcHealth=120.8}))=='120/120' and health(npc({npcHealth=0/0}))=='40/40','npcHealth is clamped')
INFO.mmdhl_npc_health=1e9 assert(health(npc({}))=='10000/10000','the setting is clamped too')
INFO.mmdhl_npc_health=-5 assert(health(npc({}))=='40/40','a negative setting keeps the class health')
-- Spawn-menu entries (Sandbox spawned the NPC, then calls PlayerSpawnedNPC) take it,
-- the hostile citizen that replaces a Combine entry holding a pistol included.
INFO.mmdhl_npc_health=300
local menu=entity('npc_citizen') menu:SetModel(RIGS[A..':citizen'].model) menu:Spawn()
gamemode.Call('PlayerSpawnedNPC',p,menu)
assert(health(menu)=='300/300' and menu.friendly,'a spawn-menu NPC takes the setting')
local entry=entity('npc_combine_s') entry:SetModel(RIGS[A..':combine'].model) entry.Equipment='weapon_pistol' entry:Spawn()
gamemode.Call('PlayerSpawnedNPC',p,entry) endFrame()
local replacement=CREATED[#CREATED]
assert(entry.removed and replacement.hostile and health(replacement)=='300/300','the hostile replacement of a menu entry takes it')
-- Sandbox's NPC duplicator calls PlayerSpawnedNPC, then restores the dupe's
-- CurHealth/MaxHealth: a pasted NPC keeps the health it was saved with.
local pasted=entity('npc_citizen') pasted:SetModel(RIGS[A..':citizen'].model) pasted:Spawn()
gamemode.Call('PlayerSpawnedNPC',p,pasted) pasted:SetHealth(17) pasted:SetMaxHealth(90)
assert(health(pasted)=='17/90','a pasted NPC keeps its saved health')
-- Ragdolls are left alone.
assert(mmdhl.SpawnNative(p,A,{role='ragdoll'}):Health()==0,'a ragdoll was given the NPC health')
INFO.mmdhl_npc_health=nil
''')
print('PASS: new NPCs, from the library or the spawn menu, take the NPC health setting or their own npcHealth; 0 keeps the class health; dupes keep theirs')

lua.execute(r'''
-- Model A's overflow state: part 34 (bodygroup 35) hidden, a submaterial on slot 35,
-- and two expressions without flex controllers.
local e=entity('player')
local function attachTo(r) return mmdhl.AttachNative(e,r.asset,{role='player',rigManifest=r}) end
local function dirty()
 e:SetNW2Int('MMDHLPart35',1) e:SetNW2String('MMDHLMaterial35','custom/old_model')
 e:SetNW2Float('MMDHLMorph0',.7) e:SetNW2Float('MMDHLMorph2',.4)
end
local function clean() return e:GetNW2Int('MMDHLPart35',0)==0 and e:GetNW2String('MMDHLMaterial35','')=='' and e:GetNW2Float('MMDHLMorph0',0)==0 and e:GetNW2Float('MMDHLMorph2',0)==0 end
attachTo(RIGS[A..':player']) dirty()
attachTo(RIGS[A..':player'])
assert(e:GetNW2Int('MMDHLPart35',0)==1 and e:GetNW2String('MMDHLMaterial35','')=='custom/old_model' and e:GetNW2Float('MMDHLMorph2',0)==.4,'reattaching the same model keeps its parts')
attachTo(RIGS[B..':player'])
assert(clean(),"model B does not inherit model A's overflow parts, submaterials or expressions")
assert(e:GetNW2Int('MMDHLBodygroupsRevision35',0)>0 and e:GetNW2Int('MMDHLMaterialsRevision35',0)>0,'clients drop local overrides of the cleared slots')
attachTo(RIGS[A..':player']) dirty()
mmdhl.ClearActorIdentity(e)
assert(clean() and e:GetNW2String('MMDHLAsset','')=='','switching to a stock model clears them too')
attachTo(RIGS[B..':player'])
assert(clean(),'model A, a stock model, then model B: nothing of A remains')
''')
print('PASS: model changes clear the previous model\'s overflow parts, submaterials and expressions; same-model reattachment keeps them')
