#pragma once
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/BroadphaseCollision/btAxisSweep3.h>
#include <memory>
#include <string>
namespace mmd {
struct BroadphaseConfig {
 bool sap=false;
 bool leafDbvt=false;
 btVector3 minimum{-4000,-4000,-4000},maximum{4000,4000,4000};
 unsigned capacity=8192;
 // Fat-volume margin of the order-preserving `dbvt-fast` tree only; `dbvt`
 // keeps Bullet's global so the reference replay stays exact.
 float margin=.05f;
};
// Only the secondary-world factory changes this thread-local construction scope.
// Direct nanoem C-API worlds always use the pinned DBVT implementation.
class BroadphaseCreationScope {
 BroadphaseConfig previous;
public:
 explicit BroadphaseCreationScope(const BroadphaseConfig&);
 ~BroadphaseCreationScope();
 BroadphaseCreationScope(const BroadphaseCreationScope&)=delete;
 BroadphaseCreationScope& operator=(const BroadphaseCreationScope&)=delete;
};
class SecondaryBroadphase final:public btBroadphaseInterface {
 struct Impl;std::unique_ptr<Impl> impl;
public:
 SecondaryBroadphase();
 explicit SecondaryBroadphase(const BroadphaseConfig&);
 ~SecondaryBroadphase() override;
 std::string mode() const;
 std::string fallbackReason() const;
 unsigned capacity() const;
 btBroadphaseProxy* createProxy(const btVector3&,const btVector3&,int,void*,int,int,btDispatcher*) override;
 void destroyProxy(btBroadphaseProxy*,btDispatcher*) override;
 void setAabb(btBroadphaseProxy*,const btVector3&,const btVector3&,btDispatcher*) override;
 void getAabb(btBroadphaseProxy*,btVector3&,btVector3&) const override;
 void rayTest(const btVector3&,const btVector3&,btBroadphaseRayCallback&,const btVector3& =btVector3(0,0,0),const btVector3& =btVector3(0,0,0)) override;
 void aabbTest(const btVector3&,const btVector3&,btBroadphaseAabbCallback&) override;
 void calculateOverlappingPairs(btDispatcher*) override;
 btOverlappingPairCache* getOverlappingPairCache() override;
 const btOverlappingPairCache* getOverlappingPairCache() const override;
 void getBroadphaseAabb(btVector3&,btVector3&) const override;
 void resetPool(btDispatcher*) override;
 void printStats() override;
};
void setSecondaryBroadphaseDefault(const std::string&);
std::string secondaryBroadphaseDefault();
bool isSapBroadphase(const std::string&);
// Validates a requested name and resolves `auto` for a world that may relax
// the reference pair order (cpu_mt_v2) or must keep it (everything else).
std::string resolveBroadphase(const std::string& requested,bool relaxedOrder);
// Fat margin for v2 `dbvt-fast` worlds (PMX units); applies to worlds created afterwards.
void setSecondaryDbvtMargin(float);
float secondaryDbvtMargin();
}
