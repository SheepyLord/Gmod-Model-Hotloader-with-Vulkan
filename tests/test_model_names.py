"""Translated model names (names.lua): what is sent and when, batching, the
response format, caching across sessions, consent per model (asked at import,
remembered for the next import, a main switch), the model's own English names,
back-off when the service fails or blocks, and names typed by the player.
Runs the addon's names.lua against a simulated game with a fake HTTP service."""
from pathlib import Path
from urllib.parse import parse_qs
import json
from lupa import LuaRuntime
from lua_i18n import attach

ROOT = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)


def lupa_table(value): return type(value).__name__ == '_LuaTable'


def to_lua(value):
    if isinstance(value, dict): return lua.table_from({k: to_lua(v) for k, v in value.items()})
    if isinstance(value, list): return lua.table_from([to_lua(v) for v in value])
    return value


def to_py(value):
    if lupa_table(value):
        keys = list(value.keys())
        if keys and all(isinstance(k, int) for k in keys) and sorted(keys) == list(range(1, len(keys) + 1)):
            return [to_py(value[k]) for k in sorted(keys)]
        if not keys: return {}
        return {str(k): to_py(v) for k, v in value.items()}
    return value


def decode(text):
    try: return to_lua(json.loads(text))
    except Exception: return None


lua.globals().py_decode = decode
lua.globals().py_encode = lambda v: json.dumps(to_py(v), ensure_ascii=False)
lua.execute(r'''
mmdhl={}
isstring=function(v) return type(v)=='string' end
istable=function(v) return type(v)=='table' end
string.Trim=function(s) return (s:gsub('^%s+',''):gsub('%s+$','')) end
table.Count=function(t) local n=0 for _ in pairs(t) do n=n+1 end return n end
util={JSONToTable=function(s) if type(s)~='string' or s=='' then return nil end return py_decode(s) end,TableToJSON=function(t) return py_encode(t) end}
FS={} file={Read=function(p) return FS[p] end,Write=function(p,v) FS[p]=v end,CreateDir=function() end}
CONVARS={gmod_language='en',mmdhl_translate_notice_seen='1'}
CreateClientConVar=function(name,default) CONVARS[name]=CONVARS[name] or default return {GetBool=function() return CONVARS[name]~='0' end,GetString=function() return CONVARS[name] end} end
GetConVar=function(name) return {GetString=function() return CONVARS[name] or '' end} end
RunConsoleCommand=function(name,value) CONVARS[name]=value end
CHANGE={} cvars={AddChangeCallback=function(name,fn) CHANGE[name]=fn end}
HOOKS={} hook={Add=function(e,n,f) HOOKS[e]=HOOKS[e] or {} HOOKS[e][n]=f end,Run=function(e,...) for _,f in pairs(HOOKS[e] or {}) do f(...) end end}
TIMERS={} timer={Create=function(n,_,_,f) TIMERS[n]=f end,Simple=function(_,f) f() end}
NOW=0 RealTime=function() return NOW end
IsValid=function(v) return v~=nil and not (type(v)=='table' and v.removed) end
LocalPlayer=function() return nil end
-- The fake service: requests are held until the test answers them.
REQUESTS={} HTTP=function(r) REQUESTS[#REQUESTS+1]=r return true end
''')
attach(lua)
lua.execute('mmdhl.I18n={Chosen=function() return CHOSEN or "" end}')
lua.execute((ROOT / 'addon/lua/mmdhl/names.lua').read_text(encoding='utf-8'))
N = lua.eval('mmdhl.names')
G = lua.globals()
A, B, C = 'a' * 64, 'b' * 64, 'c' * 64


def tick(seconds=.5):
    G.NOW = G.NOW + seconds
    G.TIMERS['MMDHL.Names']()


def requests(): return list(G.REQUESTS.values())


def answer(request, pairs, code=200):
    request.success(code, json.dumps(pairs, ensure_ascii=False))


def sent(request): return [v for v in parse_qs(request.body)['q']]


def show(text, id, english=None):
    shown, original = N.Display(text, id, english)
    return shown, original


# --- Target language: the addon's chosen language, else the game's own.
assert N.Target() == 'en'
G.CHOSEN = 'zh-cn'; assert N.Target() == 'zh-CN'
G.CHOSEN = 'zh-tw'; assert N.Target() == 'zh-TW'
G.CHOSEN = ''; G.CONVARS['gmod_language'] = 'de'; assert N.Target() == 'de', 'a German game gets German names'
G.CONVARS['gmod_language'] = 'pt-br'; assert N.Target() == 'pt'
G.CONVARS['gmod_language'] = 'en'
print('PASS: target language')

