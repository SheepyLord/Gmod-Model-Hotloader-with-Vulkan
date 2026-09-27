-- The server captures VPhysics on its owning thread. Clients receive immutable
-- shapes once, then use their rendered bone transforms for moving colliders.
local native=mmdhl.native
local function rotation(q)
 local x,y,z,w=unpack(q)
 return Angle(math.deg(math.asin(math.Clamp(2*(w*y-z*x),-1,1))),math.deg(math.atan2(2*(w*z+x*y),1-2*(y*y+z*z))),math.deg(math.atan2(2*(w*x+y*z),1-2*(x*x+y*y))))
end
local function quaternion(a)
 local x,y,z=math.rad(a.r)*.5,math.rad(a.p)*.5,math.rad(a.y)*.5
 local sx,cx,sy,cy,sz,cz=math.sin(x),math.cos(x),math.sin(y),math.cos(y),math.sin(z),math.cos(z)
 return {sx*cy*cz-cx*sy*sz,cx*sy*cz+sx*cy*sz,cx*cy*sz-sx*sy*cz,cx*cy*cz+sx*sy*sz}
end
if SERVER then
 util.AddNetworkString('mmdhl_scene') local subscribers={}
 -- A subscriber's sphere reaches at most its farthest character, as its client
 -- computes it, never the whole map. The client applies its own collision mode
 -- (userinfo) to every character. Nil: there is nothing to export for it.
 local function interestLimit(p)
  if mmdhl.GetGlobalSettings(p).secondaryCollision<=0 then return end
  local eye,limit=p:EyePos()
  for _,ent in ipairs(mmdhl.Entities()) do limit=math.max(limit or 0,eye:Distance(ent:GetPos())+ent:BoundingRadius()+512) end
  return limit and math.Clamp(limit,512,16384)
 end
 -- One pending shape request per peer, answered at most every 20 ms (1.6 MB/s).
 local function sendChunk(p,state)
  local request=state.chunk state.chunk=nil state.nextChunk=RealTime()+.02
  local bytes,err=native.ReadSceneChunk(request.id,request.offset,32768)
  net.Start('mmdhl_scene') net.WriteUInt(1,2) net.WriteString(request.id) net.WriteUInt(request.offset,32) net.WriteBool(bytes~=nil)
  if bytes then net.WriteUInt(#bytes,16) net.WriteData(bytes,#bytes) else net.WriteString(err or 'Collision shape expired') end net.Send(p)
 end
 net.Receive('mmdhl_scene',function(_,p)
  local command=net.ReadUInt(2)
  if command==0 then
   -- NaN or an infinite radius is no region: treat it as leaving.
   local radius=net.ReadBool() and net.ReadFloat()
   if not radius or radius~=radius or math.abs(radius)==math.huge then subscribers[p]=nil return end
   local state=subscribers[p] or {} subscribers[p]=state state.untilTime=RealTime()+3 state.radius=math.max(radius,512) return
  end
  if command~=1 or not subscribers[p] then return end
  local state=subscribers[p] state.chunk={id=net.ReadString(),offset=net.ReadUInt(32)}
  if RealTime()>=(state.nextChunk or 0) then sendChunk(p,state) end
 end)
 local nextFrame=0
 hook.Add('Tick','MMDHL.SharedScene',function()
  if game.SinglePlayer() then return end
  for p,state in pairs(subscribers) do if state.chunk and IsValid(p) and RealTime()>=(state.nextChunk or 0) then sendChunk(p,state) end end
  if CurTime()<nextFrame then return end nextFrame=CurTime()+.05
  for p,state in pairs(subscribers) do
   if not IsValid(p) or RealTime()>state.untilTime then subscribers[p]=nil
   else
    local limit=interestLimit(p) local raw,err
    if limit then raw,err=native.ExportSecondaryScene(p:EyePos(),math.min(state.radius,limit),p:EntIndex()) end
    if raw then
     local bytes=util.Compress(raw)
     -- Split snapshot metadata; every net message stays below the engine limit.
     state.sequence=(state.sequence or 0)+1 local count=math.ceil(#bytes/48000)
     if count<=64 then for index=1,count do local chunk=bytes:sub((index-1)*48000+1,index*48000)
      net.Start('mmdhl_scene',true) net.WriteUInt(0,2) net.WriteUInt(state.sequence,32) net.WriteUInt(index,8) net.WriteUInt(count,8) net.WriteUInt(#chunk,16) net.WriteData(chunk,#chunk) net.Send(p)
     end end
    elseif err then mmdhl.sceneError=err end
   end
  end
 end)
 hook.Add('PlayerDisconnected','MMDHL.SharedScene',function(p) subscribers[p]=nil end)
else
 local latest,previous,assembling,transfer local known,pending={},{} local queue={} local active=false
 local function request()
  if transfer or #queue==0 or not active then return end
  transfer=table.remove(queue,1) transfer.bytes={} transfer.offset=0 transfer.time=RealTime()
  net.Start('mmdhl_scene') net.WriteUInt(1,2) net.WriteString(transfer.id) net.WriteUInt(0,32) net.SendToServer()
 end
 net.Receive('mmdhl_scene',function()
  local command=net.ReadUInt(2)
  if command==0 then
   local sequence,index,count,size=net.ReadUInt(32),net.ReadUInt(8),net.ReadUInt(8),net.ReadUInt(16)
   local bytes=net.ReadData(size) if count<1 or count>64 or index<1 or index>count or size>48000 then return end
   if latest and sequence<=latest.networkSequence then return end
   if not assembling or sequence>assembling.sequence then assembling={sequence=sequence,count=count,parts={},received=0} end
   if sequence~=assembling.sequence or assembling.parts[index] then return end
   assembling.parts[index]=bytes assembling.received=assembling.received+1
   if assembling.received==count then
    -- A frame larger than 32 MB of JSON or 20000 objects is dropped.
    local decoded=util.Decompress(table.concat(assembling.parts),32*1048576) local frame=decoded and util.JSONToTable(decoded)
    if frame and istable(frame.objects) and #frame.objects<=20000 then
     frame.networkSequence=sequence frame.received=RealTime() frame.byId={}
     for _,o in ipairs(frame.objects) do
      frame.byId[o.id]=o
      if not known[o.shape] and not pending[o.shape] and #queue<2048 then pending[o.shape]=true queue[#queue+1]={id=o.shape,size=o.bytes} end
     end
     previous=latest latest=frame request()
    end assembling=nil
   end return
  end
  if command==1 then
   local id,offset,ok=net.ReadString(),net.ReadUInt(32),net.ReadBool()
   if not transfer or transfer.id~=id then return end
   if not ok then mmdhl.sceneError=net.ReadString() pending[id]=nil transfer=nil request() return end
   local size=net.ReadUInt(16) local bytes=net.ReadData(size)
   if offset~=transfer.offset or size>32768 or offset+size>transfer.size then mmdhl.sceneError='Invalid collision shape transfer' pending[id]=nil transfer=nil request() return end
   transfer.bytes[#transfer.bytes+1]=bytes transfer.offset=offset+size transfer.time=RealTime()
   if transfer.offset==transfer.size then
    local accepted,err=native.AcceptSceneGeometry(id,table.concat(transfer.bytes)) known[id]=accepted or nil mmdhl.sceneError=err pending[id]=nil transfer=nil request()
   else net.Start('mmdhl_scene') net.WriteUInt(1,2) net.WriteString(id) net.WriteUInt(transfer.offset,32) net.SendToServer() end
  end
 end)
 local nextSubscribe=0 local publication=0
 hook.Add('Think','MMDHL.SceneSubscription',function()
  if game.SinglePlayer() then return end
  if transfer and RealTime()-transfer.time>5 then pending[transfer.id]=nil transfer=nil end
  if RealTime()<nextSubscribe then return end nextSubscribe=RealTime()+1
  active=false local radius=4000
  for _,ent in ipairs(mmdhl.Entities()) do local quality=ent.MMDPhysicsLOD
   if GetConVar('mmdhl_secondary_iterations'):GetInt()>0 and mmdhl.GetInstance(ent)>0 and mmdhl.GetSecondaryCollisionMode(ent)>0 and not (quality and quality.suspended) then active=true radius=math.max(radius,LocalPlayer():EyePos():Distance(ent:GetPos())+ent:BoundingRadius()+512) end
  end
  net.Start('mmdhl_scene') net.WriteUInt(0,2) net.WriteBool(active) if active then net.WriteFloat(radius) end net.SendToServer() request()
 end)
 function mmdhl.UpdateRemoteScene()
  if game.SinglePlayer() or not active or not latest then return end
  local out={sequence=publication+1,timestamp=CurTime(),objects={}} publication=out.sequence
  local fraction=previous and math.Clamp((RealTime()-latest.received)/math.max(.001,latest.timestamp-previous.timestamp),0,1) or 1
  for _,o in ipairs(latest.objects) do if known[o.shape] then
   local copy=table.Copy(o) local ent=o.owner and Entity(o.owner)
   if not o.static and IsValid(ent) then
    local bone=ent:TranslatePhysBoneToBone(o.bone or 0) local matrix=bone and bone>=0 and ent:GetBoneMatrix(bone)
    if matrix then copy.position={(matrix*Vector(unpack(o.center or {0,0,0}))):Unpack()} copy.rotation=quaternion(matrix:GetAngles())
    else local old=previous and previous.byId[o.id] if old then copy.position={LerpVector(fraction,Vector(unpack(old.position)),Vector(unpack(o.position))):Unpack()} copy.rotation=quaternion(LerpAngle(fraction,rotation(old.rotation),rotation(o.rotation))) end end
   end
   out.objects[#out.objects+1]=copy
  end end
  local ok,err=native.PublishRemoteScene(util.TableToJSON(out)) if not ok then mmdhl.sceneError=err end
  mmdhl.remoteSceneDiagnostics={objects=#out.objects,pending=#queue+(transfer and 1 or 0),age=RealTime()-latest.received}
 end
 hook.Add('PostCleanupMap','MMDHL.RemoteSceneCleanup',function() native.ClearRemoteScene() latest=nil previous=nil assembling=nil transfer=nil known={} pending={} queue={} end)
end
