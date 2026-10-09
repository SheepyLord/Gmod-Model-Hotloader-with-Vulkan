// VRM 0.x / 1.0 to PMX 2.0. The avatar is baked into its rest pose in MMD
// coordinates (left-handed, +Y up, facing -Z, 8 cm units) and written as an
// ordinary PMX: humanoid bones get the standard MMD names the fitter already
// recognises, glTF morph targets become vertex morphs and VRM expressions
// become group/material/UV morphs. Spring bones and colliders are returned as
// JSON for the native spring simulation (spring_bones.cpp).
#include "vrm.hpp"
#include "humanoid_slots.hpp"
#include "pmx_writer.hpp"
#include "import_error.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>
#include <stb_image_write.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace mmd {
namespace {
using Vec2=glm::vec2;using Vec3=glm::vec3;using Vec4=glm::vec4;using Mat3=glm::mat3;using Mat4=glm::mat4;

constexpr int MaxNodeDepth=512;
uint32_t le32(const unsigned char* p){uint32_t v;std::memcpy(&v,p,4);return v;}
float number(const Json& j,const char* key,float fallback){auto it=j.find(key);return it!=j.end()&&it->is_number()?it->get<float>():fallback;}
int integer(const Json& j,const char* key,int fallback){auto it=j.find(key);return it!=j.end()&&it->is_number_integer()?it->get<int>():fallback;}
std::string string(const Json& j,const char* key,std::string fallback={}){auto it=j.find(key);return it!=j.end()&&it->is_string()?it->get<std::string>():fallback;}
// References into the document (or a shared empty value): value() would copy.
const Json& arr(const Json& j,const char* key){static const Json empty=Json::array();if(!j.is_object())return empty;auto it=j.find(key);return it!=j.end()&&it->is_array()?*it:empty;}
const Json& obj(const Json& j,const char* key){static const Json empty=Json::object();if(!j.is_object())return empty;auto it=j.find(key);return it!=j.end()&&it->is_object()?*it:empty;}
Vec3 vec3(const Json& j,Vec3 fallback){if(j.is_array()&&j.size()>=3&&j[0].is_number()&&j[1].is_number()&&j[2].is_number())return {j[0].get<float>(),j[1].get<float>(),j[2].get<float>()};
 if(j.is_object()&&j.contains("x"))return {number(j,"x",0),number(j,"y",0),number(j,"z",0)};return fallback;}
Vec4 vec4(const Json& j,Vec4 fallback){if(j.is_array()&&j.size()>=4){Vec4 v;for(int k=0;k<4;k++){if(!j[k].is_number())return fallback;v[k]=j[k].get<float>();}return v;}return fallback;}
bool finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
// Every failure names the glTF element at fault (code vrm.truncated, vrm.data, vrm.container,
// vrm.external, vrm.image, vrm.humanoid, vrm.no_skeleton or vrm.no_geometry); the mesh and
// primitive scopes around it join its "where", and JSON errors inside become vrm.json.
[[noreturn]] void vrmFail(const char* code,const std::string& message,Json at=Json(),Json details=Json::object()){if(!at.is_null())details["where"]=Json::array({at});importFail(code,message,std::move(details));}
std::string number(size_t v){return thousands(v);}

Bytes base64(std::string_view s){
 auto value=[](char c)->int{if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;if(c=='+'||c=='-')return 62;if(c=='/'||c=='_')return 63;return -1;};
 Bytes out;out.reserve(s.size()*3/4);uint32_t acc=0;int bits=0;
 for(char c:s){int v=value(c);if(v<0)continue;acc=(acc<<6)|uint32_t(v);bits+=6;if(bits>=8){bits-=8;out.push_back(uint8_t(acc>>bits));}}
 return out;
}

// ---- glTF container and accessors ----
struct Document {
 Json j;std::vector<Bytes> owned;std::vector<std::span<const unsigned char>> buffers;
 std::span<const unsigned char> view(int index)const{
  auto& views=j.at("bufferViews");if(index<0||size_t(index)>=views.size())vrmFail("vrm.data","Invalid VRM file: buffer view "+std::to_string(index)+" does not exist (the file has "+number(views.size())+")",place("buffer_view",index));
  ImportScope scope("buffer_view",index,{});
  auto& v=views[index];int buffer=v.at("buffer");size_t offset=v.value("byteOffset",size_t(0)),length=v.at("byteLength");
  if(buffer<0||size_t(buffer)>=buffers.size())vrmFail("vrm.data","Invalid VRM file: buffer view "+std::to_string(index)+" uses buffer "+std::to_string(buffer)+", which does not exist (the file has "+number(buffers.size())+")",Json(),{{"buffer",buffer}});
  auto data=buffers[buffer];if(offset>data.size()||length>data.size()-offset)vrmFail("vrm.truncated","Truncated VRM file: buffer view "+std::to_string(index)+" reads bytes "+number(offset)+" to "+number(offset+length)+" of buffer "+std::to_string(buffer)+", which has only "+number(data.size()),Json(),{{"buffer",buffer},{"offset",offset},{"length",length},{"size",data.size()}});
  return data.subspan(offset,length);
 }
};
Document open(std::span<const unsigned char> b){
 if(b.size()<20||std::memcmp(b.data(),"glTF",4))vrmFail("vrm.container","Not a VRM file: the glTF binary (GLB) header is missing",place("header"));
 if(le32(b.data()+4)!=2)vrmFail("vrm.container","Unsupported glTF container version "+std::to_string(le32(b.data()+4))+"; VRM uses glTF 2.0",place("header"));
 size_t length=std::min<size_t>(le32(b.data()+8),b.size());Document d;bool json=false;std::span<const unsigned char> bin;
 for(size_t at=12;at+8<=length;){
  uint32_t size=le32(b.data()+at),type=le32(b.data()+at+4);at+=8;
  if(size>length-at)vrmFail("vrm.truncated","Truncated VRM file: a GLB chunk of "+number(size)+" bytes starts at byte "+number(at)+" of "+number(length)+"; the file is incomplete",place("header"),{{"offset",at},{"length",size},{"size",length}});
  if(type==0x4E4F534Au&&!json){ImportScope scope("header",-1,{});d.j=Json::parse(b.begin()+at,b.begin()+at+size);json=true;}
  else if(type==0x004E4942u&&bin.empty())bin=b.subspan(at,size);
  at+=size;
 }
 if(!json)vrmFail("vrm.truncated","Truncated VRM file: the glTF JSON chunk is missing",place("header"));
 auto& buffers=arr(d.j,"buffers");d.owned.resize(buffers.size());
 for(size_t i=0;i<buffers.size();i++){
  auto uri=string(buffers[i],"uri");
  if(uri.empty()){if(i!=0)vrmFail("vrm.data","Invalid VRM file: buffer "+std::to_string(i)+" has no data",place("buffer",int64_t(i)));d.buffers.push_back(bin);continue;}
  auto comma=uri.find(',');if(!uri.starts_with("data:")||comma==std::string::npos||uri.substr(0,comma).find(";base64")==std::string::npos)vrmFail("vrm.external","The VRM file refers to an external buffer ("+uri.substr(0,64)+"); only self-contained .vrm files are supported",place("buffer",int64_t(i)));
  d.owned[i]=base64(std::string_view(uri).substr(comma+1));d.buffers.push_back(d.owned[i]);
 }
 return d;
}
int componentSize(int type){switch(type){case 5120:case 5121:return 1;case 5122:case 5123:return 2;case 5125:case 5126:return 4;}vrmFail("vrm.data","Invalid VRM file: unknown accessor component type "+std::to_string(type));}
int componentCount(const std::string& type){if(type=="SCALAR")return 1;if(type=="VEC2")return 2;if(type=="VEC3")return 3;if(type=="VEC4"||type=="MAT2")return 4;if(type=="MAT3")return 9;if(type=="MAT4")return 16;vrmFail("vrm.data","Invalid VRM file: unknown accessor type \""+type+"\"");}
float component(const unsigned char* p,int type,bool normalized){
 switch(type){
  case 5120:{int8_t v;std::memcpy(&v,p,1);return normalized?std::max(v/127.f,-1.f):float(v);}
  case 5121:return normalized?p[0]/255.f:float(p[0]);
  case 5122:{int16_t v;std::memcpy(&v,p,2);return normalized?std::max(v/32767.f,-1.f):float(v);}
  case 5123:{uint16_t v;std::memcpy(&v,p,2);return normalized?v/65535.f:float(v);}
  case 5125:{uint32_t v;std::memcpy(&v,p,4);return float(v);}
  default:{float v;std::memcpy(&v,p,4);return v;}
 }
}
uint32_t unsignedComponent(const unsigned char* p,int type){switch(type){case 5121:return p[0];case 5123:{uint16_t v;std::memcpy(&v,p,2);return v;}case 5125:{uint32_t v;std::memcpy(&v,p,4);return v;}}vrmFail("vrm.data","Invalid VRM file: index component type "+std::to_string(type)+" is not an unsigned integer");}
struct Accessor {size_t count=0;int components=0;std::vector<float> values;float at(size_t i,int c)const{return values[i*components+c];}};
// Offsets, strides and counts come from the file: compare by division so no product can wrap.
bool fits(std::span<const unsigned char> data,size_t offset,size_t stride,size_t element,size_t count){
 return !count||(offset<=data.size()&&element<=data.size()-offset&&count-1<=(data.size()-offset-element)/stride);
}
size_t stride(const Document& d,const Json& a,size_t element){size_t s=d.j["bufferViews"][int(a["bufferView"])].value("byteStride",size_t(0));return s?s:element;}
Accessor accessor(const Document& d,int index){
 auto& list=d.j.at("accessors");if(index<0||size_t(index)>=list.size())vrmFail("vrm.data","Invalid VRM file: accessor "+std::to_string(index)+" does not exist (the file has "+number(list.size())+")",place("accessor",index));
 ImportScope scope("accessor",index,{});auto& a=list[index];Accessor out;out.count=a.at("count");out.components=componentCount(a.at("type"));int type=a.at("componentType");bool normalized=a.value("normalized",false);int size=componentSize(type);
 if(out.components>4&&size!=4)vrmFail("vrm.data","Invalid VRM file: accessor "+std::to_string(index)+" is a packed matrix, which the importer cannot read");
 if(out.count>(1ull<<28)/out.components)vrmFail("vrm.data","Invalid VRM file: accessor "+std::to_string(index)+" has "+number(out.count)+" elements, more than the importer reads",Json(),{{"count",out.count}});
 size_t element=size_t(size)*out.components;std::span<const unsigned char> data;size_t offset=0,step=element;bool viewed=a.contains("bufferView");
 if(viewed){data=d.view(a["bufferView"]);offset=a.value("byteOffset",size_t(0));step=stride(d,a,element);
  if(!fits(data,offset,step,element,out.count))vrmFail("vrm.truncated","Truncated VRM file: accessor "+std::to_string(index)+" reads "+number(out.count)+" elements past the end of its buffer view ("+number(data.size())+" bytes)",Json(),{{"count",out.count},{"offset",offset},{"stride",step},{"size",data.size()}});}
 out.values.assign(out.count*out.components,0.f);
 if(viewed)for(size_t i=0;i<out.count;i++)for(int c=0;c<out.components;c++)out.values[i*out.components+c]=component(data.data()+offset+i*step+size_t(c)*size,type,normalized);
 if(a.contains("sparse")){
  auto& s=a["sparse"];size_t count=s.at("count");auto& ix=s.at("indices");auto& vx=s.at("values");int indexType=ix.at("componentType");int indexSize=componentSize(indexType);
  if(count>out.count)vrmFail("vrm.data","Invalid VRM file: sparse accessor "+std::to_string(index)+" replaces "+number(count)+" of only "+number(out.count)+" elements");
  auto indices=d.view(ix.at("bufferView")),values=d.view(vx.at("bufferView"));size_t io=ix.value("byteOffset",size_t(0)),vo=vx.value("byteOffset",size_t(0));
  if(count&&(io>indices.size()||indexSize*count>indices.size()-io||vo>values.size()||element*count>values.size()-vo))vrmFail("vrm.truncated","Truncated VRM file: sparse accessor "+std::to_string(index)+" reads past its data");
  for(size_t k=0;k<count;k++){size_t target=unsignedComponent(indices.data()+io+k*indexSize,indexType);if(target>=out.count)vrmFail("vrm.data","Invalid VRM file: sparse accessor "+std::to_string(index)+" replaces element "+number(target)+" of only "+number(out.count));
   for(int c=0;c<out.components;c++)out.values[target*out.components+c]=component(values.data()+vo+k*element+size_t(c)*size,type,normalized);}
 }
 for(size_t k=0;k<out.values.size();k++)if(!std::isfinite(out.values[k]))vrmFail("vrm.data","The VRM file contains non-finite vertex or transform data: element "+number(k/out.components)+" of accessor "+std::to_string(index)+" is not a number",Json(),{{"element",k/out.components}});
 return out;
}
std::vector<uint32_t> indexAccessor(const Document& d,int index){
 auto& list=d.j.at("accessors");if(index<0||size_t(index)>=list.size())vrmFail("vrm.data","Invalid VRM file: accessor "+std::to_string(index)+" does not exist (the file has "+number(list.size())+")",place("accessor",index));
 ImportScope scope("accessor",index,{});auto& a=list[index];int type=a.at("componentType");if(a.at("type")!="SCALAR"||type==5126)vrmFail("vrm.data","Invalid VRM file: triangle index accessor "+std::to_string(index)+" does not hold whole numbers");
 size_t count=a.at("count");int size=componentSize(type);if(count>(1ull<<28))vrmFail("vrm.data","Invalid VRM file: accessor "+std::to_string(index)+" has "+number(count)+" elements, more than the importer reads",Json(),{{"count",count}});
 std::span<const unsigned char> data;size_t offset=0,step=size;bool viewed=a.contains("bufferView");
 if(viewed){data=d.view(a["bufferView"]);offset=a.value("byteOffset",size_t(0));step=stride(d,a,size);
  if(!fits(data,offset,step,size,count))vrmFail("vrm.truncated","Truncated VRM file: triangle index accessor "+std::to_string(index)+" reads past the end of its buffer view ("+number(data.size())+" bytes)",Json(),{{"count",count},{"size",data.size()}});}
 std::vector<uint32_t> out(count,0);
 if(viewed)for(size_t i=0;i<count;i++)out[i]=unsignedComponent(data.data()+offset+i*step,type);
 if(a.contains("sparse")){auto acc=accessor(d,index);for(size_t i=0;i<count;i++)out[i]=uint32_t(acc.values[i]);}
 return out;
}
std::optional<Bytes> image(const Document& d,int index,std::string& mime){
 auto& images=arr(d.j,"images");if(index<0||size_t(index)>=images.size())return std::nullopt;
 auto& im=images[index];mime=string(im,"mimeType");
 if(im.contains("bufferView")){auto v=d.view(im["bufferView"]);return Bytes(v.begin(),v.end());}
 auto uri=string(im,"uri");auto comma=uri.find(',');
 if(uri.starts_with("data:")&&comma!=std::string::npos){if(mime.empty())mime=uri.substr(5,uri.find(';')-5);return base64(std::string_view(uri).substr(comma+1));}
 return std::nullopt;
}

// ---- coordinate conversion ----
// glTF is right-handed. VRM 1.0 avatars face +Z with their left side at +X;
// VRM 0.x avatars were exported rotated 180 degrees (facing -Z, left at -X).
// MMD models face -Z with their left side at +X in a left-handed space, so
// both mirror one axis. Spring and collider vectors of VRM 0.x are stored in
// Unity's left-handed axes (z negated relative to glTF).
struct Axes {
 bool v0=false;float scale=VrmUnitsPerMeter;
 Vec3 dir(Vec3 p)const{return v0?Vec3(-p.x,p.y,p.z):Vec3(p.x,p.y,-p.z);}
 Vec3 point(Vec3 p)const{return dir(p)*scale;}
 Vec3 unity(Vec3 p)const{return dir(Vec3(p.x,p.y,-p.z));}
};

// VRM 0.x expression presets under their VRM 1.0 names.
std::string presetName1(const std::string& p){
 static const std::map<std::string,std::string> names={{"a","aa"},{"i","ih"},{"u","ou"},{"e","ee"},{"o","oh"},{"joy","happy"},{"angry","angry"},{"sorrow","sad"},{"fun","relaxed"},
  {"blink","blink"},{"blink_l","blinkLeft"},{"blink_r","blinkRight"},{"lookup","lookUp"},{"lookdown","lookDown"},{"lookleft","lookLeft"},{"lookright","lookRight"},{"neutral","neutral"}};
 auto it=names.find(p);return it==names.end()?std::string():it->second;
}

// Material alpha variants: glTF OPAQUE ignores texture alpha and MASK is a
// hard cutoff, while the character renderer infers cutout/blending from the
// base texture's alpha. Bake the glTF meaning into the texture it receives.
enum class AlphaUse {Keep,Opaque,Cutout};

struct Converter {
 Document d;Axes axes;bool v0=false;const Json* vrm=nullptr;std::vector<std::string> warnings,notes;std::string fallbackName;
 struct Node {std::string name;int parent=-1,mesh=-1,skin=-1;std::vector<int> children;Mat4 local{1.f},world{1.f};};
 std::vector<Node> nodes;std::vector<int> boneOf;std::vector<PBone> bones;std::map<std::string,int> human;std::vector<int> roots;
 std::vector<PVertex> vertices;std::vector<int> vertexMaterial;std::map<int,PMaterial> materials;
 // Morph targets per (node instance, mesh target): PMX vertex deltas.
 struct Target {int node=-1,mesh=-1,index=-1;std::string name;std::vector<std::pair<uint32_t,Vec3>> deltas;};
 std::vector<Target> targets;std::map<std::pair<int,int>,std::vector<int>> targetsOfMeshIndex; // (mesh, target) -> targets
 std::map<std::string,Bytes> textures;std::map<std::tuple<int,int,int>,int> textureIndex;std::vector<std::string> texturePaths;
 std::vector<std::string> meshNames;
 std::string meshText(int mesh,size_t primitive)const{return "primitive "+std::to_string(primitive)+" of "+placeText(place("mesh",mesh,string(arr(d.j,"meshes")[mesh],"name")));}

