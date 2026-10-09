#include "file_access.hpp"
#include "model_notes.hpp"
#include "props/network_path.hpp"
#include <windows.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <algorithm>
#include <cwctype>
namespace mmd {
FileAccessError::FileAccessError(std::string c,const std::string& message):std::runtime_error(message),code(std::move(c)){}
namespace {
[[noreturn]] void refuse(const char* code,const std::string& message){throw FileAccessError(code,message);}
struct Handle{HANDLE h=INVALID_HANDLE_VALUE;~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
// Windows compares names case-insensitively with its own upper-case table.
bool sameText(std::wstring_view a,std::wstring_view b){return a.size()==b.size()&&(a.empty()||CompareStringOrdinal(a.data(),int(a.size()),b.data(),int(b.size()),TRUE)==CSTR_EQUAL);}
std::wstring trimmed(const fs::path& p){auto s=p.wstring();while(s.size()>1&&s.back()==L'\\'&&!(s.size()==3&&s[1]==L':'))s.pop_back();if(s.size()==3&&s[1]==L':')s.pop_back();return s;}
std::wstring lower(std::wstring s){for(auto& c:s)c=wchar_t(std::towlower(c));return s;}
std::string randomHex(size_t bytes){std::vector<unsigned char> b(bytes);if(BCryptGenRandom(nullptr,b.data(),ULONG(b.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("No random numbers");std::string s;for(auto c:b){s.push_back("0123456789abcdef"[c>>4]);s.push_back("0123456789abcdef"[c&15]);}return s;}
fs::path knownFolder(REFKNOWNFOLDERID id){PWSTR s=nullptr;fs::path out;if(SUCCEEDED(SHGetKnownFolderPath(id,KF_FLAG_DONT_VERIFY,nullptr,&s)))out=s;CoTaskMemFree(s);return out;}
// \\?\C:\x -> C:\x and \\?\UNC\host\share -> \\host\share: the forms paths are kept in.
fs::path finalPath(HANDLE h){
 std::wstring buffer(512,L'\0');
 for(;;){DWORD n=GetFinalPathNameByHandleW(h,buffer.data(),DWORD(buffer.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);if(!n)refuse("unreadable","Windows cannot tell where this file is");if(n<buffer.size()){buffer.resize(n);break;}buffer.resize(size_t(n)+1);}
 if(buffer.starts_with(L"\\\\?\\UNC\\"))return L"\\\\"+buffer.substr(8);
 if(buffer.starts_with(L"\\\\?\\"))return buffer.substr(4);
 return buffer;
}
void open(Handle& file,const fs::path& path,DWORD access,bool reparse){
 file.h=CreateFileW(ioPath(path).c_str(),access,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|(reparse?FILE_FLAG_OPEN_REPARSE_POINT:0),nullptr);
 if(file.h!=INVALID_HANDLE_VALUE)return;
 auto error=GetLastError();
 if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND||error==ERROR_INVALID_NAME||error==ERROR_BAD_PATHNAME||error==ERROR_NOT_READY||error==ERROR_INVALID_DRIVE)refuse("not_found","The file or folder does not exist");
 refuse("unreadable","Windows does not allow reading this file or folder");
}
// Opens without following a final link or junction (a link in the middle of the
// path is followed by Windows and shows in the final path instead). Other reparse
// points, such as OneDrive placeholders, are ordinary files: reopened normally.
ResolvedFile openChecked(Handle& file,const fs::path& path,DWORD access){
 open(file,path,access,true);
 FILE_ATTRIBUTE_TAG_INFO tag{};if(!GetFileInformationByHandleEx(file.h,FileAttributeTagInfo,&tag,sizeof(tag)))refuse("unreadable","Cannot read the file's attributes");
 if(tag.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){
  if(IsReparseTagNameSurrogate(tag.ReparseTag))refuse("link","Links and junctions are not followed");
  CloseHandle(file.h);file.h=INVALID_HANDLE_VALUE;open(file,path,access,false);
 }
 BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(file.h,&info))refuse("unreadable","Cannot read the file's attributes");
 ResolvedFile r;r.attributes=info.dwFileAttributes;r.folder=(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;r.size=r.folder?0:(uint64_t(info.nFileSizeHigh)<<32|info.nFileSizeLow);r.path=finalPath(file.h);return r;
}
bool reservedName(std::wstring_view part){
 auto base=std::wstring(part.substr(0,part.find(L'.')));while(!base.empty()&&base.back()==L' ')base.pop_back();
 for(auto& c:base)c=wchar_t(std::towupper(c));
 for(auto name:{L"CON",L"PRN",L"AUX",L"NUL",L"CONIN$",L"CONOUT$"})if(base==name)return true;
 return base.size()==4&&(base.starts_with(L"COM")||base.starts_with(L"LPT"))&&(std::iswdigit(base[3])||base[3]==0xB9||base[3]==0xB2||base[3]==0xB3);
}
// The parts of a path below its root, separated by backslashes; one trailing separator is fine.
void checkParts(std::wstring_view rest){
 size_t depth=0;
 for(size_t start=0;start<rest.size();){
  auto end=rest.find(L'\\',start);if(end==rest.npos)end=rest.size();auto part=rest.substr(start,end-start);
  if(part.empty())refuse("invalid_path","The path has an empty part");
  if(part==L"."||part==L"..")refuse("parent","Paths may not contain . or .. parts");
  if(part.find(L':')!=part.npos)refuse("stream","Alternate data streams and drive letters inside paths are not allowed");
  if(part.find_first_of(L"<>\"|?*")!=part.npos)refuse("invalid_path","Wildcards and reserved characters are not allowed in paths");
  if(part.back()==L'.'||part.back()==L' ')refuse("invalid_path","Path parts may not end with a dot or a space");
  if(reservedName(part))refuse("device","Device names such as CON or COM1 are not files");
  if(++depth>64)refuse("invalid_path","The path is too deep");
  start=end+1;
 }
}
std::wstring pathText(std::string_view s,size_t limit){
 if(s.size()>limit)refuse("invalid_path","The path is too long");
 for(unsigned char c:s)if(c<0x20||c==0x7F)refuse("invalid_path","The path contains control characters");
 std::wstring w;try{w=wide(s);}catch(...){refuse("invalid_path","The path is not valid UTF-8");}
 for(auto& c:w)if(c==L'/')c=L'\\';
 return w;
}
std::string language(const Json& options){
 auto code=options.contains("language")&&options["language"].is_string()?options["language"].get<std::string>():"en";
 for(auto known:{"en","fr","ja","ko","ru","zh-cn","zh-tw"})if(code==known)return code;
 return "en";
}
std::string text(const Json& o,const char* key,size_t limit){
 auto it=o.find(key);if(it==o.end()||it->is_null())return {};
 if(!it->is_string())refuse("invalid_options",std::string("Option ")+key+" must be text");
 return cleanLabel(it->get<std::string>(),limit);
}
bool flag(const Json& o,const char* key){auto it=o.find(key);if(it==o.end()||it->is_null())return false;if(!it->is_boolean())refuse("invalid_options",std::string("Option ")+key+" must be true or false");return it->get<bool>();}
uint64_t count(const Json& o,const char* key,uint64_t fallback,uint64_t low,uint64_t high){
 auto it=o.find(key);if(it==o.end()||it->is_null())return fallback;
 double v=it->is_number()?it->get<double>():-1;
 if(std::isfinite(v)&&v==std::floor(v)&&v>double(high)&&std::string(key)=="offset")refuse("offset_too_large","Files are read up to 256 MiB from their start");
 if(!std::isfinite(v)||v!=std::floor(v)||v<double(low)||v>double(high))refuse("invalid_options",std::string("Option ")+key+" must be a whole number from "+std::to_string(low)+" to "+std::to_string(high));
 return uint64_t(v);
}
// Picker filters: [[label, "*.json;*.txt"], ...]. Only extension patterns reach the dialog.
Json filters(const Json& o){
 Json out=Json::array();auto it=o.find("filters");if(it==o.end()||it->is_null())return out;
 if(!it->is_array()||it->size()>8)refuse("invalid_options","Up to 8 filters, each {label, pattern}");
 for(auto& f:*it){
  if(!f.is_array()||f.size()!=2||!f[0].is_string()||!f[1].is_string())refuse("invalid_options","Each filter is {label, pattern}");
  auto label=cleanLabel(f[0].get<std::string>(),40);auto pattern=f[1].get<std::string>();std::string clean;size_t parts=0;
  if(label.empty()||pattern.empty()||pattern.size()>200)refuse("invalid_options","Filters need a label and a pattern");
  for(size_t start=0;start<=pattern.size();){
   auto end=pattern.find(';',start);if(end==pattern.npos)end=pattern.size();auto part=pattern.substr(start,end-start);start=end+1;
   bool valid=part=="*.*"||(part.size()>2&&part.size()<=18&&part.starts_with("*.")&&std::all_of(part.begin()+2,part.end(),[](char c){return std::isalnum((unsigned char)c)||c=='_'||c=='-';}));
   if(!valid||++parts>8)refuse("invalid_options","Filter patterns look like *.json;*.txt");
   clean+=(clean.empty()?"":";")+part;
  }
  out.push_back({label,clean});
 }
 return out;
}
Json failed(const std::string& code,const std::string& message){return {{"state","failed"},{"code",code},{"error",message}};}
Json denied(const std::string& code,const std::string& message){return {{"state","denied"},{"code",code},{"error",message}};}
bool plainDirectory(const fs::path& path){auto a=GetFileAttributesW(ioPath(path).c_str());return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY)&&!(a&FILE_ATTRIBUTE_REPARSE_POINT);}
void removeFolder(const fs::path& dir){std::error_code error;if(!dir.empty()&&plainDirectory(dir))fs::remove_all(ioPath(dir),error);}
int64_t now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
int64_t unixTime(LARGE_INTEGER t){return t.QuadPart/10000000-11644473600ll;}
bool regularFile(const fs::path& path){std::error_code error;return fs::is_regular_file(path,error);}
// The dialogs quote addon text with “ ” « » 「 」: a closing mark (or one that looks like it)
// inside the text would let the addon write sentences that seem to be Model Hotloader's.
bool quotationMark(wchar_t c){
 static constexpr std::wstring_view marks=L"\"«»‹›“”„‟″‶ʺ˝״≪≫❝❞〃〈〉《》「」『』〝〞〟﹁﹂﹃﹄＂｢｣";
 return marks.find(c)!=marks.npos;
}
}
std::string cleanLabel(std::string_view text,size_t maxCharacters){
 std::wstring w;try{w=wide(text);}catch(...){refuse("invalid_options","Text from the addon is not valid UTF-8");}
 std::wstring out;size_t used=0;bool space=false;
 for(size_t i=0;i<w.size()&&used<maxCharacters;i++){
  wchar_t c=w[i];
  // Line breaks become spaces; other control, format and direction characters could disguise the text.
  if(c==L'\t'||c==L'\n'||c==L'\r'||c==0x2028||c==0x2029||std::iswspace(c)||c==0xA0||c==0x3000){space=true;continue;}
  if(c<0x20||(c>=0x7F&&c<0xA0)||c==0xAD||c==0x061C||(c>=0x200B&&c<=0x200F)||(c>=0x202A&&c<=0x202E)||(c>=0x2060&&c<=0x206F)||c==0xFEFF||(c>=0xFFF9&&c<=0xFFFB))continue;
  if(c>=0xDC00&&c<=0xDFFF)continue;
  bool pair=c>=0xD800&&c<=0xDBFF;if(pair&&!(i+1<w.size()&&w[i+1]>=0xDC00&&w[i+1]<=0xDFFF))continue;
  if(space&&!out.empty()){out.push_back(L' ');if(++used>=maxCharacters)break;}
  space=false;out.push_back(quotationMark(c)?L'\'':c);if(pair)out.push_back(w[++i]);used++;
 }
 return utf8(out);
}
bool insideFolder(const fs::path& path,const fs::path& folder){
 auto p=trimmed(path),f=trimmed(folder);if(f.empty()||p.size()<f.size())return false;
 return sameText(std::wstring_view(p).substr(0,f.size()),f)&&(p.size()==f.size()||p[f.size()]==L'\\');
}
bool FilePolicy::deniedPath(const fs::path& path) const {
 for(auto& d:denied)if(insideFolder(path,d))return true;
 for(auto& part:path){auto name=lower(part.wstring());for(auto& prefix:deniedNames)if(name.starts_with(prefix))return true;}
 return false;
}
bool FilePolicy::broadFolder(const fs::path& folder) const {
 if(!folder.has_relative_path()||trimmed(folder)==trimmed(folder.root_path()))return true;
 // A folder that holds one of them is broader still (C:\Users\me\AppData holds Roaming and Local).
 for(auto& b:broad)if(insideFolder(b,folder))return true;
 return false;
}
FilePolicy FilePolicy::system(const fs::path& gameRoot){
 FilePolicy p;
 // Known folders as their final paths, so a moved Documents or AppData still matches.
 auto canonical=[](const fs::path& path)->fs::path{if(path.empty())return {};try{return resolveFile(path).path;}catch(...){return path.lexically_normal();}};
 auto profile=knownFolder(FOLDERID_Profile),roaming=knownFolder(FOLDERID_RoamingAppData),local=knownFolder(FOLDERID_LocalAppData);
 auto deny=[&](const fs::path& path){if(!path.empty())p.denied.push_back(canonical(path));};
 deny(knownFolder(FOLDERID_Windows));
 if(auto data=knownFolder(FOLDERID_ProgramData);!data.empty())deny(data/L"Microsoft");
 if(!roaming.empty()){
  for(auto name:{L"Credentials",L"Protect",L"Crypto",L"Vault",L"SystemCertificates"})deny(roaming/L"Microsoft"/name);
  for(auto app:{L"Mozilla\\Firefox",L"Opera Software",L"Thunderbird",L"discord\\Local Storage",L"discordcanary\\Local Storage",L"discordptb\\Local Storage",L"gnupg"})deny(roaming/app);
  // Sessions, saved passwords and wallets that work without Windows' own protection.
  for(auto app:{L"Telegram Desktop\\tdata",L"Signal",L"FileZilla",L"Exodus",L"Electrum\\wallets",L"Ethereum\\keystore",L"Bitcoin\\wallets"})deny(roaming/app);
 }
 if(!local.empty()){
  for(auto name:{L"Credentials",L"Vault"})deny(local/L"Microsoft"/name);
  for(auto browser:{L"Google\\Chrome\\User Data",L"Microsoft\\Edge\\User Data",L"BraveSoftware\\Brave-Browser\\User Data",L"Chromium\\User Data",L"Vivaldi\\User Data",L"Yandex\\YandexBrowser\\User Data",L"Mozilla\\Firefox"})deny(local/browser);
  deny(local/L"ModelHotloader");
 }
 if(!profile.empty())for(auto keys:{L".ssh",L".gnupg",L".aws",L".azure",L".kube",L".docker",L".config\\gh"})deny(profile/keys);
 // Steam's login tokens: its config folder and the ssfn files beside it.
 wchar_t steam[MAX_PATH]{};DWORD size=sizeof(steam);
 if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,steam,&size)==ERROR_SUCCESS&&steam[0]){fs::path root(steam);root.make_preferred();deny(root/L"config");}
 if(!gameRoot.empty())deny(gameRoot/L"garrysmod"/L"cfg");
 // Registry hives, Steam's login files, plain-text tokens (git, npm, netrc, PyPI) and wallets, anywhere.
 p.deniedNames={L"ntuser.dat",L"usrclass.dat",L"ssfn",L"mmdhl-fa-",L".git-credentials",L".npmrc",L".netrc",L"_netrc",L".pypirc",L"wallet.dat"};
 for(auto id:{&FOLDERID_UserProfiles,&FOLDERID_Profile,&FOLDERID_Desktop,&FOLDERID_Documents,&FOLDERID_Downloads,&FOLDERID_Pictures,&FOLDERID_Videos,&FOLDERID_Music,&FOLDERID_SavedGames,
  &FOLDERID_RoamingAppData,&FOLDERID_LocalAppData,&FOLDERID_LocalAppDataLow,&FOLDERID_ProgramData,&FOLDERID_ProgramFiles,&FOLDERID_ProgramFilesX86,&FOLDERID_SkyDrive,&FOLDERID_Public})
  if(auto path=knownFolder(*id);!path.empty())p.broad.push_back(canonical(path));
 return p;
}
namespace {
// X:\... only, checked as text.
fs::path drivePath(std::string_view s){
 if(s.empty())refuse("invalid_path","The path is empty");
 auto w=pathText(s,2048);
 if(props::networkPath(s))refuse("network","Paths to other computers and devices are never read");
 if(w.size()<3||w[0]>0x7F||!std::iswalpha(w[0])||w[1]!=L':'||w[2]!=L'\\')refuse("relative","Only absolute paths that start with a drive letter (C:\\...) can be requested");
 w[0]=wchar_t(std::towupper(w[0]));checkParts(std::wstring_view(w).substr(3));
 if(w.size()>3&&w.back()==L'\\')w.pop_back();
 return fs::path(w);
}
// Asked before anything opens the path: opening a network drive contacts its server.
const char* driveProblem(const fs::path& path,bool requireLocal){
 wchar_t root[]={path.wstring()[0],L':',L'\\',0};auto type=GetDriveTypeW(root);
 if(type==DRIVE_NO_ROOT_DIR||type==DRIVE_UNKNOWN)return "not_found";
 return requireLocal&&type==DRIVE_REMOTE?"remote_drive":nullptr;
}
}
fs::path checkRequestedPath(std::string_view s,bool requireLocal,const FilePolicy& policy){
 auto path=drivePath(s);
 if(auto problem=driveProblem(path,requireLocal))refuse(problem,problem==std::string("not_found")?"There is no such drive":"Files on network drives can only be chosen in the file picker");
 if(policy.deniedPath(path))refuse("denied_location","Model Hotloader never lets addons read this location");
 return path;
}
fs::path checkRelativePath(std::string_view s){
 if(s.empty())return {};
 auto w=pathText(s,1024);
 if(w.front()==L'\\')refuse("invalid_path","Paths inside a folder may not start with a separator");
 if(w.find(L':')!=w.npos)refuse("stream","Alternate data streams and drive letters inside paths are not allowed");
 checkParts(w);if(w.back()==L'\\')w.pop_back();return fs::path(w);
}
ResolvedFile resolveFile(const fs::path& path){Handle file;return openChecked(file,path,GENERIC_READ);}
std::string displayPath(const fs::path& path){
 static const std::wstring profile=[]{auto p=knownFolder(FOLDERID_Profile);try{return trimmed(resolveFile(p).path);}catch(...){return trimmed(p);}}();
 auto text=trimmed(path);
 if(!profile.empty()&&insideFolder(path,profile))return utf8(L"~"+text.substr(profile.size()));
 return utf8(text);
}
fs::path fileAccessStore(){auto local=knownFolder(FOLDERID_LocalAppData);if(local.empty())throw std::runtime_error("No local application data folder");return local/L"ModelHotloader"/L"file-access.json";}
void FileGrantStore::load(const FilePolicy& policy){
 enabled=true;grants.clear();
 Json j;try{std::error_code error;if(!fs::is_regular_file(ioPath(file),error)||fs::file_size(ioPath(file),error)>(1u<<20))return;j=readJson(file);}catch(...){return;}
 if(!j.is_object()||!j.contains("schema")||j["schema"]!=1)return;
 if(j.contains("enabled")&&j["enabled"].is_boolean())enabled=j["enabled"].get<bool>();
 if(!j.contains("grants")||!j["grants"].is_array())return;
 for(auto& g:j["grants"]){
  try{
   FileGrant grant;grant.id=g.at("id").get<std::string>();grant.requester=g.at("requester").get<std::string>();
   if(grant.id.size()!=16||!std::all_of(grant.id.begin(),grant.id.end(),[](char c){return std::isxdigit((unsigned char)c)&&!std::isupper((unsigned char)c);}))continue;
   if(grant.requester.empty()||cleanLabel(grant.requester,64)!=grant.requester)continue;
   // Text checks only: a grant on an unplugged drive stays for when it returns.
   grant.folder=drivePath(g.at("folder").get<std::string>());if(policy.deniedPath(grant.folder)||policy.broadFolder(grant.folder))continue;
   grant.created=g.value("created",int64_t(0));grant.used=g.value("used",int64_t(0));
   bool duplicate=false;for(auto& other:grants)duplicate|=other.id==grant.id||(other.requester==grant.requester&&sameText(trimmed(other.folder),trimmed(grant.folder)));
   if(!duplicate&&grants.size()<256)grants.push_back(std::move(grant));
  }catch(...){}
 }
}
void FileGrantStore::save() const {
 Json list=Json::array();for(auto& g:grants)list.push_back({{"id",g.id},{"requester",g.requester},{"folder",utf8(g.folder.wstring())},{"created",g.created},{"used",g.used}});
 fs::create_directories(ioPath(file.parent_path()));writeJson(file,{{"schema",1},{"enabled",enabled},{"grants",list}});
}
const FileGrant* FileGrantStore::covering(const std::string& requester,const fs::path& path) const {
 for(auto& g:grants)if(g.requester==requester&&insideFolder(path,g.folder))return &g;
 return nullptr;
}
struct FileAccess::Item {std::string name,requester,grant;fs::path root;bool folder=false;uint64_t size=0;};
struct FileAccess::Request {
 enum Kind{Pick,Path,Enable} kind=Path;
 std::string requester,script,purpose,title,language;Json filters=Json::array(),result;bool multiple=false,folder=false;
 fs::path path,dir,rememberFolder;HANDLE process=nullptr,group=nullptr;
 std::optional<ResolvedFile> target;std::string problem;bool canRemember=false;
};
namespace {
struct ReadOptions {fs::path relative;uint64_t offset=0,length=FileReadDefault;bool text=false,hidden=false;};
// Opens root\relative and proves it is still the thing the player allowed: no final
// link, the final path inside the granted root, not denied, not hidden unless asked.
ResolvedFile openItem(Handle& file,const FileAccess::Item& item,const fs::path& relative,bool hidden,const FilePolicy& policy,DWORD access){
 if(!relative.empty()&&!item.folder)refuse("invalid_options","Only folders have paths inside them");
 auto target=relative.empty()?item.root:item.root/relative;
 if(!hidden&&!relative.empty()){auto at=item.root;for(auto& part:relative){at/=part;auto a=GetFileAttributesW(ioPath(at).c_str());if(a!=INVALID_FILE_ATTRIBUTES&&(a&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM)))refuse("hidden","Hidden and system files are read only when asked for");}}
 auto r=openChecked(file,target,access);
 if(relative.empty()?!sameText(trimmed(r.path),trimmed(item.root)):!insideFolder(r.path,item.root))refuse("outside","The file now leads outside what the player allowed");
 if(policy.deniedPath(r.path))refuse("denied_location","Model Hotloader never lets addons read this location");
 return r;
}
FileReadResult readItem(const FileAccess::Item& item,const ReadOptions& o,const FilePolicy& policy){
 Handle file;auto r=openItem(file,item,o.relative,o.hidden,policy,GENERIC_READ);
 if(r.folder)refuse("not_a_file","This is a folder; list it or read a file inside it");
 FileReadResult out;out.info={{"name",utf8((o.relative.empty()?item.root:o.relative).filename().wstring())},{"size",r.size},{"offset",o.offset}};
 uint64_t end=std::min({r.size,o.offset+o.length,FileOffsetMaximum});
 if(o.text&&(o.offset||r.size>o.length))refuse("too_large","Text is read whole; this file is larger than the limit");
 if(o.offset<end){
  LARGE_INTEGER at;at.QuadPart=LONGLONG(o.offset);if(!SetFilePointerEx(file.h,at,nullptr,FILE_BEGIN))refuse("unreadable","Cannot seek in the file");
  out.data.resize(size_t(end-o.offset));size_t cursor=0;
  while(cursor<out.data.size()){DWORD received=0;if(!ReadFile(file.h,out.data.data()+cursor,DWORD(std::min<size_t>(out.data.size()-cursor,1u<<24)),&received,nullptr))refuse("unreadable","The file could not be read");if(!received)break;cursor+=received;}
  out.data.resize(cursor);
 }
 out.info["read"]=out.data.size();out.info["eof"]=o.offset+out.data.size()>=r.size;
 if(!out.info["eof"].get<bool>()&&o.offset+out.data.size()>=FileOffsetMaximum)out.info["limited"]=true;
 if(o.text){std::string encoding;out.data=decodeText(std::span(reinterpret_cast<const unsigned char*>(out.data.data()),out.data.size()),encoding);out.info["encoding"]=encoding;}
 return out;
}
// One level of a folder, read through the opened handle (no second lookup by name).
Json listItem(const FileAccess::Item& item,const fs::path& relative,bool hidden,const FilePolicy& policy){
 Handle folder;auto r=openItem(folder,item,relative,hidden,policy,FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|SYNCHRONIZE);
 if(!r.folder)refuse("not_a_folder","This is a file; read it instead");
 Json entries=Json::array();bool truncated=false;std::vector<unsigned char> buffer(64u<<10);auto kind=FileIdBothDirectoryRestartInfo;DWORD error=ERROR_SUCCESS;
 while(!truncated){
  if(!GetFileInformationByHandleEx(folder.h,kind,buffer.data(),DWORD(buffer.size()))){error=GetLastError();break;}
  kind=FileIdBothDirectoryInfo;
  for(size_t at=0;;){
   auto e=reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(buffer.data()+at);std::wstring name(e->FileName,e->FileNameLength/sizeof(wchar_t));
   // Links are left out (they are never followed); for a reparse point EaSize holds its tag.
   bool skip=name==L"."||name==L".."||((e->FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&IsReparseTagNameSurrogate(e->EaSize))||(!hidden&&(e->FileAttributes&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM)))||policy.deniedPath(r.path/name);
   if(!skip){
    if(entries.size()>=FileListMaximum){truncated=true;break;}
    bool isFolder=(e->FileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;Json entry={{"name",utf8(name)},{"folder",isFolder},{"modified",unixTime(e->LastWriteTime)}};
    if(!isFolder)entry["size"]=uint64_t(e->EndOfFile.QuadPart);entries.push_back(entry);
   }
   if(!e->NextEntryOffset)break;at+=e->NextEntryOffset;
  }
 }
 if(error!=ERROR_SUCCESS&&error!=ERROR_NO_MORE_FILES)refuse("unreadable","The folder could not be listed");
 std::sort(entries.begin(),entries.end(),[](const Json& a,const Json& b){if(a["folder"]!=b["folder"])return a["folder"].get<bool>();return lower(wide(a["name"].get<std::string>()))<lower(wide(b["name"].get<std::string>()));});
 return {{"entries",entries},{"truncated",truncated}};
}
// The dialog's request and answer go through a folder only this process creates, in
// the user's temporary folder: Lua can write anything under garrysmod/data.
fs::path privateFolder(const fs::path& temp){
 if(temp.empty())refuse("dialog_failed","No temporary folder");
 for(int attempt=0;attempt<8;attempt++){auto dir=temp/wide("mmdhl-fa-"+randomHex(16));if(CreateDirectoryW(ioPath(dir).c_str(),nullptr))return dir;}
 refuse("dialog_failed","Cannot create a private folder for the dialog");
}
void launchDialog(FileAccess::Request& r,const fs::path& worker,const wchar_t* mode){
 if(!regularFile(worker))refuse("worker_missing","The Model Hotloader worker is missing");
 auto quote=[](const fs::path& p){if(p.wstring().find(L'"')!=std::wstring::npos)refuse("dialog_failed","Invalid worker path");return L"\""+p.wstring()+L"\"";};
 auto cmd=quote(worker)+L" "+mode+L" "+quote(r.dir);
 r.group=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;SetInformationJobObject(r.group,JobObjectExtendedLimitInformation,&limits,sizeof(limits));
 STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};
 if(!CreateProcessW(worker.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,worker.parent_path().c_str(),&start,&process)){CloseHandle(r.group);r.group=nullptr;refuse("dialog_failed","Cannot start the dialog");}
 if(!AssignProcessToJobObject(r.group,process.hProcess)){TerminateProcess(process.hProcess,1);CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(r.group);r.group=nullptr;refuse("dialog_failed","Cannot isolate the dialog");}
 ResumeThread(process.hThread);CloseHandle(process.hThread);r.process=process.hProcess;
}
}
FileAccess::FileAccess(FileAccessConfig c):config(std::move(c)),policy(std::make_shared<FilePolicy>(config.policy)){
 if(config.temp.empty()){wchar_t temp[MAX_PATH+1]{};if(GetTempPathW(MAX_PATH+1,temp))config.temp=temp;}
 store.file=config.store;store.load(*policy);
 // Private folders a crashed game left behind.
 std::error_code error;
 for(auto it=fs::directory_iterator(config.temp,error);!error&&it!=fs::directory_iterator();it.increment(error)){
  auto name=it->path().filename().wstring();std::error_code e;
  if(name.starts_with(L"mmdhl-fa-")&&plainDirectory(it->path())&&fs::last_write_time(it->path(),e)<fs::file_time_type::clock::now()-std::chrono::hours(24))removeFolder(it->path());
 }
}
FileAccess::~FileAccess(){for(auto& [id,r]:requests)stop(*r);}
// The player's answer counts at once; saving it is best effort, so a locked or read-only
// store cannot turn a decision into a failure. false: the change lasts until the map changes.
bool FileAccess::persist(){try{store.save();return true;}catch(...){return false;}}
void FileAccess::stop(Request& r){
 // The folder can go once the worker is gone (it may still hold request.json open).
 if(r.group){TerminateJobObject(r.group,1);if(r.process)WaitForSingleObject(r.process,2000);CloseHandle(r.group);r.group=nullptr;}
 if(r.process){CloseHandle(r.process);r.process=nullptr;}
 removeFolder(r.dir);r.dir.clear();
}
Json FileAccess::info(){
 std::string reason=!localServerRealm()?"no_local_server":!store.enabled?"disabled":!regularFile(config.worker)?"worker_missing":"ok";
 return {{"version",1},{"available",reason=="ok"},{"reason",reason},{"enabled",store.enabled},{"local",localServerRealm()},{"maxRead",FileReadMaximum},{"defaultRead",FileReadDefault},{"maxOffset",FileOffsetMaximum},{"maxList",FileListMaximum},{"dialog",active!=0},{"queued",queue.size()}};
}
// The enable confirmation counts its refusals under a key no cleaned label can be.
static const std::string EnableRequester="\x01enable";
static void localOnly(){if(!localServerRealm())refuse("unavailable_remote","File access works only in single player and on a server this game hosts");}
void FileAccess::available(const std::string& requester){
 localOnly();
 if(!store.enabled)refuse("disabled","The player turned file access for other addons off");
 if(!regularFile(config.worker))refuse("worker_missing","The Model Hotloader worker is missing");
 if(auto it=denials.find(requester);it!=denials.end()&&it->second>=FileDenialLimit)refuse("auto_denied","The player denied this addon three times; it can ask again after the map changes");
 // Labels are free to change, so a script cannot ask forever under new names either.
 if(refusals>=FileRefusalLimit)refuse("auto_denied_session","The player refused or closed ten requests; addons can ask again after the map changes");
}
static void common(FileAccess::Request& r,const Json& o){
 if(!o.is_object())refuse("invalid_options","File access options must be an object");
 r.requester=text(o,"requester",64);if(r.requester.empty())refuse("invalid_options","Name the requesting addon (requester)");
 r.script=text(o,"script",160);r.purpose=text(o,"purpose",120);r.language=language(o);
}
uint64_t FileAccess::pick(const Json& o){
 auto r=std::make_unique<Request>();r->kind=Request::Pick;common(*r,o);
 r->title=text(o,"title",80);r->multiple=flag(o,"multiple");r->folder=flag(o,"folder");r->filters=filters(o);
 available(r->requester);return enqueue(std::move(r));
}
uint64_t FileAccess::request(const Json& o){
 auto r=std::make_unique<Request>();r->kind=Request::Path;common(*r,o);r->folder=flag(o,"folder");
 if(!o.contains("path")||!o["path"].is_string())refuse("invalid_options","Give the path to request");
 available(r->requester);r->path=drivePath(o["path"].get<std::string>());
 // Only the text is refused at once. A missing or network drive or a never-readable place
 // is told in the window like a missing file, with nothing opened: an immediate answer
 // would let a script learn silently which drives or user folders this computer has.
 if(auto problem=driveProblem(r->path,true))r->problem=problem;else if(policy->deniedPath(r->path))r->problem="denied_location";
 // A remembered folder answers without asking, once the path proves to lead inside it.
 if(r->problem.empty()&&store.covering(r->requester,r->path))try{
  auto target=resolveFile(r->path);
  if(target.folder==r->folder&&!policy->deniedPath(target.path))if(auto g=store.covering(r->requester,target.path))r->result=grant(*r,target,g->id);
 }catch(const FileAccessError&){}
 return enqueue(std::move(r));
}
uint64_t FileAccess::enqueue(std::unique_ptr<Request> r){
 pump();
 if(r->result.is_null()&&queue.size()>=FileQueueMaximum)refuse("busy","Too many file requests are waiting for the player");
 // Answers nobody collected (the addon stopped polling) do not pile up.
 std::vector<uint64_t> done;for(auto& [id,old]:requests)if(!old->result.is_null())done.push_back(id);
 for(size_t i=0;i+32<done.size();i++)requests.erase(done[i]);
 auto id=sequence++;if(r->result.is_null())queue.push_back(id);requests.emplace(id,std::move(r));pump();return id;
}
// Other failures get fixed sentences: a system message can name a folder in the player's
// profile (and with it the Windows user name), and paths never travel to Lua.
void FileAccess::pump(){
 if(active){
  auto& r=*requests.at(active);if(WaitForSingleObject(r.process,0)==WAIT_TIMEOUT)return;
  Json answer;try{answer=readJson(r.dir/L"result.json");}catch(...){}
  stop(r);active=0;
  try{finish(r,answer);}catch(const FileAccessError& e){r.result=failed(e.code,e.what());}catch(const std::exception&){r.result=failed("dialog_failed","The answer could not be handled");}
 }
 while(!active&&!queue.empty()){
  auto id=queue.front();queue.pop_front();auto& r=*requests.at(id);
  try{start(r);if(r.result.is_null())active=id;}
  catch(const FileAccessError& e){stop(r);r.result=failed(e.code,e.what());}catch(const std::exception&){stop(r);r.result=failed("dialog_failed","The dialog could not be prepared");}
 }
}
void FileAccess::start(Request& r){
 Json request={{"language",r.language},{"requester",r.requester},{"script",r.script},{"purpose",r.purpose}};
 if(r.kind==Request::Path){
  // The path is opened here, after the request was accepted: the player sees the
  // dialog whether it exists or not, so an addon cannot probe for files silently.
  if(r.problem.empty())try{auto t=resolveFile(r.path);
   if(t.folder!=r.folder)r.problem=r.folder?"not_a_folder":"not_a_file";
   else if(policy->deniedPath(t.path))r.problem="denied_location";
   else if(props::networkPath(utf8(t.path.wstring())))r.problem="network";
   else r.target=t;
  }catch(const FileAccessError& e){r.problem=e.code;}
  if(r.target){
   if(auto g=store.covering(r.requester,r.target->path)){r.result=grant(r,*r.target,g->id);return;}
   r.rememberFolder=r.folder?r.target->path:r.target->path.parent_path();
   r.canRemember=!policy->broadFolder(r.rememberFolder)&&!policy->deniedPath(r.rememberFolder);
  }
  request.update({{"kind","consent"},{"path",utf8((r.target?r.target->path:r.path).wstring())},{"folder",r.folder},{"size",r.target?r.target->size:0},{"problem",r.problem},{"remember",r.canRemember},{"rememberFolder",utf8(r.rememberFolder.wstring())}});
 }
 else if(r.kind==Request::Pick)request.update({{"kind","pick"},{"title",r.title},{"filters",r.filters},{"multiple",r.multiple},{"folder",r.folder}});
 else request["kind"]="enable";
 r.dir=privateFolder(config.temp);writeJson(r.dir/L"request.json",request);
 launchDialog(r,config.worker,r.kind==Request::Pick?L"--fa-pick":L"--fa-consent");
}
Json FileAccess::grant(Request& r,const ResolvedFile& target,const std::string& grantId){
 if(items.size()>=1024)refuse("too_many_items","Release files the addon no longer needs");
 auto entry=std::make_shared<Item>();entry->root=target.path;entry->folder=target.folder;entry->size=target.size;entry->requester=r.requester;entry->grant=grantId;
 entry->name=utf8(target.path.filename().empty()?target.path.wstring():target.path.filename().wstring());
 auto handle=randomHex(16);items[handle]=entry;
 if(!grantId.empty())for(auto& g:store.grants)if(g.id==grantId&&g.used!=now()){g.used=now();persist();}
 Json item={{"handle",handle},{"name",entry->name},{"folder",entry->folder},{"remembered",!grantId.empty()},{"displayPath",displayPath(target.path)}};
 if(!entry->folder)item["size"]=entry->size;
 return {{"state","granted"},{"items",Json::array({item})}};
}
void FileAccess::finish(Request& r,const Json& answer){
 auto said=answer.is_object()&&answer.contains("answer")&&answer["answer"].is_string()?answer["answer"].get<std::string>():std::string();
 if(said.empty()){r.result=failed("dialog_failed","The dialog closed without an answer");return;}
 auto refused=[&](const char* message){denials[r.requester]++;refusals++;r.result=denied("denied",message);};
 if(r.kind==Request::Enable){
  if(said!="yes"){refused("The player kept file access off");return;}
  store.enabled=true;r.result={{"state","granted"},{"enabled",true},{"changed",true}};if(!persist())r.result["notSaved"]=true;return;
 }
 if(r.kind==Request::Pick){
  // Closing a picker is not a refusal of the addon (the player usually opened it from the
  // addon's own menu); it counts only towards the map's limit, which stops a picker loop.
  if(said!="selected"||!answer.contains("paths")||!answer["paths"].is_array()){refusals++;r.result=denied("denied","The player chose nothing");return;}
  Json granted=Json::array(),rejected=Json::array();std::string first;
  for(auto& p:answer["paths"]){
   if(!p.is_string()||granted.size()>=64)continue;
   try{
    // A file on a mapped network drive may be chosen here; a raw \\host path may not.
    auto path=checkRequestedPath(p.get<std::string>(),false,*policy);auto target=resolveFile(path);
    if(target.folder!=r.folder)refuse(r.folder?"not_a_folder":"not_a_file","The choice is not what the addon asked for");
    if(policy->deniedPath(target.path))refuse("denied_location","Model Hotloader never lets addons read this location");
    granted.push_back(grant(r,target,"")["items"][0]);
   }catch(const FileAccessError& e){if(first.empty())first=e.code;rejected.push_back({{"name",utf8(fs::path(wide(p.get<std::string>())).filename().wstring())},{"code",e.code}});}
  }
  if(granted.empty()){r.result=failed(first.empty()?"not_found":first,"Nothing the player chose can be read");return;}
  // The player trusts this addon after all: earlier refusals no longer count against it.
  denials.erase(r.requester);r.result={{"state","granted"},{"items",granted}};if(!rejected.empty())r.result["refused"]=rejected;return;
 }
 if(!r.target||(said!="once"&&said!="always")){refused("The player denied the request");return;}
 // The target is checked again when it is read; "always" is kept only where the dialog offered it.
 std::string grantId;bool saved=true;
 if(said=="always"&&r.canRemember){
  FileGrant g;g.id=randomHex(8);g.requester=r.requester;g.folder=r.rememberFolder;g.created=g.used=now();
  store.grants.erase(std::remove_if(store.grants.begin(),store.grants.end(),[&](const FileGrant& o){return o.requester==g.requester&&insideFolder(o.folder,g.folder);}),store.grants.end());
  if(store.grants.size()>=256)store.grants.erase(store.grants.begin());
  store.grants.push_back(g);grantId=g.id;saved=persist();
 }
 denials.erase(r.requester);
 r.result=grant(r,*r.target,grantId);if(!grantId.empty())r.result["changed"]=true;if(!saved)r.result["notSaved"]=true;
}
Json FileAccess::poll(uint64_t id){
 pump();auto it=requests.find(id);if(it==requests.end())refuse("unknown_request","Unknown file request");
 auto& r=*it->second;
 if(r.result.is_null()){size_t position=active?1:0;for(auto q:queue){if(q==id)break;position++;}return {{"state","pending"},{"dialog",active==id},{"position",active==id?0:position}};}
 auto result=r.result;requests.erase(it);return result;
}
void FileAccess::cancel(uint64_t id){
 if(auto it=requests.find(id);it!=requests.end()){
  if(active==id){stop(*it->second);active=0;}
  queue.erase(std::remove(queue.begin(),queue.end(),id),queue.end());requests.erase(it);pump();return;
 }
 if(auto it=reads.find(id);it!=reads.end()){droppedReads.push_back(std::move(it->second.future));reads.erase(it);}
 if(auto it=lists.find(id);it!=lists.end()){droppedLists.push_back(std::move(it->second.future));lists.erase(it);}
}
namespace {
// Runs f on its own thread and notes when it ended, failures included.
template<class F> auto work(F f){
 auto finished=std::make_shared<std::atomic<uint64_t>>(0);
 return FileWork<decltype(f())>{std::async(std::launch::async,[f,finished]{struct Done{std::atomic<uint64_t>& at;~Done(){at=GetTickCount64();}} done{*finished};return f();}),finished};
}
}
// The slots: reads and listings still running (canceled ones until they end) and results
// waiting to be collected. A result nobody collects within keepResultsMs is dropped, so a
// script that stops polling (or forgot its ids on a Lua refresh) cannot hold them for good.
size_t FileAccess::running(){
 auto ready=[](auto& f){return f.wait_for(std::chrono::seconds(0))==std::future_status::ready;};
 std::erase_if(droppedReads,ready);std::erase_if(droppedLists,ready);
 auto now=GetTickCount64();auto stale=[&](auto& entry){auto at=entry.second.finished->load();return at&&now-at>=config.keepResultsMs&&ready(entry.second.future);};
 std::erase_if(reads,stale);std::erase_if(lists,stale);
 return reads.size()+lists.size()+droppedReads.size()+droppedLists.size();
}
std::shared_ptr<const FileAccess::Item> FileAccess::item(const std::string& handle) const {
 auto it=items.find(handle);if(it==items.end())refuse("released","This file was released or the map changed; ask for it again");return it->second;
}
uint64_t FileAccess::read(const std::string& handle,const Json& o){
 if(!localServerRealm()||!store.enabled)refuse(!store.enabled?"disabled":"unavailable_remote","File access is not available");
 auto entry=item(handle);if(!o.is_object()&&!o.is_null())refuse("invalid_options","Read options must be an object");
 Json options=o.is_object()?o:Json::object();ReadOptions r;
 auto mode=options.value("mode",std::string("binary"));if(mode!="binary"&&mode!="text")refuse("invalid_options","mode is binary or text");
 r.text=mode=="text";r.hidden=flag(options,"hidden");r.offset=count(options,"offset",0,0,FileOffsetMaximum);r.length=count(options,"length",FileReadDefault,1,FileReadMaximum);
 if(options.contains("relative")&&!options["relative"].is_null()){if(!options["relative"].is_string())refuse("invalid_options","relative must be text");r.relative=checkRelativePath(options["relative"].get<std::string>());}
 if(!r.relative.empty()&&!entry->folder)refuse("invalid_options","Only folders have paths inside them");
 if(running()>=8)refuse("busy","Too many reads are running");
 auto id=sequence++;auto rules=policy;reads.emplace(id,work([entry,r,rules]{return readItem(*entry,r,*rules);}));return id;
}
std::optional<FileReadResult> FileAccess::pollRead(uint64_t id){
 auto it=reads.find(id);if(it==reads.end())refuse("unknown_request","Unknown read");
 if(it->second.future.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return std::nullopt;
 auto future=std::move(it->second.future);reads.erase(it);return future.get();
}
uint64_t FileAccess::list(const std::string& handle,const Json& o){
 if(!localServerRealm()||!store.enabled)refuse(!store.enabled?"disabled":"unavailable_remote","File access is not available");
 auto entry=item(handle);if(!o.is_object()&&!o.is_null())refuse("invalid_options","List options must be an object");
 Json options=o.is_object()?o:Json::object();fs::path relative;bool hidden=flag(options,"hidden");
 if(options.contains("relative")&&!options["relative"].is_null()){if(!options["relative"].is_string())refuse("invalid_options","relative must be text");relative=checkRelativePath(options["relative"].get<std::string>());}
 if(!entry->folder)refuse("not_a_folder","Only folders can be listed");
 if(running()>=8)refuse("busy","Too many reads are running");
 auto id=sequence++;auto rules=policy;lists.emplace(id,work([entry,relative,hidden,rules]{return listItem(*entry,relative,hidden,*rules);}));return id;
}
std::optional<Json> FileAccess::pollList(uint64_t id){
 auto it=lists.find(id);if(it==lists.end())refuse("unknown_request","Unknown listing");
 if(it->second.future.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return std::nullopt;
 auto future=std::move(it->second.future);lists.erase(it);return future.get();
}
void FileAccess::release(const std::string& handle){items.erase(handle);}
Json FileAccess::grants(){
 localOnly();
 Json list=Json::array();for(auto& g:store.grants)list.push_back({{"id",g.id},{"requester",g.requester},{"folder",displayPath(g.folder)},{"created",g.created},{"used",g.used}});
 return {{"enabled",store.enabled},{"grants",list}};
}
bool FileAccess::revoke(const std::string& id){
 localOnly();
 auto gone=[&](const FileGrant& g){return id=="all"||g.id==id;};
 for(auto it=items.begin();it!=items.end();)if(!it->second->grant.empty()&&std::any_of(store.grants.begin(),store.grants.end(),[&](const FileGrant& g){return gone(g)&&g.id==it->second->grant;}))it=items.erase(it);else ++it;
 store.grants.erase(std::remove_if(store.grants.begin(),store.grants.end(),gone),store.grants.end());return persist();
}
Json FileAccess::setEnabled(bool enabled,const std::string& code){
 // A remote server's scripts can neither see nor change the player's choices.
 localOnly();
 if(!enabled){
  // Off before anything is saved: a store that cannot be written must not keep it on.
  store.enabled=false;items.clear();
  for(auto& [id,r]:requests)if(r->result.is_null()){stop(*r);r->result=denied("disabled","The player turned file access for other addons off");}
  queue.clear();active=0;Json off={{"enabled",false}};if(!persist())off["notSaved"]=true;return off;
 }
 if(store.enabled)return {{"enabled",true}};
 // Turning it on is the player's decision too, asked natively.
 if(denials[EnableRequester]>=FileDenialLimit)refuse("auto_denied","The player kept file access off three times");
 auto r=std::make_unique<Request>();r->kind=Request::Enable;r->requester=EnableRequester;r->language=language({{"language",code}});
 return {{"request",enqueue(std::move(r))}};
}
}
