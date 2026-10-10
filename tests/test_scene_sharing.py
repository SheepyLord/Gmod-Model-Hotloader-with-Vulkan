"""Shared collision scene: the server grants a subscriber an interest sphere no
larger than what reaches its farthest character (never the map, never NaN),
exports nothing when there is no such character, and answers shape requests at a
bounded rate; clients bound decompression and frame size. Runs the addon's
scene_sharing.lua in a simulated server and client."""
from pathlib import Path
from lupa import LuaRuntime

root = Path(__file__).resolve().parents[1]
source = (root / 'addon/lua/mmdhl/scene_sharing.lua').read_text(encoding='utf-8')
common = r'''
NOW=0 RealTime=function() return NOW end CurTime=function() return NOW end
IsValid=function(v) return v~=nil and not v.gone end istable=function(v) return type(v)=='table' end
isnumber=function(v) return type(v)=='number' end isstring=function(v) return type(v)=='string' end
bit={band=function(a,b) return a&b end,bor=function(a,b) return a|b end}
math.Clamp=function(v,low,high) return math.min(math.max(v,low),high) end
game={SinglePlayer=function() return false end}
HOOKS={} hook={Add=function(event,name,f) HOOKS[event]=HOOKS[event] or {} HOOKS[event][name]=f end}
local V={} V.__index=V
function Vector(x,y,z) return setmetatable({x=x,y=y,z=z},V) end
function V:Distance(o) return math.sqrt((self.x-o.x)^2+(self.y-o.y)^2+(self.z-o.z)^2) end
-- Net messages: reads come from INBOX, each sent message is a list of its fields.
INBOX={} SENT={} local writing
local function write(v) writing[#writing+1]=v end
local function read() return table.remove(INBOX,1) end
net={Receive=function(_,f) RECEIVE=f end,ReadUInt=read,ReadBool=read,ReadFloat=read,ReadString=read,ReadData=read,
 Start=function() writing={} end,WriteUInt=write,WriteBool=write,WriteFloat=write,WriteString=write,WriteData=write,
 Send=function() SENT[#SENT+1]=writing end,SendToServer=function() SENT[#SENT+1]=writing end}
function run(event) for _,f in pairs(HOOKS[event] or {}) do f() end end
'''

server = LuaRuntime(unpack_returned_tuples=True)
server.execute(common + r'''
SERVER=true CLIENT=false
util={AddNetworkString=function() end,Compress=function(s) return s end}
EXPORTS={} CHUNKS={} CHARACTERS={}
native={ExportSecondaryScene=function(center,radius,consumer,kinds) EXPORTS[#EXPORTS+1]=radius EXPORTED_KINDS=kinds return '{"objects":[]}' end,
 ReadSceneChunk=function(id,offset) CHUNKS[#CHUNKS+1]={id=id,offset=offset,at=NOW} return string.rep('s',64) end}
-- The subscriber names the kinds of scene objects its collision choice needs
-- (1 map, 4 objects, 8 living players, 16 living NPCs).
mmdhl={native=native,Entities=function() return CHARACTERS end,CollideScene=29}
function character(distance,radius) return {GetPos=function() return Vector(distance,0,0) end,BoundingRadius=function() return radius end} end
P={kinds=4,EyePos=function() return Vector(0,0,0) end,EntIndex=function() return 1 end}
function subscribe(radius) INBOX={0,radius~=nil,radius,P.kinds} RECEIVE(0,P) end
function shape(offset) INBOX={1,'shape',offset} RECEIVE(0,P) end
-- One export frame: ticks until the 50 ms frame timer runs again.
function frame() local before=#EXPORTS NOW=NOW+.06 run('Tick') return #EXPORTS>before and EXPORTS[#EXPORTS] or nil end
''')
server.execute(source)
server.execute(r'''
-- NaN and infinite radii are no region: the client is not subscribed.
subscribe(0/0) assert(frame()==nil,'a NaN radius subscribes')
subscribe(math.huge) assert(frame()==nil,'an infinite radius subscribes')
subscribe(-math.huge) assert(frame()==nil,'a negative infinite radius subscribes')
-- No character with collisions: nothing is exported, whatever the client asks.
subscribe(1e9) assert(frame()==nil,'a subscriber without characters on the map is exported for')
-- A character 1000 units away (radius 40): its sphere plus 512 units, not the map.
CHARACTERS={character(1000,40)}
subscribe(1e9) assert(frame()==1552,'a map-scale request is capped at the farthest character')
subscribe(800) assert(frame()==800,'a smaller request is kept')
subscribe(-5) assert(frame()==512,'the smallest sphere is 512 units')
CHARACTERS={character(1000,40),character(40000,40)}
subscribe(1e9) assert(frame()==16384,'the server limit never exceeds 16384 units')
-- The client collides with nothing from the scene: no export.
P.kinds=0 subscribe(4000) assert(frame()==nil,'a subscriber without collisions is exported for')
P.kinds=4 subscribe(4000) assert(frame()==4000 and EXPORTED_KINDS==4,'the export is limited to the kinds the subscriber asked for')
-- The character's own body is no scene object.
P.kinds=31 subscribe(4000) assert(frame()==4000 and EXPORTED_KINDS==29)
P.kinds=4
-- Leaving and expiry.
subscribe(nil) assert(frame()==nil,'an unsubscribed client is exported for')
subscribe(4000) NOW=NOW+3.1 assert(frame()==nil,'a subscription not renewed for 3 s expires')
''')
print('PASS: subscription radii are finite, capped by the server and need a character with collisions')

