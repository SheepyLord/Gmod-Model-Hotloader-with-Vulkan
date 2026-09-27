# Secondary-physics and frame-time plan (proposal for 0.8)

Written 2026-09-21 from the 0.7.0 validation data (`validation/motion-release-*.json`), a code read of the native runtime, and new standalone measurements taken with the profiling harness added on this branch (`tests/physics_profile.cpp`, `mmdhl_profile.exe`). Everything below is a plan for evaluation, not a claim of results. Numbers marked *standalone* were measured outside the game on the same Ryzen 9 9950X3D.

## 1. Where the frame time goes today

Per frame, the client does (all on the main thread unless stated): `PreRender` → Lua reads the carrier bone palette → `PrepareFrame` fans out one job per character (physics catch-up steps, then deformation in 4096-vertex chunks) and **waits at a barrier** → engine renders: sunlight shadow pass (per material, through the hooked `ShadowBuild`), colour pass (per material `native.Draw`; the first draw of a frame uploads all vertex buffers), optional flashlight depth passes.

Measured p50 stage costs from the release runs (ms):

| Stage | 1 char (Xin) | 3 chars | 10 chars |
|---|---:|---:|---:|
| Frame | 8.88 | 17.38 | 65.28 |
| `prepareWallMs` (barrier) | 5.02 | 7.09 | 28.43 |
| `physicsWorkMs` (sum of job time) | 3.19 (1 step) | 12.71 (3 steps) | 145.85 (40 steps) |
| `deformWorkMs` (sum) | 1.58 | 3.80 | 23.82 |
| `nativeRenderMs` (colour pass incl. upload) | 0.97 | 2.54 | 11.43 |
| of which `uploadMs` (lock+fill+unlock) | 0.55 | 1.30 | 7.75 (1.31+5.22+1.22) |
| `shadowMs` | 0.80 | 2.05 | 9.93 |
| `meshDrawMs` | 0.16 | 0.48 | 1.49 |
| Pose capture (Lua) | 0.13 | 0.31 | 0.93 |
| Empty scene frame | 3.35 | | |

Three findings drive the plan:

1. **The barrier equals the slowest character's serial step chain.** Three characters: Xin 4.57 ms, Cyrene 3.01 ms, Sandrone 5.39 ms per 60 Hz step; `prepareWallMs` 7.09 ≈ Sandrone's step + its deformation. Adding workers cannot help this; only a faster or overlapped single step can. The p95 (23.86 ms) is the frames that needed two steps (`simulationSteps` p95 = 6).
2. **Ten characters sit in a catch-up equilibrium.** Every frame runs the maximum four steps per character (40 total), 146 ms of physics job time per frame on 14 workers. The frame is 65 ms, four steps cover 66.7 ms, so the simulation only just keeps real time and carries 0.18 s of debt. Cutting per-step cost *or* the non-physics part of the frame breaks the spiral: fewer steps per frame → shorter frames → fewer steps.
3. **The non-physics part of the ten-character frame is ~37 ms on its own** (upload 7.75, shadow 9.93, draws, Lua, engine). Even free physics would leave ten characters at ~27 FPS. Rendering-side work is therefore part of this plan, not optional.

Per-character physics job time in the ten-character run (4 steps each, p50): zankou 29.0, ホタル 23.6, Sandrone 22.7, 克罗瑞娜 18.3, Xin 13.1, Daniya 12.4, Cyrene 10.5, March 7th 7.7, 大国主 6.6, 初雪 2.1 ms. Deformation is dominated by vertex count (Daniya 295 k vertices: 6.3 ms; 初雪 358 k: 3.1 ms).

## 2. Inside one Bullet step (standalone profile)

`mmdhl_profile.exe` drives one character with a pelvis-relative sinusoidal motion similar to `tests/game/native-stress.lua`, collision mode 0, and reports Bullet's own `CProfileManager` tree (`-DMMDHL_BULLET_PROFILE=ON`).

