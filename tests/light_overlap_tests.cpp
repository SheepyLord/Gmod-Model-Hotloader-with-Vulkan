#include "light_overlaps.hpp"
#include "mesh_topology.hpp"
#include <iostream>
using namespace mmd;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
 check(remixLayerSeparation(0,3)==0,"Base layer moved");
 check(remixLayerSeparation(2,3)>remixLayerSeparation(1,3),"Remix layer order inverted");
 check(remixLayerSeparation(1000,1000)<=.05f,"Remix layer allowance is unbounded");
 auto m=parse(readFile(argv[1]));m->vertices.resize(6);m->indices={0,1,2,4,5,3};m->materials.resize(1);
 m->materials[0].first=0;m->materials[0].count=6;
 for(int i=0;i<6;i++){auto& v=m->vertices[i];v.position={float(i%3==1),float(i%3==2),0};v.uv[0]=v.position.x();v.uv[1]=v.position.y();}
 buildLightOverlaps(*m);check(m->lightOverlaps.size()==1,"Cyclic duplicate not detected");
 Snapshot s;s.vertices.resize(6);
 for(int i=0;i<6;i++){auto& v=m->vertices[i];auto& d=s.vertices[i];d.x=v.position.x();d.y=v.position.y();d.z=v.position.z();d.u=v.uv[0];d.v=v.uv[1];}
 auto mask=lightOverlapMask(*m,s,.001f);
 check(mask.size()==2&&!mask[0]&&mask[1],"Light must retain the last opaque surface");
 check(m->indices.size()==6&&m->vertices.size()==6,"Authored geometry was modified");
 // Subpixel skinning differences used to leave the omitted copy in the base
 // depth buffer, creating dark triangles under an additive light. One owner
 // must be selected even if its normals differ, without changing the mesh.
 s.vertices[3].z=.0002f;s.vertices[3].nx=1;
 mask=lightOverlapMask(*m,s,.001f);
 check(!mask[0]&&mask[1],"Near-coincident surfaces have inconsistent ownership");
 s.vertices[3].z=.1f;mask=lightOverlapMask(*m,s,.001f);
 check(mask[0]&&mask[1],"Separated animation surface disappeared");
 s.vertices[3].z=0;s.vertices[3].u=.1f;mask=lightOverlapMask(*m,s,.001f);
 check(mask[0]&&mask[1],"Independent UV/alpha coverage disappeared");
 s.vertices[3].u=0;std::array<uint8_t,2> view{1,0};mask=lightOverlapMask(*m,s,.001f,view);
 check(mask[0],"Hidden first-person copy suppressed a visible face");
 auto t=batchTopology(*m);
 check(t.chunks[0].triangles==std::vector<unsigned>{0,1},"Batched triangle identities changed");
 std::swap(m->indices[4],m->indices[5]);buildLightOverlaps(*m);
 check(m->lightOverlaps.empty(),"Intentional reverse-wound surface suppressed");
 std::swap(m->indices[4],m->indices[5]);m->materials.resize(2);
 m->materials[0].count=3;m->materials[1].first=3;m->materials[1].count=3;buildLightOverlaps(*m);
 check(m->lightOverlaps.empty(),"Different material layers must remain complete");
 check(m->lightLayerRanks==std::vector<unsigned>{0,1},"Coplanar material order missing");
 if(argc>2){auto real=parse(readFile(argv[2]));std::cout<<"Real model: "<<real->vertices.size()<<" vertices, "<<real->indices.size()/3<<" triangles, "<<real->lightOverlaps.size()<<" overlap candidate groups\n";}
 std::cout<<"Coincident flashlight surfaces, animation/UV separation, winding and visibility passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
