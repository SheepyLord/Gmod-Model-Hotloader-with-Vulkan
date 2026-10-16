# 10 — Debugging, gotchas and known loose ends

How to find out what went wrong, the traps that cost earlier sessions time, and the loose ends
found while writing this guide (verified against `main` @ `9e02a27`). Fix loose ends only when the
user asks; they are listed so you do not mistake them for your own regressions.

## Where the evidence is

| Evidence | Where / how |
|---|---|
| Installation state | `mmdhl.GetInstallationStatus()` (issues, features, renderer, files with size/SHA-256/build ID); the "Model Hotloader needs attention" banner and Q > External Models first line; console lines `[Model Hotloader / client|server] …`; `mmdhl_check_installation` re-reads files (never reloads a DLL) |
| Native capabilities | `mmdhl.Decode(mmdhl.native.GetCapabilities())`, `GetSecondaryCapabilities()` (`gpu_vulkan.context` = `dxvk-shared`/`standalone`, `error`), `GetWorkerCapabilities()`, `GetInstallationInfo()` |
| Rendering | `RenderStatus()`, `RenderStats()` (`renderQueue`, `gpuSkinning.blocked`), `RenderFrameStats()`, `mmdhl_debug_overlay 1` / `mmdhl_debug_print 1` (performance.lua; `diagnostics/performance-*.json`) |
| Spawn / presentation failures | `data/mmd_hotloader/failures/<time>-<ent>.json` (carrier.lua, native_render.lua) |
| Secondary scene capture | `data/mmd_hotloader/diagnostics/secondary-scene-*.json` (contacts turned off) |
| Import jobs | `<temp>/mmdhl-jobs/job-<hex>/{request.json,status.json,worker.log}`; `PollJob` results carry `errorCode`, `stageCode`, `exitCode` and the last 4 KiB of `worker.log`; codes in [02](02_character_import_pipeline.md) §10 and `docs/IMPORT_ERRORS.md` |
| Crashes | Windows: the build's PDBs (`/Zi`, `/DEBUG:FULL`); `scripts/capture-dump.py` writes a minidump of the owned test game to `validation/` for WinDbg. Linux: shipped files are stripped and the build folder keeps `<file>.debug` beside each output (`CMakeLists.txt` post-build step), so `addr2line -f -C -e <file>.debug <offset>`; the kernel log (`journalctl -k`) names the library and offset of a segfault |
| Linux Vulkan renderer | `[ToGL/DXVK] …` lines (stderr and game console: device creation, adapter, `reset:`, failures, unexpected GL calls); DXVK's own log (`DXVK_LOG_LEVEL=info`) |
| In-game RPC | the debug bridge (`addon/lua/mmdhl/debug.lua`, token-gated; Windows driver `scripts/gamectl.py`; protocol in [06](06_lua_addon.md) §10) |

On Linux there is no launcher script in the repo. The same bridge works by hand: write
`garrysmod/lua/autorun/000_mmdhl_test_session.lua` containing `MMDHL_DEBUG_TOKEN='<hex>'`,
`garrysmod/data/mmd_hotloader/debug-session.json` `{"token":"<hex>"}`, start the game (Steam
`-applaunch 4000 … +map gm_flatgrass`), then exchange
`data/mmd_hotloader/debug/<hex>/<realm>-request.json` / `-response.json`. Delete both bootstrap
files afterwards; never commit or ship them.

## Symptom → cause → fix

