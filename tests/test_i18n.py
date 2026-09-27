"""Translation lookup: catalogue parsing, placeholders, English fallback, language choice and server tokens."""
from pathlib import Path
from lupa import LuaRuntime

root = Path(__file__).resolve().parents[1]
source = (root / 'addon/lua/mmdhl/i18n.lua').read_text(encoding='utf-8')
english = '﻿# Comment\n! Also a comment\nmmdhl.a.plain = Plain text  \r\nmmdhl.a.vars=Moved {count} item(s) to {folder}.\nmmdhl.a.lines=One\\nTwo \\\\ \\t tab\nmmdhl.a.only_en=English only\nmmdhl.a.equals=a=b\nmmdhl.a.wrap=Failed: {reason}\n'
# The game's own catalogues escape characters as \uXXXX; an empty value keeps the English.
german = 'mmdhl.a.plain=Einfacher Text\nmmdhl.a.vars={count} Eintr\\u00e4ge nach {folder} verschoben.\nmmdhl.a.only_en=\nmmdhl.a.equals=\\"a\\" \\ud83d\\ude00\n'
japanese = 'mmdhl.a.plain=テキスト\n'


def runtime(server, game='en', chosen='', debug=0):
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.globals().catalogues = lua.table_from({'resource/localization/en/mmdhl.properties': english,
                                              'resource/localization/de/mmdhl.properties': german,
                                              'resource/localization/ja/mmdhl.properties': japanese})
    lua.execute(f'''
SERVER={'true' if server else 'false'} CLIENT=not SERVER
game_value={game!r} chosen_value={chosen!r} debug_value={debug} changes={{}} callbacks={{}}
file={{Read=function(path,search) assert(search=='GAME') return catalogues[path] end}}
GetConVar=function(name) assert(name=='gmod_language') return {{GetString=function() return game_value end}} end
CreateClientConVar=function(name) if name=='mmdhl_language' then return {{GetString=function() return chosen_value end}} end return {{GetInt=function() return debug_value end}} end
RunConsoleCommand=function(name,value) assert(name=='mmdhl_language') chosen_value=value callbacks[name]() end
util={{AddNetworkString=function() end}} net={{Receive=function() end}}
hook={{Add=function() end,Run=function(name,language) changes[#changes+1]=language end}}
cvars={{AddChangeCallback=function(name,f) callbacks[name]=f end}} timer={{Create=function() end}}
''')
    lua.execute(source)
    return lua


client = runtime(False)
L = client.eval('mmdhl.L')
assert L('a.plain') == 'Plain text'
assert L('a.vars', client.table_from({'count': 3, 'folder': 'Hair'})) == 'Moved 3 item(s) to Hair.'
assert L('a.vars', client.table_from({'count': 1})) == 'Moved 1 item(s) to {folder}.', 'missing variables stay visible'
assert L('a.lines') == 'One\nTwo \\ \t tab'
assert L('a.equals') == 'a=b'
assert L('a.missing') == 'mmdhl.a.missing', 'missing phrases show their key'
assert client.eval('mmdhl.Localize')('no tokens') == 'no tokens'

german_client = runtime(False, 'de')
L = german_client.eval('mmdhl.L')
assert L('a.plain') == 'Einfacher Text'
assert L('a.vars', german_client.table_from({'count': 2, 'folder': 'Haare'})) == '2 Einträge nach Haare verschoben.'
assert L('a.only_en') == 'English only', 'untranslated and empty phrases fall back to English'
assert L('a.equals') == '"a" 😀', 'escaped quotes and surrogate pairs'
assert runtime(False, 'DE').eval('mmdhl.L')('a.plain') == 'Einfacher Text', "language folders are lowercase like the game's"
assert runtime(False, '../en').eval('mmdhl.I18n.Language()') == 'en', 'language codes cannot leave the folder'
assert runtime(False, 'pl').eval('mmdhl.I18n.GameLanguage()') == 'en', 'a game language without a translation means English'

# The player's choice overrides the game language, and changing it notifies open windows.
chosen = runtime(False, 'de', 'ja')
assert chosen.eval('mmdhl.L("a.plain")') == 'テキスト'
assert chosen.eval('mmdhl.L("a.vars",{count=1,folder="x"})') == 'Moved 1 item(s) to x.', 'the chosen language falls back to English too'
assert runtime(False, 'de', 'xx').eval('mmdhl.I18n.Language()') == 'de', 'an unknown choice follows the game'
chosen.execute('mmdhl.I18n.Choose("")')
assert chosen.eval('mmdhl.L("a.plain")') == 'Einfacher Text' and chosen.eval('changes[1]') == 'de'
chosen.execute('mmdhl.I18n.Choose("de")')
assert chosen.eval('#changes') == 1, 'choosing the language already in use changes nothing'

# The server sends tokens; each client renders them in its own language, nested ones included.
server = runtime(True)
token = server.eval('mmdhl.L')('a.wrap', server.table_from({'reason': server.eval('mmdhl.L')('a.vars', server.table_from({'count': 4, 'folder': 'Props'}))}))
assert 'Failed' not in token
assert server.eval('mmdhl.Localize')(token) == 'Failed: Moved 4 item(s) to Props.', 'the server logs English'
assert german_client.eval('mmdhl.Localize')(token) == 'Failed: 4 Einträge nach Props verschoben.'
assert german_client.eval('mmdhl.Localize')('Prefix ' + token + ' suffix') == 'Prefix Failed: 4 Einträge nach Props verschoben. suffix'
# A client wrapping server text with its own phrase renders the server part too.
assert german_client.eval('mmdhl.L')('a.wrap', german_client.table_from({'reason': token})) == 'Failed: Failed: 4 Einträge nach Props verschoben.'
# Truncated tokens (for example cut by a length limit) degrade to readable text.
for cut in range(1, len(token.encode('utf-8'))):
    partial = token.encode('utf-8')[:cut].decode('utf-8', 'ignore')
    assert isinstance(client.eval('mmdhl.Localize')(partial), str)
assert client.eval('mmdhl.Localize')(token[:-1]) == 'Failed: Moved 4 item(s) to Props.'

checking = runtime(False, debug=1)
pseudo = checking.eval('mmdhl.L')('a.vars', checking.table_from({'count': 5, 'folder': 'Hair'}))
assert pseudo == '[Mövéd 5 ítém(s) tö Hair.' + '~' * 12 + ']', pseudo
assert runtime(False, debug=2).eval('mmdhl.L')('a.plain') == '[a.plain]'
print('PASS: catalogue parsing and escapes, placeholders, English fallback, safe language codes, language choice and change notice, nested server tokens, truncation, check modes')
