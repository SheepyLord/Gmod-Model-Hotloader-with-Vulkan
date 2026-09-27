-- Terms of use of imported models. Many models may not be used in games, for
-- anything but video production, shared, or in sexual or violent depictions,
-- and the addon cannot check this. Importing and exporting ask the player to
-- acknowledge it (each warning can be switched off for good). The model's
-- readme, embedded comment and VRM licence are shown at import, kept for each
-- model, viewable in the library, and travel with Workshop packages.
local native=mmdhl.native
local L=mmdhl.L
local T=mmdhl.terms or {}
mmdhl.terms=T
local importWarning=CreateClientConVar('mmdhl_terms_warning_import','1',true,false,'Ask to acknowledge a model\'s terms of use before importing it',0,1)
local exportWarning=CreateClientConVar('mmdhl_terms_warning_export','1',true,false,'Ask to acknowledge models\' terms of use before exporting a Workshop package',0,1)
local Folder='mmd_hotloader/terms/'
local MaxText=48*1024
-- Words that show what a model's terms talk about. ASCII entries are Lua
-- patterns matched in lower case; the others are plain text.
T.Topics={
 {id='sharing',ascii={'%f[%w]redistribut','%f[%w]re%-distribut','%f[%w]re%-?upload','%f[%w]distribut','%f[%w]shar[ei]'},
  text={'再配布','二次配布','配布','転載','再アップ','転用','转载','轉載','再分发','再分發','分发','分發','散布','배포','재배포','공유','무단'}},
 {id='games',ascii={'%f[%w]games?%f[%W]','%f[%w]vrchat','%f[%w]vrc%f[%W]','%f[%w]gmod%f[%W]','%f[%w]garry'},
  text={'ゲーム','游戏','遊戲','게임','MMD以外','ＭＭＤ以外','MikuMikuDance以外','他のソフト','他ソフト','其他软件','其他軟體','其它软件'}},
 {id='video',ascii={'%f[%w]videos?%f[%W]','%f[%w]movies?%f[%W]','%f[%w]stream'},
  text={'動画','映像','静画','配信','视频','視頻','影片','直播','영상','방송'}},
 {id='sexual',ascii={'%f[%w]r%-?18','%f[%w]r%-?15','%f[%w]nsfw','%f[%w]sexual','%f[%w]adult','%f[%w]porn','%f[%w]hentai','%f[%w]nud[ei]','%f[%w]erotic'},
  text={'18禁','エロ','性的','アダルト','成人向','卑猥','わいせつ','猥褻','裸','ヌード','露出','色情','性暗示','成人','성적','선정','야한'}},
 {id='violence',ascii={'%f[%w]violen','%f[%w]gore%f[%W]','%f[%w]guro','%f[%w]gory'},
  text={'暴力','グロ','残酷','流血','殺','血腥','猎奇','獵奇','폭력','고어','잔인'}},
 {id='commercial',ascii={'%f[%w]commercial','%f[%w]monetiz','%f[%w]sell','%f[%w]profit'},
  text={'商用','営利','有償','販売','収益','商业','商業','盈利','营利','상업','상용','수익'}},
 {id='modification',ascii={'%f[%w]modif','%f[%w]edit%f[%W]','%f[%w]edits%f[%W]','%f[%w]edited','%f[%w]editing','%f[%w]alter%f[%W]','%f[%w]altered','%f[%w]alteration','%f[%w]derivative','%f[%w]remix'},
  text={'改変','改造','流用','修改','改编','改編','二次创作','二次創作','개조','수정','편집'}},
 {id='credit',ascii={'%f[%w]credit','%f[%w]attribut'},
  text={'クレジット','表記','明記','署名','标注','標註','注明','크레딧','표기'}},
}
-- i18n-keys: terms.topic.sharing terms.topic.games terms.topic.video terms.topic.sexual terms.topic.violence terms.topic.commercial terms.topic.modification terms.topic.credit
-- Words that make a line a rule rather than a description.
local RuleAscii={'%f[%w]prohibit','%f[%w]forbid','%f[%w]not allowed','%f[%w]not permitted','%f[%w]do not%f[%W]','%f[%w]don\'t','%f[%w]must%f[%W]','%f[%w]only%f[%W]','%f[%w]ng%f[%W]',
 '%f[%w]allowed','%f[%w]permi','%f[%w]ok%f[%W]','%f[%w]please%f[%W]','%f[%w]requir','%f[%w]free%f[%W]','%f[%w]may%f[%W]','%f[%w]cannot%f[%W]','%f[%w]can\'t'}