| Symptom | Cause | What to do |
|---|---|---|
| Banner warns about files the addon does not know | local/CI build or a release newer than the addon's `native_policy.lua` | expected; nothing is disabled. Do not "fix" by editing the policy (release step only) |
| `game_unverified` warning on Linux | no Linux game-library profiles recorded (`compatibility_policy.lua` is win64-only) | expected; engine calls are still guarded individually |
| `mixed_builds` warning, crashes on load | module and runtime from different builds (Windows: `bin/win64` copy vs `lua/bin`) | install every file from one package |
| Imports stay off | worker self-test failed: worker missing, VC++ x64 redistributable (VCOMP140) on Windows, CoACD/libgomp on Linux, or a crash | read the installation window detail and `worker.log`; `detailedCollision` alone falls back to hull collision |
| A translucent blue wireframe Kleiner stands where a character should be | `mmdhl.AttachPresentation(ent)` failed on this client (`instances.lua` placeholder): carrier/assets not there yet, instance creation failed, or the renderer is unavailable | read `ent.MMDHLAttachError`, `RenderStatus()` and the failure JSON; attaching retries every 0.5 s (0.05 s while waiting). Renderer guards: queued-context RTTI and `abi::CallListOffset` (0x2B0) in `renderer.cpp` |
| "Incompatible carrier fit", saves/dupes refused (`persistence.error.carrier_outdated`) | `shape_atlas.hpp` changed (every carrier embeds its hash) or generator outside 30..31 | never edit the atlas casually; bump `RigGenerator` and keep `RigGeneratorMinLoadable` |
| `physics_profiles` CTest fails after a native change | fit output, MDL, `.phy` text or masses changed for unedited rigs | find the unintended change; re-record goldens only for a deliberate change, on each platform's release toolchain ([04](04_physics.md) §12) |
| CI Linux job fails `secondary_async` ("presentation stays continuous") | CPU contention under `ctest -j` on a 4-vCPU runner | already `RUN_SERIAL` (`1df48da`); reproduce with `taskset -c 0-3` plus busy loops |
| `check-lua-tests.py`: syntax error in a file that runs fine in game | lupa 2.8 = Lua 5.5: GLua operators or assignment to a `for` variable | rewrite in standard Lua ([11](11_conventions_and_rules.md)) |
| `test_release_evidence.py` fails on a machine without `pwsh` | it runs `update-native-policy.ps1` with PowerShell 7 (falls back to a Windows-only path) | install PowerShell 7 (`pwsh` on PATH; it passes on Linux then) |
| `git status` shows `native_policy.lua`, `addon/shaders/fxc/*.vcs`, `tests/fixtures/*`, `licenses/*.txt` changed after a build | `build.ps1` / CMake / `shaders.py` / `fixtures.py` / `package.py` rewrite tracked files | revert them unless the change is the point of the task |
| `gpu_vulkan` falls behind / `context: standalone` | no DXVK, D3D9 renderer, or a GPU without a compute-only queue family (common on Intel iGPUs); few CPU workers | expected; `cpu_mt_v2` stays the default. Test the shared path with `ANV_QUEUE_OVERRIDE=gc=1,c=2` on Mesa ANV (older Mesa may fail the solver self-test on that emulated family) |
| `gpu_opencl` never available on Linux | `compute_solver.cpp` calls `clewInit("OpenCL.dll")` on every platform | loose end (below); falls back to `reference` |
| Linux: message box "could not start its Vulkan renderer" | no Vulkan 1.3 driver, or `libdxvk_d3d9.so` missing beside the shim | Steam "Verify integrity" restores OpenGL |
| Linux Vulkan: projected (lamp/flashlight) shadows missing | `glPolygonOffset` units passed as a D3D bias | fixed in `609ce83` (`applyDepthBias`); keep the conversion |
| Linux Vulkan: HDR maps brighter than OpenGL | ToGL's histogram queries count every pixel and pin exposure at the map minimum | expected; DXVK matches Windows ([08](08_renderers_dxvk_togl.md)) |
| Linux Vulkan: `mat_info` vendor/device differ from ToGL; dxsupport defaults differ | adapter identity taken from DXVK instead of the launcher | must come from `launcherAdapter()`; `D3DADAPTER_IDENTIFIER9` is 0x440 bytes |
| A video-mode change during a level change crashes in `engine_client.so` | engine bug, also with the stock renderer | change modes at map start (append to `cfg/listenserver.cfg`) |
| Lua cannot run `mat_setvideomode`, `exec`, client `sv_cheats` | GMod blocks them from `RunConsoleCommand`/`game.ConsoleCommand` | use `cfg/listenserver.cfg`, or server-side `RunConsoleCommand('sv_cheats','1')` |
| Linux `gmad` refuses the addon: `shaders/fxc/*.vcs - Not allowed by whitelist` | the Linux Addon Creator 1.1 (x86-64 branch, checked 2026-10-16) whitelists fewer paths than the Windows `gmad.exe` the shipped `addon.gma` was built with | `addon.gma` is built on Windows by `publish-native-release.py` (`gmad.exe`); do not drop the shaders to make Linux gmad pass |

