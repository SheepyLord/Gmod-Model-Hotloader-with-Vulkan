# Motion and performance update — 0.7.0

Measured on 2026-09-21. The motion clock, worker scheduling and mesh upload changes are implemented. Single-character frame time meets the 16.7 ms p95 target on this machine. Three and ten full-detail characters are substantially faster than 0.6.0, but **still miss that target**. These results must not be described as ten characters at 60 FPS.

## Changes and fidelity

Secondary simulation now has one double-precision 60 Hz accumulator. Timestamped primary poses are sampled at each fixed step; the nanoem adapter executes exactly that step without a second Bullet accumulator or graphics-motion-state interpolation. The rendered secondary pose interpolates between solved states. Root-relative interpolation follows the current Source primary root; world-anchored components retain world-space interpolation. Source bones remain authoritative.

Normal movement does not reset the rig. Clock rewinds, large teleports and the explicit reset command have recorded reset reasons. Short frame stalls retain catch-up debt. A maximum of four steps per render frame bounds recovery work; exceptional debt above 0.25 seconds is discarded with a diagnostic counter. This safeguard can still cause a visible discontinuity during overload; it is not a claim of smooth animation at arbitrarily low frame rates.

A persistent worker pool processes independent characters and vertex chunks, replacing repeated thread creation and overlapping parallel schedulers. On this 16-core machine it uses 14 participants, including the calling thread. Bullet diagnostic globals and soft-body random state are made thread-local or instance-local through the reproducible build patch. Engine, graphics and Lua calls remain on their owning thread.

Morph expansion and deformation scratch storage are reused. SDEF/QDEF quaternion palettes are built once per pose. The renderer shares indexed vertex buffers across material ranges and color, flashlight, depth and shadow passes. It streams the verified common Source vertex layout in complete cache lines, retaining the SDK fallback for other shader layouts. Triangle order, material slots, vertices, authored bodies, valid joints, collision masks and solver iteration counts are preserved.

Generated VMTs and render materials use `$alphatest 1`, `$translucent 0` and `$allowalphatocoverage 1`. User material overrides retain their shader flags. Alpha testing changes partially transparent surfaces into coverage/cutout behavior; it does not reproduce continuous alpha blending. Material archives, carrier generation and renderer namespaces are versioned to regenerate this state.

