# 01 — Architecture and structure diagrams

The whole system on one page: what ships, which process runs what, how an import becomes a
character in the game, and which identities tie the pieces together. The text after each
diagram names the chapter that owns the details. Verified against `main` @ `9e02a27` (release 2.3.0).

## What ships, and how it updates

| Deliverable | Contents | Channel | Updates |
|---|---|---|---|
| Workshop addon | `addon/` → `addon.gma` (Lua, localization, shaders `mmdhl_*.vcs`, materials, sample packages in `data_static/`) | Steam Workshop item 3810025467 | whenever the maintainer repackages `addon.gma` |
| Native release (per platform) | game modules `gmcl_mmdhl_<p>.dll` + `gmsv_mmdhl_<p>.dll`, runtime library (`mmdhl_runtime_win64.dll` / `libmmdhl_runtime_linux64.so`), worker (`mmdhl_worker.exe` / `mmdhl_worker_linux64`), CoACD (`lib_coacd.dll` / `lib_coacd.so` + libgomp), shaders, notices | GitHub release packages (`-win64-vulkan`, `-win64-opengl-remix`, `-linux64-vulkan`, `-linux64-opengl`) installed by hand into the game folder | per release label (`MMDHL_RELEASE`, e.g. `2.3.0`) |
| Renderer (optional, inside the `-vulkan` packages) | DXVK `d3d9.dll` (Windows) / `libtogl_client.so` + `libdxvk_d3d9.so` (Linux) | same packages | with the native release |

The addon and the native release update independently. `addon/lua/mmdhl/native_policy.lua` is the
addon's list of known native releases (sizes, SHA-256, renderer records, approvals, the recommended
release). The addon always tries to load whatever binaries are installed and only *warns* about
unknown or mismatched ones ([03](03_engine_integration.md), [07](07_build_test_release.md)).

## Binary components (CMake targets)

```mermaid
flowchart TB
  subgraph vendor["vendor/ (pinned by dependencies.lock.json, fetched by scripts/bootstrap.py)"]
    NAN["nanoem (PMX/PMD parser + Bullet physics ext)"]
    BUL["Bullet 3 (patched)"]
    ASS["assimp (import-only build)"]
    MOP["meshoptimizer"]
    COA["CoACD wheel binary"]
    ICU["ICU 74.2 (Linux only, static)"]
    SDK["garrysmod_common, sourcesdk-minimal"]
    GLS["glslang (build-time)"]
  end
  RT["mmd_runtime (SHARED)<br/>mmdhl_runtime_win64.dll / libmmdhl_runtime_linux64.so<br/>model, rig, fitter, humanoid_map, vrm, spring_bones, assets,<br/>physics, secondary, compute/vulkan solvers, broadphase, jobs,<br/>sharing, packages, scene_share, physics_profile, file_access,<br/>installation, compatibility, import_error, naming"]
  PC["mmdhl_props_core (STATIC)<br/>props bundle decode, render plan, trace"]
  PI["mmdhl_props_import (STATIC)<br/>OBJ/FBX/glTF/PMX/Blend import, hulls, CoACD"]
  CI["mmdhl_character_import (STATIC)<br/>FBX/glTF/DAE characters"]
  WK["mmdhl_worker (EXE)<br/>pickers, imports, probes, self-test"]
  CL["mmdhl_client (MODULE) gmcl_mmdhl_PLATFORM.dll<br/>module.cpp, prop_bindings.cpp, renderer.cpp"]
  SV["mmdhl_server (MODULE) gmsv_mmdhl_PLATFORM.dll<br/>module.cpp, prop_bindings.cpp, bridge.cpp"]
  NAN --> RT
  BUL --> RT
  ICU --> RT
  GLS -. "SPIR-V for shaders/vulkan/solver.comp" .-> RT
  RT --> PI
  PC --> PI
  ASS --> PI
  MOP --> PI
  PI --> CI
  ASS --> CI
  RT --> WK
  PI --> WK
  CI --> WK
  COA -. "loaded at run time beside the worker" .-> WK
  RT --> CL
  PC --> CL
  SDK --> CL
  RT --> SV
  PC --> SV
  SDK --> SV
```

Facts that follow from the graph: assimp, CoACD, the Blender reader and every parser of untrusted
*source* files run only in the worker process; the game modules only decode the project's own
cache formats. The runtime is one shared library so the client module, the server module (listen
server: same process) and the worker share code and state per process. `renderer.cpp` (native
drawing) exists only in the client module, `bridge.cpp` (VPhysics/engine access) only in the
server module.

## Processes and realms

```mermaid
flowchart LR
  subgraph client["Game client process (gmod / gmod.exe)"]
    LC["client Lua (addon/lua)"]
    MC["gmcl module + runtime"]
    REN["engine: materialsystem, studiorender,<br/>client.dll, D3D9 or DXVK"]
    LC <--> MC
    MC --> REN
  end
  subgraph server["Server realm (same process in single player / listen server; srcds otherwise)"]
    LS["server Lua"]
    MS["gmsv module + runtime"]
    VP["engine: VPhysics, engine"]
    LS <--> MS
    MS --> VP
  end
  subgraph worker["Worker process (one job at a time, spawned by a module)"]
    W["mmdhl_worker: file pickers,<br/>character/prop imports, GPU probes,<br/>installation self-test"]
  end
  DATA[("garrysmod/data/mmd_hotloader/<br/>assets, rigs, fits, library, static, ...")]
  MC -- "job dir: request.json / status.json / result" --> W
  W --> DATA
  MC <--> DATA
  MS <--> DATA
  LC <-- "net messages (mmdhl_*), chunked transfers" --> LS
```

