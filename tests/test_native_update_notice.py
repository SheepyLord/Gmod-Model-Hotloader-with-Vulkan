"""The native update reminder (installation_ui.lua) against a simulated client: the
window opens a few seconds after joining, once per game run (not again on the next map);
Skip this version lasts until a newer release is recommended; Don't remind me again turns
mmdhl_native_update_reminder off for good, and the installation window's checkbox turns it
back on; a pending binary problem keeps it closed, other problems' notice waits until it
closes (also when VGUI deletes the closed window a frame later), and a required update
replaces the problem notice once per game run unless dismissed; Download opens only the
public releases page, the mirror only a plain https address. The banner shows an update as
a line, never as a problem (Dismiss hides problems first, then skips the release), and the
installation window always does, with the server's update for its administrators; a
language switch relabels the window. Also the dialog other features show for a native
function the binary lacks."""
from pathlib import Path
import json
from lupa import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
CATALOGUES = {lang: (ROOT / f'addon/resource/localization/{lang}/mmdhl.properties').read_text(encoding='utf-8') for lang in ('en', 'fr')}
I18N = (ROOT / 'addon/lua/mmdhl/i18n.lua').read_text(encoding='utf-8')
UI = (ROOT / 'addon/lua/mmdhl/installation_ui.lua').read_text(encoding='utf-8')
RELEASES = 'https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/releases'
MIRROR = 'https://pan.baidu.com/s/1eUaJAUhnnnGpNSnvFojmwQ?pwd=lord'
STATE = 'mmd_hotloader/native_update.json'
DISMISSED = 'mmd_hotloader/installation_dismissed.txt'

# What outlives a Lua session: the DATA folder, archived convars and the game's clocks.
DATA, CONVARS, CLOCK = {}, {}, {'now': 1_800_000_000, 'up': 120.0}
PRELUDE = r'''
SERVER=false CLIENT=true
ScrH=function() return 1080 end ScrW=function() return 1920 end
math.Clamp=function(v,low,high) return math.min(math.max(v,low),high) end math.Round=function(v) return math.floor(v+.5) end
isstring=function(v) return type(v)=='string' end istable=function(v) return type(v)=='table' end isfunction=function(v) return type(v)=='function' end
IsValid=function(v) return v~=nil and v~=false and not (type(v)=='table' and v.removed) end
COMMANDS={} concommand={Add=function(name,f) COMMANDS[name]=f end}
TIMERS={} timer={Simple=function(delay,f) TIMERS[#TIMERS+1]={delay,f} end,Create=function() end,Remove=function() end}
-- As in the game, a removed panel stays valid until VGUI deletes it on a later frame: here
-- after the timers due, the order that hides a closing window from timer.Simple(0,...).
DELETING={}
function RunTimers()
 local due=TIMERS TIMERS={} table.sort(due,function(a,b) return a[1]<b[1] end) for _,t in ipairs(due) do t[2]() end
 local gone=DELETING DELETING={} for _,p in ipairs(gone) do p.removed=true end
end
HOOKS={} hook={Add=function(event,name,f) HOOKS[event]=HOOKS[event] or {} HOOKS[event][name]=f end,
 Run=function(event,...) local list={} for name,f in pairs(HOOKS[event] or {}) do if not (type(name)=='table' and name.removed) then list[#list+1]=f end end for _,f in ipairs(list) do f(...) end end}
CALLBACKS={} cvars={AddChangeCallback=function(name,f) CALLBACKS[name]=CALLBACKS[name] or {} table.insert(CALLBACKS[name],f) end}
local function convar(name,default) if PY_CVAR(name)==nil then PY_SETCVAR(name,default) end return {GetBool=function() return PY_CVAR(name)=='1' end,GetString=function() return PY_CVAR(name) end,GetInt=function() return tonumber(PY_CVAR(name)) or 0 end} end
CreateClientConVar=function(name,default) return convar(name,default) end GetConVar=function(name) return convar(name,'') end
RunConsoleCommand=function(name,value) PY_SETCVAR(name,tostring(value)) for _,f in ipairs(CALLBACKS[name] or {}) do f(name,nil,tostring(value)) end end
SP=true ADMIN=false HOST=false PLAYER={IsAdmin=function() return ADMIN end,IsListenServerHost=function() return HOST end}
game={SinglePlayer=function() return SP end} LocalPlayer=function() return PLAYER end
OPENED={} gui={OpenURL=function(url) OPENED[#OPENED+1]=url end}
surface={SetFont=function() end,GetTextSize=function(t) return 7*#tostring(t),14 end,SetDrawColor=function() end,DrawRect=function() end,CreateFont=function() end}
SetClipboardText=function() end Derma_Message=function() end
CREATED={}
function panel(kind)
 local p={kind=kind,children={},visible=true}
 local methods={
  Add=function(self,k) local c=panel(k) self.children[#self.children+1]=c return c end,
  SetText=function(self,t) self.text=t end,GetText=function(self) return self.text end,SetTitle=function(self,t) self.title=t end,
  SetTooltip=function(self,t) self.tooltip=t end,SetVisible=function(self,v) self.visible=v end,IsVisible=function(self) return self.visible end,
  SetConVar=function(self,c) self.convar=c end,GetFont=function() return 'font' end,SetWide=function(self,w) self.wide=w end,GetWide=function(self) return self.wide or 100 end,
  -- DFrame:Close hides the frame, removes it, then calls OnClose.
  Remove=function(self) DELETING[#DELETING+1]=self end,Close=function(self) self.visible=false self:Remove() if self.OnClose then self:OnClose() end end,
 }
 -- Methods (CamelCase) do nothing unless listed; fields (lowercase) stay nil until set.
 return setmetatable(p,{__index=function(t,k) if type(k)=='string' and k:match('^%u') then return methods[k] or function() end end end})
end
vgui={Create=function(kind) local p=panel(kind) CREATED[#CREATED+1]=p return p end}
QUERIES={}
Derma_Query=function(text,title,b1,f1,b2) local w=panel('DFrame') w.query={text=text,title=title,accept=b1,click=f1,cancel=b2} QUERIES[#QUERIES+1]=w return w end
os.time=function() return PY_NOW() end SysTime=function() return PY_UP() end
file={Read=function(path,where) if where=='GAME' then return PY_CATALOGUE(path) end return PY_DATA_READ(path) end,Write=function(path,text) PY_DATA_WRITE(path,text) end,CreateDir=function() end}
util={JSONToTable=function(s) return PY_DECODE(s) end,TableToJSON=function(t) return PY_ENCODE(t) end}
mmdhl={}
'''


