"""The installation banner: problems that downloading the recommended package fixes
(missing, damaged, modified, mixed or outdated native files) read as one instruction,
with the per-file lines kept for the tooltip; other problems (the 32-bit game, the
server's installation, accepted warnings) keep their own lines; and the Download
button opens only the public repository's releases page. Runs installation_ui.lua
against a simulated client."""
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
function panel()
 local p={children={}}
 setmetatable(p,{__index=function(t,k) return function(self,...) if k=='Add' then local c=panel() c.kind=... t.children[#t.children+1]=c return c end end end})
 return p
end
vgui={Create=function() return panel() end}
mmdhl={serverInstallation=nil}
''')
attach(lua)
lua.execute('mmdhl.I18n.FontFace=function() return "x" end mmdhl.I18n.FontData=function() return {} end')
lua.execute((root/'addon/lua/mmdhl/installation_ui.lua').read_text(encoding='utf-8'))
lua.execute(r'''
local L=mmdhl.L
local function status(issues,extra) local s={installed='2.1.0-native.2',recommended='2.1.0-native.5',issues=issues,download='https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/releases'} for k,v in pairs(extra or {}) do s[k]=v end return s end
local function issue(code,message,feature,accepted) return {code=code,component='client',message=message,feature=feature or 'core',accepted=accepted} end
local S
mmdhl.GetInstallationStatus=function() return S end
local binary=mmdhl.Localize(L'install.binary_problem')
-- Missing, modified, mixed and outdated files: one instruction; the files stay in the details.
S=status({issue('missing',L('install.error.file_missing',{path='lua/bin/gmcl_mmdhl_win64.dll'})),issue('damaged_or_unrecognized',L('install.error.file_unrecognized',{path='bin/win64/mmdhl_runtime_win64.dll'})),
 issue('outdated',L('install.error.release_not_approved',{installed='2.1.0-native.2',recommended='2.1.0-native.5'}))})
local text,versions,details=mmdhl.InstallationSummary()
assert(mmdhl.Localize(text)==binary,'the banner did not collapse downloadable problems: '..tostring(text))
assert(mmdhl.Localize(details):find('gmcl_mmdhl_win64.dll',1,true) and mmdhl.Localize(details):find('mmdhl_runtime_win64.dll',1,true),'the details lost the affected files')
-- The 32-bit game is not a download problem.
S=status({issue('unsupported_platform',L'install.error.unsupported_platform','core')})
text=mmdhl.InstallationSummary()
assert(mmdhl.Localize(text)==mmdhl.Localize(L'install.error.unsupported_platform') or mmdhl.Localize(text):find(mmdhl.Localize(L'install.error.unsupported_platform'),1,true),'the platform problem was replaced')
assert(not mmdhl.Localize(text):find(binary,1,true))
-- Both together: the instruction first, the platform line kept.
S=status({issue('unsupported_platform',L'install.error.unsupported_platform'),issue('missing',L('install.error.file_missing',{path='x'}))})
text=mmdhl.Localize(mmdhl.InstallationSummary())
assert(text:find(binary,1,true)==1 and text:find(mmdhl.Localize(L'install.error.unsupported_platform'),1,true),text)
-- A problem the player accepted keeps its own line with the accepted tag, and no instruction.
S=status({issue('damaged_or_unrecognized',L('install.error.file_unrecognized',{path='y'}),'core',true)},{acceptedIssues=true})
text=mmdhl.Localize(mmdhl.InstallationSummary())
assert(not text:find(binary,1,true) and text:find(mmdhl.Localize(L'install.accepted_tag'),1,true),text)
-- The server's problems are the administrator's.
S=status({}) mmdhl.serverInstallation={issues={issue('missing',L('install.error.file_missing',{path='z'}))}}
text=mmdhl.Localize(mmdhl.InstallationSummary())
assert(not text:find(binary,1,true) and text:find('z',1,true),text)
mmdhl.serverInstallation=nil
-- A verified installation reports nothing.
S=status({}) assert(mmdhl.InstallationSummary()=='')
''')
print('PASS: downloadable problems read as one instruction with details kept; platform, accepted and server problems keep their lines')

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
