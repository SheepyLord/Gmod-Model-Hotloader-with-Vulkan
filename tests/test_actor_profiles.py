"""The proportions must subtract the animation pack, retaining actor attachments/IK.
An addon's replacement of a pack or donor that the carrier cannot use (a retargeted
skeleton without fingers, IK on other bones, an unreadable model) gives way to the
game's own copy ('MOD')."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach

root=Path(__file__).resolve().parents[1]
code=(root/'addon/lua/mmdhl/actors.lua').read_text(encoding='utf8')
lua=LuaRuntime()
lua.execute('''
mmdhl={}
isstring=function(v) return type(v)=='string' end
table.Copy=function(t) local out={} for k,v in pairs(t)do out[k]=type(v)=='table' and table.Copy(v) or v end return out end
-- A file's "bytes" name the copy that was read: path|GAME (with addons) or path|MOD.
file={Read=function(path,id) return path..'|'..(id or 'GAME') end}
util={JSONToTable=function(v)return v end}
-- Stock skeletons are ValveBiped. REPLACED[path|id] swaps a copy for an addon's:
-- 'retargeted' (no finger bones), 'foreign_ik' (IK chains on other bones) or 'broken'.
REPLACED={}
local names={'Pelvis','Spine','Spine1','Spine2','Spine4','Neck1','Head1'}
for _,side in ipairs({'L','R'}) do
 for _,part in ipairs({'Clavicle','UpperArm','Forearm','Hand','Thigh','Calf','Foot','Toe0'}) do names[#names+1]=side..'_'..part end
 for _,finger in ipairs({'0','01','02','1','11','12','2','21','22','3','31','32','4','41','42'}) do names[#names+1]=side..'_Finger'..finger end
end
local function read(bytes)
 local kind=REPLACED[bytes] if kind=='broken' then return nil,'Animation reference must be a complete Source v44 or v46-v49 model' end
 local bones={} for _,name in ipairs(names) do if kind~='retargeted' or not name:find('Finger') then bones[#bones+1]={name='ValveBiped.Bip01_'..name,from=bytes} end end
 if kind=='retargeted' then for i=1,30 do bones[#bones+1]={name='MW3_Rigged.finger'..i,from=bytes} end end
 local knee=kind=='foreign_ik' and 'j_knee_le' or 'ValveBiped.Bip01_L_Calf'
 return {sha256=bytes,bones=bones,attachments={bytes},ikChains={{from=bytes,links={{bone='ValveBiped.Bip01_L_Thigh'},{bone=knee},{bone='ValveBiped.Bip01_L_Foot'}}}}}
end
native={ReadAnimationModel=read} mmdhl.Decode=function(v)return v end
''')
attach(lua)
lua.execute('L=mmdhl.L')
lua.execute(code[code.index('mmdhl.actorProfiles='):code.index('mmdhl.actorRegistrations=')])
# Each case starts with an empty reference cache, as a new game session does.
lua.globals().ACTORS = lua.eval('function(source) return load(source) end')(code[code.index(' local references={}'):code.index(' function mmdhl.PublishActor')])
lua.execute('''
ACTORS()
for _,role in ipairs({'citizen','combine','player','arms'})do for _,gender in ipairs({'female','male'})do
 local options,err=mmdhl.ActorOptions({role=role,gender=gender}) assert(options,err)
 local mesh=mmdhl.actorProfiles[role][gender]
 local ref=mmdhl.actorAnimationReferences[role] and mmdhl.actorAnimationReferences[role][gender] or mesh
 assert(options.animationReference.bones[1].from==ref..'|GAME','Proportion subtraction used a mesh bind')
 assert(options.animationReference.referenceHash==ref..'|GAME' and options.animationReference.referenceSource==ref)
 assert(options.animationReference.attachments[1]==mesh..'|GAME' and options.animationReference.ikChains[1].from==mesh..'|GAME','Actor attachment/IK metadata lost')
 assert(options.animationSource==mesh)
end end
-- A ragdoll takes the Citizen reference; its carrier adds the player and Citizen packs.
for _,gender in ipairs({'female','male'})do
 local options=mmdhl.ActorOptions({role='ragdoll',gender=gender})
 assert(options.role=='ragdoll' and options.gender==gender)
 assert(options.animationSource==mmdhl.actorProfiles.citizen[gender],'Ragdoll lost the Citizen donor')
 assert(options.animationReference.bones[1].from==mmdhl.actorAnimationReferences.citizen[gender]..'|GAME','Ragdoll proportions use a mesh bind')
end
assert(mmdhl.ActorOptions({role='ragdoll'}).gender=='female')
-- Without a readable reference a ragdoll still spawns, as before; an actor cannot.
local read=file.Read file.Read=function(path,id) if path=='models/missing.mdl' then return nil end return read(path,id) end
local saved=mmdhl.actorProfiles.citizen.female mmdhl.actorProfiles.citizen.female='models/missing.mdl'
local plain=mmdhl.ActorOptions({role='ragdoll',animationSource='stale',animationReference={}})
assert(plain and plain.animationReference==nil and plain.animationSource==nil,'Ragdoll without a reference failed or kept a stale one')
assert(mmdhl.ActorOptions({role='citizen'})==nil,'Citizen without its donor was accepted')
mmdhl.actorProfiles.citizen.female=saved file.Read=read
assert(mmdhl.ActorOptions({role='invalid'})==nil)
''')
print('PASS: Citizen, Combine, male/female player and ragdoll animation references; actor attachments/IK preserved')

lua.execute('''
local shared,fanm,donor='models/humans/female_shared.mdl','models/f_anm.mdl','models/Humans/Group01/Female_01.mdl'
-- An addon replaces the female Citizen pack with a retargeted one (no finger bones):
-- NPCs and ragdolls take the proportions from the game's own copy; its animations
-- still play from the replacement (the carrier includes the pack by path).
REPLACED={[shared..'|GAME']='retargeted'} ACTORS()
for _,role in ipairs({'citizen','ragdoll'}) do
 local options,err=mmdhl.ActorOptions({role=role,gender='female'}) assert(options,err)
 local ref=options.animationReference
 assert(ref.bones[1].from==shared..'|MOD' and ref.referenceHash==shared..'|MOD' and ref.referenceSource==shared,role..' kept the replaced skeleton')
 assert(ref.ikChains[1].from==donor..'|GAME' and options.animationSource==donor,role..' lost its donor')
end
assert(mmdhl.ActorOptions({role='citizen',gender='male'}).animationReference.bones[1].from=='models/humans/male_shared.mdl|GAME','the male pack was not replaced')
-- The same for a replaced player pack, and an unreadable replacement.
REPLACED={[fanm..'|GAME']='retargeted',[shared..'|GAME']='broken'} ACTORS()
assert(mmdhl.ActorOptions({role='player',gender='female'}).animationReference.bones[1].from==fanm..'|MOD')
assert(mmdhl.ActorOptions({role='citizen',gender='female'}).animationReference.bones[1].from==shared..'|MOD')
-- A replaced donor whose IK chains name other bones gives way to the game's own too.
REPLACED={[donor..'|GAME']='foreign_ik'} ACTORS()
local options=mmdhl.ActorOptions({role='citizen',gender='female'})
assert(options.animationReference.ikChains[1].from==donor..'|MOD' and options.animationReference.attachments[1]==donor..'|MOD')
assert(options.animationReference.bones[1].from==shared..'|GAME','the pack itself was not replaced')
-- A folder addon replaces the game's own copy as well: actors report it, ragdolls
-- spawn without the animation packs, as without a reference.
REPLACED={[shared..'|GAME']='retargeted',[shared..'|MOD']='retargeted'} ACTORS()
local none,err=mmdhl.ActorOptions({role='citizen',gender='female'})
assert(none==nil and mmdhl.Localize(err)=='An addon replaces '..shared..' with a model whose skeleton these characters cannot use, and the game\\u{2019}s own copy cannot be read. Disable that addon to spawn them.',tostring(mmdhl.Localize(err)))
local plain=mmdhl.ActorOptions({role='ragdoll',gender='female'})
assert(plain and plain.animationReference==nil,'a ragdoll failed on a replaced pack')
REPLACED={} ACTORS()
''')
print('PASS: replaced animation packs and donors the carrier cannot use give way to the game\'s own copies')
