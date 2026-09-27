#pragma once
#include "runtime.hpp"
namespace mmd {
// Profiles describe evidence for a compiled ABI, never new calling conventions.
Json configureCompatibility(const Json& policy);
Json checkCompatibility(bool server);
const Json& requireGameBinary(const wchar_t* name);
void requireAbiRva(const wchar_t* name, const char* guard, uintptr_t observed);
bool matchesAbiRva(const wchar_t* name, const char* guard, uintptr_t observed);
void requireOwnedSlots(void* object, const wchar_t* library, std::initializer_list<size_t> slots);
Json peEvidence(const Bytes& bytes);
bool matchesEvidence(const Json& expected, const Json& observed);
}
