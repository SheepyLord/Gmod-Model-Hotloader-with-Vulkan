#pragma once
#include <cstddef>
#include <functional>
namespace mmd {
// Synchronous task groups: all captures remain alive until every child completes.
// Waiting threads help the shared queue, including nested deformation groups.
void parallelFor(size_t count,size_t grain,const std::function<void(size_t,size_t)>&);
unsigned workerCount();
unsigned availableThreadCount();
unsigned maximumWorkerCount();
unsigned automaticWorkerCount(unsigned logicalThreads);
void setWorkerCount(unsigned count); // Owning thread only, between frame barriers.
void shutdownJobs();
// Install once on the owning thread, before starting any Bullet MT world.
void initializeBulletScheduler();
// Budget nested physics jobs against the worlds already executing this frame.
void setSecondaryWorkload(unsigned worlds);
void parallelForSecondary(size_t count,size_t grain,const std::function<void(size_t,size_t)>&);
// Fire-and-forget work for asynchronous secondary worlds. Workers take these
// only when no frame-critical group work is queued, and a thread waiting on a
// synchronous group never helps with them, so the render barrier cannot stall
// behind a physics tick. The job owns its captures.
void enqueueBackground(std::function<void()>);
// User plus kernel CPU time of the calling thread in milliseconds (wall time
// on a saturated machine includes preemption; this does not).
double threadCpuMs();
// While alive on the current thread, parallelForSecondary runs inline. An
// asynchronous tick is already one worker's job; nesting would only contend.
class InlinePhysicsScope {
 bool previous;
public:
 InlinePhysicsScope();
 ~InlinePhysicsScope();
 InlinePhysicsScope(const InlinePhysicsScope&)=delete;
 InlinePhysicsScope& operator=(const InlinePhysicsScope&)=delete;
};
}
