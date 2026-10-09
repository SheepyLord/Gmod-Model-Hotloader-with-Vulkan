#include "humanoid_map.hpp"
#include "spring_bones.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <set>
#include <stdexcept>
namespace mmd {
namespace {
using U32=std::u32string;
bool scalar(char32_t c){return c<=0x10FFFF&&(c<0xD800||c>0xDFFF);}
// Strict UTF-8: an overlong form, a UTF-16 surrogate (ED A0 80) or a code point past
// U+10FFFF (F4 90.., F5..F7) is not UTF-8, so its lead byte becomes U+FFFD like any
// other stray byte. JSON (status.json, the manifest) refuses invalid UTF-8.
U32 decode(std::string_view s){
 static constexpr char32_t least[]={0,0,0x80,0x800,0x10000};
 U32 out;out.reserve(s.size());
 for(size_t i=0;i<s.size();){unsigned char c=s[i];char32_t cp;int n;
  if(c<0x80){cp=c;n=1;}else if((c>>5)==6){cp=c&0x1f;n=2;}else if((c>>4)==14){cp=c&0x0f;n=3;}else if((c>>3)==30){cp=c&0x07;n=4;}else{out.push_back(0xFFFD);i++;continue;}
  bool ok=i+n<=s.size();for(int k=1;ok&&k<n;k++){unsigned char d=s[i+k];ok=(d&0xC0)==0x80;cp=(cp<<6)|(d&0x3f);}
  ok=ok&&cp>=least[n]&&scalar(cp);
  if(!ok){out.push_back(0xFFFD);i++;continue;}out.push_back(cp);i+=n;}
 return out;
}
std::string encode(const U32& s){
 std::string out;
 for(char32_t c:s){if(!scalar(c))c=0xFFFD;
  if(c<0x80)out+=char(c);else if(c<0x800){out+=char(0xC0|(c>>6));out+=char(0x80|(c&0x3f));}
  else if(c<0x10000){out+=char(0xE0|(c>>12));out+=char(0x80|((c>>6)&0x3f));out+=char(0x80|(c&0x3f));}
  else{out+=char(0xF0|(c>>18));out+=char(0x80|((c>>12)&0x3f));out+=char(0x80|((c>>6)&0x3f));out+=char(0x80|(c&0x3f));}}
 return out;
}
char32_t lowerAscii(char32_t c){return c>='A'&&c<='Z'?c+32:c;}
bool upper(char32_t c){return c>='A'&&c<='Z';}
bool lower(char32_t c){return c>='a'&&c<='z';}
bool digit(char32_t c){return c>='0'&&c<='9';}
bool separator(char32_t c){return c=='.'||c=='_'||c=='-'||c==' ';}
bool startsI(const U32& s,std::u32string_view p){if(s.size()<p.size())return false;for(size_t i=0;i<p.size();i++)if(lowerAscii(s[i])!=p[i])return false;return true;}
bool endsI(const U32& s,std::u32string_view p){if(s.size()<p.size())return false;size_t at=s.size()-p.size();for(size_t i=0;i<p.size();i++)if(lowerAscii(s[at+i])!=p[i])return false;return true;}
bool asciiText(std::string_view s){return std::all_of(s.begin(),s.end(),[](char c){return (unsigned char)c<0x80;});}
size_t codePoints(std::string_view s){return size_t(std::count_if(s.begin(),s.end(),[](char c){return ((unsigned char)c&0xC0)!=0x80;}));}
struct Token{const char* text;const char* family;};
// Body parts. "arm" and "leg" are settled per side (classifyBones); "eye" must be the whole name.
const Token BodyTokens[]={
 {"hips","pelvis"},{"hip","pelvis"},{"pelvis","pelvis"},{"lowerbody","pelvis"},{"下半身","pelvis"},{"骨盆","pelvis"},{"엉덩이","pelvis"},
 {"spine","spine"},{"chest","spine"},{"upperchest","spine"},{"chestlower","spine"},{"chestupper","spine"},{"abdomen","spine"},{"abdomenlower","spine"},{"abdomenupper","spine"},{"torso","spine"},{"waist","spine"},{"upperbody","spine"},{"上半身","spine"},{"脊椎","spine"},{"척추","spine"},
 {"neck","neck"},{"necklower","neck"},{"neckupper","neck"},{"首","neck"},{"颈","neck"},{"頸","neck"},{"목","neck"},
 {"head","head"},{"頭","head"},{"头","head"},{"머리","head"},
 {"shoulder","clavicle"},{"clavicle","clavicle"},{"collar","clavicle"},{"collarbone","clavicle"},{"scapula","clavicle"},{"肩","clavicle"},{"锁骨","clavicle"},{"어깨","clavicle"},
 {"upperarm","upperarm"},{"uparm","upperarm"},{"shldr","upperarm"},{"shldrbend","upperarm"},{"腕","upperarm"},{"上臂","upperarm"},{"위팔","upperarm"},{"arm","arm"},
 {"forearm","forearm"},{"lowerarm","forearm"},{"elbow","forearm"},{"forearmbend","forearm"},{"ひじ","forearm"},{"肘","forearm"},{"前臂","forearm"},{"아래팔","forearm"},
 {"hand","hand"},{"wrist","hand"},{"手首","hand"},{"손","hand"},
 {"upleg","thigh"},{"upperleg","thigh"},{"thigh","thigh"},{"thighbend","thigh"},{"足","thigh"},{"大腿","thigh"},{"허벅지","thigh"},{"leg","leg"},
 {"lowerleg","calf"},{"calf","calf"},{"shin","calf"},{"knee","calf"},{"ひざ","calf"},{"膝","calf"},{"小腿","calf"},{"종아리","calf"},
 {"foot","foot"},{"ankle","foot"},{"足首","foot"},{"발","foot"},
 {"toe","toe"},{"toes","toe"},{"toebase","toe"},{"ball","toe"},{"つま先","toe"},{"足先ex","toe"},{"발가락","toe"},
 {"thumb","thumb"},{"親指","thumb"},{"拇指","thumb"},
 {"index","index"},{"fore","index"},{"pointer","index"},{"人指","index"},{"人差指","index"},{"食指","index"},
 {"middle","middle"},{"mid","middle"},{"中指","middle"},
 {"ring","ring"},{"third","ring"},{"薬指","ring"},{"无名指","ring"},
 {"little","little"},{"pinky","little"},{"pinkie","little"},{"小指","little"}};
const char* EyeTokens[]={"eye","eyeball","faceeye","目","眼","눈"};
const char* FingerTokens[]={"thumb","index","fore","pointer","middle","mid","ring","third","little","pinky","pinkie"};
// Swinging parts, considered only when no body part matched (forearm is never an ear).
const Token ChainTokens[]={
 {"hair","hair"},{"kami","hair"},{"bang","hair"},{"fringe","hair"},{"ponytail","hair"},{"twintail","hair"},{"braid","hair"},{"ahoge","hair"},{"sidehair","hair"},{"sidelock","hair"},{"tress","hair"},{"髪","hair"},{"髮","hair"},{"发","hair"},{"头发","hair"},{"もみあげ","hair"},{"머리카락","hair"},
 {"skirt","skirt"},{"dress","skirt"},{"cloth","skirt"},{"coat","skirt"},{"cape","skirt"},{"cloak","skirt"},{"mantle","skirt"},{"robe","skirt"},{"frill","skirt"},{"apron","skirt"},{"sleeve","skirt"},{"スカート","skirt"},{"裙","skirt"},{"裾","skirt"},{"袖","skirt"},{"マント","skirt"},{"치마","skirt"},
 {"breast","chest"},{"bust","chest"},{"boob","chest"},{"oppai","chest"},{"mune","chest"},{"pai","chest"},{"おっぱい","chest"},{"乳","chest"},{"胸","chest"},
 {"tail","tail"},{"shippo","tail"},{"しっぽ","tail"},{"尻尾","tail"},{"尾巴","tail"},{"꼬리","tail"},
 {"ribbon","accessory"},{"earring","accessory"},{"ear","accessory"},{"kemomimi","accessory"},{"chain","accessory"},{"strap","accessory"},{"string","accessory"},{"bell","accessory"},{"tassel","accessory"},{"tie","accessory"},{"bow","accessory"},{"acc","accessory"},{"ornament","accessory"},{"scarf","accessory"},{"hood","accessory"},{"necklace","accessory"},{"リボン","accessory"},{"耳","accessory"},{"飾り","accessory"},{"紐","accessory"},{"ネクタイ","accessory"},{"マフラー","accessory"}};
// Accessories whose names contain a body word: they never take a body part.
const char* AccessoryWords[]={"necklace","earring","headband","headdress","headphone","headset","headwear","hairband","handle","armband","legband","shoulderpad","bracelet","anklet"};
const char* HelperTokens[]={"twist","roll","捩","ik","pole","target","ctrl","control","socket","attach","weapon","prop","dummy","null","nub","root","master","center","センター","グルーブ","腰","全ての親","操作中心","肩p","shoulderp","headtop","ダミー","両目",
 "shake","adv","fix","jiggle","bulge","assist","corrective","traj","tw"};
// Words that mark helpers unless the bone is a weighted body part (Arona's Ankle_offset_L is its ankle).
const char* WeakHelperTokens[]={"offset","helper"};
// ASCII tokens of up to 3 letters must be a whole word of the name, longer ones may be part
// of it; non-ASCII tokens are the whole name (or the name and a number), or for
// helpers and swinging parts any part of it.
bool matches(std::string_view token,const std::string& base,const std::vector<std::string>& words,bool anywhere){
 if(asciiText(token)){if(token.size()<=3)return std::find(words.begin(),words.end(),token)!=words.end();return base.find(token)!=std::string::npos;}
 if(anywhere)return base.find(token)!=std::string::npos;
 if(base==token)return true;
 return base.starts_with(token)&&std::all_of(base.begin()+token.size(),base.end(),[](char c){return c>='0'&&c<='9';});
}
}

std::string sanitizeBoneName(std::string_view raw,std::string* issue){
 if(raw.size()>4096)raw=raw.substr(0,4096);
 std::string out,why;
 auto convert=[&](UINT codepage){
  if(raw.empty()){out.clear();return true;}
  int n=MultiByteToWideChar(codepage,MB_ERR_INVALID_CHARS,raw.data(),int(raw.size()),nullptr,0);if(n<=0)return false;
  std::wstring w(size_t(n),L'\0');MultiByteToWideChar(codepage,MB_ERR_INVALID_CHARS,raw.data(),int(raw.size()),w.data(),n);out=utf8(w);return true;
 };
 if(convert(CP_UTF8)){}else if(convert(932))why="cp932";else if(convert(936))why="cp936";
 else{out=encode(decode(raw));why="replaced";}
 std::erase_if(out,[](char c){return (unsigned char)c<0x20||c==0x7F;});
 if(out.size()>255){size_t cut=255;while(cut>0&&((unsigned char)out[cut]&0xC0)==0x80)cut--;out.resize(cut);}
 if(issue)*issue=why;
 return out;
}

NameParts parseBoneName(std::string_view name,bool dazStyle){
 NameParts r;U32 s=decode(name);
 {U32 low;for(auto c:s)low.push_back(lowerAscii(c));if(low.find(U"$assimpfbx$")!=U32::npos)r.helper=true;}
 // 1. Namespaces: mixamorig:Hips, Armature|Hips.
 if(auto cut=s.find_last_of(U":|");cut!=U32::npos&&cut+1<s.size())s.erase(0,cut+1);
 // 2. Rig prefixes, repeatedly; a centre marker (C_) follows some of them.
 static const std::pair<std::u32string_view,int> prefixes[]={{U"valvebiped.",0},{U"bip01_",0},{U"bip01 ",0},{U"bip001 ",0},{U"bip001_",0},{U"bip002 ",0},{U"cc_base_",0},{U"character1_",0},{U"j_bip_",0},{U"j_adj_",0},{U"def-",0},{U"org-",1},{U"mch-",2},{U"wgt-",2},{U"j_sec_",3}};
 bool biped=false;
 for(bool again=true;again;){again=false;
  for(auto& [prefix,flag]:prefixes)if(s.size()>prefix.size()&&startsI(s,prefix)){
   s.erase(0,prefix.size());if(flag==1)r.nonDeform=true;else if(flag==2)r.helper=true;else if(flag==3)r.secondaryHint=true;
   if(s.size()>2&&lowerAscii(s[0])=='c'&&(s[1]=='_'||s[1]==' '))s.erase(0,2);
   again=true;break;}
  // 3ds Max Biped names exported without spaces: Bip001Pelvis, Bip001LUpperArm.
  for(std::u32string_view prefix:{U"bip001",U"bip002",U"bip01"})if(!again&&s.size()>prefix.size()+2&&startsI(s,prefix)&&upper(s[prefix.size()])){s.erase(0,prefix.size());biped=again=true;}}
 // 3. Full-width ASCII and the ideographic space.
 for(auto& c:s){if(c>=0xFF01&&c<=0xFF5E)c-=0xFEE0;else if(c==0x3000)c=' ';}
 auto trim=[&]{while(!s.empty()&&separator(s.front()))s.erase(0,1);while(!s.empty()&&separator(s.back()))s.pop_back();};trim();
 // 4. Side: the first rule that matches wins and its token is removed.
 auto take=[&](char side,size_t at,size_t count){r.side=side;s.erase(at,count);trim();return true;};
 bool sided=false;
 for(auto [token,side]:{std::pair{std::u32string_view(U"左側"),'L'},{U"右側",'R'},{U"左",'L'},{U"右",'R'},{U"왼쪽",'L'},{U"오른쪽",'R'},{U"왼",'L'},{U"오른",'R'}})
  if(!sided&&s.size()>token.size()&&s.compare(0,token.size(),token)==0)sided=take(side,0,token.size());
 for(auto [token,side]:{std::pair{std::u32string_view(U"left"),'L'},{U"right",'R'},{U"lft",'L'},{U"rgt",'R'},{U"l",'L'},{U"r",'R'}})
  if(!sided&&s.size()>token.size()+1&&separator(s[s.size()-token.size()-1])&&endsI(s,token))sided=take(side,s.size()-token.size()-1,token.size()+1);
 if(!sided&&s.size()>2&&(lowerAscii(s[0])=='l'||lowerAscii(s[0])=='r')&&(s[1]=='_'||s[1]=='.'||s[1]==' '))sided=take(lowerAscii(s[0])=='l'?'L':'R',0,2);
 if(!sided&&biped&&s.size()>3&&(s[0]=='L'||s[0]=='R')&&upper(s[1])&&lower(s[2]))sided=take(s[0]=='L'?'L':'R',0,1);
 for(auto [token,side]:{std::pair{std::u32string_view(U"left"),'L'},{U"right",'R'}})
  if(!sided&&s.size()>token.size()&&startsI(s,token)&&(upper(s[token.size()])||separator(s[token.size()])))sided=take(side,0,token.size());
 if(!sided&&dazStyle&&s.size()>=4&&(s[0]=='l'||s[0]=='r')&&upper(s[1]))sided=take(s[0]=='l'?'L':'R',0,1);
 if(!sided&&s.size()>1&&(s.back()==U'左'||s.back()==U'右'))sided=take(s.back()==U'左'?'L':'R',s.size()-1,1);
 // Words: split at separators, case changes, digits and script changes.
 std::vector<std::string> words;
 {U32 current;auto flush=[&]{if(current.empty())return;U32 low;for(auto c:current)low.push_back(lowerAscii(c));words.push_back(encode(low));current.clear();};
  for(size_t i=0;i<s.size();i++){char32_t c=s[i];if(separator(c)){flush();continue;}
   if(!current.empty()){char32_t p=current.back();
    if((lower(p)&&upper(c))||digit(p)!=digit(c)||(p<0x80)!=(c<0x80)||(upper(p)&&upper(c)&&i+1<s.size()&&lower(s[i+1])))flush();}
   current.push_back(c);}
  flush();}
 // 5-6. Fold (lowercase ASCII, no separators; other scripts kept) and split off the number.
 U32 folded;for(auto c:s)if(!separator(c))folded.push_back(lowerAscii(c));
 size_t end=folded.size();while(end>0&&digit(folded[end-1]))end--;
 if(end<folded.size()){r.digits=encode(folded.substr(end));r.number=std::stoi(r.digits.substr(r.digits.size()>6?r.digits.size()-6:0));}
 U32 baseText=folded.substr(0,end);
 // 7. MMD deform duplicates: 足D, ひざD, 足首D.
 if(!baseText.empty()&&baseText.back()=='d'&&std::any_of(baseText.begin(),baseText.end(),[](char32_t c){return c>=0x80;})){
  r.dbone=true;baseText.pop_back();if(!words.empty()&&words.back()=="d")words.pop_back();}
 std::string base=encode(baseText);
 if(!words.empty()){r.endToken=words.back()=="end";r.tipToken=words.back()=="tip";}
 r.metacarpal=base.find("metacarpal")!=std::string::npos;
 // 8. Family. A hand prefix before a finger is dropped (Mixamo HandIndex1).
 if(base.starts_with("hand"))for(auto f:FingerTokens)if(base.compare(4,std::strlen(f),f)==0){base.erase(0,4);break;}
 r.base=base;
 for(auto t:HelperTokens)if(matches(t,base,words,true)){r.helper=true;std::string_view v(t);r.twist|=v=="twist"||v=="roll"||v=="捩"||v=="tw";r.ik|=v=="ik";}
 for(auto t:WeakHelperTokens)if(matches(t,base,words,true))r.weakHelper=true;
 bool accessory=std::any_of(std::begin(AccessoryWords),std::end(AccessoryWords),[&](const char* w){return base.find(w)!=std::string::npos;});
 if(!accessory){
  size_t best=0;
  for(auto& t:BodyTokens)if(matches(t.text,base,words,false)){auto length=codePoints(t.text);if(length>best){best=length;r.family=t.family;}}
  for(auto t:EyeTokens)if(base==t){r.family="eye";break;}
  // Biped and ValveBiped: Finger0 is the thumb, Finger12 the index finger's third segment.
  if(base=="finger"&&!r.digits.empty()&&r.digits.size()<=2&&r.digits[0]<='4'){const char* fingers[]={"thumb","index","middle","ring","little"};
   r.family=fingers[r.digits[0]-'0'];r.number=r.digits.size()==2?r.digits[1]-'0'+1:1;}
  if(r.family=="arm"){r.family="upperarm";r.armToken=true;}
  if(r.family=="leg"){r.family="thigh";r.legToken=true;}
 }
 for(auto& t:ChainTokens)if(matches(t.text,base,words,true)){r.chain=t.family;break;}
 if(accessory&&r.chain.empty())r.chain="accessory";
 return r;
}

namespace {
// Parent links, depths and an Euler tour: ancestry tests in O(1).
struct Tree {
 std::vector<std::vector<int>> children;std::vector<int> parent,depth,tin,tout,order;
 explicit Tree(const SkeletonView& v){
  size_t n=v.bones.size();children.resize(n);parent.resize(n);depth.assign(n,1);tin.assign(n,0);tout.assign(n,0);
  for(size_t i=0;i<n;i++){int p=v.bones[i].parent;parent[i]=p>=0&&size_t(p)<n&&p!=int(i)?p:-1;if(parent[i]>=0)children[parent[i]].push_back(int(i));}
  int clock=0;std::vector<std::pair<int,size_t>> stack;std::vector<uint8_t> seen(n,0);
  for(size_t r=0;r<n;r++){if(parent[r]>=0)continue;stack.push_back({int(r),0});seen[r]=1;tin[r]=clock++;order.push_back(int(r));
   while(!stack.empty()){auto& [b,k]=stack.back();if(k<children[b].size()){int c=children[b][k++];if(seen[c])continue;seen[c]=1;depth[c]=depth[b]+1;tin[c]=clock++;order.push_back(c);stack.push_back({c,0});}else{tout[b]=clock;stack.pop_back();}}}
  // Bones in a parent cycle never reach a root; treat them as roots of their own.
  for(size_t i=0;i<n;i++)if(!seen[i]){parent[i]=-1;seen[i]=1;tin[i]=clock++;tout[i]=clock;order.push_back(int(i));}
 }
 bool below(int a,int b)const{return a>=0&&b>=0&&a!=b&&tin[b]<tin[a]&&tin[a]<tout[b];}  // a strictly under b
 bool underOrSelf(int a,int b)const{return a==b||below(a,b);}
 int nca(int a,int b)const{if(a<0)return b;if(b<0)return a;while(a>=0&&!underOrSelf(b,a))a=parent[a];return a;}
 // Bones from `top` (exclusive, -1: from the root) down to `bottom` (inclusive).
 std::vector<int> path(int top,int bottom)const{std::vector<int> p;for(int b=bottom;b>=0&&b!=top;b=parent[b])p.push_back(b);std::reverse(p.begin(),p.end());return p;}
};
float length3(const std::array<float,3>& a,const std::array<float,3>& b){float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];return std::sqrt(x*x+y*y+z*z);}
struct Extent {float minY=0,maxY=0,height=0;};
Extent extent(const SkeletonView& v){
 Extent e;bool any=false;auto add=[&](float y){if(!any){e.minY=e.maxY=y;any=true;}e.minY=std::min(e.minY,y);e.maxY=std::max(e.maxY,y);};
 for(size_t i=1;i+2<v.points.size();i+=4)add(v.points[i]);
 if(!any)for(auto& b:v.bones)add(b.position[1]);
 // The body's height: up to its highest weighted bone, so a raised weapon or a long
 // staff in the mesh does not stretch every threshold (half the mesh at least).
 float top=-1e30f;for(auto& b:v.bones)if(b.weighted)top=std::max(top,b.position[1]);
 float mesh=std::max(1e-3f,e.maxY-e.minY);e.height=top>e.minY?std::max(top-e.minY,.5f*mesh):mesh;return e;
}
float median(std::vector<float> a){if(a.empty())return 0;size_t k=a.size()/2;std::nth_element(a.begin(),a.begin()+k,a.end());float upper=a[k];if(a.size()%2)return upper;return (*std::max_element(a.begin(),a.begin()+k)+upper)/2;}
bool sidedFamily(const std::string& f){return f=="eye"||f=="clavicle"||f=="upperarm"||f=="forearm"||f=="hand"||f=="thigh"||f=="calf"||f=="foot"||f=="toe"||f=="thumb"||f=="index"||f=="middle"||f=="ring"||f=="little";}
bool fingerFamily(const std::string& f){return f=="thumb"||f=="index"||f=="middle"||f=="ring"||f=="little";}
bool chainKind(const std::string& m){return m=="hair"||m=="skirt"||m=="chest"||m=="tail"||m=="accessory";}
// Partner names: the side token swapped with its exact spelling and case.
std::vector<std::string> swappedNames(const std::string& raw,bool dazStyle){
 std::vector<std::string> out;
 auto suffix=[&](const char* a,const char* b){std::string_view x(a),y(b);for(auto [p,q]:{std::pair{x,y},{y,x}})if(raw.size()>p.size()&&raw.ends_with(p))out.push_back(raw.substr(0,raw.size()-p.size())+std::string(q));};
 for(auto [a,b]:{std::pair{".L",".R"},{".l",".r"},{"_L","_R"},{"_l","_r"},{" L"," R"},{"-L","-R"},{"_Left","_Right"},{"_left","_right"},{".Left",".Right"},{" Left"," Right"},{"Lft","Rgt"},{"左","右"}})suffix(a,b);
 auto ns=raw.find_last_of(":|");size_t at=ns==std::string::npos?0:ns+1;std::string head=raw.substr(0,at),rest=raw.substr(at);
 for(auto [a,b]:{std::pair{"L_","R_"},{"l_","r_"},{"L ","R "},{"L.","R."},{"Left","Right"},{"left","right"},{"LEFT","RIGHT"},{"左","右"},{"왼쪽","오른쪽"},{"왼","오른"}}){std::string_view x(a),y(b);
  for(auto [p,q]:{std::pair{x,y},{y,x}})if(rest.size()>p.size()&&rest.starts_with(p))out.push_back(head+std::string(q)+rest.substr(p.size()));}
 for(auto [a,b]:{std::pair{" L "," R "},{"_L_","_R_"},{".L.",".R."},{"Left","Right"},{"left","right"},{"LEFT","RIGHT"},{"左","右"}}){std::string_view x(a),y(b);
  for(auto [p,q]:{std::pair{x,y},{y,x}}){auto k=raw.find(p);if(k!=std::string::npos)out.push_back(raw.substr(0,k)+std::string(q)+raw.substr(k+p.size()));}}
 if(dazStyle&&rest.size()>=2&&(rest[0]=='l'||rest[0]=='r')&&rest[1]>='A'&&rest[1]<='Z')out.push_back(head+(rest[0]=='l'?"r":"l")+rest.substr(1));
 return out;
}
bool dazName(const std::string& name){auto ns=name.find_last_of(":|");std::string_view s(name);if(ns!=std::string::npos)s=s.substr(ns+1);return s.size()>=2&&(s[0]=='l'||s[0]=='r')&&s[1]>='A'&&s[1]<='Z';}
}

