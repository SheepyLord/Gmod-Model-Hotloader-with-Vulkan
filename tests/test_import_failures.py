"""Import failures explain why (issue #8). The failure window prefers the importer's error
code: a translated cause, hint and step, and a Where line built from the places the error
names. Older natives (no codes) keep the English message and the phrase hints, whose order
no longer sends spring-bone errors to the skeleton hint. The window sizes itself to its
text, Copy details holds everything a report needs (code, context, data, exit code, log,
build), and the window also opens when the importer cannot start or the job is lost. A
character imported without a ragdoll lists the missing body parts, with Assign bones…
only when the bone window exists. Spawn-time fit errors reach players as a translated
token. Runs library.lua and server.lua's mmdhl.Spawn against a simulated game."""
import json
from pathlib import Path
from lupa import lua51
from lua_i18n import attach
from lua_source import definition

ROOT = Path(__file__).resolve().parents[1]
LIBRARY = (ROOT / 'addon/lua/mmdhl/library.lua').read_text(encoding='utf-8')
SERVER = (ROOT / 'addon/lua/mmdhl/server.lua').read_text(encoding='utf-8')


def json_bridge(rt):
    def encode(value):
        def conv(v):
            if hasattr(v, 'keys') and hasattr(v, 'values'):
                keys = list(v.keys())
                if not keys: return {}
                if all(isinstance(k, (int, float)) and k == int(k) for k in keys) and sorted(int(k) for k in keys) == list(range(1, len(keys) + 1)):
                    return [conv(v[k]) for k in range(1, len(keys) + 1)]
                return {str(k): conv(x) for k, x in v.items()}
            return v
        return json.dumps(conv(value), ensure_ascii=False)
    def decode(text):
        try: data = json.loads(text)
        except Exception: return None
        def back(v):
            if isinstance(v, dict): return rt.table_from({k: back(x) for k, x in v.items()})
            if isinstance(v, list): return rt.table_from([back(x) for x in v])
            return v
        return back(data)
    rt.globals().py_encode = encode
    rt.globals().py_decode = decode


