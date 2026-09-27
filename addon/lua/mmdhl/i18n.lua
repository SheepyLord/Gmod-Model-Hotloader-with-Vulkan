-- Translated text. Phrases live in resource/localization/<language>/mmdhl.properties;
-- English (en) is the fallback for any phrase a translation lacks. docs/TRANSLATING.md
-- describes the format. This file must work without the native module.
--
-- mmdhl.L(key,vars) returns display text for mmdhl.<key>, with {name} placeholders
-- filled from vars. The server has no player language: there it returns a portable
-- token, and each client renders it in its own language with mmdhl.Localize. Text a
-- client receives from the server must go through mmdhl.Localize before display.
--
-- The language follows the game's (gmod_language) unless the player chooses one with
-- mmdhl_language. A change fires hook MMDHL.LanguageChanged so open windows rebuild.
mmdhl=mmdhl or {}
local M=mmdhl
local I={}
M.I18n=I
-- Private-use code points U+E000..U+E003 delimit tokens: start, next variable,
-- name/value separator and end. They pass unchanged through net strings and JSON.
local START,NEXT,EQUALS,STOP='\238\128\128','\238\128\129','\238\128\130','\238\128\131'
local DELIMITER='\238\128[\128-\131]'
-- Languages with an official translation, each written in its own language.
I.Languages={{'en','English'},{'zh-cn','简体中文'},{'zh-tw','繁體中文'},{'ja','日本語'},{'ko','한국어'},{'fr','Français'},{'ru','Русский'}}
local languageNames={} for _,v in ipairs(I.Languages) do languageNames[v[1]]=v[2] end
function I.LanguageName(code) return languageNames[code] or code end
local function utf8char(n)
 if n<0x80 then return string.char(n) end
 if n<0x800 then return string.char(0xC0+math.floor(n/64),0x80+n%64) end
 if n<0x10000 then return string.char(0xE0+math.floor(n/4096),0x80+math.floor(n/64)%64,0x80+n%64) end
 return string.char(0xF0+math.floor(n/262144),0x80+math.floor(n/4096)%64,0x80+math.floor(n/64)%64,0x80+n%64)