 void warn(std::string s){if(std::find(warnings.begin(),warnings.end(),s)==warnings.end())warnings.push_back(std::move(s));}
 void note(std::string s){if(std::find(notes.begin(),notes.end(),s)==notes.end())notes.push_back(std::move(s));}

 void readNodes(){
  auto& list=arr(d.j,"nodes");nodes.resize(list.size());
  for(size_t i=0;i<list.size();i++){auto& n=list[i];auto& out=nodes[i];out.name=string(n,"name");out.mesh=integer(n,"mesh",-1);out.skin=integer(n,"skin",-1);ImportScope scope("node",int64_t(i),out.name);
   if(n.contains("matrix")&&n["matrix"].is_array()&&n["matrix"].size()==16){float m[16];for(int k=0;k<16;k++)m[k]=n["matrix"][k].get<float>();out.local=glm::make_mat4(m);}
   else{Vec3 t=vec3(n.value("translation",Json()),Vec3(0)),s=vec3(n.value("scale",Json()),Vec3(1));Vec4 r=vec4(n.value("rotation",Json()),Vec4(0,0,0,1));
    glm::quat q(r.w,r.x,r.y,r.z);if(glm::length(q)<1e-8f)q=glm::quat(1,0,0,0);out.local=glm::translate(Mat4(1.f),t)*glm::mat4_cast(glm::normalize(q))*glm::scale(Mat4(1.f),s);}
   for(auto& c:arr(n,"children")){int child=c.get<int>();if(child<0||size_t(child)>=list.size())vrmFail("vrm.data","Invalid VRM node hierarchy: "+placeText(place("node",int64_t(i),out.name))+" lists child "+std::to_string(child)+", which does not exist (the file has "+number(list.size())+" nodes)",Json(),{{"child",child}});out.children.push_back(child);}
  }
  for(size_t i=0;i<nodes.size();i++)for(int c:nodes[i].children){if(nodes[c].parent>=0)vrmFail("vrm.data","Invalid VRM node hierarchy: "+placeText(place("node",c,nodes[c].name))+" has two parents, "+placeText(place("node",nodes[c].parent,nodes[nodes[c].parent].name))+" and "+placeText(place("node",int64_t(i),nodes[i].name)),place("node",c,nodes[c].name));nodes[c].parent=int(i);}
  // Scene roots first, in scene order; then any other roots.
  std::vector<bool> isRoot(nodes.size(),false);auto& scenes=arr(d.j,"scenes");int scene=integer(d.j,"scene",0);
  if(scene>=0&&size_t(scene)<scenes.size())for(auto& r:arr(scenes[scene],"nodes")){int n=r.get<int>();if(n>=0&&size_t(n)<nodes.size()&&nodes[n].parent<0&&!isRoot[n]){isRoot[n]=true;roots.push_back(n);}}
  for(size_t i=0;i<nodes.size();i++)if(nodes[i].parent<0&&!isRoot[i]){isRoot[i]=true;roots.push_back(int(i));}
  // Later walks (bones, meshes, springs) recurse over the same tree, so its depth is bounded here.
  std::vector<int> state(nodes.size(),0);std::function<void(int,const Mat4&,int)> visit=[&](int i,const Mat4& parent,int depth){if(state[i])vrmFail("vrm.data","Invalid VRM node hierarchy: "+placeText(place("node",i,nodes[i].name))+" is its own ancestor",place("node",i,nodes[i].name));
   if(depth>MaxNodeDepth)vrmFail("vrm.data","Invalid VRM node hierarchy: nodes are nested more than "+std::to_string(MaxNodeDepth)+" levels deep, at "+placeText(place("node",i,nodes[i].name)),place("node",i,nodes[i].name));state[i]=1;nodes[i].world=parent*nodes[i].local;for(int c:nodes[i].children)visit(c,nodes[i].world,depth+1);};
  for(int r:roots)visit(r,Mat4(1.f),0);
  for(size_t i=0;i<nodes.size();i++)if(!state[i])vrmFail("vrm.data","Invalid VRM node hierarchy: "+placeText(place("node",int64_t(i),nodes[i].name))+" is not reachable from a scene root",place("node",int64_t(i),nodes[i].name));
 }
 Vec3 worldPosition(int n)const{return Vec3(nodes[n].world[3]);}

