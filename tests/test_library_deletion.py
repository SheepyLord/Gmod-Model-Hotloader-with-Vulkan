"""Deleting a library model removes every character that shows it first: server
entities through the server, client-only corpses (entity index -1, which the
server cannot address) locally. Runs library.lua's deletion and client_ragdolls.lua
against a simulated client."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach
from lua_source import definition

root = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
CLIENT=true
mmdhl={library={entries={}},editorPreviews={},assets={},rigs={},sharedAssets={}}
IsValid=function(e) return type(e)=='table' and not e.removed end
isstring=function(v) return type(v)=='string' end
concommand={Add=function() end} hook={Add=function() end}
game={SinglePlayer=function() return true end}
NOW=0 RealTime=function() return NOW end
TIMERS={} timer={Create=function(name,_,_,f) TIMERS[name]=f end,Remove=function(name) TIMERS[name]=nil end}
net={Start=function() end,WriteString=function() end,SendToServer=function() end}
util={TableToJSON=function(t) return t end}
mmdhl.Decode=function(v) return v end
ASSET=string.rep('a',64)
DELETED={} native={DeleteAssets=function(ids) for _,id in ipairs(ids) do DELETED[#DELETED+1]=id end return {pendingFiles=0} end}
mmdhl.native=native
mmdhl.library.entries[ASSET]={id=ASSET}
mmdhl.library.Refresh=function() end
-- Two characters of the model: a server ragdoll, removed when the server acts on
-- the request, and a client-only corpse left by a dead actor.
SENT={} local entities={}
mmdhl.Entities=function() local out={} for _,e in ipairs(entities) do if IsValid(e) then out[#out+1]=e end end return out end
mmdhl.IsMMD=function(e) return IsValid(e) end
mmdhl.GetAsset=function(e) return e.asset end
mmdhl.ReleasePresentation=function(e) e.released=true end
mmdhl.InvalidateEntityList=function() end
mmdhl.Action=function(action,_,ent) SENT[#SENT+1]={action=action,index=ent:EntIndex()} if ent:EntIndex()>0 then ent.removed=true end end
local function entity(index,corpse)
 local e={index=index,MMDHLCorpse=corpse,asset=ASSET}
 function e:EntIndex() return self.index end
 function e:IsRagdoll() return true end
 function e:CallOnRemove() end
 function e:Remove() self.removed=true end
 entities[#entities+1]=e return e
end
server=entity(42,false) corpse=entity(-1,true)
''')
attach(lua)
lua.execute((root / 'addon/lua/mmdhl/client_ragdolls.lua').read_text(encoding='utf-8'))
library = (root / 'addon/lua/mmdhl/library.lua').read_text(encoding='utf-8')
lua.execute('local native,L,library=mmdhl.native,mmdhl.L,mmdhl.library local function validId(id) return isstring(id) and #id==64 and not id:find("[^a-f0-9]") end '
            + definition(lua, library, 'function library.Delete('))
lua.execute(r'''
local result
mmdhl.library.Delete({ASSET},function(ok,message) result={ok=ok,message=message} end)
assert(#SENT==1 and SENT[1].index==42,'only the server ragdoll is sent to the server')
assert(corpse.removed and corpse.released,'the client-only corpse is released and removed locally')
TIMERS['MMDHL.DeleteModels']()
assert(result and result.ok and DELETED[1]==ASSET,'the model is deleted without waiting for a corpse the server cannot remove')
''')
print('PASS: library deletion removes client-only corpses locally and server ragdolls through the server')