def session(status, server=None):
    """A Lua session (map) of one client with this installation status."""
    lua = LuaRuntime(unpack_returned_tuples=True)
    g = lua.globals()
    to_lua = lambda x: lua.table_from({k: to_lua(v) for k, v in x.items()}) if isinstance(x, dict) else lua.table_from([to_lua(v) for v in x]) if isinstance(x, list) else x
    def from_lua(t):
        if hasattr(t, 'items'):
            d = dict(t.items())
            return [from_lua(d[i]) for i in sorted(d)] if d and all(isinstance(k, int) for k in d) else {k: from_lua(v) for k, v in d.items()}
        return t
    g.PY_DECODE = lambda s: to_lua(json.loads(s)) if s else None
    g.PY_ENCODE = lambda t: json.dumps(from_lua(t))
    g.PY_CVAR = lambda name: CONVARS.get(name)
    g.PY_SETCVAR = lambda name, value: CONVARS.__setitem__(name, value)
    g.PY_NOW = lambda: CLOCK['now']
    g.PY_UP = lambda: CLOCK['up']
    g.PY_DATA_READ = lambda path: DATA.get(path)
    g.PY_DATA_WRITE = lambda path, text: DATA.__setitem__(path, text)
    g.PY_CATALOGUE = lambda path: next((text for lang, text in CATALOGUES.items() if path == f'resource/localization/{lang}/mmdhl.properties'), None)
    lua.execute(PRELUDE)
    lua.execute(I18N)
    lua.execute(UI)
    g.S = to_lua(status)
    g.mmdhl.serverInstallation = to_lua(server) if server else None
    lua.execute('mmdhl.GetInstallationStatus=function() return S end')
    return lua, g


def join(lua):
    """InitPostEntity and the seconds after it."""
    lua.eval('hook.Run')('InitPostEntity'); lua.execute('RunTimers()')


def windows(g, title=None):
    """The windows on screen (closed ones are hidden at once, deleted a frame later)."""
    return [p for p in g.CREATED.values() if p.kind == 'DFrame' and not p.removed and p.visible and (title is None or p.title == title)]


