#pragma once
// In-memory PMX 2.0 output shared by the converters (VRM avatars, and FBX/glTF/DAE
// characters): UTF-8 text, 4-byte indices, BDEF1/2/4 weights, three display
// frames and no rigid bodies or joints (their physics is simulated natively).
#include "runtime.hpp"
#include <glm/glm.hpp>
#include <array>
namespace mmd {
struct PVertex {glm::vec3 p{0},n{0,1,0};glm::vec2 uv{0};std::array<int,4> bone{-1,-1,-1,-1};std::array<float,4> weight{};};
struct PBone {std::string name,english;glm::vec3 position{0};int parent=-1,tail=-1,inherit=-1;float inheritWeight=0;glm::vec3 tailOffset{0},fixedAxis{0};bool movable=false,fixed=false;};
struct PMaterial {std::string name,memo;glm::vec4 diffuse{1};glm::vec4 edgeColor{0,0,0,1};float edgeSize=1;int texture=-1,sphere=-1,sphereMode=0,queue=2000,source=-1;bool twoSided=false,edge=false;std::vector<uint32_t> indices;};
struct MaterialOffset {int material=-1;glm::vec4 diffuse{0};};
struct PMorph {std::string name,english;int panel=4,type=1;std::vector<std::pair<uint32_t,glm::vec3>> vertex;std::vector<std::pair<int,float>> group;std::vector<MaterialOffset> material;std::vector<std::pair<uint32_t,glm::vec4>> uv;};
struct PmxWriter {
 Bytes b;
 void u8(uint8_t v){b.push_back(v);}
 void u16(uint16_t v){b.insert(b.end(),reinterpret_cast<uint8_t*>(&v),reinterpret_cast<uint8_t*>(&v)+2);}
 void i32(int32_t v){b.insert(b.end(),reinterpret_cast<uint8_t*>(&v),reinterpret_cast<uint8_t*>(&v)+4);}
 void f32(float v){b.insert(b.end(),reinterpret_cast<uint8_t*>(&v),reinterpret_cast<uint8_t*>(&v)+4);}
 void v2(glm::vec2 v){f32(v.x);f32(v.y);}void v3(glm::vec3 v){f32(v.x);f32(v.y);f32(v.z);}void v4(glm::vec4 v){f32(v.x);f32(v.y);f32(v.z);f32(v.w);}
 void text(const std::string& s){i32(int32_t(s.size()));b.insert(b.end(),s.begin(),s.end());}
};
// `materials` are in draw order; vertex indices and morph material references
// already point into `vertices` and that order.
struct PmxData {
 std::string name,comment;
 std::vector<PVertex> vertices;std::vector<std::string> textures;std::vector<const PMaterial*> materials;
 std::vector<PBone> bones;std::vector<PMorph> morphs;
};
Bytes writePmx(const PmxData&);
}