std::vector<BoneMeaning> classifyBones(const SkeletonView& v){
 size_t n=v.bones.size();std::vector<BoneMeaning> out(n);if(!n)return out;
 Tree tree(v);auto ext=extent(v);float H=ext.height;
 bool daz=std::count_if(v.bones.begin(),v.bones.end(),[](const SkeletonBone& b){return dazName(b.name);})>=8;
 std::vector<NameParts> parts(n);
 for(size_t i=0;i<n;i++){auto& b=v.bones[i];parts[i]=parseBoneName(b.name,daz);
  // A name the table does not know may have a readable English name (PMX).
  if(parts[i].family.empty()&&parts[i].chain.empty()&&!parts[i].helper&&!b.english.empty()&&b.english!=b.name){
   auto e=parseBoneName(b.english,daz);if(!e.family.empty()||!e.chain.empty()||e.helper){if(!e.side)e.side=parts[i].side;parts[i]=e;}}}
 std::vector<float> xs;for(auto& b:v.bones)if(b.weighted)xs.push_back(b.position[0]);float mid=median(xs);
 for(size_t i=0;i<n;i++){auto& p=parts[i];auto& m=out[i];
  bool leaf=tree.children[i].empty();
  m.helper=p.helper||p.ik||p.endToken||(p.tipToken&&leaf)||(p.weakHelper&&(p.family.empty()||!v.bones[i].weighted));m.ik=p.ik;m.twist=p.twist;m.dbone=p.dbone;m.nonDeform=p.nonDeform;m.secondaryHint=p.secondaryHint;m.side=p.side;
  for(auto& f:v.bones[i].flags){if(f=="ik")m.ik=m.helper=true;if(f=="helper")m.helper=true;if(f=="twist")m.twist=true;}
  if(!m.side&&sidedFamily(p.family)){float lateral=v.bones[i].position[0]-mid;if(std::abs(lateral)>.02f*H)m.side=lateral>0?'L':'R';}
 }
 // A hip with a side is a hip joint, the thigh (Advanced Skeleton Hip_L); beside a
 // scapula, the shoulder is the upper arm (Shoulder_L, Elbow_L, Wrist_L).
 for(char side:{'L','R'}){bool scapula=false;for(size_t i=0;i<n;i++)scapula|=out[i].side==side&&parts[i].family=="clavicle"&&parts[i].base.find("scapula")!=std::string::npos;
  for(size_t i=0;i<n;i++){if(out[i].side!=side)continue;auto& p=parts[i];if(p.family=="pelvis"&&p.base.starts_with("hip")&&!p.base.starts_with("hips"))p.family="thigh";if(scapula&&p.family=="clavicle"&&p.base.find("shoulder")!=std::string::npos)p.family="upperarm";}}
 // "arm" is the upper arm only on a side without an upper-arm bone; "leg" is the calf
 // beside an upper leg (Mixamo LeftUpLeg/LeftLeg), the thigh beside a knee, else
 // the upper of two leg bones in a chain.
 for(char side:{'L','R'}){
  bool upperArm=false,thigh=false,calf=false;std::vector<int> legs;
  for(size_t i=0;i<n;i++){if(out[i].side!=side||out[i].helper)continue;auto& p=parts[i];
   upperArm|=p.family=="upperarm"&&!p.armToken;thigh|=p.family=="thigh"&&!p.legToken;calf|=p.family=="calf";if(p.legToken)legs.push_back(int(i));}
  for(size_t i=0;i<n;i++)if(out[i].side==side&&parts[i].armToken&&upperArm)parts[i].family.clear();
  for(int i:legs){auto& p=parts[i];
   if(thigh)p.family="calf";else if(calf)p.family="thigh";
   else{bool lower=std::any_of(legs.begin(),legs.end(),[&](int o){return tree.below(i,o);});p.family=lower?"calf":"thigh";}}
 }
 // Fingers hang below a hand: Hair_Middle is hair and EarRing an accessory.
 bool anyHand=std::any_of(parts.begin(),parts.end(),[](const NameParts& p){return p.family=="hand";});
 for(size_t i=0;i<n;i++)if(fingerFamily(parts[i].family)&&anyHand){bool under=false;for(int a=tree.parent[i];a>=0&&!under;a=tree.parent[a])under=parts[a].family=="hand";if(!under)parts[i].family.clear();}
 for(size_t i=0;i<n;i++){auto& m=out[i];auto& p=parts[i];
  m.meaning=m.helper?"helper":!p.family.empty()?p.family:p.chain;
  if(p.helper&&!p.family.empty()&&!m.helper)m.meaning=p.family;}
 // Finger segments: the first three bones of each finger by depth (metacarpals of
 // the four fingers are not segments; a thumb's is).
 for(char side:{'L','R'})for(auto finger:{"thumb","index","middle","ring","little"}){
  std::vector<int> list;for(size_t i=0;i<n;i++)if(out[i].side==side&&out[i].meaning==finger&&(std::string(finger)=="thumb"||!parts[i].metacarpal))list.push_back(int(i));
  std::stable_sort(list.begin(),list.end(),[&](int a,int b){return tree.depth[a]<tree.depth[b];});
  for(size_t k=0;k<list.size();k++)out[list[k]].segment=k<3?int(k)+1:0;}
 // Mirror partners: by swapped side token, else by mirrored position.
 std::map<std::string,int> byName;for(size_t i=0;i<n;i++)byName.emplace(v.bones[i].name,int(i));
 for(size_t i=0;i<n;i++){if(out[i].mirror>=0)continue;
  for(auto& candidate:swappedNames(v.bones[i].name,daz)){auto it=byName.find(candidate);if(it!=byName.end()&&it->second!=int(i)&&out[it->second].mirror<0){out[i].mirror=it->second;out[it->second].mirror=int(i);break;}}}
 struct Claim{float distance;int a,b;};std::vector<Claim> claims;
 for(size_t i=0;i<n;i++){if(out[i].mirror>=0)continue;float lateral=v.bones[i].position[0]-mid;if(!out[i].side&&std::abs(lateral)<.02f*H)continue;
  std::array<float,3> target{2*mid-v.bones[i].position[0],v.bones[i].position[1],v.bones[i].position[2]};int best=-1;float distance=.03f*H;
  for(size_t k=0;k<n;k++){if(k==i||out[k].mirror>=0||std::abs(tree.depth[k]-tree.depth[i])>1)continue;float d=length3(target,v.bones[k].position);if(d<=distance){distance=d;best=int(k);}}
  if(best>=0)claims.push_back({distance,int(i),best});}
 std::sort(claims.begin(),claims.end(),[](const Claim& a,const Claim& b){return a.distance!=b.distance?a.distance<b.distance:a.a<b.a;});
 for(auto& c:claims)if(out[c.a].mirror<0&&out[c.b].mirror<0){out[c.a].mirror=c.b;out[c.b].mirror=c.a;}
 return out;
}

