assert(CLIENT and MMDHL_DEBUG_TOKEN)
local l=mmdhl.library
local ids={'e2c25f9ea0b736c55fe2fd26660f94f85fb5c0f1952fa4562ea3b3df39658eb7','8f6507b0fad5769a24718bdb8dbcca19050b1e9730db313db649b8f89142881f'}
local saved={} local paths={'mmd_hotloader/library/folders.json'}
for _,id in ipairs(ids)do assert(l.entries[id]) paths[#paths+1]='mmd_hotloader/library/'..id..'.json' end
for _,p in ipairs(paths)do saved[p]=file.Read(p,'DATA') or false end
local function restore()for p,v in pairs(saved)do if v then file.Write(p,v)else file.Delete(p)end end l.Refresh()end
local ok,err=xpcall(function()
 local root='Folder test '..MMDHL_DEBUG_TOKEN:sub(1,8)
 assert(l.CreateFolder(root)==root) assert(l.CreateFolder('角色',root)==root..'/角色')
 assert(l.MoveToFolder(ids,root..'/角色')) l.Refresh()
 for _,id in ipairs(ids)do assert(l.entries[id].settings.folder==root..'/角色')end
 local renamed=root..' renamed' assert(l.RenameFolder(root,renamed)==renamed)
 mmdhl.Open() local panel=mmdhl.window.Library panel.folder=renamed panel:Refresh()
 assert(#panel.Models:GetLines()==2,'Folder filter did not show both models')
 assert(l.RemoveFolder(renamed)) l.Refresh()
 for _,id in ipairs(ids)do assert(l.entries[id].settings.folder=='' and l.entries[id].info.vertices>0)end
end,debug.traceback)
restore() assert(ok,err)
return {pass=true,models=#ids,restored=true}
