#include "picked_models.hpp"
#include "file_access.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
namespace mmd {
namespace {
constexpr size_t MaxPicked=4096;
// Paths compare as Windows compares names: normalized, case-insensitively.
std::wstring key(std::string_view source){
 if(source.empty())return {};
 try{return fs::path(wide(source)).lexically_normal().wstring();}catch(...){return {};}
}
bool same(const std::wstring& a,const std::wstring& b){return a.size()==b.size()&&(a.empty()||CompareStringOrdinal(a.data(),int(a.size()),b.data(),int(b.size()),TRUE)==CSTR_EQUAL);}
bool listed(const std::vector<std::wstring>& list,const std::wstring& path){return !path.empty()&&std::any_of(list.begin(),list.end(),[&](const std::wstring& p){return same(p,path);});}
std::wstring randomHex(size_t bytes){
 std::vector<unsigned char> b(bytes);if(BCryptGenRandom(nullptr,b.data(),ULONG(b.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("No random numbers");
 std::wstring s;for(auto c:b){s.push_back(L"0123456789abcdef"[c>>4]);s.push_back(L"0123456789abcdef"[c&15]);}return s;
}
fs::path temporaryFolder(const fs::path& temp){
 if(!temp.empty())return temp;
 wchar_t t[MAX_PATH+1]{};if(!GetTempPathW(MAX_PATH+1,t))throw std::runtime_error("No temporary folder");return t;
}
bool plainDirectory(const fs::path& path){auto a=GetFileAttributesW(ioPath(path).c_str());return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY)&&!(a&FILE_ATTRIBUTE_REPARSE_POINT);}
}
PickedModels::PickedModels(fs::path file):store(std::move(file)){
 try{
  std::error_code error;if(!fs::is_regular_file(ioPath(store),error)||fs::file_size(ioPath(store),error)>(4u<<20))return;
  auto j=readJson(store);if(!j.is_object()||j.value("schema",0)!=1||!j.contains("paths")||!j["paths"].is_array())return;
  for(auto& p:j["paths"])if(p.is_string()){auto k=key(p.get<std::string>());if(!k.empty()&&!listed(paths,k))paths.push_back(k);}
  if(paths.size()>MaxPicked)paths.erase(paths.begin(),paths.end()-MaxPicked);
 }catch(...){paths.clear();}
}
bool PickedModels::picked(std::string_view source,bool thisSession) const {return listed(thisSession?session:paths,key(source));}
bool PickedModels::add(std::string_view source){
 auto k=key(source);if(k.empty())return true;
 if(!listed(session,k))session.push_back(k);
 // Newest last; the oldest go first past the limit.
 paths.erase(std::remove_if(paths.begin(),paths.end(),[&](const std::wstring& p){return same(p,k);}),paths.end());paths.push_back(k);
 if(paths.size()>MaxPicked)paths.erase(paths.begin(),paths.end()-MaxPicked);
 try{Json list=Json::array();for(auto& p:paths)list.push_back(utf8(p));fs::create_directories(ioPath(store.parent_path()));writeJson(store,{{"schema",1},{"paths",list}});return true;}catch(...){return false;}
}
fs::path pickedModelsStore(){return fileAccessStore().parent_path()/L"picked-models.json";}
bool sourceAllowed(const PickedModels& picked,std::string_view source,bool localServer,SourceUse use){
 return localServer||picked.picked(source,use==SourceUse::Notes);
}
fs::path createPrivateFolder(const std::wstring& prefix,const fs::path& temp){
 auto base=temporaryFolder(temp);
 for(int attempt=0;attempt<8;attempt++){auto dir=base/(prefix+randomHex(16));if(CreateDirectoryW(ioPath(dir).c_str(),nullptr))return dir;}
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
}
