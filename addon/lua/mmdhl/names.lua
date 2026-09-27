-- Names that come from model files (model, parts, expressions) shown in the
-- player's language. The model's own English names are used when the player
-- reads English; everything else is sent to Google Translate in the
-- background, one small batch at a time, and cached on disk. Nothing waits for
-- it: the original name shows until its translation arrives, and a slow,
-- failing or blocked service only means names stay as authored.
-- Sending is opt-in per model (asked at import, default on, remembered for
-- the next import) and can be switched off in Physics & Performance.
-- Models without a recorded choice (imported before this existed, from the
-- Workshop, shared by a server) are sent only once the player has seen what
-- is shared: in an import window, the library notice or the setting.
local L=mmdhl.L
local N=mmdhl.names or {}
mmdhl.names=N
local enabled=CreateClientConVar('mmdhl_translate_names','1',true,false,'Translate names from imported models (sends them to Google Translate)',0,1)
local importDefault=CreateClientConVar('mmdhl_translate_new_imports','1',true,false,'Translate names of newly imported models by default',0,1)
local noticeSeen=CreateClientConVar('mmdhl_translate_notice_seen','0',true,false,'The player has seen that model names are sent to Google Translate',0,1)
N.Endpoint='https://clients5.google.com/translate_a/t?client=dict-chrome-ex&sl=auto&tl='
local Folder='mmd_hotloader/translations/'
local MaxText=200      -- bytes; longer texts are descriptions, not names
local MaxBatch=40      -- names per request
local MaxBody=4000     -- bytes of form data per request
local MaxEntries=20000 -- per language
local Timeout=15       -- seconds
local englishNames={}  -- asset id -> the model's English name, or false

-- ---- Target language ----
-- The addon's chosen language, else the game's own (a German player gets
-- German names even though the addon itself shows English).
local Google={['zh-cn']='zh-CN',['zh-tw']='zh-TW',['pt-br']='pt',['pt-pt']='pt-PT',['es-es']='es',['sv-se']='sv',['en-pt']='en',he='iw'}
function N.Target()
 local code=mmdhl.I18n and mmdhl.I18n.Chosen and mmdhl.I18n.Chosen() or ''
 if code=='' then local game=GetConVar('gmod_language') code=game and game:GetString():lower() or 'en' end
 code=code:match('^[%a%-]+$') and code or 'en'
 return Google[code] or code:match('^(%a+)') or 'en'
end
local function english(target) return target=='en' end

-- ---- Consent ----
-- Per model: true or false once chosen at import; unset models follow the
-- main switch once the player has seen the notice.
local consent
local function consents()
 if not consent then consent=util.JSONToTable(file.Read(Folder..'models.json','DATA') or '') or {} end
 return consent
end
local function saveConsent() file.CreateDir('mmd_hotloader/translations') file.Write(Folder..'models.json',util.TableToJSON(consent or {})) end
function N.Enabled() return enabled:GetBool() end
function N.ImportDefault() return importDefault:GetBool() end
function N.Allowed(id)
 if not enabled:GetBool() or not isstring(id) then return false end
 local chosen=consents()[id]
 if chosen==nil then return noticeSeen:GetBool() end
 return chosen
end
function N.NeedsNotice() return enabled:GetBool() and not noticeSeen:GetBool() end
-- The library notice: keep translating, or switch it off; either way it was seen.
function N.AnswerNotice(translate)
 RunConsoleCommand('mmdhl_translate_notice_seen','1')
 if not translate then RunConsoleCommand('mmdhl_translate_names','0') end
 timer.Simple(0,function() hook.Run('MMDHL.NamesChanged') end)
end
local dequeue
function N.SetAllowed(id,value)
 if not isstring(id) or #id~=64 or id:find('[^0-9a-f]') then return end
 consents()[id]=value==true saveConsent()
 if value~=true then dequeue(id) end
 hook.Run('MMDHL.NamesChanged')
