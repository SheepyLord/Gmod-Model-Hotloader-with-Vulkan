// The carrier's torso (issue #9) and the bone map the fitter takes: the pure
// resolver (rig_torso.hpp) on synthetic skeletons, then fitRig's pins
// (options.boneMap, a converted character's map), native.GetBoneMapProposal's
// function and the missing-landmark error on the native-cloth21 fixture.
#include "rig.hpp"
#include "rig_torso.hpp"
#include "physics_profile.hpp"
#include "import_error.hpp"
#include "humanoid_map.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
using namespace mmd;
namespace {
int checks=0;
void check(bool ok,const std::string& name){if(!ok)throw std::runtime_error("FAIL "+name);++checks;std::cout<<"PASS "<<name<<"\n";}
const char* Fixture="tests/fixtures/native-cloth21.pmx";
// The fitter's answers for the bone window, read by tests/test_bone_mapper.py.
const char* WindowFixture="tests/fixtures/bonemap/proposal.json";
const std::string VB="ValveBiped.Bip01_";

// ---- synthetic skeletons: bones only, PMX units, +Y up (spine_resolve.py's base) ----
struct Skeleton {
 std::shared_ptr<Model> m=std::make_shared<Model>();
 int add(const std::string& name,btVector3 position,int parent){Bone b;b.name=name;b.parent=parent;b.position=position;m->bones.push_back(b);return int(m->bones.size()-1);}
 int operator[](const std::string& name) const {for(size_t i=0;i<m->bones.size();i++)if(m->bones[i].name==name)return int(i);return -1;}
 void parent(std::initializer_list<const char*> names,const std::string& to){for(auto n:names)m->bones[(*this)[n]].parent=(*this)[to];}
 void move(const std::string& name,btVector3 p){m->bones[(*this)[name]].position=p;}
};
Skeleton standard(){
 Skeleton s;int root=s.add("全ての親",{0,0,0},-1),center=s.add("センター",{0,8,0},root);
 int lower=s.add("下半身",{0,10,0},center),upper=s.add("上半身",{0,10.2f,0},center),chest=s.add("上半身2",{0,12.5f,0},upper);
 int neck=s.add("首",{0,15.5f,0},chest);s.add("頭",{0,16.5f,0},neck);
 for(auto [side,x]:{std::pair{"左",1.f},{"右",-1.f}}){std::string j=side;
  int p=s.add(j+"肩P",{.3f*x,15,0},chest),shoulder=s.add(j+"肩",{.3f*x,15,0},p),arm=s.add(j+"腕",{1.3f*x,14.8f,0},shoulder),elbow=s.add(j+"ひじ",{3.5f*x,12.8f,0},arm);s.add(j+"手首",{5.5f*x,11,0},elbow);
  int leg=s.add(j+"足",{.8f*x,9.5f,0},lower),knee=s.add(j+"ひざ",{.8f*x,5,0},leg);s.add(j+"足首",{.8f*x,1,0},knee);}
 s.add("左胸",{.6f,13.6f,-.8f},chest);
 return s;
}
// The fitter's own choices for the other carrier bones, as fitRig hands them over.
TorsoInput input(const Skeleton& s){
 TorsoInput in;in.spine1=s["上半身"];in.neck=s["首"];in.head=s["頭"];in.clavicle={s["左肩"],s["右肩"]};in.upperArm={s["左腕"],s["右腕"]};
 for(auto n:{"下半身","上半身","首","頭","左肩","右肩","左腕","右腕","左ひじ","右ひじ","左手首","右手首","左足","右足","左ひざ","右ひざ","左足首","右足首"})if(s[n]>=0)in.taken.insert(s[n]);
 return in;
}
bool repaired(const TorsoChoice& c,const std::string& code){for(auto& r:c.repairs)if(r.code==code)return true;return false;}
// Every choice: mapped pivots stay on their bones, Spine1 < Spine2 < Spine4 < neck along
// the chain, and no bone is driven twice or taken from another carrier bone.
void invariants(const Model& m,const TorsoInput& in,const TorsoChoice& c,const std::string& tag){
 if(c.spine4>=0)check(c.spine4Origin==m.bones[c.spine4].position,tag+": the chest pivot is its bone's");
 if(c.spine2>=0)check(c.spine2Origin==m.bones[c.spine2].position,tag+": the middle spine pivot is its bone's");
 if(c.method!="degenerate"){auto o1=m.bones[in.spine1].position,oN=in.neck>=0?m.bones[in.neck].position:m.bones[in.head].position.lerp(o1,.25f),up=oN-o1;
  auto t=[&](const btVector3& p){return (p-o1).dot(up)/up.length2();};
  check(0<t(c.spine2Origin)&&t(c.spine2Origin)<t(c.spine4Origin)&&t(c.spine4Origin)<1,tag+": Spine1 < Spine2 < Spine4 < neck along the chain");}
 std::vector<int> all;for(int b:{c.spine2,c.spine4})if(b>=0)all.push_back(b);all.insert(all.end(),c.spine2Aliases.begin(),c.spine2Aliases.end());all.insert(all.end(),c.spine4Aliases.begin(),c.spine4Aliases.end());
 std::set<int> once(all.begin(),all.end());bool free=true;for(int b:all)free&=!in.taken.contains(b)||b==in.spine2||b==in.spine4;
 check(once.size()==all.size()&&free,tag+": no bone is driven twice");
}
TorsoChoice resolved(const Skeleton& s,const TorsoInput& in,const std::string& tag){auto c=resolveTorso(*s.m,in);invariants(*s.m,in,c,tag);return c;}

// ---- the fixture ----
int boneNamed(const Model& m,const std::string& name){for(size_t i=0;i<m.bones.size();i++)if(m.bones[i].name==name)return int(i);throw std::runtime_error("missing bone "+name);}
int carrier(const Rig& r,const std::string& name){for(size_t i=0;i<r.bones.size();i++)if(r.bones[i].name==VB+name)return int(i);throw std::runtime_error("missing carrier bone "+name);}
std::shared_ptr<Model> fixture(){return parse(readFile(Fixture));}
// A fixture bone added under parent, at a position between two fixture bones.
int add(Model& m,const std::string& name,const btVector3& position,int parent){Bone b=m.bones[parent];b.name=name;b.english="";b.position=position;b.parent=parent;b.inherit=-1;m.bones.push_back(b);return int(m.bones.size()-1);}
ImportError failure(const std::function<void()>& run){try{run();}catch(const ImportError& e){return e;}catch(const std::exception& e){throw std::runtime_error(std::string("not an ImportError: ")+e.what());}throw std::runtime_error("no error");}
Json pins(std::initializer_list<std::pair<const std::string,Json>> values){Json map=Json::object();for(auto& [k,v]:values)map[k]=v;return {{"boneMap",map}};}
// The fixture built 上半身 > 上半身3 > 上半身2 > neck base > neck and shoulders, and the
// window's two calls: InspectBoneMap and GetBoneMapProposal without and with saved pins
// (the chest pinned to the neck base, outside its band, and no left toes).
Json windowFixture(){
 auto m=fixture();int upper=boneNamed(*m,"upper body"),head=boneNamed(*m,"head"),neck=boneNamed(*m,"neck");auto lo=m->bones[upper].position,hi=m->bones[head].position;
 int third=add(*m,"上半身3",lo.lerp(hi,.35f),upper),second=add(*m,"上半身2",lo.lerp(hi,.65f),third),base=add(*m,"首根元",m->bones[neck].position-btVector3(0,.1f,0),second);
 for(auto n:{"neck","left shoulder","right shoulder"})m->bones[boneNamed(*m,n)].parent=base;
 Json saved={{VB+"Spine4",base},{VB+"L_Toe0",-1}};
 return {{"about","GetBoneMapProposal and InspectBoneMap on a torso built 上半身 > 上半身3 > 上半身2 > 首根元; written by mmdhl_torso_tests --record-window"},
  {"inspect",inspectBoneMap(*m,{{"include",{"skeleton"}}})},{"proposal",boneMapProposal(*m,Json::object())},{"pins",saved},{"current",boneMapProposal(*m,{{"boneMap",saved}})}};
}
// The fitted rig's own invariants: a mapped carrier origin sits on its PMX bone, no PMX
// bone is driven twice and the torso runs up the body.
void fitted(const Model& m,const Rig& r,const std::string& tag){
 bool pivots=true;for(auto& b:r.bones)if(b.mmd>=0&&b.name.starts_with(VB)&&b.name.find("Spine")==std::string::npos)pivots&=(b.rest.getOrigin()-toSource(m.bones[b.mmd].position)*r.scale).length()<1e-4f;
 for(auto name:{"Spine1","Spine2","Spine4","Neck1"}){auto& b=r.bones[carrier(r,name)];if(b.mmd>=0)pivots&=(b.rest.getOrigin()-toSource(m.bones[b.mmd].position)*r.scale).length()<1e-4f;}
 std::multiset<int> used;for(auto& b:r.bones){if(b.mmd>=0)used.insert(b.mmd);used.insert(b.aliases.begin(),b.aliases.end());}
 bool once=true;for(int b:used)once&=used.count(b)==1;
 auto z=[&](const char* name){return r.bones[carrier(r,name)].rest.getOrigin().z();};
 check(pivots&&once&&r.bodies.size()==18,tag+": pivots on their bones, each PMX bone driven once, 18 bodies");
 check(z("Pelvis")<=z("Spine")+1e-4f&&z("Spine")<=z("Spine1")+1e-4f&&z("Spine1")<=z("Spine2")+1e-4f&&z("Spine2")<z("Spine4")&&z("Spine4")<z("Neck1"),tag+": Pelvis <= Spine <= Spine1 <= Spine2 < Spine4 < Neck1");
}
}

