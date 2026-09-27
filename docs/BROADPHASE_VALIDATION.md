# Broadphase evaluation and release 0.7.1

Measured 2026-09-21 on the existing Windows x64 single-player installation. The Fable profiling prototype was useful for locating the cost, but its sweep-and-prune (SAP) candidate failed independent nanoem fidelity checks. Release 0.7.1 uses an order-preserving DBVT specialization instead. The timestep remains **1/60 second**, solver iterations remain **10**, and authored bodies, valid joints, materials and vertices are retained.

## Decision and implementation

`native/dbvt_leaf.hpp` specializes the common DBVT tree-versus-leaf traversal. It retains the pinned Bullet tree updates, fat AABBs, pair-cache contents and pair visitation order, and skips constructing velocity/margin operands when the existing fat leaf already contains the new AABB. Each world reuses its traversal stack. This reduces collision detection work without changing the contact or constraint solver.

`native/broadphase.cpp` gives each secondary world an explicit backend. Factory configuration is scoped to the constructing thread; changing the runtime default cannot change the independent nanoem reference. `dbvt-fast` is the production default, `dbvt` remains the control, and `sap` is an explicit diagnostic experiment. Existing worlds keep their choice across physics resets.

The SAP experiment has bounded allocation and a reported DBVT fallback for exhausted proxy capacity or out-of-bounds geometry. Fallback preserves object transforms, velocities and constraints, but rebuilds the contact cache. That safety mechanism does not make SAP pass the fidelity gate.

## Reference validation

The independent replay uses the pinned nanoem C API and independent bone-feedback equations. The candidate receives identical driving poses; contact cases construct independent reference scene colliders. The reference backend is asserted to remain the original DBVT.

SAP failed the existing tolerance without reducing timestep or solver iterations:

| Character | Maximum rigid-body position difference (MMD units) |
|---|---:|
| Xin | 0.508986 |
| Cyrene | 24.753242 |
| Sandrone | 1.471183 |

Consequently SAP was not promoted. Relaxing the replay threshold or testing the candidate against another candidate world would conceal this difference.

The final DBVT specialization passed **18 replay cases**, each with 600 driven steps: ten full character rigs; both DBVT variants with scene contacts on Xin, Cyrene and Sandrone; a soft-cloth fixture; and a world-anchor fixture. Maximum rigid-body position difference was **zero**. Maximum rotation-matrix difference was 0.000000716; soft-vertex position difference was 0.0000542 MMD units. These are backend/synchronization comparisons, not a claim of reproducing every feature of the desktop nanoem application.

The Release build passed **136 native checks**, **41 timing/scheduler checks on each DBVT backend**, and **14 broadphase checks**. The latter include complete overlap-pair order across 500 staged/moving AABB updates, removal, SAP capacity/bounds fallback, concurrent world construction and reference isolation. Timing checks cover varying frame cadence, between-tick interpolation, stalls, teleports, clock rewind and manual reset.

Local evidence: `validation/broadphase/replay-initial.json`, `replay-complete.json`, `release-build.log`, and `harness-checks.json`.

## Harness corrections

- `--workers` now controls the deformation measurement as well as the initial worker pool.
- `--no-profile` disables timing hooks and the dump; instrumented and ordinary Release builds are separate. The flags were checked in an instrumentation-enabled build too.
- Collision modes require an actual scene; an empty contact benchmark fails instead of reporting a misleading result.
- In-game reports verify the selected backend, full body/joint inventory, unchanged iterations, actual foreground duration and every requested character being drawn.
- Clustered tests use 20-unit spacing and moving contact plates. Native character/plate collisions are disabled for those plates, so their measured velocity directly detects unwanted secondary-to-Source feedback. Both map and object secondary contacts must occur.
- Correctness or measurement-validity failures return a nonzero exit code. Frame-time misses and simulation debt are reported separately.

## Standalone physics cost

Ordinary Release build, profiling disabled, 120 warm-up steps followed by 600 measured steps, no reduction in authored detail. Both backends use the same sinusoidal driving input and model-only collision mode for this CPU measurement.

| Character | DBVT mean step (ms) | Optimized mean step (ms) | Reduction |
|---|---:|---:|---:|
| Xin | 2.3620 | 2.0455 | 13.4% |
| Cyrene | 1.8788 | 1.6077 | 14.4% |
| Sandrone | 2.9605 | 2.5640 | 13.4% |

These are physics-step measurements, not complete game frames. Raw data: `validation/broadphase/standalone-final.json`.

## Addons-enabled game comparisons

