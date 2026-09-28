#include "rig_animation.hpp"
#include <set>
namespace mmd {
namespace {
struct Reader {
 std::span<const unsigned char> b;
 template<class T>T at(size_t p)const{if(p>b.size()||sizeof(T)>b.size()-p)throw std::runtime_error("Invalid Source animation reference offset");T out;std::memcpy(&out,b.data()+p,sizeof(out));return out;}
 std::string text(size_t p)const{if(p>=b.size())throw std::runtime_error("Invalid animation reference string");size_t end=p;while(end<b.size()&&end-p<1024&&b[end])++end;if(end==b.size()||end-p==1024)throw std::runtime_error("Unterminated animation reference string");return {reinterpret_cast<const char*>(b.data()+p),end-p};}
 btVector3 vec(size_t p)const{return {at<float>(p),at<float>(p+4),at<float>(p+8)};}
};
Json pose(const btTransform& t){auto p=t.getOrigin();auto q=t.getRotation();return {{"position",{p.x(),p.y(),p.z()}},{"rotation",{q.x(),q.y(),q.z(),q.w()}}};}
btTransform transform(const Json& j){auto p=j.at("position"),q=j.at("rotation");btQuaternion r(q.at(0),q.at(1),q.at(2),q.at(3));btVector3 v(p.at(0),p.at(1),p.at(2));for(int i=0;i<3;i++)if(!std::isfinite(v[i])||btFabs(v[i])>10000)throw std::runtime_error("Invalid reference bone position");if(!std::isfinite(r.length2())||r.length2()<.9||r.length2()>1.1)throw std::runtime_error("Invalid reference bone rotation");return btTransform(r.normalized(),v);}
btVector3 euler(const btQuaternion& q){float z,y,x;btMatrix3x3(q).getEulerZYX(z,y,x);return {x,y,z};}
}
Json readAnimationModel(std::span<const unsigned char> bytes){
 // The dedicated-server depot supplies the original v44 Citizen model.
 // Its bone/include/attachment/pose/IK metadata uses these same layouts; no
 // mesh or animation-stream data is decoded here. Retain every bounds check.
 Reader r{bytes};int version=r.at<int>(4);if(r.at<int>(0)!=0x54534449||(version!=44&&(version<46||version>49))||r.at<int>(76)!=bytes.size())throw std::runtime_error("Animation reference must be a complete Source v44 or v46-v49 model");
 int n=r.at<int>(156),base=r.at<int>(160);if(n<1||n>256||base<408)throw std::runtime_error("Invalid animation reference skeleton");
 Json out={{"sha256",hash(bytes)},{"bones",Json::array()},{"includes",Json::array()},{"poses",Json::array()},{"attachments",Json::array()},{"ikChains",Json::array()},{"ikLocks",Json::array()}};
 for(int i=0;i<n;i++){size_t p=base+i*216;auto pos=r.vec(p+32);btQuaternion q(r.at<float>(p+44),r.at<float>(p+48),r.at<float>(p+52),r.at<float>(p+56));auto j=pose(btTransform(q,pos));j["name"]=r.text(p+r.at<int>(p));j["parent"]=r.at<int>(p+4);out["bones"].push_back(j);}
 n=r.at<int>(336);base=r.at<int>(340);if(n<0||n>64)throw std::runtime_error("Invalid animation include count");for(int i=0;i<n;i++){size_t p=base+i*8;out["includes"].push_back(r.text(p+r.at<int>(p+4)));}
 n=r.at<int>(300);base=r.at<int>(304);if(n<0||n>64)throw std::runtime_error("Invalid pose parameter count");for(int i=0;i<n;i++){size_t p=base+i*20;out["poses"].push_back({{"name",r.text(p+r.at<int>(p))},{"flags",r.at<int>(p+4)},{"start",r.at<float>(p+8)},{"end",r.at<float>(p+12)},{"loop",r.at<float>(p+16)}});}
 n=r.at<int>(240);base=r.at<int>(244);if(n<0||n>256)throw std::runtime_error("Invalid attachment count");for(int i=0;i<n;i++){size_t p=base+i*92;int bone=r.at<int>(p+8);if(bone<0||bone>=int(out["bones"].size()))continue;btMatrix3x3 m;btVector3 v;for(int a=0;a<3;a++){for(int b=0;b<3;b++)m[a][b]=r.at<float>(p+12+a*16+b*4);v[a]=r.at<float>(p+24+a*16);}auto j=pose(btTransform(m,v));j["name"]=r.text(p+r.at<int>(p));j["bone"]=out["bones"][bone]["name"];out["attachments"].push_back(j);}
 n=r.at<int>(284);base=r.at<int>(288);if(n<0||n>32)throw std::runtime_error("Invalid IK chain count");
 for(int i=0;i<n;i++){
  size_t p=base+i*16;int count=r.at<int>(p+8);if(count!=3)throw std::runtime_error("Animation profile has an unsupported IK chain");
  Json links=Json::array();for(int j=0;j<count;j++){size_t link=p+r.at<int>(p+12)+j*28;int bone=r.at<int>(link);if(bone<0||bone>=int(out["bones"].size()))throw std::runtime_error("Invalid IK bone reference");auto v=r.vec(link+4);for(int k=0;k<3;k++)if(!std::isfinite(v[k]))throw std::runtime_error("Invalid IK knee direction");links.push_back({{"bone",out["bones"][bone]["name"]},{"knee",{v.x(),v.y(),v.z()}}});}
  out["ikChains"].push_back({{"name",r.text(p+r.at<int>(p))},{"type",r.at<int>(p+4)},{"links",links}});
 }
 n=r.at<int>(320);base=r.at<int>(324);if(n<0||n>32)throw std::runtime_error("Invalid IK autoplay lock count");
 for(int i=0;i<n;i++){int chain=r.at<int>(base+i*32);if(chain<0||chain>=int(out["ikChains"].size()))throw std::runtime_error("Invalid IK autoplay chain");out["ikLocks"].push_back(chain);}
 return out;
}
void configureAnimations(Rig& r,const Json& options){
 std::string role=options.value("role",std::string("ragdoll"));if(role!="ragdoll"&&role!="citizen"&&role!="combine"&&role!="player"&&role!="arms")throw std::runtime_error("Unknown actor variant");r.manifest["role"]=role;
 // A ragdoll keeps its bind and spawn pose. Given an animation reference it also
 // includes the player and Citizen animation packs of that style (below), so
 // animation tools can pose it with their sequences.
 const bool ragdoll=role=="ragdoll";
 if(ragdoll&&!options.contains("animationReference"))return;
 // Source's human/player animation REFERENCE faces -Y. Its animated idle
 // already turns that reference to the entity's +X forward. Using +X here
 // applies that turn twice and makes actors walk/fire sideways. This is
 // SCMI's reference basis; ragdolls retain their established mesh convention.
 // (Included animations place every bone, so a ragdoll's own facing does not
 // change how they look; only its Reference and spawn pose follow the bind.)
 if(!ragdoll){
  r.manifest["meshYaw"]=90;r.manifest["actorOrigin"]=role=="arms"?0.f:2.4f;
  auto facing=rigMeshBind(r);
  for(size_t i=0;i<r.bones.size();i++){r.bones[i].rest=facing*r.bones[i].rest;r.manifest["bones"][i].update(pose(r.bones[i].rest));}
 }
 auto donor=options.at("animationReference");auto gender=options.value("gender",std::string("female"));if(gender!="female"&&gender!="male")throw std::runtime_error("Unknown player animation profile");
 std::map<std::string,Json> named;for(auto& b:donor.at("bones"))named.emplace(b.at("name").get<std::string>(),b);
 Json reference=Json::array();int matched=0;
 std::vector<btTransform> donorGlobal;std::map<std::string,btTransform> globals;
 for(auto& b:donor.at("bones")){int parent=b.at("parent");if(parent>=int(donorGlobal.size()))throw std::runtime_error("Reference bones are not in parent order");auto local=transform(b);auto g=parent<0?local:donorGlobal[parent]*local;donorGlobal.push_back(g);globals.emplace(b.at("name").get<std::string>(),g);}
 if(role!="arms"&&!ragdoll){
  // The skin/physics bind must keep its fitted limb frames. Source's IK pass
  // aligns rotations to the fitted segments even in Reference; replacing the
  // bind with an animation pack's generic axes creates a second deformation.
  r.manifest["referenceYaw"]=90;
  r.manifest["jointFrameConvention"]="SCMI fitted limb frames; translation-only animation proportions";
 }
 std::vector<btTransform> armGlobal;
 for(auto& b:r.bones){auto local=b.parent<0?b.rest:r.bones[b.parent].rest.inverse()*b.rest;auto it=named.find(b.name);if(it!=named.end()){
   // Included animations resolve by bone name; mismatched parentage would
   // silently apply a transform in the wrong coordinate system.
   auto parent=it->second.at("parent").get<int>();std::string expected=parent<0?"":donor.at("bones").at(parent).at("name").get<std::string>();
   if(role!="arms"&&expected!=(b.parent<0?"":r.bones[b.parent].name))throw std::runtime_error("Animation reference parent mismatch: "+b.name);
   auto fittedRotation=local.getRotation();local=transform(it->second);
   // Animation curves provide lengths/offsets, not the skin bind orientation.
   // Like SCMI's reference/proportions SMDs, both subtraction operands share
   // the fitted frame, so the autoplay correction contains translation only.
   if(role!="arms")local.setRotation(fittedRotation);
   matched++;
  }
  if(role=="arms"){auto it=globals.find(b.name);auto g=it!=globals.end()?it->second:(b.parent<0?local:armGlobal[b.parent]*local);armGlobal.push_back(g);local=b.parent<0?g:armGlobal[b.parent].inverse()*g;}
  reference.push_back(pose(local));
 }
 if(matched<(role=="arms"?36:40))throw std::runtime_error("Animation reference lacks the ValveBiped skeleton");
 for(auto side:{"L","R"})for(auto bone:{"UpperArm","Forearm","Hand"})if(!named.contains(std::string("ValveBiped.Bip01_")+side+"_"+bone))throw std::runtime_error("Animation reference lacks an arm chain");
 // The player pack of the style is f_anm/m_anm, the only one the game ships;
 // Valve's player models include it alone. SCMI's QC also includes f_gst, f_pst,
 // f_shd and f_ss (m_*), which it lacks: Source resolves a missing include to
 // models/error.mdl and appends that model's one sequence, "idle" (unless an addon's
 // extended f_anm already has an "idle"), so a carrier gained a junk sequence, and
 // where other packs follow, indices that differ with the installed addons.
 const std::string playerPack=gender=="male"?"models/m_anm.mdl":"models/f_anm.mdl";
 Json includes=donor.value("includes",Json::array());
 if(role=="player")includes=Json::array({playerPack});
 else if(role=="arms")includes=Json::array();
 else if(ragdoll){
  // The Citizen packs of the style, then its player pack. Their skeletons are the
  // same reference (female_shared/f_anm, male_shared/m_anm) with the same IK
  // chain order, so one proportion layer and chain list serve both. Source numbers
  // included sequences in include order, and addons commonly replace or extend
  // f_anm/m_anm: last, a client's different player pack cannot shift the Citizen
  // sequences' indices from the server's. (A name both have, such as walk_all,
  // head_rot_z or reload_smg1, finds the Citizen sequence.)
  std::string citizen=gender=="male"?"models/humans/male_":"models/humans/female_";
  includes=Json::array();
  for(auto pack:{"shared","ss","gestures","postures"})includes.push_back(citizen+pack+".mdl");
  includes.push_back(playerPack);
 }
 else if(includes.empty())throw std::runtime_error("NPC profile has no animation-only include models");
 // Include animation packs, never the donor's visible/physical model. Its
 // ragdoll metadata must not participate in the fitted carrier's bone map.
 for(auto& item:includes){auto path=item.get<std::string>();if(path.size()>200||path.find("..")!=path.npos||path.find(':')!=path.npos||!path.ends_with(".mdl"))throw std::runtime_error("Unsafe animation include");}
 auto chains=donor.value("ikChains",Json::array());
 for(auto& chain:chains)for(auto& link:chain["links"]){auto name=link["bone"].get<std::string>();if(std::none_of(r.bones.begin(),r.bones.end(),[&](auto& b){return b.name==name;}))throw std::runtime_error("Unmapped animation IK bone: "+name);}
 r.manifest["animation"]={{"profile",role+"_"+gender},{"source",options.at("animationSource")},{"sourceHash",donor.at("sha256")},{"reference",reference},{"includes",includes},{"poseParameters",donor.value("poses",Json::array())},{"attachments",donor.value("attachments",Json::array())},{"ikChains",chains},{"correction","SCMI predelta autoplay"}};
 r.manifest["animation"]["ikLocks"]=donor.value("ikLocks",Json::array());
 r.manifest["animation"]["referenceSource"]=donor.value("referenceSource",options.at("animationSource"));
 r.manifest["animation"]["referenceHash"]=donor.value("referenceHash",donor.at("sha256"));
 if(role=="arms"){
  // SCMI's first-person cut starts at the forearm. Upper-arm sleeves can
  // surround the view camera when bonemerged onto the Physics Gun pose.
  // Version this derived variant without invalidating existing actor/save rigs.
  r.manifest["armsGeometryVersion"]=2;
  auto parts=options.value("armsParts",Json::object());
  // util.TableToJSON encodes an empty Lua table as []. It means automatic
  // extraction, not an indexed list of material modes.
  if(parts.is_array()&&parts.empty())parts=Json::object();
  if(!parts.is_object())throw std::runtime_error("Arms parts must map material slots to extraction modes");
  for(auto& [key,value]:parts.items()){
   if(key.empty()||key.size()>8||!std::all_of(key.begin(),key.end(),[](char c){return c>='0'&&c<='9';})||std::stoull(key)>=r.manifest["materials"].size()||!value.is_number_integer()||value.get<int>()<-1||value.get<int>()>1)throw std::runtime_error("Invalid arms material selection");
  }
  r.manifest["armsParts"]=std::move(parts);
 }
}
void writeAnimations(StudioWriter& w,const Rig& r,size_t bones,const btVector3& lo,const btVector3& hi){
 bool animated=r.manifest.contains("animation");size_t count=animated?4:3;auto animations=w.alloc(count*100),sequences=w.alloc(count*212);w.i(180,int(count));w.i(184,int(animations));w.i(188,int(count));w.i(192,int(sequences));
 std::vector<btTransform> reference,displayReference,delta;std::vector<btVector3> positionScale;
 for(size_t i=0;i<r.bones.size();i++){auto& b=r.bones[i];auto target=b.parent<0?b.rest:r.bones[b.parent].rest.inverse()*b.rest;auto ref=animated?transform(r.manifest["animation"]["reference"][i]):target;reference.push_back(ref);delta.emplace_back(target.getRotation()*ref.getRotation().inverse(),target.getOrigin()-ref.getOrigin());
  // Constant curves still use Source's normal quantized animation stream.
  w.vec(bones+i*216+72,{.001f,.001f,.001f});w.vec(bones+i*216+84,{.0001f,.0001f,.0001f});
 }
 displayReference=reference;
 // The server builds a ragdoll's physics (bodies and joints) from its first
 // sequence and applies no autoplay layer there; only clients add the
 // proportion layer. In a ragdoll carrier's first sequence the other bones keep
 // the reference that layer corrects (clients pose them from the sequence), and
 // each physics bone is placed so that, below those uncorrected parents, it
 // lands on its bind.
 std::vector<btTransform> spawn=reference;
 if(animated&&r.manifest["role"]=="ragdoll"){
  std::vector<btTransform> world(r.bones.size());
  for(size_t i=0;i<r.bones.size();i++){
   auto& b=r.bones[i];if(b.physics>=0)spawn[i]=b.parent<0?b.rest:world[b.parent].inverse()*b.rest;
   world[i]=b.parent<0?spawn[i]:world[b.parent]*spawn[i];
  }
 }
 for(size_t i=0;i<r.bones.size();i++){
  auto& b=r.bones[i];auto bind=b.parent<0?b.rest:r.bones[b.parent].rest.inverse()*b.rest;
  // A ragdoll's Reference stays its bind, as before it had animations.
  if(animated&&r.manifest["role"]!="arms"&&r.manifest["role"]!="ragdoll"&&b.parent<0){
   // Included idles turn their animation reference by a quarter turn. Our
   // explicit tool reference must already face the entity's +X direction.
   btTransform turn(btQuaternion(btVector3(0,0,1),SIMD_HALF_PI),btVector3(0,0,0));auto desired=turn*bind;
   displayReference[i]=btTransform(delta[i].getRotation().inverse()*desired.getRotation(),desired.getOrigin()-delta[i].getOrigin());
  }
  auto maximum=delta[i].getOrigin().absolute();maximum.setMax((displayReference[i].getOrigin()-bind.getOrigin()).absolute());maximum.setMax((spawn[i].getOrigin()-bind.getOrigin()).absolute());
  btVector3 scale;for(int j=0;j<3;j++)scale[j]=std::max(.001f,maximum[j]/32000.f);positionScale.push_back(scale);w.vec(bones+i*216+72,scale);
 }
 const char* names[]={"ragdoll","Reference","Referencef","proportions"};
 for(size_t index=0;index<count;index++){
  bool correction=index==3;int flags=correction?(4|8|0x400):0;size_t a=animations+index*100,s=sequences+index*212;w.i(a,-int(a));w.relstr(a+4,a,names[index]);w.f(a+8,30);w.i(a+12,correction?4:0);w.i(a+16,1);
  size_t first=0,previous=0;
  for(size_t i=0;i<r.bones.size();i++){
   auto& b=r.bones[i];auto bind=b.parent<0?b.rest:r.bones[b.parent].rest.inverse()*b.rest;
   const btTransform ref=(index==1||index==2)?displayReference[i]:index==0?spawn[i]:reference[i];
   auto pos=correction?delta[i].getOrigin():ref.getOrigin()-bind.getOrigin();
   auto angles=correction?euler(delta[i].getRotation()):euler(ref.getRotation())-euler(bind.getRotation());
   for(int j=0;j<3;j++){while(angles[j]>SIMD_PI)angles[j]-=SIMD_2_PI;while(angles[j]<-SIMD_PI)angles[j]+=SIMD_2_PI;}
   // Scales belong to the owning model's bone, and must fit both curves.
   auto ps=positionScale[i];
   auto p=w.alloc(40);if(!first)first=p;if(previous)w.put<int16_t>(previous+2,int16_t(p-previous));previous=p;w.b[p]=uint8_t(i);w.b[p+1]=4|8|(correction?16:0);
   for(int j=0;j<6;j++){size_t ptr=p+4+(j/3)*6;size_t values=p+16+j*4;w.put<int16_t>(ptr+(j%3)*2,int16_t(values-ptr));w.b[values]=1;w.b[values+1]=1;float scale=j<3?.0001f:ps[j-3];float value=j<3?angles[j]:pos[j-3];w.put<int16_t>(values+2,int16_t(std::clamp(std::lround(value/scale),-32767l,32767l)));}
  }
  auto terminator=w.alloc(4);if(previous)w.put<int16_t>(previous+2,int16_t(terminator-previous));w.b[terminator]=255;w.i(a+56,int(first-a));
  w.i(s,-int(s));w.relstr(s+4,s,names[index]);w.relstr(s+8,s,index==0?"ACT_DIERAGDOLL":"");w.i(s+12,flags);w.i(s+16,-1);w.i(s+20,index==0?1:0);w.vec(s+32,lo);w.vec(s+44,hi);w.i(s+56,1);w.i(s+68,1);w.i(s+72,1);w.i(s+76,-1);w.i(s+80,-1);auto blend=w.alloc(4);w.put<int16_t>(blend,int16_t(index));w.i(s+60,int(blend-s));auto weights=w.alloc(r.bones.size()*4);w.i(s+156,int(weights-s));for(size_t i=0;i<r.bones.size();i++)w.f(weights+i*4,1);
 }
 if(animated&&r.manifest["role"]!="arms"){
  // The engine's included animation path needs the profile's IK chains even
  // for an FK Reference pose. Missing chains suppress proportion autoplay;
  // an incomplete chain list is unsafe because included IK rules use indices.
  auto chains=r.manifest["animation"].value("ikChains",Json::array());auto base=w.alloc(chains.size()*16);w.i(284,int(chains.size()));w.i(288,int(base));
  for(size_t i=0;i<chains.size();i++){auto& chain=chains[i];auto p=base+i*16;auto links=w.alloc(chain["links"].size()*28);w.relstr(p,p,chain["name"].get<std::string>());w.i(p+4,chain["type"]);w.i(p+8,int(chain["links"].size()));w.i(p+12,int(links-p));
   for(size_t j=0;j<chain["links"].size();j++){auto& link=chain["links"][j];auto it=std::find_if(r.bones.begin(),r.bones.end(),[&](auto& b){return b.name==link["bone"];});if(it==r.bones.end())throw std::runtime_error("Missing IK chain bone");w.i(links+j*28,int(it-r.bones.begin()));auto knee=link["knee"];w.vec(links+j*28+4,{knee[0],knee[1],knee[2]});}
  }
  // Root locks override inherited locks by chain index. Stock locks otherwise
  // pin the feet at donor proportions before our autoplay correction. Keep
  // ordinary per-sequence locomotion IK, but preserve the corrected endpoints
  // and local orientations during this autoplay-only pass.
  auto locks=r.manifest["animation"].value("ikLocks",Json::array());auto lockBase=w.alloc(locks.size()*32);w.i(320,int(locks.size()));w.i(324,int(lockBase));
  for(size_t i=0;i<locks.size();i++){int chain=locks[i];if(chain<0||chain>=int(chains.size()))throw std::runtime_error("Invalid autoplay lock chain");w.i(lockBase+i*32,chain);w.f(lockBase+i*32+4,0);w.f(lockBase+i*32+8,1);}
 }
 if(animated){auto& info=r.manifest["animation"];auto includes=w.alloc(info["includes"].size()*8);w.i(336,int(info["includes"].size()));w.i(340,int(includes));for(size_t i=0;i<info["includes"].size();i++){auto p=includes+i*8;w.relstr(p,p,"");w.relstr(p+4,p,info["includes"][i].get<std::string>());}auto poses=w.alloc(info["poseParameters"].size()*20);w.i(300,int(info["poseParameters"].size()));w.i(304,int(poses));for(size_t i=0;i<info["poseParameters"].size();i++){auto p=poses+i*20;auto& item=info["poseParameters"][i];w.relstr(p,p,item["name"].get<std::string>());w.i(p+4,item["flags"]);w.f(p+8,item["start"]);w.f(p+12,item["end"]);w.f(p+16,item["loop"]);}}
}
}