The pinned [nanoem Bullet backend](https://github.com/hkrn/nanoem/blob/30acffaa29f5d2eb9e997d69418f2e4b97b5894f/nanoem/ext/physics_bullet.cc) and its authored physics settings remain the simulation reference. Applicable adapted-code licenses and build patches are listed in `THIRD_PARTY.md`.

## Final performance measurements

All final runs used a visible, foreground **2560×1440** game on `gm_flatgrass`, Ryzen 9 9950X3D / RTX 5090, GMod build `2026.09.15`, x86-64, tick rate 128. Existing settings included 8× MSAA, anisotropy 16, `mat_picmip -1`, HDR 2, DX95, shadows and flashlight depth enabled, VSync off and `fps_max 300`. Native rendering retains the required `mat_queue_mode 0` safeguard while characters exist; the empty scene uses the restored value 2.

The baseline and final addon inventories match: 742 engine inventory entries, 741 marked mounted, plus 30 folder-addon directories recorded. The test does not equate every inventory entry with a mounted addon. Eight seconds of warm-up precedes **ten seconds of measurement**. All requested characters were drawn throughout each accepted measurement, and no background/unfocused frames were included. Captures, functional probes and native builds ran separately from these measurements.

| Scene | 0.6.0 median / p95 (ms) | 0.7.0 median (ms) | p95 (ms) | p99 (ms) | p95 ≤ 16.7 ms |
|---|---:|---:|---:|---:|---|
| Empty scene | — | 3.35 | 4.28 | 4.76 | Pass |
| Xin | — | 8.88 | 11.81 | 13.95 | Pass |
| Xin, Cyrene, Sandrone | 38.13 / 69.16 | 17.38 | 23.86 | 27.22 | Miss |
| Ten distinct characters | 195.72 / 380.30 | 65.28 | 78.56 | 84.92 | Miss |

The three-character median frame time is 2.19 times faster; the ten-character median is 3.00 times faster. This comparison includes the requested material/render changes as well as physics work, so it is an end-to-end improvement, not a solver-only speedup.

The ten models contain **1,269,059 vertices, 4,947 rigid bodies and 7,155 valid joints**: Xin, Cyrene, Sandrone/Marionette, 残虹/zankou, March 7th, 大国主, ホタル, 克罗瑞娜, Daniya and 初雪. Source hashes are distinct. The existing invalid self-joint exclusion remains; no valid authored joints or bodies were removed to obtain these timings.

One and three characters retained less than one step of simulation debt, with no resets or newly dropped time during measurement. Ten characters executed 604 steps over approximately 10.0605 seconds, also without resets or newly dropped time, but finished with **0.1828 seconds of outstanding debt** left from warm-up. That scene therefore fails the real-time simulation criterion as well as the frame-time target. Load/warm-up stalls are retained in the cumulative diagnostics (1.85 seconds discarded in the ten-character run); they are not counted as successful smooth motion.

For three characters, median frame preparation was 7.09 ms and native rendering 2.54 ms, including 1.30 ms of upload work. For ten, preparation was 28.43 ms and native rendering 11.43 ms, including 7.75 ms of upload work. Worker totals are sums of job wall times and overlap each other; shadow, upload and draw costs also overlap native rendering. Do not sum all reported columns as sequential frame time.

Three-character private memory was approximately 7,653.6–7,653.9 MB near the end of the run. Ten-character private memory ended near 16,133 MB, with a 3,348 MB working set. Imported texture/engine caches remain resident after removal. These short measurements do not prove long-duration leak freedom.

Raw local evidence: `validation/motion-release-{empty,one,three,ten}.json`, `motion-baseline-{three,ten}.json`. Intermediate experiments are not release results.

## Correctness evidence

- Release build: 136 native checks and 41 timing/scheduler/skinning/topology checks passed (`validation/motion-final-build.log`). Timing coverage includes 30/60/120/144/240 FPS, repeated timestamps, irregular frames, short stalls, interpolation on frames without a simulation step, clock rewind, teleport and single-reset behavior.
- Independent nanoem C-API replay: all ten characters passed 600 driven steps. Maximum rigid-body position difference was zero; the soft-body fixture's maximum position difference was below 0.00006 MMD units. This compares independent backend worlds and feedback equations, not every feature of the desktop nanoem application (`validation/motion-replay-all.json`).
- Native tools: 14 gameplay checks passed, covering menu placement, 18 Source bodies, actual physgun grabbing/rotation, per-limb freezing, face/overflow/finger/eye controls, weld/rope, duplication, removal and instance cleanup (`validation/native-tools-Xin.json`).
- Materials: seven checks passed, including AME edit/save/reset and UV geometry for slots 0, 31, 40 and 63, complete inventory, duplication and unchanged ordinary-entity limits (`validation/compatibility-materials.json`). Material-based visibility checks also passed (`compatibility-visibility.json`).
- One-way secondary contacts passed against floor, wall, slope, moving prop and another ragdoll. Measured Source velocity change attributable to secondary simulation was zero. Teleport, mode change, removal and map cleanup checks passed (`validation/compatibility-contact-scenes.json`).

- Alignment: Xin, Cyrene and Sandrone passed static and moving client-palette checks at 0.5×, 1× and 2× scale. Maximum sampled bone/neutral-vertex discrepancy was below 0.00014 Source units; maximum serialized convex-surface discrepancy was below 0.000002 units. Automatic facial morphs from enabled addons are recorded and excluded only from the neutral-vertex oracle; bone alignment remains checked every frame (`validation/presentation-alignment.json`).
- Reset and render controls: exactly one manual reset was observed, preserving all 18 native object identities, positions, rotations, motion flags, color, morphs, submaterial, bodygroup and collision mode. All 64 Cyrene materials had alpha-test/coverage flags and no translucent flag; an explicit translucent override rendered successfully. This also caught and fixed a Lua call to the C++-only `IMaterial::IsTranslucent` method (`validation/motion-controls.json`).
- Motion review: 12 timestamped Cyrene frames show attached hair and clothing moving with the driven pelvis, with no exploded geometry. Across the captured interval, reset count stayed constant, dropped time remained zero, and 199 fixed ticks were recorded. These sparse screenshots establish shape/attachment continuity, not a high-frame-rate visual proof; the frame-cadence/interpolation tests supply the complementary timing evidence (`validation/motion-cyrene.json`).

The final `gm_flatgrass` capture set was reviewed directly: full silhouette, face/eye morph, curled fingers, collision overlay, flashlight/shadow, cubemap override, color/submaterial override and alpha-zero hiding. A fresh installed `gm_construct` session repeated the capture set and added a dark-room/flashlight comparison. Texture cutouts and ribbons remain visible; alpha zero removes the model and shadow. The unlit dark room stays dark, and the flashlight illuminates the character and casts its shadow. Strong flashlight highlights can still saturate the existing SCMI shading recipe. No claim is made that alpha testing preserves the appearance of every semi-transparent MMD surface.

Local flatgrass captures are in the cache's `debug/d58ec39ee6a24d97b941b84a72f20f52/motion-release-cyrene-*.png`; motion frames use `motion-cyrene-*.png`. Construct captures are `debug/5cefc020983440688cd36bed37b68310/motion-construct-cyrene-*.png`; `validation/motion-construct-visual-status.json` records no native rendering/shadow error. Benchmark and compatibility reports are local development evidence; user models, cache contents and validation dumps are not included in the release archive.

## Reproduction

Use `scripts/game-start.ps1 -Visible -WithAddons`, focus the owned game, then run `python scripts/test-performance.py <label> --count 0|1|3|10`. Run scenarios sequentially. `scripts/game-stop.ps1` stops only the token-matched owned process and restores saved settings and temporary hooks. `docs/DEBUGGING.md` documents stage counters, standalone tests and optional profiling.
