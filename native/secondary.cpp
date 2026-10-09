// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2015-2023 hkrn All rights reserved (adapted emapp portions).
// Pose synchronization adapted from nanoem emapp model/RigidBody.cc and SoftBody.cc,
// revision 30acffaa29f5d2eb9e997d69418f2e4b97b5894f. See THIRD_PARTY.md.
// The MIT physics extension has the documented world-anchor initialization fix.
#include "secondary.hpp"
#include "scene.hpp"
#include "compute_solver.hpp"
#include "vulkan_solver.hpp"
#include "jobs.hpp"
#include "contact_projection.hpp"
#include <BulletDynamics/ConstraintSolver/btGeneric6DofConstraint.h>
#include <atomic>
#include <span>
#include <map>
#include <chrono>
#include <thread>
#include <set>
#include <queue>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <LinearMath/btQuickprof.h>
#include <cstdio>
namespace mmd {
namespace {
// The opaque joint input ABI from pinned nanoem/ext/physics_bullet.cc.
struct JointInput {nanoem_physics_world_t* world; nanoem_physics_rigid_body_t *a,*b; float transformA[16],transformB[16];};
static_assert(sizeof(JointInput)==152);
void set(nanoem_physics_rigid_body_t* body,const btTransform& t){float m[16];t.getOpenGLMatrix(m);nanoemPhysicsRigidBodySetWorldTransform(body,m);}
btTransform blendPose(const btTransform& a,const btTransform& b,float t){if(t<=1e-6f)return a;if(t>=1-1e-6f)return b;return btTransform(a.getRotation().slerp(b.getRotation(),t),a.getOrigin().lerp(b.getOrigin(),t));}
double elapsedMs(std::chrono::steady_clock::time_point since){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-since).count();}
// Shared by every v2 world. Bodies below both thresholds for `sleepSeconds`
// deactivate; a moving follower wakes the chains it drives.
std::atomic<bool> sleepEnabled{true};std::atomic<float> sleepLinear{.5f},sleepAngular{.35f},sleepSeconds{1.5f},sleepWakeDrift{.02f};std::atomic<unsigned> sleepVersion{1};
std::atomic<double> waitBudgetMs{0};
std::atomic<double> testStepDelayMs{0};
std::mutex tuningMutex;
struct Tuning {int iterations=10;float gravity=1,damping=1;bool stretch=false;float tolerance=1;};
Tuning physicsTuning;std::atomic<unsigned> tuningVersion{1};
constexpr double TickSeconds=1./60;
constexpr unsigned MaxStepsPerInput=4;
constexpr double MaxAsyncJobMs=30;
// Distinct simulation states the render thread keeps for interpolation: the
// display runs one tick plus at most two ticks of margin behind the input
// clock, so five states (four ticks of history) always bracket it.
constexpr size_t PresentHistoryDepth=5;
}
struct Secondary::Filter:btOverlapFilterCallback {
 bool pruneKinematic=false;
 // Collide::Character: hair and clothing (simulated bodies) against the body
 // (the bodies that follow bones).
 bool characterContacts=true;
 bool needBroadphaseCollision(btBroadphaseProxy* a,btBroadphaseProxy* b) const override {
  auto x=static_cast<btCollisionObject*>(a->m_clientObject),y=static_cast<btCollisionObject*>(b->m_clientObject);
  bool ex=x->getUserIndex2()==ExternalCollisionTag,ey=y->getUserIndex2()==ExternalCollisionTag;
  if(ex||ey)return ex!=ey&&!(ex?y:x)->isStaticOrKinematicObject();
  if(!characterContacts&&x->isStaticOrKinematicObject()!=y->isStaticOrKinematicObject())return false;
  // Two followers never produce a contact response; skipping the pair saves the
  // broadphase bookkeeping and narrowphase visit Bullet would otherwise spend.
  if(pruneKinematic&&x->isStaticOrKinematicObject()&&y->isStaticOrKinematicObject())return false;
  return (a->m_collisionFilterGroup&b->m_collisionFilterMask)&&(b->m_collisionFilterGroup&a->m_collisionFilterMask);
 }
};
struct Secondary::External {
 struct Shape {std::shared_ptr<const SceneGeometry> source;std::unique_ptr<btTriangleMesh> mesh;std::vector<std::unique_ptr<btCollisionShape>> children;std::unique_ptr<btCollisionShape> shape;};
 struct Object {std::shared_ptr<Shape> geometry;std::unique_ptr<btDefaultMotionState> motion;std::unique_ptr<btRigidBody> body;};
 btSoftRigidDynamicsWorld* world;Secondary& owner;std::map<uint64_t,Object> objects;unsigned contacts=0,worldContacts=0,objectContacts=0;double syncMs=0,captureMs=0;uint64_t sequence=0;
 btVector3 interestCenter;bool interestPlaced=false;
 std::vector<uint8_t> stretched;
 struct Surface {btCollisionObject* object;btVector3 minimum,maximum;};
 std::vector<Surface> surfaces;
 std::vector<ProjectionObstacle> projectionObstacles;
 // Per PMX body, the obstacles it shares a broadphase pair with this step.
 // A touching pair carries its contact normals (pointing out of the obstacle):
 // clamping a correction against those planes is what a convex sweep from a
 // touching start returns (fraction zero and the contact normal), at the cost
 // of a dot product instead of a GJK query. A paired obstacle without points
 // is swept once the motion exceeds the pair's contact threshold; beyond the
 // broadphase padding every obstacle is swept again. The clustered scene used
 // to sweep every strand correction against every mirrored hull.
 struct Candidate {int obstacle;bool touching;float threshold;int normalCount;btVector3 normals[8];};
 std::vector<std::vector<Candidate>> candidates;std::vector<ProjectionObstacle> scratch;std::unordered_map<const btCollisionObject*,int> obstacleIndex;
 static constexpr float PairPadding=.04f; // two proxies, each padded by gContactBreakingThreshold
 void prepareProjection(){
  projectionObstacles.clear();obstacleIndex.clear();
  for(auto& [id,o]:objects){
   ProjectionObstacle p;p.object=o.body.get();p.object->getCollisionShape()->getAabb(p.object->getWorldTransform(),p.minimum,p.maximum);
   obstacleIndex[p.object]=int(projectionObstacles.size());projectionObstacles.push_back(p);
  }
  candidates.resize(owner.bodies.size());for(auto& list:candidates)list.clear();
  auto cache=world->getPairCache();auto pairs=cache->getOverlappingPairArrayPtr();int count=cache->getNumOverlappingPairs();
  thread_local btManifoldArray manifolds;
  for(int i=0;i<count;i++){auto& pair=pairs[i];auto a=static_cast<const btCollisionObject*>(pair.m_pProxy0->m_clientObject),b=static_cast<const btCollisionObject*>(pair.m_pProxy1->m_clientObject);
   bool ea=a->getUserIndex2()==ExternalCollisionTag,eb=b->getUserIndex2()==ExternalCollisionTag;if(ea==eb)continue;
   auto obstacle=obstacleIndex.find(ea?a:b);auto body=owner.bodyLookup.find(ea?b:a);if(obstacle==obstacleIndex.end()||body==owner.bodyLookup.end())continue;
   Candidate c{obstacle->second,true,0.f,0,{}};auto bodyObject=ea?b:a;
   if(pair.m_algorithm){manifolds.resizeNoInitialize(0);pair.m_algorithm->getAllContactManifolds(manifolds);c.touching=manifolds.size()==0;
    for(int m=0;m<manifolds.size();m++){auto manifold=manifolds[m];c.threshold=btMax(c.threshold,manifold->getContactBreakingThreshold());
     for(int k=0;k<manifold->getNumContacts()&&c.normalCount<8;k++){auto n=manifold->getContactPoint(k).m_normalWorldOnB;if(manifold->getBody1()==bodyObject)n=-n;c.normals[c.normalCount++]=n;c.touching=true;}}}
   candidates[body->second].push_back(c);
  }
 }
 // Clamp a correction against the obstacles this body can reach. Touching
 // pairs use their contact planes; a pair without points is swept only once
 // the motion exceeds its contact threshold; beyond the broadphase padding
 // every obstacle is swept, as before.
 void project(int body,const btConvexShape& shape,const btTransform& pose,btVector3& movement,unsigned& contacts){
  float length=movement.length();if(length<=0)return;
  if(body<0||size_t(body)>=candidates.size()){movement=contactSafeTranslation(shape,pose,movement,projectionObstacles,contacts);return;}
  scratch.clear();bool full=length>PairPadding;
  for(const auto& c:candidates[body]){
   if(c.normalCount){
    for(unsigned sweep=0;sweep<3;sweep++)for(int k=0;k<c.normalCount;k++){float into=movement.dot(c.normals[k]);if(into<0){movement-=c.normals[k]*into;++contacts;}}
   }else if(c.touching||length>=c.threshold)scratch.push_back(projectionObstacles[c.obstacle]);
  }
  if(movement.length2()<1e-12f)return;
  if(full)movement=contactSafeTranslation(shape,pose,movement,projectionObstacles,contacts);
  else if(!scratch.empty())movement=contactSafeTranslation(shape,pose,movement,scratch,contacts);
 }
 // Spring-bone joints against mirrored Source geometry. A triangle mesh has no
 // inside, so a sphere below the floor would be pushed further down. The
 // joint's nearest Source-driven bone lies inside a ragdoll hull and therefore
 // outside the world: the line from it to the tail must not cross a surface,
 // then the tail sphere is lifted off a surface it touches. Both slide the
 // tail along the surface at the joint length (slideTail).
 btSphereShape sphere{1.f};btCollisionObject probe;
 bool constrainTail(const btVector3& outside,const btVector3& head,btVector3& tail,float radius,float length){
  if(objects.empty())return false;bool moved=false;
  struct Ray:btCollisionWorld::ClosestRayResultCallback {
   using ClosestRayResultCallback::ClosestRayResultCallback;
   bool needsCollision(btBroadphaseProxy* proxy)const override{return static_cast<const btCollisionObject*>(proxy->m_clientObject)->getUserIndex2()==ExternalCollisionTag;}
  } ray(outside,tail);
  if((tail-outside).length2()>1e-12f){world->rayTest(outside,tail,ray);if(ray.hasHit())moved|=slideTail(outside,head,tail,ray.m_hitPointWorld,ray.m_hitNormalWorld,radius,length);}
  struct Query:btCollisionWorld::ContactResultCallback {
   const btCollisionObject* self;btVector3 point{0,0,0},normal{0,0,0};float depth=0;
   explicit Query(const btCollisionObject* s):self(s){}
   bool needsCollision(btBroadphaseProxy* proxy)const override{return static_cast<const btCollisionObject*>(proxy->m_clientObject)->getUserIndex2()==ExternalCollisionTag;}
   btScalar addSingleResult(btManifoldPoint& p,const btCollisionObjectWrapper* a,int,int,const btCollisionObjectWrapper*,int,int)override{
    float distance=p.getDistance();if(distance>=0||-distance<=depth)return 0;bool mine=a->getCollisionObject()==self;
    depth=-distance;point=mine?p.getPositionWorldOnB():p.getPositionWorldOnA();normal=mine?p.m_normalWorldOnB:-p.m_normalWorldOnB;return 0;}
  } query(&probe);
  sphere.setUnscaledRadius(radius);probe.setCollisionShape(&sphere);probe.setWorldTransform(btTransform(btQuaternion::getIdentity(),tail));
  world->contactTest(&probe,query);if(query.depth>0)moved|=slideTail(outside,head,tail,query.point,query.normal,radius,length);
  return moved;
 }
 explicit External(Secondary& o):world(nanoemWorld(o.world)),owner(o){if(!owner.filterInstalled)world->getPairCache()->setOverlapFilterCallback(owner.filter.get());}
 ~External(){forgetSceneInterest(reinterpret_cast<uintptr_t>(this));for(auto& [id,o]:objects)world->removeRigidBody(o.body.get());if(!owner.filterInstalled)world->getPairCache()->setOverlapFilterCallback(nullptr);}
 static std::shared_ptr<Shape> shape(const std::shared_ptr<const SceneGeometry>& geometry,float scale){
  static std::mutex mutex;static std::map<std::pair<const SceneGeometry*,float>,std::weak_ptr<Shape>> cache;
  std::lock_guard lock(mutex);auto key=std::make_pair(geometry.get(),scale);if(auto old=cache[key].lock())return old;
  for(auto it=cache.begin();it!=cache.end();)if(it->second.expired())it=cache.erase(it);else ++it;
  auto result=std::make_shared<Shape>();result->source=geometry;
  if(geometry->kind==SceneGeometry::Sphere)result->shape=std::make_unique<btSphereShape>(geometry->radius/scale);
  else if(geometry->kind==SceneGeometry::Triangles){result->mesh=std::make_unique<btTriangleMesh>();for(size_t i=0;i+2<geometry->vertices.size();i+=3)result->mesh->addTriangle(fromSource(geometry->vertices[i])/scale,fromSource(geometry->vertices[i+1])/scale,fromSource(geometry->vertices[i+2])/scale);result->shape=std::make_unique<btBvhTriangleMeshShape>(result->mesh.get(),true);}
  else {auto compound=std::make_unique<btCompoundShape>();size_t cursor=0;for(int count:geometry->hullCounts){auto hull=std::make_unique<btConvexHullShape>();for(int i=0;i<count;i++)hull->addPoint(fromSource(geometry->vertices.at(cursor++))/scale,false);hull->setMargin(.02f/scale);hull->recalcLocalAabb();compound->addChildShape(btTransform::getIdentity(),hull.get());result->children.push_back(std::move(hull));}result->shape=std::move(compound);}
  result->shape->setMargin(.02f/scale);cache[key]=result;return result;
 }
 // `lower`/`upper` are the character's Source-space bounds captured by the
 // caller, so an asynchronous tick never reads render-thread arrays.
 void sync(const Instance& instance,unsigned flags,btVector3 lower,btVector3 upper){
  auto start=std::chrono::steady_clock::now();auto frame=readScene(instance.owner);std::set<uint64_t> keep;
  auto c=basis();float scale=instance.sourceRig->scale;
  lower-=btVector3(48,48,48);upper+=btVector3(48,48,48);
  // The capture keeps what this box needs from the next frame on. It reads the
  // box a sync or two late, so a moving character's box leads by twice its
  // last displacement (a teleport's is clamped).
  btVector3 middle=(lower+upper)*.5f,lead(0,0,0);
  if(interestPlaced){lead=(middle-interestCenter).absolute()*2;lead.setMin(btVector3(512,512,512));}
  interestCenter=middle;interestPlaced=true;
  noteSceneInterest(reinterpret_cast<uintptr_t>(this),lower-lead,upper+lead);
  if(frame){sequence=frame->sequence;captureMs=frame->captureMs;
   for(auto& source:frame->objects){
    unsigned kind=source.actor==SceneObject::LivingPlayer?Collide::Players:source.actor==SceneObject::LivingNpc?Collide::Npcs:source.isStatic?Collide::World:Collide::Objects;
    if(source.owner==(instance.sceneOwner?instance.sceneOwner:instance.id)||!(flags&kind))continue;
    auto center=source.transform*((source.geometry->minimum+source.geometry->maximum)*.5f),extent=(source.geometry->maximum-source.geometry->minimum)*.5f;
    auto basisAbs=source.transform.getBasis().absolute();extent=basisAbs*extent+source.velocity.absolute()*.1f;
    if(center.x()+extent.x()<lower.x()||center.y()+extent.y()<lower.y()||center.z()+extent.z()<lower.z()||center.x()-extent.x()>upper.x()||center.y()-extent.y()>upper.y()||center.z()-extent.z()>upper.z())continue;
    keep.insert(source.id);auto it=objects.find(source.id);
    if(it!=objects.end()&&it->second.geometry->source!=source.geometry){world->removeRigidBody(it->second.body.get());objects.erase(it);it=objects.end();}
    auto transform=source.transform;transform.getOrigin()*=Inch;transform=instance.placement.inverse()*transform;
    btTransform pose(c.transpose()*transform.getBasis()*c,fromSource(transform.getOrigin())/instance.scale);
    if(it==objects.end()){
     Object o;o.geometry=shape(source.geometry,scale);o.motion=std::make_unique<btDefaultMotionState>(pose);btRigidBody::btRigidBodyConstructionInfo info(0,o.motion.get(),o.geometry->shape.get());o.body=std::make_unique<btRigidBody>(info);o.body->setCollisionFlags(o.body->getCollisionFlags()|btCollisionObject::CF_KINEMATIC_OBJECT);o.body->setActivationState(DISABLE_DEACTIVATION);o.body->setFriction(.5f);o.body->setUserIndex2(ExternalCollisionTag);o.body->setUserIndex(source.isStatic?1:2);world->addRigidBody(o.body.get(),1,-1);it=objects.emplace(source.id,std::move(o)).first;
    }
    auto& o=it->second;
    if((o.body->getWorldTransform().getOrigin()-pose.getOrigin()).length()*scale>128){o.body->setWorldTransform(pose);o.body->setInterpolationWorldTransform(pose);world->getPairCache()->cleanProxyFromPairs(o.body->getBroadphaseHandle(),world->getDispatcher());}
    o.motion->setWorldTransform(pose);
   }
  }
  for(auto it=objects.begin();it!=objects.end();)if(!keep.contains(it->first)){world->removeRigidBody(it->second.body.get());it=objects.erase(it);}else ++it;
  syncMs=elapsedMs(start);
 }
 void measure(){contacts=worldContacts=objectContacts=0;auto d=world->getDispatcher();for(int i=0;i<d->getNumManifolds();i++){auto m=d->getManifoldByIndexInternal(i);const auto* o=m->getBody0()->getUserIndex2()==ExternalCollisionTag?m->getBody0():m->getBody1()->getUserIndex2()==ExternalCollisionTag?m->getBody1():nullptr;if(o)for(int j=0;j<m->getNumContacts();j++)if(m->getContactPoint(j).getDistance()<=0){++contacts;if(o->getUserIndex()==1)++worldContacts;else ++objectContacts;}}}
 // A triangle mesh has no solid interior. A squeezed body can cross its
 // surface and Bullet then resolves contacts on the *underside*, trapping a
 // jointed strand there. Keep the swept centre on the side it came from.
 // This runs after integration so it also catches split-impulse corrections,
 // which Bullet's velocity-only CCD cannot protect against.
 void guardSurfaces(){
  auto started=std::chrono::steady_clock::now();
  if(objects.empty())return;
  surfaces.clear();for(auto& [id,o]:objects)if(o.geometry->shape->isConcave()){
   Surface s;s.object=o.body.get();s.object->getCollisionShape()->getAabb(s.object->getWorldTransform(),s.minimum,s.maximum);surfaces.push_back(s);
  }
  if(surfaces.empty())return;
  stretched.assign(owner.bodies.size(),0);
  // Recover a strand that started on the wrong side (e.g. a reset while the
  // ragdoll was compressed). Only a violated authored linear limit permits
  // this extra query; ordinary cloth draped around a corner is left alone.
  for(auto& j:owner.joints)if(j.active){
   auto ra=j.a<0?nullptr:nanoemRigidBody(owner.bodies[j.a].value),rb=j.b<0?nullptr:nanoemRigidBody(owner.bodies[j.b].value);
   if((!ra||!ra->isActive())&&(!rb||!rb->isActive()))continue;
   auto a=ra?ra->getWorldTransform()*j.frameA:j.frameA;
   auto b=rb?rb->getWorldTransform()*j.frameB:j.frameB;
   auto delta=a.getBasis().transpose()*(b.getOrigin()-a.getOrigin());bool broken=false;
   for(int k=0;k<3;k++)if(j.lower[k]<=j.upper[k]&&(delta[k]<j.lower[k]-.5f||delta[k]>j.upper[k]+.5f))broken=true;
   if(broken){if(j.a>=0)stretched[j.a]=1;if(j.b>=0)stretched[j.b]=1;}
  }
  auto trace=[&](const btVector3& from,const btVector3& to,btCollisionWorld::ClosestRayResultCallback& ray){
   btVector3 lower=from,upper=from;lower.setMin(to);upper.setMax(to);
   btTransform start(btQuaternion::getIdentity(),from),end(btQuaternion::getIdentity(),to);
   // Query the external mesh BVHs directly. Traversing the dynamics broadphase
   // for each strand would repeatedly visit hundreds of unrelated MMD bodies.
   for(auto& s:surfaces){
    if(lower.x()>s.maximum.x()||lower.y()>s.maximum.y()||lower.z()>s.maximum.z()||upper.x()<s.minimum.x()||upper.y()<s.minimum.y()||upper.z()<s.minimum.z())continue;
    btCollisionWorld::rayTestSingle(start,end,s.object,s.object->getCollisionShape(),s.object->getWorldTransform(),ray);
   }
  };
  for(size_t i=0;i<owner.bodies.size();i++){auto& b=owner.bodies[i];if(b.follower)continue;
   auto rigid=nanoemRigidBody(b.value);auto pose=rigid->getWorldTransform();
   auto from=b.current.getOrigin(),to=pose.getOrigin();
   bool recovery=stretched[i]&&b.anchor>=0;
   if(recovery)from=nanoemRigidBody(owner.bodies[owner.anchors[b.anchor]].value)->getWorldTransform().getOrigin();
   if((to-from).length2()<1e-12f)continue;
   btCollisionWorld::ClosestRayResultCallback ray(from,to);trace(from,to,ray);
   if(!ray.hasHit()&&recovery){recovery=false;from=b.current.getOrigin();ray=btCollisionWorld::ClosestRayResultCallback(from,to);if((to-from).length2()>=1e-12f)trace(from,to,ray);}
   if(!ray.hasHit())continue;
   auto normal=ray.m_hitNormalWorld.normalized();
   auto shape=static_cast<const btConvexShape*>(rigid->getCollisionShape());
   auto support=pose.getBasis()*shape->localGetSupportingVertex(pose.getBasis().transpose()*-normal);
   float clearance=btMax(.001f,-normal.dot(support));
   pose.getOrigin()+=normal*(clearance+.002f-normal.dot(to-ray.m_hitPointWorld));
   auto velocity=rigid->getLinearVelocity();float inward=velocity.dot(normal);
   if(inward<0)rigid->setLinearVelocity(velocity-normal*inward);
   rigid->setCenterOfMassTransform(pose);rigid->setInterpolationWorldTransform(pose);
   rigid->getMotionState()->setWorldTransform(pose);rigid->activate(true);
   ++owner.surfaceCorrections;
   if(recovery){++owner.surfaceRecoveries;b.current=pose;} // discard the invalid below-surface interpolation endpoint
  }
  owner.surfaceGuardMs+=elapsedMs(started);
 }
};
unsigned collisionFlagsForLevel(int level){
 if(level<0||level>2)throw std::runtime_error("Invalid secondary collision mode");
 return level==0?Collide::Character:level==1?Collide::Character|Collide::World:Collide::Character|Collide::World|Collide::Objects;
}
void Secondary::applyCollisionFlags(unsigned flags){
 bool character=flags&Collide::Character;
 if(filter->characterContacts!=character){
  filter->characterContacts=character;auto w=nanoemWorld(world);
  // The filter stays installed while it excludes the body. Otherwise reference
  // worlds keep Bullet's own pair order unless an external scene needs it.
  if(!character&&!filterInstalled){if(!external)w->getPairCache()->setOverlapFilterCallback(filter.get());filterInstalled=true;}
  else if(character&&filterInstalled&&!v2){if(!external)w->getPairCache()->setOverlapFilterCallback(nullptr);filterInstalled=false;}
  // Pairs are filtered when they form: rebuild the model's proxies so pairs
  // that already exist follow the new rule.
  for(auto& b:bodies)w->refreshBroadphaseProxy(nanoemRigidBody(b.value));
 }
 if(!(flags&Collide::Scene))external.reset();else if(!external)external=std::make_unique<External>(*this);
 effectiveCollisionFlags=flags;
}
void Secondary::setCollisionFlags(unsigned flags){
 if(flags>Collide::All)throw std::runtime_error("Invalid secondary collision flags");collisionFlags=flags;
 // The simulation owns `external` and the pair filter. A running job applies
 // the newest request before its next tick; an idle world applies it now and
 // drops a request a finished job left behind, which would otherwise override
 // this newer one.
 if(async){std::lock_guard lock(mutex);if(running){pendingCollisionFlags=flags;collisionPending=true;return;}collisionPending=false;applyCollisionFlags(flags);return;}
 applyCollisionFlags(flags);
}
void Secondary::setAsyncWaitBudget(double ms){if(!std::isfinite(ms)||ms<0||ms>100)throw std::runtime_error("Wait budget must be 0 through 100 ms");waitBudgetMs=ms;}
double Secondary::asyncWaitBudget(){return waitBudgetMs.load();}
void Secondary::setAsyncTestStepDelay(double ms){if(!std::isfinite(ms)||ms<0||ms>100)throw std::runtime_error("Test step delay must be 0 through 100 ms");testStepDelayMs=ms;}
double Secondary::presentationDelayMs()const{return async&&presentationMode>0?(TickSeconds+presentMargin)*1000:0;}
void Secondary::setSleepPolicy(bool enabled,float linear,float angular,float seconds,float wakeDrift){
 if(!std::isfinite(linear)||!std::isfinite(angular)||!std::isfinite(seconds)||!std::isfinite(wakeDrift)||linear<0||angular<0||seconds<.1f||linear>100||angular>100||seconds>60||wakeDrift<0||wakeDrift>10)throw std::runtime_error("Invalid sleep policy");
 sleepEnabled=enabled;sleepLinear=linear;sleepAngular=angular;sleepSeconds=seconds;sleepWakeDrift=wakeDrift;++sleepVersion;
}
Json Secondary::sleepPolicy(){return {{"enabled",sleepEnabled.load()},{"linear",sleepLinear.load()},{"angular",sleepAngular.load()},{"seconds",sleepSeconds.load()}};}
void Secondary::setTuning(int iterations,float gravity,float damping,bool stretch,float tolerance){
 if(iterations<-1||iterations>100||!std::isfinite(gravity)||gravity<0||gravity>4||!std::isfinite(damping)||damping<0||damping>4||!std::isfinite(tolerance)||tolerance<.25f||tolerance>4)throw std::runtime_error("Invalid physics tuning values");
 std::lock_guard lock(tuningMutex);physicsTuning={iterations,gravity,damping,stretch,tolerance};++tuningVersion;
}
int Secondary::accuracy(){std::lock_guard lock(tuningMutex);return physicsTuning.iterations;}
Json Secondary::tuning(){std::lock_guard lock(tuningMutex);auto& t=physicsTuning;return {{"iterations",t.iterations},{"gravity",t.gravity},{"damping",t.damping},{"stretch",t.stretch},{"tolerance",t.tolerance},{"tickRate",60}};}
void Secondary::applyTuning(){
 auto version=tuningVersion.load();if(version==tuningVersionApplied)return;Tuning t;{std::lock_guard lock(tuningMutex);t=physicsTuning;version=tuningVersion.load();}
 tuningVersionApplied=version;stretchEnabled=t.stretch;stretchToleranceScale=t.tolerance;tuningGravity=t.gravity;tuningDamping=t.damping;
 baseIterations=std::max(1,t.iterations);nanoemWorld(world)->getSolverInfo().m_numIterations=std::min(baseIterations,std::max(2,(baseIterations+activeDivisor-1)/activeDivisor));
 auto gravity=authoredGravity*t.gravity;float g[4]={gravity.x(),gravity.y(),gravity.z(),0};nanoemPhysicsWorldSetGravity(world,g);
 for(size_t i=0;i<bodies.size();i++){auto source=instance.model->bodies[i];auto rigid=nanoemRigidBody(bodies[i].value);
  float linear=btClamped(nanoemModelRigidBodyGetLinearDamping(source)*t.damping,0.f,1.f);
  rigid->setDamping(linear,btClamped(nanoemModelRigidBodyGetAngularDamping(source)*t.damping,0.f,1.f));
  if(!bodies[i].follower)rigid->activate(true);
 }

}
Secondary::Secondary(Instance& owner):instance(owner){
 static const std::set<std::string> known={"reference","cpu_mt","gpu_opencl","cpu_mt_v2","gpu_vulkan"};
 if(!known.contains(owner.secondaryBackend))throw std::runtime_error("Unknown secondary backend: "+owner.secondaryBackend);
 effectiveBackend=owner.secondaryBackend;
 if(effectiveBackend!="reference"&&!owner.model->softBodies.empty()){effectiveBackend="reference";backendFallback="This model has PMX soft bodies; all physics is preserved on the nanoem CPU backend.";}
 if(effectiveBackend=="gpu_opencl"){auto capability=openclCapabilities();if(!capability["available"].get<bool>()){effectiveBackend="reference";backendFallback=capability["error"].get<std::string>();}}
 // gpu_vulkan is a v2 world (asynchronous ticks, sleep, SAP) whose constraint
 // iterations run on Vulkan; without a usable device it is plain cpu_mt_v2.
 if(effectiveBackend=="gpu_vulkan"){auto capability=vulkanCapabilities();if(!capability["available"].get<bool>()){effectiveBackend="cpu_mt_v2";backendFallback=capability["error"].get<std::string>();}}
 v2=effectiveBackend=="cpu_mt_v2"||effectiveBackend=="gpu_vulkan";async=v2&&owner.presentationDriven;
 // v2 worlds already give up the reference pair order (sleep, pair filter),
 // so they may take the cheaper broadphase and a fatter DBVT margin.
 auto broadphase=resolveBroadphase(owner.secondaryBroadphase,v2);
 BroadphaseConfig config;config.sap=isSapBroadphase(broadphase);config.leafDbvt=broadphase=="dbvt-fast";if(v2)config.margin=secondaryDbvtMargin();
 double extent=std::max(4000.,65536./owner.sourceRig->scale);
 for(int k=0;k<3;k++)extent=std::max({extent,double(btFabs(owner.model->minimum[k]))*2+1024,double(btFabs(owner.model->maximum[k]))*2+1024});
 config.minimum=btVector3(-extent,-extent,-extent);config.maximum=-config.minimum;
 config.capacity=unsigned(std::min<size_t>(0xfffffffe,std::max<size_t>(1024,owner.model->bodies.size()*2+256)));
 nanoem_status_t status=NANOEM_STATUS_SUCCESS;world=nanoemCreateSecondaryWorld(config,&status,effectiveBackend);if(!world||status!=NANOEM_STATUS_SUCCESS)throw std::runtime_error("nanoem physics world creation failed");
 authoredGravity=vec(nanoemPhysicsWorldGetGravity(world));
 filter=std::make_unique<Filter>();filter->pruneKinematic=v2;
 if(v2){nanoemWorld(world)->getPairCache()->setOverlapFilterCallback(filter.get());filterInstalled=true;}
 try{
 nanoemPhysicsWorldSetGroundEnabled(world,false);nanoemPhysicsWorldSetPreferredFPS(world,60);nanoemPhysicsWorldSetActive(world,true);drivers.resize(owner.model->bones.size(),-1);
 bodies.reserve(owner.model->bodies.size());
 for(auto source:owner.model->bodies){Rigid b;b.bone=boneIndex(nanoemModelRigidBodyGetBoneObject(source));b.mode=nanoemModelRigidBodyGetTransformType(source);b.follower=b.mode==0||(b.bone>=0&&owner.sourceControl[b.bone]>=0);b.value=nanoemPhysicsRigidBodyCreate(source,nullptr,&status);if(!b.value||status!=NANOEM_STATUS_SUCCESS)throw std::runtime_error("nanoem rigid body creation failed");
  float m[16];nanoemPhysicsMotionStateGetInitialWorldTransform(nanoemPhysicsRigidBodyGetMotionState(b.value),m);b.initial.setFromOpenGLMatrix(m);b.current=b.initial;
  if(b.follower)nanoemPhysicsRigidBodySetKinematic(b.value,true);nanoemPhysicsWorldAddRigidBody(world,b.value);
  if(b.bone>=0&&!b.follower)drivers[b.bone]=int(bodies.size());bodies.push_back(b);
 }
 for(size_t i=0;i<bodies.size();i++)bodyLookup[nanoemRigidBody(bodies[i].value)]=int(i);
 joints.reserve(owner.model->joints.size());for(size_t ji=0;ji<owner.model->joints.size();ji++){auto source=owner.model->joints[ji];auto ref=owner.model->jointReferences[ji];if(!ref.valid)continue;int a=ref.a,b=ref.b;
  JointInput input{world,a<0?nullptr:bodies[a].value,b<0?nullptr:bodies[b].value};btTransform::getIdentity().getOpenGLMatrix(input.transformA);btTransform::getIdentity().getOpenGLMatrix(input.transformB);
  Joint j;j.sourceIndex=int(ji);j.a=a;j.b=b;auto type=nanoemModelJointGetType(source);j.stretchGuard=type==NANOEM_MODEL_JOINT_TYPE_GENERIC_6DOF_SPRING_CONSTRAINT||type==NANOEM_MODEL_JOINT_TYPE_GENERIC_6DOF_CONSTRAINT;auto angles=vec(nanoemModelJointGetOrientation(source));auto rotation=glm::eulerAngleYXZ(angles.y(),angles.x(),angles.z());btTransform joint;joint.setFromOpenGLMatrix(glm::value_ptr(rotation));joint.setOrigin(vec(nanoemModelJointGetOrigin(source)));j.frameA=a<0?joint:bodies[a].initial.inverse()*joint;j.frameB=b<0?joint:bodies[b].initial.inverse()*joint;j.lower=vec(nanoemModelJointGetLinearLowerLimit(source));j.upper=vec(nanoemModelJointGetLinearUpperLimit(source));j.value=nanoemPhysicsJointCreate(source,&input,&status);if(!j.value||status!=NANOEM_STATUS_SUCCESS)throw std::runtime_error("nanoem joint creation failed");j.active=!((a<0||bodies[a].follower)&&(b<0||bodies[b].follower));if(j.active)nanoemPhysicsWorldAddJoint(world,j.value);joints.push_back(j);
 }
 for(auto& j:joints)if(j.a<0&&j.b>=0)bodies[j.b].worldAnchored=true;else if(j.b<0&&j.a>=0)bodies[j.a].worldAnchored=true;
 bool changed=true;while(changed){changed=false;for(auto& j:joints)if(j.a>=0&&j.b>=0&&bodies[j.a].worldAnchored!=bodies[j.b].worldAnchored){bodies[j.a].worldAnchored=bodies[j.b].worldAnchored=true;changed=true;}}
 buildAttachments();
 for(auto& j:joints){float length=j.a>=0&&j.b>=0?(bodies[j.a].initial.getOrigin()-bodies[j.b].initial.getOrigin()).length():0;j.stretchTolerance=btClamped(length*.08f,.02f,.12f);}
 // Bullet does not wake the dynamic neighbours of a moving kinematic body
 // through joints, so each follower remembers the chains it drives.
 if(v2)for(auto& j:joints)if(j.active&&j.a>=0&&j.b>=0&&bodies[j.a].follower!=bodies[j.b].follower){int f=bodies[j.a].follower?j.a:j.b,d=bodies[j.a].follower?j.b:j.a;bodies[f].linked.push_back(d);}
 for(auto source:owner.model->softBodies){auto value=nanoemPhysicsSoftBodyCreate(source,world,&status);if(!value||status!=NANOEM_STATUS_SUCCESS)throw std::runtime_error("nanoem soft body creation failed");nanoemPhysicsWorldAddSoftBody(world,value);soft.push_back(value);
  nanoem_rsize_t count=0;auto pins=nanoemModelSoftBodyGetAllPinnedVertexIndices(source,&count);softPins.emplace_back();for(size_t k=0;k<count;k++)softPins.back().push_back(int(pins[k]));
  // PMX pins name model vertices, not soft-body nodes (the node order follows the material's indices).
  softFrames.emplace_back();for(int i=0;i<nanoemPhysicsSoftBodyGetNumVertexObjects(value);i++){int vi=vertexIndex(nanoemPhysicsSoftBodyGetVertexObject(value,i));softFrames.back().push_back({vi,vi>=0&&std::find(softPins.back().begin(),softPins.back().end(),vi)!=softPins.back().end()});}
 }
 display.assign(bodies.size(),btTransform::getIdentity());
 displayCompensation.assign(bodies.size(),btVector3(0,0,0));
 if(owner.model->springs){
  std::vector<uint8_t> controlled(owner.model->bones.size(),0);
  for(size_t i=0;i<controlled.size();i++)controlled[i]=owner.sourceControl[i]>=0;
  springs=std::make_unique<SpringSystem>(*owner.model,owner.model->springs,controlled,owner.sourceRig->bones[0].mmd);
  springDisplay.assign(owner.model->springs->joints.size(),btQuaternion::getIdentity());springOnly=bodies.empty()&&soft.empty();
 }
 // The Source sample is the END of this tick. Bullet normally installs that
 // target before solving and also derives its velocity, advancing attached
 // dynamics one tick ahead (4 Source units at a carry speed of 240 units/s).
 // Solve from the previous follower pose with start-to-end velocities, then
 // publish both followers and dynamics at the same tick boundary.
 nanoemWorld(world)->setInternalTickCallback([](btDynamicsWorld* w,btScalar dt){static_cast<Secondary*>(w->getWorldUserInfo())->beginFollowers(dt);},this,true);
 nanoemWorld(world)->setInternalTickCallback([](btDynamicsWorld* w,btScalar){static_cast<Secondary*>(w->getWorldUserInfo())->endFollowers();},this,false);
 applyTuning();if(v2)applySleepPolicy();
 reset();
 if(async){tickSourcePose=instance.sourcePose;tickPose.local=instance.local;tickPose.global=instance.global;tickPose.skin=instance.skin;tickPose.effective=instance.effectiveScratch;tickImpulseWeights=instance.lastImpulseWeights;publishSolved(0,lastMs,0,instance.sourceTimestamp);}
 }catch(...){clear();throw;}
}
Secondary::~Secondary(){
 if(async){std::unique_lock lock(mutex);stopping=true;idle.wait(lock,[&]{return !running;});}
 clear();
}
btSoftRigidDynamicsWorld* Secondary::dynamics()const{return nanoemWorld(world);}
void Secondary::setPairCounting(bool enabled){nanoemSetPairCounting(world,enabled);}
#ifndef BT_NO_PROFILE
bool Secondary::profilerReset(bool enabled){
 btSetCustomEnterProfileZoneFunc(enabled?CProfileManager::Start_Profile:+[](const char*){});btSetCustomLeaveProfileZoneFunc(enabled?CProfileManager::Stop_Profile:+[](){});
 if(enabled)CProfileManager::Reset();return enabled;
}
void Secondary::profilerFrame(){CProfileManager::Increment_Frame_Counter();}
void Secondary::profilerDump(){CProfileManager::dumpAll();fflush(stdout);}
Json Secondary::profilerSnapshot(){
 Json out=Json::array();auto iterator=CProfileManager::Get_Iterator();
 std::function<void(const std::string&)> walk=[&](const std::string& prefix){
  struct Child {std::string name;double ms;int calls;};std::vector<Child> children;
  for(iterator->First();!iterator->Is_Done();iterator->Next())children.push_back({iterator->Get_Current_Name(),double(iterator->Get_Current_Total_Time()),iterator->Get_Current_Total_Calls()});
  for(size_t i=0;i<children.size();i++){auto path=prefix+"/"+children[i].name;out.push_back({{"path",path},{"ms",children[i].ms},{"calls",children[i].calls}});iterator->Enter_Child(int(i));walk(path);iterator->Enter_Parent();}
 };
 walk("");CProfileManager::Release_Iterator(iterator);return out;
}
#else
bool Secondary::profilerReset(bool){return false;}
void Secondary::profilerFrame(){}
void Secondary::profilerDump(){}
Json Secondary::profilerSnapshot(){return Json::array();}
#endif
void Secondary::clear(){if(!world)return;external.reset();if(filterInstalled){nanoemWorld(world)->getPairCache()->setOverlapFilterCallback(nullptr);filterInstalled=false;}for(auto s:soft){nanoemPhysicsWorldRemoveSoftBody(world,s);nanoemPhysicsSoftBodyDestroy(s);}for(auto& j:joints){if(j.active)nanoemPhysicsWorldRemoveJoint(world,j.value);nanoemPhysicsJointDestroy(j.value);}for(auto& b:bodies){nanoemPhysicsWorldRemoveRigidBody(world,b.value);nanoemPhysicsRigidBodyDestroy(b.value);}nanoemPhysicsWorldDestroy(world);world=nullptr;}
void Secondary::applySleepPolicy(){
 if(!v2)return;bool enabled=sleepEnabled.load();
 if(enabled)nanoemPhysicsWorldSetDeactivationTimeThreshold(world,sleepSeconds.load());
 unsigned version=sleepVersion.load();if(version==sleepVersionApplied)return;sleepVersionApplied=version;
 for(auto& b:bodies){auto rigid=nanoemRigidBody(b.value);
  if(b.follower){
   // nanoem marks kinematic followers DISABLE_DEACTIVATION, and Bullet wakes
   // every body touching an active kinematic object each step, so nothing
   // could ever rest. Resting followers sleep too; follow() wakes a follower
   // (and its jointed chain) as soon as its target moves.
   rigid->setSleepingThresholds(enabled?.05f:0.f,enabled?.05f:0.f);
   if(!enabled)rigid->forceActivationState(DISABLE_DEACTIVATION);else if(rigid->getActivationState()==DISABLE_DEACTIVATION)rigid->forceActivationState(ACTIVE_TAG);
  }else{rigid->setSleepingThresholds(enabled?sleepLinear.load():0.f,enabled?sleepAngular.load():0.f);if(!enabled)rigid->activate(true);}
 }
}
void Secondary::follow(bool all,const std::vector<btTransform>& skin){
 bool wake=v2&&sleepEnabled.load();
 for(auto& b:bodies)if(all||b.follower){auto t=b.bone>=0?skin[b.bone]*b.initial:b.initial;b.followStart=all?t:nanoemRigidBody(b.value)->getWorldTransform();if(all)set(b.value,t);else {float m[16];t.getOpenGLMatrix(m);nanoemPhysicsMotionStateSetCurrentWorldTransform(nanoemPhysicsRigidBodyGetMotionState(b.value),m);}b.current=t;nanoemPhysicsRigidBodyResetStates(b.value);
  if(wake&&b.follower){
   // Accumulated drift since the follower last woke, so slow motion cannot
   // leave sleeping hair behind or a sleeping collider frozen in place.
   // 0.02 PMX units is far below visible detachment.
   float drift=sleepWakeDrift.load();float threshold=drift*drift;auto& r=b.wakeReference;
   bool moved=all||(r.getOrigin()-t.getOrigin()).length2()>threshold||(r.getBasis().getColumn(0)-t.getBasis().getColumn(0)).length2()>threshold||(r.getBasis().getColumn(2)-t.getBasis().getColumn(2)).length2()>threshold;
   if(moved){r=t;nanoemRigidBody(b.value)->activate(true);for(int k:b.linked)nanoemRigidBody(bodies[k].value)->activate(true);}
  }
 }
}
void Secondary::beginFollowers(btScalar seconds){
 // Runs after Bullet saveKinematicState, before collision detection/solving.
 // Explicit velocity calculation also handles followers that were sleeping.
 for(auto& b:bodies)if(b.follower){auto rigid=nanoemRigidBody(b.value);btVector3 linear,angular;
  btTransformUtil::calculateVelocity(b.followStart,b.current,seconds,linear,angular);
  rigid->setCenterOfMassTransform(b.followStart);rigid->setInterpolationWorldTransform(b.followStart);
  rigid->setLinearVelocity(linear);rigid->setAngularVelocity(angular);
  rigid->setInterpolationLinearVelocity(linear);rigid->setInterpolationAngularVelocity(angular);
 }
}
void Secondary::endFollowers(){
 // No dynamic-body snapping or extra presentation delay: integrate the
 // prescribed followers to the same endpoint as the completed dynamic step.
 for(auto& b:bodies)if(b.follower){auto rigid=nanoemRigidBody(b.value);
  rigid->setCenterOfMassTransform(b.current);rigid->setInterpolationWorldTransform(b.current);
 }
}
void Secondary::stabilizeJoints(){
 if(!stretchEnabled)return;auto started=std::chrono::steady_clock::now();
 if(external)external->prepareProjection();
 // Bounded positional correction for violated authored translation limits.
 // Free axes and angular limits/springs remain untouched. Source followers
 // and world anchors are read-only regardless of their PMX inverse mass.
 // Alternating sweeps avoid simply pushing the error into the next link.
 // Other PMX joint types reuse the limit fields for different purposes.
 // With world/prop collisions the contact solver already bounds the chains and
 // a crushed cluster violates most joints; two alternating sweeps keep the pass
 // proportional to what a crowded scene can afford.
 const int passes=external?2:4;
 for(int pass=0;pass<passes;pass++)for(size_t index=0;index<joints.size();index++){auto& j=joints[pass%2?joints.size()-1-index:index];if(!j.active||!j.stretchGuard)continue;
  auto ra=j.a<0?nullptr:nanoemRigidBody(bodies[j.a].value),rb=j.b<0?nullptr:nanoemRigidBody(bodies[j.b].value);
  if((!ra||!ra->isActive())&&(!rb||!rb->isActive()))continue;
  auto a=ra?ra->getWorldTransform()*j.frameA:j.frameA,b=rb?rb->getWorldTransform()*j.frameB:j.frameB;
  auto delta=a.getBasis().transpose()*(b.getOrigin()-a.getOrigin()),error=btVector3(0,0,0);float tolerance=j.stretchTolerance*stretchToleranceScale;
  for(int k=0;k<3;k++)if(j.lower[k]<=j.upper[k])error[k]=delta[k]-btClamped(delta[k],j.lower[k]-tolerance,j.upper[k]+tolerance);
  if(error.length2()<1e-10f)continue;
  // This is a geometric safety projection, not another impulse solve. Equal
  // mobility avoids endlessly pushing all error into a light link next to a
  // heavy accessory. Bullet still uses every authored mass for real dynamics.
  float wa=ra&&!bodies[j.a].follower&&ra->getInvMass()>0?1.f:0.f,wb=rb&&!bodies[j.b].follower&&rb->getInvMass()>0?1.f:0.f;if(wa+wb<=0)continue;
  auto correction=a.getBasis()*error,normal=correction.normalized();float total=wa+wb;
  auto va=ra?ra->getVelocityInLocalPoint(a.getOrigin()-ra->getCenterOfMassPosition()):btVector3(0,0,0);
  auto vb=rb?rb->getVelocityInLocalPoint(b.getOrigin()-rb->getCenterOfMassPosition()):btVector3(0,0,0);
  float separating=btMax(0.f,(vb-va).dot(normal));
  auto correct=[&](btRigidBody* rigid,int body,float weight,float sign){if(weight<=0)return;auto pose=rigid->getWorldTransform();
   auto movement=correction*(sign*weight/total);
   if(external){
    unsigned contacts=0;external->project(body,*static_cast<const btConvexShape*>(rigid->getCollisionShape()),pose,movement,contacts);
    stretchContactClamps+=contacts;
   }
   if(movement.length2()<1e-12f)return;
   pose.getOrigin()+=movement;
   rigid->setCenterOfMassTransform(pose);rigid->setInterpolationWorldTransform(pose);rigid->getMotionState()->setWorldTransform(pose);
   // Remove only the velocity separating the violated joint. Converting the
   // positional correction to velocity would inject energy into the chain.
   // A blocked normal correction must not add velocity into that same surface.
   // No broadphase refresh here: nothing reads body AABBs before the next
   // step's updateAabbs, and one SAP/DBVT update per correction dominated the pass.
   rigid->setLinearVelocity(rigid->getLinearVelocity()+movement*(separating/correction.length()));rigid->activate(true);
  };
  correct(ra,j.a,wa,1);correct(rb,j.b,wb,-1);++stretchCorrections;
 }
 stretchGuardMs+=elapsedMs(started);
}
void Secondary::followSoft(bool all,const std::vector<btTransform>& skin){for(size_t si=0;si<soft.size();si++){auto s=soft[si];for(size_t i=0;i<softFrames[si].size();i++)if(all||softFrames[si][i].pinned){int vi=softFrames[si][i].index;if(vi<0)continue;auto& vertex=instance.model->vertices[vi];auto p=skinPosition(vertex,skin),normal=skinNormal(vertex,skin);float position[4]={p.x(),p.y(),p.z(),0},direction[4]={normal.x(),normal.y(),normal.z(),0};nanoemPhysicsSoftBodySetVertexPosition(s,int(i),position);nanoemPhysicsSoftBodySetVertexNormal(s,int(i),direction);}}}
void Secondary::buildAttachments(){
 // Multi-source shortest paths stop at other followers. An unrelated
 // kinematic collision partner is not an attachment; only authored joints
 // participate. World-anchored and free components stay in simulation space.
 std::vector<std::vector<std::pair<int,float>>> edges(bodies.size());
 for(auto& j:joints)if(j.active&&j.a>=0&&j.b>=0){float distance=btMax(.001f,(bodies[j.a].initial.getOrigin()-bodies[j.b].initial.getOrigin()).length());edges[j.a].push_back({j.b,distance});edges[j.b].push_back({j.a,distance});}
 using Entry=std::pair<float,int>;std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
 std::vector<float> distance(bodies.size(),BT_LARGE_FLOAT);
 for(size_t i=0;i<bodies.size();i++)if(bodies[i].follower&&!bodies[i].worldAnchored&&bodies[i].bone>=0){bodies[i].anchor=int(anchors.size());anchors.push_back(int(i));distance[i]=0;queue.push({0,int(i)});}
 while(!queue.empty()){auto [cost,i]=queue.top();queue.pop();if(cost!=distance[i])continue;
  for(auto [next,length]:edges[i])if(!bodies[next].follower&&!bodies[next].worldAnchored&&cost+length<distance[next]){distance[next]=cost+length;bodies[next].anchor=bodies[i].anchor;queue.push({distance[next],next});}
 }
 currentAnchors.resize(anchors.size(),btTransform::getIdentity());previousAnchors=currentAnchors;
 anchorLive.resize(anchors.size());anchorPreviousInverse.resize(anchors.size());anchorCurrentInverse.resize(anchors.size());
}
void Secondary::readSolved(bool initial,const btTransform& root,const std::vector<btTransform>& skin){
 previousAnchors=currentAnchors;for(size_t i=0;i<anchors.size();i++){auto& b=bodies[anchors[i]];currentAnchors[i]=skin[b.bone]*b.initial;}if(initial)previousAnchors=currentAnchors;
 // Read Bullet's transform directly; the OpenGL matrix round trip copied the
 // same floats through a 16-float buffer for every body every tick.
 for(auto& b:bodies){b.previous=b.current;const auto& t=nanoemRigidBody(b.value)->getWorldTransform();const auto& o=t.getOrigin();const auto& r=t.getBasis();
  for(int k=0;k<3;k++)if(!std::isfinite(o[k])||!std::isfinite(r[k].x())||!std::isfinite(r[k].y())||!std::isfinite(r[k].z()))throw std::runtime_error("Non-finite nanoem transform");
  b.current=t;if(initial)b.previous=b.current;}
 for(size_t s=0;s<soft.size();s++)for(size_t i=0;i<softFrames[s].size();i++){auto& v=softFrames[s][i];v.previous=v.current;v.previousNormal=v.normal;float p[4],n[4];nanoemPhysicsSoftBodyGetVertexPosition(soft[s],int(i),p);nanoemPhysicsSoftBodyGetVertexNormal(soft[s],int(i),n);for(int k=0;k<3;k++)if(!std::isfinite(p[k])||!std::isfinite(n[k]))throw std::runtime_error("Non-finite nanoem soft body vertex");v.current=vec(p);v.normal=vec(n);if(initial){v.previous=v.current;v.previousNormal=v.normal;}}
 previousRoot=currentRoot;currentRoot=root;if(initial)previousRoot=currentRoot;
}
void Secondary::present(){
 interpolation=std::clamp(accumulator*60.,0.,1.);
 auto& live=anchorLive;auto& a=anchorPreviousInverse;auto& b=anchorCurrentInverse;
 for(size_t i=0;i<anchors.size();i++){auto& body=bodies[anchors[i]];live[i]=instance.skin[body.bone]*body.initial;a[i]=previousAnchors[i].inverse();b[i]=currentAnchors[i].inverse();}
 for(size_t i=0;i<bodies.size();i++){auto& body=bodies[i];
  if(body.follower)display[i]=body.bone>=0?instance.skin[body.bone]*body.initial:body.initial;
  else if(!instance.presentationDriven||!interpolate)display[i]=body.current;
  else if(body.anchor<0)display[i]=blendPose(body.previous,body.current,float(interpolation));
  else {int k=body.anchor;display[i]=live[k]*blendPose(a[k]*body.previous,b[k]*body.current,float(interpolation));}
  displayCompensation[i]=!body.follower&&body.anchor>=0&&interpolate&&instance.presentationDriven?display[i].getOrigin()-body.previous.getOrigin().lerp(body.current.getOrigin(),float(interpolation)):btVector3(0,0,0);
 }
 if(springs)presentSprings(springs->previous(),springs->current(),instance.presentationDriven&&interpolate);
 blendQualityPresentation();
}
void Secondary::reset(){
 int root=instance.sourceRig->bones[0].mmd;
 instance.evaluate(false);follow(true,instance.skin);nanoemPhysicsWorldReset(world);followSoft(true,instance.skin);accumulator=inputTime=simulationTime=0;inputs.clear();inputs.push_back({0,instance.sourcePose});readSolved(true,root>=0?instance.sourcePose[root]:btTransform::getIdentity(),instance.skin);if(springs)springs->reset(instance.skin);present();++resets;instance.lastImpulseWeights=instance.expandedMorphs();instance.poseDirty=true;
}
void Secondary::applyImpulses(const std::vector<float>& weights,std::vector<float>& last){
 if(last.size()!=weights.size())last.assign(weights.size(),0.f);
 for(size_t i=0;i<weights.size();i++){float delta=weights[i]-last[i];if(delta==0||nanoemModelMorphGetType(instance.model->morphs[i])!=NANOEM_MODEL_MORPH_TYPE_IMPULUSE)continue;nanoem_rsize_t n=0;auto entries=nanoemModelMorphGetAllImpulseMorphObjects(instance.model->morphs[i],&n);for(size_t k=0;k<n;k++){auto e=entries[k];int bi=bodyIndex(nanoemModelMorphImpulseGetRigidBodyObject(e));if(bi<0||bodies[bi].follower)continue;auto linear=vec(nanoemModelMorphImpulseGetVelocity(e))*delta,angular=vec(nanoemModelMorphImpulseGetTorque(e))*delta;if(nanoemModelMorphImpulseIsLocal(e)){linear=bodies[bi].current.getBasis()*linear;angular=bodies[bi].current.getBasis()*angular;}float l[4]={linear.x(),linear.y(),linear.z(),0},a[4]={angular.x(),angular.y(),angular.z(),0};nanoemPhysicsRigidBodyApplyVelocityImpulse(bodies[bi].value,l);nanoemPhysicsRigidBodyApplyTorqueImpulse(bodies[bi].value,a);if(v2)nanoemRigidBody(bodies[bi].value)->activate(true);}}
 last=weights;
}
unsigned Secondary::advance(std::vector<btTransform>& sourcePose,const std::function<const std::vector<btTransform>&()>& evaluateSkin){
 applyTuning();stretchGuardMs=0;
 constexpr double h=TickSeconds;unsigned steps=0;auto started=std::chrono::steady_clock::now();int root=instance.sourceRig->bones[0].mmd;
 while(accumulator+1e-8>=h&&steps<MaxStepsPerInput){
  // An asynchronous job yields after a bounded wall time; the debt persists.
  if(async&&steps>0&&elapsedMs(started)>MaxAsyncJobMs)break;
  double time=simulationTime+h;
  while(inputs.size()>2&&inputs[1].time<time-1e-9)inputs.pop_front();
  const auto& a=inputs.front();const auto& b=inputs.size()>1?inputs[1]:a;
  float blend=b.time>a.time?float(std::clamp((time-a.time)/(b.time-a.time),0.,1.)):1.f;
  for(size_t i=0;i<sourcePose.size();i++)if(instance.sourceControl[i]>=0)sourcePose[i]=blendPose(a.pose[i],b.pose[i],blend);
  const auto& skin=evaluateSkin();follow(false,skin);followSoft(false,skin);
  auto tickStart=std::chrono::steady_clock::now();nanoemStepFixed(world,float(h));physicsMs+=elapsedMs(tickStart);
  // Recover tunneled/buried strands before projection can erase the violated
  // joint used to identify them. Projection then sweeps/slides against contacts.
  if(external)external->guardSurfaces();
  stabilizeJoints();
  if(springs)stepSprings(skin);
  readSolved(false,root>=0?sourcePose[root]:btTransform::getIdentity(),skin);accumulator=std::max(0.,accumulator-h);simulationTime=time;++steps;++ticks;
 }
 // Preserve short catch-up debt; an exceptional stall has an explicit, measured
 // discontinuity instead of allowing unbounded work to freeze the game.
 if(accumulator>.25){double excess=accumulator-std::fmod(accumulator,h);dropped+=excess;accumulator-=excess;simulationTime+=excess;for(auto& b:bodies)b.previous=b.current;previousRoot=currentRoot;previousAnchors=currentAnchors;if(springs)springs->hold();}
 return steps;
}
void Secondary::step(double seconds){
 auto start=std::chrono::steady_clock::now();double cpuStart=threadCpuMs();if(!std::isfinite(seconds)||seconds<0)throw std::runtime_error("Invalid secondary simulation delta");
 activeDivisor=requestedDivisor;applyTuning();nanoemWorld(world)->getSolverInfo().m_numIterations=std::min(baseIterations,std::max(2,(baseIterations+activeDivisor-1)/activeDivisor));
 if(qualitySuspended){physicsMs=sceneMs=lastMs=0;lastSteps=0;instance.evaluate(false);present();instance.evaluate(true);return;}
 if(qualityResume){instance.evaluate(false);resumeQualityPose(instance.skin,instance.sourcePose);qualityResume=false;}
 physicsMs=sceneMs=surfaceGuardMs=0;lastSteps=0;double initialEvaluate=instance.evaluateMs;
 inputTime+=seconds;
 if(!inputs.empty()&&seconds==0)inputs.back().pose=instance.sourcePose;else inputs.push_back({inputTime,instance.sourcePose});
 auto latest=instance.sourcePose;
 accumulator+=seconds;
 if(external){btVector3 lower(1e9f,1e9f,1e9f),upper=-lower;for(auto& bone:instance.presentationBones){lower.setMin(bone.getOrigin());upper.setMax(bone.getOrigin());}if(instance.snapshot){lower.setMin(instance.snapshot->minimum);upper.setMax(instance.snapshot->maximum);}external->sync(instance,effectiveCollisionFlags,lower,upper);sceneMs=external->syncMs;}
 applyImpulses(instance.expandedMorphs(),instance.lastImpulseWeights);
 applySleepPolicy();
 lastSteps=advance(instance.sourcePose,[&]()->const std::vector<btTransform>&{instance.evaluate(false);return instance.skin;});
 instance.sourcePose=std::move(latest);
 instance.evaluate(false);present();instance.evaluate(true);
 poseMs=instance.evaluateMs-initialEvaluate;
 if(external)external->measure();
 instance.poseDirty=true;lastMs=elapsedMs(start);physicsTotalMs+=physicsMs;guardTotalMs+=surfaceGuardMs+stretchGuardMs;tickTotalMs+=lastMs;tickCpuTotalMs+=threadCpuMs()-cpuStart;
}
// ---- asynchronous scheduling (cpu_mt_v2) ----
void Secondary::submitAsync(Input&& input){
 if(qualitySuspended){if(input.reset){suspendedReset=true;suspendedResetReason=input.resetReason;}return;}
 if(suspendedReset){input.reset=true;input.resetReason=suspendedResetReason;suspendedReset=false;}
 input.qualityDivisor=requestedDivisor;input.resumeQuality=qualityResume;qualityResume=false;
 std::unique_lock lock(mutex);if(stopping)return;
 // Bound the backlog of a stalled world. The dropped sample's time still counts
 // toward the next input, so the simulation clock does not lose it.
 if(queue.size()>=8){auto dropped=std::move(queue.front());queue.pop_front();queue.front().elapsed+=dropped.elapsed;if(dropped.reset){queue.front().reset=true;queue.front().resetReason=dropped.resetReason;}}
 queue.push_back(std::move(input));
 bool start=!running;if(start)running=true;
 lock.unlock();
 if(start)enqueueBackground([this]{runAsync();});
}
void Secondary::runAsync(){
 InlinePhysicsScope inlineScope;
 for(;;){
  Input input;bool apply=false;unsigned flags=0;
  {std::lock_guard lock(mutex);if(stopping||queue.empty()){running=false;idle.notify_all();return;}input=std::move(queue.front());queue.pop_front();apply=collisionPending;flags=pendingCollisionFlags;collisionPending=false;}
  try{if(apply)applyCollisionFlags(flags);tickAsync(input);}
  catch(const std::exception& e){std::lock_guard lock(mutex);asyncError=e.what();running=false;idle.notify_all();return;}
  catch(...){std::lock_guard lock(mutex);asyncError="Unknown asynchronous physics failure";running=false;idle.notify_all();return;}
 }
}
void Secondary::resetTick(const Input& input){
 int root=instance.sourceRig->bones[0].mmd;
 tickSourcePose=input.pose;evaluatePose(*instance.model,*input.manual,input.morphs,&instance.sourceControl,&tickSourcePose,nullptr,tickPose.local,tickPose.global,tickPose.skin,tickPose.effective,instance.sourceRig->bones[0].mmd);
 follow(true,tickPose.skin);nanoemPhysicsWorldReset(world);followSoft(true,tickPose.skin);accumulator=inputTime=simulationTime=0;inputs.clear();inputs.push_back({0,input.pose});
 readSolved(true,root>=0?tickSourcePose[root]:btTransform::getIdentity(),tickPose.skin);if(springs)springs->reset(tickPose.skin);++resets;resetReason=input.resetReason;tickImpulseWeights=input.morphs;
}
void Secondary::tickAsync(const Input& input){
 auto start=std::chrono::steady_clock::now();double cpuStart=threadCpuMs();physicsMs=sceneMs=surfaceGuardMs=0;double evaluateMs=0;
 if(!input.manual)throw std::runtime_error("Asynchronous input without a manual pose");
 if(input.reset)resetTick(input);
 activeDivisor=input.qualityDivisor;applyTuning();nanoemWorld(world)->getSolverInfo().m_numIterations=std::min(baseIterations,std::max(2,(baseIterations+activeDivisor-1)/activeDivisor));
 if(input.resumeQuality&&!input.reset){
  tickSourcePose=input.pose;evaluatePose(*instance.model,*input.manual,input.morphs,&instance.sourceControl,&tickSourcePose,nullptr,tickPose.local,tickPose.global,tickPose.skin,tickPose.effective,instance.sourceRig->bones[0].mmd);
  resumeQualityPose(tickPose.skin,tickSourcePose);
 }
 inputTime+=input.elapsed;
 if(!inputs.empty()&&input.elapsed==0)inputs.back().pose=input.pose;else inputs.push_back({inputTime,input.pose});
 accumulator+=input.elapsed;
 if(external){external->sync(instance,effectiveCollisionFlags,input.boundsMinimum,input.boundsMaximum);sceneMs=external->syncMs;}
 applyImpulses(input.morphs,tickImpulseWeights);
 applySleepPolicy();
 unsigned steps=advance(tickSourcePose,[&]()->const std::vector<btTransform>&{auto started=std::chrono::steady_clock::now();evaluatePose(*instance.model,*input.manual,input.morphs,&instance.sourceControl,&tickSourcePose,nullptr,tickPose.local,tickPose.global,tickPose.skin,tickPose.effective,instance.sourceRig->bones[0].mmd);evaluateMs+=elapsedMs(started);return tickPose.skin;});
 if(steps&&testStepDelayMs.load()>0){auto until=std::chrono::steady_clock::now()+std::chrono::duration<double,std::milli>(testStepDelayMs.load());while(std::chrono::steady_clock::now()<until)std::this_thread::yield();} // busy, like a longer solve; a sleep would add timer granularity
 if(external)external->measure();
 physicsTotalMs+=physicsMs;guardTotalMs+=surfaceGuardMs+stretchGuardMs;tickTotalMs+=elapsedMs(start);tickCpuTotalMs+=threadCpuMs()-cpuStart;
 publishSolved(steps,elapsedMs(start),evaluateMs,input.time);
}
void Secondary::publishSolved(unsigned steps,double totalMs,double evaluateMs,double submittedTime){
 auto solved=std::make_shared<Solved>();size_t n=bodies.size();solved->current.resize(n);solved->previous.resize(n);unsigned sleeping=0;
 solved->motion.assign(n,{0.f,0.f,0.f});
 for(size_t i=0;i<n;i++){solved->current[i]=bodies[i].current;solved->previous[i]=bodies[i].previous;if(!bodies[i].follower){auto rigid=nanoemRigidBody(bodies[i].value);solved->motion[i]={rigid->getLinearVelocity().length(),rigid->getAngularVelocity().length(),float(rigid->getActivationState())};if(v2&&rigid->getActivationState()==ISLAND_SLEEPING)++sleeping;}}
 solved->currentRoot=currentRoot;solved->previousRoot=previousRoot;solved->simulationTime=simulationTime;solved->inputTime=inputTime;solved->accumulator=accumulator;solved->dropped=dropped;
 solved->currentAnchors=currentAnchors;solved->previousAnchors=previousAnchors;solved->surfaceCorrections=surfaceCorrections;solved->surfaceRecoveries=surfaceRecoveries;solved->surfaceGuardMs=surfaceGuardMs;
 solved->stretchContactClamps=stretchContactClamps;solved->stretchCorrections=stretchCorrections;solved->stretchGuardMs=stretchGuardMs;solved->iterations=nanoemWorld(world)->getSolverInfo().m_numIterations;
 solved->physicsTotalMs=physicsTotalMs;solved->tickTotalMs=tickTotalMs;solved->guardTotalMs=guardTotalMs;solved->tickCpuTotalMs=tickCpuTotalMs;
 solved->physicsMs=physicsMs;solved->sceneMs=sceneMs;solved->poseMs=evaluateMs;solved->lastMs=totalMs;solved->submittedTime=submittedTime;solved->ticks=ticks;solved->resets=resets;solved->steps=steps;solved->sleeping=sleeping;solved->resetReason=resetReason;
 if(external){solved->contacts=external->contacts;solved->worldContacts=external->worldContacts;solved->objectContacts=external->objectContacts;solved->mirrors=external->objects.size();solved->captureMs=external->captureMs;solved->syncMs=external->syncMs;solved->sceneSequence=external->sequence;}
 solved->broadphase=nanoemBroadphaseInfo(world);
 if(springs){solved->springPrevious=springs->previous();solved->springCurrent=springs->current();solved->springColliderHits=springs->colliderHits;solved->springWorldHits=springs->worldHits;solved->springSteps=springs->steps;solved->springMs=springs->stepMs;}
 std::lock_guard lock(mutex);published=std::move(solved);
}
void Secondary::presentAsync(){
 std::shared_ptr<const Solved> solved;
 {std::unique_lock lock(mutex);
  double budget=waitBudgetMs.load();if(budget>0&&running)idle.wait_for(lock,std::chrono::duration<double,std::milli>(budget),[&]{return !running;});
  if(!asyncError.empty())throw std::runtime_error(asyncError);
  solved=published;}
 if(!solved){
  // A local player can start with physics suspended in first person. There is
  // no solved tick yet: present its current animated skeleton, not the body's
  // construction pose at world origin (also used by shadow/reflection passes).
  for(size_t i=0;i<bodies.size();i++){const auto& b=bodies[i];display[i]=b.bone>=0?instance.skin[b.bone]*b.initial:b.initial;displayCompensation[i].setZero();}
  std::fill(springDisplay.begin(),springDisplay.end(),btQuaternion::getIdentity());
  presentHistory.clear();presentClock=-1;
  return;
 }
 // The tick job owns the clock and timing members; the frame keeps its own view of the presented tick.
 presented=solved;sleepingBodies=solved->sleeping;
 // The construction state carries no submitted input (submittedTime -1).
 double lag=qualitySuspended||solved->submittedTime<0?0:std::max(0.,instance.sourceTimestamp-solved->submittedTime);
 lagMs=lag*1000;
 // Distinct simulation states of the current reset epoch, newest last. A job
 // without a step republishes the same pair with newer clocks.
 if(!presentHistory.empty()&&(presentHistory.back()->resets!=solved->resets||solved->simulationTime<presentHistory.back()->simulationTime-1e-9))presentHistory.clear();
 if(presentHistory.empty()||solved->simulationTime>presentHistory.back()->simulationTime+1e-9){presentHistory.push_back(solved);if(presentHistory.size()>PresentHistoryDepth)presentHistory.pop_front();}
 else presentHistory.back()=solved;
 // Render-side input clock: the worker's clock at its last processed input plus
 // the game time since that input was submitted. Interpolating by the worker's
 // own accumulator froze the display while a step job outlasted a frame and
 // then jumped when it published (a 60 Hz judder of hair against the body).
 double now=solved->inputTime+lag;
 double dt=presentClock<0?0:std::clamp(now-presentClock,0.,.25);presentClock=now;
 // The display runs one tick (the interpolation interval) plus a margin behind
 // that clock, so a late step still finds a bracketing pair. The margin follows
 // the largest lag of the last 32 frames (a hitch frame raises it only until it
 // leaves the window; a step job that outlasts frames recurs every tick), floors
 // at half a tick, is capped at two ticks, and moves gradually so the display
 // itself never jumps.
 presentLag[presentLagIndex++%presentLag.size()]=float(std::min(lag,TickSeconds*2));
 float top=0;for(float sample:presentLag)top=std::max(top,sample);
 double target=std::clamp(double(top),TickSeconds*.5,TickSeconds*2);
 if(dt<=0&&presentHistory.size()<=1)presentMargin=target;else presentMargin+=std::clamp(target-presentMargin,-.05*dt,.5*dt);
 double displayTime=now-TickSeconds-presentMargin;
 const Solved* use=presentHistory.front().get();
 for(auto it=presentHistory.rbegin();it!=presentHistory.rend();++it)if((*it)->simulationTime-TickSeconds<=displayTime+1e-9){use=it->get();break;}
 interpolation=std::clamp((displayTime-(use->simulationTime-TickSeconds))/TickSeconds,0.,1.);
 auto& live=anchorLive;auto& a=anchorPreviousInverse;auto& b=anchorCurrentInverse;
 for(size_t i=0;i<anchors.size();i++){auto& body=bodies[anchors[i]];live[i]=instance.skin[body.bone]*body.initial;a[i]=use->previousAnchors[i].inverse();b[i]=use->currentAnchors[i].inverse();}
 for(size_t i=0;i<bodies.size();i++){auto& body=bodies[i];
  if(body.follower)display[i]=body.bone>=0?instance.skin[body.bone]*body.initial:body.initial;
  else if(!interpolate)display[i]=solved->current[i];
  else if(body.anchor<0)display[i]=blendPose(use->previous[i],use->current[i],float(interpolation));
  else {int k=body.anchor;display[i]=live[k]*blendPose(a[k]*use->previous[i],b[k]*use->current[i],float(interpolation));}
  displayCompensation[i]=!body.follower&&body.anchor>=0&&interpolate?display[i].getOrigin()-use->previous[i].getOrigin().lerp(use->current[i].getOrigin(),float(interpolation)):btVector3(0,0,0);
 }
 if(springs){const Solved& from=interpolate?*use:*solved;if(from.springCurrent.size()==springDisplay.size())presentSprings(from.springPrevious,from.springCurrent,interpolate);}
 blendQualityPresentation();
}
void Secondary::waitAsyncIdle(){if(!async)return;std::unique_lock lock(mutex);idle.wait(lock,[&]{return !running;});}
void Secondary::stepSprings(const std::vector<btTransform>& skin){
 // Joints also meet mirrored map/prop geometry in the collision modes that mirror it,
 // with at least 2 cm of radius: VRM hit radii are sized for hair against a head.
 SpringSystem::WorldContact contact=[this](const btVector3& outside,const btVector3& head,btVector3& tail,float radius,float length){return external->constrainTail(outside,head,tail,std::max(radius,.02f*springs->setup().unitsPerMeter),length);};
 springs->bodyContacts=effectiveCollisionFlags&Collide::Character;
 springs->step(float(TickSeconds),skin,tuningGravity,tuningDamping,external&&!external->objects.empty()?&contact:nullptr);
}
void Secondary::presentSprings(const std::vector<btQuaternion>& previous,const std::vector<btQuaternion>& current,bool blend){
 float t=float(std::clamp(interpolation,0.,1.));
 for(size_t k=0;k<springDisplay.size()&&k<current.size();k++)springDisplay[k]=blend&&k<previous.size()?previous[k].slerp(current[k],t).normalized():current[k];
}
Json Secondary::springInfo(uint64_t colliderHits,uint64_t worldHits,double stepMs,uint64_t steps)const{
 auto& s=springs->setup();return {{"joints",s.joints.size()},{"springs",s.springs.size()},{"colliders",s.colliders.size()},{"steps",steps},{"colliderHits",colliderHits},{"worldHits",worldHits},{"stepMs",stepMs},{"relativeDamping",springRelativeDamping()}};
}
bool Secondary::drives(size_t bone)const{return drivers[bone]>=0||(springs&&springs->setup().jointOfBone[bone]>=0);}
btTransform Secondary::feedback(size_t bone,const btTransform& animated,const btTransform* parentGlobal) const {
 if(springs&&presentationMode>=0){int joint=springs->setup().jointOfBone[bone];if(joint>=0){auto parent=parentGlobal?parentGlobal->getRotation():btQuaternion::getIdentity();return btTransform((parent*springDisplay[joint]).normalized(),animated.getOrigin());}}
 int driver=drivers[bone];if(driver<0||presentationMode<0)return animated;if(presentationMode==0)return jiggleFeedback(bone,animated);auto& b=bodies[driver];auto pose=display[driver]*b.initial.inverse()*btTransform(btQuaternion::getIdentity(),instance.model->bones[bone].position);if(b.mode==NANOEM_MODEL_RIGID_BODY_TRANSFORM_TYPE_FROM_BONE_ORIENTATION_AND_SIMULATION_TO_BONE)pose.getOrigin()-=instance.local[bone].getOrigin();return pose;}
