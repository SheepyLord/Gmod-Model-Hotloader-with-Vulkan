#pragma once
// Text decoding for builds without MultiByteToWideChar (Linux).
#include <cstddef>
#include <span>
#include <string>
namespace mmd {
// Code pages 932 (Shift-JIS), 936 (GBK), 949 (UHC), 950 (Big5) and 65001 (UTF-8) to wide
// text. strict: an invalid or unassigned sequence returns "" (MB_ERR_INVALID_CHARS);
// otherwise it becomes the code page's default character.
std::wstring decodeCodepageText(std::span<const unsigned char> bytes,unsigned codepage,bool strict);
// UTF-16LE bytes (PMX text, UTF-16 readmes) to wide text; unpaired surrogates become U+FFFD.
std::wstring wideFromUtf16le(const unsigned char* data,size_t bytes);
}
