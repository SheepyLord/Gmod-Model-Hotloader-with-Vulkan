#pragma once
#include "runtime.hpp"
#include <cstring>
namespace mmd {
// Source studio files retain 32-bit relative offsets on the x64 engine.
struct StudioWriter {
 Bytes b;
 size_t alloc(size_t n){size_t p=(b.size()+3)&~size_t(3);b.resize(p+n);return p;}
 template<class T>void put(size_t p,T v){if(p+sizeof(v)>b.size())throw std::runtime_error("Carrier offset out of range");std::memcpy(b.data()+p,&v,sizeof(v));}
 void i(size_t p,int v){put<int32_t>(p,v);} void f(size_t p,float v){put<float>(p,v);}
 void vec(size_t p,const btVector3& v){for(int k=0;k<3;k++)f(p+k*4,v[k]);}
 void matrix(size_t p,const btTransform& t){for(int row=0;row<3;row++){for(int col=0;col<3;col++)f(p+row*16+col*4,t.getBasis()[row][col]);f(p+row*16+12,t.getOrigin()[row]);}}
 size_t str(std::string_view s){size_t p=b.size();b.insert(b.end(),s.begin(),s.end());b.push_back(0);return p;}
 void relstr(size_t field,size_t base,std::string_view s){i(field,int(str(s)-base));}
 void fixed(size_t p,size_t n,std::string_view s){std::memcpy(b.data()+p,s.data(),std::min(n-1,s.size()));}
};
}
