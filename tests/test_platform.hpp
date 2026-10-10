#pragma once
// What the tests need from the operating system, on Windows and Linux.
#include <cstdint>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
inline unsigned long processId(){return GetCurrentProcessId();}
// Text in a Windows code page (65001: UTF-8), as the Windows API writes it.
inline std::vector<uint8_t> encodeCodepage(const std::wstring& w,unsigned codepage){int n=WideCharToMultiByte(codepage,0,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);std::vector<uint8_t> b(size_t(n>0?n:0));if(n>0)WideCharToMultiByte(codepage,0,w.data(),int(w.size()),reinterpret_cast<char*>(b.data()),n,nullptr,nullptr);return b;}
#else
#include <iconv.h>
#include <unistd.h>
#include <stdexcept>
inline unsigned long processId(){return static_cast<unsigned long>(getpid());}
// The same through iconv, which knows the Windows code pages under their CP names.
inline std::vector<uint8_t> encodeCodepage(const std::wstring& w,unsigned codepage){
 std::string target=codepage==65001?"UTF-8":"CP"+std::to_string(codepage);
 iconv_t cd=iconv_open(target.c_str(),"WCHAR_T");if(cd==reinterpret_cast<iconv_t>(-1))throw std::runtime_error("iconv cannot encode "+target);
 std::vector<uint8_t> out(w.size()*4+16);auto in=reinterpret_cast<char*>(const_cast<wchar_t*>(w.data()));size_t inLeft=w.size()*sizeof(wchar_t);
 auto at=reinterpret_cast<char*>(out.data());size_t outLeft=out.size();
 auto r=iconv(cd,&in,&inLeft,&at,&outLeft);iconv_close(cd);if(r==size_t(-1))throw std::runtime_error("iconv cannot encode the text as "+target);
 out.resize(out.size()-outLeft);return out;
}
#endif
