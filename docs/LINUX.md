# Linux

Model Hotloader's native files exist for two platforms (`release.hpp`'s `MMDHL_PLATFORM`):

| Platform | Game | Files in `garrysmod/lua/bin` |
|---|---|---|
| `win64` | Windows x64 (both branches' 64-bit executables) | `gmcl/gmsv_mmdhl_win64.dll`, `mmdhl_runtime_win64.dll` (also `bin/win64`), `mmdhl_worker.exe`, `lib_coacd.dll` |
| `linux64` | Linux, the x86-64 branch | `gmcl/gmsv_mmdhl_linux64.dll`, `libmmdhl_runtime_linux64.so`, `mmdhl_worker_linux64`, `lib_coacd.so`, `libgomp-a34b3233.so.1.0.0` |

Garry's Mod's default branch is 32-bit on Linux, which Model Hotloader does not support: on it the installation check reports an unsupported platform and loads nothing. Garry's Mod loads `gmcl_/gmsv_<name>_linux64.dll` on Linux too, and every Linux file names its platform. The modules and the worker find the runtime beside them (RPATH `$ORIGIN`), so no native file goes into `bin`.

## Packages

As on Windows, there are two packages:

| Package | Contents |
|---|---|
| `…-linux64-vulkan` (default) | The native files, plus `bin/linux64/libtogl_client.so` (`renderer/togl`, replacing the game's own), DXVK's `bin/linux64/libdxvk_d3d9.so` and their notices in `bin/linux64/DXVK-LICENSES`. The game renders through Vulkan, and the Vulkan physics processor can share DXVK's device. |
| `…-linux64-opengl` | The native files only: the game keeps its own OpenGL renderer. |

The installation banner (Q > External Models) says "Renderer: DXVK (Vulkan)" when `bin/linux64/libtogl_client.so` is a renderer a release records (`renderer` in its `platforms.linux64` record, with DXVK's library as `renderer.library`), and "Renderer: OpenGL" otherwise. Steam's "Verify integrity of game files" restores the game's `libtogl_client.so`, so it is the way back to OpenGL; a game update that replaces that file also returns the game to OpenGL until the package is copied again. `libdxvk_d3d9.so` stays behind, unused.

## Building

`scripts/build-linux.sh linux64 [package-name]` fetches and patches the pinned sources (as `scripts/build.ps1` does), builds, runs CTest, builds the renderer (`scripts/build-dxvk-linux.sh`), records it in the release record (`native_manifest.py --renderer build-dxvk-linux64`) and stages `dist/<package-name>-vulkan` and `dist/<package-name>-opengl`, inside the `quay.io/pypa/manylinux_2_28_x86_64` container (AlmaLinux 8: glibc 2.28, GCC 14), the same way the Actions job does. The Steam Linux Runtime the game runs in (soldier, or the host when newer) guarantees glibc 2.28; files built against a newer glibc fail to load there (`GLIBC_2.xx not found`, which the installation check reports). The shipped native files need GLIBC_2.27 and GCC_4.2.0 symbols at most.

- libstdc++ is linked statically (the runtime's own is older); `libgcc_s` stays shared, because exceptions cross from the runtime library into the modules and the worker, and separate static unwinders abort on that. With a static libstdc++ per binary, anything identified by address is per binary: `import_error.cpp` compares `error_category` by name, and `ImportScope` sums every loaded binary's `std::uncaught_exceptions()` (`registerExceptionCounter` in `runtime.hpp`).
- `--exclude-libs,ALL`, `-Bsymbolic` and hidden visibility for the modules keep the game's own libstdc++ and tier0 out of the modules' symbol lookups.
- Debug information goes to `<file>.debug` beside each shipped file in the build folder (`objcopy --only-keep-debug`, with a debug link), like the Windows PDBs. `addr2line -e gmcl_mmdhl_linux64.dll.debug <offset>` reads a crash offset.
- ICU 74.2 is built statically into the runtime by `scripts/build-icu.py` with only its transliteration and normalization data: the game's runtime has no ICU, and `native/naming.cpp` needs the same "Any-Latin; Latin-ASCII" transliterator Windows uses for engine paths.
- `scripts/bootstrap.py` fetches the CoACD manylinux wheel (Linux only) and sets `lib_coacd.so`'s search path to `$ORIGIN`, so its bundled OpenMP runtime loads from beside it.
- `scripts/build-dxvk-linux.sh` builds DXVK 3.1.1's native `d3d9` (Meson; SDL2 window support only) with the Windows renderer's `patches/dxvk` and the Linux-only `patches/dxvk-linux`, its shaders compiled by the native build's glslang, then `renderer/togl` (CMake) and its export check. The output is `build-dxvk-linux64/src/d3d9/libdxvk_d3d9.so.0.30101` and `build-dxvk-linux64/togl/libtogl_client.so` (with its `.debug`).

A build with the system compiler (GCC 11 or newer) works for development; its files need the build machine's glibc.

## Vulkan renderer (`renderer/togl`)

Garry's Mod on Linux draws through ToGL: `shaderapidx9_client.so` calls Direct3D 9, and `bin/linux64/libtogl_client.so` (Valve's ToGL, MIT) carries it out on OpenGL. The `-vulkan` package replaces that library with one that keeps ToGL's interface and carries the calls out on DXVK's native Direct3D 9 (`libdxvk_d3d9.so`, loaded from beside it), much as the Windows package's `d3d9.dll` does.

- Interface: the game imports 131 symbols from `libtogl_client.so`: `Direct3DCreate9`, the methods of ToGL's Direct3D 9 classes (C++ names, `togl_abi.hpp`), D3DX helpers (`d3dx.cpp`, ported from ToGL) and the launcher's entry points. `exports.map` lists them (nothing else is exported), and CTest's `togl_exports` compares the build with it.
- Inline code: `shaderapidx9` was compiled with ToGL's inline functions, so it writes members of `IDirect3DDevice9` and of ToGL's `GLMContext` directly (streams, index buffer, shaders, textures, sampler states, shader constants with their dirty ranges) and sets render states by calling OpenGL through the table `ToGLConnectLibraries` returns. `togl_abi.hpp` keeps those layouts (the offsets were read from the game's binaries and are checked with `static_assert`); the table's state entries turn into Direct3D 9 render states (`gl_tables.cpp`, `togl.cpp`), and each draw first applies what the inline code changed. Any other OpenGL entry in that table is logged once and ignored.
- The launcher still creates its SDL window with an OpenGL context and gets real OpenGL from `GetOpenGLEntryPoints`. DXVK presents to the same window through Vulkan: `patches/dxvk-linux` lets DXVK's SDL2 backend create an Xlib or Wayland surface for a window SDL created without `SDL_WINDOW_VULKAN`, and adds a built-in profile for `bin/linux64/gmod` that turns descriptor heaps off, as `0002-gmod-app-profile.patch` does for Windows.
- ToGL's headers number `D3DFORMAT` sequentially, and a few other values differ from Direct3D 9 (texture address modes, 64-bit `LONG` in `D3DRECT`, `D3DCOLOR` vertex data, the sRGB texture usage bit); they are translated at the interface. The game sets depth bias in OpenGL's terms (`glPolygonOffset` units, steps of the depth buffer: its shadow-map pass uses 20); they become a Direct3D depth bias by the bound depth format's bits.
- The game sees the adapter ToGL describes: the launcher's display database (`GLMDisplayDB::GetFakeAdapterInfo`) supplies the vendor and device IDs, video memory and the caps ToGL derives from it (anisotropy, sRGB writes, clip planes), so the game's per-GPU defaults (`dxsupport.cfg`) stay as with OpenGL. DXVK's own adapter only names the GPU (`mat_info`: "<GPU> (Vulkan via DXVK)").
- Before loading DXVK the library sets `DXVK_WSI_DRIVER=SDL2` and adds `d3d9.countLosableResources = False` (ToGL never needed default-pool resources released before a reset) and `d3d9.hideIntelGpu = False` to `DXVK_CONFIG`.
- When DXVK cannot start (no Vulkan 1.3 driver, a missing `libdxvk_d3d9.so`), a message box says so and that verifying the game files in Steam returns the game to OpenGL.
- Like the native files, the library links libstdc++ statically and needs glibc 2.28.

Differences from the game's OpenGL renderer:

- HDR exposure works as on Windows. ToGL's occlusion queries count every pixel for every bucket of the luminance histogram (`mat_show_histogram 1` reports over 1000 % of the screen), so with OpenGL the automatic exposure stays at the map's minimum (`mat_hdr_tonemapscale` 0.70 on gm_construct, 0.30 on gm_flatgrass) and HDR maps look darker than on Windows; through DXVK the histogram counts the screen once and the exposure follows the scene (about 1.2 at gm_construct's spawn).
- DXVK samples D16 and D24S8 depth textures with depth comparison, as Direct3D 9 drivers do; ToGL returns the raw depth when the shadow filter is off. Shadow maps compare either way, so projected shadows (flashlight, lamps) match, but debug views that display a shadow depth texture differ.

## Engine ABI

GCC lays out the same SDK interfaces differently from MSVC: a virtual destructor takes two vtable slots instead of one, and overloads keep their declaration order. `native/engine_abi.hpp` holds every slot the modules call by number, per platform; the numbers were read from the SDK headers and checked against the game's x86-64 binaries (vtables found through RTTI). Other differences handled there or beside the calls:

- Libraries: `X.dll` is `bin/linux64/X_client.so` on the client and `X.so` on dedicated servers (`compatibility.cpp`). The compatibility family is `source-linux64-v1`; evidence is taken from the ELF images (`elfEvidence`).
- Virtuals that return a `Vector` by value (`IPhysicsObject::GetMassCenterLocalSpace`, `GetInertia`) are called with each compiler's convention (`vectorCall` in `bridge.cpp`).

No game library profiles are recorded for Linux yet, so External Models shows the "build has not been tested" warning on Linux; nothing is disabled by it.

## Files, text and sharing

- Model files come from Windows: texture references use `\` and any letter case. `DependencyScope::locate`, the static-prop `TextureResolver` and `assets.cpp` read `\` as a separator and match names case-insensitively (`posix::matchCase`); `C:\…` is absolute and `\x` drive-relative, as on Windows.
- Code pages 932, 936, 949 and 950 are decoded with tables generated from Python's codecs (`codepage_tables.hpp`); strict decoding refuses what `MB_ERR_INVALID_CHARS` refuses.
- DDS textures are decoded by `dds.cpp` (BC1 to BC3 and uncompressed), where Windows uses WIC.
- Model transfers and Workshop packages from Windows (MMDPACK2 with XPRESS Huffman blocks) are decoded by `xpress_huffman.cpp`; CTest's `windows_packed_models` unpacks the packages the addon ships, which Windows packed. Linux sends its own blocks uncompressed (every release reads them), so its transfers are larger.
- Carrier keys and MDL bytes follow each platform's floating point, so a carrier fitted on Linux has another key than on Windows. Clients request a carrier they do not have from the server by its key, so mixed games still work; the physics golden files are per platform (`tests/fixtures/physics/golden-linux64.json`).
- The worker's windows are zenity (part of the Steam Linux Runtime) or kdialog (`dialogs_posix.cpp`). Zip downloads drop the executable bit; the modules restore it on the worker before starting it.

## Tested

On Ubuntu 26.04 with Steam from the snap (the game in the Steam Linux Runtime container), vanilla Garry's Mod's x86-64 branch without other addons, with the files from `scripts/build-linux.sh`:

- CTest: all 34 tests (`installation_and_abi_evidence` and `file_access` are Windows-only and not built on Linux), and the renderer's `togl_exports`.
- Both modules load; the installation check, policy records, worker probe and CoACD pass.
- PMX imports through the worker, the Workshop sample packages (packed on Windows) install, the "Linlong" sample spawns as a ragdoll, renders (textures, alpha, GPU skinning, queued rendering) and falls as a ragdoll, its hair simulating; static props import (OBJ with a Windows-style texture path), with CoACD hulls, and render; the zenity picker opens and cancels.
- The Vulkan package on Intel graphics (Mesa 25.2), installed as a player would: gm_flatgrass and gm_construct render through DXVK as they do through ToGL (world, water, decals, ragdolls, a lamp's projected shadow, the HUD), compared view by view apart from the exposure above; the Linlong sample spawns, falls as a ragdoll and renders through it. External Models reports "Renderer: DXVK (Vulkan)", and "Renderer: OpenGL" after the game's `libtogl_client.so` is restored. `mat_info` reports the same chipset IDs as with ToGL, and the compared rendering ConVars match.
- Device reset: a video mode change at map start (1280×720 to 1600×900, and to 1280×720 with 4× MSAA) resets the device and keeps rendering. A mode change in the middle of a level change crashes the game in `engine_client.so`, with the game's own ToGL too.
- The Vulkan physics processor shares DXVK's device when the GPU has a compute-only queue family: in a test program that creates a DXVK device on an SDL window and then the solver in the same process, it reports `dxvk-shared` and passes its self-test on Intel with `ANV_QUEUE_OVERRIDE=gc=1,c=2`. Without such a family (this GPU's default) it keeps a device of its own. The snap's Mesa 25.2.2 fails the solver's self-test on that emulated family even with a device of its own, so this was not repeated inside the game.

Not tested: NVIDIA and AMD GPUs with the Vulkan package, dedicated servers, multiplayer between Windows and Linux players.
