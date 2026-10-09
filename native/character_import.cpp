// FBX, glTF and DAE characters to PMX 2.0, mirroring vrm.cpp: the mesh is baked
// into the pose the file displays (node world x bone offset), turned into the
// character frame the bone assignment defines (left arm at +X, facing the MMD
// front), scaled to 8 cm units and written with the shared PMX writer. Bones
// keep their order (the probe's bone i is PMX bone i); assigned ones take the
// MMD names the fitter matches. Swinging parts become springBone data for
// spring_bones.cpp, with colliders placed on the assigned body.
#include "character_import.hpp"
#include "humanoid_map.hpp"
#include "import_error.hpp"
#include "pmx_writer.hpp"
#include "texture_resolver.hpp"
#include <assimp/Importer.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/ProgressHandler.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/commonMetaData.h>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/glm.hpp>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <set>
namespace mmd {
namespace {
using Vec2=glm::vec2;using Vec3=glm::vec3;using Vec4=glm::vec4;using Mat3=glm::mat3;using Mat4=glm::mat4;
constexpr float UnitsPerMeter=12.5f;  // PMX characters use 8 cm units, as VRM avatars do
constexpr size_t MaxBones=4096,MaxTriangles=4'000'000,MaxPoints=3000,MaxMaterials=128;
constexpr size_t MaxRequestBytes=64*1024;
std::string lowerAscii(std::string s){for(auto& c:s)c=char(std::tolower((unsigned char)c));return s;}
std::string formatOf(const fs::path& p){auto e=lowerAscii(utf8(p.extension().wstring()));return e.size()>1?e.substr(1):e;}
std::string formatLabel(const std::string& f){return f=="fbx"?"FBX":f=="glb"?"GLB":f=="gltf"?"glTF":f=="dae"?"DAE":f;}
bool networkReference(std::string_view s){return s.starts_with("\\\\")||s.starts_with("//")||s.find("://")!=std::string_view::npos;}
Mat4 toGlm(const aiMatrix4x4& a){Mat4 m;for(int r=0;r<4;r++)for(int c=0;c<4;c++)m[c][r]=a[r][c];return m;}
Vec3 v3(const aiVector3D& v){return {v.x,v.y,v.z};}
Json xyz(Vec3 v){return Json::array({v.x,v.y,v.z});}
float round4(float x){return std::round(x*1e4f)/1e4f;}

// ---- Assimp input: the file and its dependencies, never a network path ----
class MemoryStream final:public Assimp::IOStream {
 Bytes bytes;size_t pos=0;
public:
 explicit MemoryStream(Bytes b):bytes(std::move(b)){}
 size_t Read(void* out,size_t size,size_t count)override{if(!size)return 0;count=std::min(count,(bytes.size()-pos)/size);std::memcpy(out,bytes.data()+pos,count*size);pos+=count*size;return count;}
 size_t Write(const void*,size_t,size_t)override{return 0;}
 aiReturn Seek(size_t offset,aiOrigin origin)override{size_t next=offset;if(origin==aiOrigin_CUR){if(offset>bytes.size()-pos)return aiReturn_FAILURE;next=pos+offset;}if(origin==aiOrigin_END){if(offset>bytes.size())return aiReturn_FAILURE;next=bytes.size()-offset;}if(next>bytes.size())return aiReturn_FAILURE;pos=next;return aiReturn_SUCCESS;}
 size_t Tell()const override{return pos;}size_t FileSize()const override{return bytes.size();}void Flush()override{}
};
class LocalIO final:public Assimp::IOSystem {
 fs::path root;
 fs::path path(const char* p)const{std::string s(p);if(networkReference(s))throw std::runtime_error("Network model dependencies are not supported");auto q=fs::path(wide(s));return q.is_absolute()?q:root/q;}
public:
 explicit LocalIO(fs::path p):root(std::move(p)){}
 bool Exists(const char* p)const override{try{return fs::is_regular_file(ioPath(path(p)));}catch(...){return false;}}
 char getOsSeparator()const override{return '/';}
 Assimp::IOStream* Open(const char* p,const char* mode="rb")override{try{if(std::strchr(mode,'w')||std::strchr(mode,'a'))return nullptr;return new MemoryStream(readFile(path(p)));}catch(...){return nullptr;}}
 void Close(Assimp::IOStream* s)override{delete s;}
};
class Progress final:public Assimp::ProgressHandler {
 const CharacterProgress& report;const char* stage;float from,to,last=-1;
public:
 Progress(const CharacterProgress& r,const char* s,float a,float b):report(r),stage(s),from(a),to(b){}
 bool Update(float percent)override{if(report&&percent>=0&&percent-last>=.05f){last=percent;report(stage,from+(to-from)*std::clamp(percent,0.f,1.f));}return true;}
};

// Separates a repeated bone name from its occurrence number (never in a real name).
constexpr char RepeatMark='';
std::pair<std::string,int> boneName(const aiBone* b){std::string n=b->mName.C_Str();auto at=n.find(RepeatMark);if(at==std::string::npos)return {n,1};return {n.substr(0,at),std::atoi(n.c_str()+at+1)};}
struct Loaded {
 std::unique_ptr<Assimp::Importer> importer;const aiScene* scene=nullptr;
 std::string format,generator,up="+y";float metersPerUnit=1;Mat4 basis{1.f};bool axisMetadata=false;
};
Loaded load(const fs::path& source,bool probe,const CharacterProgress& report,const char* stage,float from,float to){
 Loaded l;l.format=formatOf(source);auto label=formatLabel(l.format);
 l.importer=std::make_unique<Assimp::Importer>();auto& imp=*l.importer;
 imp.SetIOHandler(new LocalIO(source.parent_path()));imp.SetProgressHandler(new Progress(report,stage,from,to));
 // Pivots off: they insert $AssimpFbx$ helper nodes into bone chains. The axis
 // metadata is applied once, here, as for static props.
 imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS,false);imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS,false);
 imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_IGNORE_UP_DIRECTION,true);imp.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS,4);
 if(probe)imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_TEXTURES,false);
 auto scene=imp.ReadFile(utf8(source.wstring()),0);
 if(!scene||!scene->mRootNode)importFail("character.parse","This "+label+" file could not be read: "+imp.GetErrorString(),{{"format",l.format}});
 // A bone listed twice in one mesh fails Assimp's validation (the FBX importer can
 // list one bone object twice): keep each object once. Two bones of one name (glTF
 // allows repeated node names) are told apart by occurrence, which collect() reads.
 for(unsigned i=0;i<scene->mNumMeshes;++i){auto mesh=scene->mMeshes[i];if(!mesh||!mesh->mBones)continue;std::set<const aiBone*> listed;std::map<std::string,int> seen;unsigned kept=0;
  for(unsigned b=0;b<mesh->mNumBones;++b){auto bone=mesh->mBones[b];if(!bone||!listed.insert(bone).second)continue;mesh->mBones[kept++]=bone;
   std::string name=bone->mName.C_Str();int k=++seen[name];if(k>1)bone->mName.Set(name+RepeatMark+std::to_string(k));}
  mesh->mNumBones=kept;}
 unsigned flags=probe?(aiProcess_ValidateDataStructure|aiProcess_LimitBoneWeights|aiProcess_PopulateArmatureData)
  :(aiProcess_Triangulate|aiProcess_JoinIdenticalVertices|aiProcess_GenSmoothNormals|aiProcess_LimitBoneWeights|aiProcess_ValidateDataStructure|aiProcess_SortByPType|aiProcess_PopulateArmatureData);
 scene=imp.ApplyPostProcessing(flags);
 if(!scene||!scene->mRootNode)importFail("character.parse","This "+label+" file could not be read: "+imp.GetErrorString(),{{"format",l.format}});
 l.scene=scene;
 if(scene->mMetaData){aiString generator;if(scene->mMetaData->Get(AI_METADATA_SOURCE_GENERATOR,generator))l.generator=sanitizeBoneName(generator.C_Str(),nullptr);}
 // FBX: UnitScaleFactor is centimetres per unit; UpAxis/FrontAxis/CoordAxis
 // name the file's axes. glTF is metres, +Y up; Assimp applies DAE's <unit> and <up_axis>.
 if(l.format=="fbx"&&scene->mMetaData){
  auto numeric=[&](const char* key,double fallback){double d;float f;int32_t i;if(scene->mMetaData->Get(key,d))return d;if(scene->mMetaData->Get(key,f))return double(f);if(scene->mMetaData->Get(key,i))return double(i);return fallback;};
  l.axisMetadata=scene->mMetaData->HasKey("UpAxis");
  int up=int(numeric("UpAxis",1)),front=int(numeric("FrontAxis",2)),right=int(numeric("CoordAxis",0));
  if(up<0||up>2||front<0||front>2||right<0||right>2||up==front||up==right||front==right)importFail("character.parse","This FBX file could not be read: its axis metadata is invalid",{{"format","fbx"}});
  Vec3 u(0),f(0),r(0);u[up]=float(numeric("UpAxisSign",1)<0?-1:1);f[front]=float(numeric("FrontAxisSign",1)<0?-1:1);r[right]=float(numeric("CoordAxisSign",1)<0?-1:1);
  for(int c=0;c<3;c++){l.basis[c][0]=r[c];l.basis[c][1]=u[c];l.basis[c][2]=f[c];}
  double scale=numeric("UnitScaleFactor",1);if(!std::isfinite(scale)||scale<=0)importFail("character.parse","This FBX file could not be read: its unit scale is invalid",{{"format","fbx"}});
  l.metersPerUnit=float(scale/100);
 }
 return l;
}