Visible foreground **2560×1440**, GMod `2026.09.15` x86-64, Ryzen 9 9950X3D / RTX 5090, tick rate 128, automatic 14 worker participants. Every scenario used eight seconds of warm-up followed by **ten seconds of measurement**. Builds, standalone tests and captures ran separately from measurements. The 742-entry addon inventory had **741 mounted entries**, with 30 folder-addon directories also recorded. Existing graphics settings included 8× MSAA, anisotropy 16, HDR 2, DX95, shadows, VSync off and `fps_max 300`. Measured character frames used the existing `mat_queue_mode 0` safeguard; empty scenes used 2.

All character comparisons used collision mode 2 (model, world and physics objects). The ten models contain **1,269,059 vertices, 4,947 rigid bodies and 7,155 authored joint records**. The existing invalid self-joint in zankou remains excluded, leaving **7,154 valid joint records**. No additional bodies or valid joints were removed. Each carrier has exactly 18 native physics objects.

| Map / scene | DBVT median / p95 (ms) | Optimized median / p95 (ms) |
|---|---:|---:|
| Flatgrass, Xin | 8.33 / 11.10 | 7.34 / 10.28 |
| Flatgrass, three spaced | 16.41 / 22.16 | 16.06 / 20.45 |
| Flatgrass, three clustered + moving props | 17.62 / 24.47 | 16.69 / 22.79 |
| Flatgrass, ten spaced | 67.04 / 82.50 | 60.12 / 75.39 |
| Construct, three spaced | 19.41 / 24.97 | 18.44 / 23.14 |
| Construct, three clustered + moving props | 21.19 / 27.74 | 20.30 / 27.97 |

Reverse-order repeats: Flatgrass three spaced, 17.06 / 21.66 → 16.71 / 20.30 ms; Construct clustered, 20.80 / 27.32 → 20.45 / 26.87 ms. **Construct's clustered p95 is effectively unchanged across these short repeats**, despite a lower median. The other measured p95 improvements are approximately 6–9%. Empty-scene p95 was 4.49 ms on Flatgrass and 4.29 ms on Construct.

The complete comparison matrix has 18 accepted measurements, with no unfocused frames and every requested character drawn. All passed correctness, backend-selection and full-rig checks. **Only the single-character and empty scenes met the 16.7 ms p95 target. Three and ten characters still miss it.** These results are not a claim of ten characters at 60 FPS.

The ten-character control dropped 0.25 seconds of simulation time during measurement and ended with 0.1262 seconds of debt. The optimized run dropped none and ended with 0.0020 seconds of debt. All one/three-character cases had no new resets or dropped time and less than one tick of final debt. This is evidence for these short runs, not a guarantee against overload in other scenes.

Three-character Flatgrass median physics job time summed across workers decreased from 12.42 to 11.01 ms, with the main-thread preparation barrier decreasing from 6.88 to 6.37 ms. For ten characters the corresponding values were 146.55 → 124.35 ms and 29.15 → 25.46 ms. Worker sums overlap one another and must not be added as sequential frame cost. Rendering/upload costs remain material bottlenecks at ten characters.

Candidate process private memory was approximately 7,629 MB for three characters and 16,171 MB for ten; the ten-character sample rose about 16 MB across the run. Engine/texture caches remain resident. Ten-second measurements do not establish long-duration leak freedom.

Raw local evidence: `validation/broadphase-{flat,construct}-*.json`; consolidated values in `validation/broadphase/game-summary.json`. The final installed-build check is recorded separately in `validation/broadphase-installed-cluster-fast.json`.

## Contacts, motion and installed build

On both maps, the candidate passed floor, wall, slope, moving-prop and other-ragdoll contact checks, followed by teleport, collision-mode change, object removal and map cleanup. Source prop velocity error was zero. Clustered candidate measurements recorded 5–9 simultaneous map contact points and 164–217 object contact points at sampled peaks, with no secondary impulses sent back to Source.

Full-resolution comparison screenshots and twelve timestamped Cyrene motion frames were captured. Direct inspection showed attached hair/clothing following the driven character without exploded geometry. These sparse frames verify appearance and attachment continuity; the independent replay and frame-cadence tests provide the stronger numerical/timing evidence.

The installed 0.7.1 build reports `dbvt-fast` by default. Reset checks preserve all 18 native object identities and poses, morphs, color, material overrides, bodygroup visibility and collision mode, and observe exactly one manual reset. Generated alpha-test/coverage materials and explicit override rendering still pass. The owned test process and temporary hooks are removed afterward, with saved settings restored.

Evidence: `validation/broadphase/{flat,construct}-contacts.json`, `motion-cyrene-fast.json`, `final-motion-controls.json`, and the owned-session capture directories. Private models, captures and benchmark dumps are excluded from release packages. Commands for reproducing the comparisons are in [DEBUGGING.md](DEBUGGING.md#broadphase-comparisons-071).
