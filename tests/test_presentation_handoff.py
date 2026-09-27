"""A same-update corpse handoff must precede hidden-source cleanup."""
from pathlib import Path
from lupa import LuaRuntime
from lua_source import definition
root=Path(__file__).resolve().parents[1]
lua=LuaRuntime(unpack_returned_tuples=True)
lua.execute('''
CLIENT=true
EF_NODRAW=32
IsValid=function(e)return type(e)=='table' and not e.removed end
local callbacks={};hook={Add=function(_,name,f)callbacks[name]=f end,Remove=function()end}
hooks=callbacks;timer={Simple=function(_,f)f()end}
util={TableToJSON=function(v)return v end}
local nextHandle=0
created=0 destroyed=0 rebound=0
mmdhl={native={},assets={}}
mmdhl.native.RequestAsset=function()end
mmdhl.native.AssetInfo=function()return {bones=1}end
mmdhl.native.CreateInstance=function()nextHandle=nextHandle+1;created=created+1;return nextHandle end
mmdhl.native.DestroyInstance=function()destroyed=destroyed+1 end
mmdhl.native.RemoveSourceShadow=function()end
mmdhl.native.RebindSourceEntity=function()rebound=rebound+1;return true end
mmdhl.Decode=function(v)return v end
mmdhl.RenderAvailable=function()return true end
mmdhl.GetAsset=function(e)return e.rig.asset end
mmdhl.GetRig=function(e)return e.rig end
mmdhl.MountPackage=function()return true end
mmdhl.GetGlobalSettings=function()return {secondaryCollision=2}end
mmdhl.IsMMD=function(e)return IsValid(e) and e.rig~=nil end
-- mmdhl.PresentationSuppressed/PresentationReleased are carrier.lua's own, loaded below.
mmdhl.GetInstance=function(e)return e.MMDHLClientInstance or 0 end
mmdhl.InvalidateEntityList=function()end
RealTime=function()return 10 end
local rig={key='rig',asset='asset',model='model',role='citizen',materialGma='materials'}
function entity(id)
 local e={id=id,rig=rig,generation=1,callbacks={}}
 function e:IsPlayer()return false end
 function e:Alive()return true end
 function e:EntIndex()return self.id end
 -- SetNoDraw sets EF_NODRAW; hidden marks a local hide. Transmission changes arrive
 -- through the NotifyShouldTransmit hook, so the stubs never report dormancy.
 function e:GetNoDraw()return self.hidden==true end
 function e:IsEffectActive(effect)return effect==EF_NODRAW and self.hidden==true end
 function e:IsDormant()return false end
 function e:GetNW2Bool(_,default)return default end
 function e:GetClass()return self.id==10 and 'npc_citizen' or 'prop_ragdoll' end
 function e:GetNW2Int(k,default)if k=='MMDHLGeneration' then return self.generation end return default end
 function e:GetNW2Entity()return self.former end
 function e:GetModel()return self.rig.model end
 function e:LookupBone()return 0 end
 function e:InvalidateBoneCache()end
 function e:CallOnRemove(k,f)self.callbacks[k]=f end
 return e
end
source=entity(10);corpse=entity(11);corpse.former=source
corpse.MMDHLFormerGeneration=1
mmdhl.Entities=function()return {source,corpse}end
''')
# The presentation gates instances.lua consults: PresentationSuppressed through
# PresentationReleased, with the local-hide grace period declared between them.
carrier=(root/'addon/lua/mmdhl/carrier.lua').read_text(encoding='utf8')
released=definition(lua,carrier,'function mmdhl.PresentationReleased(')
lua.execute(carrier[carrier.index('function mmdhl.PresentationSuppressed('):carrier.index(released)+len(released)])
lua.execute((root/'addon/lua/mmdhl/instances.lua').read_text(encoding='utf8'))
lua.execute('''
assert(mmdhl.AttachPresentation(source))
proxy={MMDOwner=source,Remove=function(self)self.removed=true end,SetNoDraw=function()end}
source.MMDHLVisual=proxy
corpse.rig={key='other',asset='asset'}
assert(not mmdhl.TransferPresentation(source,corpse),'Different rig reused the old world')
corpse.rig=source.rig
hooks['MMDHL.ClientVisibility'](source,false)
assert(destroyed==0,'Transmission loss destroyed the pending handoff')
mmdhl.UpdateClientInstances()
assert(created==1 and destroyed==0 and rebound==1,'Death unnecessarily rebuilt its world')
assert(not source.MMDHLClientInstance and corpse.MMDHLClientInstance==1)
assert(corpse.MMDHLVisual==proxy and proxy.MMDOwner==corpse and not proxy.removed)
source.callbacks['MMDHL.ClientInstance'](source)
assert(not proxy.removed and destroyed==0,'Former actor removal killed the corpse renderer')
corpse.callbacks['MMDHL.ClientInstance'](corpse)
assert(destroyed==1 and proxy.removed,'Corpse cleanup leaked the transferred resources')
''')
print('PASS: deferred transmission release, compatible handoff, proxy ownership and cleanup')