 // Every node that shapes the skeleton becomes a bone; mesh-only leaves do not.
 void readBones(const std::set<int>& referenced){
  std::vector<bool> keep(nodes.size());for(size_t i=0;i<nodes.size();i++)keep[i]=!(nodes[i].mesh>=0&&nodes[i].children.empty()&&!referenced.contains(int(i)));
  boneOf.assign(nodes.size(),-1);std::vector<int> order;std::function<void(int)> dfs=[&](int i){if(keep[i]){boneOf[i]=int(order.size());order.push_back(i);}for(int c:nodes[i].children)dfs(c);};for(int r:roots)dfs(r);
  std::map<int,std::string> humanOf;for(auto& [name,node]:human)humanOf[node]=name;
  std::set<std::string> used;
  for(int n:order){PBone b;b.position=axes.point(worldPosition(n));if(!finite(b.position))vrmFail("vrm.data","The VRM skeleton has non-finite bone positions: "+placeText(place("node",n,nodes[n].name))+" is not at a number position",place("node",n,nodes[n].name));
   for(int p=nodes[n].parent;p>=0;p=nodes[p].parent)if(boneOf[p]>=0){b.parent=boneOf[p];break;}
   auto h=humanOf.find(n);if(h!=humanOf.end())for(auto& e:humanNames)if(h->second==e.vrm){b.name=e.jp;b.english=e.en;break;}
   bones.push_back(b);
  }
  for(auto& b:bones)if(!b.name.empty())used.insert(b.name),used.insert(b.english);
  for(size_t i=0;i<order.size();i++){auto& b=bones[i];if(!b.name.empty())continue;int n=order[i];std::string name=nodes[n].name.empty()?"node_"+std::to_string(n):nodes[n].name;
   if(used.contains(name))name+="_"+std::to_string(n);used.insert(name);b.name=name;b.english=name;}
  for(size_t i=0;i<order.size();i++){auto& children=nodes[order[i]].children;for(int c:children)if(boneOf[c]>=0){bones[i].tail=boneOf[c];break;}}
  if(bones.empty())vrmFail("vrm.no_skeleton","The VRM file has no skeleton");
 }
 int boneFor(int node)const{for(int n=node;n>=0;n=nodes[n].parent)if(boneOf[n]>=0)return boneOf[n];return human.contains("hips")?boneOf[human.at("hips")]:0;}

