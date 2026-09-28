"""The proportions must subtract the animation pack, retaining actor attachments/IK."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach

root=Path(__file__).resolve().parents[1]
code=(root/'addon/lua/mmdhl/actors.lua').read_text(encoding='utf8')
lua=LuaRuntime()
lua.execute('''
mmdhl={}
table.Copy=function(t) local out={} for k,v in pairs(t)do out[k]=type(v)=='table' and table.Copy(v) or v end return out end
file={Read=function(path)return path end}
util={JSONToTable=function(v)return v end}
local function read(path)return {sha256=path,bones={path},attachments={path},ikChains={path}}end
native={ReadAnimationModel=read} mmdhl.Decode=function(v)return v end
''')
attach(lua)
lua.execute('L=mmdhl.L')
lua.execute(code[code.index('mmdhl.actorProfiles='):code.index('mmdhl.actorRegistrations=')])
lua.execute(code[code.index(' local references={}'):code.index(' function mmdhl.PublishActor')])
lua.execute('''
for _,role in ipairs({'citizen','combine','player','arms'})do for _,gender in ipairs({'female','male'})do
 local options,err=mmdhl.ActorOptions({role=role,gender=gender}) assert(options,err)
 local mesh=mmdhl.actorProfiles[role][gender]
 local ref=mmdhl.actorAnimationReferences[role] and mmdhl.actorAnimationReferences[role][gender] or mesh
 assert(options.animationReference.bones[1]==ref,'Proportion subtraction used a mesh bind')
 assert(options.animationReference.referenceHash==ref)
 assert(options.animationReference.attachments[1]==mesh and options.animationReference.ikChains[1]==mesh,'Actor attachment/IK metadata lost')
 assert(options.animationSource==mesh)
end end
-- A ragdoll takes the Citizen reference; its carrier adds the player and Citizen packs.
for _,gender in ipairs({'female','male'})do
 local options=mmdhl.ActorOptions({role='ragdoll',gender=gender})
 assert(options.role=='ragdoll' and options.gender==gender)
 assert(options.animationSource==mmdhl.actorProfiles.citizen[gender],'Ragdoll lost the Citizen donor')
 assert(options.animationReference.bones[1]==mmdhl.actorAnimationReferences.citizen[gender],'Ragdoll proportions use a mesh bind')
end
assert(mmdhl.ActorOptions({role='ragdoll'}).gender=='female')
-- Without a readable reference a ragdoll still spawns, as before; an actor cannot.
local read=file.Read file.Read=function(path) if path=='models/missing.mdl' then return nil end return read(path) end
local saved=mmdhl.actorProfiles.citizen.female mmdhl.actorProfiles.citizen.female='models/missing.mdl'
local plain=mmdhl.ActorOptions({role='ragdoll',animationSource='stale',animationReference={}})
assert(plain and plain.animationReference==nil and plain.animationSource==nil,'Ragdoll without a reference failed or kept a stale one')
assert(mmdhl.ActorOptions({role='citizen'})==nil,'Citizen without its donor was accepted')
mmdhl.actorProfiles.citizen.female=saved file.Read=read
assert(mmdhl.ActorOptions({role='invalid'})==nil)
''')
print('PASS: Citizen, Combine, male/female player and ragdoll animation references; actor attachments/IK preserved')
