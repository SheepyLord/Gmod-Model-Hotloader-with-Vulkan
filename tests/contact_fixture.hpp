#pragma once
#include "scene.hpp"
#include "rig.hpp"
namespace mmd::test {
inline std::shared_ptr<SceneFrame> contactScene(double time,int mode){
 auto frame=std::make_shared<SceneFrame>();frame->timestamp=time;frame->sequence=uint64_t(time*60)+1;
 static auto floor=[] {auto p=std::make_shared<SceneGeometry>();p->kind=SceneGeometry::Triangles;p->vertices={{-512,-512,0},{512,512,0},{512,-512,0},{-512,-512,0},{-512,512,0},{512,512,0}};p->minimum={-512,-512,0};p->maximum={512,512,0};return p;}();
 static auto plate=[] {auto p=std::make_shared<SceneGeometry>();p->kind=SceneGeometry::Convexes;p->minimum={-16,-16,-2};p->maximum={16,16,2};p->hullCounts={8};for(float x:{-1.f,1.f})for(float y:{-1.f,1.f})for(float z:{-1.f,1.f})p->vertices.emplace_back(x*16,y*16,z*2);return p;}();
 if(mode){SceneObject o;o.id=100;o.isStatic=true;o.geometry=floor;frame->objects.push_back(o);}
 if(mode==2){SceneObject o;o.id=101;o.geometry=plate;o.transform.setOrigin({float(std::sin(time)*12),0,28});o.velocity={float(std::cos(time)*12),0,0};frame->objects.push_back(o);}
 return frame;
}
// Independent raw Bullet scene adapter for the nanoem reference world. It uses
// the same immutable Source input, without calling Secondary::External.
struct ReferenceScene {
 struct Filter:btOverlapFilterCallback {bool needBroadphaseCollision(btBroadphaseProxy* a,btBroadphaseProxy* b)const override{
  auto x=static_cast<btCollisionObject*>(a->m_clientObject),y=static_cast<btCollisionObject*>(b->m_clientObject);bool ex=x->getUserIndex2()==ExternalCollisionTag,ey=y->getUserIndex2()==ExternalCollisionTag;
  if(ex||ey)return ex!=ey&&!(ex?y:x)->isStaticOrKinematicObject();return (a->m_collisionFilterGroup&b->m_collisionFilterMask)&&(b->m_collisionFilterGroup&a->m_collisionFilterMask);
 }} filter;
 struct Object {std::unique_ptr<btTriangleMesh> triangles;std::unique_ptr<btConvexHullShape> hull;std::unique_ptr<btCollisionShape> shape;std::unique_ptr<btDefaultMotionState> motion;std::unique_ptr<btRigidBody> body;};
 btSoftRigidDynamicsWorld* world;const Instance& instance;std::vector<Object> objects;
 ReferenceScene(btSoftRigidDynamicsWorld* w,const Instance& p):world(w),instance(p){world->getPairCache()->setOverlapFilterCallback(&filter);}
 ~ReferenceScene(){for(auto& o:objects)world->removeRigidBody(o.body.get());world->getPairCache()->setOverlapFilterCallback(nullptr);}
 void sync(const SceneFrame& frame){
  auto c=basis();float scale=instance.sourceRig->scale;
  for(size_t i=0;i<frame.objects.size();i++){
   const auto& input=frame.objects[i];auto t=input.transform;t.getOrigin()*=Inch;t=instance.placement.inverse()*t;btTransform pose(c.transpose()*t.getBasis()*c,fromSource(t.getOrigin())/instance.scale);
   if(i==objects.size()){
    Object o;if(input.geometry->kind==SceneGeometry::Triangles){o.triangles=std::make_unique<btTriangleMesh>();auto& v=input.geometry->vertices;for(size_t j=0;j<v.size();j+=3)o.triangles->addTriangle(fromSource(v[j])/scale,fromSource(v[j+1])/scale,fromSource(v[j+2])/scale);o.shape=std::make_unique<btBvhTriangleMeshShape>(o.triangles.get(),true);}
    else{o.hull=std::make_unique<btConvexHullShape>();for(auto& v:input.geometry->vertices)o.hull->addPoint(fromSource(v)/scale,false);o.hull->setMargin(.02f/scale);o.hull->recalcLocalAabb();auto compound=std::make_unique<btCompoundShape>();compound->addChildShape(btTransform::getIdentity(),o.hull.get());o.shape=std::move(compound);}
    o.shape->setMargin(.02f/scale);o.motion=std::make_unique<btDefaultMotionState>(pose);btRigidBody::btRigidBodyConstructionInfo info(0,o.motion.get(),o.shape.get());o.body=std::make_unique<btRigidBody>(info);o.body->setCollisionFlags(o.body->getCollisionFlags()|btCollisionObject::CF_KINEMATIC_OBJECT);o.body->setActivationState(DISABLE_DEACTIVATION);o.body->setFriction(.5f);o.body->setUserIndex2(ExternalCollisionTag);o.body->setUserIndex(input.isStatic?1:2);world->addRigidBody(o.body.get(),1,-1);objects.push_back(std::move(o));
   }
   objects[i].motion->setWorldTransform(pose);
  }
 }
};
}
