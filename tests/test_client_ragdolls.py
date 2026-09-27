"""Local-corpse sleep/cleanup boundaries and native quality transitions."""
from pathlib import Path
from lupa import LuaRuntime
root=Path(__file__).resolve().parents[1]
lua=LuaRuntime(unpack_returned_tuples=True)
lua.execute('''
mmdhl={PhysicsSettingDefaults={}}
IsValid=function(e)return type(e)=='table' and not e.removed end
mmdhl.IsMMD=function(e)return e.mmd end
concommand={Add=function()end}notification={AddLegacy=function()end}
hook={Add=function()end}
local entities={}
mmdhl.Entities=function()return entities end
mmdhl.ReleasePresentation=function(e)e.released=true end
mmdhl.InvalidateEntityList=function()end
function body(sleep)return {IsAsleep=function()return sleep end}end
function entity(index,corpse,bodies)
 local e={index=index,MMDHLCorpse=corpse,bodies=bodies,mmd=true}
 function e:EntIndex()return self.index end
 function e:IsRagdoll()return true end
 function e:GetPhysicsObjectCount()return #self.bodies end
 function e:GetPhysicsObjectNum(i)return self.bodies[i+1] end
 function e:Remove()assert(self.released,'World must be released first')self.removed=true end
 function e:CallOnRemove()end
 entities[#entities+1]=e return e
end
''')
lua.execute((root/'addon/lua/mmdhl/client_ragdolls.lua').read_text(encoding='utf8'))
lua.execute('''
sleeping=entity(-1,true,{body(true),body(true)})
awake=entity(-1,true,{body(true),body(false)})
server=entity(15,false,{body(true),body(true)})
preview=entity(-1,true,{body(true)})preview.MMDHLEditorPreview=true
unready=entity(-1,true,{})
assert(mmdhl.ClientRagdollAsleep(sleeping))
assert(not mmdhl.ClientRagdollAsleep(awake),'One awake limb must resume physics')
assert(not mmdhl.ClientRagdollAsleep(server),'Server ragdoll was suspended')
assert(not mmdhl.ClientRagdollAsleep(preview))
assert(not mmdhl.ClientRagdollAsleep(unready))
assert(mmdhl.RegisterClientRagdoll(sleeping))
assert(mmdhl.RegisterClientRagdoll(awake))
assert(sleeping.MMDHLClientRagdollId~=awake.MMDHLClientRagdollId)
assert(not mmdhl.RegisterClientRagdoll(server))
enabled=0
function GetConVar(name)return {GetInt=function()return 10 end,
 GetFloat=function()if name=='mmdhl_lod_enabled' then return enabled else return 1 end end}end
CreateClientConVar=function()end FrameNumber=function()return 1 end RealTime=function()return 10 end
mmdhl.GetInstance=function()return 7 end
calls={};mmdhl.native={SetSecondaryQuality=function(h,d,p)calls[#calls+1]={h,d,p};return true end}
''')
lua.execute((root/'addon/lua/mmdhl/physics_lod.lua').read_text(encoding='utf8'))
lua.execute('''
mmdhl.UpdatePhysicsLOD(sleeping,{})
assert(calls[1][3] and sleeping.MMDPhysicsLOD.reason=='sleeping client ragdoll')
sleeping.bodies[2]=body(false)
mmdhl.UpdatePhysicsLOD(sleeping,{})
assert(not calls[2][3] and sleeping.MMDPhysicsLOD.iterations==10)
assert(mmdhl.CleanupClientRagdolls()==3)
assert(sleeping.removed and awake.removed and unready.removed)
assert(not server.removed and not preview.removed)
''')
print('PASS: all-body sleep, awake limb, native suspension/resume, local-only cleanup')