def notices(g, key='install.notice'):
    """The problem notices shown (the small panel at the top right)."""
    return [p for p in g.CREATED.values() if p.kind == 'DPanel' and find(p, L(g, key))]


def find(p, text):
    """The descendant whose text is text."""
    if p.text == text: return p
    for c in p.children.values():
        found = find(c, text)
        if found is not None: return found
    return None


def L(g, key, **vars):
    return g.mmdhl.L(key, g.PY_DECODE(json.dumps(vars)) if vars else None)


def status(installed='2.2.0', recommended='2.3.0', issues=(), **update):
    s = {'installed': installed, 'recommended': recommended, 'issues': list(issues), 'download': RELEASES + '/tag/' + recommended, 'downloadAlt': MIRROR}
    if installed != recommended:
        u = dict(installed=installed, recommended=recommended, url=s['download'], altUrl=MIRROR, approved=True); u.update(update)
        s['update'] = {k: v for k, v in u.items() if v is not None}
    return s


def new_run():
    CLOCK['now'] += 3600; CLOCK['up'] = 90.0


def new_map():
    CLOCK['now'] += 600; CLOCK['up'] += 600


# --- Once per game run, a few seconds after joining.
lua, g = session(status())
lua.eval('hook.Run')('InitPostEntity')
assert not windows(g), 'the window opened before the delay'
delays = sorted(t[1] for t in g.TIMERS.values())
assert delays == [1, 5], delays
lua.execute('RunTimers()')
title = L(g, 'install.update.title')
shown = windows(g, title)
assert len(shown) == 1, [p.title for p in windows(g)]
frame = shown[0]
body = find(frame, L(g, 'install.update.text', installed='2.2.0', recommended='2.3.0') + '\n\n' + L(g, 'install.alternative_link', url=MIRROR))
assert body is not None, 'the window does not say which releases'
assert json.loads(DATA[STATE])['reminded'] == CLOCK['now'] and json.loads(DATA[STATE])['schema'] == 1
join(lua); assert len(windows(g, title)) == 1, 'the window opened twice in one session'
# Remind me later closes it; the next map of the same game run stays quiet, the next run reminds again.
later = find(frame, L(g, 'install.update.button.later')); later.DoClick(); lua.execute('RunTimers()')
assert frame.removed and not windows(g, title)
new_map(); lua, g = session(status()); join(lua)
assert not windows(g, title), 'the window came back on the next map'
new_run(); lua, g = session(status()); join(lua)
assert len(windows(g, title)) == 1, 'the next game run did not remind'
print('PASS: the update window opens once per game run, a few seconds after joining')

# --- Download opens only the releases page; the mirror only a plain https address.
frame = windows(g, title)[0]
find(frame, L(g, 'install.update.button.download')).DoClick()
find(frame, L(g, 'install.button.download_alternative')).DoClick()
assert list(g.OPENED.values()) == [RELEASES + '/tag/2.3.0', MIRROR], list(g.OPENED.values())
for download, mirror in ((RELEASES + 'X', 'http://example.com/x'), ('https://example.com/releases', 'https://example.com/a b'), ('https://github.com/other/repo/releases', 'javascript:alert(1)')):
    new_run(); s = status(); s['download'], s['downloadAlt'] = download, mirror
    lua, g = session(s); join(lua)
    frame = windows(g, title)[0]
    find(frame, L(g, 'install.update.button.download')).DoClick()
    alt = find(frame, L(g, 'install.button.download_alternative')); alt.DoClick()
    assert not list(g.OPENED.values()) and alt.visible is False, (download, mirror, list(g.OPENED.values()))
print('PASS: Download and Alternative download open only allowed addresses')

# --- Skip this version: not for 2.3.0 again (next run or map), but for a newer release.
new_run(); lua, g = session(status()); join(lua)
find(windows(g, title)[0], L(g, 'install.update.button.skip')).DoClick()
assert json.loads(DATA[STATE])['skipped'] == '2.3.0' and not windows(g, title)
new_run(); lua, g = session(status()); join(lua)
assert not windows(g, title), 'a skipped release reminded again'
assert g.mmdhl.NativeUpdateDue() is None and g.mmdhl.NativeUpdate().recommended == '2.3.0'
new_run(); lua, g = session(status(recommended='2.4.0')); join(lua)
assert len(windows(g, title)) == 1, 'a newer release did not remind'
# An advisory for the installed release (policy.revoked) is news: it reminds again.
new_run(); lua, g = session(status(recommended='2.3.0', advisory='install.advisory.none')); join(lua)
frame = windows(g, title)[0]
assert L(g, 'install.update.advisory') in frame.children[3].children[1].text, 'the advisory is missing'
print('PASS: Skip this version lasts until a newer release is recommended')

