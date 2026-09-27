#pragma once
// VRM avatars (glTF 2.0 binary with the VRM 0.x "VRM" or VRM 1.0 "VRMC_vrm"
// extensions) become an in-memory PMX 2.0 model, so the character pipeline
// (fitting, carriers, rendering, morphs, sharing) needs no second model format.
// Spring bones have no PMX equivalent; they travel as JSON in the asset
// manifest and are simulated natively (spring_bones.hpp).
#include "runtime.hpp"
#include <map>
namespace mmd {
// VRM lengths are metres; PMX characters use 8 cm units (a 1.6 m avatar is 20
// units tall, like an MMD model, and keeps its real size in Source).
constexpr float VrmUnitsPerMeter=12.5f;
struct VrmConversion {
 Bytes pmx;                             // PMX 2.0, UTF-8 text, 4-byte indices
 std::map<std::string,Bytes> textures;  // PMX texture path -> encoded image
 Json vrm;                              // version, meta/licence, humanoid map, spring bones, expressions
 std::vector<std::string> warnings;
};
bool isVrmData(std::span<const unsigned char>);
bool isVrmPath(const fs::path&);
VrmConversion convertVrm(std::span<const unsigned char> file,const std::string& fallbackName);
Json vrmMetadata(const Json& gltf,const std::string& fallbackName);
}
