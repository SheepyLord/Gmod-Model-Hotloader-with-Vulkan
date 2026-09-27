# MMD Hot Loader physics v2.0 plan (asynchronous simulation + render-side cuts)

## Context

Codex's v1 (`codex/mmd-native-runtime` HEAD `3846c37`, version 0.8.0) added selectable secondary-physics backends `reference`, `cpu_mt` (order-preserving multithreaded Bullet: ordered dispatcher, parallel joint setup, island-parallel iterations, `dbvt-fast` broadphase) and `gpu_opencl` (constraint iterations on the GPU, everything else on the CPU). It is bit-identical to nanoem and ~10–20 % faster than the reference on 1–3 characters, but eight characters still run at ~32 FPS (p95 38 ms standing, 132 ms clustered/moving), and the GPU backend is slower than the CPU one.

The uncommitted v2 in that checkout (`cpu_mt_v2` / `gpu_opencl_v2`) builds and passes its tests but is a **performance regression** (+31 % p95 on both backends, GPU v2 drops simulation time). It replaced eight independent per-character jobs with one frame-synchronous, colour-barriered global solve (~420 spin barriers or ~260 kernel launches per tick). Its own results doc records the failed gate.

The user's observations are correct and point at the same structural problem:

- **CPU stays ~13 % busy no matter how many models exist** because physics is *latency-bound on the frame*: the render thread blocks at `PrepareFrame` until the slowest character's serial 2–4 step chain finishes (prepare wall 11.5 ms), then does shadow/colour/upload work single-threaded. Total physics CPU is small (8 characters × 60 Hz × ~3–4 ms ≈ 1.5–2 cores); it is the serialisation, not the amount of work, that costs frames.
- **GPU stays ~26 % busy** because the kernel is level-serial (one dependency level per chain link × 10 iterations × 2 passes, a barrier each), dispatches are a handful of work-groups, a single coordinator thread serialises all worlds, and every step round-trips ~0.9 MB per character through host memory. That shape cannot be tuned into a win; a real GPU solver would be a different algorithm (coloured Jacobi/PGS with collision and integration resident on the device) with different motion, and it is unnecessary once the CPU path is asynchronous.

Fidelity requirement for v2.0 (user): does not need to match nanoem bit-for-bit; must stay reasonable and not break. v1 backends must remain selectable and unchanged.

Target: eight full-detail characters at 60 FPS (p95 ≤ 16.7 ms) in the standing/contact scene on `gm_construct`; the clustered moving scene at ≥ 30 FPS with the simulation never stalling the frame.

## Measured baseline (v1 `cpu_mt`, 8 characters standing, from HUD + `docs/PHYSICS_V2_RESULTS.md`)

| Item | Value |
|---|---|
| Frame p50 / p95 | 31.5 / 38.1 ms |
| Prepare wall (physics + deform barrier) | 11.5 ms |
| Worker CPU sums: physics / pose / deform | 42.9 / 1.3 / 11.4 ms per frame |
| Colour pass incl. upload ("draw") | 4.8 ms |
| Shadow pass (estimated from 0.7.0 data, ~1 ms/char) | ~8 ms |
| Pose capture (Lua) | 0.75 ms |
| Empty scene p95 | 3.06 ms |
| CPU / GPU utilisation | 13 % / 17 % |

Standalone per-step costs (from `docs/PERFORMANCE_PLAN.md`, still valid): 2.3–3.0 ms per character step; collision detection 56–64 % of it; skinning 34–38 ns/vertex scalar; upload ~4 ns/vertex on the render thread.

## Design

### 1. Asynchronous secondary simulation (`cpu_mt_v2`)

Each character's world runs on its own wall-clock 60 Hz schedule on a dedicated physics thread; the render thread never waits for physics. The solver inside each world is unchanged (`cpu_mt` machinery with nested lanes = 1, i.e. the same code that runs today when eight worlds share the pool). Presentation interpolates between the two latest solved states.