namespace {
// The automatic assignment (spec: humanoid engine, steps 1-6).
struct Guesser {
 const SkeletonView& v;const std::vector<BoneMeaning>& m;Tree tree;Extent ext;float H;size_t n;
 std::vector<uint32_t> subtreeWeight;std::vector<uint8_t> candidate;std::map<std::string,SlotGuess> slots;std::set<int> used;float mid=0;
 Guesser(const SkeletonView& view,const std::vector<BoneMeaning>& meanings):v(view),m(meanings),tree(view),ext(extent(view)),H(ext.height),n(view.bones.size()){
  subtreeWeight.assign(n,0);for(size_t k=tree.order.size();k-->0;){int b=tree.order[k];subtreeWeight[b]+=v.bones[b].weighted;if(tree.parent[b]>=0)subtreeWeight[tree.parent[b]]+=subtreeWeight[b];}
  bool def=std::any_of(v.bones.begin(),v.bones.end(),[](const SkeletonBone& b){auto ns=b.name.find_last_of(":|");auto s=ns==std::string::npos?b.name:b.name.substr(ns+1);return s.size()>4&&(s.starts_with("DEF-")||s.starts_with("def-"));});
  candidate.assign(n,0);for(size_t i=0;i<n;i++)candidate[i]=subtreeWeight[i]>0&&!m[i].helper&&!m[i].ik&&!(m[i].nonDeform&&def);
  std::vector<float> xs;for(auto& b:v.bones)if(b.weighted)xs.push_back(b.position[0]);mid=median(xs);
 }
 float x(int b)const{return v.bones[b].position[0];}float y(int b)const{return v.bones[b].position[1];}float z(int b)const{return v.bones[b].position[2];}
 float lateral(int b)const{return x(b)-mid;}
 // More weighted vertices, then a heavier subtree, then shallower, then lower index.
 bool better(int a,int b)const{if(b<0)return true;if(v.bones[a].weighted!=v.bones[b].weighted)return v.bones[a].weighted>v.bones[b].weighted;if(subtreeWeight[a]!=subtreeWeight[b])return subtreeWeight[a]>subtreeWeight[b];if(tree.depth[a]!=tree.depth[b])return tree.depth[a]<tree.depth[b];return a<b;}
 int bone(const char* key)const{auto it=slots.find(key);return it==slots.end()?-1:it->second.bone;}
 int anchorOf(const SlotInfo& s)const{for(auto a:s.anchors)if(a){int b=bone(a);if(b>=0)return b;}return -1;}
 void set(const std::string& key,int b,float confidence,const char* method){if(b<0)return;auto& g=slots[key];if(g.bone>=0)used.erase(g.bone);g={b,confidence,method};used.insert(b);}
 bool free(int b)const{return b>=0&&!used.contains(b);}
 float nameConfidence(const SlotInfo& s,int b)const{
  bool side=!s.side||std::abs(lateral(b))<.02f*H||(s.side=='L')==(lateral(b)>0);int anchor=anchorOf(s);bool order=anchor<0||tree.below(b,anchor);
  return side&&order?1.f:side||order?.8f:.6f;
 }
 std::vector<int> named(const SlotInfo& s)const{std::vector<int> list;for(size_t i=0;i<n;i++)if(candidate[i]&&m[i].meaning==s.family&&m[i].side==s.side)list.push_back(int(i));std::sort(list.begin(),list.end(),[&](int a,int b){return better(a,b);});return list;}
 // Step 1: names, except the spine and the fingers.
 void namePass(){
  // A skeleton that already uses the carrier's names (ValveBiped) maps one to one.
  for(auto& s:humanoidSlots())for(size_t i=0;i<n;i++)if(candidate[i]&&v.bones[i].name==s.key&&free(int(i))){set(s.key,int(i),1,"name");break;}
  std::array<int,2> thighs{-1,-1};
  for(int k=0;k<2;k++){auto s=slotByKey(k?"ValveBiped.Bip01_R_Thigh":"ValveBiped.Bip01_L_Thigh");auto list=named(*s);if(!list.empty())thighs[k]=list.front();}
  int hipsOfThighs=thighs[0]>=0&&thighs[1]>=0?tree.nca(thighs[0],thighs[1]):-1;
  for(auto& s:humanoidSlots()){std::string f=s.family;if(f=="spine"||fingerFamily(f)||f=="eye"||bone(s.key)>=0)continue;
   auto list=named(s);
   if(f=="pelvis"){
    // The hips hold both thighs; Rigify names them DEF-spine.
    auto rank=[&](int b){return hipsOfThighs<0?2:b==hipsOfThighs?0:tree.below(hipsOfThighs,b)?1:2;};
    std::stable_sort(list.begin(),list.end(),[&](int a,int b){return rank(a)<rank(b);});
    if((list.empty()||rank(list.front())==2)&&hipsOfThighs>=0&&!m[hipsOfThighs].ik&&subtreeWeight[hipsOfThighs]&&free(hipsOfThighs)){set(s.key,hipsOfThighs,m[hipsOfThighs].meaning=="spine"?.9f:.85f,"name");continue;}
   }
   for(int b:list)if(free(b)){set(s.key,b,nameConfidence(s,b),"name");break;}
  }
 }
 // A part whose next part does not hang below it takes the bone of its family that
 // holds that next part: a weighted copy of the calf (Bip001_L_Calf) beside the
 // calf the foot hangs from (Bip001LCalf).
 void repairChains(){
  auto all=humanoidSlots();
  for(size_t k=all.size();k-->0;){auto& child=all[k];if(fingerFamily(child.family)||child.convertOnly||!child.anchors[0])continue;
   int c=bone(child.key),p=bone(child.anchors[0]);if(c<0||p<0||tree.below(c,p))continue;auto parent=slotByKey(child.anchors[0]);if(!parent||parent->family==std::string("spine"))continue;
   auto it=slots.find(parent->key);if(it==slots.end()||it->second.method!="name")continue;
   for(int b:named(*parent))if(b!=p&&free(b)&&tree.below(c,b)){set(parent->key,b,nameConfidence(*parent,b),"name");break;}}
 }
 // Step 2: the torso by topology, as the fitter resolves it. fillOnly keeps what
 // is assigned and fills the rest (after the shape of the skeleton found the ends).
 void torso(bool fillOnly){
  if(!fillOnly&&bone("ValveBiped.Bip01_Spine1")>=0)fillOnly=true;
  const char *P="ValveBiped.Bip01_Pelvis",*S1="ValveBiped.Bip01_Spine1",*S2="ValveBiped.Bip01_Spine2",*S4="ValveBiped.Bip01_Spine4",*N="ValveBiped.Bip01_Neck1";
  auto open=[&](int b,const char* key){return b>=0&&(free(b)||bone(key)==b);};
  int pelvis=bone(P),neck=bone(N),head=bone("ValveBiped.Bip01_Head1");
  int left=bone("ValveBiped.Bip01_L_Clavicle");if(left<0)left=bone("ValveBiped.Bip01_L_UpperArm");
  int right=bone("ValveBiped.Bip01_R_Clavicle");if(right<0)right=bone("ValveBiped.Bip01_R_UpperArm");
  std::vector<int> anchors;for(int a:{neck>=0?neck:head,left,right})if(a>=0)anchors.push_back(a);if(anchors.size()<2)return;
  int chest=anchors[0];for(int a:anchors)chest=tree.nca(chest,a);if(chest<0)return;
  int from=pelvis>=0?tree.parent[pelvis]:-1;if(from>=0&&!tree.underOrSelf(chest,from))from=-1;
  auto path=tree.path(from,chest);
  auto outsidePelvis=[&](int b){return pelvis<0||!(b==pelvis||tree.below(pelvis,b));};
  int spine1=fillOnly?bone(S1):-1;
  if(spine1<0){
   // A named spine may start level with the hips (MMD 上半身 sits 1.5 cm above 下半身);
   // an unnamed pick must be clearly above them.
   float hips=pelvis>=0?y(pelvis):ext.minY;bool named=false;
   for(int b:path)if(outsidePelvis(b)&&m[b].meaning=="spine"&&y(b)>hips-.02f*H&&open(b,S1)){spine1=b;named=true;break;}
   if(spine1<0)for(int b:path)if(outsidePelvis(b)&&!m[b].helper&&y(b)>hips+.02f*H&&open(b,S1)){spine1=b;break;}
   if(spine1<0)return;
   set(S1,spine1,named?.95f:.7f,"topology");
  }
  int spine4=fillOnly?bone(S4):-1;
  if(spine4<0){
   spine4=chest;
   if(spine4==spine1&&left>=0&&right>=0){int shoulders=tree.nca(left,right);if(tree.below(shoulders,spine1))spine4=shoulders;}
   if(!tree.below(spine4,spine1)||!open(spine4,S4))spine4=-1;
   if(spine4>=0)set(S4,spine4,m[spine4].meaning=="spine"?.95f:.7f,"topology");
  }
  int ref=neck>=0?neck:head;
  if(spine4>=0&&ref>=0&&!(fillOnly&&bone(S2)>=0)){
   auto& o=v.bones[spine1].position;std::array<float,3> u{x(ref)-o[0],y(ref)-o[1],z(ref)-o[2]};float L2=u[0]*u[0]+u[1]*u[1]+u[2]*u[2];
   if(L2>1e-10f){auto t=[&](int b){auto& p=v.bones[b].position;return ((p[0]-o[0])*u[0]+(p[1]-o[1])*u[1]+(p[2]-o[2])*u[2])/L2;};float t4=t(spine4);int best=-1;float distance=0;
    for(int b:tree.path(spine1,spine4)){if(b==spine4||m[b].meaning!="spine"||!open(b,S2))continue;float tb=t(b);if(tb<.04f||tb>t4-.04f)continue;float d=std::abs(tb-t4/2);if(best<0||d<distance){best=b;distance=d;}}
    if(best>=0)set(S2,best,.95f,"topology");}}
  if(head>=0&&!(fillOnly&&neck>=0)){
   int base=spine4>=0?spine4:spine1;auto chain=tree.path(base,head);if(!chain.empty()&&chain.back()==head)chain.pop_back();
   int pick=-1;float confidence=.95f;
   for(int b:chain)if(m[b].meaning=="neck"&&open(b,N)){pick=b;break;}
   if(pick<0&&!chain.empty()&&!m[chain.front()].helper&&open(chain.front(),N)){pick=chain.front();confidence=.7f;}
   if(pick>=0&&pick!=neck){if(neck>=0){used.erase(neck);slots.erase(N);}set(N,pick,confidence,"topology");}
   else if(pick<0&&neck>=0&&!tree.below(neck,base)){used.erase(neck);slots.erase(N);}
  }
 }
 // Step 3: the shape of the skeleton, for required parts names did not find.
 void topology(){
  bool needed=false;for(auto& s:humanoidSlots())if(s.required&&bone(s.key)<0)needed=true;if(!needed)return;
  std::map<std::string,SlotGuess> found;std::set<int> taken;
  auto put=[&](const char* key,int b,float confidence){if(b>=0){found[key]={b,confidence,"topology"};taken.insert(b);}};
  std::vector<int> low;for(size_t i=0;i<n;i++)if(candidate[i]&&y(int(i))<ext.minY+.12f*H)low.push_back(int(i));
  if(low.size()<2)return;
  int footL=*std::max_element(low.begin(),low.end(),[&](int a,int b){return lateral(a)<lateral(b);});
  int footR=*std::min_element(low.begin(),low.end(),[&](int a,int b){return lateral(a)<lateral(b);});
  if(footL==footR||lateral(footL)<=0||lateral(footR)>=0)return;
  int headEnd=-1;for(size_t i=0;i<n;i++)if(candidate[i]&&std::abs(lateral(int(i)))<.08f*H&&(headEnd<0||y(int(i))>y(headEnd)))headEnd=int(i);
  std::array<int,2> hands{-1,-1};
  for(size_t i=0;i<n;i++){if(!candidate[i])continue;float h=y(int(i))-ext.minY;if(h<.45f*H||h>.95f*H)continue;int k=lateral(int(i))>0?0:1;if(hands[k]<0||std::abs(lateral(int(i)))>std::abs(lateral(hands[k])))hands[k]=int(i);}
  if(headEnd<0||hands[0]<0||hands[1]<0)return;
  int pelvis=tree.nca(footL,footR);if(pelvis<0)return;
  if(y(pelvis)<ext.minY+.25f*H){for(int b:tree.path(pelvis,footL))if(y(b)>ext.minY+.35f*H){pelvis=b;break;}}
  int a=tree.nca(hands[0],hands[1]),body=tree.nca(pelvis,headEnd);int spine4=a>=0&&body>=0&&tree.below(a,body)?a:tree.nca(a,headEnd);
  if(spine4<0)return;
  // Spine1: the first bone on the way up that is not the pelvis or above it.
  int spine1=-1;for(int b:tree.path(tree.parent[pelvis],spine4))if(b!=pelvis&&!tree.below(pelvis,b)){spine1=b;break;}
  if(spine1<0||spine1==spine4)spine1=-1;
  put("ValveBiped.Bip01_Pelvis",pelvis,.7f);put("ValveBiped.Bip01_Spine1",spine1,.7f);if(spine1>=0&&spine4!=spine1)put("ValveBiped.Bip01_Spine4",spine4,.7f);
  auto neckPath=tree.path(spine4,headEnd);
  if(neckPath.size()==1)put("ValveBiped.Bip01_Head1",neckPath[0],.7f);
  else if(neckPath.size()>1){put("ValveBiped.Bip01_Neck1",neckPath[0],.7f);int head=-1;for(size_t k=1;k<neckPath.size();k++)if(head<0||v.bones[neckPath[k]].weighted>v.bones[head].weighted)head=neckPath[k];put("ValveBiped.Bip01_Head1",head,.7f);}
  for(int k=0;k<2;k++){const char* side=k?"R_":"L_";auto key=[&](const char* part){return std::string("ValveBiped.Bip01_")+side+part;};
   std::vector<int> arm;for(int b:tree.path(spine4,hands[k]))if(!m[b].twist)arm.push_back(b);if(arm.size()<3)continue;
   int hand=-1;for(size_t q=arm.size();q-->0&&hand<0;){int b=arm[q];int chains=0;for(int c:tree.children[b])chains+=subtreeWeight[c]>0;if(chains>=3)hand=b;}
   if(hand<0)for(size_t q=arm.size();q-->0&&hand<0;)if(v.bones[arm[q]].weighted)hand=arm[q];
   if(hand<0)continue;  // nothing on the way moves the mesh (weights on twist or helper bones)
   size_t h=std::find(arm.begin(),arm.end(),hand)-arm.begin();if(h<2)continue;
   size_t first=0;if(h+1>=4&&length3(v.bones[arm[0]].position,v.bones[arm[1]].position)<.12f*H){put(key("Clavicle").c_str(),arm[0],.7f);first=1;}
   int upper=arm[first];float reach=length3(v.bones[upper].position,v.bones[hand].position);int fore=-1;float best=0;
   for(size_t q=first+1;q<h;q++){float d=std::abs(length3(v.bones[upper].position,v.bones[arm[q]].position)-.53f*reach);if(fore<0||d<best){fore=arm[q];best=d;}}
   if(fore<0)continue;
   float ratio=length3(v.bones[fore].position,v.bones[hand].position)/std::max(1e-5f,length3(v.bones[upper].position,v.bones[fore].position));float c=ratio<.45f||ratio>2.2f?.55f:.7f;
   put(key("UpperArm").c_str(),upper,c);put(key("Forearm").c_str(),fore,c);put(key("Hand").c_str(),hand,c);}
  for(int k=0;k<2;k++){const char* side=k?"R_":"L_";auto key=[&](const char* part){return std::string("ValveBiped.Bip01_")+side+part;};
   auto leg=tree.path(pelvis,k?footR:footL);if(leg.size()<3)continue;
   size_t t=leg.size()>=5&&length3(v.bones[leg[0]].position,v.bones[leg[1]].position)<.05f*H?1:0;int thigh=leg[t];
   int foot=leg.back(),toe=-1;int last=leg.back(),before=leg[leg.size()-2];
   if(y(last)<ext.minY+.04f*H&&z(last)-z(before)>.03f*H&&leg.size()-t>=4){toe=last;foot=before;}
   float reach=length3(v.bones[thigh].position,v.bones[foot].position);int calf=-1;float best=0;
   for(int b:leg){if(b==thigh||b==foot||b==toe||!tree.below(b,thigh)||!tree.below(foot,b))continue;float d=std::abs(length3(v.bones[thigh].position,v.bones[b].position)-.5f*reach);if(calf<0||d<best){calf=b;best=d;}}
   if(calf<0)continue;
   float ratio=length3(v.bones[calf].position,v.bones[foot].position)/std::max(1e-5f,length3(v.bones[thigh].position,v.bones[calf].position));float c=ratio<.5f||ratio>2.f?.55f:.7f;
   put(key("Thigh").c_str(),thigh,c);put(key("Calf").c_str(),calf,c);put(key("Foot").c_str(),foot,c);if(toe>=0)put(key("Toe0").c_str(),toe,c);}
  for(auto& s:humanoidSlots()){auto it=found.find(s.key);if(it==found.end()||bone(s.key)>=0||!free(it->second.bone))continue;set(s.key,it->second.bone,it->second.confidence,"topology");}
 }
 // Step 4: fingers, by name, when they hang in one chain under their hand.
 void fingers(){
  for(char side:{'L','R'}){int hand=bone(side=='L'?"ValveBiped.Bip01_L_Hand":"ValveBiped.Bip01_R_Hand");if(hand<0)continue;
   for(auto finger:{"thumb","index","middle","ring","little"}){std::array<int,3> seg{-1,-1,-1};bool exact=false;
    for(auto& s:humanoidSlots())if(s.side==side&&s.family==std::string(finger)&&bone(s.key)>=0)exact=true;if(exact)continue;
    for(size_t i=0;i<n;i++)if(m[i].side==side&&m[i].meaning==finger&&m[i].segment>=1&&!m[i].helper&&free(int(i))&&tree.below(int(i),hand))seg[m[i].segment-1]=int(i);
    bool chain=true;int last=hand;for(int s:seg)if(s>=0){chain&=tree.below(s,last);last=s;}if(!chain)continue;
    for(auto& s:humanoidSlots())if(s.side==side&&s.family==std::string(finger)&&seg[s.segment-1]>=0)set(s.key,seg[s.segment-1],nameConfidence(s,seg[s.segment-1]),"name");}}
 }
 SlotGuess eye(char side){int head=bone("ValveBiped.Bip01_Head1");SlotGuess g;if(head<0)return g;int best=-1;
  for(size_t i=0;i<n;i++)if(m[i].meaning=="eye"&&m[i].side==side&&!m[i].helper&&free(int(i))&&tree.below(int(i),head)&&(best<0||better(int(i),best)))best=int(i);
  if(best>=0){g={best,1,"name"};used.insert(best);}return g;}
};
}

