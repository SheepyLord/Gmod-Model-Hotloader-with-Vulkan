# Linux

Model Hotloader's native files exist for three platforms (`release.hpp`'s `MMDHL_PLATFORM`):

| Platform | Game | Files in `garrysmod/lua/bin` |
|---|---|---|
| `win64` | Windows x64 (both branches' 64-bit executables) | `gmcl/gmsv_mmdhl_win64.dll`, `mmdhl_runtime_win64.dll` (also `bin/win64`), `mmdhl_worker.exe`, `lib_coacd.dll` |
| `linux64` | Linux, the x86-64 branch | `gmcl/gmsv_mmdhl_linux64.dll`, `libmmdhl_runtime_linux64.so`, `mmdhl_worker_linux64`, `lib_coacd.so`, `libgomp-a34b3233.so.1.0.0` |
| `linux` | Linux, the default branch (32-bit) | `gmcl/gmsv_mmdhl_linux.dll`, `libmmdhl_runtime_linux.so`, `mmdhl_worker_linux` |

Garry's Mod loads `gmcl_/gmsv_<name>_<linux|linux64>.dll` on Linux too. Every Linux file names its platform, so both packages can share a game folder. The modules and the worker find the runtime beside them (RPATH `$ORIGIN`), so nothing goes into `bin`. The game draws through its OpenGL translation of Direct3D 9 (ToGL), so there is no renderer to install (`renderer={kind='opengl'}`). CoACD has no 32-bit build, so detailed collision for static props needs the x86-64 branch.

## Building

`scripts/build-linux.sh <linux64|linux> [package-name]` fetches and patches the pinned sources (as `scripts/build.ps1` does), builds, runs CTest and stages `dist/<package-name>` inside the `quay.io/pypa/manylinux_2_28_x86_64` container (AlmaLinux 8: glibc 2.28, GCC 14), the same way the Actions job does. The Steam Linux Runtime the game runs in (soldier, or the host when newer) guarantees glibc 2.28; files built against a newer glibc fail to load there (`GLIBC_2.xx not found`, which the installation check reports). The shipped files need GLIBC_2.27 and GCC_4.2.0 symbols at most.

- libstdc++ is linked statically (the runtime's own is older); `libgcc_s` stays shared, because exceptions cross from the runtime library into the modules and the worker, and separate static unwinders abort on that. With a static libstdc++ per binary, anything identified by address is per binary: `import_error.cpp` compares `error_category` by name, and `ImportScope` sums every loaded binary's `std::uncaught_exceptions()` (`registerExceptionCounter` in `runtime.hpp`).
- `--exclude-libs,ALL`, `-Bsymbolic` and hidden visibility for the modules keep the game's own libstdc++ and tier0 out of the modules' symbol lookups.
- Debug information goes to `<file>.debug` beside each shipped file in the build folder (`objcopy --only-keep-debug`, with a debug link), like the Windows PDBs. `addr2line -e gmcl_mmdhl_linux64.dll.debug <offset>` reads a crash offset.
- 32-bit: `-msse2 -mfpmath=sse` (x87 precision would change physics results) and `-ftls-model=initial-exec`. GCC assumes the 32-bit dynamic TLS call (`___tls_get_addr`) keeps SSE registers, but before glibc 2.39 its first call in each thread allocates through an SSE `memset` and clobbers them (glibc bug 31372): values held in them came back as garbage (the first `std::call_once` in a worker thread crashed). The binaries' TLS is small (about 320 bytes), well inside glibc's static TLS reserve.
- ICU 74.2 is built statically into the runtime by `scripts/build-icu.py` with only its transliteration and normalization data: the game's runtime has no ICU, and `native/naming.cpp` needs the same "Any-Latin; Latin-ASCII" transliterator Windows uses for engine paths.
- `scripts/bootstrap.py` fetches the CoACD manylinux wheel (Linux only) and sets `lib_coacd.so`'s search path to `$ORIGIN`, so its bundled OpenMP runtime loads from beside it.

A build with the system compiler (GCC 11 or newer) works for development; its files need the build machine's glibc.

## Engine ABI

GCC lays out the same SDK interfaces differently from MSVC: a virtual destructor takes two vtable slots instead of one, and overloads keep their declaration order. `native/engine_abi.hpp` holds every slot the modules call by number, per platform; the numbers were read from the SDK headers and checked against the game's binaries on both branches (vtables found through RTTI). Other differences handled there or beside the calls:

- Libraries: `X.dll` is `bin/linux64/X_client.so` on the x86-64 client, `bin/X.so` on the 32-bit client and dedicated servers (`compatibility.cpp`). The compatibility family is `source-linux64-v1` or `source-linux-v1`; evidence is taken from the ELF images (`elfEvidence`).
- The queued render context's call list is at `this+0x1DC` on 32-bit (`0x2B0` elsewhere). In testing, the 32-bit default branch did not queue even with `mat_queue_mode 2`; the module then draws in the hardware context.
- Virtuals that return a `Vector` by value (`IPhysicsObject::GetMassCenterLocalSpace`, `GetInertia`) are called with each compiler's convention (`vectorCall` in `bridge.cpp`).
- The default branch's ToGL cannot create an R32F render target: the legacy `mmdhl_ragdoll` renderer draws there without its projected shadow (`client.lua`).

No game library profiles are recorded for Linux yet, so External Models shows the "build has not been tested" warning on Linux; nothing is disabled by it.

## Files, text and sharing

- Model files come from Windows: texture references use `\` and any letter case. `DependencyScope::locate`, the static-prop `TextureResolver` and `assets.cpp` read `\` as a separator and match names case-insensitively (`posix::matchCase`); `C:\…` is absolute and `\x` drive-relative, as on Windows.
- Code pages 932, 936, 949 and 950 are decoded with tables generated from Python's codecs (`codepage_tables.hpp`); strict decoding refuses what `MB_ERR_INVALID_CHARS` refuses.
- DDS textures are decoded by `dds.cpp` (BC1 to BC3 and uncompressed), where Windows uses WIC.
- Model transfers and Workshop packages from Windows (MMDPACK2 with XPRESS Huffman blocks) are decoded by `xpress_huffman.cpp`; CTest's `windows_packed_models` unpacks the packages the addon ships, which Windows packed. Linux sends its own blocks uncompressed (every release reads them), so its transfers are larger.
- Carrier keys and MDL bytes follow each platform's floating point, so a carrier fitted on Linux has another key than on Windows (and the 32-bit and 64-bit builds can differ too). Clients request a carrier they do not have from the server by its key, so mixed games still work; the physics golden files are per platform (`tests/fixtures/physics/golden-linux64.json`, `golden-linux.json`).
- The worker's windows are zenity (part of the Steam Linux Runtime) or kdialog (`dialogs_posix.cpp`). Zip downloads drop the executable bit; the modules restore it on the worker before starting it.

## Tested

On Ubuntu 26.04 with Steam from the snap (the game in the Steam Linux Runtime container), vanilla Garry's Mod without other addons, both branches, with the files from `scripts/build-linux.sh`:

- CTest: all 34 tests on both builds (`installation_and_abi_evidence` and `file_access` are Windows-only and not built on Linux).
- Both modules load; the installation check, policy records, worker probe (and CoACD on x86-64) pass.
- PMX imports through the worker, the Workshop sample packages (packed on Windows) install, the "Linlong" sample spawns as a ragdoll, renders (textures, alpha, GPU skinning; queued rendering on x86-64) and falls as a ragdoll, its hair simulating (checked on x86-64); static props import (OBJ with a Windows-style texture path), with CoACD hulls on x86-64, and render; the zenity picker opens and cancels (x86-64).

Not tested: dedicated servers, the Vulkan GPU processor (it would create its own Vulkan device: there is no DXVK to share), multiplayer between Windows and Linux players.
