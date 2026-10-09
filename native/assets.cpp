#include "runtime.hpp"
#include "rig.hpp"
#include "spring_bones.hpp"
#include "vrm.hpp"
#include "assets.hpp"
#include "humanoid_slots.hpp"
#include "cutout.hpp"
#include "import_error.hpp"
#include "dependency_scope.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <set>
#include <map>
#include <stdexcept>
#include <sstream>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_STATIC
#include <stb_image_resize2.h>

namespace mmd {
std::wstring wide(std::string_view s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);if(!n)throw std::runtime_error("Invalid UTF-8 path");std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n);return out;}
std::string utf8(std::wstring_view s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;}
// GMod's host executable need not opt in to Windows long paths. Cache identities
// plus an atomic-write suffix can exceed MAX_PATH even in an ordinary install.
// Normalize before adding the extended prefix (which disables Win32 dot folding).
fs::path ioPath(const fs::path& path){
 auto value=fs::absolute(path).lexically_normal().wstring();
 if(value.starts_with(L"\\\\?\\"))return value;
 if(value.starts_with(L"\\\\"))return L"\\\\?\\UNC\\"+value.substr(2);
 return L"\\\\?\\"+value;
}
// A stream cannot say why it failed to open: ask Windows the same question (missing,
// in use, denied...) so the code and the report say what to do.
static DWORD openError(const fs::path& io,bool writing){
 std::error_code ec;if(!writing&&fs::is_directory(io,ec))return ERROR_DIRECTORY;
 HANDLE h=CreateFileW(io.c_str(),writing?GENERIC_WRITE:GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,writing?CREATE_ALWAYS:OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(h==INVALID_HANDLE_VALUE)return GetLastError();CloseHandle(h);if(writing)DeleteFileW(io.c_str());return 0;
}
// The sentences stay 2.2's: a missing texture's is a manifest warning, part of the asset's
// identity. Why it failed travels as the code and details.why, which a failure report adds.
Bytes readFile(const fs::path& path){
 auto io=ioPath(path);std::ifstream f(io,std::ios::binary|std::ios::ate);auto name=utf8(path.wstring());
 if(!f)fileFailure("Cannot read "+name,path,openError(io,false));
 auto size=f.tellg();if(size<0)importFail("io.read","Cannot determine file size",{{"path",name},{"why","its size cannot be determined"}});Bytes b(static_cast<size_t>(size));f.seekg(0);
 if(size&&!f.read(reinterpret_cast<char*>(b.data()),size))importFail("io.device","Incomplete file read",{{"path",name},{"read",int64_t(f.gcount())},{"size",uint64_t(size)},{"why","reading stopped after "+thousands(uint64_t(f.gcount()))+" of "+thousands(uint64_t(size))+" bytes (the drive may have been disconnected)"}});
 return b;
}
void writeAtomic(const fs::path& path,const std::function<void(std::ostream&)>& write){
 auto output=ioPath(path);fs::create_directories(output.parent_path());auto tmp=output;tmp+=L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetCurrentThreadId());
 // Data that stops short on a nearly full drive is a full disk; otherwise the open says why.
 // Writes fail as io.write (or io.disk_full): the cache, not the model file, is at fault.
 auto failed=[&](DWORD error){std::error_code ec;fs::remove(tmp,ec);auto space=fs::space(output.parent_path(),ec);if(!error&&!ec&&space.available<(64ull<<20))error=ERROR_DISK_FULL;
  fileFailure("Cannot write output file: "+utf8(path.wstring()),path,error,true);};
 {std::ofstream f(tmp,std::ios::binary|std::ios::trunc);
  if(!f)failed(openError(tmp,true));
  try{write(f);}catch(...){f.close();std::error_code ec;fs::remove(tmp,ec);throw;}
  if(!f.flush()){f.close();failed(0);}}
 for(int attempt=0;;attempt++){if(MoveFileExW(tmp.c_str(),output.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))break;auto error=GetLastError();if(attempt>=99||(error!=ERROR_SHARING_VIOLATION&&error!=ERROR_ACCESS_DENIED)){std::error_code ec;fs::remove(tmp,ec);fileFailure("Cannot commit output file: "+utf8(path.wstring()),path,error,true);}Sleep(10);}
}
void writeAtomic(const fs::path& path,std::span<const unsigned char> b){writeAtomic(path,[&](std::ostream& f){f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));});}
void writeJson(const fs::path& path,const Json& j){auto s=j.dump(2);writeAtomic(path,std::span(reinterpret_cast<const unsigned char*>(s.data()),s.size()));}
Json readJson(const fs::path& p){auto b=readFile(p);return Json::parse(b.begin(),b.end());}
std::string hash(std::span<const unsigned char> b){
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE state=nullptr;unsigned char digest[32];
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 unavailable");
 auto status=BCryptCreateHash(alg,&state,nullptr,0,nullptr,0,0);
 for(size_t at=0;status>=0&&at<b.size();){auto count=ULONG(std::min<size_t>(b.size()-at,128ull<<20));status=BCryptHashData(state,const_cast<unsigned char*>(b.data()+at),count,0);at+=count;}
 if(status>=0)status=BCryptFinishHash(state,digest,32,0);if(state)BCryptDestroyHash(state);BCryptCloseAlgorithmProvider(alg,0);
 if(status<0)throw std::runtime_error("SHA256 failed");std::string s;for(auto c:digest){s.push_back("0123456789abcdef"[c>>4]);s.push_back("0123456789abcdef"[c&15]);}return s;
}
void retainCacheFiles(const fs::path& cache,const std::vector<fs::path>& paths){
 auto queue=cache/L"cleanup.json";if(!fs::exists(queue))return;auto pending=readJson(queue);Json keep=Json::array();
 for(auto& item:pending){auto path=fs::path(wide(item.get<std::string>())).lexically_normal();bool retained=false;
  for(auto& root:paths){auto relative=path.lexically_relative(root);retained|=!relative.empty()&&!relative.is_absolute()&&*relative.begin()!=L"..";}if(!retained)keep.push_back(item);
 }
 if(keep.empty())fs::remove(queue);else writeJson(queue,keep);
}
void registerShortName(const fs::path& cache,const std::string& kind,const std::string& id){
 auto path=cache/L"names"/wide(kind)/wide(id.substr(0,16)+".json");
 // Detect a shortened-identifier collision before mounting any conflicting data.
 if(fs::exists(path)&&readJson(path).value("id","")!=id)throw std::runtime_error("Cache identifier collision; no existing data was overwritten");
 retainCacheFiles(cache,{path.lexically_relative(cache)});writeJson(path,{{"id",id}});
}
bool validId(std::string_view s){return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
// Texels past 4096 on an edge cost four times the memory and import time and
// look the same in game, so larger textures are scaled down, keeping the ratio.
constexpr int MaxTextureEdge=4096;
static bool fitTexture(const unsigned char* pixels,int& width,int& height,Bytes& scaled){
    if(width<=MaxTextureEdge&&height<=MaxTextureEdge)return false;
    double factor=double(MaxTextureEdge)/std::max(width,height);
    int w=std::clamp(int(std::lround(width*factor)),1,MaxTextureEdge),h=std::clamp(int(std::lround(height*factor)),1,MaxTextureEdge);
    scaled.resize(size_t(w)*h*4);
    if(!stbir_resize_uint8_srgb(pixels,width,height,0,scaled.data(),w,h,0,STBIR_RGBA))throw std::runtime_error("Cannot scale down a large texture");
    width=w;height=h;return true;
}
// A PMX or PMD texture: a path relative to the model. It may climb out of the model's
// folder as in 2.2 (artists' working folders keep textures beside it), but is `refused`
// when it is drive- or root-relative, an alternate data stream or, through any link, a
// denied place (dependency_scope.hpp, Reach::Local). The sentences are part of the
// asset's identity: an absolute path keeps 2.2's.
static fs::path resolveTexture(const DependencyScope& scope,std::string path,bool& refused){
    std::replace(path.begin(),path.end(),'\\','/');auto p=fs::path(wide(path));if(p.is_absolute())throw std::runtime_error("Texture uses an absolute path: "+path);
    auto file=(scope.folder()/p).lexically_normal();std::error_code error;
    refused=scope.locate(path).empty()||(fs::is_regular_file(ioPath(file),error)&&!scope.allows(file));
    return file;
}
static std::string outsideTexture(std::string path){std::replace(path.begin(),path.end(),'\\','/');return "Texture in a protected location: "+path;}
static std::string normalizeTexture(const Bytes& bytes,const std::string& name,const fs::path& cache,bool& alpha,std::vector<std::string>& warnings){
    int width=0,height=0,channels=0;
    if(bytes.size()>INT_MAX)throw std::runtime_error("Texture exceeds the decoder's signed 32-bit input format: "+name);
    Bytes decoded;unsigned char* pixels=nullptr;
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> guard(nullptr,stbi_image_free);
    if(bytes.size()>4&&!memcmp(bytes.data(),"DDS ",4)){
        HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);struct Apartment{HRESULT hr;~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}} apartment{initialized};
        using Microsoft::WRL::ComPtr;ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
        auto ok=[](HRESULT hr){if(FAILED(hr))throw std::runtime_error("Unsupported DDS texture (Windows codec supports BC1, BC2 and BC3)");};
        ok(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ok(factory->CreateStream(&stream));ok(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),DWORD(bytes.size())));ok(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder));ok(decoder->GetFrame(0,&frame));UINT w=0,h=0;ok(frame->GetSize(&w,&h));if(!w||!h||uint64_t(w)*h*4>UINT_MAX)throw std::runtime_error("DDS dimensions exceed the Windows decoder format");width=int(w);height=int(h);
        ok(factory->CreateFormatConverter(&converter));ok(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));decoded.resize(size_t(w)*h*4);ok(converter->CopyPixels(nullptr,w*4,UINT(decoded.size()),decoded.data()));pixels=decoded.data();
    }else{
        if(!stbi_info_from_memory(bytes.data(),int(bytes.size()),&width,&height,&channels)||width<1||height<1)throw std::runtime_error("Unsupported texture: "+name);
        guard.reset(stbi_load_from_memory(bytes.data(),int(bytes.size()),&width,&height,&channels,4));pixels=guard.get();if(!pixels)throw std::runtime_error("Cannot decode texture: "+name);
    }
    for(size_t i=3;i<size_t(width)*height*4;i+=4)if(pixels[i]<255){alpha=true;break;}
    Bytes scaled;const int sourceWidth=width,sourceHeight=height;
    if(fitTexture(pixels,width,height,scaled)){
        guard.reset();decoded=Bytes();pixels=scaled.data();
        warnings.push_back("Large texture "+name+" ("+std::to_string(sourceWidth)+" x "+std::to_string(sourceHeight)+") was scaled down to "+std::to_string(width)+" x "+std::to_string(height)+"; the game shows no more detail than that.");
    }else if(bytes.size()>(64ull<<20))warnings.push_back("Large texture "+name+" ("+std::to_string(width)+" x "+std::to_string(height)+"). Import continues; memory use and loading time may be high.");
    Bytes png;auto callback=[](void* context,void* p,int n){auto& b=*static_cast<Bytes*>(context);auto src=static_cast<unsigned char*>(p);b.insert(b.end(),src,src+n);};
    if(!stbi_write_png_to_func(callback,&png,width,height,4,pixels,width*4))throw std::runtime_error("Texture encoding failed");auto id=hash(png);auto path=cache/L"textures"/wide(id+".png");if(!fs::exists(path))writeAtomic(path,png);return id;
}
// Source-readable derivatives allow normal IMaterial/VMT editor workflows.
void prepareSourceMaterials(const fs::path& cache,const std::string& id){
 auto directory=cache/L"assets"/wide(id),package=directory/L"materials-v5.gma";
 auto manifest=readJson(directory/L"manifest.json");registerShortName(cache,"assets",id);
 std::vector<fs::path> retained={fs::path(L"assets")/wide(id)};
 for(auto& t:manifest["textures"])for(auto kind:{"base","sphere","toon"}){auto h=t.value(kind,"");if(validId(h))for(auto ext:{".png",".vtf"})retained.push_back(fs::path(L"textures")/wide(h+ext));}
 retainCacheFiles(cache,retained);if(fs::is_regular_file(package))return;
 // Textures stay on disk until the package streams them: a model with dozens of
 // 4096 textures never holds them all, plus a packed copy, in memory.
 std::map<std::string,GmaEntry> files;
 for(size_t i=0;i<manifest["materials"].size();i++){
  auto& material=manifest["materials"][i];auto& texture=manifest["textures"][i];std::string base=texture.value("base","");auto at=place("material",int64_t(i),material.value("name",""));
  std::string texturePath="models/debug/debugwhite";
  if(validId(base)){
   registerShortName(cache,"textures",base);texturePath="mmd/t/"+base.substr(0,16);auto derivative=cache/L"textures"/wide(base+".vtf");
   if(!fs::is_regular_file(derivative)){
    int width,height,channels;std::unique_ptr<unsigned char,decltype(&stbi_image_free)> decoded(nullptr,stbi_image_free);
    {auto png=readFile(cache/L"textures"/wide(base+".png"));decoded.reset(stbi_load_from_memory(png.data(),int(png.size()),&width,&height,&channels,4));}
    if(!decoded)importFail("texture.derivative","Cannot create the Source texture of "+placeText(at)+": its cached image "+base.substr(0,16)+".png cannot be decoded",{{"where",Json::array({at})},{"texture",base},{"path",utf8((cache/L"textures"/wide(base+".png")).wstring())}});
    // Caches from before 2.2 hold textures larger than 4096.
    Bytes scaled;const unsigned char* pixels=decoded.get();if(fitTexture(pixels,width,height,scaled))pixels=scaled.data();
    if(width>65535||height>65535)importFail("texture.derivative","Cannot create the Source texture of "+placeText(at)+": its image is "+std::to_string(width)+" x "+std::to_string(height)+" pixels, more than the VTF format holds",{{"where",Json::array({at})},{"texture",base}});
    // Mip levels follow the 80-byte header smallest first; each level is a 2x2
    // box filter of the one above, built in place.
    std::vector<std::pair<int,int>> levels{{width,height}};while(levels.back().first>1||levels.back().second>1)levels.push_back({std::max(1,levels.back().first/2),std::max(1,levels.back().second/2)});
    std::vector<size_t> offsets(levels.size());size_t size=80;for(size_t k=levels.size();k-->0;){offsets[k]=size;size+=size_t(levels[k].first)*levels[k].second*4;}
    Bytes vtf(size,0);auto put=[&]<class T>(size_t at,T value){std::memcpy(vtf.data()+at,&value,sizeof(T));};
    std::memcpy(vtf.data(),"VTF",3);put(4,uint32_t(7));put(8,uint32_t(2));put(12,uint32_t(80));put(16,uint16_t(width));put(18,uint16_t(height));put(20,uint32_t(texture.value("alpha",false)?0x2000:0));put(24,uint16_t(1));put(32,.5f);put(36,.5f);put(40,.5f);put(48,1.f);put(52,uint32_t(0));put(57,int32_t(-1));put(63,uint16_t(1));
    vtf[56]=uint8_t(levels.size());std::memcpy(vtf.data()+offsets[0],pixels,size_t(width)*height*4);decoded.reset();scaled=Bytes();
    for(size_t k=1;k<levels.size();k++){auto [w,h]=levels[k-1];auto [nw,nh]=levels[k];const unsigned char* prior=vtf.data()+offsets[k-1];unsigned char* next=vtf.data()+offsets[k];
     for(int y=0;y<nh;y++)for(int x=0;x<nw;x++)for(int c=0;c<4;c++){unsigned sum=0;for(int yy=0;yy<2;yy++)for(int xx=0;xx<2;xx++)sum+=prior[(size_t(std::min(h-1,y*2+yy))*w+std::min(w-1,x*2+xx))*4+c];next[(size_t(y)*nw+x)*4+c]=uint8_t(sum/4);}}
    writeAtomic(derivative,vtf);
   }
   std::string path="materials/"+texturePath+".vtf";
   if(!files.contains(path)){
    std::ifstream in(ioPath(derivative),std::ios::binary);unsigned char header[80]{};std::error_code ec;auto size=fs::file_size(ioPath(derivative),ec);
    auto damaged=[&]{importFail("texture.derivative","The cached Source texture of "+placeText(at)+" is damaged ("+base.substr(0,16)+".vtf); delete it and import again",{{"where",Json::array({at})},{"texture",base},{"path",utf8(derivative.wstring())}});};
    if(!in||!in.read(reinterpret_cast<char*>(header),80)||ec||std::memcmp(header,"VTF\0",4)!=0)damaged();
    uint16_t width=0,height=0;std::memcpy(&width,header+16,2);std::memcpy(&height,header+18,2);uint64_t count=uint64_t(width)*height*4;
    if(!width||!height||count>size-80)damaged();
    files[path].file=derivative;
   }
  }
  std::ostringstream vmt;vmt<<"VertexLitGeneric\n{\n";
  auto value=[&](const char* key,const std::string& v){vmt<<'"'<<key<<"\" \""<<v<<"\"\n";};
  value("$basetexture",texturePath);value("$model","1");value("$vertexcolor","1");value("$nocull",material.value("twoSided",false)?"1":"0");
  value("$translucent","0");value("$vertexalpha","0");value("$alphatest","1");value("$alphatestreference",".5");value("$allowalphatocoverage","1");
  value("$bumpmap","mmdhl/scmi/normal");value("$lightwarptexture","mmdhl/scmi/lightwarptexture");value("$halflambert","0");value("$phong","1");value("$phongboost","24");value("$phongalbedotint","1");value("$phongexponenttexture","mmdhl/scmi/phong_exp");value("$phongfresnelranges","[0 0 1]");value("$rimlight","1");value("$rimlightexponent","2");value("$rimlightboost","2");vmt<<"}\n";
  auto text=vmt.str();files["materials/"+materialPath(id,i,material.value("name",""))+".vmt"].data=Bytes(text.begin(),text.end());
 }
 writeGma(package,files,"Model Hotloader materials "+id);
}
Json openSourceRegistry(const fs::path& path,std::string& setAside){
    setAside.clear();std::error_code ec;if(!fs::exists(ioPath(path),ec))return Json::object();
    Json registry;bool damaged=false;
    try{registry=readJson(path);damaged=!registry.is_object();}catch(const Json::exception&){damaged=true;}
    if(!damaged)return registry;
    // Every other model's source path is in it: kept for repair, never written over.
    SYSTEMTIME t{};GetSystemTime(&t);wchar_t stamp[32];swprintf_s(stamp,L"%04u%02u%02u-%02u%02u%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    for(int n=0;;n++){
        auto name=path.filename().wstring()+L".damaged-"+stamp+(n?L"-"+std::to_wstring(n):std::wstring());
        if(MoveFileExW(ioPath(path).c_str(),ioPath(path.parent_path()/name).c_str(),MOVEFILE_WRITE_THROUGH)){setAside=utf8(name);return Json::object();}
        auto error=GetLastError();if((error!=ERROR_ALREADY_EXISTS&&error!=ERROR_FILE_EXISTS)||n>=99)fileFailure("Cannot set aside the damaged source registry "+utf8(path.wstring()),path,error,true);
    }
}
Json importAsset(const fs::path& source,const fs::path& cache,const Json& options,const fs::path& progress,CharacterConversion* character){
    // Every step has a code (stageCode) the addon names in the player's language; the
    // texture step also says which material it is on, for a failure or a crash report.
    const auto filename=utf8(source.filename().wstring());
    auto report=[&](const char* stage,const char* code,float value,const std::string& detail={},size_t current=0,size_t total=0){setImportStage(code);if(progress.empty())return;
        Json status={{"state","running"},{"stage",stage},{"stageCode",code},{"progress",value},{"filename",filename}};if(!detail.empty())status.update({{"detail",detail},{"current",current},{"total",total}});writeJson(progress,status);};
    report("Reading the file","read",.02f);auto raw=readFile(source);
    // VRM avatars become a PMX in memory with their embedded textures; spring
    // bones and licence metadata travel in the manifest (and so in its identity).
    // A .vrm is always a GLB: one that does not read as an avatar is converted anyway, so the
    // converter says why (cut off, broken JSON, no VRM extension) instead of "rename it".
    const bool vrmSource=!character&&(isVrmData(raw)||(!lstrcmpiW(source.extension().c_str(),L".vrm")&&raw.size()>=4&&!std::memcmp(raw.data(),"glTF",4)));
    std::map<std::string,Bytes> embedded;Json vrm,conversion;std::vector<std::string> converted;
    if(vrmSource){report("Converting VRM avatar","convert_vrm",.05f);auto sourceSha=hash(raw);auto result=convertVrm(raw,utf8(source.stem().wstring()));raw=std::move(result.pmx);embedded=std::move(result.textures);vrm=std::move(result.vrm);vrm["sourceSha256"]=sourceSha;converted=std::move(result.warnings);}
    // Characters in other formats arrive converted by the worker the same way (character_import.cpp).
    if(character){report("Converting the character","convert_character",.05f);conversion=std::move(character->conversion);conversion["sourceSha256"]=hash(raw);raw=std::move(character->pmx);embedded=std::move(character->textures);converted=std::move(character->warnings);}
    const bool embeddedSource=vrmSource||character;
    if(!embeddedSource&&(raw.size()<4||(std::memcmp(raw.data(),"PMX ",4)&&std::memcmp(raw.data(),"Pmd",3))))notCharacterFile(raw,source);
    report("Parsing skeleton, materials and physics","parse",.06f);
    auto model=parse(raw);for(auto& warning:converted)model->warnings.push_back(warning);if(vrmSource){ImportScope scope("Reading the VRM spring bones","vrm");model->springs=SpringSetup::fromManifest(vrm,*model);}
    if(character){model->springs=SpringSetup::fromManifest(conversion,*model);auto map=conversion.value("boneMap",Json::object());for(auto& [key,value]:map.items())model->conversionBoneMap[key]=value.get<int>();}
    Json manifest=model->info();manifest["version"]=2;if(vrmSource)manifest["vrm"]=vrm;if(character)manifest["conversion"]=conversion;for(auto& material:manifest["materials"])material.erase("path");manifest["sourceHash"]=hash(raw);manifest["textures"]=Json::array();
    std::map<std::wstring,std::pair<std::string,bool>> prepared;const DependencyScope scope(source.parent_path(),true,DependencyScope::Reach::Local);
    for(size_t i=0;i<model->materials.size();i++){auto& material=model->materials[i];Json textures;bool alpha=false;
        auto image=material.base.substr(material.base.find_last_of("/\\")+1);
        report("Preparing textures","textures",.1f+.8f*float(i)/std::max<size_t>(1,model->materials.size()),"Material "+std::to_string(i+1)+" of "+std::to_string(model->materials.size())+(material.name.empty()?std::string():" “"+cleanText(material.name)+"”")+(image.empty()?std::string():": "+cleanText(image)),i+1,model->materials.size());
        for(auto entry:std::array<std::pair<const char*,std::string>,3>{{{"base",material.base},{"sphere",material.sphere},{"toon",material.toon}}}){
            std::string id;bool localAlpha=false;if(!entry.second.empty())try{
                if(embeddedSource){auto found=embedded.find(entry.second);if(found==embedded.end())throw std::runtime_error("Missing embedded VRM texture "+entry.second);
                    auto key=L"vrm:"+wide(entry.second);auto existing=prepared.find(key);if(existing!=prepared.end()){id=existing->second.first;localAlpha=existing->second.second;}else{id=normalizeTexture(found->second,entry.second,cache,localAlpha,model->warnings);prepared[key]={id,localAlpha};}
                    textures[entry.first]=id;if(std::string(entry.first)=="base")alpha=localAlpha;continue;}
                bool refused=false;auto file=resolveTexture(scope,entry.second,refused);
                if((refused||!fs::exists(file))&&std::string(entry.first)=="toon"){
                    // MMD's shared ramps normally live beside the executable in Data: only a
                    // file of that name there, never a link out of it or a denied place.
                    auto data=source.parent_path().parent_path().parent_path()/L"Data";auto candidate=data/fs::path(wide(entry.second)).filename();
                    if(DependencyScope(data,false).allows(candidate)){file=candidate;refused=false;}
                    else if(entry.second.starts_with("toon")){textures[entry.first]="";continue;}
                }
                if(refused)throw std::runtime_error(outsideTexture(entry.second));
                auto key=file.wstring();auto existing=prepared.find(key);if(existing!=prepared.end()){id=existing->second.first;localAlpha=existing->second.second;}else{id=normalizeTexture(readFile(file),utf8(file.filename().wstring()),cache,localAlpha,model->warnings);prepared[key]={id,localAlpha};}
            }catch(const std::exception& e){model->warnings.push_back(e.what());}
            textures[entry.first]=id;if(std::string(entry.first)=="base")alpha=localAlpha;
        }
        textures["alpha"]=alpha;manifest["textures"].push_back(textures);
    }
    report("Saving to the cache","cache",.9f);
    manifest["warnings"]=model->warnings;auto identity=manifest.dump();auto id=hash(std::span(reinterpret_cast<const unsigned char*>(identity.data()),identity.size()));manifest["id"]=id;
    auto directory=cache/L"assets"/wide(id);writeAtomic(directory/L"model.bin",raw);writeJson(directory/L"manifest.json",manifest);
    // A damaged registry (Reload's source paths) must not block every import: it is set aside and a new one starts.
    auto registryPath=cache/L"sources.local.json";std::string setAside;Json registry=openSourceRegistry(registryPath,setAside);
    registry[id]={{"source",utf8(fs::absolute(source).wstring())},{"options",options}};writeJson(registryPath,registry);
    model->id=id;report("Preparing Source materials","materials",.91f);prepareSourceMaterials(cache,id);report("Fitting native collision anatomy","fit",.94f);auto fit=prepareModelFit(*model,cache);
    // The fit stays outside the manifest (and so outside the asset's identity). A failed
    // one carries its facts as errorDetails too, like any import failure (the Lua reads either).
    if(!fit.value("ok",true)&&!fit.contains("errorDetails"))fit["errorDetails"]={{"missing",fit.value("missing",Json::array())}};
    Json result={{"state","complete"},{"asset",id},{"info",manifest},{"fit",fit}};
    if(!setAside.empty())result["registryBackup"]=setAside;
    return result;
}
Json importConverted(const fs::path& source,const fs::path& cache,const Json& options,const fs::path& progress,CharacterConversion&& converted){return importAsset(source,cache,options,progress,&converted);}
std::shared_ptr<Model> loadAsset(const fs::path& cache,const std::string& id){
    if(!validId(id))throw std::runtime_error("Invalid asset ID");auto directory=cache/L"assets"/wide(id);auto manifest=readJson(directory/L"manifest.json");if((manifest.value("version",0)!=1&&manifest.value("version",0)!=2)||manifest.value("id",std::string())!=id)throw std::runtime_error("Cache version or ID mismatch");
    auto identity=manifest;identity["id"]=manifest.at("sourceHash");auto encoded=identity.dump();if(hash(std::span(reinterpret_cast<const unsigned char*>(encoded.data()),encoded.size()))!=id)throw std::runtime_error("Cached manifest checksum mismatch");
    auto raw=readFile(directory/L"model.bin");if(hash(raw)!=manifest.at("sourceHash"))throw std::runtime_error("Cached model checksum mismatch");auto model=parse(raw);model->id=id;for(auto& warning:manifest.value("warnings",std::vector<std::string>{}))if(std::find(model->warnings.begin(),model->warnings.end(),warning)==model->warnings.end())model->warnings.push_back(warning);
    if(manifest.contains("vrm"))model->springs=SpringSetup::fromManifest(manifest["vrm"],*model);
    else if(manifest.contains("conversion")){
        auto& conversion=manifest["conversion"];model->springs=SpringSetup::fromManifest(conversion,*model);
        // The converter's bone assignment is the fitter's default for this asset; check it like any cached index.
        auto map=conversion.value("boneMap",Json::object());if(!map.is_object())throw std::runtime_error("Cached conversion map is invalid");
        for(auto& [key,value]:map.items()){if(!mappedSlotKey(key)||!value.is_number_integer()||value.get<int64_t>()<-1||value.get<int64_t>()>=int64_t(model->bones.size()))throw std::runtime_error("Cached conversion map is invalid");model->conversionBoneMap[key]=value.get<int>();}
    }
    if(manifest.at("textures").size()!=model->materials.size())throw std::runtime_error("Cached material count mismatch");
    std::set<std::string> verified;
    std::map<std::string,std::vector<size_t>> alphaParts;
    for(size_t i=0;i<model->materials.size();i++){auto& t=manifest["textures"][i];auto& m=model->materials[i];auto resolve=[&](const char* key){std::string h=t.value(key,std::string());if(h.empty())return h;if(!validId(h))throw std::runtime_error("Invalid cached texture reference");if(verified.insert(h).second){auto p=cache/L"textures"/wide(h+".png");if(hash(readFile(p))!=h)throw std::runtime_error("Cached texture checksum mismatch");}return h;};m.base=resolve("base");m.sphere=resolve("sphere");m.toon=resolve("toon");m.alphaTexture=t.value("alpha",false);}
    // Cached manifests remain immutable. Classify their decoded base alpha at
    // load time so cutout hair writes depth, while authored sheer fabric/glass
    // still blends. Count meaningful coverage rather than empty atlas space.
    // The RTX Remix renderer (renderer.cpp) also needs what the materials' 0.5
    // alpha test removes, sampled where each triangle maps (corners, edge
    // midpoints and centre), not over the whole texture: atlases pad the regions
    // a part uses with transparent texels (Furina's socks: 7 % of an 8192 atlas
    // passes, 99 % where they map). A triangle with no passing sample is cut
    // away (cutoutTriangles 0), and alphaCoverage is the area-weighted passing
    // share of the part's remaining triangles, 0 when none remains. Near-empty
    // shells (a body copy that keeps only the gloves) then lose their empty
    // triangles instead of being drawn blended over the whole body.
    // A texture (UV) morph slides UVs across an atlas: Ruan Mei's stockings change
    // style by moving to another column, transparent at rest over the legs. Rest
    // UVs cannot decide for triangles a UV morph moves; they stay in, and the
    // renderer cuts them at each instance's current UVs with their texture's pass
    // mask (kept only for such parts) by the same rule (cutout.hpp). Coverage is
    // still measured at rest, over every triangle, exactly as before. Whether such
    // a part blends is decided once for all instances, so it also weighs the styles
    // its texture morphs show (uvMorphCoverage): one a morph makes opaque keeps the test.
    for(size_t i=0;i<model->materials.size();i++){auto& m=model->materials[i];if(m.alphaTexture&&!m.base.empty())alphaParts[m.base].push_back(i);}
    if(!alphaParts.empty())model->cutoutTriangles.assign(model->indices.size()/3,1);
    std::vector<uint8_t> uvMoved;std::vector<std::vector<std::pair<unsigned,std::array<float,2>>>> uvMorphs;
    if(!alphaParts.empty())for(auto morph:model->morphs)if(nanoemModelMorphGetType(morph)==NANOEM_MODEL_MORPH_TYPE_TEXTURE){nanoem_rsize_t n=0;auto entries=nanoemModelMorphGetAllUVMorphObjects(morph,&n);std::vector<std::pair<unsigned,std::array<float,2>>> offsets;
        for(size_t k=0;k<n;k++){int v=vertexIndex(nanoemModelMorphUVGetVertexObject(entries[k]));auto p=nanoemModelMorphUVGetPosition(entries[k]);
            if(v>=0&&size_t(v)<model->vertices.size()&&(p[0]!=0||p[1]!=0)){if(uvMoved.empty())uvMoved.assign(model->vertices.size(),0);uvMoved[size_t(v)]=1;offsets.push_back({unsigned(v),{p[0],p[1]}});}}
        if(!offsets.empty())uvMorphs.push_back(std::move(offsets));}
    std::vector<std::array<float,2>> offset;
    for(auto& [base,parts]:alphaParts){
        auto png=readFile(cache/L"textures"/wide(base+".png"));int w=0,h=0,c=0;
        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(png.data(),int(png.size()),&w,&h,&c,4),stbi_image_free);
        if(!pixels||w<1||h<1)throw std::runtime_error("Cannot inspect cached texture alpha");
        size_t visible=0,soft=0;for(size_t k=3;k<size_t(w)*h*4;k+=4){auto a=pixels.get()[k];visible+=a>8;soft+=a>8&&a<247;}
        auto mask=std::make_shared<const AlphaPassMask>(pixels.get(),w,h);pixels.reset();
        for(auto index:parts){
            auto& material=model->materials[index];material.translucentTexture=visible>0&&soft>visible/10;bool dynamic=false;
            material.alphaCoverage=partCoverage(*model,material,*mask,[&](unsigned v){return model->vertices[v].uv;},[&](size_t t,int hits){
                bool moved=!uvMoved.empty()&&(uvMoved[model->indices[t*3]]||uvMoved[model->indices[t*3+1]]||uvMoved[model->indices[t*3+2]]);
                if(moved){model->uvCutoutTriangles.push_back(unsigned(t));dynamic=true;}else if(!hits)model->cutoutTriangles[t]=0;});
            if(!dynamic)continue;
            material.dynamicCutout=true;model->cutoutMasks.resize(model->materials.size());model->cutoutMasks[index]=mask;
            // One texture morph at a time, at quarter steps (an atlas slide shows a style per step).
            if(offset.empty())offset.assign(model->vertices.size(),{0.f,0.f});
            for(auto& morph:uvMorphs){
                for(auto& [v,o]:morph){offset[v][0]+=o[0];offset[v][1]+=o[1];}
                bool moves=false;for(size_t k=material.first;!moves&&k<size_t(material.first)+material.count&&k<model->indices.size();k++){const auto& o=offset[model->indices[k]];moves=o[0]!=0||o[1]!=0;}
                if(moves)for(float weight:{.25f,.5f,.75f,1.f})material.uvMorphCoverage=std::max(material.uvMorphCoverage,partCoverage(*model,material,*mask,
                    [&](unsigned v){const auto& r=model->vertices[v].uv;return std::array<float,2>{r[0]+offset[v][0]*weight,r[1]+offset[v][1]*weight};},[](size_t,int){}));
                for(auto& [v,o]:morph)offset[v]={0.f,0.f};
            }
        }
    }
    std::sort(model->uvCutoutTriangles.begin(),model->uvCutoutTriangles.end());
    prepareSourceMaterials(cache,id);prepareModelFit(*model,cache);return model;
}
static bool plainDirectory(const fs::path& path){auto attributes=GetFileAttributesW(path.c_str());return attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT);}
size_t sweepJobFolders(const fs::path& cache,std::chrono::hours age){
 std::error_code error;auto jobs=cache/L"jobs";if(!plainDirectory(jobs))return 0;auto cutoff=fs::file_time_type::clock::now()-age;size_t removed=0;
 for(auto& entry:fs::directory_iterator(jobs,error)){std::error_code e;auto time=entry.last_write_time(e);if(!e&&time<cutoff&&plainDirectory(entry.path())&&fs::remove_all(entry.path(),e)>0&&!e)removed++;}
 return removed;
}
// Only generated cache files are eligible. Resolve and check every path before
// deletion; never follow a junction/symlink out of the cache or touch sources.
Json deleteAssets(const fs::path& cache,const std::vector<std::string>& ids){
 std::set<std::string> selected(ids.begin(),ids.end()),textures,used;
 for(auto& id:selected)if(!validId(id))throw std::runtime_error("Invalid asset ID for deletion");
 auto root=fs::weakly_canonical(cache);std::set<fs::path> paths;
 auto checked=[&](const fs::path& relative){
  if(relative.empty()||relative.is_absolute())throw std::runtime_error("Invalid cache cleanup path");
  static const std::set<std::wstring> allowed={L"assets",L"textures",L"fits",L"rigs",L"library",L"fit_overrides",L"names",L"jobs"};
  if(!allowed.contains(relative.begin()->wstring()))throw std::runtime_error("Invalid cache cleanup directory");
  for(auto& part:relative)if(part==L"..")throw std::runtime_error("Cache cleanup cannot traverse parents");
  auto path=root/relative,resolved=fs::weakly_canonical(path);auto rel=resolved.lexically_relative(root);
  if(rel.empty()||rel.is_absolute()||*rel.begin()==L"..")throw std::runtime_error("Cache cleanup escaped its directory");
  auto current=root;for(auto& part:relative){current/=part;auto attr=GetFileAttributesW(current.c_str());if(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Cache cleanup will not follow a junction or symlink");}
  return path;
 };
 std::function<void(fs::path)> collect=[&](fs::path relative){auto path=checked(relative);if(!fs::exists(path))return;paths.insert(relative);if(fs::is_directory(path))for(auto& e:fs::directory_iterator(path))collect(relative/e.path().filename());};
 auto queue=cache/L"cleanup.json";
 if(fs::exists(queue))for(auto& entry:readJson(queue)){auto relative=fs::path(wide(entry.get<std::string>()));checked(relative);paths.insert(relative);}
 // One unreadable manifest must not block every deletion. Its textures are
 // unknown, so no texture is collected for it (a selected one may leak its own).
 size_t unreadable=0;
 if(fs::exists(cache/L"assets"))for(auto& entry:fs::directory_iterator(cache/L"assets")){
  auto id=utf8(entry.path().filename().wstring());if(!validId(id)||!fs::exists(entry.path()/L"manifest.json"))continue;
  std::set<std::string> refs;
  try{auto manifest=readJson(entry.path()/L"manifest.json");
   for(auto& material:manifest.value("textures",Json::array()))for(auto key:{"base","sphere","toon"}){auto ref=material.value(key,"");if(validId(ref))refs.insert(ref);}
  }catch(const std::exception&){unreadable++;continue;}
  (selected.contains(id)?textures:used).insert(refs.begin(),refs.end());
 }
 for(auto& id:selected){collect(fs::path(L"assets")/wide(id));collect(fs::path(L"library")/wide(id+".json"));collect(fs::path(L"fit_overrides")/wide(id+".json"));collect(fs::path(L"names/assets")/wide(id.substr(0,16)+".json"));collect(fs::path(L"names/assets")/wide(id.substr(0,16)+"-materials-v5.gma"));}
 for(auto& id:textures)if(!used.contains(id)){for(auto ext:{".png",".vtf",".png.share2"})collect(fs::path(L"textures")/wide(id+ext));collect(fs::path(L"names/textures")/wide(id.substr(0,16)+".json"));}
 for(auto dir:{L"fits",L"rigs",L"jobs"})if(fs::exists(cache/dir))for(auto& entry:fs::directory_iterator(cache/dir)){
  auto path=entry.is_directory()?entry.path()/(std::wstring(dir)==L"rigs"?L"rig.json":L"status.json"):entry.path();if(path.extension()!=L".json"||!fs::exists(path))continue;
  Json metadata;try{metadata=readJson(path);}catch(...){continue;}auto asset=metadata.contains("fit")?metadata["fit"].value("asset",""):metadata.value("asset","");if(selected.contains(asset)){collect(fs::path(dir)/entry.path().filename());if(std::wstring(dir)==L"rigs"){collect(fs::path(L"names/rigs")/wide(metadata.value("key","").substr(0,16)+".json"));collect(fs::path(L"names/rigs")/wide(metadata.value("key","").substr(0,16)+"-carrier.gma"));}}
 }
 auto registryPath=cache/L"sources.local.json";
 if(!selected.empty()&&fs::exists(registryPath)){auto registry=readJson(registryPath);for(auto& id:selected)registry.erase(id);writeJson(registryPath,registry);}
 // Persist intent before touching mounted files. Windows may hold an archive
 // open until game exit; the next client startup finishes these exact paths.
 auto save=[&](const auto& entries){Json list=Json::array();for(auto& relative:entries)list.push_back(utf8(relative.generic_wstring()));writeJson(queue,list);};
 save(paths);std::vector<fs::path> ordered(paths.begin(),paths.end());std::sort(ordered.begin(),ordered.end(),[](auto& a,auto& b){return a.native().size()>b.native().size();});
 uint64_t bytes=0;size_t removed=0;std::vector<fs::path> pending;
 for(auto& relative:ordered){auto path=checked(relative);std::error_code ec;auto size=fs::is_regular_file(path,ec)?fs::file_size(path,ec):0;ec.clear();bool done=fs::remove(path,ec);if(ec)pending.push_back(relative);else if(done){bytes+=size;removed++;}}
 save(pending);if(pending.empty())fs::remove(queue);
 return {{"removedFiles",removed},{"removedBytes",bytes},{"pendingFiles",pending.size()},{"unreadableManifests",unreadable}};
}
} // namespace mmd
