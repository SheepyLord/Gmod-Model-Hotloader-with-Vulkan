"""Model terms of use (terms.lua): what a model's readme, embedded comment and
VRM licence talk about, the import and export acknowledgements and switching
them off for good, the record kept for each model, what Workshop packages
carry, and how untrusted package terms are bounded. Runs the addon's terms.lua
against a simulated game."""
from pathlib import Path
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
# Like util.TableToJSON: UTF-8 text is written as is, so lengths are bytes of UTF-8.
lua.globals().py_encode = lambda v: json.dumps(to_py(v), ensure_ascii=False)
lua.execute(r'''
mmdhl={library={entries={}},props={library={entries={}}}}
isstring=function(v) return type(v)=='string' end
istable=function(v) return type(v)=='table' end
isbool=function(v) return type(v)=='boolean' end
isnumber=function(v) return type(v)=='number' end
string.Trim=function(s) return (s:gsub('^%s+',''):gsub('%s+$','')) end
string.GetFileFromFilename=function(p) return p:match('[\\/]([^\\/]*)$') or p end
table.HasValue=function(t,v) for _,x in pairs(t) do if x==v then return true end end return false end
util={JSONToTable=function(s) if type(s)~='string' or s=='' then return nil end return py_decode(s) end,TableToJSON=function(t) return py_encode(t) end}
mmdhl.Decode=function(value,err) if value==nil then return nil,err end if isstring(value) then return util.JSONToTable(value) end return value end
FS={}
file={Read=function(path) return FS[path] end,Write=function(path,value) FS[path]=value end,Delete=function(path) FS[path]=nil end,CreateDir=function() end}
CONVARS={}
CreateClientConVar=function(name,default) CONVARS[name]=CONVARS[name] or default return {GetBool=function() return CONVARS[name]~='0' end} end
RunConsoleCommand=function(name,value) CONVARS[name]=value end
-- VGUI: panels record what the dialogs build.
PANELS={}
local Panel={}
Panel.__index=function(t,k) local m=rawget(Panel,k) if m~=nil then return m end return function() end end
function Panel:Add(class) return NewPanel(class,self) end
function Panel:SetText(t) rawset(self,'text',t) end
function Panel:GetText() return rawget(self,'text') end
function Panel:SetChecked(v) rawset(self,'checked',v) end
function Panel:GetChecked() return rawget(self,'checked')==true end
function Panel:SetEnabled(v) rawset(self,'enabled',v) end
function Panel:IsEnabled() return rawget(self,'enabled')~=false end
function Panel:SetTitle(t) rawset(self,'title',t) end
function Panel:SetTooltip(t) rawset(self,'tooltip',t) end
function Panel:AppendText(t) rawset(self,'text',(rawget(self,'text') or '')..t) rawset(self,'first',rawget(self,'first') or t) end
function Panel:AddSheet(label,panel,icon) local s=rawget(self,'sheets') or {} s[#s+1]={label=label,panel=panel,icon=icon} rawset(self,'sheets',s) end
function Panel:Close() rawset(self,'removed',true) local f=rawget(self,'OnClose') if f then f(self) end end
function Panel:Remove() rawset(self,'removed',true) end
function Panel:GetTall() return 20 end
function NewPanel(class,parent)
 local p=setmetatable({class=class,parent=parent,children={}},Panel)
 if parent then parent.children[#parent.children+1]=p end
 if class=='DFrame' then rawset(p,'lblTitle',NewPanel('DLabel')) end
 if class=='DCheckBoxLabel' then rawset(p,'Label',NewPanel('DLabel')) end
 PANELS[#PANELS+1]=p
 return p
end
vgui={Create=function(class,parent) return NewPanel(class,parent) end}
IsValid=function(v) return v~=nil and not (type(v)=='table' and rawget(v,'removed')) end
-- The newest panel of a class, optionally with a text; texts of a frame's labels.
function Last(class,text) for i=#PANELS,1,-1 do local p=PANELS[i] if p.class==class and (text==nil or rawget(p,'text')==text) then return p end end end
function Frames() local n=0 for _,p in ipairs(PANELS) do if p.class=='DFrame' then n=n+1 end end return n end
function Texts(root) local out={} local function walk(p) if rawget(p,'text') then out[#out+1]=rawget(p,'text') end for _,c in ipairs(p.children) do walk(c) end end walk(root) return table.concat(out,'\n') end
Color=function(r,g,b,a) return {r=r,g=g,b=b,a=a} end color_white=Color(255,255,255)
ScrW=function() return 1920 end ScrH=function() return 1080 end
draw={} surface={} TOP,BOTTOM,FILL,LEFT,RIGHT=4,5,1,2,3
mmdhl.UI={metrics=function() return function(x) return x end,{Body='b',Small='s',Strong='st',Title='t'} end,
 label=function(parent,text) local p=parent:Add('DLabel') p:SetText(text) return p end,
 checkbox=function(parent,text) local p=parent:Add('DCheckBoxLabel') p:SetText(text) return p end,
 button=function(parent,text,fn) local p=parent:Add('DButton') p:SetText(text) rawset(p,'DoClick',fn) return p end}
-- The native reader: notes per source path, as JSON.
NOTES={} INSPECTED={}
native={InspectModelNotes=function(source) INSPECTED[#INSPECTED+1]=source local n=NOTES[source] if not n then return nil,'The selected model file no longer exists' end return n end}
mmdhl.native=native
''')
attach(lua)
lua.execute((ROOT / 'addon/lua/mmdhl/terms.lua').read_text(encoding='utf-8'))
T = lua.eval('mmdhl.terms')
G = lua.globals()
L = lua.eval('mmdhl.L')