- New `native/secondary_async.hpp/.cpp`: `class SimulationThreads` owning `N = min(worlds, physicalCores - 4)` threads (`timeBeginPeriod(1)`, above-normal priority). Worlds are assigned round-robin by measured step cost; each thread loops: wait (condvar with deadline) for the earliest due tick of its worlds → run `tick()` → publish. Shutdown/removal joins in-flight ticks (≤ one step).
- `Secondary::tick(double tickTime)` (extracted from `Secondary::step`): sample the input pose queue at `tickTime` (existing `inputs` deque blending), external scene sync, `follow`, `nanoemStepFixed`, `readSolved`, then publish a `SolvedState { tickTime, body transforms (current + previous), root transforms, soft vertices, diagnostics }` through an atomic `shared_ptr` swap. Input queue push (`submitPresentationPose`) and the state swap are the only synchronisation points; a per-instance mutex guards reset/collision-mode/backend changes, which become requests executed at the next tick.
- Pose-evaluation re-entrancy: `Instance::evaluate` writes `local/global/skin/effectiveScratch`. Move the bone/IK evaluation into a `PoseEvaluator` that operates on explicit arrays (`native/model.cpp`, function body of `Instance::evaluate`), give the tick thread its own arrays, and keep the render thread's arrays for deformation. `Secondary::follow/followSoft` take the tick-local skin.
- Presentation (`Secondary::present`, render thread, inside `PrepareFrame`): choose render time = now − one tick of latency so two bracketing states exist; interpolate root-relative to the *current* primary root exactly as today (this compensates whole-body motion lag; only relative motion lags by ≤ 1–2 frames). `feedback()` reads the interpolated `display` transforms; `evaluate(true)` + `publish()` (deform) stay on the frame.
- Overload policy (cvar `mmdhl_secondary_catchup`): a world whose step exceeds its budget runs at most 2 ticks per period and then *time-dilates* (simulation time lags wall time, motion slows smoothly); beyond `0.25 s` of lag it drops ticks and counts `dropped`, as today. The frame is never affected.
- Frame-side scheduling: `PrepareFrame` (`native/module.cpp`) for v2 instances only does present + deform via the existing pool; v1 instances keep the current path untouched.
- Threading budget: physics threads are ~25 % busy each; keep the pool at `cores − 2` workers for deform/fill. Nested lanes for v2 worlds stay 1 (predictable latency); optional `mmdhl_secondary_lanes` for ≤ 3 characters.

### 2. Cheaper steps where fidelity is allowed to move (all cvars, v2 only)

- **Sleeping** (`mmdhl_secondary_sleep 1`, default on for v2): nanoem sets sleeping thresholds to 0 and deactivation time to 30 s, so bodies never rest. Set thresholds (linear ~0.02 PMX units/s, angular ~0.05 rad/s) and a 1–2 s deactivation time; when a follower's transform moves beyond a threshold, `activate()` every body jointed to it (Bullet does not wake joint neighbours of kinematic bodies; contacts already wake). Idle standing characters then cost a broadphase update only. This is the largest single win for the sandbox "many models standing around" case.
- **Kinematic–kinematic pair filter** in `PairFilter` / `External::Filter` (`native/physics.cpp`, `native/secondary.cpp`): never create pairs both sides of which are kinematic (identical contacts; fewer narrowphase calls).
- **Sweep-and-prune broadphase** for v2 (`mmdhl_secondary_broadphase sap`, exists as a diagnostic in `native/broadphase.cpp`): −25…−32 % per step standalone; v1 rejected it on bit-identity grounds only. Default off until the visual capture set is reviewed; expected to become default.
- **Cheaper mutual-carrier mirrors** for clustered scenes: mirror other characters' 18 hulls as fitted capsules instead of 64-vertex convex hulls (`External::shape` in `native/secondary.cpp`), and chunk the static map mesh into spatial cells at capture time (`captureSecondaryScene` in `native/bridge.cpp`) so each world only carries nearby triangles.
- Solver iterations, time step (1/60), joints, masks and authored rigs stay unchanged.

### 3. Deformation

- SIMD SoA skinning kernel in `Instance::publish` (`native/model.cpp`): pre-convert the palette to Source space once per publish (placement × toSource × skin), store vertices in SoA blocks grouped by skinning type (BDEF first, SDEF/QDEF last), AVX2 position/normal/tangent skinning writing the final 64-byte Source vertex layout (salvage v2's `DrawVertex` layout mirroring Source's common vertex, `native/runtime.hpp`). Morph deltas remain a sparse pre-pass; drop the per-publish `materials`/`bones` copies (keep a material-state delta).
- Larger deform grain (8192 vertices) and pool wake fixes (`notify_one` on push; completion event per group) in `native/jobs.cpp`.

### 4. Rendering (applies to every backend)

- **Shadow pass batching** (`shadowDraw` in `native/renderer.cpp`): one opaque `ShadowBuild` material for materials without texture alpha (import already classifies `alphaTexture`/`translucentTexture`), merged contiguous index ranges → one `Draw` per chunk; alpha-tested materials keep their own draw. Cache `IMaterial*` per material name; hoist the `GetModuleHandleW`/vtable check and `observedFormats` insert out of the per-draw path.
- **Colour pass**: same material cache; merge adjacent ranges resolving to the same engine material.
- **Parallel fill**: lock all chunks on the render thread, blit on pool workers (pure 64-byte streaming copies now that deform writes the Source layout), unlock on the render thread (`drawInstanceNative`, `native/vertex_upload.hpp`).
- Keep `mat_queue_mode 0`; measure its cost (empty scene mode 0 vs 2) and decide separately on `ICallQueue`-based queued uploads.

### 5. GPU

Keep `gpu_opencl` (v1) as experimental and untouched. Do not ship `gpu_opencl_v2`. Re-evaluate a GPU solver only if the async CPU path is throughput-limited (e.g. >16 characters); the prerequisite design is device-resident bodies/rows, GPU-side collision and integration, and a coloured solver, i.e. a new solver with its own motion.