HumanoidGuess guessHumanoid(const SkeletonView& v,const std::vector<BoneMeaning>& meanings){
 HumanoidGuess out;if(v.bones.empty()||meanings.size()!=v.bones.size())return out;
 Guesser g(v,meanings);
 g.namePass();g.repairChains();
 if(int pelvis=g.bone("ValveBiped.Bip01_Pelvis");pelvis>=0)g.mid=g.x(pelvis);
 g.torso(false);g.topology();g.torso(true);
 g.fingers();out.eyeL=g.eye('L');out.eyeR=g.eye('R');
 // Names now meet their final anchors.
 for(auto& s:humanoidSlots()){auto it=g.slots.find(s.key);if(it!=g.slots.end()&&it->second.method=="name"&&it->second.confidence<1)it->second.confidence=std::max(it->second.confidence,g.nameConfidence(s,it->second.bone));}
 // Step 6: guesses under 0.5 are left for the player.
 size_t found=0;
 for(auto& s:humanoidSlots()){if(s.convertOnly)continue;auto it=g.slots.find(s.key);SlotGuess guess=it==g.slots.end()?SlotGuess{}:it->second;
  if(guess.confidence<.5f)guess=SlotGuess{};out.slots[s.key]=guess;if(s.required&&guess.bone>=0)found++;}
 out.candidates=size_t(std::count(g.candidate.begin(),g.candidate.end(),1));
 out.humanoid=found>=6&&out.candidates>=15;
 return out;
}

