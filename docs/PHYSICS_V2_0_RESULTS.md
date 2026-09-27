# Physics v2.0 results (2026-09-22)

Scene: the eight pinned full-detail characters of tests/v2-assets.json on
gm_flatgrass, 2560x1440, 14 worker threads, collision mode 2, Ryzen 9
9950X3D. Frame percentiles are foreground frame times in milliseconds over
the harness's ten-second measurement window; stage columns are per-frame p50
values from the client profile. Every run below passed the functional gates
(all rigs verified, contacts verified, no runtime errors). The workshop
addons the user plays with were loaded (`-WithAddons`), including an entity
expression addon that animates resting ragdolls; see "What keeps characters
awake" below.

Reproduce a row with

```bash
python scripts/test-performance.py <label> --count 8 --backend cpu_mt_v2 --motion standing --contacts
```

(`--backend cpu_mt` for v1, `--motion moving --layout clustered` for the
cluster scene, `--suspend` for the engine floor).

## Standing, contacts (eight characters, frozen carriers, contact props)

| Build | p50 | p95 | p99 | prepare wall | physics work (workers) | deform work (workers) | native render | upload | Lua render | binds |
|---|---|---|---|---|---|---|---|---|---|---|
| v1 `cpu_mt` (3846c37) | 29.8 | 36.8 | 40.4 | 10.0 | 37.4 | 13.3 | 6.3 | 3.2 | 2.8 | 430 |
| v2 async only (state race still present) | 26.5 | 32.7 | 35.5 | 6.3 | n/a | 39.4 | 6.4 | 3.2 | 2.8 | 430 |
| + tick-state ownership fix + SIMD skinning | 20.3 | 26.0 | 29.2 | 1.6 | 20.7 | 6.8 | 6.6 | 3.5 | 2.8 | 430 |
| + shadow grouping, material cache, parallel fill | 16.0 | 22.2 | 28.4 | 1.6 | 20.9 | 6.7 | 4.1 | 2.9 | 2.8 | 430 |
| + native draw loop, follower sleep policy, partial uploads (final) | **14.3** | **19.7** | 25.7 | 1.5 | 20.5 | 6.4 | 4.1 | 2.8 | 1.3 | 430 |
| final build, no contact props | 13.9 | 18.2 | 20.9 | 1.5 | 20.3 | 6.2 | 4.1 | 2.8 | 1.3 | 430 |
| v1 `cpu_mt` on the final build (render gains only) | 18.8 | 24.5 | 27.0 | 4.5 | 19.9 | 2.9 | 4.0 | 2.8 | 1.3 | 430 |
| engine floor: scene present, native work suspended | 2.7 | 4.9 | 6.5 | | | | 0 | 0 | 0 | 0 |

Frame time fell from 29.8 / 36.8 ms to 14.3 / 19.7 ms (p50 / p95), a 52 %
lower p50 and 46 % lower p95. The 60 FPS gate (p95 <= 16.7 ms) is not met
yet; p50 is at 70 FPS.

## Moving, eight characters

| Build and scene | p50 | p95 | physics work (workers) | keeps up |
|---|---|---|---|---|
| v1 `cpu_mt`, clustered, contacts | 65.6 | 107.7 | 106.9 | no (0.78 s dropped per character) |
| v2 final, clustered, contacts | 29.1 | 40.4 | 61.7 | yes |
| v2 final, clustered, collision mode 0 | 23.6 | 32.0 | 31.9 | yes |
| v2 final, spaced, contacts | 14.3 | 18.7 | 25.8 | yes |
| engine floor, clustered, native work suspended | 5.6 | 8.2 | | |

Eight moving characters spread out cost the same as eight standing ones.
The clustered scene is 2.3x faster than v1 and no longer loses simulation
time; its remaining cost is the cluster itself (each world mirrors the other
seven characters' hulls, 62 ms of worker CPU per frame, and heavy overdraw
on screen), not the frame-synchronous barrier v1 had.