# --- Nothing is sent before the game has loaded, and nothing blocks: the
# original shows until the translation arrives.
assert show('髪', A) == ('髪', None)
tick(); assert requests() == [], 'HTTP is unavailable while loading'
G.HOOKS['InitPostEntity']['MMDHL.Names']()
for name in ['スカート', '前髪2', 'Body', 'Vesna', '2', '']: show(name, A)
tick()
assert len(requests()) == 1
request = requests()[0]
assert request.method == 'POST' and request.url.endswith('&tl=en') and 'clients5.google.com' in request.url and request.timeout == 15
assert sorted(sent(request)) == sorted(['髪', 'スカート', '前髪2']), 'English readers keep ASCII names; digits alone are not sent'
tick(); assert len(requests()) == 1, 'one request at a time'
order = sent(request)
answer(request, [[{'髪': 'hair', 'スカート': 'skirt', '前髪2': 'bangs 2'}[t], 'ja'] for t in order])
assert show('髪', A) == ('Hair', '髪') and show('スカート', A) == ('Skirt', 'スカート') and show('前髪2', A) == ('Bangs 2', '前髪2')
assert N.Both('スカート', A) == 'Skirt (スカート)'
assert G.HOOKS['MMDHL.NamesTranslated'] is not None and N.revision >= 1
print('PASS: queued, batched, answered in the background')

# --- Cached on disk as pairs and reused by the next session without a request.
stored = json.loads(G.FS['mmd_hotloader/translations/en.json'])
assert stored['provider'] == 'google' and ['髪', 'Hair'] in stored['pairs']
lua.execute("package.loaded=nil")
lua.execute((ROOT / 'addon/lua/mmdhl/names.lua').read_text(encoding='utf-8'))
N = lua.eval('mmdhl.names')
G.REQUESTS = to_lua([])
lua.execute('mmdhl.names.state.ready=true')
assert show('髪', A) == ('Hair', '髪')
tick(); assert requests() == []
print('PASS: cache')

# --- Names already in the target language are kept as authored.
G.CHOSEN = 'zh-cn'
show('头发', A); show('スカート', A); show('Hair ribbon', A)
tick(); request = requests()[-1]
assert request.url.endswith('&tl=zh-CN') and sorted(sent(request)) == sorted(['头发', 'スカート', 'Hair ribbon']), 'ASCII names are sent for other languages'
results = {'头发': ['头发', 'zh-CN'], 'スカート': ['裙子', 'ja'], 'Hair ribbon': ['发带', 'en']}
answer(request, [results[t] for t in sent(request)])
assert show('头发', A) == ('头发', None) and show('スカート', A) == ('裙子', 'スカート') and show('Hair ribbon', A) == ('发带', 'Hair ribbon')
# A single text may come back without the outer list.
show('翼', A); tick(); request = requests()[-1]
request.success(200, json.dumps(['翼', 'zh-CN'], ensure_ascii=False))
assert show('翼', A) == ('翼', None)
G.CHOSEN = 'ru'
show('まばたき', A); tick(); request = requests()[-1]
answer(request, [['моргать', 'ja']])
assert show('まばたき', A) == ('Моргать', 'まばたき'), 'Cyrillic translations start with a capital'
G.CHOSEN = ''
print('PASS: identity and response shapes')

# --- Failures and blocks back off; names stay as authored meanwhile.
show('靴', A); tick(); request = requests()[-1]
request.success(429, '<html>Sorry...</html>')
assert show('靴', A) == ('靴', None) and N.Status().failures == 1
count = len(requests()); tick(5); assert len(requests()) == count, 'waits 15 s after a failure'
tick(11); assert len(requests()) == count + 1
requests()[-1].failed('timeout')
waits = [30, 60, 120, 240]
for wait in waits:
    before = len(requests()); tick(wait + 1); assert len(requests()) == before + 1, wait
    requests()[-1].failed('unreachable')