std::string skeletonSignature(const SkeletonView& v,const std::vector<BoneMeaning>& meanings){
 bool daz=std::count_if(v.bones.begin(),v.bones.end(),[](const SkeletonBone& b){return dazName(b.name);})>=8;
 std::set<std::string> parts;for(size_t i=0;i<v.bones.size();i++)if(v.bones[i].weighted){auto p=parseBoneName(v.bones[i].name,daz);char side=i<meanings.size()?meanings[i].side:p.side;parts.insert(p.base+"|"+(side?std::string(1,side):std::string()));}
 std::string text;for(auto& p:parts){if(!text.empty())text+="\n";text+=p;}
 return hash(std::span(reinterpret_cast<const unsigned char*>(text.data()),text.size()));
}
int skeletonMaxDepth(const SkeletonView& v){if(v.bones.empty())return 0;Tree t(v);return *std::max_element(t.depth.begin(),t.depth.end());}

SkeletonView skeletonFromModel(const Model& model,int maxPoints){
 SkeletonView v;size_t n=model.bones.size();v.bones.resize(n);
 std::vector<uint8_t> secondary(n,0),ik(n,0);
 for(auto b:model.bodies)if(nanoemModelRigidBodyGetTransformType(b)!=0){int i=boneIndex(nanoemModelRigidBodyGetBoneObject(b));if(i>=0&&size_t(i)<n)secondary[i]=1;}
 if(model.springs)for(auto& j:model.springs->joints)if(j.bone>=0&&size_t(j.bone)<n)secondary[j.bone]=1;
 if(model.source){nanoem_rsize_t count=0;auto constraints=nanoemModelGetAllConstraintObjects(model.source,&count);
  for(nanoem_rsize_t k=0;k<count;k++){int i=boneIndex(nanoemModelConstraintGetTargetBoneObject(constraints[k]));if(i>=0&&size_t(i)<n)ik[i]=1;}}
 std::vector<uint8_t> leaf(n,1);for(auto& b:model.bones)if(b.parent>=0&&size_t(b.parent)<n)leaf[b.parent]=0;
 for(auto& vertex:model.vertices)for(int k=0;k<4;k++){int b=vertex.bones[k];if(b<0||size_t(b)>=n||vertex.weights[k]<.1f)continue;bool repeat=false;for(int q=0;q<k;q++)repeat|=vertex.bones[q]==b&&vertex.weights[q]>=.1f;if(!repeat)v.bones[b].weighted++;}
 for(size_t i=0;i<n;i++){auto& b=model.bones[i];auto& out=v.bones[i];out.name=b.name;out.english=b.english;out.parent=b.parent;
  out.position={b.position.x()*.08f,b.position.y()*.08f,-b.position.z()*.08f};
  if(out.weighted)out.flags.push_back("deform");if(ik[i])out.flags.push_back("ik");if(secondary[i])out.flags.push_back("secondary");if(leaf[i])out.flags.push_back("leaf");
  if(b.source&&nanoemModelBoneIsVisible(b.source))out.flags.push_back("visible");}
 size_t total=model.vertices.size(),step=std::max<size_t>(1,(total+size_t(std::max(1,maxPoints))-1)/size_t(std::max(1,maxPoints)));
 float lo=0,hi=0;bool any=false;
 for(size_t i=0;i<total;i+=step){auto& vertex=model.vertices[i];int dominant=-1;float w=0;for(int k=0;k<4;k++)if(vertex.bones[k]>=0&&size_t(vertex.bones[k])<n&&vertex.weights[k]>w){w=vertex.weights[k];dominant=vertex.bones[k];}
  float y=vertex.position.y()*.08f;v.points.insert(v.points.end(),{vertex.position.x()*.08f,y,-vertex.position.z()*.08f,float(dominant)});
  if(!any){lo=hi=y;any=true;}lo=std::min(lo,y);hi=std::max(hi,y);}
 v.height=any?std::max(1e-3f,hi-lo):0;
 return v;
}

