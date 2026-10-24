# 00 — Repository map

Snapshot: `main` at `9e02a27` ("Approve and recommend native release 2.3.0"), release label `2.3.0`.
Paths are repo-relative. Target abbreviations: **RT** `mmd_runtime` (shared lib) · **WK** `mmdhl_worker` ·
**CL/SV** `mmdhl_client`/`mmdhl_server` (game modules) · **PC/PI** `mmdhl_props_core`/`mmdhl_props_import` ·
**CI** `mmdhl_character_import` · **NC/NP** `nanoem_core`/`nanoem_physics` · **hdr** header-only (→ includers).

## 1. Top level

| Path | Purpose |
|---|---|
| `CMakeLists.txt` | The whole native build: targets, flags, CTest, policy generation (07). Only other CMake project: `renderer/togl/`. |
| `dependencies.lock.json` | Pinned revisions (nanoem, glm, bullet, garrysmod_common, sourcesdk, json, stb, assimp, meshoptimizer, vulkan_headers, glslang); CoACD wheels `coacd`/`coacd_linux64` and ICU 74.2 with SHA-256. |
| `addon/` | Workshop addon source (§5). Shipped only as `addon.gma`. |
| `addon.gma` | Committed Workshop package (~61 MB, 101 entries = tracked `addon/` minus `addon.json`, which feeds the header). Built from `git archive HEAD addon` + `gmad create` (`package_addon` in `scripts/publish-native-release.py`; "Repackage addon.gma…" commits do the same). |
| `native/` | C++20 runtime, worker, game modules; `native/props/` static-prop importer (§3). |
| `renderer/togl/` | Linux `-vulkan` package's `libtogl_client.so` (§4). |
| `shaders/` | `mmdhl_vs20.hlsl`, `mmdhl_ps20b.hlsl` → `scripts/shaders.py` → `addon/shaders/fxc/*.vcs`; `vulkan/solver.comp` → glslang at build time → `<build>/generated/vulkan_solver{,_profile}_spv.h`. |
| `patches/` | DXVK only: `dxvk/0001-shared-compute-queues.patch`, `dxvk/0002-gmod-app-profile.patch` (both OSes), `dxvk-linux/0001-sdl2-surface-and-gmod-profile.patch`. Bullet/nanoem patches are string replacements inside `scripts/bootstrap.py` and `scripts/patch-bullet-threading.py`. |
| `scripts/` | Build, CI, release, generators, Windows in-game harness, research (§6). |
| `tests/` | CTest C++, offline Python/lupa tests, in-game probes, fixtures (§7). |
| `docs/` | Human design notes and dated reports; often stale (§8). |
| `licenses/` | Notices copied into packages (`*.txt`); `licenses/linux/{icu.txt, libgomp-GPL-3.0-with-GCC-exception.txt, togl.txt}` for Linux packages. `scripts/package.py` rewrites several from `vendor/`. |
| `integrations/` | `advanced-material-editor.patch` + README; applied to an AME checkout by `scripts/install-ame-compat.ps1`. |
| `.github/workflows/package.yml` | Only workflow, "Build drop-in package", `workflow_dispatch` only. |
| `install.ps1` | Wrapper for `scripts/install.ps1 -GameRoot … [-InstallAddon]`. |
| `README.md`, `ReleaseNote.md` | Player docs + build commands; release notes (newest `#` section first). |
| `THIRD_PARTY.md`, `LICENSE` | Components, licenses, the five bootstrap patches; MIT (MPL-2.0: `native/secondary.cpp`, `tests/nanoem_replay.cpp`). Both shipped in packages. |
| `Translation Templates Write.txt` | Steam Workshop description in 7 languages (BBCode). Not used by builds. |

**Gitignored outputs:** `vendor/` (pinned sources, `coacd/`, `icu-linux64/`, `dxvk/`, `dxvk-native/`, `ShaderCompile.exe`), `build/`, `build-linux64/`, `build-dxvk/`, `build-dxvk-linux64/`, `build-profile/`, `dist/`, `test-output/`, `validation/` (private reports, `session.json`), `shaders/include/`, `shaders/shaders/`, `__pycache__/`, `build-log.txt`, `*.user`. CI also writes an untracked `linux-summary.md`.

