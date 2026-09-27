"""Gives a lupa test runtime the addon's text lookup (mmdhl.L, mmdhl.Localize) with the real
English catalogue, so tests that run addon Lua see the same English players do."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'addon/lua/mmdhl/i18n.lua').read_text(encoding='utf-8')
CATALOGUE = (ROOT / 'addon/resource/localization/en/mmdhl.properties').read_text(encoding='utf-8')

SETUP = '''
MMDHL_TEST_SAVED={file=file,GetConVar=GetConVar,CreateClientConVar=CreateClientConVar,util=util,net=net,language=language,hook=hook,cvars=cvars,timer=timer,CLIENT=CLIENT,SERVER=SERVER}
file={Read=function(path) if path=='resource/localization/en/mmdhl.properties' then return MMDHL_TEST_CATALOGUE end end}
GetConVar=function() return {GetString=function() return 'en' end} end
CreateClientConVar=function() return {GetInt=function() return 0 end,GetString=function() return '' end} end
hook={Add=function() end,Run=function() end} cvars={AddChangeCallback=function() end} timer={Create=function() end}
util=nil net=nil language=nil CLIENT=true SERVER=false
'''
RESTORE = '''
mmdhl.L('common.ok')
local s=MMDHL_TEST_SAVED
file,GetConVar,CreateClientConVar,util,net,language,hook,cvars,timer,CLIENT,SERVER=s.file,s.GetConVar,s.CreateClientConVar,s.util,s.net,s.language,s.hook,s.cvars,s.timer,s.CLIENT,s.SERVER
MMDHL_TEST_SAVED=nil
'''


def attach(lua):
    """Adds the lookup to the runtime's current global mmdhl table, creating it if needed.
    mmdhl.L follows the runtime's SERVER flag at call time: server code returns tokens,
    which mmdhl.Localize renders in English."""
    lua.globals().MMDHL_TEST_CATALOGUE = CATALOGUE
    lua.execute(SETUP)
    lua.execute(SOURCE)
    lua.execute(RESTORE)
