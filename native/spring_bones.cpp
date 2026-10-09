#include "spring_bones.hpp"
#include "import_error.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <set>
#include <stdexcept>
namespace mmd {
namespace {
std::atomic<bool> relativeDampingEnabled{true};
// Failures (code spring.data) name the value; the spring, joint or collider scope around
// them says which one (spring 2 “Hair” › spring joint 5).
std::string text(float v){if(!std::isfinite(v))return std::isnan(v)?"NaN":"infinity";char b[32];snprintf(b,sizeof b,"%g",v);return b;}
btVector3 xyz(const Json& j,const char* what){if(!j.is_array()||j.size()<3)importFail("spring.data",std::string("Invalid spring bone vector: the ")+what+" is not a list of three numbers",{{"field",what}});btVector3 v(j[0].get<float>(),j[1].get<float>(),j[2].get<float>());
 for(int k=0;k<3;k++)if(!std::isfinite(v[k])||std::fabs(v[k])>1e7f)importFail("spring.data",std::string("Invalid spring bone vector: the ")+what+" has the value "+text(v[k]),{{"field",what},{"value",text(v[k])}});return v;}
// Spring values scale positions every step: finite and within a sane range.
float bounded(float v,float limit,const char* what){if(!std::isfinite(v)||std::fabs(v)>limit)importFail("spring.data",std::string("Invalid spring bone ")+what+": "+text(v)+" is outside -"+text(limit)+" to "+text(limit),{{"field",what},{"value",text(v)}});return v;}
}
bool slideTail(const btVector3& outside,const btVector3& head,btVector3& tail,const btVector3& point,btVector3 normal,float radius,float length){
 if(normal.length2()<1e-12f)return false;normal.normalize();if(normal.dot(outside-point)<0)normal=-normal;
 float depth=radius-normal.dot(tail-point);if(depth<=0)return false;
 // Signed height of the head over the offset surface: a scalp inside a ragdoll's
 // (slightly smaller) collision hull can sit below the floor it lies on.
 float height=normal.dot(head-point)-radius;
 auto offset=tail-head;auto tangent=offset-normal*normal.dot(offset);
 if(tangent.length2()<1e-10f)tangent=normal.cross(btFabs(normal.x())<.9f?btVector3(1,0,0):btVector3(0,1,0));tangent.normalize();
 if(height>=length){tail+=normal*depth;return true;}
 if(height<=-length){tail=head+normal*length;return true;} // out of reach: point the strand at it
 tail=head-normal*height+tangent*std::sqrt(length*length-height*height); // on the surface, at the joint length
 return true;
}
void setSpringRelativeDamping(bool enabled){relativeDampingEnabled=enabled;}
bool springRelativeDamping(){return relativeDampingEnabled.load();}

std::shared_ptr<const SpringSetup> SpringSetup::fromManifest(const Json& vrm,const Model& model){
 if(!vrm.is_object()||!vrm.contains("springBone"))return nullptr;
 auto& j=vrm["springBone"];auto out=std::make_shared<SpringSetup>();size_t bones=model.bones.size();
 auto bone=[&](const Json& v,bool optional){int b=v.is_number_integer()?v.get<int>():-1;if(b<-1||b>=int(bones)||(!optional&&b<0))importFail("spring.data","Spring bone data refers to bone "+std::to_string(b)+", a bone outside the model (it has "+thousands(bones)+")",{{"field","bone"},{"value",b},{"count",bones}});return b;};
 out->unitsPerMeter=bounded(j.value("unitsPerMeter",12.5f),1e5f,"units");if(!(out->unitsPerMeter>0))importFail("spring.data","Invalid spring bone units: "+text(out->unitsPerMeter)+" units per metre",{{"field","units"},{"value",text(out->unitsPerMeter)}});
 auto colliderList=j.value("colliders",Json::array());
 for(size_t ci=0;ci<colliderList.size();ci++){auto& c=colliderList[ci];ImportScope scope("collider",int64_t(ci),{});Collider x;x.bone=bone(c.value("bone",Json()),true);x.capsule=c.value("shape",std::string())=="capsule";x.offset=xyz(c.at("offset"),"offset");if(x.capsule)x.tail=xyz(c.at("tail"),"tail");x.radius=std::max(0.f,bounded(c.value("radius",0.f),1e5f,"collider radius"));out->colliders.push_back(x);}
 std::vector<std::vector<int>> groups;auto groupList=j.value("colliderGroups",Json::array());
 for(size_t gi=0;gi<groupList.size();gi++){ImportScope scope("collider_group",int64_t(gi),{});std::vector<int> list;for(auto& k:groupList[gi]){int i=k.get<int>();if(i<0||size_t(i)>=out->colliders.size())importFail("spring.data","Invalid spring collider reference: collider "+std::to_string(i)+" does not exist (there are "+std::to_string(out->colliders.size())+")",{{"field","colliders"},{"value",i}});list.push_back(i);}groups.push_back(list);}
 auto springList=j.value("springs",Json::array());
 for(size_t si=0;si<springList.size();si++){auto& s=springList[si];Spring x;x.name=s.value("name",std::string());ImportScope scope("spring",int64_t(si),x.name);x.center=bone(s.value("center",Json(-1)),true);
  std::set<int> unique;for(auto& g:s.value("colliderGroups",Json::array())){int i=g.get<int>();if(i<0||size_t(i)>=groups.size())importFail("spring.data","Invalid spring collider group reference: group "+std::to_string(i)+" does not exist (there are "+std::to_string(groups.size())+")",{{"field","colliderGroups"},{"value",i}});for(int c:groups[i])if(unique.insert(c).second)x.colliders.push_back(c);}
  out->springs.push_back(std::move(x));}
 out->jointOfBone.assign(bones,-1);auto jointList=j.value("joints",Json::array());
 for(size_t ji=0;ji<jointList.size();ji++){auto& k=jointList[ji];ImportScope scope("spring_joint",int64_t(ji),{});Joint x;x.spring=k.at("spring").get<int>();if(x.spring<0||size_t(x.spring)>=out->springs.size())importFail("spring.data","Invalid spring reference: spring "+std::to_string(x.spring)+" does not exist (there are "+std::to_string(out->springs.size())+")",{{"field","spring"},{"value",x.spring}});
  x.bone=bone(k.at("bone"),false);x.tail=bone(k.value("tail",Json(-1)),true);auto offset=xyz(k.at("tailOffset"),"tail offset");x.length=offset.length();if(x.length<1e-6f)continue;x.axis=offset/x.length;
  x.hitRadius=std::max(0.f,bounded(k.value("hitRadius",0.f),1e5f,"hit radius"));x.stiffness=bounded(k.value("stiffness",1.f),1e5f,"stiffness");x.gravityPower=bounded(k.value("gravityPower",0.f),1e5f,"gravity");x.dragForce=std::clamp(bounded(k.value("dragForce",.4f),1e5f,"drag"),0.f,1.f);x.gravityDir=xyz(k.value("gravityDir",Json::array({0,-1,0})),"gravity direction");
  if(!std::isfinite(x.stiffness)||!std::isfinite(x.gravityPower)||!std::isfinite(x.hitRadius))importFail("spring.data","Invalid spring bone parameters",{{"field","stiffness"}});
  if(out->jointOfBone[x.bone]>=0)continue;out->jointOfBone[x.bone]=int(out->joints.size());out->joints.push_back(x);}
 if(out->joints.empty())return nullptr;
 out->isAffected.assign(bones,0);
 for(auto i:model.order){int p=model.bones[i].parent;if(out->jointOfBone[i]>=0||(p>=0&&out->isAffected[p])){out->isAffected[i]=1;out->affected.push_back(int(i));}}
 return out;
}

SpringSystem::SpringSystem(const Model& m,std::shared_ptr<const SpringSetup> setup,const std::vector<uint8_t>& controlled,int referenceBone):model(m),data(std::move(setup)),reference(referenceBone){
 if(!data)throw std::runtime_error("Missing spring bone data");
 size_t n=model.bones.size();moving.assign(n,0);std::vector<int> nearest(n,-1);
 for(auto i:model.order){int p=model.bones[i].parent;bool driven=i<controlled.size()&&controlled[i];moving[i]=driven||(p>=0&&moving[p]);nearest[i]=driven?int(i):p>=0?nearest[p]:-1;}
 for(auto& j:data->joints){int p=model.bones[j.bone].parent;anchors.push_back(p>=0?nearest[p]:-1);}
 state.resize(data->joints.size());previousLocal.assign(data->joints.size(),btQuaternion::getIdentity());currentLocal=previousLocal;global.assign(model.bones.size(),btTransform::getIdentity());
 relativeDamping=springRelativeDamping();
}
btTransform SpringSystem::centre(const SpringSetup::Spring& s,const std::vector<btTransform>& skin)const{
 if(!centred(s))return btTransform::getIdentity();return data->isAffected[s.center]?global[s.center]:animated(skin,s.center);
}
void SpringSystem::place(const std::vector<btTransform>& skin,bool keepBend){
 auto& d=*data;
 for(int b:d.affected){
  int p=model.bones[b].parent;auto parentAnimated=parentFrame(skin,p);auto parentCurrent=p>=0&&d.isAffected[p]?global[p]:parentAnimated;
  auto local=parentAnimated.inverseTimes(animated(skin,b));int ji=d.jointOfBone[b];
  if(ji<0){global[b]=parentCurrent*local;continue;}
  auto& j=d.joints[ji];auto head=parentCurrent*local.getOrigin();auto rotation=(parentCurrent.getRotation()*(keepBend?currentLocal[ji]:btQuaternion::getIdentity())).normalized();
  global[b]=btTransform(rotation,head);auto tail=head+quatRotate(rotation,j.axis)*j.length;
  auto& s=d.springs[j.spring];auto c=centre(s,skin);state[ji].tail=state[ji].previousTail=centred(s)?c.invXform(tail):tail;
  if(!keepBend)currentLocal[ji]=btQuaternion::getIdentity();
 }
 previousLocal=currentLocal;
 if(reference>=0&&size_t(reference)<skin.size()){lastReference=animated(skin,reference).getOrigin();haveReference=true;}else haveReference=false;
}
void SpringSystem::reset(const std::vector<btTransform>& skin){relativeDamping=springRelativeDamping();place(skin,false);}
void SpringSystem::settle(const std::vector<btTransform>& skin){place(skin,true);}

void SpringSystem::step(float dt,const std::vector<btTransform>& skin,float gravityScale,float dragScale,const WorldContact* world){
 auto started=std::chrono::steady_clock::now();auto& d=*data;
 relativeDamping=springRelativeDamping();
 // VRM damping acts on world velocity, which drags every chain behind a
 // character that simply walks (about 45 degrees at 2 m/s with common hair
 // settings). Measured against the hips instead, steady locomotion and
 // ragdoll flight leave the rest shape alone while any change of speed or
 // direction still swings the chains; a stationary avatar is unchanged.
 btVector3 referenceStep(0,0,0);
 if(reference>=0&&size_t(reference)<skin.size()){auto now=animated(skin,reference).getOrigin();if(haveReference&&relativeDamping)referenceStep=now-lastReference;lastReference=now;haveReference=true;}
 previousLocal=currentLocal;
 for(int b:d.affected){
  int p=model.bones[b].parent;auto parentAnimated=parentFrame(skin,p);auto parentCurrent=p>=0&&d.isAffected[p]?global[p]:parentAnimated;
  auto local=parentAnimated.inverseTimes(animated(skin,b));int ji=d.jointOfBone[b];
  if(ji<0){global[b]=parentCurrent*local;continue;}
  auto& j=d.joints[ji];auto& s=state[ji];auto& spring=d.springs[j.spring];
  auto head=parentCurrent*local.getOrigin();auto parentRotation=parentCurrent.getRotation();auto rest=quatRotate(parentRotation,j.axis);
  bool inCentre=centred(spring);auto c=centre(spring,skin);
  auto tail=inCentre?c*s.tail:s.tail,previous=inCentre?c*s.previousTail:s.previousTail;
  float drag=std::clamp(j.dragForce*dragScale,0.f,1.f);
  auto inertia=(tail-previous)*(1-drag);if(!inCentre)inertia+=referenceStep*drag;
  auto next=tail+inertia+rest*(j.stiffness*dt*d.unitsPerMeter)+j.gravityDir*(j.gravityPower*gravityScale*dt*d.unitsPerMeter);
  auto constrain=[&]{auto v=next-head;float l=v.length();next=head+(l>1e-7f?v/l:rest)*j.length;};
  constrain();
  if(bodyContacts)for(int ci:spring.colliders){auto& col=d.colliders[ci];if(col.bone<0)continue;
   auto frame=d.isAffected[col.bone]?global[col.bone]:animated(skin,col.bone);auto a=frame*col.offset;btVector3 delta=next-a;
   if(col.capsule){auto segment=frame*col.tail-a;float along=segment.dot(delta),squared=segment.length2();if(along>0)delta-=along>=squared?segment:segment*(along/squared);}
   float length=delta.length(),distance=length-col.radius-j.hitRadius;
   if(distance<0){next-=(length>1e-7f?delta/length:rest)*distance;constrain();++colliderHits;}
  }
  if(world){auto outside=anchors[ji]>=0?animated(skin,anchors[ji]).getOrigin():head;
   for(int pass=0;pass<2&&(*world)(outside,head,next,j.hitRadius,j.length);pass++){constrain();++worldHits;}}
  s.previousTail=s.tail;s.tail=inCentre?c.invXform(next):next;
  auto to=next-head;auto rotation=(to.length2()>1e-12f?shortestArcQuatNormalize2(rest,to):btQuaternion::getIdentity())*parentRotation;rotation.normalize();
  global[b]=btTransform(rotation,head);currentLocal[ji]=(parentRotation.inverse()*rotation).normalized();
 }
 ++steps;stepMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();totalMs+=stepMs;
}
std::vector<btVector3> SpringSystem::tails(const std::vector<btTransform>& skin)const{
 std::vector<btVector3> out;out.reserve(state.size());
 for(size_t k=0;k<state.size();k++){auto& s=data->springs[data->joints[k].spring];out.push_back(centred(s)?centre(s,skin)*state[k].tail:state[k].tail);}
 return out;
}
}
