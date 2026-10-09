#include "physics_profile.hpp"
#include "scmi_data.hpp"
#include <BulletCollision/NarrowPhaseCollision/btGjkPairDetector.h>
#include <BulletCollision/NarrowPhaseCollision/btGjkEpaPenetrationDepthSolver.h>
#include <BulletCollision/NarrowPhaseCollision/btPointCollector.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
namespace mmd {
namespace {
Json xyz(const btVector3& v){return {v.x(),v.y(),v.z()};}
// Presentation values for Lua: four decimals keep the preview small and match its 1e-4 shape quantum.
double rounded(double v){double r=std::round(v*1e4)/1e4;return r==0?0.:r;}
Json roundedXyz(const btVector3& v){return {rounded(v.x()),rounded(v.y()),rounded(v.z())};}
btVector3 vec3(const Json& a){return {a.at(0).get<float>(),a.at(1).get<float>(),a.at(2).get<float>()};}
int carrierBody(std::string_view name){for(int i=0;i<18;i++)if(name==CarrierBodyNames[i])return i;return -1;}
bool adjacent(int a,int b){return CarrierBodyParents[a]==b||CarrierBodyParents[b]==a;}
// Half away from zero in double; -0 becomes 0 so the dump never prints "-0.0".
double quantize(double v,int decimals){double p=std::pow(10.,decimals),r=std::round(v*p)/p;return r==0?0.:r;}
std::string range(double lo,double hi){std::ostringstream s;s.imbue(std::locale::classic());s<<"expected "<<lo<<" to "<<hi;return s.str();}
// Restored manifests are untrusted: profile reads tolerate any JSON shape.
std::string text(const Json& j,const char* key,const char* fallback){if(!j.is_object())return fallback;auto it=j.find(key);return it!=j.end()&&it->is_string()?it->get<std::string>():fallback;}
std::string collisionMode(const Json& c){auto it=c.is_object()?c.find("collisions"):c.end();return it!=c.end()?text(*it,"mode","all"):"all";}
const Json& facesOf(const Rig& r,size_t i){
 static const Json none=Json::array();auto bodies=r.manifest.is_object()?r.manifest.find("bodies"):r.manifest.end();
 if(bodies==r.manifest.end()||!bodies->is_array()||i>=bodies->size()||!(*bodies)[i].is_object())return none;
 auto faces=(*bodies)[i].find("faces");return faces!=(*bodies)[i].end()?*faces:none;
}
struct Canonicalizer {
 std::vector<PhysicsError>& errors;
 void fail(const std::string& code,const std::string& path,const std::string& detail){errors.push_back({code,path,detail});}
 // An object, or the [] a Lua empty table serialises to.
 const Json* object(const Json& v,const std::string& path){static const Json empty=Json::object();if(v.is_object())return &v;if(v.is_array()&&v.empty())return &empty;fail("not_object",path,"expected an object");return nullptr;}
 void known(const Json& o,std::initializer_list<const char*> keys,const std::string& path){
  for(auto& [key,value]:o.items())if(std::none_of(keys.begin(),keys.end(),[&](const char* k){return key==k;}))fail("unknown_field",path.empty()?key:path+"."+key,"not part of physics schema 1");
 }
 bool number(const Json& v,const std::string& path,int decimals,double& out){
  if(!v.is_number()){fail("not_finite",path,"expected a number");return false;}
  double d=v.get<double>();if(!std::isfinite(d)){fail("not_finite",path,"not a finite number");return false;}
  out=quantize(d,decimals);return true;
 }
 // Integer-valued numbers: Lua may print 3 as 3.0.
 bool integer(const Json& v,const std::string& path,double& out){
  if(!number(v,path,6,out))return false;if(std::floor(out)!=out){fail("not_integer",path,"expected a whole number");return false;}return true;
 }
};
}
const std::array<BodyDefaults,18>& carrierBodyDefaults(){
 static const auto table=[]{
  std::array<BodyDefaults,18> out{};auto data=Json::parse(ScmiData);
  for(int i=0;i<18;i++){auto& d=out[i];auto it=data["limits"].find(CarrierBodyNames[i]);if(it==data["limits"].end())continue;
   for(auto& line:*it){std::istringstream in(line.get<std::string>());std::string command,ignored;in>>command>>ignored;if(command=="$jointmassbias")in>>d.massBias;else if(command=="$jointrotdamping")in>>d.rotationDamping;else if(command=="$jointconstrain"){char axis;in>>axis>>ignored;int k=axis-'x';float friction;in>>d.lower[k]>>d.upper[k]>>friction;}}
  }
  return out;
 }();
 return table;
}
bool validSurfaceprop(std::string_view s){return !s.empty()&&s.size()<=32&&std::all_of(s.begin(),s.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_';});}
std::string shapeStyle(const Json& o){
 if(!o.is_object()||!o.contains("style"))return "fitted";
 auto& s=o["style"];if(!s.is_string()||(s!="fitted"&&s!="box"&&s!="capsule"))throw std::runtime_error("Invalid collision override: style");
 return s.get<std::string>();
}
CanonicalPhysics canonicalPhysics(const Json& raw){
 CanonicalPhysics out;Canonicalizer c{out.errors};
 if(raw.is_null()||((raw.is_array()||raw.is_object())&&raw.empty()))return out;
 if(!raw.is_object()){c.fail("not_object","","expected an object");return out;}
 // Strict: a newer addon's profile is refused whole instead of half applied.
 auto schema=raw.find("schema");
 if(schema==raw.end()||!schema->is_number()||schema->get<double>()!=PhysicsSchema){c.fail("schema_unsupported","schema","this module reads physics schema 1");return out;}
 c.known(raw,{"schema","surfaceprop","massMode","automass","collisions","animatedFriction","bodies"},"");
 Json result={{"schema",PhysicsSchema}};std::string model="flesh";
 if(auto it=raw.find("surfaceprop");it!=raw.end()){
  if(!it->is_string()||!validSurfaceprop(it->get<std::string>()))c.fail("surfaceprop","surfaceprop","expected 1 to 32 of a-z, 0-9 and _");
  else if((model=it->get<std::string>())!="flesh")result["surfaceprop"]=model;
 }
 if(auto it=raw.find("massMode");it!=raw.end()){if(*it=="volume")result["massMode"]="volume";else if(*it!="bias")c.fail("mass_mode","massMode","expected bias or volume");}
 if(auto it=raw.find("automass");it!=raw.end())if(auto o=c.object(*it,"automass")){
  c.known(*o,{"density"},"automass");double density;auto d=o->find("density");
  if(d==o->end())c.fail("density_range","automass.density","missing");
  else if(c.number(*d,"automass.density",1,density)){if(density<10||density>20000)c.fail("density_range","automass.density",range(10,20000));else result["automass"]={{"density",density}};}
 }
 if(auto it=raw.find("collisions");it!=raw.end())if(auto o=c.object(*it,"collisions")){
  c.known(*o,{"mode","pairs"},"collisions");auto m=o->find("mode");const Json mode=m==o->end()?Json():*m;
  if(mode=="custom"){
   std::set<std::pair<int,int>> set;auto p=o->find("pairs");
   if(p!=o->end()&&!p->is_array())c.fail("collision_mode","collisions.pairs","expected a list of pairs");
   else if(p!=o->end())for(size_t n=0;n<p->size();n++){auto path="collisions.pairs."+std::to_string(n);auto& pair=(*p)[n];double a,b;
    if(!pair.is_array()||pair.size()!=2){c.fail("pair_index",path,"expected [a, b]");continue;}
    if(!c.integer(pair[0],path,a)||!c.integer(pair[1],path,b))continue;
    if(a<0||a>17||b<0||b>17||a==b){c.fail("pair_index",path,"expected two different bodies 0 to 17");continue;}
    if(adjacent(int(a),int(b))){c.fail("pair_adjacent",path,"joined bodies never collide");continue;}
    set.insert({int(std::min(a,b)),int(std::max(a,b))});
   }
   // Every non-adjacent pair is the default rule set.
   if(set.empty())result["collisions"]={{"mode","none"}};
   else if(set.size()<136){Json pairs=Json::array();for(auto [a,b]:set)pairs.push_back({a,b});result["collisions"]={{"mode","custom"},{"pairs",pairs}};}
  }
  else if(mode=="none"||mode=="all"){if(o->contains("pairs")&&!o->at("pairs").empty())c.fail("collision_mode","collisions.pairs","pairs need mode custom");else if(mode=="none")result["collisions"]={{"mode","none"}};}
  else c.fail("collision_mode","collisions.mode","expected all, none or custom");
 }
 if(auto it=raw.find("animatedFriction");it!=raw.end())if(auto o=c.object(*it,"animatedFriction")){
  c.known(*o,{"min","max","timeIn","timeOut","timeHold"},"animatedFriction");Json a=Json::object();bool ok=true;
  for(auto key:{"min","max","timeIn","timeOut","timeHold"}){auto path=std::string("animatedFriction.")+key;auto v=o->find(key);double x;bool whole=key[0]=='m';
   if(v==o->end()){c.fail("animated_friction_range",path,"missing");ok=false;continue;}
   if(!(whole?c.integer(*v,path,x):c.number(*v,path,2,x))){ok=false;continue;}
   if(x<0||x>(whole?1000:10)){c.fail("animated_friction_range",path,range(0,whole?1000:10));ok=false;continue;}
   if(whole)a[key]=int(x);else a[key]=x;
  }
  if(ok&&a["min"].get<int>()>a["max"].get<int>()){c.fail("animated_friction_range","animatedFriction.min","the minimum is above the maximum");ok=false;}
  if(ok)result["animatedFriction"]=a;
 }
 if(auto it=raw.find("bodies");it!=raw.end())if(auto o=c.object(*it,"bodies")){
  Json bodies=Json::object();
  for(auto& [name,value]:o->items()){
   auto path="bodies."+name;int i=carrierBody(name);if(i<0){c.fail("unknown_body",path,"not one of the 18 carrier bodies");continue;}
   auto b=c.object(value,path);if(!b)continue;
   c.known(*b,{"limits","massBias","damping","rotdamping","inertia","drag","surfaceprop"},path);
   const auto& d=carrierBodyDefaults()[i];Json out=Json::object();
   if(auto l=b->find("limits");l!=b->end()){
    if(i==0)c.fail("root_joint",path+".limits","the pelvis is the root and has no joint");
    else if(auto axes=c.object(*l,path+".limits")){
     c.known(*axes,{"x","y","z"},path+".limits");Json limits=Json::object();
     for(int k=0;k<3;k++){
      std::string axis(1,char('x'+k));auto v=axes->find(axis);if(v==axes->end())continue;auto ap=path+".limits."+axis;double lo,hi,f;
      if(!v->is_array()||v->size()!=3){c.fail("limit_range",ap,"expected [min, max, friction]");continue;}
      if(!c.number((*v)[0],ap,1,lo)||!c.number((*v)[1],ap,1,hi)||!c.number((*v)[2],ap,3,f))continue;
      if(f<0||f>100){c.fail("friction_range",ap,range(0,100));continue;}
      // studiomdl's free axis is -360..360 and keeps its friction; a fixed one is 0..0 without friction.
      bool free=lo==-360&&hi==360;
      if(!free){if(lo<-180||hi>180){c.fail("limit_range",ap,range(-180,180));continue;}if(lo>hi){c.fail("limit_order",ap,"the minimum is above the maximum");continue;}if(lo==0&&hi==0)f=0;}
      if(!free&&lo==d.lower[k]&&hi==d.upper[k]&&f==0)continue;
      limits[axis]={lo,hi,f};
     }
     if(!limits.empty())out["limits"]=limits;
    }
   }
   auto field=[&](const char* key,double lo,double hi,double fallback,const char* code){
    auto v=b->find(key);double x;if(v==b->end()||!c.number(*v,path+"."+key,3,x))return;
    if(x<lo||x>hi)c.fail(code,path+"."+key,range(lo,hi));else if(x!=fallback)out[key]=x;
   };
   field("massBias",.01,100,d.massBias,"mass_bias_range");field("damping",0,10,.8,"damping_range");field("rotdamping",0,100,d.rotationDamping,"rotdamping_range");field("inertia",.1,100,12,"inertia_range");
   // Drag is written only when set, so any value is a change.
   field("drag",0,100,-1,"drag_range");
   if(auto s=b->find("surfaceprop");s!=b->end()){
    if(!s->is_string()||!validSurfaceprop(s->get<std::string>()))c.fail("surfaceprop",path+".surfaceprop","expected 1 to 32 of a-z, 0-9 and _");
    else if(*s!=model)out["surfaceprop"]=*s;
   }
   if(!out.empty())bodies[name]=out;
  }
  if(!bodies.empty())result["bodies"]=bodies;
 }
 if(out.errors.empty()&&result.size()>1)out.value=result;
 return out;
}
Json requireCanonicalPhysics(const Json& raw){
 auto c=canonicalPhysics(raw);
 if(!c.errors.empty()){auto& e=c.errors.front();throw std::runtime_error("Invalid physics settings: "+e.code+" "+e.path+" "+e.detail);}
 return c.value;
}
Json normalizeCarrierOptions(Json options){
 if(!options.is_object()||!options.contains("physicsOverrides"))return options;
 // c_arms have no physics; their key must not depend on the character's profile.
 if(options.value("role",std::string("ragdoll"))=="arms"){options.erase("physicsOverrides");return options;}
 auto c=requireCanonicalPhysics(options["physicsOverrides"]);
 if(c.empty())options.erase("physicsOverrides");else options["physicsOverrides"]=std::move(c);
 return options;
}
std::string carrierFitKey(const std::string& id,const Json& options){
 Json geometry;
 for(auto key:{"scaleMultiplier","scale","height","mass","collisionOverrides","collisionOverrideScale","excludedMaterials","role","gender","animationSource","animationReference","armsParts","physicsOverrides","boneMap"})if(options.contains(key))geometry[key]=options[key];
 return id+geometry.dump();
}
void applyPhysics(Rig& r,const Json& c){
 if(!c.is_object()||c.empty())return;
 if(r.bodies.size()!=18||!r.manifest.contains("bodies")||r.manifest["bodies"].size()!=18)throw std::runtime_error("A carrier must have 18 physics bodies");
 r.physics=c;const auto model=text(c,"surfaceprop","flesh");const auto bodies=c.value("bodies",Json::object());
 for(auto& b:r.bodies){
  b.surfaceprop=model;auto it=bodies.find(r.bones[b.bone].name);if(it==bodies.end())continue;auto& o=*it;
  if(auto l=o.find("limits");l!=o.end())for(int k=0;k<3;k++){auto a=l->find(std::string(1,char('x'+k)));if(a==l->end())continue;b.lower[k]=(*a)[0].get<float>();b.upper[k]=(*a)[1].get<float>();b.friction[k]=(*a)[2].get<float>();}
  b.massBias=o.value("massBias",b.massBias);b.rotationDamping=o.value("rotdamping",b.rotationDamping);b.damping=o.value("damping",b.damping);b.inertia=o.value("inertia",b.inertia);b.drag=o.value("drag",b.drag);b.surfaceprop=o.value("surfaceprop",b.surfaceprop);
 }
 auto& manifest=r.manifest["bodies"];std::vector<double> volumes(18);for(size_t i=0;i<18;i++)volumes[i]=hullVolume(r.bodies[i].hull,manifest[i].value("faces",Json::array()));
 // $automass: Σ hull volume (in³ to m³) times the density.
 if(auto a=c.find("automass");a!=c.end()){double total=0;for(auto v:volumes)total+=v;r.mass=float(std::clamp(std::round(total*1.6387064e-5*a->at("density").get<double>()*1e3)/1e3,1.,1000.));}
 auto masses=solidMasses(r);float biasTotal=0;
 for(size_t i=0;i<18;i++){
  auto& b=r.bodies[i];auto& j=manifest[i];biasTotal+=b.massBias;
  j["lower"]=xyz(b.lower);j["upper"]=xyz(b.upper);j["massBias"]=b.massBias;j["rotationDamping"]=b.rotationDamping;
  j["friction"]=xyz(b.friction);j["damping"]=b.damping;j["inertia"]=b.inertia;j["surfaceprop"]=b.surfaceprop;j["mass"]=masses[i];j["volume"]=volumes[i];
  if(b.drag>=0)j["drag"]=b.drag;else j.erase("drag");
 }
 r.manifest["physicsOverrides"]=c;r.manifest["physicsWriter"]=PhysicsWriterVersion;r.manifest["mass"]=r.mass;r.manifest["massBiasTotal"]=biasTotal;
}
double hullVolume(const std::vector<btVector3>& hull,const Json& faces){
 if(hull.empty()||!faces.is_array())return 0;
 btVector3 center(0,0,0);for(auto& v:hull)center+=v;center/=float(hull.size());double total=0;
 for(auto& face:faces){
  if(!face.is_array()||face.size()<3)continue;std::vector<size_t> ids;
  for(auto& id:face){if(!id.is_number_integer()||id.get<int64_t>()<0||id.get<uint64_t>()>=hull.size()){ids.clear();break;}ids.push_back(id.get<size_t>());}
  // Tetrahedra from the centroid: winding-independent for a convex hull.
  for(size_t k=1;k+1<ids.size();k++){auto a=hull[ids[0]]-center,b=hull[ids[k]]-center,d=hull[ids[k+1]]-center;total+=std::abs(double(a.dot(b.cross(d))))/6;}
 }
 return total;
}
std::vector<float> solidMasses(const Rig& r){
 std::vector<float> out(r.bodies.size());float bias=0;for(auto& b:r.bodies)bias+=b.massBias;
 const bool edited=r.physics.is_object()&&!r.physics.empty();
 // studiomdl's $jointmassbias weighting: volume times bias, with its 1 kg floor.
 if(edited&&text(r.physics,"massMode","bias")=="volume"){
  std::vector<double> volumes(r.bodies.size());double weighted=0;
  for(size_t i=0;i<r.bodies.size();i++){volumes[i]=hullVolume(r.bodies[i].hull,facesOf(r,i));weighted+=volumes[i]*r.bodies[i].massBias;}
  if(weighted>0){for(size_t i=0;i<r.bodies.size();i++)out[i]=std::max(1.f,float(double(r.mass)*volumes[i]*r.bodies[i].massBias/weighted));return out;}
 }
 // The 2.2 expression; an edited profile keeps VPhysics' 0.1 kg minimum.
 for(size_t i=0;i<r.bodies.size();i++){float m=r.mass*r.bodies[i].massBias/bias;out[i]=edited?std::max(.1f,m):m;}
 return out;
}
std::string physicsText(const Rig& rig){
 std::ostringstream kv;kv.imbue(std::locale::classic());auto masses=solidMasses(rig);
 for(size_t i=0;i<rig.bodies.size();i++){auto& b=rig.bodies[i];kv<<"solid {\n\"index\" \""<<i<<"\"\n\"name\" \""<<rig.bones[b.bone].name<<"\"\n";if(b.parent>=0)kv<<"\"parent\" \""<<rig.bones[rig.bodies[b.parent].bone].name<<"\"\n";
  kv<<"\"mass\" \""<<masses[i]<<"\"\n\"surfaceprop\" \""<<b.surfaceprop<<"\"\n\"damping\" \""<<b.damping<<"\"\n\"rotdamping\" \""<<b.rotationDamping<<"\"\n";
  // studiomdl writes drag (only when set) between rotdamping and inertia.
  if(b.drag>=0)kv<<"\"drag\" \""<<b.drag<<"\"\n";
  kv<<"\"inertia\" \""<<b.inertia<<"\"\n}\n";
 }
 for(size_t i=1;i<rig.bodies.size();i++){auto& b=rig.bodies[i];kv<<"ragdollconstraint {\n\"parent\" \""<<b.parent<<"\"\n\"child\" \""<<i<<"\"\n";for(int k=0;k<3;k++){char axis='x'+char(k);kv<<'"'<<axis<<"min\" \""<<b.lower[k]<<"\"\n\""<<axis<<"max\" \""<<b.upper[k]<<"\"\n\""<<axis<<"friction\" \""<<b.friction[k]<<"\"\n";}kv<<"}\n";}
 // Source treats the presence of selfcollisions as OFF, regardless of value.
 // Enabled pairs must appear without that key (CRagdollCollisionRules).
 auto mode=collisionMode(rig.physics);kv<<"collisionrules {\n";
 if(mode=="none")kv<<"\"selfcollisions\" \"0\"\n";
 else if(mode=="custom")for(auto [a,b]:enabledPairs(rig.physics))kv<<"\"collisionpair\" \""<<a<<","<<b<<"\"\n";
 else for(size_t a=0;a<18;a++)for(size_t b=a+1;b<18;b++)if(rig.bodies[b].parent!=int(a)&&rig.bodies[a].parent!=int(b))kv<<"\"collisionpair\" \""<<a<<","<<b<<"\"\n";
 kv<<"}\n";
 // Read only by client-side death ragdolls (C_ClientRagdoll::HandleAnimatedFriction).
 if(auto a=rig.physics.is_object()?rig.physics.find("animatedFriction"):rig.physics.end();a!=rig.physics.end()&&a->is_object()){
  auto value=[&](const char* key){auto v=a->find(key);return v!=a->end()&&v->is_number()?v->get<double>():0.;};
  kv<<"animatedfriction {\n\"animfrictionmin\" \""<<int(value("min"))<<"\"\n\"animfrictionmax\" \""<<int(value("max"))<<"\"\n\"animfrictiontimein\" \""<<value("timeIn")<<"\"\n\"animfrictiontimeout\" \""<<value("timeOut")<<"\"\n\"animfrictiontimehold\" \""<<value("timeHold")<<"\"\n}\n";
 }
 kv<<"editparams {\n\"rootname\" \"ValveBiped.Bip01_Pelvis\"\n\"totalmass\" \""<<rig.mass<<"\"\n}\n";
 return kv.str();
}
std::vector<btVector3> primitiveHull(const std::string& style,const btVector3& c,const btVector3& e){
 std::vector<btVector3> out;
 if(style=="box"){for(int x:{-1,1})for(int y:{-1,1})for(int z:{-1,1})out.push_back(c+e*btVector3(float(x),float(y),float(z)));return out;}
 if(style!="capsule")throw std::runtime_error("Invalid collision override: style");
 // Rounded ends on the longest axis. The equator rings hold 16 points so the
 // hull reaches exactly c±e and keeps ~95% of a limb capsule's volume within
 // the 64-point budget; latitude rings thin out toward the poles.
 int L=0;if(e[1]>e[L])L=1;if(e[2]>e[L])L=2;const int a=(L+1)%3,b=(L+2)%3;
 const float radius=std::min(e[a],e[b]),cap=std::min(e[L],radius),half=e[L]-cap;
 const struct {float latitude;int count;} rings[]{{0,16},{30,10},{60,4}};
 for(float side:{-1.f,1.f}){
  for(int ring=0;ring<3;ring++){
   float phi=rings[ring].latitude*SIMD_RADS_PER_DEG;int n=rings[ring].count;
   for(int j=0;j<n;j++){float theta=SIMD_2_PI*float(j)/float(n)+(ring%2?SIMD_PI/float(n):0.f);btVector3 p(0,0,0);p[a]=e[a]*std::cos(phi)*std::cos(theta);p[b]=e[b]*std::cos(phi)*std::sin(theta);p[L]=side*(half+cap*std::sin(phi));out.push_back(c+p);}
  }
  btVector3 pole(0,0,0);pole[L]=side*e[L];out.push_back(c+pole);
 }
 return out;
}
std::vector<std::pair<int,int>> enabledPairs(const Json& c){
 std::vector<std::pair<int,int>> out;auto mode=collisionMode(c);
 if(mode=="none")return out;
 if(mode=="custom"){
  auto& collisions=c.at("collisions");auto p=collisions.find("pairs");
  if(p!=collisions.end()&&p->is_array())for(auto& pair:*p){
   if(!pair.is_array()||pair.size()!=2||!pair[0].is_number_integer()||!pair[1].is_number_integer())continue;
   int a=pair[0].get<int>(),b=pair[1].get<int>();if(a>b)std::swap(a,b);
   if(a>=0&&b<18&&a!=b&&!adjacent(a,b))out.push_back({a,b});
  }
  std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end()),out.end());return out;
 }
 for(int a=0;a<18;a++)for(int b=a+1;b<18;b++)if(!adjacent(a,b))out.push_back({a,b});
 return out;
}
std::vector<Penetration> restPenetrations(const Rig& r,const std::vector<std::pair<int,int>>& pairs){
 std::vector<std::unique_ptr<btConvexHullShape>> shapes;
 for(auto& body:r.bodies){auto shape=std::make_unique<btConvexHullShape>();shape->setMargin(0);for(auto v:body.hull)shape->addPoint(v,false);shape->recalcLocalAabb();shapes.push_back(std::move(shape));}
 const float reported=.01f*r.scale/ScmiSourceUnitsPerPmx;std::vector<Penetration> out;
 for(auto [a,b]:pairs){
  if(a<0||b<0||a>=int(shapes.size())||b>=int(shapes.size())||r.bodies[a].hull.empty()||r.bodies[b].hull.empty())continue;
  btVoronoiSimplexSolver simplex;btGjkEpaPenetrationDepthSolver epa;btGjkPairDetector detector(shapes[a].get(),shapes[b].get(),&simplex,&epa);btDiscreteCollisionDetectorInterface::ClosestPointInput query;query.m_transformA=r.bones[r.bodies[a].bone].rest;query.m_transformB=r.bones[r.bodies[b].bone].rest;btPointCollector result;detector.getClosestPoints(query,result,nullptr);
  if(result.m_hasResult&&-result.m_distance>reported)out.push_back({a,b,-result.m_distance});
 }
 return out;
}
Json previewCarrier(const Rig& r){
 auto masses=solidMasses(r);const auto& manifest=r.manifest.at("bodies");
 // fitRig's shape unit: a sixtieth of the head-to-feet stature.
 auto rest=[&](const char* name){for(auto& b:r.bones)if(b.name==name)return b.rest.getOrigin();return btVector3(0,0,0);};
 float stature=(rest("ValveBiped.Bip01_Head1")-(rest("ValveBiped.Bip01_L_Foot")+rest("ValveBiped.Bip01_R_Foot"))*.5f).length();
 Json bodies=Json::array();
 for(size_t i=0;i<r.bodies.size();i++){
  auto& b=r.bodies[i];auto& j=manifest.at(i);Json hull=Json::array();for(auto v:b.hull)hull.push_back(roundedXyz(v));auto faces=j.value("faces",Json::array());
  bodies.push_back({{"index",i},{"name",r.bones[b.bone].name},{"parent",b.parent},{"bone",b.bone},{"center",roundedXyz(vec3(j.at("center")))},{"extent",roundedXyz(vec3(j.at("extent")))},
   {"hull",hull},{"faces",faces},{"style",b.style},{"volume",rounded(hullVolume(b.hull,faces))},{"mass",rounded(masses[i])},{"massBias",b.massBias},{"damping",b.damping},{"rotdamping",b.rotationDamping},{"inertia",b.inertia},
   {"drag",b.drag>=0?Json(b.drag):Json()},{"surfaceprop",b.surfaceprop},{"lower",xyz(b.lower)},{"upper",xyz(b.upper)},{"friction",xyz(b.friction)},{"confidence",rounded(b.confidence)},{"needsReview",j.value("needsReview",false)}});
 }
 auto pairs=enabledPairs(r.physics);Json overlaps=Json::array();
 for(auto& p:restPenetrations(r,pairs))overlaps.push_back({{"a",p.a},{"b",p.b},{"depth",rounded(p.depth)}});
 Json counted={{"mode",collisionMode(r.physics)},{"count",pairs.size()}};
 return {{"status","ready"},{"key",r.key},{"scale",r.scale},{"m",r.scale/ScmiSourceUnitsPerPmx},{"unit",stature/60},{"mass",r.mass},{"canonical",r.physics.is_object()?r.physics:Json::object()},{"bodies",bodies},{"pairs",counted},{"penetrations",overlaps},{"phyText",physicsText(r)}};
}
void PreviewQueue::harvest(){
 for(auto it=running.begin();it!=running.end();){
  if(it->second.wait_for(std::chrono::seconds(0))!=std::future_status::ready){++it;continue;}
  auto key=it->first;auto done=std::move(it->second);it=running.erase(it);
  std::erase_if(finished,[&](auto& f){return f.first==key;});finished.emplace_back(key,done.get());if(finished.size()>4)finished.erase(finished.begin());
 }
}
Json PreviewQueue::poll(const std::string& id,const std::string& key,std::function<Json()> run){
 harvest();for(auto& [k,result]:finished)if(k==key)return result;
 bool wait=running.size()>=2;for(auto& [k,future]:running)wait|=k.starts_with(id);
 if(!wait)running.emplace(key,std::async(std::launch::async,std::move(run)));
 return {{"status","pending"}};
}
bool PreviewQueue::busy(const std::string& id){harvest();return std::any_of(running.begin(),running.end(),[&](auto& r){return r.first.starts_with(id);});}
void PreviewQueue::forget(const std::string& id){std::erase_if(finished,[&](auto& f){return f.first.starts_with(id);});}
}
