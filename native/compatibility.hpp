#pragma once
#include "runtime.hpp"
#include <array>
#include <stdexcept>
#include <string>
#include <utility>
namespace mmd {
// Profiles describe evidence for a compiled ABI, never new calling conventions.
// A game library without a matching profile (every game update) is "unverified":
// it still runs, behind the runtime checks (interface versions, slot ownership,
// class identity, IAppSystem layout) and guards learned from their first observation.
Json configureCompatibility(const Json& policy);
Json checkCompatibility(bool server);
const Json& requireGameBinary(const wchar_t* name);
void requireAbiRva(const wchar_t* name, const char* guard, uintptr_t observed);
bool matchesAbiRva(const wchar_t* name, const char* guard, uintptr_t observed);
void requireOwnedSlots(void* object, const wchar_t* library, std::initializer_list<size_t> slots);
// The MSVC RTTI class name (".?AVName@@") of an object whose vtable lies in library, or "".
std::string rttiClass(const void* object, const wchar_t* library);
// IAppSystem-derived interfaces (VMaterialSystem080, VPhysics031) are four slots
// shorter on the default branch's 64-bit build of 2026-09-17: it lacks the four
// newer IAppSystem methods of the x86-64 build these modules are compiled against,
// so every later method sits four slots lower under the same version string.
// Returns how many slots lower the running game has them (0 or 4), from the length
// of the object's vtable; compiledLength is that length in the compiled-for build.
size_t appSystemShift(void* object, const wchar_t* library, size_t compiledLength);
// Vtable lengths in the x86-64 build: CMaterialSystem (VMaterialSystem080) and the
// VPhysics031 object; the default branch's build of 2026-09-17 has 147 and 13.
constexpr size_t MaterialSystemVtableLength = 151, PhysicsVtableLength = 17;
// Slot shifts found so far, per library, for the compatibility report.
Json appSystemShifts();
namespace probe {
inline thread_local size_t hit = ~size_t(0);
template<size_t I> void stub() { hit = I; }
template<size_t... I> std::array<void*, sizeof...(I)> table(std::index_sequence<I...>) { return {reinterpret_cast<void*>(&stub<I>)...}; }
}
// The compiled vtable slot of the virtual method call invokes on its T*, found by
// calling it on a probe object whose slots record their index. The method must
// return a scalar or nothing; its arguments are ignored.
template<class T, class F> size_t virtualSlot(F call) {
    static const auto slots = probe::table(std::make_index_sequence<256>{});
    struct { void* const* vtable; } object{slots.data()};
    T* volatile target = reinterpret_cast<T*>(&object);
    probe::hit = ~size_t(0);
    call(target);
    if (probe::hit == ~size_t(0)) throw std::runtime_error("Cannot locate a compiled virtual method");
    return probe::hit;
}
Json peEvidence(const Bytes& bytes);
bool matchesEvidence(const Json& expected, const Json& observed);
}
