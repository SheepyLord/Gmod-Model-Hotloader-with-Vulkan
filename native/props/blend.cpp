// Blender .blend reader for static props. Reads the file's own structure
// catalogue (SDNA), so layouts from Blender 2.63 to 5.x load without Blender:
// meshes (legacy MVert/MPoly/MLoop, 4.x CustomData attributes and 5.x
// attribute storage), object transforms and parenting, material colours and
// image textures (packed or on disk). Modifiers, curves and instanced
// collections are not evaluated; the import warnings say so.
#include "core.hpp"
#include "texture_resolver.hpp"
#include "zstd_decode.hpp"
#include <algorithm>
#include <climits>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <zlib.h>

namespace props {
namespace {
[[noreturn]] void bad(const std::string& what){throw std::runtime_error("Blender file: "+what);}
constexpr float BlenderUnits=39.3700787f; // metres to Source units (inches)

Bytes gunzip(std::span<const uint8_t> src,uint64_t limit){
    if(src.size()>UINT_MAX)bad("compressed data is too large");
    z_stream z{};if(inflateInit2(&z,16+MAX_WBITS)!=Z_OK)bad("cannot start the gzip decoder");
    Bytes out(size_t(std::min<uint64_t>(limit,std::max<uint64_t>(uint64_t(src.size())*4,1u<<20))));
    z.next_in=const_cast<Bytef*>(src.data());z.avail_in=uInt(src.size());
    for(;;){
        if(z.total_out==out.size()){
            if(out.size()>=limit){inflateEnd(&z);bad("the decompressed file exceeds the size limit");}
            out.resize(size_t(std::min<uint64_t>(limit,uint64_t(out.size())*2)));
        }
        z.next_out=out.data()+z.total_out;z.avail_out=uInt(std::min<size_t>(out.size()-z.total_out,UINT_MAX));
        int rc=inflate(&z,Z_NO_FLUSH);
        if(rc==Z_STREAM_END)break;
        if(rc!=Z_OK||(z.avail_in==0&&z.avail_out!=0)){inflateEnd(&z);bad("damaged or truncated gzip data");}
    }
    out.resize(z.total_out);inflateEnd(&z);return out;
}

struct Member{std::string type;size_t offset=0,size=0,count=1;bool pointer=false;};
struct Struct{std::string name;size_t size=0;bool id=false;std::unordered_map<std::string,Member> members;};
// owner: the ID block a DATA block was written after. Blender 4.x+ writes
// deterministic addresses that are only unique within one ID's data.
struct Block{char code[4]{};uint64_t old=0;size_t offset=0,length=0,owner=0;bool data=false;};
class BlendFile;
// A struct instance inside the file, read through its SDNA layout. Pointers
// it holds resolve within its owning ID first (scope).
struct View {
    const BlendFile* file=nullptr;const Struct* type=nullptr;size_t at=0;uint64_t address=0;size_t scope=SIZE_MAX;
    explicit operator bool()const{return file&&type;}
    const Member* member(std::string_view name)const{if(!type)return nullptr;auto it=type->members.find(std::string(name));return it==type->members.end()?nullptr:&it->second;}
    bool has(std::string_view name)const{return member(name)!=nullptr;}
    // Blender renames members between versions; the file keeps one of the names.
    std::string_view pick(std::initializer_list<std::string_view> names)const{for(auto n:names)if(has(n))return n;return {};}
    double num(std::string_view name,size_t index=0,double fallback=0)const;
    uint64_t ptr(std::string_view name,size_t index=0)const;
    std::string str(std::string_view name)const;
    View sub(std::string_view name)const;
    View deref(uint64_t target,std::string_view typeName,size_t index=0)const;
    View follow(std::string_view name,std::string_view typeName)const{return deref(ptr(name),typeName);}
    std::span<const uint8_t> raw(uint64_t target)const;
    std::string cstring(uint64_t target)const;
    uint64_t pointerIn(uint64_t arrayAddress,size_t index)const;
    template<class T>std::vector<T> array(uint64_t target,size_t count,const char* what)const;
};
class BlendFile {
public:
    Bytes data;bool ptr8=true;int version=0;std::string compression="none";
    std::vector<Block> blocks;std::unordered_map<uint64_t,std::vector<uint32_t>> byAddress;
    std::vector<Struct> structs;std::unordered_map<std::string,size_t> byName;
    BlendFile(Bytes raw,uint64_t limit){
        if(raw.size()>=2&&raw[0]==0x1F&&raw[1]==0x8B){data=gunzip(raw,limit);compression="gzip";}
        else if(raw.size()>=4&&raw[0]==0x28&&raw[1]==0xB5&&raw[2]==0x2F&&raw[3]==0xFD){data=zstdDecompress(raw,limit);compression="zstd";}
        else data=std::move(raw);
        if(data.size()<12||std::memcmp(data.data(),"BLENDER",7))throw std::runtime_error("Not a Blender file: the BLENDER header is missing");
        auto digit=[&](size_t i){return i<data.size()&&data[i]>='0'&&data[i]<='9';};
        enum{Head4,Small8,Large8} layout;size_t p;char endian;
        if(data[7]=='_'||data[7]=='-'){
            ptr8=data[7]=='-';endian=char(data[8]);if(!digit(9)||!digit(10)||!digit(11))bad("invalid version in the header");
            version=(data[9]-'0')*100+(data[10]-'0')*10+(data[11]-'0');layout=ptr8?Small8:Head4;p=12;
        }else if(digit(7)&&digit(8)){
            size_t header=size_t((data[7]-'0')*10+(data[8]-'0'));
            if(header<17||data.size()<header||data[9]!='-'||!digit(10)||!digit(11)||!digit(13)||!digit(14)||!digit(15)||!digit(16))bad("invalid header");
            int format=(data[10]-'0')*10+(data[11]-'0');
            if(format!=1)bad("file format "+std::to_string(format)+" is newer than this importer; export the model as GLB or FBX");
            endian=char(data[12]);version=(data[13]-'0')*1000+(data[14]-'0')*100+(data[15]-'0')*10+(data[16]-'0');layout=Large8;p=header;
        }else bad("invalid header");
        if(endian!='v')bad("big-endian files from PowerPC-era Blender are not supported; open and save the file in a current Blender");
        bool ended=false;size_t owner=0;
        while(p<data.size()){
            Block b;int64_t length=0;
            if(layout==Large8){at(p,32);std::memcpy(b.code,data.data()+p,4);b.old=read<uint64_t>(p+8);length=read<int64_t>(p+16);p+=32;}
            else if(layout==Small8){at(p,24);std::memcpy(b.code,data.data()+p,4);length=read<int32_t>(p+4);b.old=read<uint64_t>(p+8);p+=24;}
            else{at(p,20);std::memcpy(b.code,data.data()+p,4);length=read<int32_t>(p+4);b.old=read<uint32_t>(p+8);p+=20;}
            if(!std::memcmp(b.code,"ENDB",4)){ended=true;break;}
            if(length<0||uint64_t(length)>data.size()-p)bad("a data block runs past the end of the file (the file may be truncated)");
            b.offset=p;b.length=size_t(length);p+=b.length;
            b.data=!std::memcmp(b.code,"DATA",4);if(!b.data)owner=blocks.size();b.owner=owner;
            if(blocks.size()>=UINT32_MAX)bad("too many data blocks");
            if(b.old)byAddress[b.old].push_back(uint32_t(blocks.size()));
            blocks.push_back(b);
        }
        if(!ended)bad("the end marker is missing (the file may be truncated)");
        parseCatalogue();
    }
    size_t pointerSize()const{return ptr8?8:4;}
    const uint8_t* at(size_t offset,size_t n)const{if(offset>data.size()||n>data.size()-offset)bad("a read runs past the end of the file");return data.data()+offset;}
    template<class T>T read(size_t offset)const{T v;std::memcpy(&v,at(offset,sizeof(T)),sizeof(T));return v;}
    uint64_t pointer(size_t offset)const{return ptr8?read<uint64_t>(offset):read<uint32_t>(offset);}
    const Struct* type(std::string_view name)const{auto it=byName.find(std::string(name));return it==byName.end()?nullptr:&structs[it->second];}
    // Data of the same ID first; ID blocks (objects, meshes, materials...) file-wide.
    const Block* block(uint64_t address,size_t scope,bool id)const{
        if(!address)return nullptr;auto it=byAddress.find(address);if(it==byAddress.end())return nullptr;
        const Block* scoped=nullptr;const Block* whole=nullptr;
        for(auto i:it->second){auto& b=blocks[i];if(b.data&&b.owner==scope&&!scoped)scoped=&b;if(!b.data&&!whole)whole=&b;}
        if(id)return whole?whole:scoped?scoped:&blocks[it->second.front()];
        return scoped?scoped:whole?whole:&blocks[it->second.front()];
    }
    View view(const Block& b,std::string_view typeName,size_t index=0)const{
        auto t=type(typeName);if(!t||!t->size||index>=b.length/t->size)return {};
        return {this,t,b.offset+index*t->size,index?0:b.old,b.owner};
    }
    View view(uint64_t address,std::string_view typeName,size_t scope,size_t index=0)const{
        auto t=type(typeName);auto b=block(address,scope,t&&t->id);if(!b)return {};auto v=view(*b,typeName,index);if(v&&!index)v.address=address;return v;
    }
    std::span<const uint8_t> raw(uint64_t address,size_t scope)const{auto b=block(address,scope,false);if(!b)return {};return {data.data()+b->offset,b->length};}
    // ListBase: items linked through their leading "next" pointer.
    std::vector<View> list(const View& owner,std::string_view member,std::string_view typeName)const{
        std::vector<View> out;auto m=owner.member(member);if(!m||m->pointer)return out;
        uint64_t next=pointer(owner.at+m->offset);std::unordered_set<uint64_t> seen;
        while(next&&seen.insert(next).second&&out.size()<(1u<<20)){auto v=view(next,typeName,owner.scope);if(!v)break;out.push_back(v);next=v.ptr("next");}
        return out;
    }
    const Block* first(const char* code)const{for(auto& b:blocks)if(!std::memcmp(b.code,code,4))return &b;return nullptr;}
private:
    void parseCatalogue(){
        auto dna=first("DNA1");if(!dna)bad("the structure catalogue (DNA1) is missing");
        size_t start=dna->offset,end=start+dna->length,p=start;
        auto need=[&](size_t n){if(n>end-p)bad("the structure catalogue is truncated");};
        auto tag=[&](const char* t){need(4);if(std::memcmp(data.data()+p,t,4))bad(std::string("the structure catalogue has no ")+t+" section");p+=4;};
        auto align=[&]{p=start+((p-start+3)&~size_t(3));if(p>end)bad("the structure catalogue is truncated");};
        auto count=[&]{need(4);auto n=read<int32_t>(p);p+=4;if(n<0||n>2000000)bad("invalid structure catalogue count");return size_t(n);};
        auto strings=[&](size_t n){std::vector<std::string> out;out.reserve(n);
            for(size_t i=0;i<n;++i){size_t q=p;while(q<end&&data[q])++q;if(q>=end)bad("unterminated name in the structure catalogue");out.emplace_back(reinterpret_cast<const char*>(data.data()+p),q-p);p=q+1;}
            return out;};
        tag("SDNA");tag("NAME");auto names=strings(count());align();
        tag("TYPE");auto types=strings(count());align();
        tag("TLEN");need(types.size()*2);std::vector<uint16_t> lengths(types.size());
        for(size_t i=0;i<types.size();++i)lengths[i]=read<uint16_t>(p+i*2);p+=types.size()*2;align();
        tag("STRC");auto n=count();structs.reserve(n);
        for(size_t i=0;i<n;++i){
            need(4);auto t=read<uint16_t>(p),members=read<uint16_t>(p+2);p+=4;if(t>=types.size())bad("invalid structure type");
            Struct s;s.name=types[t];s.size=lengths[t];size_t offset=0;need(size_t(members)*4);
            for(size_t k=0;k<members;++k){
                auto mt=read<uint16_t>(p),mn=read<uint16_t>(p+2);p+=4;
                if(mt>=types.size()||mn>=names.size())bad("invalid structure member");
                const auto& spelled=names[mn];Member m;m.type=types[mt];
                m.pointer=!spelled.empty()&&(spelled[0]=='*'||(spelled.size()>1&&spelled[0]=='('&&spelled[1]=='*'));
                std::string base;size_t number=0;bool inArray=false;
                for(char c:spelled){
                    if(c=='['){inArray=true;number=0;continue;}
                    if(c==']'){m.count*=number;inArray=false;continue;}
                    if(inArray){if(c>='0'&&c<='9')number=number*10+size_t(c-'0');continue;}
                    if(c!='*'&&c!='('&&c!=')')base+=c;
                }
                m.offset=offset;m.size=(m.pointer?pointerSize():lengths[mt])*m.count;offset+=m.size;
                // Blender lays members out back to back within the declared length.
                // A damaged catalogue's members beyond it are treated as missing.
                if(m.offset<=s.size&&m.size<=s.size-m.offset)s.members.emplace(std::move(base),m);
            }
            auto id=s.members.find("id");s.id=id!=s.members.end()&&id->second.type=="ID"&&id->second.offset==0;
            byName.emplace(s.name,structs.size());structs.push_back(std::move(s));
        }
    }
};
double View::num(std::string_view name,size_t index,double fallback)const{
    auto m=member(name);if(!m||m->pointer||!m->count||index>=m->count)return fallback;
    size_t element=m->size/m->count,o=at+m->offset+index*element;const auto& t=m->type;
    if(t=="float")return file->read<float>(o);
    if(t=="double")return file->read<double>(o);
    bool isUnsigned=t=="uchar"||t=="uint8_t"||t=="ushort"||t=="uint16_t"||t=="uint"||t=="uint32_t"||t=="uint64_t";
    switch(element){
    case 1:return isUnsigned?double(file->read<uint8_t>(o)):double(file->read<int8_t>(o));
    case 2:return isUnsigned?double(file->read<uint16_t>(o)):double(file->read<int16_t>(o));
    case 4:return isUnsigned?double(file->read<uint32_t>(o)):double(file->read<int32_t>(o));
    case 8:return isUnsigned?double(file->read<uint64_t>(o)):double(file->read<int64_t>(o));
    default:return fallback;
    }
}
uint64_t View::ptr(std::string_view name,size_t index)const{auto m=member(name);if(!m||!m->pointer||index>=m->count)return 0;return file->pointer(at+m->offset+index*file->pointerSize());}
std::string View::str(std::string_view name)const{auto m=member(name);if(!m||m->pointer)return {};auto p=file->at(at+m->offset,m->size);size_t n=0;while(n<m->size&&p[n])++n;return std::string(reinterpret_cast<const char*>(p),n);}
View View::sub(std::string_view name)const{auto m=member(name);if(!m||m->pointer)return {};auto t=file->type(m->type);if(!t)return {};return {file,t,at+m->offset,0,scope};}
View View::deref(uint64_t target,std::string_view typeName,size_t index)const{return file->view(target,typeName,scope,index);}
std::span<const uint8_t> View::raw(uint64_t target)const{return file->raw(target,scope);}
std::string View::cstring(uint64_t target)const{auto r=raw(target);size_t n=0;while(n<r.size()&&r[n])++n;return std::string(reinterpret_cast<const char*>(r.data()),n);}
uint64_t View::pointerIn(uint64_t arrayAddress,size_t index)const{auto r=raw(arrayAddress);auto s=file->pointerSize();if((index+1)*s>r.size())return 0;return file->pointer(size_t(r.data()-file->data.data())+index*s);}
template<class T>std::vector<T> View::array(uint64_t target,size_t count,const char* what)const{
    auto r=raw(target);if(count&&r.size()/sizeof(T)<count)bad(std::string(what)+" is missing or shorter than its element count");
    std::vector<T> out(count);if(count)std::memcpy(out.data(),r.data(),count*sizeof(T));return out;
}
std::string idName(const View& v){auto n=v.sub("id").str("name");return n.size()>2?n.substr(2):n;}

// ---- Transforms (column-major like Blender: m[column][row]) ----
struct M4{std::array<std::array<double,4>,4> m{};static M4 identity(){M4 r;for(int i=0;i<4;++i)r.m[i][i]=1;return r;}};
M4 operator*(const M4& a,const M4& b){M4 r;for(int c=0;c<4;++c)for(int row=0;row<4;++row){double s=0;for(int k=0;k<4;++k)s+=a.m[k][row]*b.m[c][k];r.m[c][row]=s;}return r;}
using M3=std::array<std::array<double,3>,3>; // row-major
M3 mul(const M3& a,const M3& b){M3 r{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k)r[i][j]+=a[i][k]*b[k][j];return r;}
M3 axisRotation(int axis,double angle){
    double c=std::cos(angle),s=std::sin(angle);M3 r{};r[axis][axis]=1;int i=(axis+1)%3,j=(axis+2)%3;
    r[i][i]=c;r[i][j]=-s;r[j][i]=s;r[j][j]=c;return r;
}
// Blender's Euler modes name the axes in the order they are applied.
M3 euler(const double e[3],int mode){
    static const int orders[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    auto& o=orders[std::clamp(mode,1,6)-1];
    return mul(axisRotation(o[2],e[o[2]]),mul(axisRotation(o[1],e[o[1]]),axisRotation(o[0],e[o[0]])));
}
M3 quaternion(double w,double x,double y,double z){
    double n=std::sqrt(w*w+x*x+y*y+z*z);if(n<1e-12||!std::isfinite(n))return {{{1,0,0},{0,1,0},{0,0,1}}};w/=n;x/=n;y/=n;z/=n;
    return {{{1-2*(y*y+z*z),2*(x*y-w*z),2*(x*z+w*y)},{2*(x*y+w*z),1-2*(x*x+z*z),2*(y*z-w*x)},{2*(x*z-w*y),2*(y*z+w*x),1-2*(x*x+y*y)}}};
}
M3 axisAngle(const double axis[3],double angle){
    double n=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);if(n<1e-12)return quaternion(1,0,0,0);
    double s=std::sin(angle/2)/n;return quaternion(std::cos(angle/2),axis[0]*s,axis[1]*s,axis[2]*s);
}
M4 readMatrix(const View& v,std::string_view name){M4 r;for(int c=0;c<4;++c)for(int row=0;row<4;++row)r.m[c][row]=v.num(name,size_t(c*4+row),c==row?1:0);return r;}
bool usable(const M4& m){
    for(auto& c:m.m)for(double x:c)if(!std::isfinite(x))return false;
    double det=m.m[0][0]*(m.m[1][1]*m.m[2][2]-m.m[2][1]*m.m[1][2])-m.m[1][0]*(m.m[0][1]*m.m[2][2]-m.m[2][1]*m.m[0][2])+m.m[2][0]*(m.m[0][1]*m.m[1][2]-m.m[1][1]*m.m[0][2]);
    return std::abs(det)>1e-18;
}
double determinant(const M4& m){return m.m[0][0]*(m.m[1][1]*m.m[2][2]-m.m[2][1]*m.m[1][2])-m.m[1][0]*(m.m[0][1]*m.m[2][2]-m.m[2][1]*m.m[0][2])+m.m[2][0]*(m.m[0][1]*m.m[1][2]-m.m[1][1]*m.m[0][2]);}
// Local matrix exactly as BKE_object_to_mat4: T(loc+dloc) * (drot*rot) * S(scale*dscale).
M4 localMatrix(const View& ob){
    auto vec=[&](std::string_view name,double fallback,double out[3]){for(size_t i=0;i<3;++i)out[i]=ob.num(name,i,fallback);};
    double loc[3],dloc[3],rot[3],drot[3],size[3],dsize[3];
    vec("loc",0,loc);vec("dloc",0,dloc);vec("rot",0,rot);vec("drot",0,drot);vec(ob.pick({"size","scale"}),1,size);
    auto dscaleName=ob.pick({"dscale","dsize"});vec(dscaleName,1,dsize);
    int mode=int(ob.num("rotmode"));M3 r;
    if(mode>=1&&mode<=6)r=mul(euler(drot,mode),euler(rot,mode));
    else if(mode==-1){double axis[3],daxis[3];vec("rotAxis",0,axis);vec("drotAxis",0,daxis);
        if(axis[0]==0&&axis[1]==0&&axis[2]==0)axis[1]=1;if(daxis[0]==0&&daxis[1]==0&&daxis[2]==0)daxis[1]=1;
        r=mul(axisAngle(daxis,ob.num("drotAngle")),axisAngle(axis,ob.num("rotAngle")));}
    else r=mul(quaternion(ob.num("dquat",0,1),ob.num("dquat",1),ob.num("dquat",2),ob.num("dquat",3)),quaternion(ob.num("quat",0,1),ob.num("quat",1),ob.num("quat",2),ob.num("quat",3)));
    M4 out=M4::identity();
    for(int c=0;c<3;++c){double s=size[c]*dsize[c];for(int row=0;row<3;++row)out.m[c][row]=r[row][c]*s;}
    for(int i=0;i<3;++i)out.m[3][i]=loc[i]+dloc[i];
    return out;
}
Vec transformPoint(const M4& m,const std::array<float,3>& p){
    return {float(m.m[0][0]*p[0]+m.m[1][0]*p[1]+m.m[2][0]*p[2]+m.m[3][0]),float(m.m[0][1]*p[0]+m.m[1][1]*p[1]+m.m[2][1]*p[2]+m.m[3][1]),float(m.m[0][2]*p[0]+m.m[1][2]*p[1]+m.m[2][2]*p[2]+m.m[3][2])};
}

// ---- Scene: objects, visibility and parenting ----
constexpr int ObMesh=1,ObArmature=25;
struct ObjectEntry{uint64_t address=0;View ob;std::string name;int type=0;bool restricted=false,inScene=false,shown=false,baseHidden=false,excluded=false;
    bool hidden()const{return restricted||baseHidden||excluded||(inScene&&!shown);}};
struct Scene{std::vector<ObjectEntry> objects;std::unordered_map<uint64_t,size_t> index;bool collections=false;};
Scene readScene(const BlendFile& f){
    Scene s;
    for(auto& b:f.blocks){
        if(std::memcmp(b.code,"OB\0\0",4))continue;
        auto v=f.view(b,"Object");if(!v)continue;
        ObjectEntry e;e.address=b.old;e.ob=v;e.name=idName(v);e.type=int(v.num("type"));
        e.restricted=(int(v.num(v.pick({"restrictflag","visibility_flag"})))&(1|4))!=0; // hidden in viewport or render
        if(b.old&&!s.index.count(b.old))s.index[b.old]=s.objects.size();
        s.objects.push_back(std::move(e));
    }
    View scene;
    if(auto glob=f.first("GLOB"))scene=f.view(*glob,"FileGlobal").follow("curscene","Scene");
    if(!scene)if(auto sc=f.first("SC\0\0"))scene=f.view(*sc,"Scene");
    if(!scene||!scene.has("master_collection")){for(auto& o:s.objects)o.inScene=o.shown=true;return s;}
    s.collections=true;
    std::function<void(const View&,bool,bool,int)> visit=[&](const View& c,bool hidden,bool excluded,int depth){
        if(!c||depth>64)return;
        hidden|=(int(c.num("flag"))&(1|8))!=0; // collection hidden in viewport or render
        for(auto& link:f.list(c,"gobject","CollectionObject")){
            auto it=s.index.find(link.ptr("ob"));if(it==s.index.end())continue;
            auto& o=s.objects[it->second];o.inScene=true;if(!hidden&&!excluded)o.shown=true;
        }
        for(auto& child:f.list(c,"children","CollectionChild"))visit(child.follow("collection","Collection"),hidden,excluded,depth+1);
    };
    // Collections excluded or hidden in the first view layer.
    std::unordered_set<size_t> excluded; // Collection struct offsets
    auto layers=f.list(scene,"view_layers","ViewLayer");
    if(!layers.empty()){
        for(auto& base:f.list(layers.front(),"object_bases","Base")){
            auto it=s.index.find(base.ptr("object"));if(it!=s.index.end()&&(int64_t(base.num("flag"))&(1<<8)))s.objects[it->second].baseHidden=true;
        }
        std::function<void(const View&,int)> walk=[&](const View& lc,int depth){
            if(depth>64)return;if(int64_t(lc.num("flag"))&((1<<4)|(1<<7)))if(auto c=lc.follow("collection","Collection"))excluded.insert(c.at);
            for(auto& child:f.list(lc,"layer_collections","LayerCollection"))walk(child,depth+1);
        };
        for(auto& lc:f.list(layers.front(),"layer_collections","LayerCollection"))walk(lc,0);
    }
    std::function<void(const View&,bool,int)> visitExcluded=[&](const View& c,bool off,int depth){
        if(!c||depth>64)return;off|=excluded.count(c.at)>0;
        for(auto& link:f.list(c,"gobject","CollectionObject")){auto it=s.index.find(link.ptr("ob"));if(it!=s.index.end()&&off)s.objects[it->second].excluded=true;}
        for(auto& child:f.list(c,"children","CollectionChild"))visitExcluded(child.follow("collection","Collection"),off,depth+1);
    };
    auto master=scene.follow("master_collection","Collection");
    visit(master,false,false,0);
    // The master collection lists only its own objects; an object also shown
    // through a visible collection stays visible.
    if(!excluded.empty())visitExcluded(master,false,0);
    return s;
}
M4 worldMatrix(const Scene& s,const ObjectEntry& o,int depth=0){
    if(o.ob.has("obmat")){auto m=readMatrix(o.ob,"obmat");if(usable(m))return m;} // saved world matrix (Blender 4.1 and older)
    auto local=localMatrix(o.ob);auto parent=o.ob.ptr("parent");
    if(!parent||depth>64)return local;
    auto it=s.index.find(parent);if(it==s.index.end())return local;
    auto inverse=readMatrix(o.ob,"parentinv");if(!usable(inverse))inverse=M4::identity();
    return worldMatrix(s,s.objects[it->second],depth+1)*inverse*local;
}

// ---- Mesh data ----
struct Attribute{int type=-1,domain=-1,storage=0;uint64_t data=0;};
struct Layer{int domain=0,type=0;std::string name;uint64_t data=0;int activeRender=0;};
struct MeshArrays{
    std::vector<std::array<float,3>> positions;std::vector<std::pair<int32_t,int32_t>> faces; // first corner, corner count
    std::vector<int32_t> corners,materials;std::vector<uint8_t> flat;std::vector<std::array<float,2>> uvs;
};
constexpr int AttrBool=0,AttrInt32=3,AttrFloat2=6,AttrFloat3=7,DomainPoint=0,DomainFace=2,DomainCorner=3,DomainTessFace=4;
MeshArrays readMesh(const BlendFile& f,const View& mesh,const std::string& name,const Limits& limits){
    auto count=[&](std::initializer_list<std::string_view> names,uint64_t max){
        double n=mesh.num(mesh.pick(names),0,0);if(n<0||n>double(max))bad("mesh "+name+" has an invalid element count");return size_t(n);};
    uint64_t cap=limits.expandedBytes/12;
    size_t verts=count({"totvert","verts_num"},cap),faces=count({"totpoly","faces_num"},cap),corners=count({"totloop","corners_num"},cap);
    std::map<std::string,Attribute> attributes;std::vector<Layer> layers;
    if(mesh.has("attribute_storage")){ // Blender 5.0+
        auto storage=mesh.sub("attribute_storage");auto n=size_t(std::max(0.,storage.num("dna_attributes_num")));auto list=storage.ptr("dna_attributes");
        for(size_t i=0;i<n;++i){
            auto a=mesh.deref(list,"Attribute",i);if(!a)break;
            Attribute at{int(a.num("data_type")),int(a.num("domain")),int(a.num("storage_type"))};
            auto data=a.ptr("data");
            at.data=at.storage==0?mesh.deref(data,"AttributeArray").ptr("data"):mesh.deref(data,"AttributeSingle").ptr("data");
            attributes[mesh.cstring(a.ptr("name"))]=at;
        }
    }
    auto readLayers=[&](std::initializer_list<std::string_view> names,int domain){
        auto which=mesh.pick(names);if(which.empty())return;auto cd=mesh.sub(which);if(!cd)return;
        auto n=size_t(std::max(0.,cd.num("totlayer")));auto list=cd.ptr("layers");
        for(size_t i=0;i<n;++i){
            auto l=mesh.deref(list,"CustomDataLayer",i);if(!l)break;
            Layer layer{domain,int(l.num("type")),l.str("name"),l.ptr("data"),int(l.num("active_rnd"))};
            layers.push_back(layer);
            static const std::map<int,int> generic={{48,AttrFloat3},{49,AttrFloat2},{11,AttrInt32},{50,AttrBool}};
            auto g=generic.find(layer.type);
            if(g!=generic.end()&&!layer.name.empty()&&!attributes.count(layer.name))attributes[layer.name]={g->second,domain,0,layer.data};
        }
    };
    readLayers({"vdata","vert_data"},DomainPoint);readLayers({"pdata","face_data"},DomainFace);readLayers({"ldata","corner_data"},DomainCorner);readLayers({"fdata","fdata_legacy"},DomainTessFace);
    auto find=[&](const std::string& n,int type,int domain)->const Attribute*{auto it=attributes.find(n);return it!=attributes.end()&&it->second.type==type&&it->second.domain==domain&&it->second.data?&it->second:nullptr;};
    auto layerOf=[&](int domain,int type)->const Layer*{for(auto& l:layers)if(l.domain==domain&&l.type==type&&l.data)return &l;return nullptr;};
    auto values=[&]<class T>(const Attribute& a,size_t n,const char* what){
        if(a.storage==1){auto one=mesh.template array<T>(a.data,1,what);return std::vector<T>(n,one[0]);}
        return mesh.template array<T>(a.data,n,what);
    };
    // Reads one member from each element of a legacy struct array. Each read
    // must lie inside its own record, whatever sizes the file declares.
    auto strided=[&]<class T>(uint64_t address,const char* structName,std::string_view memberName,size_t n,size_t index=0){
        auto t=f.type(structName);if(!t)bad(std::string("the file has no ")+structName+" layout");
        auto it=t->members.find(std::string(memberName));if(it==t->members.end())bad(std::string(structName)+" has no "+std::string(memberName));
        const auto& member=it->second;size_t element=member.size/std::max<size_t>(1,member.count);
        if(element!=sizeof(T))bad(std::string(structName)+"."+std::string(memberName)+" has an unexpected size");
        if(index>=member.count||member.offset+(index+1)*element>t->size)bad(std::string("the ")+structName+" layout is damaged");
        auto r=mesh.raw(address);if(n&&r.size()/t->size<n)bad(std::string("mesh ")+name+": "+structName+" array is too short");
        std::vector<T> out(n);for(size_t i=0;i<n;++i)std::memcpy(&out[i],r.data()+i*t->size+member.offset+index*element,sizeof(T));return out;
    };
    MeshArrays m;
    // Positions.
    if(auto a=find("position",AttrFloat3,DomainPoint))m.positions=values.template operator()<std::array<float,3>>(*a,verts,"vertex positions");
    else{
        uint64_t mv=0;if(auto l=layerOf(DomainPoint,0))mv=l->data;if(!mv)mv=mesh.ptr("mvert");
        if(!mv&&verts)bad("mesh "+name+" has no vertex positions this importer can read");
        if(verts){auto t=f.type("MVert");if(!t)bad("the file has no MVert layout");m.positions.resize(verts);
            auto r=mesh.raw(mv);auto it=t->members.find("co");
            if(it==t->members.end()||it->second.type!="float"||it->second.size!=12||it->second.offset+12>t->size||r.size()/t->size<verts)bad("mesh "+name+" vertex array is damaged");
            for(size_t i=0;i<verts;++i)std::memcpy(m.positions[i].data(),r.data()+i*t->size+it->second.offset,12);}
    }
    size_t tessellated=count({"totface","totface_legacy"},cap);
    if(!faces&&tessellated){ // Blender 2.62 and older store only triangles and quads (MFace)
        uint64_t mf=0;if(auto l=layerOf(DomainTessFace,4))mf=l->data;if(!mf)mf=mesh.ptr("mface");
        if(!mf)bad("mesh "+name+" has no face data this importer can read");
        std::array<std::vector<uint32_t>,4> v;const char* names[4]={"v1","v2","v3","v4"};
        for(size_t k=0;k<4;++k)v[k]=strided.template operator()<uint32_t>(mf,"MFace",names[k],tessellated);
        auto mat=strided.template operator()<int16_t>(mf,"MFace","mat_nr",tessellated);auto flag=strided.template operator()<int8_t>(mf,"MFace","flag",tessellated);
        std::vector<std::vector<float>> uv;
        std::vector<const Layer*> maps;for(auto& l:layers)if(l.domain==DomainTessFace&&l.type==5&&l.data)maps.push_back(&l); // MTFace
        if(!maps.empty()){auto pick=size_t(std::clamp(maps.front()->activeRender,0,int(maps.size())-1));for(size_t k=0;k<8;++k)uv.push_back(strided.template operator()<float>(maps[pick]->data,"MTFace","uv",tessellated,k));}
        for(size_t i=0;i<tessellated;++i){
            int32_t n=v[3][i]?4:3;m.faces.push_back({int32_t(m.corners.size()),n});
            for(int32_t k=0;k<n;++k){m.corners.push_back(int32_t(v[size_t(k)][i]));if(!uv.empty())m.uvs.push_back({uv[size_t(k)*2][i],uv[size_t(k)*2+1][i]});}
            m.materials.push_back(mat[i]);m.flat.push_back((flag[i]&1)?0:1);
        }
        for(auto c:m.corners)if(c<0||size_t(c)>=m.positions.size())bad("mesh "+name+" has a corner outside its vertex list");
        return m;
    }
    // Faces, corners, material indices and flat shading.
    uint64_t offsets=mesh.ptr(mesh.pick({"poly_offset_indices","face_offset_indices"}));
    std::vector<int32_t> legacyMaterials;std::vector<uint8_t> legacyFlags;
    if(offsets&&faces){
        auto o=mesh.array<int32_t>(offsets,faces+1,"face offsets");m.faces.resize(faces);
        for(size_t i=0;i<faces;++i)m.faces[i]={o[i],o[i+1]-o[i]};
    }else if(faces){
        uint64_t mp=0;if(auto l=layerOf(DomainFace,25))mp=l->data;if(!mp)mp=mesh.ptr("mpoly");
        if(!mp)bad("mesh "+name+" has no face data this importer can read");
        auto starts=strided.template operator()<int32_t>(mp,"MPoly","loopstart",faces),sizes=strided.template operator()<int32_t>(mp,"MPoly","totloop",faces);
        legacyMaterials.resize(faces);auto mat=strided.template operator()<int16_t>(mp,"MPoly","mat_nr",faces);for(size_t i=0;i<faces;++i)legacyMaterials[i]=mat[i];
        auto flags=strided.template operator()<int8_t>(mp,"MPoly","flag",faces);legacyFlags.resize(faces);for(size_t i=0;i<faces;++i)legacyFlags[i]=(flags[i]&1)?0:1; // ME_SMOOTH
        m.faces.resize(faces);for(size_t i=0;i<faces;++i)m.faces[i]={starts[i],sizes[i]};
    }
    if(auto a=find(".corner_vert",AttrInt32,DomainCorner))m.corners=values.template operator()<int32_t>(*a,corners,"corner vertices");
    else if(corners){
        uint64_t ml=0;if(auto l=layerOf(DomainCorner,26))ml=l->data;if(!ml)ml=mesh.ptr("mloop");
        if(!ml)bad("mesh "+name+" has no corner data this importer can read");
        m.corners=strided.template operator()<int32_t>(ml,"MLoop","v",corners);
    }
    if(auto a=find("material_index",AttrInt32,DomainFace))m.materials=values.template operator()<int32_t>(*a,faces,"material indices");
    else if(!legacyMaterials.empty())m.materials=std::move(legacyMaterials);
    else m.materials.assign(faces,0);
    if(auto a=find("sharp_face",AttrBool,DomainFace)){auto b=values.template operator()<uint8_t>(*a,faces,"flat shading flags");m.flat.resize(faces);for(size_t i=0;i<faces;++i)m.flat[i]=b[i]?1:0;}
    else if(!legacyFlags.empty())m.flat=std::move(legacyFlags);
    else m.flat.assign(faces,0);
    // The UV map used for rendering.
    std::string uvName;
    if(auto p=mesh.ptr("default_uv_map_attribute"))uvName=mesh.cstring(p);
    if(uvName.empty())if(auto p=mesh.ptr("active_uv_map_attribute"))uvName=mesh.cstring(p);
    const Attribute* uv=uvName.empty()?nullptr:find(uvName,AttrFloat2,DomainCorner);
    if(!uv){ // 4.x: the render-active float2 corner layer; 5.x: the first UV map
        std::vector<const Layer*> maps;for(auto& l:layers)if(l.domain==DomainCorner&&l.type==49&&l.data)maps.push_back(&l);
        if(!maps.empty()){auto pick=size_t(std::clamp(maps.front()->activeRender,0,int(maps.size())-1));uv=find(maps[pick]->name,AttrFloat2,DomainCorner);}
        if(!uv)for(auto& [n,a]:attributes)if(!n.empty()&&n[0]!='.'&&a.type==AttrFloat2&&a.domain==DomainCorner&&a.data){uv=&a;break;}
    }
    if(uv&&corners)m.uvs=values.template operator()<std::array<float,2>>(*uv,corners,"UV map");
    else if(corners){
        std::vector<const Layer*> maps;for(auto& l:layers)if(l.domain==DomainCorner&&l.type==16&&l.data)maps.push_back(&l); // MLoopUV
        if(!maps.empty()){auto pick=size_t(std::clamp(maps.front()->activeRender,0,int(maps.size())-1));
            auto u=strided.template operator()<float>(maps[pick]->data,"MLoopUV","uv",corners,0),v=strided.template operator()<float>(maps[pick]->data,"MLoopUV","uv",corners,1);
            m.uvs.resize(corners);for(size_t i=0;i<corners;++i)m.uvs[i]={u[i],v[i]};}
    }
    for(auto& [first,n]:m.faces)if(first<0||n<0||size_t(first)+size_t(n)>m.corners.size())bad("mesh "+name+" has a face outside its corner list");
    for(auto v:m.corners)if(v<0||size_t(v)>=m.positions.size())bad("mesh "+name+" has a corner outside its vertex list");
    return m;
}

// ---- Triangulation (ear clipping in the polygon plane) ----
std::vector<std::array<uint32_t,3>> triangulate(const std::vector<Vec>& p,Vec normal){
    size_t n=p.size();std::vector<std::array<uint32_t,3>> out;
    if(n==3){out.push_back({0,1,2});return out;}
    auto area=[&](size_t a,size_t b,size_t c){return (p[b]-p[a]).cross(p[c]-p[a]).dot(normal);};
    if(n==4){ // split along the diagonal that keeps both halves facing forward
        bool a=area(0,1,2)>0&&area(0,2,3)>0,b=area(0,1,3)>0&&area(1,2,3)>0;
        bool first=a&&(!b||(p[2]-p[0]).length()<=(p[3]-p[1]).length());
        if(first||!b){out.push_back({0,1,2});out.push_back({0,2,3});}else{out.push_back({0,1,3});out.push_back({1,2,3});}
        return out;
    }
    std::vector<uint32_t> ring(n);for(size_t i=0;i<n;++i)ring[i]=uint32_t(i);
    auto inside=[&](size_t q,size_t a,size_t b,size_t c){return area(a,b,q)>=0&&area(b,c,q)>=0&&area(c,a,q)>=0;};
    size_t guard=0;
    while(ring.size()>3&&n<=1024&&guard++<n*n){
        bool clipped=false;
        for(size_t i=0;i<ring.size();++i){
            size_t a=ring[(i+ring.size()-1)%ring.size()],b=ring[i],c=ring[(i+1)%ring.size()];
            if(area(a,b,c)<=0)continue;
            bool ear=true;for(auto q:ring)if(q!=a&&q!=b&&q!=c&&inside(q,a,b,c)){ear=false;break;}
            if(!ear)continue;
            out.push_back({uint32_t(a),uint32_t(b),uint32_t(c)});ring.erase(ring.begin()+ptrdiff_t(i));clipped=true;break;
        }
        if(!clipped)break;
    }
    for(size_t i=1;i+1<ring.size();++i)out.push_back({ring[0],ring[i],ring[i+1]}); // remainder (or degenerate input) as a fan
    return out;
}

// ---- Materials and textures ----
class Materials {
    const BlendFile& f;Asset& a;const Options& o;TextureResolver resources;
    std::map<size_t,size_t> byOffset;std::map<size_t,std::string> images,failures; // keyed by struct offset in the file
    struct Node{uint64_t address;View v;std::string idname;std::vector<View> inputs;};
    struct Link{uint64_t fromNode,fromSocket,toNode,toSocket;};
public:
    unsigned repaired=0;
    Materials(const BlendFile& file,Asset& asset,const Options& options,const fs::path& dir):f(file),a(asset),o(options),resources(dir){}
    size_t get(const View& ma){
        auto key=ma?ma.at:SIZE_MAX;auto found=byOffset.find(key);if(found!=byOffset.end())return found->second;
        if(a.manifest["materials"].size()>=o.limits.materials)throw std::runtime_error("Material limit exceeded (128 materials)");
        auto index=a.manifest["materials"].size();byOffset[key]=index;
        a.manifest["materials"].push_back(build(ma,index));return index;
    }
private:
    std::string texture(const View& im,const std::string& material,Json& j,const char* slot){
        if(!im)return "";
        auto cached=images.find(im.at);
        if(cached!=images.end()){auto failed=failures.find(im.at);if(failed!=failures.end()){j["missing_texture"]=true;j["texture_errors"][slot]=failed->second;}return cached->second;}
        std::string hash;
        auto name=idName(im);
        try{
            Bytes bytes;
            View pf;for(auto& item:f.list(im,"packedfiles","ImagePackedFile"))if((pf=item.follow("packedfile","PackedFile")))break;
            if(!pf)pf=im.follow("packedfile","PackedFile");
            if(pf){
                auto size=int64_t(pf.num("size"));auto r=pf.raw(pf.ptr("data"));
                if(size<=0||uint64_t(size)>r.size())throw std::runtime_error("packed image data is damaged");
                bytes.assign(r.begin(),r.begin()+ptrdiff_t(size));
            }else{
                auto path=im.str(im.pick({"name","filepath"}));
                if(int(im.num("source"))==4||path.empty())throw std::runtime_error("the image was generated in Blender and never saved; save or pack it in Blender");
                if(path.rfind("//",0)==0)path=path.substr(2);
                auto resolved=resources.resolve(path);repaired+=resolved.repaired;bytes=readFile(resolved.path,128ull<<20);
            }
            auto t=makeTexture(bytes,o.limits.textureDimension);hash=t.hash;addTexture(a,std::move(t));
        }catch(const std::exception& e){
            auto message="Texture "+name+" for material "+material+": "+e.what();
            a.manifest["warnings"].push_back(message);j["missing_texture"]=true;j["texture_errors"][slot]=message;failures[im.at]=message;
        }
        images[im.at]=hash;return hash;
    }
    Json build(const View& ma,size_t index){
        Json j={{"alpha_explicit",true},{"specular",{0,0,0}},{"shininess",1},{"name","Material"},{"color",{.8,.8,.8,1}},{"two_sided",true},{"alpha_mode","opaque"},{"alpha_cutoff",.5},{"base_texture",""},{"normal_texture",""},{"source_order",index}};
        if(!ma){j["name"]="Default";return j;}
        auto name=idName(ma);j["name"]=name;
        auto clamp=[](double x){return std::clamp(std::isfinite(x)?x:0.,0.,1.);};
        std::array<double,4> color{clamp(ma.num("r",0,.8)),clamp(ma.num("g",0,.8)),clamp(ma.num("b",0,.8)),clamp(ma.num("a",0,1))};
        j["two_sided"]=(int(ma.num("blend_flag"))&1)==0; // backface culling off by default
        int blend=int(ma.num("blend_method"));
        if(blend==3){j["alpha_cutoff"]=clamp(ma.num("alpha_threshold",0,.5));}
        bool alphaTexture=false;std::string base,normal;
        auto tree=ma.follow("nodetree","bNodeTree");
        bool nodes=tree&&(!ma.has("use_nodes")||ma.num("use_nodes")!=0||f.version>=500);
        if(nodes){
            std::vector<Node> list;std::unordered_map<uint64_t,size_t> byNode;
            for(auto& n:f.list(tree,"nodes","bNode")){byNode[n.address]=list.size();list.push_back({n.address,n,n.str("idname"),f.list(n,"inputs","bNodeSocket")});}
            std::vector<Link> links;
            for(auto& l:f.list(tree,"links","bNodeLink"))if(!(int(l.num("flag"))&(1<<4)))links.push_back({l.ptr("fromnode"),l.ptr("fromsock"),l.ptr("tonode"),l.ptr("tosock")}); // skip muted links
            auto input=[&](const Node& n,std::string_view id)->const View*{for(auto& s:n.inputs)if(s.str("identifier")==id||s.str("name")==id)return &s;return nullptr;};
            auto linkInto=[&](const Node& n,std::string_view id)->const Link*{auto s=input(n,id);if(!s)return nullptr;for(auto& l:links)if(l.toNode==n.address&&l.toSocket==s->address)return &l;return nullptr;};
            auto node=[&](uint64_t addr)->const Node*{auto it=byNode.find(addr);return it==byNode.end()?nullptr:&list[it->second];};
            // Walks upstream (through mix, hue, gamma and similar nodes) to an image texture.
            std::function<const Node*(uint64_t,int,uint64_t*)> image=[&](uint64_t addr,int depth,uint64_t* socket)->const Node*{
                auto n=node(addr);if(!n||depth>8)return nullptr;if(n->idname=="ShaderNodeTexImage")return n;
                for(auto& l:links)if(l.toNode==addr)if(auto found=image(l.fromNode,depth+1,nullptr)){if(socket)*socket=l.fromSocket;return found;}
                return nullptr;
            };
            const Node* output=nullptr;const Link* surface=nullptr;
            for(auto& n:list)if(n.idname=="ShaderNodeOutputMaterial")if(auto l=linkInto(n,"Surface")){if(!output||(int(n.v.num("flag"))&(1<<6))){output=&n;surface=l;}}
            // The shader feeding the output, looking through mix/add shader nodes.
            std::function<const Node*(uint64_t,int)> shader=[&](uint64_t addr,int depth)->const Node*{
                auto n=node(addr);if(!n||depth>8)return nullptr;auto& id=n->idname;
                if((id.rfind("ShaderNodeBsdf",0)==0&&id!="ShaderNodeBsdfTransparent")||id=="ShaderNodeEmission"||id=="ShaderNodeSubsurfaceScattering")return n;
                for(auto& l:links)if(l.toNode==addr)if(auto s=shader(l.fromNode,depth+1))return s;return nullptr;
            };
            const Node* s=surface?shader(surface->fromNode,0):nullptr;
            if(!s)for(auto& n:list)if(n.idname=="ShaderNodeBsdfPrincipled"){s=&n;break;}
            if(s){
                bool principled=s->idname=="ShaderNodeBsdfPrincipled";
                const char* colorSocket=principled?"Base Color":"Color";
                if(auto l=linkInto(*s,colorSocket)){
                    uint64_t from=l->fromSocket;
                    if(auto tex=image(l->fromNode,0,&from)){base=texture(tex->v.follow("id","Image"),name,j,"base_texture");if(!base.empty())color[0]=color[1]=color[2]=1;}
                }else if(auto socket=input(*s,colorSocket)){
                    if(auto value=socket->follow("default_value","bNodeSocketValueRGBA"))for(size_t i=0;i<3;++i)color[i]=clamp(value.num("value",i,color[i]));
                }
                if(principled){
                    // Blender's glTF exporter maps Specular IOR Level 0.5 to the 4% dielectric reflectance.
                    auto scalar=[&](std::initializer_list<const char*> names,double fallback){
                        for(auto n:names)if(auto socket=input(*s,n))if(auto value=socket->follow("default_value","bNodeSocketValueFloat"))return clamp(value.num("value",0,fallback));
                        return fallback;};
                    double level=std::min(2*scalar({"Specular IOR Level","Specular"},.5),1.);
                    j["pbr"]={{"workflow","metallic_roughness"},{"roughness",scalar({"Roughness"},.5)},{"metallic",scalar({"Metallic"},0)},{"specular",{level,level,level}}};
                    j["source_shading"]="pbr";
                    if(auto l=linkInto(*s,"Alpha")){uint64_t from=0;if(auto tex=image(l->fromNode,0,&from)){alphaTexture=true;if(base.empty())base=texture(tex->v.follow("id","Image"),name,j,"base_texture");}}
                    else if(auto socket=input(*s,"Alpha"))if(auto value=socket->follow("default_value","bNodeSocketValueFloat"))color[3]=clamp(value.num("value",0,1));
                    if(auto l=linkInto(*s,"Normal"))if(auto n=node(l->fromNode);n&&n->idname=="ShaderNodeNormalMap")
                        if(auto c=linkInto(*n,"Color"))if(auto tex=image(c->fromNode,0,nullptr))normal=texture(tex->v.follow("id","Image"),name,j,"normal_texture");
                }
                if(s->idname=="ShaderNodeEmission")j["unlit"]=true;
            }
        }
        j["color"]={color[0],color[1],color[2],color[3]};j["base_texture"]=base;j["normal_texture"]=normal;
        if(alphaTexture){j["alpha_mode"]=blend==3?"mask":"blend";j["alpha_explicit"]=blend==3;} // let texture analysis choose cutout or blend
        else if(color[3]<.999){j["alpha_mode"]="blend";}
        return j;
    }
};
std::string armatureHumanoid(const BlendFile& f,const Scene& s,Json& skeleton){
    static const std::vector<std::vector<std::string>> parts={{"head"},{"neck"},{"spine","chest"},{"hip","pelvis"},{"arm"},{"hand"},{"leg","thigh","calf","knee"},{"foot"}};
    std::unordered_set<std::string> bones;
    std::function<void(const View&,std::string_view,int)> collect=[&](const View& owner,std::string_view member,int depth){
        if(depth>256||bones.size()>20000)return;
        for(auto& b:f.list(owner,member,"Bone")){auto n=b.str("name");for(auto& c:n)c=char(std::tolower((unsigned char)c));bones.insert(n);collect(b,"childbase",depth+1);}
    };
    std::string name;
    for(auto& o:s.objects)if(o.type==ObArmature)if(auto arm=o.ob.follow("data","bArmature")){collect(arm,"bonebase",0);if(name.empty())name=o.name;}
    if(bones.empty())return {};
    size_t found=0;for(auto& group:parts){bool hit=false;for(auto& n:bones){for(auto& t:group)if(n.find(t)!=n.npos){hit=true;break;}if(hit)break;}found+=hit;}
    skeleton={{"bones",bones.size()},{"humanoid",found>=6&&bones.size()>=15}};return name;
}
const char* typeName(int type){
    switch(type){case 2:return "curve";case 3:return "surface";case 4:return "text";case 5:return "metaball";case 26:case 30:return "grease pencil";case 27:return "hair curves";case 28:return "point cloud";case 29:return "volume";default:return nullptr;}
}
BlendFile open(const fs::path& path,const Options& o,const Progress& progress){
    progress("Reading Blender file",.02f,{},0,0,true);
    auto raw=readFile(path,o.limits.expandedBytes);
    if(raw.size()>=4&&(raw[0]==0x1F||raw[0]==0x28))progress("Decompressing Blender file",.03f,{},0,0,true);
    return BlendFile(std::move(raw),o.limits.expandedBytes);
}
}

Json listBlendObjects(const fs::path& path,const Options& o,const Progress& progress){
    auto f=open(path,o,progress);auto s=readScene(f);
    progress("Listing objects",.5f);
    Json objects=Json::array(),other=Json::array();
    for(auto& e:s.objects){
        if(e.type!=ObMesh){if(auto t=typeName(e.type))other.push_back({{"name",e.name},{"type",t}});continue;}
        auto mesh=e.ob.follow("data","Mesh");if(!mesh)continue;
        auto num=[&](std::initializer_list<std::string_view> names){return std::max(0.,mesh.num(mesh.pick(names)));};
        double faces=num({"totpoly","faces_num"}),corners=num({"totloop","corners_num"});
        if(!faces){faces=0;corners=4*num({"totface","totface_legacy"});} // pre-2.63 quads: about two triangles each
        Json materials=Json::array();auto slots=size_t(std::max(0.,mesh.num("totcol")));
        for(size_t i=0;i<slots&&i<64;++i)if(auto m=mesh.deref(mesh.pointerIn(mesh.ptr("mat"),i),"Material"))materials.push_back(idName(m));
        std::string parent;if(auto p=e.ob.ptr("parent")){auto it=s.index.find(p);if(it!=s.index.end())parent=s.objects[it->second].name;}
        objects.push_back({{"name",e.name},{"mesh",idName(mesh)},{"vertices",uint64_t(num({"totvert","verts_num"}))},{"triangles",uint64_t(std::max(0.,corners-2*faces))},
            {"materials",materials},{"hidden",e.hidden()},{"in_scene",e.inScene||!s.collections},{"parent",parent},{"modifiers",f.list(e.ob,"modifiers","ModifierData").size()}});
    }
    return {{"objects",objects},{"other",other},{"version",f.version},{"compression",f.compression}};
}

Asset importBlend(const fs::path& path,const Options& o,const Progress& progress){
    auto f=open(path,o,progress);auto s=readScene(f);
    std::vector<const ObjectEntry*> chosen;
    if(!o.objects.empty()){
        std::unordered_set<std::string> wanted(o.objects.begin(),o.objects.end());
        for(auto& e:s.objects)if(e.type==ObMesh&&wanted.count(e.name))chosen.push_back(&e);
        if(chosen.empty())throw std::runtime_error("None of the selected objects are in the .blend file any more; import it again to choose objects");
    }else{
        for(auto& e:s.objects)if(e.type==ObMesh&&!e.hidden()&&(e.inScene||!s.collections))chosen.push_back(&e);
        if(chosen.empty())for(auto& e:s.objects)if(e.type==ObMesh)chosen.push_back(&e);
    }
    if(chosen.empty()){
        std::string kinds;for(auto& e:s.objects)if(auto t=typeName(e.type)){if(kinds.find(t)==kinds.npos)kinds+=(kinds.empty()?"":", ")+std::string(t);}
        throw std::runtime_error("The file contains no triangle geometry: the .blend file has no mesh objects"+(kinds.empty()?std::string():" (it has "+kinds+" objects; convert them to meshes in Blender with Object > Convert > Mesh)"));
    }
    Asset a;a.manifest={{"version",1},{"name",chosen.size()==1?chosen.front()->name:utf8(path.stem().wstring())},{"format","blend"},{"materials",Json::array()},{"parts",Json::array()},{"warnings",Json::array()},{"unit_scale",BlenderUnits}};
    Json objectNames=Json::array();for(auto e:chosen)objectNames.push_back(e->name);a.manifest["blend_objects"]=objectNames;
    Materials materials(f,a,o,path.parent_path());
    struct Key{uint32_t vertex;int32_t face;uint32_t u,v;bool operator==(const Key&)const=default;};
    struct KeyHash{size_t operator()(const Key& k)const{uint64_t h=k.vertex*0x9E3779B97F4A7C15ull;h^=uint64_t(uint32_t(k.face))+0x632BE59BD9B4E019ull+(h<<6)+(h>>2);h^=(uint64_t(k.u)<<32|k.v)+0x94D049BB133111EBull+(h<<6)+(h>>2);return size_t(h);}};
    std::vector<std::string> modified,boneParented;
    for(size_t index=0;index<chosen.size();++index){
        auto& e=*chosen[index];
        progress("Reading meshes",.04f+.3f*float(index)/float(chosen.size()),e.name,index,chosen.size());
        auto meshView=e.ob.follow("data","Mesh");if(!meshView){a.manifest["warnings"].push_back("Object "+e.name+" has no mesh data in this file and was skipped.");continue;}
        if(meshView.sub("id").ptr("lib")){a.manifest["warnings"].push_back("Object "+e.name+" uses a mesh linked from another .blend file and was skipped; make it local in Blender first.");continue;}
        auto m=readMesh(f,meshView,e.name,o.limits);
        if(m.faces.empty()){a.manifest["warnings"].push_back("Object "+e.name+" has no faces (only vertices or edges) and was skipped.");continue;}
        if(!f.list(e.ob,"modifiers","ModifierData").empty())modified.push_back(e.name);
        if(int(e.ob.num("partype"))==7&&e.ob.ptr("parent"))boneParented.push_back(e.name);
        auto world=worldMatrix(s,e);bool mirrored=determinant(world)<0;
        std::vector<Vec> positions(m.positions.size());for(size_t i=0;i<positions.size();++i)positions[i]=transformPoint(world,m.positions[i])*BlenderUnits;
        // Material slots: the object's slot overrides the mesh's where its link is set to Object.
        auto meshSlots=size_t(std::max(0.,meshView.num("totcol"))),objectSlots=size_t(std::max(0.,e.ob.num("totcol")));
        auto bits=e.ob.raw(e.ob.ptr("matbits"));std::unordered_map<int32_t,size_t> slotCache;
        auto slot=[&](int32_t i)->size_t{
            auto count=std::max(meshSlots,objectSlots);if(count)i=std::clamp(i,0,int32_t(count)-1);else i=0;
            auto hit=slotCache.find(i);if(hit!=slotCache.end())return hit->second;
            View material=size_t(i)<meshSlots?meshView.deref(meshView.pointerIn(meshView.ptr("mat"),size_t(i)),"Material"):View{};
            if(size_t(i)<objectSlots&&size_t(i)<bits.size()&&bits[size_t(i)])material=e.ob.deref(e.ob.pointerIn(e.ob.ptr("mat"),size_t(i)),"Material");
            return slotCache[i]=materials.get(material);
        };
        // Face normals, triangles and angle-weighted smooth normals.
        struct Tri{uint32_t corner[3];uint32_t face;};
        std::vector<Tri> tris;std::vector<Vec> faceNormals(m.faces.size()),smooth(positions.size());
        uint64_t estimate=0;for(auto& face:m.faces)if(face.second>=3)estimate+=uint64_t(face.second)-2;
        checkGeometryStorage(a.vertices.size()+std::min<uint64_t>(estimate*3,m.corners.size()),a.indices.size()+estimate*3,o.limits);
        tris.reserve(size_t(estimate));
        std::vector<uint32_t> order;std::vector<Vec> ring;
        for(size_t face=0;face<m.faces.size();++face){
            auto [first,count]=m.faces[face];if(count<3)continue;
            order.resize(size_t(count));for(int32_t k=0;k<count;++k)order[size_t(k)]=uint32_t(first+(mirrored?count-1-k:k));
            ring.resize(order.size());for(size_t k=0;k<order.size();++k)ring[k]=positions[size_t(m.corners[order[k]])];
            Vec normal{};for(size_t k=0;k<ring.size();++k){auto& p=ring[k];auto& q=ring[(k+1)%ring.size()];normal=normal+Vec{(p.y-q.y)*(p.z+q.z),(p.z-q.z)*(p.x+q.x),(p.x-q.x)*(p.y+q.y)};}
            if(normal.length()<1e-20f)continue; // zero-area face
            normal=normal.normalized();faceNormals[face]=normal;
            for(auto& t:triangulate(ring,normal))tris.push_back({{order[t[0]],order[t[1]],order[t[2]]},uint32_t(face)});
            if(!m.flat[face])for(size_t k=0;k<ring.size();++k){
                auto e1=(ring[(k+ring.size()-1)%ring.size()]-ring[k]).normalized(),e2=(ring[(k+1)%ring.size()]-ring[k]).normalized();
                float angle=std::acos(std::clamp(e1.dot(e2),-1.f,1.f));auto v=size_t(m.corners[order[k]]);smooth[v]=smooth[v]+normal*angle;
            }
        }
        std::unordered_map<Key,uint32_t,KeyHash> welded;welded.reserve(tris.size());
        std::map<size_t,std::vector<uint32_t>> byMaterial;
        for(auto& t:tris){
            auto& out=byMaterial[slot(m.materials[t.face])];
            for(auto corner:t.corner){
                auto vertex=uint32_t(m.corners[corner]);bool flat=m.flat[t.face]!=0;
                std::array<float,2> uv{0,0};if(!m.uvs.empty())uv=m.uvs[corner];
                Key key{vertex,flat?int32_t(t.face):-1,0,0};std::memcpy(&key.u,&uv[0],4);std::memcpy(&key.v,&uv[1],4);
                auto [it,added]=welded.try_emplace(key,uint32_t(a.vertices.size()));
                if(added){
                    Vertex v;v.pos=positions[vertex];auto n=flat?faceNormals[t.face]:smooth[vertex];v.normal=n.length()>1e-12f?n.normalized():faceNormals[t.face];
                    v.u=std::isfinite(uv[0])?uv[0]:0;v.v=std::isfinite(uv[1])?1-uv[1]:0;a.vertices.push_back(v);
                }
                out.push_back(it->second);
            }
        }
        for(auto& [material,indices]:byMaterial){
            auto first=uint32_t(a.indices.size());a.indices.insert(a.indices.end(),indices.begin(),indices.end());
            a.manifest["parts"].push_back({{"material",material},{"first",first},{"count",indices.size()},{"mesh",e.name}});
        }
    }
    if(a.indices.empty())throw std::runtime_error("The file contains no triangle geometry: the selected objects have no faces (only vertices or edges)");
    a.manifest["resolved_texture_references"]=materials.repaired;
    if(!modified.empty()){std::string names;for(size_t i=0;i<modified.size()&&i<5;++i)names+=(i?", ":"")+modified[i];if(modified.size()>5)names+=" and "+std::to_string(modified.size()-5)+" more";
        a.manifest["warnings"].push_back("Modifiers are not applied (on "+names+"). Apply them in Blender (Ctrl+A > Visual Geometry to Mesh) if the shape looks wrong.");}
    if(!boneParented.empty())a.manifest["warnings"].push_back("Objects parented to bones use their saved position; posed rigs may differ from Blender.");
    Json skeleton;if(!armatureHumanoid(f,s,skeleton).empty())a.manifest["skeleton"]=skeleton;
    if(skeleton.is_object()&&skeleton.value("bones",0)>0)a.manifest["warnings"].push_back("The armature is ignored; the model is imported in its rest shape.");
    for(auto& e:s.objects)if(auto t=typeName(e.type)){a.manifest["warnings"].push_back(std::string("Non-mesh objects (")+t+" and similar) were skipped; convert them to meshes in Blender to include them.");break;}
    // Tangents from UVs, as for PMX.
    progress("Generating tangents",.4f);
    std::vector<Vec> ts(a.vertices.size()),bs(a.vertices.size());
    for(size_t i=0;i+2<a.indices.size();i+=3){auto i0=a.indices[i],i1=a.indices[i+1],i2=a.indices[i+2];auto& p=a.vertices[i0];auto& q=a.vertices[i1];auto& r=a.vertices[i2];
        Vec e1=q.pos-p.pos,e2=r.pos-p.pos;float u=q.u-p.u,v=q.v-p.v,u2=r.u-p.u,v2=r.v-p.v,d=u*v2-u2*v;if(std::abs(d)<1e-10f)continue;
        Vec t=(e1*v2-e2*v)*(1/d),b=(e2*u-e1*u2)*(1/d);for(auto k:{i0,i1,i2}){ts[k]=ts[k]+t;bs[k]=bs[k]+b;}}
    for(size_t i=0;i<a.vertices.size();++i){auto& v=a.vertices[i];auto t=(ts[i]-v.normal*ts[i].dot(v.normal)).normalized();v.tangent={t.x,t.y,t.z,v.normal.cross(t).dot(bs[i])<0?-1.f:1.f};}
    return a;
}
}