// ---- skeleton: the bones the probe lists and the converter writes, in one order ----
struct MeshUse {
 const aiNode* node=nullptr;const aiMesh* mesh=nullptr;std::string name,kept="yes";
 std::vector<int> target;std::vector<Mat4> skin;int rigid=-1;
 std::vector<std::vector<std::pair<int,float>>> influences;  // per vertex: (mesh bone, weight)
};
struct Collected {
 std::vector<const aiNode*> nodes;std::vector<int> parent;std::map<const aiNode*,int> index;std::vector<Mat4> world;
 std::vector<std::string> raw,names,issues;
 std::map<const aiNode*,Mat4> nodeWorld;
 struct Armature {const aiNode* node=nullptr;std::string name;std::set<const aiNode*> bones;uint64_t weighted=0;};
 std::vector<Armature> armatures;int chosen=-1;
 std::vector<MeshUse> meshes;size_t bones=0;uint64_t triangles=0,vertices=0;
 std::vector<std::string> rebound,dropped;
};
bool foldedNode(const aiNode* n){return std::string_view(n->mName.C_Str()).find("$AssimpFbx$")!=std::string_view::npos;}
Collected collect(const Loaded& l,bool probe){
 Collected c;const aiScene* s=l.scene;Mat4 frame=Mat4(l.metersPerUnit)*l.basis;frame[3][3]=1;
 // Every node's world transform, in the file frame (metres).
 std::vector<const aiNode*> all;
 std::function<void(const aiNode*,const Mat4&,int)> walk=[&](const aiNode* n,const Mat4& parent,int depth){
  if(depth>512)importFail("character.parse","This "+formatLabel(l.format)+" file could not be read: its scene is nested more than 512 levels deep",{{"format",l.format}});
  Mat4 w=parent*toGlm(n->mTransformation);c.nodeWorld[n]=w;all.push_back(n);for(unsigned i=0;i<n->mNumChildren;i++)walk(n->mChildren[i],w,depth+1);};
 walk(s->mRootNode,frame,0);
 std::multimap<std::string,const aiNode*> byName;for(auto n:all)byName.emplace(n->mName.C_Str(),n);
 auto nodeOf=[&](const aiBone* b)->const aiNode*{if(b->mNode)return b->mNode;auto [name,k]=boneName(b);auto [first,last]=byName.equal_range(name);for(auto it=first;it!=last;++it)if(--k==0)return it->second;return nullptr;};
 std::set<const aiNode*> boneNodes;std::set<const aiBone*> distinct;
 for(auto n:all)for(unsigned k=0;k<n->mNumMeshes;k++){auto m=s->mMeshes[n->mMeshes[k]];for(unsigned b=0;b<m->mNumBones;b++){distinct.insert(m->mBones[b]);if(auto bn=nodeOf(m->mBones[b]))boneNodes.insert(bn);}}
 c.bones=distinct.size();
 // An armature is a top-level hierarchy of the scene. Assimp's own aiBone::mArmature
 // (the first ancestor that no mesh lists as a bone) splits one skeleton wherever an
 // unweighted bone sits between weighted ones.
 auto armatureOf=[&](const aiBone* b)->const aiNode*{auto n=nodeOf(b);if(!n)return s->mRootNode;while(n->mParent&&n->mParent!=s->mRootNode)n=n->mParent;return n;};
 // Armatures, with the vertices each one moves.
 std::map<const aiNode*,int> armatureIndex;
 for(auto n:all)for(unsigned k=0;k<n->mNumMeshes;k++){auto m=s->mMeshes[n->mMeshes[k]];std::map<int,uint64_t> moved;std::vector<uint8_t> weighted(m->mNumVertices,0);
  for(unsigned b=0;b<m->mNumBones;b++){auto bone=m->mBones[b];auto a=armatureOf(bone);auto [it,fresh]=armatureIndex.emplace(a,int(c.armatures.size()));
   if(fresh){Collected::Armature arm;arm.node=a;arm.name=sanitizeBoneName(a->mName.C_Str(),nullptr);c.armatures.push_back(arm);}
   if(auto bn=nodeOf(bone))c.armatures[it->second].bones.insert(bn);
   for(unsigned w=0;w<bone->mNumWeights;w++){auto& vw=bone->mWeights[w];if(vw.mWeight>0&&vw.mVertexId<m->mNumVertices&&!weighted[vw.mVertexId]){weighted[vw.mVertexId]=1;moved[it->second]++;}}}
  for(auto& [a,count]:moved)c.armatures[a].weighted+=count;}
 if(c.armatures.empty())return c;
 c.chosen=0;for(size_t a=1;a<c.armatures.size();a++)if(c.armatures[a].weighted>c.armatures[c.chosen].weighted)c.chosen=int(a);
 auto& chosen=c.armatures[c.chosen];
 // Kept: the chosen armature's bones, their ancestors below it, and the non-mesh nodes under them (end bones).
 std::set<const aiNode*> keep;
 for(auto b:chosen.bones)for(auto n=b;n&&n!=s->mRootNode;n=n->mParent){if(n==chosen.node&&!boneNodes.contains(n))break;keep.insert(n);}
 std::function<void(const aiNode*)> under=[&](const aiNode* n){for(unsigned i=0;i<n->mNumChildren;i++){auto ch=n->mChildren[i];if(ch->mNumMeshes&&!ch->mNumChildren&&!boneNodes.contains(ch))continue;keep.insert(ch);under(ch);}};
 for(auto n:std::vector<const aiNode*>(keep.begin(),keep.end()))under(n);
 for(auto n:all)if(keep.contains(n)&&!foldedNode(n)){int parent=-1;for(auto p=n->mParent;p;p=p->mParent)if(auto it=c.index.find(p);it!=c.index.end()){parent=it->second;break;}
  c.index[n]=int(c.nodes.size());c.nodes.push_back(n);c.parent.push_back(parent);c.world.push_back(c.nodeWorld[n]);}
 if(c.nodes.size()>MaxBones)importFail("character.too_many_bones","The skeleton has "+std::to_string(c.nodes.size())+" bones; at most 4,096 are supported.",{{"bones",c.nodes.size()}});
 // Names: readable UTF-8, unique.
 std::set<std::string> used;
 for(size_t i=0;i<c.nodes.size();i++){std::string issue;auto name=sanitizeBoneName(c.nodes[i]->mName.C_Str(),&issue);if(name.empty()){name="bone_"+std::to_string(i);issue="empty";}
  auto unique=name;for(int k=2;used.contains(unique);k++)unique=name+" #"+std::to_string(k);used.insert(unique);
  c.raw.push_back(c.nodes[i]->mName.C_Str());c.names.push_back(unique);c.issues.push_back(issue);}
 std::multimap<std::string,int> keptByName;for(size_t i=0;i<c.nodes.size();i++)keptByName.emplace(c.raw[i],int(i));
 auto keptAncestor=[&](const aiNode* n)->int{for(;n;n=n->mParent)if(auto it=c.index.find(n);it!=c.index.end())return it->second;return -1;};
 // Meshes: skinned to the kept skeleton, rebound by bone name, or left out.
 for(auto n:all)for(unsigned k=0;k<n->mNumMeshes;k++){auto m=s->mMeshes[n->mMeshes[k]];
  if(!m->mNumVertices||!m->mNumFaces||(!probe&&!(m->mPrimitiveTypes&aiPrimitiveType_TRIANGLE)))continue;
  MeshUse u;u.node=n;u.mesh=m;u.name=sanitizeBoneName(m->mName.C_Str(),nullptr);if(u.name.empty())u.name=sanitizeBoneName(n->mName.C_Str(),nullptr);
  size_t weightedBones=0,named=0;bool foreign=false;
  for(unsigned b=0;b<m->mNumBones;b++){auto bone=m->mBones[b];auto bn=nodeOf(bone);int target=-1;bool weights=false;for(unsigned w=0;w<bone->mNumWeights&&!weights;w++)weights=bone->mWeights[w].mWeight>0;
   if(bn&&c.armatures[armatureIndex[armatureOf(bone)]].node==chosen.node)target=keptAncestor(bn);
   else{foreign=true;auto it=keptByName.find(boneName(bone).first);if(it!=keptByName.end())target=it->second;}
   if(weights){weightedBones++;named+=target>=0;}
   u.target.push_back(target);u.skin.push_back((bn?c.nodeWorld[bn]:c.nodeWorld[n])*toGlm(bone->mOffsetMatrix));}
  if(foreign){if(weightedBones&&named*10>=weightedBones*9)u.kept="rebound";else u.kept="dropped";}
  u.rigid=keptAncestor(n);
  if(u.kept=="rebound"&&std::find(c.rebound.begin(),c.rebound.end(),u.name)==c.rebound.end())c.rebound.push_back(u.name);
  if(u.kept=="dropped"&&std::find(c.dropped.begin(),c.dropped.end(),u.name)==c.dropped.end())c.dropped.push_back(u.name);
  if(u.kept!="dropped"){u.influences.resize(m->mNumVertices);
   for(unsigned b=0;b<m->mNumBones;b++){auto bone=m->mBones[b];for(unsigned w=0;w<bone->mNumWeights;w++){auto& vw=bone->mWeights[w];if(vw.mWeight>0&&vw.mVertexId<m->mNumVertices)u.influences[vw.mVertexId].push_back({int(b),vw.mWeight});}}
   c.vertices+=m->mNumVertices;for(unsigned f=0;f<m->mNumFaces;f++)c.triangles+=m->mFaces[f].mNumIndices>=3?m->mFaces[f].mNumIndices-2:0;}
  c.meshes.push_back(std::move(u));}
 if(c.triangles>MaxTriangles)importFail("character.too_complex","The model has "+std::to_string(c.triangles)+" triangles; at most 4,000,000 are supported.",{{"triangles",c.triangles}});
 return c;
}
// One vertex in the displayed pose: position and blended linear part (file frame,
// metres), and up to four kept-bone weights (BDEF1/2/4).
struct Skinned {Vec3 p{0};Mat3 linear{1.f};std::array<int,4> bone{-1,-1,-1,-1};std::array<float,4> weight{};bool skinned=false;};
Skinned skinVertex(const Collected& c,const MeshUse& u,unsigned v,int fallback){
 Skinned out;auto& list=u.influences[v];float total=0;for(auto& [b,w]:list)total+=w;
 Mat4 m(0.f);
 if(total>1e-8f){for(auto& [b,w]:list)m+=u.skin[b]*(w/total);
  std::map<int,float> merged;for(auto& [b,w]:list)if(u.target[b]>=0)merged[u.target[b]]+=w/total;
  std::vector<std::pair<float,int>> sorted;for(auto& [bone,w]:merged)sorted.push_back({w,bone});std::sort(sorted.begin(),sorted.end(),[](auto& a,auto& b){return a.first!=b.first?a.first>b.first:a.second<b.second;});
  if(sorted.size()>4)sorted.resize(4);float kept=0;for(auto& s:sorted)kept+=s.first;
  for(size_t k=0;k<sorted.size()&&kept>0;k++){out.bone[k]=sorted[k].second;out.weight[k]=sorted[k].first/kept;}
  out.skinned=out.bone[0]>=0;}
 else m=c.nodeWorld.at(u.node);
 if(!out.skinned){out.bone={u.rigid>=0?u.rigid:fallback,-1,-1,-1};out.weight={1,0,0,0};}
 out.p=Vec3(m*Vec4(v3(u.mesh->mVertices[v]),1));out.linear=Mat3(m);
 return out;
}

