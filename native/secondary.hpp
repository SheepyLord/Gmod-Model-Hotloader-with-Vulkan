#pragma once
#include "rig.hpp"
#include "broadphase.hpp"
#include "spring_bones.hpp"
#include "scene.hpp"
#include <ext/physics.h>
#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <unordered_map>
namespace mmd {
// The collision levels of releases before 2.2: 0 the character only, 1 and the
// map, 2 and objects. Throws outside 0-2.
unsigned collisionFlagsForLevel(int level);
class Secondary {
public:
 explicit Secondary(Instance&);
 ~Secondary();
 void step(double seconds);
 void preparePresentationMode();
 bool simplified()const{return presentationMode<=0;}
 void stepSimplified(double seconds,bool teleport);
 void reset();
 void setCollisionFlags(unsigned flags);
 void setCollisionMode(int level){setCollisionFlags(collisionFlagsForLevel(level));}
 void setQuality(int divisor,bool suspended);
 Json qualityInfo() const;
 unsigned collisionFlags=Collide::Default;
 std::string effectiveBackend="reference",backendFallback;
 // `parentGlobal`: the parent bone's pose in this same evaluation (spring bones bend relative to it).
 btTransform feedback(size_t bone,const btTransform& animated,const btTransform* parentGlobal=nullptr) const;
 bool drives(size_t bone) const;
 bool hasSprings() const {return springs!=nullptr;}
 void deformSoft(Snapshot&) const;
 bool deformsSoft() const;
 Json diagnostics(bool detailed=true) const;
 btSoftRigidDynamicsWorld* dynamics() const;
 static bool profilerReset(bool enabled=true);
 static void profilerFrame();
 static void profilerDump();
 // Bullet's per-thread profile tree of the calling thread as [{path, ms, calls}];
 // Bullet resets it at every step, so callers accumulate. Empty without profiling.
 static Json profilerSnapshot();
 void setPairCounting(bool enabled); // profiler diagnostics; call while the world is idle
 // cpu_mt_v2: presentation poses are queued and simulated on a shared worker,
 // one world at a time; the render thread only interpolates the last solved
 // state. Nothing in the frame waits for physics unless a wait budget is set.
 struct Input {
  double time=0,elapsed=0;
  std::vector<btTransform> pose;
  std::vector<float> morphs;
  std::shared_ptr<const std::vector<btTransform>> manual;
  btVector3 boundsMinimum{0,0,0},boundsMaximum{0,0,0};
  bool reset=false;
  bool resumeQuality=false;int qualityDivisor=1;
  std::string resetReason;
 };
 bool asynchronous()const{return async&&presentationMode>0;}
 void submitAsync(Input&&);
 void presentAsync();
 void waitAsyncIdle();
 // Submitted inputs the worker has not yet started.
 size_t queuedInputs()const{std::lock_guard lock(mutex);return queue.size();}
 static void setAsyncWaitBudget(double ms);
 static double asyncWaitBudget();
 // Tests: extra wall time every stepping job spends, to make a 60 Hz step outlast a frame.
 static void setAsyncTestStepDelay(double ms);
 // Presented body transform (after interpolation) and the display delay behind the input clock.
 const btTransform& displayTransform(size_t body)const{return display[body];}
 double presentationDelayMs()const;
 // Resting bodies of v2 worlds may deactivate; followers wake their chains.
 static void setSleepPolicy(bool enabled,float linear,float angular,float seconds,float wakeDrift=.02f);
 static Json sleepPolicy();
 static void setTuning(int iterations,float gravity,float damping,bool stretch,float tolerance);
 static Json tuning();
 static int accuracy();
 std::vector<int> drivers;
 double lastMs=0,dropped=0,accumulator=0;
 double physicsMs=0,poseMs=0,sceneMs=0,inputTime=0,simulationTime=0,interpolation=0,lagMs=0;
 uint64_t ticks=0,resets=0;unsigned lastSteps=0,sleepingBodies=0;
 bool interpolate=true;
 std::string resetReason="initial",asyncError;
 // Clock and timing state as the render thread may read it. Asynchronous
 // worlds report their last presented tick; the members above are then owned
 // by the tick job and must not be read or written from the frame.
 struct FrameStats {double physicsMs=0,sceneMs=0,poseMs=0,lastMs=0,accumulator=0,inputTime=0,simulationTime=0,dropped=0;uint64_t ticks=0,resets=0;unsigned steps=0;int iterations=10;std::string resetReason;double physicsTotalMs=0,tickTotalMs=0,guardTotalMs=0,tickCpuTotalMs=0;};
 FrameStats frameStats() const;
private:
 int presentationMode=1;
 // VRM spring bones: simulated at the same fixed ticks as the Bullet world; the
 // frame blends each joint's rotation relative to its parent between ticks.
 std::unique_ptr<SpringSystem> springs;std::vector<btQuaternion> springDisplay;bool springOnly=false;
 float tuningGravity=1,tuningDamping=1;
 void stepSprings(const std::vector<btTransform>& skin);
 void presentSprings(const std::vector<btQuaternion>& previous,const std::vector<btQuaternion>& current,bool blend);
 Json springInfo(uint64_t colliderHits,uint64_t worldHits,double stepMs,uint64_t steps) const;
 struct JiggleBone {btVector3 axis{0,-1,0},tip{0,0,0},velocity{0,0,0};btQuaternion rotation=btQuaternion::getIdentity();float length=1,directionScale=1;int valveParent=-1;bool active=false,ready=false;uint64_t frame=0;};
 mutable std::vector<JiggleBone> jiggle;
 std::vector<btTransform> jiggleTargets;
 btVector3 jiggleTravel{0,0,0},previousJiggleRoot{0,0,0};bool jiggleRootReady=false;
 uint64_t jiggleFrame=0;float jiggleSeconds=0,jiggleDamping=1;double simplifiedMs=0;
 btTransform jiggleFeedback(size_t,const btTransform&) const;
 int requestedDivisor=1,activeDivisor=1,baseIterations=10;
 bool qualitySuspended=false,qualityResume=false;
 bool suspendedReset=false;std::string suspendedResetReason;
 uint64_t qualityWakes=0;
 double qualityBlendStart=-1;
 std::vector<btTransform> qualityBlendPose,qualityBlendAnchors;
 void blendQualityPresentation();
 void resumeQualityPose(const std::vector<btTransform>& skin,const std::vector<btTransform>& pose);
 struct External;
 struct Filter;
 std::unique_ptr<Filter> filter;
 bool filterInstalled=false;
 std::unique_ptr<External> external;
 Instance& instance;
 nanoem_physics_world_t* world=nullptr;
 struct Rigid {nanoem_physics_rigid_body_t* value=nullptr;btTransform initial=btTransform::getIdentity(),current=btTransform::getIdentity(),previous=btTransform::getIdentity(),wakeReference=btTransform::getIdentity(),followStart=btTransform::getIdentity();int bone=-1,mode=0,anchor=-1;bool follower=false,worldAnchored=false;std::vector<int> linked;};
 std::vector<Rigid> bodies;
 std::vector<btTransform> display;
 std::vector<btVector3> displayCompensation;
 struct Joint {nanoem_physics_joint_t* value=nullptr;bool active=false,stretchGuard=false;int a=-1,b=-1,sourceIndex=-1;btTransform frameA,frameB;btVector3 lower,upper;float stretchTolerance=.02f;};
 std::vector<Joint> joints;
 std::vector<nanoem_physics_soft_body_t*> soft;
 std::vector<std::vector<int>> softPins;
 struct SoftVertex {int index;bool pinned;btVector3 previous,current,normal,previousNormal;};
 std::vector<std::vector<SoftVertex>> softFrames;
 struct Sample {double time;std::vector<btTransform> pose;};
 std::deque<Sample> inputs;
 btTransform previousRoot=btTransform::getIdentity(),currentRoot=btTransform::getIdentity();
 // Presentation follows the nearest joint-connected animated attachment,
 // never the pelvis merely because it is the first Source bone.
 std::vector<int> anchors;
 std::vector<btTransform> previousAnchors,currentAnchors;
 std::vector<btTransform> anchorLive,anchorPreviousInverse,anchorCurrentInverse; // frame-owned scratch
 uint64_t surfaceCorrections=0,surfaceRecoveries=0;double surfaceGuardMs=0;
 uint64_t stretchCorrections=0,stretchContactClamps=0;double stretchGuardMs=0;
 // Running totals (tick-owned) so a measurement window can divide by ticks.
 double physicsTotalMs=0,tickTotalMs=0,guardTotalMs=0,tickCpuTotalMs=0;
 // Bullet object -> index into `bodies`, for pair-cache lookups.
 std::unordered_map<const btCollisionObject*,int> bodyLookup;
 unsigned tuningVersionApplied=0;bool stretchEnabled=false;float stretchToleranceScale=1;
 btVector3 authoredGravity{0,-9.8f,0};
 bool v2=false,async=false;unsigned sleepVersionApplied=0;
 // Asynchronous scheduling state. `mutex` guards the queue, `running`,
 // `stopping`, the pending collision mode and the published state.
 mutable std::mutex mutex;std::condition_variable idle;std::deque<Input> queue;bool running=false,stopping=false,collisionPending=false;unsigned pendingCollisionFlags=0;
 // The flags the simulation runs with: `collisionFlags` is the latest request,
 // applied between ticks by whichever thread owns the world at that moment.
 std::atomic<unsigned> effectiveCollisionFlags{0};
 PoseArrays tickPose;std::vector<btTransform> tickSourcePose;std::vector<float> tickImpulseWeights;
 struct Solved {
  std::vector<btTransform> current,previous;btTransform currentRoot=btTransform::getIdentity(),previousRoot=btTransform::getIdentity();
  std::vector<btTransform> currentAnchors,previousAnchors;
  uint64_t surfaceCorrections=0,surfaceRecoveries=0;double surfaceGuardMs=0;
  uint64_t stretchCorrections=0,stretchContactClamps=0;double stretchGuardMs=0;
  int iterations=10;double physicsTotalMs=0,tickTotalMs=0,guardTotalMs=0,tickCpuTotalMs=0;
  std::vector<std::array<float,3>> motion; // speed, spin, Bullet activation state per body
  double simulationTime=0,inputTime=0,accumulator=0,dropped=0,physicsMs=0,sceneMs=0,poseMs=0,lastMs=0,submittedTime=0;
  uint64_t ticks=0,resets=0,sceneSequence=0;unsigned steps=0,sleeping=0,contacts=0,worldContacts=0,objectContacts=0;size_t mirrors=0;double captureMs=0,syncMs=0;
  std::string resetReason;Json broadphase;
  std::vector<btQuaternion> springPrevious,springCurrent;uint64_t springColliderHits=0,springWorldHits=0,springSteps=0;double springMs=0;
 };
 std::shared_ptr<const Solved> published,presented;
 // Render-thread presentation state: the last few distinct simulation states
 // (newest last, one reset epoch), the render-side input clock, and the display
 // margin behind one tick that absorbs how far the worker runs behind.
 std::deque<std::shared_ptr<const Solved>> presentHistory;
 double presentMargin=0,presentClock=-1;
 std::array<float,32> presentLag{};unsigned presentLagIndex=0; // recent worker lag samples (seconds)
 FrameStats statsFrom(const Solved&) const;
 void publishSolved(unsigned steps,double startedMs,double poseMs,double submittedTime);
 void runAsync();
 void tickAsync(const Input&);
 void resetTick(const Input&);
 void applyCollisionFlags(unsigned flags);
 unsigned advance(std::vector<btTransform>& sourcePose,const std::function<const std::vector<btTransform>&()>& evaluateSkin);
 void applyImpulses(const std::vector<float>& weights,std::vector<float>& last);
 void applySleepPolicy();
 void applyTuning();
 void stabilizeJoints();
 void buildAttachments();
 void readSolved(bool initial,const btTransform& root,const std::vector<btTransform>& skin);
 void present();
 void follow(bool all,const std::vector<btTransform>& skin);
 void beginFollowers(btScalar seconds);
 void endFollowers();
 void followSoft(bool all,const std::vector<btTransform>& skin);
 void clear();
 Json diagnosticsFrom(bool detailed,const std::vector<btTransform>& current,const Json& broadphase,const FrameStats& stats,const std::vector<std::array<float,3>>* motion=nullptr)const;
};
}
