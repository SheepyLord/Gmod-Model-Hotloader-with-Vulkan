#include "sharing.hpp"
#include <regex>
#include <set>
#include <cstring>
#include <compressapi.h>
#include <fstream>
#include <mutex>
#include <map>
namespace mmd {
namespace {
constexpr char PackedMagic[]="MMDPACK2";
constexpr size_t PackedHeader=80;
struct Codec {
 COMPRESSOR_HANDLE handle=nullptr;bool decode=false;
 explicit Codec(bool decoding):decode(decoding){
  if(!(decode?CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&handle):CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&handle)))throw std::runtime_error("Windows lossless compression is unavailable");
 }
 ~Codec(){if(decode)CloseDecompressor(handle);else CloseCompressor(handle);}
};
template<class T>void append(Bytes& out,T value){auto p=reinterpret_cast<const unsigned char*>(&value);out.insert(out.end(),p,p+sizeof(value));}
template<class T>T take(std::span<const unsigned char> bytes,size_t& at){if(at>bytes.size()||sizeof(T)>bytes.size()-at)throw std::runtime_error("Truncated model transfer");T out;std::memcpy(&out,bytes.data()+at,sizeof(out));at+=sizeof(out);return out;}
bool headerMatches(std::span<const unsigned char> bytes,uint64_t size,const std::string& digest){
 if(bytes.size()<PackedHeader||std::memcmp(bytes.data(),PackedMagic,8)||digest.size()!=64)return false;
 size_t at=8;return take<uint64_t>(bytes,at)==size&&!std::memcmp(bytes.data()+16,digest.data(),64);
}
}
// Blocks that do not shrink are stored raw, so the packet never exceeds the
// bound BeginSharedFile accepts: 80 header bytes plus 8 per block.
Bytes packSharedBytes(std::span<const unsigned char> raw,const std::string& digest,bool compress){
 if(!validId(digest)||raw.size()>UINT32_MAX)throw std::runtime_error("Invalid model transfer metadata");
 uint64_t size=raw.size();Bytes output;output.insert(output.end(),PackedMagic,PackedMagic+8);append(output,size);output.insert(output.end(),digest.begin(),digest.end());
 Codec codec(false);Bytes compressed(SharedBlockSize+65536);
 for(size_t at=0;at<raw.size();){
  auto count=uint32_t(std::min<size_t>(SharedBlockSize,raw.size()-at));SIZE_T written=0;
  bool encoded=compress&&Compress(codec.handle,raw.data()+at,count,compressed.data(),compressed.size(),&written)&&written<count;
  append(output,count);append(output,encoded?uint32_t(written):(count|0x80000000u));
  auto data=encoded?compressed.data():raw.data()+at;output.insert(output.end(),data,data+(encoded?written:count));at+=count;
 }
 if(output.size()>UINT32_MAX)throw std::runtime_error("Transfer exceeds Source's 4 GiB file offset format");
 return output;
}
// The file's NTFS index: an atomic replace (writeAtomic) always installs a new
// file, even within one tick of the write-time clock. 0 when unavailable.
static uint64_t fileIndex(const fs::path& path){
 HANDLE file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE)return 0;BY_HANDLE_FILE_INFORMATION info{};bool ok=GetFileInformationByHandle(file,&info)!=0;CloseHandle(file);
 return ok?uint64_t(info.nFileIndexHigh)<<32|info.nFileIndexLow:0;
}
fs::path packSharedFile(const fs::path& cache,const std::string& relative,uint64_t size,const std::string& digest){
 if(!validId(digest)||size>UINT32_MAX)throw std::runtime_error("Invalid model transfer metadata");
 // Multiple clients share this immutable sidecar. The lock is taken only by
 // export workers, never by the engine thread or physics pool.
 static std::mutex packing;std::lock_guard lock(packing);
 auto source=sharedPath(cache,relative),packed=source;packed+=L".share2";
 if(!fs::is_regular_file(source)||fs::file_size(source)!=size)throw std::runtime_error("Shared source changed; request a fresh manifest");
 // A sidecar is reused once it unpacked to the digest in this process: a
 // damaged one behind a valid header would otherwise fail every retry. Its
 // write time alone cannot tell a replacement written in the same clock tick.
 struct Verified {fs::file_time_type stamp;uint64_t file;uint64_t size;std::string digest;};
 static std::map<std::wstring,Verified> verified;
 {std::error_code error;auto stamp=fs::last_write_time(packed,error);
  if(!error){auto it=verified.find(packed.native());auto file=fileIndex(packed);if(it!=verified.end()&&file&&it->second.file==file&&it->second.stamp==stamp&&it->second.size==size&&it->second.digest==digest)return packed;
   try{auto previous=readFile(packed);if(headerMatches(previous,size,digest)){unpackSharedFile(previous,size,digest);verified[packed.native()]={stamp,file,size,digest};return packed;}}catch(const std::exception&){}}}
 auto raw=readFile(source);if(hash(raw)!=digest)throw std::runtime_error("Shared source failed SHA256 verification");
 writeAtomic(packed,packSharedBytes(raw,digest));
 std::error_code error;auto stamp=fs::last_write_time(packed,error);if(!error)verified[packed.native()]={stamp,fileIndex(packed),size,digest};
 return packed;
}
Bytes unpackSharedFile(std::span<const unsigned char> packet,uint64_t size,const std::string& digest){
 if(size>UINT32_MAX||!headerMatches(packet,size,digest))throw std::runtime_error("Invalid compressed model transfer header");
 size_t at=PackedHeader;Bytes raw;Codec codec(true);
 while(raw.size()<size){
  auto count=take<uint32_t>(packet,at),stored=take<uint32_t>(packet,at);bool uncompressed=(stored&0x80000000u)!=0;stored&=0x7fffffffu;
  if(!count||count>SharedBlockSize||count>size-raw.size()||!stored||stored>count||stored>packet.size()-at||(uncompressed&&stored!=count))throw std::runtime_error("Invalid compressed model transfer block");
  auto begin=raw.size();raw.resize(begin+count);
  if(uncompressed)std::memcpy(raw.data()+begin,packet.data()+at,count);
  else{SIZE_T written=0;if(!Decompress(codec.handle,packet.data()+at,stored,raw.data()+begin,count,&written)||written!=count)throw std::runtime_error("Corrupt compressed model transfer");}
  at+=stored;
 }
 if(at!=packet.size()||hash(raw)!=digest)throw std::runtime_error("Shared file failed SHA256 verification");
 return raw;
}
std::string mountablePackage(const fs::path& cache,const std::string& relative){
 auto source=sharedPath(cache,relative);
 if(!relative.ends_with(".gma")||!fs::is_regular_file(source))throw std::runtime_error("Model archive is not available yet");
 if(source.native().size()<240)return "data/mmd_hotloader/"+relative;
 // Source's filesystem does not accept extended Windows paths. Give it a
 // shorter hard link while keeping transfer identities and hashes unchanged.
 auto first=relative.find('/'),last=relative.rfind('/');auto kind=relative.substr(0,first),id=relative.substr(first+1,last-first-1);
 registerShortName(cache,kind,id);
 auto alias=fs::path(L"names")/wide(kind)/wide(id.substr(0,16)+"-"+relative.substr(last+1));
 auto destination=cache/alias;
 if(destination.native().size()>=250)throw std::runtime_error("Game installation path is too long for Source to mount models");
 if(fs::exists(destination)&&!fs::equivalent(source,destination)&&hash(readFile(source))!=hash(readFile(destination)))throw std::runtime_error("Model mount alias conflicts with another archive");
 if(!fs::exists(destination))fs::create_hard_link(source,destination);
 retainCacheFiles(cache,{alias});
 return "data/mmd_hotloader/"+utf8(alias.generic_wstring());
}
fs::path sharedPath(const fs::path& cache,const std::string& relative){
 static const std::regex allowed("(assets/[a-f0-9]{64}/(manifest\\.json|model\\.bin|materials-v5\\.gma)|textures/[a-f0-9]{64}\\.png|rigs/[a-f0-9]{32}/(rig\\.json|carrier\\.gma)|static/assets/[a-f0-9]{64}\\.gmdl)");
 if(!std::regex_match(relative,allowed))throw std::runtime_error("Shared package contains a forbidden path");
 auto path=(cache/wide(relative)).lexically_normal();auto rel=path.lexically_relative(cache.lexically_normal());if(rel.empty()||rel.is_absolute()||*rel.begin()==L"..")throw std::runtime_error("Shared path escaped cache");return path;
}
void validateSharedFile(const std::string& relative,std::span<const unsigned char> bytes){
 if(!relative.ends_with(".gma"))return;
 if(bytes.size()<26||std::memcmp(bytes.data(),"GMAD",4)||bytes[4]!=3)throw std::runtime_error("Invalid shared GMA header");size_t p=21;
 auto text=[&](){size_t start=p;while(p<bytes.size()&&bytes[p])++p;if(p==bytes.size())throw std::runtime_error("Invalid shared GMA string");std::string out(reinterpret_cast<const char*>(bytes.data()+start),p-start);++p;return out;};
 auto u32=[&](){if(p+4>bytes.size())throw std::runtime_error("Truncated shared GMA");uint32_t out;std::memcpy(&out,bytes.data()+p,4);p+=4;return out;};
 if(!text().empty())throw std::runtime_error("Shared GMA requires another archive");text();text();text();u32();uint64_t total=0;std::set<std::string> paths;
 static const std::regex model("models/mmd/[a-f0-9]{16}/[^/.:\\\\]+\\.(mdl|vvd|phy|dx90\\.vtx)");
 static const std::regex material("materials/(mmd/[a-zA-Z0-9_/.-]+|mmdhl/scmi/[a-zA-Z0-9_/.-]+)\\.(vmt|vtf)");
 bool rig=relative.starts_with("rigs/");
 while(u32()!=0){auto name=text();if(name.find("..")!=name.npos||!std::regex_match(name,rig?model:material)||!paths.insert(name).second)throw std::runtime_error("Shared archives may only contain generated models or materials; executable and Lua files are forbidden");if(p+12>bytes.size())throw std::runtime_error("Invalid shared GMA directory");uint64_t size;std::memcpy(&size,bytes.data()+p,8);p+=12;if(size>bytes.size()||total>bytes.size()-size)throw std::runtime_error("Invalid shared GMA member size");total+=size;}
 if(total>bytes.size()-p)throw std::runtime_error("Incomplete shared GMA data");
}
Json sharedManifest(const fs::path& cache,const std::string& asset,const Json& rigs,bool sourceOnly){
 if(!validId(asset))throw std::runtime_error("Invalid shared asset identity");auto manifest=readJson(sharedPath(cache,"assets/"+asset+"/manifest.json"));
 std::set<std::string> paths={"assets/"+asset+"/manifest.json","assets/"+asset+"/model.bin"};
 if(!sourceOnly)paths.insert("assets/"+asset+"/materials-v5.gma");
 for(auto& mat:manifest.at("textures"))for(auto key:{"base","sphere","toon"}){auto id=mat.value(key,std::string());if(!id.empty()){if(!validId(id))throw std::runtime_error("Invalid shared texture identity");paths.insert("textures/"+id+".png");}}
 for(auto& item:rigs){auto key=item.get<std::string>();auto relative="rigs/"+key+"/rig.json";auto rig=readJson(sharedPath(cache,relative));if(rig.at("asset")!=asset)throw std::runtime_error("Rig belongs to another asset");paths.insert(relative);paths.insert("rigs/"+key+"/carrier.gma");}
 Json out={{"version",1},{"asset",asset},{"name",manifest.value("name",asset)},{"files",Json::array()}};uint64_t size=0;
 for(auto& path:paths){auto bytes=readFile(sharedPath(cache,path));validateSharedFile(path,bytes);Json entry={{"path",path},{"size",bytes.size()},{"sha256",hash(bytes)}};if(path=="assets/"+asset+"/materials-v5.gma")entry["derive"]="source_materials_v5";out["files"].push_back(entry);size+=bytes.size();}out["size"]=size;return out;
}
}
