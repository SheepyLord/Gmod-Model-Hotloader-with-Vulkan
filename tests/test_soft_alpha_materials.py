"""Soft alpha: parts with partly transparent texels the alpha test cuts get a blended copy.

Runs the renderer's material registration and draw passes against Source-like
materials. Parts the native build reports soft triangles for (AssetInfo
softTriangles) register a blended VertexLitGeneric copy as their "soft" name; the
translucent pass then draws for them outside RTX Remix (which draws them in the
opaque pass natively). Assets without soft triangles, older native builds and the
mmdhl_soft_alpha 0 setting register and draw exactly as before.
"""
from pathlib import Path
from lupa import LuaRuntime
from test_remix_preview import glua_to_lua

root = Path(__file__).resolve().parents[1]
source = (root / 'addon/lua/mmdhl/native_render.lua').read_text(encoding='utf-8')


def runtime(remix, soft_enabled, soft_parts, set_soft=True):
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.globals().REMIX = remix
    lua.globals().SOFT_ENABLED = soft_enabled
    lua.globals().SET_SOFT = set_soft
    lua.execute(r'''
NOW, FRAME = 0, 1
function SysTime() return NOW end
function FrameNumber() return FRAME end
function Vector(...) return {...} end
function IsValid(v) return v ~= nil and v ~= false end
function LocalPlayer() return nil end
unpack = table.unpack
CONVARS, SOFT_CALLS = {}, {}
CreateClientConVar = function(name) CONVARS[name]=true return {GetBool=function() return name~='mmdhl_soft_alpha' or SOFT_ENABLED end} end
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
CREATED, BINDINGS, DRAWS = {}, {}, {}
local Mat = {} Mat.__index = Mat
function Mat:GetName() return self.name end
function Mat:IsError() return false end
function Mat:GetTexture(k) return self.texture end
function Mat:SetTexture(k,v) self.texture=v end
function Mat:SetString(k,v) self.params[k]=v end
function Texture(name) return {GetName=function() return name end, IsError=function() return false end} end
SOURCES={}
for i=1,4 do SOURCES['fixture/'..i]=setmetatable({name='fixture/'..i,params={},texture=Texture('tex/'..i)},Mat) end
function Material(path) return SOURCES[path] end
function CreateMaterial(name,shader,params)
 if CREATED[name] then return CREATED[name] end
 local copy={} for k,v in pairs(params) do copy[k]=v end
 local mat=setmetatable({name=name,shader=shader,params=copy},Mat)
 CREATED[name]=mat
 return mat
end
INFO={materials={}}
ENT={MMDHLClientInstance=9,MMDPresentationFrame=1}
function ENT:GetColor() return {r=255,g=255,b=255,a=255} end
function ENT:EntIndex() return 42 end
function ENT:GetMaterial() return '' end
function ENT:GetSubMaterial() return '' end
NATIVE={SetNativeVertexCache=function() end,SetupSourceLighting=function() return true end}
if SET_SOFT then function NATIVE.SetSoftAlpha(on) SOFT_CALLS[#SOFT_CALLS+1]=on end end
function NATIVE.RenderStats() return {fixedFunctionRemix=REMIX} end
function NATIVE.SetInstanceMaterials(instance,names) BINDINGS[#BINDINGS+1]=names return true end
function NATIVE.DrawInstance(instance,translucent,depth,overrides) DRAWS[#DRAWS+1]={translucent=translucent,depth=depth,overrides=overrides} end
mmdhl={native=NATIVE,assets={asset=INFO},Decode=function(v) return v end,
 L=function(key,vars) return key..':'..(vars and vars.path or '') end,
 GetAsset=function() return 'asset' end, GetInstance=function(ent) return ent.MMDHLClientInstance end,
 IsMMD=function() return true end, PresentationSuppressed=function() return false end,
 ShadowKey=function() return 42 end, Entities=function() return {ENT} end}
''')
    info = lua.globals().INFO.materials
    # soft_parts None: an older native build, whose AssetInfo has no softTriangles.
    for i in range(1, 5):
        soft = soft_parts is not None and i in soft_parts
        part = lua.table_from({'path': 'fixture/%d' % i, 'alphaTexture': soft, 'twoSided': i % 2 == 0})
        if soft_parts is not None:
            part['softTriangles'] = 3 if soft else 0
        info[i] = part
    lua.execute(glua_to_lua(source[:source.index("hook.Add('Think','MMDHL.NativeVisuals'")]))
    lua.execute("mmdhl.LightingOrigin=function() return Vector(0,0,0) end")
    return lua