## End-to-end: from a file to a simulated character

```mermaid
flowchart TD
  PICK["Player: External Models > Import (library.lua)"] --> JOB["native BeginImport → worker job"]
  JOB --> PARSE["worker: parse PMX/PMD (nanoem), VRM/glTF (vrm.cpp),<br/>FBX/glTF/DAE (assimp, character_import.cpp); textures"]
  PARSE --> ASSET[("assets/HASH/ manifest + source data")]
  ASSET --> MAP["bone mapping: humanoid_map.cpp (+ bone_mapper UI when unsure)"]
  MAP --> FIT["fit a Source rig: fitter.cpp, rig*.cpp,<br/>shape_atlas / scmi_data references"]
  FIT --> CAR["carrier: MDL/VVD/VTX + .phy text (bridge.cpp physicsText)<br/>key = carrierFitKey(...)"]
  CAR --> RIG[("rigs/KEY/carrier.gma")]
  RIG --> MOUNT["mount + precache in the engine; spawn as<br/>prop_ragdoll / NPC / player model (Lua: carrier.lua, server.lua)"]
  MOUNT --> PHYS["server: VPhysics ragdoll from the .phy"]
  MOUNT --> SEC["client: secondary motion world per character<br/>(nanoem/Bullet, cpu_mt_v2 default)"]
  SEC --> DRAW["client: native renderer (renderer.cpp)<br/>GPU skinning, materials, queued render context"]
  PHYS -. "kinematic colliders each tick" .-> SEC
  MOUNT --> SHARE["multiplayer: server sends assets/carriers to clients<br/>(sharing.lua / sharing.cpp, approvals)"]
```

Details: import and carriers [02](02_character_import_pipeline.md); mounting, spawning,
rendering, networking, compatibility [03](03_engine_integration.md); secondary motion and the
ragdoll `.phy` [04](04_physics.md); static props take a separate path (bundle `.gmdl`, Lua-built
meshes) [05](05_static_props.md); the Lua side of every flow [06](06_lua_addon.md).

## Identities and versions

| Identity | Defined in | Meaning / rule |
|---|---|---|
| `MMDHL_RELEASE` (e.g. `2.3.0`) | `CMakeLists.txt` | native release label; policy records are keyed by it |
| Build ID `<commit12>-<UTC stamp>` | CMake configure (`MMDHL_BUILD_ID`) | embedded in every binary; module and runtime must agree |
| `ApiVersion=1` | `native/runtime.hpp` | Lua binding API generation (`GetCapabilities().api`) |
| `MMDHL_INSTALL_API=1` | `native/release.hpp.in` | installation-identity protocol; 0 = legacy, update required |
| `RigVersion=3`, `RigGenerator=31`, `RigGeneratorMinLoadable=30` | `native/rig.hpp` | carrier format and fitter generation; older-but-loadable carriers stay valid for saves/dupes |
| carrier fit key | `carrierFitKey` (runtime) | hashes the fit inputs; unedited rigs keep their 2.2 key |
| `PhysicsSchema=1`, `PhysicsWriterVersion=1` | `native/physics_profile.hpp` | physics-editor data and `.phy` text writer |
| prop bundle id = SHA-256 of `.gmdl` | `native/props/core.cpp` | content address; magic `GMLHOT1`, manifest `version` 1 |
| policy record | `addon/lua/mmdhl/native_policy.lua` | sizes/SHA-256 of each shipped file (+ renderer); `approved`, `recommended`, `platforms.linux64` |

## Safety model (why the code is shaped like this)

- **Untrusted inputs** (model files, textures, network payloads, Workshop packages) are parsed in
  the worker or decoded with explicit limits; every numeric input path is "finite or fail".
- **Unknown game builds**: engine interfaces are called by vtable slot; `native/compatibility.cpp`
  checks ownership of slots and pinned RVAs/evidence per game binary, and refuses *individual
  engine calls* it cannot make safely, never the whole module. Findings surface as warnings.
- **Mixed versions** everywhere (addon vs binaries, server vs clients, saves from older
  releases): formats are versioned and additive; capabilities are queried, not assumed.

## Platforms

| Platform id | Game | Notes |
|---|---|---|
| `win64` | Windows x64: the x86-64 branch's `bin/win64/gmod.exe`, and the main branch's `gmod_win64.exe` | runtime also copied to `bin/win64/` (the game loads it from beside the exe) |
| `linux64` | Linux, Garry's Mod x86-64 branch (`bin/linux64/gmod`) | built in the manylinux_2_28 container (glibc 2.28), every file in `garrysmod/lua/bin`, `$ORIGIN` RPATH; the 32-bit default branch is unsupported (reported as an unsupported platform) |
