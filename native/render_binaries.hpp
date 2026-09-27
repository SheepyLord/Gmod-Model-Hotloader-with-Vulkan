#pragma once
#include <string_view>

namespace mmd {
// SourceRTXTweaks in the tested x64 Remix client changes bounded instruction
// sequences, not these interfaces or mesh layouts. Do not accept arbitrary
// patched DLLs. See RTX_REMIX_VALIDATION.md and validate-render-abi.py.
inline bool supportedRenderBinary(std::wstring_view name,std::string_view sha){
 if(name==L"materialsystem.dll")return sha=="b933ffacf596411773eaf8b7b6847cdda46e8d70191fad25e493be1f7e4639e5"||sha=="daee78fee22c84312f984a60dee5911ff7fe5c0fac07499ac1bd02d388cb832b";
 if(name==L"shaderapidx9.dll")return sha=="4a07b56a2f6f03d5c06bc3d3bddb67d26d9a12eb1c4d01eaf35eb52623ae7326"||sha=="83f11968822f7896f9e666956719497b9ec22ea5535f6bd4a6f6684937f84f7e";
 if(name==L"stdshader_dx9.dll")return sha=="c9814355c1bdae82d1156e655795c1c0bc2086756cccc9c641fb2f2fde9bf546";
 if(name==L"stdshader_dx6.dll")return sha=="4bf218dbafc6b27514dd77141f9163a154f5ecb13713a5af8774dd9a9a4ab5a5";
 if(name==L"engine.dll")return sha=="4c0b6d27a25215256862de2d10976e53dc59331fa253b204c51e8dda2ce0b745"||sha=="7c21e827722fa7ac9ba4539dc652d3b79f58e240da49d28e75a1a9a1a88c4173"||sha=="626618f94fabefc69e2f321f984cef76e06591271b0725a9bf220a1c0105b3e5";
 return false;
}
}
