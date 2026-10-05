"""A late texture must recover without a bad depth cache or per-view errors.

Runs the renderer's material builders, draw registration and shadow registration
against Source-like materials. CreateMaterial retains its first parameter table,
so using a fallback once would remain visible after the texture becomes ready.
"""
from pathlib import Path
from lupa import LuaRuntime
from test_remix_preview import glua_to_lua

root = Path(__file__).resolve().parents[1]
source = (root / 'addon/lua/mmdhl/native_render.lua').read_text(encoding='utf-8')
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
NOW, FRAME = 0, 1
function SysTime() return NOW end
function FrameNumber() return FRAME end
function Vector(...) return {...} end
function IsValid(v) return v ~= nil and v ~= false end
function LocalPlayer() return nil end
unpack = table.unpack
CreateClientConVar = function() return {GetBool=function() return true end} end
cvars = {AddChangeCallback=function() end}
file = {Exists=function() return false end}
util = {TableToJSON=function(v) return v end}
hook = {Add=function() end, Remove=function() end}
scripted_ents = {Register=function() end}
bit = {band=function(a,b) return a & b end}
render = {
 SuppressEngineLighting=function() end, SetLightingOrigin=function() end,
 GetColorModulation=function() return 1,1,1 end, GetBlend=function() return 1 end,
 SetColorModulation=function() end, SetBlend=function() end,
 ResetModelLighting=function() end, SetLocalModelLights=function() end,
 RenderFlashlights=function(f) f() end
}
LOOKUPS, TEXTURE_POLLS, CREATED, BINDINGS, DRAWS, SHADOWS = {}, {}, {}, {}, {}, {}
local Mat = {} Mat.__index = Mat
function Mat:GetName() return self.name end
function Mat:IsError() return self.error == true end
function Mat:GetTexture(k)
 TEXTURE_POLLS[self.name]=(TEXTURE_POLLS[self.name] or 0)+1
 return self.texture
end
function Mat:SetTexture(k,v) self.texture=v end
function Mat:SetString(k,v) self.params[k]=v end
function Mat:SetUndefined(k) self.params[k]=nil end
function Mat:GetString(k) return self.params[k] end
function Texture(name,bad)
 return {GetName=function() return name end, IsError=function() return bad==true end}
end
SOURCES={}
for i=1,30 do SOURCES['fixture/'..i]=setmetatable({name='fixture/'..i,params={},texture=Texture('tex/'..i)},Mat) end
SOURCES['fixture/30'].texture=nil
function Material(path)
 LOOKUPS[path]=(LOOKUPS[path] or 0)+1
 return SOURCES[path]
end
function CreateMaterial(name,shader,params)
 if CREATED[name] then return CREATED[name] end
 local copy={} for k,v in pairs(params) do copy[k]=v end
 local mat=setmetatable({name=name,shader=shader,params=copy},Mat)
 CREATED[name]=mat
 return mat
