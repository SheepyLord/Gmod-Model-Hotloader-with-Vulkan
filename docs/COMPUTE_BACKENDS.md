# Selectable secondary physics (0.8.0)

## Implemented scope

The importer and per-ragdoll editor expose Existing CPU, CPU multicore, and an explicitly experimental OpenCL constraint solver. Existing CPU remains the default. All modes retain full authored rigid bodies/joints, the fixed 1/60-second tick, ten solver iterations, collision masks, Source-controlled followers and one-way Source scene contacts. Changing backend rebuilds secondary physics at the current native ragdoll pose. It does not recreate the Source ragdoll.

Soft bodies are intentionally not accelerated: if a PMX has any, the whole secondary world uses the pinned nanoem reference, with an explicit reason in diagnostics. Public rigid-only PMX models are the optimization focus.

**The full GPU dynamics portion of the proposed plan is not complete.** The supplied experimental backend moves constraint iterations to OpenCL; Bullet collision detection, spring/contact row construction, integration and feedback remain CPU work. It is slower on the tested GPU and has not passed strict trajectory parity. It must not be described as a validated full GPU physics implementation or as a performance improvement. The UI and capability API identify it as experimental. No default is changed to it.

## CPU implementation

- An ordered dispatcher runs narrowphase pairs concurrently but replays manifold insertion/removal in original broadphase order. Stock Bullet MT's per-thread merge order failed reference replay and was rejected.
- Spring Jacobian/row setup runs in parallel, preserving original row and solver-body indices. Override-iteration maxima are computed before worker execution to avoid a shared write race. Unreviewed joint types use original setup.
- Independent islands solve on the shared worker pool with original scalar Bullet iterations. Islands sharing a positive-mass kinematic follower are flushed in order before the next island's setup; a focused regression exercises this dependency.
- Unconstrained prediction and integration run in parallel. Nested job budgets account for the number of worlds already executing, avoiding a separate thread pool per character.
- Randomized solver-row mode retains the original sequential solver and reports a reason. Supported PMX defaults do not randomize rows.

## OpenCL experiment

The custom kernel ports Bullet's sequential impulse row equations, including six-axis spring rows, contact normal impulses, friction, rolling friction and split penetration impulses. Joint, contact and friction groups preserve every shared-body dependency in the original sweep. Greedy spring coloring was rejected after full-rig replay showed motion drift. Host buffers grow and reuse allocations. Independent character submissions can join one device dispatch; worlds remain physically independent.

This is OpenCL compute, with no OpenGL context or renderer replacement. The stock Bullet 3 GPU pipeline does not supply nanoem's complete PMX spring/capsule behavior as a drop-in backend. A full GPU port still requires validated collision generation, persistent device dynamics state and integration, plus further fidelity/performance work.

Startup is checked in an owned hidden worker with a 15-second timeout. Missing or failed OpenCL initialization selects reference CPU and exposes the reason. A runtime device error switches iterations to CPU; timed-out DMA buffers and the failed context are quarantined until process exit. Worker shutdown and driver-resource release happen explicitly before module unload. `MMDHL_DISABLE_OPENCL=1` exercises unavailable-device fallback. `MMDHL_OPENCL_WORKGROUP` is a developer-only benchmark override (32, 64, 128, 256), not a simulation quality setting.

## Lua API

```lua
mmdhl.SetSecondaryBackend(ent, "cpu_mt")
local state = mmdhl.GetSecondaryBackend(ent)
-- state.requested, state.effective, state.reason, state.compute
local caps = mmdhl.Decode(mmdhl.native.GetSecondaryCapabilities())
-- available/device/driver/error, experimental=true, validated=false for OpenCL
```

Backend IDs: `reference`, `cpu_mt`, `gpu_opencl`. Selection is stored in spawn options and duplication data. The client setter requests a server-authorized switch. Unknown IDs are rejected by the setter. Soft-body and device fallbacks do not silently rewrite the user's requested choice. Resetting secondary physics keeps that choice.

The existing `mmdhl.ResetPhysics(ent)` and its bindable command continue to work. Bodygroups, material overrides, morph weights and the 18 native physics objects remain independent of secondary backend selection.

## Reproduction

Build with `scripts/build.ps1 -SkipBootstrap`. This now includes `mmdhl_compute_tests.exe cpu_mt` alongside the runtime, timing and broadphase tests. GPU fixture tests are explicitly opt-in and currently return a failing status for the strict contact-trajectory gate.

