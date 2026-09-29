#include "runtime.hpp"
#include "secondary.hpp"
#include "scene.hpp"
#include "jobs.hpp"
#include "compute_solver.hpp"
#include <BulletSoftBody/btSoftBodyRigidBodyCollisionConfiguration.h>
#include <BulletSoftBody/btSoftBodyHelpers.h>
#include <BulletDynamics/ConstraintSolver/btGeneric6DofSpringConstraint.h>
#include <algorithm>
#include <chrono>
#include <set>
#include <stdexcept>

namespace mmd {
namespace {
btVector3 jvec(const Json& j,btVector3 fallback=btVector3(0,0,0)){
    if(!j.is_array()||j.size()!=3)return fallback;btVector3 v(j[0].get<float>(),j[1].get<float>(),j[2].get<float>());
    for(int i=0;i<3;i++)if(!std::isfinite(v[i]))throw std::runtime_error("Non-finite vector");return v;
}
Json array(const btVector3& v){return {v.x(),v.y(),v.z()};}
btTransform pose(const float* p,const float* r){btMatrix3x3 rotation;rotation.setEulerZYX(r[0],r[1],r[2]);return btTransform(rotation,vec(p));}
struct PairFilter:btOverlapFilterCallback {
    bool needBroadphaseCollision(btBroadphaseProxy* a,btBroadphaseProxy* b) const override {
        auto x=static_cast<btCollisionObject*>(a->m_clientObject),y=static_cast<btCollisionObject*>(b->m_clientObject);
        int kx=x->getUserIndex(),ky=y->getUserIndex();
        if(kx==2||ky==2)return kx!=ky;
        if(x->getUserIndex2()==y->getUserIndex2()&&(kx==1)!=(ky==1))return false; // Mode-0 colliders service their own secondary rig.
        return (a->m_collisionFilterGroup&b->m_collisionFilterMask)&&(b->m_collisionFilterGroup&a->m_collisionFilterMask);
    }
};
struct Dynamics:btSoftRigidDynamicsWorld {
    World* owner;
    Dynamics(World* w,btDispatcher* d,btBroadphaseInterface* b,btConstraintSolver* s,btCollisionConfiguration* c):btSoftRigidDynamicsWorld(d,b,s,c),owner(w){}
    void solveConstraints(btContactSolverInfo& info) override {owner->captureBefore();btDiscreteDynamicsWorld::solveConstraints(info);owner->captureAfter();}
    void solveSoftBodiesConstraints(btScalar h) override {owner->captureBefore();btSoftRigidDynamicsWorld::solveSoftBodiesConstraints(h);owner->captureAfter();}
};
}
struct World::Impl {
    btSoftBodyRigidBodyCollisionConfiguration configuration;
    btCollisionDispatcher dispatcher{&configuration};
    btDbvtBroadphase broadphase;
    btSequentialImpulseConstraintSolver solver;
    PairFilter filter;
    Dynamics dynamics;
    std::unique_ptr<btPoint2PointConstraint> grab;
    uint64_t grabInstance=0;
    int physgunBody=-1;
    btTransform physgunTarget=btTransform::getIdentity(),physgunOffset=btTransform::getIdentity();
    explicit Impl(World* w):dynamics(w,&dispatcher,&broadphase,&solver,&configuration){
        broadphase.getOverlappingPairCache()->setOverlapFilterCallback(&filter);
        dynamics.getWorldInfo().m_sparsesdf.Initialize();dynamics.getSolverInfo().m_numIterations=20;
        dynamics.getSolverInfo().m_solverMode&=~SOLVER_RANDMIZE_ORDER;
    }
};
World::World():impl(std::make_unique<Impl>(this)){initializeBulletScheduler();}
World::~World(){clear();}
btSoftRigidDynamicsWorld& World::dynamics(){return impl->dynamics;}
static std::unique_ptr<World> singleton;
static thread_local World* boundWorld=nullptr;
WorldScope::WorldScope(World* value):previous(boundWorld){boundWorld=value;}
WorldScope::~WorldScope(){boundWorld=previous;}
World& world(){if(boundWorld)return *boundWorld;if(!singleton)singleton=std::make_unique<World>();return *singleton;}
void shutdownWorld(){singleton.reset();}
static unsigned runtimeRealms=0;
void acquireRuntimeRealm(){++runtimeRealms;}
void releaseRuntimeRealm(){if(runtimeRealms&&--runtimeRealms==0){shutdownWorld();publishScene({});shutdownCompute();shutdownJobs();}}
uint64_t World::create(std::shared_ptr<Model> m,const Json& options){auto id=next++;auto p=std::make_unique<Instance>(*this,std::move(m),id,options);instances.emplace(id,std::move(p));return id;}
Instance& World::get(uint64_t id){auto it=instances.find(id);if(it==instances.end())throw std::runtime_error("Expired instance handle");return *it->second;}
void World::remove(uint64_t id){if(impl->grabInstance==id)endGrab();instances.erase(id);}
void World::clear(){endGrab();instances.clear();for(auto& [id,m]:mirrors)dynamics().removeRigidBody(m->body.get());mirrors.clear();accumulator=time=0;}
void World::captureBefore(){for(auto& [id,m]:mirrors)if(m->body->getInvMass()>0){
    m->beforeLinear=m->body->getLinearVelocity();m->beforeAngular=m->body->getAngularVelocity();
    // Locked axes have zero inverse inertia. Inverting that singular tensor
    // manufactures NaNs, which must never be fed back into Source.
    auto inv=m->body->getInvInertiaDiagLocal();btVector3 inertia(0,0,0);for(int i=0;i<3;i++)if(inv[i]>0)inertia[i]=1/inv[i];
    const auto& r=m->body->getWorldTransform().getBasis();m->beforeInertia=r.scaled(inertia)*r.transpose();
}}
void World::captureAfter(){for(auto& [id,m]:mirrors)if(m->body->getInvMass()>0){m->impulse+=(m->body->getLinearVelocity()-m->beforeLinear)/m->body->getInvMass();m->torque+=m->beforeInertia*(m->body->getAngularVelocity()-m->beforeAngular);}}
void World::step(double seconds,const btVector3& gravity,bool publishSnapshots){
    if(!std::isfinite(seconds)||seconds<0)throw std::runtime_error("Invalid simulation delta");
    auto start=std::chrono::steady_clock::now();tick++;dynamics().setGravity(gravity);dynamics().getWorldInfo().m_gravity=gravity;
    accumulator+=seconds;constexpr double h=1.0/120;unsigned steps=0;
    while(accumulator+1e-10>=h&&steps<8){
        for(auto& [id,p]:instances)if(!p->frozen&&!p->sourceRig)p->beforeStep();drivePhysgun();dynamics().stepSimulation(float(h),0);time+=h;accumulator-=h;steps++;
        for(auto& [id,p]:instances)if(!p->frozen&&!p->sourceRig){p->evaluate(true);p->poseDirty=true;}
    }
    if(accumulator>=h){double excess=accumulator-std::fmod(accumulator,h);dropped+=excess;accumulator-=excess;}
    if(publishSnapshots)for(auto& [id,p]:instances)p->ensureSnapshot();
    lastStepMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
void World::setMirror(uint64_t id,const Json& j,std::span<const float> geometry){
    auto it=mirrors.find(id);
    if(it==mirrors.end()){
        auto m=std::make_unique<Mirror>();m->id=id;auto type=j.value("shape",std::string("convex"));
        if(type=="sphere")m->shape=std::make_unique<btSphereShape>(std::max(.001f,j.value("radius",1.f)*Inch));
        else if(type=="box")m->shape=std::make_unique<btBoxShape>(jvec(j.at("half"))*Inch);
        else if(type=="triangles"){
            if(geometry.size()%9||geometry.empty())throw std::runtime_error("Invalid collision triangles");m->triangles=std::make_unique<btTriangleMesh>();
            for(size_t i=0;i<geometry.size();i+=9)m->triangles->addTriangle(vec(&geometry[i])*Inch,vec(&geometry[i+3])*Inch,vec(&geometry[i+6])*Inch);
            m->shape=std::make_unique<btBvhTriangleMeshShape>(m->triangles.get(),true);
        } else if(type=="compound"){
            auto compound=std::make_unique<btCompoundShape>();size_t cursor=0;
            for(auto& count:j.at("hulls")){size_t n=count.get<size_t>();if(n<4||cursor+n*3>geometry.size())throw std::runtime_error("Invalid convex hull range");
                auto shape=std::make_unique<btConvexHullShape>();for(size_t i=0;i<n;i++)shape->addPoint(vec(geometry.data()+cursor+i*3)*Inch,false);cursor+=n*3;shape->recalcLocalAabb();shape->setMargin(.002f);compound->addChildShape(btTransform::getIdentity(),shape.get());m->children.push_back(std::move(shape));}
            if(cursor!=geometry.size())throw std::runtime_error("Trailing convex vertices");m->shape=std::move(compound);
        } else throw std::runtime_error("Unsupported mirror shape");
        float mass=j.value("mass",0.f);if(type=="triangles")mass=0;
        btVector3 inertia=j.contains("inertia")?jvec(j["inertia"]):btVector3(0,0,0);if(mass>0&&inertia.length2()<1e-12f)m->shape->calculateLocalInertia(mass,inertia);
        btRigidBody::btRigidBodyConstructionInfo ci(mass,nullptr,m->shape.get(),inertia);m->body=std::make_unique<btRigidBody>(ci);m->body->setUserIndex(2);m->body->setUserPointer(m.get());
        m->body->setFlags(BT_DISABLE_WORLD_GRAVITY);m->body->setGravity(btVector3(0,0,0));m->body->setDamping(0,0);m->body->setActivationState(DISABLE_DEACTIVATION);
        dynamics().addRigidBody(m->body.get());it=mirrors.emplace(id,std::move(m)).first;
    }
    auto& m=*it->second;
    float mass=m.triangles?0:std::max(0.f,j.value("mass",0.f));
    if((mass>0?1.f/mass:0)!=m.body->getInvMass()){
        dynamics().removeRigidBody(m.body.get());auto inertia=j.contains("inertia")?jvec(j["inertia"]):btVector3(0,0,0);if(mass>0&&inertia.length2()<1e-12f)m.shape->calculateLocalInertia(mass,inertia);
        m.body->setMassProps(mass,inertia);m.body->updateInertiaTensor();m.impulse.setZero();m.torque.setZero();dynamics().addRigidBody(m.body.get());
    }
    auto angles=jvec(j.value("angles",Json::array()));btQuaternion rotation;rotation.setEulerZYX(angles.y()*SIMD_RADS_PER_DEG,angles.x()*SIMD_RADS_PER_DEG,angles.z()*SIMD_RADS_PER_DEG);
    // Callers pass Source roll,pitch,yaw as intrinsic XYZ radians via quaternion when available.
    if(j.contains("rotation")){auto r=j["rotation"];rotation=btQuaternion(r[0],r[1],r[2],r[3]);rotation.normalize();}
    btTransform transform(rotation,jvec(j.value("position",Json::array()))*Inch);
    if((transform.getOrigin()-m.body->getWorldTransform().getOrigin()).length2()>4){m.impulse.setZero();m.torque.setZero();if(m.body->getBroadphaseHandle())impl->broadphase.getOverlappingPairCache()->cleanProxyFromPairs(m.body->getBroadphaseHandle(),&impl->dispatcher);}
    m.body->setWorldTransform(transform);m.body->setInterpolationWorldTransform(transform);m.body->setLinearVelocity(jvec(j.value("velocity",Json::array()))*Inch);
    m.body->setAngularVelocity(transform.getBasis()*jvec(j.value("angularVelocity",Json::array()))*SIMD_RADS_PER_DEG);m.body->updateInertiaTensor();dynamics().updateSingleAabb(m.body.get());m.touched=tick;
}
void World::removeMirror(uint64_t id){auto it=mirrors.find(id);if(it!=mirrors.end()){dynamics().removeRigidBody(it->second->body.get());mirrors.erase(it);}}
Json World::takeImpulses(){Json out=Json::array();for(auto& [id,m]:mirrors){if(m->impulse.length2()>1e-12f||m->torque.length2()>1e-12f)out.push_back({{"id",id},{"linear",array(m->impulse/Inch)},{"angular",array(m->torque*SIMD_DEGS_PER_RAD)}});m->impulse.setZero();m->torque.setZero();}return out;}
Json World::raycast(const btVector3& start,const btVector3& end,uint64_t instance,bool coreOnly){
    struct Pick:btCollisionWorld::ClosestRayResultCallback{uint64_t instance=0;bool coreOnly=false;using ClosestRayResultCallback::ClosestRayResultCallback;bool needsCollision(btBroadphaseProxy* proxy) const override{auto object=static_cast<btCollisionObject*>(proxy->m_clientObject);if(instance&&object->getUserIndex2()!=int(instance))return false;if(object->getUserIndex()==2)return !instance;if(object->getUserIndex()!=0&&object->getUserIndex()!=1)return false;auto body=static_cast<Body*>(object->getUserPointer());return body&&body->mode!=0&&body->mass>0&&(!coreOnly||body->core);}} ray(start,end);
    ray.instance=instance;ray.coreOnly=coreOnly;dynamics().rayTest(start,end,ray);if(!ray.hasHit()||ray.m_collisionObject->getUserIndex()==2)return nullptr;
    for(auto& [id,p]:instances)for(size_t i=0;i<p->bodies.size();i++)if(p->bodies[i]->rigid.get()==ray.m_collisionObject)return {{"instance",id},{"body",i},{"position",array(ray.m_hitPointWorld/Inch)},{"normal",array(ray.m_hitNormalWorld)},{"fraction",ray.m_closestHitFraction}};return nullptr;
}
void World::beginGrab(uint64_t id,int body,const btVector3& p){endGrab();auto& instance=get(id);if(body<0||size_t(body)>=instance.bodies.size())throw std::runtime_error("Invalid body");auto& r=*instance.bodies[body]->rigid;if(r.getInvMass()==0)throw std::runtime_error("Unfreeze this body before grabbing");impl->grab=std::make_unique<btPoint2PointConstraint>(r,r.getWorldTransform().inverse()*p);impl->grab->m_setting.m_impulseClamp=50;impl->grab->m_setting.m_tau=.2f;impl->grab->setPivotB(p);impl->grabInstance=id;dynamics().addConstraint(impl->grab.get(),true);r.activate(true);}
void World::updateGrab(const btVector3& p){if(impl->grab)impl->grab->setPivotB(p);}
void World::endGrab(){if(!impl)return;if(impl->grab){dynamics().removeConstraint(impl->grab.get());impl->grab.reset();}impl->grabInstance=0;impl->physgunBody=-1;}
void World::beginPhysgun(uint64_t id,int body,const btTransform& handle){
    endGrab();auto& p=get(id);if(body<0||size_t(body)>=p.bodies.size()||p.bodies[body]->mode==0)throw std::runtime_error("Invalid physics gun body");
    p.freeze(false);beginGrab(id,body,p.bodies[body]->rigid->getWorldTransform().getOrigin());
    float payload=0;for(auto& b:p.bodies)if(b->mode!=0)payload+=b->mass;
    impl->grab->m_setting.m_impulseClamp=std::clamp(payload*.4f,30.f,200.f);
    impl->physgunBody=body;impl->physgunTarget=handle;
    impl->physgunOffset=handle.inverse()*p.bodies[body]->rigid->getWorldTransform();
}
void World::updatePhysgun(const btTransform& handle){impl->physgunTarget=handle;}
void World::drivePhysgun(){
    if(impl->physgunBody<0)return;auto& r=*get(impl->grabInstance).bodies[impl->physgunBody]->rigid;if(r.getInvMass()<=0)return;
    // Solve translation with the ragdoll constraints, sharing the full payload
    // even when grabbing a light hand/head. Bounded torque supplies rotation.
    // Teleporting the body here would bypass the anatomical limits each tick.
    auto goal=impl->physgunTarget*impl->physgunOffset;
    if(impl->grab)impl->grab->setPivotB(goal.getOrigin());
    auto q=goal.getRotation()*r.getOrientation().inverse();q.normalize();if(q.w()<0)q=-q;
    btVector3 rotation(q.x(),q.y(),q.z());float len=rotation.length();if(len>1e-6f)rotation*=2*btAtan2(len,q.w())/len;
    auto angular=rotation*100-r.getAngularVelocity()*20;if(angular.length2()>22500)angular*=150/angular.length();
    auto local=r.getWorldTransform().getBasis().transpose()*angular;auto inv=r.getInvInertiaDiagLocal();for(int k=0;k<3;k++)local[k]=inv[k]>0?local[k]/inv[k]:0;
    r.applyTorque(r.getWorldTransform().getBasis()*local);r.activate(true);
}

Instance::Instance(World& w,std::shared_ptr<Model> m,uint64_t handle,const Json& options):id(handle),owner(&w),model(std::move(m)){
    float height=model->maximum.y()-model->minimum.y();scale=resolveSourceScale(options,height)*Inch;if(!std::isfinite(scale)||scale<.0001f||scale>100)throw std::runtime_error("Invalid model scale");
    auto position=jvec(options.value("position",Json::array()))*Inch;auto rotation=jvec(options.value("angles",Json::array()));btQuaternion q;q.setEulerZYX(rotation.y()*SIMD_RADS_PER_DEG,rotation.x()*SIMD_RADS_PER_DEG,rotation.z()*SIMD_RADS_PER_DEG);placement=btTransform(q,position);
    if(options.contains("center"))placement.setOrigin(jvec(options.at("center"))*Inch-placement.getBasis()*toSource((model->minimum+model->maximum)*.5f)*scale);
    size_t n=model->bones.size();local.resize(n,btTransform::getIdentity());global=skin=manual=local;drivers.resize(n,-1);morphWeights.resize(model->morphs.size());lastImpulseWeights=morphWeights;
    sourceControl.resize(n,-1);evaluate(false);
    try{sceneOwner=options.value("sourceEntity",uint64_t(0));if(options.value("backend","")=="source"){secondaryBackend=options.value("secondaryBackend",std::string("reference"));sourceRig=std::make_shared<Rig>(options.contains("rigManifest")?rigFromManifest(options.at("rigManifest")):fitRig(*model,options));validateRig(*sourceRig,*model);secondaryBroadphase=options.value("secondaryBroadphase",secondaryBroadphaseDefault());isSapBroadphase(secondaryBroadphase);presentationDriven=options.value("presentationDriven",false);scale=sourceRig->scale*Inch;sourcePose=global;for(size_t i=0;i<sourceRig->bones.size();i++){int b=sourceRig->bones[i].mmd;if(b>=0)sourceControl[b]=int(i);for(int alias:sourceRig->bones[i].aliases)sourceControl[alias]=int(i);}secondary=std::make_unique<Secondary>(*this);secondary->setCollisionFlags(options.contains("collisionFlags")?options.at("collisionFlags").get<unsigned>():options.contains("secondaryCollision")?collisionFlagsForLevel(options.at("secondaryCollision").get<int>()):Collide::Default);}else buildPhysics(options);publish(0);}catch(...){for(auto& s:softBodies)w.dynamics().removeSoftBody(s.body.get());for(auto& j:joints)w.dynamics().removeConstraint(j.get());for(auto& b:bodies)w.dynamics().removeRigidBody(b->rigid.get());throw;}
    if(!sourceRig&&options.value("frozen",false))freeze(true);
}
Instance::~Instance(){
    // The queued tick reads sourceControl and other Instance members. Join it
    // before member destruction; those vectors are declared after secondary
    // and would otherwise be freed before Secondary's destructor can wait.
    secondary.reset();
    for(auto& s:softBodies)owner->dynamics().removeSoftBody(s.body.get());for(auto& j:joints)owner->dynamics().removeConstraint(j.get());for(auto& b:bodies)owner->dynamics().removeRigidBody(b->rigid.get());
}
void Instance::buildPhysics(const Json& options){
    auto& dynamics=owner->dynamics();auto& m=*model;
    auto add=[&](std::unique_ptr<Body> b,btTransform transform){
        b->initial=transform;btVector3 inertia(0,0,0);if(b->mass>0)b->shape->calculateLocalInertia(b->mass,inertia);
        btRigidBody::btRigidBodyConstructionInfo ci(b->mode==0?0:b->mass,nullptr,b->shape.get(),inertia);b->rigid=std::make_unique<btRigidBody>(ci);b->rigid->setWorldTransform(transform);b->rigid->setInterpolationWorldTransform(transform);b->rigid->setUserPointer(b.get());b->rigid->setUserIndex(b->generated?1:0);
        if(b->mode==0){b->rigid->setCollisionFlags((b->rigid->getCollisionFlags()&~btCollisionObject::CF_STATIC_OBJECT)|btCollisionObject::CF_KINEMATIC_OBJECT);b->rigid->setActivationState(DISABLE_DEACTIVATION);}
        else {b->rigid->setSleepingThresholds(.01f,.05f);b->rigid->setCcdMotionThreshold(.05f);b->rigid->setCcdSweptSphereRadius(.005f);}
        b->rigid->setUserIndex2(int(id));dynamics.addRigidBody(b->rigid.get(),1<<b->group,b->mask);bodies.push_back(std::move(b));
    };
    for(auto s:m.bodies){auto b=std::make_unique<Body>();b->bone=boneIndex(nanoemModelRigidBodyGetBoneObject(s));b->mode=nanoemModelRigidBodyGetTransformType(s);b->mass=std::max(0.f,nanoemModelRigidBodyGetMass(s));b->group=nanoemModelRigidBodyGetCollisionGroupId(s);b->mask=nanoemModelRigidBodyGetCollisionMask(s)&0xffff;
        auto size=vec(nanoemModelRigidBodyGetShapeSize(s))*scale;int shape=nanoemModelRigidBodyGetShapeType(s);
        if(shape==0)b->shape=std::make_unique<btSphereShape>(std::max(.001f,size.x()));
        else if(shape==1){auto v=toSource(size).absolute();v.setMax(btVector3(.001f,.001f,.001f));b->shape=std::make_unique<btBoxShape>(v);}
        else if(shape==2)b->shape=std::make_unique<btCapsuleShapeZ>(std::max(.001f,size.x()),std::max(0.f,size.y()));else throw std::runtime_error("Invalid rigid-body shape");
        b->shape->setMargin(.001f);auto initial=pose(nanoemModelRigidBodyGetOrigin(s),nanoemModelRigidBodyGetOrientation(s));
        if(nanoemModelRigidBodyIsBoneRelativePosition(s)&&b->bone>=0)initial.getOrigin()+=m.bones[b->bone].position;
        auto converted=convert(initial,scale);b->offset=b->bone>=0?convert(global[b->bone],scale).inverse()*converted:converted;
        add(std::move(b),placement*converted);auto& added=*bodies.back();added.rigid->setDamping(std::clamp(nanoemModelRigidBodyGetLinearDamping(s),0.f,1.f),std::clamp(nanoemModelRigidBodyGetAngularDamping(s),0.f,1.f));added.rigid->setFriction(std::max(0.f,nanoemModelRigidBodyGetFriction(s)));added.rigid->setRestitution(std::clamp(nanoemModelRigidBodyGetRestitution(s),0.f,1.f));
        if(added.bone>=0&&added.mode!=0&&drivers[added.bone]<0)drivers[added.bone]=int(bodies.size()-1);
    }
    for(size_t ji=0;ji<m.joints.size();ji++){auto s=m.joints[ji];auto ref=m.jointReferences[ji];if(!ref.valid)continue;int a=ref.a,b=ref.b;
        auto& ra=a<0?btTypedConstraint::getFixedBody():*bodies[a]->rigid;auto& rb=b<0?btTypedConstraint::getFixedBody():*bodies[b]->rigid;auto jt=placement*convert(pose(nanoemModelJointGetOrigin(s),nanoemModelJointGetOrientation(s)),scale);auto fa=ra.getWorldTransform().inverse()*jt,fb=rb.getWorldTransform().inverse()*jt;
        int type=nanoemModelJointGetType(s);auto ll=toSource(vec(nanoemModelJointGetLinearLowerLimit(s)))*scale,lu=toSource(vec(nanoemModelJointGetLinearUpperLimit(s)))*scale;
        auto al=-toSource(vec(nanoemModelJointGetAngularLowerLimit(s))),au=-toSource(vec(nanoemModelJointGetAngularUpperLimit(s)));for(int i=0;i<3;i++){if(ll[i]>lu[i])std::swap(ll[i],lu[i]);if(al[i]>au[i])std::swap(al[i],au[i]);}
        std::unique_ptr<btTypedConstraint> constraint;
        if(type==0||type==1){auto spring=std::make_unique<btGeneric6DofSpringConstraint>(ra,rb,fa,fb,true);spring->setLinearLowerLimit(ll);spring->setLinearUpperLimit(lu);spring->setAngularLowerLimit(al);spring->setAngularUpperLimit(au);
            auto ls=toSource(vec(nanoemModelJointGetLinearStiffness(s))).absolute(),as=toSource(vec(nanoemModelJointGetAngularStiffness(s))).absolute()*(scale*scale);
            if(type==0)for(int i=0;i<3;i++){if(ls[i]>0){spring->enableSpring(i,true);spring->setStiffness(i,ls[i]);spring->setDamping(i,.5f);}if(as[i]>0){spring->enableSpring(i+3,true);spring->setStiffness(i+3,as[i]);spring->setDamping(i+3,.5f);}}spring->setEquilibriumPoint();constraint=std::move(spring);
        }else if(type==2)constraint=std::make_unique<btPoint2PointConstraint>(ra,rb,fa.getOrigin(),fb.getOrigin());
        else if(type==3){auto c=std::make_unique<btConeTwistConstraint>(ra,rb,fa,fb);c->setLimit(btFabs(au.y()),btFabs(au.z()),btFabs(au.x()));constraint=std::move(c);}
        else if(type==4){auto c=std::make_unique<btSliderConstraint>(ra,rb,fa,fb,true);c->setLowerLinLimit(ll.x());c->setUpperLinLimit(lu.x());c->setLowerAngLimit(al.x());c->setUpperAngLimit(au.x());constraint=std::move(c);}
        else if(type==5){auto c=std::make_unique<btHingeConstraint>(ra,rb,fa,fb);c->setLimit(al.z(),au.z());constraint=std::move(c);}else throw std::runtime_error("Unsupported PMX joint type");
        dynamics.addConstraint(constraint.get(),false);joints.push_back(std::move(constraint));
    }
    if(options.value("ragdoll",true)){
        const std::vector<std::pair<std::string,std::string>> names={{"下半身","lower body"},{"上半身","upper body"},{"上半身2","upper body2"},{"首","neck"},{"頭","head"},{"左腕","arm_l"},{"右腕","arm_r"},{"左ひじ","elbow_l"},{"右ひじ","elbow_r"},{"左手首","wrist_l"},{"右手首","wrist_r"},{"左足","leg_l"},{"右足","leg_r"},{"左ひざ","knee_l"},{"右ひざ","knee_r"},{"左足首","ankle_l"},{"右足首","ankle_r"}};
        std::set<int> core;
        for(size_t i=0;i<m.bones.size();i++)for(auto& [jp,en]:names)if(m.bones[i].name==jp||m.bones[i].english==en||m.bones[i].name==en)core.insert(int(i));
        if(options.contains("coreBones")){core.clear();for(auto& value:options["coreBones"]){int i=value;if(i<0||size_t(i)>=m.bones.size())throw std::runtime_error("Invalid core bone mapping");core.insert(i);}}
        if(core.empty()&&!m.bones.empty()){core.insert(0);warnings.push_back("No standard humanoid core found; root body only. Set coreBones to map this rig.");}
        auto roleOf=[&](int i){for(size_t k=0;k<names.size();k++)if(m.bones[i].name==names[k].first||m.bones[i].english==names[k].second||m.bones[i].name==names[k].second)return int(k);return -1;};
        auto boneForRole=[&](int role){for(int i:core)if(roleOf(i)==role)return i;return -1;};
        float totalMass=options.value("mass",70.f);if(!std::isfinite(totalMass)||totalMass<=0||totalMass>10000)throw std::runtime_error("Invalid ragdoll mass");
        for(int i:core){if(drivers[i]>=0&&bodies[drivers[i]]->mass>0){bodies[drivers[i]]->core=true;continue;}
            auto start=toSource(m.bones[i].position)*scale;btVector3 end=start+btVector3(0,0,.12f);
            int role=roleOf(i),nextRole=-1;
            if(role==1)nextRole=2;else if(role==2)nextRole=3;else if(role==3)nextRole=4;else if(role==5||role==6||role==7||role==8||role==11||role==12||role==13||role==14)nextRole=role+2;
            int child=nextRole>=0?boneForRole(nextRole):-1;
            if(child<0&&role==1)child=boneForRole(3);
            if(child>=0)end=toSource(m.bones[child].position)*scale;
            else if(role==9||role==10){int elbow=boneForRole(role-2);if(elbow>=0){auto d=start-toSource(m.bones[elbow].position)*scale;if(d.length2()>1e-8f)end=start+d.normalized()*.09f;}}
            else if(role==15||role==16)end=start+btVector3(-.12f,0,-.025f);
            float length=(end-start).length();float radius=std::clamp(length*.22f,.025f,.13f);if(m.bones[i].name=="頭"||m.bones[i].english=="head")radius=.095f;
            auto b=std::make_unique<Body>();b->bone=i;b->mode=1;b->mass=totalMass/std::max<size_t>(1,core.size());b->core=b->generated=true;b->shape=std::make_unique<btCapsuleShapeZ>(radius,std::max(.01f,length-2*radius));
            auto rotation=length>.001f?shortestArcQuat(btVector3(0,0,1),(end-start).normalized()):btQuaternion::getIdentity();btTransform transform(rotation,(start+end)*.5f);b->offset=convert(global[i],scale).inverse()*transform;
            add(std::move(b),placement*transform);drivers[i]=int(bodies.size()-1);bodies.back()->rigid->setDamping(.04f,.12f);bodies.back()->rigid->setFriction(.65f);
        }
        int pelvis=core.empty()?-1:*core.begin();for(int i:core)if(m.bones[i].name=="下半身"||m.bones[i].english=="lower body")pelvis=i;
        // Replace only primary-to-primary author joints. PMX secondary springs
        // and mode-0 followers remain intact; authored cores also get limits.
        for(auto it=joints.begin();it!=joints.end();){auto* a=static_cast<Body*>((*it)->getRigidBodyA().getUserPointer());auto* b=static_cast<Body*>((*it)->getRigidBodyB().getUserPointer());
            if(a&&b&&a->core&&b->core){dynamics.removeConstraint(it->get());it=joints.erase(it);}else ++it;}
        for(int i:core){int parent=m.bones[i].parent;while(parent>=0&&!core.contains(parent))parent=m.bones[parent].parent;if(parent<0&&i!=pelvis)parent=pelvis;if(parent<0||drivers[i]<0||drivers[parent]<0)continue;
            auto& child=*bodies[drivers[i]],&ancestor=*bodies[drivers[parent]];
            int kind=roleOf(i);
            btVector3 lower(-20,-25,-20),upper(20,30,20);btMatrix3x3 axes=btMatrix3x3::getIdentity();
            if(kind>=5){
                // X is the flexion hinge; Y follows the limb. Deriving its frame
                // from the bind skeleton handles A poses and mirrored arms.
                auto direction=toSource(m.bones[i].position-m.bones[parent].position);
                if(kind==5||kind==6||kind==11||kind==12){int tip=boneForRole(kind+2);if(tip>=0)direction=toSource(m.bones[tip].position-m.bones[i].position);}
                if(direction.length2()<1e-8f)direction={0,0,-1};direction.normalize();
                auto hinge=direction.cross(btVector3(-1,0,0));if(hinge.length2()<1e-8f)hinge={0,1,0};hinge.normalize();
                // Bullet's Euler limit angle has the opposite sign to physical
                // rotation: knees flex backward, arms and hips flex forward.
                if(kind!=13&&kind!=14)hinge=-hinge;
                auto front=hinge.cross(direction).normalized();
                axes=btMatrix3x3(hinge.x(),direction.x(),front.x(),hinge.y(),direction.y(),front.y(),hinge.z(),direction.z(),front.z());
                if(kind==7||kind==8){lower={-3,0,0};upper={145,0,0};} // elbows
                else if(kind==13||kind==14){lower={-2,0,0};upper={145,0,0};} // knees
                else if(kind==5||kind==6){lower={-95,-65,-95};upper={120,65,95};}
                else if(kind==9||kind==10){lower={-65,-25,-30};upper={65,25,30};}
                else if(kind==11||kind==12){lower={-30,-35,-45};upper={115,35,45};}
                else {lower={-45,-15,-20};upper={35,15,20};}
            }else if(kind==3){lower={-30,-40,-65};upper={30,45,65};}
            else if(kind==4){lower={-20,-25,-25};upper={20,30,25};}
            btTransform frame(placement.getBasis()*axes,placement*(toSource(m.bones[i].position)*scale));auto c=std::make_unique<btGeneric6DofConstraint>(*ancestor.rigid,*child.rigid,ancestor.rigid->getWorldTransform().inverse()*frame,child.rigid->getWorldTransform().inverse()*frame,true);
            lower*=SIMD_RADS_PER_DEG;upper*=SIMD_RADS_PER_DEG;
            c->setLinearLowerLimit(btVector3(0,0,0));c->setLinearUpperLimit(btVector3(0,0,0));c->setAngularLowerLimit(lower);c->setAngularUpperLimit(upper);c->setOverrideNumSolverIterations(40);
            anatomicalJoints.push_back({i,parent,c.get(),lower,upper});dynamics.addConstraint(c.get(),true);joints.push_back(std::move(c));
        }
    }
    softBodies.reserve(m.softBodies.size());
    for(auto s:m.softBodies){auto material=nanoemModelSoftBodyGetMaterialObject(s);int mi=material?nanoemModelObjectGetIndex(nanoemModelMaterialGetModelObject(material)):-1;if(mi<0||size_t(mi)>=m.materials.size())throw std::runtime_error("Soft body references missing material");auto& mat=m.materials[mi];
        Soft soft;std::unordered_map<unsigned,int> mapping;std::vector<btVector3> nodes;std::vector<std::array<int,3>> faces;
        for(unsigned i=mat.first;i<mat.first+mat.count;i+=3){std::array<int,3> face;for(int k=0;k<3;k++){unsigned vi=m.indices[i+k];auto [it,inserted]=mapping.emplace(vi,int(nodes.size()));if(inserted){nodes.push_back(placement*(toSource(m.vertices[vi].position)*scale));soft.vertexIndices.push_back(vi);}face[k]=it->second;}faces.push_back(face);}
        if(nodes.empty()){warnings.push_back("Empty soft-body material");continue;}
        soft.body=std::make_unique<btSoftBody>(&dynamics.getWorldInfo(),int(nodes.size()),nodes.data(),nullptr);auto& b=*soft.body;auto bm=b.m_materials[0];
        bm->m_kLST=std::clamp(nanoemModelSoftBodyGetLinearStiffnessCoefficient(s),0.f,1.f);bm->m_kAST=std::clamp(nanoemModelSoftBodyGetAngularStiffnessCoefficient(s),0.f,1.f);bm->m_kVST=std::clamp(nanoemModelSoftBodyGetVolumeStiffnessCoefficient(s),0.f,1.f);
        bool rope=nanoemModelSoftBodyGetShapeType(s)==NANOEM_MODEL_SOFT_BODY_SHAPE_TYPE_ROPE;
        std::set<std::pair<int,int>> edges;
        if(rope){for(int i=1;i<int(nodes.size());i++)b.appendLink(i-1,i,bm);}
        else for(auto f:faces){b.appendFace(f[2],f[1],f[0],bm);for(int k=0;k<3;k++){int a=f[k],c=f[(k+1)%3];if(a>c)std::swap(a,c);if(a!=c&&edges.emplace(a,c).second)b.appendLink(a,c,bm);}}
        b.setTotalMass(std::max(.001f,nanoemModelSoftBodyGetTotalMass(s)));b.getCollisionShape()->setMargin(std::max(.001f,nanoemModelSoftBodyGetCollisionMargin(s)*scale));
        auto& c=b.m_cfg;c.aeromodel=btSoftBody::eAeroModel::_ (std::max(0,int(nanoemModelSoftBodyGetAeroModel(s))));
        c.kVCF=nanoemModelSoftBodyGetVelocityCorrectionFactor(s);c.kDP=std::clamp(nanoemModelSoftBodyGetDampingCoefficient(s),0.f,1.f);c.kDG=nanoemModelSoftBodyGetDragCoefficient(s);c.kLF=nanoemModelSoftBodyGetLiftCoefficient(s);c.kPR=nanoemModelSoftBodyGetPressureCoefficient(s);c.kVC=nanoemModelSoftBodyGetVolumeConversationCoefficient(s);c.kDF=nanoemModelSoftBodyGetDynamicFrictionCoefficient(s);c.kMT=nanoemModelSoftBodyGetPoseMatchingCoefficient(s);
        c.kCHR=nanoemModelSoftBodyGetRigidContactHardness(s);c.kKHR=nanoemModelSoftBodyGetKineticContactHardness(s);c.kSHR=nanoemModelSoftBodyGetSoftContactHardness(s);c.kAHR=nanoemModelSoftBodyGetAnchorHardness(s);
        c.kSRHR_CL=nanoemModelSoftBodyGetSoftVSRigidHardness(s);c.kSKHR_CL=nanoemModelSoftBodyGetSoftVSKineticHardness(s);c.kSSHR_CL=nanoemModelSoftBodyGetSoftVSSoftHardness(s);c.kSR_SPLT_CL=nanoemModelSoftBodyGetSoftVSRigidImpulseSplit(s);c.kSK_SPLT_CL=nanoemModelSoftBodyGetSoftVSKineticImpulseSplit(s);c.kSS_SPLT_CL=nanoemModelSoftBodyGetSoftVSSoftImpulseSplit(s);
        c.viterations=std::clamp(nanoemModelSoftBodyGetVelocitySolverIterations(s),0,128);c.piterations=std::clamp(nanoemModelSoftBodyGetPositionsSolverIterations(s),1,128);c.diterations=std::clamp(nanoemModelSoftBodyGetDriftSolverIterations(s),0,128);c.citerations=std::clamp(nanoemModelSoftBodyGetClusterSolverIterations(s),1,128);
        c.collisions=btSoftBody::fCollision::SDF_RS|btSoftBody::fCollision::VF_SS;
        if(nanoemModelSoftBodyIsBendingConstraintsEnabled(s))b.generateBendingConstraints(std::clamp(nanoemModelSoftBodyGetBendingConstraintsDistance(s),1,8),bm);
        if(nanoemModelSoftBodyIsClustersEnabled(s)){b.generateClusters(std::clamp(nanoemModelSoftBodyGetClusterCount(s),1,128));c.collisions=btSoftBody::fCollision::CL_RS|btSoftBody::fCollision::CL_SS;}
        nanoem_rsize_t n=0;auto pins=nanoemModelSoftBodyGetAllPinnedVertexIndices(s,&n);for(size_t k=0;k<n;k++){auto it=mapping.find(pins[k]);if(it==mapping.end())throw std::runtime_error("Pinned vertex is outside soft-body material");b.setMass(it->second,0);soft.pins.push_back(it->second);}
        auto anchors=nanoemModelSoftBodyGetAllAnchorObjects(s,&n);for(size_t k=0;k<n;k++){int vi=vertexIndex(nanoemModelSoftBodyAnchorGetVertexObject(anchors[k])),ri=bodyIndex(nanoemModelSoftBodyAnchorGetRigidBodyObject(anchors[k]));auto it=mapping.find(vi);if(it==mapping.end()||ri<0||size_t(ri)>=bodies.size())throw std::runtime_error("Invalid soft-body anchor");b.appendAnchor(it->second,bodies[ri]->rigid.get(),!nanoemModelSoftBodyAnchorIsNearEnabled(anchors[k]));}
        b.updateNormals();b.setUserIndex(3);b.setUserIndex2(int(id));dynamics.addSoftBody(&b,1<<nanoemModelSoftBodyGetCollisionGroupId(s),nanoemModelSoftBodyGetCollisionMask(s)&0xffff);softBodies.push_back(std::move(soft));
    }
}
void Instance::beforeStep(){
    auto weights=expandedMorphs();for(size_t i=0;i<weights.size();i++){float delta=weights[i]-lastImpulseWeights[i];if(delta!=0&&nanoemModelMorphGetType(model->morphs[i])==NANOEM_MODEL_MORPH_TYPE_IMPULUSE){nanoem_rsize_t n=0;auto entries=nanoemModelMorphGetAllImpulseMorphObjects(model->morphs[i],&n);for(size_t k=0;k<n;k++){auto e=entries[k];int bi=bodyIndex(nanoemModelMorphImpulseGetRigidBodyObject(e));if(bi<0)continue;auto& body=*bodies[bi]->rigid;auto linear=toSource(vec(nanoemModelMorphImpulseGetVelocity(e)))*scale*delta;auto angular=-toSource(vec(nanoemModelMorphImpulseGetTorque(e)))*(scale*scale*delta);auto rotation=nanoemModelMorphImpulseIsLocal(e)?body.getWorldTransform().getBasis():placement.getBasis();body.applyCentralImpulse(rotation*linear);body.applyTorqueImpulse(rotation*angular);body.activate(true);}}}lastImpulseWeights=weights;
    for(auto& b:bodies)if(b->mode==0&&b->bone>=0){auto t=placement*convert(global[b->bone],scale)*b->offset;b->rigid->setWorldTransform(t);owner->dynamics().updateSingleAabb(b->rigid.get());}
    for(auto& s:softBodies)for(auto pin:s.pins){unsigned vi=s.vertexIndices[pin];auto p=placement*(toSource(skinPosition(model->vertices[vi],skin))*scale);auto& node=s.body->m_nodes[int(pin)];node.m_x=node.m_q=p;node.m_v.setZero();}
}
void Instance::freeze(bool value){
    if(frozen==value)return;
    if(value)owner->endGrab();
    frozen=value;
    // Zero mass alone leaves bodies in Bullet's dynamic/island lists and keeps
    // springs solving against two immovable ends. Re-register their static state.
    for(auto& j:joints)j->setEnabled(!value);
    for(auto& b:bodies)if(b->mode!=0){
        auto& r=*b->rigid;owner->dynamics().removeRigidBody(&r);
        btVector3 inertia(0,0,0);if(!value)b->shape->calculateLocalInertia(b->mass,inertia);
        r.setMassProps(value?0:b->mass,inertia);r.updateInertiaTensor();r.clearForces();
        r.setLinearVelocity(btVector3(0,0,0));r.setAngularVelocity(btVector3(0,0,0));
        r.setInterpolationWorldTransform(r.getWorldTransform());r.forceActivationState(value?ISLAND_SLEEPING:ACTIVE_TAG);
        owner->dynamics().addRigidBody(&r,1<<b->group,b->mask);
    }
    for(auto& s:softBodies)s.body->forceActivationState(value?DISABLE_SIMULATION:ACTIVE_TAG);
}
void Instance::applyPose(){evaluate(false);for(auto& b:bodies)if(b->bone>=0){auto t=placement*convert(global[b->bone],scale)*b->offset;b->rigid->setWorldTransform(t);b->rigid->setInterpolationWorldTransform(t);b->rigid->setLinearVelocity(btVector3(0,0,0));b->rigid->setAngularVelocity(btVector3(0,0,0));owner->dynamics().updateSingleAabb(b->rigid.get());}for(auto& s:softBodies){for(size_t i=0;i<s.vertexIndices.size();i++){auto& n=s.body->m_nodes[int(i)];n.m_x=n.m_q=placement*(toSource(skinPosition(model->vertices[s.vertexIndices[i]],skin))*scale);n.m_v.setZero();}s.body->updateNormals();}publish(owner->time);}
void Instance::updatePose(){evaluate(true);poseDirty=true;}
void Instance::setBonePose(size_t bone,const btTransform& transform){
    if(frozen){
        auto pivot=placement*convert(global.at(bone),scale);
        auto delta=pivot*convert(manual.at(bone).inverse()*transform,scale)*pivot.inverse();
        std::vector<bool> affected(model->bones.size());
        for(auto i:model->order){int parent=model->bones[i].parent;affected[i]=i==bone||(parent>=0&&affected[parent]);}
        for(auto& b:bodies)if(b->bone>=0&&affected[b->bone]){
            auto& r=*b->rigid;auto t=delta*r.getWorldTransform();r.setWorldTransform(t);r.setInterpolationWorldTransform(t);owner->dynamics().updateSingleAabb(&r);
        }
        for(auto& soft:softBodies){for(size_t n=0;n<soft.vertexIndices.size();n++){
            const auto& v=model->vertices[soft.vertexIndices[n]];float weight=0;for(int k=0;k<4;k++)if(v.bones[k]>=0&&affected[v.bones[k]])weight+=v.weights[k];
            auto& node=soft.body->m_nodes[int(n)];node.m_x=node.m_q=node.m_x.lerp(delta*node.m_x,std::clamp(weight,0.f,1.f));node.m_v.setZero();
        }soft.body->updateNormals();}
    }
    manual.at(bone)=transform;manualVersion++;updatePose();
}
void Instance::reset(){if(secondary){
    // Rebuild only the secondary world, including its solver/contact history.
    // Source physics objects, the current primary pose and appearance stay intact.
    unsigned flags=secondary->collisionFlags;sourceError.clear();pendingSourceDelta=0;
    secondary->waitAsyncIdle();auto resets=secondary->resets;
    secondary.reset();evaluate(false);secondary=std::make_unique<Secondary>(*this);secondary->setCollisionFlags(flags);
    secondary->resets=resets+1;secondary->resetReason="manual";
    // Construction already initializes bodies at the current Source pose. Do
    // not repeat the reset at the next presentation frame or replay old debt.
    sourceTeleport=false;pendingSourceDelta=0;presentationDirty=true;poseDirty=true;return;
}owner->endGrab();applyPose();lastImpulseWeights=expandedMorphs();for(auto& b:bodies){b->rigid->clearForces();b->rigid->activate(true);}}
Json Instance::diagnostics(bool detailed) const {
    if(secondary){auto j=secondary->diagnostics(detailed);j["quality"]=secondary->qualityInfo();j.update({{"id",id},{"asset",model->id},{"sourceObjects",18},{"sourceTimestamp",sourceTimestamp},{"rig",sourceRig->key},{"presentationDriven",presentationDriven},{"presentationSmoothingMs",presentationDelay*1000},{"presentationUpdateIntervalMs",presentationUpdateInterval*1000},{"sourceError",sourceError},{"presentationFrame",presentationFrame},{"sourceUnitsPerPmx",sourceRig->scale},{"deformMs",deformMs}});return j;}
    Json j={{"id",id},{"asset",model->id},{"bodies",bodies.size()},{"joints",joints.size()},{"anatomicalJoints",anatomicalJoints.size()},{"softBodies",softBodies.size()},{"frozen",frozen},{"scale",scale},{"warnings",warnings},{"bodyList",Json::array()},{"jointLimits",Json::array()}};
    for(auto& b:bodies){auto p=b->rigid->getWorldTransform().getOrigin()/Inch;j["bodyList"].push_back({{"bone",b->bone},{"mode",b->mode},{"core",b->core},{"generated",b->generated},{"position",array(p)},{"mass",b->mass}});}
    for(auto& a:anatomicalJoints){auto& c=*a.constraint;c.calculateTransforms();j["jointLimits"].push_back({{"bone",a.bone},{"parent",a.parent},{"name",model->bones[a.bone].name},{"lower",array(a.lower*SIMD_DEGS_PER_RAD)},{"upper",array(a.upper*SIMD_DEGS_PER_RAD)},{"angle",array(btVector3(c.getAngle(0),c.getAngle(1),c.getAngle(2))*SIMD_DEGS_PER_RAD)},{"pivotError",(c.getCalculatedTransformA().getOrigin()-c.getCalculatedTransformB().getOrigin()).length()/Inch}});}
    return j;
}
void Instance::submitSourcePose(std::span<const btTransform> physical,std::span<const btTransform> manipulation,double timestamp,bool defer){
 if(!sourceRig||physical.size()!=18||manipulation.size()!=sourceRig->bones.size()||!std::isfinite(timestamp))throw std::runtime_error("Source pose does not match rig");
 if(presentationDriven)return;
 if(timestamp<=sourceTimestamp)return;
 auto old=sourcePose;std::vector<btTransform> live(sourceRig->bones.size());auto c=basis();
 for(size_t i=0;i<live.size();i++){auto& b=sourceRig->bones[i];if(b.physics>=0)live[i]=physical[b.physics];else live[i]=(b.parent>=0?live[b.parent]*sourceRig->bones[b.parent].rest.inverse():btTransform::getIdentity())*b.rest*manipulation[i];
  auto drive=[&](int mmd){if(mmd<0)return;auto delta=live[i]*b.rest.inverse()*rigMeshBind(*sourceRig);auto sourcePosition=delta*(toSource(model->bones[mmd].position)*sourceRig->scale);auto relative=placement.inverse()*btTransform(delta.getBasis(),sourcePosition*Inch);sourcePose[mmd]=btTransform(c.transpose()*relative.getBasis()*c,fromSource(relative.getOrigin())/scale);};drive(b.mmd);for(int alias:b.aliases)drive(alias);
 }
 bool teleport=sourceTimestamp<0||(sourcePose[sourceRig->bones[0].mmd].getOrigin()-old[sourceRig->bones[0].mmd].getOrigin()).length()>80;
 double elapsed=sourceTimestamp<0?1./60:timestamp-sourceTimestamp;sourceTimestamp=timestamp;
 sourceTeleport=sourceTeleport||teleport;pendingSourceDelta+=elapsed;if(!defer)stepSource();
}
namespace {
bool poseDiffers(const std::vector<btTransform>& a,std::span<const btTransform> b){
 if(a.size()!=b.size())return true;
 for(size_t i=0;i<a.size();i++){const auto& x=a[i];const auto& y=b[i];
  if((x.getOrigin()-y.getOrigin()).length2()>1e-8f||(x.getBasis().getColumn(0)-y.getBasis().getColumn(0)).length2()>1e-10f||(x.getBasis().getColumn(2)-y.getBasis().getColumn(2)).length2()>1e-10f)return true;}
 return false;
}
}
// Bone poses networked to the client (an NPC posed through bone manipulation by
// an animation addon) only change at the server tick rate and hold between
// packets: 66 Hz steps at 350 FPS. Fed straight to the 60 Hz physics they beat
// against its steps (follower velocity alternates between one and two steps
// per tick) and the hair's live anchor steps as well. Such skeletons are shown
// one update interval behind, interpolated between the last distinct poses.
// Skeletons that change every frame (ragdolls, players) pass through unchanged.
std::span<const btTransform> Instance::smoothPresentation(std::span<const btTransform> live,double timestamp,double frameDt){
 constexpr double MaxInterval=.06; // slower updates (the 10 Hz unseen-model rate) are not smoothed
 btVector3 root=live[0].getOrigin();
 bool jump=presentationHaveRoot&&(root-presentationLastRoot).length()>128.f;presentationLastRoot=root;presentationHaveRoot=true;
 if(!owner||!owner->poseSmoothing||jump||(sourceTimestamp>=0&&timestamp<sourceTimestamp)){presentationSamples.clear();presentationDelay=0;presentationUpdateInterval=0;return live;}
 if(frameDt>0)presentationFrameInterval=presentationFrameInterval<=0?frameDt:presentationFrameInterval+(frameDt-presentationFrameInterval)*.1;
 if(presentationSamples.empty()||poseDiffers(presentationSamples.back().bones,live)){
  if(!presentationSamples.empty()){double dt=timestamp-presentationSamples.back().time;if(dt>0)presentationUpdateInterval=presentationUpdateInterval<=0?dt:presentationUpdateInterval+(dt-presentationUpdateInterval)*.2;}
  presentationSamples.push_back({timestamp,std::vector<btTransform>(live.begin(),live.end())});
  while(presentationSamples.size()>4)presentationSamples.pop_front();
 }
 // One update interval of delay while the skeleton updates less often than the
 // frames (hysteresis around 1.3-1.6 frames), none otherwise. The delay moves at
 // most a quarter frame per frame, so the displayed time never runs backwards.
 bool stepped=presentationUpdateInterval>0&&presentationUpdateInterval<=MaxInterval&&presentationFrameInterval>0&&presentationUpdateInterval>presentationFrameInterval*(presentationDelay>0?1.3:1.6);
 // A fifth more than the interval covers packet jitter; catching up with the
 // newest pose would hold a frame.
 double target=stepped?presentationUpdateInterval*1.2:0,step=std::max(frameDt,1e-4);
 presentationDelay+=std::clamp(target-presentationDelay,-.1*step,.25*step);
 if(presentationDelay<=1e-5||presentationSamples.size()<2)return live;
 double t=timestamp-presentationDelay;const auto& s=presentationSamples;
 if(t>=s.back().time)return live; // caught up with the newest pose (the current one)
 size_t i=0;while(i+2<s.size()&&s[i+1].time<=t)i++;
 const auto& a=s[i];const auto& b=s[i+1];
 float f=b.time>a.time?float(std::clamp((t-a.time)/(b.time-a.time),0.,1.)):1.f;
 presentationSmoothed.resize(live.size());
 for(size_t k=0;k<live.size();k++)presentationSmoothed[k]=btTransform(a.bones[k].getRotation().slerp(b.bones[k].getRotation(),f).normalized(),a.bones[k].getOrigin().lerp(b.bones[k].getOrigin(),f));
 return presentationSmoothed;
}
void Instance::submitPresentationPose(std::span<const btTransform> raw,double timestamp,uint64_t frame){
 if(!sourceRig||!presentationDriven||raw.size()!=sourceRig->bones.size()||!std::isfinite(timestamp))throw std::runtime_error("Invalid presentation pose");
 if(frame==presentationFrame)return;
 auto live=smoothPresentation(raw,timestamp,sourceTimestamp<0?0:std::max(0.,timestamp-sourceTimestamp));
 auto old=sourcePose;auto c=basis();presentationBones.assign(live.begin(),live.end());
 for(size_t i=0;i<live.size();i++){
  auto& bone=sourceRig->bones[i];auto delta=live[i]*bone.rest.inverse()*rigMeshBind(*sourceRig);
  auto drive=[&](int index){if(index<0)return;auto position=delta*(toSource(model->bones[index].position)*sourceRig->scale);auto relative=placement.inverse()*btTransform(delta.getBasis(),position*Inch);sourcePose[index]=btTransform(c.transpose()*relative.getBasis()*c,fromSource(relative.getOrigin())/scale);};
  drive(bone.mmd);for(int alias:bone.aliases)drive(alias);
 }
 bool teleport=sourceTimestamp<0||timestamp<sourceTimestamp||(sourcePose[sourceRig->bones[0].mmd].getOrigin()-old[sourceRig->bones[0].mmd].getOrigin()).length()*sourceRig->scale>128.f;
 if(teleport&&secondary&&!secondary->asynchronous())secondary->resetReason=sourceTimestamp<0?"first_pose":timestamp<sourceTimestamp?"clock_rewind":"teleport";
 double elapsed=sourceTimestamp<0?1./60:std::max(0.,timestamp-sourceTimestamp);
 std::string reason=teleport?(sourceTimestamp<0?"first_pose":timestamp<sourceTimestamp?"clock_rewind":"teleport"):"";
 sourceTimestamp=timestamp;presentationFrame=frame;presentationDirty=true;
 if(secondary)secondary->preparePresentationMode();
 if(secondary&&secondary->asynchronous()){
  // The tick runs on a worker while this frame renders; everything it needs travels with the input.
  Secondary::Input input;input.time=timestamp;input.elapsed=elapsed;input.pose=sourcePose;input.morphs=expandedMorphs();input.manual=shareManual();input.reset=teleport||sourceTeleport;input.resetReason=sourceTeleport&&reason.empty()?"teleport":reason;
  btVector3 lower(1e9f,1e9f,1e9f),upper=-lower;for(auto& bone:presentationBones){lower.setMin(bone.getOrigin());upper.setMax(bone.getOrigin());}if(snapshot){lower.setMin(snapshot->minimum);upper.setMax(snapshot->maximum);}input.boundsMinimum=lower;input.boundsMaximum=upper;
  secondary->submitAsync(std::move(input));sourceTeleport=false;pendingSourceDelta=0;return;
 }
 sourceTeleport=sourceTeleport||teleport;pendingSourceDelta+=elapsed;
}
void Instance::stepSource(){
 if(!secondary)return;
 secondary->preparePresentationMode();
 if(secondary->simplified()){
  if(!presentationDirty&&pendingSourceDelta<=0)return;
  secondary->stepSimplified(pendingSourceDelta,sourceTeleport);pendingSourceDelta=0;sourceTeleport=false;presentationDirty=false;poseDirty=true;return;
 }
 if(secondary->asynchronous()){if(!presentationDirty)return;presentationDirty=false;evaluate(false);secondary->presentAsync();evaluate(true);poseDirty=true;return;}
 if(pendingSourceDelta<=0&&!presentationDirty)return;presentationDirty=false;if(sourceTeleport){secondary->reset();sourceTeleport=false;}double elapsed=pendingSourceDelta;pendingSourceDelta=0;secondary->step(elapsed);
}
} // namespace mmd
