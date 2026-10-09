#pragma once
// The carrier's chest (Spine4) and middle spine (Spine2), chosen from the PMX
// hierarchy like SCMI's spine fix (tools/blender_fix_spine_bones.py) instead of by
// name alone: names put Spine2 above Spine4 on models built 上半身 > 上半身3 >
// 上半身2 > 首 (issue #9). The chest is the bone the neck and both shoulders hang
// from, and Spine1 < Spine2 < Spine4 < Neck1 runs up the body. A mapped carrier
// origin always stays on its PMX bone: a bone outside its band, or one at the
// chest's place, becomes an alias of a synthesized carrier bone, which drives it
// rigidly (where SCMI merges the two bones).
#include "runtime.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <set>
namespace mmd {
struct TorsoInput {
 static constexpr int Auto=-2;
 int spine1=-1,neck=-1,head=-1;                       // the PMX bones of the carrier's Spine1, Neck1 and Head1
 std::array<int,2> clavicle{-1,-1},upperArm{-1,-1};   // left, right
 int spine2=Auto,spine4=Auto;                         // an explicit choice (a pin or a converted character's map): a bone or -1
 std::set<int> secondary;                             // moved by the model's physics: dynamic rigid bodies, VRM springs
 std::set<int> taken;                                 // driven by another carrier bone
};
struct TorsoRepair { std::string code; std::vector<int> bones; std::string text; };
struct TorsoChoice {
 int spine2=-1,spine4=-1;                             // the PMX bones of Spine2 and Spine4; -1: synthesized
 std::vector<int> spine2Aliases,spine4Aliases;        // bones moved rigidly by Spine2 / Spine4
 btVector3 spine2Origin{0,0,0},spine4Origin{0,0,0};   // PMX units: the bone's, or the point fitRig synthesizes
 std::string method="topology";                       // "topology", "names" (nothing hangs below Spine1) or "degenerate"
 std::vector<TorsoRepair> repairs;                    // English notes for manifest.torso and the bone window
};
namespace torso {
inline std::string folded(const std::string& s){std::string r=s;for(char& c:r)c=c=='_'?' ':char(std::tolower((unsigned char)c));return r;}
// Breast helpers may hold the neck and shoulders; they never become the chest.
inline bool breast(const Bone& b){auto s=folded(b.name+" "+b.english);for(auto t:{"おっぱい","乳","breast","boob","pai","mune","bust"})if(s.find(t)!=std::string::npos)return true;return false;}
// 0: MMD upper-body names, 1: other spine or chest names, 2: anything else. A
// helper of tier 2 never drives Spine2: its bone morphs or physics would be lost.
inline int tier(const Bone& b){auto name=folded(b.name),english=folded(b.english);
 if(b.name.starts_with("上半身"))return 0;for(auto& s:{name,english})if(s.starts_with("upper body")||s.starts_with("upperbody"))return 0;
 auto both=name+" "+english;for(auto w:{"上半身","upper body","upperbody","spine","chest","胸"})if(both.find(w)!=std::string::npos)return 1;return 2;}
// N of 上半身N / upper bodyN (ASCII or full-width digit), else -1.
inline int segment(const Bone& b){
 auto digit=[](const std::string& rest){if(rest.size()==1&&rest[0]>='0'&&rest[0]<='9')return rest[0]-'0';if(rest.size()==3&&rest.compare(0,2,"\xEF\xBC")==0&&(unsigned char)rest[2]>=0x90&&(unsigned char)rest[2]<=0x99)return (unsigned char)rest[2]-0x90;return -1;};
 if(b.name.starts_with("上半身"))return digit(b.name.substr(std::string("上半身").size()));
 for(auto& s:{folded(b.name),folded(b.english)})for(auto p:{"upper body","upperbody"})if(s.starts_with(p))if(int d=digit(s.substr(std::string(p).size()));d>=0)return d;
 return -1;}
}
inline TorsoChoice resolveTorso(const Model& m,const TorsoInput& in){
 TorsoChoice c;const int n=int(m.bones.size());
 auto valid=[&](int i){return i>=0&&i<n;};
 auto pos=[&](int i){return m.bones[i].position;};
 auto label=[&](int i){return "\""+(m.bones[i].name.empty()?m.bones[i].english:m.bones[i].name)+"\"";};
 auto ancestors=[&](int b){std::vector<int> up;std::set<int> seen;for(int p=valid(b)?m.bones[b].parent:-1;valid(p)&&seen.insert(p).second;p=m.bones[p].parent)up.push_back(p);return up;};
 auto below=[&](int b,int a){auto up=ancestors(b);return valid(a)&&std::find(up.begin(),up.end(),a)!=up.end();};
 // The nearest strict ancestor of every valid bone (SCMI's nearest_common_ancestor).
 auto common=[&](std::initializer_list<int> items){std::vector<std::vector<int>> chains;for(int i:items)if(valid(i))chains.push_back(ancestors(i));if(chains.empty())return -1;
  for(int a:chains[0])if(std::all_of(chains.begin()+1,chains.end(),[&](const std::vector<int>& up){return std::find(up.begin(),up.end(),a)!=up.end();}))return a;return -1;};
 auto named=[&](std::initializer_list<const char*> names){for(auto name:names)for(int i=0;i<n;i++)if(m.bones[i].name==name||m.bones[i].english==name)return i;return -1;};
 auto note=[&](const char* code,std::vector<int> bones,std::string text){c.repairs.push_back({code,std::move(bones),std::move(text)});};
 auto listed=[](const std::vector<int>& list,int i){return std::find(list.begin(),list.end(),i)!=list.end();};
 // t runs from Spine1 (0) to the neck (1), synthesized a quarter of the way down from the head as fitRig does.
 const bool anchored=valid(in.spine1)&&(valid(in.neck)||valid(in.head));
 const btVector3 o1=anchored?pos(in.spine1):btVector3(0,0,0),oN=!anchored?o1:valid(in.neck)?pos(in.neck):pos(in.head).lerp(o1,.25f),up=oN-o1;
 const float length=up.length();
 auto t=[&](const btVector3& p){return length>0?(p-o1).dot(up)/(length*length):0.f;};
 const bool degenerate=!anchored||length<1e-4f||up.y()<=0;
 // SCMI's torso line limit: a chest candidate stays within 0.26 of the chain's length from it.
 auto offLine=[&](const btVector3& p){return !degenerate&&((p-o1)-up*t(p)).length()>.26f*length;};
 std::string why;
 auto usable=[&](int i){why=in.taken.contains(i)||i==in.spine2||i==in.spine4?"another body part uses it":in.secondary.contains(i)?"the model's physics moves it":torso::breast(m.bones[i])?"it is a breast helper":offLine(pos(i))?"it is off the line from the upper body to the neck":"";return why.empty();};
 const int third=named({"上半身3","上半身３","upper body3","UpperBody3"}),second=named({"上半身2","上半身２","upper body2","UpperBody2"});
 const bool pinned2=in.spine2!=TorsoInput::Auto,pinned4=in.spine4!=TorsoInput::Auto;
 int s4=-1;std::vector<int> middle;  // usable bones below the chest: the Spine2 candidates
 if(degenerate){c.method="degenerate";if(anchored)note("degenerate",{in.spine1},"The neck is not above "+label(in.spine1)+": the chest is chosen by its name");}
 else{
  int neck=valid(in.neck)?in.neck:in.head;std::array<int,2> shoulder{};for(int k=0;k<2;k++)shoulder[k]=valid(in.clavicle[k])?in.clavicle[k]:in.upperArm[k];
  int chest=common({neck,shoulder[0],shoulder[1]});
  if(chest==in.spine1){int shoulders=common({shoulder[0],shoulder[1]}),holder=m.bones[neck].parent;
   if(shoulders!=in.spine1&&below(shoulders,in.spine1)){chest=shoulders;note("neck_on_spine",{neck,shoulders},"The neck hangs from "+label(in.spine1)+": the chest is "+label(shoulders)+", which holds the shoulders");}
   else if(holder!=in.spine1&&below(holder,in.spine1)){chest=holder;note("shoulders_on_spine",{holder},"The shoulders hang from "+label(in.spine1)+": the chest is "+label(holder)+", which holds the neck");}}
  if(chest!=in.spine1&&!below(chest,in.spine1)){c.method="names";note("names",{},"The neck and shoulders do not hang below "+label(in.spine1)+": the chest is chosen by its name");}
  else{
   // The chain from Spine1 (excluded) up to the chest, or up to the chest the player chose.
   const int top=pinned4&&valid(in.spine4)?in.spine4:chest;std::vector<int> path;
   if(valid(top)&&below(top,in.spine1)){path.push_back(top);for(int p:ancestors(top)){if(p==in.spine1)break;path.push_back(p);}std::reverse(path.begin(),path.end());}
   for(size_t k=0;k+1<path.size();k++)if(usable(path[k]))middle.push_back(path[k]);
   if(pinned4)s4=valid(in.spine4)?in.spine4:-1;
   else if(!path.empty()){int held=path.back();
    if(usable(held))s4=held;
    else{auto reason=why;if(!middle.empty()){s4=middle.back();middle.pop_back();}note("rejected",{held},label(held)+" holds the neck and shoulders, but "+reason+": the chest is "+(valid(s4)?label(s4):std::string("placed between the upper body and the neck")));}}
  }
 }
 if(c.method!="topology"){
  // By name (generator 30): 上半身3, else 上半身2, else chest; 上半身2 is the middle spine below a 上半身3.
  if(pinned4)s4=valid(in.spine4)?in.spine4:-1;else for(int i:{third,second,named({"chest"})})if(valid(i)&&usable(i)){s4=i;break;}
  if(valid(second)&&second!=s4&&usable(second))middle={second};
  if(!pinned4&&valid(s4)&&!middle.empty()&&below(middle[0],s4)){note("swapped",{s4,middle[0]},label(middle[0])+" hangs below "+label(s4)+": the chest is "+label(middle[0]));std::swap(s4,middle[0]);}
 }
 if(!degenerate&&valid(s4)){float t4=t(pos(s4));
  if(t4<.10f||t4>.95f){
   // A neck base holding the neck and shoulders: the highest chain bone in the band below it is the chest.
   int k=0;if(!pinned4&&t4>.95f)for(k=int(middle.size());k>0;k--){float tk=t(pos(middle[k-1]));if(tk>=.10f&&tk<=.95f)break;}
   c.spine4Aliases.push_back(s4);
   if(k>0){note("band",{s4,middle[k-1]},label(s4)+" sits at the neck: the chest is "+label(middle[k-1])+", and "+label(s4)+" moves with it");s4=middle[k-1];middle.resize(k-1);}
   else{note("band",{s4},label(s4)+" is "+(t4<.10f?"below":"above")+" the chest's place between "+label(in.spine1)+" and the neck: it moves with a chest placed between them");s4=-1;}}}
 c.spine4=s4;c.spine4Origin=valid(s4)?pos(s4):o1.lerp(oN,.55f);
 const float t4=t(c.spine4Origin);
 int s2=-1;
 if(pinned2){s2=valid(in.spine2)?in.spine2:-1;
  if(!degenerate&&valid(s2)){float t2=t(pos(s2));if(t2<.04f||t2>t4-.04f){c.spine2Aliases.push_back(s2);note("band",{s2},label(s2)+" is not between "+label(in.spine1)+" and the chest: it moves with a middle spine placed halfway between them");s2=-1;}}}
 else if(degenerate)s2=middle.empty()?-1:middle[0];
 else{int bestTier=2;float bestGap=0;
  for(int i:middle){float ti=t(pos(i)),gap=std::abs(ti-t4*.5f);int k=torso::tier(m.bones[i]);if(ti<.04f||ti>t4-.04f||k>=2)continue;if(s2<0||k<bestTier||(k==bestTier&&gap<bestGap)){s2=i;bestTier=k;bestGap=gap;}}}
 c.spine2=s2;c.spine2Origin=valid(s2)?pos(s2):o1.lerp(c.spine4Origin,.5f);
 // A chain bone at the chest's place (a duplicate such as 上半身2+) moves with the chest.
 if(!degenerate&&valid(s4))for(int i:middle)if(i!=s2&&std::abs(t(pos(i))-t4)<.04f){c.spine4Aliases.push_back(i);note("coincident",{i,s4},label(i)+" is at the place of the chest "+label(s4)+": it moves with it");}
 if(!degenerate&&!pinned2&&!pinned4){
  if(valid(s2)&&valid(s4)&&torso::segment(m.bones[s4])>=2&&torso::segment(m.bones[s2])>torso::segment(m.bones[s4]))note("reordered",{s2,s4},label(s2)+" lies below "+label(s4)+": the middle spine follows "+label(s2)+" and the chest "+label(s4));
  // A 上半身3 off the chain below the chest (a leaf helper, or a neck base above it) is not a spine segment.
  auto onChain=[&](int b){return valid(b)&&(b==third||below(b,third));};
  if(valid(third)&&!onChain(s2)&&!onChain(s4)&&std::none_of(c.spine4Aliases.begin(),c.spine4Aliases.end(),onChain)&&!listed(c.spine2Aliases,third)&&!in.taken.contains(third))
   note("ignored",{third},label(third)+" does not hold the shoulders, so it is not the chest: it follows its parent");
 }
 return c;
}
}
