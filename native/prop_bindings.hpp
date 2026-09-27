#pragma once
#include <GarrysMod/Lua/Interface.h>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
namespace mmd {
// Static props live under <cache>/static: assets/<sha256>.gmdl bundles (the id
// is the bundle's own SHA-256), textures/<sha256>.png, and sources.local.json.
void registerPropFunctions(GarrysMod::Lua::ILuaBase* LUA,const std::filesystem::path& cache);
void shutdownProps();
// Shared-file hooks: a transferred bundle must decode, validate and hash to its name.
bool isPropBundle(const std::string& relative);
void validatePropBundle(const std::string& relative,std::span<const unsigned char> bytes);
// Remove bundles no longer referenced by the local library. Returns bytes and files removed.
std::string deleteProps(const std::vector<std::string>& ids);
}