Json skeletonJson(const SkeletonView& v,const std::vector<BoneMeaning>& meanings){
 auto round4=[](float x){return std::round(x*1e4f)/1e4f;};
 Json bones=Json::array();
 for(size_t i=0;i<v.bones.size();i++){auto& b=v.bones[i];BoneMeaning m=i<meanings.size()?meanings[i]:BoneMeaning{};
  std::vector<std::string> flags=b.flags;auto flag=[&](bool on,const char* name){if(on&&std::find(flags.begin(),flags.end(),name)==flags.end())flags.push_back(name);};
  flag(b.weighted>0,"deform");flag(m.helper,"helper");flag(m.ik,"ik");flag(m.twist,"twist");flag(m.dbone,"dbone");flag(m.nonDeform,"nonDeform");flag(m.secondaryHint,"secondaryHint");
  Json bone={{"name",b.name},{"parent",b.parent},{"position",{round4(b.position[0]),round4(b.position[1]),round4(b.position[2])}},{"weighted",b.weighted},{"flags",flags},
   {"meaning",m.meaning},{"side",m.side?std::string(1,m.side):std::string()},{"segment",m.segment},{"mirror",m.mirror},{"nameIssue",b.nameIssue}};
  if(!b.english.empty()&&b.english!=b.name)bone["english"]=b.english;
  bones.push_back(std::move(bone));}
 Json points=Json::array();for(size_t i=0;i+3<v.points.size();i+=4){points.push_back(round4(v.points[i]));points.push_back(round4(v.points[i+1]));points.push_back(round4(v.points[i+2]));points.push_back(int(v.points[i+3]));}
 auto ext=extent(v);
 return {{"signature",skeletonSignature(v,meanings)},{"maxDepth",skeletonMaxDepth(v)},{"height",std::round(ext.height*1e3f)/1e3f},{"bones",bones},{"points",points}};
}