lua = lua51.LuaRuntime(unpack_returned_tuples=True)
json_bridge(lua)
lua.execute(r'''
SERVER=false CLIENT=true
ScrW=function() return 1920 end ScrH=function() return 1080 end
math.Clamp=function(v,a,b) return math.max(a,math.min(b,v)) end
isstring=function(v) return type(v)=='string' end istable=function(v) return type(v)=='table' end
isnumber=function(v) return type(v)=='number' end isfunction=function(v) return type(v)=='function' end
IsValid=function(v) return v~=nil and v~=false and not (type(v)=='table' and v.removed) end
string.GetFileFromFilename=function(p) return (p:match('[^/\\]+$')) end
string.Trim=function(s) return s:match('^%s*(.-)%s*$') end
string.Comma=function(n) local s=tostring(math.floor(n)) local out=s:reverse():gsub('(%d%d%d)','%1,'):reverse() return (out:gsub('^,','')) end
table.Copy=function(t) local c={} for k,v in pairs(t) do c[k]=type(v)=='table' and table.Copy(v) or v end return c end
table.HasValue=function(t,v) for _,x in pairs(t) do if x==v then return true end end return false end
Color=function(r,g,b,a) return {r=r,g=g,b=b,a=a or 255} end
NOTIFY_GENERIC,NOTIFY_ERROR,NOTIFY_HINT=0,1,3
TIMERS={} timer={Simple=function(_,fn) TIMERS[#TIMERS+1]=fn end,Create=function() end,Remove=function() end}
function RUN_TIMERS() local list=TIMERS TIMERS={} for _,fn in ipairs(list) do fn() end end
NOTES={} notification={AddLegacy=function(text) NOTES[#NOTES+1]=text end,AddProgress=function() end,Kill=function() end}
HOOKS={} hook={Add=function(event,name,fn) HOOKS[event..'/'..name]=fn end,Run=function() end}
net={Receive=function() end} chat={AddText=function() end} surface={PlaySound=function() end}
system={IsWindowed=function() return true end}
game={SinglePlayer=function() return true end}
NOW=0 RealTime=function() NOW=NOW+1 return NOW end
CLIPBOARD=nil SetClipboardText=function(t) CLIPBOARD=t end
MESSAGES={} Derma_Message=function(...) MESSAGES[#MESSAGES+1]={...} end Derma_Query=function(...) MESSAGES[#MESSAGES+1]={...} end
FILES={} file={Read=function(p) return FILES[p] end,Write=function(p,v) FILES[p]=v end,CreateDir=function() end,Find=function() return {},{} end}
util={TableToJSON=function(v) return py_encode(v) end,JSONToTable=function(v) return py_decode(v) end}
-- Panels record what the window shows; Add makes a child, unknown methods are no-ops.
function panel(kind)
 local p={kind=kind,children={}}
 setmetatable(p,{__index=function(t,k) return function(self,...)
  if k=='Add' then local c=panel(...) t.children[#t.children+1]=c return c end
  if k=='SetText' then t.text=... elseif k=='SetTall' then t.tall=... elseif k=='SetSize' then t.w,t.h=... elseif k=='Close' or k=='Remove' then t.removed=true end
 end end})
 p.btnMinim=setmetatable({},{__index=function() return function() end end}) p.btnMaxim=p.btnMinim
 return p
end
FRAMES={} vgui={Create=function(kind) local f=panel(kind) FRAMES[#FRAMES+1]=f return f end}
surface.SetFont=function() end surface.GetTextSize=function(t) return #tostring(t)*7,16 end
mmdhl={native={},library={entries={}}}
mmdhl.Decode=function(v,err) if v==nil then return nil,err end if type(v)=='string' then return py_decode(v) end return v end
mmdhl.native.DeleteAssets=function() return '{"removedFiles":0,"removedBytes":0,"pendingFiles":0,"unreadableManifests":0}' end
mmdhl.UI={metrics=function() return function(n) return n end,{Body='Body',Small='Small',Title='Title',Strong='Strong'} end,colors={ink='ink',muted='muted',accent='accent'},ownScale=function() end,
 label=function(parent,text,font,height) local l=parent:Add('DLabel') l.text=text l.font=font l.tall=height return l end,
 button=function(parent,text,callback,height,font,style) local b=parent:Add('DButton') b.text=text b.DoClick=callback return b end}
-- Every text a panel tree shows, in order.
function TEXTS(p,out) out=out or {} if p.text then out[#out+1]=p.text end for _,c in ipairs(p.children) do TEXTS(c,out) end return out end
function FIND(p,text) if p.text==text then return p end for _,c in ipairs(p.children) do local f=FIND(c,text) if f then return f end end end
function HAS(p,part) for _,t in ipairs(TEXTS(p)) do if tostring(t):find(part,1,true) then return true end end return false end
''')
attach(lua)
lua.execute(LIBRARY)
L = lua.eval('mmdhl.L')

