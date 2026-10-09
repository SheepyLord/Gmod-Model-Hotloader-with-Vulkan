#pragma once
#include "runtime.hpp"
#include <map>
namespace mmd {
// The on-disk formats use 32-bit offsets even in the 64-bit engine.
constexpr int RigVersion=3;
constexpr int RigGenerator=31;
struct RigBone { std::string name; int parent=-1,mmd=-1,physics=-1; std::vector<int> aliases; btTransform rest=btTransform::getIdentity(); };
struct RigBody { int bone=-1,parent=-1; std::vector<btVector3> hull; float confidence=0,massBias=1,rotationDamping=3; btVector3 lower{0,0,0},upper{0,0,0}; };
struct Rig { std::string key,path; float scale=1,mass=70; std::vector<RigBone> bones; std::vector<RigBody> bodies; Json morphs,manifest; };
// Mesh-to-carrier bind, in Source units. The actor origin is a fixed world
// unit offset (SCMI $origin), not a multiplier on the PMX skeleton.
inline btTransform rigMeshBind(const Rig& rig){
 return btTransform(btQuaternion(btVector3(0,0,1),rig.manifest.value("meshYaw",0.f)*SIMD_RADS_PER_DEG),
                    btVector3(0,0,rig.manifest.value("actorOrigin",0.f)));
}
Rig fitRig(const Model&,const Json& options);
Rig rigFromManifest(const Json&);
// Throws unless every index and transform of the rig is usable with this model.
void validateRig(const Rig&,const Model&);
// The import-time fit, cached per asset: {"ok":true}, or {"ok":false,"errorCode",
// "error","missing":[carrier names]} (the import result's "fit" block).
Json prepareModelFit(Model&,const fs::path& cache);
std::map<std::string,Bytes> carrierFiles(const Rig&,const Model* armsModel=nullptr);
Bytes makeGma(const std::map<std::string,Bytes>&,const std::string& title);
// A package entry held in memory, or streamed from `file` when that is set.
struct GmaEntry { Bytes data; fs::path file; };
// Writes the same bytes as makeGma without loading the file entries into memory.
void writeGma(const fs::path&,const std::map<std::string,GmaEntry>&,const std::string& title);
void prepareSourceMaterials(const fs::path&,const std::string&);
Json packageCarrier(const fs::path& cache,const Rig&,Bytes physics);
}