## 2. CMake targets
- `nanoem_core` (static): `vendor/nanoem/nanoem/nanoem.c` + `ext/mbwc.c` (Win) | `native/nanoem_unicode.c` (Linux). `nanoem_physics` (static): `native/nanoem_backend.cpp`.
- `mmd_runtime` (shared → `mmdhl_runtime_win64.dll` / `libmmdhl_runtime_linux64.so`): 30 `native/*.cpp` in its `add_library` + `vendor/bullet/src/clew/clew.c` + `vulkan_solver.cpp` + generated SPIR-V headers; Linux adds `posix.cpp xpress_huffman.cpp dds.cpp codepages.cpp` and static ICU.
- `mmdhl_props_core` (static, linked into the game modules), `mmdhl_props_import` (static, + Assimp/meshoptimizer/zlib), `mmdhl_character_import` (static, + Assimp; worker only).
- `mmdhl_worker` (→ `mmdhl_worker.exe` / `mmdhl_worker_linux64`): `worker.cpp file_access_dialog.cpp` + `worker.manifest` (Win) | `dialogs_posix.cpp`.
- `mmdhl_client`/`mmdhl_server` (MODULE → `gmcl_/gmsv_mmdhl_<platform>.dll`, `.dll` also on Linux): `module.cpp prop_bindings.cpp` + `renderer.cpp` (client) | `bridge.cpp` (server, `MMDHL_SERVER`).
- `mmdhl_native_manifest` (custom, ALL): runs `scripts/native_manifest.py`. Test/tool executables: 07.

## 3. `native/` sources (role [target])

**Runtime core / I/O**
- `runtime.hpp` — core types (`Model`, `Material`, `Instance`, `World`, `Snapshot`, `DrawVertex`, `GpuSkin`), IO decls, `ApiVersion` [hdr, all].
- `model.cpp` — PMX/PMD parse (nanoem), numeric validation + repair, pose evaluation (IK, morphs, linear/dual-quaternion skinning), skin layout, GPU-skin plan, `Instance::publish` [RT].
- `assets.cpp`, `assets.hpp` — cache I/O (`readFile`, `writeAtomic`, JSON), textures (≤4096 px, PNG), `importAsset`/`importConverted`/`loadAsset`/`deleteAssets`/`sweepJobFolders`, Source material prep; `CharacterConversion` [RT].
- `naming.cpp` — `readableName`: ASCII engine paths via ICU "Any-Latin; Latin-ASCII" [RT].
- `jobs.cpp/.hpp` — shared thread pool (`parallelFor`), Bullet task scheduler [RT].
- `scene.cpp/.hpp` — captured Source scene, `Collide::` flags, interest regions [RT]. `scene_share.cpp/.hpp` — `SceneShare` remote collision geometry [RT].
- `release.hpp.in` — → `<build>/generated/release.hpp` (`MMDHL_RELEASE`, `MMDHL_BUILD_ID`, `MMDHL_INSTALL_API 1`, `MMDHL_PLATFORM`, file names) [all].
- `validation_once.hpp` (call_once keeping failures), `win_errors.hpp` (Windows error codes/errno on Linux), `mmd_names.hpp` (JP morph names → English) [hdr].

**Import pipeline / worker**
- `worker.cpp` — worker CLI: `--version`, `--installation-test`, `--probe-compute`, `--probe-vulkan`, `--request` (import job), `--pick`, `--inspect`, `--inspect-physics`, `--fit`/`--fit-raw`, `--probe-character`, `--convert-character`, `--convert-vrm`, `--inspect-bone-map`, `--bone-map-proposal`, `--fa-pick`/`--fa-consent`, `--crash-test` [WK]. `worker.manifest` — Common Controls 6, DPI [WK Win].
- `import_error.cpp/.hpp` — `ImportError` (code, details, places), `describeException`, worker exit/status text [RT].
- `dependency_scope.cpp/.hpp` — which referenced files a model may open (denylist, confined scope, links) [RT].
- `character_import.cpp/.hpp` — FBX/glTF/DAE → in-memory PMX; skeleton probe [CI].
- `vrm.cpp/.hpp` — VRM 0.x/1.0 → PMX + spring JSON [RT]. `pmx_writer.cpp/.hpp` — PMX 2.0 writer for converters [RT].
- `humanoid_map.cpp/.hpp` — bone-name engine, auto slot assignment, `inspectBoneMap` [RT]. `humanoid_slots.hpp` — slot catalogue mirrored by `bone_mapper_rules.lua` (parity `tests/fixtures/bonemap/slots.json`) [hdr].
- `model_notes.cpp/.hpp` — terms-of-use text (readmes in UTF-8/16/SJIS/GBK/Big5/UHC, PMX comments, VRM/glTF licences) [RT].
- `picked_models.cpp/.hpp` — remote-server imports limited to picker-chosen files; private job folders [RT].
- `dds.cpp/.hpp` — DDS BC1–BC3/uncompressed decoder (Windows uses WIC) [RT Linux].