# ---- hints: the error code first, then the family, then the English phrases ----
lua.execute(r'''
local L=mmdhl.L
assert(mmdhl.ImportHint('Invalid PMX file: it ends early','pmx.truncated')==L'library.hint.pmx_truncated')
assert(mmdhl.ImportHint('whatever','pmx.some_new_code')==L'library.hint.pmx','an unknown code of a known family gets the family hint')
assert(mmdhl.ImportHint('x','vrm.json')==L'library.hint.vrm_damaged' and mmdhl.ImportHint('x','io.locked')==L'library.hint.io_locked')
assert(mmdhl.ImportHint('x','format.renamed',{extension='.glb'}):find('.glb',1,true),'the renamed-file hint names the extension')
assert(mmdhl.ImportHint('x','character.bone_map')==L'library.hint.character_bone_map','the character codes keep their hints')
-- Older natives: phrase order. Spring-bone errors are not skeleton problems; nanoem's PMX errors are not "truncated" files.
assert(mmdhl.ImportHint('Invalid spring bone stiffness')==L'library.hint.vrm_spring')
assert(mmdhl.ImportHint('Spring bone data refers to a bone outside the model')==L'library.hint.vrm_spring')
assert(mmdhl.ImportHint('Invalid/truncated PMX/PMD model (nanoem status 101)')==L'library.hint.pmx')
assert(mmdhl.ImportHint('Truncated VRM file: a buffer view points past its buffer')==L'library.hint.vrm_damaged')
assert(mmdhl.ImportHint('Cyclic bone hierarchy')==L'library.hint.skeleton','other bone errors keep the skeleton hint')
assert(mmdhl.ImportHint('Import worker missing')==L'library.hint.worker_missing' and mmdhl.ImportHint('Cannot start import worker')==L'library.hint.worker_start')
-- A code no hint knows (unknown, json) falls back to the phrases.
assert(mmdhl.ImportHint('Cannot open file C:/x.obj','unknown')==L'library.hint.cannot_open' and mmdhl.ImportHint('?','json')==L'library.hint.default')
-- A crash while the file picker is open, or before the importer's first step, is not the file's fault.
assert(mmdhl.ImportHint('x','worker.crash',nil,'pick')==L'library.hint.picker_crash' and mmdhl.ImportHint('x','worker.crash',nil,'textures')==L'library.hint.worker_crash')
assert(mmdhl.ImportHint('x','worker.crash',nil,'start')==L'library.hint.worker_start' and mmdhl.ImportHint('x','pmx.truncated',nil,'pick')==L'library.hint.pmx_truncated')
assert(mmdhl.ImportStage({stageCode='pick'})==L'library.stage.pick')
''')
print('PASS: hints by error code and family first; the English phrases (fixed order) for older natives')

# ---- cause, step and Where ----
lua.execute(r'''
local L=mmdhl.L
local status={state='failed',error='Invalid PMX file: morph 1 (the one after “まばたき”) is damaged and cannot be read. Reading stopped at byte 3,210 of 6,377 (nanoem status 107)',
 errorCode='pmx.section_corrupt',stage='Parsing skeleton, materials and physics',stageCode='parse',filename='x.pmx',source='C:/m/x.pmx',kind='character',context={},
 errorDetails={section='morph',offset=3210,size=6377,status=107,where={{kind='morph',index=1}},after={kind='morph',index=0,name='まばたき'}}}
assert(mmdhl.ImportCause(status)==L'library.cause.pmx_damaged','the translated cause wins over the English message')
assert(mmdhl.ImportStage(status)==L'library.stage.parse')
local where=mmdhl.ImportWhere(status)
assert(where=='morph 1 › after morph 0 “まばたき” › byte 3,210 of 6,377',where)
-- A value at fault, inside a named element.
where=mmdhl.ImportWhere({errorDetails={where={{kind='soft_body',index=0,name='soft fabric'}},field='velocity correction factor',value='NaN'}})
assert(where=='soft body 0 “soft fabric” › velocity correction factor: NaN',where)
-- VRM places; a humanoid bone known by name only.
where=mmdhl.ImportWhere({errorDetails={where={{kind='mesh',index=0,name='Body'},{kind='primitive',index=2},{kind='accessor',index=7}}}})
assert(where=='mesh 0 “Body” › primitive 2 › accessor 7',where)
assert(mmdhl.ImportWhere({errorDetails={where={{kind='humanoid_bone',name='leftFoot'}}}})=='humanoid bone “leftFoot”')
assert(mmdhl.ImportCause({errorCode='vrm.humanoid',errorDetails={bone='leftFoot'},error='x'})==L('library.cause.vrm_humanoid',{bone='leftFoot'}))
-- No places: the English context; a crash: the last progress line.
assert(mmdhl.ImportWhere({context={'Converting VRM avatar','mesh 0 “Body”'}})=='Converting VRM avatar › mesh 0 “Body”')
local crash={errorCode='worker.crash',error='The import worker exited before completion after an access violation (exit code 0xC0000005).',exitCode=3221225477,
 errorDetails={exitCode=3221225477,exitCodeHex='0xC0000005',cause='access_violation'},stageCode='textures',stage='Preparing textures',detail='Material 3 of 9 “Hair”: hair.png'}
assert(mmdhl.ImportCause(crash)==L('library.cause.worker_crash',{code='0xC0000005'}))
assert(mmdhl.ImportWhere(crash)==L('library.where.progress',{detail='Material 3 of 9 “Hair”: hair.png'}))
-- Older natives: the English message and the lower-cased English step; no Where line.
local old={state='failed',error='Invalid/truncated PMX/PMD model (nanoem status 101)',stage='Parsing skeleton, materials and physics',filename='x.pmx'}
assert(mmdhl.ImportCause(old)==old.error and mmdhl.ImportStage(old)=='parsing skeleton, materials and physics' and mmdhl.ImportWhere(old)==nil)
''')
print('PASS: the translated cause and step, and a Where line from the places, the value and the byte; English for older natives')