 // ---- materials ----
 const Json* mtoon0(int material){
  if(!vrm||!v0)return nullptr;auto& props=arr(*vrm,"materialProperties");auto& mats=arr(d.j,"materials");if(material<0||size_t(material)>=mats.size())return nullptr;
  auto name=string(mats[material],"name");if(size_t(material)<props.size()&&string(props[material],"name")==name)return &props[material];
  for(auto& p:props)if(string(p,"name")==name)return &p;return nullptr;
 }
 int texture(int gltfTexture,AlphaUse use,float cutoff){
  auto& list=arr(d.j,"textures");if(gltfTexture<0||size_t(gltfTexture)>=list.size())return -1;
  int source=integer(list[gltfTexture],"source",-1);
  if(source<0){auto& ext=obj(list[gltfTexture],"extensions");for(auto& [name,value]:ext.items())if(value.is_object()&&value.contains("source")){warn("Texture "+std::to_string(gltfTexture)+" uses the unsupported "+name+" image format; its material is drawn untextured.");return -1;}return -1;}
  int cut=use==AlphaUse::Cutout?int(std::lround(std::clamp(cutoff,0.f,1.f)*255)):0;auto key=std::make_tuple(source,int(use),cut);
  if(auto it=textureIndex.find(key);it!=textureIndex.end())return it->second;
  std::string mime;auto bytes=image(d,source,mime);
  if(!bytes){warn("Image "+std::to_string(source)+" is not embedded in the VRM file; its material is drawn untextured.");return textureIndex[key]=-1;}
  int w=0,h=0,channels=0;bool decodable=bytes->size()<INT_MAX&&stbi_info_from_memory(bytes->data(),int(bytes->size()),&w,&h,&channels);
  if(!decodable){warn("Image "+std::to_string(source)+" ("+(mime.empty()?"unknown format":mime)+") cannot be decoded; its material is drawn untextured.");return textureIndex[key]=-1;}
  std::string extension=mime=="image/jpeg"?".jpg":".png";Bytes out=std::move(*bytes);
  if(use!=AlphaUse::Keep&&(channels==2||channels==4)){
   int x=0,y=0,c=0;std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(out.data(),int(out.size()),&x,&y,&c,4),stbi_image_free);
   if(!pixels){warn("Image "+std::to_string(source)+" cannot be decoded; its material is drawn untextured.");return textureIndex[key]=-1;}
   bool changed=false;size_t n=size_t(x)*y;
   for(size_t i=0;i<n;i++){auto& a=pixels.get()[i*4+3];uint8_t v=use==AlphaUse::Opaque?255:(a>=cut?255:0);changed|=v!=a;a=v;}
   if(changed){Bytes png;auto sink=[](void* context,void* p,int size){auto& b=*static_cast<Bytes*>(context);auto s=static_cast<unsigned char*>(p);b.insert(b.end(),s,s+size);};
    if(!stbi_write_png_to_func(sink,&png,x,y,4,pixels.get(),x*4))vrmFail("vrm.image","Texture encoding failed: image "+std::to_string(source)+" ("+std::to_string(x)+" x "+std::to_string(y)+") cannot be saved again as PNG",place("image",source));out=std::move(png);extension=".png";}
  }
  std::string path="vrm/image"+std::to_string(source)+(use==AlphaUse::Opaque?"_opaque":use==AlphaUse::Cutout?"_cutout"+std::to_string(cut):"")+extension;
  textures[path]=std::move(out);int index=int(texturePaths.size());texturePaths.push_back(path);return textureIndex[key]=index;
 }
 // KHR_texture_transform: scale, rotate, then offset. The rotation is the one the
 // Khronos glTF Sample Renderer and three.js (three-vrm) apply to glTF's
 // top-left UV space, u' = cos*u + sin*v, v' = -sin*u + cos*v: counter-clockwise
 // as the extension's text states. Its GLSL sample, read as column-major, is the
 // transpose; following it would turn every rotated atlas region the other way.
 struct UvTransform {Vec2 offset{0},scale{1};float rotation=0;bool identity()const{return offset==Vec2(0)&&scale==Vec2(1)&&rotation==0;} Vec2 apply(Vec2 uv)const{float c=std::cos(rotation),s=std::sin(rotation);Vec2 p=uv*scale;return Vec2(c*p.x+s*p.y,-s*p.x+c*p.y)+offset;}};
 std::map<int,UvTransform> uvTransform;std::map<int,int> uvSet;
 PMaterial& material(int index){
  if(auto it=materials.find(index);it!=materials.end())return it->second;
  auto& list=arr(d.j,"materials");PMaterial m;m.source=index;
  if(index<0||size_t(index)>=list.size()){m.name="default";m.twoSided=true;return materials[index]=m;}
  auto& g=list[index];m.name=string(g,"name","material_"+std::to_string(index));auto& pbr=obj(g,"pbrMetallicRoughness");
  m.diffuse=vec4(pbr.value("baseColorFactor",Json()),Vec4(1));m.twoSided=g.value("doubleSided",false);
  std::string mode=string(g,"alphaMode","OPAQUE");float cutoff=number(g,"alphaCutoff",.5f);
  int baseTexture=-1;UvTransform transform;int set=0;
  if(pbr.contains("baseColorTexture")){auto& t=pbr["baseColorTexture"];baseTexture=integer(t,"index",-1);set=integer(t,"texCoord",0);
   if(t.contains("extensions")&&t["extensions"].contains("KHR_texture_transform")){auto& x=t["extensions"]["KHR_texture_transform"];auto o=x.value("offset",Json()),s=x.value("scale",Json());
    if(o.is_array()&&o.size()==2)transform.offset={o[0].get<float>(),o[1].get<float>()};if(s.is_array()&&s.size()==2)transform.scale={s[0].get<float>(),s[1].get<float>()};transform.rotation=number(x,"rotation",0);set=integer(x,"texCoord",set);}}
  m.queue=mode=="BLEND"?3000:mode=="MASK"?2450:2000;int sphereTexture=-1;float outlineWidth=0;int outlineMode=0;Vec4 outline(0,0,0,1);
  if(auto p=mtoon0(index)){
   auto shader=string(*p,"shader");auto& f=obj((*p),"floatProperties");auto& v=obj((*p),"vectorProperties");auto& t=obj((*p),"textureProperties");
   m.queue=integer(*p,"renderQueue",m.queue);m.memo="VRM 0.x "+shader;
   if(shader=="VRM/MToon"){
    if(v.contains("_Color"))m.diffuse=vec4(v["_Color"],m.diffuse);
    if(t.contains("_MainTex")&&baseTexture<0)baseTexture=integer(t,"_MainTex",-1);
    sphereTexture=integer(t,"_SphereAdd",-1);outlineMode=int(number(f,"_OutlineWidthMode",0));outlineWidth=number(f,"_OutlineWidth",0);if(v.contains("_OutlineColor"))outline=vec4(v["_OutlineColor"],outline);
    if(!g.contains("doubleSided"))m.twoSided=number(f,"_CullMode",2)==0;
    if(!g.contains("alphaMode")){int blend=int(number(f,"_BlendMode",0));mode=blend==1?"MASK":blend>=2?"BLEND":"OPAQUE";cutoff=number(f,"_Cutoff",cutoff);}
    auto st=vec4(v.value("_MainTex",Json()),Vec4(0,0,1,1));
    // UniVRM writes _MainTex as [offsetX, offsetY, scaleX, scaleY] in Unity UV space (V up).
    if(transform.identity()&&(st!=Vec4(0,0,1,1))){transform.scale={st.z,st.w};transform.offset={st.x,1-st.w-st.y};}
   }else if(shader.starts_with("VRM/Unlit")){if(t.contains("_MainTex")&&baseTexture<0)baseTexture=integer(t,"_MainTex",-1);}
  }
  if(g.contains("extensions")&&g["extensions"].contains("VRMC_materials_mtoon")){
   auto& x=g["extensions"]["VRMC_materials_mtoon"];m.memo="VRM 1.0 MToon";
   if(x.contains("matcapTexture")&&vec3(x.value("matcapFactor",Json::array({1,1,1})),Vec3(1))!=Vec3(0))sphereTexture=integer(x["matcapTexture"],"index",-1);
   auto mode1=string(x,"outlineWidthMode","none");outlineMode=mode1=="worldCoordinates"?1:mode1=="screenCoordinates"?2:0;outlineWidth=number(x,"outlineWidthFactor",0)*100; // metres -> centimetres as in 0.x
   auto c=vec3(x.value("outlineColorFactor",Json()),Vec3(0));outline=Vec4(c,1);
   m.queue+=integer(x,"renderQueueOffsetNumber",0);
  }
  if(m.memo.empty())m.memo="glTF material";
  AlphaUse use=mode=="OPAQUE"?AlphaUse::Opaque:mode=="MASK"?AlphaUse::Cutout:AlphaUse::Keep;
  m.texture=texture(baseTexture,use,cutoff);if(sphereTexture>=0){m.sphere=texture(sphereTexture,AlphaUse::Keep,0);m.sphereMode=m.sphere>=0?2:0;}
  if(mode=="OPAQUE")m.diffuse.w=1; // glTF OPAQUE ignores alpha entirely
  if(outlineMode>0&&outlineWidth>0){m.edge=true;m.edgeColor=outline;m.edgeSize=outlineMode==1?outlineWidth*2:outlineWidth;}
  if(!transform.identity())uvTransform[index]=transform;uvSet[index]=set;
  return materials[index]=m;
 }