bool Secondary::deformsSoft()const{return presentationMode>0&&!softFrames.empty();}
void Secondary::deformSoft(Snapshot& snapshot)const{
 if(presentationMode<=0)return;
 int root=instance.sourceRig->bones[0].mmd;auto live=root>=0?instance.sourcePose[root]:btTransform::getIdentity();auto a=previousRoot.inverse(),b=currentRoot.inverse();
 for(auto& vertices:softFrames)for(auto& v:vertices){if(v.index<0)continue;auto p=v.current,n=v.normal;
  if(instance.presentationDriven&&interpolate){p=live*((a*v.previous).lerp(b*v.current,float(interpolation)));n=live.getBasis()*(a.getBasis()*v.previousNormal).lerp(b.getBasis()*v.normal,float(interpolation));}
  if(v.pinned&&instance.presentationDriven){p=skinPosition(instance.model->vertices[v.index],instance.skin);n=skinNormal(instance.model->vertices[v.index],instance.skin);}
  auto pos=instance.placement*(toSource(p)*instance.scale)/Inch;auto normal=instance.placement.getBasis()*toSource(n);if(normal.length2()>1e-12f)normal.normalize();
  auto& out=snapshot.vertices[v.index];out.x=pos.x();out.y=pos.y();out.z=pos.z();out.nx=normal.x();out.ny=normal.y();out.nz=normal.z();snapshot.minimum.setMin(pos);snapshot.maximum.setMax(pos);
 }
}
Json Secondary::diagnostics(bool detailed)const{
 if(presentationMode<=0){
  std::vector<btTransform> current;current.reserve(bodies.size());for(auto& b:bodies)current.push_back(b.bone>=0?instance.skin[b.bone]*b.initial:b.initial);
  auto out=diagnosticsFrom(detailed,current,Json::object(),frameStats());
  unsigned linked=0;for(const auto& j:jiggle)linked+=j.valveParent>=0;
  auto travel=jiggleTravel.length2()>1e-12f?instance.placement.getBasis()*toSource(jiggleTravel.normalized()):btVector3(0,0,0);
  out["jiggleDirectionalBones"]=linked;out["jiggleTravelDirection"]={travel.x(),travel.y(),travel.z()};
  if(detailed)for(size_t i=0;i<bodies.size();i++){int bone=bodies[i].bone;if(bone>=0&&size_t(bone)<jiggle.size()){out["bodyList"][i]["jiggleValveParent"]=jiggle[bone].valveParent;out["bodyList"][i]["jiggleDirectionScale"]=jiggle[bone].directionScale;}}
  out.update({{"simulationMode",presentationMode<0?"disabled":"jiggle"},{"jiggleMs",qualitySuspended?0:simplifiedMs},{"sourceMirrors",0},{"externalContacts",0},{"worldContacts",0},{"objectContacts",0},{"sceneCaptureMs",0},{"sceneSyncMs",0},{"asynchronous",false}});return out;
 }
 if(async){
  // The tick job publishes and writes `asyncError` under this mutex; hold it
  // while the report reads them (diagnostics are an occasional HUD/harness call).
  std::lock_guard lock(mutex);auto solved=published;
  if(!solved)return diagnosticsFrom(detailed,{},Json::object(),frameStats());
  auto out=diagnosticsFrom(detailed,solved->current,solved->broadphase,qualitySuspended?frameStats():statsFrom(*solved),&solved->motion);
  out["surfaceCorrections"]=solved->surfaceCorrections;out["surfaceRecoveries"]=solved->surfaceRecoveries;out["surfaceGuardMs"]=solved->surfaceGuardMs;
  out["stretchContactClamps"]=solved->stretchContactClamps;out["stretchCorrections"]=solved->stretchCorrections;out["stretchGuardMs"]=solved->stretchGuardMs;
  if(springs)out["springBones"]=springInfo(solved->springColliderHits,solved->springWorldHits,solved->springMs,solved->springSteps);
  out.update({{"sourceMirrors",solved->mirrors},{"externalContacts",solved->contacts},{"worldContacts",solved->worldContacts},{"objectContacts",solved->objectContacts},{"sceneCaptureMs",solved->captureMs},{"sceneSyncMs",solved->syncMs},{"sceneSequence",solved->sceneSequence},{"sleepingBodies",solved->sleeping}});
  if(qualitySuspended)for(auto key:{"surfaceGuardMs","stretchGuardMs","sceneCaptureMs","sceneSyncMs"})out[key]=0;
  return out;}
 std::vector<btTransform> current;current.reserve(bodies.size());for(auto& b:bodies)current.push_back(b.current);
 auto out=diagnosticsFrom(detailed,current,nanoemBroadphaseInfo(world),frameStats());
 out["surfaceCorrections"]=surfaceCorrections;out["surfaceRecoveries"]=surfaceRecoveries;out["surfaceGuardMs"]=surfaceGuardMs;
 out["stretchContactClamps"]=stretchContactClamps;out["stretchCorrections"]=stretchCorrections;out["stretchGuardMs"]=stretchGuardMs;
 if(springs)out["springBones"]=springInfo(springs->colliderHits,springs->worldHits,springs->stepMs,springs->steps);
 out.update({{"sourceMirrors",external?external->objects.size():0},{"externalContacts",external?external->contacts:0},{"worldContacts",external?external->worldContacts:0},{"objectContacts",external?external->objectContacts:0},{"sceneCaptureMs",external?external->captureMs:0},{"sceneSyncMs",external?external->syncMs:0},{"sceneSequence",external?external->sequence:0}});
 if(qualitySuspended)for(auto key:{"surfaceGuardMs","stretchGuardMs","sceneCaptureMs","sceneSyncMs"})out[key]=0;
 return out;
}
Secondary::FrameStats Secondary::statsFrom(const Solved& s)const{FrameStats out;out.physicsMs=s.physicsMs;out.sceneMs=s.sceneMs;out.poseMs=s.poseMs;out.lastMs=s.lastMs;out.accumulator=s.accumulator;out.inputTime=s.inputTime;out.simulationTime=s.simulationTime;out.dropped=s.dropped;out.ticks=s.ticks;out.resets=s.resets;out.steps=s.steps;out.iterations=s.iterations;out.resetReason=s.resetReason;out.physicsTotalMs=s.physicsTotalMs;out.tickTotalMs=s.tickTotalMs;out.guardTotalMs=s.guardTotalMs;out.tickCpuTotalMs=s.tickCpuTotalMs;return out;}
Secondary::FrameStats Secondary::frameStats()const{
 FrameStats out;
 if(async)out=presented?statsFrom(*presented):FrameStats{};
 else {out.physicsMs=physicsMs;out.sceneMs=sceneMs;out.poseMs=poseMs;out.lastMs=lastMs;out.accumulator=accumulator;out.inputTime=inputTime;out.simulationTime=simulationTime;out.dropped=dropped;out.ticks=ticks;out.resets=resets;out.steps=lastSteps;out.iterations=nanoemWorld(world)->getSolverInfo().m_numIterations;out.resetReason=resetReason;out.physicsTotalMs=physicsTotalMs;out.tickTotalMs=tickTotalMs;out.guardTotalMs=guardTotalMs;out.tickCpuTotalMs=tickCpuTotalMs;}
 if(qualitySuspended){out.physicsMs=out.sceneMs=out.poseMs=out.lastMs=0;out.steps=0;out.iterations=0;out.accumulator=0;}
 if(presentationMode<=0){out.physicsMs=out.sceneMs=0;out.poseMs=out.lastMs=simplifiedMs;out.steps=0;out.iterations=presentationMode;out.accumulator=0;}
 return out;
}