def last(cls, text=None): return G.Last(cls, text)


def record(id): return json.loads(G.FS[f'mmd_hotloader/terms/{id}.json'])


def analyze(notes):
    topics, lines = T.Analyze(to_lua(notes))
    return to_py(topics) or [], to_py(lines) or []


ids = [c * 64 for c in 'abcdef0123456789']

# --- What a Japanese readme talks about, and which of its lines are rules.
readme_ja = '\n'.join([
    '【利用規約】',
    '・MMDでの動画制作にご利用いただけます。',
    '・MMD以外のソフトやゲームでの使用は禁止です。',
    '・再配布は禁止します。',
    '・R-18、グロ表現での使用はご遠慮ください。',
    '・改変は自由です。',
    '・クレジット表記をお願いします。',
    'モデル制作：テスト',
])
notes_ja = {'file': 'miku.pmx', 'embedded': {'name': '初音ミク', 'comment': 'モデル：テスト\n配布元：example', 'commentEnglish': 'モデル：テスト\n配布元：example'},
            'readmes': [{'name': 'readme.txt', 'folder': 'model', 'matched': True, 'encoding': 'shift_jis', 'text': readme_ja}]}
topics, lines = analyze(notes_ja)
assert topics == ['sharing', 'games', 'video', 'sexual', 'violence', 'modification', 'credit'], topics
assert lines == ['・MMD以外のソフトやゲームでの使用は禁止です。', '・再配布は禁止します。', '・R-18、グロ表現での使用はご遠慮ください。', '・改変は自由です。', '・クレジット表記をお願いします。'], lines
docs = to_py(T.Documents(to_lua(notes_ja)))
assert [d['label'] for d in docs] == ['Model comment', 'readme.txt'], 'the same comment in both languages is shown once'
print('PASS: Japanese readme topics and rule lines')

readme_en = '\n'.join([
    'Terms of Use',
    '- Do not redistribute this model.',
    '- Use in games (VRChat, GMod, etc.) is not allowed.',
    '- Commercial use is prohibited.',
    '- Please credit me when you post a video.',
    '- Special edition release notes.',
    '- Alternative outfits are included.',
])
topics, lines = analyze({'readmes': [{'name': 'LICENSE', 'text': readme_en}]})
assert topics == ['sharing', 'games', 'video', 'commercial', 'credit'], topics
assert lines == readme_en.split('\n')[1:5], lines
long_line = '再配布禁止' + 'あ' * 400
topics, lines = analyze({'readmes': [{'name': 'x.txt', 'text': long_line}]})
assert lines[0].endswith('…') and len(lines[0]) == 220, 'long rule lines are cut to 219 characters and an ellipsis'
print('PASS: English readme, false friends (edition, alternative), long lines')

