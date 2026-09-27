#pragma once
#include "runtime.hpp"
#include <atomic>
#include <mutex>
namespace mmd {
// Workshop model packages. An export is an ordinary GMA whose members are all on
// GMod's addon whitelist: one package manifest plus content-addressed, compressed
// cache files under data_static/mmdhl/. Readers install them with the verified
// shared-file transfer (BeginSharedFile), so installing needs no new native API.
// Installers refuse a larger package manifest (MaxPackageBytes in addon/lua/mmdhl/workshop.lua).
constexpr size_t PackageManifestLimit=4u<<20;
// They also refuse (workshop.lua fileLimit and ValidatePackage) a file over its kind's
// limit (0: a path no package may list), more than PackageTotalLimit of files, and a
// file that inflates more than PackageExpansionLimit times its packed size once it is
// over PackageExpansionFloor.
uint64_t packageInstallLimit(const std::string& path);
constexpr uint64_t PackageTotalLimit=16ull<<30,PackageExpansionLimit=256,PackageExpansionFloor=1u<<20;
struct PackageProgress{std::atomic<float> fraction{0};std::atomic_bool cancel{false};std::mutex lock;std::string stage="collecting";};
// Writes <cache>/exports/<name>.gma and describes the result.
Json exportPackage(const fs::path& cache,const fs::path& gameRoot,const Json& spec,PackageProgress& progress);
// Package ids listed in each GMA (engine.GetAddons() file paths), in order; empty when unreadable.
Json readAddonPackages(const fs::path& gameRoot,const Json& files);
std::string packageFileName(const std::string& proposed);
uint32_t crc32(std::span<const unsigned char> bytes,uint32_t crc=0);
}
