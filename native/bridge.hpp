#pragma once
#include "rig.hpp"
#include "runtime.hpp"
#include <unordered_set>
namespace mmd { Json probePhysics();Json probeCarrierCollisions(unsigned modelIndex);Json capturePhysics();Json captureSecondaryScene(const std::unordered_map<void*,uint64_t>&,double,const std::unordered_set<void*>&);void clearSecondaryScene();void clearPhysicsBridge();Bytes carrierPhysics(const Rig&); }