# ---- the failure window ----
lua.execute(r'''
local L=mmdhl.L
FRAMES={}
local status={state='failed',error='Invalid model data: material 0 “スカート” uses triangle indices 0 to 2,999, past the 60 the model has',errorCode='pmx.materials',stage='Parsing skeleton, materials and physics',stageCode='parse',
 filename='x.pmx',source='C:/m/x.pmx',kind='character',context={'Importing'},errorDetails={where={{kind='material',index=0,name='スカート'}},first=0,count=3000,indices=60},
 exitCode=1,worker={release='2.3.0',build='abc-123'},elapsed_ms=1234,log='unhandled exception 0xC0000005 at mmdhl_runtime_win64.dll+0x10 during stage parse\r\n'}
mmdhl.ShowImportFailure(status,'library')
local frame=FRAMES[#FRAMES]
assert(HAS(frame,L('library.failure.while',{step=L'library.stage.parse',error=L'library.cause.pmx_materials'})),'the cause line is translated')
assert(HAS(frame,L('library.failure.where',{where='material 0 “スカート”'})),'the Where line names the material')
assert(HAS(frame,L('library.failure.what_to_try',{hint=L'library.hint.pmx_materials'})))
assert(FIND(frame,L'library.failure.retry'),'the file can be imported again')
-- Copy details: the code, context, data, exit code, build, time, log and the English message.
FIND(frame,L'library.failure.copy_details').DoClick()
for _,part in ipairs({'Error: Invalid model data: material 0 “スカート”','Error code: pmx.materials','Context: Importing','Error data: {','"count": 3000','Importer exit code: 1','Importer: 2.3.0 (build abc-123)',
  'Step: Parsing skeleton, materials and physics [parse]','Time spent: 1.2 s','Importer log:','during stage parse','Source: C:/m/x.pmx'}) do
 assert(CLIPBOARD:find(part,1,true),'Copy details lacks '..part..'\n'..CLIPBOARD) end
-- The window grows with its text instead of clipping it.
local short=frame.h
local long=table.Copy(status) long.errorDetails={where={{kind='material',index=0,name=string.rep('スカート',10)}},field=string.rep('a very long field name ',12),value='NaN'}
mmdhl.ShowImportFailure(long,'library')
assert(FRAMES[#FRAMES].h>short,'a longer Where line made no room')
for _,c in ipairs(FRAMES[#FRAMES].children) do if c.kind=='DLabel' then assert(c.tall and c.tall>=16,'a label has no height for its text') end end
-- The file picker crashed (a shell extension): the step is the picker's and the hint does not blame a file.
mmdhl.ShowImportFailure({state='failed',error='The import worker exited before completion after an access violation (exit code 0xC0000005).',errorCode='worker.crash',stage='Select model',stageCode='pick',kind='character',
 exitCode=3221225477,errorDetails={exitCodeHex='0xC0000005',cause='access_violation'}},'library')
frame=FRAMES[#FRAMES]
assert(HAS(frame,L('library.failure.while',{step=L'library.stage.pick',error=L('library.cause.worker_crash',{code='0xC0000005'})})) and HAS(frame,L('library.failure.what_to_try',{hint=L'library.hint.picker_crash'})))
-- Older natives: no code, no Where, the English error and the phrase hint still show.
mmdhl.ShowImportFailure({state='failed',error='Invalid spring bone stiffness',stage='Converting VRM avatar',filename='a.vrm',source='C:/m/a.vrm'},'library')
frame=FRAMES[#FRAMES]
assert(HAS(frame,L('library.failure.while',{step='converting vrm avatar',error='Invalid spring bone stiffness'})) and HAS(frame,L'library.hint.vrm_spring'))
for _,t in ipairs(TEXTS(frame)) do assert(not tostring(t):find(L('library.failure.where',{where=''}),1,true),'an old status got a Where line') end
''')
print('PASS: the window shows the translated cause, Where and hint, grows with its text, and copies everything a report needs; older statuses still render')

