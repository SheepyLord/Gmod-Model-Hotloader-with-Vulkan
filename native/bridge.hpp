#pragma once
#include "rig.hpp"
#include "runtime.hpp"
#include <unordered_set>
namespace mmd { Json probePhysics();Json probeCarrierCollisions(unsigned modelIndex);Json capturePhysics();// `excluded` bodies (and their owners' other shadows) are dropped; `actors` tags
// living players' and NPCs' bodies (SceneObject::Actor) for each world to choose.
Json captureSecondaryScene(const std::unordered_map<void*,uint64_t>& owners,double timestamp,const std::unordered_set<void*>& excluded,const std::unordered_map<void*,uint8_t>& actors={});void clearSecondaryScene();void clearPhysicsBridge();Bytes carrierPhysics(const Rig&); }
