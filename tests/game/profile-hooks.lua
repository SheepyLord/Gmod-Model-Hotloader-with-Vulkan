assert(MMDHL_DEBUG_TOKEN,'Owned test session required')
if MMDHL_HOOK_PROFILE then error('Hook profiling is already active') end
MMDHL_HOOK_PROFILE={data={},saved={}}
local profile=MMDHL_HOOK_PROFILE
local function pack(...) return {n=select('#',...),...} end
for event,handlers in pairs(hook.GetTable()) do
 for key,fn in pairs(handlers) do
  local name=event..'/'..tostring(key)
  local entry={calls=0,ms=0,maximum=0}
  profile.data[name]=entry
  local wrapped=function(...)
   local t=SysTime() local values=pack(fn(...)) local ms=(SysTime()-t)*1000
   entry.calls=entry.calls+1 entry.ms=entry.ms+ms entry.maximum=math.max(entry.maximum,ms)
   return unpack(values,1,values.n)
  end
  profile.saved[#profile.saved+1]={event=event,key=key,original=fn,wrapper=wrapped}
  handlers[key]=wrapped
 end
end
function profile.finish()
 for _,saved in ipairs(profile.saved) do
  local handlers=hook.GetTable()[saved.event]
  if handlers and handlers[saved.key]==saved.wrapper then handlers[saved.key]=saved.original end
 end
 MMDHL_HOOK_PROFILE=nil
 return profile.data
end
return true