# --- VRM licences: the permission fields are the rules; titles and credits are not.
vrm1 = {'version': '1.0', 'meta': {'title': 'Alicia', 'authors': ['Author'], 'copyright': '(c) Author', 'licenseUrl': 'https://vrm.dev/licenses/1.0/',
                                   'avatarPermission': 'onlyAuthor', 'commercialUsage': 'personalNonProfit', 'allowExcessivelyViolentUsage': False,
                                   'allowExcessivelySexualUsage': False, 'allowRedistribution': False, 'modification': 'prohibited', 'creditNotation': 'required',
                                   'allowPoliticalOrReligiousUsage': False, 'allowAntisocialOrHateUsage': False}}
topics, lines = analyze({'vrm': vrm1})
assert topics == ['sharing', 'sexual', 'violence', 'commercial', 'modification', 'credit'], topics
assert lines == ['Who may use the avatar: Only the author', 'Violent use: Not allowed', 'Sexual use: Not allowed', 'Commercial use: Individuals, non-profit only',
                 'Redistribution: Not allowed', 'Modification: Not allowed', 'Credit: Required', 'Political or religious use: Not allowed',
                 'Antisocial or hateful use: Not allowed'], lines
vrm0 = {'version': '0.x', 'meta': {'title': 'Old', 'licenseName': 'Redistribution_Prohibited', 'allowedUser': 'OnlyAuthor', 'violentUsage': 'Disallow',
                                   'sexualUsage': 'Disallow', 'commercialUsage': 'Allow', 'allowRedistribution': False, 'otherPermissionUrl': ''}}
topics, lines = analyze({'vrm': vrm0})
assert topics == ['sharing', 'sexual', 'violence'], topics
assert lines == ['Licence: Redistribution prohibited', 'Who may use the avatar: Only the author', 'Violent use: Not allowed', 'Sexual use: Not allowed', 'Commercial use: Allowed'], lines
assert analyze({'vrm': {'version': '0.x', 'meta': {'licenseName': 'CC_BY_NC'}}})[1] == ['Licence: CC BY-NC']
print('PASS: VRM 1.0 and 0.x licences')

# --- Import: the player must tick the acknowledgement; "don't show again" applies on accepting.
source = 'D:\\Models\\Miku\\miku.pmx'
G.NOTES[source] = json.dumps(notes_ja, ensure_ascii=False)
calls = []
T.BeforeImport(source, 'character', lambda: calls.append('proceed'), lambda: calls.append('cancel'))
frame = T.pending
assert frame is not None and frame.title == 'Terms of use: miku.pmx' and calls == []
text = G.Texts(frame)
assert 'Many models are released only for making videos' in text and 'You alone are responsible' in text, 'warning and disclaimer are shown'
assert 'Its texts mention: sharing, games or other software, videos, sexual content, violence, modification, credit' in text
sheet = last('DPropertySheet')
tabs = [t.label for t in sheet.sheets.values()]
assert tabs == ['Rules at a glance', 'Model comment', 'readme.txt'], tabs
assert last('RichText', readme_ja) is not None, 'the readme is shown in full'
agree, never = last('DCheckBoxLabel', "I have read the model's terms and take full responsibility for how I use it."), last('DCheckBoxLabel', "Don't show this warning again")
accept, cancel = last('DButton', 'Accept and import'), last('DButton', 'Cancel')
assert agree and never and accept and cancel
assert accept.enabled is False, 'importing needs the acknowledgement'
assert 'Utilities > User > Character Models' in never.tooltip
agree.OnChange(agree, True)
assert accept.enabled is True
never.SetChecked(never, True)
accept.DoClick()
assert calls == ['proceed'] and G.CONVARS['mmdhl_terms_warning_import'] == '0' and T.pending is None and frame.removed
assert T.acknowledged[source] is not None
# Switched off: no window, but the texts are still read and kept.
frames = G.Frames()
G.INSPECTED = to_lua([])
T.BeforeImport(source, 'character', lambda: calls.append('proceed'), lambda: calls.append('cancel'))
assert calls == ['proceed', 'proceed'] and G.Frames() == frames and list(G.INSPECTED.values()) == [source]
T.Imported('character', ids[0], source)
saved = record(ids[0])
assert saved['origin'] == 'import' and saved['kind'] == 'character' and saved['file'] == 'miku.pmx' and saved['acknowledged']
assert saved['topics'] == ['sharing', 'games', 'video', 'sexual', 'violence', 'modification', 'credit']
assert saved['readmes'][0]['text'] == readme_ja and saved['embedded']['name'] == '初音ミク'
print('PASS: import acknowledgement, dismissal, record kept')