Json guessJson(const HumanoidGuess& g){
 auto one=[](const SlotGuess& s){return Json{{"bone",s.bone},{"confidence",std::round(double(s.confidence)*100)/100},{"method",s.method}};};
 Json slots=Json::object();for(auto& [key,s]:g.slots)slots[key]=one(s);
 return {{"humanoid",g.humanoid},{"candidates",g.candidates},{"slots",slots},{"eyes",{{"L",one(g.eyeL)},{"R",one(g.eyeR)}}}};
}

int jiggleJointCount(const SkeletonView& v,int root){
 if(root<0||size_t(root)>=v.bones.size())return 0;Tree t(v);int count=0;
 std::vector<int> stack{root};while(!stack.empty()){int b=stack.back();stack.pop_back();if(!(t.children[b].empty()&&v.bones[b].weighted==0))count++;for(int c:t.children[b])stack.push_back(c);}
 return count;
}

std::vector<MapProblem> checkBoneMap(const SkeletonView& v,const std::map<std::string,int>& values,const std::vector<int>& chainRoots){
 std::vector<MapProblem> out;Tree tree(v);int n=int(v.bones.size());bool flat=skeletonMaxDepth(v)<=3;
 auto value=[&](const char* key){auto it=values.find(key);return it==values.end()?-1:it->second;};
 auto name=[&](int b){return b>=0&&b<n?"\""+v.bones[b].name+"\"":std::string("none");};
 std::map<int,std::string> owner;int spine=value("ValveBiped.Bip01_Spine1");
 for(auto& s:humanoidSlots()){int b=value(s.key);std::string key=s.key;
  if(b<-1||b>=n){out.push_back({"range",key,"error","range",b,-1,"The bone assigned to "+key+" is not in the model."});continue;}
  if(b<0){if(s.required)out.push_back({"required",key,"error","required",-1,-1,"No bone is assigned to "+key+"."});continue;}
  if(auto it=owner.find(b);it!=owner.end()){out.push_back({"duplicate",key,"error","duplicate",b,-1,"Bone "+name(b)+" is assigned to both "+it->second+" and "+key+"."});continue;}
  owner[b]=key;
  int anchor=-1;for(auto a:s.anchors)if(a){int ab=value(a);if(ab>=0&&ab<n){anchor=ab;break;}}
  if(anchor>=0&&!tree.below(b,anchor)){std::string slot=s.id;bool soft=flat||slot=="middle_spine"||slot=="chest"||slot=="neck";
   out.push_back({"order",key,soft?"warning":"error","order",b,anchor,"Bone "+name(b)+" for "+key+" is not below "+name(anchor)+" in the skeleton."});}
  bool secondary=std::find(v.bones[b].flags.begin(),v.bones[b].flags.end(),"secondary")!=v.bones[b].flags.end();
  if(secondary)out.push_back({"physics",key,s.required?"error":"warning","physics",b,-1,"Bone "+name(b)+" for "+key+" is moved by the model's own physics."});
  if(std::string(s.family)=="thigh"&&spine>=0&&spine<n&&tree.below(b,spine))out.push_back({"leg_on_spine",key,"error","leg_on_spine",b,spine,"Bone "+name(b)+" for "+key+" hangs from the upper body; legs must hang from the hips."});
 }
 std::set<int> body;for(auto& [key,b]:values)if(b>=0&&b<n)body.insert(b);
 int joints=0;
 for(int root:chainRoots){if(root<0||root>=n){out.push_back({"jiggle_missing","","error","missing",root,-1,"A swinging part starts at a bone that is not in the model."});continue;}
  joints+=jiggleJointCount(v,root);
  for(int b:body)if(tree.underOrSelf(b,root)){std::string slot;for(auto& [key,bb]:values)if(bb==b){slot=key;break;}
   out.push_back({"jiggle_body",slot,"error","body",root,b,"The swinging part "+name(root)+" contains the body bone "+name(b)+" ("+slot+")."});break;}}
 if(int(chainRoots.size())>MaxJiggleChains||joints>MaxJiggleJoints)out.push_back({"jiggle_too_many","","error","too_many",-1,-1,"Too many swinging bones: "+std::to_string(joints)+" in "+std::to_string(chainRoots.size())+" parts (at most "+std::to_string(MaxJiggleJoints)+" in "+std::to_string(MaxJiggleChains)+")."});
 return out;
}

