#pragma once
#include "runtime.hpp"
namespace mmd {
fs::path sharedPath(const fs::path& cache,const std::string& relative);
Json sharedManifest(const fs::path& cache,const std::string& asset,const Json& rigs,bool sourceOnly);
void validateSharedFile(const std::string& relative,std::span<const unsigned char> bytes);
std::string mountablePackage(const fs::path& cache,const std::string& relative);
// Lossless, bounded-block transport cache. Only validated raw files are mounted.
constexpr uint32_t SharedBlockSize=1024*1024;
constexpr uint32_t SharedChunkSize=48*1024;
// compress=false stores every block raw (a packet about the size of the file).
Bytes packSharedBytes(std::span<const unsigned char> raw,const std::string& digest,bool compress=true);
fs::path packSharedFile(const fs::path& cache,const std::string& relative,uint64_t size,const std::string& digest);
Bytes unpackSharedFile(std::span<const unsigned char> packet,uint64_t size,const std::string& digest);
}