// ---- the character frame: x the character's left, y up, z the way it faces ----
struct Frame {Mat3 rotation{1.f};Vec3 offset{0};std::string upSource="default",facingSource="none",up="+y";Vec3 apply(Vec3 p)const{return rotation*p+offset;}};
// Rows of `rotation` are left, up and front.
Mat3 rows(Vec3 left,Vec3 up,Vec3 front){Mat3 m;for(int c=0;c<3;c++){m[c][0]=left[c];m[c][1]=up[c];m[c][2]=front[c];}return m;}
Vec3 snapAxis(Vec3 v,bool& snapped){v=glm::normalize(v);snapped=false;int best=0;for(int k=1;k<3;k++)if(std::abs(v[k])>std::abs(v[best]))best=k;Vec3 axis(0);axis[best]=v[best]<0?-1.f:1.f;if(glm::dot(v,axis)>=std::cos(glm::radians(30.f))){snapped=true;return axis;}return v;}
std::string axisName(Vec3 v){int best=0;for(int k=1;k<3;k++)if(std::abs(v[k])>std::abs(v[best]))best=k;return std::string(v[best]<0?"-":"+")+"xyz"[best];}
struct Picks {int pelvis=-1,head=-1,lArm=-1,rArm=-1,lThigh=-1,rThigh=-1;bool headNamed=false;};
// Up from the hips to the head; left from the right arm and leg to the left ones.
Frame frameFrom(const std::vector<Vec3>& p,const Picks& k,const Vec3& fileFront,bool fileHint,bool metadata,bool final){
 Frame f;Vec3 up(0,1,0);f.upSource=metadata?"metadata":"default";
 if(k.pelvis>=0&&k.head>=0&&(final||k.headNamed)&&glm::length(p[k.head]-p[k.pelvis])>1e-5f){bool snapped;up=snapAxis(p[k.head]-p[k.pelvis],snapped);f.upSource="skeleton";}
 Vec3 l(0);if(k.lArm>=0&&k.rArm>=0)l+=p[k.lArm]-p[k.rArm];if(k.lThigh>=0&&k.rThigh>=0)l+=p[k.lThigh]-p[k.rThigh];l-=up*glm::dot(l,up);
 Vec3 front;
 if(glm::length(l)>1e-4f){l=glm::normalize(l);front=glm::cross(l,up);f.facingSource="names";}
 else{if(final)importFail("character.orientation","The model's orientation could not be worked out from its hips, head, arms and legs.");
  front=fileFront-up*glm::dot(fileFront,up);if(glm::length(front)<1e-4f)front=Vec3(0,0,1)-up*up.z;if(glm::length(front)<1e-4f)front=Vec3(1,0,0);front=glm::normalize(front);f.facingSource=fileHint?"file":"none";}
 Vec3 left=glm::normalize(glm::cross(up,front));front=glm::cross(left,up);
 f.rotation=rows(left,up,front);f.up=axisName(up);
 return f;
}

// ---- names the fitter matches: unassigned bones must not take them ----
std::string loose(std::string s){for(char& c:s)if((unsigned char)c>=128)c=' ';else c=char(std::tolower((unsigned char)c));std::erase_if(s,[](char c){return c==' '||c=='_'||c=='-';});return s;}  // rig.cpp normalized()
struct Reserved {std::set<std::string> exact,loose;};
const Reserved& reservedNames(){
 static const Reserved names=[]{Reserved r;auto add=[&](const std::string& n){r.exact.insert(n);auto l=loose(n);if(!l.empty())r.loose.insert(l);};
  for(auto& s:humanoidSlots()){add(s.key);add(s.mmdJp);add(s.mmdEn);}add("上半身2");add("upper body2");
  // The fitter's alias table (rig.cpp), every spelling it tries.
  const std::pair<const char*,std::vector<const char*>> centre[]={{"Pelvis",{"下半身","lower body","hips"}},{"Spine1",{"上半身","upper body","spine"}},{"Spine4",{"上半身3","上半身３","upper body3","UpperBody3","上半身2","上半身２","upper body2","UpperBody2","chest"}},{"Neck1",{"首","neck"}},{"Head1",{"頭","head"}}};
  for(auto& [part,list]:centre)for(auto n:list)add(n);
  const std::vector<const char*> sided[]={{"肩","shoulder"},{"腕","arm"},{"ひじ","肘","elbow"},{"手首","wrist"},{"足","leg"},{"ひざ","膝","knee"},{"足首","ankle"},{"つま先","足先EX","toe"}};
  const char* fingers[]={"親指","人指","中指","薬指","小指"};const char* english[]={"thumb","index","middle","ring","little"};const char* full[]={"０","１","２","３"};
  for(auto [jp,side,word]:{std::tuple{"左","l","left "},{"右","r","right "}}){
   for(auto& list:sided)for(auto n:list){add(std::string(jp)+n);add(std::string(n)+"_"+side);add(std::string(word)+n);}
   for(int d=0;d<5;d++){for(int k=0;k<4;k++){add(std::string(jp)+fingers[d]+std::to_string(k));add(std::string(jp)+fingers[d]+full[k]);add(std::string(jp)+"人差指"+full[k]);}for(int k=1;k<=3;k++)add(std::string(english[d])+std::to_string(k)+"_"+side);}}
  return r;}();
 return names;
}

