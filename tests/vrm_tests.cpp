// VRM avatars: synthetic VRM 0.x and 1.0 files through the PMX conversion,
// the spring-bone solver against an independent step of the published VRM
// algorithm, character-relative damping, colliders, and a spring-only model
// driven through the secondary pipeline (synchronous and asynchronous).
#include "runtime.hpp"
#include "rig.hpp"
#include "secondary.hpp"
#include "spring_bones.hpp"
#include "vrm.hpp"
#include <nanoem.h>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <iostream>
using namespace mmd;
namespace {
int checks=0;
void check(bool ok,const std::string& name){if(!ok)throw std::runtime_error("FAIL "+name);++checks;std::cout<<"PASS "<<name<<"\n";}

// ---- a small humanoid written as a real GLB ----
struct Glb {
 Json j=Json::object();Bytes bin;
 int view(const void* data,size_t size){while(bin.size()%4)bin.push_back(0);size_t offset=bin.size();auto p=static_cast<const unsigned char*>(data);bin.insert(bin.end(),p,p+size);
  j["bufferViews"].push_back({{"buffer",0},{"byteOffset",offset},{"byteLength",size}});return int(j["bufferViews"].size()-1);}
 int floats(const std::vector<float>& v,const char* type,int components){int bv=view(v.data(),v.size()*4);j["accessors"].push_back({{"bufferView",bv},{"componentType",5126},{"count",v.size()/components},{"type",type}});return int(j["accessors"].size()-1);}
 int shorts(const std::vector<uint16_t>& v){int bv=view(v.data(),v.size()*2);j["accessors"].push_back({{"bufferView",bv},{"componentType",5123},{"count",v.size()/4},{"type","VEC4"}});return int(j["accessors"].size()-1);}
 int indices(const std::vector<uint32_t>& v){int bv=view(v.data(),v.size()*4);j["accessors"].push_back({{"bufferView",bv},{"componentType",5125},{"count",v.size()},{"type","SCALAR"}});return int(j["accessors"].size()-1);}
 Bytes file(){
  while(bin.size()%4)bin.push_back(0);j["buffers"]=Json::array({{{"byteLength",bin.size()}}});auto text=j.dump();while(text.size()%4)text.push_back(' ');
  Bytes out;auto u32=[&](uint32_t v){for(int k=0;k<4;k++)out.push_back(uint8_t(v>>(8*k)));};
  out.insert(out.end(),{'g','l','T','F'});u32(2);u32(uint32_t(12+8+text.size()+8+bin.size()));
  u32(uint32_t(text.size()));u32(0x4E4F534Au);out.insert(out.end(),text.begin(),text.end());u32(uint32_t(bin.size()));u32(0x004E4942u);out.insert(out.end(),bin.begin(),bin.end());return out;
 }
};
struct NodeSpec {const char* name;int parent;btVector3 world;const char* human;};
// VRM 1.0 space: the avatar faces +Z with its left side at +X (metres).
const NodeSpec nodes[]={
 {"Root",-1,{0,0,0},nullptr},{"Hips",0,{0,.9f,0},"hips"},{"Spine",1,{0,1.f,0},"spine"},{"Chest",2,{0,1.15f,0},"chest"},{"Neck",3,{0,1.4f,0},"neck"},{"Head",4,{0,1.5f,0},"head"},
 {"L_Shoulder",3,{.05f,1.35f,0},"leftShoulder"},{"L_UpperArm",6,{.15f,1.35f,0},"leftUpperArm"},{"L_LowerArm",7,{.4f,1.35f,0},"leftLowerArm"},{"L_Hand",8,{.65f,1.35f,0},"leftHand"},
 {"R_Shoulder",3,{-.05f,1.35f,0},"rightShoulder"},{"R_UpperArm",10,{-.15f,1.35f,0},"rightUpperArm"},{"R_LowerArm",11,{-.4f,1.35f,0},"rightLowerArm"},{"R_Hand",12,{-.65f,1.35f,0},"rightHand"},
 {"L_UpperLeg",1,{.1f,.85f,0},"leftUpperLeg"},{"L_LowerLeg",14,{.1f,.5f,0},"leftLowerLeg"},{"L_Foot",15,{.1f,.1f,0},"leftFoot"},{"L_Toes",16,{.1f,.02f,.1f},"leftToes"},
 {"R_UpperLeg",1,{-.1f,.85f,0},"rightUpperLeg"},{"R_LowerLeg",18,{-.1f,.5f,0},"rightLowerLeg"},{"R_Foot",19,{-.1f,.1f,0},"rightFoot"},{"R_Toes",20,{-.1f,.02f,.1f},"rightToes"},
 {"Hair0",5,{0,1.6f,-.1f},nullptr},{"Hair1",22,{0,1.5f,-.12f},nullptr},{"Hair2",23,{0,1.4f,-.14f},nullptr},{"Hair3",24,{0,1.3f,-.16f},nullptr}};
constexpr int BoneNodes=26,MeshNode=26,Head=5,Chest=3,Hair0=22;
Bytes png(){unsigned char px[16]={255,255,255,0, 255,255,255,64, 255,255,255,200, 255,255,255,255};Bytes out;
 stbi_write_png_to_func([](void* c,void* d,int n){auto& b=*static_cast<Bytes*>(c);b.insert(b.end(),static_cast<unsigned char*>(d),static_cast<unsigned char*>(d)+n);},&out,2,2,4,px,8);return out;}
Bytes synthetic(bool v0,const Json& hairTransform=Json()){
 // VRM 0.x avatars were exported turned 180 degrees about +Y (facing -Z).
 auto place=[&](btVector3 p){return v0?btVector3(-p.x(),p.y(),-p.z()):p;};
 Glb g;auto& j=g.j;j["asset"]={{"version","2.0"},{"generator","mmdhl vrm_tests"}};
 for(int i=0;i<BoneNodes;i++){auto& n=nodes[i];auto local=place(n.world)-(n.parent>=0?place(nodes[n.parent].world):btVector3(0,0,0));
  Json node={{"name",n.name},{"translation",{local.x(),local.y(),local.z()}}};Json children=Json::array();for(int c=0;c<BoneNodes;c++)if(nodes[c].parent==i)children.push_back(c);if(!children.empty())node["children"]=children;j["nodes"].push_back(node);}
 j["nodes"].push_back({{"name","Body"},{"mesh",0},{"skin",0}});
 j["scenes"]=Json::array({{{"nodes",{0,MeshNode}}}});j["scene"]=0;
 // One 4 cm box per bone; body boxes and hair boxes are two primitives sharing one vertex buffer.
 std::vector<float> position,normal,uv,weight,morph,ibm;std::vector<uint16_t> joint;std::vector<uint32_t> body,hair;
 for(int b=0;b<BoneNodes;b++){auto c=place(nodes[b].world);uint32_t base=uint32_t(position.size()/3);
  for(int k=0;k<8;k++){btVector3 s((k&1)?1.f:-1.f,(k&2)?1.f:-1.f,(k&4)?1.f:-1.f);auto p=c+s*.02f;auto n=s.normalized();
   position.insert(position.end(),{p.x(),p.y(),p.z()});normal.insert(normal.end(),{n.x(),n.y(),n.z()});uv.insert(uv.end(),{(k&1)?1.f:0.f,(k&2)?1.f:0.f});
   joint.insert(joint.end(),{uint16_t(b),0,0,0});weight.insert(weight.end(),{1,0,0,0});
   auto d=b==Head?place(btVector3(0,-.01f,.005f)):btVector3(0,0,0);morph.insert(morph.end(),{d.x(),d.y(),d.z()});}
  const int faces[6][4]={{0,2,6,4},{1,5,7,3},{0,4,5,1},{2,3,7,6},{0,1,3,2},{4,6,7,5}};
  for(auto& f:faces)for(int t=0;t<2;t++){uint32_t a=f[0],x=f[t+1],y=f[t+2];
   btVector3 pa(position[(base+a)*3],position[(base+a)*3+1],position[(base+a)*3+2]),px(position[(base+x)*3],position[(base+x)*3+1],position[(base+x)*3+2]),py(position[(base+y)*3],position[(base+y)*3+1],position[(base+y)*3+2]);
   btVector3 outward=(pa+px+py)/3-c;if((px-pa).cross(py-pa).dot(outward)<0)std::swap(x,y); // counter-clockwise from outside
   auto& list=b>=Hair0?hair:body;list.insert(list.end(),{base+a,base+x,base+y});}
  float m[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, -c.x(),-c.y(),-c.z(),1};ibm.insert(ibm.end(),m,m+16);}
 int P=g.floats(position,"VEC3",3),N=g.floats(normal,"VEC3",3),T=g.floats(uv,"VEC2",2),J=g.shorts(joint),W=g.floats(weight,"VEC4",4),M=g.floats(morph,"VEC3",3),I=g.floats(ibm,"MAT4",16);
 Json attributes={{"POSITION",P},{"NORMAL",N},{"TEXCOORD_0",T},{"JOINTS_0",J},{"WEIGHTS_0",W}};
 j["meshes"]=Json::array({{{"name","Body"},{"extras",{{"targetNames",{"Fcl_MTH_A"}}}},{"primitives",Json::array({
  {{"attributes",attributes},{"indices",g.indices(body)},{"material",0},{"targets",Json::array({{{"POSITION",M}}})}},
  {{"attributes",attributes},{"indices",g.indices(hair)},{"material",1},{"targets",Json::array({{{"POSITION",M}}})}}})}}});
 Json joints=Json::array();for(int b=0;b<BoneNodes;b++)joints.push_back(b);j["skins"]=Json::array({{{"joints",joints},{"inverseBindMatrices",I},{"skeleton",0}}});
 auto image=png();int iv=g.view(image.data(),image.size());j["images"]=Json::array({{{"bufferView",iv},{"mimeType","image/png"}}});j["textures"]=Json::array({{{"source",0}}});
 j["materials"]=Json::array({{{"name","Body"},{"alphaMode","MASK"},{"alphaCutoff",.5f},{"pbrMetallicRoughness",{{"baseColorTexture",{{"index",0}}},{"baseColorFactor",{1,1,1,1}}}}},
  {{"name","Hair"},{"doubleSided",true},{"pbrMetallicRoughness",{{"baseColorTexture",{{"index",0}}}}}}});
 if(!hairTransform.is_null())j["materials"][1]["pbrMetallicRoughness"]["baseColorTexture"]["extensions"]["KHR_texture_transform"]=hairTransform;
 if(v0){
  Json human=Json::array();for(int b=0;b<BoneNodes;b++)if(nodes[b].human)human.push_back({{"bone",nodes[b].human},{"node",b}});
  // 0.x spring and collider vectors are Unity axes: +Z is the avatar's front.
  j["extensionsUsed"]={"VRM"};
  j["extensions"]["VRM"]={{"specVersion","0.0"},{"meta",{{"title","Synthetic"},{"author","tests"},{"licenseName","CC0"}}},{"humanoid",{{"humanBones",human}}},
   {"blendShapeMaster",{{"blendShapeGroups",Json::array({{{"name","A"},{"presetName","a"},{"binds",{{{"mesh",0},{"index",0},{"weight",100}}}}},
     {{"name","Blink"},{"presetName","blink"},{"materialValues",{{{"materialName","Body"},{"propertyName","_Color"},{"targetValue",{1,0,0,1}}}}}},
     {{"name","Shift"},{"presetName","unknown"},{"materialValues",{{{"materialName","Hair"},{"propertyName","_MainTex_ST"},{"targetValue",{1,1,.5f,0}}}}}}})}}},
   {"secondaryAnimation",{{"boneGroups",Json::array({{{"comment","hair"},{"stiffiness",1},{"gravityPower",.5f},{"gravityDir",{{"x",0},{"y",-1},{"z",0}}},{"dragForce",.4f},{"center",-1},{"hitRadius",.02f},{"bones",{Hair0}},{"colliderGroups",{0}}}})},
     {"colliderGroups",Json::array({{{"node",Head},{"colliders",Json::array({{{"offset",{{"x",0},{"y",0},{"z",.1f}}},{"radius",.08f}}})}}})}}},
   {"materialProperties",Json::array({{{"name","Body"},{"shader","VRM/MToon"},{"renderQueue",2450},{"floatProperties",{{"_BlendMode",1},{"_Cutoff",.5f},{"_CullMode",2}}},{"vectorProperties",{{"_Color",{1,1,1,1}}}},{"textureProperties",{{"_MainTex",0}}}},
    {{"name","Hair"},{"shader","VRM/MToon"},{"renderQueue",2000},{"floatProperties",{{"_BlendMode",0},{"_CullMode",0}}},{"vectorProperties",{{"_Color",{1,1,1,1}}}},{"textureProperties",{{"_MainTex",0}}}}})}};
 }else{
  Json human=Json::object();for(int b=0;b<BoneNodes;b++)if(nodes[b].human)human[nodes[b].human]={{"node",b}};
  j["extensionsUsed"]={"VRMC_vrm","VRMC_springBone","VRMC_materials_mtoon"};
  j["materials"][0]["extensions"]["VRMC_materials_mtoon"]={{"specVersion","1.0"},{"shadeColorFactor",{.5f,.5f,.5f}}};
  j["extensions"]["VRMC_vrm"]={{"specVersion","1.0"},{"meta",{{"name","Synthetic"},{"version","1"},{"authors",{"tests"}},{"licenseUrl","https://vrm.dev/licenses/1.0/"},{"allowRedistribution",true}}},{"humanoid",{{"humanBones",human}}},
   {"expressions",{{"preset",{{"aa",{{"morphTargetBinds",{{{"node",MeshNode},{"index",0},{"weight",1}}}}}},{"blink",{{"materialColorBinds",{{{"material",0},{"type","color"},{"targetValue",{1,0,0,1}}}}}}}}},
    {"custom",{{"Shift",{{"textureTransformBinds",{{{"material",1},{"scale",{1,1}},{"offset",{.5f,0}}}}}}}}}}}};
  Json chain=Json::array();for(int b=Hair0;b<BoneNodes;b++){Json k={{"node",b}};if(b+1<BoneNodes)k.update({{"hitRadius",.02f},{"stiffness",1},{"gravityPower",.5f},{"gravityDir",{0,-1,0}},{"dragForce",.4f}});chain.push_back(k);}
  Json sphere={{"node",Head},{"shape",{{"sphere",{{"offset",{0,0,.1f}},{"radius",.08f}}}}}};
  Json capsule={{"node",Chest},{"shape",{{"capsule",{{"offset",{0,0,0}},{"radius",.1f},{"tail",{0,.2f,0}}}}}}};
  Json group={{"colliders",{0,1}}},spring={{"name","hair"},{"joints",chain},{"colliderGroups",{0}}};
  j["extensions"]["VRMC_springBone"]={{"specVersion","1.0"},{"colliders",Json::array({sphere,capsule})},{"colliderGroups",Json::array({group})},{"springs",Json::array({spring})}};
 }
 return g.file();
}
int bone(const Model& m,const std::string& name){for(size_t i=0;i<m.bones.size();i++)if(m.bones[i].name==name)return int(i);throw std::runtime_error("missing bone "+name);}
int morph(const Model& m,const std::string& name){for(size_t i=0;i<m.morphNames.size();i++)if(m.morphNames[i]==name)return int(i);return -1;}

// ---- an independent spring step, written from the VRMC_springBone text ----
struct Reference {
 const Model& m;const SpringSetup& s;std::vector<btVector3> current,previous;std::vector<btTransform> world;
 Reference(const Model& model,const SpringSetup& setup,const std::vector<btTransform>& skin):m(model),s(setup){
  world.resize(m.bones.size());forward(skin);
  for(auto& j:s.joints){auto tail=world[j.bone].getOrigin()+quatRotate(world[m.bones[j.bone].parent].getRotation(),j.axis)*j.length;current.push_back(tail);previous.push_back(tail);}
 }
 void forward(const std::vector<btTransform>& skin){for(auto i:m.order)world[i]=skin[i]*btTransform(btQuaternion::getIdentity(),m.bones[i].position);}
 void step(float dt,const std::vector<btTransform>& skin){
  forward(skin);
  for(size_t k=0;k<s.joints.size();k++){auto& j=s.joints[k];int parent=m.bones[j.bone].parent;
   // world position of the joint from the (already updated) parent
   auto parentWorld=world[parent];auto position=parentWorld*(m.bones[j.bone].position-m.bones[parent].position);
   auto axis=quatRotate(parentWorld.getRotation(),j.axis);
   auto next=current[k]+(current[k]-previous[k])*(1-j.dragForce)+axis*(j.stiffness*dt*s.unitsPerMeter)+j.gravityDir*(j.gravityPower*dt*s.unitsPerMeter);
   next=position+(next-position).normalized()*j.length;
   for(int c:s.springs[j.spring].colliders){auto& col=s.colliders[c];auto centre=world[col.bone]*col.offset;auto d=next-centre;float r=col.radius+j.hitRadius;
    if(!col.capsule&&d.length()<r){next=centre+d.normalized()*r;next=position+(next-position).normalized()*j.length;}}
   previous[k]=current[k];current[k]=next;
   auto direction=next-position;auto rotation=shortestArcQuatNormalize2(axis,direction)*parentWorld.getRotation();world[j.bone]=btTransform(rotation.normalized(),position);
   // bones below this joint follow it rigidly until their own joint is reached
   for(auto i:m.order){int p=m.bones[i].parent;if(p==j.bone&&s.jointOfBone[i]<0)world[i]=world[p]*btTransform(btQuaternion::getIdentity(),m.bones[i].position-m.bones[p].position);}
  }
 }
};
std::vector<btTransform> rigid(size_t n,const btTransform& t){return std::vector<btTransform>(n,t);}
float degrees(const btQuaternion& q){return q.getAngleShortestPath()*SIMD_DEGS_PER_RAD;}
}