 // ---- geometry ----
 void readGeometry(){
  auto& meshes=arr(d.j,"meshes");auto& skins=arr(d.j,"skins");
  meshNames.resize(meshes.size());for(size_t i=0;i<meshes.size();i++)meshNames[i]=string(meshes[i],"name","mesh"+std::to_string(i));
  std::vector<int> order;std::function<void(int)> dfs=[&](int i){order.push_back(i);for(int c:nodes[i].children)dfs(c);};for(int r:roots)dfs(r);
  for(int n:order){auto& node=nodes[n];if(node.mesh<0)continue;if(size_t(node.mesh)>=meshes.size())vrmFail("vrm.data","Invalid VRM file: "+placeText(place("node",n,node.name))+" uses mesh "+std::to_string(node.mesh)+", which does not exist (the file has "+number(meshes.size())+")",place("node",n,node.name));
   ImportScope meshScope("mesh",node.mesh,string(meshes[node.mesh],"name"));auto& mesh=meshes[node.mesh];const Json* names=&arr(obj(mesh,"extras"),"targetNames");
   std::vector<Mat4> skinMatrices;std::vector<int> skinBones;
   if(node.skin>=0){if(size_t(node.skin)>=skins.size())vrmFail("vrm.data","Invalid VRM file: "+placeText(place("node",n,node.name))+" uses skin "+std::to_string(node.skin)+", which does not exist (the file has "+number(skins.size())+")",place("node",n,node.name));
    ImportScope skinScope("skin",node.skin,string(skins[node.skin],"name"));auto& skin=skins[node.skin];auto& joints=arr(skin,"joints");
    Accessor inverse;if(skin.contains("inverseBindMatrices")){inverse=accessor(d,skin["inverseBindMatrices"]);if(inverse.components!=16)vrmFail("vrm.data","Invalid VRM file: the inverse bind matrices of skin "+std::to_string(node.skin)+" are not 4 x 4 matrices");}
    for(size_t k=0;k<joints.size();k++){int jn=joints[k].get<int>();if(jn<0||size_t(jn)>=nodes.size())vrmFail("vrm.data","Invalid VRM file: joint "+std::to_string(k)+" of skin "+std::to_string(node.skin)+" is node "+std::to_string(jn)+", which does not exist (the file has "+number(nodes.size())+" nodes)",Json(),{{"joint",k},{"node",jn}});Mat4 ibm(1.f);if(inverse.count>k)ibm=glm::make_mat4(&inverse.values[k*16]);skinMatrices.push_back(nodes[jn].world*ibm);skinBones.push_back(boneFor(jn));}}
   int fallback=boneFor(n);
   auto& primitives=arr(mesh,"primitives");
   for(size_t pi=0;pi<primitives.size();pi++){auto& p=primitives[pi];int mode=integer(p,"mode",4);ImportScope primitiveScope("primitive",int64_t(pi),{});
    if(mode!=4&&mode!=5&&mode!=6){warn("Mesh \""+meshNames[node.mesh]+"\" has a primitive drawn as points or lines; it was skipped.");continue;}
    auto& attributes=p.at("attributes");if(!attributes.contains("POSITION"))continue;
    if(names->empty())names=&arr(obj(p,"extras"),"targetNames");
    auto positions=accessor(d,attributes["POSITION"]);size_t count=positions.count;if(positions.components!=3)vrmFail("vrm.data","Invalid VRM vertex positions: "+meshText(node.mesh,pi)+" has positions with "+std::to_string(positions.components)+" components instead of 3",Json(),{{"field","POSITION"}});
    Accessor normals,uvs;if(attributes.contains("NORMAL"))normals=accessor(d,attributes["NORMAL"]);
    int matIndex=integer(p,"material",-1);auto& mat=material(matIndex);int set=uvSet.count(matIndex)?uvSet[matIndex]:0;
    auto uvName="TEXCOORD_"+std::to_string(set);if(attributes.contains(uvName))uvs=accessor(d,attributes[uvName]);else if(attributes.contains("TEXCOORD_0"))uvs=accessor(d,attributes["TEXCOORD_0"]);
    std::vector<Accessor> joints,weights;for(int s=0;s<4;s++){auto j="JOINTS_"+std::to_string(s),w="WEIGHTS_"+std::to_string(s);if(!attributes.contains(j)||!attributes.contains(w))break;joints.push_back(accessor(d,attributes[j]));weights.push_back(accessor(d,attributes[w]));}
    // Attributes are read at every POSITION index with fixed component counts.
    if(normals.count&&normals.components!=3)vrmFail("vrm.data","Invalid VRM vertex normals: "+meshText(node.mesh,pi)+" has normals with "+std::to_string(normals.components)+" components instead of 3",Json(),{{"field","NORMAL"}});
    if(uvs.count&&uvs.components!=2)vrmFail("vrm.data","Invalid VRM texture coordinates: "+meshText(node.mesh,pi)+" has texture coordinates with "+std::to_string(uvs.components)+" components instead of 2",Json(),{{"field",uvName}});
    for(size_t s=0;s<joints.size();s++)if(joints[s].components!=4||weights[s].components!=4||joints[s].count<count||weights[s].count<count)
     vrmFail("vrm.data","Invalid VRM skin weights: "+meshText(node.mesh,pi)+" has JOINTS_"+std::to_string(s)+"/WEIGHTS_"+std::to_string(s)+" with "+number(std::min(joints[s].count,weights[s].count))+" entries for "+number(count)+" vertices",Json(),{{"field","WEIGHTS_"+std::to_string(s)}});
    const UvTransform* transform=uvTransform.count(matIndex)?&uvTransform[matIndex]:nullptr;
    std::vector<uint32_t> index;if(p.contains("indices"))index=indexAccessor(d,p["indices"]);else{index.resize(count);for(size_t i=0;i<count;i++)index[i]=uint32_t(i);}
    for(size_t t=0;t<index.size();t++)if(index[t]>=count)vrmFail("vrm.data","Invalid VRM triangle index: "+meshText(node.mesh,pi)+" uses vertex "+number(index[t])+", but the primitive has "+number(count)+" vertices",Json(),{{"field","indices"},{"value",index[t]},{"count",count}});
    // Primitives of one mesh often share a vertex buffer (VRoid: every submesh);
    // keep only the vertices this primitive draws.
    std::vector<int64_t> remap(count,-1);for(auto i:index)remap[i]=0;
    std::vector<uint32_t> used;for(size_t i=0;i<count;i++)if(remap[i]==0){remap[i]=int64_t(vertices.size()+used.size());used.push_back(uint32_t(i));}
    std::vector<Mat3> linear(used.size());
    for(size_t u=0;u<used.size();u++){size_t i=used[u];
     std::vector<std::pair<float,int>> influences;
     if(!skinMatrices.empty())for(size_t s=0;s<joints.size();s++)for(int c=0;c<4&&c<joints[s].components;c++){float w=weights[s].at(i,c);int j=int(joints[s].at(i,c));if(w>0&&j>=0&&size_t(j)<skinMatrices.size())influences.push_back({w,j});}
     float total=0;for(auto& f:influences)total+=f.first;
     Mat4 m(0.f);if(total>1e-8f){for(auto& f:influences)m+=skinMatrices[f.second]*(f.first/total);}else m=node.world;
     Vec3 position(positions.at(i,0),positions.at(i,1),positions.at(i,2));PVertex v;v.p=axes.point(Vec3(m*Vec4(position,1)));linear[u]=Mat3(m);
     if(normals.count>i){Vec3 normal=Mat3(m)*Vec3(normals.at(i,0),normals.at(i,1),normals.at(i,2));v.n=glm::length(normal)>1e-12f?axes.dir(glm::normalize(normal)):Vec3(0);}else v.n=Vec3(0);
     if(uvs.count>i){v.uv={uvs.at(i,0),uvs.at(i,1)};if(transform)v.uv=transform->apply(v.uv);}
     if(total>1e-8f){
      // Merge repeated joints, keep the four strongest influences (PMX BDEF4).
      std::map<int,float> merged;for(auto& f:influences)merged[skinBones[f.second]]+=f.first/total;
      std::vector<std::pair<float,int>> sorted;for(auto& [bone,w]:merged)sorted.push_back({w,bone});std::sort(sorted.begin(),sorted.end(),[](auto& a,auto& b){return a.first>b.first;});
      if(sorted.size()>4)sorted.resize(4);float kept=0;for(auto& s:sorted)kept+=s.first;
      for(size_t k=0;k<sorted.size();k++){v.bone[k]=sorted[k].second;v.weight[k]=sorted[k].first/kept;}
     }else{v.bone[0]=fallback;v.weight[0]=1;}
     if(!finite(v.p))vrmFail("vrm.data","The VRM mesh has non-finite vertex positions: vertex "+number(i)+" of "+meshText(node.mesh,pi)+" is skinned to no number position",Json(),{{"field","POSITION"},{"vertex",i}});
     vertices.push_back(v);vertexMaterial.push_back(matIndex);
    }
    std::vector<std::array<uint32_t,3>> triangles;
    if(mode==4)for(size_t i=0;i+2<index.size();i+=3)triangles.push_back({index[i],index[i+1],index[i+2]});
    else if(mode==5)for(size_t i=0;i+2<index.size();i++)triangles.push_back(i%2?std::array<uint32_t,3>{index[i+1],index[i],index[i+2]}:std::array<uint32_t,3>{index[i],index[i+1],index[i+2]});
    else for(size_t i=1;i+1<index.size();i++)triangles.push_back({index[0],index[i],index[i+1]});
    for(auto& t:triangles)for(auto& k:t)k=uint32_t(remap[k]);
    // Mirroring one axis reverses the apparent winding; PMX keeps glTF's outward normal convention.
    for(auto& t:triangles){mat.indices.push_back(t[0]);mat.indices.push_back(t[2]);mat.indices.push_back(t[1]);}
    uint32_t base=uint32_t(vertices.size()-used.size());
    if(!attributes.contains("NORMAL")){std::vector<Vec3> sum(used.size(),Vec3(0));for(auto& t:triangles){auto a=vertices[t[0]].p,b=vertices[t[2]].p,c=vertices[t[1]].p;auto face=glm::cross(b-a,c-a);for(auto k:t)sum[k-base]+=face;}
     for(size_t u=0;u<used.size();u++)vertices[base+u].n=glm::length(sum[u])>1e-12f?glm::normalize(sum[u]):Vec3(0,1,0);}
    for(size_t u=0;u<used.size();u++)if(glm::length(vertices[base+u].n)<.5f)vertices[base+u].n=Vec3(0,1,0);
    auto& morphs=arr(p,"targets");
    for(size_t t=0;t<morphs.size();t++){if(!morphs[t].contains("POSITION"))continue;auto delta=accessor(d,morphs[t]["POSITION"]);if(delta.count!=count||delta.components!=3)vrmFail("vrm.data","Invalid VRM morph target: target "+std::to_string(t)+" of "+meshText(node.mesh,pi)+" has "+number(delta.count)+" positions for "+number(count)+" vertices",Json(),{{"field","targets"},{"target",t}});
     Target* target=nullptr;
     for(int ti:targetsOfMeshIndex[std::make_pair(node.mesh,int(t))])if(targets[ti].node==n)target=&targets[ti];
     if(!target){Target fresh;fresh.node=n;fresh.mesh=node.mesh;fresh.index=int(t);fresh.name=t<names->size()&&(*names)[t].is_string()?(*names)[t].get<std::string>():meshNames[node.mesh]+"_"+std::to_string(t);
      targetsOfMeshIndex[std::make_pair(node.mesh,int(t))].push_back(int(targets.size()));targets.push_back(fresh);target=&targets.back();}
     // Exporters leave float noise (about 1e-7 m) in untouched vertices; keep offsets above 1 micron.
     for(size_t u=0;u<used.size();u++){size_t i=used[u];Vec3 dv(delta.at(i,0),delta.at(i,1),delta.at(i,2));if(dv==Vec3(0))continue;auto out=axes.point(linear[u]*dv);if(glm::length(out)>1e-6f*VrmUnitsPerMeter)target->deltas.push_back({base+uint32_t(u),out});}
    }
   }
  }
  if(vertices.empty())vrmFail("vrm.no_geometry","The VRM file contains no triangle geometry");
 }
};

// Spring settings shared by VRM 0.x bone groups and VRM 1.0 joints.
struct SpringSettings {float stiffness=1,gravityPower=0,dragForce=.4f,hitRadius=0;Vec3 gravityDir{0,-1,0};};
} // namespace