local RuleText={'禁止','不可','ご遠慮','のみ','限定','許可','自由','お願い','ください','必須','可能','不得','请勿','請勿','严禁','嚴禁','禁用','仅限','僅限','允许','允許','必须','必需',
 '금지','하지 마','허용','가능','불가','필수','주세요','바랍니다','OK','ＯＫ','NG','ＮＧ'}
local function mentions(line,ascii,text)
 local lower=line:lower()
 for _,p in ipairs(ascii) do if lower:find(p) then return true end end
 for _,w in ipairs(text) do if line:find(w,1,true) then return true end end
 return false
end
-- Every text a model came with, as {label,text} documents.
function T.Documents(notes)
 local docs={}
 if not istable(notes) then return docs end
 local e=istable(notes.embedded) and notes.embedded or {}
 local comment={} for _,key in ipairs({'comment','commentEnglish'}) do if isstring(e[key]) and e[key]~='' and not table.HasValue(comment,e[key]) then comment[#comment+1]=e[key] end end
 if #comment>0 then docs[#docs+1]={label=L'terms.doc.comment',text=table.concat(comment,'\n\n')} end
 if isstring(e.copyright) and e.copyright~='' then docs[#docs+1]={label=L'terms.doc.copyright',text=e.copyright} end
 local vrm,rules=T.VrmLines(notes.vrm) if #vrm>0 then docs[#docs+1]={label=L'terms.doc.vrm',text=table.concat(vrm,'\n'),vrm=true,rules=rules} end
 for _,r in ipairs(istable(notes.readmes) and notes.readmes or {}) do
  if isstring(r.text) and r.text~='' then docs[#docs+1]={label=tostring(r.name or '?')..(r.folder=='parent' and '  ↑' or ''),text=r.text..(r.truncated and '\n\n'..L'terms.truncated' or '')} end
 end
 return docs
end
-- Topics the texts mention and the lines that state rules about them.
function T.Analyze(notes)
 local found,lines,seen={},{},{}
 local function keep(line)
  if seen[line] or #lines>=24 then return end
  seen[line]=true lines[#lines+1]=utf8.len(line) and utf8.len(line)>220 and line:sub(1,utf8.offset(line,220)-1)..'…' or line
 end
 for _,doc in ipairs(T.Documents(notes)) do
  -- A VRM licence is structured: its permission fields are the rules.
  if doc.vrm then for _,line in ipairs(doc.rules) do keep(line) end
  else
   for line in (doc.text..'\n'):gmatch('([^\n]*)\n') do
    local clean=string.Trim(line)
    if clean~='' then
     local hit=false
     for _,topic in ipairs(T.Topics) do if mentions(clean,topic.ascii,topic.text) then found[topic.id]=true hit=true end end
     if hit and mentions(clean,RuleAscii,RuleText) then keep(clean) end
    end
   end
  end
 end
 -- VRM licences state these definitively.
 local meta=istable(notes) and istable(notes.vrm) and istable(notes.vrm.meta) and notes.vrm.meta
 if meta then
  if meta.allowRedistribution==false then found.sharing=true end
  if meta.sexualUsage=='Disallow' or meta.allowExcessivelySexualUsage==false then found.sexual=true end
  if meta.violentUsage=='Disallow' or meta.allowExcessivelyViolentUsage==false then found.violence=true end
  if meta.commercialUsage=='Disallow' or meta.commercialUsage=='personalNonProfit' or meta.commercialUsage=='personalProfit' then found.commercial=true end
  if meta.modification=='prohibited' then found.modification=true end
  if meta.creditNotation=='required' then found.credit=true end
 end
 local topics={} for _,topic in ipairs(T.Topics) do if found[topic.id] then topics[#topics+1]=topic.id end end
 return topics,lines
end
-- Topic names as a list; with a limit, the first few and an ellipsis.
function T.TopicNames(topics,limit)
 local names={} for _,id in ipairs(topics or {}) do if isstring(id) then names[#names+1]=L('terms.topic.'..id) end end
 if limit and #names>limit then return table.concat(names,', ',1,limit)..'…' end
 return table.concat(names,', ')
end
-- VRM licence values shown in words; licence names as the library shows them.
local VrmValues={OnlyAuthor='only_author',onlyAuthor='only_author',ExplicitlyLicensedPerson='licensed_person',onlySeparatelyLicensedPerson='licensed_person',Everyone='everyone',everyone='everyone',
 personalNonProfit='personal_non_profit',personalProfit='personal_profit',corporation='corporation',prohibited='prohibited',allowModification='allow_modification',
 allowModificationRedistribution='allow_modification_redistribution',required='required',unnecessary='unnecessary'}
-- i18n-keys: terms.vrm.value.only_author terms.vrm.value.licensed_person terms.vrm.value.everyone terms.vrm.value.personal_non_profit terms.vrm.value.personal_profit terms.vrm.value.corporation terms.vrm.value.prohibited terms.vrm.value.allow_modification terms.vrm.value.allow_modification_redistribution terms.vrm.value.required terms.vrm.value.unnecessary
local VrmLicences={CC0='CC0',CC_BY='CC BY',CC_BY_NC='CC BY-NC',CC_BY_SA='CC BY-SA',CC_BY_NC_SA='CC BY-NC-SA',CC_BY_ND='CC BY-ND',CC_BY_NC_ND='CC BY-NC-ND'}
-- VRM licence fields as readable lines, and the subset that grants or denies permissions.
function T.VrmLines(vrm)
 local lines,rules={},{}
 if not istable(vrm) or not istable(vrm.meta) then return lines,rules end
 local m=vrm.meta
 local function add(label,value,rule)
  if value==nil or value=='' then return end
  local line=label..': '..tostring(value) lines[#lines+1]=line
  if rule then rules[#rules+1]=line end
 end
 local function yesno(v) if v==true or v=='Allow' then return L'terms.vrm.allowed' elseif v==false or v=='Disallow' then return L'terms.vrm.not_allowed' end return v end
 local function named(v) local key=isstring(v) and VrmValues[v] return key and L('terms.vrm.value.'..key) or v end
 add(L'terms.vrm.title',m.title)
 if istable(m.authors) and #m.authors>0 then add(L'terms.vrm.authors',table.concat(m.authors,', ')) end
 if vrm.version=='0.x' then
  local licence=m.licenseName=='Redistribution_Prohibited' and L'library.vrm.redistribution_prohibited' or m.licenseName=='Other' and L'library.vrm.other_licence' or VrmLicences[m.licenseName] or m.licenseName
  add(L'terms.vrm.licence',licence,true) add(L'terms.vrm.who',named(m.allowedUser),true)
  add(L'terms.vrm.violent',yesno(m.violentUsage),true) add(L'terms.vrm.sexual',yesno(m.sexualUsage),true) add(L'terms.vrm.commercial',yesno(m.commercialUsage),true)
  add(L'terms.vrm.permission_url',m.otherPermissionUrl) add(L'terms.vrm.licence_url',m.otherLicenseUrl)
 else
  add(L'terms.vrm.who',named(m.avatarPermission),true) add(L'terms.vrm.violent',yesno(m.allowExcessivelyViolentUsage),true) add(L'terms.vrm.sexual',yesno(m.allowExcessivelySexualUsage),true)
  add(L'terms.vrm.commercial',named(m.commercialUsage),true) add(L'terms.vrm.redistribution',yesno(m.allowRedistribution),true) add(L'terms.vrm.modification',named(m.modification),true)
  add(L'terms.vrm.credit',named(m.creditNotation),true) add(L'terms.vrm.political',yesno(m.allowPoliticalOrReligiousUsage),true) add(L'terms.vrm.hate',yesno(m.allowAntisocialOrHateUsage),true)
  add(L'terms.vrm.copyright',m.copyright) add(L'terms.vrm.licence_url',m.licenseUrl) add(L'terms.vrm.licence_url',m.otherLicenseUrl)
 end
 return lines,rules
end
-- Stored terms, one file per model; derived props share their original's.
local function valid(id) return isstring(id) and #id==64 and not id:find('[^0-9a-f]') end
function T.Get(kind,id)
 if not valid(id) then return nil end
 local record=util.JSONToTable(file.Read(Folder..id..'.json','DATA') or '')
 if istable(record) then return record end
 if kind=='static' and mmdhl.props and mmdhl.props.library then
  local entry=mmdhl.props.library.entries[id] local parent=entry and entry.settings and entry.settings.parent
  if valid(parent) and parent~=id then return util.JSONToTable(file.Read(Folder..parent..'.json','DATA') or '') end
 end
end
local function clip(s,bytes)
 s=isstring(s) and s or '' if utf8.force then s=utf8.force(s) end
 if #s<=bytes then return s end
 local cut=bytes while cut>0 and s:byte(cut+1)>=128 and s:byte(cut+1)<192 do cut=cut-1 end
 return s:sub(1,cut)
end
-- Keeps only known fields with bounded text; package terms are not trusted.
function T.Clean(notes)
 if not istable(notes) then return nil end
 local out={file=clip(notes.file,200),embedded={},readmes={}}
 local e=istable(notes.embedded) and notes.embedded or {}
 for _,key in ipairs({'name','nameEnglish','comment','commentEnglish','copyright'}) do if isstring(e[key]) and e[key]~='' then out.embedded[key]=clip(e[key],MaxText) end end
 for _,r in ipairs(istable(notes.readmes) and notes.readmes or {}) do
  if #out.readmes>=8 then break end
  if istable(r) and isstring(r.text) and r.text~='' then out.readmes[#out.readmes+1]={name=clip(r.name,200),text=clip(r.text,MaxText),folder=r.folder=='parent' and 'parent' or 'model',matched=r.matched==true,truncated=r.truncated==true or #r.text>MaxText,encoding=clip(r.encoding,20)} end
 end
 if istable(notes.vrm) and istable(notes.vrm.meta) then
  local meta={}
  for k,v in pairs(notes.vrm.meta) do if isstring(k) and #k<=40 then
   if isstring(v) then meta[k]=clip(v,2048) elseif isbool(v) then meta[k]=v
   elseif istable(v) and k=='authors' then local a={} for i,x in ipairs(v) do if isstring(x) and i<=16 then a[#a+1]=clip(x,200) end end meta[k]=a end
  end end
  out.vrm={version=notes.vrm.version=='0.x' and '0.x' or '1.0',meta=meta}
 end
 return out
end
function T.Save(id,record)
 if not valid(id) or not istable(record) then return false end
 record.version=1 record.topics=T.Analyze(record)
 file.CreateDir('mmd_hotloader/terms') file.Write(Folder..id..'.json',util.TableToJSON(record,true))
 return true
end
function T.Forget(ids) for _,id in ipairs(ids or {}) do if valid(id) then file.Delete(Folder..id..'.json') end end end
-- The stored record, or the VRM licence of a model imported before records were kept.
local function recordFor(kind,id)
 local record=T.Get(kind,id) if record then return record end
 local lib=kind=='static' and mmdhl.props and mmdhl.props.library or mmdhl.library
 local entry=lib and istable(lib.entries) and lib.entries[id]
 if entry and istable(entry.info) and istable(entry.info.vrm) then return {vrm=entry.info.vrm} end
end
T.Record=recordFor
-- Topics saved with the record (kept current by Save), else worked out now.
local function topicsOf(record) return istable(record.topics) and record.topics or (T.Analyze(record)) end
-- The subset a Workshop package carries: embedded texts, VRM licence and
-- files named like readmes (not other text that happened to sit beside it).
function T.ForPackage(kind,id)
 local record=recordFor(kind,id) if not record then return nil end
 local out=T.Clean(record) local readmes=out.readmes out.readmes={}
 for _,r in ipairs(readmes) do if r.matched then
  if #r.text>32*1024 then r.text=clip(r.text,32*1024) r.truncated=true end
  out.readmes[#out.readmes+1]=r
 end end
 while #util.TableToJSON(out)>240*1024 and #out.readmes>0 do table.remove(out.readmes) end
 if #util.TableToJSON(out)>240*1024 then for key,text in pairs(out.embedded) do out.embedded[key]=clip(text,16*1024) end end
 return out
end
-- Terms from an installed Workshop package, unless the player has their own record.
function T.SaveFromPackage(kind,id,terms,title)
 if not istable(terms) or T.Get(kind,id) then return end
 local record=T.Clean(terms) record.origin='workshop' record.package=clip(title,200) record.kind=kind
 T.Save(id,record)
end
-- The native module reads the files; older natives have no reader.
T.bySource=T.bySource or {}
T.acknowledged=T.acknowledged or {}
function T.Inspect(source,fresh)
 if not isstring(source) or source=='' then return nil end
 if T.bySource[source] and not fresh then return T.bySource[source] end
 if not native.InspectModelNotes then return nil end
 local notes=mmdhl.Decode(native.InspectModelNotes(source))
 T.bySource[source]=istable(notes) and notes or nil
 return T.bySource[source]
end
-- After an import: keep what the source came with. Reimports and presets
-- without a readable source keep the record they replace.
function T.Imported(kind,id,source,previous)
 if not valid(id) then return end
 local old=T.Get(kind,id) or (valid(previous) and previous~=id and T.Get(kind,previous)) or nil
 local notes=T.Inspect(source)
 if notes then
  local record=T.Clean(notes) record.kind=kind record.origin='import'
  record.acknowledged=T.acknowledged[source] or old and old.acknowledged or nil
  T.Save(id,record)
 elseif old and valid(previous) and previous~=id then T.Save(id,old) end
end
-- ---- Dialogs ----
-- Source wraps labels only at spaces, so a Japanese or Chinese sentence that
-- follows a Latin word starts a new line early and leaves a short one behind.
-- Such text is broken here instead: after any Han, Kana or full-width
-- character, and at spaces between other words.
local function cjk(ch) local a,b=ch:byte(1,2) return #ch==3 and (a>=227 and a<=233 or a==239 and b>=188 and b<=191) end
local function wrapCJK(text,font,width)
 surface.SetFont(font)
 local out={}
 for paragraph in (text..'\n'):gmatch('(.-)\n') do
  local tokens,word={},''
  local function flush() if word~='' then tokens[#tokens+1]=word word='' end end
  for ch in paragraph:gmatch(utf8.charpattern) do
   if ch==' ' or cjk(ch) then flush() tokens[#tokens+1]=ch else word=word..ch end
  end
  flush()
  local line=''
  for _,token in ipairs(tokens) do
   if line~='' and surface.GetTextSize(line..token)>width then
    out[#out+1]=(line:gsub(' +$','')) line=token==' ' and '' or token
   else line=line..token end
  end
  out[#out+1]=line
 end
 return table.concat(out,'\n')
end
function T.WrapLabel(label,font)
 local setText,layout=label.SetText,label.PerformLayout
 label.rawText=label:GetText()
 label.SetText=function(self,text) self.rawText=text self.wrappedFor=nil setText(self,text) self:InvalidateLayout() end
 label.PerformLayout=function(self,w,h)
  local raw=self.rawText
  if isstring(raw) and w>0 and self.wrappedFor~=w then
   self.wrappedFor=w
   setText(self,(raw:find('[\227-\233]') or raw:find('\239[\188-\191]')) and wrapCJK(raw,font,w) or raw)
  end
  if layout then layout(self,w,h) end
 end
 return label
end
local function richText(parent,text,font)
 local rt=parent:Add('RichText') rt:Dock(FILL)
 rt.PerformLayout=function(self) self:SetFontInternal(font) self:SetFGColor(Color(34,40,49)) end
 -- Text starting with # is a localization token to Source, looked up in a
 -- 1024-character buffer; a Markdown readme would be cut off there.
 if text:sub(1,1)=='#' then rt:AppendText('#') text=text:sub(2) end
 rt:AppendText(text) rt:GotoTextStart()
 rt.Paint=function(_,w,h) draw.RoundedBox(4,0,0,w,h,color_white) end
 return rt
end
-- Documents as tabs; key lines first when there are any.
local function documentTabs(parent,notes,s,f)
 local docs=T.Documents(notes) local topics,lines=T.Analyze(notes)
 if #docs==0 then
  local none=mmdhl.UI.label(parent,L'terms.none_found',f.Body,s(60)) none:Dock(TOP) none:SetWrap(true) none:SetTextColor(Color(150,90,20)) T.WrapLabel(none,f.Body)
  return topics
 end
 local sheet=parent:Add('DPropertySheet') sheet:Dock(FILL)
 if #lines>0 then local page=vgui.Create('DPanel',sheet) page:SetPaintBackground(false) richText(page,table.concat(lines,'\n'),f.Body) sheet:AddSheet(L'terms.doc.key_lines',page,'icon16/error.png') end
 for _,doc in ipairs(docs) do local page=vgui.Create('DPanel',sheet) page:SetPaintBackground(false) richText(page,doc.text,f.Body) sheet:AddSheet(doc.label:sub(1,1)=='#' and ' '..doc.label or doc.label,page,doc.vrm and 'icon16/vcard.png' or 'icon16/page_white_text.png') end
 return topics
end
local function frame(title,wide,tall)
 local UI=mmdhl.UI local s,f=UI.metrics()
 local frame=vgui.Create('DFrame') frame:SetTitle(title) frame:SetSize(math.min(ScrW()*.92,s(wide)),math.min(ScrH()*.92,s(tall))) frame:Center() frame:MakePopup()
 frame:DockPadding(s(14),s(30),s(14),s(12))
 frame.Paint=function(_,w,h) draw.RoundedBox(6,0,0,w,h,Color(246,248,251)) draw.RoundedBoxEx(6,0,0,w,s(24),Color(168,96,18),true,true,false,false) end
 frame.lblTitle:SetTextColor(color_white)
 return frame,UI,s,f
end
-- One label per paragraph: Source cuts label text off after 1023 bytes.
local function warningBox(parent,heading,paragraphs,s,f)
 local box=parent:Add('DPanel') box:Dock(TOP) box:DockMargin(0,0,0,s(8)) box:DockPadding(s(12),s(8),s(12),s(8))
 box.Paint=function(_,w,h) draw.RoundedBox(5,0,0,w,h,Color(255,240,214)) surface.SetDrawColor(214,137,34) surface.DrawRect(0,0,s(4),h) end
 local head=mmdhl.UI.label(box,heading,f.Strong,s(26)) head:Dock(TOP)
 local labels={}
 for i,body in ipairs(paragraphs) do
  local text=mmdhl.UI.label(box,body,f.Small,s(20)) text:Dock(TOP) text:SetWrap(true) text:SetAutoStretchVertical(true) T.WrapLabel(text,f.Small)
  if i>1 then text:DockMargin(0,s(8),0,0) end
  labels[#labels+1]=text
 end
 -- The labels grow to their wrapped text in Think, after this box was laid
 -- out: follow them there. An unchanged height does not relayout.
 local function fit(self)
  local h=head:GetTall()+s(16)+s(8)*(#labels-1) for _,text in ipairs(labels) do h=h+text:GetTall() end
  if self:GetTall()~=h then self:SetTall(h) self:InvalidateParent() end
 end
 box.PerformLayout=fit box.Think=fit
 return box
end
local function topicLine(parent,topics,s,f)
 if not topics or #topics==0 then return end
 local line=mmdhl.UI.label(parent,L('terms.mentions',{topics=T.TopicNames(topics)}),f.Body,s(26)) line:Dock(TOP) line:SetTextColor(Color(160,60,30)) line:DockMargin(0,0,0,s(6))
 line:SetWrap(true) line:SetAutoStretchVertical(true) T.WrapLabel(line,f.Body)
end
-- Acknowledgement dialog; done(true) once the player accepts, done(false) otherwise.
-- `options(bottom,s,f)` may add controls above the acceptance and returns their height.
local function acknowledge(title,heading,paragraphs,accept,confirmText,content,convar,done,options)
 local frame,UI,s,f=frame(title,940,720)
 warningBox(frame,heading,paragraphs,s,f)
 content(frame,s,f)
 local bottom=frame:Add('DPanel') bottom:Dock(BOTTOM) bottom:SetTall(s(104)) bottom:SetPaintBackground(false) bottom:DockMargin(0,s(8),0,0)
 if options then bottom:SetTall(s(104)+(options(bottom,s,f) or 0)) end
 local agree=UI.checkbox(bottom,accept,nil,f.Body,s(28)) agree:Dock(TOP)
 local never=UI.checkbox(bottom,L'terms.dont_show',nil,f.Small,s(26)) never:Dock(TOP) never:SetTooltip(L('terms.dont_show_tip',{menu=L'ui.toolmenu.characters'}))
 local row=bottom:Add('DPanel') row:Dock(BOTTOM) row:SetTall(s(38)) row:SetPaintBackground(false)
 local answered=false
 local function finish(ok)
  if answered then return end answered=true
  if ok and never:GetChecked() then RunConsoleCommand(convar,'0') end
  if IsValid(frame) then frame:Close() end
  done(ok)
 end
 local cancel=UI.button(row,L'common.cancel',function() finish(false) end,s(38),f.Body) cancel:Dock(RIGHT) cancel:SetWide(s(130))
 local go=UI.button(row,confirmText,function() finish(true) end,s(38),f.Strong,true) go:Dock(RIGHT) go:SetWide(s(220)) go:DockMargin(0,0,s(8),0) go:SetEnabled(false)
 agree.OnChange=function(_,value) go:SetEnabled(value) end
 frame.OnClose=function() if not answered then answered=true done(false) end end
 return frame
end
-- Import: shows the model's terms and asks the player to take responsibility.
function T.BeforeImport(source,kind,proceed,cancel)
 -- Read even when the warning is off: the record is kept either way.
 local notes=T.Inspect(source,true)
 if not importWarning:GetBool() then proceed() return end
 if IsValid(T.pending) then T.pending:MakePopup() return end
 local name=notes and notes.file or string.GetFileFromFilename(source)
 -- Name translation shares the model's names with the provider: ask here too.
 local names,translate=mmdhl.names
 local options=names and names.Enabled() and function(bottom,s,f)
  translate=mmdhl.UI.checkbox(bottom,L'names.import_translate',nil,f.Body,s(28)) translate:Dock(TOP) translate:SetChecked(names.ImportDefault())
  local note=mmdhl.UI.label(bottom,L'names.import_note',f.Small,s(36)) note:Dock(TOP) note:SetWrap(true) note:SetTextColor(Color(90,98,110)) note:DockMargin(s(26),0,0,s(6)) T.WrapLabel(note,f.Small)
  return s(70)
 end or nil
 T.pending=acknowledge(L('terms.import_title',{file=name}),L'terms.import_heading',{L'terms.import_warning',L'terms.disclaimer'},L'terms.import_accept',L'terms.import_confirm',function(frame,s,f)
  -- Docked to the top in creation order; the document tabs fill what is left.
  local topics=documentTabs(frame,notes,s,f)
  topicLine(frame,topics,s,f)
  if not native.InspectModelNotes then local old=mmdhl.UI.label(frame,L'terms.needs_native',f.Small,s(24)) old:Dock(TOP) old:SetTextColor(Color(150,90,20)) old:SetWrap(true) old:SetAutoStretchVertical(true) T.WrapLabel(old,f.Small) end
 end,'mmdhl_terms_warning_import',function(ok)
  T.pending=nil
  if ok then
   T.acknowledged[source]=os.time()
   if translate then names.Choose(source,translate:GetChecked()) end
   proceed()
  else cancel() end
 end,options)
end
-- Export: asks the player to confirm they may redistribute the models.
function T.BeforeExport(items,proceed)
 if not exportWarning:GetBool() then proceed() return end
 local lines={}
 for _,item in ipairs(items) do
  local record=recordFor(item.kind,item.asset) local topics=record and topicsOf(record)
  local meta=record and record.vrm and record.vrm.meta
  local note=not record and L'terms.export_no_record' or #topics>0 and L('terms.mentions',{topics=T.TopicNames(topics)}) or L'terms.export_no_topics'
  if meta and meta.allowRedistribution==false then note=L'terms.export_vrm_prohibited'..'  ·  '..note end
  lines[#lines+1]='• '..tostring(item.name)..' — '..note
 end
 acknowledge(L'terms.export_title',L'terms.export_heading',{L'terms.export_warning',L'terms.disclaimer'},L'terms.export_accept',L'export.start',function(frame,s,f)
  richText(frame,table.concat(lines,'\n'),f.Body)
 end,'mmdhl_terms_warning_export',function(ok) if ok then proceed() end end)
end
-- Short summary for the export window: how many selected models mention restrictions.
function T.ExportSummary(items)
 local flagged,missing=0,0
 for _,item in ipairs(items) do
  local record=recordFor(item.kind,item.asset)
  if not record then missing=missing+1 else local topics=topicsOf(record) if #topics>0 or (record.vrm and record.vrm.meta and record.vrm.meta.allowRedistribution==false) then flagged=flagged+1 end end
 end
 if flagged==0 and missing==0 then return nil end
 return L('terms.export_summary',{flagged=flagged,missing=missing,total=#items})
end
-- Read-only viewer from the library.
function T.Show(kind,id,name)
 local record=recordFor(kind,id)
 local frame,UI,s,f=frame(L('terms.view_title',{name=name or id:sub(1,12)}),900,680)
 local source=record and (record.origin=='workshop' and L('terms.from_package',{title=tostring(record.package or '?')}) or record.file and L('terms.from_file',{file=tostring(record.file)})) or nil
 warningBox(frame,L'terms.view_heading',source and {source,L'terms.disclaimer'} or {L'terms.disclaimer'},s,f)
 local close=UI.button(frame,L'common.close',function() frame:Close() end,s(36),f.Body) close:Dock(BOTTOM) close:DockMargin(0,s(8),0,0)
 if not record then local none=UI.label(frame,L'terms.view_none',f.Body,s(60)) none:Dock(TOP) none:SetWrap(true) T.WrapLabel(none,f.Body) return end
 local topics=T.Analyze(record) topicLine(frame,topics,s,f)
 documentTabs(frame,record,s,f)
end
-- The library's button text for a model with something to read, whether the
-- texts mention restrictions, and every topic they mention (for the tooltip).
function T.Summary(kind,id)
 local record=recordFor(kind,id) if not record then return nil end
 local topics=topicsOf(record)
 if #topics>0 then return L('terms.button_topics',{topics=T.TopicNames(topics,3)}),true,L('terms.mentions',{topics=T.TopicNames(topics)}) end
 return #T.Documents(record)>0 and L'terms.button' or L'terms.button_none',false
end
