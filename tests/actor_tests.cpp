#include "rig_animation.hpp"
#include "rig_geometry.hpp"
#include "sharing.hpp"
#include "scene_share.hpp"
#include <algorithm>
#include <iostream>
#include <set>
using namespace mmd;
template<class T>T read(const Bytes& b,size_t p){if(p+sizeof(T)>b.size())throw std::runtime_error("Generated offset out of range");T v;std::memcpy(&v,b.data()+p,sizeof(v));return v;}
btVector3 vectorAt(const Bytes& b,size_t p){return {read<float>(b,p),read<float>(b,p+4),read<float>(b,p+8)};}
std::pair<btVector3,btQuaternion> decode(const Bytes& b,size_t bone,size_t stream,bool delta){
 btVector3 p(0,0,0),angles(0,0,0);
 for(int k=0;k<6;k++){size_t ptr=stream+4+(k/3)*6;size_t values=ptr+read<int16_t>(b,ptr+(k%3)*2);float scale=read<float>(b,bone+(k<3?84:72)+(k%3)*4);float value=read<int16_t>(b,values+2)*scale;if(k<3)angles[k]=value;else p[k-3]=value;}
 if(!delta){p+=vectorAt(b,bone+32);angles+=vectorAt(b,bone+60);}btMatrix3x3 m;m.setEulerZYX(angles.x(),angles.y(),angles.z());btQuaternion q;m.getRotation(q);return {p,q};
}
int main(int argc,char** argv){try{
 // Automatic c_arms exclude upper-arm sleeves near the view camera while
 // retaining forearms/hands. Explicit material choices still override it.
 {
  Model sample;sample.bones.resize(3);sample.vertices.resize(9);sample.indices={0,1,2,3,4,5,6,7,8};sample.materials.resize(1);sample.materials[0].count=9;
  Rig rig;rig.bones.resize(3);const char* names[]={"ValveBiped.Bip01_R_UpperArm","ValveBiped.Bip01_R_Forearm","ValveBiped.Bip01_R_Hand"};
  for(int i=0;i<3;i++){rig.bones[i].name=names[i];rig.bones[i].mmd=i;for(int j=0;j<3;j++){sample.vertices[i*3+j].bones[0]=i;sample.vertices[i*3+j].weights[0]=1;}}
  rig.manifest={{"armsParts",Json::object()},{"materials",Json::array({{{"defaultHidden",false}}})}};
  if(firstPersonTriangles(sample,rig,true)!=std::vector<uint8_t>{0,1,1})throw std::runtime_error("First-person mesh includes an upper-arm sleeve");
  rig.manifest["armsParts"]={{"0",1}};
  if(firstPersonTriangles(sample,rig,true)!=std::vector<uint8_t>{1,1,1})throw std::runtime_error("Explicit arms selection was lost");
 }
 // Exercise paths beyond MAX_PATH, including the temporary commit suffix.
 // This runs inside a host without requiring its manifest to enable long paths.
 auto temporary=fs::absolute(fs::temp_directory_path()/wide("mmdhl-sharing-"+std::to_string(GetCurrentProcessId())));
 struct Cleanup{fs::path root;~Cleanup(){std::error_code error;fs::remove_all(root,error);}} cleanup{temporary};
 auto deep=temporary;for(int i=0;i<5;i++)deep/=std::string(64,'a'+i);
 auto longFile=deep/"manifest.json";writeJson(longFile,{{"longPath",true}});
 if(!readJson(longFile).value("longPath",false))throw std::runtime_error("Long shared-cache path failed");
 writeJson(longFile,{{"replacement",true}});
 if(!readJson(longFile).value("replacement",false))throw std::runtime_error("Long shared-cache replacement failed");
 if(!fs::is_regular_file(ioPath(longFile))||fs::file_size(ioPath(longFile))==0)throw std::runtime_error("Long cache metadata query failed");
 if(argc!=2)throw std::runtime_error("Expected PMX fixture");auto model=parse(readFile(argv[1]));auto rag=fitRig(*model,Json::object());auto files=carrierFiles(rag);auto reference=readAnimationModel(files.at(rag.path));
 // The torso follows the PMX hierarchy (issue #9). Every variant of the fixture's upper
 // body keeps its pivots, drives each PMX bone once and runs up the body.
 {
  const int upper=rag.bones[2].mmd,head=rag.bones[6].mmd;
  auto named=[](const Model& m,const std::string& name){for(size_t i=0;i<m.bones.size();i++)if(m.bones[i].name==name)return int(i);return -1;};
  // Adds a bone under parent, or moves the bone of that name; hang() reparents bones.
  auto add=[&](Model& m,const std::string& name,const btVector3& position,int parent){
   if(int i=named(m,name);i>=0){m.bones[i].position=position;return i;}
   auto b=m.bones[upper];b.name=name;b.english="";b.position=position;b.parent=parent;b.inherit=-1;
   m.bones.push_back(b);return int(m.bones.size()-1);
  };
  auto hang=[&](Model& m,std::initializer_list<const char*> names,int parent){for(auto n:names)m.bones[named(m,n)].parent=parent;};
  auto fit=[&](const Model& m,const std::string& what){auto r=fitRig(m,Json::object());
   if(r.bodies.size()!=18)throw std::runtime_error(what+" changed the native body count");
   std::multiset<int> used;for(auto& b:r.bones){if(b.mmd>=0)used.insert(b.mmd);used.insert(b.aliases.begin(),b.aliases.end());}
   for(int b:used)if(used.count(b)>1)throw std::runtime_error(what+" drives a PMX bone from two carrier bones");
   for(auto& b:r.bones)if(b.physics>=0&&b.mmd>=0&&(b.rest.getOrigin()-toSource(m.bones[b.mmd].position)*r.scale).length()>1e-4f)throw std::runtime_error(what+" moved a carrier pivot off its PMX bone");
   float z[6];for(int i=0;i<6;i++)z[i]=r.bones[i].rest.getOrigin().z();// Pelvis, Spine, Spine1, Spine2, Spine4, Neck1
   if(!(z[0]<=z[1]&&z[1]<=z[2]&&z[2]<=z[3]&&z[3]<z[4]&&z[4]<z[5]))throw std::runtime_error(what+" does not run Pelvis <= Spine <= Spine1 <= Spine2 < Spine4 < Neck1");
   return r;};
  auto lo=model->bones[upper].position,hi=model->bones[head].position;
  fit(*model,"The fixture torso");
  {auto m=parse(readFile(argv[1]));int middle=add(*m,"上半身2",lo.lerp(hi,.35f),upper),chest=add(*m,"上半身3",lo.lerp(hi,.65f),middle);hang(*m,{"neck","left shoulder","right shoulder"},chest);
   auto three=fit(*m,"A three-segment torso");if(three.bones[3].mmd!=middle||three.bones[4].mmd!=chest)throw std::runtime_error("Three-segment PMX torso lost a native spine driver");}
  // Built 上半身 > 上半身3 > 上半身2 > neck: the chest is the bone holding the neck and shoulders.
  {auto m=parse(readFile(argv[1]));int third=add(*m,"上半身3",lo.lerp(hi,.35f),upper),second=add(*m,"上半身2",lo.lerp(hi,.65f),third);hang(*m,{"neck","left shoulder","right shoulder"},second);
   auto inverted=fit(*m,"An inverted-name torso");if(inverted.bones[3].mmd!=third||inverted.bones[4].mmd!=second)throw std::runtime_error("Inverted torso names put Spine2 above Spine4");}
  // 右腕捩3 folds to "3" like 上半身3: no loose match may double the chest.
  {auto m=parse(readFile(argv[1]));int arm=named(*m,"right arm");add(*m,"右腕捩3",m->bones[arm].position,arm);
   auto loose=fit(*m,"A twist bone named ...3");if(loose.bones[3].mmd!=-1||loose.bones[4].mmd!=named(*m,"upper body2"))throw std::runtime_error("A loosely matched name doubled the chest");}
  {auto m=parse(readFile(argv[1]));add(*m,"上半身3",lo.lerp(hi,.2f),upper);
   auto leaf=fit(*m,"A leaf 上半身3");if(leaf.bones[3].mmd!=-1||leaf.bones[4].mmd!=named(*m,"upper body2"))throw std::runtime_error("A leaf 上半身3 became a spine driver");}
  // A chest above the neck or below Spine1 moves with a synthesized chest; its pivot is not moved.
  for(float y:{16.5f,9.8f}){auto m=parse(readFile(argv[1]));int chest=named(*m,"upper body2");m->bones[chest].position.setY(y);
   auto band=fit(*m,"An out-of-band chest");if(band.bones[4].mmd!=-1||band.bones[4].aliases!=std::vector<int>{chest})throw std::runtime_error("An out-of-band chest drives Spine4 from its own pivot");}
  // A collapsed torso with a hair bone named like a third segment: the chest is synthesized.
  {auto m=parse(readFile(argv[1]));hang(*m,{"neck","left shoulder","right shoulder"},upper);auto& old=m->bones[named(*m,"upper body2")];old.name=old.english="x";
   int hair=add(*m,"後髪3",hi+btVector3(0,.5f,-.5f),head);auto collapsed=fit(*m,"A collapsed torso");
   if(collapsed.bones[4].mmd==hair||std::count(collapsed.bones[4].aliases.begin(),collapsed.bones[4].aliases.end(),hair))throw std::runtime_error("A hair bone became the chest");}
  {auto m=parse(readFile(argv[1]));hang(*m,{"neck"},upper);
   auto neck=fit(*m,"A neck on Spine1");if(neck.bones[4].mmd!=named(*m,"upper body2"))throw std::runtime_error("A neck hanging from Spine1 lost the chest that holds the shoulders");}
 }
 auto oldHeader=files.at(rag.path);int legacyVersion=44;std::memcpy(oldHeader.data()+4,&legacyVersion,4);
 auto legacy=readAnimationModel(oldHeader);legacy["sha256"]=reference["sha256"];
 if(legacy!=reference)throw std::runtime_error("Dedicated v44 metadata differs");
 auto cache=ioPath(temporary/std::string(90,'c'));auto identity=std::string(64,'a');
 auto relative="assets/"+identity+"/materials-v5.gma";
 auto archive=makeGma({{"materials/mmd/test.vmt",{'x'}}},"path test");writeAtomic(cache/wide(relative),archive);
 writeJson(cache/L"assets"/wide(identity)/L"manifest.json",{{"textures",Json::array()}});
 auto mount=mountablePackage(cache,relative);auto alias=ioPath(cache/wide(mount.substr(std::string("data/mmd_hotloader/").size())));
 if(!fs::equivalent(ioPath(cache/wide(relative)),alias)||readFile(alias)!=archive)throw std::runtime_error("Mount alias changed shared bytes");
 writeAtomic(cache/wide(relative),archive); // A verified transfer can replace the same immutable bytes.
 if(mountablePackage(cache,relative)!=mount)throw std::runtime_error("Identical replacement invalidated mount alias");
 // Multi-block compression includes both incompressible and repeated data,
 // preserves exact model bytes, rejects corruption and remains deletable.
 Bytes payload(3*SharedBlockSize+91);uint32_t random=0x98765432;
 for(size_t i=0;i<payload.size();++i){random^=random<<13;random^=random>>17;random^=random<<5;payload[i]=i<SharedBlockSize?uint8_t(random):uint8_t(i%17);}
 auto sourceRelative="assets/"+identity+"/model.bin";writeAtomic(sharedPath(cache,sourceRelative),payload);auto digest=hash(payload);
 auto packetPath=packSharedFile(cache,sourceRelative,payload.size(),digest);auto packet=readFile(packetPath);
 if(packet.size()>=payload.size()||unpackSharedFile(packet,payload.size(),digest)!=payload)throw std::runtime_error("Lossless shared packet round trip failed");
 if(packSharedFile(cache,sourceRelative,payload.size(),digest)!=packetPath)throw std::runtime_error("Shared packet cache was not reused");
 {auto verifiedAt=fs::last_write_time(packetPath);auto damaged=readFile(packetPath);damaged[damaged.size()/2]^=0x5a;writeAtomic(packetPath,damaged);fs::last_write_time(packetPath,verifiedAt);auto repaired=readFile(packSharedFile(cache,sourceRelative,payload.size(),digest));if(unpackSharedFile(repaired,payload.size(),digest)!=payload)throw std::runtime_error("A damaged shared sidecar behind a valid header was reused");}
 auto rejectPacket=[&](Bytes bad,uint64_t size,const std::string& h){bool failed=false;try{unpackSharedFile(bad,size,h);}catch(...){failed=true;}if(!failed)throw std::runtime_error("Malformed shared packet accepted");};
 auto bad=packet;bad.back()^=1;rejectPacket(bad,payload.size(),digest);
 bad=packet;bad.pop_back();rejectPacket(bad,payload.size(),digest);
 bad=packet;bad.push_back(0);rejectPacket(bad,payload.size(),digest);
 bad=packet;uint32_t giant=SharedBlockSize+1;std::memcpy(bad.data()+80,&giant,4);rejectPacket(bad,payload.size(),digest);
 rejectPacket(packet,payload.size()+1,digest);rejectPacket(packet,payload.size(),std::string(64,'0'));
 Bytes empty;writeAtomic(sharedPath(cache,sourceRelative),empty);auto zero=readFile(packSharedFile(cache,sourceRelative,0,hash(empty)));
 if(zero.size()!=80||!unpackSharedFile(zero,0,hash(empty)).empty())throw std::runtime_error("Empty shared packet failed");
 deleteAssets(cache,{identity});if(fs::exists(alias)||fs::exists(packetPath))throw std::runtime_error("Deleting an asset retained a mount or transfer cache");
 // Included rules address IK chains by index, so preserve complete chain
 // ordering while remapping every link by bone name (never by donor index).
 reference["ikChains"]=Json::array();
 for(auto side:{"R","L"})for(auto limb:{"hand","foot"}){
  Json links=Json::array();auto segments=std::string(limb)=="hand"?std::array{"UpperArm","Forearm","Hand"}:std::array{"Thigh","Calf","Foot"};
  for(auto segment:segments)links.push_back({{"bone",std::string("ValveBiped.Bip01_")+side+"_"+segment},{"knee",{0,-1,0}}});
  reference["ikChains"].push_back({{"name",std::string(side)+limb},{"type",0},{"links",links}});
 }
 reference["ikLocks"]={1,3};
 reference["includes"]={"models/animation_fixture.mdl"};
 for(auto role:{"citizen","combine","player","arms"}){
  Json options={{"role",role},{"gender","female"},{"animationSource","models/reference.mdl"},{"animationReference",reference}};
  if(std::string(role)=="arms")options["armsParts"]=Json::array();
  auto actor=fitRig(*model,options);if(actor.bodies.size()!=18||actor.bones.size()!=rag.bones.size())throw std::runtime_error("Actor changed physics/bone indices");
  if((std::string(role)=="citizen"||std::string(role)=="combine")&&actor.manifest["animation"]["includes"]!=reference["includes"])throw std::runtime_error("NPC included the donor mesh/physics instead of its animation packs");
  // Only the player pack the game ships: a missing include adds error.mdl's "idle".
  if(std::string(role)=="player"&&actor.manifest["animation"]["includes"]!=Json::array({"models/f_anm.mdl"}))throw std::runtime_error("Player model includes packs the game lacks");
  if(std::string(role)=="arms"&&!actor.manifest["armsParts"].is_object())throw std::runtime_error("Empty GLua arms selection was not normalized");
  if(actor.key==rag.key)throw std::runtime_error("Actor cache identities alias");
  // Arms fixture explicitly includes its tiny mesh to exercise VVD/VTX writing.
  if(std::string(role)=="arms")actor.manifest["armsParts"]={{"0",1}};
  auto package=carrierFiles(actor,std::string(role)=="arms"?model.get():nullptr);auto& mdl=package.at(actor.path);
  auto count=read<int>(mdl,188);if(count!=(std::string(role)=="arms"?3:4))throw std::runtime_error("Missing reference/proportion sequences");
  auto bones=read<int>(mdl,160),anims=read<int>(mdl,184);
  if(std::string(role)!="arms"){
   auto parsed=readAnimationModel(mdl);if(parsed["ikChains"]!=reference["ikChains"])throw std::runtime_error("IK chain order/link names were not retained");
   if(parsed["ikLocks"]!=reference["ikLocks"])throw std::runtime_error("Autoplay lock override lost");
   auto locks=read<int>(mdl,324);if(read<float>(mdl,locks+4)!=0||read<float>(mdl,locks+8)!=1)throw std::runtime_error("Inherited locks still pin donor proportions");
   const btTransform quarterTurn(btQuaternion(btVector3(0,0,1),SIMD_HALF_PI),btVector3(0,0,0));
   for(size_t i=0;i<actor.bones.size();i++){
    auto expected=rigMeshBind(actor)*rag.bones[i].rest;
    if((expected.getOrigin()-actor.bones[i].rest.getOrigin()).length()>1e-4f)throw std::runtime_error("Actor origin did not move fitted bones by 2.4 Source units");
    if(btFabs(expected.getRotation().dot(actor.bones[i].rest.getRotation()))<.99999f)throw std::runtime_error("Animation profile replaced a fitted skin bind frame");
   }
   for(int sequence=0;sequence<3;sequence++){
    size_t base=anims+sequence*100,correction=anims+300;size_t a=base+read<int>(mdl,base+56),d=correction+read<int>(mdl,correction+56);
    for(size_t i=0;i<actor.bones.size();i++){
     auto [position,q]=decode(mdl,bones+i*216,a,false);auto [offset,rotation]=decode(mdl,bones+i*216,d,true);auto& bone=actor.bones[i];auto expected=bone.parent<0?bone.rest:actor.bones[bone.parent].rest.inverse()*bone.rest;
     if(sequence>0&&bone.parent<0)expected=quarterTurn*expected;
     if((position+offset-expected.getOrigin()).length()>.003||btFabs((rotation*q).dot(expected.getRotation()))<.99999f)throw std::runtime_error("Native reference/corpse pose failed to recover fitted pivot and orientation");
     if(btFabs(rotation.w())<.99999f)throw std::runtime_error("Proportion layer rotates an animation bone");
     a+=read<int16_t>(mdl,a+2);d+=read<int16_t>(mdl,d+2);
    }
   }
   for(size_t i=0;i<actor.bodies.size();i++)for(size_t v=0;v<actor.bodies[i].hull.size();v++){
    auto old=rigMeshBind(actor)*(rag.bones[rag.bodies[i].bone].rest*rag.bodies[i].hull[v]);
    auto now=actor.bones[actor.bodies[i].bone].rest*actor.bodies[i].hull[v];
    if((old-now).length()>.001f)throw std::runtime_error("Animation frame rebasing moved a collision hull");
   }
  }else{auto stem=actor.path.substr(0,actor.path.size()-4);auto& vvd=package.at(stem+".vvd");if(read<int>(vvd,16)<=0)throw std::runtime_error("Arms have no native vertices");}
  auto archive=makeGma(package,"test");validateSharedFile("rigs/"+actor.key+"/carrier.gma",archive);
 }
 // A ragdoll given an animation reference keeps its bind, bodies, spawn pose and
 // Reference, and includes the stock player and Citizen packs of its style.
 for(std::string gender:{"female","male"}){
  auto animated=fitRig(*model,{{"role","ragdoll"},{"gender",gender},{"animationSource","models/reference.mdl"},{"animationReference",reference}});
  if(animated.key==rag.key)throw std::runtime_error("Animated ragdoll aliases the plain ragdoll carrier");
  if(animated.manifest.contains("meshYaw")||animated.manifest.contains("actorOrigin"))throw std::runtime_error("Animated ragdoll changed its mesh bind");
  for(size_t i=0;i<animated.bones.size();i++)if((animated.bones[i].rest.getOrigin()-rag.bones[i].rest.getOrigin()).length()>1e-5f||btFabs(animated.bones[i].rest.getRotation().dot(rag.bones[i].rest.getRotation()))<.99999f)throw std::runtime_error("Animated ragdoll moved a fitted bone");
  for(size_t i=0;i<animated.bodies.size();i++)if(animated.bodies[i].hull!=rag.bodies[i].hull)throw std::runtime_error("Animated ragdoll changed a collision hull");
  // The Citizen packs, then the player pack of player models (the one addons
  // replace, last so it cannot shift the others' indices); all ship with the game.
  std::string player=gender=="male"?"models/m_anm.mdl":"models/f_anm.mdl",citizen="models/humans/"+gender+"_";
  Json includes={citizen+"shared.mdl",citizen+"ss.mdl",citizen+"gestures.mdl",citizen+"postures.mdl",player};
  if(animated.manifest["animation"]["includes"]!=includes||animated.manifest["animation"]["profile"]!="ragdoll_"+gender)throw std::runtime_error("Animated ragdoll does not include the player and Citizen packs");
  auto package=carrierFiles(animated);auto& mdl=package.at(animated.path);auto parsed=readAnimationModel(mdl);
  if(read<int>(mdl,188)!=4||parsed["includes"]!=includes||parsed["ikChains"]!=reference["ikChains"])throw std::runtime_error("Animated ragdoll header lacks its sequences, includes or IK chains");
  auto bones=read<int>(mdl,160),anims=read<int>(mdl,184);
  for(int sequence=0;sequence<3;sequence++){
   size_t a=anims+sequence*100+read<int>(mdl,anims+sequence*100+56),d=anims+300+read<int>(mdl,anims+300+56);
   std::vector<btTransform> world(animated.bones.size());
   for(size_t i=0;i<animated.bones.size();i++){
    auto [position,q]=decode(mdl,bones+i*216,a,false);auto [offset,rotation]=decode(mdl,bones+i*216,d,true);auto& bone=animated.bones[i];auto expected=bone.parent<0?bone.rest:animated.bones[bone.parent].rest.inverse()*bone.rest;
    btTransform local(q,position);world[i]=bone.parent<0?local:world[bone.parent]*local;
    // The server builds the ragdoll's bodies and joints from sequence 0 without
    // the client-only proportion layer: there its physics bones land on their bind.
    if(sequence==0&&bone.physics>=0){if((world[i].getOrigin()-bone.rest.getOrigin()).length()>.003||btFabs(world[i].getRotation().dot(bone.rest.getRotation()))<.99999f)throw std::runtime_error("The server would build the animated ragdoll's bodies off its bind");}
    // Clients pose its other bones, and every bone of Reference, with the layer.
    else if((position+offset-expected.getOrigin()).length()>.003||btFabs((rotation*q).dot(expected.getRotation()))<.99999f)throw std::runtime_error("Animated ragdoll's spawn or Reference pose is not its bind");
    a+=read<int16_t>(mdl,a+2);d+=read<int16_t>(mdl,d+2);
   }
  }
  validateSharedFile("rigs/"+animated.key+"/carrier.gma",makeGma(package,"test"));
 }
 // Without a reference a ragdoll is the plain carrier it always was.
 if(fitRig(*model,{{"role","ragdoll"},{"gender","female"}}).key!=rag.key)throw std::runtime_error("A ragdoll without an animation reference changed identity");
 bool rejected=false;try{sharedPath("cache","../lua/autorun/payload.lua");}catch(...){rejected=true;}if(!rejected)throw std::runtime_error("Unsafe transfer path accepted");
 rejected=false;try{validateSharedFile("rigs/test/carrier.gma",makeGma({{"lua/autorun/payload.lua",{'x'}}},"bad"));}catch(...){rejected=true;}if(!rejected)throw std::runtime_error("Executable archive accepted");
 auto frame=std::make_shared<SceneFrame>();frame->sequence=9;frame->timestamp=1;auto geometry=std::make_shared<SceneGeometry>();geometry->kind=SceneGeometry::Sphere;geometry->radius=1;geometry->minimum={-1,-1,-1};geometry->maximum={1,1,1};SceneObject object;object.id=1;object.geometry=geometry;frame->objects.push_back(object);publishScene(frame);
 SceneShare sender,receiver;auto description=sender.describe({0,0,0},100);std::string id=description["objects"][0]["shape"];auto bytes=sender.chunk(id,0,32768);receiver.accept(id,bytes);World client;receiver.publish(client,description);if(readScene(&client)->objects.size()!=1)throw std::runtime_error("Scene publication failed");bytes.back()^=1;rejected=false;try{receiver.accept(id,bytes);}catch(...){rejected=true;}if(!rejected)throw std::runtime_error("Corrupt geometry accepted");
 // Living players and NPCs travel tagged; a subscriber receives only the kinds it asked for.
 {auto tagged=std::make_shared<SceneFrame>();tagged->sequence=10;tagged->timestamp=2;
  for(uint8_t actor:{uint8_t(SceneObject::NoActor),uint8_t(SceneObject::LivingPlayer),uint8_t(SceneObject::LivingNpc)}){SceneObject o=object;o.id=10+actor;o.actor=actor;tagged->objects.push_back(o);}
  SceneObject fixed=object;fixed.id=20;fixed.isStatic=true;tagged->objects.push_back(fixed);publishScene(tagged);
  auto count=[&](unsigned kinds){return sender.describe({0,0,0},100,0,kinds)["objects"].size();};
  if(count(Collide::All)!=4||count(Collide::Objects)!=1||count(Collide::World)!=1||count(Collide::Players|Collide::Npcs)!=2||count(Collide::Character)!=0)throw std::runtime_error("Scene export ignores the subscriber's collision kinds");
  receiver.publish(client,sender.describe({0,0,0},100));int kinds=0;for(auto& o:readScene(&client)->objects)kinds|=1<<o.actor;
  if(kinds!=7)throw std::runtime_error("Remote scene lost living actor tags");}
 std::cout<<"Actor reference/proportion encoding, native arms geometry, safe packages and remote collision snapshots passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
