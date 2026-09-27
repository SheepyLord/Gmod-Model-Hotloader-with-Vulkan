#pragma once
#include "runtime.hpp"
namespace mmd {
// Terms of use that come with a model, read before it is imported: readme and
// licence text files beside it (decoded from UTF-8, UTF-16, Shift-JIS, GBK,
// Big5 or UHC), the name and comment embedded in PMX and PMD files, and the
// licence fields of VRM avatars and glTF files. Nothing is interpreted here.
Json inspectModelNotes(const fs::path& model);
// Text file bytes to UTF-8 with normalized line ends; encoding receives the name used.
std::string decodeText(std::span<const unsigned char> bytes,std::string& encoding);
}
