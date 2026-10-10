#include "file_access.hpp"
#include "model_notes.hpp"
#include "props/network_path.hpp"
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <bcrypt.h>
#else
#include "posix.hpp"
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <thread>
#endif
#include <algorithm>
#include <cwctype>
namespace mmd {
FileAccessError::FileAccessError(std::string c,const std::string& message):std::runtime_error(message),code(std::move(c)){}
namespace {
[[noreturn]] void refuse(const char* code,const std::string& message){throw FileAccessError(code,message);}
#ifndef _WIN32
// The same rules with POSIX paths: names compare exactly, '/' separates, a symbolic link
// takes the place of a junction, and an open file's real place is /proc/self/fd/<n>.
struct Handle{int fd=-1;~Handle(){if(fd>=0)close(fd);}};
using DWORD=uint32_t;
constexpr DWORD GENERIC_READ=1,FILE_LIST_DIRECTORY=2,FILE_READ_ATTRIBUTES=0,SYNCHRONIZE=0;
bool sameText(std::wstring_view a,std::wstring_view b){return a==b;}
std::wstring trimmed(const fs::path& p){auto s=p.wstring();while(s.size()>1&&s.back()==L'/')s.pop_back();return s;}
std::wstring lower(std::wstring s){for(auto& c:s)c=wchar_t(std::towlower(c));return s;}
std::string randomHex(size_t bytes){std::vector<unsigned char> b(bytes);posix::randomBytes(b.data(),b.size());std::string s;for(auto c:b){s.push_back("0123456789abcdef"[c>>4]);s.push_back("0123456789abcdef"[c&15]);}return s;}
fs::path home(){if(auto h=getenv("HOME");h&&*h=='/')return fs::path(h).lexically_normal();return {};}
// $XDG_<NAME>_HOME (absolute) or its default below the home folder.
fs::path xdgHome(const char* variable,const char* fallback){if(auto v=getenv(variable);v&&*v=='/')return fs::path(v).lexically_normal();auto h=home();return h.empty()?fs::path():h/fallback;}
// The XDG user folders (~/.config/user-dirs.dirs: XDG_DESKTOP_DIR="$HOME/Desktop"...).
std::vector<fs::path> userDirs(){
 std::vector<fs::path> out;auto h=home();if(h.empty())return out;
 std::ifstream f(xdgHome("XDG_CONFIG_HOME",".config")/"user-dirs.dirs");std::string line;
 while(std::getline(f,line)){auto eq=line.find('=');if(line.empty()||line[0]=='#'||eq==std::string::npos)continue;auto v=line.substr(eq+1);
  if(v.size()>=2&&v.front()=='"'&&v.back()=='"')v=v.substr(1,v.size()-2);if(v.starts_with("$HOME"))v=h.string()+v.substr(5);if(!v.empty()&&v[0]=='/')out.push_back(fs::path(v).lexically_normal());}
 for(auto name:{"Desktop","Documents","Downloads","Pictures","Videos","Music","Public","Templates"})out.push_back(h/name);
 return out;
}
fs::path finalPath(int fd){
 std::error_code error;auto real=fs::read_symlink("/proc/self/fd/"+std::to_string(fd),error);
 if(error||real.is_relative())refuse("unreadable","The system cannot tell where this file is");
 return real;
}
void open(Handle& file,const fs::path& path,DWORD,bool reparse){
 file.fd=::open(ioPath(path).c_str(),O_RDONLY|O_CLOEXEC|O_NONBLOCK|(reparse?O_NOFOLLOW:0));
 if(file.fd>=0)return;
 auto error=errno;
 if(error==ELOOP&&reparse)refuse("link","Links and junctions are not followed");
 if(error==ENOENT||error==ENOTDIR||error==ENAMETOOLONG)refuse("not_found","The file or folder does not exist");
 refuse("unreadable","The system does not allow reading this file or folder");
}
// Opens without following a final symbolic link (a link in the middle of the path is
// followed and shows in the final path instead).
ResolvedFile openChecked(Handle& file,const fs::path& path,DWORD access){
 open(file,path,access,true);
 struct stat st{};if(fstat(file.fd,&st)!=0)refuse("unreadable","Cannot read the file's attributes");
 if(!S_ISDIR(st.st_mode)&&!S_ISREG(st.st_mode))refuse("device","Devices, sockets and pipes are not files");
 ResolvedFile r;r.attributes=uint32_t(st.st_mode);r.folder=S_ISDIR(st.st_mode);r.size=r.folder?0:uint64_t(st.st_size);r.path=finalPath(file.fd);return r;
}
// The parts of a path below its root, separated by slashes; one trailing separator is fine.
void checkParts(std::wstring_view rest){
 size_t depth=0;
 for(size_t start=0;start<rest.size();){
  auto end=rest.find(L'/',start);if(end==rest.npos)end=rest.size();auto part=rest.substr(start,end-start);
  if(part.empty())refuse("invalid_path","The path has an empty part");
  if(part==L"."||part==L"..")refuse("parent","Paths may not contain . or .. parts");
  if(++depth>64)refuse("invalid_path","The path is too deep");
  start=end+1;
 }
}
std::wstring pathText(std::string_view s,size_t limit){
 if(s.size()>limit)refuse("invalid_path","The path is too long");
 for(unsigned char c:s)if(c<0x20||c==0x7F)refuse("invalid_path","The path contains control characters");
 std::wstring w;try{w=wide(s);}catch(...){refuse("invalid_path","The path is not valid UTF-8");}
 return w;
}
#else
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
#endif
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
#ifdef _WIN32
bool plainDirectory(const fs::path& path){auto a=GetFileAttributesW(ioPath(path).c_str());return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY)&&!(a&FILE_ATTRIBUTE_REPARSE_POINT);}
#else
bool plainDirectory(const fs::path& path){return posix::plainDirectory(path);}
#endif
void removeFolder(const fs::path& dir){std::error_code error;if(!dir.empty()&&plainDirectory(dir))fs::remove_all(ioPath(dir),error);}
int64_t now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
#ifdef _WIN32
int64_t unixTime(LARGE_INTEGER t){return t.QuadPart/10000000-11644473600ll;}
#endif
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
#ifndef _WIN32
FilePolicy FilePolicy::system(const fs::path& gameRoot){
 FilePolicy p;
 // Folders as their final paths, so a linked Documents or ~/.local still matches.
 auto canonical=[](const fs::path& path)->fs::path{if(path.empty())return {};std::error_code error;auto real=fs::canonical(path,error);return error?path.lexically_normal():real;};
 auto deny=[&](const fs::path& path){if(!path.empty())p.denied.push_back(canonical(path));};
 auto h=home(),config=xdgHome("XDG_CONFIG_HOME",".config"),data=xdgHome("XDG_DATA_HOME",".local/share");
 // The system: configuration, kernel interfaces, devices and other users' homes.
 for(auto root:{"/etc","/proc","/sys","/dev","/run","/boot","/root","/var/lib","/var/log","/var/spool","/lost+found"})deny(root);
 if(!h.empty()){
  for(auto keys:{".ssh",".gnupg",".aws",".azure",".kube",".docker",".password-store",".pki",".mozilla",".thunderbird",".electrum/wallets",".bitcoin/wallets",".ethereum/keystore",".local/share/keyrings"})deny(h/keys);
  // Steam's login tokens, wherever its folder is (classic, Flatpak, snap).
  for(auto steam:{".steam/steam/config",".steam/root/config",".local/share/Steam/config",".var/app/com.valvesoftware.Steam/.local/share/Steam/config",".var/app/com.valvesoftware.Steam/data/Steam/config","snap/steam/common/.local/share/Steam/config"})deny(h/steam);
 }
 if(!config.empty()){
  for(auto app:{"gh","google-chrome","google-chrome-beta","chromium","BraveSoftware","microsoft-edge","vivaldi","yandex-browser","opera","discord/Local Storage","discordcanary/Local Storage","discordptb/Local Storage","Signal","filezilla","Exodus","keepassxc","Bitwarden"})deny(config/app);
 }
 if(!data.empty()){for(auto app:{"keyrings","TelegramDesktop/tdata","ModelHotloader"})deny(data/app);}
 if(!gameRoot.empty())deny(gameRoot/L"garrysmod"/L"cfg");
 // Plain-text tokens (git, npm, netrc, PyPI), Steam's login files and wallets, anywhere.
 p.deniedNames={L"ssfn",L"mmdhl-fa-",L".git-credentials",L".npmrc",L".netrc",L"_netrc",L".pypirc",L"wallet.dat",L".bash_history",L".zsh_history"};
 // Folders too broad to allow for good: the roots, the home folder and the user folders.
 for(auto root:{"/home","/usr","/opt","/mnt","/media","/run/media","/srv","/tmp","/var"})p.broad.push_back(canonical(root));
 if(!h.empty())for(auto folder:{"",".local",".config",".cache",".var","snap"})p.broad.push_back(canonical(folder[0]?h/folder:h));
 if(!data.empty())p.broad.push_back(canonical(data));
 for(auto& folder:userDirs())p.broad.push_back(canonical(folder));
 return p;
}
#else
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
#endif
namespace {
// X:\... only, checked as text.
#ifndef _WIN32
// Absolute /... paths only, checked as text.
fs::path drivePath(std::string_view s){
 if(s.empty())refuse("invalid_path","The path is empty");
 auto w=pathText(s,2048);
 if(props::networkPath(s))refuse("network","Paths to other computers and devices are never read");
 if(w.empty()||w[0]!=L'/')refuse("relative","Only absolute paths (starting with /) can be requested");
 checkParts(std::wstring_view(w).substr(1));
 if(w.size()>1&&w.back()==L'/')w.pop_back();
 return fs::path(w);
}
// The file system holding path as the mount table names it (no access to the path itself:
// opening a network share contacts its server).
const char* driveProblem(const fs::path& path,bool requireLocal){
 if(!requireLocal)return nullptr;
 std::ifstream mounts("/proc/self/mountinfo");std::string line,best,type;
 auto text=path.string();
 while(std::getline(mounts,line)){
  // id parent major:minor root mountpoint options [tags] - fstype source superoptions
  std::istringstream in(line);std::string id,parent,dev,root,point;in>>id>>parent>>dev>>root>>point;
  auto dash=line.find(" - ");if(dash==std::string::npos)continue;std::istringstream rest(line.substr(dash+3));std::string fstype;rest>>fstype;
  bool covers=point=="/"||text==point||(text.starts_with(point)&&text[point.size()]=='/');
  if(covers&&point.size()>=best.size()){best=point;type=fstype;}
 }
 static const char* remote[]={"nfs","nfs4","cifs","smb3","smbfs","ncpfs","afs","9p","ceph","glusterfs","fuse.sshfs","fuse.rclone","fuse.s3fs","fuse.gvfsd-fuse","davfs","fuse.davfs2"};
 for(auto r:remote)if(type==r)return "remote_drive";
 return nullptr;
}
#else
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
#endif
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
#ifndef _WIN32
 static const std::wstring profile=[]{auto p=home();std::error_code error;auto real=fs::canonical(p,error);return trimmed(error?p:real);}();
#else
 static const std::wstring profile=[]{auto p=knownFolder(FOLDERID_Profile);try{return trimmed(resolveFile(p).path);}catch(...){return trimmed(p);}}();
#endif
 auto text=trimmed(path);
 if(!profile.empty()&&insideFolder(path,profile))return utf8(L"~"+text.substr(profile.size()));
 return utf8(text);
}
#ifndef _WIN32
// $XDG_DATA_HOME/ModelHotloader/file-access.json (~/.local/share/...): outside the game folders.
fs::path fileAccessStore(){auto data=xdgHome("XDG_DATA_HOME",".local/share");if(data.empty())throw std::runtime_error("No local application data folder");return data/"ModelHotloader"/"file-access.json";}
std::optional<std::string> readGrantStore(const fs::path& file){
 FILE* f=fopen(ioPath(file).c_str(),"rb");
 if(!f){if(errno==ENOENT||errno==ENOTDIR)return std::nullopt;throw std::runtime_error("The file access store cannot be read now");}
 struct Closer{FILE* f;~Closer(){fclose(f);}} closer{f};
 struct stat st{};if(fstat(fileno(f),&st)!=0)throw std::runtime_error("The file access store cannot be read now");
 if(st.st_size>(1<<20))return std::string();  // far larger than any store: damaged
 std::string text(size_t(st.st_size),'\0');
 if(!text.empty()&&fread(text.data(),1,text.size(),f)!=text.size())throw std::runtime_error("The file access store cannot be read now");
 return text;
}
#else
fs::path fileAccessStore(){auto local=knownFolder(FOLDERID_LocalAppData);if(local.empty())throw std::runtime_error("No local application data folder");return local/L"ModelHotloader"/L"file-access.json";}
std::optional<std::string> readGrantStore(const fs::path& file){
 Handle h;h.h=CreateFileW(ioPath(file).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(h.h==INVALID_HANDLE_VALUE){auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)return std::nullopt;throw std::runtime_error("The file access store cannot be read now");}
 LARGE_INTEGER size{};if(!GetFileSizeEx(h.h,&size))throw std::runtime_error("The file access store cannot be read now");
 if(size.QuadPart>(1<<20))return std::string();  // far larger than any store: damaged
 std::string text(size_t(size.QuadPart),'\0');DWORD received=0;
 if(!text.empty()&&(!ReadFile(h.h,text.data(),DWORD(text.size()),&received,nullptr)||received!=text.size()))throw std::runtime_error("The file access store cannot be read now");
 return text;
}
#endif
bool FileGrantStore::parse(std::string_view text,const FilePolicy& policy){
 enabled=true;grants.clear();
 auto j=Json::parse(text.begin(),text.end(),nullptr,false);
 if(j.is_discarded()||!j.is_object()||!j.contains("schema")||j["schema"]!=1)return false;
 if(j.contains("enabled")&&j["enabled"].is_boolean())enabled=j["enabled"].get<bool>();
 if(!j.contains("grants")||!j["grants"].is_array())return true;
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
 return true;
}
std::string FileGrantStore::save() const {
 Json list=Json::array();for(auto& g:grants)list.push_back({{"id",g.id},{"requester",g.requester},{"folder",utf8(g.folder.wstring())},{"created",g.created},{"used",g.used}});
 auto text=Json({{"schema",1},{"enabled",enabled},{"grants",list}}).dump(2);
 fs::create_directories(ioPath(file.parent_path()));writeAtomic(file,std::span(reinterpret_cast<const unsigned char*>(text.data()),text.size()));return text;
}
const FileGrant* FileGrantStore::covering(const std::string& requester,const fs::path& path) const {
 for(auto& g:grants)if(g.requester==requester&&insideFolder(path,g.folder))return &g;
 return nullptr;
}
struct FileAccess::Item {std::string name,requester,grant;fs::path root;bool folder=false;uint64_t size=0;};
struct FileAccess::Request {
 enum Kind{Pick,Path,Enable} kind=Path;
 std::string requester,script,purpose,title,language;Json filters=Json::array(),result;bool multiple=false,folder=false;
#ifdef _WIN32
 fs::path path,dir,rememberFolder;HANDLE process=nullptr,group=nullptr;
#else
 fs::path path,dir,rememberFolder;posix::Child child;
#endif
 std::optional<ResolvedFile> target;std::string problem;bool canRemember=false;
};
// Why a read or listing was told to stop (0: it runs on).
enum TaskStop:int {TaskRunning,TaskCanceled,TaskDisabled,TaskRevoked,TaskClosing};
struct FileTask {
 bool listing=false;
 std::shared_ptr<const FileAccess::Item> item;  // what it reads: revoking its remembered folder stops it
 std::atomic<int> stop{TaskRunning};
 std::atomic<bool> done{false};         // its thread finished: the fields below are complete
 uint64_t finished=0;                   // GetTickCount64 when it did
#ifdef _WIN32
 HANDLE thread=nullptr;                 // to cancel the file operation it waits in
#endif
 FileReadResult read;Json list;std::exception_ptr error;
 FileTask()=default;FileTask(const FileTask&)=delete;FileTask& operator=(const FileTask&)=delete;
#ifdef _WIN32
 ~FileTask(){if(thread)CloseHandle(thread);}
#endif
};
namespace {
struct ReadOptions {fs::path relative;uint64_t offset=0,length=FileReadDefault;bool text=false,hidden=false;};
// A read or listing told to stop gives up at its next step; the step it waits in is canceled.
void checkStop(const std::atomic<int>& stop){if(stop.load())refuse("canceled","The read was stopped");}
// Opens root\relative and proves it is still the thing the player allowed: no final
// link, the final path inside the granted root, not denied, not hidden unless asked.
ResolvedFile openItem(Handle& file,const FileAccess::Item& item,const fs::path& relative,bool hidden,const FilePolicy& policy,DWORD access){
 if(!relative.empty()&&!item.folder)refuse("invalid_options","Only folders have paths inside them");
 auto target=relative.empty()?item.root:item.root/relative;
#ifdef _WIN32
 if(!hidden&&!relative.empty()){auto at=item.root;for(auto& part:relative){at/=part;auto a=GetFileAttributesW(ioPath(at).c_str());if(a!=INVALID_FILE_ATTRIBUTES&&(a&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM)))refuse("hidden","Hidden and system files are read only when asked for");}}
#else
 // Hidden on Linux: a name that starts with a dot.
 if(!hidden&&!relative.empty())for(auto& part:relative){auto name=part.wstring();if(!name.empty()&&name[0]==L'.')refuse("hidden","Hidden and system files are read only when asked for");}
#endif
 auto r=openChecked(file,target,access);
 if(relative.empty()?!sameText(trimmed(r.path),trimmed(item.root)):!insideFolder(r.path,item.root))refuse("outside","The file now leads outside what the player allowed");
 if(policy.deniedPath(r.path))refuse("denied_location","Model Hotloader never lets addons read this location");
 return r;
}
FileReadResult readItem(const FileAccess::Item& item,const ReadOptions& o,const FilePolicy& policy,const std::atomic<int>& stop){
 checkStop(stop);Handle file;auto r=openItem(file,item,o.relative,o.hidden,policy,GENERIC_READ);
 if(r.folder)refuse("not_a_file","This is a folder; list it or read a file inside it");
 FileReadResult out;out.info={{"name",utf8((o.relative.empty()?item.root:o.relative).filename().wstring())},{"size",r.size},{"offset",o.offset}};
 uint64_t end=std::min({r.size,o.offset+o.length,FileOffsetMaximum});
 if(o.text&&(o.offset||r.size>o.length))refuse("too_large","Text is read whole; this file is larger than the limit");
 if(o.offset<end){
#ifdef _WIN32
  LARGE_INTEGER at;at.QuadPart=LONGLONG(o.offset);if(!SetFilePointerEx(file.h,at,nullptr,FILE_BEGIN))refuse("unreadable","Cannot seek in the file");
  out.data.resize(size_t(end-o.offset));size_t cursor=0;
  // In 1 MiB steps: a slow drive cannot hold a read the player stopped for long.
  while(cursor<out.data.size()){checkStop(stop);DWORD received=0;if(!ReadFile(file.h,out.data.data()+cursor,DWORD(std::min<size_t>(out.data.size()-cursor,1u<<20)),&received,nullptr))refuse("unreadable","The file could not be read");if(!received)break;cursor+=received;}
#else
  out.data.resize(size_t(end-o.offset));size_t cursor=0;
  // In 1 MiB steps: a slow drive cannot hold a read the player stopped for long.
  while(cursor<out.data.size()){checkStop(stop);auto received=pread(file.fd,out.data.data()+cursor,std::min<size_t>(out.data.size()-cursor,1u<<20),off_t(o.offset+cursor));if(received<0){if(errno==EINTR)continue;refuse("unreadable","The file could not be read");}if(!received)break;cursor+=size_t(received);}
#endif
  out.data.resize(cursor);
 }
 out.info["read"]=out.data.size();out.info["eof"]=o.offset+out.data.size()>=r.size;
 if(!out.info["eof"].get<bool>()&&o.offset+out.data.size()>=FileOffsetMaximum)out.info["limited"]=true;
 if(o.text){std::string encoding;out.data=decodeText(std::span(reinterpret_cast<const unsigned char*>(out.data.data()),out.data.size()),encoding);out.info["encoding"]=encoding;}
 return out;
}
// One level of a folder, read through the opened handle (no second lookup by name).
#ifndef _WIN32
// One level of a folder, read through the opened descriptor (no second lookup by name).
Json listItem(const FileAccess::Item& item,const fs::path& relative,bool hidden,const FilePolicy& policy,const std::atomic<int>& stop){
 checkStop(stop);Handle folder;auto r=openItem(folder,item,relative,hidden,policy,FILE_LIST_DIRECTORY);
 if(!r.folder)refuse("not_a_folder","This is a file; read it instead");
 int fd=dup(folder.fd);DIR* dir=fd>=0?fdopendir(fd):nullptr;if(!dir){if(fd>=0)close(fd);refuse("unreadable","The folder could not be listed");}
 struct Closer{DIR* d;~Closer(){closedir(d);}} closer{dir};
 Json entries=Json::array();bool truncated=false;
 while(auto e=readdir(dir)){
  checkStop(stop);std::string name=e->d_name;if(name=="."||name=="..")continue;
  struct stat st{};if(fstatat(dirfd(dir),e->d_name,&st,AT_SYMLINK_NOFOLLOW)!=0)continue;
  // Links are left out (they are never followed), as are devices, pipes and sockets.
  if(!S_ISDIR(st.st_mode)&&!S_ISREG(st.st_mode))continue;
  std::wstring wname;try{wname=wide(name);}catch(...){continue;}
  if((!hidden&&name[0]=='.')||policy.deniedPath(r.path/wname))continue;
  if(entries.size()>=FileListMaximum){truncated=true;break;}
  bool isFolder=S_ISDIR(st.st_mode);Json entry={{"name",name},{"folder",isFolder},{"modified",int64_t(st.st_mtime)}};
  if(!isFolder)entry["size"]=uint64_t(st.st_size);entries.push_back(entry);
 }
 std::sort(entries.begin(),entries.end(),[](const Json& a,const Json& b){if(a["folder"]!=b["folder"])return a["folder"].get<bool>();return lower(wide(a["name"].get<std::string>()))<lower(wide(b["name"].get<std::string>()));});
 return {{"entries",entries},{"truncated",truncated}};
}
#else
Json listItem(const FileAccess::Item& item,const fs::path& relative,bool hidden,const FilePolicy& policy,const std::atomic<int>& stop){
 checkStop(stop);Handle folder;auto r=openItem(folder,item,relative,hidden,policy,FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|SYNCHRONIZE);
 if(!r.folder)refuse("not_a_folder","This is a file; read it instead");
 Json entries=Json::array();bool truncated=false;std::vector<unsigned char> buffer(64u<<10);auto kind=FileIdBothDirectoryRestartInfo;DWORD error=ERROR_SUCCESS;
 while(!truncated){
  checkStop(stop);
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
#endif
// The dialog's request and answer go through a folder only this process creates, in
// the user's temporary folder: Lua can write anything under garrysmod/data.
fs::path privateFolder(const fs::path& temp){
 if(temp.empty())refuse("dialog_failed","No temporary folder");
#ifdef _WIN32
 for(int attempt=0;attempt<8;attempt++){auto dir=temp/wide("mmdhl-fa-"+randomHex(16));if(CreateDirectoryW(ioPath(dir).c_str(),nullptr))return dir;}
#else
 for(int attempt=0;attempt<8;attempt++){auto dir=temp/wide("mmdhl-fa-"+randomHex(16));if(mkdir(ioPath(dir).c_str(),0700)==0)return dir;}
#endif
 refuse("dialog_failed","Cannot create a private folder for the dialog");
}
#ifndef _WIN32
void launchDialog(FileAccess::Request& r,const fs::path& worker,const wchar_t* mode){
 if(!regularFile(worker))refuse("worker_missing","The Model Hotloader worker is missing");
 try{r.child=posix::spawn(worker,{utf8(mode),r.dir.string()},worker.parent_path());}catch(const std::exception&){refuse("dialog_failed","Cannot start the dialog");}
}
#else
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
#endif
// Reads and listings run on threads of their own, never joined: the client module goes away
// at every map change, and a read waiting on a network share that stopped answering would
// hold the game until Windows gives up. Each thread keeps this library loaded until it ends.
#ifndef _WIN32
// The runtime is never unloaded (-z nodelete), so a detached thread may outlive the module.
std::atomic<size_t> liveTasks{0};
std::shared_ptr<FileTask> launch(bool listing,std::shared_ptr<const FileAccess::Item> item,std::function<void(FileTask&)> body){
 auto task=std::make_shared<FileTask>();task->listing=listing;task->item=std::move(item);
 liveTasks++;
 try{std::thread([task,body=std::move(body)]{try{body(*task);}catch(...){task->error=std::current_exception();}task->finished=posix::tickMs();task->done=true;liveTasks--;}).detach();}
 catch(...){liveTasks--;throw std::runtime_error("Cannot start the read");}
 return task;
}
// Tells a read or listing to stop; it gives up at its next step (a blocked read cannot be canceled).
void halt(FileTask& t,TaskStop why){int running=TaskRunning;t.stop.compare_exchange_strong(running,why);}
#else
std::atomic<size_t> liveTasks{0};
struct TaskStart {std::shared_ptr<FileTask> task;std::function<void(FileTask&)> body;HMODULE library=nullptr;};
DWORD WINAPI taskThread(void* data){
 HMODULE library=nullptr;
 {std::unique_ptr<TaskStart> start(static_cast<TaskStart*>(data));library=start->library;auto& t=*start->task;
  try{start->body(t);}catch(...){t.error=std::current_exception();}
  t.finished=GetTickCount64();t.done=true;}
 liveTasks--;FreeLibraryAndExitThread(library,0);
}
std::shared_ptr<FileTask> launch(bool listing,std::shared_ptr<const FileAccess::Item> item,std::function<void(FileTask&)> body){
 auto task=std::make_shared<FileTask>();task->listing=listing;task->item=std::move(item);
 auto start=std::make_unique<TaskStart>(TaskStart{task,std::move(body)});
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&taskThread),&start->library))throw std::runtime_error("Cannot start the read");
 liveTasks++;HANDLE thread=CreateThread(nullptr,0,taskThread,start.get(),0,nullptr);
 if(!thread){liveTasks--;FreeLibrary(start->library);throw std::runtime_error("Cannot start the read");}
 start.release();task->thread=thread;return task;
}
// Tells a read or listing to stop and cancels the file operation its thread waits in (an
// open or a read of a slow or unreachable drive). Its result is never delivered.
void halt(FileTask& t,TaskStop why){int running=TaskRunning;t.stop.compare_exchange_strong(running,why);if(!t.done.load())CancelSynchronousIo(t.thread);}
#endif
// Game processes that share the store (two installs, -multirun) change it one at a time.
#ifndef _WIN32
// An exclusive lock on <store>.lock (flock), held two seconds at most.
struct StoreLock {
 int fd=-1;
 StoreLock(){
  auto file=fileAccessStore();file+=".lock";std::error_code error;fs::create_directories(file.parent_path(),error);
  fd=::open(file.c_str(),O_RDWR|O_CREAT|O_CLOEXEC,0600);if(fd<0)throw std::runtime_error("No store lock");
  for(int attempt=0;flock(fd,LOCK_EX|LOCK_NB)!=0;attempt++){if(attempt>=200){close(fd);throw std::runtime_error("The store is busy");}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 }
 ~StoreLock(){flock(fd,LOCK_UN);close(fd);}
 StoreLock(const StoreLock&)=delete;StoreLock& operator=(const StoreLock&)=delete;
};
#else
struct StoreLock {
 HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\ModelHotloader.FileAccessStore");
 StoreLock(){if(!mutex)throw std::runtime_error("No store lock");auto w=WaitForSingleObject(mutex,2000);if(w!=WAIT_OBJECT_0&&w!=WAIT_ABANDONED){CloseHandle(mutex);throw std::runtime_error("The store is busy");}}
 ~StoreLock(){ReleaseMutex(mutex);CloseHandle(mutex);}
 StoreLock(const StoreLock&)=delete;StoreLock& operator=(const StoreLock&)=delete;
};
#endif
}
size_t fileAccessThreads(){return liveTasks.load();}
FileAccess::FileAccess(FileAccessConfig c):config(std::move(c)),policy(std::make_shared<FilePolicy>(config.policy)){
#ifdef _WIN32
 if(config.temp.empty()){wchar_t temp[MAX_PATH+1]{};if(GetTempPathW(MAX_PATH+1,temp))config.temp=temp;}
#else
 if(config.temp.empty())config.temp=posix::tempDirectory();
#endif
 // A missing, unreadable or damaged store means nothing remembered.
 store.file=config.store;try{storeText=readGrantStore(store.file);storeRead=true;}catch(...){}
 if(storeText)store.parse(*storeText,*policy);
 // Private folders a crashed game left behind.
 std::error_code error;
 for(auto it=fs::directory_iterator(config.temp,error);!error&&it!=fs::directory_iterator();it.increment(error)){
  auto name=it->path().filename().wstring();std::error_code e;
  if(name.starts_with(L"mmdhl-fa-")&&plainDirectory(it->path())&&fs::last_write_time(it->path(),e)<fs::file_time_type::clock::now()-std::chrono::hours(24))removeFolder(it->path());
 }
}
FileAccess::~FileAccess(){
 for(auto& [id,r]:requests)stop(*r);
 // Reads and listings are told to stop and left to end on their own threads.
 for(auto& [id,t]:tasks)halt(*t,TaskClosing);for(auto& t:dropped)halt(*t,TaskClosing);
}
// The player's answer counts at once; saving it is best effort, so a locked or read-only
// store cannot turn a decision into a failure. false: not saved yet (the change applies
// here until the map changes, and is written with the next change that can be saved).
// keep=false: a change worth no retry (when a remembered folder was last used).
bool FileAccess::persist(const StoreChange& change,bool keep){
 change(store);if(keep)unsaved.push_back(change);
 try{
  StoreLock lock;auto text=readGrantStore(store.file);
  FileGrantStore merged;merged.file=store.file;
  // Whatever another game process saved meanwhile stays; a damaged file is replaced by what is known here.
  if(text&&!merged.parse(*text,*policy))merged=store;
  else{for(auto& c:unsaved)c(merged);if(!keep)change(merged);}
  storeText=merged.save();storeRead=true;unsaved.clear();adopt(std::move(merged));return true;
 }catch(...){}
 // Not saved: the change applies here all the same.
 prune();return false;
}
// What another game process saved counts here too: a folder revoked there is no longer
// remembered here, and file access turned off there is off here. A store file that cannot
// be read now, or is damaged, changes nothing. Cheap when nothing changed (one small read).
void FileAccess::refresh(){
 std::optional<std::string> text;try{text=readGrantStore(store.file);}catch(...){return;}
 if(storeRead&&text==storeText)return;
 storeRead=true;storeText=text;
 FileGrantStore fresh;fresh.file=store.file;if(text&&!fresh.parse(*text,*policy))return;
 for(auto& c:unsaved)c(fresh);
 adopt(std::move(fresh));
}
void FileAccess::adopt(FileGrantStore fresh){
 if(store.enabled&&!fresh.enabled)turnedOff();
 store=std::move(fresh);prune();
}
// Off: nothing granted stays readable, no read or listing delivers, no dialog waits.
void FileAccess::turnedOff(){
 items.clear();for(auto& [id,t]:tasks)halt(*t,TaskDisabled);
 for(auto& [id,r]:requests)if(r->result.is_null()){stop(*r);r->result=denied("disabled","The player turned file access for other addons off");}
 queue.clear();active=0;
}
// What a remembered folder answered lasts while a remembered folder of the same addon still
// covers it. Revoked here or in another game process, the items go and their reads and
// listings no longer deliver: also the ones a smaller folder answered before a larger one
// replaced it (that keeps them while it stays, in every process). Allowed once: until the map changes.
void FileAccess::prune(){
 auto ended=[&](const Item& i){return !i.grant.empty()&&!store.covering(i.requester,i.root);};
 std::erase_if(items,[&](const auto& entry){return ended(*entry.second);});
 for(auto& [id,t]:tasks)if(t->item&&ended(*t->item))halt(*t,TaskRevoked);
}
void FileAccess::stop(Request& r){
 // The folder can go once the worker is gone (it may still hold request.json open).
#ifdef _WIN32
 if(r.group){TerminateJobObject(r.group,1);if(r.process)WaitForSingleObject(r.process,2000);CloseHandle(r.group);r.group=nullptr;}
 if(r.process){CloseHandle(r.process);r.process=nullptr;}
#else
 r.child.kill();r.child=posix::Child{};
#endif
 removeFolder(r.dir);r.dir.clear();
}
Json FileAccess::info(){
 refresh();
 std::string reason=!localServerRealm()?"no_local_server":!store.enabled?"disabled":!regularFile(config.worker)?"worker_missing":"ok";
 return {{"version",1},{"available",reason=="ok"},{"reason",reason},{"enabled",store.enabled},{"local",localServerRealm()},{"maxRead",FileReadMaximum},{"defaultRead",FileReadDefault},{"maxOffset",FileOffsetMaximum},{"maxList",FileListMaximum},{"dialog",active!=0},{"queued",queue.size()}};
}
// The enable confirmation counts its refusals under a key no cleaned label can be.
static const std::string EnableRequester="\x01enable";
static void localOnly(){if(!localServerRealm())refuse("unavailable_remote","File access works only in single player and on a server this game hosts");}
// worker=false: a request only a remembered folder may answer, which needs no window.
void FileAccess::available(const std::string& requester,bool worker){
 localOnly();refresh();
 if(!store.enabled)refuse("disabled","The player turned file access for other addons off");
 if(worker&&!regularFile(config.worker))refuse("worker_missing","The Model Hotloader worker is missing");
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
 // Set by installation.lua's guard while its check does not allow mmdhl_worker.exe, which shows the windows.
 bool noDialog=flag(o,"noDialog");
 if(!o.contains("path")||!o["path"].is_string())refuse("invalid_options","Give the path to request");
 available(r->requester,!noDialog);r->path=drivePath(o["path"].get<std::string>());
 // Only the text is refused at once. A missing or network drive or a never-readable place
 // is told in the window like a missing file, with nothing opened: an immediate answer
 // would let a script learn silently which drives or user folders this computer has.
 if(auto problem=driveProblem(r->path,true))r->problem=problem;else if(policy->deniedPath(r->path))r->problem="denied_location";
 // A remembered folder answers without asking, once the path proves to lead inside it.
 if(r->problem.empty()&&store.covering(r->requester,r->path))try{
  auto target=resolveFile(r->path);
  if(target.folder==r->folder&&!policy->deniedPath(target.path))if(auto g=store.covering(r->requester,target.path))r->result=grant(*r,target,g->id);
 }catch(const FileAccessError& e){if(e.code=="disabled")throw;}  // turned off meanwhile in another game process: no window either
 // Without the worker nothing else can answer. The same refusal whatever the path is, so it tells a script nothing about it.
 if(noDialog&&r->result.is_null())refuse("worker_unavailable","Model Hotloader's installation check does not allow the worker that shows file access windows");
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
#ifdef _WIN32
  auto& r=*requests.at(active);if(WaitForSingleObject(r.process,0)==WAIT_TIMEOUT)return;
#else
  auto& r=*requests.at(active);if(r.child.running())return;
#endif
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
  request.update({{"kind","consent"},{"path",utf8((r.target?r.target->path:r.path).wstring())},{"folder",r.folder},{"problem",r.problem},{"remember",r.canRemember},{"rememberFolder",utf8(r.rememberFolder.wstring())}});
  // A size only for a file that was opened: a refused or missing one shows no made-up 0 bytes.
  if(r.target)request["size"]=r.target->size;
 }
 else if(r.kind==Request::Pick)request.update({{"kind","pick"},{"title",r.title},{"filters",r.filters},{"multiple",r.multiple},{"folder",r.folder}});
 else request["kind"]="enable";
 r.dir=privateFolder(config.temp);writeJson(r.dir/L"request.json",request);
 launchDialog(r,config.worker,r.kind==Request::Pick?L"--fa-pick":L"--fa-consent");
}
Json FileAccess::grant(Request& r,const ResolvedFile& target,std::string grantId){
 if(items.size()>=1024)refuse("too_many_items","Release files the addon no longer needs");
 // When the remembered folder was last used: best effort, never written back over a newer store.
 if(auto at=now();!grantId.empty())if(auto g=std::find_if(store.grants.begin(),store.grants.end(),[&](const FileGrant& f){return f.id==grantId;});g!=store.grants.end()&&g->used!=at)
  persist([grantId,at](FileGrantStore& s){for(auto& f:s.grants)if(f.id==grantId)f.used=at;},false);
 // Saving read the newest store: another game process may have turned file access off, or
 // revoked the folder (an item it answered lasts only while a remembered folder covers it).
 if(!store.enabled)refuse("disabled","The player turned file access for other addons off");
 if(!grantId.empty()&&!store.covering(r.requester,target.path))refuse("released","The player revoked this folder; ask for it again");
 auto entry=std::make_shared<Item>();entry->root=target.path;entry->folder=target.folder;entry->size=target.size;entry->requester=r.requester;entry->grant=grantId;
 entry->name=utf8(target.path.filename().empty()?target.path.wstring():target.path.filename().wstring());
 auto handle=randomHex(16);items[handle]=entry;
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
  bool saved=persist([](FileGrantStore& s){s.enabled=true;});r.result={{"state","granted"},{"enabled",true},{"changed",true}};if(!saved)r.result["notSaved"]=true;return;
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
  FileGrant g;g.id=randomHex(8);g.requester=r.requester;g.folder=r.rememberFolder;g.created=g.used=now();grantId=g.id;
  saved=persist([g](FileGrantStore& s){
   std::erase_if(s.grants,[&](const FileGrant& o){return o.requester==g.requester&&insideFolder(o.folder,g.folder);});
   if(s.grants.size()>=256)s.grants.erase(s.grants.begin());
   s.grants.push_back(g);});
 }
 denials.erase(r.requester);
 r.result=grant(r,*r.target,grantId);if(!grantId.empty())r.result["changed"]=true;if(!saved)r.result["notSaved"]=true;
}
// Polls read the store again too (refresh): another game process may have turned file access
// off or revoked a folder since, and nothing it ended is handed out here.
Json FileAccess::poll(uint64_t id){
 refresh();pump();auto it=requests.find(id);if(it==requests.end())refuse("unknown_request","Unknown file request");
 auto& r=*it->second;
 if(r.result.is_null()){size_t position=active?1:0;for(auto q:queue){if(q==id)break;position++;}return {{"state","pending"},{"dialog",active==id},{"position",active==id?0:position}};}
 auto result=r.result;requests.erase(it);return result;
}
void FileAccess::cancel(uint64_t id){
 if(auto it=requests.find(id);it!=requests.end()){
  if(active==id){stop(*it->second);active=0;}
  queue.erase(std::remove(queue.begin(),queue.end(),id),queue.end());requests.erase(it);pump();return;
 }
 if(auto it=tasks.find(id);it!=tasks.end()){halt(*it->second,TaskCanceled);if(!it->second->done.load())dropped.push_back(it->second);tasks.erase(it);}
}
// The slots: reads and listings still running (stopped ones until their threads end) and
// results waiting to be collected. A result nobody collects within keepResultsMs is dropped,
// so a script that stops polling (or forgot its ids on a Lua refresh) cannot hold them for good.
size_t FileAccess::running(){
 std::erase_if(dropped,[](const std::shared_ptr<FileTask>& t){return t->done.load();});
 #ifdef _WIN32
 auto now=GetTickCount64();
 #else
 auto now=posix::tickMs();
 #endif
 std::erase_if(tasks,[&](const auto& entry){return entry.second->done.load()&&now-entry.second->finished>=config.keepResultsMs;});
 return tasks.size()+dropped.size();
}
// The finished (or stopped) task a poll asks for; nullptr while it runs.
static std::shared_ptr<FileTask> collect(std::map<uint64_t,std::shared_ptr<FileTask>>& tasks,std::vector<std::shared_ptr<FileTask>>& dropped,uint64_t id,bool listing){
 auto it=tasks.find(id);if(it==tasks.end()||it->second->listing!=listing)refuse("unknown_request",listing?"Unknown listing":"Unknown read");
 auto task=it->second;
 // Stopped by the player: what it read, finished or not, is never handed out.
 if(auto why=task->stop.load()){tasks.erase(it);if(!task->done.load())dropped.push_back(task);
  if(why==TaskDisabled)refuse("disabled","The player turned file access for other addons off");
  refuse("released","The player revoked the folder this file is in; ask for it again");}
 if(!task->done.load())return nullptr;
 tasks.erase(it);if(task->error)std::rethrow_exception(task->error);return task;
}
std::shared_ptr<const FileAccess::Item> FileAccess::item(const std::string& handle) const {
 auto it=items.find(handle);if(it==items.end())refuse("released","This file was released or the map changed; ask for it again");return it->second;
}
uint64_t FileAccess::read(const std::string& handle,const Json& o){
 refresh();if(!localServerRealm()||!store.enabled)refuse(!store.enabled?"disabled":"unavailable_remote","File access is not available");
 auto entry=item(handle);if(!o.is_object()&&!o.is_null())refuse("invalid_options","Read options must be an object");
 Json options=o.is_object()?o:Json::object();ReadOptions r;
 auto mode=options.value("mode",std::string("binary"));if(mode!="binary"&&mode!="text")refuse("invalid_options","mode is binary or text");
 r.text=mode=="text";r.hidden=flag(options,"hidden");r.offset=count(options,"offset",0,0,FileOffsetMaximum);r.length=count(options,"length",FileReadDefault,1,FileReadMaximum);
 if(options.contains("relative")&&!options["relative"].is_null()){if(!options["relative"].is_string())refuse("invalid_options","relative must be text");r.relative=checkRelativePath(options["relative"].get<std::string>());}
 if(!r.relative.empty()&&!entry->folder)refuse("invalid_options","Only folders have paths inside them");
 if(running()>=8)refuse("busy","Too many reads are running");
 auto rules=policy;auto task=launch(false,entry,[entry,r,rules](FileTask& t){t.read=readItem(*entry,r,*rules,t.stop);});
 auto id=sequence++;tasks.emplace(id,std::move(task));return id;
}
std::optional<FileReadResult> FileAccess::pollRead(uint64_t id){
 refresh();auto task=collect(tasks,dropped,id,false);if(!task)return std::nullopt;return std::move(task->read);
}
uint64_t FileAccess::list(const std::string& handle,const Json& o){
 refresh();if(!localServerRealm()||!store.enabled)refuse(!store.enabled?"disabled":"unavailable_remote","File access is not available");
 auto entry=item(handle);if(!o.is_object()&&!o.is_null())refuse("invalid_options","List options must be an object");
 Json options=o.is_object()?o:Json::object();fs::path relative;bool hidden=flag(options,"hidden");
 if(options.contains("relative")&&!options["relative"].is_null()){if(!options["relative"].is_string())refuse("invalid_options","relative must be text");relative=checkRelativePath(options["relative"].get<std::string>());}
 if(!entry->folder)refuse("not_a_folder","Only folders can be listed");
 if(running()>=8)refuse("busy","Too many reads are running");
 auto rules=policy;auto task=launch(true,entry,[entry,relative,hidden,rules](FileTask& t){t.list=listItem(*entry,relative,hidden,*rules,t.stop);});
 auto id=sequence++;tasks.emplace(id,std::move(task));return id;
}
std::optional<Json> FileAccess::pollList(uint64_t id){
 refresh();auto task=collect(tasks,dropped,id,true);if(!task)return std::nullopt;return std::move(task->list);
}
void FileAccess::release(const std::string& handle){items.erase(handle);}
Json FileAccess::grants(){
 localOnly();refresh();
 Json list=Json::array();for(auto& g:store.grants)list.push_back({{"id",g.id},{"requester",g.requester},{"folder",displayPath(g.folder)},{"created",g.created},{"used",g.used}});
 return {{"enabled",store.enabled},{"grants",list}};
}
bool FileAccess::revoke(const std::string& id){
 localOnly();refresh();
 // Saved or not, what the folder answered no longer has one covering it and goes (prune).
 return persist([id](FileGrantStore& s){std::erase_if(s.grants,[&](const FileGrant& g){return id=="all"||g.id==id;});});
}
Json FileAccess::setEnabled(bool enabled,const std::string& code){
 // A remote server's scripts can neither see nor change the player's choices.
 localOnly();refresh();
 if(!enabled){
  // Off before anything is saved: a store that cannot be written must not keep it on.
  turnedOff();Json off={{"enabled",false}};if(!persist([](FileGrantStore& s){s.enabled=false;}))off["notSaved"]=true;return off;
 }
 if(store.enabled)return {{"enabled",true}};
 // Turning it on is the player's decision too, asked natively.
 if(denials[EnableRequester]>=FileDenialLimit)refuse("auto_denied","The player kept file access off three times");
 auto r=std::make_unique<Request>();r->kind=Request::Enable;r->requester=EnableRequester;r->language=language({{"language",code}});
 return {{"request",enqueue(std::move(r))}};
}
}