int main(int argc,char** argv){try{
 if(argc==2&&std::string(argv[1])=="--record-window"){std::ofstream(WindowFixture,std::ios::binary)<<windowFixture().dump(1)<<"\n";std::cout<<"Recorded "<<WindowFixture<<"\n";return 0;}
 // ---- the resolver on synthetic skeletons (spine_resolve.py's cases A-K and more) ----
 {auto s=standard();auto c=resolved(s,input(s),"A standard");
  check(c.method=="topology"&&c.spine4==s["上半身2"]&&c.spine2<0&&c.spine4Aliases.empty()&&c.repairs.empty(),"A: 上半身 > 上半身2 > neck: the chest is 上半身2 and the middle spine is synthesized");}
 {auto s=standard();s.add("上半身3",{0,14,0},s["上半身2"]);s.parent({"首","左肩P","右肩P"},"上半身3");auto c=resolved(s,input(s),"B");
  check(c.spine2==s["上半身2"]&&c.spine4==s["上半身3"]&&c.repairs.empty(),"B: 上半身 > 上半身2 > 上半身3 (Furina, Yixuan, Ruan Mei): middle 上半身2, chest 上半身3");}
 {auto s=standard();s.move("上半身2",{0,13.4f,0});s.add("上半身3",{0,11.6f,0},s["上半身"]);s.parent({"上半身2"},"上半身3");auto c=resolved(s,input(s),"C");
  check(c.spine2==s["上半身3"]&&c.spine4==s["上半身2"]&&repaired(c,"reordered"),"C: 上半身 > 上半身3 > 上半身2 (Ganyu, issue #9): the middle spine is 上半身3, the chest 上半身2, with a note");
  auto in=input(s);in.spine4=s["上半身2"];auto pinned=resolved(s,in,"C pinned");
  check(pinned.spine4==s["上半身2"]&&pinned.spine2==s["上半身3"]&&pinned.repairs.empty(),"C: a pinned chest keeps the automatic middle spine below it");
  in.spine4=s["上半身3"];pinned=resolved(s,in,"C pinned low");
  check(pinned.spine4==s["上半身3"]&&pinned.spine2<0,"C: a chest pinned to the lower bone leaves no middle spine below it");}
 {auto s=standard();s.parent({"首","左肩P","右肩P","左胸"},"上半身");s.m->bones[s["上半身2"]].name="unused";auto c=resolved(s,input(s),"D");
  check(c.spine2<0&&c.spine4<0&&c.spine4Aliases.empty(),"D: neck and shoulders on 上半身: both synthesized");
  s.add("後髪3",{0,16.8f,-.5f},s["頭"]);c=resolved(s,input(s),"D hair");
  check(c.spine4<0&&c.spine4Aliases.empty(),"D: a hair bone named like a third segment is never the chest");}
 for(auto [tag,y,above]:{std::tuple{"E",15.8f,true},{"F",9.8f,false}}){auto s=standard();s.move("上半身2",{0,y,0});auto c=resolved(s,input(s),tag);
  check(c.spine4<0&&c.spine4Aliases==std::vector<int>{s["上半身2"]}&&repaired(c,"band"),std::string(tag)+(above?": 上半身2 above the neck":": 上半身2 below 上半身")+" moves with a synthesized chest; its pivot is not moved");}
 {auto s=standard();s.add("上半身3",{0,11,0},s["上半身2"]);auto c=resolved(s,input(s),"G");
  check(c.spine4==s["上半身2"]&&c.spine2<0&&repaired(c,"ignored"),"G: a leaf 上半身3 (Hu Tao) is not the chest and follows its parent");}
 {auto s=standard();s.move("上半身2",{0,11.5f,0});int u3=s.add("上半身3",{0,12.8f,0},s["上半身2"]);s.add("上半身4",{0,14,0},u3);s.parent({"首","左肩P","右肩P"},"上半身4");auto c=resolved(s,input(s),"H");
  check(c.spine2==s["上半身2"]&&c.spine4==s["上半身4"],"H: four segments: middle 上半身2 (nearest the halfway point), chest 上半身4");}
 {auto s=standard();s.parent({"首"},"上半身");auto c=resolved(s,input(s),"I");
  check(c.spine4==s["上半身2"]&&repaired(c,"neck_on_spine"),"I: neck on 上半身, shoulders on 上半身2: the chest holds the shoulders");}
 {auto s=standard();s.parent({"左肩P","右肩P"},"上半身");auto c=resolved(s,input(s),"I'");
  check(c.spine4==s["上半身2"]&&repaired(c,"shoulders_on_spine"),"I': shoulders on 上半身, neck on 上半身2: the chest holds the neck");}
 {auto s=standard();s.m->bones[s["頭"]].parent=s["上半身2"];s.m->bones[s["首"]].name="x";auto in=input(s);in.neck=-1;auto c=resolved(s,in,"J");
  check(c.spine4==s["上半身2"],"J: without a neck the head anchors the chest");}
 {auto s=standard();s.add("おっぱい調整",{0,13.5f,-.5f},s["上半身2"]);s.parent({"首","左肩P","右肩P"},"おっぱい調整");auto c=resolved(s,input(s),"K");
  check(c.spine4==s["上半身2"]&&repaired(c,"rejected"),"K: a breast helper holding the neck and shoulders is not the chest");}
 {auto s=standard();int a=s.add("Bip01 Spine0a",{0,11.2f,0},s["上半身"]);s.parent({"上半身2"},"Bip01 Spine0a");s.move("上半身2",{0,12.3f,0});int b=s.add("Bip01 Spine1a",{0,13.6f,0},s["上半身2"]);s.parent({"首","左肩P","右肩P"},"Bip01 Spine1a");
  auto c=resolved(s,input(s),"Maid");check(c.spine2==s["上半身2"]&&c.spine4==b&&a!=c.spine2,"3ds Max Biped chain: the MMD name wins the middle spine, the chest is the bone holding the neck");}
 {auto s=standard();int dup=s.add("上半身2+",{0,12.55f,0},s["上半身2"]);s.parent({"首","左肩P","右肩P"},"上半身2+");auto c=resolved(s,input(s),"coincident");
  check(c.spine4==dup&&c.spine4Aliases==std::vector<int>{s["上半身2"]}&&repaired(c,"coincident"),"a duplicate at the chest's place (上半身2+) moves with the chest");}
 {auto s=standard();s.parent({"首","左肩P","右肩P"},"センター");auto c=resolved(s,input(s),"names");
  check(c.method=="names"&&c.spine4==s["上半身2"]&&repaired(c,"names"),"neck and shoulders outside the upper body: the chest is chosen by name, still band-checked");}
 {auto s=standard();s.move("首",{0,9,0});auto c=resolveTorso(*s.m,input(s));
  check(c.method=="degenerate"&&c.spine4==s["上半身2"]&&repaired(c,"degenerate"),"a neck below the upper body keeps the name-based chest");}
 {auto s=standard();auto in=input(s);in.secondary.insert(s["上半身2"]);auto c=resolved(s,in,"secondary");
  check(c.spine4<0&&repaired(c,"rejected"),"a chest moved by the model's own physics is never a carrier pivot");}
 {auto s=standard();auto in=input(s);in.spine4=s["左胸"];auto c=resolved(s,in,"pin breast");
  check(c.spine4==s["左胸"],"a pinned chest inside its band is kept, whatever its name");
  in.spine4=s["センター"];c=resolved(s,in,"pin low");
  check(c.spine4<0&&c.spine4Aliases==std::vector<int>{s["センター"]}&&repaired(c,"band"),"a pinned chest below the upper body moves with a synthesized chest");
  in=input(s);in.spine2=s["首"];c=resolveTorso(*s.m,in);
  check(c.spine2<0&&c.spine2Aliases==std::vector<int>{s["首"]}&&repaired(c,"band"),"a pinned middle spine above the chest moves with a synthesized middle spine");}
 {auto s=standard();s.add("上半身3",{0,14,0},s["上半身2"]);s.parent({"首","左肩P","右肩P"},"上半身3");auto in=input(s);in.spine2=-1;auto c=resolved(s,in,"no middle");
  check(c.spine2<0&&c.spine4==s["上半身3"]&&c.spine2Aliases.empty(),"a middle spine pinned to none stays synthesized");
  in=input(s);in.spine4=-1;c=resolved(s,in,"no chest");
  check(c.spine4<0&&c.spine4Aliases.empty()&&c.spine2==s["上半身2"],"a chest pinned to none is synthesized; the middle spine stays below it");}
 {auto s=standard();auto in=input(s);in.spine1=-1;auto c=resolveTorso(*s.m,in);Skeleton empty;auto none=resolveTorso(*empty.m,TorsoInput{});
  check(c.method=="degenerate"&&c.repairs.empty()&&none.spine2<0&&none.spine4<0,"without Spine1 or bones the resolver gives up quietly (the fit fails on the landmark)");}
 {auto s=standard();int base=s.add("首根元",{0,15.4f,0},s["上半身2"]);s.parent({"首","左肩P","右肩P"},"首根元");auto c=resolved(s,input(s),"neck base");
  check(c.spine4==s["上半身2"]&&c.spine4Aliases==std::vector<int>{base}&&repaired(c,"band"),"a neck base holding the neck and shoulders moves with the chest below it");
  auto in=input(s);in.spine4=base;c=resolved(s,in,"neck base pinned");
  check(c.spine4<0&&c.spine4Aliases==std::vector<int>{base}&&c.spine2==s["上半身2"],"a chest pinned to the neck base moves with a synthesized chest");}
 {auto s=standard();s.add("上半身3",{0,14,0},s["上半身2"]);s.parent({"首","左肩P","右肩P"},"上半身3");auto in=input(s);in.spine2=s["上半身3"];auto c=resolved(s,in,"middle pinned to the chest");
  check(c.spine4==s["上半身2"]&&c.spine2<0&&c.spine2Aliases==std::vector<int>{s["上半身3"]}&&repaired(c,"rejected"),"a middle spine pinned to the chest's bone is never also the chest");}

 // ---- fitRig on the fixture: pins, conversion maps, the proposal and the errors ----
 auto model=fixture();auto rag=fitRig(*model,Json::object());
 const int upper=boneNamed(*model,"upper body"),chest=boneNamed(*model,"upper body2"),neck=boneNamed(*model,"neck"),leftToe=boneNamed(*model,"left toe");
 check(rag.manifest["torso"]==Json({{"method","topology"},{"repairs",Json::array()}})&&rag.bones[carrier(rag,"Spine4")].mmd==chest&&rag.bones[carrier(rag,"Spine2")].mmd<0,"fit: the fixture's torso is found by its shape, with nothing to repair");
 fitted(*model,rag,"fit");
 {auto old=rag.manifest;old["generator"]=30;bool refused=false;try{rigFromManifest(old);}catch(const std::exception& e){refused=std::string(e.what())=="Incompatible carrier fit";}
  check(refused&&rigFromManifest(rag.manifest).key==rag.key,"cache: a generator-30 fit is refused, a current one restored");}
 // A valid pin changes the mapping and the identity; 12.0 from Lua is 12.
 {auto pinned=fitRig(*model,pins({{VB+"L_Toe0",-1}}));int toe=carrier(pinned,"L_Toe0");
  check(pinned.bones[toe].mmd<0&&pinned.manifest["bones"][toe]["provenance"]=="user"&&pinned.key!=rag.key,"pins: a part pinned to none loses its bone and the carrier its identity");
  check(fitRig(*model,pins({{VB+"L_Toe0",-1.0}})).key==pinned.key,"pins: an integral number from Lua's JSON is the same pin");
  check(carrierFitKey("a",normalizeCarrierOptions(pins({{VB+"L_Toe0",-1}})))!=carrierFitKey("a",Json::object()),"pins: the fit cache key holds the bone map");
  fitted(*model,pinned,"pins");}
 // The cached fit answers only without pins.
 {auto cached=fixture();cached->fittedRig=std::make_shared<Rig>(fitRig(*cached,Json::object()));
  check(cachedFitApplies(*cached,Json::object())&&cachedFitApplies(*cached,{{"boneMap",Json::object()}})&&!cachedFitApplies(*cached,pins({{VB+"L_Toe0",-1}})),"cache: pins bypass the cached fit, an empty map does not");
  check(fitRig(*cached,pins({{VB+"L_Toe0",-1}})).bones[carrier(rag,"L_Toe0")].mmd<0,"cache: a model with a cached fit still takes its pins");}
 // An extended fixture: a toe tip and a spine helper off the chain.
 {auto extended=fixture();int tip=add(*extended,"toe tip",extended->bones[leftToe].position+btVector3(0,0,-.5f),leftToe);
  int helper=add(*extended,"spine helper",extended->bones[upper].position.lerp(extended->bones[neck].position,.3f),upper);
  auto rig=fitRig(*extended,pins({{VB+"L_Toe0",double(tip)},{VB+"Spine2",helper}}));auto& toe=rig.bones[carrier(rig,"L_Toe0")];auto& middle=rig.bones[carrier(rig,"Spine2")];
  check(toe.mmd==tip&&(toe.rest.getOrigin()-toSource(extended->bones[tip].position)*rig.scale).length()<1e-4f,"pins: a pinned bone drives the part from its own pivot");
  check(middle.mmd==helper&&(middle.rest.getOrigin()-toSource(extended->bones[helper].position)*rig.scale).length()<1e-4f&&rig.manifest["bones"][carrier(rig,"Spine2")]["provenance"]=="user","pins: a middle spine pinned inside its band is kept");
  fitted(*extended,rig,"pinned middle spine");
  // A converted character's own map is the base; pins override it.
  extended->conversionBoneMap={{VB+"L_Toe0",tip},{VB+"Spine2",-1}};
  auto converted=fitRig(*extended,Json::object());int t=carrier(converted,"L_Toe0");
  check(converted.bones[t].mmd==tip&&converted.manifest["bones"][t]["provenance"]=="conversion"&&converted.bones[carrier(converted,"Spine2")].mmd<0,"conversion: the fitter starts from the converter's assignment");
  check(fitRig(*extended,pins({{VB+"L_Toe0",-1}})).bones[t].mmd<0,"conversion: a pin overrides the converter's assignment");
  extended->conversionBoneMap={{VB+"Spine2",chest},{VB+"Spine4",-1}};auto single=fitRig(*extended,Json::object());
  check(single.bones[carrier(single,"Spine4")].mmd==chest&&single.bones[carrier(single,"Spine2")].mmd<0,"conversion: a middle spine without a chest is the chest, the part with a physics body");}
 // Invalid pins are refused with the reason, never ignored.
 for(auto [label,options,code]:{std::tuple{"a fraction",pins({{VB+"L_Toe0",1.5}}),"range"},{"a bone past the end",pins({{VB+"L_Toe0",99999}}),"range"},{"a text",pins({{VB+"L_Toe0","x"}}),"range"},
   {"the synthesized spine",pins({{VB+"Spine",upper}}),"range"},{"an eye",pins({{"Eye_L",neck}}),"range"},{"an unknown part",pins({{"Nonsense",neck}}),"range"},
   {"one bone for two parts",pins({{VB+"L_Toe0",leftToe},{VB+"R_Toe0",leftToe}}),"duplicate"},{"a required part without a bone",pins({{VB+"Head1",-1}}),"required"}}){
  auto e=failure([&]{fitRig(*model,options);});auto issues=e.details.value("issues",Json::array());
  check(e.code=="fit.bone_map"&&!issues.empty()&&issues[0]["code"]==code&&std::string(e.what()).starts_with("The bone assignment cannot be used: "),std::string("pins: ")+label+" is refused ("+code+")");}
 check(failure([&]{fitRig(*model,{{"boneMap",Json::array()}});}).code=="fit.bone_map","pins: a bone map that is not an object is refused");
 // GetBoneMapProposal: the same mapping, without bodies.
 {auto p=boneMapProposal(*model,Json::object());bool same=p["bones"].size()==56;
  for(size_t i=0;i<56&&same;i++)same&=p["bones"][i]["name"]==rag.manifest["bones"][i]["name"]&&p["bones"][i]["mmd"]==rag.manifest["bones"][i]["mmd"]&&p["bones"][i]["aliases"]==rag.manifest["bones"][i]["mmdAliases"]&&p["bones"][i]["provenance"]==rag.manifest["bones"][i]["provenance"];
  check(same,"proposal: the bones, aliases and provenance of the fit");
  check(p["missing"].empty()&&p["issues"].empty()&&!p.contains("error")&&p["torso"]==rag.manifest["torso"]&&p["bones"][0]["required"]==true&&p["bones"][3]["required"]==false,"proposal: nothing missing, no issues, the fit's torso, required flags");
  auto moved=boneMapProposal(*model,pins({{VB+"R_Toe0",leftToe}}));std::set<std::string> codes;for(auto& i:moved["issues"])codes.insert(i["code"].get<std::string>()+" "+i["severity"].get<std::string>()+" "+i["slot"].get<std::string>());
  check(moved["bones"][carrier(rag,"R_Toe0")]["mmd"]==leftToe&&moved["bones"][carrier(rag,"R_Toe0")]["provenance"]=="user"&&moved["bones"][carrier(rag,"L_Toe0")]["mmd"]==-1&&codes.contains("moved warning "+VB+"L_Toe0")&&!moved.contains("error"),"proposal: a pinned bone leaves the part the fitter gave it to, with a warning");
  auto band=boneMapProposal(*model,pins({{VB+"Spine4",neck}}));auto& s4=band["bones"][carrier(rag,"Spine4")];codes.clear();for(auto& i:band["issues"])codes.insert(i["code"].get<std::string>()+" "+i["severity"].get<std::string>()+" "+i["slot"].get<std::string>());
  check(s4["mmd"]==-1&&s4["aliases"]==Json::array({neck})&&s4["provenance"]=="user"&&codes.contains("band warning "+VB+"Spine4")&&codes.contains("moved warning "+VB+"Neck1"),"proposal: a chest pinned above its band moves with a synthesized chest, with a band warning");
  fitted(*model,fitRig(*model,pins({{VB+"Spine4",neck}})),"band pin");
  auto bad=boneMapProposal(*model,pins({{VB+"L_Toe0",2.5},{VB+"Head1",-1}}));codes.clear();for(auto& i:bad["issues"])codes.insert(i["code"].get<std::string>()+" "+i["severity"].get<std::string>()+" "+i["slot"].get<std::string>());
  check(codes.contains("range error "+VB+"L_Toe0")&&codes.contains("required error "+VB+"Head1")&&bad["missing"]==Json::array({VB+"Head1"})&&bad["errorCode"]=="fit.bone_map","proposal: bad pins are issues and the error fitRig would give, not a failure");
  check(failure([&]{boneMapProposal(*model,{{"boneMap",7}});}).code=="fit.bone_map","proposal: a malformed bone map fails");}
 // Every missing landmark is listed in words, with the names searched.
 {auto renamed=fixture();for(auto name:{"left knee","right elbow"}){auto& b=renamed->bones[boneNamed(*renamed,name)];b.name=b.english="renamed";}
  auto e=failure([&]{fitRig(*renamed,Json::object());});
  check(e.code=="fit.landmarks"&&e.details["missing"]==Json::array({VB+"R_Forearm",VB+"L_Calf"})&&std::string(e.what())=="No bone found for: right forearm (searched 右ひじ, 右肘, elbow_R, right elbow), left lower leg (searched 左ひざ, 左膝, knee_L, left knee)",
   "landmarks: every missing one is named in words with the names searched");
  check(e.details["searched"][VB+"L_Calf"]==Json::array({VB+"L_Calf","左ひざ","左膝","knee_L","left knee"}),"landmarks: the details list the searched names by carrier bone");
  auto p=boneMapProposal(*renamed,Json::object());
  check(p["missing"]==e.details["missing"]&&p["errorCode"]=="fit.landmarks"&&p["error"]==e.what(),"landmarks: the proposal reports them without failing");}
 // The bone window's recorded answers are still the fitter's (re-record with --record-window).
 check(readJson(WindowFixture)==windowFixture(),std::string("window: ")+WindowFixture+" holds the fitter's current answers");
 {auto f=windowFixture();auto& base=f["proposal"]["bones"];auto& now=f["current"]["bones"];int n=int(f["inspect"]["skeleton"]["bones"].size());
  check(base[3]["mmd"]==n-3&&base[4]["mmd"]==n-2&&base[4]["aliases"]==Json::array({n-1})&&now[4]["mmd"]==-1&&now[4]["aliases"]==Json::array({n-1})&&now[3]["mmd"]==n-3,
   "window: the chest is 上半身2 with the neck base moving with it; pinned there, the chest is synthesized and the middle spine stays");}
 // Loose names compare ASCII with ASCII only.
 {auto spaced=fixture();auto& knee=spaced->bones[boneNamed(*spaced,"left knee")];knee.name=knee.english="Left_Knee";
  check(fitRig(*spaced,Json::object()).bones[carrier(rag,"L_Calf")].mmd==boneNamed(*spaced,"Left_Knee"),"names: Left_Knee still matches left knee");
  auto folded=fixture();auto& other=folded->bones[boneNamed(*folded,"left knee")];other.name=other.english="ひざleft knee";
  check(failure([&]{fitRig(*folded,Json::object());}).code=="fit.landmarks","names: a Japanese name is never folded into an English one");}
 std::cout<<checks<<" torso checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
