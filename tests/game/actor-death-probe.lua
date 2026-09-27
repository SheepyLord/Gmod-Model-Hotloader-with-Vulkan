-- Owned-session regression: set MMDHLDeathSubject to an actor index, then run.
local subject=Entity(MMDHLDeathSubject)
assert(IsValid(subject),'Missing death test subject')
MMDHLDeathProbe={started=CurTime(),frames=0,controllers={},maxLinear=0,maxAngular=0,maxSpan=0,finite=true,phases={}}
local result=MMDHLDeathProbe
local function record(source,rag)
 if source~=subject then return end
 result.id=rag:EntIndex() result.model=rag:GetModel() result.bodies=rag:GetPhysicsObjectCount()
 result.massAtSpawn={}
 for i=0,rag:GetPhysicsObjectCount()-1 do local b=rag:GetPhysicsObjectNum(i)
  result.massAtSpawn[i+1]={mass=b:GetMass(),inertia={b:GetInertia():Unpack()},speed=b:GetVelocity():Length(),angular=b:GetAngleVelocity():Length()}
 end
end
hook.Add('CreateEntityRagdoll','MMDHL.ActorDeathProbe',record)
hook.Add('Tick','MMDHL.ActorDeathProbe',function()
 local now=CurTime() local age=now-result.started local rag=result.id and Entity(result.id)
 -- EEER can replace a player's first corpse with its isolated copy.
 if subject:IsPlayer() and IsValid(subject.RPE_LastServerRagdoll) then rag=subject.RPE_LastServerRagdoll result.id=rag:EntIndex() end
 if IsValid(rag) then
  result.frames=result.frames+1 result.bodies=rag:GetPhysicsObjectCount()
  local root=rag:GetPhysicsObjectNum(0):GetPos() local linear,angular,span=0,0,0
  for i=0,rag:GetPhysicsObjectCount()-1 do local b=rag:GetPhysicsObjectNum(i)
   local p,v,a=b:GetPos(),b:GetVelocity(),b:GetAngleVelocity()
   for _,n in ipairs({p.x,p.y,p.z,v.x,v.y,v.z,a.x,a.y,a.z}) do result.finite=result.finite and n==n and math.abs(n)<1e12 end
   linear=math.max(linear,v:Length()) angular=math.max(angular,a:Length()) span=math.max(span,p:Distance(root))
  end
  result.maxLinear=math.max(result.maxLinear,linear) result.maxAngular=math.max(result.maxAngular,angular) result.maxSpan=math.max(result.maxSpan,span)
  local phase=math.floor(age) local previous=result.phases[phase] or {linear=0,angular=0,span=0}
  previous.linear=math.max(previous.linear,linear) previous.angular=math.max(previous.angular,angular) previous.span=math.max(previous.span,span) result.phases[phase]=previous
  for _,c in ipairs(ents.GetAll())do if c.Ragdoll==rag then result.controllers[c:GetClass()]=true end end
  result.finalLinear=linear result.finalAngular=angular
 end
 if age>=(MMDHLDeathDuration or 10) then result.done=true hook.Remove('Tick','MMDHL.ActorDeathProbe') hook.Remove('CreateEntityRagdoll','MMDHL.ActorDeathProbe') end
end)
-- A zero force/position asks Source to synthesize a large force at world zero,
-- making distant corpses spin even with stock models. Use an explicit impact.
local damage=DamageInfo() damage:SetDamage(10) damage:SetDamageType(DMG_BULLET)
damage:SetDamagePosition(subject:WorldSpaceCenter()) damage:SetDamageForce(Vector(25,0,0))
damage:SetAttacker(player.GetAll()[1]) damage:SetInflictor(player.GetAll()[1])
subject:SetHealth(1) subject:TakeDamageInfo(damage)
return {started=true,subject=subject:EntIndex()}