# --- Don't remind me again: the convar, for good; the installation window turns it back on.
find(frame, L(g, 'install.update.button.never')).DoClick()
assert CONVARS['mmdhl_native_update_reminder'] == '0' and not windows(g, title)
for recommended in ('2.3.0', '2.4.0', '9.0.0'):
    new_run(); lua, g = session(status(recommended=recommended)); join(lua)
    assert not windows(g, title), 'the reminder came back after Don\'t remind me again'
    banner = g.mmdhl.AddInstallationBanner(g.panel('DPanel'))
    assert banner.visible is False, 'the External Models banner still shows the update'
g.COMMANDS['mmdhl_installation'](); installation = windows(g, L(g, 'install.window_title'))[0]
box = [c for c in installation.children.values() if c.kind == 'DCheckBoxLabel'][0]
assert box.convar == 'mmdhl_native_update_reminder' and box.text == L(g, 'install.update.setting')
summary = installation.children[1].children[2].children[1].text
assert L(g, 'install.update.banner', recommended='9.0.0', installed='2.2.0') in summary and L(g, 'install.update.available_tag') in summary, 'the installation window does not show the update'
lua.eval('RunConsoleCommand')('mmdhl_native_update_reminder', '1')
assert g.mmdhl.NativeUpdateDue() is not None
new_run(); lua, g = session(status(recommended='9.0.0')); join(lua)
assert len(windows(g, title)) == 1, 'turning reminders back on did not remind'
print("PASS: Don't remind me again turns the reminder off for good; the checkbox turns it back on")

# --- Never beside the problem notice. A pending binary problem keeps the window closed (the
# notice says to download); other problems' notice waits until the window closes.
DATA.pop(STATE, None)
missing = {'code': 'missing', 'component': 'worker', 'message': 'Missing lua/bin/mmdhl_worker.exe', 'feature': 'imports'}
new_run(); lua, g = session(status(issues=[missing])); join(lua)
assert not windows(g, title), 'the update window opened beside a binary problem'
assert len(notices(g, 'install.notice_binary')) == 1, 'the problem notice is missing'
# Dismissed, the binary problem raises no notice, and still no update window.
new_run(); lua, g = session(status(issues=[missing])); g.mmdhl.DismissInstallation(); join(lua)
assert not windows(g, title) and not g.mmdhl.installationNoticeShown, 'the update window opened beside a dismissed binary problem'
DATA.pop(DISMISSED)
# A warning (a game build no profile describes, after every Garry's Mod update): the window
# first, the notice once it closes; later maps of the run get the notice at once.
warning = {'code': 'game_unverified', 'component': 'game', 'message': 'untested', 'feature': 'rendering', 'warning': True}
new_run(); lua, g = session(status(issues=[warning])); join(lua)
assert len(windows(g, title)) == 1 and not notices(g), 'a warning kept the update window closed, or showed beside it'
find(windows(g, title)[0], L(g, 'install.update.button.later')).DoClick(); lua.execute('RunTimers()')
assert len(notices(g)) == 1, 'the warning got no notice after the update window closed'
new_map(); lua, g = session(status(issues=[warning])); join(lua)
assert not windows(g, title) and len(notices(g)) == 1
# A problem that comes up while the window is open (the worker probe) gets its notice when it
# closes, although the closing window is deleted only a frame later.
new_run(); lua, g = session(status()); join(lua)
lua.execute("S.issues[1]={code='worker_failed',component='worker',message='no answer',feature='imports'} hook.Run('MMDHL.InstallationChanged',S)")
assert not notices(g), 'the notice opened beside the update window'
find(windows(g, title)[0], L(g, 'install.update.button.later')).DoClick(); lua.execute('RunTimers()')
assert len(notices(g)) == 1, 'the problem that came up meanwhile got no notice'
# An outdated release's own warning is the reminder's: no notice, the window. So is a compatibility
# policy newer than the binary while an update is known; without one it is a problem as before.
outdated = {'code': 'outdated_release', 'component': 'module', 'message': 'old', 'feature': 'core', 'warning': True}
fallback = {'code': 'compatibility_fallback', 'component': 'compatibility', 'message': 'older binary', 'feature': 'core', 'warning': True}
for issues in ([outdated], [outdated, fallback]):
    new_run(); lua, g = session(status(issues=issues, approved=None)); join(lua)
    assert len(windows(g, title)) == 1 and not g.mmdhl.installationNoticeShown, issues
    _, _, _, pending = g.mmdhl.InstallationSummary(); assert pending == 0 and not g.mmdhl.InstallationDismissed()
