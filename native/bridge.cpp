#include "compatibility.hpp"
#include "bridge.hpp"
#include "scene.hpp"
#include <chrono>
#include <windows.h>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <sstream>
#include <map>
#include "mathlib/polyhedron.h"
namespace mmd {
namespace {
// VPhysics031 methods by slot, at the running layout (see appSystemShift):
// GetActiveEnvironmentByIndex and FindCollisionSet.
size_t physicsShift=0;size_t activeEnvironmentSlot(){return 11-physicsShift;}size_t collisionSetSlot(){return 15-physicsShift;}
// IPhysicsCollision and IPhysicsObject methods at the running layout (olderPhysicsLayout).
bool olderPhysics=false;size_t collisionMethod(size_t compiled){return collisionSlot(compiled,olderPhysics);}size_t objectMethod(size_t compiled){return physicsObjectSlot(compiled,olderPhysics);}
HMODULE module=nullptr;uintptr_t base=0;void* physics=nullptr;void* environment=nullptr;void* collision=nullptr;std::string fingerprint;
// objectTable / objectPosition ABI guards as addresses (every scene object is checked each tick).
void* objectTableGuard=nullptr;void* objectPositionGuard=nullptr;
struct V {float x=0,y=0,z=0;};
struct Tracked {uint64_t id;void* collide;float radius;V center;};
std::unordered_map<void*,Tracked> tracked;
uint64_t nextMirror=0x10000000;
Json xyz(V v){return {v.x,v.y,v.z};}
V subtract(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
bool finite(V v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
template<typename R,typename... Args> R call(void* object,size_t slot,Args... args){auto table=*reinterpret_cast<void***>(object);auto f=reinterpret_cast<R(*)(void*,Args...)>(table[slot]);return f(object,args...);}
uintptr_t rva(void* object,size_t slot){return reinterpret_cast<uintptr_t>((*reinterpret_cast<void***>(object))[slot])-base;}
void initialize(){if(physics){auto current=call<void*>(physics,activeEnvironmentSlot(),0);if(current==environment&&current)return;clearPhysicsBridge();}module=GetModuleHandleW(L"vphysics.dll");if(!module)throw std::runtime_error("VPhysics is not loaded");wchar_t filename[32768];GetModuleFileNameW(module,filename,32768);fingerprint=requireGameBinary(L"vphysics.dll")["observedSHA256"].get<std::string>();

    base=reinterpret_cast<uintptr_t>(module);auto factory=reinterpret_cast<void*(*)(const char*,int*)>(GetProcAddress(module,"CreateInterface"));physics=factory("VPhysics031",nullptr);collision=factory("VPhysicsCollision007",nullptr);if(!physics||!collision)throw std::runtime_error("Physics factory unavailable");physicsShift=appSystemShift(physics,L"vphysics.dll",PhysicsVtableLength);olderPhysics=olderPhysicsLayout(physics,collision);requireOwnedSlots(physics,L"vphysics.dll",{activeEnvironmentSlot(),collisionSetSlot()});requireOwnedSlots(collision,L"vphysics.dll",{collisionMethod(8),collisionMethod(14),collisionMethod(16),collisionMethod(41),collisionMethod(42),collisionMethod(43),collisionMethod(44)});requireAbiRva(L"vphysics.dll","physics",rva(physics,activeEnvironmentSlot()));environment=call<void*>(physics,activeEnvironmentSlot(),0);if(!environment||!matchesAbiRva(L"vphysics.dll","environment",rva(environment,47))){environment=nullptr;throw std::runtime_error("Physics environment not initialized or wrong ABI");}
    // An unverified game build has no pinned object guards: checked() learns them from the first scene object.
    const auto& guards=requireGameBinary(L"vphysics.dll").at("guards");auto pinned=[&](const char* guard)->void*{return guards.contains(guard)?reinterpret_cast<void*>(base+guards.at(guard).get<uintptr_t>()):nullptr;};
    objectTableGuard=pinned("objectTable");objectPositionGuard=pinned("objectPosition");
}
}
Json probePhysics(){initialize();Json j={{"sha256",fingerprint},{"environmentVtable",reinterpret_cast<uintptr_t>(*reinterpret_cast<void***>(environment))-base},{"objects",Json::array()},{"collisionVtable",Json::array()}};
    int count=0;auto objects=call<void**>(environment,47,&count);if(count<0||count>100000)throw std::runtime_error("Invalid native object count");j["count"]=count;
    std::set<uintptr_t> tables;for(int i=0;i<count;i++){auto t=reinterpret_cast<uintptr_t>(*reinterpret_cast<void***>(objects[i]));if(!tables.insert(t).second)continue;Json obj={{"vtable",t-base},{"methods",Json::array()}};for(size_t k=0;k<90;k++)obj["methods"].push_back(rva(objects[i],k));j["objects"].push_back(obj);}
    for(size_t k=0;k<60;k++)j["collisionVtable"].push_back(rva(collision,k));return j;
}
Json probeCarrierCollisions(unsigned modelIndex){
    initialize();if(modelIndex==0||modelIndex>65535)throw std::runtime_error("Invalid model index");
    // Read the engine's actual parsed collision set, not our serialized intent.
    // VPhysics031::FindCollisionSet and IPhysicsCollisionSet::ShouldCollide.
    auto set=call<void*>(physics,collisionSetSlot(),modelIndex);if(!set)throw std::runtime_error("No native collision set for this model");
    Json pairs=Json::array();for(int a=0;a<18;a++)for(int b=a+1;b<18;b++)if(call<bool>(set,2,a,b))pairs.push_back({a,b});
    return {{"modelIndex",modelIndex},{"enabledPairs",pairs},{"count",pairs.size()}};
}
Bytes carrierPhysics(const Rig& rig){
    initialize();if(rig.bodies.size()!=18)throw std::runtime_error("A carrier must have 18 physics bodies");
    // Slots below are VPhysicsCollision007 (the DLL fingerprint is verified above).
    Bytes result(16);auto integer=[&](size_t p,uint32_t v){std::memcpy(result.data()+p,&v,4);};
    integer(0,16);integer(8,18);integer(12,uint32_t(std::stoul(rig.key.substr(0,8),nullptr,16)));
    for(size_t index=0;index<rig.bodies.size();index++){
        const auto& body=rig.bodies[index];
        // IVP's point/plane builders coarsen small hulls even with a tiny merge
        // tolerance. Supply the complete convex topology so limb surfaces survive
        // Source's serialization. This ABI is guarded by initialize()'s DLL hash.
        struct OwnedPolyhedron final : CPolyhedron { void Release() override {} } poly;
        std::vector<Vector> vertices;for(auto v:body.hull)vertices.emplace_back(v.x(),v.y(),v.z());
        std::vector<Polyhedron_IndexedLine_t> lines;
        std::vector<Polyhedron_IndexedLineReference_t> indices;
        std::vector<Polyhedron_IndexedPolygon_t> polygons;
        std::map<std::pair<unsigned short,unsigned short>,unsigned short> edges;
        btVector3 center(0,0,0);for(auto v:body.hull)center+=v;center/=float(body.hull.size());
        for(const auto& face:rig.manifest["bodies"][index]["faces"]){
            auto ids=face.get<std::vector<unsigned short>>();if(ids.size()<3)throw std::runtime_error("Invalid fitted collision face");
            for(auto id:ids)if(id>=body.hull.size())throw std::runtime_error("Invalid fitted collision vertex index");
            auto a=body.hull[ids[0]];btVector3 n(0,0,0);
            for(size_t k=1;k+1<ids.size();k++){n=(body.hull[ids[k]]-a).cross(body.hull[ids[k+1]]-a);if(n.length2()>1e-12f)break;}
            if(n.length2()<=1e-12f)throw std::runtime_error("Degenerate fitted collision face");
            n.normalize();if(n.dot(a-center)<0){n=-n;std::reverse(ids.begin(),ids.end());}
            // Source polyhedra use clockwise vertex traversal with outward normals.
            // Opposite winding can trace correctly yet pass through other bodies.
            std::reverse(ids.begin(),ids.end());
            polygons.push_back({static_cast<unsigned short>(indices.size()),static_cast<unsigned short>(ids.size()),Vector(n.x(),n.y(),n.z())});
            for(size_t k=0;k<ids.size();k++){
                auto u=ids[k],v=ids[(k+1)%ids.size()];auto edge=std::minmax(u,v);auto key=std::make_pair(edge.first,edge.second);
                auto [it,added]=edges.emplace(key,static_cast<unsigned short>(lines.size()));if(added)lines.push_back({{key.first,key.second}});
                // The reference selects the END of the directed edge, not its start.
                indices.push_back({it->second,static_cast<unsigned char>(v==key.first?0:1)});
            }
        }
        poly.pVertices=vertices.data();poly.pLines=lines.data();poly.pIndices=indices.data();poly.pPolygons=polygons.data();
        poly.iVertexCount=static_cast<unsigned short>(vertices.size());poly.iLineCount=static_cast<unsigned short>(lines.size());poly.iIndexCount=static_cast<unsigned short>(indices.size());poly.iPolygonCount=static_cast<unsigned short>(polygons.size());
        void* convex=call<void*>(collision,collisionMethod(8),&poly);if(!convex)throw std::runtime_error("VPhysics rejected fitted collision topology");
        void* collide=call<void*>(collision,collisionMethod(14),&convex,1);if(!collide)throw std::runtime_error("VPhysics convex conversion failed");
        // Validate the actual engine hull before caching a .phy. Surface support
        // tests tolerate triangulation changes without accepting simplification.
        try {
            void* query=call<void*>(collision,collisionMethod(43),collide);if(!query)throw std::runtime_error("VPhysics collision query failed");
            float error=0;
            try {
                if(call<int>(query,1)!=1)throw std::runtime_error("Expected one convex per physics body");
                int count=call<int>(query,2,0);if(count<4||count>256)throw std::runtime_error("Unexpected collision triangle count");
                std::vector<btVector3> actual;actual.reserve(count*3);
                for(int t=0;t<count;t++){V tri[3];call<void>(query,4,0,t,tri);for(auto v:tri)actual.emplace_back(v.x,v.y,v.z);}
                for(size_t t=0;t<actual.size();t+=3)if((actual[t+1]-actual[t]).cross(actual[t+2]-actual[t]).dot(actual[t]-center)>1e-6f)throw std::runtime_error("VPhysics collision winding is inverted");
                auto check=[&](const btVector3& a,const btVector3& b,const btVector3& c,const auto& points){
                    auto n=(b-a).cross(c-a);if(n.length2()<1e-12f)return;n.normalize();if(n.dot(a-center)<0)n=-n;
                    for(const auto& v:points)error=std::max(error,n.dot(v-a));
                };
                for(size_t t=0;t<actual.size();t+=3)check(actual[t],actual[t+1],actual[t+2],body.hull);
                for(const auto& face:rig.manifest["bodies"][index]["faces"])for(size_t k=1;k+1<face.size();k++)check(body.hull[face[0].get<size_t>()],body.hull[face[k].get<size_t>()],body.hull[face[k+1].get<size_t>()],actual);
            }catch(...){call<void>(collision,collisionMethod(44),query);throw;}
            call<void>(collision,collisionMethod(44),query);
            if(error>.01f*rig.scale/ScmiSourceUnitsPerPmx+.0001f)throw std::runtime_error("VPhysics distorted body "+std::to_string(index)+" by "+std::to_string(error)+" Source units");
        }catch(...){call<void>(collision,collisionMethod(16),collide);throw;}
        try {int n=call<int>(collision,collisionMethod(17),collide);if(n<16||n>4*1024*1024)throw std::runtime_error("Invalid serialized collision size");size_t at=result.size();result.resize(at+4+n);integer(at,n);int written=call<int>(collision,collisionMethod(18),result.data()+at+4,collide,false);if(written!=n)throw std::runtime_error("Collision serialization size mismatch");}
        catch(...){call<void>(collision,collisionMethod(16),collide);throw;}call<void>(collision,collisionMethod(16),collide);
    }
    std::ostringstream kv;float bias=0;for(auto& b:rig.bodies)bias+=b.massBias;
    for(size_t i=0;i<rig.bodies.size();i++){auto& b=rig.bodies[i];kv<<"solid {\n\"index\" \""<<i<<"\"\n\"name\" \""<<rig.bones[b.bone].name<<"\"\n";if(b.parent>=0)kv<<"\"parent\" \""<<rig.bones[rig.bodies[b.parent].bone].name<<"\"\n";
        kv<<"\"mass\" \""<<rig.mass*b.massBias/bias<<"\"\n\"surfaceprop\" \"flesh\"\n\"damping\" \"0.8\"\n\"rotdamping\" \""<<b.rotationDamping<<"\"\n\"inertia\" \"12\"\n}\n";
    }
    for(size_t i=1;i<rig.bodies.size();i++){auto& b=rig.bodies[i];kv<<"ragdollconstraint {\n\"parent\" \""<<b.parent<<"\"\n\"child\" \""<<i<<"\"\n";for(int k=0;k<3;k++){char axis='x'+char(k);kv<<'"'<<axis<<"min\" \""<<b.lower[k]<<"\"\n\""<<axis<<"max\" \""<<b.upper[k]<<"\"\n\""<<axis<<"friction\" \"0\"\n";}kv<<"}\n";}
    // Source treats the presence of selfcollisions as OFF, regardless of value.
    // Enabled pairs must appear without that key (CRagdollCollisionRules).
    kv<<"collisionrules {\n";for(size_t a=0;a<18;a++)for(size_t b=a+1;b<18;b++)if(rig.bodies[b].parent!=int(a)&&rig.bodies[a].parent!=int(b))kv<<"\"collisionpair\" \""<<a<<","<<b<<"\"\n";kv<<"}\neditparams {\n\"rootname\" \"ValveBiped.Bip01_Pelvis\"\n\"totalmass\" \""<<rig.mass<<"\"\n}\n";
    auto s=kv.str();result.insert(result.end(),s.begin(),s.end());result.push_back(0);return result;
}
namespace {
struct SceneTracked {uint64_t id;void* collide;float radius;V center;std::shared_ptr<SceneGeometry> geometry;uint64_t seen=0;};
std::unordered_map<void*,SceneTracked> sceneTracked;uint64_t sceneNext=1,sceneSequence=0;
}
void clearSecondaryScene(){sceneTracked.clear();publishScene(nullptr);}
// Every VPhysics object stays tracked (its geometry is read once), but only
// the ones a scene consumer can reach go into the frame: bounds grown by a
// quarter second of their own motion must touch a sceneInterest region. Busy
// maps (animated props' bone followers, ragdolls, builds) used to be read in
// full every tick. Until a consumer registers, everything is captured.
Json captureSecondaryScene(const std::unordered_map<void*,uint64_t>& owners,double timestamp,const std::unordered_set<void*>& excluded){
 auto started=std::chrono::steady_clock::now();initialize();int count=0;auto list=call<void**>(environment,47,&count);
 if(count<0||count>100000)throw std::runtime_error("Invalid scene object count");
 auto frame=newSceneFrame();frame->sequence=++sceneSequence;frame->timestamp=timestamp;const uint64_t generation=frame->sequence;
 unsigned triangles=0,excludedLiving=0,outside=0;
 const auto regions=sceneInterest();
 auto reachable=[&](const btVector3& c,const btVector3& e){
  if(regions.empty())return true;
  for(auto& r:regions)if(c.x()+e.x()>=r.lower.x()&&c.x()-e.x()<=r.upper.x()&&c.y()+e.y()>=r.lower.y()&&c.y()-e.y()<=r.upper.y()&&c.z()+e.z()>=r.lower.z()&&c.z()-e.z()<=r.upper.z())return true;
  return false;
 };
 auto checked=[&](void* object){auto table=*reinterpret_cast<void***>(object);
  if(!objectTableGuard&&matchesAbiRva(L"vphysics.dll","objectTable",reinterpret_cast<uintptr_t>(table)-base)&&matchesAbiRva(L"vphysics.dll","objectPosition",reinterpret_cast<uintptr_t>(table[objectMethod(48)])-base)){objectTableGuard=table;objectPositionGuard=table[objectMethod(48)];}
  if(table!=objectTableGuard||table[objectMethod(48)]!=objectPositionGuard)throw std::runtime_error("Unsupported scene physics object vtable");};
 // Players can own both standing and crouched shadow bodies; Lua may expose
 // only the active one. GetGameData is the supported VPhysics owner link.
 // Excluded objects are only dereferenced when this environment lists them.
 std::unordered_set<void*> excludedOwners;
 if(!excluded.empty())for(int i=0;i<count;i++)if(excluded.contains(list[i])){checked(list[i]);if(auto owner=call<void*>(list[i],objectMethod(17)))excludedOwners.insert(owner);}
 frame->objects.reserve(size_t(std::min(count,1024)));
 for(int i=0;i<count;i++){void* object=list[i];checked(object);
  auto it=sceneTracked.find(object);if(it!=sceneTracked.end())it->second.seen=generation;
  if(!excludedOwners.empty()&&(excluded.contains(object)||excludedOwners.contains(call<void*>(object,objectMethod(17))))){excludedLiving++;continue;}
  if(call<bool>(object,objectMethod(3))||call<bool>(object,objectMethod(4))||!call<bool>(object,objectMethod(6)))continue;
  bool isStatic=call<bool>(object,objectMethod(1));auto collide=call<void*>(object,objectMethod(74));float radius=call<float>(object,objectMethod(42));V center;call<void>(object,objectMethod(45),&center);
  if(it!=sceneTracked.end()&&(it->second.collide!=collide||it->second.radius!=radius)){sceneTracked.erase(it);it=sceneTracked.end();}
  if(it==sceneTracked.end()){
   auto g=newSceneGeometry();g->minimum={BT_LARGE_FLOAT,BT_LARGE_FLOAT,BT_LARGE_FLOAT};g->maximum=-g->minimum;
   auto append=[&](V value){value=subtract(value,center);if(!finite(value))throw std::runtime_error("Invalid scene geometry");btVector3 v(value.x,value.y,value.z);g->vertices.push_back(v);g->minimum.setMin(v);g->maximum.setMax(v);};
   if(radius>0){g->kind=SceneGeometry::Sphere;g->radius=radius;g->minimum={-radius,-radius,-radius};g->maximum=-g->minimum;}
   else if(collide&&isStatic){V* vertices=nullptr;int n=call<int>(collision,collisionMethod(41),collide,&vertices);if(n<0||n>12000000||n%3)throw std::runtime_error("Invalid scene triangle buffer");for(int i=0;i<n;i++)append(vertices[i]);if(vertices)call<void>(collision,collisionMethod(42),n,vertices);if(!n)continue;g->kind=SceneGeometry::Triangles;triangles+=n/3;}
   else if(collide){auto query=call<void*>(collision,collisionMethod(43),collide);if(!query)continue;try{int n=call<int>(query,1);if(n<0||n>8192)throw std::runtime_error("Invalid scene convex count");for(int h=0;h<n;h++){int t=call<int>(query,2,h);if(t<0||t>100000)throw std::runtime_error("Invalid scene convex triangles");if(!t)continue;g->hullCounts.push_back(t*3);for(int j=0;j<t;j++){V v[3];call<void>(query,4,h,j,v);for(auto p:v)append(p);}triangles+=t;}}catch(...){call<void>(collision,collisionMethod(44),query);throw;}call<void>(collision,collisionMethod(44),query);if(g->vertices.empty())continue;g->kind=SceneGeometry::Convexes;}
   else continue;
   it=sceneTracked.emplace(object,SceneTracked{sceneNext++,collide,radius,center,std::move(g),generation}).first;
  }
  float matrix[12];call<void>(object,objectMethod(49),matrix);for(float v:matrix)if(!std::isfinite(v))throw std::runtime_error("Non-finite scene transform");
  btMatrix3x3 rotation(matrix[0],matrix[1],matrix[2],matrix[4],matrix[5],matrix[6],matrix[8],matrix[9],matrix[10]);
  btTransform transform(rotation,rotation*btVector3(center.x,center.y,center.z)+btVector3(matrix[3],matrix[7],matrix[11]));
  V linear,angular;call<void>(object,objectMethod(52),&linear,&angular);if(!finite(linear)||!finite(angular))throw std::runtime_error("Non-finite scene velocity");
  const auto& g=*it->second.geometry;
  if(!reachable(transform*((g.minimum+g.maximum)*.5f),rotation.absolute()*((g.maximum-g.minimum)*.5f)+btVector3(linear.x,linear.y,linear.z).absolute()*.25f)){outside++;continue;}
  SceneObject item;item.id=it->second.id;item.geometry=it->second.geometry;item.isStatic=isStatic;item.transform=transform;
  if(auto owner=owners.find(object);owner!=owners.end()){item.owner=owner->second>>16;item.physicsBone=int(owner->second&65535);frame->ownedObjects++;}
  item.localCenter={center.x,center.y,center.z};item.velocity={linear.x,linear.y,linear.z};item.angular={angular.x,angular.y,angular.z};frame->objects.push_back(std::move(item));
 }
 for(auto it=sceneTracked.begin();it!=sceneTracked.end();)if(it->second.seen!=generation)it=sceneTracked.erase(it);else ++it;
 frame->captureMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();publishScene(frame);
 return {{"excludedLivingObjects",excludedLiving},{"objects",frame->objects.size()},{"sourceObjects",count},{"outsideInterest",outside},{"interestRegions",regions.size()},{"ownedObjects",frame->ownedObjects},{"captureMs",frame->captureMs},{"newTriangles",triangles},{"sequence",frame->sequence},{"feedbackApplied",0}};
}
Json capturePhysics(){
    initialize();int count=0;auto list=call<void**>(environment,47,&count);if(count<0||count>100000)throw std::runtime_error("Invalid native object count");
    std::set<void*> alive(list,list+count);auto& w=world();
    for(auto it=tracked.begin();it!=tracked.end();)if(!alive.contains(it->first)){w.removeMirror(it->second.id);it=tracked.erase(it);}else ++it;
    std::unordered_map<uint64_t,void*> targets;for(auto& [object,t]:tracked)targets[t.id]=object;
    auto impulses=w.takeImpulses();unsigned applied=0,added=0,skipped=0,triangles=0;
    for(auto& entry:impulses){auto it=targets.find(entry["id"]);if(it==targets.end())continue;void* object=it->second;requireAbiRva(L"vphysics.dll","objectTable",reinterpret_cast<uintptr_t>(*reinterpret_cast<void***>(object))-base);
        if(!call<bool>(object,objectMethod(10))||!call<bool>(object,objectMethod(6)))continue;
        auto a=entry["linear"],b=entry["angular"];V linear{a[0],a[1],a[2]},angular{b[0],b[1],b[2]};
        if(!finite(linear)||!finite(angular))throw std::runtime_error("Rejected non-finite Bullet feedback before Source physics");
        call<void>(object,objectMethod(60),&linear);call<void>(object,objectMethod(62),&angular);applied++;
    }
    for(void* object:alive){
        if(!matchesAbiRva(L"vphysics.dll","objectTable",reinterpret_cast<uintptr_t>(*reinterpret_cast<void***>(object))-base)||!matchesAbiRva(L"vphysics.dll","objectPosition",rva(object,objectMethod(48)))||!matchesAbiRva(L"vphysics.dll","objectForce",rva(object,objectMethod(60))))throw std::runtime_error("Unsupported physics object vtable");
        bool isStatic=call<bool>(object,objectMethod(1)),moveable=call<bool>(object,objectMethod(10));
        if(call<bool>(object,objectMethod(3))||call<bool>(object,objectMethod(4))||!call<bool>(object,objectMethod(6))){
            auto it=tracked.find(object);if(it!=tracked.end()){w.removeMirror(it->second.id);tracked.erase(it);}skipped++;continue;
        }
        void* collide=call<void*>(object,objectMethod(74));float radius=call<float>(object,objectMethod(42));V center;call<void>(object,objectMethod(45),&center);
        auto it=tracked.find(object);if(it!=tracked.end()&&(it->second.collide!=collide||it->second.radius!=radius||!w.mirrors.contains(it->second.id))){w.removeMirror(it->second.id);tracked.erase(it);it=tracked.end();}
        Json spec;std::vector<float> geometry;
        float mass=moveable&&!isStatic?call<float>(object,objectMethod(29)):0;
        V inertia,velocity,angular;call<void>(object,objectMethod(31),&inertia);call<void>(object,objectMethod(52),&velocity,&angular);
        if(!finite(inertia)||!finite(velocity)||!finite(angular)||!std::isfinite(mass))throw std::runtime_error("Source supplied non-finite physics state");
        float matrix[12];call<void>(object,objectMethod(49),matrix);
        btMatrix3x3 basis(matrix[0],matrix[1],matrix[2],matrix[4],matrix[5],matrix[6],matrix[8],matrix[9],matrix[10]);btQuaternion q;basis.getRotation(q);
        auto com=basis*btVector3(center.x,center.y,center.z)+btVector3(matrix[3],matrix[7],matrix[11]);
        spec={{"mass",mass},{"inertia",xyz(inertia)},{"velocity",xyz(velocity)},{"angularVelocity",xyz(angular)},{"position",{com.x(),com.y(),com.z()}},{"rotation",{q.x(),q.y(),q.z(),q.w()}}};
        if(it==tracked.end()){
            auto append=[&](V v){v=subtract(v,center);if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw std::runtime_error("Invalid Source collision coordinate");geometry.insert(geometry.end(),{v.x,v.y,v.z});};
            if(radius>0){spec["shape"]="sphere";spec["radius"]=radius;}
            else if(collide&&isStatic){
                V* vertices=nullptr;int n=call<int>(collision,collisionMethod(41),collide,&vertices);
                if(n<0||n>12000000||n%3)throw std::runtime_error("Invalid Source triangle buffer");
                for(int k=0;k<n;k++)append(vertices[k]);if(vertices)call<void>(collision,collisionMethod(42),n,vertices);
                if(!n){skipped++;continue;}spec["shape"]="triangles";triangles+=n/3;
            }else if(collide){
                void* query=call<void*>(collision,collisionMethod(43),collide);if(!query){skipped++;continue;}
                try{int hulls=call<int>(query,1);if(hulls<0||hulls>8192)throw std::runtime_error("Invalid convex count");spec["shape"]="compound";spec["hulls"]=Json::array();
                    for(int h=0;h<hulls;h++){int n=call<int>(query,2,h);if(n<0||n>100000)throw std::runtime_error("Invalid convex triangle count");if(!n)continue;spec["hulls"].push_back(n*3);for(int k=0;k<n;k++){V vertices[3];call<void>(query,4,h,k,vertices);for(auto v:vertices)append(v);}triangles+=n;}
                }catch(...){call<void>(collision,collisionMethod(44),query);throw;}call<void>(collision,collisionMethod(44),query);if(geometry.empty()){skipped++;continue;}
            }else {skipped++;continue;}
            Tracked t{nextMirror++,collide,radius,center};w.setMirror(t.id,spec,geometry);tracked.emplace(object,t);added++;
        }else w.setMirror(it->second.id,spec);
    }
    return {{"sourceObjects",count},{"mirrors",tracked.size()},{"added",added},{"skipped",skipped},{"trianglesAdded",triangles},{"feedbackApplied",applied},{"sha256",fingerprint}};
}
void clearPhysicsBridge(){clearSecondaryScene();for(auto& [object,t]:tracked)world().removeMirror(t.id);tracked.clear();environment=physics=collision=nullptr;module=nullptr;base=0;}
}
