# Vulkan test repository merged into main: validation and performance (2026-09-26)

Branch `claude/vulkan-backend-merge-test-4f317a` merges
[Gmodl_MMD_Hot_Loader_Vulkan](https://github.com/SheepyLord/Gmodl_MMD_Hot_Loader_Vulkan)
at `scene-capture` 1fb6379 into main 1a57ed7. That tip contains all three test-repo lines:

- **Vulkan solver:** the `gpu_vulkan` secondary backend (exact Vulkan compute constraint solver) and the patched DXVK
  whose compute queues it shares ([VULKAN_COMPUTE.md](VULKAN_COMPUTE.md), [DXVK_VANILLA.md](DXVK_VANILLA.md)).
- **Multicore rendering:** character models draw on Source's render thread instead of forcing
  `mat_queue_mode 0` ([MULTICORE_RENDERING.md](MULTICORE_RENDERING.md)).
- **Scene capture:** the secondary-physics scene capture is culled to consumer interest regions
  ([SCENE_CAPTURE.md](SCENE_CAPTURE.md)).

## Merge

- **Conflict:** one, in `native/renderer.cpp`. The light-overlap cache on the queued `DrawJob` path keeps main's
  snapshot geometry-revision check (Codex review fix). Main's other renderer, model, runtime and secondary changes
  merged cleanly.
- **Follow-up test fix:** `tests/test_qol_lua.py` predates the region-culled scene hook, which classifies
  `player.GetAll()` and then `ents.FindInBox` around each region. Its mock gained `player.GetAll` and a check of the
  region path: a 64-unit margin, no full entity scan, nearby and player actors excluded.
- **Docs:** README and [API.md](API.md) no longer say native rendering requires `mat_queue_mode 0`.

## Validation

| Check | Result |
|---|---|
| Native build (`scripts/build.ps1`) with its checks | pass (354 native, 33 async, 18 mid-phase checks, ...) |
| CTest | 25 / 25 |
| Offline Lua (`scripts/check-lua-tests.py`), i18n, `tests/test_native_installer.py` | 23 / 23, 1011 / 1011 phrases, pass |
| Patched DXVK (`scripts/build-dxvk.ps1`) | builds; exports the four `DXVK_*SharedComputeQueue` functions |
| `mmdhl_compute_tests gpu_vulkan ordered` | 8 / 8 fixtures, 0 position and row error |
| 8 pinned rigs x {10, 100} iterations x 600 ticks, lockstep vs `cpu_mt_v2` | max position difference **0** in all 32 runs: 16 on a standalone device, 16 on the patched DXVK's shared queues |
| In game, patched DXVK + multicore rendering | solver context `dxvk-shared` (4 queues, chunks of 3) while Source renders with `mat_queue_mode 2`. The test repo measured the shared solver only with mode 0 forced, so this combination is new. |
| Screenshots (PrintWindow, standing scene) | main vs merge on D3D9: mean difference 0.1/255. D3D9 vs DXVK + `gpu_vulkan`: 1.1/255 (2.4 in the character band: DXVK filtering and hair phase). No visible differences. |

## Performance

**Setup:**
- **Hardware:** Ryzen 9 9950X3D, RTX 5090 (driver 610.88).
- **Game:** gm_flatgrass at 2560x1440, tick rate 128, isolated sessions (`-noaddons`), the user's settings
  (multicore on, `fps_max 0`).
- **Scene:** the 8 pinned characters standing with contact plates (collision mode 2). `+48` adds 48 animated
  citizens (render load and scene-capture load).
- **Runs:** 8 s warm-up, then 10 s of focused frames.
- **Sessions,** in order: A1 main / D3D9 → B1 merge / D3D9 → V1 merge / DXVK → V2 merge / DXVK (reversed order) →
  B2 merge / D3D9 → A2 main / D3D9. The whole benchmark took about 15 minutes.
- **Renderers:** D3D9 is the stock renderer. DXVK is this branch's patched 3.1.1 `d3d9.dll` with
  `dxvk.enableDescriptorHeap = False`.
- **Repeatability:** in the 8-character scene A1 and A2 agree within 0.03 ms p50; B1 and B2 agree within
  0.00-0.14 ms in every scene. Main's collapsing citizens scene varies more (73.0 vs 78.7 ms).
- **Excluded runs:** two V1 CPU-solver runs lost window focus mid-run. V2 repeats them.

Frame times are ranges over the sessions listed.

### 8 characters, 100 iterations

| Build / renderer / solver | Sessions | Frame p50 / p95 ms | FPS | Physics |
|---|---|---|---|---|
| main / D3D9 / `cpu_mt_v2` | A1, A2 | 11.70-11.73 / 13.72-13.77 | 85 | real time, lag 11.7 ms |
| merge / D3D9 / `cpu_mt_v2` | B1, B2 | **4.08 / 5.51-5.57** | 245 | real time, lag 8 ms |
| merge / D3D9 / `gpu_vulkan` (standalone device) | B1 | 7.87 / 12.21 | 127 | **falls behind**: lag 123 ms, 10.1 s dropped |
| merge / DXVK / `cpu_mt_v2` | V1, V2 | 4.18 / 5.39-5.59 | 239 | real time |
| merge / DXVK / `gpu_vulkan` (DXVK's device) | V1, V2 | 4.63-4.67 / 7.62-9.10 | 215 | real time, lag 13-15 ms |

### 8 characters + 48 citizens, 100 iterations

| Build / renderer / solver | Sessions | Frame p50 / p95 ms | FPS | Physics |
|---|---|---|---|---|
| main / D3D9 / `cpu_mt_v2` | A1, A2 | 73.0-78.7 / 81.3-93.9 | 13-14 | **falls behind**: 8-14 s dropped |
| merge / D3D9 / `cpu_mt_v2` | B1, B2 | **9.33-9.47 / 11.26-11.75** | 106 | real time |
| merge / DXVK / `cpu_mt_v2` | V2 | 9.75 / 11.89 | 103 | real time |
| merge / DXVK / `gpu_vulkan` | V1, V2 | 10.77-10.93 / 13.03-13.35 | 92 | real time, lag 21 ms |

### 8 characters, 10 iterations

| Build / renderer / solver | Sessions | Frame p50 / p95 ms | FPS |
|---|---|---|---|
| main / D3D9 / `cpu_mt_v2` | A2 | 11.41 / 13.40 | 88 |
| merge / D3D9 / `cpu_mt_v2` | B1, B2 | 4.22-4.23 / 5.55-5.74 | 237 |
| merge / DXVK / `cpu_mt_v2` | V2 | 4.37 / 5.57 | 229 |
| merge / DXVK / `gpu_vulkan` | V1 | 4.61 / 6.14 | 217 |

### Solver cost (same session, sum over the 8 characters)

| Scene | `cpu_mt_v2` worker CPU ms/tick | `gpu_vulkan` worker CPU ms/tick | `gpu_vulkan` kernel ms per solve |
|---|---|---|---|
| 8 characters, 100 it | 37.9-45.4 | 24.5-24.7 | 7.7-9.2 |
| + 48 citizens, 100 it | 35.7 | 19.4-23.8 | 7.9-9.7 |
| 8 characters, 10 it | 13.6-17.8 | 17.7 | 0.8 |

**GPU 3D load:**
- D3D9 with the CPU solver: 41-49 %.
- DXVK with the CPU solver: 17-32 %.
- DXVK with the Vulkan solver: 72-91 % at 100 iterations, 40 % at 10.

**Offline, on DXVK's device, 8-rig mean step:**
- 10 iterations: CPU 1.56 ms, GPU 2.29 ms (kernel 0.68).
- 100 iterations: CPU 4.77 ms, GPU 7.94 ms (kernel 6.12).

## Findings

1. **The merge is a large win on the stock renderer.** Main forces single-threaded rendering whenever a model
   exists, and its scene capture scans every object on the map.
   - 8 characters: 11.7 → 4.1 ms p50 (2.9x) at 100 iterations, 11.4 → 4.2 ms at 10.
   - With 48 animated citizens: 73-79 → 9.4 ms (about 8x). Main also drops physics time there; the merge keeps real
     time.
   - The physics step itself is unchanged: summed step wall time is 43.5 vs 44.8 ms/tick at 100 iterations.
2. **DXVK brings no frame-time gain once models draw with multicore rendering:** +0.1 to +0.4 ms p50. The earlier
   ~12 % DXVK advantage came from the forced `mat_queue_mode 0`. DXVK does lower GPU load by about half.
3. **The Vulkan solver works on DXVK's device, but costs frame time on this machine.**
   - It is exact and keeps real time at 100 iterations.
   - It frees 13-21 ms/tick of worker CPU.
   - Frames are +0.5 ms p50 / +2-3.5 ms p95 with 8 characters and +1.0-1.2 / +1.0-1.5 ms with citizens.
   - At 10 iterations it saves no CPU.
   - Without DXVK (standalone device next to D3D9) it cannot keep real time.
4. **Full Vulkan (DXVK + `gpu_vulkan`) vs main:** 11.7 → 4.65 ms p50 and 76 → 10.9 ms with citizens. All of that
   comes from the merged rendering and scene-capture work. It is 0.4-1.5 ms p50 slower than the merge on stock D3D9
   with the CPU solver.

**Recommendation:**
- Merge with the current defaults: stock D3D9 and `cpu_mt_v2`.
- Keep `gpu_vulkan` and the patched DXVK experimental and opt-in. They need the patched `d3d9.dll` and a `dxvk.conf`,
  and shipping them is still open ([VULKAN_COMPUTE.md](VULKAN_COMPUTE.md), option 4).
- The Vulkan path may pay off where worker CPU is the limit: fewer cores, or more characters at high iteration
  counts. Only this machine was measured.

**Not exercised here:**
- Queued rendering of the first-person view.
- The RTX Remix fixed-function path.
- The `mmdhl_vertex_cache 0` stream path.
- A recreated D3D9 device under the shared solver.

## Reproduce

```
python scripts/bootstrap.py; powershell -File scripts/build.ps1          # natives + checks
powershell -File scripts/build-dxvk.ps1                                  # build-dxvk/src/d3d9/d3d9.dll
python scripts/vulkan-rigs.py --iterations 10 100                        # standalone device
python scripts/vulkan-rigs.py --dxvk build-dxvk/src/d3d9/d3d9.dll --iterations 10 100
python scripts/test-performance.py <label> --count 8 --contacts --motion standing --iterations 100 [--citizens 48] --backend cpu_mt_v2|gpu_vulkan
```

**In-game sessions:**
- **Natives:** copy the natives by hand (lua/bin x6 + bin/win64 runtime). `scripts/install.ps1` moves a loose
  `addons/mmd_hotloader`.
- **Session:** start an isolated session that stages the matching Lua.
- **DXVK:** for DXVK runs, copy the patched `d3d9.dll` to `GarrysMod/bin/win64` and point `DXVK_CONFIG_FILE` at a
  config with `dxvk.enableDescriptorHeap = False`.
- **Afterwards:** restore the original natives and remove the DLL and `%LOCALAPPDATA%\dxvk`.

The build rewrites `addon/lua/mmdhl/native_policy.lua` to approve the local natives; do not commit that change.