lua, g = session(status(installed='2.3.0', issues=[fallback])); join(lua)
_, _, _, pending = g.mmdhl.InstallationSummary(); assert pending == 1 and len(notices(g)) == 1
# A module this Lua cannot use: the window, worded as required and without skipping, instead of
# the notice; once per game run (it takes the mouse and keyboard), later maps get the notice.
blocked = {'code': 'outdated', 'component': 'module', 'message': 'no verification', 'feature': 'core'}
required = status(issues=[blocked], required=True)
new_run(); lua, g = session(required); join(lua)
frame = windows(g, L(g, 'install.update.title_required'))
assert len(frame) == 1 and not g.mmdhl.installationNoticeShown, 'a required update did not open its window'
frame = frame[0]
assert find(frame, L(g, 'install.update.button.skip')).visible is False and find(frame, L(g, 'install.update.button.never')).visible is False and find(frame, L(g, 'common.close')) is not None
# Closed, it was this map's notice.
frame.Close(frame); lua.execute('RunTimers()')
assert not g.mmdhl.installationNoticeShown, 'the notice repeated the required update'
new_map(); lua, g = session(required); join(lua)
assert not windows(g) and len(notices(g, 'install.notice_binary')) == 1, 'a required update opened its window again in the same game run'
# Dismiss in the banner silences both, as it did the notice, until the problems change.
new_run(); lua, g = session(required); g.mmdhl.DismissInstallation(); join(lua)
assert not windows(g) and not g.mmdhl.installationNoticeShown, 'Dismiss did not silence the required update'
DATA.pop(DISMISSED)
# With reminders off, the notice as before.
CONVARS['mmdhl_native_update_reminder'] = '0'
new_run(); lua, g = session(required); join(lua)
assert not windows(g) and g.mmdhl.installationNoticeShown
CONVARS['mmdhl_native_update_reminder'] = '1'
print('PASS: the update window never opens beside the problem notice; other notices follow it; a required update replaces it once per run')

# --- The banner: a line while due (nothing pending), hidden once skipped; the window always has it.
DATA.pop(STATE, None)
new_run(); lua, g = session(status(issues=[outdated], approved=None))
banner = g.mmdhl.AddInstallationBanner(g.panel('DPanel'))
summary = banner.children[2].children[1]
line = L(g, 'install.update.banner', recommended='2.3.0', installed='2.2.0')
assert banner.visible is True and line in summary.text, summary.text
_, _, _, pending = g.mmdhl.InstallationSummary(); assert pending == 0, 'the update counts as a problem'
controls = banner.children[1]
dismiss = find(controls, L(g, 'install.button.dismiss'))
assert dismiss.visible is True and dismiss.tooltip == L(g, 'install.update.dismiss_tip', recommended='2.3.0'), 'Dismiss does not say that it skips the release'
dismiss.DoClick()
assert banner.visible is False and json.loads(DATA[STATE])['skipped'] == '2.3.0', 'Dismiss did not hide the update line'
always = g.mmdhl.AddInstallationBanner(g.panel('DFrame'), True)
assert line in always.children[2].children[1].text and L(g, 'install.update.available_tag') in always.children[2].children[1].text
# Beside a problem, Dismiss hides the problem first and keeps the reminder; then the update line.
DATA.pop(STATE, None)
new_run(); lua, g = session(status(issues=[warning]))
banner = g.mmdhl.AddInstallationBanner(g.panel('DPanel'))
dismiss = find(banner.children[1], L(g, 'install.button.dismiss'))
assert dismiss.tooltip == L(g, 'install.button.dismiss_tip')
dismiss.DoClick()
assert g.mmdhl.InstallationDismissed() and STATE not in DATA and banner.visible is True, 'Dismiss skipped the release beside a problem'
assert dismiss.tooltip == L(g, 'install.update.dismiss_tip', recommended='2.3.0') and line in banner.children[2].children[1].text
dismiss.DoClick()
assert banner.visible is False and json.loads(DATA[STATE])['skipped'] == '2.3.0'
DATA.pop(DISMISSED)
# No update: no line and no tag.
lua, g = session(status(installed='2.3.0'))
always = g.mmdhl.AddInstallationBanner(g.panel('DFrame'), True)
assert L(g, 'install.update.available_tag') not in always.children[2].children[1].text and 'Native update' not in always.children[2].children[1].text
assert g.mmdhl.OpenNativeUpdate().title == L(g, 'install.window_title'), 'mmdhl_native_update without an update should open the installation window'
# The server's update, for its administrators only.
server = {'schema': 1, 'realm': 'server', 'features': {'core': True}, 'issues': [], 'update': {'installed': '2.1.0-native.12', 'recommended': '2.3.0', 'approved': True}}
lua, g = session(status(installed='2.3.0'), server=server); g.SP = False
server_line = L(g, 'install.update.server', installed='2.1.0-native.12', recommended='2.3.0')
assert server_line not in g.mmdhl.AddInstallationBanner(g.panel('DFrame'), True).children[2].children[1].text, 'a player who is no administrator sees the server line'
g.ADMIN = True
assert server_line in g.mmdhl.AddInstallationBanner(g.panel('DFrame'), True).children[2].children[1].text
assert server_line not in g.mmdhl.AddInstallationBanner(g.panel('DPanel')).children[2].children[1].text, 'the server line belongs to the installation window'
# A dedicated server's administrator sees it even when their own game runs that release too;
# the listen-server host, whose game shares the server's files, has only their own line.
lua, g = session(status(installed='2.1.0-native.12'), server=server); g.SP = False; g.ADMIN = True
assert server_line in g.mmdhl.AddInstallationBanner(g.panel('DFrame'), True).children[2].children[1].text, 'a dedicated server administrator with the same release misses the server line'
g.HOST = True
assert server_line not in g.mmdhl.AddInstallationBanner(g.panel('DFrame'), True).children[2].children[1].text
print('PASS: the banner shows an update as a line, never as a problem; the installation window always does')