## Where the standing frame goes now (final build, p50)

| Item | ms |
|---|---|
| engine floor with the scene present (server tick hooks, carriers, HUD) | 2.7 |
| prepare wall (pose evaluation + skinning barrier, eight instances) | 1.5 |
| native render: shadow pass 3.2 (of which vertex upload 2.8) + colour pass 0.9 | 4.1 |
| Lua draw entry, lighting setup, override gathering | 0.5 |
| pose capture (bone matrices to the module) | 0.9 |
| not attributed (Think hooks, bounds decode, GC, driver) | about 4.6 |

The vertex upload (2.8 ms for about 25 MB per frame) is the largest single
native item. Partial uploads exist and work, but only pay off when spans are
clean, which brings us to:

## What keeps characters awake

Sleeping, idle snapshot reuse and partial uploads all depend on bones not
moving. In these runs `sleepingBodies` stayed 0 and every instance reported
a full vertex change each frame, for two reasons that were found in order:

1. nanoem marks kinematic follower bodies `DISABLE_DEACTIVATION`, and
   Bullet's island builder wakes every body touching an active kinematic
   object each step. Fixed: followers sleep too, and `follow()` wakes them
   with their chains when the target drifts (`mmdhl_secondary_wake_drift`,
   default 0.02 units).
2. With the workshop addons loaded, the resting ragdolls are animated
   (breathing, expression response), so followers drift every frame and
   the whole palette changes. Raising `mmdhl_secondary_wake_drift` above the
   animation amplitude lets such characters sleep at the cost of that much
   hair drift before the chain re-follows; the default keeps fidelity.
3. The packaged verification run records the reason for every full vertex
   change: `fullMorph` 7 of 8 instances, `fullStatics`/`fullSoft`/
   `fullBuffer` 0. Active vertex morphs (expressions) currently mark the
   whole model dirty; tracking which spans a morph touches would keep the
   body spans clean and is the natural next step for partial uploads.

Hook timings from the same run (p50 per frame, client): PreRender hook
(pose capture + PrepareFrame) 2.46 ms, visuals Think 0.14 ms, debug Think
0.21 ms. Server tick hooks at 128 Hz: native pose 0.32 ms and scene capture
0.19 ms per tick, about 0.9 ms per 14 ms frame. That leaves roughly 3 ms of
the standing frame unattributed (engine shadow setup, driver, GC).

An addon-free standing run and the standalone diagnostic
(`mmdhl_profile --static --sleep --backend cpu_mt_v2`) are the way to verify
the sleep path itself; see Known issues for the profiler.

## Fidelity

- tests/async_tests.cpp: the asynchronous trajectory equals the synchronous
  `cpu_mt` one step for step with sleep disabled (24 checks).
- tests/timing_tests.cpp: the SIMD skinning kernel matches the scalar
  definitions for BDEF1/2/4, SDEF and QDEF within 1e-4, and the streamed
  vertex layout check passes.
- The `reference` and `cpu_mt` backends are unchanged; every suite of
  scripts/build.ps1 passes (136 native, 41+41 timing, compute, 14 broadphase).

## Known issues and next steps

- `mmdhl_profile.exe` crashes while loading any model in this build (all
  backends, fixtures included); the runtime suites pass, so the fault is in
  the profiler's own path. Diagnose before relying on the standalone sleep
  diagnostic.
- The 60 FPS p95 gate remains open: the next levers are cutting the upload
  (sleep + idle reuse once characters actually rest, or a compressed vertex
  format), the unattributed ~4.6 ms (hook timing is now recorded in the
  harness stages as `hookVisualsMs`, `hookPreRenderMs`, and per server tick
  as `hookMsPerTick` in the server report), and the cluster scene's mirror
  cost (capsule mirrors and map chunking from the plan).
- `gpu_opencl` is untouched and still experimental.