# --- Cancelling never dismisses the warning; closing the window cancels; one window at a time.
G.CONVARS['mmdhl_terms_warning_import'] = '1'
calls.clear()
T.BeforeImport(source, 'character', lambda: calls.append('proceed'), lambda: calls.append('cancel'))
never, agree = last('DCheckBoxLabel', "Don't show this warning again"), last('DCheckBoxLabel', "I have read the model's terms and take full responsibility for how I use it.")
never.SetChecked(never, True)
agree.OnChange(agree, True)
last('DButton', 'Cancel').DoClick()
assert calls == ['cancel'] and G.CONVARS['mmdhl_terms_warning_import'] == '1'
T.BeforeImport(source, 'character', lambda: calls.append('proceed'), lambda: calls.append('cancel'))
frame, frames = T.pending, G.Frames()
T.BeforeImport(source, 'character', lambda: calls.append('second'), lambda: calls.append('second'))
assert G.Frames() == frames and 'second' not in calls, 'a second request waits for the open window'
frame.Close(frame)
assert calls == ['cancel', 'cancel'] and T.pending is None
print('PASS: cancel, close and a single pending window')

# --- Older natives cannot read readmes: the warning still shows, nothing is recorded.
reader = G.native.InspectModelNotes
G.native.InspectModelNotes = None
T.bySource = to_lua({})
calls.clear()
other = 'D:\\Models\\Other\\other.pmx'
T.BeforeImport(other, 'character', lambda: calls.append('proceed'), lambda: calls.append('cancel'))
text = G.Texts(T.pending)
assert 'Terms of use: other.pmx' == T.pending.title and 'No readme, licence text or comment was found' in text and 'cannot read readme files' in text
last('DCheckBoxLabel', "I have read the model's terms and take full responsibility for how I use it.").OnChange(None, True)
last('DButton', 'Accept and import').DoClick()
assert calls == ['proceed']
T.Imported('character', ids[1], other)
assert f'mmd_hotloader/terms/{ids[1]}.json' not in list(G.FS.keys())
G.native.InspectModelNotes = reader
print('PASS: older native module')

# --- Reimports and presets keep the record they replace; a reload keeps the acknowledgement.
T.Imported('static', ids[2], None, ids[0])
assert record(ids[2])['readmes'][0]['text'] == readme_ja, 'a preset without a source keeps the replaced record'
T.bySource = to_lua({})
T.acknowledged = to_lua({})
G.INSPECTED = to_lua([])
T.Imported('character', ids[0], source)
assert list(G.INSPECTED.values()) == [source], 'a reload reads the source again'
assert record(ids[0])['acknowledged'] == saved['acknowledged'], 'the earlier acknowledgement is kept'
# On a server the game does not host, the notes beside a model picked in an earlier session
# are not read again: a reloaded character (library.ReloadCharacter passes the revision it
# replaces) keeps that revision's record; a new import has none to keep.
G.native.InspectModelNotes = lua.eval("function() return nil,'On a server you do not host, model terms are read only beside a model just chosen in the file window' end")
T.bySource = to_lua({})
T.Imported('character', ids[8], source, ids[0])
assert record(ids[8])['readmes'][0]['text'] == readme_ja and record(ids[8])['acknowledged'] == saved['acknowledged'], 'a reloaded character lost the record it replaces'
T.Imported('character', ids[9], source)
assert f'mmd_hotloader/terms/{ids[9]}.json' not in list(G.FS.keys()), 'an import without readable notes made up a record'
G.native.InspectModelNotes = reader
T.bySource = to_lua({})
print('PASS: reimports, presets and reloads')

# --- Derived props read their original's terms; Forget ignores anything but asset ids.
G.mmdhl.props.library.entries[ids[3]] = to_lua({'settings': {'parent': ids[0]}})
assert T.Get('static', ids[3]).file == 'miku.pmx' and T.Get('character', ids[3]) is None
T.Forget(to_lua(['../../cfg/config', 'short', ids[2]]))
assert f'mmd_hotloader/terms/{ids[2]}.json' not in list(G.FS.keys()) and f'mmd_hotloader/terms/{ids[0]}.json' in list(G.FS.keys())
assert T.Get('character', '../../x') is None
print('PASS: derived props and forgetting')