assert N.Status().paused, 'six failures pause translation for the session'
before = len(requests()); tick(700); assert len(requests()) == before
G.HOOKS['MMDHL.LanguageChanged']['MMDHL.Names']()
tick(); assert len(requests()) == before + 1, 'a language change resumes'
answer(requests()[-1], [['shoes', 'ja']])
assert show('靴', A) == ('Shoes', '靴') and N.Status().failures == 0
G.REQUESTS = to_lua([])
lua.execute('HTTP=function(r) REQUESTS[#REQUESTS+1]=r return nil end')
show('袖', A); tick(); assert N.Status().failures == 1 and not N.state.inflight, 'a refused request is a failure, not a stuck request'
lua.execute('HTTP=function(r) REQUESTS[#REQUESTS+1]=r return true end')
G.NOW = G.NOW + 20; tick(); answer(requests()[-1], [['sleeve', 'ja']])
print('PASS: back-off and recovery')

# --- Consent: asked at import, stored per model, remembered for the next import;
# the main switch stops everything.
N.Choose('D:\\a.pmx', False)
assert G.CONVARS['mmdhl_translate_new_imports'] == '0', 'the choice is the default for the next import'
N.Imported(B, 'D:\\a.pmx')
G.REQUESTS = to_lua([])
assert show('髪飾り', B) == ('髪飾り', None)
tick(); assert requests() == [], "a declined model's names are never sent"
assert show('髪', B) == ('髪', None), 'not even cached translations are shown for it'
N.Imported(C, 'D:\\b.pmx')
assert json.loads(G.FS['mmd_hotloader/translations/models.json'])[C] is False, 'no window (warning dismissed): the remembered choice applies'
N.Choose('D:\\c.pmx', True); N.Imported(C, 'D:\\c.pmx')
assert G.CONVARS['mmdhl_translate_new_imports'] == '1' and show('髪', C) == ('Hair', '髪')
N.Imported(A, None, C)
assert json.loads(G.FS['mmd_hotloader/translations/models.json'])[A] is True, 'a preset without a source follows the model it replaces'
G.CONVARS['mmdhl_translate_names'] = '0'; G.CHANGE['mmdhl_translate_names']('mmdhl_translate_names', '1', '0')
assert show('髪', C) == ('髪', None) and not N.Allowed(C)
show('新しい', C); tick(); assert requests() == [], 'switched off: nothing is sent'
G.CONVARS['mmdhl_translate_names'] = '1'
N.Forget(to_lua([B]))
assert B not in json.loads(G.FS['mmd_hotloader/translations/models.json'])
assert not N.Allowed(None), 'text without a model is never sent'
print('PASS: consent')

# --- Models without a recorded choice are never sent before the player has seen
# what translation shares (import window, library notice or the setting).
D, E = 'd' * 64, 'e' * 64
G.CONVARS['mmdhl_translate_notice_seen'] = '0'
G.REQUESTS = to_lua([])
assert N.NeedsNotice() and not N.Allowed(D)
assert show('髪', D) == ('髪', None), 'not even cached translations before the notice'
show('腕輪', D); tick(); assert requests() == []
N.Imported(E, 'D:/e.pmx')
assert E not in json.loads(G.FS['mmd_hotloader/translations/models.json']), 'no window, notice unseen: nothing recorded'
N.AnswerNotice(True)
assert G.CONVARS['mmdhl_translate_notice_seen'] == '1' and G.CONVARS['mmdhl_translate_names'] == '1' and not N.NeedsNotice()
assert show('髪', D) == ('Hair', '髪')
G.CONVARS['mmdhl_translate_notice_seen'] = '0'
N.AnswerNotice(False)
assert G.CONVARS['mmdhl_translate_names'] == '0' and G.CONVARS['mmdhl_translate_notice_seen'] == '1', 'declining switches translation off'
G.CONVARS['mmdhl_translate_names'] = '1'
G.CONVARS['mmdhl_translate_notice_seen'] = '0'
N.Choose('D:/f.pmx', True)
assert G.CONVARS['mmdhl_translate_notice_seen'] == '1', 'answering an import window counts as seeing the notice'
G.CONVARS['mmdhl_translate_notice_seen'] = '0'
G.CHANGE['mmdhl_translate_names']('mmdhl_translate_names', '1', '1')
assert G.CONVARS['mmdhl_translate_notice_seen'] == '1', 'changing the setting counts too'
print('PASS: notice before existing models are sent')

