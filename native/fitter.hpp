#pragma once
#include "rig.hpp"
namespace mmd {
struct ConvexFit {
 std::vector<btVector3> vertices;
 Json faces=Json::array(),regions=Json::array();
 std::vector<float> features;
 btVector3 center{0,0,0},extent{1,1,1};
 float confidence=0,coverage=0,outliers=0;
 bool repaired=false,fallback=false;
 std::string method;
};
ConvexFit fitBody(const std::string&,const std::vector<btVector3>&,float stature,float length);
void convexTopology(ConvexFit&);
const std::string& shapeAtlasHash();
}