# --- Untrusted terms (from Workshop packages) are bounded and stripped to known fields.
evil = {'file': 'x' * 1000, 'embedded': {'comment': 'あ' * 30000, 'name': 5, 'evil': 'z'},
        'readmes': [{'name': f'r{i}.txt', 'text': f'text {i}', 'matched': i % 2 == 0, 'folder': 'etc', 'extra': 'no'} for i in range(20)] + ['junk'],
        'vrm': {'version': 'bogus', 'meta': {'title': 'T', 'allowRedistribution': False, 'authors': ['a'] * 40 + [5], 'nested': {'x': 1}, 'k' * 50: 'long key'}},
        'origin': 'import', 'acknowledged': 1, 'topics': ['everything']}
clean = to_py(T.Clean(to_lua(evil)))
assert set(clean) == {'file', 'embedded', 'readmes', 'vrm'} and len(clean['file']) == 200
comment = clean['embedded']['comment']
assert set(clean['embedded']) == {'comment'} and len(comment.encode()) <= 48 * 1024 and set(comment) == {'あ'}, 'cut on a character boundary'
assert len(clean['readmes']) == 8 and all(set(r) == {'name', 'text', 'folder', 'matched', 'truncated', 'encoding'} and r['folder'] == 'model' for r in clean['readmes'])
assert clean['vrm']['version'] == '1.0' and set(clean['vrm']['meta']) == {'title', 'allowRedistribution', 'authors'} and len(clean['vrm']['meta']['authors']) == 16
print('PASS: untrusted terms are bounded')

# --- Packages carry embedded texts, the VRM licence and readme-named files only.
G.FS[f'mmd_hotloader/terms/{ids[4]}.json'] = json.dumps({'file': 'a.pmx', 'origin': 'import', 'acknowledged': 5, 'embedded': {'comment': 'c'}, 'vrm': vrm1,
                                                        'readmes': [{'name': 'readme.txt', 'text': 'r' * 40000, 'matched': True},
                                                                    {'name': 'notes.txt', 'text': 'private', 'matched': False}]})
package = to_py(T.ForPackage('character', ids[4]))
assert [r['name'] for r in package['readmes']] == ['readme.txt'] and len(package['readmes'][0]['text']) == 32 * 1024 and package['readmes'][0]['truncated']
assert 'acknowledged' not in package and 'origin' not in package and package['vrm']['meta']['allowRedistribution'] is False
G.FS[f'mmd_hotloader/terms/{ids[5]}.json'] = json.dumps({'embedded': {'comment': 'é' * 30000},
                                                        'readmes': [{'name': f'readme{i}.txt', 'text': 'x' * 40000, 'matched': True} for i in range(8)]})
package = to_py(T.ForPackage('character', ids[5]))
assert len(json.dumps(package, ensure_ascii=False).encode()) <= 240 * 1024 and 0 < len(package['readmes']) < 8
assert T.ForPackage('character', ids[6]) is None
# A VRM imported before records were kept still carries its licence.
G.mmdhl.library.entries[ids[6]] = to_lua({'id': ids[6], 'info': {'vrm': vrm1}})
assert to_py(T.ForPackage('character', ids[6]))['vrm']['meta']['modification'] == 'prohibited'
print('PASS: package subset')

# --- Installing a package keeps its terms unless the player has their own record.
T.SaveFromPackage('static', ids[7], to_lua({'readmes': [{'name': 'readme.txt', 'text': '再配布禁止', 'matched': True}], 'origin': 'import'}), 'Demo Pack')
installed = record(ids[7])
assert installed['origin'] == 'workshop' and installed['package'] == 'Demo Pack' and installed['topics'] == ['sharing'] and 'acknowledged' not in installed
T.SaveFromPackage('character', ids[0], to_lua({'readmes': [{'name': 'x.txt', 'text': 'overwrite'}]}), 'Other')
assert record(ids[0])['origin'] == 'import', "the player's own record wins"
print('PASS: Workshop installs')

