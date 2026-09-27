-- Local developer bridge. It is dormant unless the test launcher writes a
-- session specification matching a process-specific console token.
local realm=SERVER and 'server' or 'client'
local token=GetConVar('mmdhl_debug_token')
if not token then token=CreateConVar('mmdhl_debug_token','',FCVAR_NONE,'Local Model Hotloader developer session token') end
local session=util.JSONToTable(file.Read('mmd_hotloader/debug-session.json','DATA') or '')
if not session or session.token~=MMDHL_DEBUG_TOKEN or (not game.SinglePlayer() and not session.ownedMultiplayer) then return end
local base='mmd_hotloader/debug/'..session.token..'/'
file.CreateDir(base)
local previous=util.JSONToTable(file.Read(base..realm..'-response.json','DATA') or '')
mmdhl.debugSequences=mmdhl.debugSequences or {}
local sequence=mmdhl.debugSequences[realm] or (previous and previous.sequence) or 0
local function write(name,value) file.Write(base..name,util.TableToJSON(value,true)) end
write(realm..'-ready.json',{token=session.token,realm=realm,loaded=mmdhl.native~=nil,capabilities=mmdhl.native and mmdhl.Decode(mmdhl.native.GetCapabilities()),installation=mmdhl.GetInstallationStatus and mmdhl.GetInstallationStatus()})
hook.Add('Think','MMDHL.LocalDebug',function()
    local request=util.JSONToTable(file.Read(base..realm..'-request.json','DATA') or '')
    if not request or request.token~=session.token or request.sequence<=sequence then return end
    sequence=request.sequence mmdhl.debugSequences[realm]=sequence
    local result={token=session.token,sequence=sequence,realm=realm}
    local fn=CompileString(request.code,'Model Hotloader local debug '..sequence,false)
    if isfunction(fn) then
        local ok,value=xpcall(fn,debug.traceback)
        result.ok=ok
        if ok then result.value=value else result.error=value end
    else result.ok=false result.error=fn end
    write(realm..'-response.json',result)
end)
if CLIENT then
    concommand.Add('mmdhl_debug_capture',function(_,_,args)
        local name=tostring(args[1] or 'frame'):gsub('[^%w_-]','')
        local event=args[2]=='ui' and 'PostRenderVGUI' or 'PostRender'
        hook.Add(event,'MMDHL.Capture',function()
            local image=render.Capture({format='png',x=0,y=0,w=ScrW(),h=ScrH(),alpha=false})
            if image then file.Write(base..name..'.png',image) end
            hook.Remove(event,'MMDHL.Capture')
        end)
    end)
end