# ---- the window also opens when the importer cannot start, or the job is lost ----
lua.execute(r'''
local L=mmdhl.L local library=mmdhl.library
FRAMES={} TIMERS={}
assert(library.StartImport(nil,'Import worker missing')==false and library.status=='Import worker missing')
RUN_TIMERS() local frame=FRAMES[#FRAMES]
assert(frame and HAS(frame,L('library.failure.while',{step=L'library.stage.start',error='Import worker missing'})) and HAS(frame,L'library.hint.worker_missing'))
FRAMES={} library.StartImport(nil,'An import is already running') RUN_TIMERS() assert(#FRAMES==0,'a busy importer needs only the status line')
-- PollJob itself fails (the module lost the job).
library.job=5 library.nextPoll=0 library.filename='lost.pmx' mmdhl.native.PollJob=function() return nil,'Unknown job' end
HOOKS['Think/MMDHL.LibraryImport']() RUN_TIMERS() frame=FRAMES[#FRAMES]
assert(library.job==nil and frame and HAS(frame,L('library.failure.while',{step=L'library.stage.worker',error='Unknown job'})) and HAS(frame,L('library.failure.title',{file='lost.pmx'})))
-- A worker failure through the job poll opens the window with its code.
FRAMES={} library.job=6 library.nextPoll=0
mmdhl.native.PollJob=function() return py_encode({state='failed',error='This is a ZIP archive, not a model.',errorCode='format.archive',stage='Reading the file',stageCode='read',source='C:/m/x.zip',filename='x.zip',kind='character'}) end
HOOKS['Think/MMDHL.LibraryImport']() RUN_TIMERS() frame=FRAMES[#FRAMES]
assert(frame and HAS(frame,L'library.cause.format_archive') and HAS(frame,L'library.hint.format_archive') and library.status:find('ZIP archive',1,true),'the status line keeps the importer\'s English message')
''')
print('PASS: the failure window also opens when the importer cannot start and when the job is lost')

