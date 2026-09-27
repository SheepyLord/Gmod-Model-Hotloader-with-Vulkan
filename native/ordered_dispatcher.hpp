#pragma once
#include <BulletCollision/CollisionDispatch/btCollisionDispatcherMt.h>
#include <BulletCollision/CollisionDispatch/btCollisionObjectWrapper.h>
#include <BulletCollision/BroadphaseCollision/btOverlappingPairCache.h>
#include <BulletCollision/NarrowPhaseCollision/btPersistentManifold.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include "jobs.hpp"
#include <atomic>
#include <cstdint>
#include <vector>
namespace mmd {
// Mid-phase gate for the multicore dispatchers (cpu_mt, cpu_mt_v2, gpu_opencl).
// A broadphase pair lives while the fat DBVT volumes or SAP intervals overlap,
// and Bullet runs its narrowphase algorithm for every live pair each step. A
// dense PMX rig keeps thousands of such pairs alive for a few hundred contacts.
// The gate compares the two bodies' current tight AABBs (the values
// updateSingleAabb stored on the proxies, already padded by
// gContactBreakingThreshold) padded once more by the pair manifold's own
// contact breaking threshold. No algorithm adds a point whose surface gap
// exceeds that threshold, so a separated pair can only age or drop the points
// it already has, which is exactly what the skipped algorithm would have done
// at its end (refreshContactPoints). The step therefore stays bit-identical.
// Pairs without an algorithm yet, and compound pairs with several manifolds,
// take the ordinary path.
// Defined in nanoem_backend.cpp so the runtime DLL owns the single switch.
void setMidphaseGate(bool enabled);
bool midphaseGate();
// Bullet's stock MT dispatcher merges new manifolds by worker index. Keeping
// the reference pair traversal order prevents that scheduling detail from
// changing the order of the sequential impulse solver every frame.
class OrderedDispatcher final:public btCollisionDispatcherMt {
 struct Operation {btPersistentManifold* manifold;bool add;};
 std::vector<std::vector<Operation>> operations;
 inline static thread_local std::vector<Operation>* recording=nullptr;
 // Every PMX primitive lies inside a capsule: a sphere or capsule exactly, a
 // box within the capsule around its longest axis whose radius covers the other
 // two half extents (margins included). The gap between two shapes is therefore
 // at least the gap between their capsules, which costs one segment distance
 // instead of a GJK or box SAT query. Only pairs that Bullet would send through
 // those general algorithms (at least one box) take this test; capsule and
 // sphere pairs already have analytic paths.
 struct Sweep {btVector3 a,b;btScalar radius;};
 static bool sweep(const btCollisionObject* object,Sweep& out,bool& box){
  auto shape=object->getCollisionShape();const btTransform& t=object->getWorldTransform();
  switch(shape->getShapeType()){
   case SPHERE_SHAPE_PROXYTYPE:{out.a=out.b=t.getOrigin();out.radius=static_cast<const btSphereShape*>(shape)->getRadius();return true;}
   case CAPSULE_SHAPE_PROXYTYPE:{auto capsule=static_cast<const btCapsuleShape*>(shape);auto axis=t.getBasis().getColumn(capsule->getUpAxis())*capsule->getHalfHeight();out.a=t.getOrigin()-axis;out.b=t.getOrigin()+axis;out.radius=capsule->getRadius();return true;}
   case BOX_SHAPE_PROXYTYPE:{auto extents=static_cast<const btBoxShape*>(shape)->getHalfExtentsWithMargin();int k=extents.maxAxis();auto axis=t.getBasis().getColumn(k)*extents[k];out.a=t.getOrigin()-axis;out.b=t.getOrigin()+axis;out.radius=btSqrt(btMax(btScalar(0),extents.length2()-extents[k]*extents[k]));box=true;return true;}
   default:return false;
  }
 }
 // Squared distance between two segments (Ericson, Real-Time Collision Detection 5.1.9).
 static btScalar segmentDistance2(const Sweep& p,const Sweep& q){
  btVector3 d1=p.b-p.a,d2=q.b-q.a,r=p.a-q.a;btScalar a=d1.dot(d1),e=d2.dot(d2),f=d2.dot(r),s,t;
  if(a<=SIMD_EPSILON&&e<=SIMD_EPSILON)return r.length2();
  if(a<=SIMD_EPSILON){s=0;t=btClamped(f/e,btScalar(0),btScalar(1));}
  else{btScalar c=d1.dot(r);
   if(e<=SIMD_EPSILON){t=0;s=btClamped(-c/a,btScalar(0),btScalar(1));}
   else{btScalar b=d1.dot(d2),denominator=a*e-b*b;s=denominator!=0?btClamped((b*f-c*e)/denominator,btScalar(0),btScalar(1)):btScalar(0);t=(b*s+f)/e;
    if(t<0){t=0;s=btClamped(-c/a,btScalar(0),btScalar(1));}else if(t>1){t=1;s=btClamped((b-c)/a,btScalar(0),btScalar(1));}}}
  return (p.a+d1*s-(q.a+d2*t)).length2();
 }
 static void gatedNearCallback(btBroadphasePair& pair,btCollisionDispatcher& dispatcher,const btDispatcherInfo& info){
  auto object0=static_cast<btCollisionObject*>(pair.m_pProxy0->m_clientObject),object1=static_cast<btCollisionObject*>(pair.m_pProxy1->m_clientObject);
  if(!dispatcher.needsCollision(object0,object1))return;
  auto& self=static_cast<OrderedDispatcher&>(dispatcher);
  if(pair.m_algorithm&&midphaseGate()){
   thread_local btManifoldArray manifolds;manifolds.resizeNoInitialize(0);
   pair.m_algorithm->getAllContactManifolds(manifolds);
   if(manifolds.size()==1){
    auto manifold=manifolds[0];const btScalar pad=manifold->getContactBreakingThreshold();
    const btVector3 &a0=pair.m_pProxy0->m_aabbMin,&a1=pair.m_pProxy0->m_aabbMax,&b0=pair.m_pProxy1->m_aabbMin,&b1=pair.m_pProxy1->m_aabbMax;
    bool separated=a0.x()-pad>b1.x()||b0.x()-pad>a1.x()||a0.y()-pad>b1.y()||b0.y()-pad>a1.y()||a0.z()-pad>b1.z()||b0.z()-pad>a1.z();
    if(!separated){
     Sweep s0,s1;bool box=false;
     if(sweep(object0,s0,box)&&sweep(object1,s1,box)&&box){
      // A little slack keeps rounding from ever admitting a pair Bullet would touch.
      const btScalar limit=s0.radius+s1.radius+pad+btScalar(1e-4);
      separated=segmentDistance2(s0,s1)>limit*limit;
      if(separated&&self.countPairs)self.sweptPairs.fetch_add(1,std::memory_order_relaxed);
     }
    }
    if(separated){
     if(manifold->getNumContacts())manifold->refreshContactPoints(manifold->getBody0()->getWorldTransform(),manifold->getBody1()->getWorldTransform());
     if(self.countPairs)self.gatedPairs.fetch_add(1,std::memory_order_relaxed);
     return;
    }
   }
  }
  if(self.countPairs)self.dispatchedPairs.fetch_add(1,std::memory_order_relaxed);
  btCollisionObjectWrapper wrap0(nullptr,object0->getCollisionShape(),object0,object0->getWorldTransform(),-1,-1);
  btCollisionObjectWrapper wrap1(nullptr,object1->getCollisionShape(),object1,object1->getWorldTransform(),-1,-1);
  if(!pair.m_algorithm)pair.m_algorithm=dispatcher.findAlgorithm(&wrap0,&wrap1,nullptr,BT_CONTACT_POINT_ALGORITHMS);
  if(!pair.m_algorithm)return;
  btManifoldResult result(&wrap0,&wrap1);
  if(info.m_dispatchFunc==btDispatcherInfo::DISPATCH_DISCRETE)pair.m_algorithm->processCollision(&wrap0,&wrap1,info,&result);
  else {btScalar toi=pair.m_algorithm->calculateTimeOfImpact(object0,object1,info,&result);if(info.m_timeOfImpact>toi)info.m_timeOfImpact=toi;}
 }
public:
 // Diagnostic counters, enabled by the standalone profiler only.
 bool countPairs=false;
 std::atomic<uint64_t> dispatchedPairs{0},gatedPairs{0},sweptPairs{0}; // sweptPairs: gated by the capsule bound rather than the AABBs
 explicit OrderedDispatcher(btCollisionConfiguration* config):btCollisionDispatcherMt(config,64){
  // The application's worker count may change between frames while worlds live.
  m_batchManifoldsPtr.resize(BT_MAX_THREAD_COUNT);m_batchReleasePtr.resize(BT_MAX_THREAD_COUNT);
  setNearCallback(gatedNearCallback);
 }
 btPersistentManifold* getNewManifold(const btCollisionObject* a,const btCollisionObject* b)override{
  auto p=btCollisionDispatcherMt::getNewManifold(a,b);if(m_batchUpdating&&recording)recording->push_back({p,true});return p;
 }
 void releaseManifold(btPersistentManifold* p)override{
  if(m_batchUpdating&&recording)recording->push_back({p,false});btCollisionDispatcherMt::releaseManifold(p);
 }
 void dispatchAllCollisionPairs(btOverlappingPairCache* cache,const btDispatcherInfo& info,btDispatcher*)override{
  size_t n=cache->getNumOverlappingPairs();if(!n)return;operations.resize(n);auto pairs=cache->getOverlappingPairArrayPtr();auto callback=getNearCallback();
  m_batchUpdating=true;
  parallelForSecondary(n,64,[&](size_t a,size_t b){for(size_t i=a;i<b;i++){auto previous=recording;recording=&operations[i];callback(pairs[i],*this,info);recording=previous;}});
  m_batchUpdating=false;
  for(auto& ops:operations){for(auto& op:ops){if(op.add){op.manifold->m_index1a=m_manifoldsPtr.size();m_manifoldsPtr.push_back(op.manifold);}else btCollisionDispatcherMt::releaseManifold(op.manifold);}ops.clear();}
  for(int i=0;i<BT_MAX_THREAD_COUNT;i++){m_batchManifoldsPtr[i].resizeNoInitialize(0);m_batchReleasePtr[i].resizeNoInitialize(0);}
 }
};
}
