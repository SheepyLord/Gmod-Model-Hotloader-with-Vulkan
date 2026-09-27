#pragma once
#include <LinearMath/btVector3.h>
#include <algorithm>
#include <cmath>

namespace mmd {
// Compare character travel with the inward child -> ValveBiped vector. A
// front-mounted part has theta=pi while travelling forward, so it cannot lag
// backwards into its parent. Rear-mounted parts may trail outwards normally.
inline float jiggleDirectionScale(const btVector3& inward,const btVector3& movement){
 const float product=inward.length2()*movement.length2();
 if(product<1e-12f)return 1.f; // stationary, coincident pivot or absent direction
 const float dot=std::clamp(float(inward.dot(movement)/std::sqrt(product)),-1.f,1.f);
 static const float cutoff=std::cos(1.33f*SIMD_HALF_PI);
 if(dot<=cutoff)return 0.f;
 if(dot>=1.f-1e-6f)return 1.f;
 return std::max(0.f,std::cos(std::acos(dot)/1.33f));
}
}
