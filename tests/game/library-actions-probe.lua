-- Exercise the actual four library buttons and their network request/ack flow.
assert(CLIENT and MMDHL_DEBUG_TOKEN)
local asset='e2c25f9ea0b736c55fe2fd26660f94f85fb5c0f1952fa4562ea3b3df39658eb7'
local roles={'ragdoll','citizen','combine','player'} local current=0 local started=0 local results={}
local request
local function nextAction()
 current=current+1
 if current>#roles then
  timer.Remove('MMDHL.LibraryActionsProbe')
  file.Write('mmd_hotloader/debug/'..MMDHL_DEBUG_TOKEN..'/library-actions.json',util.TableToJSON(results))
  return
 end
 mmdhl.Open() local panel=mmdhl.window.Library panel:SelectAsset(asset)
 assert(IsValid(panel.SpawnButtons[roles[current]]),'Missing action button')
 panel.SpawnButtons[roles[current]]:DoClick()
 request=mmdhl.spawnSequence started=RealTime()
end
nextAction()
timer.Create('MMDHL.LibraryActionsProbe',.2,0,function()
 local s=mmdhl.lastSpawnStatus
 if s and s.request==request and s.state~='loading' then
  results[#results+1]={role=roles[current],state=s.state,message=s.message,entity=s.entity}
  if s.state~='ready' then timer.Remove('MMDHL.LibraryActionsProbe') error(s.message) end
  nextAction()
 elseif RealTime()-started>80 then timer.Remove('MMDHL.LibraryActionsProbe') error('Timed out testing '..roles[current]) end
end)
return true