| Character | Step (ms) | Bodies (sphere/box/capsule) | Joints / rows | Overlapping pairs | Contacts | Islands (largest: bodies/rows) |
|---|---:|---|---:|---:|---:|---|
| Xin | 2.35 | 686 (91/296/299) | 946 / 5153 | 2464 | 279 | 55 (332 / 2851) |
| Sandrone | 2.98 | 571 (2/430/139) | 873 / 4361 | 5641 | 378 | 13 (438 / 3759) |
| zankou | 2.85 | 718 (16/178/524) | 926 / 4861 | 4453 | 218 | 13 (203 / 1224, then 124, 98, 95, 60, 56) |

Phase shares of the step (Xin / Sandrone / zankou):

| Phase | Xin | Sandrone | zankou |
|---|---:|---:|---:|
| `updateAabbs` (dbvt broadphase update) | 33 % | 36 % | 41 % |
| `dispatchAllCollisionPairs` (narrowphase) | 23 % | 27 % | 21 % |
| Solver iterations (10 × `solveSingleIteration`) | 19 % | 17 % | 16 % |
| Solver setup (`convertJoints` + `convertBodies`) | 12 % | 13 % | 11 % |
| Island build / batching overhead | 5 % | 6 % | 5 % |
| Integration, activation, misc | 4 % | 1 % | 2 % |

Observations:

- **Collision detection is more than half the step**, and most of it is the broadphase *update*, not pair finding. All joints are `btGeneric6DofSpringConstraint` (type 9), ~5.4 rows each. Iterations cost ~69 µs each (10 → 1 iteration reduced the Xin step from 2.34 to 1.72 ms), so the joint solve is already cheap relative to collision work.
- **Pairs vastly outnumber contacts** (2464 pairs → 279 contacts for Xin). Each pair is a narrowphase call; most produce nothing. Box-box and box-capsule pairs use SAT / GJK; sphere and capsule pairs use analytic paths.
- **Island structure differs per model.** zankou splits into six similar islands (parallel island solving helps); Sandrone is one 438-body island (it does not).
- **In-game steps cost 1.4–1.9× the standalone number** (Xin 3.19 ms in-game vs 2.35). Candidate causes: the mode-2 map mirror (`btBvhTriangleMeshShape` pair for every body), memory/cache contention between concurrent worlds, and worker threads landing on the non-V-cache CCD or SMT siblings. This needs an in-game A/B (see §7).

### Broadphase experiment (standalone, `--broadphase N`)

| Character | dbvt (pinned) | dbvt + deferred collide | 32-bit sweep-and-prune |
|---|---:|---:|---:|
| Xin | 2.41 ms | 2.36 ms | **1.81 ms (−25 %)**, pairs 2464 → 2065 |
| Sandrone | 2.99 ms | 2.96 ms | **2.09 ms (−30 %)**, pairs 5641 → 4757 |
| zankou | 2.88 ms | 2.86 ms | **1.96 ms (−32 %)**, pairs 4453 → 3672 |

Sweep-and-prune cut `updateAabbs` from ~0.4 to ~0.15 ms and removed the dbvt margin inflation (fewer non-touching pairs). Contact counts and island structure stayed essentially the same. Deferring the dbvt collide changed nothing, so the dbvt cost is the tree update itself.

### Deformation and upload (standalone / in-game)

- Skinning runs at **34–38 ns per vertex single-threaded** (Xin: 3.5 ms for 92.5 k vertices); with 14 workers 0.42 ms (8.3× scaling). The kernel is scalar `btTransform` math with per-vertex branching on BDEF/SDEF/QDEF, three matrix products per bone influence, and a `placement` conversion per vertex. A SoA/SIMD kernel with pre-converted Source-space palettes should reach 4–8 ns per vertex.
- Upload fills the GPU buffer at ~4 ns per vertex on the main thread (write-combined streaming stores; 81 MB per frame for ten characters at a 64-byte stride), plus ~13 µs per `BeginModify`/`EndModify` pair (~100 chunks per frame for ten characters → 2.5 ms of lock/unlock).
- `publish()` also copies the whole material array (with strings) and the bone array into every snapshot.

## 3. Fidelity levels used in this plan

The reference is the pinned nanoem Bullet backend. Every change below is tagged:

