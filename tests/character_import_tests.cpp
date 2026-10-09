// Characters in other formats: the bone-name engine (humanoid_map) on many naming
// conventions, and synthetic glTF humanoids (no VRM extension) through the probe,
// the converter, importConverted/loadAsset and the fitter. Nothing here needs a
// GPU, the game or a user's model; --write-fixtures <dir> writes the glTF files.
#include "character_import.hpp"
#include "humanoid_map.hpp"
#include "import_error.hpp"
#include "rig.hpp"
#include "spring_bones.hpp"
#include <nanoem.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
using namespace mmd;
namespace {
int checks=0;
void check(bool ok,const std::string& name){if(!ok)throw std::runtime_error("FAIL "+name);++checks;std::cout<<"PASS "<<name<<"\n";}
template<class F>void fails(F f,const std::string& code,const std::string& reason,const std::string& name){
 try{f();}catch(const ImportError& e){check(e.code==code&&(reason.empty()||e.details.value("reason",std::string())==reason),name+" ("+e.code+" "+e.details.value("reason",std::string())+": "+e.what()+")");return;}
 catch(const std::exception& e){throw std::runtime_error("FAIL "+name+": "+e.what());}
 throw std::runtime_error("FAIL "+name+": no error");
}

// ---- a small humanoid written as a real GLB (glTF space: metres, +Y up, facing +Z, left at +X) ----
struct Glb {
 Json j=Json::object();Bytes bin;
 int view(const void* data,size_t size){while(bin.size()%4)bin.push_back(0);size_t offset=bin.size();auto p=static_cast<const unsigned char*>(data);bin.insert(bin.end(),p,p+size);
  j["bufferViews"].push_back({{"buffer",0},{"byteOffset",offset},{"byteLength",size}});return int(j["bufferViews"].size()-1);}
 int floats(const std::vector<float>& v,const char* type,int components){int bv=view(v.data(),v.size()*4);j["accessors"].push_back({{"bufferView",bv},{"componentType",5126},{"count",v.size()/components},{"type",type}});return int(j["accessors"].size()-1);}
 int shorts(const std::vector<uint16_t>& v){int bv=view(v.data(),v.size()*2);j["accessors"].push_back({{"bufferView",bv},{"componentType",5123},{"count",v.size()/4},{"type","VEC4"}});return int(j["accessors"].size()-1);}
 int indices(const std::vector<uint32_t>& v){int bv=view(v.data(),v.size()*4);j["accessors"].push_back({{"bufferView",bv},{"componentType",5125},{"count",v.size()},{"type","SCALAR"}});return int(j["accessors"].size()-1);}
 Bytes file(const std::vector<std::pair<std::string,std::string>>& raw={}){
  while(bin.size()%4)bin.push_back(0);j["buffers"]=Json::array({{{"byteLength",bin.size()}}});auto text=j.dump();
  // Placeholders become raw bytes (names that are not UTF-8, as old exporters write them).
  for(auto& [from,to]:raw)for(size_t at;(at=text.find(from))!=std::string::npos;)text.replace(at,from.size(),to);
  while(text.size()%4)text.push_back(' ');
  Bytes out;auto u32=[&](uint32_t v){for(int k=0;k<4;k++)out.push_back(uint8_t(v>>(8*k)));};
  out.insert(out.end(),{'g','l','T','F'});u32(2);u32(uint32_t(12+8+text.size()+8+bin.size()));
  u32(uint32_t(text.size()));u32(0x4E4F534Au);out.insert(out.end(),text.begin(),text.end());u32(uint32_t(bin.size()));u32(0x004E4942u);out.insert(out.end(),bin.begin(),bin.end());return out;
 }
};
Bytes png(){unsigned char px[16]={255,255,255,0, 255,255,255,64, 255,255,255,200, 255,255,255,255};Bytes out;
 stbi_write_png_to_func([](void* c,void* d,int n){auto& b=*static_cast<Bytes*>(c);b.insert(b.end(),static_cast<unsigned char*>(d),static_cast<unsigned char*>(d)+n);},&out,2,2,4,px,8);return out;}

struct BoneSpec {std::string name;int parent;btVector3 world;bool weighted=true;};
enum class Names {Mixamo,Unreal,Nonsense};
struct Variant {
 Names names=Names::Mixamo;
 bool zUpCentimetres=false;  // the root node scales by 0.01 and turns Z-up into Y-up
 bool mirrored=false;        // the root node has scale x -1 (a mirrored export)
 int second=0;               // 1: an outfit on a copy of the skeleton (rebound); 2: on a foreign skeleton (dropped)
 bool shiftJis=false;        // one bone name stored as Shift-JIS bytes
 bool duplicate=false;       // two bones share one name
};
std::vector<BoneSpec> skeleton(Names style){
 // Mixamo layout; other styles rename the same bones.
 std::vector<BoneSpec> b;auto add=[&](std::string name,int parent,btVector3 p,bool w=true){b.push_back({std::move(name),parent,p,w});return int(b.size()-1);};
 int hips=add("Hips",-1,{0,.95f,0});int spine=add("Spine",hips,{0,1.05f,0});int spine1=add("Spine1",spine,{0,1.17f,0});int spine2=add("Spine2",spine1,{0,1.30f,0});
 int neck=add("Neck",spine2,{0,1.45f,0});int head=add("Head",neck,{0,1.55f,0});add("HeadTop_End",head,{0,1.75f,0},false);
 add("LeftEye",head,{.03f,1.62f,.08f});add("RightEye",head,{-.03f,1.62f,.08f});
 int hair=add("Hair_01",head,{0,1.66f,-.08f});int hair2=add("Hair_02",hair,{0,1.56f,-.12f});int hair3=add("Hair_03",hair2,{0,1.46f,-.14f});add("Hair_end",hair3,{0,1.38f,-.15f},false);
 for(int side=0;side<2;side++){float s=side?-1.f:1.f;std::string L=side?"Right":"Left";
  int shoulder=add(L+"Shoulder",spine2,{.06f*s,1.40f,0});int arm=add(L+"Arm",shoulder,{.17f*s,1.40f,0});int fore=add(L+"ForeArm",arm,{.43f*s,1.40f,0});
  add(L+"ForeArm_Twist",fore,{.55f*s,1.40f,0});int hand=add(L+"Hand",fore,{.68f*s,1.40f,0});
  const char* fingers[]={"Thumb","Index","Middle","Ring","Pinky"};
  for(int f=0;f<5;f++){int parent=hand;float z=f==0?.04f:.03f-f*.015f;float x0=f==0?.70f:.76f;
   for(int k=1;k<=4;k++)parent=add(L+"Hand"+fingers[f]+std::to_string(k),parent,{(x0+(k-1)*.025f)*s,f==0?1.38f-k*.01f:1.40f,z},k<4);}
  int thigh=add(L+"UpLeg",hips,{.09f*s,.90f,0});int calf=add(L+"Leg",thigh,{.09f*s,.50f,0});int foot=add(L+"Foot",calf,{.09f*s,.08f,0});
  int toe=add(L+"ToeBase",foot,{.09f*s,.02f,.10f});add(L+"Toe_End",toe,{.09f*s,.02f,.16f},false);}
 int skirt=add("Skirt",hips,{0,.90f,0},false);
 for(auto [name,z]:{std::pair{"Skirt_F",.12f},{"Skirt_B",-.12f}}){int a=add(std::string(name)+"_01",skirt,{0,.85f,z});add(std::string(name)+"_02",a,{0,.70f,z*1.15f});}
 add("Ribbon_L",head,{.06f,1.66f,-.04f});
 for(auto& bone:b){
  if(style==Names::Mixamo){if(!bone.name.starts_with("Hair")&&!bone.name.starts_with("Skirt")&&!bone.name.starts_with("Ribbon"))bone.name="mixamorig:"+bone.name;}
  else if(style==Names::Unreal){static const std::map<std::string,std::string> ue={{"Hips","pelvis"},{"Spine","spine_01"},{"Spine1","spine_02"},{"Spine2","spine_03"},{"Neck","neck_01"},{"Head","head"},{"LeftEye","eye_l"},{"RightEye","eye_r"},
    {"LeftShoulder","clavicle_l"},{"LeftArm","upperarm_l"},{"LeftForeArm","lowerarm_l"},{"LeftForeArm_Twist","lowerarm_twist_01_l"},{"LeftHand","hand_l"},{"LeftUpLeg","thigh_l"},{"LeftLeg","calf_l"},{"LeftFoot","foot_l"},{"LeftToeBase","ball_l"},
    {"RightShoulder","clavicle_r"},{"RightArm","upperarm_r"},{"RightForeArm","lowerarm_r"},{"RightForeArm_Twist","lowerarm_twist_01_r"},{"RightHand","hand_r"},{"RightUpLeg","thigh_r"},{"RightLeg","calf_r"},{"RightFoot","foot_r"},{"RightToeBase","ball_r"}};
   if(auto it=ue.find(bone.name);it!=ue.end())bone.name=it->second;
   else for(auto [from,to]:{std::pair{"LeftHand","_l"},{"RightHand","_r"}})if(bone.name.starts_with(from)&&bone.name.size()>std::strlen(from)+1){auto rest=bone.name.substr(std::strlen(from));std::string finger=rest.substr(0,rest.size()-1);for(auto& c:finger)c=char(std::tolower((unsigned char)c));if(finger=="pinky")finger="pinky";bone.name=finger+"_0"+rest.back()+to;}}
  else{static int counter=0;if(!bone.name.starts_with("Hair")&&!bone.name.starts_with("Skirt")&&!bone.name.starts_with("Ribbon")){char text[32];std::snprintf(text,sizeof text,"Bone.%03d",++counter%1000);bone.name=text;}}
 }
 return b;
}
Bytes humanoid(const Variant& v=Variant{}){
 auto bones=skeleton(v.names);if(v.duplicate)bones[bones.size()-1].name="Hair_02";if(v.shiftJis)bones.back().name="@SJIS@";
 int n=int(bones.size());Glb g;auto& j=g.j;j["asset"]={{"version","2.0"},{"generator","mmdhl character_import_tests"}};
 // Node 0 is the armature (not a joint); bones follow; then the mesh nodes.
 j["nodes"].push_back({{"name","Armature"},{"children",Json::array()}});
 if(v.zUpCentimetres){j["nodes"][0]["scale"]={.01f,.01f,.01f};j["nodes"][0]["rotation"]={-std::sqrt(.5f),0,0,std::sqrt(.5f)};}
 if(v.mirrored)j["nodes"][0]["scale"]={-1,1,1};
 // Positions under the armature node: undo its transform so the character lands at the same world place.
 auto local=[&](btVector3 p){if(v.zUpCentimetres)return btVector3(p.x()*100,-p.z()*100,p.y()*100);if(v.mirrored)return btVector3(-p.x(),p.y(),p.z());return p;};
 for(int i=0;i<n;i++){auto& b=bones[i];auto t=local(b.world)-(b.parent>=0?local(bones[b.parent].world):btVector3(0,0,0));
  Json node={{"name",b.name},{"translation",{t.x(),t.y(),t.z()}}};Json children=Json::array();for(int c=0;c<n;c++)if(bones[c].parent==i)children.push_back(c+1);if(!children.empty())node["children"]=children;j["nodes"].push_back(node);
  if(b.parent<0)j["nodes"][0]["children"].push_back(i+1);}
 int meshNode=int(j["nodes"].size());j["nodes"].push_back({{"name","Body"},{"mesh",0},{"skin",0}});
 Json roots=Json::array({0,meshNode});
 // One 4 cm box per weighted bone, weighted fully to it.
 std::vector<float> position,normal,uv,weight,aa,blink,ibm;std::vector<uint16_t> joint;std::vector<uint32_t> index;Json jointList=Json::array();std::map<int,int> jointOf;
 for(int b=0;b<n;b++){jointOf[b]=int(jointList.size());jointList.push_back(b+1);auto c=local(bones[b].world);
  // Inverse bind matrices in the armature's local space (the skin's joints' parent).
  float m[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, -c.x(),-c.y(),-c.z(),1};ibm.insert(ibm.end(),m,m+16);}
 for(int b=0;b<n;b++){if(!bones[b].weighted)continue;auto c=local(bones[b].world);uint32_t base=uint32_t(position.size()/3);float half=v.zUpCentimetres?2.f:.02f;
  for(int k=0;k<8;k++){btVector3 s((k&1)?1.f:-1.f,(k&2)?1.f:-1.f,(k&4)?1.f:-1.f);auto p=c+s*half;auto nn=s.normalized();
   position.insert(position.end(),{p.x(),p.y(),p.z()});normal.insert(normal.end(),{nn.x(),nn.y(),nn.z()});uv.insert(uv.end(),{(k&1)?1.f:0.f,(k&2)?1.f:0.f});
   joint.insert(joint.end(),{uint16_t(jointOf[b]),0,0,0});weight.insert(weight.end(),{1,0,0,0});
   bool head=bones[b].name.ends_with("Head")||bones[b].name=="head"||bones[b].name=="Bone.006";
   btVector3 d=head?btVector3(0,-.01f,.005f):btVector3(0,0,0);if(v.zUpCentimetres)d=btVector3(d.x()*100,-d.z()*100,d.y()*100);if(v.mirrored)d.setX(-d.x());
   aa.insert(aa.end(),{d.x(),d.y(),d.z()});blink.insert(blink.end(),{0,head?.002f:0.f,0});}
  const int faces[6][4]={{0,2,6,4},{1,5,7,3},{0,4,5,1},{2,3,7,6},{0,1,3,2},{4,6,7,5}};
  for(auto& f:faces)for(int t=0;t<2;t++){uint32_t a=f[0],x=f[t+1],y=f[t+2];
   btVector3 pa(position[(base+a)*3],position[(base+a)*3+1],position[(base+a)*3+2]),px(position[(base+x)*3],position[(base+x)*3+1],position[(base+x)*3+2]),py(position[(base+y)*3],position[(base+y)*3+1],position[(base+y)*3+2]);
   btVector3 outward=(pa+px+py)/3-c;if((px-pa).cross(py-pa).dot(outward)<0)std::swap(x,y); // counter-clockwise from outside in the mesh's own space (glTF: a mirrored node turns it clockwise)
   index.insert(index.end(),{base+a,base+x,base+y});}}
 int P=g.floats(position,"VEC3",3),N=g.floats(normal,"VEC3",3),T=g.floats(uv,"VEC2",2),J=g.shorts(joint),W=g.floats(weight,"VEC4",4),A=g.floats(aa,"VEC3",3),B=g.floats(blink,"VEC3",3),I=g.floats(ibm,"MAT4",16);
 Json attributes={{"POSITION",P},{"NORMAL",N},{"TEXCOORD_0",T},{"JOINTS_0",J},{"WEIGHTS_0",W}};
 j["meshes"]=Json::array({{{"name","Body"},{"extras",{{"targetNames",{"vrc.v_aa","Blink"}}}},{"primitives",Json::array({{{"attributes",attributes},{"indices",g.indices(index)},{"material",0},{"targets",Json::array({{{"POSITION",A}},{{"POSITION",B}}})}}})}}});
 j["skins"]=Json::array({{{"joints",jointList},{"inverseBindMatrices",I}}});
 if(v.second){
  // An outfit: a box skinned to a second skeleton (a copy with the same names, or foreign names).
  int root=int(j["nodes"].size());j["nodes"].push_back({{"name","OutfitRig"},{"children",{root+1}}});
  j["nodes"].push_back({{"name",v.second==1?bones[0].name:"Foreign_Root"},{"translation",{0,local(bones[0].world).y(),0}}});
  int outfit=int(j["nodes"].size());j["nodes"].push_back({{"name","Hat"},{"mesh",1},{"skin",1}});roots.push_back(root);roots.push_back(outfit);
  std::vector<float> p2,n2,w2;std::vector<uint16_t> j2;std::vector<uint32_t> i2{0,1,2,0,2,3};auto c=local(bones[0].world);
  for(int k=0;k<4;k++){p2.insert(p2.end(),{c.x()+(k&1?.1f:-.1f),c.y(),c.z()+(k&2?.1f:-.1f)});n2.insert(n2.end(),{0,1,0});j2.insert(j2.end(),{0,0,0,0});w2.insert(w2.end(),{1,0,0,0});}
  std::swap(i2[1],i2[2]);
  float m[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,-c.y(),0,1};std::vector<float> ibm2(m,m+16);
  j["meshes"].push_back({{"name","Hat"},{"primitives",Json::array({{{"attributes",{{"POSITION",g.floats(p2,"VEC3",3)},{"NORMAL",g.floats(n2,"VEC3",3)},{"JOINTS_0",g.shorts(j2)},{"WEIGHTS_0",g.floats(w2,"VEC4",4)}}},{"indices",g.indices(i2)},{"material",0}}})}});
  j["skins"].push_back({{"joints",{root+1}},{"inverseBindMatrices",g.floats(ibm2,"MAT4",16)}});
 }
 j["scenes"]=Json::array({{{"nodes",roots}}});j["scene"]=0;
 auto image=png();int iv=g.view(image.data(),image.size());j["images"]=Json::array({{{"bufferView",iv},{"mimeType","image/png"}}});j["textures"]=Json::array({{{"source",0}}});
 j["materials"]=Json::array({{{"name","Body"},{"alphaMode","MASK"},{"alphaCutoff",.5f},{"pbrMetallicRoughness",{{"baseColorTexture",{{"index",0}}},{"baseColorFactor",{1,1,1,1}}}}}});
 // 左足 in Shift-JIS: 8D B6 91 AB.
 return g.file({{"@SJIS@",std::string("\x8D\xB6\x91\xAB",4)}});
}
fs::path temp(const std::wstring& name){auto p=fs::temp_directory_path()/L"mmdhl_character_import"/name;fs::create_directories(p.parent_path());return p;}
fs::path writeFile(const std::wstring& name,const Bytes& bytes){auto p=temp(name);writeAtomic(p,bytes);return p;}
int boneNamed(const Json& probe,const std::string& name){auto& b=probe["skeleton"]["bones"];for(size_t i=0;i<b.size();i++)if(b[i]["name"]==name)return int(i);return -1;}
int pmxBone(const Model& m,const std::string& name){for(size_t i=0;i<m.bones.size();i++)if(m.bones[i].name==name)return int(i);return -1;}
// ---- the name engine ----
SkeletonView view(const std::vector<std::tuple<std::string,int,btVector3,uint32_t>>& bones){
 SkeletonView v;for(auto& [name,parent,p,w]:bones){SkeletonBone b;b.name=name;b.parent=parent;b.position={p.x(),p.y(),p.z()};b.weighted=w;v.bones.push_back(b);}return v;}
BoneMeaning alone(const std::string& name){auto v=view({{name,-1,{0,1,0},10}});return classifyBones(v)[0];}
Json slotsJson(){Json out=Json::array();for(auto& s:humanoidSlots()){Json anchors=Json::array();for(auto a:s.anchors)if(a)anchors.push_back(a);
 out.push_back({{"key",s.key},{"id",s.id},{"group",s.group},{"family",s.family},{"side",s.side?std::string(1,s.side):std::string()},{"segment",s.segment},{"required",s.required},{"physical",s.physical},
  {"recommended",s.recommended},{"convertOnly",s.convertOnly},{"anchors",anchors},{"partner",s.partner},{"mmdJp",s.mmdJp},{"mmdEn",s.mmdEn}});}return out;}
void nameTests(){
 struct Case{const char* name;const char* meaning;char side;int number;bool daz=false;};
 const Case cases[]={
  {"mixamorig:LeftUpLeg","thigh",'L',-1},{"Bip001 L Forearm","forearm",'L',-1},{"upperarm_twist_01_l","helper",'L',1},{"lShldrBend","upperarm",'L',-1,true},
  {"J_Bip_R_LowerLeg","calf",'R',-1},{"DEF-upper_arm.L","upperarm",'L',-1},{"左ひじ","forearm",'L',-1},{"上半身３","spine",0,3},{"右腕捩3","helper",'R',3},
  {"Hair_F_01_end","helper",0,-1},{"左足ＩＫ","helper",'L',-1},{"左足D","thigh",'L',-1},{"Bip01 R Finger12","index",'R',3},{"LeftHandIndex1","index",'L',1},
  {"Earring_L","accessory",'L',-1},{"LeftForeArm","forearm",'L',-1},{"Elbow_R","forearm",'R',-1},{"왼손","hand",'L',-1},{"Hips","pelvis",0,-1},
  {"mixamorig:Spine2","spine",0,2},{"neck_01","neck",0,1},{"clavicle_l","clavicle",'L',-1},{"ball_r","toe",'R',-1},{"thumb_03_r","thumb",'R',3},
  {"pinky_02_l","little",'L',2},{"index_metacarpal_l","index",'L',-1},{"J_Bip_C_Hips","pelvis",0,-1},{"J_Bip_L_UpperArm","upperarm",'L',-1},{"J_Sec_Hair1_01","hair",0,-1},
  {"J_Adj_L_FaceEye","eye",'L',-1},{"DEF-shin.L","calf",'L',-1},{"DEF-f_index.01.L","index",'L',1},{"ORG-thigh.L","thigh",'L',-1},{"MCH-thigh.L","helper",'L',-1},
  {"Bip001 Pelvis","pelvis",0,-1},{"Bip001LUpperArm","upperarm",'L',-1},{"Bip001 L Toe0","toe",'L',0},{"ValveBiped.Bip01_L_Thigh","thigh",'L',-1},
  {"ValveBiped.Bip01_Spine4","spine",0,4},{"ValveBiped.Bip01_L_Finger0","thumb",'L',1},{"ValveBiped.Bip01_R_Finger41","little",'R',2},{"lCollar","clavicle",'L',-1,true},
  {"lThighBend","thigh",'L',-1,true},{"abdomenLower","spine",0,-1},{"chestUpper","spine",0,-1},{"neckLower","neck",0,-1},{"下半身","pelvis",0,-1},{"首","neck",0,-1},
  {"頭","head",0,-1},{"左肩","clavicle",'L',-1},{"左腕","upperarm",'L',-1},{"左手首","hand",'L',-1},{"右足","thigh",'R',-1},{"右ひざ","calf",'R',-1},{"右足首","foot",'R',-1},
  {"左つま先","toe",'L',-1},{"左足先EX","toe",'L',-1},{"左親指０","thumb",'L',0},{"右人指１","index",'R',1},{"左薬指３","ring",'L',3},{"左目","eye",'L',-1},{"両目","helper",0,-1},
  {"センター","helper",0,-1},{"全ての親","helper",0,-1},{"左肩P","helper",'L',-1},{"EyeBrow_L","",'L',-1},{"mixamorig:HeadTop_End","helper",0,-1},{"Ponytail_02","hair",0,2},
  {"Skirt_B_01","skirt",0,1},{"Tail_03","tail",0,3},{"Breast_L","chest",'L',-1},{"Necklace","accessory",0,-1},{"HeadBand","accessory",0,-1},{"upper body2","spine",0,2},
  {"lower body","pelvis",0,-1},{"knee_L","calf",'L',-1},{"wrist_R","hand",'R',-1},{"thumb0_L","thumb",'L',0},{"fore2_L","index",'L',2},{"third1_L","ring",'L',1},
  {"Scapula_L","clavicle",'L',-1},{"Root_M","helper",0,-1},{"左大腿","thigh",'L',-1},{"头","head",0,-1},{"왼쪽어깨","clavicle",'L',-1},{"Bip001 R Calf","calf",'R',-1},
  {"Character1_LeftUpLeg","thigh",'L',-1},{"CC_Base_L_Upperarm","upperarm",'L',-1},{"J_L_Arm_00_tw","helper",0,-1},{"Skn_L_Knee_Fix_02","helper",0,2}};
 size_t passed=0;
 for(auto& c:cases){auto p=parseBoneName(c.name,c.daz);std::string meaning;
  if(c.daz){meaning=p.helper?"helper":!p.family.empty()?p.family:p.chain;}else meaning=alone(c.name).meaning;
  bool ok=meaning==c.meaning&&p.side==c.side&&(c.number<0||p.number==c.number);
  if(!ok)std::cout<<"  "<<c.name<<": meaning "<<meaning<<" side "<<(p.side?p.side:'-')<<" number "<<p.number<<"\n";
  passed+=ok;}
 check(passed==std::size(cases),"names: "+std::to_string(std::size(cases))+" naming conventions parse to their body part, side and number");
 check(parseBoneName("upperarm_twist_01_l",false).twist&&parseBoneName("右腕捩3",false).twist&&parseBoneName("左足ＩＫ",false).ik&&parseBoneName("左足D",false).dbone,"names: twist, IK and MMD deform-duplicate bones are flagged");
 check(parseBoneName("J_Sec_Hair1_01",false).secondaryHint&&parseBoneName("ORG-thigh.L",false).nonDeform&&parseBoneName("index_metacarpal_l",false).metacarpal,"names: secondary, non-deforming and metacarpal bones are flagged");
 check(parseBoneName("上半身3",false).base=="上半身"&&parseBoneName("左人差指１",false).base=="人差指","names: CJK characters are kept, never stripped like the fitter's loose ASCII pass");
 // Context: "leg" beside an upper leg is the calf, beside a knee the thigh; "arm" yields to an upper arm.
 {auto v=view({{"LeftUpLeg",-1,{.1f,.9f,0},10},{"LeftLeg",0,{.1f,.5f,0},10}});check(classifyBones(v)[1].meaning=="calf","names: Mixamo LeftLeg is the lower leg beside LeftUpLeg");}
 {auto v=view({{"leg_L",-1,{.1f,.9f,0},10},{"knee_L",0,{.1f,.5f,0},10}});check(classifyBones(v)[0].meaning=="thigh","names: MMD leg_L is the thigh beside knee_L");}
 {auto v=view({{"Hips",-1,{0,.95f,0},10},{"Leg_L",0,{.1f,.9f,0},10},{"Leg_L_2",1,{.1f,.5f,0},10},{"Leg_R",0,{-.1f,.9f,0},10},{"Leg_R_2",3,{-.1f,.5f,0},10}});auto m=classifyBones(v);
  check(m[1].meaning=="thigh"&&m[2].meaning=="calf"&&m[2].side=='L'&&m[4].meaning=="calf"&&m[4].side=='R',"names: of two leg bones in a chain the upper is the thigh; an unnamed side comes from the position");}
 {auto v=view({{"upperarm_l",-1,{.2f,1.4f,0},10},{"arm_l",0,{.3f,1.4f,0},10}});check(classifyBones(v)[1].meaning=="","names: arm is not the upper arm where an upper arm exists");}
 {auto v=view({{"Head",-1,{0,1.5f,0},10},{"Hair_Middle",0,{0,1.6f,0},10},{"LeftHand",-1,{.6f,1.4f,0},10},{"LeftHandMiddle1",2,{.65f,1.4f,0},10}});auto m=classifyBones(v);
  check(m[1].meaning=="hair"&&m[3].meaning=="middle","names: finger names count only below a hand (Hair_Middle is hair)");}
 {auto v=view({{"LeftHand",-1,{.6f,1.4f,0},10},{"LeftHandThumb1",0,{.62f,1.4f,0},10},{"LeftHandThumb2",1,{.64f,1.4f,0},10},{"LeftHandThumb3",2,{.66f,1.4f,0},10},{"LeftHandThumb4",3,{.68f,1.4f,0},0}});
  auto m=classifyBones(v);check(m[1].segment==1&&m[2].segment==2&&m[3].segment==3&&m[4].meaning=="thumb"&&m[4].segment==0,"names: finger segments count from the hand");}
 {auto v=view({{"Root_M",-1,{0,1,0},0},{"Hip_L",0,{.1f,.9f,0},10},{"Scapula_L",0,{.05f,1.4f,0},10},{"Shoulder_L",2,{.15f,1.4f,0},10}});auto m=classifyBones(v);
  check(m[1].meaning=="thigh"&&m[2].meaning=="clavicle"&&m[3].meaning=="upperarm","names: Advanced Skeleton Hip_L is the thigh and Shoulder_L the upper arm beside Scapula_L");}
 // Mirror partners: by the swapped side token, else by mirrored position.
 {auto v=view({{"Root",-1,{0,1,0},0},{"LeftArm",0,{.2f,1.4f,0},5},{"RightArm",0,{-.2f,1.4f,0},5},{"hand_l",0,{.6f,1.4f,0},5},{"hand_r",0,{-.6f,1.4f,0},5},{"左腕",0,{.25f,1.3f,0},5},{"右腕",0,{-.25f,1.3f,0},5},
   {"Bip001 L Hand",0,{.5f,1.2f,0},5},{"Bip001 R Hand",0,{-.5f,1.2f,0},5},{"Bone.001",0,{.3f,.8f,0},5},{"Bone.002",0,{-.3f,.8f,0},5}});auto m=classifyBones(v);
  bool ok=m[1].mirror==2&&m[2].mirror==1&&m[3].mirror==4&&m[5].mirror==6&&m[7].mirror==8&&m[9].mirror==10&&m[10].mirror==9&&m[0].mirror==-1;
  check(ok,"names: mirror partners by side token (Left/Right, _l/_r, 左/右, Biped L/R) and by position, symmetric");}
 // Sanitising.
 {std::string issue;check(sanitizeBoneName("左足",&issue)=="左足"&&issue.empty(),"sanitise: valid UTF-8 is kept");
  check(sanitizeBoneName(std::string("\x8D\xB6\x91\xAB",4),&issue)=="左足"&&issue=="cp932","sanitise: Shift-JIS names are decoded");
  auto bad=sanitizeBoneName(std::string("A\xFF\xFF",3),&issue);check(bad=="A\xEF\xBF\xBD\xEF\xBF\xBD"&&issue=="replaced","sanitise: undecodable bytes become U+FFFD");
  check(sanitizeBoneName("Bone\x01\x7F",&issue)=="Bone","sanitise: control characters are stripped");
  std::string longName;for(int i=0;i<100;i++)longName+="あ";auto cut=sanitizeBoneName(longName,&issue);check(cut.size()==255&&cut.substr(252)=="あ","sanitise: long names are cut at 255 bytes on a character boundary");}
}

// ---- naming conventions: every style of one T-posed humanoid ----
struct Role {std::string id,parent;btVector3 p;bool weighted=true;};
std::vector<Role> roles(bool dBones){
 std::vector<Role> r;auto add=[&](std::string id,std::string parent,btVector3 p,bool w=true){r.push_back({std::move(id),std::move(parent),p,w});};
 add("root","",{0,0,0},false);add("hips","root",{0,.95f,0});
 add("spineA","hips",{0,1.03f,0});add("spineB","spineA",{0,1.11f,0});add("spineC","spineB",{0,1.19f,0});add("spineD","spineC",{0,1.27f,0});add("spineE","spineD",{0,1.33f,0});
 add("neck","spineE",{0,1.45f,0});add("neck2","neck",{0,1.49f,0});add("head","neck2",{0,1.55f,0});add("headEnd","head",{0,1.75f,0},false);
 for(auto [S,s]:{std::pair{"L",1.f},{"R",-1.f}}){std::string side=S;
  add("eye"+side,"head",{.03f*s,1.62f,.08f});add("clav"+side,"spineE",{.06f*s,1.40f,0});add("upper"+side,"clav"+side,{.17f*s,1.40f,0});add("upperTwist"+side,"upper"+side,{.3f*s,1.40f,0});
  add("fore"+side,"upper"+side,{.43f*s,1.40f,0});add("foreTwist"+side,"fore"+side,{.55f*s,1.40f,0});add("hand"+side,"fore"+side,{.68f*s,1.40f,0});
  for(int f=0;f<5;f++){std::string parent="hand"+side;for(int k=1;k<=4;k++){auto id="f"+std::to_string(f)+std::to_string(k)+side;add(id,parent,{(.71f+k*.025f)*s,f==0?1.38f-k*.01f:1.40f,.03f-f*.015f},k<4);parent=id;}}
  add("thigh"+side,"hips",{.09f*s,.90f,0},!dBones);add("calf"+side,"thigh"+side,{.09f*s,.50f,0},!dBones);add("foot"+side,"calf"+side,{.09f*s,.08f,0},!dBones);
  add("toe"+side,"foot"+side,{.09f*s,.02f,.10f},!dBones);add("toeEnd"+side,"toe"+side,{.09f*s,.02f,.16f},false);
  add("legIK"+side,"root",{.09f*s,.08f,0},false);
  if(dBones){add("thighD"+side,"hips",{.09f*s,.90f,0});add("calfD"+side,"thighD"+side,{.09f*s,.50f,0});add("footD"+side,"calfD"+side,{.09f*s,.08f,0});add("toeExD"+side,"footD"+side,{.09f*s,.02f,.10f});}}
 return r;
}
struct Style {std::string name;std::map<std::string,std::string> names;std::map<std::string,std::string> expect;bool dBones=false;float confidence=.85f;std::set<std::string> loose;};
SkeletonView build(const Style& style,std::map<std::string,int>& index){
 auto all=roles(style.dBones);SkeletonView v;std::map<std::string,const Role*> byId;for(auto& r:all)byId[r.id]=&r;
 for(auto& r:all){auto it=style.names.find(r.id);if(it==style.names.end())continue;std::string parent=r.parent;while(!parent.empty()&&!style.names.contains(parent))parent=byId[parent]->parent;
  SkeletonBone b;b.name=it->second;b.parent=parent.empty()?-1:index.at(parent);b.position={r.p.x(),r.p.y(),r.p.z()};b.weighted=r.weighted?(r.id=="head"?200:40):0;index[r.id]=int(v.bones.size());v.bones.push_back(b);}
 // Points: a cloud from the floor to the top of the head.
 for(int k=0;k<=40;k++)v.points.insert(v.points.end(),{0,1.8f*k/40,0,0});
 return v;
}
std::vector<Style> styles(){
 std::vector<Style> out;
 auto common=[](Style& s,std::map<std::string,std::string> core){
  for(auto& [k,v]:core)s.names[k]=v;
  for(auto side:{"L","R"})for(auto part:{"clav","upper","fore","hand","thigh","calf","foot","toe"})s.expect[std::string(part)+side]=std::string(part)+side;
  s.expect["hips"]="hips";s.expect["head"]="head";};
 auto sided=[](Style& s,const std::string& part,const std::string& left,const std::string& right){s.names[part+"L"]=left;s.names[part+"R"]=right;};
 auto fingers=[&](Style& s,auto name){for(auto side:{"L","R"})for(int f=0;f<5;f++)for(int k=1;k<=4;k++){auto n=name(side,f,k);if(!n.empty())s.names["f"+std::to_string(f)+std::to_string(k)+side]=n;}};
 const char* mixamoFinger[]={"Thumb","Index","Middle","Ring","Pinky"};
 {Style s;s.name="mixamo";common(s,{{"hips","mixamorig:Hips"},{"spineA","mixamorig:Spine"},{"spineC","mixamorig:Spine1"},{"spineE","mixamorig:Spine2"},{"neck","mixamorig:Neck"},{"head","mixamorig:Head"},{"headEnd","mixamorig:HeadTop_End"}});
  for(auto [p,n]:{std::pair{"eye","Eye"},{"clav","Shoulder"},{"upper","Arm"},{"fore","ForeArm"},{"hand","Hand"},{"thigh","UpLeg"},{"calf","Leg"},{"foot","Foot"},{"toe","ToeBase"},{"toeEnd","Toe_End"}})sided(s,p,std::string("mixamorig:Left")+n,std::string("mixamorig:Right")+n);
  fingers(s,[&](const char* side,int f,int k){return std::string("mixamorig:")+(side[0]=='L'?"Left":"Right")+"Hand"+mixamoFinger[f]+std::to_string(k);});
  s.expect.insert({{"spine1","spineA"},{"spine2","spineC"},{"spine4","spineE"},{"neck1","neck"},{"eyeL","eyeL"},{"eyeR","eyeR"}});out.push_back(s);}
 for(bool five:{false,true}){Style s;s.name=five?"ue5":"ue4";common(s,{{"root","root"},{"hips","pelvis"},{"neck","neck_01"},{"head","head"}});
  if(five)s.names.insert({{"spineA","spine_01"},{"spineB","spine_02"},{"spineC","spine_03"},{"spineD","spine_04"},{"spineE","spine_05"},{"neck2","neck_02"}});else s.names.insert({{"spineA","spine_01"},{"spineC","spine_02"},{"spineE","spine_03"}});
  for(auto [p,n]:{std::pair{"clav","clavicle_"},{"upper","upperarm_"},{"upperTwist","upperarm_twist_01_"},{"fore","lowerarm_"},{"foreTwist","lowerarm_twist_01_"},{"hand","hand_"},{"thigh","thigh_"},{"calf","calf_"},{"foot","foot_"},{"toe","ball_"},{"legIK","ik_foot_"}})sided(s,p,std::string(n)+"l",std::string(n)+"r");
  const char* ue[]={"thumb","index","middle","ring","pinky"};fingers(s,[&](const char* side,int f,int k){return k<4?std::string(ue[f])+"_0"+std::to_string(k)+"_"+(side[0]=='L'?"l":"r"):std::string();});
  s.expect.insert({{"spine1","spineA"},{"spine2",five?"spineC":"spineC"},{"spine4","spineE"},{"neck1","neck"}});out.push_back(s);}
 for(int variant=0;variant<3;variant++){Style s;s.name=variant==0?"unity":variant==1?"unity_noupperchest":"vroid";bool vroid=variant==2;auto c=[&](const char* n){return vroid?std::string("J_Bip_C_")+n:std::string(n);};
  common(s,{{"hips",c("Hips")},{"spineA",c("Spine")},{"neck",c("Neck")},{"head",c("Head")}});
  if(variant==1)s.names["spineE"]=c("Chest");else{s.names["spineC"]=c("Chest");s.names["spineE"]=c("UpperChest");}
  for(auto [p,n]:{std::pair{"clav","Shoulder"},{"upper","UpperArm"},{"fore","LowerArm"},{"hand","Hand"},{"thigh","UpperLeg"},{"calf","LowerLeg"},{"foot","Foot"},{"toe",vroid?"ToeBase":"Toes"}})
   sided(s,p,vroid?std::string("J_Bip_L_")+n:std::string("Left")+n,vroid?std::string("J_Bip_R_")+n:std::string("Right")+n);
  sided(s,"eye",vroid?"J_Adj_L_FaceEye":"LeftEye",vroid?"J_Adj_R_FaceEye":"RightEye");
  const char* unity[]={"Thumb","Index","Middle","Ring","Little"};const char* seg[]={"Proximal","Intermediate","Distal"};
  fingers(s,[&](const char* side,int f,int k)->std::string{if(k>3)return "";if(vroid)return std::string("J_Bip_")+side+"_"+unity[f]+std::to_string(k);return std::string(side[0]=='L'?"Left":"Right")+unity[f]+seg[k-1];});
  s.expect.insert({{"spine1","spineA"},{"spine4","spineE"},{"neck1","neck"},{"eyeL","eyeL"},{"eyeR","eyeR"}});if(variant!=1)s.expect["spine2"]="spineC";out.push_back(s);}
 {Style s;s.name="rigify";s.confidence=.55f;common(s,{{"hips","DEF-spine"},{"spineA","DEF-spine.001"},{"spineC","DEF-spine.002"},{"spineE","DEF-spine.003"},{"neck","DEF-spine.004"},{"neck2","DEF-spine.005"},{"head","DEF-spine.006"}});
  for(auto [p,n]:{std::pair{"clav","DEF-shoulder"},{"upper","DEF-upper_arm"},{"fore","DEF-forearm"},{"hand","DEF-hand"},{"thigh","DEF-thigh"},{"calf","DEF-shin"},{"foot","DEF-foot"},{"toe","DEF-toe"}})sided(s,p,std::string(n)+".L",std::string(n)+".R");
  const char* rig[]={"thumb","f_index","f_middle","f_ring","f_pinky"};fingers(s,[&](const char* side,int f,int k){return k<4?std::string("DEF-")+rig[f]+".0"+std::to_string(k)+"."+side:std::string();});
  s.expect.insert({{"spine1","spineA"},{"spine2","spineC"},{"spine4","spineE"},{"neck1","neck"}});s.loose={"head","neck1"};out.push_back(s);}
 {Style s;s.name="biped";common(s,{{"root","Bip001"},{"hips","Bip001 Pelvis"},{"spineA","Bip001 Spine"},{"spineC","Bip001 Spine1"},{"spineE","Bip001 Spine2"},{"neck","Bip001 Neck"},{"head","Bip001 Head"},{"headEnd","Bip001 HeadNub"}});
  for(auto [p,n]:{std::pair{"clav","Clavicle"},{"upper","UpperArm"},{"fore","Forearm"},{"hand","Hand"},{"thigh","Thigh"},{"calf","Calf"},{"foot","Foot"},{"toe","Toe0"},{"toeEnd","Toe0Nub"}})sided(s,p,std::string("Bip001 L ")+n,std::string("Bip001 R ")+n);
  fingers(s,[&](const char* side,int f,int k){return std::string("Bip001 ")+side+" Finger"+std::to_string(f)+(k==1?"":k<4?std::to_string(k-1):"Nub");});
  s.expect.insert({{"spine1","spineA"},{"spine2","spineC"},{"spine4","spineE"},{"neck1","neck"}});out.push_back(s);}
 {Style s;s.name="daz_g8";common(s,{{"root","hip"},{"hips","pelvis"},{"spineA","abdomenLower"},{"spineC","abdomenUpper"},{"spineD","chestLower"},{"spineE","chestUpper"},{"neck","neckLower"},{"neck2","neckUpper"},{"head","head"}});
  for(auto [p,n]:{std::pair{"clav","Collar"},{"upper","ShldrBend"},{"upperTwist","ShldrTwist"},{"fore","ForearmBend"},{"foreTwist","ForearmTwist"},{"hand","Hand"},{"thigh","ThighBend"},{"calf","Shin"},{"foot","Foot"},{"toe","Toe"},{"eye","Eye"}})sided(s,p,std::string("l")+n,std::string("r")+n);
  const char* daz[]={"Thumb","Index","Mid","Ring","Pinky"};fingers(s,[&](const char* side,int f,int k){return k<4?std::string(side[0]=='L'?"l":"r")+daz[f]+std::to_string(k):std::string();});
  s.expect.insert({{"spine1","spineA"},{"spine4","spineE"},{"neck1","neck"},{"eyeL","eyeL"},{"eyeR","eyeR"}});out.push_back(s);}
 {Style s;s.name="valvebiped";common(s,{{"hips","ValveBiped.Bip01_Pelvis"},{"spineA","ValveBiped.Bip01_Spine"},{"spineB","ValveBiped.Bip01_Spine1"},{"spineC","ValveBiped.Bip01_Spine2"},{"spineE","ValveBiped.Bip01_Spine4"},{"neck","ValveBiped.Bip01_Neck1"},{"head","ValveBiped.Bip01_Head1"}});
  for(auto [p,n]:{std::pair{"clav","Clavicle"},{"upper","UpperArm"},{"fore","Forearm"},{"hand","Hand"},{"thigh","Thigh"},{"calf","Calf"},{"foot","Foot"},{"toe","Toe0"}})sided(s,p,std::string("ValveBiped.Bip01_L_")+n,std::string("ValveBiped.Bip01_R_")+n);
  fingers(s,[&](const char* side,int f,int k){return k<4?std::string("ValveBiped.Bip01_")+side+"_Finger"+std::to_string(f)+(k==1?"":std::to_string(k-1)):std::string();});
  s.expect.insert({{"spine1","spineB"},{"spine2","spineC"},{"spine4","spineE"},{"neck1","neck"}});out.push_back(s);}
 for(bool english:{false,true}){Style s;s.name=english?"mmd_en":"mmd_jp";s.dBones=!english;
  if(english)common(s,{{"root","master"},{"hips","lower body"},{"spineA","upper body"},{"spineC","upper body2"},{"spineE","upper body3"},{"neck","neck"},{"head","head"}});
  else common(s,{{"root","全ての親"},{"hips","下半身"},{"spineA","上半身"},{"spineC","上半身2"},{"spineE","上半身3"},{"neck","首"},{"head","頭"}});
  const std::pair<const char*,const char*> parts[]={{"clav",english?"shoulder":"肩"},{"upper",english?"arm":"腕"},{"upperTwist",english?"arm twist":"腕捩"},{"fore",english?"elbow":"ひじ"},{"foreTwist",english?"wrist twist":"手捩"},{"hand",english?"wrist":"手首"},
   {"thigh",english?"leg":"足"},{"calf",english?"knee":"ひざ"},{"foot",english?"ankle":"足首"},{"toe",english?"toe":"つま先"},{"eye",english?"eye":"目"},{"legIK",english?"leg IK":"足ＩＫ"},{"thighD","足D"},{"calfD","ひざD"},{"footD","足首D"},{"toeExD","足先EX"}};
  for(auto [p,n]:parts){if(english&&std::string(p).ends_with("D"))continue;if(english)sided(s,p,std::string(n)+"_L",std::string(n)+"_R");else sided(s,p,std::string("左")+n,std::string("右")+n);}
  const char* jp[]={"親指","人指","中指","薬指","小指"};const char* en[]={"thumb","fore","middle","third","little"};const char* full[]={"０","１","２","３","４"};
  fingers(s,[&](const char* side,int f,int k){if(k>3)return std::string();int n=f==0?k-1:k;return english?std::string(en[f])+std::to_string(n)+"_"+side:std::string(side[0]=='L'?"左":"右")+jp[f]+full[n];});
  s.expect.insert({{"spine1","spineA"},{"spine2","spineC"},{"spine4","spineE"},{"neck1","neck"},{"eyeL","eyeL"},{"eyeR","eyeR"}});
  if(!english)for(auto side:{"L","R"}){for(auto part:{"thigh","calf","foot"})s.expect[std::string(part)+side]=std::string(part)+"D"+side;s.expect[std::string("toe")+side]=std::string("toeExD")+side;}
  out.push_back(s);}
 {Style s;s.name="numbered";s.confidence=.55f;int k=0;auto all=roles(false);for(auto& r:all){char text[16];std::snprintf(text,sizeof text,"Bone.%03d",k++);if(r.id.find("Twist")==std::string::npos&&!r.id.starts_with("legIK")&&r.id!="eyeL"&&r.id!="eyeR"&&r.id!="neck2")s.names[r.id]=text;}
  for(auto side:{"L","R"})for(auto part:{"upper","fore","hand","thigh","calf","foot"})s.expect[std::string(part)+side]=std::string(part)+side;s.expect["hips"]="hips";s.expect["head"]="head";s.expect["spine1"]="spineA";s.expect["spine4"]="spineE";
  out.push_back(s);}
 return out;
}
void conventionTests(){
 const std::map<std::string,std::string> slotOf={{"hips","Pelvis"},{"spine1","Spine1"},{"spine2","Spine2"},{"spine4","Spine4"},{"neck1","Neck1"},{"head","Head1"}};
 auto key=[&](const std::string& role){if(auto it=slotOf.find(role);it!=slotOf.end())return "ValveBiped.Bip01_"+it->second;if(role=="eyeL")return std::string("Eye_L");if(role=="eyeR")return std::string("Eye_R");
  std::string side=role.substr(role.size()-1),part=role.substr(0,role.size()-1);if(part.ends_with("D"))part.pop_back();if(part=="toeEx")part="toe";
  static const std::map<std::string,std::string> parts={{"clav","Clavicle"},{"upper","UpperArm"},{"fore","Forearm"},{"hand","Hand"},{"thigh","Thigh"},{"calf","Calf"},{"foot","Foot"},{"toe","Toe0"}};
  return "ValveBiped.Bip01_"+side+"_"+parts.at(part);};
 for(auto& style:styles()){std::map<std::string,int> index;auto v=build(style,index);auto m=classifyBones(v);auto g=guessHumanoid(v,m);
  std::vector<std::string> wrong;
  for(auto& [slotRole,role]:style.expect){auto k=key(slotRole);int want=index.at(role);int got=k=="Eye_L"?g.eyeL.bone:k=="Eye_R"?g.eyeR.bone:g.slots[k].bone;float c=k.starts_with("Eye")?1.f:g.slots[k].confidence;
   float floor=style.loose.contains(slotRole)?.55f:style.confidence;if(got!=want||c<floor)wrong.push_back(k+" -> "+(got>=0?v.bones[got].name:"none")+" ("+std::to_string(c)+")");}
  // Unexpected middle-spine and neck picks also count when the style names none.
  if(!style.expect.contains("spine2")&&style.name!="numbered"&&style.name!="daz_g8"&&g.slots["ValveBiped.Bip01_Spine2"].bone>=0)wrong.push_back("Spine2 should be empty");
  int fingersFound=0,fingersNamed=0;for(auto& s:humanoidSlots())if(std::string(s.group).ends_with("fingers")){fingersNamed++;fingersFound+=g.slots[s.key].bone>=0;}
  bool named=style.name!="numbered";
  for(auto& [k,s]:g.slots)if(s.bone>=0){auto& meaning=m[s.bone];if(meaning.twist||meaning.ik)wrong.push_back(k+" took a twist or IK bone");}
  if(!wrong.empty())for(auto& w:wrong)std::cout<<"  "<<style.name<<": "<<w<<"\n";
  check(wrong.empty()&&g.humanoid&&(!named||fingersFound>=28),"conventions: "+style.name+" assigns every body part"+(named?" and "+std::to_string(fingersFound)+" of "+std::to_string(fingersNamed)+" fingers":""));
 }
 // The Ganyu torso: 上半身 -> 上半身3 -> 上半身2 -> neck and shoulders.
 {auto v=view({{"センター",-1,{0,.8f,0},0},{"腰",0,{0,.97f,0},0},{"下半身",1,{0,.977f,0},50},{"上半身",1,{0,.991f,0},50},{"上半身3",3,{0,1.045f,0},50},{"上半身2",4,{0,1.111f,0},80},{"首",5,{0,1.298f,0},20},{"頭",6,{0,1.38f,0},100},
   {"左肩P",5,{.05f,1.25f,0},0},{"左肩",8,{.05f,1.25f,0},10},{"左腕",9,{.15f,1.25f,0},20},{"左ひじ",10,{.38f,1.25f,0},20},{"左手首",11,{.58f,1.25f,0},20},
   {"右肩P",5,{-.05f,1.25f,0},0},{"右肩",13,{-.05f,1.25f,0},10},{"右腕",14,{-.15f,1.25f,0},20},{"右ひじ",15,{-.38f,1.25f,0},20},{"右手首",16,{-.58f,1.25f,0},20},
   {"左足",2,{.08f,.92f,0},20},{"左ひざ",18,{.08f,.5f,0},20},{"左足首",19,{.08f,.08f,0},20},{"右足",2,{-.08f,.92f,0},20},{"右ひざ",21,{-.08f,.5f,0},20},{"右足首",22,{-.08f,.08f,0},20}});
  for(int k=0;k<=20;k++)v.points.insert(v.points.end(),{0,1.5f*k/20,0,0});
  auto g=guessHumanoid(v,classifyBones(v));
  check(g.slots["ValveBiped.Bip01_Spine1"].bone==3&&g.slots["ValveBiped.Bip01_Spine2"].bone==4&&g.slots["ValveBiped.Bip01_Spine4"].bone==5,"torso: Ganyu's inverted chain gives the chest to 上半身2 and the middle spine to 上半身3");}
 // The signature ignores bone order, namespaces, units and rotation.
 {auto mixamo=styles()[0];std::map<std::string,int> index;auto v=build(mixamo,index);auto sig=skeletonSignature(v,classifyBones(v));
  auto moved=v;for(auto& b:moved.bones){b.position={-b.position[0]*100,b.position[1]*100,-b.position[2]*100};if(b.name.starts_with("mixamorig:"))b.name="rig1:"+b.name.substr(10);}
  // A permutation that keeps parents first: reverse the children of each node.
  std::vector<int> order;std::function<void(int)> walk=[&](int b){order.push_back(b);std::vector<int> kids;for(size_t c=0;c<v.bones.size();c++)if(v.bones[c].parent==b)kids.push_back(int(c));for(size_t k=kids.size();k-->0;)walk(kids[k]);};
  for(size_t r=0;r<v.bones.size();r++)if(v.bones[r].parent<0)walk(int(r));
  std::vector<int> at(v.bones.size());for(size_t k=0;k<order.size();k++)at[order[k]]=int(k);
  SkeletonView permuted;for(int b:order){auto bone=moved.bones[b];bone.parent=bone.parent>=0?at[bone.parent]:-1;permuted.bones.push_back(bone);}
  check(skeletonSignature(permuted,classifyBones(permuted))==sig,"signature: unchanged by bone order, namespaces, units and a half turn");}
}
void ruleTests(){
 std::ifstream f("tests/fixtures/bonemap/rules.json");if(!f)throw std::runtime_error("FAIL rules.json is missing (run from the source folder)");auto rules=Json::parse(f);
 size_t passed=0,total=0;
 for(auto& vector:rules["vectors"]){SkeletonView v;for(auto& b:rules["skeletons"][vector["skeleton"].get<std::string>()]["bones"]){SkeletonBone bone;bone.name=b["name"];bone.parent=b["parent"];
   bone.position={b["position"][0].get<float>(),b["position"][1].get<float>(),b["position"][2].get<float>()};bone.weighted=b["weighted"];bone.flags=b["flags"].get<std::vector<std::string>>();v.bones.push_back(bone);}
  std::map<std::string,int> values;for(auto& [k,b]:vector["values"].items())values[k]=b;
  std::multiset<std::string> got,want;for(auto& p:checkBoneMap(v,values,vector["chainRoots"].get<std::vector<int>>()))got.insert(p.code+" "+p.slot+" "+p.severity);
  for(auto& e:vector["expect"])want.insert(e["code"].get<std::string>()+" "+e["slot"].get<std::string>()+" "+e["severity"].get<std::string>());
  total++;if(got==want)passed++;else{std::cout<<"  "<<vector["name"].get<std::string>()<<":";for(auto& g:got)std::cout<<" ["<<g<<"]";std::cout<<" want";for(auto& w:want)std::cout<<" ["<<w<<"]";std::cout<<"\n";}}
 check(total>=30&&passed==total,"rules: "+std::to_string(total)+" shared rule vectors give the expected problems");
 std::ifstream s("tests/fixtures/bonemap/slots.json");if(!s)throw std::runtime_error("FAIL slots.json is missing");
 check(Json::parse(s)==slotsJson(),"slots: the catalogue equals tests/fixtures/bonemap/slots.json (shared with the Lua window)");
 check(humanoidSlots().size()==54&&std::count_if(humanoidSlots().begin(),humanoidSlots().end(),[](const SlotInfo& x){return mappedSlotKey(x.key);})==int(MappedSlotCount)&&!slotByKey("ValveBiped.Bip01_Spine"),"slots: 54 parts, 52 assignable, the synthesized spine locked");
}

// ---- the converter on synthetic glTF humanoids ----
Json autoRequest(const Json& probe,Json jiggle=Json()){
 Json map=Json::object();auto& bones=probe["skeleton"]["bones"];
 for(auto& [key,g]:probe["auto"]["slots"].items()){int b=g["bone"];map[key]=b>=0?bones[b]["name"].get<std::string>():"";}
 Json eyes=Json::object();for(auto side:{"L","R"}){int b=probe["auto"]["eyes"][side]["bone"];eyes[side]=b>=0?bones[b]["name"].get<std::string>():"";}
 Json r={{"kind","character"},{"requestVersion",1},{"boneMap",map},{"eyes",eyes}};if(!jiggle.is_null())r["jiggle"]=jiggle;return r;
}
Json hairJiggle(float stiffness=1){return {{"version",1},{"groups",Json::array({{{"kind","hair"},{"enabled",true},{"swing","normal"},{"custom",false},{"collide",true},
 {"values",{{"stiffness",stiffness},{"dragForce",.4f},{"gravityPower",0},{"hitRadius",.02f}}},{"chains",Json::array({{{"root","Hair_01"},{"enabled",true}}})}},
 {{"kind","skirt"},{"enabled",true},{"swing","normal"},{"custom",false},{"collide",true},{"values",{{"stiffness",.8f},{"dragForce",.5f},{"gravityPower",.3f},{"hitRadius",.03f}}},
  {"chains",Json::array({{{"root","Skirt_F_01"},{"enabled",true}},{{"root","Skirt_B_01"},{"enabled",true}}})}}})}};}
void converterTests(){
 auto mixamo=writeFile(L"mixamo.glb",humanoid());
 // ---- probe ----
 auto probe=probeCharacter(mixamo,Json::object(),{});
 bool keys=true;for(auto k:{"version","format","generator","sourceSha256","units","height","skeleton","auto","meshes","materials","textures","vertices","triangles","morphs","warnings"})keys&=probe.contains(k);
 keys&=probe["sourceSha256"]==hash(readFile(mixamo));
 for(auto k:{"signature","maxDepth","armatures","bones","points"})keys&=probe["skeleton"].contains(k);
 check(keys&&probe["version"]==1&&probe["format"]=="glb","probe: every field of the bone window's input is present");
 auto& bones=probe["skeleton"]["bones"];bool parentsFirst=true;for(size_t i=0;i<bones.size();i++)parentsFirst&=bones[i]["parent"].get<int>()<int(i);
 check(parentsFirst&&boneNamed(probe,"Armature")<0&&boneNamed(probe,"mixamorig:Hips")==0,"probe: bones are parents first, without the armature node");
 check(probe["skeleton"]["points"].size()/4<=3000&&probe["skeleton"]["points"].size()%4==0,"probe: at most 3000 points");
 uint32_t weighted=0;for(auto& b:bones)weighted+=b["weighted"].get<uint32_t>();check(weighted==probe["vertices"].get<uint32_t>(),"probe: every vertex is counted for its bone");
 auto slot=[&](const Json& p,const char* key){int b=p["auto"]["slots"][std::string("ValveBiped.Bip01_")+key]["bone"];return b>=0?p["skeleton"]["bones"][b]["name"].get<std::string>():std::string();};
 auto conf=[&](const Json& p,const char* key){return p["auto"]["slots"][std::string("ValveBiped.Bip01_")+key]["confidence"].get<float>();};
 bool mapped=slot(probe,"Pelvis")=="mixamorig:Hips"&&slot(probe,"Spine1")=="mixamorig:Spine"&&slot(probe,"Spine2")=="mixamorig:Spine1"&&slot(probe,"Spine4")=="mixamorig:Spine2"&&slot(probe,"L_Forearm")=="mixamorig:LeftForeArm"&&slot(probe,"R_Calf")=="mixamorig:RightLeg"&&slot(probe,"L_Finger12")=="mixamorig:LeftHandIndex3";
 bool sure=true;for(auto& s:humanoidSlots())if(s.required)sure&=conf(probe,s.key+17)>=.85f;
 check(mapped&&sure&&probe["auto"]["humanoid"]==true,"probe: the Mixamo skeleton maps automatically and confidently");
 check(probe["auto"]["eyes"]["L"]["bone"]==boneNamed(probe,"mixamorig:LeftEye"),"probe: eyes are found below the head");
 check(std::abs(probe["height"]["meters"].get<float>()-1.68f)<.03f&&probe["units"]["facingSource"]=="names"&&probe["units"]["upSource"]=="skeleton","probe: height, up and facing come from the named skeleton");
 auto again=probeCharacter(mixamo,Json::object(),{});check(again["skeleton"]["points"]==probe["skeleton"]["points"],"probe: the point cloud is deterministic");
 {Variant z;z.zUpCentimetres=true;auto p=probeCharacter(writeFile(L"zup.glb",humanoid(z)),Json::object(),{});
  check(p["skeleton"]["signature"]==probe["skeleton"]["signature"]&&std::abs(p["height"]["meters"].get<float>()-1.68f)<.03f,"probe: a Z-up skeleton in centimetres gives the same skeleton and height");}
 {Variant u;u.names=Names::Unreal;auto p=probeCharacter(writeFile(L"unreal.glb",humanoid(u)),Json::object(),{});
  check(slot(p,"Pelvis")=="pelvis"&&slot(p,"L_Forearm")=="lowerarm_l"&&slot(p,"Spine4")=="spine_03","probe: an Unreal skeleton maps, twist bones aside");}
 {Variant nn;nn.names=Names::Nonsense;auto p=probeCharacter(writeFile(L"numbered.glb",humanoid(nn)),Json::object(),{});
  check(slot(p,"Pelvis")==p["skeleton"]["bones"][0]["name"]&&slot(p,"L_Hand")!=""&&conf(p,"L_Thigh")<.85f&&p["auto"]["humanoid"]==true,"probe: a numbered skeleton is found by its shape, with guesses to check");}
 {Variant v;v.second=1;auto p=probeCharacter(writeFile(L"rebound.glb",humanoid(v)),Json::object(),{});bool rebound=false;for(auto& m:p["meshes"])rebound|=m["name"]=="Hat"&&m["kept"]=="rebound";check(rebound&&p["warnings"].empty(),"probe: an outfit on a copy of the skeleton is rebound by bone names");}
 {Variant v;v.second=2;auto p=probeCharacter(writeFile(L"dropped.glb",humanoid(v)),Json::object(),{});bool dropped=false;for(auto& m:p["meshes"])dropped|=m["name"]=="Hat"&&m["kept"]=="dropped";
  check(dropped&&p["warnings"].size()==1&&p["warnings"][0].get<std::string>().find("Hat")!=std::string::npos,"probe: a mesh on a foreign skeleton is left out, with a warning");}
 {Variant v;v.shiftJis=true;auto p=probeCharacter(writeFile(L"sjis.glb",humanoid(v)),Json::object(),{});int b=boneNamed(p,"左足");check(b>=0&&p["skeleton"]["bones"][b]["nameIssue"]=="cp932","probe: a Shift-JIS bone name is decoded and marked");}
 {Variant v;v.duplicate=true;auto p=probeCharacter(writeFile(L"duplicate.glb",humanoid(v)),Json::object(),{});check(boneNamed(p,"Hair_02 #2")>=0,"probe: duplicate names are numbered");}

 // ---- convert ----
 auto request=autoRequest(probe,hairJiggle());
 auto c=convertCharacter(mixamo,request,{});auto m=parse(c.pmx);
 check(pmxBone(*m,"下半身")==0&&pmxBone(*m,"上半身")>=0&&pmxBone(*m,"上半身2")>=0&&pmxBone(*m,"上半身3")>=0&&pmxBone(*m,"左ひじ")>=0&&pmxBone(*m,"右足首")>=0&&pmxBone(*m,"左人指３")>=0&&pmxBone(*m,"左目")>=0,"convert: assigned bones carry the MMD names the fitter matches");
 check(m->bones.size()==bones.size(),"convert: the PMX has the probe's bones, in order");
 bool sameOrder=true;for(auto& [key,b]:c.conversion["boneMap"].items()){int index=b;auto name=request["boneMap"][key].get<std::string>();sameOrder&=index==(name.empty()?-1:boneNamed(probe,name));}
 check(sameOrder,"convert: conversion.boneMap holds the PMX index of each assigned bone (probe index == PMX index)");
 check(m->bones[pmxBone(*m,"左腕")].position.x()>1&&m->bones[pmxBone(*m,"右腕")].position.x()<-1,"convert: the left arm lies on +X");
 check(m->bones[pmxBone(*m,"左つま先")].position.z()<m->bones[pmxBone(*m,"左足首")].position.z()-.5f,"convert: the toes point to -Z, the MMD front");
 check(m->bones[pmxBone(*m,"頭")].position.y()>m->bones[pmxBone(*m,"下半身")].position.y()&&std::abs(m->minimum.y())<.05f,"convert: the head is above the hips and the feet stand on the floor");
 check(std::abs(m->bones[pmxBone(*m,"頭")].position.y()-1.55f*12.5f)<.1f,"convert: metres become 8 cm PMX units");
 auto winding=[](const Model& model){size_t agree=0,total=0;for(size_t t=0;t+2<model.indices.size();t+=3){auto& a=model.vertices[model.indices[t]];auto& b=model.vertices[model.indices[t+1]];auto& d=model.vertices[model.indices[t+2]];
  auto n=(b.position-a.position).cross(d.position-a.position);if(n.length2()<1e-12f)continue;total++;agree+=n.dot(a.normal+b.normal+d.normal)>0;}return total&&double(agree)/total>.9;};
 check(winding(*m),"convert: triangle winding agrees with the normals");
 int aa=-1;for(size_t i=0;i<m->morphNames.size();i++)if(m->morphNames[i]=="あ")aa=int(i);int raw=-1;for(size_t i=0;i<m->morphNames.size();i++)if(m->morphNames[i]=="vrc.v_aa")raw=int(i);
 check(aa>=0&&raw>aa&&nanoemModelMorphGetType(m->morphs[aa])==NANOEM_MODEL_MORPH_TYPE_GROUP&&c.conversion["morphRenames"]["vrc.v_aa"]=="あ","convert: the vrc.v_aa viseme becomes the MMD group morph あ");
 {nanoem_rsize_t n=0;auto entries=nanoemModelMorphGetAllVertexMorphObjects(m->morphs[raw],&n);bool moved=n==8;
  for(size_t k=0;k<n;k++){auto p=nanoemModelMorphVertexGetPosition(entries[k]);moved&=std::abs(p[1]+.125f)<1e-4f&&std::abs(p[2]+.0625f)<1e-4f&&std::abs(p[0])<1e-5f;}
  check(moved,"convert: morph offsets turn and scale with the mesh");}
 check(c.conversion["generator"]==CharacterConverterGenerator&&c.conversion["format"]=="glb"&&c.conversion["units"]["heightCorrection"]==1.f,"convert: the conversion block records the converter and units");
 // Unassigned bones do not take the fitter's names: a numbered bone of the same loose form as 上半身2 is renamed.
 check(pmxBone(*m,"Hair_01")>=0&&pmxBone(*m,"mixamorig:LeftForeArm_Twist")>=0,"convert: unassigned bones keep their names");
 for(auto [variant,label]:{std::pair{0,"Z-up centimetres"},{1,"mirrored"}}){Variant v;if(variant==0)v.zUpCentimetres=true;else v.mirrored=true;
  auto file=writeFile(variant?L"mirrored.glb":L"zup.glb",humanoid(v));auto p=probeCharacter(file,Json::object(),{});auto cv=convertCharacter(file,autoRequest(p),{});auto mm=parse(cv.pmx);
  check(mm->bones[pmxBone(*mm,"左腕")].position.x()>1&&mm->bones[pmxBone(*mm,"左つま先")].position.z()<mm->bones[pmxBone(*mm,"左足首")].position.z()&&winding(*mm)&&std::abs(mm->bones[pmxBone(*mm,"頭")].position.y()-1.55f*12.5f)<.15f,
   std::string("convert: a ")+label+" export lands upright, left arm +X, facing -Z, outward winding");}
 // ---- the fitter ----
 {auto rig=fitRig(*m,Json::object());check(rig.bodies.size()==18,"fit: the converted character fits the 18-body carrier");}
 // ---- a manual assignment overrides the automatic one ----
 {auto manual=request;manual["boneMap"]["ValveBiped.Bip01_L_Forearm"]="mixamorig:LeftForeArm_Twist";
  fails([&]{convertCharacter(mixamo,manual,{});},"character.bone_map","order","errors: a hand that is not below the forearm chosen for it is refused");
  manual=request;manual["boneMap"]["ValveBiped.Bip01_L_Hand"]="mixamorig:LeftForeArm_Twist";
  for(auto& s:humanoidSlots())if(std::string(s.group)=="left_fingers")manual["boneMap"][s.key]="";
  auto cv=convertCharacter(mixamo,manual,{});auto mm=parse(cv.pmx);
  check(mm->bones[cv.conversion["boneMap"]["ValveBiped.Bip01_L_Hand"].get<int>()].name=="左手首"&&pmxBone(*mm,"mixamorig:LeftHand")>=0,"convert: a hand assigned by hand takes the MMD hand name; the bone it replaced keeps its own");}
 // ---- request errors ----
 auto with=[&](auto change){auto r=request;change(r);return r;};
 fails([&]{convertCharacter(mixamo,with([](Json& r){r["boneMap"]["ValveBiped.Bip01_Tail"]="";}),{});},"character.bone_map","unknown_slot","errors: an unknown body part");
 fails([&]{convertCharacter(mixamo,with([](Json& r){r["boneMap"]["ValveBiped.Bip01_Spine"]="mixamorig:Spine";}),{});},"character.bone_map","locked","errors: the synthesized spine cannot be assigned");
 fails([&]{convertCharacter(mixamo,with([](Json& r){r["boneMap"]["ValveBiped.Bip01_L_Hand"]="NoSuchBone";}),{});},"character.bone_map","missing","errors: a bone that is not in the file");
 fails([&]{convertCharacter(mixamo,with([](Json& r){r["boneMap"]["ValveBiped.Bip01_Head1"]="";}),{});},"character.bone_map","required","errors: a required part without a bone");
 fails([&]{convertCharacter(mixamo,with([](Json& r){r["boneMap"]["ValveBiped.Bip01_R_Hand"]="mixamorig:LeftHand";}),{});},"character.bone_map","duplicate","errors: one bone for two parts");
 fails([&]{convertCharacter(mixamo,with([](Json& r){r["boneMap"]["ValveBiped.Bip01_L_Calf"]="mixamorig:RightLeg";r["boneMap"]["ValveBiped.Bip01_R_Calf"]="";}),{});},"character.bone_map","","errors: a calf under the other thigh");
 fails([&]{convertCharacter(mixamo,with([](Json& r){r["boneMap"]["ValveBiped.Bip01_L_Thigh"]="Hair_01";}),{});},"character.bone_map","leg_on_spine","errors: a thigh hanging from the upper body");
 fails([&]{convertCharacter(mixamo,with([](Json& r){r["jiggle"]["groups"][0]["chains"][0]["root"]="mixamorig:LeftForeArm";}),{});},"character.jiggle","body","errors: a swinging part containing the hand");
 fails([&]{convertCharacter(mixamo,with([](Json& r){for(int k=0;k<70;k++)r["jiggle"]["groups"][0]["chains"].push_back({{"root","Hair_01"},{"enabled",true}});}),{});},"character.jiggle","too_many","errors: more than 64 swinging parts");
 fails([&]{convertCharacter(mixamo,with([](Json& r){r["jiggle"]["groups"][0]["values"]["stiffness"]=9;}),{});},"character.jiggle","range","errors: a stiffness out of range");
 fails([&]{convertCharacter(mixamo,with([](Json& r){r["jiggle"]["groups"][0]["chains"][0]["root"]="NoSuchBone";}),{});},"character.jiggle","missing","errors: a swinging part that is not in the file");
 fails([&]{convertCharacter(mixamo,with([](Json& r){r["requestVersion"]=2;}),{});},"character.request_version","","errors: a request from a newer version");
 {Variant nn;nn.names=Names::Nonsense;auto file=writeFile(L"numbered.glb",humanoid(nn));fails([&]{convertCharacter(file,Json::object(),{});},"character.needs_mapping","","errors: no bone map and unsure guesses need the bone window");}
 {auto legacy=convertCharacter(mixamo,Json::object(),{});check(parse(legacy.pmx)->bones.size()==bones.size()&&!legacy.conversion.contains("springBone"),"legacy: older addon files send no bone map; a certain automatic map imports without swinging parts");}
 fails([&]{convertCharacter(writeFile(L"static.glb",[]{Glb g;g.j["asset"]={{"version","2.0"}};std::vector<float> p{0,0,0,1,0,0,0,1,0};int P=g.floats(p,"VEC3",3);g.j["meshes"]=Json::array({{{"primitives",Json::array({{{"attributes",{{"POSITION",P}}}}})}}});
  g.j["nodes"]=Json::array({{{"mesh",0}}});g.j["scenes"]=Json::array({{{"nodes",{0}}}});return g.file();}()),Json::object(),{});},"character.no_skeleton","","errors: a model without a skeleton");
 // ---- swinging parts ----
 {auto& sb=c.conversion["springBone"];std::set<int> jointBones;for(auto& j:sb["joints"])jointBones.insert(j["bone"].get<int>());
  int h1=pmxBone(*m,"Hair_01"),h2=pmxBone(*m,"Hair_02"),h3=pmxBone(*m,"Hair_03"),end=pmxBone(*m,"Hair_end");
  check(jointBones.contains(h1)&&jointBones.contains(h2)&&jointBones.contains(h3)&&!jointBones.contains(end),"jiggle: each hair bone swings; the unweighted end bone is the last one's tail");
  check(sb["springs"].size()==3&&sb["springs"][1]["name"]=="Skirt_F_01"&&sb["springs"][2]["name"]=="Skirt_B_01","jiggle: a skirt under an unweighted group swings as one part per strand");
  m->springs=SpringSetup::fromManifest(c.conversion,*m);check(m->springs&&m->springs->joints.size()==sb["joints"].size(),"jiggle: the spring setup loads from the conversion block");
  float head=0;for(auto& col:sb["colliders"])if(col["bone"]==pmxBone(*m,"頭"))head=col["radius"];
  check(std::abs(head-.10f*(1.68f/1.6f)*12.5f)<.05f&&sb["colliderGroups"].size()==7,"jiggle: body colliders scale with the character's height, only for the groups springs use");}
 // ---- identity, import and reload ----
 auto cache=temp(L"cache");std::error_code ec;fs::remove_all(cache,ec);
 auto first=importConverted(mixamo,cache,request,{},convertCharacter(mixamo,request,{}));
 auto second=importConverted(mixamo,cache,request,{},convertCharacter(mixamo,request,{}));
 auto stiffer=request;stiffer["jiggle"]=hairJiggle(1.5f);auto third=importConverted(mixamo,cache,stiffer,{},convertCharacter(mixamo,stiffer,{}));
 check(first["asset"]==second["asset"]&&first["asset"]!=third["asset"],"identity: the same file and assignment give the same asset; a swing value changes it");
 check(first["info"]["conversion"]["sourceSha256"]==hash(readFile(mixamo))&&first["info"].contains("conversion")&&!first["info"].contains("vrm"),"identity: the manifest's conversion block names the source file");
 {auto id=first["asset"].get<std::string>();auto loaded=loadAsset(cache,id);
  check(loaded->springs&&loaded->conversionBoneMap.size()==MappedSlotCount&&loaded->conversionBoneMap.at("ValveBiped.Bip01_Pelvis")==0,"reload: springs and the conversion bone map come back with the cached asset");
  auto registry=readJson(cache/L"sources.local.json");check(registry[id]["options"]==request,"reload: the registry keeps the request, so Reload converts the same way");
  // InspectBoneMap: the skeleton for the window and the structural rules on an assignment.
  // (Options are built explicitly: a braced {{"key",value}} argument can become an array.)
  auto options=[](const char* key,Json value){Json o=Json::object();o[key]=std::move(value);return o;};
  auto inspected=inspectBoneMap(*loaded,options("include",Json::array({"skeleton"})));
  check(inspected["auto"]["slots"]["ValveBiped.Bip01_Pelvis"]["bone"]==0&&inspected["skeleton"]["bones"].size()==loaded->bones.size()&&inspected["issues"].empty(),"inspect: the cached model's skeleton and automatic map");
  Json values=Json::object();for(auto& [key,value]:loaded->conversionBoneMap)values[key]=value;
  values["ValveBiped.Bip01_Spine1"]=double(loaded->conversionBoneMap.at("ValveBiped.Bip01_Spine1"));// Lua numbers may arrive as 3.0
  auto clean=inspectBoneMap(*loaded,options("values",values));
  check(!clean.contains("skeleton")&&clean["issues"].empty(),"inspect: the converted map passes the structural rules (integral numbers in any JSON form)");
  values["ValveBiped.Bip01_R_Hand"]=values["ValveBiped.Bip01_L_Hand"];values["ValveBiped.Bip01_L_Foot"]=9999;values["ValveBiped.Bip01_L_Toe0"]=1.5;
  auto checked=inspectBoneMap(*loaded,options("values",values));std::set<std::string> codes;for(auto& i:checked["issues"])codes.insert(i["code"].get<std::string>()+":"+i["slot"].get<std::string>());
  check(codes.contains("duplicate:ValveBiped.Bip01_R_Hand")&&codes.contains("range:ValveBiped.Bip01_L_Foot")&&codes.contains("range:ValveBiped.Bip01_L_Toe0"),"inspect: duplicates and values out of range or not whole are problems");
  bool invalid=false;try{inspectBoneMap(*loaded,options("values",Json::array()));}catch(const std::exception& e){invalid=std::string(e.what())=="Invalid bone map options";}
  check(invalid,"inspect: malformed options are refused");
  // A hand-edited map (with a consistent identity) is checked like any cached index.
  auto manifest=readJson(cache/L"assets"/wide(id)/L"manifest.json");manifest["conversion"]["boneMap"]["ValveBiped.Bip01_L_Hand"]=9999;
  auto identity=manifest;identity["id"]=manifest["sourceHash"];auto text=identity.dump();auto forged=hash(std::span(reinterpret_cast<const unsigned char*>(text.data()),text.size()));manifest["id"]=forged;
  fs::create_directories(cache/L"assets"/wide(forged));fs::copy_file(cache/L"assets"/wide(id)/L"model.bin",cache/L"assets"/wide(forged)/L"model.bin");writeJson(cache/L"assets"/wide(forged)/L"manifest.json",manifest);
  bool refused=false;try{loadAsset(cache,forged);}catch(const std::exception& e){refused=std::string(e.what())=="Cached conversion map is invalid";}
  check(refused,"reload: an out-of-range conversion map is refused");}
 fs::remove_all(cache,ec);
}
}

int main(int argc,char** argv){try{
 if(argc==3&&std::string(argv[1])=="--write-fixtures"){fs::path dir=argv[2];fs::create_directories(dir);
  writeAtomic(dir/L"mixamo.glb",humanoid());Variant ue;ue.names=Names::Unreal;writeAtomic(dir/L"unreal.glb",humanoid(ue));Variant numbered;numbered.names=Names::Nonsense;writeAtomic(dir/L"numbered.glb",humanoid(numbered));
  Variant z;z.zUpCentimetres=true;writeAtomic(dir/L"mixamo-zup-cm.glb",humanoid(z));Variant mirror;mirror.mirrored=true;writeAtomic(dir/L"mixamo-mirrored.glb",humanoid(mirror));std::cout<<"fixtures written to "<<utf8(dir.wstring())<<"\n";return 0;}
 if(argc==3&&std::string(argv[1])=="--write-slots"){auto text=slotsJson().dump(1)+"\n";writeAtomic(argv[2],std::span(reinterpret_cast<const unsigned char*>(text.data()),text.size()));return 0;}
 nameTests();conventionTests();ruleTests();converterTests();
 std::cout<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
