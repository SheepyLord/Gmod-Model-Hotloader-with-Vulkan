#pragma once
#include "core.hpp"

namespace props {
// Decodes every Zstandard frame in src (Blender 3.0+ compresses .blend files
// with it). Throws when the output would exceed limit bytes.
Bytes zstdDecompress(std::span<const uint8_t> src,uint64_t limit);
}
