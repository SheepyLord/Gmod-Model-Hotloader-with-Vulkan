#include "packages.hpp"
#include "props/network_path.hpp"
#include "sharing.hpp"
#include "release.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <set>
namespace mmd {
namespace {
const std::array<uint32_t,256> CrcTable=[]{std::array<uint32_t,256> t{};for(uint32_t i=0;i<256;i++){uint32_t c=i;for(int k=0;k<8;k++)c=(c>>1)^((0u-(c&1))&0xedb88320u);t[i]=c;}return t;}();
// Workshop accepts at most two of these tags for an addon of type "model".
const std::set<std::string> WorkshopTags={"fun","roleplay","scenic","movie","realism","cartoon","water","comic","build"};
std::string trimmed(const std::string& s){auto first=s.find_first_not_of(" \t\r\n");if(first==std::string::npos)return {};return s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);}
std::string text(const Json& spec,const char* key,size_t maximum,bool multiline,bool required){
 auto it=spec.find(key);std::string name=std::string("Package ")+key;
 if(it==spec.end()||it->is_null()){if(required)throw std::runtime_error(name+" is required");return {};}
 if(!it->is_string())throw std::runtime_error(name+" must be text");
 auto value=trimmed(it->get<std::string>());
 if(required&&value.empty())throw std::runtime_error(name+" is required");
 if(value.size()>maximum)throw std::runtime_error(name+" is too long");
 for(unsigned char c:value)if(c==0x7f||(c<0x20&&!(multiline&&(c=='\n'||c=='\r'||c=='\t'))))throw std::runtime_error(name+" contains control characters");
 wide(value);return value;
}
std::string randomId(){
 unsigned char bytes[16];if(BCryptGenRandom(nullptr,bytes,sizeof bytes,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot create a package identity");
 std::string s;for(auto c:bytes){s.push_back("0123456789abcdef"[c>>4]);s.push_back("0123456789abcdef"[c&15]);}return s;
}
std::string readable(const fs::path& path){
 auto s=path.wstring();
 if(s.starts_with(L"\\\\?\\UNC\\"))s=L"\\\\"+s.substr(8);else if(s.starts_with(L"\\\\?\\"))s=s.substr(4);
 return utf8(s);
}
// Per-model extras are opaque here; the importing Lua validates them again.
Json extra(const Json& item,const char* key,size_t maximum){
 auto it=item.find(key);if(it==item.end()||!it->is_object())return Json();
 if(it->dump().size()>maximum)throw std::runtime_error(std::string("Model ")+key+" data is too large to export");
 return *it;
}
struct Output{
 std::ofstream stream;uint32_t crc=0;
 void put(const void* data,size_t size){if(!size)return;stream.write(static_cast<const char*>(data),std::streamsize(size));if(!stream)throw std::runtime_error("Cannot write the package file (is the disk full?)");crc=crc32({static_cast<const unsigned char*>(data),size},crc);}
 template<class T>void value(T v){put(&v,sizeof v);}
 void text(const std::string& s){put(s.data(),s.size());value<char>(0);}
};
struct Reader{
 std::istream& in;
 template<class T>T value(){T v;if(!in.read(reinterpret_cast<char*>(&v),sizeof v))throw std::runtime_error("Truncated GMA");return v;}
 std::string text(){std::string s;for(char c;;){if(!in.get(c))throw std::runtime_error("Truncated GMA");if(!c)return s;if(s.size()>=8192)throw std::runtime_error("Invalid GMA string");s.push_back(c);}}
};
// engine.GetAddons() reports GMA paths relative to garrysmod/ (addons/, cache/workshop/)
// or to steamapps/workshop/ (content/4000/<id>/).
fs::path resolveAddon(const fs::path& root,const std::string& file){
 // The list comes from Lua: a path to another computer would make Windows sign in there.
 if(props::networkPath(file))return {};
 auto path=fs::path(wide(file));std::vector<fs::path> candidates;
 if(path.is_absolute())candidates.push_back(path);
 else{
  candidates.push_back(root/L"garrysmod"/path);candidates.push_back(root/path);
  if(root.parent_path().filename()==L"common")candidates.push_back(root.parent_path().parent_path()/L"workshop"/path);
  candidates.push_back(root/L"steamapps"/L"workshop"/path);
 }
 for(auto& candidate:candidates){std::error_code ec;if(candidate.extension()==L".gma"&&fs::is_regular_file(candidate,ec))return candidate;}
 return {};
}
}
uint32_t crc32(std::span<const unsigned char> bytes,uint32_t crc){crc=~crc;for(auto b:bytes)crc=CrcTable[(crc^b)&255]^(crc>>8);return ~crc;}
std::string packageFileName(const std::string& proposed){
 std::string name;
 for(unsigned char c:proposed){
  bool keep=std::isalnum(c)||c=='-'||c=='_'||c=='.'||c=='('||c==')'||c==' ';
  char out=keep&&c<0x80?char(c):'_';
  if(out=='_'&&!name.empty()&&name.back()=='_')continue;
  name.push_back(out);
 }
 auto strip=[](std::string s){while(!s.empty()&&(s.back()=='.'||s.back()==' '||s.back()=='_'))s.pop_back();auto first=s.find_first_not_of(" ._");return first==std::string::npos?std::string():s.substr(first);};
 name=strip(name);if(name.size()>80)name=strip(name.substr(0,80));
 if(name.size()<2)name="model-package";
 static const std::set<std::string> reserved={"con","prn","aux","nul","com1","com2","com3","com4","com5","com6","com7","com8","com9","lpt1","lpt2","lpt3","lpt4","lpt5","lpt6","lpt7","lpt8","lpt9"};
 auto stem=name.substr(0,name.find('.'));std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c){return char(std::tolower(c));});
 if(reserved.contains(stem))name="_"+name;
 return name;
}
uint64_t packageInstallLimit(const std::string& path){
 static const std::regex asset("assets/[a-f0-9]{64}/(manifest\\.json|model\\.bin)"),texture("textures/[a-f0-9]{64}\\.png"),prop("static/assets/[a-f0-9]{64}\\.gmdl");
 std::smatch match;
 if(std::regex_match(path,match,asset))return match[1]=="manifest.json"?64ull<<20:1ull<<30;
 if(std::regex_match(path,texture)||std::regex_match(path,prop))return 256ull<<20;
 return 0;
}
Json exportPackage(const fs::path& cache,const fs::path& gameRoot,const Json& spec,PackageProgress& progress){
 auto stage=[&](const char* name,float fraction){{std::lock_guard lock(progress.lock);progress.stage=name;}progress.fraction=fraction;if(progress.cancel)throw std::runtime_error("Export cancelled");};
 if(!spec.is_object())throw std::runtime_error("Invalid package description");
 auto title=text(spec,"title",128,false,true),author=text(spec,"author",128,false,false),description=text(spec,"description",8000,true,false),folder=text(spec,"folder",80,false,false);
 auto install=spec.value("install",std::string("workshop"));if(install!="workshop"&&install!="user")throw std::runtime_error("Unknown package install mode");
 Json tags=Json::array();
 if(spec.contains("tags")){
  if(!spec["tags"].is_array()||spec["tags"].size()>2)throw std::runtime_error("Choose at most two Workshop tags");
  for(auto& tag:spec["tags"]){if(!tag.is_string()||!WorkshopTags.contains(tag.get<std::string>()))throw std::runtime_error("Unknown Workshop tag");if(std::find(tags.begin(),tags.end(),tag)==tags.end())tags.push_back(tag);}
 }
 if(!spec.contains("items")||!spec["items"].is_array()||spec["items"].empty()||spec["items"].size()>1000)throw std::runtime_error("Select between 1 and 1000 models to export");
 auto name=packageFileName(spec.value("fileName",title));
 stage("collecting",0);
 Json items=Json::array();std::map<std::string,Json> files;std::map<std::string,std::string> owners;std::set<std::string> seen;
 for(auto& source:spec["items"]){
  if(!source.is_object())throw std::runtime_error("Invalid model in the export list");
  auto kind=source.value("kind",std::string()),asset=source.value("asset",std::string());
  if((kind!="character"&&kind!="static")||!validId(asset))throw std::runtime_error("Invalid model in the export list");
  if(!seen.insert(kind+":"+asset).second)continue;
  auto label=text(source,"name",100,false,false);
  Json item={{"kind",kind},{"asset",asset},{"name",label.empty()?asset.substr(0,12):label},{"files",Json::array()}};
  // Terms of use travel with the model: its readme text, embedded comment and VRM licence.
  for(auto key:{"settings","arms","fit","terms"}){auto value=extra(source,key,std::string(key)=="fit"?(512u<<10):std::string(key)=="terms"?(256u<<10):(64u<<10));if(!value.is_null())item[key]=value;}
  if(kind=="character"){
   Json manifest;try{manifest=sharedManifest(cache,asset,Json::array(),true);}catch(const std::exception& e){throw std::runtime_error("Model "+item["name"].get<std::string>()+" cannot be exported: "+e.what());}
   for(auto& file:manifest.at("files")){auto path=file.at("path").get<std::string>();item["files"].push_back(path);files[path]={{"size",file.at("size")},{"sha256",file.at("sha256")}};}
  }else{
   auto path="static/assets/"+asset+".gmdl";std::error_code ec;auto size=fs::file_size(sharedPath(cache,path),ec);
   if(ec)throw std::runtime_error("Prop "+item["name"].get<std::string>()+" is no longer in the cache; refresh the library");
   item["files"].push_back(path);files[path]={{"size",size},{"sha256",asset}};
  }
  for(auto& path:item["files"])owners.emplace(path.get<std::string>(),(kind=="character"?"Model ":"Prop ")+item["name"].get<std::string>());
  items.push_back(std::move(item));
 }
 // Installers refuse a file over its kind's limit, and packages over
 // PackageTotalLimit: refuse such an export before packing.
 uint64_t listed=0;
 for(auto& [path,file]:files){
  auto size=file["size"].get<uint64_t>(),limit=packageInstallLimit(path);listed+=size;
  if(size<=limit)continue;
  auto what=path.ends_with("manifest.json")?"model description":path.ends_with("model.bin")?"model data":path.starts_with("textures/")?"texture":"prop data";
  throw std::runtime_error(owners[path]+" cannot be exported: "+(limit?std::string("its ")+what+" ("+std::to_string(size>>20)+" MB) is larger than the "+std::to_string(limit>>20)+" MB that players can install":std::string("it lists a file that players cannot install")));
 }
 if(listed>PackageTotalLimit)throw std::runtime_error("The selected models hold more than the "+std::to_string(PackageTotalLimit>>30)+" GB that players can install from one package; export fewer models per package");
 // The manifest lists every file with its packed size. Installers refuse one over
 // PackageManifestLimit, so refuse the export instead: before packing with the
 // smallest packed size a file can have (80 bytes), and exactly afterwards.
 auto id=randomId();auto created=int64_t(std::time(nullptr));
 auto describe=[&](const std::function<uint64_t(const std::string&)>& packedSize){
  Json listing=Json::object();for(auto& [path,file]:files){auto sha=file["sha256"].get<std::string>();listing[path]={{"size",file["size"]},{"sha256",sha},{"packed",packedSize(sha)}};}
  Json package={{"format","mmdhl-package"},{"version",1},{"id",id},{"title",title},{"author",author},{"description",description},{"created",created},{"install",install},
   {"generator",{{"release",MMDHL_RELEASE},{"build",MMDHL_BUILD_ID}}},{"items",items},{"files",listing}};
  if(!folder.empty())package["folder"]=folder;
  auto text=package.dump();
  if(text.size()>PackageManifestLimit)throw std::runtime_error("The package description (model settings, fits and terms of use) exceeds the 4 MB that players can install; export fewer models per package");
  return text;
 };
 describe([](const std::string&){return uint64_t(80);});
 // Content-addressed storage: bytes shared by several models (textures) are stored once.
 std::map<std::string,std::pair<std::string,uint64_t>> blobs;uint64_t total=0;
 for(auto& [path,file]:files){auto sha=file["sha256"].get<std::string>();if(!blobs.contains(sha)){auto size=file["size"].get<uint64_t>();blobs[sha]={path,size};total+=size;}}
 auto directory=cache/L"exports";fs::create_directories(directory);
 auto target=directory/wide(name+".gma"),body=directory/wide("."+name+".body.part"),part=directory/wide("."+name+".gma.part");
 struct Cleanup{fs::path a,b;~Cleanup(){std::error_code ec;fs::remove(a,ec);fs::remove(b,ec);}} cleanup{body,part};
 std::map<std::string,std::pair<uint64_t,uint32_t>> packed;
 {
  std::ofstream out(body,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot create the export file");
  uint64_t done=0;
  for(auto& [sha,source]:blobs){
   stage("packing",total?float(.92*double(done)/double(total)):0.f);
   auto raw=readFile(sharedPath(cache,source.first));
   if(raw.size()!=source.second||hash(raw)!=sha)throw std::runtime_error("A model file changed during the export; try again");
   auto bytes=packSharedBytes(raw,sha);
   // Installers refuse a file that inflates more than PackageExpansionLimit times its
   // packet (a decompression bomb): store one that compresses that well uncompressed.
   if(raw.size()>std::max<uint64_t>(PackageExpansionFloor,uint64_t(bytes.size())*PackageExpansionLimit))bytes=packSharedBytes(raw,sha,false);
   out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));if(!out)throw std::runtime_error("Cannot write the package file (is the disk full?)");
   packed[sha]={bytes.size(),crc32(bytes)};done+=raw.size();
  }
 }
 stage("writing",.92f);
 auto manifest=describe([&](const std::string& sha){return packed.at(sha).first;});auto manifestBytes=std::span(reinterpret_cast<const unsigned char*>(manifest.data()),manifest.size());
 // gmad's header: the description is JSON carrying the Workshop type and tags.
 Json workshop={{"description",description.empty()?title:description},{"type","model"},{"tags",tags}};
 Output gma;gma.stream.open(part,std::ios::binary|std::ios::trunc);if(!gma.stream)throw std::runtime_error("Cannot create the export file");
 gma.put("GMAD",4);gma.value<uint8_t>(3);gma.value<uint64_t>(0);gma.value<uint64_t>(uint64_t(std::time(nullptr)));gma.text("");
 gma.text(title);gma.text(workshop.dump());gma.text(author.empty()?"Model Hotloader":author);gma.value<int32_t>(1);
 uint32_t index=0;
 auto entry=[&](const std::string& path,uint64_t size,uint32_t crc){gma.value<uint32_t>(++index);gma.text(path);gma.value<uint64_t>(size);gma.value<uint32_t>(crc);};
 entry("data_static/mmdhl/packages/"+id+".json",manifest.size(),crc32(manifestBytes));
 for(auto& [sha,info]:packed)entry("data_static/mmdhl/files/"+sha+".dat",info.first,info.second);
 gma.value<uint32_t>(0);
 gma.put(manifest.data(),manifest.size());
 {
  std::ifstream in(body,std::ios::binary);if(!in)throw std::runtime_error("Cannot read the packed models back");
  Bytes block(1u<<20);uint64_t copied=0,size=0;for(auto& [sha,info]:packed)size+=info.first;
  while(copied<size){
   in.read(reinterpret_cast<char*>(block.data()),std::streamsize(block.size()));auto count=size_t(in.gcount());if(!count)throw std::runtime_error("The packed models were truncated");
   gma.put(block.data(),count);copied+=count;stage("writing",float(.92+.08*double(copied)/double(size)));
  }
 }
 uint32_t crc=gma.crc;gma.stream.write(reinterpret_cast<const char*>(&crc),sizeof crc);gma.stream.close();
 if(!gma.stream)throw std::runtime_error("Cannot write the package file (is the disk full?)");
 if(!MoveFileExW(part.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace "+name+".gma; close any program using it and export again");
 stage("complete",1);
 std::error_code ec;
 Json result={{"file",name+".gma"},{"path",readable(target)},{"folder",readable(directory)},{"bytes",fs::file_size(target,ec)},{"packageId",id},{"items",items.size()},{"files",packed.size()},{"rawBytes",total},{"title",title}};
 auto publisher=gameRoot/L"bin"/L"gmpublish.exe";if(fs::is_regular_file(publisher,ec))result["gmpublish"]=readable(publisher);
 return result;
}
Json readAddonPackages(const fs::path& gameRoot,const Json& files){
 static const std::regex member("data_static/mmdhl/packages/([0-9a-f]{32})\\.json");
 Json out=Json::array();
 if(!files.is_array())return out;
 for(auto& item:files){
  Json ids=Json::array();
  try{
   auto path=item.is_string()?resolveAddon(gameRoot,item.get<std::string>()):fs::path();
   std::ifstream in(path.empty()?fs::path():ioPath(path),std::ios::binary);
   if(in){
    Reader r{in};char magic[4];if(!in.read(magic,4)||std::memcmp(magic,"GMAD",4))throw std::runtime_error("Not a GMA");
    auto version=r.value<uint8_t>();if(version>3)throw std::runtime_error("Unknown GMA version");
    r.value<uint64_t>();r.value<uint64_t>();
    if(version>1)for(int i=0;i<256&&!r.text().empty();i++){}
    r.text();r.text();r.text();r.value<int32_t>();
    for(int i=0;i<500000;i++){
     if(!r.value<uint32_t>())break;
     auto name=r.text();r.value<uint64_t>();r.value<uint32_t>();
     std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
     std::smatch match;if(std::regex_match(name,match,member))ids.push_back(match[1].str());
    }
   }
  }catch(...){}
  out.push_back(ids);
 }
 return out;
}
}