end
local escapes={n='\n',t='\t'}
-- \n, \t, \uXXXX (the game's own catalogues use it) and \\; any other \x is x.
local function unescape(s)
 if not s:find('\\',1,true) then return s end
 local out,i={},1
 while true do
  local at=s:find('\\',i,true)
  if not at then out[#out+1]=s:sub(i) break end
  out[#out+1]=s:sub(i,at-1)
  local c=s:sub(at+1,at+1)
  local hex=c=='u' and s:match('^%x%x%x%x',at+2)
  if hex then
   local n=tonumber(hex,16) i=at+6
   local low=n>=0xD800 and n<0xDC00 and s:match('^\\u(%x%x%x%x)',i)
   if low then n=0x10000+(n-0xD800)*1024+(tonumber(low,16)-0xDC00) i=i+6 end
   out[#out+1]=utf8char(n)
  else out[#out+1]=escapes[c] or c i=at+2 end
 end
 return table.concat(out)
end
-- key=value per line; # or ! starts a comment. Empty values are left out.
function I.Parse(text)
 local phrases={}
 text=tostring(text or ''):gsub('^\239\187\191','')
 for line in (text..'\n'):gmatch('([^\n]*)\n') do
  if not line:match('^%s*[#!]') then
   local key,value=line:match('^%s*([^=%s]+)%s*=(.*)$')
   value=value and unescape(value:match('^%s*(.-)%s*$'))
   if key and value~='' then phrases[key]=value end
  end
 end
 return phrases
end
local catalogues={}
local function catalogue(language)
 local phrases=catalogues[language]
 if not phrases then
  phrases={}
  if file and file.Read then
   local ok,text=pcall(file.Read,'resource/localization/'..language..'/mmdhl.properties','GAME')
   if ok and text then phrases=I.Parse(text) end
  end
  catalogues[language]=phrases
 end
 return phrases
end
local function clean(code) code=tostring(code or ''):lower() return code:match('^[%w_%-]+$') and code or nil end
local function translated(code) return code=='en' or next(catalogue(code))~=nil end
I.Translated=translated
local gameVar=CLIENT and GetConVar and GetConVar('gmod_language')
local chosenVar=CLIENT and CreateClientConVar and CreateClientConVar('mmdhl_language','',true,false,'Model Hotloader language: empty follows the game language; or en, zh-cn, zh-tw, ja, ko, fr, ru.')
local debugVar=CLIENT and CreateClientConVar and CreateClientConVar('mmdhl_i18n_debug','0',false,false,'Translation check: 1 marks every phrase from the catalogue, 2 shows phrase keys instead of text.',0,2)
-- The game's language when the addon has it, otherwise English.
function I.GameLanguage() local code=clean(gameVar and gameVar:GetString()) return code and translated(code) and code or 'en' end
-- The player's choice (mmdhl_language), or '' when the language follows the game.
function I.Chosen() local code=clean(chosenVar and chosenVar:GetString()) return code and code~='auto' and translated(code) and code or '' end
function I.Language() local chosen=I.Chosen() return chosen~='' and chosen or I.GameLanguage() end
function I.Choose(code) if CLIENT then RunConsoleCommand('mmdhl_language',code or '') end end
-- Reloads the catalogues from disk.
function I.Reload() catalogues={} end
local currentLanguage
local function phrase(key)
 local language=currentLanguage or I.Language()
 return catalogue(language)[key] or catalogue('en')[key]
end
I.Phrase=phrase
local accents={a='á',e='é',i='í',o='ö',u='ü',A='Á',E='É',I='Í',O='Ö',U='Ü',c='ç',n='ñ',y='ý'}
local function pseudo(template)
 -- Accents everything outside {placeholders} so untranslated or concatenated text stands
 -- out, and lengthens it by about a third, as many translations are, to expose clipping.
 local text=('}'..template):gsub('}([^{]*)',function(part) return '}'..part:gsub('[aeiouAEIOUcny]',accents) end):sub(2)
 return '['..text..string.rep('~',math.ceil(#template/3))..']'
end
local function render(key,vars)
 local full='mmdhl.'..key
 local mode=debugVar and debugVar:GetInt() or 0
 if mode==2 then return '['..key..']' end
 local template=phrase(full)
 if not template then return full end
 if mode==1 then template=pseudo(template) end
 if not vars then return template end
 return (template:gsub('{([%w_]+)}',function(name) local value=vars[name] if value~=nil then return tostring(value) end end))
end
-- Renders every token in text in this realm's language; plain text passes unchanged.
function I.Localize(text)
 if type(text)~='string' or not text:find(START,1,true) then return text end
 local token
 -- Reads plain text and nested tokens up to the next variable separator or end.
 local function value(i)
  local parts={}
  while true do
   local at=text:find(DELIMITER,i)
   if not at then parts[#parts+1]=text:sub(i) return table.concat(parts),#text+1 end
   parts[#parts+1]=text:sub(i,at-1)
   local mark=text:sub(at,at+2)
   if mark==START then local rendered rendered,i=token(at+3) parts[#parts+1]=rendered
   elseif mark==EQUALS then i=at+3
   else return table.concat(parts),at end
  end
 end
 function token(i)
  local key,at=value(i) local vars={}
  while text:sub(at,at+2)==NEXT do
   local stop=text:find(DELIMITER,at+3) local name=text:sub(at+3,(stop or #text+1)-1)
   local rendered
   if stop and text:sub(stop,stop+2)==EQUALS then rendered,at=value(stop+3) else rendered,at='',stop or #text+1 end
   vars[name]=rendered
  end
  -- A truncated token still renders what arrived.
  if text:sub(at,at+2)==STOP then at=at+3 end
  return render(key,vars),at
 end
 local parts,i={},1
 while i<=#text do
  local at=text:find(DELIMITER,i)
  if not at then parts[#parts+1]=text:sub(i) break end
  parts[#parts+1]=text:sub(i,at-1)
  if text:sub(at,at+2)==START then local rendered rendered,i=token(at+3) parts[#parts+1]=rendered else i=at+3 end
 end
 return table.concat(parts)
end
M.Localize=I.Localize
-- A token that a client renders later; variables are kept in a stable order.
function I.Token(key,vars)
 local parts={START,key}
 if vars then
  local names={} for name in pairs(vars) do names[#names+1]=tostring(name) end table.sort(names)
  for _,name in ipairs(names) do parts[#parts+1]=NEXT..name..EQUALS..tostring(vars[name]) end
 end
 parts[#parts+1]=STOP
 return table.concat(parts)
end
function M.L(key,vars)
 if SERVER then return I.Token(key,vars) end
 if vars then
  local localized={}
  for name,value in pairs(vars) do localized[name]=type(value)=='string' and I.Localize(value) or value end
  vars=localized
 end
 return render(key,vars)
end
-- Labels in tables built at load time (choice lists): reading a field named in fields
-- runs its function, so the label follows the current language.
function I.Lazy(t,fields) return setmetatable(t,{__index=function(_,name) local get=fields[name] if get then return get() end end}) end
-- Chat text for one player, rendered in that player's language.
if SERVER then
 util.AddNetworkString('mmdhl_chat')
 function M.ChatPrint(p,text) if IsValid(p) then net.Start('mmdhl_chat') net.WriteString(tostring(text)) net.Send(p) end end
end
if not CLIENT then return end
-- UI font with the language's own glyphs; Segoe UI would fall back per character, which
-- mixes Japanese and Chinese glyph shapes. These faces have only regular and bold (other
-- weights are smeared by synthesis), and their line spacing makes them render smaller at
-- the same size, so both are adjusted.
local faces={['zh-cn']={'Microsoft YaHei UI',1.2},['zh-tw']={'Microsoft JhengHei UI',1.2},ja={'Meiryo UI',1.1},ko={'Malgun Gothic',1.05}}
function I.FontFace() local face=faces[currentLanguage or I.Language()] return face and face[1] or 'Segoe UI' end
-- surface.CreateFont data for text of this size and weight in the current language.
function I.FontData(size,weight)
 local face=faces[currentLanguage or I.Language()]
 if not face then return {font='Segoe UI',size=math.Round(size),weight=weight,extended=true} end
 return {font=face[1],size=math.Round(size*face[2]),weight=weight>=600 and 700 or 400,extended=true}
end
-- The game translates some names itself with language.GetPhrase: the tool gun's texts,
-- undo entries (the server names them by catalogue key, e.g. undo.Create('mmdhl.undo.ragdoll')),
-- context menu entries (MenuLabel='#mmdhl.physics_reset.menu') and the cleanup type.
-- Weapon and entity names are copied into their stored tables.
local function addGamePhrases()
 if not language or not language.Add then return end
 language.Add('Cleanup_mmdhl',mmdhl.L'game.cleanup')
 language.Add('Cleaned_mmdhl',mmdhl.L'game.cleaned')
 for _,part in ipairs({'name','desc','left','right','reload'}) do language.Add('tool.mmdhl_prop.'..part,mmdhl.L('tool.prop.'..part)) end -- i18n-keys: tool.prop.name tool.prop.desc tool.prop.left tool.prop.right tool.prop.reload
 -- i18n-keys: undo.ragdoll undo.citizen undo.combine undo.hostile undo.static_prop undo.attached_prop props.menu.collides_with props.menu.gravity physics_reset.menu
 for _,key in ipairs({'undo.ragdoll','undo.citizen','undo.combine','undo.hostile','undo.static_prop','undo.attached_prop','props.menu.collides_with','props.menu.gravity','physics_reset.menu'}) do language.Add('mmdhl.'..key,mmdhl.L(key)) end
 local weapon=weapons and weapons.GetStored('weapon_mmdhl')
 if weapon then weapon.PrintName=mmdhl.L'weapon.name' weapon.Instructions=mmdhl.L'weapon.instructions' end
 -- The Weapons tab reads the copy weapons.Register made in list 'Weapon'.
 local listed=list and list.GetForEdit and list.GetForEdit('Weapon',true)
 if listed and listed.weapon_mmdhl then listed.weapon_mmdhl.PrintName=mmdhl.L'weapon.name' end
 -- i18n-keys: entity.ragdoll props.entity_name entity.attach_link
 for class,key in pairs({mmdhl_ragdoll='entity.ragdoll',mmdhl_prop='props.entity_name',mmdhl_attach_link='entity.attach_link'}) do
  local stored=scripted_ents and scripted_ents.GetStored(class) if stored and stored.t then stored.t.PrintName=mmdhl.L(key) end
 end
end
-- Applies a new language (or check mode) once per change and tells open windows.
function I.Check()
 local language=I.Language()..'/'..(debugVar and debugVar:GetInt() or 0)
 if language==I.applied then return end
 local first=I.applied==nil
 I.applied=language currentLanguage=I.Language()
 addGamePhrases()
 if not first then hook.Run('MMDHL.LanguageChanged',currentLanguage) end
end
I.Check()
hook.Add('InitPostEntity','MMDHL.GamePhrases',addGamePhrases)
cvars.AddChangeCallback('mmdhl_language',function() I.Check() end,'MMDHL.Language')
cvars.AddChangeCallback('mmdhl_i18n_debug',function() I.Check() end,'MMDHL.Language')
cvars.AddChangeCallback('gmod_language',function() I.Check() end,'MMDHL.Language')
-- The game's language can change from its own options menu at any time.
timer.Create('MMDHL.Language',1,0,I.Check)
if net then net.Receive('mmdhl_chat',function() chat.AddText(I.Localize(net.ReadString())) end) end
