#pragma once
// A character converted from another format (FBX, glTF, DAE) by the worker
// (character_import.cpp), imported like a VRM avatar: an in-memory PMX with
// embedded textures and a manifest block, "conversion", inside its identity.
#include "runtime.hpp"
#include <map>
namespace mmd {
struct CharacterConversion {
 Bytes pmx;                             // PMX 2.0, UTF-8 text, 4-byte indices
 std::map<std::string,Bytes> textures;  // PMX texture path -> encoded image
 Json conversion;                       // manifest.conversion: bone map, units, springs, renamed morphs
 std::vector<std::string> warnings;
};
// importAsset for a converted character: the same identity, cache, registry and fit.
Json importConverted(const fs::path& source,const fs::path& cache,const Json& options,const fs::path& progress,CharacterConversion&& converted);
}