end
INFO={materials={}}
for i=1,30 do INFO.materials[i]={path='fixture/'..i,alphaTexture=i==30,twoSided=i%2==0} end
ENT={MMDHLClientInstance=9,MMDPresentationFrame=1}
function ENT:GetColor() return {r=255,g=255,b=255,a=255} end
function ENT:EntIndex() return 42 end
function ENT:GetMaterial() return '' end
function ENT:GetSubMaterial() return '' end
function ENT:GetNoDraw() return false end
function ENT:CreateShadow() self.shadowCreated=true end
function ENT:CallOnRemove() end
function ENT:MarkShadowAsDirty() self.dirty=true end
ENT.MMDHLVisual={SetPos=function() end,SetRenderBoundsWS=function() end}
NATIVE={SetNativeVertexCache=function() end,SetupSourceLighting=function() return true end}
function NATIVE.SetInstanceMaterials(instance,names)
 BINDINGS[#BINDINGS+1]=names
 if FAIL_BIND then return nil,'registration fixture failed' end
 return true
end
function NATIVE.DrawInstance(instance,translucent,depth)
 assert(not depth or mmdhl.depthMaterials.asset,'An incomplete depth mapping was drawn')
 DRAWS[#DRAWS+1]={translucent=translucent,depth=depth}
end
function NATIVE.GetBoundsValues() return 0,0,0,1,1,1,nil end
function NATIVE.RegisterSourceShadow(entity,instance,names,alpha)
 for _,name in ipairs(names) do
  local mat=CREATED[name:sub(2)]
  assert(not mat.initialTranslucent,'ShadowBuild snapped an untyped material reference')
  if mat.params['$translucent_material'] then mat.typed=true end
 end
 SHADOWS[#SHADOWS+1]=names
 return true
end
local originalCreate=CreateMaterial
function CreateMaterial(name,shader,params)
 local mat=originalCreate(name,shader,params)
 mat.initialTranslucent=mat.initialTranslucent or params['$translucent_material']~=nil
 return mat
end
mmdhl={native=NATIVE,assets={asset=INFO},Decode=function(v) return v end,
 L=function(key,vars) return key..':'..(vars and vars.path or '') end,
 GetAsset=function() return 'asset' end, GetInstance=function(ent) return ent.MMDHLClientInstance end,
 IsMMD=function() return true end, PresentationSuppressed=function() return false end,
 ShadowKey=function() return 42 end, Entities=function() return {ENT} end}
''')
lua.execute(glua_to_lua(source[:source.index("hook.Add('Think','MMDHL.NativeVisuals'")]))
lua.execute("mmdhl.LightingOrigin=function() return Vector(0,0,0) end")
lua.execute(r'''
-- One texture is still uploading. Colour rendering continues; depth never
-- dereferences nil, and neither a complete cache nor a success marker appears.
mmdhl.DrawCarrier(ENT,false,0)
assert(#DRAWS==2 and #BINDINGS==1,'Colour drawing was blocked by the depth upload')
assert(not ENT.MMDNamesSent and not mmdhl.depthMaterials.asset)
assert(next(CREATED)==nil,'A partial depth build was published')
for view=1,200 do
 mmdhl.DrawCarrier(ENT,false,1073741824)
 mmdhl.DrawCarrier(ENT,true,536870912)
 mmdhl.DrawCarrier(ENT,false,0)
 mmdhl.UpdateNativeVisuals(true)
end
assert(#BINDINGS==1,'Incomplete mappings were registered for every view')
assert(not mmdhl.shadowMaterials.asset and #SHADOWS==0 and not ENT.MMDShadowAlpha)
assert(LOOKUPS['fixture/30']<10 and TEXTURE_POLLS['fixture/30']<10,'An upload was polled for every render view')
-- Source may also return its error texture. It remains retryable.
NOW=.3 SOURCES['fixture/30'].texture=Texture('error',true)
mmdhl.DrawCarrier(ENT,false,0)
assert(not mmdhl.depthMaterials.asset and not ENT.MMDNamesSent)
-- The very same material acquires its real texture on a later frame.
NOW=.6 SOURCES['fixture/30'].texture=Texture('tex/30')
mmdhl.DrawCarrier(ENT,false,1073741824)
assert(ENT.MMDNamesSent=='asset' and #BINDINGS==2)
assert(#mmdhl.depthMaterials.asset==30 and BINDINGS[2].depth[30]~='')
local last=mmdhl.depthMaterials.asset[30]
assert(last.params['$basetexture']=='tex/30' and last.texture==SOURCES['fixture/30'].texture)
assert(last.params['$nocull']=='1')
mmdhl.UpdateNativeVisuals(true)
assert(#SHADOWS==1 and ENT.MMDShadowAlpha==1 and ENT.shadowCreated)
assert(CREATED[SHADOWS[1][30]:sub(2)].typed,'Shadow source was not handed to the native type bridge')
for view=1,200 do mmdhl.DrawCarrier(ENT,false,0) mmdhl.UpdateNativeVisuals(true) end
assert(#BINDINGS==2 and #SHADOWS==1,'Ready materials were rebuilt or re-registered')
-- Clearing caches must also force existing instances to receive the new maps.
mmdhl.sourceTextureVersion=mmdhl.sourceTextureVersion+1
mmdhl.depthMaterials={} mmdhl.shadowMaterials={}
mmdhl.DrawCarrier(ENT,false,0) mmdhl.UpdateNativeVisuals(true)
assert(#BINDINGS==3 and #SHADOWS==2,'Existing instances retained obsolete material maps')
-- Native registration can fail without falsely marking the mapping complete.
ENT.MMDNamesSent=nil ENT.MMDColorNamesSent=nil FAIL_BIND=true NOW=1
mmdhl.DrawCarrier(ENT,false,0)
local failedCount=#BINDINGS
for view=1,100 do mmdhl.DrawCarrier(ENT,false,0) end
assert(#BINDINGS==failedCount and not ENT.MMDNamesSent,'A failed registration repeated every view')
FAIL_BIND=false NOW=1.3 mmdhl.DrawCarrier(ENT,false,0)
assert(ENT.MMDNamesSent=='asset' and #BINDINGS==failedCount+1)
-- A missing VMT also stays uncached and can recover after mounting.
mmdhl.sourceMaterials={} mmdhl.depthMaterials={} mmdhl.shadowMaterials={} mmdhl.sourceBlendMaterials={}
ENT.MMDNamesSent=nil ENT.MMDColorNamesSent=nil
SOURCES['fixture/30'].error=true NOW=2
mmdhl.DrawCarrier(ENT,false,0) mmdhl.UpdateNativeVisuals(true)
assert(not mmdhl.sourceMaterials.asset and not mmdhl.depthMaterials.asset and not mmdhl.shadowMaterials.asset)
SOURCES['fixture/30'].error=false NOW=2.3
mmdhl.DrawCarrier(ENT,false,0) mmdhl.UpdateNativeVisuals(true)
assert(ENT.MMDNamesSent=='asset' and #mmdhl.depthMaterials.asset==30)
''')
print('PASS: 30-slot delayed/error textures, uninterrupted colour, bounded retries, depth recovery, shadow typing and registration failure recovery')