end
function N.Forget(ids)
 local c=consents() local changed=false
 for _,id in ipairs(ids or {}) do dequeue(id) if c[id]~=nil then c[id]=nil changed=true end end
 if changed then saveConsent() end
end
-- The import window records the choice for a source file; the finished
-- import stores it for the new model and remembers it for the next import.
N.chosen=N.chosen or {}
function N.Choose(source,value)
 if isstring(source) then N.chosen[source]=value==true end
 RunConsoleCommand('mmdhl_translate_new_imports',value and '1' or '0')
 RunConsoleCommand('mmdhl_translate_notice_seen','1')
end
function N.Imported(id,source,previous)
 englishNames[id]=nil
 local value if isstring(source) then value=N.chosen[source] end
 if value==nil and previous and consents()[previous]~=nil then value=consents()[previous] end
 -- No import window (its warning was dismissed): the remembered choice, but
 -- only once the player has seen what translation shares.
 if value==nil and noticeSeen:GetBool() then value=importDefault:GetBool() end
 if value~=nil then N.SetAllowed(id,value) end
end

-- ---- Cache ----
-- source text -> translation; false means "keep as authored" (already in the
-- target language, or nothing to translate). Stored as [source, translation]
-- pairs: GMod's JSON reader turns number-like object keys ("1e5") into numbers.
local caches={}
local function cache(target)
 if caches[target] then return caches[target] end
 local stored=util.JSONToTable(file.Read(Folder..target..'.json','DATA') or '')
 local entries,count={},0
 for _,pair in ipairs(istable(stored) and istable(stored.pairs) and stored.pairs or {}) do
  if istable(pair) and isstring(pair[1]) and (isstring(pair[2]) or pair[2]==false) and entries[pair[1]]==nil and count<MaxEntries then entries[pair[1]]=pair[2] count=count+1 end
 end
 local c={entries=entries,count=count}
 caches[target]=c return c
end
local dirty={}
local function remember(target,text,translation)
 local c=cache(target)
 if c.entries[text]==nil then if c.count>=MaxEntries then return end c.count=c.count+1 end
 c.entries[text]=translation dirty[target]=true