- **L0, bit-identical**: same floating-point operations in the same order on the same data. Verified by `mmdhl_replay.exe` reporting zero body-position difference, plus a recorded 600-step golden trajectory per acceptance model compared with tolerance 0.
- **L1, same model, different sweep order**: same equations, parameters, iteration counts and pair/contact sets, but Gauss–Seidel row order or pair discovery order changes. Trajectories diverge at the ~1e-4 PMX-unit level and grow chaotically over seconds, but are statistically indistinguishable motion. Verified with the replay tool run against an unmodified reference world (the reference must be built with the pinned code path, not the candidate), a relaxed tolerance (proposal: max 0.05 PMX units over 600 steps, no resets, no dropped time), and the existing visual capture set.
- **L2, different solver or engine**: cannot be validated as "the same response"; only visual sign-off is possible. Nothing in the recommended path is L2.

Note for the replay tool: it constructs its reference through the same `nanoemPhysicsWorldCreate`, so any diagnostic switch (like `gBroadphaseExperiment`) must be applied only to the runtime world under test, never to the reference.

## 4. Workstreams

Ordered by expected value per unit of effort. Each item lists fidelity level, expected effect, effort, and how to verify.

### A. Cheaper Bullet step (helps every scenario)

**A1. Sweep-and-prune broadphase per world** — L1 — expected −25…−32 % per step (measured standalone) — effort: small.
Replace `btDbvtBroadphase` with `bt32BitAxisSweep3` in the world creation shim (the branch already has a wrapper in `native/nanoem_backend.cpp` selected by `gBroadphaseExperiment`; production would select it explicitly). Bounds must be per-world and generous (the map mirror's AABB is clamped, which is harmless because it overlaps everything anyway); keep handle count small (8192) to avoid the default 1.5 M-handle allocation. Because pair discovery order changes, this is L1; verify with the relaxed replay tolerance and the flatgrass/construct capture sets.

**A2. Order-preserving parallel narrowphase and joint setup inside one world** — L0 — expected −20…−30 % of the *critical path* for 1–3 characters (no throughput gain for 10) — effort: medium.
`convertJoints` writes disjoint row ranges per joint after `convertBodies`; only the lazily created fixed solver body must be pre-created (as `btSequentialImpulseConstraintSolverMt::convertJoints` does). Narrowphase per pair is independent; `btCollisionDispatcherMt` already batches it but appends *new* manifolds in thread order, so sort each batch by pair index before merging to reproduce the serial manifold order exactly. Implement `btITaskScheduler` on top of the existing job pool (`native/jobs.cpp`) so nested parallelism uses the same threads. Keep the iteration loop serial. Gate on world size (skip for tiny worlds).

**A3. Island-parallel solving** — L0 if done as described — expected −10…−20 % of the critical path on models with several large islands (zankou), little on single-island models (Sandrone) — effort: medium.
Run `btSimulationIslandManager` with `m_minimumSolverBatchSize = 1` semantics and dispatch each island's `solveGroup` to a worker with a per-thread `btSequentialImpulseConstraintSolver` instance, preserving each island's constraint and manifold order from the same sorted arrays Bullet builds today. Islands share no dynamic bodies, and the fixed body's solver state is never modified (inverse mass 0), so per-body operation sequences are unchanged. Validate with replay tolerance 0 before trusting the L0 claim. Bullet's own `btDiscreteDynamicsWorldMt` is the reference implementation but is not a soft-body world; use it only as a code guide.

**A4. Pair pruning** — L0 for contact results — expected −5…−10 % — effort: small.
Reject kinematic–kinematic pairs in the overlap filter callbacks (`PairFilter`, `External::Filter`) so they never enter the pair cache; today the dispatcher skips them only after the pair exists. Audit `gContactBreakingThreshold`-based AABB inflation for these small PMX-unit bodies (Bullet's default 0.02 is relative to a metre scale). Any change to inflation is L1; the kinematic filter is L0.

**A5. Build flags** — L0 must be re-verified — expected a few percent — effort: trivial.
`/arch:AVX2` for the runtime and Bullet (check the replay reports exactly zero difference; MSVC does not contract FMA by default under `/fp:precise`), and `BT_USE_SSE_IN_API` (Bullet's own comment says "a few percent"). `GetCapabilities` already reports `bulletApiSIMD` so the flag can be surfaced.

**A6. External scene cost** — L0 — unknown, potentially large on real maps — effort: medium.
Every body forms a broadphase pair with the mirrored map mesh, so each step runs one BVH query per body against the whole map. On `gm_flatgrass` this is cheap; on `gm_construct` or larger maps the mesh has orders of magnitude more triangles. Split the captured static geometry into spatial cells at capture time and mirror only the cells intersecting each character's swept bounds (the per-object AABB cull already exists in `External::sync`). Characters near each other also mirror each other's 18 convex hulls (GJK/EPA per pair); the release benchmark spaced characters 110–150 units apart, which hides this. Add a clustered-characters scenario (§7).

**A7. Catch-up policy** — behavioural, not numerical — effort: trivial.
Expose the maximum steps per frame as a cvar (default stays 4). At 2, overload produces slow motion instead of quadrupled physics work and 15 FPS. Document it as a user trade-off, not a fidelity change. Also log the equilibrium in diagnostics (steps per frame histogram) so users can see the spiral.

### B. Scheduling: take physics off the main-thread critical path

**B1. Start early, join late** — L0 — expected: hides up to the shadow-pass time (2 ms for three, 10 ms for ten characters) — effort: medium.
`PrepareFrame` currently blocks in `PreRender`. Instead, submit the pose and *kick* the jobs in `PreRender`, and join immediately before the first native draw of the frame. The sunlight shadow pass runs before that point; let it use the previous frame's snapshot (the triple-buffered vertex slots already retain it), which is a one-frame-old shadow and imperceptible. Requires the shadow callback and `GetBounds` to read the last completed snapshot, not to trigger `ensureSnapshot`.

**B2. Optional one-frame-lag pipeline** — L0 numerically, +1 frame presentation latency — expected: physics and deformation fully overlapped with the engine's own frame — effort: medium-high.
Run the whole `stepSource`+`ensureSnapshot` for the pose captured at frame N during frame N's render, present at frame N+1. The visible mesh then lags the invisible carrier by one frame (16 ms at 60 FPS), noticeable only under physgun drags. Ship as an opt-in cvar; keep B1 as the default. The alignment tests must compare against the palette of the previous frame in this mode.

**B3. Worker pool details** — L0 — small but free — effort: small.
`notify_all` on every job completion wakes all 13 sleeping threads; use `notify_one` for queue pushes and a dedicated completion event per group. Consider a brief spin before blocking (the deform chunks are ~80 µs). Try pinning workers to the V-cache CCD and physical cores (`SetThreadAffinityMask`/`SetThreadIdealProcessor`) and compare in-game step times with the standalone ones. Default worker count 14 leaves little for the engine once B1/D5 restore more engine threads; measure 10/12/14.

### C. Deformation (main-thread barrier and ten-character throughput)

**C1. SIMD SoA skinning kernel** — L0 for physics (vertex positions may differ in the last bits; validate against the scalar reference with the existing 1e-4 checks) — expected: summed deform 24 ms → ~4 ms for ten characters, and the per-character deform on the critical path from 1.5–3 ms to <0.5 ms — effort: medium.
Pre-convert the bone palette to Source space once per publish (placement × toSource × skin) so the per-vertex conversion disappears; store vertices in SoA blocks grouped by skinning type (BDEF1/2/4 first, SDEF/QDEF last) so the hot loop has no branches; skin position, normal and tangent with one 3×4 matrix per influence in AVX2; write straight into `DrawVertex` (or the GPU buffer, see D1). Morph deltas apply as a sparse pre-pass, as now.

**C2. Snapshot overheads** — L0 — small — effort: trivial.
Stop copying `model->materials` (strings) per snapshot; keep a material-state delta instead. Reuse the `bones` array. Skip `publish` for frozen instances (already cached) and for instances whose pose sequence has not changed.

### D. Rendering and upload

**D1. Parallel vertex fill into locked buffers** — L0 — expected: −4…−5 ms on the ten-character main thread — effort: small-medium.
Lock all chunks on the main thread, fill them on workers (the write-combined pointer is plain memory; each worker fences before signalling), unlock on the main thread. With C1, deform can write the GPU layout directly and skip the `DrawVertex` intermediate.

**D2. Shadow-pass draw batching** — L0 — expected: −5…−7 ms of the 9.93 ms ten-character shadow pass — effort: small.
The shadow callback issues one draw per material (~40 per character) with `FindMaterial`, `AlphaModulate`, a `GetModuleHandleW`, map lookups and matrix push/pop each time. Materials without texture alpha (classified at import: `alphaTexture`/`translucentTexture`) can share one opaque `ShadowBuild` material and be drawn as merged index ranges per chunk. Cache `IMaterial*` per material name, hoist the module-handle check to initialisation, and stop inserting into `observedFormats` per draw.

**D3. Colour-pass overhead** — L0 — expected: −1…−2 ms for ten characters — effort: small.
Same caching for the colour pass (~410 draws per frame: 9 µs each outside the engine's own draw cost). Merge adjacent material ranges that resolve to the same engine material.

**D4. Vertex format** — L0 — expected: −30 % upload bandwidth — effort: medium, needs shader checks.
Use Source's compressed vertex format (packed normals/tangents) for the shared layout if `VertexLitGeneric` and the depth/shadow shaders accept it on this build. Must be validated visually; the current 64-byte layout was verified against the SDK.

**D5. Queued material system compatibility** — L0 — expected: unknown but potentially the largest render-side win — effort: high, ABI-sensitive.
Native rendering forces `mat_queue_mode 0` for the whole engine while any character exists, giving up the multicore rendering the user normally runs with (`gmod_mcore_test 1`, `mat_queue_mode 2` in the recorded settings). The vendored SDK exposes `IMatRenderContext::GetCallQueue()`; uploads and draws could be queued to the material thread instead of executed inline, with the triple-buffered snapshots kept alive until the queued call runs. First measure how much mode 0 costs on this scene (§7, "engine baseline"); if it is several milliseconds, this item moves up the list.

### E. Bullet upgrade paths (answering the "upgrade Bullet / GPU" question)

- **Version**: the vendored Bullet is 3.25 at commit `2c204c4`, which is the newest official release (3.2.5, April 2023). There is no newer Bullet to move to; all "upgrade" options are different code paths inside this same repository.
- **OpenCL GPU pipeline (`Bullet3OpenCL`)**: not viable for MMD rigs. It supports only point-to-point and fixed constraints (`b3GpuGenericConstraint`), no 6-DoF springs, limits, or soft bodies; shapes are registered as convex polyhedra/compounds/concave meshes; the README marks it experimental, notes kernels often fail to compile on drivers, and it targets tens of thousands of simple bodies where kernel-launch latency amortises. A ~700-body, ~950-joint world per character would run slower and the response would be a different solver (L2). Recommendation: do not pursue.
- **Bullet's CPU multithreading classes (`btDiscreteDynamicsWorldMt`, `btSequentialImpulseConstraintSolverMt`, `btCollisionDispatcherMt`, `btSimulationIslandManagerMt`)**: viable and already compiled in (`BT_THREADSAFE=1`). Used as-is they are L1 (batched iteration order, thread-order manifold appends) and drop soft-body support (the Mt world derives from `btDiscreteDynamicsWorld`). Used as a code guide for A2/A3 with the order-preserving changes above they are L0. The community-reported speedup of ~2.4× on 4 threads applies to scenes with many islands; the single-world gain here is bounded by the largest island and by collision detection, which is why A1/A4 come first.
- **Other engines (Jolt, PhysX 5 GPU)**: PhysX 5 does run D6 joints fully on the GPU and Jolt has a fast multithreaded solver, but both are different constraint models and solvers (L2). MMD spring-joint tuning targets Bullet's `btGeneric6DofSpringConstraint` behaviour; a port would need visual re-validation of every model and cannot satisfy "same physics response". Not recommended while Bullet-internal work (A1–A4) remains.

## 5. Expected outcome (budget model, to be verified)

Three characters, p50: barrier 7.09 → about 3 ms after A1+A2+C1 (Sandrone step ~2.1 ms standalone-equivalent, deform <0.5 ms) → frame ≈ 13 ms; with B1 the barrier largely disappears behind the shadow pass → ≈ 11–12 ms. The p95 target (16.7 ms) becomes reachable because two-step frames stop cascading.

Ten characters: physics job time 146 ms → roughly 100 ms after A1/A4 at four steps, but the point is to leave the four-step regime. With C1, D1, D2, D3 the non-physics main-thread work drops from ~37 ms to an estimated ~18–20 ms; at that frame time only two steps per frame are needed, halving physics work again. A plausible end state is 30–45 FPS for ten full-detail characters; 60 FPS for ten is not promised by this plan (D5 is the only item with that kind of headroom and it is unmeasured).

## 6. Suggested order and effort

1. Measurement baselines (§7) — half a day.
2. A1 sweep-and-prune (+ replay tolerance for L1) — one day.
3. C1 SIMD skinning + C2 — two to three days.
4. D2/D3 draw batching and material caching — one day.
5. D1 parallel fill — one day.
6. B1 start-early/join-late — two days (touches shadow callback and bounds).
7. A2 order-preserving parallel setup/narrowphase with a task scheduler on the job pool — three days.
8. A3 island-parallel solve — two days.
9. A4/A5/A7/B3 small items — one day total.
10. A6 map chunking — two days, after the gm_construct measurement.
11. B2 and D5 only if the numbers after 1–9 still miss the target.

## 7. Measurement protocol for evaluation

Standalone (no game, deterministic, seconds per run):

```powershell
cmake -S . -B build-profile -G "Visual Studio 17 2022" -A x64 -DMMDHL_BULLET_PROFILE=ON
cmake --build build-profile --config Release --target mmdhl_profile --parallel 16
build-profile\bin\Release\mmdhl_profile.exe --cache "H:\SteamLibrary\steamapps\common\GarrysMod\garrysmod\data\mmd_hotloader" --asset <assetId> --steps 600 [--broadphase 0|1|2] [--iterations N] [--workers N]
```

Record per character: step p50, pairs/contacts, island table, and the phase percentages. Re-run after every physics change. The tool also prints deformation ns/vertex (single and multi-threaded) for C1.

In-game (existing harness, plus new scenarios):

- `python scripts/test-performance.py <label> --count 1|3|10` as today.
- **Engine baseline for mode 0**: empty scene with `mat_queue_mode 0` forced vs 2 (the current empty-scene number is at mode 2). This decides whether D5 matters.
- **Mode A/B**: `--count 1 --mode 0` vs `--mode 2` to isolate the map-mirror cost per step.
- **Clustered characters**: three characters standing within 20 units of each other, mode 2, so the mutual carrier mirrors and contacts are exercised (the user-reported case, "multiple models around the player in close range").
- **Map**: repeat one and three characters on `gm_construct`.
- **Affinity/worker sweep**: `--workers 10|12|14` with and without the B3 affinity change.

Acceptance stays as defined in `docs/DEBUGGING.md`: foreground p95 ≤ 16.7 ms plus the simulation criterion (no resets, no dropped time, ≤ one tick of debt). For each change also record the fidelity check: replay tolerance 0 (L0) or the relaxed tolerance (L1) with reference world unmodified.

## 8. What is on this branch

- `tests/physics_profile.cpp` and the `mmdhl_profile` target: per-step Bullet profile, pair/contact/island statistics, deformation timing, `--broadphase` and `--iterations` switches.
- `CMakeLists.txt`: `MMDHL_BULLET_PROFILE` option (defines `BT_ENABLE_PROFILE` instead of `BT_NO_PROFILE`; Bullet 3.25 needs the profile hooks installed explicitly, which `Secondary::profilerReset()` does).
- `native/nanoem_backend.cpp`: diagnostic broadphase wrapper (`gBroadphaseExperiment`, default 0 = pinned behaviour). `native/secondary.*`: `dynamics()` accessor, profiler helpers, `setBroadphaseExperiment`.
- No release behaviour changes; the default code path is unchanged. Verified on this branch with a default (non-profiling) build: 136 native checks and 41 timing/scheduler checks pass, and `mmdhl_replay.exe` on Xin reports zero body-position error over 600 frames.
