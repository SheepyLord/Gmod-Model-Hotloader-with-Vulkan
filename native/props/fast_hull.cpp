#include "core.hpp"
#include <algorithm>
#include <limits>
#include <map>

namespace props {
namespace {
struct D {
    double x{},y{},z{};
    D operator+(D b)const{return{x+b.x,y+b.y,z+b.z};}
    D operator-(D b)const{return{x-b.x,y-b.y,z-b.z};}
    D operator*(double s)const{return{x*s,y*s,z*s};}
    double dot(D b)const{return x*b.x+y*b.y+z*b.z;}
    D cross(D b)const{return{y*b.z-z*b.y,z*b.x-x*b.z,x*b.y-y*b.x};}
    double length()const{return std::sqrt(dot(*this));}
};
struct Plane{D n;double distance;};
Hull triangulateRounded(const std::vector<Vec>& input,D center,double radius,double epsilon){
    std::vector<Vec> vertices;std::vector<D> p;
    for(auto v:input){
        bool duplicate=false;for(auto old:vertices)if(old.x==v.x&&old.y==v.y&&old.z==v.z){duplicate=true;break;}
        if(!duplicate){vertices.push_back(v);p.push_back((D{v.x,v.y,v.z}-center)*(1/radius));}
    }
    if(p.size()<4)throw std::runtime_error("Single hull collapsed at float precision; recenter or enlarge the source model");
    size_t a=0,b=0,c=0,d=0;double best=0;
    for(size_t i=0;i<p.size();++i)for(size_t j=i+1;j<p.size();++j){double value=(p[i]-p[j]).dot(p[i]-p[j]);if(value>best){best=value;a=i;b=j;}}
    best=0;for(size_t i=0;i<p.size();++i){auto n=(p[b]-p[a]).cross(p[i]-p[a]);double value=n.dot(n);if(value>best){best=value;c=i;}}
    auto n=(p[b]-p[a]).cross(p[c]-p[a]);best=0;
    for(size_t i=0;i<p.size();++i){double value=std::abs(n.dot(p[i]-p[a]));if(value>best){best=value;d=i;}}
    if(best<1e-15)throw std::runtime_error("Single hull has no usable volume; increase import scale");
    D inside=(p[a]+p[b]+p[c]+p[d])*.25;
    using Face=std::array<size_t,3>;std::vector<Face> faces;
    auto add=[&](size_t x,size_t y,size_t z){if((p[y]-p[x]).cross(p[z]-p[x]).dot(inside-p[x])>0)std::swap(y,z);faces.push_back({x,y,z});};
    add(a,b,c);add(a,d,b);add(a,c,d);add(b,d,c);
    // At most 48 support vertices: incremental hull construction is bounded and
    // retriangulates float rounding into genuinely convex (not warped) faces.
    for(size_t i=0;i<p.size();++i){
        if(i==a||i==b||i==c||i==d)continue;
        std::map<std::pair<size_t,size_t>,std::pair<unsigned,std::pair<size_t,size_t>>> edges;
        std::vector<Face> kept;
        for(auto face:faces){
            auto normal=(p[face[1]]-p[face[0]]).cross(p[face[2]]-p[face[0]]);
            // Points barely outside a face (float noise) are treated as on the hull;
            // adding them would create near-zero-area sliver faces.
            if(normal.dot(p[i]-p[face[0]])<=normal.length()*epsilon){kept.push_back(face);continue;}
            for(int e=0;e<3;++e){auto x=face[e],y=face[(e+1)%3];auto key=std::minmax(x,y);auto& edge=edges[{key.first,key.second}];++edge.first;edge.second={x,y};}
        }
        if(edges.empty())continue;
        faces=std::move(kept);
        for(auto& [key,edge]:edges)if(edge.first==1)add(edge.second.first,edge.second.second,i);
    }
    Hull out;std::vector<uint32_t> remap(vertices.size(),~0u);
    for(auto face:faces)for(auto i:face){if(remap[i]==~0u){remap[i]=uint32_t(out.points.size());out.points.push_back(vertices[i]);}out.indices.push_back(remap[i]);}
    return out;
}
// Points are normalized around center by radius. Nearly coincident corners
// (thin blades, tiny bevels) and barely-outside points produce float faces with
// almost no area; weld them and relax visibility at increasing tolerances.
// Every candidate must pass the same validation as cached hulls.
bool buildHull(const std::vector<D>& points,D center,double radius,Hull& out,std::string& failure){
    const std::pair<double,double> attempts[]={{0.,1e-10},{1e-5,1e-7},{1e-4,1e-6},{1e-3,1e-5},{1e-2,1e-4},{3e-2,1e-3}};
    for(auto [weld,epsilon]:attempts){
        std::vector<D> welded;
        for(auto p:points){bool near=false;for(auto& old:welded)if((old-p).length()<=weld){near=true;break;}if(!near)welded.push_back(p);}
        if(welded.size()<4||welded.size()>64)continue;
        try{
            std::vector<Vec> rounded;for(auto p:welded){p=center+p*radius;rounded.push_back({float(p.x),float(p.y),float(p.z)});}
            auto result=triangulateRounded(rounded,center,radius,epsilon);
            parseHulls(hullJson({result}));
            out=std::move(result);return true;
        }catch(const std::exception& e){failure=e.what();}
    }
    return false;
}
}
Hull hullFromPoints(const std::vector<Vec>& input){
    if(input.size()<4)throw std::runtime_error("Collision piece has too few points");
    D center{},mn{INFINITY,INFINITY,INFINITY},mx{-INFINITY,-INFINITY,-INFINITY};
    for(auto v:input){D p{v.x,v.y,v.z};center=center+p;mn={std::min(mn.x,p.x),std::min(mn.y,p.y),std::min(mn.z,p.z)};mx={std::max(mx.x,p.x),std::max(mx.y,p.y),std::max(mx.z,p.z)};}
    center=center*(1.0/input.size());auto e=mx-mn;double radius=std::max({e.x,e.y,e.z})*.5;
    if(!(radius>1e-7))throw std::runtime_error("Collision piece has no size");
    std::vector<D> points;for(auto v:input)points.push_back((D{v.x,v.y,v.z}-center)*(1/radius));
    Hull out;std::string failure;
    if(!buildHull(points,center,radius,out,failure))throw std::runtime_error("Collision piece is degenerate: "+failure);
    return out;
}

Hull makeFastHull(const Asset& a,const Progress& progress,std::string* note){
    if(a.vertices.empty()||a.indices.empty())throw std::runtime_error("No geometry for collision");
    progress("Building single convex hull",.60f,"Scanning geometry; no decomposition search",0,a.vertices.size());
    // 13 projection axes give 26 supporting planes. The bounded intersection
    // encloses the input and has at most 48 vertices (2*26-4), below Source's cap.
    // Unlike a sampled point hull, thin extremities cannot fall outside it.
    std::vector<D> axes;
    for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z){
        if((x==0&&y==0&&z==0)||x<0||(x==0&&y<0)||(x==0&&y==0&&z<0))continue;
        axes.push_back({double(x),double(y),double(z)});
    }
    std::vector<uint8_t> used(a.vertices.size());
    for(auto i:a.indices){if(i>=used.size())throw std::runtime_error("Invalid collision vertex index");used[i]=1;}
    D center{},mn{INFINITY,INFINITY,INFINITY},mx{-INFINITY,-INFINITY,-INFINITY};size_t count=0;
    for(size_t i=0;i<a.vertices.size();++i)if(used[i]){
        auto v=a.vertices[i].pos;D p{v.x,v.y,v.z};
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))throw std::runtime_error("Non-finite collision geometry");
        center=center+p;++count;
        mn={std::min(mn.x,p.x),std::min(mn.y,p.y),std::min(mn.z,p.z)};
        mx={std::max(mx.x,p.x),std::max(mx.y,p.y),std::max(mx.z,p.z)};
    }
    if(count<3)throw std::runtime_error("At least three collision points are required");
    center=center*(1.0/count);auto extent=mx-mn;
    const double radius=std::max({extent.x,extent.y,extent.z})*.5;
    if(radius<1e-7)throw std::runtime_error("Model is too small for physics; increase import scale");
    if(radius>32768)throw std::runtime_error("Collider is too large for Source physics; reduce import scale");
    // Work near the origin in double precision. The mean lies inside the point
    // hull, so uniformly expanding its support distances always encloses it.
    std::vector<double> lo(axes.size(),INFINITY),hi(axes.size(),-INFINITY);
    for(size_t i=0;i<a.vertices.size();++i)if(used[i]){
        auto v=a.vertices[i].pos;D p=(D{v.x,v.y,v.z}-center)*(1/radius);
        for(size_t k=0;k<axes.size();++k){auto d=axes[k].dot(p);lo[k]=std::min(lo[k],d);hi[k]=std::max(hi[k],d);}
    }
    const double coordinate=std::max({std::abs(mn.x),std::abs(mn.y),std::abs(mn.z),std::abs(mx.x),std::abs(mx.y),std::abs(mx.z)});
    const double precision=coordinate*std::numeric_limits<float>::epsilon()/radius;
    if(precision>.1)throw std::runtime_error("Model is too small relative to its local coordinates; recenter the source model before importing");
    // Flat geometry (cards, sheets, blades) gets a small physical thickness:
    // up to a quarter unit, never more than 2% of the prop's size.
    const double thickness=std::max({2e-5,std::min(.25/radius,.02),.001/radius,precision*16});
    const double expansion=1+std::max(2e-5,precision*16);
    std::vector<Plane> planes;
    for(size_t k=0;k<axes.size();++k){
        double floor=thickness*axes[k].length();
        planes.push_back({axes[k],std::max(hi[k]*expansion,floor)});
        planes.push_back({axes[k]*-1,std::max(-lo[k]*expansion,floor)});
    }
    progress("Building single convex hull",.76f,"Intersecting bounded support planes",count,count);
    std::vector<D> points;
    for(size_t i=0;i<planes.size();++i)for(size_t j=i+1;j<planes.size();++j)for(size_t k=j+1;k<planes.size();++k){
        auto& p=planes[i];auto& q=planes[j];auto& r=planes[k];auto qr=q.n.cross(r.n);double determinant=p.n.dot(qr);
        if(std::abs(determinant)<1e-12)continue;
        auto v=(qr*p.distance+r.n.cross(p.n)*q.distance+p.n.cross(q.n)*r.distance)*(1/determinant);
        bool inside=true;for(auto& plane:planes)if(plane.n.dot(v)>plane.distance+1e-8){inside=false;break;}
        if(!inside)continue;
        bool duplicate=false;for(auto old:points)if((old-v).length()<1e-7){duplicate=true;break;}
        if(!duplicate)points.push_back(v);
    }
    Hull result;std::string failure;
    if(buildHull(points,center,radius,result,failure))return result;
    // Last resort: the thickened bounding box always forms a valid convex solid.
    if(note)*note="The single hull was numerically degenerate ("+(failure.empty()?std::string("too few corners"):failure)+"); a box collider is used instead.";
    auto boxMin=mn,boxMax=mx;const double pad=thickness*radius;
    for(int axis=0;axis<3;++axis){double& low=axis==0?boxMin.x:axis==1?boxMin.y:boxMin.z;double& high=axis==0?boxMax.x:axis==1?boxMax.y:boxMax.z;double mid=(low+high)*.5,half=std::max((high-low)*.5,pad);low=mid-half;high=mid+half;}
    Hull box;
    for(int i=0;i<8;++i)box.points.push_back({float(i&1?boxMax.x:boxMin.x),float(i&2?boxMax.y:boxMin.y),float(i&4?boxMax.z:boxMin.z)});
    // Outward counter-clockwise faces, matching the other colliders.
    box.indices={0,2,1,1,2,3,4,5,6,5,7,6,0,1,4,1,5,4,2,6,3,3,6,7,0,4,2,2,4,6,1,3,5,3,7,5};
    parseHulls(hullJson({box}));
    return box;
}
}
