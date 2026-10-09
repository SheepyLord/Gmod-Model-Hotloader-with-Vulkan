#include "rig.hpp"
#include "physics_profile.hpp"
#include "rig_animation.hpp"
#include "rig_geometry.hpp"
#include "scmi_data.hpp"
#include "mmd_names.hpp"
#include "fitter.hpp"
#include "spring_bones.hpp"
#include <BulletCollision/NarrowPhaseCollision/btGjkPairDetector.h>
#include <BulletCollision/NarrowPhaseCollision/btGjkEpaPenetrationDepthSolver.h>
#include <BulletCollision/NarrowPhaseCollision/btPointCollector.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
namespace mmd {
namespace {
Json xyz(const btVector3& v){return {v.x(),v.y(),v.z()};}
btVector3 v3(const Json& a){return {a.at(0).get<float>(),a.at(1).get<float>(),a.at(2).get<float>()};}
Json transform(const btTransform& t){auto q=t.getRotation();return {{"position",xyz(t.getOrigin())},{"rotation",{q.x(),q.y(),q.z(),q.w()}}};}
std::string ascii(std::string s){for(char& c:s)if((unsigned char)c>=128)c=' ';else c=char(std::tolower((unsigned char)c));return s;}
std::string normalized(std::string s){s=ascii(s);s.erase(std::remove_if(s.begin(),s.end(),[](char c){return c==' '||c=='_'||c=='-';}),s.end());return s;}
int findBone(const Model& m,const std::vector<std::string>& names){for(auto& name:names)for(size_t i=0;i<m.bones.size();i++)if(m.bones[i].name==name||m.bones[i].english==name)return int(i);for(auto& name:names){auto n=normalized(name);if(n.empty())continue;for(size_t i=0;i<m.bones.size();i++)if(normalized(m.bones[i].name)==n||normalized(m.bones[i].english)==n)return int(i);}return -1;}
float percentile(std::vector<float> a,float p){if(a.empty())return 0;size_t k=size_t(p*(a.size()-1));std::nth_element(a.begin(),a.begin()+k,a.end());return a[k];}
using Writer=StudioWriter;
uint32_t crcUpdate(uint32_t c,const unsigned char* p,size_t n){
 static const auto table=[]{std::array<uint32_t,256> t{};for(uint32_t i=0;i<256;i++){uint32_t v=i;for(int k=0;k<8;k++)v=(v>>1)^((0u-(v&1))&0xedb88320u);t[i]=v;}return t;}();
 for(size_t i=0;i<n;i++)c=table[(c^p[i])&255]^(c>>8);return c;
}
uint32_t crc(const Bytes& b){return ~crcUpdate(~0u,b.data(),b.size());}
struct GmaItem{std::string name;uint64_t size;uint32_t crc;};
// GMAD version 3: header, file table (ending in index 0), file data, then a zero CRC.
Bytes gmaHeader(const std::vector<GmaItem>& items,const std::string& title){
 Writer g;g.b={'G','M','A','D',3};g.b.resize(21);g.str("");g.str(title);g.str("{\"type\":\"model\",\"tags\":[]}");g.str("Model Hotloader");size_t p=g.b.size();g.b.resize(p+4);g.i(p,1);int id=0;
 for(auto& item:items){p=g.b.size();g.b.resize(p+4);g.i(p,++id);g.str(item.name);p=g.b.size();g.b.resize(p+12);g.put<uint64_t>(p,item.size);g.put<uint32_t>(p+8,item.crc);}
 g.b.resize(g.b.size()+4);return g.b;
}
}
float resolveSourceScale(const Json& options,float height){
 int modes=int(options.contains("scaleMultiplier"))+int(options.contains("height"))+int(options.contains("scale"));
 if(modes>1)throw std::runtime_error("Choose only one of scaleMultiplier, height, or legacy scale");
 float value=ScmiSourceUnitsPerPmx;
 if(options.contains("scaleMultiplier"))value*=options.at("scaleMultiplier").get<float>();
 else if(options.contains("scale"))value=options.at("scale").get<float>()/Inch;
 else if(options.contains("height"))value=options.at("height").get<float>()/std::max(height,.001f);
 if(!std::isfinite(value)||value<.001f||value>10000)throw std::runtime_error("Invalid model scale");
 return value;
}
static void identify(Rig& r){
 r.manifest.erase("key");r.manifest.erase("model");r.manifest.erase("gma");auto encoded=r.manifest.dump();r.key=hash(std::span(reinterpret_cast<const unsigned char*>(encoded.data()),encoded.size())).substr(0,32);r.path="models/mmd/"+r.key.substr(0,16)+"/"+readableName(r.manifest.value("name",std::string("model")),28)+".mdl";r.manifest["key"]=r.key;r.manifest["model"]=r.path;
}
Rig rigFromManifest(const Json& j){
 if(j.value("version",0)!=RigVersion||j.value("generator",0)!=RigGenerator||j.value("shapeAtlasHash","")!=shapeAtlasHash())throw std::runtime_error("Incompatible carrier fit");
 Rig r;r.manifest=j;r.scale=j.at("scale");r.mass=j.at("mass");r.morphs=j.at("morphs");
 for(auto& item:j.at("bones")){RigBone b;b.name=item.at("name");b.parent=item.at("parent");b.mmd=item.at("mmd");b.physics=item.at("physics");b.aliases=item.value("mmdAliases",std::vector<int>{});auto q=item.at("rotation");b.rest=btTransform(btQuaternion(q[0],q[1],q[2],q[3]),v3(item.at("position")));r.bones.push_back(b);}
 for(auto& item:j.at("bodies")){RigBody b;b.bone=item.at("bone");b.parent=item.at("parent");b.confidence=item.at("confidence");b.massBias=item.at("massBias");b.rotationDamping=item.at("rotationDamping");b.lower=v3(item.at("lower"));b.upper=v3(item.at("upper"));for(auto& v:item.at("hull"))b.hull.push_back(v3(v));
   // Physics fields of an edited carrier; unedited manifests keep the 2.2 defaults.
   b.friction=v3(item.value("friction",Json::array({0,0,0})));b.damping=item.value("damping",.8f);b.inertia=item.value("inertia",12.f);b.drag=item.value("drag",-1.f);b.surfaceprop=item.value("surfaceprop",std::string("flesh"));b.style=item.value("style",std::string("fitted"));r.bodies.push_back(b);}
 r.physics=j.value("physicsOverrides",Json::object());
 if(r.bodies.size()!=18||r.bones.size()<56||r.bones.size()>58)throw std::runtime_error("Invalid cached anatomy");
 // Lua JSON numbers round differently. A mounted immutable carrier retains its
 // published identity; rehashing its decoded floats produces a phantom rig.
 r.key=j.value("key",std::string());r.path=j.value("model",std::string());
 if(r.key.size()!=32||r.key.find_first_not_of("0123456789abcdef")!=r.key.npos||!r.path.starts_with("models/mmd/"+r.key.substr(0,16)+"/")||r.path.find("..")!=r.path.npos||!r.path.ends_with(".mdl"))throw std::runtime_error("Invalid carrier identity");return r;
}
// A manifest may come from another peer, a save or a cached fit. Instances index
// the model's bone arrays, the 18 Source bodies and earlier rig bones directly
// with these values, so every one is checked against this model first.
void validateRig(const Rig& r,const Model& m){
 auto reject=[](const char* what){throw std::runtime_error(std::string("Invalid carrier fit: ")+what);};
 auto finite=[](const btVector3& v){return std::isfinite(v.x())&&std::isfinite(v.y())&&std::isfinite(v.z());};
 const int modelBones=int(m.bones.size()),bodies=int(r.bodies.size());
 if(bodies!=18||r.bones.size()<56||r.bones.size()>58)reject("anatomy size");
 if(!std::isfinite(r.scale)||r.scale<.001f||r.scale>10000||!std::isfinite(r.mass))reject("scale");
 for(int i=0;i<int(r.bones.size());i++){
  const auto& b=r.bones[i];
  if(b.parent<-1||b.parent>=i)reject("bone parent");
  if(b.mmd<-1||b.mmd>=modelBones)reject("model bone");
  for(int alias:b.aliases)if(alias<0||alias>=modelBones)reject("model bone alias");
  if(b.physics<-1||b.physics>=bodies)reject("bone body");
  const auto& basis=b.rest.getBasis();
  if(!finite(b.rest.getOrigin())||!finite(basis[0])||!finite(basis[1])||!finite(basis[2]))reject("bone transform");
 }
 if(r.bones.front().mmd<0)reject("root bone");
 for(int k=0;k<bodies;k++){
  const auto& body=r.bodies[k];
  if(body.bone<0||body.bone>=int(r.bones.size())||body.parent<-1||body.parent>=bodies)reject("body");
  if(!finite(body.lower)||!finite(body.upper))reject("body limits");
  // Clients only render: physicsOverrides itself is never a reason to reject.
  if(!finite(body.friction)||body.friction.x()<0||body.friction.y()<0||body.friction.z()<0)reject("body friction");
  if(!std::isfinite(body.damping)||body.damping<0||!std::isfinite(body.inertia)||body.inertia<0||!(body.drag==-1.f||(std::isfinite(body.drag)&&body.drag>=0)))reject("body physics");
  if(!validSurfaceprop(body.surfaceprop))reject("body surfaceprop");
  for(const auto& v:body.hull)if(!finite(v))reject("body hull");
 }
 for(auto key:{"meshYaw","actorOrigin"}){auto it=r.manifest.find(key);if(it!=r.manifest.end()&&(!it->is_number()||!(std::abs(it->get<double>())<1e6)))reject("mesh bind");}
}
void prepareModelFit(Model& model,const fs::path& cache){
 if(model.fittedRig)return;
 auto path=cache/L"fits"/wide("g"+std::to_string(RigGenerator)+"-"+shapeAtlasHash().substr(0,16)+"-"+model.id+".json");
 try{auto stored=readJson(path);auto text=stored.at("fit").dump();if(stored.at("sha256")==hash(std::span(reinterpret_cast<const unsigned char*>(text.data()),text.size()))&&stored["fit"]["asset"]==model.id){auto rig=rigFromManifest(stored["fit"]);validateRig(rig,model);model.fittedRig=std::make_shared<Rig>(std::move(rig));return;}}catch(const std::exception&){}
 try{auto rig=fitRig(model,Json::object());auto text=rig.manifest.dump();writeJson(path,{{"fit",rig.manifest},{"sha256",hash(std::span(reinterpret_cast<const unsigned char*>(text.data()),text.size()))}});model.fittedRig=std::make_shared<Rig>(std::move(rig));}catch(const std::exception& e){model.warnings.push_back(std::string("Native fit unavailable: ")+e.what());}
}
static Rig scaledFit(const Model& m,const Json& options,const Json& physics){
 Rig r=*m.fittedRig;float target=resolveSourceScale(options,m.maximum.y()-m.minimum.y()),factor=target/r.scale;r.scale=target;r.mass=options.value("mass",70.f);
 if(!std::isfinite(r.mass)||r.mass<1||r.mass>1000)throw std::runtime_error("Invalid carrier mass");
 for(size_t i=0;i<r.bones.size();i++){r.bones[i].rest.getOrigin()*=factor;r.manifest["bones"][i]["position"]=xyz(r.bones[i].rest.getOrigin());}
 auto overrides=options.value("collisionOverrides",Json::object());
 for(size_t i=0;i<r.bodies.size();i++){auto& body=r.manifest["bodies"][i];auto before=v3(body["center"])*factor,extent=v3(body["extent"])*factor,center=before,changedExtent=extent;auto name=body["name"].get<std::string>();
  std::string style="fitted";
  if(overrides.contains(name)){auto& o=overrides[name];float correctionScale=r.scale/options.value("collisionOverrideScale",r.scale);if(o.contains("center"))center=v3(o["center"])*correctionScale;if(o.contains("extent"))changedExtent=v3(o["extent"])*correctionScale;style=shapeStyle(o);}
  for(int k=0;k<3;k++)if(!std::isfinite(center[k])||!std::isfinite(changedExtent[k])||changedExtent[k]<=.001f||changedExtent[k]>1000||btFabs(center[k])>1000)throw std::runtime_error("Invalid collision correction");
  // A box or capsule replaces the fitted hull inside the same centre and half size.
  ConvexFit corrected;if(style=="fitted")for(auto v:r.bodies[i].hull)corrected.vertices.push_back(center+(v*factor-before)/extent*changedExtent);else corrected.vertices=primitiveHull(style,center,changedExtent);
  convexTopology(corrected);if(style!="fitted"&&corrected.fallback)throw std::runtime_error("Invalid collision override: style");
  r.bodies[i].style=style;if(style!="fitted")body["style"]=style;r.bodies[i].hull=corrected.vertices;body["hull"]=Json::array();for(auto v:corrected.vertices)body["hull"].push_back(xyz(v));body["faces"]=corrected.faces;body["center"]=xyz(corrected.center);body["extent"]=xyz(corrected.extent);body["topologyRepaired"]=body.value("topologyRepaired",false)||corrected.repaired;if(corrected.fallback){body["topologyFallback"]=true;body["needsReview"]=true;body["confidence"]=0;r.bodies[i].confidence=0;}
 }
 r.manifest["eyesAttachment"]["position"]=xyz(v3(r.manifest["eyesAttachment"]["position"])*factor);r.manifest["mass"]=r.mass;r.manifest["scale"]=r.scale;r.manifest["sourceUnitsPerPmx"]=r.scale;r.manifest["scaleMultiplier"]=r.scale/ScmiSourceUnitsPerPmx;r.manifest["maxInitialPenetration"]=r.manifest.value("maxInitialPenetration",0.f)*factor;applyPhysics(r,physics);configureAnimations(r,options);identify(r);return r;
}
Rig fitRig(const Model& m,const Json& options){
 // c_arms have no physics bodies of their own; every other role carries the profile.
 const Json physics=options.value("role",std::string("ragdoll"))=="arms"?Json::object():requireCanonicalPhysics(options.value("physicsOverrides",Json::object()));
 if(m.fittedRig&&!options.contains("height")&&options.value("excludedMaterials",Json::array()).empty())return scaledFit(m,options,physics);
 const auto data=Json::parse(ScmiData);Rig r;float height=m.maximum.y()-m.minimum.y();
 if(height<=0)throw std::runtime_error("Model has no height");r.scale=resolveSourceScale(options,height);r.mass=options.value("mass",70.f);
 if(!std::isfinite(r.scale)||r.scale<.001f||r.scale>10000||!std::isfinite(r.mass)||r.mass<1||r.mass>1000)throw std::runtime_error("Invalid carrier scale/mass");
 std::vector<btTransform> reference;std::map<std::string,int> indices;
 const std::map<std::string,std::vector<std::string>> aliases={
 {"Pelvis",{"下半身","lower body","hips"}},{"Spine1",{"上半身","upper body","spine"}},{"Spine4",{"上半身3","上半身３","upper body3","UpperBody3","上半身2","上半身２","upper body2","chest"}},
 {"Neck1",{"首","neck"}},{"Head1",{"頭","head"}},
 {"Clavicle",{"肩","shoulder"}},{"UpperArm",{"腕","arm"}},{"Forearm",{"ひじ","肘","elbow"}},{"Hand",{"手首","wrist"}},
 {"Thigh",{"足","leg"}},{"Calf",{"ひざ","膝","knee"}},{"Foot",{"足首","ankle"}},{"Toe0",{"つま先","足先EX","toe"}}};
 // SCMI's exported SMDs have lateral +X / vertical +Z. Our renderer maps
 // PMX +X to Source -Y and +Y to +Z. Convert once at the reference root:
 // otherwise a calf's local Z hinge points along the face.
 const btTransform smdToSource(btQuaternion(btVector3(0,0,1),-SIMD_HALF_PI),btVector3(0,0,0));
 for(auto& j:data["bones"]){RigBone b;b.name=j["name"];b.parent=j["parent"];auto ang=v3(j["angles"]);btQuaternion q;q.setEulerZYX(ang.z(),ang.y(),ang.x());btTransform t(q,v3(j["position"]));t=b.parent>=0?reference[b.parent]*t:smdToSource*t;reference.push_back(t);b.rest=t;
   std::string suffix=b.name.substr(b.name.find_last_of('.')+1);if(suffix.starts_with("Bip01_"))suffix=suffix.substr(6);
   std::string side,jp;if(suffix.starts_with("L_")||suffix.starts_with("R_")){side=suffix.substr(0,1);jp=side=="L"?"左":"右";suffix=suffix.substr(2);}
   std::vector<std::string> names{b.name};if(aliases.contains(suffix))for(auto s:aliases.at(suffix)){if(side.empty())names.push_back(s);else {names.push_back(jp+s);names.push_back(s+"_"+ascii(side));names.push_back((side=="L"?"left ":"right ")+s);}}
   if(suffix.starts_with("Finger")){int digit=suffix[6]-'0',segment=suffix.size()>7?suffix[7]-'0':0;const char* fingers[]={"親指","人指","中指","薬指","小指"};const char* eng[]={"thumb","index","middle","ring","little"};int start=digit==0&&findBone(m,{jp+"親指０",jp+"親指0"})>=0?0:1;int n=start+segment;const char* full[]={"０","１","２","３"};names.insert(names.end(),{jp+fingers[digit]+std::to_string(n),jp+fingers[digit]+full[n],jp+(digit==1?std::string("人差指"):std::string(fingers[digit]))+full[n],std::string(eng[digit])+std::to_string(segment+1)+"_"+ascii(side)});}
   b.mmd=findBone(m,names);if(b.mmd>=0)b.rest.setOrigin(toSource(m.bones[b.mmd].position)*r.scale);indices[b.name]=int(r.bones.size());r.bones.push_back(b);
 }
 auto index=[&](std::string name){return indices.at("ValveBiped.Bip01_"+name);};
 // Models with a third upper-body segment need all three native spine drivers.
 // Leaving the middle/upper segment implicit lets authored helper/physics
 // bones treat animated torso rotation as secondary motion.
 if(findBone(m,{"上半身3","上半身３","upper body3","UpperBody3"})>=0){
  auto& middle=r.bones[index("Spine2")];middle.mmd=findBone(m,{"上半身2","上半身２","upper body2","UpperBody2"});
  if(middle.mmd>=0)middle.rest.setOrigin(toSource(m.bones[middle.mmd].position)*r.scale);
 }
 // PMX D bones are deform duplicates of the animation skeleton. Keep both
 // original indices controlled by the same Source bone instead of fitting an
 // empty leg and then letting Bullet overwrite its actual weighted D chain.
 for(auto& b:r.bones)if(b.mmd>=0)for(size_t i=0;i<m.bones.size();i++)if(int(i)!=b.mmd){
  auto& candidate=m.bones[i];if((candidate.position-m.bones[b.mmd].position).length()>.002f*height)continue;
  int parent=int(i);std::set<int> seen;while(parent>=0&&seen.insert(parent).second){auto& current=m.bones[parent];if(!current.inheritRotation||std::abs(current.coefficient-1.f)>.001f)break;parent=current.inherit;if(parent==b.mmd){b.aliases.push_back(int(i));break;}}
 }
 // Reject unsupported rigs before generating an engine asset, rather than manufacturing a humanoid.
 for(auto name:{"Pelvis","Spine1","Head1","L_UpperArm","R_UpperArm","L_Forearm","R_Forearm","L_Hand","R_Hand","L_Thigh","R_Thigh","L_Calf","R_Calf","L_Foot","R_Foot"})if(r.bones[index(name)].mmd<0)throw std::runtime_error(std::string("Missing humanoid landmark: ")+name);
 auto& pelvis=r.bones[index("Pelvis")];auto& chest=r.bones[index("Spine4")];auto& neck=r.bones[index("Neck1")];auto& head=r.bones[index("Head1")];
 if(neck.mmd<0)neck.rest.setOrigin(head.rest.getOrigin().lerp(r.bones[index("Spine1")].rest.getOrigin(),.25f));
 if(chest.mmd<0)chest.rest.setOrigin(r.bones[index("Spine1")].rest.getOrigin().lerp(neck.rest.getOrigin(),.55f));
 r.bones[index("Spine")].rest.setOrigin(pelvis.rest.getOrigin().lerp(r.bones[index("Spine1")].rest.getOrigin(),.5f));
 if(r.bones[index("Spine2")].mmd<0)r.bones[index("Spine2")].rest.setOrigin(r.bones[index("Spine1")].rest.getOrigin().lerp(chest.rest.getOrigin(),.5f));
 for(auto side:{"L_","R_"}){auto i=index(std::string(side)+"Clavicle");if(r.bones[i].mmd<0)r.bones[i].rest.setOrigin(chest.rest.getOrigin());}
 float refHeight=reference[index("Head1")].getOrigin().z();float ratio=(head.rest.getOrigin().z()-m.minimum.y()*r.scale)/std::max(1.f,refHeight);
 for(size_t i=0;i<r.bones.size();i++){auto& b=r.bones[i];if(b.mmd<0&&b.parent>=0&&b.name.find("Spine")==std::string::npos&&b.name.find("Neck")==std::string::npos&&b.name.find("Clavicle")==std::string::npos)b.rest.setOrigin(r.bones[b.parent].rest.getOrigin()+(reference[i].getOrigin()-reference[b.parent].getOrigin())*ratio);}
 // Like SCMI's proportion trick, track local X down each limb and retain its
 // roll. Explicit successors avoid choosing a thumb or accessory as the axis.
 // The fitted pose is also the carrier bind pose: no autoplay delta is needed.
 std::map<int,int> successors;
 auto track=[&](int i,int child){auto x=r.bones[child].rest.getOrigin()-r.bones[i].rest.getOrigin();if(x.length2()<1e-8f)return;x.normalize();auto z=reference[i].getBasis().getColumn(2);z-=x*x.dot(z);if(z.length2()<1e-6f)z=x.cross(reference[i].getBasis().getColumn(1));z.normalize();auto y=z.cross(x).normalized();z=x.cross(y);r.bones[i].rest.setBasis(btMatrix3x3(x.x(),y.x(),z.x(),x.y(),y.y(),z.y(),x.z(),y.z(),z.z()));successors[i]=child;};
 for(auto side:{"L_","R_"}){
   for(auto pair:{std::pair{"Thigh","Calf"},{"Calf","Foot"},{"UpperArm","Forearm"},{"Forearm","Hand"}})track(index(std::string(side)+pair.first),index(std::string(side)+pair.second));
   for(int digit=0;digit<5;digit++){auto name=std::string(side)+"Finger"+std::to_string(digit);track(index(name),index(name+"1"));track(index(name+"1"),index(name+"2"));}
 }
 const char* physical[]={"Pelvis","Spine1","Spine4","Head1","L_Clavicle","L_UpperArm","L_Forearm","L_Hand","R_Clavicle","R_UpperArm","R_Forearm","R_Hand","L_Thigh","L_Calf","L_Foot","R_Thigh","R_Calf","R_Foot"};
 for(auto name:physical){RigBody b;b.bone=index(name);r.bones[b.bone].physics=int(r.bodies.size());r.bodies.push_back(b);}
 std::vector<int> mmdBody(m.bones.size(),-1),direct(m.bones.size(),-1);for(size_t i=0;i<r.bones.size();i++){if(r.bones[i].mmd>=0)direct[r.bones[i].mmd]=int(i);for(int alias:r.bones[i].aliases)direct[alias]=int(i);}
 std::set<int> secondary;for(auto b:m.bodies)if(nanoemModelRigidBodyGetTransformType(b)!=0){int i=boneIndex(nanoemModelRigidBodyGetBoneObject(b));if(i>=0)secondary.insert(i);}
 // VRM spring bones are secondary chains too: hair must not widen the head body.
 if(m.springs)for(auto& j:m.springs->joints)secondary.insert(j.bone);
 for(size_t i=0;i<m.bones.size();i++){int at=int(i);std::set<int> visited;while(at>=0&&visited.insert(at).second){if(direct[at]>=0){int si=direct[at];while(si>=0&&r.bones[si].physics<0)si=r.bones[si].parent;if(si>=0)mmdBody[i]=r.bones[si].physics;break;}auto n=m.bones[at].name;bool twist=n.find("捩")!=std::string::npos||ascii(m.bones[at].english).find("twist")!=std::string::npos;if(secondary.contains(at)&&!twist)break;at=m.bones[at].parent;}}
 std::vector<std::vector<btVector3>> clouds(18);std::vector<std::array<float,18>> weights(m.vertices.size());std::array<float,18> maxWeight{};std::vector<int> materialOf(m.vertices.size(),-1);
 std::set<int> excluded;for(auto value:options.value("excludedMaterials",Json::array()))excluded.insert(value.get<int>());
 for(size_t mi=0;mi<m.materials.size();mi++){auto& mat=m.materials[mi];if(mat.alpha<.01f||excluded.contains(int(mi)))continue;for(unsigned j=mat.first;j<mat.first+mat.count;j++)materialOf[m.indices[j]]=int(mi);}
 for(size_t vi=0;vi<m.vertices.size();vi++){auto& v=m.vertices[vi];if(materialOf[vi]<0)continue;for(int k=0;k<4;k++)if(v.bones[k]>=0&&mmdBody[v.bones[k]]>=0)weights[vi][mmdBody[v.bones[k]]]+=v.weights[k];for(int i=0;i<18;i++)maxWeight[i]=std::max(maxWeight[i],weights[vi][i]);}
 Json regions=Json::array();for(int i=0;i<18;i++)regions.push_back(Json::object());
 // SCMI selects half of each region's maximum summed influence. A blended
 // boundary may belong to both neighboring bodies; dominant ownership loses it.
 for(int i=0;i<18;i++){
  std::vector<int> components(m.vertices.size());std::iota(components.begin(),components.end(),0);std::vector<bool> selected(m.vertices.size());
  for(size_t vi=0;vi<m.vertices.size();vi++)selected[vi]=materialOf[vi]>=0&&maxWeight[i]>0&&weights[vi][i]>=maxWeight[i]*.5f;
  auto root=[&](int a){while(components[a]!=a){components[a]=components[components[a]];a=components[a];}return a;};
  for(size_t j=0;j+2<m.indices.size();j+=3){bool include=false;for(int k=0;k<3;k++)include=include||selected[m.indices[j+k]];if(!include)continue;for(int k=0;k<3;k++){int a=m.indices[j+k],b=m.indices[j+(k+1)%3];if(materialOf[a]>=0&&materialOf[b]>=0)components[root(a)]=root(b);}}
  std::vector<int> sizes(m.vertices.size());int largest=0;for(size_t vi=0;vi<m.vertices.size();vi++)if(selected[vi])largest=std::max(largest,++sizes[root(int(vi))]);
  for(size_t vi=0;vi<m.vertices.size();vi++)if(selected[vi]&&sizes[root(int(vi))]>=std::min(24,std::max(3,largest/100))){clouds[i].push_back(r.bones[r.bodies[i].bone].rest.inverse()*(toSource(m.vertices[vi].position)*r.scale));auto key=std::to_string(materialOf[vi]);regions[i][key]=regions[i].value(key,0)+1;}
 }
 // Size the anatomical body, excluding remote expression meshes, hair and props.
 // Head/foot estimates are bounded by the skeleton even if painted weights are bad.
 float fittedHeight=height;
 if(options.contains("height")){
  float headZ=head.rest.getOrigin().z(),footZ=(r.bones[index("L_Foot")].rest.getOrigin().z()+r.bones[index("R_Foot")].rest.getOrigin().z())*.5f;
  float span=std::max(.01f,headZ-footZ);std::vector<float> topPoints,bottomPoints;
  for(auto v:clouds[3])topPoints.push_back((head.rest*v).z());
  for(int i:{14,17})for(auto v:clouds[i])bottomPoints.push_back((r.bones[r.bodies[i].bone].rest*v).z());
  float top=std::clamp(topPoints.empty()?headZ+span*.13f:percentile(topPoints,.98f),headZ+span*.04f,headZ+span*.25f);
  float bottom=std::clamp(bottomPoints.empty()?footZ-span*.04f:percentile(bottomPoints,.02f),footZ-span*.08f,footZ-span*.005f);
  fittedHeight=(top-bottom)/r.scale;float factor=options.value("height",72.f)/(top-bottom);
  r.scale*=factor;for(auto& b:r.bones)b.rest.getOrigin()*=factor;for(auto& cloud:clouds)for(auto& v:cloud)v*=factor;
 }
 Json bodies=Json::array(),bones=Json::array();float massTotal=0;
 for(size_t i=0;i<r.bodies.size();i++){auto& b=r.bodies[i];auto& bone=r.bones[b.bone];int p=bone.parent;while(p>=0&&r.bones[p].physics<0)p=r.bones[p].parent;b.parent=p<0?-1:r.bones[p].physics;
   for(auto line:data["limits"].at(bone.name)){std::istringstream in(line.get<std::string>());std::string command,ignored;in>>command>>ignored;if(command=="$jointmassbias")in>>b.massBias;else if(command=="$jointrotdamping")in>>b.rotationDamping;else if(command=="$jointconstrain"){char axis;in>>axis>>ignored;int k=axis-'x';float friction;in>>b.lower[k]>>b.upper[k]>>friction;}}
   massTotal+=b.massBias;auto& points=clouds[i];float stature=(head.rest.getOrigin()-(r.bones[index("L_Foot")].rest.getOrigin()+r.bones[index("R_Foot")].rest.getOrigin())*.5f).length();
   float length=successors.contains(b.bone)?(r.bones[successors.at(b.bone)].rest.getOrigin()-bone.rest.getOrigin()).length():0;
   auto fitted=fitBody(bone.name,points,stature,length);auto center=fitted.center,extent=fitted.extent;float unit=stature/60.f;
   auto overrides=options.value("collisionOverrides",Json::object());if(overrides.contains(bone.name)){auto o=overrides.at(bone.name);float correctionScale=r.scale/options.value("collisionOverrideScale",r.scale);if(o.contains("center"))center=v3(o["center"])*correctionScale;if(o.contains("extent"))extent=v3(o["extent"])*correctionScale;for(int k=0;k<3;k++)if(!std::isfinite(center[k])||btFabs(center[k])>72*unit||!std::isfinite(extent[k])||extent[k]<.01f*unit||extent[k]>36*unit)throw std::runtime_error("Invalid collision override");b.style=shapeStyle(o);}
   auto faces=fitted.faces;
   if(b.style=="fitted")for(auto v:fitted.vertices)b.hull.push_back(center+(v-fitted.center)/fitted.extent*extent);
   else{ConvexFit primitive;primitive.vertices=primitiveHull(b.style,center,extent);convexTopology(primitive);if(primitive.fallback)throw std::runtime_error("Invalid collision override: style");b.hull=primitive.vertices;faces=primitive.faces;center=primitive.center;extent=primitive.extent;}
   b.confidence=fitted.fallback?0:fitted.confidence;
   Json vertices=Json::array();for(auto v:b.hull)vertices.push_back(xyz(v));bodies.push_back({{"bone",b.bone},{"parent",b.parent},{"name",bone.name},{"hull",vertices},{"faces",faces},{"center",xyz(center)},{"extent",xyz(extent)},{"confidence",b.confidence},{"coverage",fitted.coverage},{"outlierFraction",fitted.outliers},{"method",fitted.method},{"needsReview",b.confidence<.7f||fitted.fallback},{"topologyRepaired",fitted.repaired},{"topologyFallback",fitted.fallback},{"regions",regions[i]},{"features",fitted.features},{"samples",points.size()},{"lower",xyz(b.lower)},{"upper",xyz(b.upper)},{"massBias",b.massBias},{"rotationDamping",b.rotationDamping}});if(b.style!="fitted")bodies.back()["style"]=b.style;

 }
 // Only pairs that collide under the profile are separated, and a box or capsule keeps the size the player chose.
 float maxPenetration=0;int overlapAdjustments=0;std::set<std::pair<int,int>> colliding;for(auto pair:enabledPairs(physics))colliding.insert(pair);
 for(int iteration=0;iteration<5;iteration++){
  std::vector<std::unique_ptr<btConvexHullShape>> shapes;for(auto& body:r.bodies){auto shape=std::make_unique<btConvexHullShape>();shape->setMargin(0);for(auto v:body.hull)shape->addPoint(v,false);shape->recalcLocalAabb();shapes.push_back(std::move(shape));}
  std::set<int> shrink;maxPenetration=0;
  for(int a=0;a<18;a++)for(int b=a+1;b<18;b++){if(r.bodies[a].parent==b||r.bodies[b].parent==a||!colliding.contains({a,b}))continue;
   btVoronoiSimplexSolver simplex;btGjkEpaPenetrationDepthSolver epa;btGjkPairDetector detector(shapes[a].get(),shapes[b].get(),&simplex,&epa);btDiscreteCollisionDetectorInterface::ClosestPointInput query;query.m_transformA=r.bones[r.bodies[a].bone].rest;query.m_transformB=r.bones[r.bodies[b].bone].rest;btPointCollector result;detector.getClosestPoints(query,result,nullptr);
   if(result.m_hasResult&&result.m_distance<0){maxPenetration=std::max(maxPenetration,-result.m_distance);if(result.m_distance<-.12f*r.scale/ScmiSourceUnitsPerPmx)for(int i:{a,b})if(r.bodies[i].style=="fitted")shrink.insert(i);}
  }
  if(shrink.empty()||iteration==4)break;
  for(int i:shrink){auto center=v3(bodies[i]["center"]);for(auto& v:r.bodies[i].hull)v=center+(v-center)*.94f;auto extent=v3(bodies[i]["extent"])*.94f;bodies[i]["extent"]=xyz(extent);bodies[i]["hull"]=Json::array();for(auto v:r.bodies[i].hull)bodies[i]["hull"].push_back(xyz(v));bodies[i]["overlapAdjusted"]=true;overlapAdjustments++;}
 }
 // Appended indices leave every existing primary/finger index unchanged.
 for(int side=0;side<2;side++){
  int eye=findBone(m,side==0?std::vector<std::string>{"左目","Eye_L","eye_l","left eye"}:std::vector<std::string>{"右目","Eye_R","eye_r","right eye"});
  if(eye<0)continue;RigBone bone;bone.name=side==0?"Eye_L":"Eye_R";bone.parent=6;bone.mmd=eye;
  // SCMI eye axes: local X up, Y back, Z character-left.
  btMatrix3x3 axes(0,1,0,0,0,1,1,0,0);bone.rest=btTransform(axes,toSource(m.bones[eye].position)*r.scale);r.bones.push_back(bone);
 }
 for(auto& b:r.bones){auto j=transform(b.rest);j.update({{"name",b.name},{"parent",b.parent},{"mmd",b.mmd},{"mmdAliases",b.aliases},{"physics",b.physics},{"provenance",b.mmd>=0?"PMX":"synthesized"}});bones.push_back(j);}
 // IDs are assigned once, from the original order; overflow cannot renumber native controls.
 std::set<std::string> used;std::vector<Json> morphs;for(size_t i=0;i<m.morphNames.size();i++){auto original=m.morphNames[i];std::string name=englishMorph(original,data["flexNames"]);bool recognized=!name.empty();std::string display=recognized?name:original;if(display.empty())display=m.text(nanoemModelMorphGetName(m.morphs[i],NANOEM_LANGUAGE_TYPE_ENGLISH));if(name.empty()){auto e=m.text(nanoemModelMorphGetName(m.morphs[i],NANOEM_LANGUAGE_TYPE_ENGLISH));name=normalized(e);if(name.empty())name="morph_"+std::to_string(i);}
   for(char& c:name)if(!std::isalnum((unsigned char)c)&&c!='_')c='_';
   if(used.contains(name)){
    auto base=name+"_"+std::to_string(i);name=base;size_t suffix=1;
    // An authored name can already contain our index suffix. Keep searching
    // so native controllers and name-keyed face presets never alias it.
    while(used.contains(name))name=base+"_"+std::to_string(suffix++);
   }
   used.insert(name);morphs.push_back({{"mmd",i},{"name",name},{"original",original},{"displayName",display.empty()?name:display},{"namingSource",recognized?"scmi":"authored"},{"native",-1}});}
 std::vector<int> priority(morphs.size());std::iota(priority.begin(),priority.end(),0);auto rank=[&](int i){auto s=morphs[i]["name"].get<std::string>();return (s=="blink"||s=="mouth_a"||s=="mouth_i"||s=="mouth_u"||s=="mouth_e"||s=="mouth_o")?0:(s.starts_with("eye")||s.starts_with("brow")||s.starts_with("mouth"))?1:2;};std::stable_sort(priority.begin(),priority.end(),[&](int a,int b){return rank(a)<rank(b);});std::set<int> assigned;int nextController=0;for(int i:priority){if(assigned.contains(i))continue;std::vector<int> group{i};auto n=morphs[i]["name"].get<std::string>();std::string pair;if(n.ends_with("_left"))pair=n.substr(0,n.size()-5)+"_right";else if(n.ends_with("_right"))pair=n.substr(0,n.size()-6)+"_left";if(!pair.empty())for(size_t j=0;j<morphs.size();j++)if(morphs[j]["name"]==pair&&!assigned.contains(int(j)))group.push_back(int(j));if(nextController+group.size()>96)continue;for(int j:group){morphs[j]["native"]=nextController++;assigned.insert(j);}}r.morphs=morphs;
 auto eyePosition=r.bones[6].rest.getOrigin();int leftEye=findBone(m,{"左目","eye_l","left eye"}),rightEye=findBone(m,{"右目","eye_r","right eye"});
 if(leftEye>=0&&rightEye>=0)eyePosition=toSource((m.bones[leftEye].position+m.bones[rightEye].position)*.5f)*r.scale;
 auto eyes=transform(r.bones[6].rest.inverse()*btTransform(btQuaternion(btVector3(0,0,1),SIMD_PI),eyePosition));
 r.manifest={{"version",RigVersion},{"generator",RigGenerator},{"sourceUnitsPerPmx",r.scale},{"scaleMultiplier",r.scale/ScmiSourceUnitsPerPmx},{"skeletonPositions","PMX landmarks"},{"jointFrameConvention","SCMI SMD to renderer, -90deg Z; tracked X, preserved Z roll"},{"calibrationVersion",2},{"shapeAtlasHash",shapeAtlasHash()},{"maxInitialPenetration",maxPenetration},{"overlapAdjustments",overlapAdjustments},{"asset",m.id},{"name",m.name},{"scale",r.scale},{"fittedHeightMMD",fittedHeight},{"mass",r.mass},{"massBiasTotal",massTotal},{"bones",bones},{"bodies",bodies},{"morphs",r.morphs},{"eyesAttachment",eyes},{"referenceSha256",data["referenceSha256"]},{"excludedMaterials",options.value("excludedMaterials",Json::array())},{"materialCount",m.materials.size()},{"nativeFlexCount",nextController}};
 r.manifest["materials"]=Json::array();
 for(size_t i=0;i<m.materials.size();i++)r.manifest["materials"].push_back({{"slot",i},{"bodygroup",i+1},{"name",m.materials[i].name},{"authoredAlpha",m.materials[i].alpha},{"defaultHidden",m.materials[i].alpha<=0},{"path",materialPath(m.id,i,m.materials[i].name)}});
 r.manifest["nativeBodygroups"]=std::min<size_t>(31,m.materials.size());
 r.manifest["materialGma"]="data/mmd_hotloader/assets/"+m.id+"/materials-v5.gma";
 applyPhysics(r,physics);configureAnimations(r,options);identify(r);return r;
}
std::map<std::string,Bytes> carrierFiles(const Rig& r,const Model* armsModel){
 Writer w;w.alloc(408);int checksum=int(std::stoul(r.key.substr(0,8),nullptr,16));w.i(0,0x54534449);w.i(4,48);w.i(8,checksum);w.fixed(12,64,r.path.substr(7));
 btVector3 lo(1e6f,1e6f,1e6f),hi(-1e6f,-1e6f,-1e6f);for(auto& b:r.bodies)for(auto v:b.hull){auto p=r.bones[b.bone].rest*v;lo.setMin(p);hi.setMax(p);}w.vec(80,r.bones[6].rest.getOrigin());w.vec(92,(lo+hi)*.5f);for(int p:{104,128})w.vec(p,lo);for(int p:{116,140})w.vec(p,hi);w.i(152,0);w.f(328,r.mass);w.i(332,1);w.b[378]=1;
 size_t bones=w.alloc(r.bones.size()*216);w.i(156,int(r.bones.size()));w.i(160,int(bones));
 // Surface materials: a bone takes its own body's, else its nearest physical ancestor's, else the model's.
 std::string modelSurface=r.physics.is_object()&&r.physics.contains("surfaceprop")&&r.physics["surfaceprop"].is_string()?r.physics["surfaceprop"].get<std::string>():"flesh";
 auto surface=[&](int i){while(i>=0&&r.bones[i].physics<0)i=r.bones[i].parent;return i>=0?r.bodies[r.bones[i].physics].surfaceprop:modelSurface;};
 for(size_t i=0;i<r.bones.size();i++){auto& b=r.bones[i];size_t p=bones+i*216;auto local=b.parent<0?b.rest:r.bones[b.parent].rest.inverse()*b.rest;auto q=local.getRotation();w.relstr(p,p,b.name);w.i(p+4,b.parent);for(int j=0;j<6;j++)w.i(p+8+j*4,-1);w.vec(p+32,local.getOrigin());for(int j=0;j<4;j++)w.f(p+44+j*4,q[j]);float z,y,x;local.getBasis().getEulerZYX(z,y,x);w.vec(p+60,{x,y,z});w.vec(p+72,{1,1,1});w.vec(p+84,{1,1,1});w.matrix(p+96,b.rest.inverse());w.f(p+156,1);w.i(p+160,0x7ff00|(b.physics>=0?1:0));w.i(p+172,b.physics);w.relstr(p+176,p,surface(int(i)));w.i(p+180,1);}
 auto sorted=w.alloc(r.bones.size());std::vector<int> order(r.bones.size());std::iota(order.begin(),order.end(),0);std::sort(order.begin(),order.end(),[&](int a,int b){return ascii(r.bones[a].name)<ascii(r.bones[b].name);});for(size_t i=0;i<order.size();i++)w.b[sorted+i]=(unsigned char)order[i];w.i(364,int(sorted));
 writeAnimations(w,r,bones,lo,hi);
 int toggles=std::min(31,r.manifest["materialCount"].get<int>()),parts=toggles+1;
 auto bp=w.alloc(parts*16);w.i(232,parts);w.i(236,int(bp));
 for(int i=0;i<parts;i++){
  auto p=bp+i*16;int count=i==0?1:2;auto model=w.alloc(count*148);
  bool hidden=i>0&&r.manifest["materials"][i-1].value("defaultHidden",false);
  std::string name=i==0?"carrier":std::string(hidden?"Show ":"Hide ")+r.manifest["materials"][i-1]["name"].get<std::string>();
  w.relstr(p,p,name);w.i(p+4,count);w.i(p+8,i==0?1:1<<(i-1));w.i(p+12,int(model-p));
  for(int j=0;j<count;j++){w.fixed(model+j*148,64,(j==0)!=hidden?"visible":"hidden");w.f(model+j*148+68,(hi-lo).length());}
 }
 size_t nf=r.manifest.at("nativeFlexCount").get<size_t>();auto fc=w.alloc(nf*20),fd=w.alloc(nf*4);w.i(268,int(nf));w.i(272,int(fc));w.i(260,int(nf));w.i(264,int(fd));for(auto& j:r.morphs){int i=j["native"];if(i<0)continue;auto p=fc+i*20;w.relstr(p,p,"MMD");w.relstr(p+4,p,j["name"].get<std::string>());w.i(p+8,-1);w.f(p+16,1);w.relstr(fd+i*4,fd+i*4,j["name"].get<std::string>());}
 auto hs=w.alloc(12),hb=w.alloc(r.bodies.size()*68);w.i(172,1);w.i(176,int(hs));w.relstr(hs,hs,"default");w.i(hs+4,int(r.bodies.size()));w.i(hs+8,int(hb-hs));for(size_t i=0;i<r.bodies.size();i++){auto& b=r.bodies[i];btVector3 a(1e6f,1e6f,1e6f),z(-1e6f,-1e6f,-1e6f);for(auto v:b.hull){a.setMin(v);z.setMax(v);}size_t p=hb+i*68;w.i(p,b.bone);w.i(p+4,i==3?1:0);w.vec(p+8,a);w.vec(p+20,z);}
 struct Attachment{std::string name;int bone;btTransform pose;};std::vector<Attachment> attachments;
 auto eye=r.manifest.at("eyesAttachment"),eq=eye.at("rotation");attachments.push_back({"eyes",6,btTransform(btQuaternion(eq[0],eq[1],eq[2],eq[3]),v3(eye.at("position")))});
 if(r.manifest.contains("animation"))for(auto& item:r.manifest["animation"]["attachments"]){auto name=item.at("name").get<std::string>();if(name=="eyes")continue;for(size_t i=0;i<r.bones.size();i++)if(r.bones[i].name==item.at("bone")){auto q=item.at("rotation");attachments.push_back({name,int(i),btTransform(btQuaternion(q[0],q[1],q[2],q[3]),v3(item.at("position")))});break;}}
 for(auto side:{"LH","RH"}){auto name=std::string("anim_attachment_")+side;if(std::none_of(attachments.begin(),attachments.end(),[&](auto& a){return a.name==name;}))attachments.push_back({name,side==std::string("LH")?12:32,btTransform::getIdentity()});}
 auto at=w.alloc(attachments.size()*92);w.i(240,int(attachments.size()));w.i(244,int(at));for(size_t i=0;i<attachments.size();i++){size_t p=at+i*92;auto& a=attachments[i];w.relstr(p,p,a.name);w.i(p+8,a.bone);w.matrix(p+12,a.pose);}
 int mats=std::clamp(r.manifest["materialCount"].get<int>(),1,128);auto tx=w.alloc(mats*64);w.i(204,mats);w.i(208,int(tx));for(int i=0;i<mats;i++)w.relstr(tx+i*64,tx+i*64,r.manifest["materials"].empty()?"mmdhl/carrier":r.manifest["materials"][i]["path"].get<std::string>());auto cd=w.alloc(4);w.i(212,1);w.i(216,int(cd));w.i(cd,int(w.str("")));auto sk=w.alloc(mats*2);w.i(220,mats);w.i(224,1);w.i(228,int(sk));for(int i=0;i<mats;i++)w.put<uint16_t>(sk+i*2,uint16_t(i));w.relstr(308,0,modelSurface);w.i(76,int(w.b.size()));
 Writer vvd;vvd.alloc(64);vvd.i(0,0x56534449);vvd.i(4,4);vvd.i(8,checksum);vvd.i(12,1);vvd.i(52,64);vvd.i(56,64);vvd.i(60,64);
 Writer vtx;vtx.alloc(44);vtx.i(0,7);vtx.i(4,32);vtx.put<uint16_t>(8,53);vtx.put<uint16_t>(10,9);vtx.i(12,3);vtx.i(16,checksum);vtx.i(20,1);vtx.i(24,36);vtx.i(28,parts);
 auto vb=vtx.alloc(parts*8);vtx.i(32,int(vb));
 for(int i=0;i<parts;i++){auto p=vb+i*8;int count=i==0?1:2;auto models=vtx.alloc(count*8);vtx.i(p,count);vtx.i(p+4,int(models-p));for(int j=0;j<count;j++){auto m=models+j*8,lod=vtx.alloc(12);vtx.i(m,1);vtx.i(m+4,int(lod-m));}}
 if(r.manifest.value("role",std::string("ragdoll"))=="arms"){if(!armsModel)throw std::runtime_error("Arms model geometry is missing");writeArmsGeometry(w,vvd,vtx,r,*armsModel);}
 std::string stem=r.path.substr(0,r.path.size()-4);return {{r.path,std::move(w.b)},{stem+".vvd",std::move(vvd.b)},{stem+".dx90.vtx",std::move(vtx.b)}};
}
Bytes makeGma(const std::map<std::string,Bytes>& files,const std::string& title){
 std::vector<GmaItem> items;size_t total=0;for(auto& [name,data]:files){items.push_back({name,data.size(),crc(data)});total+=data.size();}
 auto b=gmaHeader(items,title);b.reserve(b.size()+total+4);for(auto& [name,data]:files)b.insert(b.end(),data.begin(),data.end());b.resize(b.size()+4);return b;
}
void writeGma(const fs::path& path,const std::map<std::string,GmaEntry>& files,const std::string& title){
 std::vector<unsigned char> buffer(4u<<20);
 auto stream=[&](const fs::path& file,const std::function<void(const unsigned char*,size_t)>& use){
  std::ifstream in(ioPath(file),std::ios::binary);if(!in)throw std::runtime_error("Cannot read "+utf8(file.wstring()));uint64_t size=0;
  while(in.read(reinterpret_cast<char*>(buffer.data()),std::streamsize(buffer.size()))||in.gcount()>0){auto n=size_t(in.gcount());use(buffer.data(),n);size+=n;}
  if(in.bad())throw std::runtime_error("Cannot read "+utf8(file.wstring()));return size;};
 std::vector<GmaItem> items;
 for(auto& [name,entry]:files){
  if(entry.file.empty()){items.push_back({name,entry.data.size(),crc(entry.data)});continue;}
  uint32_t c=~0u;auto size=stream(entry.file,[&](const unsigned char* p,size_t n){c=crcUpdate(c,p,n);});items.push_back({name,size,~c});
 }
 auto header=gmaHeader(items,title);
 writeAtomic(path,[&](std::ostream& out){
  out.write(reinterpret_cast<const char*>(header.data()),std::streamsize(header.size()));size_t k=0;
  for(auto& [name,entry]:files){auto& item=items[k++];
   if(entry.file.empty()){out.write(reinterpret_cast<const char*>(entry.data.data()),std::streamsize(entry.data.size()));continue;}
   uint32_t c=~0u;auto size=stream(entry.file,[&](const unsigned char* p,size_t n){c=crcUpdate(c,p,n);out.write(reinterpret_cast<const char*>(p),std::streamsize(n));});
   if(size!=item.size||~c!=item.crc)throw std::runtime_error("A file changed while its package was written: "+name);
  }
  const char end[4]{};out.write(end,4);
 });
}
Json packageCarrier(const fs::path& cache,const Rig& r,Bytes physics){registerShortName(cache,"rigs",r.key);retainCacheFiles(cache,{fs::path(L"rigs")/wide(r.key)});auto arms=r.manifest.value("role",std::string("ragdoll"))=="arms"?loadAsset(cache,r.manifest.at("asset").get<std::string>()):std::shared_ptr<Model>{};auto files=carrierFiles(r,arms.get());files[r.path.substr(0,r.path.size()-4)+".phy"]=std::move(physics);
 auto bytes=makeGma(files,"Model Hotloader carrier "+r.key);
 auto dir=cache/L"rigs"/wide(r.key);fs::create_directories(dir);auto package=dir/L"carrier.gma";writeAtomic(package,bytes);writeJson(dir/L"rig.json",r.manifest);auto result=r.manifest;result["gma"]="data/mmd_hotloader/rigs/"+r.key+"/carrier.gma";return result;
}
}
