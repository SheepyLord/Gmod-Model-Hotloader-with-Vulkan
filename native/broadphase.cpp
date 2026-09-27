#include "broadphase.hpp"
#include "dbvt_leaf.hpp"
#include <BulletCollision/CollisionDispatch/btCollisionObject.h>
#include <atomic>
#include <cmath>
#include <map>
#include <stdexcept>
#include <vector>
namespace mmd {
namespace {thread_local BroadphaseConfig creationConfig;std::atomic<int> defaultMode{3};std::atomic<float> v2Margin{.05f};}
void setSecondaryDbvtMargin(float margin){if(!std::isfinite(margin)||margin<0||margin>10)throw std::runtime_error("DBVT margin must be 0 through 10 PMX units");v2Margin.store(margin);}
float secondaryDbvtMargin(){return v2Margin.load();}
bool isSapBroadphase(const std::string& name){if(name=="sap")return true;if(name=="dbvt"||name=="dbvt-fast"||name=="auto")return false;throw std::runtime_error("Broadphase must be auto, dbvt, dbvt-fast or sap");}
// `auto` keeps the order-preserving DBVT for the replay-exact backends and
// gives the asynchronous v2 worlds sweep-and-prune, whose incremental endpoint
// sort is far cheaper for a few hundred bodies that all move every tick.
std::string resolveBroadphase(const std::string& requested,bool relaxedOrder){isSapBroadphase(requested);if(requested!="auto")return requested;return relaxedOrder?"sap":"dbvt-fast";}
void setSecondaryBroadphaseDefault(const std::string& name){bool sap=isSapBroadphase(name);defaultMode.store(sap?1:name=="dbvt-fast"?2:name=="auto"?3:0);}
std::string secondaryBroadphaseDefault(){auto mode=defaultMode.load();return mode==1?"sap":mode==2?"dbvt-fast":mode==3?"auto":"dbvt";}
BroadphaseCreationScope::BroadphaseCreationScope(const BroadphaseConfig& c):previous(creationConfig){creationConfig=c;}
BroadphaseCreationScope::~BroadphaseCreationScope(){creationConfig=previous;}
struct SecondaryBroadphase::Impl {
 BroadphaseConfig config;
 btHashedOverlappingPairCache pairs;
 std::unique_ptr<btBroadphaseInterface> inner;
 struct Record {btCollisionObject* object;int shape,group,mask;btVector3 minimum,maximum;};
 std::map<btBroadphaseProxy*,Record> records;
 std::string fallback;
 explicit Impl(const BroadphaseConfig& c):config(c){
  if(c.sap&&c.leafDbvt)throw std::runtime_error("Conflicting broadphase options");
  if(c.sap&&c.capacity<2)throw std::runtime_error("Invalid SAP capacity");
  if(c.sap)for(int k=0;k<3;k++)if(!std::isfinite(c.minimum[k])||!std::isfinite(c.maximum[k])||c.minimum[k]>=c.maximum[k])throw std::runtime_error("Invalid SAP bounds");
  if(c.sap)inner=std::make_unique<bt32BitAxisSweep3>(c.minimum,c.maximum,c.capacity,&pairs);
  else if(c.leafDbvt)inner=std::make_unique<LeafDbvt>(&pairs,c.margin);
  else inner=std::make_unique<btDbvtBroadphase>(&pairs);
 }
 bool contains(const btVector3& a,const btVector3& b)const {for(int k=0;k<3;k++)if(a[k]<config.minimum[k]||b[k]>config.maximum[k])return false;return true;}
 void useDbvt(btDispatcher* dispatcher,const char* reason){
  // This runs only at an AABB/proxy update boundary, never inside the solver.
  // Retain objects, velocities, constraints and the external collision filter.
  // Contact caches are rebuilt once; report the transition instead of silently
  // clamping geometry or exhausting Bullet's fixed SAP handle allocator.
  auto next=std::make_unique<btDbvtBroadphase>(&pairs);
  std::vector<Record> saved;for(auto& [proxy,r]:records){auto copy=r;inner->getAabb(proxy,copy.minimum,copy.maximum);saved.push_back(copy);}
  for(auto& [proxy,r]:records)inner->destroyProxy(proxy,dispatcher);
  records.clear();inner=std::move(next);config.sap=false;fallback=reason;
  for(auto& r:saved){auto p=inner->createProxy(r.minimum,r.maximum,r.shape,r.object,r.group,r.mask,dispatcher);r.object->setBroadphaseHandle(p);records.emplace(p,r);}
 }
};
SecondaryBroadphase::SecondaryBroadphase():SecondaryBroadphase(creationConfig){}
SecondaryBroadphase::SecondaryBroadphase(const BroadphaseConfig& c):impl(std::make_unique<Impl>(c)){}
SecondaryBroadphase::~SecondaryBroadphase()=default;
std::string SecondaryBroadphase::mode()const{return impl->config.sap?"sap":impl->config.leafDbvt?"dbvt-fast":"dbvt";}
std::string SecondaryBroadphase::fallbackReason()const{return impl->fallback;}
unsigned SecondaryBroadphase::capacity()const{return impl->config.sap?impl->config.capacity:0;}
btBroadphaseProxy* SecondaryBroadphase::createProxy(const btVector3& a,const btVector3& b,int shape,void* user,int group,int mask,btDispatcher* d){
 if(impl->config.sap){if(impl->records.size()+1>=impl->config.capacity)impl->useDbvt(d,"capacity");else if(!impl->contains(a,b))impl->useDbvt(d,"bounds");}
 auto p=impl->inner->createProxy(a,b,shape,user,group,mask,d);
 // Bullet's SAP initializes its edges but leaves these query fields untouched
 // until the first setAabb. Initialize them before a possible capacity fallback.
 p->m_aabbMin=a;p->m_aabbMax=b;
 impl->records.emplace(p,Impl::Record{static_cast<btCollisionObject*>(user),shape,group,mask,a,b});return p;
}
void SecondaryBroadphase::destroyProxy(btBroadphaseProxy* p,btDispatcher* d){impl->records.erase(p);impl->inner->destroyProxy(p,d);}
void SecondaryBroadphase::setAabb(btBroadphaseProxy* p,const btVector3& a,const btVector3& b,btDispatcher* d){
 if(impl->config.sap&&!impl->contains(a,b)){auto object=static_cast<btCollisionObject*>(p->m_clientObject);impl->useDbvt(d,"bounds");p=object->getBroadphaseHandle();}
 impl->inner->setAabb(p,a,b,d);
}
void SecondaryBroadphase::getAabb(btBroadphaseProxy* p,btVector3& a,btVector3& b)const{impl->inner->getAabb(p,a,b);}
void SecondaryBroadphase::rayTest(const btVector3& a,const btVector3& b,btBroadphaseRayCallback& c,const btVector3& lo,const btVector3& hi){impl->inner->rayTest(a,b,c,lo,hi);}
void SecondaryBroadphase::aabbTest(const btVector3& a,const btVector3& b,btBroadphaseAabbCallback& c){impl->inner->aabbTest(a,b,c);}
void SecondaryBroadphase::calculateOverlappingPairs(btDispatcher* d){impl->inner->calculateOverlappingPairs(d);}
btOverlappingPairCache* SecondaryBroadphase::getOverlappingPairCache(){return &impl->pairs;}
const btOverlappingPairCache* SecondaryBroadphase::getOverlappingPairCache()const{return &impl->pairs;}
void SecondaryBroadphase::getBroadphaseAabb(btVector3& a,btVector3& b)const{impl->inner->getBroadphaseAabb(a,b);}
void SecondaryBroadphase::resetPool(btDispatcher* d){impl->inner->resetPool(d);}
void SecondaryBroadphase::printStats(){impl->inner->printStats();}
}