### 6. Telemetry and gates

- HUD/report additions (`addon/lua/mmdhl/performance.lua`, `native/module.cpp` `GetWorkerStats`): per-thread CPU time (salvage v2's `jobsTelemetry` using `GetThreadTimes`), per-character tick rate, presentation lag (ms), dilation state, sleeping body fraction, draw calls per pass.
- Harness (`scripts/test-performance.py`): keep v2's `--count 8` pinned manifest, `--motion standing|moving`, `--layout clustered`; v2 gates: p95 ≤ 16.7 ms, tick rate ≥ 59 Hz per character, lag ≤ 2 frames, zero dropped in the standing scene; ≥ 30 FPS p95 and reported dilation in the clustered scene.
- Fidelity: `mmdhl_replay.exe` stays bit-identical for the solver step (sleep off, dbvt-fast); relaxed replay tolerance and the flatgrass/construct capture review for sleep/SAP/mirror changes.

## Step 0: branch hygiene (do first, at implementation time)

1. In the main checkout, preserve the uncommitted v2 attempt on a side branch (`git switch -c codex/physics-v2-attempt && git add -A && git commit`), then return `codex/mmd-native-runtime` to HEAD `3846c37` clean.
2. Base the work on `3846c37` in this session's worktree (`git reset --hard 3846c37`, re-copy `vendor/`).
3. Cherry-pick from the attempt: `tests/compute_fixture.hpp`, `tests/v2-assets.json`, `tests/crash_report.hpp`, the harness `--count 8/--motion/--layout` additions, `jobsTelemetry`, and the Source-layout `DrawVertex` + stream blit. Leave out: `stepV2Batch`/`beginV2`/`finishV2`, `iterateV2` team/barriers, the per-colour OpenCL dispatch, the MXCSR FTZ/DAZ flip, the second scheduler in `jobs.cpp`, the kinematic mass rewrite, and the harness default broadphase change.

## Work items in order (with the critical files)

1. Branch hygiene + cherry-picks. `CMakeLists.txt`, `tests/*`, `scripts/test-performance.py`, `native/jobs.cpp`.
2. Async scheduler + re-entrant pose evaluation. New `native/secondary_async.*`; `native/secondary.cpp/.hpp` (`step` → `tick`/`present`/`SolvedState`), `native/model.cpp` (`PoseEvaluator`), `native/physics.cpp` (`submitPresentationPose` → queue; `Instance` destructor joins), `native/module.cpp` (`PrepareFrame`, `SetSecondaryBackend`, `GetWorkerStats`), `addon/lua/mmdhl/secondary_backend.lua` (`cpu_mt_v2` ID).
3. Sleep/wake policy + kinematic pair filter. `native/nanoem_backend.cpp` (world config), `native/secondary.cpp` (`follow` wake logic), `native/physics.cpp`.
4. SIMD skinning + snapshot copy elision. `native/model.cpp`, `native/runtime.hpp`.
5. Shadow/colour batching + material cache. `native/renderer.cpp`, `addon/lua/mmdhl/native_render.lua` (shadow material set).
6. Parallel fill. `native/renderer.cpp`, `native/vertex_upload.hpp`.
7. Telemetry, HUD, harness gates, docs (`docs/PHYSICS_V2.md` rewritten to describe this design; results doc).
8. Optional after measurement: SAP default, map chunking, capsule mirrors, deform pipelining.

## Expected outcome (budget model, to verify)

Eight standing characters: prepare wall 11.5 → ~1.5 ms (deform only), shadow ~8 → ~2.5 ms, draw 4.8 → ~2.5 ms, engine ~3 ms, Lua ~2 ms → ~12–13 ms p50, p95 ≤ 16.7 ms plausible. Physics threads carry ~2 cores of load; CPU utilisation rises to ~25–35 % with the render thread no longer idle-waiting. Clustered moving scene: the frame stays near 60 FPS; whether the simulation keeps real time depends on per-step cost under mutual contacts (SAP + pair filter + capsule mirrors), otherwise it time-dilates instead of dropping frames.

## Verification

- Build: `scripts/build.ps1` (native tests, timing tests, ctest including the multicore and broadphase fixtures).
- Standalone: `mmdhl_profile.exe` per character before/after steps 3 and 8; `mmdhl_replay.exe` zero difference with sleep/SAP off.
- New unit tests: async scheduler (tick cadence at 30/60/144 FPS input, dilation, reset/remove during tick), sleep/wake (chain wakes when its follower moves), parallel fill layout check.
- In-game: `python scripts/test-performance.py <label> --count 8 --backend cpu_mt_v2 --motion standing --contacts` and `--layout clustered`, plus `--count 1/3`; empty scene mode 0 vs mode 2; capture set on `gm_flatgrass` and `gm_construct`; gates above.