# ---- a character imported without a ragdoll ----
lua.execute(r'''
local L=mmdhl.L local library=mmdhl.library
local VB='ValveBiped.Bip01_'
local bones={} for _,n in ipairs({'頭','首','上半身','下半身','左腕','右腕','左ひじ','右ひじ','左足','右足','左ひざ','右ひざ'}) do bones[#bones+1]={name=n} end
for i=1,8 do bones[#bones+1]={name='hair'..i} end
local status={state='complete',asset=string.rep('e',64),source='C:/m/hero.pmx',filename='hero.pmx',info={name='Hero',boneList=bones},fit={ok=false,errorCode='fit.landmarks',error='No bone found for: left thigh, left lower leg',missing={VB..'L_Thigh',VB..'L_Calf'}}}
-- With the bone window: Assign bones… opens it on this model.
FRAMES={} local opened mmdhl.OpenBoneMapper=function(o) opened=o return true end
assert(mmdhl.ExplainFit(status)) local frame=FRAMES[#FRAMES]
assert(HAS(frame,L'library.fit_failed.title') and HAS(frame,L('library.fit_failed.missing',{name='Hero',parts='L_Thigh, L_Calf'})) and HAS(frame,L'library.fit_failed.assign_hint'))
FIND(frame,L'library.fit_failed.assign').DoClick()
assert(opened and opened.asset==status.asset and opened.source=='C:/m/hero.pmx' and opened.fit==status.fit and frame.removed)
-- Without it: no Assign bones…, the renaming advice, and Keep for now.
FRAMES={} mmdhl.OpenBoneMapper=nil
assert(mmdhl.ExplainFit(status)) frame=FRAMES[#FRAMES]
assert(not FIND(frame,L'library.fit_failed.assign') and FIND(frame,L'library.fit_failed.keep') and HAS(frame,L'library.fit_failed.rename_hint'))
-- The bone window's own rescue prompt takes the case it can fix; part names come from it.
mmdhl.OpenBoneMapper=function() end
mmdhl.boneMapper={AfterImport=function() end,Available=function() return true end,PartLabel=function(k) return 'part:'..k end}
FRAMES={} assert(mmdhl.ExplainFit(status)==false and #FRAMES==0,'two windows for one problem')
-- The rescue check could not be made (the model did not load, the server did not answer): this window says it.
FRAMES={} assert(mmdhl.ExplainFit(status,true)) assert(FIND(FRAMES[#FRAMES],L'library.fit_failed.assign'),'the deferred explanation was lost')
mmdhl.boneMapper.Available=function() return false end
assert(mmdhl.ExplainFit(status)) assert(HAS(FRAMES[#FRAMES],'part:'..VB..'L_Thigh, part:'..VB..'L_Calf'),'the parts are named in plain words')
-- This binary lacks the fit functions: Assign bones… would only ask for an update, so the renaming advice shows.
assert(not FIND(FRAMES[#FRAMES],L'library.fit_failed.assign') and HAS(FRAMES[#FRAMES],L'library.fit_failed.rename_hint'),'a dead-end Assign bones…')
-- Other fit failures say why; assigning bones cannot fix them.
FRAMES={} local other=table.Copy(status) other.fit={ok=false,errorCode='fit.error',error='Model has no height'}
assert(mmdhl.ExplainFit(other)) frame=FRAMES[#FRAMES]
assert(HAS(frame,L('library.fit_failed.reason',{name='Hero',reason='Model has no height'})) and HAS(frame,L'library.fit_failed.other_hint') and not FIND(frame,L'library.fit_failed.assign'))
-- Nothing to say: a fit that worked, an older native (no fit block), a model that is not a character.
FRAMES={} local fine=table.Copy(status) fine.fit={ok=true} assert(not mmdhl.ExplainFit(fine))
local old=table.Copy(status) old.fit=nil assert(not mmdhl.ExplainFit(old))
local box=table.Copy(status) box.info={name='Box',boneList={{name='root'}}} assert(not mmdhl.ExplainFit(box) and #FRAMES==0)
-- After an import the rescue prompt's check goes first; when that check fails, this window explains the fit after all.
local AFTER
mmdhl.boneMapper={OnJobStatus=function() return false end,AfterImport=function(s,failed) AFTER={s,failed} end,Available=function() return true end,PartLabel=function(k) return 'part:'..k end}
mmdhl.OpenBoneMapper=function() end library.Refresh=function() end
FRAMES={} TIMERS={} library.job=8 library.nextPoll=0 mmdhl.native.PollJob=function() return py_encode(status) end
HOOKS['Think/MMDHL.LibraryImport']() RUN_TIMERS()
assert(AFTER and AFTER[1].asset==status.asset and isfunction(AFTER[2]) and #FRAMES==0,'the rescue prompt decides first')
AFTER[2]() assert(#FRAMES==1 and HAS(FRAMES[1],L'library.fit_failed.title') and FIND(FRAMES[1],L'library.fit_failed.assign'),'a failed rescue check left the player without an explanation')
mmdhl.boneMapper=nil mmdhl.OpenBoneMapper=nil
''')
print('PASS: a character imported without a ragdoll lists the missing parts; Assign bones… only with the bone window; other reasons are said; nothing for older natives')