// ---- the request ----
std::string slotWords(const SlotInfo& s){std::string w=s.id;std::replace(w.begin(),w.end(),'_',' ');return w;}
struct Chain {int root=-1;std::string kind;Json values;bool collide=true;};
struct Mapping {std::map<std::string,int> values;int eyeL=-1,eyeR=-1;std::vector<Chain> chains;Json jiggle;bool automatic=false;};
float jiggleValue(const Json& values,const char* key,float lo,float hi,const std::string& kind){
 if(!values.is_object()||!values.contains(key)||!values[key].is_number())importFail("character.jiggle","The swinging part settings for "+kind+" are incomplete.",{{"reason","range"},{"kind",kind}});
 float v=values[key].get<float>();if(!std::isfinite(v)||v<lo||v>hi)importFail("character.jiggle","The swinging part setting "+std::string(key)+" for "+kind+" is out of range.",{{"reason","range"},{"kind",kind},{"setting",key}});
 return v;
}
Mapping resolveRequest(const Json& request,const Collected& c,const SkeletonView& view){
 Mapping out;std::map<std::string,int> byName;for(size_t i=0;i<c.names.size();i++)byName.emplace(c.names[i],int(i));
 auto boneNamed=[&](const Json& value,const std::string& slot,const char* code,const char* subject)->int{
  if(!value.is_string())importFail(code,std::string("The bone for ")+subject+" is not a name.",{{"reason","missing"},{"slot",slot},{"bone",""}});
  auto name=value.get<std::string>();if(name.empty())return -1;
  auto it=byName.find(name);if(it==byName.end())importFail(code,"Bone “"+name+"” for "+subject+" is not in the file.",{{"reason","missing"},{"slot",slot},{"bone",name}});
  return it->second;};
 auto& map=request["boneMap"];
 if(!map.is_object())importFail("character.bone_map","The bone assignment is malformed.",{{"reason","unknown_slot"},{"slot",""}});
 for(auto& [key,value]:map.items()){
  if(key=="ValveBiped.Bip01_Spine")importFail("character.bone_map","ValveBiped.Bip01_Spine is created automatically and cannot be assigned.",{{"reason","locked"},{"slot",key}});
  if(!mappedSlotKey(key))importFail("character.bone_map","“"+key+"” is not a body part this importer knows.",{{"reason","unknown_slot"},{"slot",key}});}
 for(auto& s:humanoidSlots()){if(s.convertOnly)continue;int b=map.contains(s.key)?boneNamed(map[s.key],s.key,"character.bone_map",("the "+slotWords(s)).c_str()):-1;out.values[s.key]=b;}
 auto eyes=request.value("eyes",Json::object());if(!eyes.is_object())eyes=Json::object();
 if(eyes.contains("L"))out.eyeL=boneNamed(eyes["L"],"Eye_L","character.bone_map","the left eye");
 if(eyes.contains("R"))out.eyeR=boneNamed(eyes["R"],"Eye_R","character.bone_map","the right eye");
 // Swinging parts: enabled groups' enabled chains.
 if(request.contains("jiggle")&&!request["jiggle"].is_null()){
  auto& jiggle=request["jiggle"];if(!jiggle.is_object())importFail("character.jiggle","The swinging part settings are malformed.",{{"reason","range"}});
  if(jiggle.value("version",1)>1)importFail("character.request_version","This import was prepared by a newer Model Hotloader.",{{"requestVersion",jiggle.value("version",1)}});
  out.jiggle=jiggle;
  for(auto& group:jiggle.value("groups",Json::array())){
   if(!group.is_object())continue;auto kind=group.value("kind",std::string());
   static const std::set<std::string> kinds={"hair","skirt","chest","tail","accessory"};
   if(!kinds.contains(kind))importFail("character.jiggle","“"+kind+"” is not a kind of swinging part.",{{"reason","range"},{"kind",kind}});
   bool enabled=group.value("enabled",false);auto values=group.value("values",Json::object());
   Json clean={{"stiffness",jiggleValue(values,"stiffness",0,4,kind)},{"dragForce",jiggleValue(values,"dragForce",0,1,kind)},{"gravityPower",jiggleValue(values,"gravityPower",0,2,kind)},{"hitRadius",jiggleValue(values,"hitRadius",0,.1f,kind)}};
   for(auto& chain:group.value("chains",Json::array())){if(!chain.is_object())continue;
    auto root=chain.value("root",Json());if(!root.is_string())continue;int b=byName.contains(root.get<std::string>())?byName[root.get<std::string>()]:-2;
    if(b==-2)importFail("character.jiggle","The swinging part “"+root.get<std::string>()+"” is not in the file.",{{"reason","missing"},{"root",root.get<std::string>()}});
    if(enabled&&chain.value("enabled",false))out.chains.push_back({b,kind,clean,group.value("collide",true)&&kind!="chest"});}
  }
 }
 // The structural rules the window shows, enforced here too.
 auto values=out.values;values["Eye_L"]=out.eyeL;values["Eye_R"]=out.eyeR;
 std::vector<int> roots;for(auto& ch:out.chains)roots.push_back(ch.root);
 for(auto& p:checkBoneMap(view,values,roots)){if(p.severity!="error")continue;
  auto slot=slotByKey(p.slot);std::string part=slot?"the "+slotWords(*slot):p.slot;std::string bone=p.bone>=0?c.names[p.bone]:"";
  if(p.code=="required")importFail("character.bone_map","No bone is assigned to "+part+", which every character needs.",{{"reason","required"},{"slot",p.slot},{"bone",""}});
  if(p.code=="duplicate")importFail("character.bone_map","Bone “"+bone+"” is assigned to two body parts.",{{"reason","duplicate"},{"slot",p.slot},{"bone",bone}});
  if(p.code=="order"){std::string parent=p.other>=0?c.names[p.other]:"";importFail("character.bone_map","Bone “"+bone+"” for "+part+" is not below “"+parent+"” in the skeleton.",{{"reason","order"},{"slot",p.slot},{"bone",bone},{"parent",parent}});}
  if(p.code=="leg_on_spine")importFail("character.bone_map","Bone “"+bone+"” for "+part+" hangs from the upper body; legs must hang from the hips.",{{"reason","leg_on_spine"},{"slot",p.slot},{"bone",bone}});
  if(p.code=="jiggle_body"){std::string body=p.other>=0?c.names[p.other]:"";importFail("character.jiggle","The swinging part “"+bone+"” contains the body bone “"+body+"” ("+part+").",{{"reason","body"},{"root",bone},{"bone",body},{"slot",p.slot}});}
  if(p.code=="jiggle_too_many")importFail("character.jiggle",p.text,{{"reason","too_many"}});
 }
 return out;
}

