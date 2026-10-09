// File access for other addons: the path policy, links and junctions, final-path checks,
// limits, text decoding, the grants store, the local-session rule and the dialog round
// trip through the test-only worker (argv[1], answers from MMDHL_FA_TEST_ANSWER). argv[2]
// is the shipped worker: its --request refuses network sources and cache folders before
// opening them, and takes the cache folder from its command line when the game names one.
// Also: turning file access off, revoking a folder and closing the module stop the reads
// and listings they concern, also reads that wait on a file that does not answer; two
// game processes share the store without writing back each other's old state; and, with
// MMDHL_FA_UI_TESTS=1, the real folder picker takes no OK in its first moment.
#include "file_access.hpp"
#include "props/network_path.hpp"
#include <windows.h>
#include <winioctl.h>
#include <shlobj.h>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <thread>
using namespace mmd;
static void check(bool value,const std::string& what){if(!value)throw std::runtime_error("File access validation failed: "+what);}
static std::string refusal(const std::function<void()>& f){try{f();}catch(const FileAccessError& e){return e.code;}catch(const std::exception& e){return std::string("other: ")+e.what();}return "";}
static void write(const fs::path& p,const std::string& bytes){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write(bytes.data(),std::streamsize(bytes.size()));}
static std::string slurp(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void answer(const wchar_t* value){SetEnvironmentVariableW(L"MMDHL_FA_TEST_ANSWER",value);}
static Json wait(FileAccess& fa,uint64_t id){for(int i=0;i<500;i++){auto r=fa.poll(id);if(r.value("state","")!="pending")return r;Sleep(20);}throw std::runtime_error("File access validation failed: dialog never answered");}
static FileReadResult readNow(FileAccess& fa,const std::string& handle,const Json& o){auto id=fa.read(handle,o);for(int i=0;i<500;i++){if(auto r=fa.pollRead(id))return *r;Sleep(10);}throw std::runtime_error("File access validation failed: read never finished");}
static Json listNow(FileAccess& fa,const std::string& handle,const Json& o){auto id=fa.list(handle,o);for(int i=0;i<500;i++){if(auto r=fa.pollList(id))return *r;Sleep(10);}throw std::runtime_error("File access validation failed: listing never finished");}
static std::vector<Json> logged(const fs::path& log){std::vector<Json> out;std::ifstream f(log);std::string line;while(std::getline(f,line))if(!line.empty())out.push_back(Json::parse(line));return out;}
static std::string u8(const fs::path& p){return utf8(p.wstring());}
// Until every read and listing thread ended (they are never joined).
static void settle(){for(int i=0;i<1000&&fileAccessThreads();i++)Sleep(10);check(!fileAccessThreads(),"a read or listing never ended");}
// A file that does not answer, like one on a network share that stopped responding: this
// process holds a read-write-handle oplock on it and never acknowledges the break, so any
// other open of the file waits (until the oplock is released here).
struct StuckFile {
 HANDLE file=INVALID_HANDLE_VALUE;OVERLAPPED overlapped{};REQUEST_OPLOCK_OUTPUT_BUFFER out{};bool held=false;
 explicit StuckFile(const fs::path& path){
  overlapped.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  // Antivirus software may still have the new file open for a moment: no oplock then.
  for(int attempt=0;attempt<40&&!held;attempt++){
   if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);Sleep(50);}
   file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);if(file==INVALID_HANDLE_VALUE)break;
   REQUEST_OPLOCK_INPUT_BUFFER in{REQUEST_OPLOCK_CURRENT_VERSION,sizeof(in),OPLOCK_LEVEL_CACHE_READ|OPLOCK_LEVEL_CACHE_HANDLE|OPLOCK_LEVEL_CACHE_WRITE,REQUEST_OPLOCK_INPUT_FLAG_REQUEST};
   out={};out.StructureVersion=REQUEST_OPLOCK_CURRENT_VERSION;out.StructureLength=sizeof(out);ResetEvent(overlapped.hEvent);
   held=!DeviceIoControl(file,FSCTL_REQUEST_OPLOCK,&in,sizeof(in),&out,sizeof(out),nullptr,&overlapped)&&GetLastError()==ERROR_IO_PENDING;
  }
 }
 // Someone opened the file: the oplock breaks and that open waits for this process.
 bool breaking() const {return held&&WaitForSingleObject(overlapped.hEvent,0)==WAIT_OBJECT_0;}
 void release(){if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);file=INVALID_HANDLE_VALUE;}}
 ~StuckFile(){release();if(overlapped.hEvent)CloseHandle(overlapped.hEvent);}
};
int wmain(int argc,wchar_t** argv){try{
 check(argc==3,"usage: mmdhl_file_access_tests <test worker> <mmdhl_worker>");
 auto base=fs::temp_directory_path()/(L"mmdhl-file-access-test-"+std::to_wstring(GetCurrentProcessId()));
 struct Clean{fs::path dir;~Clean(){std::error_code ec;fs::remove_all(dir,ec);}} clean{base};
 fs::create_directories(base);auto root=resolveFile(base).path;  // the long, final form (TEMP may hold 8.3 names)
 // ---- Paths from Lua, checked as text ----
 FilePolicy policy;policy.denied={root/L"outside"/L"denied"};policy.deniedNames={L"ntuser.dat",L"mmdhl-fa-"};policy.broad={root};
 for(auto bad:{"\\\\host\\share\\a.json","//host/share/a.json","/\\host\\a","\\\\?\\C:\\a.json","\\\\.\\PhysicalDrive0","\\??\\C:\\a.json","//?/C:/a.json","file://host/a.json","\\\\?\\UNC\\host\\a"})
  check(refusal([&]{checkRequestedPath(bad,true,policy);})=="network",std::string("network path accepted: ")+bad);
 check(props::networkPath("\\\\host\\share")&&!props::networkPath("C:\\models\\a.pmx")&&!props::networkPath("Z:/nas/a.pmx")&&props::networkPath("C:\\\xff.pmx"),"the shared network path parser");
 for(auto bad:{"a.json","C:a.json","\\a.json","/a.json"})check(refusal([&]{checkRequestedPath(bad,true,policy);})=="relative",std::string("relative path accepted: ")+bad);
 auto local=u8(root);
 check(refusal([&]{checkRequestedPath(local+"\\a\\..\\b",true,policy);})=="parent","a .. part was accepted");
 check(refusal([&]{checkRequestedPath(local+"\\.\\b",true,policy);})=="parent","a . part was accepted");
 check(refusal([&]{checkRequestedPath(local+"\\a.txt:secret",true,policy);})=="stream","an alternate data stream was accepted");
 check(refusal([&]{checkRequestedPath(local+"\\CON",true,policy);})=="device"&&refusal([&]{checkRequestedPath(local+"\\com1.txt",true,policy);})=="device"&&refusal([&]{checkRequestedPath(local+"\\LPT\xc2\xb9",true,policy);})=="device","device names were accepted");
 check(refusal([&]{checkRequestedPath(local+"\\a*.txt",true,policy);})=="invalid_path"&&refusal([&]{checkRequestedPath(local+"\\a. ",true,policy);})=="invalid_path"&&refusal([&]{checkRequestedPath(local+"\\a\\\\b",true,policy);})=="invalid_path","odd path parts were accepted");
 check(refusal([&]{checkRequestedPath(local+"\\a\tb",true,policy);})=="invalid_path"&&refusal([&]{checkRequestedPath("C:\\\xc0\xaf",true,policy);})=="invalid_path","control characters or broken UTF-8 were accepted");
 check(refusal([&]{checkRequestedPath(local+"\\outside\\denied\\x.txt",true,policy);})=="denied_location"&&refusal([&]{checkRequestedPath(local+"\\NTUSER.DAT",true,policy);})=="denied_location","the denylist did not apply");
 char letter=0;{DWORD drives=GetLogicalDrives();for(char c='Z';c>='D';c--)if(!(drives&(1u<<(c-'A')))){letter=c;break;}}
 if(letter)check(refusal([&]{checkRequestedPath(std::string(1,letter)+":\\a.txt",true,policy);})=="not_found","a missing drive was accepted");
 check(checkRequestedPath(local+"/allowed/",true,policy)==root/L"allowed","forward slashes and a trailing separator");
 check(checkRelativePath("").empty()&&checkRelativePath("sub/b.bin")==fs::path(L"sub\\b.bin"),"relative paths inside a folder");
 for(auto bad:{"../x","sub/../../x","/x","\\x","c:x","a:b","sub/./b","CON","sub//b"})check(!refusal([&]{checkRelativePath(bad);}).empty(),std::string("relative path accepted: ")+bad);
 // Text shown in the dialogs: no control, format or direction characters; whole characters only.
 check(cleanLabel("  My\tAddon\r\n v2\x1b ",64)=="My Addon v2","spaces and control characters");
 check(cleanLabel("evil\xe2\x80\xae" "gnp.exe",64)=="evilgnp.exe","a right-to-left override survived");
 check(cleanLabel("\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80",2)=="\xf0\x9f\x98\x80\xf0\x9f\x98\x80","a surrogate pair was split");
 check(refusal([&]{cleanLabel("\xff",64);})=="invalid_options","broken UTF-8 in a label");
 // The dialogs quote addon text: a quotation mark inside it cannot close those quotes.
 check(cleanLabel("Import a preset.\xe2\x80\x9d Model Hotloader checked this addon. \xe2\x80\x9cOK",120)=="Import a preset.' Model Hotloader checked this addon. 'OK","a closing quotation mark survived");
 check(cleanLabel("\"a\" \xc2\xbb\xc2\xab \xe3\x80\x8d\xe3\x80\x8c \xef\xbc\x82 Bob's",64)=="'a' '' '' ' Bob's","quotation marks of other languages survived");
 // ---- The player's own folders (FilePolicy::system) ----
 {auto system=FilePolicy::system(root);wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);
  PWSTR profileText=nullptr;SHGetKnownFolderPath(FOLDERID_Profile,0,nullptr,&profileText);fs::path profile=resolveFile(profileText).path;CoTaskMemFree(profileText);
  PWSTR localText=nullptr;SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&localText);fs::path localData=resolveFile(localText).path;CoTaskMemFree(localText);
  check(system.deniedPath(resolveFile(windows).path/L"System32"/L"config"/L"SAM"),"the Windows folder is readable");
  check(system.deniedPath(localData/L"ModelHotloader"/L"file-access.json")&&system.deniedPath(fileAccessStore()),"the grants store is readable");
  check(system.deniedPath(profile/L".ssh"/L"id_ed25519")&&system.deniedPath(profile/L"NTUSER.DAT")&&system.deniedPath(localData/L"Google"/L"Chrome"/L"User Data"/L"Default"/L"Login Data"),"keys, the registry hive or browser profiles are readable");
  check(system.deniedPath(root/L"garrysmod"/L"cfg"/L"config.cfg"),"the game's cfg folder is readable");
  check(!system.deniedPath(profile/L"Documents"/L"preset.json"),"an ordinary document is denied");
  PWSTR roamingText=nullptr;SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&roamingText);fs::path roaming=resolveFile(roamingText).path;CoTaskMemFree(roamingText);
  check(system.deniedPath(roaming/L"Telegram Desktop"/L"tdata"/L"key_datas")&&system.deniedPath(roaming/L"FileZilla"/L"sitemanager.xml")&&system.deniedPath(roaming/L"Electrum"/L"wallets"/L"default_wallet")
   &&system.deniedPath(profile/L".git-credentials")&&system.deniedPath(profile/L".config"/L"gh"/L"hosts.yml")&&system.deniedPath(profile/L"Documents"/L".npmrc"),"plain-text sessions, tokens or wallets are readable");
  check(system.broadFolder(profile)&&system.broadFolder(fs::path(L"C:\\"))&&system.broadFolder(localData),"broad folders can be allowed permanently");
  // A folder that holds broad ones is broad too: AppData holds Roaming and Local.
  check(system.broadFolder(localData.parent_path())&&system.broadFolder(localData.parent_path()/L"LocalLow")&&!system.broadFolder(localData/L"SomeTool"),"AppData can be allowed permanently");
  check(displayPath(profile/L"Documents"/L"a.json")=="~\\Documents\\a.json","the profile folder is not shown as ~");}
 // ---- A folder tree with links ----
 write(root/L"allowed"/L"a.json","{\"x\":1}\r\n");
 write(root/L"allowed"/L"sjis.txt",std::string("\x82\xb1\x82\xcc\x83\x82\x83\x66\x83\x8b\x82\xcd\x8d\xc4\x94\x7a\x95\x7a\x8b\xd6\x8e\x7e\x82\xc5\x82\xb7\x81\x42"));
 std::string blob(3u<<20,'\0');for(size_t i=0;i<blob.size();i++)blob[i]=char(i*7+i/4096);write(root/L"allowed"/L"sub"/L"b.bin",blob);
 write(root/L"allowed"/L"secret.ini","hidden");SetFileAttributesW((root/L"allowed"/L"secret.ini").c_str(),FILE_ATTRIBUTE_HIDDEN);
 write(root/L"outside"/L"secret.txt","outside");write(root/L"outside"/L"denied"/L"x.txt","denied");
 auto junction=[](const fs::path& link,const fs::path& target){auto cmd=L"cmd /c mklink /J \""+link.wstring()+L"\" \""+target.wstring()+L"\" >nul";return _wsystem(cmd.c_str())==0;};
 check(junction(root/L"allowed"/L"escape",root/L"outside")&&junction(root/L"allowed"/L"inner",root/L"allowed"/L"sub"),"cannot create the test junctions");
 bool symlinks=CreateSymbolicLinkW((root/L"allowed"/L"link.txt").c_str(),(root/L"outside"/L"secret.txt").c_str(),SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)!=0;
 check(refusal([&]{resolveFile(root/L"allowed"/L"escape");})=="link","a junction was followed");
 if(symlinks)check(refusal([&]{resolveFile(root/L"allowed"/L"link.txt");})=="link","a file symlink was followed");
 else std::cout<<"note: file symlinks need Developer Mode here; that check was skipped\n";
 check(resolveFile(root/L"allowed"/L"escape"/L"secret.txt").path==root/L"outside"/L"secret.txt","a junction in the middle of a path does not show in its final path");
 check(insideFolder(root/L"allowed"/L"a.json",root/L"ALLOWED")&&!insideFolder(root/L"allowedx",root/L"allowed")&&insideFolder(root,root),"folder containment");
 // ---- Imports only from this computer (model notes: model_notes_tests.cpp) ----
 {auto importJob=[&](const fs::path& job,const Json& request,const fs::path& cache){
   write(job/L"request.json",request.dump());
   auto cmd=L"\""+std::wstring(argv[2])+L"\" --request \""+(job/L"request.json").wstring()+L"\""+(cache.empty()?L"":L" \""+cache.wstring()+L"\"");STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};
   check(CreateProcessW(argv[2],cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process)!=0,"cannot start the worker");
   check(WaitForSingleObject(process.hProcess,20000)==WAIT_OBJECT_0,"the worker hung");CloseHandle(process.hThread);CloseHandle(process.hProcess);
   return readJson(job/L"status.json");};
  auto status=importJob(root/L"job",{{"source","\\\\127.0.0.1\\mmdhl-test\\m.vrm"},{"cache",u8(root/L"cache")},{"options",Json::object()}},{});
  check(status.value("state","")=="failed"&&status.value("errorCode","")=="io.network","the worker imported from a network path: "+status.dump());
  // request.json sits in data/, where a script can rewrite it before the worker reads it. Tools
  // that name the cache there cannot name another computer...
  status=importJob(root/L"job2",{{"source",""},{"cache","\\\\127.0.0.1\\mmdhl-test"},{"options",{{"kind","derive"},{"parent",std::string(64,'a')}}}},{});
  check(status.value("state","")=="failed"&&status.value("errorCode","")=="io.network","the worker used a network cache folder: "+status.dump());
  // ...and when the game starts the worker, its command line names the cache: the file's field is not read.
  write(root/L"tetra.obj","v 0 0 0\nv 10 0 0\nv 0 10 0\nv 0 0 10\nf 1 3 2\nf 1 2 4\nf 1 4 3\nf 2 3 4\n");
  status=importJob(root/L"job3",{{"source",u8(root/L"tetra.obj")},{"cache",u8(root/L"forged")},{"options",{{"kind","static"}}}},root/L"cache");
  check(status.value("state","")=="complete"&&fs::exists(root/L"cache"/L"static"/L"assets"/wide(status.value("asset","")+".gmdl"))&&!fs::exists(root/L"forged"),"the worker took its cache folder from request.json: "+status.dump());}
 // ---- Requests, dialogs and reads ----
 auto log=root/L"dialogs.log";SetEnvironmentVariableW(L"MMDHL_FA_TEST_LOG",log.c_str());
 FileAccessConfig config;config.worker=argv[1];config.store=root/L"store"/L"file-access.json";config.temp=root/L"tmp";config.policy=policy;fs::create_directories(config.temp);
 // Lua can write anything under garrysmod/data: the module's store is in %LOCALAPPDATA% (the
 // dialogs' folders are in the user's temporary folder, checked below).
 {PWSTR p=nullptr;SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&p);fs::path localData=p;CoTaskMemFree(p);
  check(fileAccessStore()==localData/L"ModelHotloader"/L"file-access.json","the grants store is not in %LOCALAPPDATA%: "+u8(fileAccessStore()));}
 auto fa=std::make_unique<FileAccess>(config);
 // Without the server realm in this process (a remote server) nothing can be asked.
 check(!localServerRealm()&&fa->info().value("reason","")=="no_local_server"&&!fa->info().value("available",true),"file access looked available without a local server");
 Json ask={{"requester","Test Addon"},{"script","lua/autorun/client/test.lua"},{"path",local+"\\allowed\\a.json"},{"purpose","Load a preset"},{"language","ja"}};
 check(refusal([&]{fa->request(ask);})=="unavailable_remote"&&refusal([&]{fa->pick({{"requester","Test Addon"}});})=="unavailable_remote","a request passed without a local server");
 // A remote server's scripts cannot see or change the player's choices either.
 check(refusal([&]{fa->grants();})=="unavailable_remote"&&refusal([&]{fa->revoke("all");})=="unavailable_remote"&&refusal([&]{fa->setEnabled(false,"en");})=="unavailable_remote","management passed without a local server");
 acquireRuntimeRealm(true);
 check(fa->grants()["grants"].empty()&&fa->info().value("enabled",false),"a new store has grants or is off");
 // The module's own setup names no temporary folder: the dialogs use the user's, by a random name.
 {wchar_t temp[MAX_PATH+1]{};GetTempPathW(MAX_PATH+1,temp);auto label="Temp test "+std::to_string(GetCurrentProcessId());
  auto folder=[&]{for(auto& e:fs::directory_iterator(temp))if(e.path().filename().wstring().starts_with(L"mmdhl-fa-")&&fs::exists(e.path()/L"request.json"))try{if(readJson(e.path()/L"request.json").value("requester","")==label)return e.path();}catch(...){}return fs::path();};
  FileAccess own({config.worker,root/L"store3"/L"file-access.json",{},policy});answer(L"wait");auto id=own.pick({{"requester",label}});
  auto dir=folder();check(!dir.empty()&&dir.filename().wstring().size()==9+32,"the dialog's private folder is not in %TEMP%");
  own.cancel(id);Sleep(100);check(!fs::exists(dir),"the dialog's private folder was left in %TEMP%");}
 // A missing drive or a never-readable place is told in the window like a missing file, not
 // at once: a script cannot learn silently which drives or user folders this computer has.
 {FileAccess probe({config.worker,root/L"store4"/L"file-access.json",config.temp,policy});answer(L"once");
  auto told=[&](const std::string& path,const std::string& problem){auto id=probe.request({{"requester","Drives"},{"path",path}});auto r=wait(probe,id);auto shown=logged(log).back();
   check(r.value("state","")=="denied"&&shown.value("problem","")==problem&&shown.value("path","")==path&&!shown.value("remember",true),"the window for "+path+": "+r.dump()+" "+shown.dump());};
  if(letter)told(std::string(1,letter)+":\\a.txt","not_found");
  told(local+"\\outside\\denied\\x.txt","denied_location");}
 check(fa->setEnabled(true,"en").value("enabled",false),"turning on while already on");
 check(fa->info().value("available",false),"file access unavailable in a local session: "+fa->info().dump());
 answer(L"once");auto granted=wait(*fa,fa->request(ask));
 check(granted.value("state","")=="granted"&&granted["items"].size()==1,"allow once: "+granted.dump());
 auto item=granted["items"][0];auto handle=item.value("handle",std::string());
 check(handle.size()==32&&item.value("name","")=="a.json"&&item.value("size",uint64_t(0))==9&&!item.value("remembered",true),"the granted item: "+item.dump());
 {PWSTR p=nullptr;SHGetKnownFolderPath(FOLDERID_Profile,0,nullptr,&p);fs::path profile=resolveFile(p).path;CoTaskMemFree(p);
  if(insideFolder(root,profile))check(granted.dump().find(u8(profile))==std::string::npos&&item.value("displayPath","").starts_with("~\\"),"the user's folder reached Lua: "+granted.dump());}
 auto request=logged(log).back();
 check(request.value("kind","")=="consent"&&request.value("language","")=="ja"&&request.value("requester","")=="Test Addon"&&request.value("path","")==u8(root/L"allowed"/L"a.json")&&request.value("problem","x").empty()&&request.value("remember",false),"the consent request: "+request.dump());
 auto text=readNow(*fa,handle,{{"mode","text"}});
 check(text.data=="{\"x\":1}"&&text.info.value("encoding","")=="ascii"&&text.info.value("eof",false),"text read: "+text.data);
 check(refusal([&]{fa->read(handle,{{"relative","x"}});})=="invalid_options","a file item took a path inside it");
 // "Always" for a folder: remembered outside the game, used without a dialog next time.
 answer(L"always");Json folderAsk={{"requester","Test Addon"},{"path",local+"\\allowed"},{"folder",true}};
 granted=wait(*fa,fa->request(folderAsk));check(granted.value("state","")=="granted"&&granted.value("changed",false),"allow always: "+granted.dump());
 auto folder=granted["items"][0]["handle"].get<std::string>();
 auto stored=readJson(config.store);check(stored["grants"].size()==1&&stored["grants"][0]["folder"]==u8(root/L"allowed")&&stored["grants"][0]["requester"]=="Test Addon","the stored grant: "+stored.dump());
 auto sjis=readNow(*fa,folder,{{"relative","sjis.txt"},{"mode","text"}});
 check(sjis.data==utf8(L"このモデルは再配布禁止です。")&&sjis.info.value("encoding","")=="shift_jis","Shift-JIS text: "+sjis.data);
 auto chunk=readNow(*fa,folder,{{"relative","sub/b.bin"},{"offset",1000000},{"length",4096}});
 check(chunk.data==blob.substr(1000000,4096)&&!chunk.info.value("eof",true)&&chunk.info.value("size",uint64_t(0))==blob.size(),"a binary chunk");
 auto tail=readNow(*fa,folder,{{"relative","sub/b.bin"},{"offset",blob.size()-10}});check(tail.data==blob.substr(blob.size()-10)&&tail.info.value("eof",false),"the last chunk");
 check(refusal([&]{fa->read(folder,{{"relative","sub/b.bin"},{"length",(16u<<20)+1}});})=="invalid_options"&&refusal([&]{fa->read(folder,{{"relative","sub/b.bin"},{"offset",(256ull<<20)+1}});})=="offset_too_large","read limits");
 check(refusal([&]{readNow(*fa,folder,{{"relative","sub/b.bin"},{"mode","text"}});})=="too_large","a text read past the limit");
 check(refusal([&]{readNow(*fa,folder,{{"relative","escape/secret.txt"}});})=="outside","a junction led outside the grant");
 check(refusal([&]{fa->read(folder,{{"relative","../outside/secret.txt"}});})=="parent","a .. path left the grant");
 check(refusal([&]{readNow(*fa,folder,{{"relative","escape"}});})=="link","a junction was opened");
 if(symlinks)check(refusal([&]{readNow(*fa,folder,{{"relative","link.txt"}});})=="link","a file symlink was read");
 check(refusal([&]{readNow(*fa,folder,{{"relative","secret.ini"}});})=="hidden","a hidden file was read unasked");
 check(readNow(*fa,folder,{{"relative","secret.ini"},{"hidden",true}}).data=="hidden","a hidden file asked for");
 auto listing=listNow(*fa,folder,Json::object());
 std::vector<std::string> names;for(auto& e:listing["entries"])names.push_back(e["name"].get<std::string>());
 check(names==std::vector<std::string>{"sub","a.json","sjis.txt"}&&!listing.value("truncated",true),"the listing (links and hidden files left out): "+listing.dump());
 check(listNow(*fa,folder,{{"relative","sub"}})["entries"][0]["size"]==blob.size(),"a subfolder listing");
 check(refusal([&]{listNow(*fa,folder,{{"relative","inner"}});})=="link","a junction inside the grant was listed");
 // A new session (map change) remembers the folder: no dialog.
 fa=std::make_unique<FileAccess>(config);auto dialogs=logged(log).size();
 granted=wait(*fa,fa->request({{"requester","Test Addon"},{"path",local+"\\allowed\\sub\\b.bin"}}));
 check(granted.value("state","")=="granted"&&granted["items"][0].value("remembered",false)&&logged(log).size()==dialogs,"the remembered folder still asked: "+granted.dump());
 answer(L"deny");granted=wait(*fa,fa->request({{"requester","Other Addon"},{"path",local+"\\allowed\\a.json"}}));
 check(granted.value("state","")=="denied"&&logged(log).size()==dialogs+1,"another addon used the remembered folder: "+granted.dump());
 // Missing, linked and denied targets still show a dialog: Lua cannot probe silently.
 answer(L"once");granted=wait(*fa,fa->request({{"requester","Prober"},{"path",local+"\\allowed\\missing.json"}}));
 check(granted.value("state","")=="denied"&&logged(log).back().value("problem","")=="not_found","a missing file: "+granted.dump());
 granted=wait(*fa,fa->request({{"requester","Prober"},{"path",local+"\\allowed\\escape"},{"folder",true}}));
 check(granted.value("state","")=="denied"&&logged(log).back().value("problem","")=="link","a junction: "+granted.dump());
 answer(L"deny");check(wait(*fa,fa->request({{"requester","Prober"},{"path",local+"\\allowed\\a.json"}})).value("state","")=="denied","a denied request");
 check(refusal([&]{fa->request({{"requester","Prober"},{"path",local+"\\allowed\\a.json"}});})=="auto_denied","a requester refused three times asked again");
 granted=wait(*fa,fa->request({{"requester","Broad"},{"path",local+"\\allowed.txt"}}));  // not found, but the requester counts separately
 check(granted.value("state","")=="denied","a fresh requester was refused");
 answer(L"always");write(root/L"top.json","{}");granted=wait(*fa,fa->request({{"requester","Broad"},{"path",local+"\\top.json"}}));
 check(granted.value("state","")=="granted"&&!granted.value("changed",false)&&!logged(log).back().value("remember",true),"always was kept for a broad folder: "+granted.dump());
 // The picker: what the player chose, never a raw network path.
 answer((L"pick:"+(root/L"allowed"/L"a.json").wstring()+L"|\\\\host\\share\\x.json").c_str());
 auto filters=Json::array({Json::array({"JSON files","*.json"}),Json::array({"Text","*.txt;*.md"})});
 granted=wait(*fa,fa->pick({{"requester","Picker"},{"filters",filters},{"multiple",true},{"title","Choose a preset"}}));
 check(granted.value("state","")=="granted"&&granted["items"].size()==1&&granted["refused"][0]["code"]=="network","a picked network path: "+granted.dump());
 check(logged(log).back()["filters"]==filters,"picker filters: "+logged(log).back().dump());
 for(auto bad:{Json::array({Json::array({"x","*.exe /c"})}),Json::array({Json::array({"x","C:\\*"})}),Json::array({Json::array({"","*.json"})}),Json::array({"*.json"})})check(refusal([&]{fa->pick({{"requester","Picker"},{"filters",bad}});})=="invalid_options","a bad filter was accepted: "+bad.dump());
 answer(L"cancelled");check(wait(*fa,fa->pick({{"requester","Picker"}})).value("state","")=="denied","a cancelled picker");
 check(refusal([&]{fa->pick(Json::object());})=="invalid_options"&&refusal([&]{fa->pick({{"requester","\x01\x02"}});})=="invalid_options","a request without a requester");
 // A dialog that dies answers nothing; one that is canceled stops; the queue is bounded.
 answer(L"crash");check(wait(*fa,fa->pick({{"requester","Crash"}})).value("code","")=="dialog_failed","a crashed dialog");
 answer(L"wait");std::vector<uint64_t> waiting;for(int i=0;i<5;i++)waiting.push_back(fa->pick({{"requester","Queue"}}));
 check(fa->poll(waiting[0]).value("dialog",false)&&fa->poll(waiting[2]).value("position",0)==2,"queue positions");
 check(refusal([&]{fa->pick({{"requester","Queue"}});})=="busy","the dialog queue is unbounded");
 bool privateFolder=false;for(auto& e:fs::directory_iterator(config.temp))privateFolder|=e.path().filename().wstring().starts_with(L"mmdhl-fa-")&&fs::exists(e.path()/L"request.json");
 check(privateFolder,"the dialog's private folder");
 for(auto id:waiting)fa->cancel(id);check(refusal([&]{fa->poll(waiting[0]);})=="unknown_request"&&fa->info().value("queued",1)==0&&!fa->info().value("dialog",true),"canceled dialogs");
 Sleep(100);check(fs::is_empty(config.temp),"a private folder was left behind");
 // Off at once; on only through the native confirmation.
 check(!fa->setEnabled(false,"en").value("enabled",true)&&refusal([&]{fa->request(ask);})=="disabled"&&refusal([&]{fa->read(folder,Json::object());})=="disabled","turning off");
 answer(L"no");auto on=fa->setEnabled(true,"fr");check(wait(*fa,on["request"]).value("state","")=="denied"&&!fa->info().value("enabled",true),"turning on without confirmation");
 check(logged(log).back().value("kind","")=="enable"&&logged(log).back().value("language","")=="fr","the confirmation request");
 answer(L"yes");on=fa->setEnabled(true,"en");check(wait(*fa,on["request"]).value("state","")=="granted"&&fa->info().value("enabled",false)&&readJson(config.store).value("enabled",false),"turning on");
 // Revoking: the grant is gone and so are the items it answered.
 granted=wait(*fa,fa->request({{"requester","Test Addon"},{"path",local+"\\allowed\\a.json"}}));check(granted["items"][0].value("remembered",false),"the remembered grant after re-enabling");
 auto grants=fa->grants();check(grants["grants"].size()==1&&grants["grants"][0]["folder"].get<std::string>().starts_with("~"),"the grants list: "+grants.dump());
 check(fa->revoke(grants["grants"][0]["id"])&&fa->grants()["grants"].empty()&&readJson(config.store)["grants"].empty(),"revoke");
 check(refusal([&]{fa->read(granted["items"][0]["handle"],Json::object());})=="released","an item outlived its revoked grant");
 // The store: a corrupt file means no grants; entries that fail the rules are dropped.
 write(config.store,"{not json");fa=std::make_unique<FileAccess>(config);check(fa->grants()["grants"].empty()&&fa->info().value("enabled",false),"a corrupt store");
 write(config.store,Json{{"schema",1},{"enabled",false},{"grants",{{{"id","0123456789abcdef"},{"requester","Kept"},{"folder",local+"\\allowed"},{"created",1},{"used",2}},
  {{"id","0123456789abcde0"},{"requester","Broad"},{"folder",local}},{{"id","0123456789abcde1"},{"requester","Network"},{"folder","\\\\host\\share"}},
  {{"id","0123456789abcde2"},{"requester","Denied"},{"folder",local+"\\outside\\denied"}},{{"id","BAD"},{"requester","Bad id"},{"folder",local+"\\allowed"}},
  {{"id","0123456789abcde3"},{"requester","Bad\nname"},{"folder",local+"\\allowed"}}}}}.dump());
 fa=std::make_unique<FileAccess>(config);grants=fa->grants();
 check(grants["grants"].size()==1&&grants["grants"][0]["requester"]=="Kept"&&!grants.value("enabled",true),"store validation: "+grants.dump());
 // Closing a picker is not a refusal of the addon, and an allowed request clears its refusals.
 fa=std::make_unique<FileAccess>(FileAccessConfig{config.worker,root/L"store2"/L"file-access.json",config.temp,policy});
 answer(L"cancelled");for(int i=0;i<4;i++)check(wait(*fa,fa->pick({{"requester","Picker"}})).value("state","")=="denied","closing the picker "+std::to_string(i+1)+" times");
 Json again={{"requester","Again"},{"path",local+"\\allowed\\a.json"}};
 answer(L"deny");for(int i=0;i<2;i++)wait(*fa,fa->request(again));
 answer(L"once");check(wait(*fa,fa->request(again)).value("state","")=="granted","allowed after two refusals");
 answer(L"deny");for(int i=0;i<3;i++)check(wait(*fa,fa->request(again)).value("state","")=="denied","refusals before the allowed request still counted");
 check(refusal([&]{fa->request(again);})=="auto_denied","a requester refused three times after an allowed request asked again");
 // Labels are free to change: after ten refusals or closed pickers in a map nobody may ask.
 check(wait(*fa,fa->request({{"requester","Name"},{"path",local+"\\allowed\\a.json"}})).value("state","")=="denied","a renamed requester");
 check(refusal([&]{fa->pick({{"requester","Fresh name"}});})=="auto_denied_session","a renamed requester kept asking");
 // A store that cannot be written: the player's answers apply until the map changes, are
 // reported as not saved, and no error (with a path in it) reaches Lua.
 write(root/L"blocker","not a folder");
 {FileAccess locked({config.worker,root/L"blocker"/L"file-access.json",config.temp,policy});answer(L"always");
  auto r=wait(locked,locked.request({{"requester","Locked"},{"path",local+"\\allowed\\a.json"}}));
  check(r.value("state","")=="granted"&&r.value("notSaved",false)&&r.value("changed",false)&&r["items"][0].value("remembered",false)&&locked.grants()["grants"].size()==1,"always with a store that cannot be saved: "+r.dump());
  check(!locked.revoke("all")&&locked.grants()["grants"].empty(),"revoking with a store that cannot be saved");
  auto off=locked.setEnabled(false,"en");check(!off.value("enabled",true)&&off.value("notSaved",false)&&!locked.info().value("enabled",true),"turning off with a store that cannot be saved: "+off.dump());
  answer(L"yes");auto turnedOn=wait(locked,locked.setEnabled(true,"en")["request"]);
  check(turnedOn.value("state","")=="granted"&&turnedOn.value("notSaved",false)&&locked.info().value("enabled",false),"turning on with a store that cannot be saved: "+turnedOn.dump());}
 // A result nobody collects holds its read slot only for keepResultsMs.
 {FileAccessConfig quick=config;quick.store=root/L"store5"/L"file-access.json";quick.keepResultsMs=1000;FileAccess reader(quick);answer(L"once");
  auto file=wait(reader,reader.request({{"requester","Reader"},{"path",local+"\\allowed\\a.json"}}))["items"][0]["handle"].get<std::string>();
  std::vector<uint64_t> forgotten;for(int i=0;i<8;i++)forgotten.push_back(reader.read(file,Json::object()));
  check(refusal([&]{reader.read(file,Json::object());})=="busy","a ninth read ran beside eight");
  Sleep(1500);check(readNow(reader,file,Json::object()).data=="{\"x\":1}\r\n","uncollected results kept their slots");
  check(refusal([&]{reader.pollRead(forgotten[0]);})=="unknown_request","an uncollected result was kept");}
 // ---- Revoking a folder or turning file access off stops what was read through it ----
 settle();
 {FileAccessConfig own=config;own.store=root/L"store6"/L"file-access.json";FileAccess reader(own);
  answer(L"always");auto inFolder=wait(reader,reader.request({{"requester","Reader"},{"path",local+"\\allowed"},{"folder",true}}))["items"][0]["handle"].get<std::string>();
  answer(L"once");auto once=wait(reader,reader.request({{"requester","Other"},{"path",local+"\\allowed\\a.json"}}))["items"][0]["handle"].get<std::string>();
  // Finished but not collected yet: a result of a revoked folder is not handed out; a file allowed once still is.
  auto read=reader.read(inFolder,{{"relative","a.json"}}),listing=reader.list(inFolder,Json::object()),other=reader.read(once,Json::object());settle();
  check(reader.revoke(reader.grants()["grants"][0]["id"]),"revoking the folder");
  check(refusal([&]{reader.pollRead(read);})=="released"&&refusal([&]{reader.pollList(listing);})=="released","a read or listing of a revoked folder was delivered");
  check(reader.pollRead(other).value().data=="{\"x\":1}\r\n","revoking a folder stopped the read of a file allowed once");
  read=reader.read(once,Json::object());auto decoded=reader.read(once,{{"mode","text"}});settle();
  check(!reader.setEnabled(false,"en").value("enabled",true)&&refusal([&]{reader.pollRead(read);})=="disabled"&&refusal([&]{reader.pollRead(decoded);})=="disabled","a read was delivered after file access was turned off");}
 // ---- Reads that wait on files that do not answer ----
 // Turning off answers at once, and closing the module (every map change) does not wait for them.
 write(root/L"stuck"/L"first.bin","stuck");write(root/L"stuck"/L"second.bin","stuck");
 {FileAccessConfig own=config;answer(L"once");
  own.store=root/L"store7"/L"file-access.json";auto first=std::make_unique<FileAccess>(own);
  own.store=root/L"store8"/L"file-access.json";auto second=std::make_unique<FileAccess>(own);
  auto firstItem=wait(*first,first->request({{"requester","Stuck"},{"path",local+"\\stuck\\first.bin"}}))["items"][0]["handle"].get<std::string>();
  auto secondItem=wait(*second,second->request({{"requester","Stuck"},{"path",local+"\\stuck\\second.bin"}}))["items"][0]["handle"].get<std::string>();
  StuckFile stuckFirst(root/L"stuck"/L"first.bin"),stuckSecond(root/L"stuck"/L"second.bin");
  // A read waits only when the open breaks the oplock and nothing completes it.
  auto stuckRead=[&](FileAccess& fa,const std::string& item,const StuckFile& stuck)->std::optional<uint64_t>{
   if(!stuck.held)return std::nullopt;auto id=fa.read(item,Json::object());
   for(int i=0;i<200&&!stuck.breaking();i++)Sleep(10);Sleep(200);
   if(!stuck.breaking()||fa.pollRead(id))return std::nullopt;return id;};
  auto firstRead=stuckRead(*first,firstItem,stuckFirst);auto secondRead=firstRead?stuckRead(*second,secondItem,stuckSecond):std::nullopt;
  if(!firstRead||!secondRead)std::cout<<"note: this volume grants no oplock that holds an open; the checks of reads that do not answer were skipped\n";
  else{
   check(!first->setEnabled(false,"en").value("enabled",true)&&refusal([&]{first->pollRead(*firstRead);})=="disabled","a read that does not answer held up turning file access off");
   std::atomic<bool> closed=false;std::thread closer([&]{second.reset();closed=true;});
   for(int i=0;i<200&&!closed;i++)Sleep(10);
   bool waited=!closed;stuckSecond.release();closer.join();
   check(!waited,"closing file access waited for a read of a file that does not answer");
  }
  stuckFirst.release();stuckSecond.release();first.reset();second.reset();settle();}
 // ---- Two game processes share the store (two installs, -multirun) ----
 // Neither writes back what the other revoked or turned off, and each sees the other's choices.
 {FileAccessConfig shared=config;shared.store=root/L"store9"/L"file-access.json";
  FileAccess one(shared);answer(L"always");
  auto remembered=wait(one,one.request({{"requester","Shared"},{"path",local+"\\allowed"},{"folder",true}}));check(remembered.value("state","")=="granted","the shared grant: "+remembered.dump());
  FileAccess two(shared);auto dialogs=logged(log).size();
  auto viaGrant=wait(two,two.request({{"requester","Shared"},{"path",local+"\\allowed\\a.json"}}));
  check(viaGrant["items"][0].value("remembered",false)&&logged(log).size()==dialogs,"the other process did not use the remembered folder: "+viaGrant.dump());
  auto twoItem=viaGrant["items"][0]["handle"].get<std::string>();
  // Revoked in one: the other asks again, loses what the folder answered, and saves nothing of it.
  check(one.revoke(one.grants()["grants"][0]["id"]),"revoking in the first process");
  check(refusal([&]{two.read(twoItem,Json::object());})=="released","an item outlived a folder another process revoked");
  answer(L"always");auto other=wait(two,two.request({{"requester","Shared"},{"path",local+"\\outside\\secret.txt"}}));
  check(other.value("state","")=="granted"&&logged(log).size()==dialogs+1,"the second process: "+other.dump());
  auto stored=readJson(shared.store)["grants"];
  check(stored.size()==1&&stored[0]["folder"]==u8(root/L"outside")&&two.grants()["grants"].size()==1,"a revoked folder was written back by another process: "+stored.dump());
  // Both add a folder: the store keeps both.
  answer(L"always");wait(one,one.request({{"requester","One"},{"path",local+"\\allowed\\sub\\b.bin"}}));
  stored=readJson(shared.store)["grants"];check(stored.size()==2&&one.grants()["grants"].size()==2&&two.grants()["grants"].size()==2,"one process dropped the other's folder: "+stored.dump());
  // Turned off in one: off in the other at once, and it stays off in the store.
  auto twoOnce=other["items"][0]["handle"].get<std::string>();
  check(!one.setEnabled(false,"en").value("enabled",true),"turning off in the first process");
  check(refusal([&]{two.request({{"requester","Shared"},{"path",local+"\\outside\\secret.txt"}});})=="disabled"&&refusal([&]{two.read(twoOnce,Json::object());})=="disabled"&&!two.info().value("enabled",true),"file access stayed on in the other process");
  check(!readJson(shared.store).value("enabled",true)&&readJson(shared.store)["grants"].size()==2,"the store after turning off: "+readJson(shared.store).dump());}
 // A choice that could not be saved is written with the next change that can be.
 write(root/L"blocker2","not a folder");
 {FileAccess later({config.worker,root/L"blocker2"/L"file-access.json",config.temp,policy});answer(L"always");
  check(wait(later,later.request({{"requester","Later"},{"path",local+"\\allowed\\a.json"}})).value("notSaved",false),"a store that cannot be written");
  fs::remove(root/L"blocker2");fs::create_directories(root/L"blocker2");
  check(!later.setEnabled(false,"en").contains("notSaved"),"the store was not written once it could be");
  auto saved=readJson(root/L"blocker2"/L"file-access.json");check(!saved.value("enabled",true)&&saved["grants"].size()==1&&saved["grants"][0]["requester"]=="Later","the unsaved folder was lost: "+saved.dump());}
 // ---- The real folder picker: an Enter or click at once chooses nothing ----
 // It shows a window on this desktop, so it runs only when MMDHL_FA_UI_TESTS is set.
 if(GetEnvironmentVariableW(L"MMDHL_FA_UI_TESTS",nullptr,0)){
  FileAccess ui({config.worker,root/L"store10"/L"file-access.json",config.temp,policy});answer(L"ui-pick");SetEnvironmentVariableW(L"MMDHL_FA_TEST_FOLDER",(root/L"allowed").c_str());
  auto picked=wait(ui,ui.pick({{"requester","Picker"},{"folder",true}}));auto shown=logged(log).back();
  check(shown.contains("uiPick")&&shown["uiPick"].value("refusedAtOnce",false),"the picker took an OK at once: "+shown.dump()+" "+picked.dump());
  check(picked.value("state","")=="granted"&&picked["items"][0].value("name","")=="allowed","the picker after its button woke up: "+picked.dump());
 }else std::cout<<"note: the real folder picker was not shown (set MMDHL_FA_UI_TESTS=1 on a desktop)\n";
 fa.reset();releaseRuntimeRealm(true);check(!localServerRealm(),"the server realm count");
 std::cout<<"PASS: path policy, links and final paths, limits, text, grants store, local-session rule, dialogs, stopped reads and the shared store\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
