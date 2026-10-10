// LZ77+Huffman decompression as [MS-XCA] 2.2.4 specifies it, with the 32-bit match
// length of later Windows versions, for builds without Windows' Compression API.
#include "xpress_huffman.hpp"
#include <array>
#include <cstring>
#include <stdexcept>
namespace mmd {
namespace {
constexpr unsigned TableBits=15;
constexpr uint16_t Invalid=0xffff;
struct Reader {
 std::span<const unsigned char> in;size_t at=0;
 // Bytes past the end read as zero (the encoder's final bits may end mid-word), but a
 // stream that keeps reading far past its end is broken.
 unsigned byte(){unsigned v=at<in.size()?in[at]:0;if(++at>in.size()+8)throw std::runtime_error("Truncated compressed data");return v;}
 unsigned u16(){unsigned lo=byte();return lo|byte()<<8;}
 uint32_t u32(){uint32_t lo=u16();return lo|uint32_t(u16())<<16;}
};
}
std::vector<unsigned char> decodeXpressHuffman(std::span<const unsigned char> input,size_t outputSize){
 std::vector<unsigned char> out(outputSize);
 std::vector<uint16_t> table(size_t(1)<<TableBits);std::array<uint8_t,512> lengths{};
 Reader r{input};size_t produced=0;
 while(produced<outputSize){
  // Each block: 256 bytes of 4-bit code lengths, then codes for 65536 bytes of output.
  if(r.at+256>input.size())throw std::runtime_error("Truncated compressed data (Huffman table)");
  for(size_t i=0;i<256;i++){lengths[2*i]=input[r.at+i]&15;lengths[2*i+1]=input[r.at+i]>>4;}
  r.at+=256;
  // Canonical codes in order of length, then symbol; a 15-bit prefix names the symbol.
  size_t entry=0;
  for(unsigned bits=1;bits<=TableBits;bits++)for(unsigned symbol=0;symbol<512;symbol++)if(lengths[symbol]==bits){
   size_t count=size_t(1)<<(TableBits-bits);if(entry+count>table.size())throw std::runtime_error("Invalid Huffman table in compressed data");
   std::fill(table.begin()+std::ptrdiff_t(entry),table.begin()+std::ptrdiff_t(entry+count),uint16_t(symbol));entry+=count;
  }
  if(!entry)throw std::runtime_error("Empty Huffman table in compressed data");
  std::fill(table.begin()+std::ptrdiff_t(entry),table.end(),Invalid);
  uint32_t next=r.u16()<<16;next|=r.u16();int extra=16;
  auto refill=[&]{if(extra<0){next|=uint32_t(r.u16())<<(-extra);extra+=16;}};
  const size_t blockEnd=produced+65536;
  while(produced<blockEnd&&produced<outputSize){
   uint16_t symbol=table[next>>(32-TableBits)];if(symbol==Invalid)throw std::runtime_error("Invalid Huffman code in compressed data");
   next<<=lengths[symbol];extra-=lengths[symbol];refill();
   if(symbol<256){out[produced++]=uint8_t(symbol);continue;}
   symbol-=256;size_t length=symbol&15;unsigned offsetBits=symbol>>4;
   if(length==15){
    length=r.byte();
    if(length==255){
     // A 16-bit length of 0 announces a 32-bit one (matches beyond 64 KiB).
     length=r.u16();if(length==0)length=r.u32();
     if(length<15)throw std::runtime_error("Invalid match length in compressed data");
     length-=15;
    }
    length+=15;
   }
   length+=3;
   uint32_t offset=offsetBits?next>>(32-offsetBits):0;offset+=uint32_t(1)<<offsetBits;
   next=offsetBits==32?0:next<<offsetBits;extra-=int(offsetBits);refill();
   if(offset>produced)throw std::runtime_error("Invalid match offset in compressed data");
   if(length>outputSize-produced)throw std::runtime_error("Compressed data expands beyond its recorded size");
   // Overlapping copies repeat the last offset bytes, byte by byte.
   const unsigned char* from=out.data()+produced-offset;unsigned char* to=out.data()+produced;
   if(offset>=length)std::memcpy(to,from,length);else for(size_t i=0;i<length;i++)to[i]=from[i];
   produced+=length;
  }
 }
 return out;
}
// Compress() in buffer mode (no COMPRESS_RAW) puts a header before the stream: the
// signature 0A 51 E5 C0, the header size (24), the algorithm (4: XPRESS_HUFF) in byte 7 and
// the uncompressed size. XPRESS blocks reach 1 GiB, so a model transfer block is one stream.
std::vector<unsigned char> decodeCompressionApiBuffer(std::span<const unsigned char> input,size_t outputSize){
 // Buffer mode: a header (signature, its size, a check byte, the algorithm, the output
 // size and the chunk size, 24 bytes so far), then each chunk of up to the chunk size as
 // its compressed length (32 bits) and its stream. Model transfers hold one chunk per block.
 static const unsigned char signature[4]={0x0a,0x51,0xe5,0xc0};
 auto u64=[&](size_t at){uint64_t v=0;for(int i=0;i<8;i++)v|=uint64_t(input[at+size_t(i)])<<(8*i);return v;};
 if(input.size()<24||std::memcmp(input.data(),signature,4))throw std::runtime_error("Compressed block lacks the Compression API header");
 size_t header=size_t(input[4])|size_t(input[5])<<8;
 if(header<24||header>input.size())throw std::runtime_error("Invalid Compression API header size");
 if(input[7]!=4)throw std::runtime_error("Compressed block uses an algorithm other than XPRESS Huffman");
 if(u64(8)!=outputSize)throw std::runtime_error("Compressed block records another size");
 uint64_t chunk=u64(16);if(!chunk)throw std::runtime_error("Invalid Compression API chunk size");
 std::vector<unsigned char> out;out.reserve(outputSize);
 for(size_t at=header;out.size()<outputSize;){
  if(input.size()-at<4)throw std::runtime_error("Truncated Compression API chunk");
  size_t length=size_t(input[at])|size_t(input[at+1])<<8|size_t(input[at+2])<<16|size_t(input[at+3])<<24;at+=4;
  if(length>input.size()-at)throw std::runtime_error("Truncated Compression API chunk");
  auto part=decodeXpressHuffman(input.subspan(at,length),size_t(std::min<uint64_t>(chunk,outputSize-out.size())));
  out.insert(out.end(),part.begin(),part.end());at+=length;
  if(out.size()==outputSize&&at!=input.size())throw std::runtime_error("Data after the last Compression API chunk");
 }
 return out;
}
}