# Parts 2 and 3 have soft triangles; Source draws them again in the translucent pass.
lua = runtime(remix=False, soft_enabled=True, soft_parts={2, 3})
lua.execute(r'''
assert(CONVARS.mmdhl_soft_alpha and #SOFT_CALLS==1 and SOFT_CALLS[1]==true,'The setting did not reach the native module')
mmdhl.DrawCarrier(ENT,false,0)
local soft=BINDINGS[1].soft
assert(#BINDINGS==1 and ENT.MMDNamesSent=='asset' and ENT.MMDSoftNamesSent=='asset','Soft names did not complete the registration')
assert(soft[1]=='' and soft[4]=='' and soft[2]=='!mmdhl_soft_v1_asset_2' and soft[3]=='!mmdhl_soft_v1_asset_3','Soft names are per slot')
for i,nocull in pairs({[2]='1',[3]='0'}) do
 local mat=CREATED['mmdhl_soft_v1_asset_'..i]
 assert(mat.shader=='VertexLitGeneric' and mat.params['$translucent']=='1' and mat.params['$alphatest']=='0','The copy is not blended')
 assert(mat.params['$nocull']==nocull and mat.params['$basetexture']=='tex/'..i and mat.texture==SOURCES['fixture/'..i].texture,'The copy lost its texture or culling')
 assert(mat.params['$phong']=='1' and mat.params['$bumpmap']=='mmdhl/scmi/normal','The copy does not follow the worker recipe')
end
assert(not CREATED['mmdhl_soft_v1_asset_1'] and not CREATED['mmdhl_soft_v1_asset_4'],'Parts without soft triangles got a copy')
local before=#DRAWS
mmdhl.DrawCarrier(ENT,true,0)
assert(#DRAWS==before+2 and DRAWS[before+1].translucent==true and DRAWS[before+1].depth==false,'The translucent pass did not draw the soft triangles')
before=#DRAWS
mmdhl.DrawCarrier(ENT,true,536870912)
assert(#DRAWS==before+1 and DRAWS[before+1].depth==true,'The depth view changed')
for view=1,50 do mmdhl.DrawCarrier(ENT,false,0) mmdhl.DrawCarrier(ENT,true,0) end
assert(#BINDINGS==1,'Ready soft names were registered again')
''')

# Under RTX Remix the native module draws soft triangles in the opaque pass: no translucent call.
lua = runtime(remix=True, soft_enabled=True, soft_parts={2})
lua.execute(r'''
mmdhl.DrawCarrier(ENT,false,0)
assert(BINDINGS[1].soft[2]=='!mmdhl_soft_v1_asset_2','Remix did not register soft names')
local before=#DRAWS
mmdhl.DrawCarrier(ENT,true,0)
assert(#DRAWS==before,'Remix drew a translucent pass without overrides')
''')

# Switched off: names stay registered (the native module ignores them), no translucent pass.
lua = runtime(remix=False, soft_enabled=False, soft_parts={2})
lua.execute(r'''
assert(SOFT_CALLS[1]==false)
mmdhl.DrawCarrier(ENT,false,0)
local before=#DRAWS
mmdhl.DrawCarrier(ENT,true,0)
assert(#DRAWS==before,'mmdhl_soft_alpha 0 still drew a translucent pass')
''')

# No soft triangles, and an older native build (no softTriangles, no SetSoftAlpha): as before.
for parts, set_soft in (({}, True), (None, False)):
    lua = runtime(remix=False, soft_enabled=True, soft_parts=parts, set_soft=set_soft)
    lua.execute(r'''
mmdhl.DrawCarrier(ENT,false,0)
assert(#BINDINGS==1 and BINDINGS[1].soft==nil and ENT.MMDNamesSent=='asset' and not ENT.MMDSoftNamesSent,'A model without soft triangles registered soft names')
for name in pairs(CREATED) do assert(not name:find('mmdhl_soft_',1,true),'A soft copy was created') end
local before=#DRAWS
mmdhl.DrawCarrier(ENT,true,0)
assert(#DRAWS==before,'A model without soft triangles drew a translucent pass')
''')

print('PASS: soft-alpha copies per slot, translucent pass outside Remix only, setting and older builds unchanged')