## Gotchas by area

- **Two physics systems.** Secondary motion (hair/cloth, client, nanoem/Bullet) and the Source
  ragdoll (`.phy` generated by `bridge.cpp` `carrierPhysics` + `physics_profile.cpp`, simulated by
  VPhysics on the server) are unrelated code paths. `native/physics.cpp` is neither the `.phy`
  writer nor the import; `native/jobs.cpp` is a thread pool, not the import job system.
- **Carriers are built on the server** (`.phy` needs VPhysics); clients download them by key and
  never refit. Rig keys differ between Windows and Linux builds (floating point), by design.
- **Asset ids hash warning strings.** Changing a repair/warning message re-identifies unchanged
  models ([02](02_character_import_pipeline.md) §12).
- **Every engine call is by vtable slot** (`engine_abi.hpp`: MSVC slot + `Dtor`, overload order
  differs between compilers). Read both platforms' vtables before touching a slot.
- **`mmdhl.native` is a guarded proxy**, not the raw module; void natives return nothing on
  success, so check the second return value (`local _,err=native.X()`).
- **Net names used by clients must be pooled before the native module loads** (in
  `installation.lua`), and the pre-native fallback handlers decode the same layouts as the real
  ones: change both together.
- **The addon and the native release ship separately** and mix in the field: feature-detect.
- **`native/render_binaries.hpp` is not a runtime gate.** Its pinned SHA-256 list is used only by
  `render_guard_tests` and `scripts/compatibility_profiles.py`; unknown game binaries are reported
  as warnings, never refused.
- **Windows-only scripts:** the in-game harness (`game-start.ps1`, `gamectl.py` sessions),
  `shaders.py`, `package.py`, `publish-native-release.py`'s gmad step. Linux builds must run in the
  manylinux_2_28 container; host-built Linux binaries need the host's glibc and break in Steam.
- **Linux 32-bit (default branch) is unsupported** and reported as an unsupported platform.

## Known loose ends (found while writing this guide; not fixed)

| Item | Where |
|---|---|
| OpenCL loader name is Windows-only (`clewInit("OpenCL.dll")`) | `native/compute_solver.cpp` |
| `carrier.lua` Tick hook `MMDHL.NativePose` reads NW2 `MMDHLInstance`, which nothing sets | `addon/lua/mmdhl/carrier.lua` |
| Natives without Lua callers: `SubmitSourcePose`, `StepSources`, `SubmitPresentationPose`, `GetMaterialState`, `ReadSharedChunk` | `native/module.cpp` |
| `GetModelAnimationDiagnostics` makes a virtual call without a slot guard | `native/module.cpp` / renderer |
| Bridge checks slot ownership only on the VPhysics factory interfaces (query-model / collision-set slots unchecked) | `native/bridge.cpp` |
| Net `mmdhl_secondary_backend` has a server receiver but no sender; NW2 `MMDHLSecondaryBackend` is set but never read | `secondary_backend.lua`, `carrier.lua` |
| `mmdhl_debug_token` ConVar is created but never read | `addon/lua/mmdhl/debug.lua` |
| `native/fitter_calibration.hpp` is generated but included nowhere; `--fit-raw` behaves like `--fit` | `native/`, `native/worker.cpp` |
| After a runtime GPU failure diagnostics say `cpu_mt` although the world stays v2 | `native/secondary.cpp` |
| Stale comments: `vulkan_solver.hpp` (standalone only), `ordered_dispatcher.hpp` (omits `gpu_vulkan`), `release.hpp.in` (lists removed `linux`), `blend.cpp` header (2.63+) | as named |
| `props::Protocol=2` is never read; Lua `P.Pins` (props) is read in `server.lua` but never written | `native/props/core.hpp`, `addon/lua/mmdhl/props/` |
| `bootstrap.py` records `.archive-sha256` but never checks it; nothing checks the glibc 2.28 symbol ceiling automatically | `scripts/bootstrap.py`, CI |
| `scripts/package.py` (legacy) omits `patches/`, `renderer/`, `shaders/vulkan/` and overwrites `licenses/*.txt` | `scripts/package.py` |
| Many `docs/*.md` describe older behaviour (catalogue with staleness in [09](09_history_and_lineage.md) §3) | `docs/` |