// ---- springs (spec: jiggle presets, joints and colliders) ----
Json springBones(const Mapping& map,const std::vector<PBone>& bones,const std::vector<std::vector<int>>& children,const std::vector<uint32_t>& weighted,float scale,std::vector<std::string>& warnings){
 if(map.chains.empty())return Json();
 float s=scale;  // final height / 1.6 m
 auto pos=[&](int b){return bones[b].position;};
 auto bone=[&](const char* key){auto it=map.values.find(std::string("ValveBiped.Bip01_")+key);return it==map.values.end()?-1:it->second;};
 Json colliders=Json::array();std::map<std::string,std::vector<int>> groups;
 auto sphere=[&](const char* group,int b,Vec3 offset,float radius){if(b<0)return;groups[group].push_back(int(colliders.size()));colliders.push_back({{"bone",b},{"shape","sphere"},{"offset",xyz(offset)},{"radius",radius*s*UnitsPerMeter}});};
 auto capsule=[&](const char* group,int b,Vec3 offset,Vec3 tail,float radius){if(b<0)return;groups[group].push_back(int(colliders.size()));colliders.push_back({{"bone",b},{"shape","capsule"},{"offset",xyz(offset)},{"tail",xyz(tail)},{"radius",radius*s*UnitsPerMeter}});};
 int head=bone("Head1"),neck=bone("Neck1"),chest=bone("Spine4"),spine=bone("Spine1"),pelvis=bone("Pelvis");
 sphere("head",head,Vec3(0,.09f*s*UnitsPerMeter,0),.10f);sphere("neck",neck,Vec3(0),.05f);
 int top=neck>=0?neck:head;
 if(top>=0){if(chest>=0)capsule("chest",chest,Vec3(0),pos(top)-pos(chest),.11f);else if(spine>=0){Vec3 at=glm::mix(pos(spine),pos(top),.55f)-pos(spine);capsule("chest",spine,at,pos(top)-pos(spine),.11f);}}
 sphere("hips",pelvis,Vec3(0),.12f);
 for(auto side:{"L_","R_"}){auto key=[&](const char* part){return bone((std::string(side)+part).c_str());};
  int upper=key("UpperArm"),fore=key("Forearm"),thigh=key("Thigh"),calf=key("Calf"),foot=key("Foot");
  if(upper>=0&&fore>=0)capsule("upperArms",upper,Vec3(0),pos(fore)-pos(upper),.045f);
  if(thigh>=0&&calf>=0)capsule("thighs",thigh,Vec3(0),pos(calf)-pos(thigh),.075f);
  if(calf>=0&&foot>=0)capsule("calves",calf,Vec3(0),pos(foot)-pos(calf),.055f);}
 static const std::map<std::string,std::vector<const char*>> touches={{"hair",{"head","neck","chest","upperArms"}},{"skirt",{"hips","thighs","calves"}},{"chest",{}},{"tail",{"hips","thighs"}},{"accessory",{"head","chest"}}};
 // Only the groups an imported spring uses are written; colliders are renumbered.
 std::map<std::string,int> groupIndex;Json colliderGroups=Json::array(),keptColliders=Json::array();std::map<int,int> renumber;
 auto groupOf=[&](const std::string& name)->int{if(auto it=groupIndex.find(name);it!=groupIndex.end())return it->second;auto g=groups.find(name);if(g==groups.end())return -1;Json ids=Json::array();
  for(int k:g->second){if(!renumber.contains(k)){renumber[k]=int(keptColliders.size());keptColliders.push_back(colliders[k]);}ids.push_back(renumber[k]);}
  int index=int(colliderGroups.size());colliderGroups.push_back(ids);groupIndex[name]=index;return index;};
 Json springs=Json::array(),joints=Json::array();std::set<int> simulated;size_t degenerate=0;
 for(auto& chain:map.chains){Json cg=Json::array();if(chain.collide)for(auto g:touches.at(chain.kind)){int index=groupOf(g);if(index>=0)cg.push_back(index);}
  int spring=int(springs.size());springs.push_back({{"name",bones[chain.root].name},{"center",-1},{"colliderGroups",cg}});
  auto& v=chain.values;float hit=v["hitRadius"].get<float>()*UnitsPerMeter;
  std::vector<int> stack{chain.root};std::vector<int> order;while(!stack.empty()){int b=stack.back();stack.pop_back();order.push_back(b);for(size_t k=children[b].size();k-->0;)stack.push_back(children[b][k]);}
  std::vector<int> size(bones.size(),0);for(size_t k=order.size();k-->0;){int b=order[k];size[b]=1;for(int ch:children[b])size[b]+=size[ch];}
  for(int b:order){bool leaf=children[b].empty();if(leaf&&!weighted[b])continue;  // an end bone is its parent's tail
   if(!simulated.insert(b).second)continue;
   int tail=-1;Vec3 offset;
   if(!leaf){tail=children[b][0];for(int ch:children[b])if(size[ch]>size[tail])tail=ch;offset=pos(tail)-pos(b);}
   else{int parent=bones[b].parent;Vec3 dir=parent>=0?pos(b)-pos(parent):Vec3(0,-1,0);offset=glm::length(dir)>1e-6f?glm::normalize(dir)*(.07f*s*UnitsPerMeter):Vec3(0);}
   if(glm::length(offset)<1e-5f){degenerate++;simulated.erase(b);continue;}
   joints.push_back({{"spring",spring},{"bone",b},{"tail",tail},{"tailOffset",xyz(offset)},{"hitRadius",hit},{"stiffness",v["stiffness"]},{"gravityPower",v["gravityPower"]},{"gravityDir",Json::array({0,-1,0})},{"dragForce",v["dragForce"]}});}
 }
 if(degenerate)warnings.push_back(std::to_string(degenerate)+" spring bone(s) have no length (their tail sits on the bone) and stay still.");
 // Parents update before children: bone indices are depth-first.
 std::stable_sort(joints.begin(),joints.end(),[](const Json& a,const Json& b){return a["bone"].get<int>()<b["bone"].get<int>();});
 if(joints.empty())return Json();
 return {{"unitsPerMeter",UnitsPerMeter},{"colliders",keptColliders},{"colliderGroups",colliderGroups},{"springs",springs},{"joints",joints}};
}

// ---- materials ----
enum class AlphaUse {Keep,Opaque,Cutout};
std::string imageExtension(const Bytes& b){if(b.size()>=8&&!std::memcmp(b.data(),"\x89PNG",4))return ".png";if(b.size()>=3&&b[0]==0xFF&&b[1]==0xD8)return ".jpg";if(b.size()>=4&&!std::memcmp(b.data(),"DDS ",4))return ".dds";return "";}
Bytes encodePng(const unsigned char* rgba,int w,int h){Bytes png;auto sink=[](void* context,void* p,int size){auto& b=*static_cast<Bytes*>(context);auto s=static_cast<unsigned char*>(p);b.insert(b.end(),s,s+size);};
 if(!stbi_write_png_to_func(sink,&png,w,h,4,rgba,w*4))throw std::runtime_error("Texture encoding failed");return png;}
// Bakes glTF's OPAQUE (alpha ignored) and MASK (alpha cut) into the texture, as vrm.cpp does.
Bytes applyAlpha(Bytes bytes,AlphaUse use,int cut){
 if(use==AlphaUse::Keep||bytes.size()>INT_MAX)return bytes;
 int x=0,y=0,c=0;if(!stbi_info_from_memory(bytes.data(),int(bytes.size()),&x,&y,&c)||(c!=2&&c!=4))return bytes;
 std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(bytes.data(),int(bytes.size()),&x,&y,&c,4),stbi_image_free);if(!pixels)return bytes;
 bool changed=false;for(size_t i=0;i<size_t(x)*y;i++){auto& a=pixels.get()[i*4+3];uint8_t v=use==AlphaUse::Opaque?255:(a>=cut?255:0);changed|=v!=a;a=v;}
 return changed?encodePng(pixels.get(),x,y):bytes;
}
struct Textures {
 const aiScene* scene;props::TextureResolver resolver;std::map<std::string,Bytes> files;std::vector<std::string> paths;std::map<std::pair<std::string,int>,int> index;std::vector<std::string>& warnings;
 Textures(const aiScene* s,const fs::path& folder,std::vector<std::string>& w):scene(s),resolver(folder),warnings(w){}
 int get(const std::string& reference,const std::string& material,AlphaUse use,float cutoff){
  int cut=use==AlphaUse::Cutout?int(std::lround(std::clamp(cutoff,0.f,1.f)*255)):0;auto key=std::make_pair(reference,int(use)*1000+cut);
  if(auto it=index.find(key);it!=index.end())return it->second;
  Bytes bytes;
  try{
   if(auto embedded=scene->GetEmbeddedTexture(reference.c_str())){
    if(!embedded->mHeight)bytes.assign(reinterpret_cast<const unsigned char*>(embedded->pcData),reinterpret_cast<const unsigned char*>(embedded->pcData)+embedded->mWidth);
    else{if(embedded->mWidth>16384||embedded->mHeight>16384)throw std::runtime_error("Embedded texture exceeds 16384 pixels");
     std::vector<unsigned char> rgba(size_t(embedded->mWidth)*embedded->mHeight*4);for(size_t i=0;i<rgba.size()/4;i++){auto p=embedded->pcData[i];rgba[i*4]=p.r;rgba[i*4+1]=p.g;rgba[i*4+2]=p.b;rgba[i*4+3]=p.a;}
     bytes=encodePng(rgba.data(),int(embedded->mWidth),int(embedded->mHeight));}}
   else{
    // Network locations are never opened; only the file name is looked up beside the model.
    std::string local=networkReference(reference)?utf8(fs::path(wide(reference)).filename().wstring()):reference;
    if(local.empty())throw std::runtime_error("Missing texture: "+reference);
    auto file=resolver.resolve(local).path;std::error_code ec;auto size=fs::file_size(ioPath(file),ec);
    if(ec||size>(256ull<<20))throw std::runtime_error("Texture file is unreadable or larger than 256 MB: "+utf8(file.filename().wstring()));
    bytes=readFile(file);}
  }catch(const std::exception& e){warnings.push_back("Texture for material "+material+": "+e.what());return index[key]=-1;}
  bytes=applyAlpha(std::move(bytes),use,cut);
  auto extension=imageExtension(bytes);if(extension.empty()){auto from=lowerAscii(utf8(fs::path(wide(reference)).extension().wstring()));extension=from.size()>1&&from.size()<=5?from:".png";}
  std::string path="char/tex"+std::to_string(paths.size())+extension;files[path]=std::move(bytes);int i=int(paths.size());paths.push_back(path);return index[key]=i;
 }
};
PMaterial material(const aiMaterial* m,unsigned index,const std::string& format,Textures& textures){
 PMaterial out;out.source=int(index);aiString name;if(m->Get(AI_MATKEY_NAME,name)==AI_SUCCESS)out.name=sanitizeBoneName(name.C_Str(),nullptr);if(out.name.empty())out.name="material_"+std::to_string(index);
 aiColor4D c(1,1,1,1);if(m->Get(AI_MATKEY_BASE_COLOR,c)!=AI_SUCCESS)m->Get(AI_MATKEY_COLOR_DIFFUSE,c);
 float opacity=1;m->Get(AI_MATKEY_OPACITY,opacity);bool gltf=format=="glb"||format=="gltf";
 // Exporters write a zero transparency factor meaning "not set"; such parts are opaque.
 if(gltf||!std::isfinite(opacity)||opacity<.01f)opacity=1;
 auto unit=[](float x){return std::isfinite(x)?std::clamp(x,0.f,1.f):1.f;};
 out.diffuse=Vec4(unit(c.r),unit(c.g),unit(c.b),unit(c.a*opacity));
 int two=0;m->Get(AI_MATKEY_TWOSIDED,two);out.twoSided=two!=0;
 std::string mode;aiString alpha;if(m->Get(AI_MATKEY_GLTF_ALPHAMODE,alpha)==AI_SUCCESS)mode=alpha.C_Str();float cutoff=.5f;m->Get(AI_MATKEY_GLTF_ALPHACUTOFF,cutoff);
 AlphaUse use=mode=="OPAQUE"?AlphaUse::Opaque:mode=="MASK"?AlphaUse::Cutout:AlphaUse::Keep;
 out.queue=mode=="BLEND"||(mode.empty()&&out.diffuse.w<.999f)?3000:mode=="MASK"?2450:2000;
 if(mode=="OPAQUE")out.diffuse.w=1;
 aiString ref;if(m->GetTexture(aiTextureType_BASE_COLOR,0,&ref)==AI_SUCCESS||m->GetTexture(aiTextureType_DIFFUSE,0,&ref)==AI_SUCCESS)out.texture=textures.get(ref.C_Str(),out.name,use,cutoff);
 out.memo=formatLabel(format)+" material";
 return out;
}