# --- A language switch relabels the open window.
DATA.pop(STATE, None)
new_run(); lua, g = session(status()); join(lua)
frame = windows(g, title)[0]
CONVARS['mmdhl_language'] = 'fr'; g.mmdhl.I18n.Check()
assert frame.title == 'Model Hotloader — mise à jour disponible', frame.title
for key in ('install.update.button.download', 'install.update.button.later', 'install.update.button.skip', 'install.update.button.never'):
    assert find(frame, L(g, key)) is not None and L(g, key) != key, key
assert find(frame, 'Remind me later') is None
CONVARS['mmdhl_language'] = ''
print('PASS: a language switch relabels the update window')

# --- The dialog other features show for a native function the binary lacks.
DATA.pop(STATE, None)
new_run(); lua, g = session(status())
dialog = g.mmdhl.ShowNativeUpdateNeeded(L(g, 'physics_editor.feature'), '2.3.0')
assert dialog.query.text == L(g, 'install.update.needed', feature=L(g, 'physics_editor.feature'), release='2.3.0') and dialog.query.text.startswith(L(g, 'physics_editor.feature') + '\n'), dialog.query.text
assert dialog.query.title == L(g, 'install.update.needed_title') and dialog.query.accept == L(g, 'install.update.button.show') and dialog.query.cancel == L(g, 'common.close')
assert lua.eval('rawequal')(g.mmdhl.ShowNativeUpdateNeeded(L(g, 'physics_editor.feature'), '2.3.0'), dialog) and len(g.QUERIES) == 1, 'the same feature opened a second dialog'
dialog.query.click()
assert len(windows(g, title)) == 1, 'the dialog did not open the update window'
# Closed (deleted only a frame later), the next request opens a new dialog.
dialog.Close(dialog)
again = g.mmdhl.ShowNativeUpdateNeeded(L(g, 'physics_editor.feature'), '2.3.0')
assert not lua.eval('rawequal')(again, dialog) and len(g.QUERIES) == 2, 'a closed dialog was reused'
lua, g = session(status(installed='2.3.0', recommended='2.3.0'))
g.mmdhl.ShowNativeUpdateNeeded(L(g, 'bonemap.feature'), '2.4.0').query.click()
assert len(windows(g, L(g, 'install.window_title'))) == 1, 'without a known update the dialog should open the installation window'
print('PASS: ShowNativeUpdateNeeded shows one dialog per feature that opens the update or installation window')