int main(){try{
 // ---- conversion, both VRM versions ----
 std::shared_ptr<Model> latest;VrmConversion latestConversion;
 for(bool v0:{true,false}){
  std::string tag=v0?"VRM 0.x: ":"VRM 1.0: ";auto file=synthetic(v0);
  check(isVrmData(file),tag+"the GLB is recognised as a VRM avatar");
  auto c=convertVrm(file,"synthetic");auto m=parse(c.pmx);
  check(m->bones.size()==BoneNodes,tag+"every skeleton node becomes a bone and the mesh-only node does not");
  auto head=bone(*m,"頭"),arm=bone(*m,"左腕"),ankle=bone(*m,"左足首"),toe=bone(*m,"左つま先");
  check(bone(*m,"下半身")>=0&&bone(*m,"上半身2")>=0&&bone(*m,"右ひじ")>=0,tag+"humanoid bones carry the MMD names the fitter matches");
  check(std::abs(m->bones[head].position.y()-1.5f*VrmUnitsPerMeter)<1e-4f,tag+"metres become 8 cm PMX units");
  check(m->bones[arm].position.x()>1.f,tag+"the left arm lies on +X like an MMD model");
  check(m->bones[toe].position.z()<m->bones[ankle].position.z()-.5f,tag+"the toes point to -Z: the avatar faces the MMD front");
  size_t agree=0,total=0;for(size_t t=0;t+2<m->indices.size();t+=3){auto& a=m->vertices[m->indices[t]];auto& b=m->vertices[m->indices[t+1]];auto& d=m->vertices[m->indices[t+2]];
   auto n=(b.position-a.position).cross(d.position-a.position);if(n.length2()<1e-12f)continue;total++;agree+=n.dot(a.normal+b.normal+d.normal)>0;}
  check(total>0&&agree==total,tag+"triangle winding agrees with every vertex normal after the mirror");
  check(m->vertices.size()==BoneNodes*8,tag+"two primitives sharing one vertex buffer keep one copy of each vertex");
  int hair2=bone(*m,"Hair2");bool bound=false;for(auto& v:m->vertices)if(v.bones[0]==hair2&&v.weights[0]>.99f)bound=true;
  check(bound,tag+"skin weights follow the joint nodes");
  int aa=morph(*m,"あ"),raw=morph(*m,"Fcl_MTH_A");
  check(aa>=0&&raw>aa&&nanoemModelMorphGetType(m->morphs[aa])==NANOEM_MODEL_MORPH_TYPE_GROUP,tag+"the aa expression becomes the MMD group morph あ, listed before raw targets");
  {nanoem_rsize_t n=0;auto entries=nanoemModelMorphGetAllVertexMorphObjects(m->morphs[raw],&n);bool mirrored=n==8;
   for(size_t k=0;k<n;k++){auto p=nanoemModelMorphVertexGetPosition(entries[k]);mirrored&=std::abs(p[1]+.125f)<1e-4f&&std::abs(p[2]+.0625f)<1e-4f&&std::abs(p[0])<1e-5f;}
   check(mirrored,tag+"morph offsets are mirrored and scaled like the mesh");}
  int blink=morph(*m,"まばたき");check(blink>=0&&morph(*m,"まばたき (colour)")>=0,tag+"a colour expression becomes a group plus a material morph");
  check(morph(*m,"Shift (texture)")>=0,tag+"a texture-offset expression becomes a UV morph");
  // Material alpha semantics are baked into the textures the renderer classifies.
  bool cutout=false,opaque=false;
  for(auto& [path,bytes]:c.textures){int w,h,ch;auto* px=stbi_load_from_memory(bytes.data(),int(bytes.size()),&w,&h,&ch,4);if(!px)continue;std::set<int> alpha;for(int k=0;k<w*h;k++)alpha.insert(px[k*4+3]);stbi_image_free(px);
   if(path.find("_cutout128")!=std::string::npos)cutout=alpha==std::set<int>{0,255};if(path.find("_opaque")!=std::string::npos)opaque=alpha==std::set<int>{255};}
  check(cutout,tag+"a MASK material gets its alpha cut at alphaCutoff");check(opaque,tag+"an OPAQUE material ignores texture alpha");
  check(m->materials[0].name=="Hair"&&m->materials[0].twoSided&&m->materials[1].name=="Body",tag+"materials follow the render queue and keep double-sidedness");
  auto& sb=c.vrm["springBone"];
  check(sb["joints"].size()==(v0?4u:3u),tag+(v0?"0.x simulates every node below a root, the leaf with a 7 cm tail":"1.0 simulates each joint up to the last, which is only a tail"));
  auto offset=sb["colliders"][0]["offset"];check(offset[2].get<float>()<-1.2f&&std::abs(offset[0].get<float>())<1e-5f,tag+"a collider offset toward the face stays in front of the head");
  check(std::abs(sb["joints"][0]["hitRadius"].get<float>()-.25f)<1e-5f,tag+"hit radii convert to PMX units");
  check(c.vrm["meta"]["allowRedistribution"]==true&&c.vrm["meta"]["title"]=="Synthetic",tag+"licence metadata is kept");
  check(c.vrm["humanoid"]["hips"]==bone(*m,"下半身"),tag+"the humanoid map points at PMX bones");
  if(!v0){latest=m;latestConversion=c;check(sb["colliders"][1]["shape"]=="capsule",tag+"capsule colliders survive");}
 }
 auto& model=*latest;model.springs=SpringSetup::fromManifest(latestConversion.vrm,model);
 // KHR_texture_transform on the hair: scale (2, 0.5), a quarter turn, offset (0.1, 0.2).
 // As the Khronos Sample Renderer and three.js compute it, (u, v) becomes
 // (0.5v + 0.1, -2u + 0.2); the transposed rotation would give (-0.5v + 0.1, 2u + 0.2).
 {auto c=convertVrm(synthetic(false,{{"offset",{.1f,.2f}},{"rotation",float(SIMD_HALF_PI)},{"scale",{2.f,.5f}}}),"synthetic");auto m=parse(c.pmx);
  int first=bone(*m,"Hair0"),last=bone(*m,"Hair3");size_t hairVertices=0;bool follows=true;std::set<int> vs;
  for(auto& v:m->vertices){int b=v.bones[0];if(b<first||b>last)continue;hairVertices++;
   float v0=v.position.y()>m->bones[size_t(b)].position.y()?1.f:0.f; // the box corner's texture v
   follows&=std::abs(v.uv[0]-(.5f*v0+.1f))<1e-5f;vs.insert(int(std::lround(v.uv[1]*10)));}
  check(hairVertices==32&&follows&&vs==std::set<int>{-18,2},"a texture transform rotates UVs as the Khronos Sample Renderer and three.js do");}
 check(model.springs&&model.springs->joints.size()==3&&model.springs->affected.size()==4,"spring setup: three joints and the tail bone move");
 auto setup=model.springs;size_t n=model.bones.size();const float dt=1.f/60;
 int hips=bone(model,"下半身"),headBone=bone(model,"頭");
 auto rest=rigid(n,btTransform::getIdentity());

 // ---- solver ----
 {SpringSystem s(model,setup,std::vector<uint8_t>(n,0),-1);s.reset(rest);auto before=s.tails(rest);
  // gravity 0.5 bends the resting chain a little; without gravity it keeps its shape
  auto still=std::make_shared<SpringSetup>(*setup);for(auto& j:still->joints)j.gravityPower=0;SpringSystem calm(model,still,std::vector<uint8_t>(n,0),-1);calm.reset(rest);
  for(int k=0;k<600;k++)calm.step(dt,rest,1,1,nullptr);float drift=0;auto after=calm.tails(rest);auto first=calm.tails(rest);
  for(size_t k=0;k<after.size();k++)drift=std::max(drift,(after[k]-before[k]).length());
  check(drift<1e-4f,"a resting chain without gravity stays at its rest shape");
  for(int k=0;k<600;k++)s.step(dt,rest,1,1,nullptr);auto sag=s.tails(rest);check(sag.back().y()<before.back().y()-1e-3f,"gravity power pulls the chain down");}
 {// the solver against the independent step: a head that sways and turns, relative damping off
  // (collisions are covered separately; the reference handles spheres only)
  auto plain=std::make_shared<SpringSetup>(*setup);for(auto& sp:plain->springs)sp.colliders.clear();
  setSpringRelativeDamping(false);SpringSystem s(model,plain,std::vector<uint8_t>(n,0),hips);s.reset(rest);Reference r(model,*plain,rest);
  float worst=0,lengthError=0;
  for(int k=1;k<=300;k++){float t=k*dt;btTransform m(btQuaternion(btVector3(0,1,0),std::sin(t*3)*.8f),btVector3(std::sin(t*2)*4,std::cos(t*1.5f)*2,0));
   auto skin=rigid(n,m);s.step(dt,skin,1,1,nullptr);r.step(dt,skin);auto tails=s.tails(skin);
   for(size_t j=0;j<tails.size();j++){worst=std::max(worst,(tails[j]-r.current[j]).length());auto head=r.world[setup->joints[j].bone].getOrigin();lengthError=std::max(lengthError,std::abs((tails[j]-head).length()-setup->joints[j].length));}}
  std::cout<<"reference difference "<<worst<<" PMX units, length error "<<lengthError<<"\n";
  check(worst<1e-3f,"the solver follows the published VRM spring step");check(lengthError<1e-3f,"every joint keeps its length");}
 {// colliders: a sphere placed on the chain's rest path pushes the tails out
  auto blocked=std::make_shared<SpringSetup>(*setup);auto tail=model.bones[setup->joints[0].tail].position;
  blocked->colliders={{headBone,false,tail-model.bones[headBone].position+btVector3(.2f,0,0),btVector3(0,0,0),.6f}};blocked->springs[0].colliders={0};
  // The VRM step pushes a tail out and then restores the bone length, which may
  // leave it slightly inside; the independent step must agree exactly.
  setSpringRelativeDamping(false);SpringSystem s(model,blocked,std::vector<uint8_t>(n,0),-1);s.reset(rest);Reference r(model,*blocked,rest);float worst=1e9f,difference=0,first=0;
  auto centre=blocked->colliders[0].offset+model.bones[headBone].position;float clearance=blocked->colliders[0].radius+blocked->joints[0].hitRadius;
  first=(model.bones[setup->joints[0].tail].position-centre).length()-clearance;
  for(int k=0;k<240;k++){s.step(dt,rest,1,1,nullptr);r.step(dt,rest);auto tails=s.tails(rest);worst=std::min(worst,(tails[0]-centre).length()-clearance);for(size_t j=0;j<tails.size();j++)difference=std::max(difference,(tails[j]-r.current[j]).length());}
  std::cout<<"collider: rest tail "<<first<<" inside, worst after push "<<worst<<", reference difference "<<difference<<"\n";
  check(s.colliderHits>0&&difference<1e-3f,"sphere colliders push the tails like the published step");
  check(worst>first*.25f,"collisions move a buried tail most of the way out");setSpringRelativeDamping(true);}
 {// world contacts: the head sits just above a floor the rest shape would reach through
  float floor=model.bones[setup->joints[0].bone].position.y()-1.f;
  SpringSystem::WorldContact ground=[&](const btVector3& outside,const btVector3& head,btVector3& tail,float radius,float length){return slideTail(outside,head,tail,btVector3(0,floor,0),btVector3(0,1,0),radius,length);};
  auto still=std::make_shared<SpringSetup>(*setup);for(auto& j:still->joints)j.gravityPower=1;SpringSystem s(model,still,std::vector<uint8_t>(n,0),-1);s.reset(rest);float lowest=1e9f,lengthError=0;
  for(int k=0;k<240;k++){s.step(dt,rest,1,1,&ground);auto tails=s.tails(rest);
   for(size_t j=0;j<tails.size();j++){lowest=std::min(lowest,tails[j].y()-floor-still->joints[j].hitRadius);}
   auto first=tails[0]-model.bones[still->joints[0].bone].position;lengthError=std::max(lengthError,std::abs(first.length()-still->joints[0].length));}
  std::cout<<"floor contact: lowest tail "<<lowest<<" above the hit radius, root length error "<<lengthError<<"\n";
  check(lowest>-1e-3f&&s.worldHits>0,"world contacts keep every tail on the surface");check(lengthError<1e-3f,"a strand on the floor keeps its length by sliding");
  // A tail that starts under the surface returns to the head side.
  btVector3 head(0,1,0),tail(0,-2,0);bool moved=slideTail(head,head,tail,btVector3(0,0,0),btVector3(0,-1,0),.1f,3);
  check(moved&&std::abs(tail.y()-.1f)<1e-4f&&std::abs((tail-head).length()-3)<1e-4f,"a buried tail comes back above the surface, whichever way the surface normal faces");
  // A scalp sunk into the floor (ragdoll hulls are a little smaller than the mesh): the
  // side is taken from a point outside, and the strand still rises onto the floor.
  btVector3 embedded(0,-.5f,0),buried(0,-3,0);moved=slideTail(btVector3(0,5,0),embedded,buried,btVector3(0,0,0),btVector3(0,1,0),.1f,3);
  check(moved&&std::abs(buried.y()-.1f)<1e-4f&&std::abs((buried-embedded).length()-3)<1e-4f,"a strand rooted under the floor still ends on top of it");}
 auto deflection=[&](bool relative,int centre,std::vector<uint8_t> moving){
  setSpringRelativeDamping(relative);auto data=std::make_shared<SpringSetup>(*setup);for(auto& j:data->joints)j.gravityPower=0;data->springs[0].center=centre;
  SpringSystem s(model,data,std::move(moving),hips);s.reset(rest);btTransform m=btTransform::getIdentity();
  // 2 m/s along +X for three seconds, like a walking NPC
  for(int k=1;k<=180;k++){m.setOrigin(btVector3(2*VrmUnitsPerMeter*k*dt,0,0));s.step(dt,rigid(n,m),1,1,nullptr);}
  auto tails=s.tails(rigid(n,m));auto& j=data->joints[0];auto head=m*model.bones[j.bone].position;
  return float(std::acos(std::clamp((tails[0]-head).normalized().dot(j.axis),-1.f,1.f))*SIMD_DEGS_PER_RAD);
 };
 float world=deflection(false,-1,std::vector<uint8_t>(n,0)),relative=deflection(true,-1,std::vector<uint8_t>(n,0));
 std::vector<uint8_t> moving(n,0);moving[hips]=1;float centred=deflection(false,hips,moving);
 std::cout<<"steady 2 m/s walk: world damping "<<world<<" deg, relative "<<relative<<" deg, hips centre "<<centred<<" deg\n";
 check(world>15,"plain VRM damping drags the chain behind steady motion");
 check(relative<1,"damping relative to the hips keeps the rest shape at steady speed");
 check(centred<1,"a spring centred on a moving bone ignores that bone's motion");
 setSpringRelativeDamping(true);

 // ---- the secondary pipeline with a spring-only model ----
 for(const char* backend:{"reference","cpu_mt_v2"}){
  std::string tag=std::string(backend)+": ";
  World host;auto id=host.create(latest,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",0},{"secondaryBackend",backend}});auto& p=host.get(id);
  check(p.secondary->hasSprings()&&p.secondary->diagnostics(false)["springBones"]["joints"]==3,tag+"the instance simulates the VRM springs");
  int hair=bone(model,"Hair0");auto relative=[&]{return p.global[headBone].getRotation().inverse()*p.global[hair].getRotation();};btQuaternion restQ=btQuaternion::getIdentity();
  auto relativeTurn=[&]{return degrees(restQ.inverse()*relative());};
  auto frame=[&](int f,float yaw){btTransform drive(btQuaternion(btVector3(0,0,1),yaw),btVector3(0,0,0));std::vector<btTransform> pose;for(auto& b:p.sourceRig->bones)pose.push_back(drive*b.rest);
   p.submitPresentationPose(pose,f/60.,uint64_t(f)+1);p.secondary->waitAsyncIdle();p.stepSource();};
  int f=0;float quiet=0;for(;f<90;f++){frame(f,0);if(f==60)restQ=relative();if(f>60)quiet=std::max(quiet,relativeTurn());}
  float lag=0;for(int k=0;k<=15;k++,f++){frame(f,float(SIMD_HALF_PI)*k/15);lag=std::max(lag,relativeTurn());}
  float settled=0;for(int k=0;k<180;k++,f++){frame(f,float(SIMD_HALF_PI));if(k>150)settled=std::max(settled,relativeTurn());}
  std::cout<<tag<<"hair turn relative to head: resting "<<quiet<<" deg, during a quarter turn "<<lag<<" deg, settled "<<settled<<" deg\n";
  check(quiet<2,tag+"a standing avatar keeps its hair at rest");check(lag>3,tag+"turning swings the hair behind the head");check(settled<2,tag+"the hair settles back");
  check(p.sourceError.empty()&&p.secondary->asyncError.empty(),tag+"no simulation errors");
  Secondary::setTuning(0,1,1,false,1);frame(f++,float(SIMD_HALF_PI));check(p.secondary->qualityInfo()["mode"]=="springs",tag+"the jiggle level keeps spring bones");
  Secondary::setTuning(-1,1,1,false,1);for(int k=0;k<3;k++)frame(f++,float(SIMD_HALF_PI)+.5f);
  check(degrees(relative())<.01f,tag+"physics off holds spring bones at their animated pose");
  Secondary::setTuning(10,1,1,false,1);
 }
 std::cout<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