// ---- morphs: well-known expressions become the MMD names Face Poser and lip sync use ----
struct Expression {const char* source;const char* mmd;int panel;};
const Expression Expressions[]={{"vrc.v_aa","あ",3},{"v_aa","あ",3},{"aa","あ",3},{"a","あ",3},{"mouthopen","あ",3},{"jawopen","あ",3},{"vrc.v_ih","い",3},{"v_ih","い",3},{"vrc.v_ou","う",3},{"v_ou","う",3},{"vrc.v_e","え",3},{"v_e","え",3},{"vrc.v_oh","お",3},{"v_oh","お",3},
 {"vrc.blink","まばたき",2},{"blink","まばたき",2},{"eyeblink","まばたき",2},{"eyeblinkleft","ウィンク２",2},{"blink_l","ウィンク２",2},{"blink_left","ウィンク２",2},{"eyeblinkright","ウィンク２右",2},{"blink_r","ウィンク２右",2},{"blink_right","ウィンク２右",2}};
int morphPanel(const std::string& name){auto lower=lowerAscii(name);return lower.find("brw")!=std::string::npos||lower.find("brow")!=std::string::npos?1:lower.find("eye")!=std::string::npos||lower.find("blink")!=std::string::npos?2:lower.find("mth")!=std::string::npos||lower.find("mouth")!=std::string::npos?3:4;}

// ---- shared analysis of the probe and the converter ----
struct Analysis {
 Collected c;std::vector<Skinned> verts;std::vector<std::pair<size_t,unsigned>> owner;  // vertex -> (mesh, vertex)
 std::vector<uint32_t> weighted;SkeletonView view;std::vector<BoneMeaning> meanings;HumanoidGuess guess;Frame frame;
};
// Bone positions and weighted counts in the file frame, then the automatic
// assignment, the frame it implies, and the assignment again in that frame.
void analyse(Analysis& a,const Loaded& l,bool samplePoints){
 auto& c=a.c;size_t n=c.nodes.size();a.weighted.assign(n,0);
 int fallback=-1;
 for(size_t mi=0;mi<c.meshes.size();mi++){auto& u=c.meshes[mi];if(u.kept=="dropped")continue;
  for(unsigned v=0;v<u.mesh->mNumVertices;v++){auto s=skinVertex(c,u,v,fallback);for(int k=0;k<4;k++)if(s.bone[k]>=0&&s.weight[k]>=.1f&&s.skinned)a.weighted[s.bone[k]]++;a.verts.push_back(s);a.owner.push_back({mi,v});}}
 std::vector<Vec3> bones(n);for(size_t i=0;i<n;i++)bones[i]=Vec3(c.world[i][3]);
 auto build=[&](const Frame& f){SkeletonView v;v.bones.resize(n);
  for(size_t i=0;i<n;i++){auto& b=v.bones[i];b.name=c.names[i];b.nameIssue=c.issues[i];b.parent=c.parent[i];auto p=f.apply(bones[i]);b.position={p.x,p.y,p.z};b.weighted=a.weighted[i];
   if(c.nodes[i]->mNumChildren==0)b.flags.push_back("leaf");}
  // Points: every k-th vertex, plus up to 8 strongest vertices of each sparsely sampled bone.
  if(samplePoints&&!a.verts.empty()){size_t step=(a.verts.size()+MaxPoints-1)/MaxPoints;std::vector<int> samples(n,0);std::vector<uint8_t> taken(a.verts.size(),0);
   auto add=[&](size_t k){auto& s=a.verts[k];auto p=f.apply(s.p);int bone=s.skinned?s.bone[0]:-1;v.points.insert(v.points.end(),{p.x,p.y,p.z,float(bone)});taken[k]=1;if(bone>=0)samples[bone]++;};
   for(size_t k=0;k<a.verts.size();k+=step)add(k);
   std::vector<std::vector<std::pair<float,size_t>>> best(n);
   for(size_t k=0;k<a.verts.size();k++){auto& s=a.verts[k];if(!s.skinned)continue;for(int q=0;q<4;q++){int b=s.bone[q];if(b<0)continue;auto& list=best[b];list.push_back({-s.weight[q],k});if(list.size()>16){std::sort(list.begin(),list.end());list.resize(8);}}}
   for(size_t b=0;b<n;b++){if(!a.weighted[b]||samples[b]>=8)continue;auto& list=best[b];std::sort(list.begin(),list.end());int extra=0;for(auto& [w,k]:list){if(extra>=8)break;if(!taken[k]){add(k);extra++;}}}}
  float lo=0,hi=0;bool any=false;for(size_t k=1;k<v.points.size();k+=4){if(!any){lo=hi=v.points[k];any=true;}lo=std::min(lo,v.points[k]);hi=std::max(hi,v.points[k]);}
  if(!any)for(auto& b:v.bones){if(!any){lo=hi=b.position[1];any=true;}lo=std::min(lo,b.position[1]);hi=std::max(hi,b.position[1]);}
  v.height=std::max(1e-3f,hi-lo);return v;};
 auto picks=[&](const HumanoidGuess& g){Picks k;auto get=[&](const char* key){auto it=g.slots.find(std::string("ValveBiped.Bip01_")+key);return it==g.slots.end()?SlotGuess{}:it->second;};
  auto named=[&](const char* key){auto s=get(key);return s.method=="name"&&s.confidence>=.85f?s.bone:-1;};
  k.pelvis=named("Pelvis");k.head=named("Head1");k.headNamed=k.pelvis>=0&&k.head>=0;
  k.lArm=named("L_UpperArm");k.rArm=named("R_UpperArm");k.lThigh=named("L_Thigh");k.rThigh=named("R_Thigh");return k;};
 Frame identity;a.view=build(identity);a.meanings=classifyBones(a.view);a.guess=guessHumanoid(a.view,a.meanings);
 bool hint=l.format!="fbx"||l.axisMetadata;
 a.frame=frameFrom(bones,picks(a.guess),Vec3(0,0,1),hint,hint,false);
 // The feet point forward when no names say where left is.
 if(a.frame.facingSource!="names"){Vec3 sum(0);int feet=0;auto up=Vec3(a.frame.rotation[0][1],a.frame.rotation[1][1],a.frame.rotation[2][1]);
  for(auto key:{"ValveBiped.Bip01_L_Foot","ValveBiped.Bip01_R_Foot"}){auto it=a.guess.slots.find(key);if(it==a.guess.slots.end()||it->second.bone<0)continue;int f=it->second.bone;
   for(size_t i=0;i<n;i++)if(c.parent[i]==f){Vec3 d=bones[i]-bones[f];d-=up*glm::dot(d,up);if(glm::length(d)>1e-5f){sum+=glm::normalize(d);feet++;}break;}}
  if(feet&&glm::length(sum)>1e-4f){a.frame=frameFrom(bones,Picks{},glm::normalize(sum),true,hint,false);a.frame.facingSource="feet";}}
 // Recentred: the hips over the origin, the lowest point on the floor.
 auto place=[&](Frame& f,int pelvis){Vec3 lo(1e30f);for(auto& s:a.verts)lo=glm::min(lo,f.apply(s.p));if(a.verts.empty())for(auto& b:bones)lo=glm::min(lo,f.apply(b));
  Vec3 centre=pelvis>=0?f.apply(bones[pelvis]):Vec3(0);f.offset-=Vec3(centre.x,lo.y,centre.z);};
 int pelvis=-1;if(auto it=a.guess.slots.find("ValveBiped.Bip01_Pelvis");it!=a.guess.slots.end())pelvis=it->second.bone;
 place(a.frame,pelvis);
 a.view=build(a.frame);a.meanings=classifyBones(a.view);a.guess=guessHumanoid(a.view,a.meanings);
}
Analysis start(const fs::path& source,bool probe,const CharacterProgress& report,const char* stage,float from,float to,Loaded& loaded){
 loaded=load(source,probe,report,stage,from,to);
 Analysis a;a.c=collect(loaded,probe);auto label=formatLabel(loaded.format);
 if(!a.c.bones||a.c.chosen<0)importFail("character.no_skeleton","This "+label+" file has no skeleton: no bones move its meshes.",{{"meshes",loaded.scene->mNumMeshes},{"bones",0}});
 if(report)report(stage,to);
 analyse(a,loaded,probe);
 if(a.guess.candidates<15)importFail("character.too_few_bones","The skeleton has only "+std::to_string(a.guess.candidates)+" usable bones; a character needs at least 15.",{{"bones",a.guess.candidates}});
 return a;
}
}