# ---- spawn: the fitter's reason inside a translated token ----
spawn = lua51.LuaRuntime(unpack_returned_tuples=True)
spawn.execute(r'''
SERVER=true CLIENT=false
isstring=function(v) return type(v)=='string' end istable=function(v) return type(v)=='table' end
IsValid=function(v) return type(v)=='table' end
table.Copy=function(t) local c={} for k,v in pairs(t) do c[k]=type(v)=='table' and table.Copy(v) or v end return c end
util={TableToJSON=function() return '{}' end}
SysTime=function() return 0 end GetConVar=function() return {GetBool=function() return true end} end
timer={Create=function(_,_,_,fn) fn() end,Remove=function() end}
mmdhl={native={},cleanupGeneration=0}
''')
attach(spawn)
spawn.execute(r'''
native=mmdhl.native L=mmdhl.L fitSequence=0 NOTICES={} notice=function(p,t) NOTICES[#NOTICES+1]=t end
mmdhl.FeatureAvailable=function() return true end mmdhl.WithSpawnDefaults=function(p,o) return table.Copy(o or {}) end
mmdhl.LoadSavedFit=function() return nil end mmdhl.SavedBoneMap=function() return nil end mmdhl.CanUseAsset=function() return true end mmdhl.ActorOptions=function(o) return o end
mmdhl.LoadAsset=function(id,cb) cb({name='Hero'}) end
FIT_ERROR='No bone found for: left thigh, left lower leg'
native.RequestCarrierFit=function() return nil,FIT_ERROR end
''')
spawn.execute('load=loadstring')  # lua_source.definition compiles with load (Lua 5.2 style)
spawn.execute(definition(spawn, SERVER, 'function mmdhl.Spawn('))
spawn.execute(r'''
local id=string.rep('d',64) local got
mmdhl.Spawn({},id,{angles={0,0,0}},function(ent,err) got=err end)
assert(got:find('server.error.fit_failed',1,true),'the fit error is not a token: '..tostring(got))
assert(mmdhl.Localize(got)=='This character cannot become a ragdoll: No bone found for: left thigh, left lower leg',mmdhl.Localize(got))
-- The physics editor reads the fitter's own words for every operation, its test builds included.
mmdhl.Spawn({},id,{angles={0,0,0}},function(ent,err) got=err end,nil,{replace=true}) assert(got==FIT_ERROR)
mmdhl.Spawn({},id,{angles={0,0,0}},function(ent,err) got=err end,nil,{replace=false}) assert(got==FIT_ERROR,'a physics editor test build got the ragdoll token')
-- A rejected shape or setting (the collision editor's Fit) is not "cannot become a ragdoll".
for _,reason in ipairs({'Invalid collision correction','Invalid physics settings: range bodies.3.mass too large','Invalid carrier scale/mass'}) do
 FIT_ERROR=reason mmdhl.Spawn({},id,{angles={0,0,0}},function(ent,err) got=err end) assert(got==reason,tostring(got))
end
FIT_ERROR='Model has no height' mmdhl.Spawn({},id,{angles={0,0,0}},function(ent,err) got=err end) assert(got:find('server.error.fit_failed',1,true),'no height is a ragdoll problem')
''')
print('PASS: spawn-time fit errors reach players as a translated token around the fitter\'s reason')