# --- Library names: the model's own English name first; names typed by the player stay.
lua.execute('mmdhl.terms={Get=function(kind,id) return RECORDS and RECORDS[id] end}')
G.RECORDS = to_lua({A: {'embedded': {'nameEnglish': 'Vesna'}}})
entry = {'id': A, 'name': '薇斯纳', 'info': {'name': '薇斯纳'}, 'settings': {}}
assert tuple(N.EntryName('character', to_lua(entry))) == ('Vesna', '薇斯纳'), 'English readers get the PMX English name without a request'
renamed = dict(entry, name='My Vesna', settings={'name': 'My Vesna'})
assert tuple(N.EntryName('character', to_lua(renamed))) == ('My Vesna', None)
preset = dict(entry, settings={'parent': B})
assert tuple(N.EntryName('static', to_lua(preset))) == ('薇斯纳', None)
workshop = {'id': C, 'name': '髪', 'info': {'name': 'x'}, 'settings': {'name': '髪'}, 'workshop': {'title': 'Pack'}}
assert tuple(N.EntryName('character', to_lua(workshop))) == ('Hair', '髪'), 'Workshop items carry the model name'
morph = {'name': 'blink', 'original': 'まばたき', 'displayName': 'まばたき'}
assert tuple(N.MorphLabel(to_lua(morph), A)) == ('blink', 'まばたき'), 'English readers get the PMX English morph name'
assert tuple(N.MorphLabel(to_lua({'name': 'Blink', 'original': 'まばたき', 'displayName': 'Blink'}), A)) == ('Blink', None), 'standard names stay'
print('PASS: library names')

# --- Windows built once are relabelled when translations arrive; closed ones are dropped.
G.ID = A
lua.execute('''
PANEL={texts={}} function PANEL:SetText(t) self.text=t end
CLOSED={removed=true}
mmdhl.names.Bind(PANEL,function(p) p:SetText(mmdhl.names.Both('手袋',ID)) end)
mmdhl.names.Bind(CLOSED,function(p) error('removed panels are not updated') end)
''')
assert G.PANEL.text == '手袋'
tick(); request = requests()[-1]; answer(request, [['gloves', 'ja']])
assert G.PANEL.text == 'Gloves (手袋)'
print('PASS: bound labels')

# --- Batches stay small: at most 40 names and 4000 bytes of form data each.
G.REQUESTS = to_lua([])
for i in range(95): show(f'名前{i}', A)
tick(); first = requests()[-1]
assert len(sent(first)) == 40 and len(first.body) <= 4000
answer(first, [['x', 'ja']] * 40)
G.REQUESTS = to_lua([]); N.queue = to_lua({})
long = 'あ' * 65  # 196-byte names: about 590 bytes of form data each once encoded
for i in range(10): show(long + str(i), A)
show('あ' * 67 + 'x', A)  # 202 bytes: a description, not a name
tick(); request = requests()[-1]
assert len(sent(request)) == 6 and len(request.body) <= 4000, 'long names split into small requests'
assert 'あ' * 67 + 'x' not in sent(request)
print('PASS: batch limits')

# --- Consent is checked again just before each request: a model whose choice
# changes while its names wait (queued, or backing off after a failure) sends
# nothing, while a name another permitted model still wants is sent.
answer(requests()[-1], [['x', 'ja']] * len(sent(requests()[-1])))
F, H, J = 'f' * 64, '9' * 64, '8' * 64
G.REQUESTS = to_lua([]); N.queue = to_lua({})
N.SetAllowed(F, True); N.SetAllowed(H, True)
show('秘密', F); show('共有', F); show('共有', H)
N.SetAllowed(F, False)
tick(); request = requests()[-1]
assert sent(request) == ['共有'], "a revoked model's queued name is not sent"
answer(request, [['shared', 'ja']])
show('隠す', H); tick(); requests()[-1].failed('timeout')
N.SetAllowed(H, False)
count = len(requests()); tick(20)
assert len(requests()) == count and N.Status()['queued'] == 0, 'a model revoked during back-off sends nothing when the retry comes'
# A model without a recorded choice follows the notice: if that changes while
# its name waits, the check at send time alone keeps the name back.
G.REQUESTS = to_lua([])
show('未決定', J); G.CONVARS['mmdhl_translate_notice_seen'] = '0'
tick(); assert requests() == [], 'permission is checked again for the queued model right before sending'
G.CONVARS['mmdhl_translate_notice_seen'] = '1'
print('PASS: consent checked again before every request')
