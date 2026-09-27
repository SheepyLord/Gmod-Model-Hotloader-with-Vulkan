"""Invisible animation helpers retain metadata but must not own a visual world."""
from pathlib import Path
from lupa import LuaRuntime

root = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute('''
CLIENT=true SERVER=false EF_NODRAW=32
mmdhl={native={}}
IsValid=function(e)return e~=nil and e.valid~=false end
''')
source = (root/'addon/lua/mmdhl/carrier.lua').read_text(encoding='utf8')
lua.execute(source[:source.index('local entityFrame,entityCandidates')])
lua.execute('''
local e={hidden=false,effect=false,serverHidden=false,dormant=false,index=12}
function e:GetNoDraw()return self.hidden end
function e:IsEffectActive(flag)assert(flag==EF_NODRAW)return self.effect end
function e:GetNW2Bool(key,default)assert(key=='MMDHLNoDraw')return self.serverHidden end
function e:IsDormant()return self.dormant end
function e:EntIndex()return self.index end
assert(not mmdhl.PresentationSuppressed(e))
e.hidden=true assert(mmdhl.PresentationSuppressed(e))
e.hidden=false e.serverHidden=true assert(mmdhl.PresentationSuppressed(e),
 'A client clearing NoDraw must not reveal a server-hidden helper')
e.serverHidden=false e.effect=true assert(mmdhl.PresentationSuppressed(e))
e.effect=false e.dormant=true assert(mmdhl.PresentationSuppressed(e))
e.dormant=false e.MMDHLNotTransmitting=true assert(mmdhl.PresentationSuppressed(e))
e.MMDHLNotTransmitting=false assert(not mmdhl.PresentationSuppressed(e),
 'A revealed/retransmitted actor must be allowed to resume')
e.index=-1 e.dormant=true assert(not mmdhl.PresentationSuppressed(e),
 'Local previews do not have network dormancy')
e.valid=false assert(mmdhl.PresentationSuppressed(e))
''')
print('PASS: no-draw, server intent, effects, dormancy, resumption and local previews')