**Rig fit / carrier writing**
- `rig.cpp`, `rig.hpp` — `fitRig` (ValveBiped carrier, 18 bodies), `rigFromManifest`, `boneMapProposal`, `packageCarrier` → `rigs/<key>/carrier.gma` (`.mdl .vvd .dx90.vtx .phy`); `RigVersion=3`, `RigGenerator=31`, `RigGeneratorMinLoadable=30` [RT].
- `rig_animation.cpp/.hpp` (Source animation references), `rig_geometry.cpp/.hpp` (first-person masks, arms model), `fitter.cpp/.hpp` (`fitBody` convex fitting) [RT].
- `rig_torso.hpp` (spine/chest from hierarchy, issue #9), `rig_writer.hpp` (`StudioWriter`), `shape_atlas.hpp` (generated by `build-shape-atlas.py`), `scmi_data.hpp` (generated by `sync-scmi-data.py`) [hdr].
- `physics_profile.cpp/.hpp` — physics-editor model: canonical `physicsOverrides`, `.phy` text, masses, preview queue; mirrored by `physics_profile.lua` [RT].
- `fitter_calibration.hpp` — generated by `export-fit-calibration.py`; **included by nothing** (dead).

**Rendering (client)**
- `renderer.cpp/.hpp` — native drawing via IMaterialSystem: buffers, render-thread queue, GPU skinning, Source shadows/lighting, stats [CL].
- `mesh_topology.hpp` (shared vertex-buffer batching, masked index ranges), `gpu_topology.hpp`, `vertex_upload.hpp`, `cutout.hpp` (Remix alpha cutouts, soft-alpha texel mask and triangle footprints), `light_overlaps.hpp` (flashlight overlap masks), `sdk_end.hpp` (undo SDK macros under GCC) [hdr].
- `render_binaries.hpp` — pinned render-DLL SHA-256; used only by `tests/render_guard_tests.cpp` and `scripts/compatibility_profiles.py`. `thread_sampler.hpp` — main-thread profiler (Windows client only).

**Physics / secondary motion**
- `physics.cpp` — `World` (Bullet world, Source-object mirrors, raycast, grab/physgun, impulses), `Instance` pose/step, realm counting [RT].
- `bridge.cpp/.hpp` — server VPhysics bridge: probe/capture physics and secondary scene, carrier `.phy` bytes [SV].
- `secondary.cpp/.hpp` — `Secondary`: per-character nanoem/Bullet world (hair/cloth), sync/async present, collision flags, sleep, reset [RT]. `secondary_quality.cpp` — accuracy modes, lightweight jiggle [RT].
- `nanoem_backend.cpp` — compiles nanoem `ext/physics_bullet.cc` with `mmd::SecondaryBroadphase`; backend selection; mid-phase gate [NP].
- `broadphase.cpp/.hpp` — `auto/dbvt/dbvt-fast/sap`, SAP→DBVT migration [RT]. `spring_bones.cpp/.hpp` — VRM spring bones [RT].
- `secondary_mt.hpp`, `dbvt_leaf.hpp`, `ordered_dispatcher.hpp` (Bullet-derived MT world, order-preserving DBVT, gated dispatcher), `contact_projection.hpp`, `jiggle_direction.hpp` [hdr].

**Compute solvers**
- `compute_solver.cpp/.hpp` — `ComputeSolver` (Bullet SI subclass): CPU pool / OpenCL (clew) / Vulkan [RT].
- `compute_probe.cpp` — runs worker `--probe-compute`/`--probe-vulkan` in a disposable process (`MMDHL_DISABLE_OPENCL`/`_VULKAN`) [RT].
- `vulkan_solver.cpp/.hpp` — Vulkan compute, own device or DXVK shared compute queue, embedded SPIR-V [RT].
- `compute_rows.hpp`, `opencl_solver_kernel.hpp`, `ordered_joint_setup.hpp`, `dxvk_shared_compute.h` [hdr].

**Compatibility, installation, file access, sharing, Lua bindings**
- `compatibility.cpp/.hpp` — game-library evidence (PE/ELF), profiles from `compatibility_policy.lua`, vtable slot-ownership checks [RT]. `engine_abi.hpp` — vtable slots per compiler (`Dtor` 0 MSVC / 1 GCC) [hdr].
- `installation.cpp/.hpp` — `runtimeIdentity`, `componentIdentity`, `workerSelfTest`, `probeWorker` [RT].
- `file_access.cpp/.hpp` — file access for other addons (grants, requests, reads, path policy) [RT]. `file_access_dialog.cpp` + `file_access_picker.hpp` — worker consent dialogs [WK]. `dialogs_posix.cpp/.hpp` — zenity/kdialog [WK Linux].
- `sharing.cpp/.hpp` — MMDPACK2 transfer packing/validation, mountable GMAs [RT]. `packages.cpp/.hpp` — Workshop package export + mounted-addon scans, limits mirrored in `workshop.lua` [RT].
- `module.cpp` — `GMOD_MODULE_OPEN/CLOSE`, the `mmdhl_native` table (`FUNCTION(x)…END_FUNCTION` + `REGISTER(x)`, realm `#ifdef MMDHL_SERVER`), worker jobs [CL SV]. `prop_bindings.cpp/.hpp` — `Prop*` functions, bundle validation [CL SV].

**Platform layer (Linux) / generated tables**
- `posix.cpp/.hpp` (Windows-service counterparts), `codepages.cpp/.hpp` (CP932/936/949/950 decode), `xpress_huffman.cpp/.hpp` (XPRESS-Huffman decoder) [RT Linux]; `nanoem_unicode.c` [NC Linux].
- `codepage_tables.hpp`, `cp932_table.h` — generated by `scripts/codepage-tables.py` [hdr].

**`native/props/`**
- [PC] `core.cpp/.hpp` (prop `Asset`, `.gmdl` bundles, validation, cache I/O), `trace.cpp` (swept box vs hulls), `render_plan.cpp` (scale bounds, render plan), `image_validation.cpp` (PNG), `material_processing.cpp/.hpp` (material analysis).
- [PI] `import.cpp` (Assimp OBJ/FBX/glTF), `pmx.cpp`, `blend.cpp` (SDNA reader), `zstd_decode.cpp/.hpp`, `collision.cpp` (fast hull or CoACD loaded at run time), `fast_hull.cpp`, `texture_resolver.cpp/.hpp`, `material_overrides.cpp` (`<model>.gmodel.json`).
- `network_path.hpp` — `networkPath()` refusal of UNC/URL/device paths, used wherever paths enter [hdr].

## 4. `renderer/togl/`
`CMakeLists.txt` (`togl_client` → `libtogl_client.so`; needs `-DMMDHL_DXVK_SOURCE`; version script; debug split; CTest `togl_exports`) · `togl.cpp` (ToGL D3D9 interface on DXVK, flushes game-inlined state) · `togl_abi.hpp` (ToGL layouts, `static_assert` offsets) · `togl_internal.hpp` · `backend.cpp/.hpp` (only TU with DXVK headers; dlopens `libdxvk_d3d9.so`) · `gl_tables.cpp` + `gl_layout.inc` (`COpenGLEntryPoints` tables) · `d3dx.cpp` · `exports.map` (131 symbols) · `check_exports.py` (`nm -D` vs map).

## 5. `addon/`
- `addon.json` (title "Model Hotloader"); `data_static/mmdhl/` sample packages (`packages/<id>.json`, `files/<sha256>.dat`, packed on Windows); `materials/mmdhl/` (`toon.*`, `scmi/*.vtf`); `shaders/fxc/mmdhl_{vs20,ps20b}.vcs`; `resource/localization/{en,fr,ja,ko,ru,zh-cn,zh-tw}/mmdhl.properties` (`en` = source + fallback).
- `lua/autorun/mmdhl.lua` — entry: `AddCSLuaFile` list, include order, gate `mmdhl.CheckInstallation()`, `mmdhl.MountPackage`, `mmdhl.Decode`, Entity adapters for `mmdhl_ragdoll`. New top-level Lua files must be registered here.

`lua/mmdhl/` (S shared · C client · V server):

| File | R | Role |
|---|---|---|
| `i18n.lua` | S | `mmdhl.L`, `Localize`, `I.Languages`; works without the native module |
| `installation.lua` / `installation_ui.lua` | S / C | Installation check (`EvaluateInstallation`, `CheckInstallation`, `platformFiles`, wrong-folder roots, renderer, update reminder) / banner and windows |
| `native_policy.lua`, `compatibility_policy.lua` | S | **Generated** release policy / Windows game-DLL profiles (win64 only) |
| `menu.lua`, `client.lua` | C | "External Models" tab; client bootstrap (includes `library.lua`, `ui.lua`, `bodygroups.lua`) |
| `server.lua`, `compat.lua` | V | Net strings, `mmdhl.Spawn`, actions; physgun/gravgun hooks for legacy `mmdhl_ragdoll` |
| `carrier.lua` | S | Carrier helpers (`GetAsset/GetRig/GetMorphs…`), `mmdhl_native_carrier`; client part includes `native_render.lua` (C) |
| `actors.lua`, `materials.lua` | S | NPC/player/arms actors, rig-generator compatibility; material/bodygroup adapters |
| `library.lua`, `ui.lua`, `entity_editor.lua`, `bodygroups.lua` | C | Library model, window, entity editor, bodygroup presets |
| `instances.lua`, `client_ragdolls.lua`, `player_copies.lua`, `first_person.lua` | C | Instance handles, local corpses, first-person-body addon copies, first person |
| `sharing.lua`, `scene_sharing.lua`, `workshop.lua` | S | Model transfers; shared collision scene; Workshop packages |
| `secondary_backend.lua`, `secondary_collision.lua`, `settings.lua` | S | Solver list (`cpu_mt_v2`, `gpu_vulkan`, `reference`…), collide-with bits, default ConVars |
| `physics_settings.lua`, `physics_lod.lua`, `performance.lua` | C | Tuning ConVars, LOD, opt-in perf HUD |
| `physics_reset.lua`, `faceposer.lua`, `native_tools.lua`, `persistence.lua` | S | Reset, Face Poser, stool bones/eyes, dupes/saves |
| `collision_editor.lua`, `file_access.lua` | C | Collision inspector; `mmdhl.FileAccess` + `hook 'MMDHL.RequestUserFile'` |
| `physics_editor.lua` → `physics_profile.lua` (S), `physics_editor_ui.lua` (C) | S | Ragdoll physics editor |
| `bone_mapper.lua` → `bone_mapper_rules.lua` (S), `bone_mapper_ui.lua` (C) | S | Bone assignment window |
| `names.lua`, `terms.lua`, `workshop_export.lua` | C | Translated model names (opt-in service), terms of use, export dialog |
| `debug.lua` | S | Dormant token-gated dev RPC used by `scripts/gamectl.py` |
| `props/shared.lua`, `props/collision.lua`; `props/server.lua`; `props/{render,library,tool,editor}.lua` | S; V; C | Static props |

Also: `entities/mmdhl_ragdoll/{shared,init,cl_init,tool_api}.lua` (legacy backend, `mmdhl_native_carrier 0`), `entities/mmdhl_prop/*` (prop entity), `entities/mmdhl_attach_link.lua` (dupe record of attached prop), `weapons/weapon_mmdhl.lua` (grabber SWEP), `weapons/gmod_tool/stools/mmdhl_prop.lua` (Static Prop tool).

## 6. `scripts/` (81 files)
Use: **B** build · **CI** workflow · **R** release · **G** Windows owned game session (`game-start.ps1` first) · **Gen** rewrites tracked files · **X** research (private data in `validation/`).

| Script | Use | Role |
|---|---|---|
| `bootstrap.py` | B CI | Fetch pinned repos to `vendor/<name>` (`.mmdhl-revision`; refuses a folder at another revision), CoACD wheels (SHA-256; Linux RPATH `$ORIGIN`), nanoem/Bullet patches |
| `patch-bullet-threading.py` | B CI | Idempotent Bullet patches (thread-local counters, soft RNG, heavy joint rows) |
| `build.ps1`, `build-linux.sh` | B CI | Windows / Linux (manylinux container) builds (07) |
| `build-icu.py` | B CI | Static ICU → `vendor/icu-linux64` |
| `build-dxvk.ps1`, `build-dxvk-linux.sh` | B CI | DXVK v3.1.1 → `build-dxvk/` / `build-dxvk-linux64/` (+ `renderer/togl`) |
| `shaders.py`, `fixtures.py` | B Gen | VCS shaders → `addon/shaders/fxc/`; CC0 fixtures → `tests/fixtures/` |
| `native_manifest.py` | B CI R | `native-release.json` + `native_policy.lua` from final binaries; `--renderer`, `--check` |
| `package-dropin.py` | CI R | Drop-in `GarrysMod/` folders + zips in `dist/`; job summary |
| `publish-native-release.py` | R | GitHub release + policy commit + `addon.gma` from a CI run |
| `update-native-policy.ps1`, `package.py` | R legacy | win64-only policy update; Windows source/binary/symbol zips |
| `compatibility_profiles.py`, `validate-render-abi.py` | R Gen | Game-DLL profiles → `compatibility_policy.lua`; offline renderer ABI diff / `--emit-profile` |
| `check-i18n.py`, `check-lua-tests.py` | CI | Translation checks; run `tests/test_*.py` |
| `codepage-tables.py`, `sync-scmi-data.py`, `make-bonemap-fixtures.py` | Gen | `codepage_tables.hpp`+`cp932_table.h` (`--check`); `scmi_data.hpp`; `bonemap/rules.json` |
| `build-shape-atlas.py`, `evaluate-shape-atlas.py`, `calibrate-fitter.py`, `export-fit-calibration.py`, `corpus.py`, `check-torso.py`, `vulkan-rigs.py` | X | Fitter corpus audits (atlas → `shape_atlas.hpp`), torso check, Vulkan-vs-CPU survey |
| `install.ps1`, `install-ame-compat.ps1` | dev | Install `build/bin/Release` into a Windows game (`-InstallAddon`); AME patch |
| `game-start.ps1`, `game-stop.ps1`, `game-focus.ps1`, `gamectl.py`, `reload-lua.py` | G | Owned session lifecycle; file RPC (`gamectl.py server …` / `client …`); Lua reload |
| `stage-candidate.ps1`, `multiplayer-session.ps1`, `prepare-multiplayer.py` | G | Candidate staging/restore; dedicated server + 2 clients |
| `prepare-acceptance.py`, `import-model.py` | G | Import the 3 private acceptance PMX (`~/Desktop/test_models`) → `validation/acceptance-models.json`; import one model |
| `capture-dump.py`, `capture-native.py`, `capture-native-motion.py`, `capture-window.py`, `measure-scene.py`, `measure-lod.py`, `measure-vanilla.py` | G | Dumps, captures, frame-cost measurements |
| `test-character-import.py` | CTest | `character_import_worker` (worker `status.json` contract) |
| `test-carrier.py`, `test-native-{anatomy,tools,extras,interactions,stability,visuals,migration}.py`, `test-presentation.py`, `test-portable-fit.py` | G | Native-carrier gates (docs/DEBUGGING.md) |
| `test-compatibility.py`, `test-compiled-eyes.py`, `test-control-roots.py`, `test-import-feedback.py`, `test-motion-controls.py`, `test-spawn-settings.py`, `test-global-settings.py`, `test-qol.py`, `test-library-ui.py` | G | Feature regressions (`test-library-ui.py` runs `test-qol.py`) |
| `test-secondary-{scenes,motion,backends}.py`, `test-adaptive-physics.py`, `test-performance.py`, `test-scene.py` | G | Secondary physics / performance |
| `test-installation-game.py`, `test-installation-failures.py` | G | Installation smoke / fault injection |
| `test-multiplayer.py`, `test-props-multiplayer.py`, `test-transfer-speed.py` | G | Dedicated server + 2 clients |
| `test-game.py`, `test-vanilla.py`, `test-soak.py` | G | Legacy-backend suites |

## 7. `tests/`
- **CTest C++**: `tests/*.cpp`, `tests/props/native_tests.cpp` (+ `tests/props/*_tests.hpp`); 36 `add_test` (35 Windows, 34 Linux) — list in 07. Helpers `test_platform.hpp`, `wide_main.hpp`, `crash_report.hpp`, `contact_fixture.hpp`. Built, not registered: `retarget_probe.cpp`, `physics_parameters.cpp`, `physics_profile.cpp` (`mmdhl_profile`), `nanoem_replay.cpp` (`mmdhl_replay`), `skinning_analysis.cpp`, `vulkan_compare.cpp` (Win).
- **Offline Python/lupa**: `tests/test_*.py` (49; real addon Lua against simulated games; helpers `lua_source.py`, `lua_i18n.py`) via `scripts/check-lua-tests.py`, which skips `test_native_installer.py` (needs `build/bin/Release` + pwsh).
- **In-game probes**: `tests/game/*.lua` (27), run by `gamectl.py --file` or `test-*.py` in an owned session (most assert `MMDHL_DEBUG_TOKEN`).
- **Fixtures**: `fixtures.py` output (`cloth21.pmx`, `native-*.pmx/.pmd`, `corrupt-*.pmx`, `cutout-*.png`, `checker.dds`; CC0 `LICENSE.txt`); `bonemap/rules.json` (make-bonemap-fixtures.py), `slots.json` (`mmdhl_character_import_tests --write-slots`), `proposal.json` (`mmdhl_torso_tests --record-window`); `physics/golden.json` (Win) + `golden-linux64.json` (`mmdhl_physics_profile_tests --record`, only for a deliberate fit change), `canonical_cases.json`, `hl2_citizen_phy.txt`; `props/cube.*`, `colors.png`. `tests/v2-assets.json`: private acceptance assets by hash.

## 8. `docs/`
Current references (still verify): `LINUX.md`, `NATIVE_BACKEND.md` (`mmdhl.Spawn` API), `ABI.md`, `INSTALLATION_VALIDATION.md`, `DXVK_PACKAGE.md`, `CHARACTER_IMPORT.md`, `VRM_IMPORT.md`, `STATIC_PROPS.md`, `PHYSICS_EDITOR.md`, `PHYSICS_TUNING.md`, `TORSO_FIT.md`, `FILE_ACCESS.md`, `IMPORT_ERRORS.md`, `MODEL_NAMES.md`, `MODEL_TERMS.md`, `TRANSLATING.md`, `WORKSHOP_PACKAGES.md`, `MULTICORE_RENDERING.md`, `COMPUTE_BACKENDS.md`, `VULKAN_COMPUTE.md`, `DEBUGGING.md`. Banner "Historical … legacy custom-entity backend": `API.md` (yet has 2.3.0 additions), `IMPLEMENTATION.md`, `VALIDATION.md`. Everything else (`*_VALIDATION.md`, `*_PLAN.md`, `*_RESULTS.md`, `RTX_*`, `VULKAN_MERGE.md`, `DXVK_VANILLA.md`…) is a dated report.

## 9. Where do I change X?

| Task | Where |
|---|---|
| Lua API for other addons | Owning `addon/lua/mmdhl/*.lua` (`server.lua` `mmdhl.Spawn`, `file_access.lua` `mmdhl.FileAccess`); events are `hook.Run('MMDHL.*')`; doc `docs/NATIVE_BACKEND.md`/`FILE_ACCESS.md`; test `tests/test_*.py` |
| New Lua file | `addon/lua/mmdhl/x.lua` + `AddCSLuaFile`/`include` in `lua/autorun/mmdhl.lua` (or include from owner) |
| Translated phrase | `en/mmdhl.properties` (`mmdhl.area.name`, context comment), `L'area.name'`; dynamic keys `-- i18n-keys:`; other langs `# en:` + value; `python scripts/check-i18n.py` |
| New language | `check-i18n.py --sync <lang>` (only if asked) + `I.Languages` in `i18n.lua` |
| Native Lua binding | `native/module.cpp` `FUNCTION`+`REGISTER` (realm guard); props `prop_bindings.cpp`; Lua feature-detects (`if native.X then`); capabilities in `GetCapabilities` |
| Build flag | `CMakeLists.txt` `if(MSVC)`/GCC branches (`MMDHL_WARNINGS`, link options); shim `renderer/togl/CMakeLists.txt` |
| New CTest | `tests/x.cpp` + `add_executable`/`add_test` (WORKING_DIRECTORY for repo paths) |
| Offline Lua test | `tests/test_x.py` (auto-discovered; `NEEDS_BUILD` if it needs binaries) |
| In-game probe | `tests/game/x.lua` + `scripts/test-x.py` via `gamectl.execute` |
| Package contents | `scripts/package-dropin.py` + `native_manifest.py` `PLATFORM_FILES` + `publish-native-release.py` `PACKAGES/RECORD_FILES/check_packages` + workflow artifact names + `tests/test_release_evidence.py` |
| Native file names | `CMakeLists.txt` `MMDHL_*_FILE`, `release.hpp.in`, `native_manifest.py`, `package-dropin.py`, `installation.lua` `platformFiles`, `scripts/install.ps1` |
| Installation-check rules | `installation.lua`, `installation_ui.lua`; `native/installation.cpp`, `compatibility.cpp`; `tests/test_installation*.py`, `test_native_update_*.py` |
| Policy record | `scripts/publish-native-release.py publish` (maintainer only); never hand-edit hashes |
| Game-DLL ABI profile | `compatibility_profiles.py --audited` / `validate-render-abi.py --emit-profile` → `compatibility_policy.lua`; slots `native/engine_abi.hpp`; `docs/ABI.md` |
| Release label | `project(… VERSION)` + `set(MMDHL_RELEASE "x" CACHE …)` (CI `sed` parses this exact form) |
| Pinned dependency | `dependencies.lock.json` (+ remove stale `vendor/<name>`), `THIRD_PARTY.md`, `licenses/` |
| Bullet/nanoem patch | `scripts/bootstrap.py` / `patch-bullet-threading.py`; `THIRD_PARTY.md` |
| DXVK version/patch | `$tag/$commit` in `build-dxvk.ps1` and `tag=/commit=` in `build-dxvk-linux.sh`; `patches/dxvk/` or `patches/dxvk-linux/` |
| Linux ToGL shim | `renderer/togl/*` (`exports.map`, `togl_abi.hpp`) |
| Source shaders | `shaders/*.hlsl` → `python scripts/shaders.py` (Windows) → commit `addon/shaders/fxc/*.vcs` |
| GPU solver | `shaders/vulkan/solver.comp`, `native/vulkan_solver.cpp`, `compute_rows.hpp` |
| Static-prop formats | `native/props/{import,pmx,blend}.cpp`; `addon/lua/mmdhl/props/*` |
| FBX/glTF/DAE + bone window | `character_import.cpp`, `humanoid_map.cpp`, `humanoid_slots.hpp` ⇄ `bone_mapper_rules.lua` (+`slots.json`) |
| Carrier fit / rig format | `native/rig.cpp/.hpp` (`RigGenerator`), `fitter.cpp`, `rig_torso.hpp`; `tests/fixtures/physics/golden*.json` |
| Physics editor rules | `native/physics_profile.cpp` ⇄ `physics_profile.lua` (`canonical_cases.json`) |
| Worker command | `native/worker.cpp` |
| File access for addons | `file_access.cpp`, `file_access_dialog.cpp`, `dialogs_posix.cpp`, `file_access.lua` |
| Workshop packages / transfers | `packages.cpp`, `sharing.cpp`; `workshop.lua`, `workshop_export.lua`, `sharing.lua` |
| CI | `.github/workflows/package.yml` |
| Release notes / Workshop text | `ReleaseNote.md` (new `#` section on top), `README.md`; `Translation Templates Write.txt` |