```powershell
build/bin/Release/mmdhl_replay.exe path/to/model.pmx --backend cpu_mt --broadphase dbvt-fast
build/bin/Release/mmdhl_compute_tests.exe gpu_opencl
powershell -File scripts/game-start.ps1 -Visible -WithAddons
python scripts/test-performance.py sample --count 3 --backend cpu_mt --broadphase dbvt-fast --layout clustered --contacts
python scripts/test-secondary-scenes.py --backend cpu_mt
python scripts/test-secondary-backends.py
powershell -File scripts/game-stop.ps1
```

Measurements use eight seconds of warm-up and ten seconds of foreground gameplay. The harness rejects unfocused runs, checks all characters were drawn, verifies backend/fallback, all body/joint counts, ten iterations and actual world/object contacts. It records simulation debt, dropped time, reset counts and memory separately. GPU functional checks mean finite transforms and working contacts; they do not imply nanoem fidelity (`fidelityValidated=false`).

## Validation results

Measurements on 2026-09-21 used a Ryzen 9950X3D (16 physical cores; 14 shared worker participants) and RTX 5090, driver 610.88. GMod build 2026.09.15, x86-64 branch, rendered at the verified 2560 x 1440 resolution. The environment reports list 742 Workshop addons, 741 mounted, and 30 folder addon directories. The game used 8x MSAA, 16x anisotropic filtering, HDR level 2, shadows and flashlight depth enabled, `mat_queue_mode=0`, `mat_vsync=0`, `fps_max=300`, and `gmod_mcore_test=1`. No isolation flags were used. Empty-scene p95 frame times were 4.32 ms on flatgrass and 4.63 ms on construct.

### Fidelity and functional gates

- All five CTest suites pass: 136 native regression checks, 41 timing/scheduler checks in each of two broadphase modes, eight multicore fixtures and 14 broadphase checks. Final runner log: `build/accelerated-final-ctest.log`.
- CPU multicore: all ten full-rig, 600-tick independent nanoem replays passed with **zero maximum body-position difference**. This includes Xin (686 bodies / 947 authored joints), Cyrene (447 / 639), Sandrone (571 / 873), Zankou, March 7th, Ookuninushi, Firefly, Clorinde, Daniya and Hatsuyuki. Zankou retains its prior named self-joint rejection; no additional bodies or valid joints were removed. Reference input, binding and feedback checks passed. Reports: `validation/accelerated/cpu-mt-final-rigs-summary.json` and its ten referenced reports.
- Eight focused multicore fixtures passed: linear and angular springs on all six axes, sphere/box/capsule contacts, and a positive-mass kinematic follower shared by multiple islands. The latter caught a dependency bug during development; pending island impulses now finish before a later island reads that follower. Three full-rig replays with external geometry also had zero position error.
- PMX soft-body fixture: reference fallback passed for both requested acceleration modes. Disabled OpenCL also fell back to reference and passed a complete Cyrene replay. Soft-body simulation is retained; acceleration of it is deferred.
- Actual importer dropdown clicks placed Cyrene using `cpu_mt`, reporting `prop_ragdoll` and exactly 18 physics objects. Live client-requested backend changes, physics reset and duplication preserved the native physics objects, frozen states, pose, morphs, color, material overrides and hidden parts. Invalid backend IDs were rejected. Reports: `validation/accelerated/backend-menu-test.json` and `validation/accelerated-backend-controls.json`.
- Floor, wall, slope, moving prop and other-ragdoll contact scenarios passed on both acceleration paths. Source prop velocity error stayed zero. Teleport, mode change, object removal and map cleanup also passed. These are functional contact checks, not GPU fidelity acceptance. Reports: `validation/accelerated-contact-scenes-cpu_mt.json` and `validation/accelerated-contact-scenes-gpu_opencl.json`.
- Direct review of clustered scenes and six-frame Cyrene motion captures found attached hair/clothing and finite visible geometry in those scenes. These short captures do not overturn the failed GPU numerical comparisons below.

### CPU game measurements

Each entry is one 10-second foreground measurement after eight seconds of warm-up, with full authored rigs, fixed 1/60-second physics steps and ten iterations. Lower frame time is better; these are frame-time percentiles, not average FPS. Short runs are subject to scene and scheduling variance and do not establish long-session stability.

| Scene | Reference p50 / p95, ms | Multicore p50 / p95, ms | p95 change |
|---|---:|---:|---:|
| Construct, one Xin | 10.25 / 12.76 | 9.08 / 11.14 | -12.7% |
| Flatgrass, three spaced | 16.90 / 22.55 | 15.94 / 20.26 | -10.2% |
| Flatgrass, three clustered, world/prop contacts | 18.40 / 24.64 | 16.52 / 19.62 | -20.4% |
| Construct, three clustered, world/prop contacts, final session | 24.39 / 30.92 | 20.79 / 25.89 | -16.3% |
| Flatgrass, ten distinct full rigs | 65.22 / 80.44 | 66.69 / 80.10 | -0.4% |

