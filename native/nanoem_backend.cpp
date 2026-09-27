// The pinned MIT backend plus a narrow access shim for one-way scene colliders.
#include "broadphase.hpp"
#include "scene.hpp"
#include "secondary_mt.hpp"
#include "ordered_dispatcher.hpp"
#include "compute_solver.hpp"
#define btDbvtBroadphase mmd::SecondaryBroadphase
#include "../vendor/nanoem/nanoem/ext/physics_bullet.cc"
#undef btDbvtBroadphase
namespace mmd {
 namespace {std::atomic<bool> midphaseGateEnabled{true};}
 void setMidphaseGate(bool enabled){midphaseGateEnabled.store(enabled,std::memory_order_relaxed);}
 bool midphaseGate(){return midphaseGateEnabled.load(std::memory_order_relaxed);}
 nanoem_physics_world_t* nanoemCreateSecondaryWorld(const BroadphaseConfig& config,nanoem_status_t* status,const std::string& backend){
  BroadphaseCreationScope scope(config);auto world=nanoemPhysicsWorldCreate(nullptr,status);
  if(world&&(backend=="cpu_mt"||backend=="gpu_opencl"||backend=="cpu_mt_v2"||backend=="gpu_vulkan")){
   nanoemPhysicsWorldSetGroundEnabled(world,false);
   auto gravity=world->m_world->getGravity();auto info=world->m_world->getSolverInfo();
   try{
    auto dispatcher=std::make_unique<OrderedDispatcher>(world->m_config);
    auto solver=std::make_unique<ComputeSolver>(backend=="gpu_opencl"?ComputeBackend::OpenCl:backend=="gpu_vulkan"?ComputeBackend::Vulkan:ComputeBackend::Cpu);
    auto dynamics=std::make_unique<SecondaryMtWorld>(dispatcher.get(),world->m_broadphase,solver.get(),world->m_config);
    dynamics->setGravity(gravity);dynamics->setDebugDrawer(world->m_debugger);dynamics->getSolverInfo()=info;
    delete world->m_world;delete world->m_solver;delete world->m_dispatcher;
    world->m_dispatcher=dispatcher.release();world->m_solver=solver.release();world->m_world=dynamics.release();world->m_worldInfo->m_dispatcher=world->m_dispatcher;
   }catch(...){nanoemPhysicsWorldDestroy(world);throw;}

  }
  return world;
 }
 Json nanoemBroadphaseInfo(nanoem_physics_world_t* world){auto p=world->m_broadphase;Json out={{"broadphase",p->mode()},{"broadphaseFallback",p->fallbackReason()},{"broadphaseCapacity",p->capacity()}};
  if(int(world->m_solver->getSolverType())==ComputeSolver::Type){out["compute"]=static_cast<ComputeSolver*>(world->m_solver)->diagnostics();
   // The multicore worlds always pair the ordered dispatcher with the compute solver.
   auto dispatcher=static_cast<OrderedDispatcher*>(world->m_dispatcher);if(dispatcher->countPairs)out["midphase"]={{"dispatched",dispatcher->dispatchedPairs.load()},{"gated",dispatcher->gatedPairs.load()},{"swept",dispatcher->sweptPairs.load()}};}
  return out;}
 void nanoemSetPairCounting(nanoem_physics_world_t* world,bool enabled){if(int(world->m_solver->getSolverType())!=ComputeSolver::Type)return;auto dispatcher=static_cast<OrderedDispatcher*>(world->m_dispatcher);dispatcher->countPairs=enabled;dispatcher->dispatchedPairs=0;dispatcher->gatedPairs=0;dispatcher->sweptPairs=0;}
btSoftRigidDynamicsWorld* nanoemWorld(nanoem_physics_world_t* world){return world?world->m_world:nullptr;}
btRigidBody* nanoemRigidBody(nanoem_physics_rigid_body_t* body){return body?body->m_internalRigidBody:nullptr;}
btTypedConstraint* nanoemConstraint(nanoem_physics_joint_t* joint){return joint?joint->m_internalConstraint:nullptr;}
void nanoemStepFixed(nanoem_physics_world_t* world,float seconds){
 // One externally scheduled, fixed 1/60 s tick, without Bullet's second clock.
 world->m_world->stepSimulation(seconds,0,seconds);
 world->m_worldInfo->m_sparsesdf.GarbageCollect();
}
}