bool convertibleCharacter(const fs::path& path){auto f=formatOf(path);return f=="fbx"||f=="glb"||f=="gltf"||f=="dae";}
std::vector<std::string> characterFormats(){return {"fbx","glb","gltf","dae"};}

Json probeCharacter(const fs::path& source,const Json&,const CharacterProgress& report){
 ImportScope scope("Reading the skeleton");Loaded loaded;
 auto a=start(source,true,report,"Reading the skeleton",.02f,.8f,loaded);auto& c=a.c;
 if(report)report("Reading the skeleton",.9f);
 Json armatures=Json::array();for(size_t i=0;i<c.armatures.size();i++){auto& arm=c.armatures[i];armatures.push_back({{"name",arm.name},{"bones",arm.bones.size()},{"weightedVertices",arm.weighted},{"chosen",int(i)==c.chosen}});}
 Json meshes=Json::array();std::set<std::string> morphNames;size_t renamed=0;std::set<std::string> textures;
 for(auto& u:c.meshes){uint64_t tris=0;for(unsigned f=0;f<u.mesh->mNumFaces;f++)tris+=u.mesh->mFaces[f].mNumIndices>=3?u.mesh->mFaces[f].mNumIndices-2:0;
  meshes.push_back({{"name",u.name},{"vertices",u.mesh->mNumVertices},{"triangles",tris},{"skinned",u.mesh->mNumBones>0},{"kept",u.kept}});
  if(u.kept!="dropped")for(unsigned k=0;k<u.mesh->mNumAnimMeshes;k++){auto name=sanitizeBoneName(u.mesh->mAnimMeshes[k]->mName.C_Str(),nullptr);if(!name.empty()&&morphNames.insert(name).second){auto lower=lowerAscii(name);for(auto& e:Expressions)if(lower==e.source){renamed++;break;}}}}
 for(unsigned i=0;i<loaded.scene->mNumMaterials;i++){aiString ref;auto m=loaded.scene->mMaterials[i];for(auto type:{aiTextureType_BASE_COLOR,aiTextureType_DIFFUSE})if(m->GetTexture(type,0,&ref)==AI_SUCCESS)textures.insert(ref.C_Str());}
 Json warnings=Json::array();
 if(!c.dropped.empty()){std::string names;for(size_t i=0;i<c.dropped.size();i++)names+=(i?", ":"")+c.dropped[i];warnings.push_back(std::to_string(c.dropped.size())+" meshes use a second skeleton and will be left out: "+names);}
 auto skeleton=skeletonJson(a.view,a.meanings);skeleton["armatures"]=armatures;
 float height=a.view.height;bool normalized=height<.3f||height>3.f;
 Json probe={{"version",1},{"format",loaded.format},{"generator",loaded.generator},
  {"units",{{"metersPerUnit",loaded.metersPerUnit},{"up",a.frame.up},{"upSource",a.frame.upSource},{"facingSource",a.frame.facingSource}}},
  {"height",{{"meters",std::round(height*1e3f)/1e3f},{"normalized",normalized}}},
  {"skeleton",skeleton},{"auto",guessJson(a.guess)},{"meshes",meshes},{"materials",loaded.scene->mNumMaterials},{"textures",textures.size()},{"vertices",c.vertices},{"triangles",c.triangles},
  {"morphs",{{"count",morphNames.size()},{"renamed",renamed}}},{"warnings",warnings}};
 // status.json stays under 2 MB: thin the points first.
 for(int pass=0;pass<4&&probe.dump().size()>(2u<<20);pass++){auto& points=probe["skeleton"]["points"];Json thinned=Json::array();for(size_t i=0;i+3<points.size();i+=8)for(int k=0;k<4;k++)thinned.push_back(points[i+k]);points=thinned;}
 if(report)report("Reading the skeleton",.95f);
 return probe;
}

