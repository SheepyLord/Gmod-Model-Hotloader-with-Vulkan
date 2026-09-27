#include "core.hpp"
#include <algorithm>
#include <limits>

namespace props {
// Continuous SAT for a translating box against a stationary convex polyhedron.
// The box basis vectors include half-extents, expressed in model coordinates.
Hit trace(const std::vector<Hull>& hulls,Vec start,Vec delta,const std::array<Vec,3>& box){
    Hit nearest;
    for(auto& h:hulls){
        float enter=0,leave=nearest.fraction;Vec normal=delta.normalized()*-1;bool miss=false;
        auto test=[&](Vec axis){float len=axis.length();if(len<1e-7f)return;axis=axis*(1/len);float mn=INFINITY,mx=-INFINITY;for(auto p:h.points){float d=p.dot(axis);mn=std::min(mn,d);mx=std::max(mx,d);}float radius=0;for(auto b:box)radius+=std::abs(b.dot(axis));mn-=radius;mx+=radius;float s=start.dot(axis),v=delta.dot(axis);
            if(std::abs(v)<1e-9f){if(s<mn||s>mx)miss=true;return;}
            float t0=(mn-s)/v,t1=(mx-s)/v;Vec n=axis*-1;if(t0>t1){std::swap(t0,t1);n=axis;}if(t0>enter){enter=t0;normal=n;}leave=std::min(leave,t1);if(enter>leave||leave<0)miss=true;
        };
        for(size_t i=0;i+2<h.indices.size()&&!miss;i+=3){Vec p=h.points[h.indices[i]],q=h.points[h.indices[i+1]],r=h.points[h.indices[i+2]];test((q-p).cross(r-p));for(auto b:box){test((q-p).cross(b));test((r-q).cross(b));test((p-r).cross(b));}}
        for(auto b:box)if(!miss)test(b);
        if(!miss&&enter>=0&&enter<=nearest.fraction&&leave>=enter){nearest={enter,normal,true};}
    }return nearest;
}
}
