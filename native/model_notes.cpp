#include "model_notes.hpp"
#include "vrm.hpp"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <fstream>
namespace mmd {
namespace {
constexpr size_t MaxTextFile=1u<<20;   // larger "readme" files are not documents
constexpr size_t MaxTextKept=48u<<10;  // UTF-8 bytes kept per document
constexpr size_t MaxDocuments=8;
constexpr size_t MaxEntries=4000;      // a download folder can hold thousands of files
std::wstring lower(std::wstring s){for(auto& c:s)c=wchar_t(std::towlower(c));return s;}
std::wstring decodeCodepage(std::span<const unsigned char> b,UINT codepage,bool strict){
 if(b.empty()||b.size()>INT_MAX)return {};
 DWORD flags=strict?MB_ERR_INVALID_CHARS:0;auto data=reinterpret_cast<const char*>(b.data());
 int n=MultiByteToWideChar(codepage,flags,data,int(b.size()),nullptr,0);if(n<=0)return {};
 std::wstring out(size_t(n),L'\0');if(MultiByteToWideChar(codepage,flags,data,int(b.size()),out.data(),n)!=n)return {};
 return out;
}
// Line ends become \n; control characters other than tab and newline, and BOMs, are dropped.
std::wstring normalize(const std::wstring& text){
 std::wstring out;out.reserve(text.size());
 for(size_t i=0;i<text.size();i++){
  wchar_t c=text[i];
  if(c==L'\r'){if(i+1<text.size()&&text[i+1]==L'\n')continue;c=L'\n';}
  if(c==0xFEFF||c==0x7F||(c<0x20&&c!=L'\n'&&c!=L'\t'))continue;
  out.push_back(c);
 }
 auto first=out.find_first_not_of(L" \t\n"),last=out.find_last_not_of(L" \t\n");
 return first==std::wstring::npos?std::wstring():out.substr(first,last-first+1);
}
// Character mix of a decoding: wrong code pages produce implausible scripts.
struct Script{size_t kana=0,halfKana=0,hangul=0,han=0,symbols=0,nonAscii=0;};
Script script(const std::wstring& s){
 Script r;
 for(wchar_t c:s){
  if(c<0x80)continue;r.nonAscii++;
  if(c>=0x3040&&c<=0x30FF)r.kana++;else if(c>=0xFF61&&c<=0xFF9F)r.halfKana++;else if(c>=0xAC00&&c<=0xD7A3)r.hangul++;else if(c>=0x4E00&&c<=0x9FFF)r.han++;
  if((c>=0x0370&&c<=0x04FF)||(c>=0x2500&&c<=0x257F))r.symbols++;
 }
 return r;
}
// Keeps whole UTF-8 characters.
bool clip(std::string& s,size_t maximum){
 if(s.size()<=maximum)return false;
 size_t cut=maximum;while(cut>0&&(static_cast<unsigned char>(s[cut])&0xC0)==0x80)cut--;
 s.resize(cut);return true;
}
std::string text(const std::wstring& w){return utf8(normalize(w));}
// Words in a file name that mark readme and licence documents (2) or likely ones (1).
int nameScore(const fs::path& path){
 auto name=lower(path.filename().wstring());
 static const wchar_t* strong[]={L"readme",L"read_me",L"read me",L"license",L"licence",L"copying",L"terms",L"eula",L"利用規約",L"使用規約",L"規約",L"読んで",L"よんで",L"りどみ",L"リードミー",
  L"必読",L"ガイドライン",L"guideline",L"許諾",L"利用条件",L"使用条件",L"著作権",L"使用说明",L"使用說明",L"使用须知",L"使用須知",L"条款",L"條款",L"协议",L"協議",L"规约",L"授权",L"授權",
  L"许可",L"許可",L"读我",L"讀我",L"라이선스",L"라이센스",L"약관",L"이용 규약",L"읽어"};
 static const wchar_t* weak[]={L"説明",L"注意",L"はじめに",L"について",L"credit",L"クレジット",L"说明",L"說明",L"注意事项",L"注意事項",L"about",L"notice",L"설명",L"주의"};
 for(auto word:strong)if(name.find(word)!=std::wstring::npos)return 2;
 for(auto word:weak)if(name.find(word)!=std::wstring::npos)return 1;
 return 0;
}
bool textDocument(const fs::path& path){
 auto extension=lower(path.extension().wstring());
 if(extension==L".txt"||extension==L".md"||extension==L".text"||extension==L".nfo")return true;
 if(!extension.empty())return false;
 auto name=lower(path.filename().wstring());return name==L"license"||name==L"licence"||name==L"copying"||name==L"readme";
}
bool modelFile(const fs::path& path){auto e=lower(path.extension().wstring());return e==L".pmx"||e==L".pmd"||e==L".vrm"||e==L".fbx"||e==L".glb"||e==L".gltf"||e==L".dae";}
struct Reader{
 std::ifstream in;
 template<class T>bool value(T& v){return bool(in.read(reinterpret_cast<char*>(&v),sizeof v));}
 bool bytes(std::string& s,size_t n){s.resize(n);return !n||bool(in.read(s.data(),std::streamsize(n)));}
};
// PMX 2.x: name, English name, comment, English comment in UTF-16LE or UTF-8.
// PMD: a 20-byte name and a 256-byte comment in Shift-JIS.
Json embeddedText(const fs::path& path){
 Reader r{std::ifstream(ioPath(path),std::ios::binary)};char magic[4];
 if(!r.in.read(magic,4))return Json();
 Json out=Json::object();
 auto put=[&](const char* key,std::string value){if(!value.empty()){clip(value,MaxTextKept);out[key]=value;}};
 if(!std::memcmp(magic,"PMX ",4)){
  float version;uint8_t count;std::string globals;
  if(!r.value(version)||!r.value(count)||!count||count>64||!r.bytes(globals,count))return Json();
  bool utf16=globals[0]==0;
  for(auto key:{"name","nameEnglish","comment","commentEnglish"}){
   int32_t length;std::string raw;if(!r.value(length)||length<0||length>(1<<20)||!r.bytes(raw,size_t(length)))break;
   std::string encoding;
   if(utf16)put(key,text(std::wstring(reinterpret_cast<const wchar_t*>(raw.data()),raw.size()/2)));
   else put(key,decodeText(std::span(reinterpret_cast<const unsigned char*>(raw.data()),raw.size()),encoding));
  }
  return out;
 }
 if(!std::memcmp(magic,"Pmd",3)){
  r.in.seekg(3);float version;char name[20],comment[256];
  if(!r.value(version)||!r.in.read(name,20)||!r.in.read(comment,256))return Json();
  auto field=[](const char* p,size_t n){size_t length=0;while(length<n&&p[length])length++;return std::span(reinterpret_cast<const unsigned char*>(p),length);};
  put("name",text(decodeCodepage(field(name,20),932,false)));put("comment",text(decodeCodepage(field(comment,256),932,false)));
  return out;
 }
 return Json();
}
// VRM licence metadata and the glTF copyright line; only the JSON is read.
Json gltfNotes(const fs::path& path){
 std::ifstream in(ioPath(path),std::ios::binary);Json j;
 unsigned char head[20];
 if(in.read(reinterpret_cast<char*>(head),20)&&!std::memcmp(head,"glTF",4)){
  uint32_t size,type;std::memcpy(&size,head+12,4);std::memcpy(&type,head+16,4);
  if(type!=0x4E4F534Au||size>(64u<<20))return Json();
  std::string json(size,'\0');if(!in.read(json.data(),size))return Json();
  j=Json::parse(json,nullptr,false);
 }else{
  std::error_code ec;auto size=fs::file_size(ioPath(path),ec);
  if(ec||size>(32u<<20)||lower(path.extension().wstring())!=L".gltf")return Json();
  in.clear();in.seekg(0);std::string json(size_t(size),'\0');if(!in.read(json.data(),std::streamsize(size)))return Json();
  j=Json::parse(json,nullptr,false);
 }
 if(!j.is_object())return Json();
 Json out=Json::object();
 auto meta=vrmMetadata(j,utf8(path.stem().wstring()));if(!meta.is_null())out["vrm"]=meta;
 auto asset=j.find("asset");
 if(asset!=j.end()&&asset->is_object()&&asset->contains("copyright")&&(*asset)["copyright"].is_string()){auto c=(*asset)["copyright"].get<std::string>();clip(c,MaxTextKept);if(!c.empty())out["copyright"]=c;}
 return out;
}
}
std::string decodeText(std::span<const unsigned char> b,std::string& encoding){
 std::wstring w;
 if(b.size()>=3&&b[0]==0xEF&&b[1]==0xBB&&b[2]==0xBF){encoding="utf-8";w=decodeCodepage(b.subspan(3),CP_UTF8,false);}
 else if(b.size()>=2&&b[0]==0xFF&&b[1]==0xFE){encoding="utf-16le";w.assign(reinterpret_cast<const wchar_t*>(b.data()+2),(b.size()-2)/2);}
 else if(b.size()>=2&&b[0]==0xFE&&b[1]==0xFF){encoding="utf-16be";for(size_t i=2;i+1<b.size();i+=2)w.push_back(wchar_t(b[i]<<8|b[i+1]));}
 else if(auto u=decodeCodepage(b,CP_UTF8,true);!u.empty()||b.empty()){encoding=std::all_of(b.begin(),b.end(),[](unsigned char c){return c<0x80;})?"ascii":"utf-8";w=u;}
 else{
  // Japanese readmes use Shift-JIS, Chinese ones GBK or Big5, Korean ones UHC. Each
  // byte stream decodes in several of them; the wrong ones give implausible text:
  // GBK or UHC read as Shift-JIS turn into half-width katakana, Chinese read as UHC
  // is full of hanja (rare in modern Korean), and Big5 read as GBK hits GB2312's
  // kana, Greek, Cyrillic and box-drawing rows.
  auto sjis=decodeCodepage(b,932,true);auto js=script(sjis);
  auto uhc=decodeCodepage(b,949,true);auto ks=script(uhc);
  auto gbk=decodeCodepage(b,936,true);auto gs=script(gbk);
  auto big5=decodeCodepage(b,950,true);
  if(!sjis.empty()&&js.kana*10>=js.nonAscii&&js.halfKana*4<=js.nonAscii){encoding="shift_jis";w=sjis;}
  else if(!uhc.empty()&&ks.hangul*2>=ks.nonAscii&&ks.han*10<=ks.hangul){encoding="uhc";w=uhc;}
  else if(!gbk.empty()&&(big5.empty()||(gs.kana+gs.symbols)*20<gs.nonAscii)){encoding="gbk";w=gbk;}
  else if(!big5.empty()){encoding="big5";w=big5;}
  else{encoding="shift_jis";w=sjis.empty()?decodeCodepage(b,932,false):sjis;}
 }
 return text(w);
}
Json inspectModelNotes(const fs::path& model){
 std::error_code ec;if(!fs::is_regular_file(ioPath(model),ec))throw std::runtime_error("The selected model file no longer exists");
 Json out={{"file",utf8(model.filename().wstring())},{"embedded",Json::object()},{"readmes",Json::array()}};
 auto extension=lower(model.extension().wstring());
 try{
  if(extension==L".pmx"||extension==L".pmd"){auto e=embeddedText(model);if(e.is_object())out["embedded"]=e;}
  if(extension==L".vrm"||extension==L".glb"||extension==L".gltf"){
   auto g=gltfNotes(model);
   if(g.contains("vrm"))out["vrm"]=g["vrm"];
   if(g.contains("copyright"))out["embedded"]["copyright"]=g["copyright"];
  }
 }catch(...){}
 // Readmes beside the model and, when named like one, in the folder above it.
 // In a folder of its own every text file counts; in a crowded folder (a
 // download folder) only files named like readmes or licences.
 struct Candidate{fs::path path;int score;bool own;};std::vector<Candidate> found;
 auto scan=[&](const fs::path& folder,bool own){
  std::vector<std::pair<fs::path,int>> texts;size_t models=0,seen=0;std::error_code e;
  for(auto it=fs::directory_iterator(ioPath(folder),fs::directory_options::skip_permission_denied,e);!e&&it!=fs::directory_iterator()&&seen<MaxEntries;it.increment(e),seen++){
   std::error_code f;if(!it->is_regular_file(f))continue;auto path=it->path();
   if(modelFile(path))models++;
   if(textDocument(path))texts.push_back({path,nameScore(path)});
  }
  bool alone=own&&models<=3&&texts.size()<=12;
  for(auto& [path,score]:texts)if(score>0||alone)found.push_back({path,score,own});
 };
 auto folder=model.parent_path();scan(folder,true);
 auto parent=folder.parent_path();if(!parent.empty()&&parent!=folder&&parent.has_relative_path())scan(parent,false);
 std::stable_sort(found.begin(),found.end(),[](const Candidate& a,const Candidate& b){
  if(a.score!=b.score)return a.score>b.score;if(a.own!=b.own)return a.own;return lower(a.path.filename().wstring())<lower(b.path.filename().wstring());});
 for(auto& c:found){
  if(out["readmes"].size()>=MaxDocuments)break;
  std::error_code e;auto size=fs::file_size(c.path,e);if(e||!size||size>MaxTextFile)continue;
  Bytes bytes;try{bytes=readFile(c.path);}catch(...){continue;}
  if(std::count(bytes.begin(),bytes.end(),0)>int(bytes.size()/4)&&!(bytes.size()>=2&&(bytes[0]==0xFF||bytes[0]==0xFE)))continue; // binary, not text
  std::string encoding;auto decoded=decodeText(bytes,encoding);if(decoded.empty())continue;
  bool truncated=clip(decoded,MaxTextKept);
  out["readmes"].push_back({{"name",utf8(c.path.filename().wstring())},{"folder",c.own?"model":"parent"},{"matched",c.score>0},{"encoding",encoding},{"bytes",size},{"truncated",truncated},{"text",decoded}});
 }
 return out;
}
}
