#pragma once
#include "rig.hpp"
#include "rig_writer.hpp"
namespace mmd {
Json readAnimationModel(std::span<const unsigned char> bytes);
void configureAnimations(Rig&,const Json& options);
void writeAnimations(StudioWriter&,const Rig&,size_t bones,const btVector3& lo,const btVector3& hi);
}
