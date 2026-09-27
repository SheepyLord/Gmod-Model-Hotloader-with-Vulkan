#include "broadphase.hpp"
#include "scene.hpp"
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <iostream>
#include <future>
#include <random>
using namespace mmd;
int main(){try{
 int checks=0;auto check=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);++checks;std::cout<<"PASS "<<name<<"\n";};
 btDefaultCollisionConfiguration config;btCollisionDispatcher dispatcher(&config);
 {
  BroadphaseConfig fast;fast.leafDbvt=true;SecondaryBroadphase control, candidate(fast);
  std::vector<btCollisionObject> oldObjects(96),newObjects(96);std::mt19937 random(417);
  auto scalar=[&]{return float(int(random()%2001)-1000)/100.f;};
  for(int i=0;i<96;i++){
   auto p=btVector3(scalar(),scalar(),scalar());auto e=btVector3(.3f,.5f,.7f);
   oldObjects[i].setUserIndex(i);newObjects[i].setUserIndex(i);
   oldObjects[i].setBroadphaseHandle(control.createProxy(p-e,p+e,BOX_SHAPE_PROXYTYPE,&oldObjects[i],1,-1,&dispatcher));
   newObjects[i].setBroadphaseHandle(candidate.createProxy(p-e,p+e,BOX_SHAPE_PROXYTYPE,&newObjects[i],1,-1,&dispatcher));
  }
  auto pairs=[](SecondaryBroadphase& b){std::vector<std::pair<int,int>> out;auto& list=b.getOverlappingPairCache()->getOverlappingPairArray();for(int i=0;i<list.size();i++)out.emplace_back(static_cast<btCollisionObject*>(list[i].m_pProxy0->m_clientObject)->getUserIndex(),static_cast<btCollisionObject*>(list[i].m_pProxy1->m_clientObject)->getUserIndex());return out;};
  bool exact=true;int peakPairs=0;
  for(int frame=0;frame<500;frame++){
   for(int i=0;i<96;i++){
    // Stale objects become static; tiny moves exercise fat-leaf containment;
    // occasional large moves exercise reinsertion and static-to-dynamic moves.
    if((frame+i)%7<3)continue;btVector3 lo,hi;control.getAabb(oldObjects[i].getBroadphaseHandle(),lo,hi);
    auto delta=btVector3(scalar(),scalar(),scalar())*((frame%23)==0?1.f:.005f);lo+=delta;hi+=delta;
    control.setAabb(oldObjects[i].getBroadphaseHandle(),lo,hi,&dispatcher);
    candidate.setAabb(newObjects[i].getBroadphaseHandle(),lo,hi,&dispatcher);
   }
   control.calculateOverlappingPairs(&dispatcher);candidate.calculateOverlappingPairs(&dispatcher);exact=exact&&pairs(control)==pairs(candidate);peakPairs=std::max(peakPairs,control.getOverlappingPairCache()->getNumOverlappingPairs());
  }
  check(exact&&peakPairs>0,"optimized DBVT preserves complete pair order through motion and tree staging");
  for(int i=0;i<96;i++){control.destroyProxy(oldObjects[i].getBroadphaseHandle(),&dispatcher);candidate.destroyProxy(newObjects[i].getBroadphaseHandle(),&dispatcher);}
  check(candidate.getOverlappingPairCache()->getNumOverlappingPairs()==0,"optimized DBVT clears all pairs on removal");
 }
 for(bool capacity:{false,true}){
  BroadphaseConfig c;c.sap=true;c.capacity=4;c.minimum={-10,-10,-10};c.maximum={10,10,10};SecondaryBroadphase bp(c);
  btSphereShape shape(1);std::vector<std::unique_ptr<btCollisionObject>> objects;
  auto add=[&](float x){auto o=std::make_unique<btCollisionObject>();o->setCollisionShape(&shape);o->setWorldTransform(btTransform(btQuaternion::getIdentity(),{x,0,0}));auto proxy=bp.createProxy({x-1,-1,-1},{x+1,1,1},SPHERE_SHAPE_PROXYTYPE,o.get(),1,-1,&dispatcher);o->setBroadphaseHandle(proxy);objects.push_back(std::move(o));};
  add(0);add(1);check(bp.mode()=="sap","small scene uses SAP");
  if(capacity){add(2);add(3);}else {objects[0]->getWorldTransform().setOrigin({100,0,0});bp.setAabb(objects[0]->getBroadphaseHandle(),{99,-1,-1},{101,1,1},&dispatcher);}
  check(bp.mode()=="dbvt"&&bp.fallbackReason()==(capacity?"capacity":"bounds"),"capacity or bounds migrates to DBVT");
  bool valid=true;for(auto& o:objects){btVector3 a,b;bp.getAabb(o->getBroadphaseHandle(),a,b);bool ok=(a+b).distance(o->getWorldTransform().getOrigin()*2)<.001f&&o->getBroadphaseHandle()->m_clientObject==o.get();if(!ok)std::cerr<<"AABB "<<a.x()<<","<<b.x()<<" transform "<<o->getWorldTransform().getOrigin().x()<<" owner "<<(o->getBroadphaseHandle()->m_clientObject==o.get())<<"\n";valid&=ok;}
  check(valid,"migration preserves all object handles, AABBs and transforms");
  bp.calculateOverlappingPairs(&dispatcher);check(bp.getOverlappingPairCache()->getNumOverlappingPairs()>=(capacity?1:0),"migrated collision pairs remain available");
  for(auto& o:objects)bp.destroyProxy(o->getBroadphaseHandle(),&dispatcher);
  check(bp.getOverlappingPairCache()->getNumOverlappingPairs()==0,"migrated objects remove safely");
 }
 auto create=[](bool sap){nanoem_status_t status=NANOEM_STATUS_SUCCESS;BroadphaseConfig c;c.sap=sap;auto w=nanoemCreateSecondaryWorld(c,&status);auto mode=nanoemBroadphaseInfo(w)["broadphase"];nanoemPhysicsWorldDestroy(w);return mode== (sap?"sap":"dbvt");};
 auto a=std::async(std::launch::async,[&]{for(int i=0;i<16;i++)if(!create(true))return false;return true;});
 auto b=std::async(std::launch::async,[&]{for(int i=0;i<16;i++)if(!create(false))return false;return true;});
 check(a.get()&&b.get(),"concurrent world creation keeps independent backend choices");
 setSecondaryBroadphaseDefault("sap");nanoem_status_t status=NANOEM_STATUS_SUCCESS;auto reference=nanoemPhysicsWorldCreate(nullptr,&status);
 check(nanoemBroadphaseInfo(reference)["broadphase"]=="dbvt","direct nanoem reference stays DBVT despite runtime candidate default");nanoemPhysicsWorldDestroy(reference);setSecondaryBroadphaseDefault("dbvt");
 std::cout<<checks<<" broadphase checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