server.execute(r'''
subscribe(4000) CHUNKS={} SENT={}
shape(0) assert(#CHUNKS==1,'the first shape request is answered at once')
assert(SENT[1][1]==1 and SENT[1][2]=='shape' and SENT[1][3]==0 and SENT[1][4]==true and SENT[1][6]==string.rep('s',64))
-- The client asks for the next chunk right away (a listen host has no latency):
-- the request waits instead of being dropped, so the transfer never stalls.
shape(32768) assert(#CHUNKS==1,'a second chunk within 20 ms is not answered yet')
NOW=NOW+.01 run('Tick') assert(#CHUNKS==1)
NOW=NOW+.011 run('Tick') assert(#CHUNKS==2 and CHUNKS[2].offset==32768,'the waiting request is answered after 20 ms')
-- A flood of requests: one pending request per peer, one chunk per 20 ms.
local first,started=#CHUNKS,NOW
for i=1,200 do shape(i) NOW=NOW+.001 run('Tick') end
local served=#CHUNKS-first
assert(served>=9 and served<=11,'200 requests in 200 ms got '..served..' chunks')
for i=first+2,#CHUNKS do assert(CHUNKS[i].at-CHUNKS[i-1].at>=.0199,'two chunks within 20 ms') end
''')
print('PASS: shape chunks are paced to one per 20 ms per peer without dropping requests')

client = LuaRuntime(unpack_returned_tuples=True)
client.execute(common + r'''
SERVER=false CLIENT=true
FRAMES={} LIMITS={}
util={Decompress=function(s,limit) LIMITS[#LIMITS+1]=limit return s end,JSONToTable=function(s) return FRAMES[s] end,TableToJSON=function() return '{}' end}
GetConVar=function() return {GetInt=function() return 1 end} end
LocalPlayer=function() return {EyePos=function() return Vector(0,0,0) end} end
local character={GetPos=function() return Vector(100,0,0) end,BoundingRadius=function() return 30 end}
native={PublishRemoteScene=function() return true end}
mmdhl={native=native,Entities=function() return {character} end,GetInstance=function() return 1 end,GetCollisionFlags=function() return 6 end,SceneKinds=function(flags) return flags&29 end}
function deliver(sequence,name) INBOX={0,sequence,1,1,#name,name} RECEIVE() end
''')
client.execute(source)
client.execute(r'''
run('Think')
assert(SENT[1][1]==0 and SENT[1][2]==true and SENT[1][3]==4000,'the client subscribes with its own sphere')
assert(SENT[1][4]==4,'the client names the kinds of scene objects it collides with')
SENT={}
-- Objects as the server's ExportSecondaryScene writes them (shape: the geometry's SHA-256).
local function object(id,shape) return {id=id,owner=0,bone=0,shape=shape or string.rep('a',64),bytes=60,static=false,actor=0,position={0,0,0},rotation={0,0,0,1},velocity={0,0,0},angular={0,0,0}} end
-- A frame of 20001 objects is dropped: nothing is requested or published.
local big={objects={},timestamp=1} for i=1,20001 do big.objects[i]=object(i,string.format('%064x',i)) end
FRAMES.big=big deliver(1,'big')
assert(LIMITS[1]==32*1048576,'frames decompress to at most 32 MB')
assert(#SENT==0,'an oversized frame was used')
-- A frame within the bounds is used: its unknown shape is requested.
FRAMES.small={objects={object(1)},timestamp=1} deliver(2,'small')
assert(#SENT==1 and SENT[1][1]==1 and SENT[1][2]==string.rep('a',64) and SENT[1][3]==0,'a valid frame was not used')
-- An empty chunk short of the shape's end would ask for the same bytes forever: refused.
SENT={} INBOX={1,string.rep('a',64),0,true,0,''} RECEIVE()
assert(mmdhl.sceneError=='Invalid collision shape transfer' and #SENT==0,'an empty chunk was accepted')
mmdhl.sceneError=nil
-- A malformed object (a mismatched or modified server) drops its frame, with no Lua error: a
-- scalar entry, no id or shape, a NaN position, a zero rotation, a shape larger than any capture.
local bad={5,object(nil),object(2,'not a hash'),object(2),object(2),object(2),object(2)}
bad[4].position={0,0/0,0} bad[5].rotation={0,0,0,0} bad[6].bytes=2^40 bad[7].center={1,2}
for n,o in ipairs(bad) do
 SENT={} FRAMES['bad'..n]={objects={object(3,string.rep('c',64)),o},timestamp=1} deliver(2+n,'bad'..n)
 assert(#SENT==0,'malformed object '..n..' was used')
end
-- A frame without a finite clock is dropped too.
SENT={} FRAMES.clock={objects={object(4,string.rep('d',64))},timestamp=1/0} deliver(20,'clock') assert(#SENT==0,'a frame with an infinite clock was used')
''')
print('PASS: clients bound frame decompression to 32 MB and 20000 objects, and drop frames with malformed objects or empty chunks')