# --- Library button text and export summaries.
text, restricted, full = T.Summary('character', ids[0])
assert restricted is True and text == 'Terms of use mention: sharing, games or other software, videos…', 'the button lists three topics'
assert full == 'Its texts mention: sharing, games or other software, videos, sexual content, violence, modification, credit', 'the tooltip lists them all'
G.FS[f'mmd_hotloader/terms/{ids[8]}.json'] = json.dumps({'file': 'plain.pmx', 'readmes': []})
assert tuple(T.Summary('character', ids[8])) == ('No terms of use found; check the model\'s download page', False)
G.FS[f'mmd_hotloader/terms/{ids[9]}.json'] = json.dumps({'file': 'calm.pmx', 'readmes': [{'name': 'readme.txt', 'text': 'Thank you for downloading!'}]})
assert tuple(T.Summary('character', ids[9])) == ('Terms of use', False)
assert T.Summary('character', ids[10]) is None
assert T.Summary('character', ids[6])[1] is True, 'VRM information of older imports counts'
items = to_lua([{'kind': 'character', 'asset': ids[0], 'name': 'Miku'}, {'kind': 'character', 'asset': ids[10], 'name': 'Unknown'},
                {'kind': 'character', 'asset': ids[9], 'name': 'Calm'}, {'kind': 'character', 'asset': ids[6], 'name': 'Alicia'}])
assert T.ExportSummary(items) == ('Terms of use: 2 of 4 selected models mention restrictions and 1 have none recorded. '
                                  "Check each model's terms before publishing.")
assert T.ExportSummary(to_lua([{'kind': 'character', 'asset': ids[9]}])) is None
calls.clear()
T.BeforeExport(items, lambda: calls.append('export'))
listing = last('RichText').text
assert '• Miku — Its texts mention: sharing' in listing and '• Unknown — no terms recorded' in listing
assert '• Calm — no restrictions found in its texts' in listing and '• Alicia — VRM licence forbids redistribution  ·  Its texts mention:' in listing
assert last('DButton', 'Export package').enabled is False
last('DCheckBoxLabel', "I have checked that each model's terms allow sharing it this way, and I take full responsibility for publishing them.").OnChange(None, True)
never = last('DCheckBoxLabel', "Don't show this warning again")
never.SetChecked(never, True)
last('DButton', 'Export package').DoClick()
assert calls == ['export'] and G.CONVARS['mmdhl_terms_warning_export'] == '0'
frames = G.Frames()
T.BeforeExport(items, lambda: calls.append('export'))
assert calls == ['export', 'export'] and G.Frames() == frames
print('PASS: library summary and export confirmation')

# --- The viewer shows where the texts came from and the disclaimer.
T.Show('static', ids[7], 'Chair')
viewer = last('DFrame')
assert viewer.title == 'Terms of use: Chair' and 'These texts came with the Workshop package "Demo Pack".' in G.Texts(viewer)
T.Show('character', ids[10], None)
assert 'No texts were recorded for this model.' in G.Texts(last('DFrame')) and 'You alone are responsible' in G.Texts(last('DFrame'))
print('PASS: viewer')

# --- Source limits: labels keep 1023 bytes of text, and text starting with # is
# looked up as a localization token (through a 1024-character buffer).
for folder in sorted((ROOT / 'addon/resource/localization').iterdir()):
    if not folder.is_dir(): continue
    for line in (folder / 'mmdhl.properties').read_text(encoding='utf-8').splitlines():
        if line.startswith('mmdhl.terms.'):
            key, value = line.split('=', 1)
            assert len(value.encode()) < 1000, f'{folder.name} {key} is {len(value.encode())} bytes; a label shows 1023'
markdown = '# Model\n' + 'Redistribution is prohibited.\n' * 200
G.FS[f'mmd_hotloader/terms/{ids[11]}.json'] = json.dumps({'file': 'md.pmx', 'readmes': [{'name': '#notes.md', 'text': markdown, 'matched': True}]})
T.Show('character', ids[11], 'Markdown')
shown = last('RichText', markdown)
assert shown is not None and shown.first == '#', 'a leading # is sent on its own'
assert '#notes.md' not in [t.label for t in last('DPropertySheet').sheets.values()] and ' #notes.md' in [t.label for t in last('DPropertySheet').sheets.values()]
print('PASS: Source text limits')
