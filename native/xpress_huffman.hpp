#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace mmd {
// LZ77+Huffman ("XPRESS Huffman", [MS-XCA] 2.2.4), the format of Windows' Compression API
// with COMPRESS_ALGORITHM_XPRESS_HUFF, for builds without Windows. decodeXpressHuffman
// takes the raw stream and produces exactly outputSize bytes; decodeCompressionApiBuffer
// takes Compress()'s buffer-mode output (its header names the algorithm and size).
// Both throw std::runtime_error on malformed input.
std::vector<unsigned char> decodeXpressHuffman(std::span<const unsigned char> input,size_t outputSize);
std::vector<unsigned char> decodeCompressionApiBuffer(std::span<const unsigned char> input,size_t outputSize);
}