Json inspectBoneMap(const Model& model,const Json& options){
 auto view=skeletonFromModel(model,3000);auto meanings=classifyBones(view);auto guess=guessHumanoid(view,meanings);
 Json out={{"version",1},{"asset",model.id},{"auto",guessJson(guess)}};
 bool skeleton=false;if(options.contains("include")&&options["include"].is_array())for(auto& item:options["include"])skeleton|=item=="skeleton";
 if(skeleton)out["skeleton"]=skeletonJson(view,meanings);
 Json issues=Json::array();
 if(options.contains("values")){
  if(!options["values"].is_object())throw std::runtime_error("Invalid bone map options");
  std::map<std::string,int> values;
  for(auto& [key,item]:options["values"].items()){
   // Lua's JSON may write a whole number as 3.0: any integral number is accepted.
   double v=item.is_number()?item.get<double>():.5;
   if(!slotByKey(key)||!std::isfinite(v)||std::floor(v)!=v||v<-1||v>=double(view.bones.size())){issues.push_back({{"code","range"},{"severity","error"},{"slot",key},{"bone",-1},{"text","The assignment of "+key+" does not fit this model."}});continue;}
   values[key]=int(v);}
  for(auto& p:checkBoneMap(view,values,{}))issues.push_back({{"code",p.code},{"severity",p.severity},{"slot",p.slot},{"bone",p.bone},{"text",p.text}});
 }
 out["issues"]=issues;
 return out;
}
}
