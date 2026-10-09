"""The installation banner (installation_ui.lua against a simulated client). A problem keeps
a feature off; a warning disables nothing. When the native module did not load because its
files are missing, unreadable or from two builds, those problems read as one instruction
(install.binary_problem) with the per-file lines kept for the tooltip. Files this addon does
not know (a test build, a newer release, modified or mixed files), which run anyway, read as
one warning line (install.binary_unrecognized), never as that instruction, and are pending,
so the banner shows. Other problems and warnings (another platform, a file the loaded module
did without, a game build) keep their own lines. A server's problems are worded as the
server's; its warnings about files this addon does not know are its administrator's: only the
detailed view (the installation window's tooltip, Copy diagnostics) lists them, and they are
never pending. Dismiss lasts until the problems or warnings change, and the warning about
another build of a file (another size and SHA-256, the same sentence) comes back. The version
line shows the loaded module's own label and build for files this addon does not know, and
the Download button opens only the public repository's releases page."""
from pathlib import Path
from lupa import LuaRuntime
from lua_i18n import attach

root = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
SERVER=false CLIENT=true
ScrH=function() return 1080 end ScrW=function() return 1920 end
math.Clamp=function(v,low,high) return math.min(math.max(v,low),high) end
isstring=function(v) return type(v)=='string' end
IsValid=function(v) return v~=nil end
concommand={Add=function() end} timer={Simple=function() end}
HOOKS={} hook={Add=function(event,name,f) HOOKS[event]=HOOKS[event] or {} HOOKS[event][name]=f end,Run=function() end}
game={SinglePlayer=function() return true end} LocalPlayer=function() return {} end
OPENED={} gui={OpenURL=function(url) OPENED[#OPENED+1]=url end}
surface={SetFont=function() end,GetTextSize=function() return 10,10 end,SetDrawColor=function() end,DrawRect=function() end,CreateFont=function() end}
-- A VGUI panel that keeps its children, visibility, text and tooltip; other methods (CamelCase)
-- do nothing, and fields stay nil until the addon sets them.
function panel()
 local p={children={},visible=true}
 local methods={Add=function(self,kind) local c=panel() c.kind=kind self.children[#self.children+1]=c return c end,
  SetVisible=function(self,v) self.visible=v end,SetText=function(self,t) self.text=t end,SetTooltip=function(self,t) self.tooltip=t end}
 return setmetatable(p,{__index=function(_,k) if type(k)=='string' and k:match('^%u') then return methods[k] or function() end end end})
end
vgui={Create=function() return panel() end}
FILES={} file={Read=function(p) return FILES[p] end,Write=function(p,c) FILES[p]=c end,CreateDir=function() end}
CreateClientConVar=function() return {GetBool=function() return true end} end cvars={AddChangeCallback=function() end}
util={JSONToTable=function() return nil end,TableToJSON=function() return '{}' end} istable=function(v) return type(v)=='table' end SysTime=function() return 0 end
mmdhl={serverInstallation=nil}
''')
attach(lua)
lua.execute('mmdhl.I18n.FontFace=function() return "x" end mmdhl.I18n.FontData=function() return {} end')
lua.execute((root/'addon/lua/mmdhl/installation_ui.lua').read_text(encoding='utf-8'))
lua.execute(r'''
local L,T=mmdhl.L,mmdhl.Localize
local function status(issues,extra) local s={installed='2.1.0-native.2',recommended='2.1.0-native.5',issues=issues,download='https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/releases'} for k,v in pairs(extra or {}) do s[k]=v end return s end
-- As installation.lua reports them: a problem keeps its feature off; a warning (warning=true)
-- disables nothing. identity: files this addon does not know; detail: what tells one such
-- warning from another; cause: a warning that became the problem when loading failed.
local function problem(code,message,feature,extra) local v={code=code,component='client',message=message,feature=feature or 'core'} for k,x in pairs(extra or {}) do v[k]=x end return v end
local function warning(code,message,feature,extra) local v=problem(code,message,feature,extra) v.warning=true return v end
local S
mmdhl.GetInstallationStatus=function() return S end
local binary,unrecognized=T(L'install.binary_problem'),T(L'install.binary_unrecognized')
local function summary() local text,versions,details,pending=mmdhl.InstallationSummary() return T(text),T(versions),T(details),pending end
local function has(text,part) return text:find(T(part),1,true)~=nil end
-- The External Models banner, built afresh as the spawn menu builds it: whether it shows.
local function shown() return mmdhl.AddInstallationBanner(panel()).visible end
local function serverLine(v) return T(L('install.server_issue',{issue=L('install.issue',{feature=L'install.feature.core',message=T(v.message)})})) end
local DISMISSED='mmd_hotloader/installation_dismissed.txt'

-- The module did not load: the missing runtime, an unreadable file and the two builds of module
-- and runtime explained it (their warnings became problems), beside the loader's error. One
-- instruction comes first and the loader keeps its line; a file this addon does not know beside
-- them needs no line of its own (the instruction speaks for it); the details keep every file.
local runtimeMissing=problem('missing',L('install.error.file_missing',{path='bin/win64/mmdhl_runtime_win64.dll'}),'core',{component='runtime',cause=true})
local unreadable=problem('unreadable',L('install.error.file_unreadable',{path='lua/bin/gmcl_mmdhl_win64.dll'}),'core',{cause=true})
local A,B='0123456789ab-20261001T120000Z','ba9876543210-20261008T120000Z'
local twoBuilds=problem('mixed_builds',L('install.warning.mixed_builds',{module='lua/bin/gmcl_mmdhl_win64.dll',build=A,runtime='bin/win64/mmdhl_runtime_win64.dll',runtimeBuild=B}),'core',{component='runtime',identity=true,cause=true,detail=A..'/'..B})
local loader=problem('loader_failed',L('install.error.loader_failed',{reason="Couldn't load module library!"}),'core',{component='module'})
local function unknownFile(path,sha,component,feature) return warning('damaged_or_unrecognized',L('install.error.file_unrecognized',{path=path}),feature,{component=component,identity=true,detail='3160576:'..sha}) end
local unknownWorker=unknownFile('lua/bin/mmdhl_worker.exe',string.rep('d',64),'worker','imports')
S=status({runtimeMissing,twoBuilds,loader,unknownWorker})
local text,_,details,pending=summary()
assert(text:find(binary,1,true)==1,'the module did not load and the banner did not say to download: '..text)
assert(has(text,loader.message),'the loader\'s error lost its line: '..text)
assert(not text:find(unrecognized,1,true) and not text:find('mmdhl_worker.exe',1,true),'a file this addon does not know got a line beside the instruction: '..text)
for _,part in ipairs({'mmdhl_runtime_win64.dll','gmcl_mmdhl_win64.dll',A,B,'mmdhl_worker.exe'}) do assert(details:find(part,1,true),'the details lost '..part) end
assert(pending==4 and shown(),'pending '..pending)
-- Each of them alone is the same instruction.
for _,v in ipairs({runtimeMissing,unreadable,twoBuilds}) do
 S=status({v,loader}) text=summary()
 assert(text:find(binary,1,true)==1 and has(text,loader.message) and not has(text,v.message),v.code..': '..text)
end
-- The module loaded: a file it did without (Windows found the runtime elsewhere) is a warning with
-- its own line, never the download instruction.
S=status({warning('missing',runtimeMissing.message,'core',{component='runtime',cause=true})})
text,_,_,pending=summary()
assert(not text:find(binary,1,true) and has(text,runtimeMissing.message) and pending==1 and shown(),text)

-- Files this addon does not know, loaded (a test build, a newer or unapproved release, a file of
-- another release, two builds, a worker from another build): one warning line, never the
-- download instruction. They are pending, so the banner shows; the details list each one.
local fresh={unknownFile('lua/bin/gmcl_mmdhl_win64.dll',string.rep('a',64),'client','core'),unknownFile('bin/win64/mmdhl_runtime_win64.dll',string.rep('c',64),'runtime','core'),
 warning('mixed_installation',L('install.error.file_mixed',{path='lua/bin/lib_coacd.dll',release='2.1.0-native.2',required='2.1.0-native.5'}),'detailedCollision',{component='coacd',identity=true,detail='6169600:'..string.rep('e',64)}),
 warning('unapproved_release',L('install.error.release_not_approved',{installed='9.0.0',recommended='2.1.0-native.5'}),'core',{component='module',identity=true,detail='9.0.0'}),
 warning('mixed_builds',twoBuilds.message,'core',{component='runtime',identity=true,cause=true,detail=twoBuilds.detail}),
 warning('mixed_installation',L'install.error.worker_mismatch','imports',{component='worker',identity=true,probe=true,detail=A..'/'..B})}
S=status(fresh) S.installed=nil
text,_,details,pending=summary()
assert(text==unrecognized,'files this addon does not know did not read as one warning line: '..text)
assert(pending==#fresh and shown(),'files this addon does not know raised nothing: '..pending)
for _,v in ipairs(fresh) do assert(has(details,v.message),'the details lost '..v.code) end
-- Beside another warning (a game build no profile describes) the line comes first; it keeps its own.
local untested=warning('game_unverified',L('install.warning.game_untested',{libraries='engine.dll'}),'rendering',{component='game'})
S=status({fresh[1],untested})
text,_,_,pending=summary()
assert(text:find(unrecognized,1,true)==1 and has(text,untested.message) and not text:find(binary,1,true) and pending==2,text)

-- Dismiss hides them until the files change: the same files stay hidden; another build of the
-- module (another size and SHA-256, the same sentence) brings the banner back. The installation
-- window still lists what was dismissed.
S=status({fresh[1],fresh[2]})
assert(shown() and not mmdhl.InstallationDismissed())
mmdhl.DismissInstallation()
assert(mmdhl.InstallationDismissed() and not shown(),'Dismiss did not hide files this addon does not know')
S=status({fresh[1],fresh[2]}) assert(mmdhl.InstallationDismissed() and not shown(),'the same files came back')
local window=mmdhl.AddInstallationBanner(panel(),true)
assert(window.visible and T(window.children[2].children[1].text):find(unrecognized,1,true),'the installation window does not list a dismissed warning')
local rebuilt=unknownFile('lua/bin/gmcl_mmdhl_win64.dll',string.rep('b',64),'client','core')
assert(T(rebuilt.message)==T(fresh[1].message) and rebuilt.detail~=fresh[1].detail)
S=status({rebuilt,fresh[2]})
assert(not mmdhl.InstallationDismissed() and shown(),'another build of the module stayed hidden')
FILES[DISMISSED]=nil

-- The 32-bit game is not a download problem.
S=status({problem('unsupported_platform',L'install.error.unsupported_platform','core',{component='platform',cause=true})})
text=summary()
assert(has(text,L'install.error.unsupported_platform') and not text:find(binary,1,true),'the platform problem was replaced: '..text)
-- Both together: the instruction first, the platform line kept.
S=status({problem('unsupported_platform',L'install.error.unsupported_platform'),problem('missing',L('install.error.file_missing',{path='x'}))})
text=summary()
assert(text:find(binary,1,true)==1 and has(text,L'install.error.unsupported_platform'),text)
-- Recheck's restart line is a problem of its own.
S=status({problem('restart_required',L'install.error.restart_to_load','core',{component='module'})})
text,_,_,pending=summary() assert(pending==1 and has(text,L'install.error.restart_to_load') and not text:find(binary,1,true),text)

-- The server's problems are the administrator's, worded as the server's.
local serverMissing=problem('missing',L('install.error.file_missing',{path='z'}),'core',{component='server'})
S=status({}) mmdhl.serverInstallation={issues={serverMissing}}
text,_,_,pending=summary()
assert(not text:find(binary,1,true) and text==serverLine(serverMissing) and pending==1 and shown(),text)
-- Its files this addon does not know are listed only in the detailed view (the installation
-- window's tooltip, Copy diagnostics): never in a multiplayer player's banner, never pending.
game.SinglePlayer=function() return false end
LocalPlayer=function() return {IsListenServerHost=function() return false end,IsAdmin=function() return false end} end
local serverUnknown=unknownFile('lua/bin/gmsv_mmdhl_win64.dll',string.rep('f',64),'server','core')
local serverBuilds=warning('mixed_builds',twoBuilds.message,'core',{component='runtime',identity=true,cause=true,detail=twoBuilds.detail})
mmdhl.serverInstallation={issues={serverUnknown,serverBuilds}}
text,_,details,pending=summary()
assert(text=='' and pending==0 and not shown(),'a server\'s files this addon does not know reached the player\'s banner: '..text)
assert(details:find(serverLine(serverUnknown),1,true) and details:find(serverLine(serverBuilds),1,true),'the detailed view does not list the server\'s warnings: '..details)
window=mmdhl.AddInstallationBanner(panel(),true)
local label=window.children[2].children[1]
assert(not T(label.text):find(serverLine(serverUnknown),1,true) and T(label.tooltip):find(serverLine(serverUnknown),1,true),'the installation window lists the server\'s warning outside its tooltip')
assert(not mmdhl.InstallationDismissed(),'nothing to dismiss')
-- They change nothing for Dismiss either: another server build keeps the player's warning dismissed.
S=status({fresh[1]}) mmdhl.DismissInstallation()
mmdhl.serverInstallation={issues={unknownFile('lua/bin/gmsv_mmdhl_win64.dll',string.rep('9',64),'server','core')}}
assert(mmdhl.InstallationDismissed() and not shown(),'a server\'s warning brought the player\'s banner back')
-- A server problem does.
mmdhl.serverInstallation={issues={serverMissing}}
assert(not mmdhl.InstallationDismissed() and shown(),'a new server problem stayed hidden')
mmdhl.serverInstallation=nil FILES[DISMISSED]=nil
-- In single player and on a listen server this player runs the server's files: for the host
-- they read as its own, as the one line, pending, and Dismiss tells one build from another.
for _host,host in ipairs({'single player','listen server'}) do
 game.SinglePlayer=function() return host=='single player' end
 LocalPlayer=function() return {IsListenServerHost=function() return host=='listen server' end,IsAdmin=function() return true end} end
 S=status({}) mmdhl.serverInstallation={issues={serverUnknown,serverBuilds}}
 text,_,details,pending=summary()
 assert(text==unrecognized and pending==2 and shown(),host..': the server\'s files this addon does not know missed the host\'s banner: '..text)
 assert(not text:find(binary,1,true) and not text:find(serverLine(serverUnknown),1,true),host..': '..text)
 mmdhl.DismissInstallation() assert(mmdhl.InstallationDismissed() and not shown(),host..': Dismiss did not hide them')
 mmdhl.serverInstallation={issues={unknownFile('lua/bin/gmsv_mmdhl_win64.dll',string.rep('8',64),'server','core')}}
 assert(not mmdhl.InstallationDismissed() and shown(),host..': another server build stayed dismissed')
 -- Beside the host's own such warnings the same line speaks for both: the server's status,
 -- which arrives a second later, never brings a dismissed line back.
 S=status({fresh[1]}) mmdhl.serverInstallation=nil mmdhl.DismissInstallation()
 mmdhl.serverInstallation={issues={serverUnknown}}
 assert(mmdhl.InstallationDismissed() and summary()==unrecognized,host..': the server\'s status brought the host\'s dismissed line back')
 mmdhl.serverInstallation=nil FILES[DISMISSED]=nil
end
game.SinglePlayer=function() return true end LocalPlayer=function() return {} end

-- A verified installation reports nothing.
S=status({}) assert(mmdhl.InstallationSummary()=='' and not shown())
-- Dismiss hides the banner and the notice until the problems change.
S=status({problem('game_incompatible','Game ABI check failed for engine.dll (a)','rendering')})
assert(not mmdhl.InstallationDismissed()) mmdhl.DismissInstallation() assert(mmdhl.InstallationDismissed())
S=status({problem('game_incompatible','Game ABI check failed for engine.dll (a)','rendering')}) assert(mmdhl.InstallationDismissed(),'the same problems came back')
S=status({problem('game_incompatible','Game ABI check failed for engine.dll (b)','rendering')}) assert(not mmdhl.InstallationDismissed(),'a changed problem stayed hidden')
mmdhl.serverInstallation={issues={problem('missing','z')}} S=status({problem('game_incompatible','Game ABI check failed for engine.dll (a)','rendering')})
assert(not mmdhl.InstallationDismissed(),'a new server problem stayed hidden') mmdhl.serverInstallation=nil
S=status({}) assert(not mmdhl.InstallationDismissed(),'nothing to dismiss')

-- The version line: the release the files match; for files this addon does not know, the loaded
-- module's own label and build; nothing loaded, not verified.
local versions
S=status({}) _,versions=summary()
assert(has(versions,L('install.native_release',{version='2.1.0-native.2'})),versions)
S=status(fresh) S.installed=nil S.loaded={module={release='2.3.0',build='feedfacecafe-20261009T120000Z'}}
_,versions=summary()
assert(has(versions,L('install.native_release',{version='2.3.0 (feedfacecafe-20261009T120000Z)'})),versions)
S=status({runtimeMissing,loader}) S.installed=nil
_,versions=summary()
assert(has(versions,L('install.native_release',{version=L'install.not_verified'})),versions)
''')
print('PASS: a module that did not load (missing, unreadable, two builds) reads as one download instruction; files this addon does not know as one '
      'warning line that is pending; a server\'s such warnings only in the detailed view; Dismiss lasts until the problems or the files change; '
      'the version line shows the loaded module\'s own label and build')

# The Download button: only the public repository's releases page or a release on it.
lua.execute(r'''
local function press(url)
 mmdhl.GetInstallationStatus=function() return {issues={},download=url} end
 OPENED={} local p=panel() mmdhl.AddInstallationBanner(p,true)
 local controls=p.children[1].children[1]
 controls.children[1].DoClick() return OPENED[1]
end
local page='https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/releases'
assert(press(page)==page and press(page..'/tag/2.1.0-native.5')==page..'/tag/2.1.0-native.5')
assert(press('https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/releasesX')==nil and press('https://example.com/releases')==nil and press('https://github.com/SheepyLord/-Gmodl_MMD_Hot_Loader/releases')==nil,'the Download button opened another address')
''')
print('PASS: the Download button opens only the public releases page')