end
local function flush()
 for target in pairs(dirty) do
  file.CreateDir('mmd_hotloader/translations')
  local list={} for text,translation in pairs(cache(target).entries) do list[#list+1]={text,translation} end
  file.Write(Folder..target..'.json',util.TableToJSON({version=1,provider='google',pairs=list}))
 end
 dirty={}
end

-- ---- What needs translating ----
local function letters(text) return text:find('[A-Za-z\128-\255]') end
local function ascii(text) return not text:find('[\128-\255]') end
local function worth(text,target)
 if not isstring(text) or text=='' or #text>MaxText or not letters(text) then return false end
 -- English readers already read ASCII names; other languages may not.
 if english(target) and ascii(text) then return false end
 return true
end

-- ---- Queue and requests ----
-- target -> {text -> {model id -> true}}: the models whose names want each text.
-- Consent can change while a text waits (or backs off), so it is checked again
-- for those models right before a request, not only when the text is queued.
N.queue=N.queue or {}
local state={inflight=false,failures=0,retryAt=0,paused=false,ready=false}
N.state=state
local function enqueue(target,text,id)
 local q=N.queue[target] if not q then q={} N.queue[target]=q end
 local models=q[text] if not istable(models) then models={} q[text]=models end
 models[id]=true
end
-- A model whose choice turned to "keep as authored" (or that was deleted) no longer asks.
function dequeue(id)
 for _,q in pairs(N.queue) do for text,models in pairs(q) do
  if istable(models) then models[id]=nil if next(models)==nil then q[text]=nil end else q[text]=nil end
 end end
end
local function stillWanted(models)
 local wanted=false
 if istable(models) then for id in pairs(models) do if N.Allowed(id) then wanted=true else models[id]=nil end end end
 return wanted
end
-- Explicit ASCII classes: %w follows the C library locale, which may count UTF-8 bytes as letters.
local function urlencode(s) return (s:gsub('[^A-Za-z0-9%-_%.~]',function(c) return string.format('%%%02X',c:byte()) end)) end
-- Latin, Cyrillic and Greek translations start with a capital, like names.
local function capitalize(text)
 local first=text:sub(1,1)
 if first:match('^[a-z]$') then return first:upper()..text:sub(2) end
 local a,b=text:byte(1,2)
 if a==0xD0 and b>=0xB0 and b<=0xBF then return string.char(0xD0,b-0x20)..text:sub(3) end -- а..п
 if a==0xD1 and b>=0x80 and b<=0x8F then return string.char(0xD0,b+0x20)..text:sub(3) end -- р..я
 if a==0xD1 and b==0x91 then return string.char(0xD0,0x81)..text:sub(3) end -- ё
 return text
end
N.Capitalize=capitalize
-- The service answers one [translation, detected language] pair per text.
function N.ParseResponse(body,count)
 local data=util.JSONToTable(body or '')
 if not istable(data) then return nil end
 if count==1 and isstring(data[1]) then data={data} end
 local out={}
 for i=1,count do
  local item=data[i]
  if istable(item) and isstring(item[1]) then out[i]={text=item[1],language=isstring(item[2]) and item[2] or nil}
  elseif isstring(item) then out[i]={text=item}
  else return nil end
 end
 return out
end
local function accept(target,texts,results)
 for i,text in ipairs(texts) do
  local r=results[i] local translation=string.Trim(r.text or '')
  local same=translation=='' or translation==text or (r.language and r.language:lower()==target:lower())
  remember(target,text,not same and capitalize(translation) or false)
 end
end
local function fail(reason)
 state.failures=state.failures+1
 -- 15 s, 30 s, 1 min ... up to 10 min; after six failures wait for a
 -- language change or the setting to be switched off and on again.
 state.retryAt=RealTime()+math.min(600,15*2^(state.failures-1))
 state.paused=state.failures>=6
 state.lastError=tostring(reason)
end
function N.Send()
 if state.inflight or state.paused or not state.ready or not enabled:GetBool() or RealTime()<state.retryAt then return false end
 local target=N.Target() local q=N.queue[target]
 if not q or next(q)==nil then return false end
 local texts,parts,size={},{},0
 for text in pairs(q) do
  local c=cache(target).entries[text]
  if c~=nil or not stillWanted(q[text]) then q[text]=nil
  else
   local part='q='..urlencode(text)
   if #texts>0 and (#texts>=MaxBatch or size+#part+1>MaxBody) then break end
   texts[#texts+1]=text parts[#parts+1]=part size=size+#part+1
  end
 end
 if #texts==0 then return false end
 state.inflight=true
 local ok=HTTP({url=N.Endpoint..target,method='POST',body=table.concat(parts,'&'),type='application/x-www-form-urlencoded; charset=utf-8',timeout=Timeout,
  success=function(code,body)
   state.inflight=false
   local results=code==200 and N.ParseResponse(body,#texts)
   if not results then fail('HTTP '..tostring(code)) return end
   state.failures=0 state.paused=false
   for _,text in ipairs(texts) do q[text]=nil end
   accept(target,texts,results) flush()
   hook.Run('MMDHL.NamesTranslated',target)
  end,
  failed=function(reason) state.inflight=false fail(reason) end})
 if not ok then state.inflight=false fail('request refused') end
 return true
end
timer.Create('MMDHL.Names',.5,0,function() N.Send() end)
-- HTTP is unavailable while the game is still loading.
hook.Add('InitPostEntity','MMDHL.Names',function() state.ready=true end)
if IsValid(LocalPlayer()) then state.ready=true end
local function resume() state.failures=0 state.paused=false state.retryAt=0 end
hook.Add('MMDHL.LanguageChanged','MMDHL.Names',resume)
-- Changing the setting (whose help explains what is shared) counts as seeing the notice.
cvars.AddChangeCallback('mmdhl_translate_names',function(_,_,value) resume() RunConsoleCommand('mmdhl_translate_notice_seen','1') hook.Run('MMDHL.NamesChanged') end,'MMDHL.Names')

-- ---- Lookup ----
-- The name to show for text from model `id`, and the original when they
-- differ. `english` is the model's own English name for this text, if any.
function N.Display(text,id,englishName)
 if not isstring(text) or text=='' or not N.Allowed(id) then return text,nil end
 local target=N.Target()
 if english(target) and isstring(englishName) and englishName~='' and englishName~=text and ascii(englishName) and not ascii(text) then return englishName,text end
 if not worth(text,target) then return text,nil end
 local translation=cache(target).entries[text]
 if translation==nil then enqueue(target,text,id) return text,nil end
 if translation==false then return text,nil end
 return translation,text
end
-- Display text plus the original in brackets, for places with room for both.
function N.Both(text,id,englishName)
 local shown,original=N.Display(text,id,englishName)
 return original and L('names.both',{translation=shown,original=original}) or shown
end
-- Queue many names at once (a model's parts) without waiting for a lookup.
function N.Prefetch(texts,id)
 if not N.Allowed(id) then return end
 local target=N.Target() local c=cache(target).entries
 for _,text in ipairs(texts or {}) do if worth(text,target) and c[text]==nil then enqueue(target,text,id) end end
end
-- A library entry's name as shown. Names the player typed (renames, prop
-- presets) stay as typed; model names, Workshop items and server-shared
-- models are translated. English readers get the model's own English name
-- (PMX), kept in its terms-of-use record, before anything is sent.
local function englishName(kind,id)
 local cached=englishNames[id] if cached~=nil then return cached or nil end
 local record=mmdhl.terms and mmdhl.terms.Get(kind,id)
 local embedded=istable(record) and istable(record.embedded) and record.embedded
 local value=embedded and isstring(embedded.nameEnglish) and embedded.nameEnglish~='' and embedded.nameEnglish or false
 englishNames[id]=value return value or nil
end
function N.EntryName(kind,entry)
 if not istable(entry) or not isstring(entry.name) then return entry and entry.name,nil end
 local settings=istable(entry.settings) and entry.settings or {} local info=istable(entry.info) and entry.info or {}
 if settings.parent then return entry.name,nil end
 if entry.name~=info.name and not entry.workshop and not entry.shared then return entry.name,nil end
 return N.Display(entry.name,entry.id,englishName(kind,entry.id))
end
-- An expression's label: a recognized standard name stays; a name shown as
-- authored is translated (English readers get the PMX English morph name).
function N.MorphLabel(morph,id)
 local display=morph.displayName or morph.name
 if not isstring(morph.original) or morph.original=='' or display~=morph.original then return display,nil end
 local englishName=isstring(morph.name) and not morph.name:match('^morph_%d+$') and morph.name or nil
 return N.Display(morph.original,id,englishName)
end
-- Windows built once (editors, Face Poser) keep their labels current:
-- `update(panel)` sets the text now and again whenever translations arrive.
N.bound=N.bound or setmetatable({},{__mode='k'})
function N.Bind(panel,update)
 if not IsValid(panel) then return end
 N.bound[panel]=update update(panel)
end
-- Checkbox labels are sized to their first text.
function N.SetCheckboxText(box,text) box:SetText(text) if box.Label then box.Label:SizeToContents() end end
local function redraw()
 N.revision=(N.revision or 0)+1
 for panel,update in pairs(N.bound) do if IsValid(panel) then update(panel) else N.bound[panel]=nil end end
end
N.revision=N.revision or 0
hook.Add('MMDHL.NamesTranslated','MMDHL.NamesRevision',redraw)
hook.Add('MMDHL.NamesChanged','MMDHL.NamesRevision',redraw)
function N.Status() return {enabled=enabled:GetBool(),target=N.Target(),failures=state.failures,paused=state.paused,lastError=state.lastError,queued=table.Count(N.queue[N.Target()] or {})} end
