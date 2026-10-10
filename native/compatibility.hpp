#pragma once
#include "runtime.hpp"
#include "engine_abi.hpp"
#include <array>
#include <optional>
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
// The RTTI class name of an object whose vtable lies in library, or "": MSVC's decorated
// name (".?AVName@@") on Windows, the Itanium mangled name ("4Name") on Linux.
std::string rttiClass(const void* object, const wchar_t* library);
// IAppSystem-derived interfaces (VMaterialSystem080, VPhysics031) are four slots
// shorter on the default branch's 64-bit build of 2026-09-17: it lacks the four
// newer IAppSystem methods of the x86-64 build these modules are compiled against,
// so every later method sits four slots lower under the same version string.
// Returns how many slots lower the running game has them (0 or 4), from the length
// of the object's vtable; compiledLength is that length in the compiled-for build.
// Any length but those two layouts, or a longer table (methods appended), throws: the
// slots of a table this code does not know hold other functions.
size_t appSystemShift(void* object, const wchar_t* library, size_t compiledLength);
// The shift a measured length means (appSystemShift's rule), or nothing for an unknown layout.
std::optional<size_t> knownAppSystemShift(size_t length, size_t compiledLength);
// Vtable lengths in the x86-64 build: CMaterialSystem (VMaterialSystem080) and the
// VPhysics031 object; the default branch's build of 2026-09-17 has 147 and 13.
// On Linux the lengths of the same tables as GCC lays them out (engine_abi.hpp).
constexpr size_t MaterialSystemVtableLength = abi::MaterialSystemVtableLength, PhysicsVtableLength = abi::PhysicsVtableLength;
// Slot shifts found so far, per library, for the compatibility report.
Json appSystemShifts();
// Entries of object's vtable: consecutive pointers to code, up to the last one in library
// (an entry another module hooked counts; see compatibility.cpp).
size_t vtableLength(void* object, const wchar_t* library);
// The default branch's vphysics.dll of 2026-09-17 also lacks
// IPhysicsCollision::VPhysicsKeyParserCreate(vcollide_t*) (slot 38) and six trailing
// methods (52 slots instead of 59), and IPhysicsObject::SetSphereRadius (slot 43):
// the later methods of those two interfaces sit one slot lower.
constexpr size_t CollisionVtableLength = abi::CollisionVtableLength;
// True when physics (VPhysics031) and collision (VPhysicsCollision007) have that layout,
// false for the x86-64 one; any other pair of lengths throws.
bool olderPhysicsLayout(void* physics, void* collision);
inline size_t collisionSlot(size_t compiled, bool older) { return older && compiled > abi::CollisionMissingInOlder ? compiled - 1 : compiled; }
inline size_t physicsObjectSlot(size_t compiled, bool older) { return older && compiled > abi::ObjectMissingInOlder ? compiled - 1 : compiled; }
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
#ifndef _WIN32
// The read-only and executable segments of an ELF shared object, hashed as stored in the file.
Json elfEvidence(const Bytes& bytes);
// The file name a Linux game process loaded for a library the policy names (engine.dll:
// engine_client.so on the x86-64 client, engine.so on the 32-bit client and servers), or "".
std::string gameLibraryName(const wchar_t* name);
// The address of a game library's CreateInterface, or nullptr while it is not loaded.
void* gameInterfaceFactory(const wchar_t* name);
// The load address of a game library (what RVAs are relative to), or 0.
uintptr_t gameLibraryBase(const wchar_t* name);
// Whether p is code of the game library (an executable segment of it).
bool gameLibraryCode(const wchar_t* name,const void* p);
// Whether p lies in the game library's image (any of its segments: code, vtables, data).
bool gameLibraryImage(const wchar_t* name,const void* p);
#endif
bool matchesEvidence(const Json& expected, const Json& observed);
}
