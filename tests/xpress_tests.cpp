// The Linux XPRESS Huffman decoder (native/xpress_huffman.cpp) against Windows' own
// Compression API output: the model packages the addon ships (addon/data_static/mmdhl/
// files, MMDPACK2 files named by their SHA-256) were packed on Windows. Each must unpack
// to its digest (unpackSharedFile), and damaged copies must never pass.
#include "runtime.hpp"
#include "sharing.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace mmd;
namespace {
Bytes load(const fs::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot read "+p.string());return Bytes(std::istreambuf_iterator<char>(f),{});}
}
int main(int argc,char** argv){try{
 if(argc!=2){std::cerr<<"usage: mmdhl_xpress_tests <folder of MMDPACK2 .dat files>\n";return 2;}
 int files=0,compressed=0;
 for(auto& entry:fs::directory_iterator(argv[1])){
  if(entry.path().extension()!=".dat")continue;
  auto packet=load(entry.path());auto name=entry.path().stem().string();
  if(packet.size()<80||std::memcmp(packet.data(),"MMDPACK2",8))throw std::runtime_error("FAIL "+name+": not an MMDPACK2 file");
  uint64_t size=0;std::memcpy(&size,packet.data()+8,8);std::string digest(reinterpret_cast<const char*>(packet.data()+16),64);
  if(digest!=name)throw std::runtime_error("FAIL "+name+": the file is named for another digest");
  auto raw=unpackSharedFile(packet,size,digest);
  // Count the compressed blocks, and damage the middle of each: never accepted.
  for(size_t at=80;at+8<=packet.size();){
   uint32_t count,stored;std::memcpy(&count,packet.data()+at,4);std::memcpy(&stored,packet.data()+at+4,4);at+=8;
   bool raw=(stored&0x80000000u)!=0;stored&=0x7fffffffu;
   if(!raw){compressed++;auto bad=packet;bad[at+stored/2]^=0x5a;bool refused=false;try{unpackSharedFile(bad,size,digest);}catch(const std::exception&){refused=true;}
    if(!refused)throw std::runtime_error("FAIL "+name+": a damaged block was accepted");}
   at+=stored;
  }
  std::cout<<"PASS "<<name.substr(0,12)<<" ("<<size<<" bytes from "<<packet.size()<<")\n";files++;
 }
 if(!files||!compressed)throw std::runtime_error("No compressed MMDPACK2 files in "+std::string(argv[1]));
 std::cout<<files<<" Windows-packed files unpacked ("<<compressed<<" compressed blocks; damaged copies refused)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
