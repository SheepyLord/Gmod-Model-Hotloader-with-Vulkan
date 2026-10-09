#pragma once
// Characters in other formats (FBX, glTF, DAE): the worker reads them through
// Assimp, which only it links. A probe reads the skeleton for the bone window
// (no textures); the import converts the file to an in-memory PMX with the
// player's bone assignment and swinging parts, imported like a VRM avatar.
#include "assets.hpp"
#include <functional>
namespace mmd {
// Part of every converted asset's identity: bump it when the conversion output changes.
constexpr int CharacterConverterGenerator=1;
using CharacterProgress=std::function<void(const char* stage,float progress)>;
// .fbx, .glb, .gltf and .dae (a .glb that is really a VRM avatar is read by vrm.cpp).
bool convertibleCharacter(const fs::path&);
std::vector<std::string> characterFormats();
// The bone window's input (status.json "probe": skeleton, automatic assignment, meshes).
Json probeCharacter(const fs::path& source,const Json& options,const CharacterProgress& progress);
// request: the import request (kind "character": boneMap by bone name, eyes, jiggle).
// Without a boneMap the automatic assignment is used when it is certain.
CharacterConversion convertCharacter(const fs::path& source,const Json& request,const CharacterProgress& progress);
}