The one-character comparison meets the 16.7 ms p95 target. **Three and ten characters still miss it.** Physics/deformation preparation p95 for the three spaced models improved from 9.66 to 6.41 ms, and the flatgrass clustered scene improved from 11.28 to 6.66 ms. This does not remove all other game/render costs.

The three-character measurements had no simulation drops or resets and kept up with the simulation clock. The ten-character multicore run **failed the real-time simulation gate**: 0.2833 seconds were dropped per world and up to 0.2283 seconds remained as debt. Its reference comparison also failed to keep up, with 0.0992 seconds of debt. No authored detail, timestep or iteration setting was reduced to disguise this result.

After warm-up, three-character memory remained approximately flat within each measurement. The ten-character run's final private allocation was about 16.25 GB; most of the earlier allocation change was cache/loading stabilization, with about 2.6 MB of variation during the later samples. A ten-second window is not sufficient to certify absence of slow memory leaks.

CPU report names: `reference-construct-one`, `multicore-construct-one`, `multicore-final-flat-reference`, `multicore-final-flat-three`, `accelerated-flat-cluster-reference`, `multicore-final-flat-cluster-repeat`, `compute-ordered-construct-cluster-reference`, `compute-ordered-construct-cluster-multicore`, `accelerated-flat-ten-reference`, and `multicore-final-flat-ten`, all under `validation/` with a `.json` extension. An earlier construct session measured 27.04 versus 23.06 ms p95 (14.7% improvement); the variation is another reason not to extrapolate short samples into a fixed FPS promise. `validation/accelerated/game-comparisons.json` provides a compact local index.

### GPU gates that remain failed

All eight focused GPU fixtures remain finite; seven pass the trajectory thresholds. Capsule/ground motion fails: maximum position difference 0.1553 MMD units, rotation difference 0.7849 radians, and energy ratio 1.0277. Comparing identical prepared rows before integration shows small but nonzero floating-point differences (up to 8.2e-7 in velocity and 4.8e-7 in impulse for that fixture). The thresholds were not relaxed.

The final dependency-ordered GPU solver also fails strict full-rig agreement:

| 600-tick replay | Maximum body-position difference, MMD units | RMS difference, MMD units |
|---|---:|---:|
| Xin | 0.2458 | 0.0174 |
| Cyrene | 25.8760 | 2.5835 |
| Sandrone | 0.8464 | 0.0236 |

These differences are too large to call the GPU path equivalent to nanoem. Preserving dependency order reduced drift on Xin and Sandrone but did not solve Cyrene. On Cyrene, the optional identical-input row comparison records a maximum velocity difference of 4.58e-5 and impulse difference of 0.0009766 before feedback. Dense contacting chains amplify numerical differences over time. Further solver work and reference comparisons are required; finiteness alone is insufficient.

Reports: `validation/accelerated/gpu-ordered-focused.log`, `gpu-ordered-rigs-summary.json`, its three referenced reports, and `gpu-ordered-rows-cyrene.json`. Add `--validate-compute-rows` to a GPU replay to enable that extra comparison; do not use it for timing measurements.

The final ordered GPU clustered construct measurement has p50 / p95 **32.53 / 45.29 ms**, with actual OpenCL execution, all bodies/joints present and verified world/prop contacts (`compute-ordered-construct-cluster-opencl.json`). It is slower than both CPU choices in the same session. An earlier graph-colored GPU candidate reached 34.40 ms p95 but changed spring order and was rejected. Larger workgroups (256 instead of 32), persistent buffers and batching reduced overhead, but do not make this host/device split beneficial on the tested models.

### Cleanup and evidence

The menu test restored its model's saved spawn options. The owned GMod session exited normally after OpenCL use; worker and device shutdown were exercised. Temporary test hooks and graphics/input configuration were restored. Test watchdogs and process termination are restricted to the token-tagged, owned GMod session.

An unfocused run (`multicore-final-flat-cluster`) was rejected and repeated. An early supposed GPU flatgrass run (`opencl-final-flat-three`) actually fell back to CPU because of a worker path bug; it is excluded from GPU results. The installed-worker lookup was corrected and later GPU measurements verify actual OpenCL execution. CTest's native regression working directory was corrected so fixture paths resolve when using `ctest --test-dir build`. The motion capture probe now chooses valid ground on the active map, keeps the actor in the player's network-visible area, waits for replicated bones, and cleans up even if setup fails; its old flatgrass-only default failed when reused on construct.

Raw reports, environment captures and screenshots remain under local `validation/` and the owned-session debug directories. Distribution packages include code, licenses and synthetic fixtures, never the user's models, corpus or game cache. The CPU option is usable; the full GPU dynamics and large-scale performance objectives remain unfinished.
