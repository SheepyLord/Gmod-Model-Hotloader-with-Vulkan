# Vulkan compute constraint solver (test repository, 2026-09-25; shared device 2026-09-26)

Developed on branch `vulkan-compute` of the test repository
[Gmodl_MMD_Hot_Loader_Vulkan](https://github.com/SheepyLord/Gmodl_MMD_Hot_Loader_Vulkan) and merged into the main
project with the rest of that repository's work; merge validation and in-game results are in
[VULKAN_MERGE.md](VULKAN_MERGE.md).

## What it is

A secondary-physics backend, `gpu_vulkan`, that runs the constraint-solver
iterations of the v2 (asynchronous) worlds on a Vulkan compute device.
Collision detection, row setup, integration and everything else stay on the
CPU, exactly as with `cpu_mt_v2`.

- Two device contexts. When the game renders through the patched DXVK
  (`patches/dxvk`, see "Sharing the renderer's device" below) the solver uses
  queues of a dedicated compute family on DXVK's own `VkDevice`. Otherwise it
  creates a standalone `VkDevice` on the best GPU, loaded from `vulkan-1.dll`
  at run time (no link dependency), async-compute queue family. 16 in-flight
  slots over up to 8 queues (4 on the shared device).
  `MMDHL_VULKAN_STANDALONE=1` forces the standalone device.
- Startup is checked in a disposable worker (`mmdhl_worker --probe-vulkan`)
  including a numeric self-test; without a usable device the world stays on
  `cpu_mt_v2` with the reason in diagnostics. The worker probes a standalone
  device. The in-process solver repeats the self-test on whichever device it
  attaches to.
- Dependencies are pinned sources: KhronosGroup/Vulkan-Headers and glslang at
  `vulkan-sdk-1.4.357.0` (`dependencies.lock.json`, fetched by
  `scripts/bootstrap.py`). glslang is built in-tree as a host tool; the kernel
  (`shaders/vulkan/solver.comp`) is compiled to SPIR-V and embedded.
- UI/API: `gpu_vulkan` in the backend list (all seven languages),
  `GetSecondaryCapabilities().gpu_vulkan`, `SetVulkanSolverOrdering("ordered"|"colored")`.

## Exactness

The OpenCL experiment ported Bullet's *scalar reference* row solver, but the
CPU actually runs `gResolveSingleConstraintRow*_sse4_1_fma3` (FMA3 + SSE4.1
detected at run time; `_mm_dp_ps` dot products, fused multiply-adds, `blendv`
clamps) and the SSE2 split-impulse solver. The kernel mirrors those operations
exactly (`precise`, `fma`, the same comparison forms; the FMA path is a
specialization constant set from Bullet's own CPU check). Rows are emitted in
dependency-level order, which is Bullet's sweep up to swaps of independent rows.

| Check | Result |
|---|---|
| 8 compute fixtures (6-axis springs, sphere/box/capsule contacts, shared kinematic follower) | all pass, **0** position error, **0** row error on identical inputs (capsule/ground failed under OpenCL: 0.155 units) |
| 8 pinned rigs, 600 ticks, lockstep vs `cpu_mt_v2`, 10 and 100 iterations | **max position difference 0** for every rig |

The optional colored ordering (greedy coloring, not exact) diverges at once
(Xin: 1.9 units max at 10 iterations) and is only ~9 % faster, so ordered is the
default.

## Kernel design (current)

One workgroup (256 threads) per Bullet solver batch ("island"). Island body
velocities, inverse masses and the running impulse component live in shared
memory (structure of arrays). The host converts each batch into compact
112-byte rows in level order and merges runs of tiny levels into serial runs.
Level kinds: groups (one thread per constraint, all rows fetched at once, the
body pair held in registers across the constraint's rows) and serial runs (one
subgroup, one row per lane, solved in order). Worlds that reach the solver
within 0.5 ms share one submission.

## Offline performance (per character, `mmdhl_vulkan_compare`, 600 ticks)

Step = the whole secondary tick on the worker; kernel = GPU time of the solve.

| Rig (bodies) | 10 it CPU / GPU step | 10 it kernel | 100 it CPU / GPU step | 100 it kernel |
|---|---|---|---|---|
| Xin (686) | 1.75 / 2.37 | 0.49 | 6.32 / 7.62 | 4.55 |
| Cyrene (447) | 1.20 / 1.79 | 0.46 | 3.95 / 6.09 | 5.16 |
| Sandrone (571) | 1.96 / 2.78 | 0.87 | 5.54 / 7.86 | 4.47 |
| March 7th (287) | 0.76 / 1.18 | 0.37 | 2.41 / 4.29 | 3.63 |
| Long Night Moon (282) | 0.72 / 1.16 | 0.37 | 2.55 / 4.74 | 3.80 |
| Firefly (654) | 2.03 / 2.70 | 0.73 | 6.77 / 8.09 | 6.34 |
| Furina (356) | 1.18 / 2.08 | 1.05 | 3.76 / 10.63 | 10.38 |
| Columbina (607) | 2.09 / 2.72 | 0.71 | 6.34 / 7.72 | 5.91 |

The GPU step is slower than the CPU step for every rig; its CPU time is lower
at 100 iterations (the solve moves off the core).

Optimization history (Xin, 100 iterations, kernel): first port 8.6 ms ->
shared-memory pool 5.8 -> 256-thread workgroups 4.9 -> compact level-ordered
rows / register-held pairs / unconditional fetches 4.5 ms. Tried without gain:
bank-conflict-free layout alone, branch-free rows, lane teams, shared-only
barriers, level/group tables in shared memory, `volatile` prefetch, cross-level
prefetch (spilled 784 B, 3x slower), colored ordering.

## Where the time goes (SM clock profile, `MMDHL_VULKAN_CLOCK=1`)

Largest Xin island (560 bodies, 4,909 rows), one iteration ~150k cycles (~50 us):
10 joint levels 7-11k cycles each, ~12 contact/friction levels ~2.5k each, two
serial runs of 24 rows ~20k each. A row solve alone is ~180-270 cycles; the
rest is per-level latency (dependent fetches of the level, group and rows,
~1-1.5k cycles each at 3 GHz) and per-row round trips. The ordered sweep's
dependency chain (~50 levels, up to 6 rows each, per iteration) is a latency
chain a GPU runs slower per step than a CPU core (~7 ns per row with
out-of-order overlap). Driver statistics: 255 registers, small spill.

## In game (vanilla D3D9 client, flatgrass 2560x1440, 8 pinned characters, contacts)

| Scene | Backend | Frame p50 / p95 ms | Worker CPU ms/tick | Keeps up | Display lag p50 |
|---|---|---|---|---|---|
| standing, 100 it | `cpu_mt_v2` | 11.16 / 13.35 | 31.9 | yes | ~10 ms |
| standing, 100 it | `gpu_vulkan` (batched) | **10.12** / 13.15 | **19.0** | **no** (up to 1.27 s dropped) | 147 ms |
| standing, 10 it | `cpu_mt_v2` | 11.44 / 13.27 | 9.5 | yes | 10.5 ms |
| standing, 10 it | `gpu_vulkan` | 12.17 / 14.15 | 15.4 | yes | 11.9 ms |
| moving, 10 it | `cpu_mt_v2` | 13.27 / 14.93 | 15.6 | yes | 13.1 ms |
| moving, 10 it | `gpu_vulkan` | 14.91 / 16.25 | 20.7 | yes | 14.9 ms |

In the game the kernel takes 7-10 ms instead of ~5 ms, plus 1-4 ms queueing:
the physics device is a separate GPU context from the game's D3D9 (or DXVK)
device, and the driver time-slices the two contexts. At 100 iterations freeing
~40 % of the worker CPU helps the frame (-9 % p50) but the physics cannot keep
real time. At 10 iterations the GPU path costs more CPU (fence waits, packing)
than the solve it replaces.

## Sharing the renderer's device (option 2: patched DXVK)

With the game rendering through DXVK, the solver can submit to the renderer's
own `VkDevice` instead of a second one, so the two overlap inside one GPU
context rather than being time-sliced.

**DXVK patch** (`patches/dxvk/0001-shared-compute-queues.patch` on DXVK v3.1.1
`b1a1c99`; `scripts/build-dxvk.ps1` clones, patches and builds
`build-dxvk/src/d3d9/d3d9.dll`, d3d9 only, with MSVC + Meson + Ninja):

- Device creation also enables `DXVK_SHARED_COMPUTE_QUEUES` (default 4)
  queues of a compute-only family, after any queue DXVK itself uses there.
  DXVK never submits to them. RTX 5090: family 2, queues 0-3 (DXVK uses
  families 0 and 1).
- `d3d9.dll` exports a C API (copy in `native/dxvk_shared_compute.h`).
  `DXVK_AcquireSharedComputeQueue` returns the instance, physical device,
  device, loader entry point and queue range of the most recently created D3D9
  device. Its `Owner` is a token holding a reference on DXVK's device object,
  which keeps the VkDevice and VkInstance alive until
  `DXVK_ReleaseSharedComputeQueue` even if the D3D9 device is destroyed first.
  Earlier builds AddRef'd the D3D9 device instead, which could revive a device
  whose last `Release` was already in its destructor (heap corruption in a
  create/destroy stress loop).
  `DXVK_Lock`/`UnlockSharedComputeQueue` wrap our submissions so they exclude
  DXVK's `vkDeviceWaitIdle`, which needs every queue externally synchronized.
- Priorities: when DXVK uses no queue of that family, the shared queues get LOW
  global priority (`VK_KHR_global_priority`;
  `DXVK_SHARED_COMPUTE_PRIORITY=low|medium|high|off`).
  `VK_NV_compute_occupancy_priority` is enabled where supported.

**Module side** (`native/vulkan_solver.cpp`, `attachShared()`):

- When the process's `d3d9.dll` has the exports and a device exists, the
  solver loads its Vulkan functions through DXVK's loader entry point and uses
  the shared queues.
- It never destroys that device or instance and never calls `vkDeviceWaitIdle`
  on it. Shutdown waits on its own queues, destroys its own objects, then
  releases the device reference.
- Dispatches take occupancy priority LOW (`MMDHL_VULKAN_OCCUPANCY`,
  `MMDHL_VULKAN_THROTTLING`).
- On the shared device each solve is split into dispatches of 3 iterations
  (`MMDHL_VULKAN_CHUNK`; 0 means one dispatch, the standalone default). The
  kernel gets its dispatch's iteration range as push constants, counted over
  each island's split-impulse iterations followed by its main iterations. The
  state carries over in the global buffers, so results are unchanged.

**Exactness and offline cost:**

- All 8 fixtures pass with 0 error at chunk sizes 0, 1, 3 and 7.
- The 8 rigs at {10, 100} iterations over 600 ticks on DXVK's device give a
  max position difference of 0 unchunked, with chunks of 10, and with the
  default of 3.
- Chunks of 3 cost 13 % more kernel time (8-rig mean at 100 iterations: 5.57
  -> 6.29 ms; at 10 iterations: 0.63 -> 0.70 ms).

**In game.** Setup: DXVK renderer with `dxvk.enableDescriptorHeap = False`,
flatgrass at 2560x1440, the 8 pinned characters, contacts, standing. Each GPU
run is paired with a `cpu_mt_v2` run in the same game session. Between
sessions the CPU run varies by about +-0.5 ms p50. Physics kept real time in
every shared-device run (nothing dropped). GPU load is the Windows 3D-engine
counter, which on this driver includes the solver's compute queue.

| Solver | It. | GPU frame p50 / p95 ms | Same-session CPU | Delta p50 / p95 | Physics lag p50 | Worker CPU ms/tick (GPU / CPU) | GPU load (GPU / CPU) |
|---|---|---|---|---|---|---|---|
| standalone device (DXVK renderer) | 100 | 9.09 / 11.93 | - | - | 134 ms, 3.25 s dropped | - | - |
| shared, one dispatch | 100 | 12.42 / 21.46 | 9.99 / 12.15 | +2.4 / +9.3 | 22 ms | 18.8 / 40.4 | 73 / 41 % |
| shared + LOW priorities, one dispatch | 100 | 13.08 / 22.52 | 10.02 / 12.25 | +3.1 / +10.3 | 24 ms | 17.3 / 38.3 | 83 / 38 % |
| shared + priorities, chunks of 10 | 100 | 12.37 / 15.76 | 10.67 / 13.14 | +1.7 / +2.6 | 23 ms | 20.9 / 41.7 | 83 / 34 % |
| shared + priorities, chunks of 3 | 100 | 11.93 / 13.63 | 10.48 / 12.83 | +1.5 / +0.8 | 23 ms | 22.2 / 39.3 | 92 / 36 % |
| shared, no priorities, chunks of 3 | 100 | 11.83 / 13.78 | 11.01 / 13.61 | +0.8 / +0.2 | 23 ms | 22.9 / 39.7 | 91 / 38 % |
| shared + priorities, one dispatch | 10 | 11.34 / 13.32 | 10.28 / 12.58 | +1.1 / +0.7 | 11 ms | 19.6 / 10.6 | 42 / 37 % |
| shared + priorities, chunks of 3 | 10 | 11.80 / 13.88 | 10.94 / 13.22 | +0.9 / +0.7 | 12 ms | 24.7 / 14.3 | 42 / 37 % |

- **Sharing removes the queueing and keeps physics real-time.** On the shared
  device the GPU wall time is the kernel time plus ~0.1 ms; on a separate
  device it carried 1-4 ms of queueing. At 100 iterations the physics keeps
  real time (lag ~23 ms), where a standalone device under the same renderer
  falls 134 ms behind and drops 3.25 s. The standalone run's frames look good
  only because its physics barely runs. The solve takes ~17 ms/tick of worker
  CPU off the cores.
- **Long dispatches hold up the renderer; short ones don't.** With one 4-10 ms
  dispatch per solve, frame p95 rises by ~10 ms. The same work split into
  3-iteration dispatches (0.15-0.3 ms each) stays within 1 ms of the CPU
  backend at p95. The penalty follows the length of a dispatch, not the amount
  of GPU work. Our reading (inferred from these measurements, not from driver
  documentation): at its synchronization points the renderer waits for work
  already running on the GPU, including our dispatches.
- **Priorities make no measurable difference**, with or without chunking. This
  covers LOW global queue priority and NV occupancy priority LOW. 128-thread
  workgroups did not help either (12.3 / 24.97 ms, one dispatch).
- **What remains:**
  - At 100 iterations frames are 1-1.5 ms slower at p50 (both chunk-3 sessions
    together: +1.1 / +0.5). The solver keeps ~3 workgroups per character
    resident for 6-10 ms per tick, and each fills a whole SM's register file
    (255 registers x 256 threads).
  - At 10 iterations the GPU path still costs more worker CPU than it saves
    (fence waits, packing) and ~0.9 ms p50.

## Conclusion and options

The exact Vulkan solver runs on the renderer's own device with bit-identical
results. It keeps real time at 100 iterations and frees ~17 ms of worker CPU
per tick, at a cost of ~1-1.5 ms frame p50 (under 1 ms p95) against
`cpu_mt_v2`. On this machine (RTX 5090, fast many-core CPU) the CPU backend is
still the better default. The GPU path could pay off where worker CPU is the
limit: fewer cores, or many characters at high iteration counts.

It requires DXVK as the renderer: the patched 4 MB `d3d9.dll`, plus
`dxvk.enableDescriptorHeap = False` on NVIDIA 610.88. Without it the backend
falls back to the standalone device, which loses in game.

Options:

1. Keep `cpu_mt_v2` as the default and `gpu_vulkan` experimental. Shared mode
   is automatic under the patched DXVK (current state).
2. Cut the remaining p50 cost by shrinking the solver's SM footprint. Fewer
   registers (the six register-held rows account for most of them) would let
   graphics warps share the SMs; packing small islands into one workgroup would
   free whole SMs. Either trades some kernel latency.
3. Relax parity for throughput: a GPU-native solver (Jacobi/graph-colored with
   many substeps, or XPBD) batched for all characters in one dispatch. Much
   more parallel, but motion would differ from nanoem/Bullet and needs visual
   validation.
4. Before any main-project use, decide how to ship DXVK: maintain a patched
   build, or propose the export API upstream. Also handle a recreated D3D9
   device (the solver keeps the Vulkan device it attached to alive until
   shutdown; the D3D9 device itself may go away).

## Reproduce

```
python scripts/bootstrap.py
powershell -File scripts/build.ps1 -SkipBootstrap
build/bin/Release/mmdhl_worker.exe --probe-vulkan probe.json
build/bin/Release/mmdhl_compute_tests.exe gpu_vulkan ordered
python scripts/vulkan-rigs.py --iterations 10 100
build/bin/Release/mmdhl_vulkan_compare.exe --cache <cache> --asset <id> --iterations 100 --order ordered
MMDHL_VULKAN_CLOCK=1 build/bin/Release/mmdhl_vulkan_compare.exe ...   # per-level cycles, driver statistics
python scripts/test-performance.py <label> --count 8 --backend gpu_vulkan --contacts --motion standing --iterations 100
powershell -File scripts/build-dxvk.ps1        # build-dxvk/src/d3d9/d3d9.dll: DXVK 3.1.1 + patches/dxvk
build/bin/Release/mmdhl_vulkan_compare.exe --cache <cache> --asset <id> --iterations 100 --dxvk build-dxvk/src/d3d9/d3d9.dll
python scripts/vulkan-rigs.py --dxvk build-dxvk/src/d3d9/d3d9.dll --iterations 10 100
```

Developer environment variables:

- Solver: `MMDHL_DISABLE_VULKAN`, `MMDHL_VULKAN_DEVICE`,
  `MMDHL_VULKAN_WORKGROUP`, `MMDHL_VULKAN_BATCH_US`, `MMDHL_VULKAN_CLOCK`.
- Shared device: `MMDHL_VULKAN_STANDALONE`, `MMDHL_VULKAN_CHUNK`,
  `MMDHL_VULKAN_OCCUPANCY`, `MMDHL_VULKAN_THROTTLING`.
- DXVK: `DXVK_SHARED_COMPUTE_QUEUES`, `DXVK_SHARED_COMPUTE_PRIORITY`.

In-game tests need the build's natives copied into the game and the repo's Lua
staged (isolated session); restore the original natives afterwards.

For the shared device:

- Copy `build-dxvk/src/d3d9/d3d9.dll` to `GarrysMod/bin/win64`.
- Point `DXVK_CONFIG_FILE` at a `dxvk.conf` with
  `dxvk.enableDescriptorHeap = False`; NVIDIA 610.88 crashes at device creation
  otherwise.
- The solver attaches by itself:
  `GetSecondaryCapabilities().gpu_vulkan.context` reads `dxvk-shared`.
- Remove the DLL afterwards.
- `scripts/install.ps1` moves an existing loose `addons/mmd_hotloader` folder
  into `mmdhl-install-backups`. Copy natives by hand if a user's loose install
  must stay in place.
