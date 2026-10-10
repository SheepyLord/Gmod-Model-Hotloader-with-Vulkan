// .blend reader and Zstandard decoder checks on files written here, so no
// binary fixtures are needed. The real-Blender corpus (2.60 to 5.2, gzip and
// zstd) is in docs/STATIC_PROPS_VALIDATION.md.
#include "zstd_decode.hpp"
#ifdef _WIN32
#include <psapi.h>
#else
#include <sys/resource.h>
#endif
#include <tuple>
#include <fstream>
#include <map>

namespace blendtest {
// 211,691 bytes of zstdContent() compressed by libzstd at level 19: two blocks,
// Huffman literals, FSE sequence tables and repeat offsets.
static const char* ZstdVector=
    "28b52ffda0eb3a03002c1000e6b45411b025690c0cc3943642c82664973a1d05255b0050004800e5206b143588140e1dcd60cb26098eb24831864ae2"
    "841b1acc92cc11ac529061a43ae2c2416b8a0d9b149c72810cc6528938c2a26286a5cd098eca802c23a943ac50d030696773130cca429271d4124518"
    "280d0076ce0060dc0d044b72ac62c3d2ce6e96ecd8124b9cf766832d4b766cadd8b0b4b30b96e458c590cebdd960cb921d5b2b0a2574d012838166c1"
    "2c93cc314bcc83cbe4b19624ad6270ca9163ac2ac410129db930d82c41123bb6566c58dad9dd0c96e458c590ce7bb3c196253bb6566c58dad90d2e93"
    "c7b5384ccf7bb3c1164a8e550ce9dccd6059b2634ba4b4b32374d0128126edec0697c9e35a1cd2b99b41c7b5384ccf7bb3c196253bb6566c58dad90d"
    "2ec9b18a219dbb196c59b283687676839608344b3bbbc165f2b816433a7733589263add8b0b4b31b5c268f6b71989ef766832d4b02814fa8612c629f"
    "fd0e417358c621041c79700c9fcacf4fcd6398d12c9db3dd437ed5000311a8741289b6e61560b24cae999757d519af14bbb76c90f7c111f95df28350"
    "8d7ce7d96acbe4566451f00e5f254f0d902a98488cc837e7dd70483e568f4a277a58e58afa6096158652e97a60b77dc1955f4da20594b4df9654fda9"
    "06554a15519702cdb0dba34ff96931dedfa5bea5cf9751915e82c32fb13f8f04293698f817f79abfd3c6c1369bad0ff82a040a007790191700150016"
    "009b0db62cd9b1b562c3d2cedd0c96e458c5909ef766990ce9dccd60498ead151b96767683cbe4712d0ed3f30ec5617ade9b0db62cd9b1b5220c4b3b"
    "bbc165f2b88a21331344a89a3066c60844443582a03044a80983022d6cb5d9dd1c5c268f6b80a7a89054f00660b957071288200cfeffff95dc379810"
    "64a06c62f1744d26ea4c33ef252fb80925933729b954defd923689b2b609ca2ac8e04d70ed661ab3aab240bed3c53261b55bbe732137f9bcdd265796"
    "6d72b67c2646a7d5e465b94dee6ecbe4d5f29b8cacddf46b65637713ee66676272bdbe6f6074656212155c96a401e92522e05cfab5c2005b26ce807d"
    "891c6832f94f2303ad921b50fbd2039356fbd35b5dade171aec0084b532578199dbb2aba5e6bda9aaa5eedada563a6591d2f4d3510e8557945dee412"
    "b964d657552b9a6a1a30003fac090062c50806e06d92362166c0b6057996b94185516e552b05afe54a13ee05daa5681c07f673783880a8a8003f601f"
    "12f8ffff7f5c90fe1e6fb0c965d964ad2a1b4d588b4cdcd5262dcb26d52513b761939f5a5e36a14c9b968a58fdb477cae4a92edb26ce33eff1059b6c"
    "bb4cbeda32db84bcca64a99d59dc67596cd24310f59eefa2c9cf8a1e702d93f3b68950694e98f8469b08f579d9c450b5b64c5607b289eb5c7c7cc126"
    "cb967cf29526846adb64425d6c5259e51f2f64e234da64d9365d2b96d126bc2dd3ada28c3619ebd2b68967a0c979b589b1904fb69409e3b489a13a0f"
    "4d0f9550cb23eff4459b30974c54549d379b5c569b5c8b4c9a95b2d344abb7619396ad7cf8a24d947df6b83f56cab689ef28936b49beca8536b90c9b"
    "7e2a73e39d7d6993dc2513d2b449a1268f4d0c5571802e02550800b384060b102202b31114b4fd93c5086f4ca7654cc380e6191409ef3702809bfc5f"
    "847ade3671196ce2d4e2041e79d926e44526d29679c2254dc893262a834dc425d3ad72196d22af34116ad2249379619365d96457f2059a54169b94b7"
    "4d5a964d58432667d5b16ca23a6452db88b72e6daa562c93a64a75dd36719936f96e81972fdac4a7e7c92694c526de2a93cec97bf8cfab4c96cab369"
    "82bbca3762be866a1e68faa8a41526e7c52695659356e58d3611aab35e06f5c0ff3e80f2591798bceab854befa6a13570d1732f95594d526ee9249a7"
    "a26c9b9e1565b449a73e974d5c069b8cb533a2177ee46113ca0e3ff81f17efe22f8f69f28cd02f2e65823c790f5edbe4b9813f1622";
inline Bytes unhex(const char* text){
    auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    Bytes out;for(size_t i=0;text[i]&&text[i+1];i+=2)out.push_back(uint8_t(digit(text[i])*16+digit(text[i+1])));return out;
}
inline std::string zstdContent(){
    std::string out;uint32_t state=12345;
    for(int i=0;i<24000;++i){state=(state*1103515245u+12345u)&0x7fffffffu;out+="v "+std::to_string(i%37)+" "+std::to_string(i%11)+" "+std::to_string(i%97==0?(state>>16)%7:0)+"\n";}
    return out;
}
// Zstandard frames built by hand. `window` is the window descriptor byte of a
// frame without a content size; the block maximum is min(window, 128 KiB).
inline Bytes zstdFrame(uint8_t window,const std::vector<std::tuple<bool,int,Bytes,size_t>>& blocks){
    Bytes out={0x28,0xB5,0x2F,0xFD,0x00,window};
    for(auto& [last,type,content,size]:blocks){uint32_t header=uint32_t(size<<3)|uint32_t(type<<1)|(last?1u:0u);
        out.push_back(uint8_t(header));out.push_back(uint8_t(header>>8));out.push_back(uint8_t(header>>16));out.insert(out.end(),content.begin(),content.end());}
    return out;
}
// One stored byte, then a compressed block of `count` sequences coded with RLE
// tables (literal length 0, offset code 2, match length code 52) whose extra
// bits are all zero: each sequence copies 65,539 bytes from offset 1.
inline Bytes zstdLongMatches(unsigned count){
    Bytes content={0x00,uint8_t(0x80+(count>>8)),uint8_t(count&0xFF),0x54,0,2,52};
    size_t bits=size_t(count)*18,bytes=bits/8+1;Bytes stream(bytes,0);stream.back()=uint8_t(1u<<(bits-(bytes-1)*8));
    content.insert(content.end(),stream.begin(),stream.end());
    return zstdFrame(0x38,{{false,0,Bytes{'x'},1},{true,2,content,content.size()}});
}
#ifdef _WIN32
inline size_t peakPrivateBytes(){PROCESS_MEMORY_COUNTERS counters{};GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof counters);return counters.PeakPagefileUsage;}
#else
// The peak resident size: what the decoder wrote, as allocations are filled when made.
inline size_t peakPrivateBytes(){rusage usage{};getrusage(RUSAGE_SELF,&usage);return size_t(usage.ru_maxrss)*1024;}
#endif
// A Zstandard frame of raw (stored) blocks, as a size-checked container test.
inline Bytes zstdStored(const Bytes& in){
    Bytes out={0x28,0xB5,0x2F,0xFD,0xE0};uint64_t size=in.size();for(int i=0;i<8;++i)out.push_back(uint8_t(size>>(8*i)));
    size_t at=0;do{size_t n=std::min<size_t>(in.size()-at,100000);bool last=at+n==in.size();uint32_t header=uint32_t(n<<3)|(last?1u:0u);
        out.push_back(uint8_t(header));out.push_back(uint8_t(header>>8));out.push_back(uint8_t(header>>16));out.insert(out.end(),in.begin()+ptrdiff_t(at),in.begin()+ptrdiff_t(at+n));at+=n;}while(at<in.size());
    return out;
}
// Writes .blend files with a small structure catalogue (SDNA) of its own.
class Writer {
    std::vector<std::string> names,types;std::vector<uint16_t> lengths;
    struct Def{uint16_t type;std::vector<std::pair<uint16_t,uint16_t>> members;};std::vector<Def> defs;
    std::map<std::string,std::map<std::string,size_t>> offsets;
    struct Out{char code[4];uint64_t old;Bytes data;};std::vector<Out> blocks;
    uint16_t intern(std::vector<std::string>& list,const std::string& s){for(size_t i=0;i<list.size();++i)if(list[i]==s)return uint16_t(i);list.push_back(s);return uint16_t(list.size()-1);}
    uint16_t type(const std::string& t,uint16_t length=0){auto i=intern(types,t);if(lengths.size()<types.size())lengths.push_back(length);return i;}
public:
    Writer(){for(auto [t,n]:std::vector<std::pair<const char*,uint16_t>>{{"char",1},{"short",2},{"int",4},{"float",4},{"int64_t",8},{"void",0}})type(t,n);}
    void define(const std::string& name,std::vector<std::pair<std::string,std::string>> members){
        Def d{type(name)};size_t offset=0;
        for(auto& [t,n]:members){
            bool pointer=n[0]=='*';size_t count=1,number=0;bool inArray=false;std::string base;
            for(char c:n){if(c=='['){inArray=true;number=0;continue;}if(c==']'){count*=number;inArray=false;continue;}if(inArray){number=number*10+size_t(c-'0');continue;}if(c!='*')base+=c;}
            auto ti=type(t);offsets[name][base]=offset;offset+=(pointer?8:lengths[ti])*count;
            d.members.push_back({ti,intern(names,n)});
        }
        lengths[d.type]=uint16_t(offset);defs.push_back(d);
    }
    size_t size(const std::string& s)const{return lengths[size_t(std::find(types.begin(),types.end(),s)-types.begin())];}
    // Declares a different struct length in the catalogue (damaged-file tests).
    void declareLength(const std::string& s,uint16_t length){lengths[size_t(std::find(types.begin(),types.end(),s)-types.begin())]=length;}
    size_t at(const std::string& s,const std::string& member)const{return offsets.at(s).at(member);}
    template<class T>static void put(Bytes& b,size_t offset,T value){std::memcpy(b.data()+offset,&value,sizeof(T));}
    void block(const char* code,uint64_t old,Bytes data){Out o{};std::memcpy(o.code,code,4);o.old=old;o.data=std::move(data);blocks.push_back(std::move(o));}
    Bytes file(bool large)const{
        Bytes out;auto raw=[&](const void* p,size_t n){auto q=static_cast<const uint8_t*>(p);out.insert(out.end(),q,q+n);};
        auto u32=[&](uint32_t v){raw(&v,4);};auto i64=[&](int64_t v){raw(&v,8);};auto u64=[&](uint64_t v){raw(&v,8);};
        raw(large?"BLENDER17-01v0500":"BLENDER-v305",large?17:12);
        auto head=[&](const char* code,uint64_t old,size_t length){
            raw(code,4);if(large){u32(0);u64(old);i64(int64_t(length));i64(1);}else{u32(uint32_t(length));u64(old);u32(0);u32(1);}
        };
        for(auto& b:blocks){head(b.code,b.old,b.data.size());raw(b.data.data(),b.data.size());}
        Bytes dna;auto put4=[&](const char* s){dna.insert(dna.end(),s,s+4);};auto n32=[&](uint32_t v){dna.insert(dna.end(),reinterpret_cast<uint8_t*>(&v),reinterpret_cast<uint8_t*>(&v)+4);};
        auto n16=[&](uint16_t v){dna.push_back(uint8_t(v));dna.push_back(uint8_t(v>>8));};auto align=[&]{while(dna.size()%4)dna.push_back(0);};
        put4("SDNA");put4("NAME");n32(uint32_t(names.size()));for(auto& s:names){dna.insert(dna.end(),s.begin(),s.end());dna.push_back(0);}align();
        put4("TYPE");n32(uint32_t(types.size()));for(auto& s:types){dna.insert(dna.end(),s.begin(),s.end());dna.push_back(0);}align();
        put4("TLEN");for(auto l:lengths)n16(l);align();
        put4("STRC");n32(uint32_t(defs.size()));for(auto& d:defs){n16(d.type);n16(uint16_t(d.members.size()));for(auto [t,n]:d.members){n16(t);n16(n);}}
        head("DNA1",0,dna.size());raw(dna.data(),dna.size());head("ENDB",0,0);
        return out;
    }
};
// Two meshes whose DATA blocks reuse the same addresses (as Blender 4.x+ does
// per ID), parenting, Euler and quaternion rotation, a mirrored object, a
// concave n-gon, a hidden object and a coloured material.
inline Writer scene(){
    Writer w;
    w.define("ID",{{"char","name[64]"},{"void","*lib"}});
    w.define("ListBase",{{"void","*first"},{"void","*last"}});
    w.define("Attribute",{{"char","*name"},{"short","data_type"},{"char","domain"},{"char","storage_type"},{"char","_pad[4]"},{"void","*data"}});
    w.define("AttributeArray",{{"void","*data"},{"void","*sharing_info"},{"int64_t","size"}});
    w.define("AttributeSingle",{{"void","*data"},{"void","*sharing_info"}});
    w.define("AttributeStorage",{{"Attribute","*dna_attributes"},{"int","dna_attributes_num"},{"char","_pad[4]"},{"void","*runtime"}});
    w.define("Material",{{"ID","id"},{"float","r"},{"float","g"},{"float","b"},{"float","a"},{"char","blend_method"},{"char","blend_flag"},{"char","_pad[6]"},{"void","*nodetree"}});
    w.define("Mesh",{{"ID","id"},{"int","totvert"},{"int","totpoly"},{"int","totloop"},{"short","totcol"},{"short","_pad"},{"Material","**mat"},{"int","*poly_offset_indices"},{"AttributeStorage","attribute_storage"},{"char","*default_uv_map_attribute"}});
    w.define("Object",{{"ID","id"},{"short","type"},{"short","rotmode"},{"short","restrictflag"},{"short","totcol"},{"int","partype"},{"int","_pad"},{"void","*data"},{"Object","*parent"},
        {"float","parentinv[4][4]"},{"float","loc[3]"},{"float","dloc[3]"},{"float","rot[3]"},{"float","drot[3]"},{"float","quat[4]"},{"float","dquat[4]"},{"float","size[3]"},{"float","dscale[3]"},
        {"Material","**mat"},{"char","*matbits"},{"ListBase","modifiers"}});
    auto id=[&](Bytes& b,const std::string& type,const std::string& name){std::memcpy(b.data()+w.at(type,"id")+w.at("ID","name"),name.data(),name.size());};
    uint64_t red=0x4000;{Bytes m(w.size("Material"));id(m,"Material","MARed");Writer::put(m,w.at("Material","r"),1.f);Writer::put(m,w.at("Material","a"),1.f);w.block("MA\0\0",red,m);}
    auto mesh=[&](uint64_t address,const std::string& name,const std::vector<std::array<float,3>>& points,const std::vector<std::vector<int32_t>>& faces,bool withMaterial){
        std::vector<int32_t> offsets{0},corners;for(auto& f:faces){corners.insert(corners.end(),f.begin(),f.end());offsets.push_back(int32_t(corners.size()));}
        Bytes me(w.size("Mesh"));id(me,"Mesh","ME"+name);
        Writer::put(me,w.at("Mesh","totvert"),int32_t(points.size()));Writer::put(me,w.at("Mesh","totpoly"),int32_t(faces.size()));Writer::put(me,w.at("Mesh","totloop"),int32_t(corners.size()));
        Writer::put(me,w.at("Mesh","totcol"),int16_t(withMaterial?1:0));Writer::put(me,w.at("Mesh","mat"),uint64_t(withMaterial?0x150:0));
        Writer::put(me,w.at("Mesh","poly_offset_indices"),uint64_t(0x140));
        size_t storage=w.at("Mesh","attribute_storage");Writer::put(me,storage+w.at("AttributeStorage","dna_attributes"),uint64_t(0x100));Writer::put(me,storage+w.at("AttributeStorage","dna_attributes_num"),int32_t(3));
        Writer::put(me,w.at("Mesh","default_uv_map_attribute"),uint64_t(0x113));
        w.block("ME\0\0",address,me);
        // Every mesh's data uses the same addresses 0x100..0x150.
        Bytes attrs(3*w.size("Attribute"));
        auto attr=[&](int i,uint64_t nameAt,int16_t type,int8_t domain,int8_t storageType,uint64_t data){size_t o=size_t(i)*w.size("Attribute");
            Writer::put(attrs,o+w.at("Attribute","name"),nameAt);Writer::put(attrs,o+w.at("Attribute","data_type"),type);Writer::put(attrs,o+w.at("Attribute","domain"),domain);
            Writer::put(attrs,o+w.at("Attribute","storage_type"),storageType);Writer::put(attrs,o+w.at("Attribute","data"),data);};
        attr(0,0x110,7,0,0,0x120);attr(1,0x111,3,3,0,0x121);attr(2,0x112,0,2,1,0x122);
        w.block("DATA",0x100,attrs);
        auto text=[&](uint64_t at,const std::string& s){Bytes b(s.begin(),s.end());b.push_back(0);w.block("DATA",at,b);};
        text(0x110,"position");text(0x111,".corner_vert");text(0x112,"sharp_face");text(0x113,"UVMap");
        auto array=[&](uint64_t at,uint64_t data,int64_t count){Bytes b(w.size("AttributeArray"));Writer::put(b,w.at("AttributeArray","data"),data);Writer::put(b,w.at("AttributeArray","size"),count);w.block("DATA",at,b);};
        array(0x120,0x130,int64_t(points.size()));array(0x121,0x131,int64_t(corners.size()));
        {Bytes b(w.size("AttributeSingle"));Writer::put(b,w.at("AttributeSingle","data"),uint64_t(0x132));w.block("DATA",0x122,b);}
        {Bytes b(points.size()*12);std::memcpy(b.data(),points.data(),b.size());w.block("DATA",0x130,b);}
        {Bytes b(corners.size()*4);std::memcpy(b.data(),corners.data(),b.size());w.block("DATA",0x131,b);}
        w.block("DATA",0x132,Bytes{1}); // flat shading, stored once for every face
        {Bytes b(offsets.size()*4);std::memcpy(b.data(),offsets.data(),b.size());w.block("DATA",0x140,b);}
        if(withMaterial){Bytes b(8);Writer::put(b,0,red);w.block("DATA",0x150,b);}
    };
    mesh(0x10000,"Box",{{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f},{-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}},
        {{0,3,2,1},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}},true);
    mesh(0x20000,"Roof",{{0,0,0},{2,0,0},{2,2,0},{1,.5f,0},{0,2,0}},{{0,1,2,3,4}},false);
    auto object=[&](uint64_t address,const std::string& name,uint64_t data,uint64_t parent,std::array<float,3> loc,int16_t rotmode,std::array<float,3> rot,std::array<float,4> quat,std::array<float,3> size,int16_t restrict){
        Bytes ob(w.size("Object"));id(ob,"Object","OB"+name);
        Writer::put(ob,w.at("Object","type"),int16_t(1));Writer::put(ob,w.at("Object","rotmode"),rotmode);Writer::put(ob,w.at("Object","restrictflag"),restrict);
        Writer::put(ob,w.at("Object","data"),data);Writer::put(ob,w.at("Object","parent"),parent);
        for(int i=0;i<4;++i)Writer::put(ob,w.at("Object","parentinv")+size_t(i*5)*4,1.f);
        for(size_t i=0;i<3;++i){Writer::put(ob,w.at("Object","loc")+i*4,loc[i]);Writer::put(ob,w.at("Object","rot")+i*4,rot[i]);Writer::put(ob,w.at("Object","size")+i*4,size[i]);Writer::put(ob,w.at("Object","dscale")+i*4,1.f);}
        for(size_t i=0;i<4;++i)Writer::put(ob,w.at("Object","quat")+i*4,quat[i]);Writer::put(ob,w.at("Object","dquat"),1.f);
        w.block("OB\0\0",address,ob);
    };
    const float quarter=1.57079633f;
    object(0x30000,"Box",0x10000,0,{1,2,3},1,{0,0,quarter},{1,0,0,0},{2,1,1},0);
    object(0x30001,"Roof",0x20000,0x30000,{0,0,1},0,{0,0,0},{1,0,0,0},{1,1,1},0);
    object(0x30002,"Mirror",0x10000,0,{-5,0,0},0,{0,0,0},{1,0,0,0},{-1,1,1},0);
    object(0x30003,"Hidden",0x10000,0,{0,0,-9},0,{0,0,0},{1,0,0,0},{1,1,1},1);
    return w;
}
// One quad stored the Blender 2.7x way: MVert, MPoly and MLoop struct arrays
// read member by member through the catalogue.
inline Writer legacyQuad(){
    Writer w;
    w.define("ID",{{"char","name[64]"},{"void","*lib"}});
    w.define("MVert",{{"float","co[3]"},{"short","no[3]"},{"char","flag"},{"char","bweight"}});
    w.define("MPoly",{{"int","loopstart"},{"int","totloop"},{"short","mat_nr"},{"char","flag"},{"char","_pad"}});
    w.define("MLoop",{{"int","v"},{"int","e"}});
    w.define("Mesh",{{"ID","id"},{"MVert","*mvert"},{"MPoly","*mpoly"},{"MLoop","*mloop"},{"int","totvert"},{"int","totpoly"},{"int","totloop"}});
    w.define("Object",{{"ID","id"},{"short","type"},{"short","rotmode"},{"int","_pad"},{"void","*data"},{"float","loc[3]"},{"float","rot[3]"},{"float","size[3]"}});
    Bytes me(w.size("Mesh"));std::memcpy(me.data()+w.at("Mesh","id")+w.at("ID","name"),"MEQuad",6);
    Writer::put(me,w.at("Mesh","mvert"),uint64_t(0x100));Writer::put(me,w.at("Mesh","mpoly"),uint64_t(0x200));Writer::put(me,w.at("Mesh","mloop"),uint64_t(0x300));
    Writer::put(me,w.at("Mesh","totvert"),int32_t(4));Writer::put(me,w.at("Mesh","totpoly"),int32_t(1));Writer::put(me,w.at("Mesh","totloop"),int32_t(4));
    w.block("ME\0\0",0x10000,me);
    Bytes verts(4*w.size("MVert"));const float corners[4][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
    for(size_t i=0;i<4;++i)for(size_t k=0;k<3;++k)Writer::put(verts,i*w.size("MVert")+w.at("MVert","co")+k*4,corners[i][k]);
    w.block("DATA",0x100,verts);
    Bytes poly(w.size("MPoly"));Writer::put(poly,w.at("MPoly","loopstart"),int32_t(0));Writer::put(poly,w.at("MPoly","totloop"),int32_t(4));w.block("DATA",0x200,poly);
    Bytes loops(4*w.size("MLoop"));for(size_t i=0;i<4;++i)Writer::put(loops,i*w.size("MLoop")+w.at("MLoop","v"),int32_t(i));w.block("DATA",0x300,loops);
    Bytes ob(w.size("Object"));std::memcpy(ob.data()+w.at("Object","id")+w.at("ID","name"),"OBQuad",6);
    Writer::put(ob,w.at("Object","type"),int16_t(1));Writer::put(ob,w.at("Object","data"),uint64_t(0x10000));
    for(size_t i=0;i<3;++i)Writer::put(ob,w.at("Object","size")+i*4,1.f);
    w.block("OB\0\0",0x30000,ob);
    return w;
}
struct Stats{Vec min{1e9f,1e9f,1e9f},max{-1e9f,-1e9f,-1e9f};double volume=0,area=0;bool up=true;};
inline Stats stats(const Asset& a){
    Stats s;for(auto& v:a.vertices){s.min={std::min(s.min.x,v.pos.x),std::min(s.min.y,v.pos.y),std::min(s.min.z,v.pos.z)};s.max={std::max(s.max.x,v.pos.x),std::max(s.max.y,v.pos.y),std::max(s.max.z,v.pos.z)};}
    for(size_t i=0;i+2<a.indices.size();i+=3){auto p=a.vertices[a.indices[i]].pos,q=a.vertices[a.indices[i+1]].pos,r=a.vertices[a.indices[i+2]].pos;
        s.volume+=p.dot(q.cross(r))/6;auto n=(q-p).cross(r-p);s.area+=n.length()/2;s.up&=n.z>0;}
    return s;
}
}
void blendTests(){
    using namespace blendtest;
    // Zstandard: a real encoder's output, stored frames, truncation and the size limit.
    auto compressed=unhex(ZstdVector);auto expected=zstdContent();
    auto decoded=zstdDecompress(compressed,1u<<30);
    check(decoded.size()==expected.size()&&std::equal(decoded.begin(),decoded.end(),expected.begin()),"zstd vector decodes wrong");
    for(size_t n=0;n<compressed.size();n+=37)rejects([&]{zstdDecompress(std::span(compressed).first(n),1u<<30);},"truncated zstd frame accepted");
    rejects([&]{zstdDecompress(compressed,100000);},"zstd output limit ignored");
    Bytes plain(250000);for(size_t i=0;i<plain.size();++i)plain[i]=uint8_t(i*31);
    check(zstdDecompress(zstdStored(plain),1u<<30)==plain,"stored zstd blocks decode wrong");
    for(int trial=0;trial<200;++trial){auto bad=compressed;bad[4+size_t(trial*7919)%(bad.size()-4)]^=uint8_t(1+trial%255);
        try{zstdDecompress(bad,1u<<24);}catch(const std::exception&){}} // corrupt input must fail cleanly, never crash
    // Block_Maximum_Size also bounds decompressed blocks; the budget holds before each append.
    check(zstdDecompress(zstdLongMatches(1),1u<<30)==Bytes(65540,'x'),"hand-built zstd sequences decode wrong");
    rejects([&]{zstdDecompress(zstdLongMatches(2),1u<<30);},"zstd block past its maximum size accepted");
    check(zstdDecompress(zstdFrame(0x00,{{true,1,Bytes{'y'},1000}}),1u<<30)==Bytes(1000,'y'),"zstd RLE block decodes wrong");
    rejects([&]{zstdDecompress(zstdFrame(0x00,{{true,1,Bytes{'y'},2000}}),1u<<30);},"zstd RLE block past the window accepted");
    rejects([&]{zstdDecompress(zstdFrame(0x38,{{false,1,Bytes{'y'},100000},{true,1,Bytes{'y'},100000}}),150000);},"zstd RLE output limit ignored");
    {   // 16,000 matches would regenerate about 1 GB inside one block of 36 KB.
        auto hostile=zstdLongMatches(16000);auto before=peakPrivateBytes();std::string error;
        try{zstdDecompress(hostile,1u<<20);}catch(const std::exception& e){error=e.what();}
        check(error.find("maximum size")!=std::string::npos,"zstd block with repeated long matches accepted");
        check(peakPrivateBytes()-before<(64u<<20),"zstd allocated past the block bound before rejecting");
    }
    // .blend files: both header layouts, compressed, per-ID addresses, transforms.
    auto dir=fs::temp_directory_path()/fs::path(L"mmdhl-blend-test-"+std::to_wstring(processId()));fs::create_directories(dir);
    auto w=scene();auto legacy=w.file(false),large=w.file(true);
    auto save=[&](const std::wstring& name,const Bytes& bytes){auto p=dir/name;std::ofstream(p,std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));return p;};
    const float u=39.3700787f;auto same=[&](Vec a,Vec b){return std::abs(a.x-b.x)<1e-3f&&std::abs(a.y-b.y)<1e-3f&&std::abs(a.z-b.z)<1e-3f;};
    for(auto [name,bytes]:std::vector<std::pair<std::wstring,Bytes>>{{L"legacy.blend",legacy},{L"large.blend",large},{L"zstd.blend",zstdStored(large)}}){
        auto path=save(name,bytes);Options o;
        auto listed=listBlendObjects(path,o,{});
        check(listed["objects"].size()==4,"blend object listing count");
        for(auto& ob:listed["objects"])check(ob["hidden"].get<bool>()==(ob["name"]=="Hidden"),"blend hidden flag");
        auto all=importBlend(path,o,{});
        check(all.manifest["blend_objects"].size()==3,"hidden object imported by default");
        o.objects={"Box"};auto box=importBlend(path,o,{});auto s=stats(box);
        check(same(s.min,Vec{.5f,1,2.5f}*u)&&same(s.max,Vec{1.5f,3,3.5f}*u),"blend Euler transform bounds");
        check(std::abs(s.volume-2*u*u*u)<1,"blend box volume or winding");
        check(box.manifest["materials"].size()==1&&box.manifest["materials"][0]["name"]=="Red"&&box.manifest["materials"][0]["color"][1].get<float>()==0,"blend material slot");
        check(box.vertices.size()==24,"flat faces share vertices");
        o.objects={"Roof"};auto roof=importBlend(path,o,{});s=stats(roof);
        check(same(s.min,Vec{-1,2,4}*u)&&same(s.max,Vec{1,6,4}*u),"blend parent transform or per-ID data addresses");
        check(roof.indices.size()==9&&std::abs(s.area-5*u*u)<.5&&s.up,"concave n-gon triangulation");
        o.objects={"Mirror"};s=stats(importBlend(path,o,{}));
        check(std::abs(s.volume-u*u*u)<1,"mirrored object winding");
        o.objects={"Nothing"};rejects([&]{importBlend(path,o,{});},"missing object selection accepted");
    }
    for(size_t n=0;n<legacy.size();n+=std::max<size_t>(1,legacy.size()/50)){auto path=save(L"cut.blend",Bytes(legacy.begin(),legacy.begin()+ptrdiff_t(n)));rejects([&]{listBlendObjects(path,{},{});},"truncated blend accepted");}
    // Legacy struct arrays: declared lengths come from the file, so a damaged
    // catalogue must fail as an import error, never divide by zero or read
    // past the record it describes.
    {   auto quad=importBlend(save(L"quad.blend",legacyQuad().file(false)),{},{});
        check(quad.indices.size()==6&&std::abs(stats(quad).area-u*u)<.5,"legacy MVert/MPoly/MLoop mesh");
        for(auto [type,length]:std::vector<std::pair<const char*,uint16_t>>{{"MVert",0},{"MVert",8},{"MPoly",0},{"MPoly",4},{"MLoop",0},{"MLoop",2}}){
            auto damaged=legacyQuad();damaged.declareLength(type,length);auto path=save(L"damaged.blend",damaged.file(false));
            rejects([&]{importBlend(path,{},{});},"blend struct array with a damaged declared length accepted");}
    }
    auto bad=legacy;bad[0]='X';rejects([&]{listBlendObjects(save(L"bad.blend",bad),{},{});},"non-blend header accepted");
    bad=large;bad[11]='9';rejects([&]{listBlendObjects(save(L"bad.blend",bad),{},{});},"unknown blend format version accepted");
    fs::remove_all(dir);
}
