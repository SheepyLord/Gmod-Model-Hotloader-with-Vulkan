#pragma once
#include <btBulletDynamicsCommon.h>
#include <span>

namespace mmd {
struct ProjectionObstacle {
 btCollisionObject* object;
 btVector3 minimum,maximum;
};
// Project translations against external geometry without modifying it. Starting
// contacts must permit escape/tangential motion, otherwise a resting strand
// would stick to the floor when its attachment is lifted.
inline btVector3 contactSafeTranslation(const btConvexShape& shape,const btTransform& initial,
                                       btVector3 movement,std::span<const ProjectionObstacle> obstacles,
                                       unsigned& contacts) {
 struct InwardHit:btCollisionWorld::ClosestConvexResultCallback {
  btVector3 movement;
  InwardHit(const btVector3& from,const btVector3& to):ClosestConvexResultCallback(from,to),movement(to-from){}
  btScalar addSingleResult(btCollisionWorld::LocalConvexResult& hit,bool world) override {
   auto normal=world?hit.m_hitNormalLocal:hit.m_hitCollisionObject->getWorldTransform().getBasis()*hit.m_hitNormalLocal;
   if(normal.dot(movement)>=-1e-6f*movement.length())return m_closestHitFraction;
   return ClosestConvexResultCallback::addSingleResult(hit,world);
  }
 };
 auto pose=initial;btVector3 planes[3];unsigned planeCount=0;
 for(unsigned pass=0;pass<3&&movement.length2()>1e-12f;pass++){
  auto target=pose;target.getOrigin()+=movement;
  btVector3 lo,hi,endLo,endHi;shape.getAabb(pose,lo,hi);shape.getAabb(target,endLo,endHi);lo.setMin(endLo);hi.setMax(endHi);
  InwardHit hit(pose.getOrigin(),target.getOrigin());
  for(auto& obstacle:obstacles){
   if(lo.x()>obstacle.maximum.x()||lo.y()>obstacle.maximum.y()||lo.z()>obstacle.maximum.z()||
      hi.x()<obstacle.minimum.x()||hi.y()<obstacle.minimum.y()||hi.z()<obstacle.minimum.z())continue;
   auto o=obstacle.object;
   btCollisionWorld::objectQuerySingle(&shape,pose,target,o,o->getCollisionShape(),o->getWorldTransform(),hit,0);
  }
  if(!hit.hasHit()){pose=target;break;}
  ++contacts;auto normal=hit.m_hitNormalWorld.normalized();
  // Stop just before contact, then spend the remaining correction tangentially.
  auto fraction=btMax(0.f,hit.m_closestHitFraction-.001f/btMax(.001f,movement.length()));
  pose.getOrigin()+=movement*fraction;movement*=1-fraction;planes[planeCount++]=normal;
  // Multiple planes (floor + wall, corners) must remain satisfied together.
  for(unsigned sweep=0;sweep<3;sweep++)for(unsigned i=0;i<planeCount;i++){
   auto into=movement.dot(planes[i]);if(into<0)movement-=planes[i]*into;
  }
 }
 return pose.getOrigin()-initial.getOrigin();
}
}