// A GLB whose JSON chunk declares the VRM 0.x or 1.0 extension. Only the JSON
// chunk is needed, so callers may pass just the file's leading bytes.
bool isVrmData(std::span<const unsigned char> b){
 if(b.size()<20||std::memcmp(b.data(),"glTF",4))return false;
 uint32_t size=le32(b.data()+12),type=le32(b.data()+16);if(type!=0x4E4F534Au||size>b.size()-20)return false;
 try{auto j=Json::parse(b.begin()+20,b.begin()+20+size);auto& extensions=obj(j,"extensions");return extensions.contains("VRMC_vrm")||extensions.contains("VRM");}catch(...){return false;}
}
bool isVrmPath(const fs::path& path){
 auto extension=path.extension().wstring();for(auto& c:extension)c=wchar_t(towlower(c));
 if(extension==L".vrm")return true;if(extension!=L".glb")return false;
 // Read the header and the JSON chunk only; static-prop GLBs can be hundreds of megabytes.
 try{std::ifstream f(ioPath(path),std::ios::binary);Bytes head(20);if(!f.read(reinterpret_cast<char*>(head.data()),20))return false;
  uint32_t size=le32(head.data()+12);if(std::memcmp(head.data(),"glTF",4)||size>(256u<<20))return false;
  head.resize(20+size);if(!f.read(reinterpret_cast<char*>(head.data()+20),size))return false;return isVrmData(head);}catch(...){return false;}
}

