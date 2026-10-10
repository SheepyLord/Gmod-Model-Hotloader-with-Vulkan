#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace mmd {
// The first image of a DDS file as 8-bit RGBA, for builds without Windows' WIC DDS
// codec: BC1 (DXT1), BC2 (DXT2/DXT3), BC3 (DXT4/DXT5), and uncompressed 32- and 24-bit
// colour from the pixel format's channel masks. Throws std::runtime_error otherwise.
std::vector<unsigned char> decodeDds(std::span<const unsigned char> file,int& width,int& height);
}
