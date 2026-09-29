#pragma once
#include "scene.hpp"
namespace mmd {
struct SceneShare {
 struct Cached {std::weak_ptr<const SceneGeometry> source;std::string key;Bytes bytes;};
 std::map<const SceneGeometry*,Cached> exported;
 std::map<std::string,std::shared_ptr<const SceneGeometry>> imported;
 // consumer identifies the subscriber whose interest region this refreshes;
 // kinds (Collide:: flags) are the objects it asked for: map geometry, objects,
 // living players, living NPCs.
 Json describe(const btVector3& center,float radius,uint64_t consumer=0,unsigned kinds=Collide::All);
 Bytes chunk(const std::string&,size_t offset,size_t count)const;
 void accept(const std::string&,std::span<const unsigned char>);
 void publish(World&,const Json&);
};
}