// Licence and identity fields of a VRM glTF document (the converter's manifest
// "vrm" block without the rig data); null when it has no VRM extension.
Json vrmMetadata(const Json& j,const std::string& fallbackName){
 auto& extensions=obj(j,"extensions");
 const Json* vrm=extensions.contains("VRMC_vrm")?&extensions.at("VRMC_vrm"):extensions.contains("VRM")?&extensions.at("VRM"):nullptr;
 if(!vrm)return Json();bool v0=!extensions.contains("VRMC_vrm");
 auto& meta=obj(*vrm,"meta");Json info={{"version",v0?"0.x":"1.0"},{"specVersion",string(*vrm,"specVersion",v0?"0.0":"1.0")},{"exporter",string(obj(j,"asset"),"generator")}};
 std::string title=string(meta,v0?"title":"name");if(title.empty())title=fallbackName;
 Json m={{"title",title}};
 if(v0){std::string authors=string(meta,"author");m.update({{"version",string(meta,"version")},{"authors",authors.empty()?Json::array():Json::array({authors})},{"contact",string(meta,"contactInformation")},{"reference",string(meta,"reference")},
  {"licenseName",string(meta,"licenseName")},{"otherLicenseUrl",string(meta,"otherLicenseUrl")},{"otherPermissionUrl",string(meta,"otherPermissionUrl")},{"allowedUser",string(meta,"allowedUserName")},{"violentUsage",string(meta,"violentUssageName")},{"sexualUsage",string(meta,"sexualUssageName")},{"commercialUsage",string(meta,"commercialUssageName")}});
  auto license=string(meta,"licenseName");m["allowRedistribution"]=license=="Redistribution_Prohibited"?Json(false):license.starts_with("CC")?Json(true):Json(nullptr);}
 else{Json authors=Json::array();for(auto& a:arr(meta,"authors"))if(a.is_string())authors.push_back(a);
  m.update({{"version",string(meta,"version")},{"authors",authors},{"copyright",string(meta,"copyrightInformation")},{"contact",string(meta,"contactInformation")},{"licenseUrl",string(meta,"licenseUrl")},{"otherLicenseUrl",string(meta,"otherLicenseUrl")},
   {"avatarPermission",string(meta,"avatarPermission","onlyAuthor")},{"commercialUsage",string(meta,"commercialUsage","personalNonProfit")},{"allowExcessivelyViolentUsage",meta.value("allowExcessivelyViolentUsage",false)},{"allowExcessivelySexualUsage",meta.value("allowExcessivelySexualUsage",false)},
   {"allowPoliticalOrReligiousUsage",meta.value("allowPoliticalOrReligiousUsage",false)},{"allowAntisocialOrHateUsage",meta.value("allowAntisocialOrHateUsage",false)},{"creditNotation",string(meta,"creditNotation","required")},{"modification",string(meta,"modification","prohibited")},{"allowRedistribution",meta.value("allowRedistribution",false)}});}
 info["meta"]=m;
 return info;
}
VrmConversion convertVrm(std::span<const unsigned char> file,const std::string& fallbackName){
 ImportScope scope("Converting VRM avatar","vrm");
 Converter c;c.d=open(file);c.fallbackName=fallbackName;auto& j=c.d.j;auto& extensions=obj(j,"extensions");
 const Json* vrm1=extensions.contains("VRMC_vrm")?&extensions.at("VRMC_vrm"):nullptr;const Json* vrm0=!vrm1&&extensions.contains("VRM")?&extensions.at("VRM"):nullptr;
 if(!vrm1&&!vrm0)vrmFail("vrm.container","This glTF file has no VRM extension, so it has no humanoid map. Static 3D models belong in Static Props.");
 c.v0=vrm0!=nullptr;c.vrm=vrm1?vrm1:vrm0;c.axes.v0=c.v0;
 c.readNodes();
 auto nodeRef=[&](const Json& value)->int{int n=value.is_number_integer()?value.get<int>():-1;return n>=0&&size_t(n)<c.nodes.size()?n:-1;};
 // Humanoid map (VRM 1.0 names).
 auto& humanoid=obj(*c.vrm,"humanoid");
 if(c.v0){for(auto& b:arr(humanoid,"humanBones")){int n=nodeRef(b.value("node",Json()));auto name=string(b,"bone");if(n>=0&&!name.empty())c.human[humanName1(name)]=n;}}
 else{for(auto& [name,b]:obj(humanoid,"humanBones").items()){int n=nodeRef(b.value("node",Json()));if(n>=0)c.human[name]=n;}}
 for(auto required:{"hips","spine","head","leftUpperArm","leftLowerArm","leftHand","rightUpperArm","rightLowerArm","rightHand","leftUpperLeg","leftLowerLeg","leftFoot","rightUpperLeg","rightLowerLeg","rightFoot"})
  if(!c.human.contains(required))vrmFail("vrm.humanoid",std::string("The VRM humanoid map has no ")+required+" bone; the avatar cannot become a ragdoll",place("humanoid_bone",-1,required),{{"bone",required}});
 // Nodes that must stay bones: skins, humanoid, springs and colliders.
 std::set<int> referenced;for(auto& [name,n]:c.human)referenced.insert(n);
 for(auto& skin:arr(j,"skins"))for(auto& jn:arr(skin,"joints")){int n=nodeRef(jn);if(n>=0)referenced.insert(n);}
 std::function<void(int)> subtree=[&](int n){referenced.insert(n);for(int ch:c.nodes[n].children)subtree(ch);};
 const Json* secondary=nullptr;const Json* spring1=nullptr;
 if(c.v0){secondary=&obj(*c.vrm,"secondaryAnimation");
  for(auto& g:arr(*secondary,"boneGroups")){for(auto& r:arr(g,"bones")){int n=nodeRef(r);if(n>=0)subtree(n);}int center=nodeRef(g.value("center",Json()));if(center>=0)referenced.insert(center);}
  for(auto& g:arr(*secondary,"colliderGroups")){int n=nodeRef(g.value("node",Json()));if(n>=0)referenced.insert(n);}}
 else if(extensions.contains("VRMC_springBone")){spring1=&extensions.at("VRMC_springBone");
  for(auto& s:arr(*spring1,"springs")){for(auto& jn:arr(s,"joints")){int n=nodeRef(jn.value("node",Json()));if(n>=0)referenced.insert(n);}int center=nodeRef(s.value("center",Json()));if(center>=0)referenced.insert(center);}
  for(auto& col:arr(*spring1,"colliders")){int n=nodeRef(col.value("node",Json()));if(n>=0)referenced.insert(n);}}
 for(size_t i=0;i<c.nodes.size();i++){auto& n=j["nodes"][i];if(n.contains("extensions")&&n["extensions"].contains("VRMC_node_constraint")){referenced.insert(int(i));auto& k=obj(n["extensions"]["VRMC_node_constraint"],"constraint");for(auto& [kind,value]:k.items()){int s=nodeRef(value.value("source",Json()));if(s>=0)referenced.insert(s);}}}
 c.readBones(referenced);
 c.readGeometry();

 VrmConversion out;
 // ---- spring bones (PMX bones, units and axes) ----
 Json colliders=Json::array(),groups=Json::array(),springs=Json::array(),joints=Json::array();
 auto xyz=[](Vec3 v){return Json::array({v.x,v.y,v.z});};
 auto nodeBasis=[&](int n){return Mat3(c.nodes[n].world);};
 std::set<int> simulated;size_t duplicate=0,degenerate=0;
 auto addJoint=[&](int spring,int head,int tailNode,Vec3 tailOffset,const SpringSettings& s){
  int bone=c.boneOf[head];if(bone<0)return;if(!simulated.insert(bone).second){duplicate++;return;}
  if(glm::length(tailOffset)<1e-5f||!finite(tailOffset)){degenerate++;simulated.erase(bone);return;}
  Vec3 gravity=glm::length(s.gravityDir)>1e-6f?glm::normalize(s.gravityDir):Vec3(0);
  joints.push_back({{"spring",spring},{"bone",bone},{"tail",tailNode>=0?c.boneOf[tailNode]:-1},{"tailOffset",xyz(tailOffset)},{"hitRadius",s.hitRadius*VrmUnitsPerMeter},{"stiffness",s.stiffness},{"gravityPower",s.gravityPower},{"gravityDir",xyz(gravity)},{"dragForce",std::clamp(s.dragForce,0.f,1.f)}});
 };
 if(c.v0&&secondary){
  std::vector<std::vector<int>> groupColliders;
  for(auto& g:arr(*secondary,"colliderGroups")){int n=nodeRef(g.value("node",Json()));std::vector<int> list;
   if(n>=0)for(auto& col:arr(g,"colliders")){Vec3 u=vec3(col.value("offset",Json()),Vec3(0));Vec3 offset=c.axes.dir(nodeBasis(n)*Vec3(u.x,u.y,-u.z))*VrmUnitsPerMeter;
    list.push_back(int(colliders.size()));colliders.push_back({{"bone",c.boneOf[n]},{"shape","sphere"},{"offset",xyz(offset)},{"radius",std::max(0.f,number(col,"radius",0))*VrmUnitsPerMeter}});}
   groupColliders.push_back(list);groups.push_back(list);}
  auto& bg=arr(*secondary,"boneGroups");
  for(size_t gi=0;gi<bg.size();gi++){auto& g=bg[gi];SpringSettings s;s.stiffness=number(g,"stiffiness",1);s.gravityPower=number(g,"gravityPower",0);s.dragForce=number(g,"dragForce",.4f);s.hitRadius=number(g,"hitRadius",.02f);
   s.gravityDir=c.axes.unity(vec3(g.value("gravityDir",Json()),Vec3(0,-1,0)));int center=nodeRef(g.value("center",Json()));
   Json cg=Json::array();for(auto& k:arr(g,"colliderGroups"))if(k.is_number_integer()&&k.get<int>()>=0&&size_t(k.get<int>())<groupColliders.size())cg.push_back(k.get<int>());
   int spring=int(springs.size());springs.push_back({{"name",string(g,"comment","group "+std::to_string(gi))},{"center",center>=0?c.boneOf[center]:-1},{"colliderGroups",cg}});
   // UniVRM 0.x: every node below each root is a joint; its first child is the
   // tail, and a leaf gets a virtual tail 7 cm along its parent-to-node line.
   std::function<void(int)> visit=[&](int n){
    auto& node=c.nodes[n];Vec3 head=c.axes.point(c.worldPosition(n)),offset;int tail=-1;
    if(!node.children.empty()){tail=node.children[0];offset=c.axes.point(c.worldPosition(tail))-head;}
    else{Vec3 from=node.parent>=0?c.axes.point(c.worldPosition(node.parent)):head-Vec3(0,1,0);Vec3 delta=head-from;offset=glm::length(delta)>1e-6f?glm::normalize(delta)*(.07f*VrmUnitsPerMeter):Vec3(0);}
    addJoint(spring,n,tail,offset,s);for(int ch:node.children)visit(ch);
   };
   for(auto& r:arr(g,"bones")){int n=nodeRef(r);if(n>=0)visit(n);}
  }
 }else if(spring1){
  auto& list=arr(*spring1,"colliders");bool extended=false;
  for(auto& col:list){int n=nodeRef(col.value("node",Json()));auto& shape=obj(col,"shape");Json item={{"bone",n>=0?c.boneOf[n]:-1}};
   extended|=col.contains("extensions")&&col["extensions"].contains("VRMC_springBone_extended_collider");
   auto local=[&](const Json& v){return n>=0?c.axes.dir(nodeBasis(n)*vec3(v,Vec3(0)))*VrmUnitsPerMeter:Vec3(0);};
   if(shape.contains("capsule")){auto& s=shape["capsule"];item.update({{"shape","capsule"},{"offset",xyz(local(s.value("offset",Json())))},{"tail",xyz(local(s.value("tail",Json())))},{"radius",std::max(0.f,number(s,"radius",0))*VrmUnitsPerMeter}});}
   else{auto& s=obj(shape,"sphere");item.update({{"shape","sphere"},{"offset",xyz(local(s.value("offset",Json())))},{"radius",std::max(0.f,number(s,"radius",0))*VrmUnitsPerMeter}});}
   colliders.push_back(item);}
  if(extended)c.note("VRM approximation: extended spring-bone colliders (planes and inside shapes) use their standard fallback shapes.");
  for(auto& g:arr(*spring1,"colliderGroups")){Json ids=Json::array();for(auto& k:arr(g,"colliders"))if(k.is_number_integer()&&k.get<int>()>=0&&size_t(k.get<int>())<colliders.size())ids.push_back(k.get<int>());groups.push_back(ids);}
  auto list1=arr(*spring1,"springs");
  for(size_t si=0;si<list1.size();si++){auto& s=list1[si];int center=nodeRef(s.value("center",Json()));Json cg=Json::array();for(auto& k:arr(s,"colliderGroups"))if(k.is_number_integer()&&k.get<int>()>=0&&size_t(k.get<int>())<groups.size())cg.push_back(k.get<int>());
   int spring=int(springs.size());springs.push_back({{"name",string(s,"name","spring "+std::to_string(si))},{"center",center>=0?c.boneOf[center]:-1},{"colliderGroups",cg}});
   // VRM 1.0: joint k uses its own settings and joint k+1 as its tail; the last joint is only a tail.
   auto list2=arr(s,"joints");
   for(size_t k=0;k+1<list2.size();k++){auto& a=list2[k];int head=nodeRef(a.value("node",Json())),tail=nodeRef(list2[k+1].value("node",Json()));if(head<0||tail<0)continue;
    bool descendant=false;for(int p=c.nodes[tail].parent;p>=0;p=c.nodes[p].parent)if(p==head){descendant=true;break;}
    if(!descendant){c.warn("Spring \""+string(s,"name","spring "+std::to_string(si))+"\" lists a joint that is not below the previous one; that link was skipped.");continue;}
    SpringSettings st;st.stiffness=number(a,"stiffness",1);st.gravityPower=number(a,"gravityPower",0);st.dragForce=number(a,"dragForce",.5f);st.hitRadius=number(a,"hitRadius",0);st.gravityDir=c.axes.dir(vec3(a.value("gravityDir",Json()),Vec3(0,-1,0)));
    addJoint(spring,head,tail,c.axes.point(c.worldPosition(tail))-c.axes.point(c.worldPosition(head)),st);}
  }
 }
 if(duplicate)c.note("VRM approximation: "+std::to_string(duplicate)+" spring bone(s) listed by more than one spring group are simulated once.");
 if(degenerate)c.warn(std::to_string(degenerate)+" spring bone(s) have no length (their tail sits on the bone) and stay still.");
 // Parents update before children: bone indices are depth-first.
 std::stable_sort(joints.begin(),joints.end(),[](const Json& a,const Json& b){return a["bone"].get<int>()<b["bone"].get<int>();});

 // ---- node constraints (VRM 1.0) -> PMX inherited rotation ----
 size_t aims=0;
 for(size_t i=0;i<c.nodes.size();i++){auto& n=j["nodes"][i];if(!n.contains("extensions")||!n["extensions"].contains("VRMC_node_constraint")||c.boneOf[i]<0)continue;
  auto& k=obj(n["extensions"]["VRMC_node_constraint"],"constraint");auto& bone=c.bones[c.boneOf[i]];
  for(auto kind:{"rotation","roll"})if(k.contains(kind)){int s=nodeRef(k[kind].value("source",Json()));if(s<0||c.boneOf[s]<0)continue;bone.inherit=c.boneOf[s];bone.inheritWeight=number(k[kind],"weight",1);
   if(std::string(kind)=="roll"){auto axis=string(k[kind],"rollAxis","X");Vec3 local=axis=="Y"?Vec3(0,1,0):axis=="Z"?Vec3(0,0,1):Vec3(1,0,0);bone.fixed=true;bone.fixedAxis=glm::normalize(c.axes.dir(nodeBasis(s)*local));}}
  if(k.contains("aim"))aims++;}
 if(aims)c.warn(std::to_string(aims)+" VRM aim constraint(s) have no PMX equivalent; those bones keep their rest direction.");

 // ---- expressions ----
 struct Bind {int target;float weight;};
 struct Expression {std::string name,jp;int panel=4;std::vector<Bind> binds;std::vector<MaterialOffset> colors;std::vector<std::pair<int,std::pair<Vec2,Vec2>>> uv;bool binary=false;};
 std::vector<Expression> expressions;size_t unsupportedMaterialBinds=0;
 auto materialByName=[&](const std::string& name){auto& list=arr(j,"materials");for(size_t i=0;i<list.size();i++)if(string(list[i],"name")==name)return int(i);return -1;};
 auto setup=[&](Expression& e,const std::string& preset){
  static const std::map<std::string,std::pair<const char*,int>> mmd={{"aa",{"あ",3}},{"ih",{"い",3}},{"ou",{"う",3}},{"ee",{"え",3}},{"oh",{"お",3}},{"blink",{"まばたき",2}},{"blinkLeft",{"ウィンク２",2}},{"blinkRight",{"ウィンク２右",2}},
   {"lookUp",{"lookUp",2}},{"lookDown",{"lookDown",2}},{"lookLeft",{"lookLeft",2}},{"lookRight",{"lookRight",2}}};
  if(auto it=mmd.find(preset);it!=mmd.end()){e.jp=it->second.first;e.panel=it->second.second;}else e.jp=e.name;
 };
 if(c.v0){
  for(auto& g:arr(obj(*c.vrm,"blendShapeMaster"),"blendShapeGroups")){
   Expression e;auto preset=presetName1(string(g,"presetName"));e.name=preset.empty()?string(g,"name","expression"):preset;setup(e,preset);e.binary=g.value("isBinary",false);
   for(auto& b:arr(g,"binds")){int mesh=integer(b,"mesh",-1),index=integer(b,"index",-1);auto it=c.targetsOfMeshIndex.find({mesh,index});if(it==c.targetsOfMeshIndex.end())continue;for(int t:it->second)e.binds.push_back({t,number(b,"weight",100)/100.f});}
   for(auto& mv:arr(g,"materialValues")){int m=materialByName(string(mv,"materialName"));auto property=string(mv,"propertyName");Vec4 target=vec4(mv.value("targetValue",Json()),Vec4(0));
    if(m<0||!c.materials.contains(m)){continue;}
    if(property=="_Color"){e.colors.push_back({m,target-c.materials[m].diffuse});}
    else if(property=="_MainTex_ST"){e.uv.push_back({m,{Vec2(target.x,target.y),Vec2(target.z,1-target.y-target.w)}});}
    else unsupportedMaterialBinds++;}
   expressions.push_back(std::move(e));}
 }else{
  auto& ex=obj(*c.vrm,"expressions");
  for(auto kind:{"preset","custom"})for(auto& [name,g]:obj(ex,kind).items()){
   Expression e;e.name=name;setup(e,std::string(kind)=="preset"?name:std::string());e.binary=g.value("isBinary",false);
   for(auto& b:arr(g,"morphTargetBinds")){int n=nodeRef(b.value("node",Json())),index=integer(b,"index",-1);if(n<0||c.nodes[n].mesh<0)continue;auto it=c.targetsOfMeshIndex.find({c.nodes[n].mesh,index});if(it==c.targetsOfMeshIndex.end())continue;for(int t:it->second)if(c.targets[t].node==n)e.binds.push_back({t,number(b,"weight",1)});}
   for(auto& b:arr(g,"materialColorBinds")){int m=integer(b,"material",-1);if(m<0||!c.materials.contains(m))continue;if(string(b,"type")=="color")e.colors.push_back({m,vec4(b.value("targetValue",Json()),Vec4(0))-c.materials[m].diffuse});else unsupportedMaterialBinds++;}
   for(auto& b:arr(g,"textureTransformBinds")){int m=integer(b,"material",-1);if(m<0||!c.materials.contains(m))continue;auto s=b.value("scale",Json::array({1,1})),o=b.value("offset",Json::array({0,0}));
    if(s.is_array()&&s.size()==2&&o.is_array()&&o.size()==2)e.uv.push_back({m,{Vec2(s[0].get<float>(),s[1].get<float>()),Vec2(o[0].get<float>(),o[1].get<float>())}});}
   expressions.push_back(std::move(e));}
 }
 if(unsupportedMaterialBinds)c.note("VRM approximation: "+std::to_string(unsupportedMaterialBinds)+" expression material effect(s) (shade, emission, rim or outline colour) have no Source equivalent and are ignored.");
 if(std::any_of(expressions.begin(),expressions.end(),[](auto& e){return e.binary;}))c.note("VRM approximation: expressions marked binary blend smoothly instead of switching at 50%.");

 // ---- raw morph targets: one PMX vertex morph per name unless binds tell instances apart ----
 std::map<std::string,std::vector<int>> byName;for(size_t t=0;t<c.targets.size();t++)if(!c.targets[t].deltas.empty())byName[c.targets[t].name].push_back(int(t));
 std::vector<PMorph> vertexMorphs;std::vector<int> morphOfTarget(c.targets.size(),-1);
 std::vector<std::pair<std::string,std::vector<int>>> ordered(byName.begin(),byName.end());
 std::sort(ordered.begin(),ordered.end(),[&](auto& a,auto& b){return a.second.front()<b.second.front();}); // file order
 for(auto& [name,list]:ordered){
  bool merge=list.size()>1;
  for(auto& e:expressions){std::vector<float> w;for(int t:list){float weight=-1;for(auto& b:e.binds)if(b.target==t)weight=std::max(weight,b.weight);w.push_back(weight);}if(std::adjacent_find(w.begin(),w.end(),std::not_equal_to<>())!=w.end())merge=false;}
  auto emit=[&](const std::vector<int>& group,const std::string& label){PMorph m;m.name=label;m.english=label;std::string lower;for(char ch:label)lower+=char(std::tolower((unsigned char)ch));
   m.panel=lower.find("brw")!=std::string::npos||lower.find("brow")!=std::string::npos?1:lower.find("eye")!=std::string::npos||lower.find("blink")!=std::string::npos?2:lower.find("mth")!=std::string::npos||lower.find("mouth")!=std::string::npos?3:4;
   for(int t:group){for(auto& dlt:c.targets[t].deltas)m.vertex.push_back(dlt);morphOfTarget[t]=int(vertexMorphs.size());}vertexMorphs.push_back(std::move(m));};
  if(merge||list.size()==1)emit(list,name);else for(int t:list){auto& node=c.nodes[c.targets[t].node];emit({t},name+" ("+(node.name.empty()?c.meshNames[c.targets[t].mesh]:node.name)+")");}
 }
 // ---- assemble morphs: expressions, their helpers, then raw targets ----
 std::vector<PMorph> front,helpers;Json expressionInfo=Json::array();
 struct Pending {size_t morph;std::vector<std::pair<int,float>> raw;std::vector<size_t> helper;};std::vector<Pending> pending;
 std::map<int,std::vector<uint32_t>> verticesOfMaterial;for(size_t v=0;v<c.vertexMaterial.size();v++)verticesOfMaterial[c.vertexMaterial[v]].push_back(uint32_t(v));
 for(auto& e:expressions){
  std::map<int,float> raw;for(auto& b:e.binds){int m=morphOfTarget[b.target];if(m<0)continue;auto& w=raw[m];w=std::max(w,b.weight);}
  Pending p;p.morph=front.size();for(auto& [m,w]:raw)if(w!=0)p.raw.push_back({m,w});
  if(!e.colors.empty()){PMorph h;h.name=e.jp+" (colour)";h.english=e.name+" (colour)";h.panel=0;h.type=8;for(auto& o:e.colors)h.material.push_back(o);p.helper.push_back(helpers.size());helpers.push_back(std::move(h));}
  if(!e.uv.empty()){PMorph h;h.name=e.jp+" (texture)";h.english=e.name+" (texture)";h.panel=0;h.type=3;
   for(auto& [m,st]:e.uv)for(auto v:verticesOfMaterial[m]){auto uv=c.vertices[v].uv;auto moved=uv*st.first+st.second;if(moved!=uv)h.uv.push_back({v,Vec4(moved-uv,0,0)});}
   if(!h.uv.empty()){p.helper.push_back(helpers.size());helpers.push_back(std::move(h));}}
  if(p.raw.empty()&&p.helper.empty())continue; // e.g. an unbound neutral or look direction
  PMorph g;g.name=e.jp;g.english=e.name;g.panel=e.panel;g.type=0;front.push_back(std::move(g));pending.push_back(std::move(p));
  expressionInfo.push_back({{"name",e.name},{"morph",front.size()-1}});
 }
 size_t helperBase=front.size(),rawBase=front.size()+helpers.size();
 for(auto& p:pending){auto& g=front[p.morph];for(auto h:p.helper)g.group.push_back({int(helperBase+h),1.f});for(auto& [m,w]:p.raw)g.group.push_back({int(rawBase+m),w});}
 std::vector<PMorph> morphs;for(auto* list:{&front,&helpers,&vertexMorphs})for(auto& m:*list)morphs.push_back(std::move(m));

 // ---- materials in draw order ----
 std::vector<PMaterial*> order;for(auto& [index,m]:c.materials)if(!m.indices.empty())order.push_back(&m);
 std::stable_sort(order.begin(),order.end(),[](PMaterial* a,PMaterial* b){return a->queue!=b->queue?a->queue<b->queue:a->source<b->source;});
 std::map<int,int> pmxMaterial;for(size_t i=0;i<order.size();i++)pmxMaterial[order[i]->source]=int(i);
 for(auto& m:morphs)for(auto& o:m.material)o.material=pmxMaterial.count(o.material)?pmxMaterial[o.material]:-1;
 for(auto& m:morphs)std::erase_if(m.material,[](auto& o){return o.material<0;});

 // ---- metadata ----
 Json info=vrmMetadata(j,c.fallbackName);auto& m=info["meta"];std::string title=m["title"].get<std::string>();
 Json humanInfo=Json::object();for(auto& [name,n]:c.human)if(c.boneOf[n]>=0)humanInfo[name]=c.boneOf[n];info["humanoid"]=humanInfo;
 info["springBone"]={{"unitsPerMeter",VrmUnitsPerMeter},{"colliders",colliders},{"colliderGroups",groups},{"springs",springs},{"joints",joints}};
 info["expressions"]=expressionInfo;
 c.note("Source shading approximates VRM MToon materials; shade colour, rim light, outlines and emission are not drawn.");

 // ---- PMX 2.0 ----
 PmxData pmx;pmx.name=title;pmx.comment="Converted from VRM "+info["version"].get<std::string>()+" by Model Hotloader.";
 if(!m["authors"].empty())pmx.comment+="\nAuthor: "+m["authors"][0].get<std::string>();
 pmx.vertices=std::move(c.vertices);pmx.textures=c.texturePaths;pmx.materials.assign(order.begin(),order.end());pmx.bones=c.bones;pmx.morphs=std::move(morphs);
 out.pmx=writePmx(pmx);out.textures=std::move(c.textures);out.vrm=std::move(info);
 for(auto& s:c.warnings)out.warnings.push_back(s);for(auto& s:c.notes)out.warnings.push_back(s);
 return out;
}
}
