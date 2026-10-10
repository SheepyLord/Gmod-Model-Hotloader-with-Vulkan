#include "picked_models.hpp"
#include "file_access.hpp"
#include "model_notes.hpp"
#include "props/network_path.hpp"
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include "posix.hpp"
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>
#endif
#include <algorithm>
namespace mmd {
namespace {
constexpr size_t MaxPicked=4096;
// Paths compare as Windows compares names: normalized, case-insensitively.
std::wstring key(std::string_view source){
 if(source.empty())return {};
 try{return fs::path(wide(source)).lexically_normal().wstring();}catch(...){return {};}
}
#ifdef _WIN32
bool same(const std::wstring& a,const std::wstring& b){return a.size()==b.size()&&(a.empty()||CompareStringOrdinal(a.data(),int(a.size()),b.data(),int(b.size()),TRUE)==CSTR_EQUAL);}
#else
// Linux file names are case-sensitive.
bool same(const std::wstring& a,const std::wstring& b){return a==b;}
#endif
bool listed(const std::vector<std::wstring>& list,const std::wstring& path){return !path.empty()&&std::any_of(list.begin(),list.end(),[&](const std::wstring& p){return same(p,path);});}
std::wstring randomHex(size_t bytes){
#ifdef _WIN32
 std::vector<unsigned char> b(bytes);if(BCryptGenRandom(nullptr,b.data(),ULONG(b.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("No random numbers");
#else
 std::vector<unsigned char> b(bytes);posix::randomBytes(b.data(),b.size());
#endif
 std::wstring s;for(auto c:b){s.push_back(L"0123456789abcdef"[c>>4]);s.push_back(L"0123456789abcdef"[c&15]);}return s;
}
fs::path temporaryFolder(const fs::path& temp){
 if(!temp.empty())return temp;
#ifdef _WIN32
 wchar_t t[MAX_PATH+1]{};if(!GetTempPathW(MAX_PATH+1,t))throw std::runtime_error("No temporary folder");return t;
#else
 return posix::tempDirectory();
#endif
}
#ifdef _WIN32
bool plainDirectory(const fs::path& path){auto a=GetFileAttributesW(ioPath(path).c_str());return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY)&&!(a&FILE_ATTRIBUTE_REPARSE_POINT);}
#else
bool plainDirectory(const fs::path& path){return posix::plainDirectory(path);}
#endif
// Games running at the same time (the Steam copy and another install) share the store:
// one saves at a time, each merging what the other saved.
#ifdef _WIN32
struct StoreLock{
 HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\ModelHotloader-picked-models");bool owned=false;
 StoreLock(){if(mutex){auto r=WaitForSingleObject(mutex,2000);owned=r==WAIT_OBJECT_0||r==WAIT_ABANDONED;}}
 ~StoreLock(){if(owned)ReleaseMutex(mutex);if(mutex)CloseHandle(mutex);}
 StoreLock(const StoreLock&)=delete;StoreLock& operator=(const StoreLock&)=delete;
};
#else
// flock on <store>.lock, waited for two seconds at most (then it saves unlocked, as on Windows).
struct StoreLock{
 int fd=-1;bool owned=false;
 StoreLock(){
  fs::path file;try{file=pickedModelsStore();}catch(...){return;}
  file+=".lock";std::error_code error;fs::create_directories(file.parent_path(),error);
  fd=open(file.c_str(),O_RDWR|O_CREAT|O_CLOEXEC,0600);if(fd<0)return;
  for(int attempt=0;attempt<200;attempt++){if(flock(fd,LOCK_EX|LOCK_NB)==0){owned=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 }
 ~StoreLock(){if(owned)flock(fd,LOCK_UN);if(fd>=0)close(fd);}
 StoreLock(const StoreLock&)=delete;StoreLock& operator=(const StoreLock&)=delete;
};
#endif
}
PickedModels::PickedModels(fs::path file):store(std::move(file)){refresh();}
// Reads the store again when it changed since it was last read and adds what it holds
// that this game does not know yet (another game's picks). A damaged store adds nothing.
void PickedModels::refresh() const {
 if(store.empty())return;
 // Every save writes a new file in its place (writeAtomic): its file ID says whether it changed.
#ifndef _WIN32
 struct stat st{};if(stat(ioPath(store).c_str(),&st)!=0)return;
 uint64_t index=uint64_t(st.st_ino),time=uint64_t(st.st_mtim.tv_sec)*1000000000ull+uint64_t(st.st_mtim.tv_nsec),size=uint64_t(st.st_size);
 if(index==seenIndex&&time==seenTime&&size==seenSize)return;
 seenIndex=index;seenTime=time;seenSize=size;
 if(S_ISDIR(st.st_mode)||size>(4u<<20))return;
#else
 BY_HANDLE_FILE_INFORMATION data{};
 {HANDLE h=CreateFileW(ioPath(store).c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);if(h==INVALID_HANDLE_VALUE)return;
  bool known=GetFileInformationByHandle(h,&data);CloseHandle(h);if(!known)return;}
 uint64_t index=(uint64_t(data.nFileIndexHigh)<<32)|data.nFileIndexLow,time=(uint64_t(data.ftLastWriteTime.dwHighDateTime)<<32)|data.ftLastWriteTime.dwLowDateTime,size=(uint64_t(data.nFileSizeHigh)<<32)|data.nFileSizeLow;
 if(index==seenIndex&&time==seenTime&&size==seenSize)return;
 seenIndex=index;seenTime=time;seenSize=size;
 if((data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||size>(4u<<20))return;
#endif
 std::vector<std::wstring> stored;
 try{
  auto j=readJson(store);if(!j.is_object()||j.value("schema",0)!=1||!j.contains("paths")||!j["paths"].is_array())return;
  for(auto& p:j["paths"])if(p.is_string()){auto k=key(p.get<std::string>());if(!k.empty()&&!listed(stored,k))stored.push_back(k);}
 }catch(...){return;}
 // The store's order (oldest first), then what only this game remembers.
 for(auto& p:paths)if(!listed(stored,p))stored.push_back(p);
 if(stored.size()>MaxPicked)stored.erase(stored.begin(),stored.end()-MaxPicked);
 paths=std::move(stored);
}
bool PickedModels::picked(std::string_view source,bool thisSession) const {
 auto k=key(source);if(k.empty())return false;
 if(thisSession)return listed(session,k);
 if(listed(paths,k))return true;
 refresh();return listed(paths,k);
}
bool PickedModels::add(std::string_view source){
 auto k=key(source);if(k.empty())return true;
 if(!listed(session,k))session.push_back(k);
 StoreLock lock;seenIndex=seenTime=seenSize=0;refresh();
 // Newest last; the oldest go first past the limit.
 paths.erase(std::remove_if(paths.begin(),paths.end(),[&](const std::wstring& p){return same(p,k);}),paths.end());paths.push_back(k);
 if(paths.size()>MaxPicked)paths.erase(paths.begin(),paths.end()-MaxPicked);
 if(store.empty())return false;
 try{Json list=Json::array();for(auto& p:paths)list.push_back(utf8(p));fs::create_directories(ioPath(store.parent_path()));writeJson(store,{{"schema",1},{"paths",list}});}catch(...){return false;}
 refresh();return true;
}
fs::path pickedModelsStore(){return fileAccessStore().parent_path()/L"picked-models.json";}
bool sourceAllowed(const PickedModels& picked,std::string_view source,bool localServer,SourceUse use){
 return localServer||picked.picked(source,use==SourceUse::Notes);
}
fs::path createPrivateFolder(const std::wstring& prefix,const fs::path& temp){
 auto base=temporaryFolder(temp);
#ifdef _WIN32
 for(int attempt=0;attempt<8;attempt++){auto dir=base/(prefix+randomHex(16));if(CreateDirectoryW(ioPath(dir).c_str(),nullptr))return dir;}
#else
 // 0700: other users of the machine cannot read or replace a job's request or answer.
 for(int attempt=0;attempt<8;attempt++){auto dir=base/(prefix+randomHex(16));if(mkdir(ioPath(dir).c_str(),0700)==0)return dir;}
#endif
 throw std::runtime_error("Cannot create a private folder in "+utf8(base.wstring()));
}
size_t sweepPrivateFolders(const std::wstring& prefix,std::chrono::hours age,const fs::path& temp){
 fs::path base;try{base=temporaryFolder(temp);}catch(...){return 0;}
 size_t removed=0;std::error_code error;auto cutoff=fs::file_time_type::clock::now()-age;
 for(auto it=fs::directory_iterator(base,error);!error&&it!=fs::directory_iterator();it.increment(error)){
  std::error_code e;auto name=it->path().filename().wstring();if(!name.starts_with(prefix)||!plainDirectory(it->path()))continue;
  auto time=fs::last_write_time(it->path(),e);if(!e&&time<cutoff&&fs::remove_all(ioPath(it->path()),e)>0&&!e)removed++;
 }
 return removed;
}
fs::path importJobsFolder(){auto dir=temporaryFolder({})/L"mmdhl-jobs";fs::create_directories(ioPath(dir));return dir;}
fs::path prepareImportJob(const PickedModels& picked,bool localServer,bool picker,const std::string& source,const Json& options,const fs::path& jobs){
 // On a server this game does not host, every client script is that server's: it imports
 // only files the player picked, decided before anything opens the path.
 if(!source.empty()&&!sourceAllowed(picked,source,localServer,SourceUse::Import))throw std::runtime_error("On a server you do not host, Model Hotloader imports only models chosen in its file window. Choose the model again.");
 // Sources come from Lua (or registries in data/ that Lua can write): a path to another
 // computer would make Windows sign in there. Mapped drive letters still work.
 if(props::networkPath(source))throw std::runtime_error("Model Hotloader does not import from network paths (\\\\computer\\share). Copy the model to this computer, or open it through a mapped drive letter.");
 // The request, the status and the picker's answer go through a folder of the job's own
 // in the user's temporary folder: Lua can rewrite anything under garrysmod/data.
 auto dir=createPrivateFolder(L"job-",jobs.empty()?importJobsFolder():jobs);
 try{
  writeJson(dir/L"status.json",{{"state","running"},{"stage",picker?"Select model":"Starting import"},{"stageCode",picker?"pick":"start"},{"progress",0}});
  if(!picker)writeJson(dir/L"request.json",{{"source",source},{"options",options}});
 }catch(...){std::error_code error;fs::remove_all(ioPath(dir),error);throw;}
 return dir;
}
void notePickedSource(PickedModels& picked,bool pickerJob,const Json& result){
 // The picker's answer came through the job's private folder: the player chose this file.
 if(!pickerJob||!result.is_object())return;
 auto state=result.find("state"),source=result.find("source");
 if(state!=result.end()&&state->is_string()&&*state=="selected"&&source!=result.end()&&source->is_string())picked.add(source->get<std::string>());
}
Json inspectModelNotesFor(const PickedModels& picked,bool localServer,const std::string& source){
 if(!sourceAllowed(picked,source,localServer,SourceUse::Notes))throw std::runtime_error("On a server you do not host, model terms are read only beside a model just chosen in the file window");
 return inspectModelNotes(fs::path(wide(source)));
}
}
