# Third-party components

Exact source revisions are recorded in `dependencies.lock.json`; bootstrap records each downloaded archive's SHA-256 under `vendor/`. Source archives are fetched from the original GitHub repositories. Release ZIPs carry the applicable notices in `licenses/`; the drop-in packages carry them in `GarrysMod/bin/win64/LICENSES` (DXVK's in `DXVK-LICENSES` beside it). The source of every release is this repository.

| Component | Use | License |
|---|---|---|
| [hkrn/nanoem](https://github.com/hkrn/nanoem) | PMD/PMX parser, Unicode conversion, pinned Bullet physics extension | MIT (nanoem component; no emapp binary) |
| [g-truc/glm](https://github.com/g-truc/glm) | nanoem physics rotation conventions | MIT option |
| [bulletphysics/bullet3](https://github.com/bulletphysics/bullet3) | Rigid/soft dynamics and constraints | zlib |
| [danielga/garrysmod_common](https://github.com/danielga/garrysmod_common) | GMod Lua module interface | BSD-style, supplied license |
| [danielga/sourcesdk-minimal](https://github.com/danielga/sourcesdk-minimal) | Source material/mesh headers and import libraries | Valve Source SDK license and notices |
| [nlohmann/json](https://github.com/nlohmann/json) | Internal metadata and cache serialization | MIT |
| [nothings/stb](https://github.com/nothings/stb) | Image decoding and PNG encoding | MIT option |
| [SCell555/ShaderCompile](https://github.com/SCell555/ShaderCompile) | Build-time shader compiler, not shipped in the runtime ZIP | Upstream tool terms |
| [assimp/assimp](https://github.com/assimp/assimp) | Static props (OBJ, FBX and glTF) and characters in other formats (FBX, glTF and COLLADA/DAE) parsed in `mmdhl_worker.exe` only (import-only build with its bundled zlib, poly2tri, pugixml, rapidjson, utf8cpp, earcut and openddlparser; the COLLADA importer reads XML through pugixml) | BSD-3-Clause (pugixml: MIT); bundled notices in `licenses/` |
| [zeux/meshoptimizer](https://github.com/zeux/meshoptimizer) | Static props: position welding and simplification of the collision copy | MIT |
| [SarahWeiii/CoACD](https://github.com/SarahWeiii/CoACD) 1.0.14 | Static props: optional detailed (multi-hull) collision. `lib_coacd.dll` is the pinned upstream wheel binary (SHA-256 in `dependencies.lock.json`), loaded by the worker only | MIT; its bundled OpenVDB, oneTBB, Boost, spdlog, fmt and CDT notices are in `licenses/` |
| [KhronosGroup/Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | Vulkan API declarations for the `gpu_vulkan` solver, which loads `vulkan-1.dll` at run time | Apache-2.0 or MIT |
| [KhronosGroup/glslang](https://github.com/KhronosGroup/glslang) | Build-time compiler of `shaders/vulkan/solver.comp` to the embedded SPIR-V; not shipped | BSD-3-Clause and others (its `LICENSE.txt`) |
| [doitsujin/dxvk](https://github.com/doitsujin/dxvk) 3.1.1 | Default drop-in package only: `bin/win64/d3d9.dll`, Garry's Mod's Direct3D 9 on Vulkan, built by `scripts/build-dxvk.ps1` with `patches/dxvk` (shared compute queues for the Vulkan solver, a built-in Garry's Mod profile). Links its dxbc-spirv and libdisplay-info subprojects | zlib (DXVK), MIT (dxbc-spirv, libdisplay-info); notices in the package's `bin/win64/DXVK-LICENSES` folder |

The static-prop importer (`native/props/`) is ported from the GModel Hot Loader project by the same author, including its cache format, fast single-hull collider, material analysis, texture resolver and render plan.

The bone-name tables of the character importer (`native/humanoid_map.cpp`) were written for this project from public naming conventions (Mixamo, Unreal, Unity/VRoid, Rigify, 3ds Max Biped, Daz, ValveBiped and the MMD standard names); no third-party tables or code are used.

The shader compiler release is `build_235_20231013.2`; its executable SHA-256 is `c341f397483b70b4e2260211dcba772e7c3e0e82db0edb9c15b75abb8e12db70`. `scripts/shaders.py` verifies it before execution.

Five small source patches are applied reproducibly by `scripts/bootstrap.py`:

1. Make Bullet's existing soft constraint solve method virtual, allowing the bridge to observe momentum changes after that solver phase without changing the solver itself.
2. Fix nanoem's soft-anchor vertex accessor to use `anchor->vertex_index`, rather than `anchor->rigid_body_index`. The generated cloth fixture exercises distinct indices and catches the original bug.

3. Initialize both inverse rigid-body transforms to identity in nanoem's Bullet joint constructor. A null endpoint is a valid world anchor and must not leave its joint frame uninitialized. The world-anchor fixture and independent replay cover this path.
4. Build a nanoem soft-body rope from the vertices of its own material, in first-use order, with its links at the authored spacing. Upstream sized the rope from the whole model's vertex count and end points, adding nodes that belong to no vertex. The rope fixtures, including one with extra unrelated vertices, cover this.
5. Resolve nanoem soft-body pins, which PMX stores as model vertex indices, to the node made from that vertex; upstream used them as node indices and pinned the wrong vertices. An anchor between a pinned node and a massless follow-bone body is dropped: it constrains nothing, and Bullet cannot invert its zero impulse matrix. The cloth and rope fixtures cover this.

Rendering references consulted: [Facepunch custom shaders](https://wiki.facepunch.com/gmod/custom_shaders), [screenspace_general parameters](https://wiki.facepunch.com/gmod/Shaders/screenspace_general), and [meetric1's shader guide](https://github.com/meetric1/gmod_shader_guide). The shaders in this project are authored for this runtime, not copied MMD effect packages.

No user model, MMD executable, third-party game executable, or engine DLL is bundled. Generated files under `tests/fixtures/` are dedicated to the public domain under CC0; regenerate them with `scripts/fixtures.py`.

The synchronization behavior in `native/secondary.cpp` and the independent reference equations in `tests/nanoem_replay.cpp` are adapted from nanoem emapp `model/RigidBody.cc` and `model/SoftBody.cc` at the pinned revision, Copyright (c) 2015-2023 hkrn All rights reserved. These files are provided under MPL-2.0; their source is published in this repository. See `licenses/nanoem-MPL-2.0.txt`. The nanoem parser (including the accessor fix described above) and physics extension retain their MIT license.

## SCMI material preset

The three small generic VTF maps in `addon/materials/mmdhl/scmi/` are reused from the user-provided SCMI/corpus shared material set (`E:/G/Upload/zibai/zibai/materials/models/sheepylord/shared`), as requested by the author of these ports. They contain the flat normal, toon lighting ramp and Phong exponent response, not character artwork. The matching VMT recipe is the SheepyLord legacy MMD2SMD preset (boost 24 / Fresnel [0 0 1] / rim 2). Character base textures are never packaged.

| Map | SHA-256 |
|---|---|
| normal.vtf | 856c88d845d96b51dff2a39906a0a533c551a0f19e26e55da692057ebff84b42 |
| lightwarptexture.vtf | 53d3d28e87a3ea40454557d54f0e9a03d40b81f438287b42f465ba83531e1491 |
| phong_exp.vtf | 8da92b304b5d97c72338fbde8079da59338ec0d3ac22fd4cf1609375688f8c78 |


## Portable collision atlas

`native/shape_atlas.hpp` contains normalized anatomical convex references and fitted support-plane coefficients derived from the user's SCMI collision library, as authorized for this importer. It contains no source paths, model names, meshes for rendering, textures, or PMX files. Calibration uses 159 registered training pairs; 47 other registered pairs are held out, including the supplied character families. Private correspondence/audit files remain outside the package. `scripts/build-shape-atlas.py` documents the deterministic derivation, with NumPy/SciPy required only by that development tool. The runtime requires neither Python nor access to SCMI or the source library. The half-maximum summed-weight selection follows SCMI's collision-fitting approach.

## System Unicode naming

Engine-safe path romanization calls the [Windows ICU C API](https://learn.microsoft.com/en-us/windows/win32/intl/international-components-for-unicode--icu-), using the system-provided `icu.dll` on Windows 10 1903 or newer. No ICU DLL or Unicode data is redistributed. `native/naming.cpp` contains the project-authored material terminology table; original authored names remain in metadata.

## Independent-world threading and fixed-step integration (0.7.0)

`scripts/patch-bullet-threading.py`, invoked by every build, isolates Bullet's diagnostic allocation/solver counters and debugger deactivation flag per thread. Soft-body repulsion keeps its random state per soft body, removing the shared mutable seed between character worlds. The existing equations, iteration counts and contact/constraint algorithms are retained. One altered line in `btSequentialImpulseConstraintSolver.cpp` (marked `MMDHL`) keeps joint rows active whose inverse-mass sum is below `FLT_EPSILON`: Bullet 2.75, which MikuMikuDance uses, solved them, and PMX models author locked chains with masses near 1e8 (wings, for example) that otherwise come off their anchors. Only rows with a sum at or below 1e-20 are skipped. `BT_THREADSAFE` is enabled consistently for all Bullet consumers; profiling globals are disabled with `BT_NO_PROFILE`.

`native/nanoem_backend.cpp` adds a narrow fixed-tick entry point to the pinned backend. The caller schedules complete 1/60-second ticks, so the shim calls Bullet with zero internal substeps and performs the same sparse-SDF maintenance. This avoids running a second accumulator and graphics interpolation inside Bullet. The independent replay still invokes the upstream nanoem stepping API, and compares actual rigid-body world transforms and bone-feedback equations.

`native/dbvt_leaf.hpp` is an altered, zlib-licensed adaptation of Bullet's `btDbvtBroadphase.cpp` and `btDbvt.h` tree traversal by Nathanael Presson / Erwin Coumans. It specializes a tree-versus-leaf query while retaining the original tree updates and overlap-pair visitation order. The original Bullet DBVT remains available as a control; direct nanoem reference worlds always use it. Optional timing instrumentation is confined to explicitly configured profiling builds.

## Optional compute backends (0.8.0)

`native/secondary_mt.hpp`, `native/ordered_joint_setup.hpp`, and `native/opencl_solver_kernel.hpp` adapt Bullet's zlib-licensed dynamics and sequential impulse solver, with complete original notices retained and alterations marked. The ordered dispatcher preserves Bullet's reference manifold insertion/removal order while using its thread-safe allocators. The custom OpenCL kernel executes constraint iterations; the Bullet 3 full GPU rigid-body pipeline is not shipped as a working MMD backend.

The optional runtime loader is Bullet's vendored CLEW: `clew.c`, Copyright (c) 2009 Organic Vectory B.V., George van Venrooij, under Boost Software License 1.0; `clew.h`, Copyright (c) 2009-2011 Organic Vectory B.V., KindDragon, under MIT, including Khronos OpenCL declarations. See `licenses/clew-Boost-1.0.txt` and `licenses/clew-MIT-Khronos.txt`. OpenCL.dll is supplied by the user's graphics driver and is not redistributed. GPU probing runs in an owned, timeout-limited worker process.
