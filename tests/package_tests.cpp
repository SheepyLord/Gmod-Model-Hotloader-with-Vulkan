#include "packages.hpp"
#include "sharing.hpp"
#include "test_platform.hpp"
#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <map>
#include <cstring>
#include <algorithm>
#include <stdexcept>
using namespace mmd;
static void check(bool value,const char* what){if(!value)throw std::runtime_error(std::string("Package validation failed: ")+what);}
static Bytes random(size_t size,unsigned seed){std::mt19937 rng(seed);Bytes b(size);for(auto& v:b)v=(unsigned char)(rng()%7);return b;}
static void write(const fs::path& path,const Bytes& b){fs::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));}
static Bytes text(const std::string& s){return Bytes(s.begin(),s.end());}
struct Entry{std::string name;uint64_t size;uint32_t crc;Bytes data;};
// An independent reader of the written GMA, following gmad's layout.
static std::vector<Entry> readGma(const fs::path& path,Json& description){
 auto b=readFile(path);size_t p=0;
 auto take=[&](size_t n){check(p+n<=b.size(),"GMA truncated");auto at=p;p+=n;return at;};
 auto str=[&](){auto start=p;while(p<b.size()&&b[p])p++;check(p<b.size(),"GMA string");std::string s(reinterpret_cast<const char*>(b.data()+start),p-start);p++;return s;};
 check(!std::memcmp(b.data()+take(4),"GMAD",4)&&b[take(1)]==3,"GMA magic");take(16);
 check(str().empty(),"no required content");check(str()=="Test pack","GMA title");description=Json::parse(str());str();take(4);
 std::vector<Entry> entries;
 for(;;){uint32_t index;std::memcpy(&index,b.data()+take(4),4);if(!index)break;Entry e;e.name=str();std::memcpy(&e.size,b.data()+take(8),8);std::memcpy(&e.crc,b.data()+take(4),4);entries.push_back(e);}
 for(auto& e:entries){auto at=take(size_t(e.size));e.data.assign(b.begin()+at,b.begin()+at+e.size);check(crc32(e.data)==e.crc,"member CRC");}
 uint32_t trailer;auto at=take(4);std::memcpy(&trailer,b.data()+at,4);check(p==b.size(),"GMA trailing bytes");
 check(trailer==crc32(std::span(b.data(),at)),"GMA CRC");
 return entries;
}
int main(){try{
 auto base=fs::temp_directory_path()/("mmdhl-package-test-"+std::to_string(processId()));
 struct Clean{fs::path dir;~Clean(){std::error_code ec;fs::remove_all(dir,ec);}} clean{base};
 auto cache=base/L"garrysmod"/L"data"/L"mmd_hotloader";fs::create_directories(cache);
 // One character (manifest, model, two textures shared with nothing) and one static prop.
 auto png=random(300000,1),toon=random(1000,2),model=random(2500000,3),prop=random(700000,4);
 auto pngId=hash(png),toonId=hash(toon),propId=hash(prop);
 write(cache/L"textures"/wide(pngId+".png"),png);write(cache/L"textures"/wide(toonId+".png"),toon);
 auto manifest=text(Json({{"version",2},{"name","Tester"},{"textures",Json::array({{{"base",pngId},{"toon",toonId}},{{"base",pngId}}})}}).dump());
 std::string assetId=hash(manifest);
 write(cache/L"assets"/wide(assetId)/L"manifest.json",manifest);write(cache/L"assets"/wide(assetId)/L"model.bin",model);
 write(cache/L"static"/L"assets"/wide(propId+".gmdl"),prop);
 Json spec={{"title","Test pack"},{"author","Tester"},{"description","Line one\nLine two"},{"tags",{"fun","build"}},{"install","user"},{"folder","Demo"},{"fileName","Test pack: v1/2"},
  {"items",Json::array({{{"kind","character"},{"asset",assetId},{"name","Tester"},{"settings",{{"spawn",{{"scaleMultiplier",1.5}}}}}},{{"kind","static"},{"asset",propId},{"name","Chair"}},{{"kind","character"},{"asset",assetId},{"name","Duplicate"}}})}};
 PackageProgress progress;auto result=exportPackage(cache,base,spec,progress);
 check(result["file"]=="Test pack_ v1_2.gma","sanitized file name");check(result["items"]==2&&result["files"]==5,"deduplicated items and files");
 check(progress.fraction==1.f&&progress.stage=="complete","progress reaches complete");
 auto gma=cache/L"exports"/L"Test pack_ v1_2.gma";check(fs::is_regular_file(gma),"export written");
 for(auto& e:fs::directory_iterator(cache/L"exports"))check(e.path().extension()==L".gma","no temporary files remain");
 Json description;auto entries=readGma(gma,description);
 check(description["type"]=="model"&&description["tags"]==Json({"fun","build"})&&description["description"]=="Line one\nLine two","Workshop description");
 // Every member must pass GMod's addon whitelist (data_static/*.json and *.dat).
 static const std::regex allowed("data_static/mmdhl/(packages/[0-9a-f]{32}\\.json|files/[0-9a-f]{64}\\.dat)");
 for(auto& e:entries)check(std::regex_match(e.name,allowed),"whitelisted member");
 check(entries.size()==6&&entries[0].name.starts_with("data_static/mmdhl/packages/"),"manifest plus five files");
 auto package=Json::parse(entries[0].data.begin(),entries[0].data.end());
 check(package["format"]=="mmdhl-package"&&package["version"]==1&&package["install"]=="user"&&package["folder"]=="Demo"&&package["title"]=="Test pack","package header");
 check(entries[0].name=="data_static/mmdhl/packages/"+package["id"].get<std::string>()+".json"&&package["id"]==result["packageId"],"package identity");
 check(package["items"].size()==2&&package["items"][0]["settings"]["spawn"]["scaleMultiplier"]==1.5&&package["items"][1]["files"]==Json({"static/assets/"+propId+".gmdl"}),"items keep settings and files");
 std::map<std::string,Bytes> originals={{"assets/"+assetId+"/manifest.json",manifest},{"assets/"+assetId+"/model.bin",model},{"textures/"+pngId+".png",png},{"textures/"+toonId+".png",toon},{"static/assets/"+propId+".gmdl",prop}};
 check(package["files"].size()==originals.size(),"file listing");
 for(auto& [path,raw]:originals){
  auto& f=package["files"][path];auto sha=f["sha256"].get<std::string>();check(sha==hash(raw)&&f["size"]==raw.size(),"file metadata");
  auto member=std::find_if(entries.begin(),entries.end(),[&](auto& e){return e.name=="data_static/mmdhl/files/"+sha+".dat";});check(member!=entries.end()&&member->size==f["packed"],"packed member");
  // Readers install through BeginSharedFile; its wire-size bound and unpacking must accept the packet.
  check(member->size<=raw.size()+80+8*((raw.size()+SharedBlockSize-1)/SharedBlockSize),"packet within the transfer bound");
  check(unpackSharedFile(member->data,raw.size(),sha)==raw,"lossless round trip");
 }
 auto found=readAddonPackages(base,Json::array({"garrysmod/data/mmd_hotloader/exports/Test pack_ v1_2.gma","missing.gma",42}));
 check(found.size()==3&&found[0]==Json::array({package["id"]})&&found[1].empty()&&found[2].empty(),"addon scan finds the package");
 check(!fs::exists(cache/L"assets"/wide(assetId)/L"model.bin.share2"),"export leaves no transfer sidecars");
 // Rejections: nothing selected, unknown tags, missing models; none leaves partial files behind.
 for(auto broken:{Json({{"title","x"},{"items",Json::array()}}),Json({{"title","x"},{"tags",{"weapon"}},{"items",spec["items"]}}),Json({{"title","x"},{"items",Json::array({{{"kind","static"},{"asset",std::string(64,'a')}}})}}),Json({{"title",""},{"items",spec["items"]}})}){
  bool rejected=false;try{PackageProgress p;exportPackage(cache,base,broken,p);}catch(...){rejected=true;}check(rejected,"invalid export rejected");
 }
 for(auto& e:fs::directory_iterator(cache/L"exports"))check(e.path().filename()==L"Test pack_ v1_2.gma","failed exports leave no files");
 check(packageFileName("CON")=="_CON"&&packageFileName("...")=="model-package"&&packageFileName(std::string("\xe6\x97\xa5\xe6\x9c\xac Pack"))=="Pack","file name sanitizing");
 std::cout<<"PASS: GMA export (whitelisted members, CRCs, Workshop header), deduplication, lossless packets, addon scan and rejected exports\n";
 // Installers refuse manifests over PackageManifestLimit; an export must never
 // produce one. Nine models whose fit records pad the manifest to an exact size.
 {
  std::vector<std::string> assets;
  for(int k=0;k<9;k++){auto m=text(Json({{"version",2},{"name","Pad "+std::to_string(k)},{"textures",Json::array()}}).dump());auto id=hash(m);
   write(cache/L"assets"/wide(id)/L"manifest.json",m);write(cache/L"assets"/wide(id)/L"model.bin",random(2000,10+k));assets.push_back(id);}
  auto padded=[&](size_t total){Json items=Json::array();for(size_t k=0;k<assets.size();k++){size_t share=total/assets.size()+(k<total%assets.size()?1:0);
    items.push_back({{"kind","character"},{"asset",assets[k]},{"name","Pad"},{"fit",{{"pad",std::string(share,'x')}}}});}
   return Json({{"title","Test pack"},{"fileName","Limit"},{"items",items}});};
  auto manifestSize=[&](const fs::path& gma){Json header;auto entries=readGma(gma,header);return entries.at(0).size;};
  auto limitGma=cache/L"exports"/L"Limit.gma";
  PackageProgress measure;exportPackage(cache,base,padded(0),measure);auto baseSize=manifestSize(limitGma);
  PackageProgress exact;exportPackage(cache,base,padded(PackageManifestLimit-baseSize),exact);
  check(manifestSize(limitGma)==PackageManifestLimit,"a manifest of exactly the installable size exports");
  auto kept=readFile(limitGma);
  for(size_t over:{size_t(1),size_t(100000)}){
   PackageProgress p;bool rejected=false;try{exportPackage(cache,base,padded(PackageManifestLimit-baseSize+over),p);}catch(const std::exception&){rejected=true;}
   check(rejected,"a manifest over the installable size is refused");
   check(readFile(limitGma)==kept,"a refused export leaves the previous package in place");
   for(auto& e:fs::directory_iterator(cache/L"exports"))check(e.path().extension()==L".gma","a refused export leaves no partial files");
   if(over>1000)check(p.stage=="collecting","a clearly oversized manifest is refused before packing");
  }
  std::cout<<"PASS: package manifests stay within the installer's "<<PackageManifestLimit<<" byte limit\n";
 }
 // Installers (workshop.lua fileLimit) cap each file by kind and refuse any other path.
 {
  auto id=std::string(64,'a'),rig=std::string(32,'b');
  check(packageInstallLimit("assets/"+id+"/manifest.json")==64ull<<20&&packageInstallLimit("assets/"+id+"/model.bin")==1ull<<30,"model file limits");
  check(packageInstallLimit("textures/"+id+".png")==256ull<<20&&packageInstallLimit("static/assets/"+id+".gmdl")==256ull<<20,"texture and prop limits");
  for(auto path:{"assets/"+id+"/materials-v5.gma","rigs/"+rig+"/rig.json","textures/"+std::string(64,'A')+".png","assets/"+id+"/../model.bin"})check(packageInstallLimit(path)==0,"paths installers refuse");
 }
 // They also refuse a file that inflates more than PackageExpansionLimit times its packet
 // (above PackageExpansionFloor): a file that compresses that well is stored uncompressed.
 {
  auto m=text(Json({{"version",2},{"name","Zeros"},{"textures",Json::array()}}).dump());auto id=hash(m);Bytes zeros(3u<<20,0);
  write(cache/L"assets"/wide(id)/L"manifest.json",m);write(cache/L"assets"/wide(id)/L"model.bin",zeros);
  PackageProgress p;exportPackage(cache,base,Json({{"title","Test pack"},{"fileName","Zeros"},{"items",Json::array({{{"kind","character"},{"asset",id},{"name","Zeros"}}})}}),p);
  Json header;auto entries=readGma(cache/L"exports"/L"Zeros.gma",header);auto package=Json::parse(entries[0].data.begin(),entries[0].data.end());
  auto sha=hash(zeros);auto packedSize=package["files"]["assets/"+id+"/model.bin"]["packed"].get<uint64_t>();
  check(zeros.size()<=std::max<uint64_t>(PackageExpansionFloor,packedSize*PackageExpansionLimit),"a file past the expansion bound is stored within it");
  auto member=std::find_if(entries.begin(),entries.end(),[&](auto& e){return e.name=="data_static/mmdhl/files/"+sha+".dat";});
  check(member!=entries.end()&&unpackSharedFile(member->data,zeros.size(),sha)==zeros,"the stored file round-trips");
  auto manifestSha=hash(m);auto small=std::find_if(entries.begin(),entries.end(),[&](auto& e){return e.name=="data_static/mmdhl/files/"+manifestSha+".dat";});
  check(small!=entries.end()&&unpackSharedFile(small->data,m.size(),manifestSha)==m,"other files keep their packets");
  std::cout<<"PASS: exports stay within the installer's per-kind path limits and expansion bound\n";
 }
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
