#pragma once
#include "rig.hpp"
#include "rig_writer.hpp"
namespace mmd {
std::vector<uint8_t> firstPersonTriangles(const Model&,const Rig&,bool arms);
void writeArmsGeometry(StudioWriter& mdl,StudioWriter& vvd,StudioWriter& vtx,const Rig&,const Model&);
}