Json Secondary::diagnosticsFrom(bool detailed,const std::vector<btTransform>& current,const Json& broadphase,const FrameStats& stats,const std::vector<std::array<float,3>>* motion)const{
 Json list=Json::array();int followers=0,active=0;double maxError=0,maxLimitError=0;bool haveCurrent=current.size()==bodies.size();
 for(size_t i=0;i<bodies.size();i++){
  auto& b=bodies[i];followers+=b.follower;if(!detailed||!haveCurrent)continue;auto p=current[i].getOrigin();auto q=current[i].getRotation();
  auto world=instance.placement*convert(current[i],instance.scale);auto wp=world.getOrigin()/Inch;float yaw,pitch,roll;world.getBasis().getEulerZYX(yaw,pitch,roll);
  auto source=instance.model->bodies[i];int shape=nanoemModelRigidBodyGetShapeType(source);auto size=vec(nanoemModelRigidBodyGetShapeSize(source));
  auto extent=shape==0?btVector3(size.x(),size.x(),size.x()):shape==2?btVector3(size.x(),size.y()*.5f+size.x(),size.x()):size;extent=toSource(extent).absolute()*instance.scale/Inch;
  list.push_back({{"bone",b.bone},{"mode",b.mode},{"follower",b.follower},{"position",{p.x(),p.y(),p.z()}},{"rotation",{q.x(),q.y(),q.z(),q.w()}},{"displayPosition",{display[i].getOrigin().x(),display[i].getOrigin().y(),display[i].getOrigin().z()}},{"worldPosition",{wp.x(),wp.y(),wp.z()}},{"worldAngles",{pitch*SIMD_DEGS_PER_RAD,yaw*SIMD_DEGS_PER_RAD,roll*SIMD_DEGS_PER_RAD}},{"extent",{extent.x(),extent.y(),extent.z()}},{"shape",shape}});
  list.back()["presentationAnchorBody"]=b.anchor<0?-1:anchors[b.anchor];
  list.back()["presentationAnchorBone"]=b.anchor<0?-1:bodies[anchors[b.anchor]].bone;
  list.back()["presentationCompensation"]={displayCompensation[i].x(),displayCompensation[i].y(),displayCompensation[i].z()};
  if(motion&&motion->size()==bodies.size()&&!b.follower){auto& m=(*motion)[i];list.back().update({{"speed",m[0]},{"spin",m[1]},{"activation",int(m[2])}});}
 }
 int worstJoint=-1;Json worstBodies=Json::array(),attachments=Json::array();
 for(size_t i=0;i<joints.size();i++){auto& j=joints[i];if(!j.active)continue;active++;if(!detailed||!haveCurrent)continue;
  auto a=j.a<0?j.frameA:current[j.a]*j.frameA,b=j.b<0?j.frameB:current[j.b]*j.frameB;
  auto d=a.getBasis().transpose()*(b.getOrigin()-a.getOrigin());maxError=std::max(maxError,double(d.length()));double error=0;
  for(int k=0;k<3;k++)if(j.lower[k]<=j.upper[k])error=std::max({error,double(j.lower[k]-d[k]),double(d[k]-j.upper[k])});
  if(error>maxLimitError){maxLimitError=error;worstJoint=int(i);worstBodies={j.a,j.b};}
  if(j.a>=0&&j.b>=0&&bodies[j.a].follower!=bodies[j.b].follower){
   // Compare each first dynamic joint to its own follower. Both display
   // endpoints belong to one presented frame; published worker snapshots
   // can be newer and must never be mixed with that display measurement.
   float sign=bodies[j.a].follower?1.f:-1.f;
   auto offset=instance.placement.getBasis()*toSource((b.getOrigin()-a.getOrigin())*sign)*instance.sourceRig->scale;
   auto da=display[j.a]*j.frameA,db=display[j.b]*j.frameB;
   auto shown=instance.placement.getBasis()*toSource((db.getOrigin()-da.getOrigin())*sign)*instance.sourceRig->scale;
   attachments.push_back({{"joint",i},{"follower",bodies[j.a].follower?j.a:j.b},{"body",bodies[j.a].follower?j.b:j.a},{"lockedLinear",j.lower==btVector3(0,0,0)&&j.upper==btVector3(0,0,0)},{"offsetSource",{offset.x(),offset.y(),offset.z()}},{"displayOffsetSource",{shown.x(),shown.y(),shown.z()}},{"limitErrorPmx",error}});
  }
 }
 Json out=broadphase.is_object()?broadphase:Json::object();
 out.update({{"physicsMs",stats.physicsMs},{"poseMs",stats.poseMs},{"ticks",stats.ticks},{"lastSteps",stats.steps},{"simulationTime",stats.simulationTime},{"inputTime",stats.inputTime},{"debtSeconds",stats.accumulator},{"interpolation",interpolation},{"resets",stats.resets},{"resetReason",stats.resetReason},{"backend","nanoem-30acffaa"},{"bodies",bodies.size()},{"joints",joints.size()},{"authoredJoints",instance.model->joints.size()},{"skippedJoints",instance.model->joints.size()-joints.size()},{"warnings",instance.model->warnings},{"activeJoints",active},{"followers",followers},{"softBodies",soft.size()},{"stepMs",stats.lastMs},{"physicsTotalMs",stats.physicsTotalMs},{"tickTotalMs",stats.tickTotalMs},{"guardTotalMs",stats.guardTotalMs},{"tickCpuTotalMs",stats.tickCpuTotalMs},{"droppedTime",stats.dropped},{"maxJointDistance",maxError},{"maxLinearLimitError",maxLimitError},{"ground",false},{"collisionFlags",collisionFlags},{"effectiveCollisionFlags",effectiveCollisionFlags.load()},{"feedbackApplied",0},{"asynchronous",async},{"lagMs",lagMs},{"presentationDelayMs",presentationDelayMs()},{"sleepingBodies",sleepingBodies},{"asyncError",asyncError},{"bodyList",list}});
 out["secondaryBackendRequested"]=instance.secondaryBackend;out["secondaryBackend"]=effectiveBackend;out["secondaryBackendFallback"]=backendFallback;out["broadphaseRequested"]=instance.secondaryBroadphase;out["solverIterations"]=stats.iterations;
 if(detailed){out["maxLinearLimitJoint"]=worstJoint;out["maxLinearLimitBodies"]=worstBodies;out["attachmentJoints"]=std::move(attachments);}
 if(out.contains("compute")&&!out["compute"].value("failure",std::string()).empty()){out["secondaryBackend"]="cpu_mt";out["secondaryBackendFallback"]=out["compute"]["failure"];}
 return out;
}
}
