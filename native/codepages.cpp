#include "codepages.hpp"
#include "codepage_tables.hpp"
#include "posix.hpp"
namespace mmd {
namespace {
const uint16_t (*table(unsigned codepage))[191]{
 switch(codepage){case 932:return codepage_tables::cp932;case 936:return codepage_tables::cp936;case 949:return codepage_tables::cp949;case 950:return codepage_tables::cp950;default:return nullptr;}
}
}
std::wstring decodeCodepageText(std::span<const unsigned char> b,unsigned codepage,bool strict){
 if(codepage==65001){try{return posix::wideFromUtf8(std::string_view(reinterpret_cast<const char*>(b.data()),b.size()),strict);}catch(...){return {};}}
 auto rows=table(codepage);if(!rows)return {};
 const wchar_t fallback=codepage==932?L'・':L'?';
 std::wstring out;out.reserve(b.size());
 for(size_t i=0;i<b.size();){
  unsigned c=b[i];wchar_t value=0;size_t used=1;
  if(c<0x80)value=wchar_t(c);
  else if(codepage==932&&c>=0xa1&&c<=0xdf)value=wchar_t(0xff61+(c-0xa1));
  // Bytes Windows maps outside Shift-JIS proper (to U+0080 and private-use characters):
  // invalid to MB_ERR_INVALID_CHARS, so only when not strict.
  else if(codepage==932&&!strict&&c==0x80)value=L'\u0080';
  else if(codepage==932&&!strict&&c==0xa0)value=wchar_t(0xf8f0);
  else if(codepage==932&&!strict&&c>=0xfd)value=wchar_t(0xf8f1+(c-0xfd));
  else if(codepage==936&&c==0x80)value=L'€';
  else if(c>=0x81&&c<=0xfe&&i+1<b.size()&&b[i+1]>=0x40&&b[i+1]<=0xfe){value=wchar_t(rows[c-0x81][b[i+1]-0x40]);if(value)used=2;}
  if(!value){if(strict)return {};value=fallback;}
  out.push_back(value);i+=used;
 }
 return out;
}
std::wstring wideFromUtf16le(const unsigned char* data,size_t bytes){
 std::wstring out;out.reserve(bytes/2);
 for(size_t i=0;i+1<bytes;i+=2){
  unsigned unit=unsigned(data[i])|unsigned(data[i+1])<<8;
  if(unit>=0xd800&&unit<=0xdbff&&i+3<bytes){unsigned low=unsigned(data[i+2])|unsigned(data[i+3])<<8;if(low>=0xdc00&&low<=0xdfff){out.push_back(wchar_t(0x10000+((unit-0xd800)<<10)+(low-0xdc00)));i+=2;continue;}}
  out.push_back(unit>=0xd800&&unit<=0xdfff?L'�':wchar_t(unit));
 }
 return out;
}
}
