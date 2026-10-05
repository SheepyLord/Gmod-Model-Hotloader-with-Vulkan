"""Saved editor rules mount their asset before the first model presentation."""
from pathlib import Path
from lupa import LuaRuntime

root = Path(__file__).resolve().parents[1]
source = (root / 'addon/lua/mmdhl/materials.lua').read_text(encoding='utf-8')
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
CLIENT=true
function isstring(v) return type(v)=='string' end
local asset=string.rep('a',64)
INDEX=nil MOUNTS={} AVAILABLE=false
file={Read=function(path,search)
 assert(search=='DATA' and path=='mmd_hotloader/names/assets/'..string.rep('a',16)..'.json')
 return INDEX
end}
util={JSONToTable=function(value) return value=='valid' and {id=asset} or value=='unsafe' and {id='../escape'} or nil end}
mmdhl={MountPackage=function(path)
 MOUNTS[#MOUNTS+1]=path
 assert(path=='data/mmd_hotloader/assets/'..asset..'/materials-v5.gma')
 return AVAILABLE,'not yet downloaded'
end}
''')
lua.execute(source[source.index('function mmdhl.GetMaterialAsset('):])
lua.execute(r'''
local path='mmd/'..string.rep('a',16)..'/body_1'
assert(not mmdhl.MountMaterialAsset(path) and #MOUNTS==0,'Missing metadata was treated as mounted')
INDEX='unsafe' assert(not mmdhl.MountMaterialAsset(path) and #MOUNTS==0)
INDEX='valid'
assert(not mmdhl.MountMaterialAsset(path) and #MOUNTS==1,'A failed mount became permanent')
AVAILABLE=true assert(mmdhl.MountMaterialAsset(path) and #MOUNTS==2)
for i=1,100 do assert(mmdhl.MountMaterialAsset('mmd/'..string.rep('a',16)..'/part_'..i)) end
assert(#MOUNTS==2,'Each material remounted the same package')
for _,path in ipairs({'models/ordinary/body','mmd/aaa/body','mmd/'..string.rep('a',16)..'/../escape'}) do
 assert(not mmdhl.MountMaterialAsset(path),'Non-asset material path accepted')
end
CLIENT=false assert(not mmdhl.MountMaterialAsset(path))
''')
print('PASS: deferred/failed asset mounts retry, successful mounts are shared across slots, and unsafe paths are rejected')