CharacterConversion convertCharacter(const fs::path& source,const Json& request,const CharacterProgress& report){
 ImportScope scope("Converting the character");
 if(request.value("requestVersion",1)>1)importFail("character.request_version","This import was prepared by a newer Model Hotloader.",{{"requestVersion",request.value("requestVersion",1)}});
 if(request.dump().size()>MaxRequestBytes)importFail("character.bone_map","The bone assignment is too large.",{{"reason","size"},{"slot",""}});
 Loaded loaded;auto a=start(source,false,report,"Converting the character",.05f,.5f,loaded);auto& c=a.c;size_t n=c.nodes.size();
 // The assignment: the request's, or the automatic one when it is certain (older addon files send none).
 Mapping map;
 if(request.contains("boneMap"))map=resolveRequest(request,c,a.view);
 else{std::vector<std::string> unsure;
  for(auto& [key,g]:a.guess.slots){map.values[key]=g.bone;auto s=slotByKey(key);if(s&&s->required&&(g.bone<0||g.confidence<.85f))unsure.push_back(key);}
  map.eyeL=a.guess.eyeL.bone;map.eyeR=a.guess.eyeR.bone;map.automatic=true;
  auto values=map.values;values["Eye_L"]=map.eyeL;values["Eye_R"]=map.eyeR;
  for(auto& p:checkBoneMap(a.view,values,{}))if(p.severity=="error"&&std::find(unsure.begin(),unsure.end(),p.slot)==unsure.end())unsure.push_back(p.slot);
  if(!unsure.empty())importFail("character.needs_mapping","This model needs its bones assigned. Update Model Hotloader's addon files to get the bone window.",{{"missing",unsure}});}
 if(report)report("Converting the character",.55f);
 // The final frame, from the assigned hips, head, arms and legs.
 std::vector<Vec3> bonesFile(n);for(size_t i=0;i<n;i++)bonesFile[i]=Vec3(c.world[i][3]);
 Picks k;auto bone=[&](const char* key){return map.values[std::string("ValveBiped.Bip01_")+key];};
 k.pelvis=bone("Pelvis");k.head=bone("Head1");k.lArm=bone("L_UpperArm");k.rArm=bone("R_UpperArm");k.lThigh=bone("L_Thigh");k.rThigh=bone("R_Thigh");
 Frame frame=frameFrom(bonesFile,k,Vec3(0,0,1),true,true,true);
 Vec3 lo(1e30f),hi(-1e30f);for(auto& s:a.verts){auto p=frame.apply(s.p);lo=glm::min(lo,p);hi=glm::max(hi,p);}
 if(a.verts.empty())importFail("character.no_skeleton","This "+formatLabel(loaded.format)+" file has no skeleton: no bones move its meshes.",{{"meshes",0},{"bones",c.bones}});
 Vec3 centre=frame.apply(bonesFile[k.pelvis]);frame.offset-=Vec3(centre.x,lo.y,centre.z);
 float height=hi.y-lo.y,correction=height<.3f||height>3.f?1.6f/std::max(height,1e-4f):1.f;float scale=UnitsPerMeter*correction;
 if(frame.apply(bonesFile[k.head]).y<=frame.apply(bonesFile[k.pelvis]).y)importFail("character.orientation","The model's orientation could not be worked out from its hips, head, arms and legs.");
 auto pmxPoint=[&](Vec3 p){p=frame.apply(p);return Vec3(p.x,p.y,-p.z)*scale;};
 auto pmxDir=[&](Vec3 d){d=frame.rotation*d;return Vec3(d.x,d.y,-d.z);};
 std::vector<std::string> warnings;
 // ---- bones ----
 std::vector<PBone> bones(n);std::vector<std::vector<int>> children(n);
 for(size_t i=0;i<n;i++){bones[i].position=pmxPoint(bonesFile[i]);bones[i].parent=c.parent[i];if(c.parent[i]>=0)children[c.parent[i]].push_back(int(i));}
 std::map<int,const SlotInfo*> slotOf;for(auto& s:humanoidSlots())if(!s.convertOnly&&map.values[s.key]>=0)slotOf[map.values[s.key]]=&s;
 bool middle=map.values["ValveBiped.Bip01_Spine2"]>=0;
 std::set<std::string> used;Json sourceNames=Json::object();
 for(auto& [b,s]:slotOf){bool chest=std::string(s->key)=="ValveBiped.Bip01_Spine4";bones[b].name=chest&&!middle?"上半身2":s->mmdJp;bones[b].english=chest&&!middle?"upper body2":s->mmdEn;}
 for(auto [b,jp,en]:{std::tuple{map.eyeL,"左目","eye_L"},{map.eyeR,"右目","eye_R"}})if(b>=0){bones[b].name=jp;bones[b].english=en;}
 for(auto& b:bones)if(!b.name.empty())used.insert(b.name),used.insert(b.english);
 auto& reserved=reservedNames();
 for(size_t i=0;i<n;i++){auto& b=bones[i];if(!b.name.empty()){if(b.name!=c.names[i])sourceNames[std::to_string(i)]=c.names[i];continue;}
  std::string name=c.names[i];if(reserved.exact.contains(name)||reserved.loose.contains(loose(name)))name+="_src";
  auto unique=name;for(int q=2;used.contains(unique);q++)unique=name+" #"+std::to_string(q);used.insert(unique);b.name=b.english=unique;if(unique!=c.names[i])sourceNames[std::to_string(i)]=c.names[i];}
 {std::vector<int> size(n,1);for(size_t i=n;i-->0;)if(c.parent[i]>=0)size[c.parent[i]]+=size[i];for(size_t i=0;i<n;i++){if(children[i].empty())continue;int tail=children[i][0];for(int ch:children[i])if(size[ch]>size[tail])tail=ch;bones[i].tail=tail;}}
 // ---- geometry and materials ----
 Textures textures(loaded.scene,source.parent_path(),warnings);
 std::map<unsigned,PMaterial> materials;std::vector<PVertex> vertices;vertices.reserve(a.verts.size());
 std::vector<Mat3> linear;linear.reserve(a.verts.size());std::vector<uint8_t> mirrored;
 for(auto& s:a.verts){PVertex v;v.p=pmxPoint(s.p);v.bone=s.bone;v.weight=s.weight;if(v.bone[0]<0){v.bone={k.pelvis,-1,-1,-1};v.weight={1,0,0,0};}vertices.push_back(v);linear.push_back(s.linear);mirrored.push_back(glm::determinant(s.linear)<0);}
 std::map<std::string,std::vector<std::pair<uint32_t,Vec3>>> morphDeltas;std::vector<std::string> morphOrder;
 for(size_t vi=0,mi=0;mi<c.meshes.size();mi++){auto& u=c.meshes[mi];if(u.kept=="dropped")continue;auto m=u.mesh;uint32_t base=uint32_t(vi);
  ImportScope meshScope("mesh \""+u.name+"\"");
  for(unsigned v=0;v<m->mNumVertices;v++){auto& out=vertices[base+v];
   Vec3 normal=m->HasNormals()?linear[base+v]*v3(m->mNormals[v]):Vec3(0);out.n=glm::length(normal)>1e-12f?glm::normalize(pmxDir(glm::normalize(normal))):Vec3(0,1,0);
   if(m->HasTextureCoords(0))out.uv=Vec2(m->mTextureCoords[0][v].x,1-m->mTextureCoords[0][v].y);
   if(!std::isfinite(out.p.x)||!std::isfinite(out.p.y)||!std::isfinite(out.p.z))importFail("character.parse","The mesh \""+u.name+"\" has non-finite vertex positions.",{{"format",loaded.format}});}
  unsigned mat=std::min(m->mMaterialIndex,loaded.scene->mNumMaterials?loaded.scene->mNumMaterials-1:0u);
  if(!materials.contains(mat)){if(loaded.scene->mNumMaterials)materials[mat]=material(loaded.scene->mMaterials[mat],mat,loaded.format,textures);else{PMaterial d;d.name="default";d.source=0;materials[mat]=d;}}
  auto& target=materials[mat];
  for(unsigned f=0;f<m->mNumFaces;f++){auto& face=m->mFaces[f];if(face.mNumIndices!=3)continue;uint32_t i0=base+face.mIndices[0],i1=base+face.mIndices[1],i2=base+face.mIndices[2];
   // The z mirror reverses the apparent winding; a mirrored node transform reverses it back.
   bool flip=int(mirrored[i0])+int(mirrored[i1])+int(mirrored[i2])>=2;
   if(flip)target.indices.insert(target.indices.end(),{i0,i1,i2});else target.indices.insert(target.indices.end(),{i0,i2,i1});}
  for(unsigned t=0;t<m->mNumAnimMeshes;t++){auto anim=m->mAnimMeshes[t];if(!anim||!anim->mVertices||anim->mNumVertices!=m->mNumVertices)continue;
   auto name=sanitizeBoneName(anim->mName.C_Str(),nullptr);if(name.empty())name=u.name+"_"+std::to_string(t);
   auto& list=morphDeltas[name];if(list.empty()&&std::find(morphOrder.begin(),morphOrder.end(),name)==morphOrder.end())morphOrder.push_back(name);
   for(unsigned v=0;v<m->mNumVertices;v++){Vec3 d=v3(anim->mVertices[v])-v3(m->mVertices[v]);if(d==Vec3(0))continue;Vec3 world=linear[base+v]*d;
    // Exporters leave float noise in untouched vertices; keep offsets above 1 micron.
    if(glm::length(world)>1e-6f)list.push_back({base+v,pmxDir(world)*scale});}}
  vi+=m->mNumVertices;}
 // ---- materials in draw order, at most 128 ----
 std::vector<PMaterial*> order;for(auto& [index,m]:materials)if(!m.indices.empty())order.push_back(&m);
 std::stable_sort(order.begin(),order.end(),[](PMaterial* a,PMaterial* b){return a->queue!=b->queue?a->queue<b->queue:a->source<b->source;});
 if(order.size()>MaxMaterials){size_t extra=order.size()-MaxMaterials;auto last=order[MaxMaterials-1];for(size_t i=MaxMaterials;i<order.size();i++)last->indices.insert(last->indices.end(),order[i]->indices.begin(),order[i]->indices.end());order.resize(MaxMaterials);
  warnings.push_back(std::to_string(extra)+" materials over the limit of 128 were merged into the last one");}
 // ---- morphs ----
 std::vector<PMorph> raw;std::map<std::string,int> rawIndex;
 for(auto& name:morphOrder){auto& list=morphDeltas[name];if(list.empty())continue;PMorph m;m.name=m.english=name;m.panel=morphPanel(name);m.type=1;m.vertex=std::move(list);rawIndex[name]=int(raw.size());raw.push_back(std::move(m));}
 std::vector<PMorph> groups;Json renames=Json::object();std::set<std::string> taken;for(auto& m:raw)taken.insert(m.name);
 for(auto& m:raw){auto lower=lowerAscii(m.name);for(auto& e:Expressions)if(lower==e.source&&!taken.contains(e.mmd)){PMorph g;g.name=e.mmd;g.english=m.name;g.panel=e.panel;g.type=0;g.group.push_back({rawIndex[m.name],1.f});groups.push_back(std::move(g));taken.insert(e.mmd);renames[m.name]=e.mmd;break;}}
 for(auto& g:groups)for(auto& [i,w]:g.group)i+=int(groups.size());
 std::vector<PMorph> morphs;for(auto* list:{&groups,&raw})for(auto& m:*list)morphs.push_back(std::move(m));
 // ---- springs ----
 auto springs=springBones(map,bones,children,a.weighted,height*correction/1.6f,warnings);
 if(report)report("Converting the character",.7f);
 // ---- PMX ----
 PmxData pmx;pmx.name=utf8(source.stem().wstring());pmx.comment="Converted from "+formatLabel(loaded.format)+" by Model Hotloader.";
 pmx.vertices=std::move(vertices);pmx.textures=textures.paths;pmx.materials.assign(order.begin(),order.end());pmx.bones=bones;pmx.morphs=std::move(morphs);
 CharacterConversion out;out.pmx=writePmx(pmx);out.textures=std::move(textures.files);
 Json boneMap=Json::object();for(auto& [key,b]:map.values)boneMap[key]=b;
 Json meshes={{"rebound",c.rebound},{"dropped",c.dropped}};
 out.conversion={{"version",1},{"generator",CharacterConverterGenerator},{"format",loaded.format},{"sourceGenerator",loaded.generator},
  {"units",{{"metersPerUnit",loaded.metersPerUnit},{"up",frame.up},{"heightMeters",std::round(height*1e3f)/1e3f},{"heightCorrection",correction}}},
  {"boneMap",boneMap},{"eyes",{{"L",map.eyeL},{"R",map.eyeR}}},{"sourceNames",sourceNames},{"morphRenames",renames},{"meshes",meshes}};
 if(!map.jiggle.is_null())out.conversion["jiggle"]=map.jiggle;
 if(!springs.is_null())out.conversion["springBone"]=springs;
 if(!c.dropped.empty()){std::string names;for(size_t i=0;i<c.dropped.size();i++)names+=(i?", ":"")+c.dropped[i];warnings.push_back(std::to_string(c.dropped.size())+" meshes use a second skeleton and were left out: "+names);}
 if(correction!=1.f){char text[160];std::snprintf(text,sizeof text,"The model was %.2f m tall, so it was resized to 1.6 m.",height);warnings.push_back(text);}
 out.warnings=std::move(warnings);
 return out;
}
}
