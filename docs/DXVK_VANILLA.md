# DXVK vs stock D3D9 in ordinary GMod scenes (2026-09-26)

Question: does running GMod on DXVK (D3D9 -> Vulkan) raise the frame rate in
normal scenes, without MMD models?

**Short answer:** not with GMod's multicore rendering, which this install uses.
It averages +0.3 % across eight scenes:

- **Faster:** the near-empty map, +20 %, with half the p95 frame time.
- **Equal:** prop and character scenes on gm_flatgrass (-3 % to +3 %).
- **Slower:** gm_construct scenes, -4 % to -6 %.

With single-threaded rendering (`mat_queue_mode 0`) DXVK is about 12 % faster.
Our addon forces that mode while MMD models are present, which is why MMD
scenes gained ~12 % on DXVK earlier.

## Setup

- **Hardware:** Ryzen 9 9950X3D and RTX 5090 on driver 610.88, 2560x1440
  windowed.
- **Game settings:** the user's own config.
  - Rendering: `mat_queue_mode 2` with `gmod_mcore_test 1` (multicore),
    `fps_max 0`, vsync off, `mat_disable_d3d9ex 1`, DX level 95, HDR.
  - Effects: `r_waterforceexpensive 1`, `r_flashlightdepthres 16384`.
  - Threaded bone setup and leaf system (autoexec).
- **Isolation:** isolated test session (`-noaddons -noworkshop -nosound`). This
  addon's Lua is loaded but idle: no MMD entity exists, so it leaves the queue
  mode alone.
- **DXVK:** upstream 3.1.1 x64 `d3d9.dll`. The config sets
  `dxvk.enableDescriptorHeap = False`, required on driver 610.88, and
  `dxvk.enableNvRawAccessChains = False`.
- **Tools:** `scripts/measure-vanilla.py` with `tests/game/vanilla-scene.lua`.
  - Scenes are seeded and rebuilt identically each time.
  - The camera is locked, with the player frozen at the camera so everything
    is in the PVS.
  - Each measurement is 3 s warm-up then 10 s of frames, counting only frames
    while the game has focus.

Scenes (screenshots via `--shots`):

| Scene | Content |
|---|---|
| flatgrass_empty | spawn view, nothing spawned |
| flatgrass_props | 400 frozen HL2 props (15 models) in a 20x20 grid |
| flatgrass_physics | 200 unfrozen props, kicked every 1.5 s (seeded) |
| flatgrass_characters | 48 animated citizens (prop_dynamic walk cycle). **This install replaces the citizen models** (loose `garrysmod/models/humans/...`: anime models), so this is a heavy-character scene |
| flatgrass_combine | 48 animated stock Combine soldiers / Metrocops |
| construct_view | gm_construct spawn view (world, 3D skybox, water) |
| construct_props | 300 frozen props in a 15x20 grid on gm_construct |
| construct_flashlight | construct_view with the flashlight on (16384 depth map) |

## Results

### Multicore rendering (the user's normal setting)

These are the sessions that agreed with each other: two D3D9 sessions
(median 2 % apart) and three DXVK sessions. The runs alternated D3D9, DXVK,
D3D9, DXVK, followed by DXVK then D3D9.

| Scene | D3D9 FPS | DXVK FPS | Change | p95 frame ms D3D9 / DXVK | GPU 3D load D3D9 / DXVK |
|---|---|---|---|---|---|
| flatgrass_empty | 1413 | 1697 | +20% | 1.87 / 0.88 | 78 / 65 % |
| flatgrass_props | 619 | 602 | -3% | 2.18 / 2.22 | 44 / 30 % |
| flatgrass_physics | 471 | 485 | +3% | 4.01 / 3.89 | 34 / 32 % |
| flatgrass_characters | 249 | 249 | +0% | 6.84 / 6.57 | 69 / 31 % |
| flatgrass_combine | 453 | 448 | -1% | 4.55 / 4.53 | 31 / 29 % |
| construct_view | 829 | 780 | -6% | 1.57 / 1.69 | 62 / 42 % |
| construct_props | 365 | 348 | -5% | 3.32 / 3.53 | 37 / 25 % |
| construct_flashlight | 776 | 743 | -4% | 1.67 / 1.73 | 83 / 62 % |

Geometric mean over the eight scenes: **+0.3 %**. Every scene is limited by
the CPU. GPU load is consistently lower under DXVK: characters 69 -> 31 %,
flashlight 83 -> 62 %. The likely reason is cheaper presentation: the D3D9 path
is windowed and non-Ex, while DXVK uses a flip-model swapchain. On a slower
GPU, or at GPU-heavy settings, that could turn into frame rate; here it does
not.

### Single-threaded rendering (`mat_queue_mode 0`)

This is the mode our addon forces while MMD models are present. Each session
measured both modes on the same built scene, one after the other. The table
shows the share of multicore FPS kept in single-threaded mode (mean of three
sessions per renderer). That share is not affected by a slow session, since
the session slows both modes.

| Scene | D3D9 keeps | DXVK keeps | DXVK advantage |
|---|---|---|---|
| flatgrass_empty | 59 % | 72 % | +21 % |
| flatgrass_props | 61 % | 73 % | +19 % |
| flatgrass_physics | 67 % | 78 % | +16 % |
| flatgrass_characters | 55 % | 56 % | +2 % |
| flatgrass_combine | 71 % | 82 % | +15 % |
| construct_view | 57 % | 65 % | +14 % |
| construct_props | 60 % | 63 % | +5 % |
| construct_flashlight | 56 % | 61 % | +8 % |

Geometric mean: **+12 %**. Multicore rendering is roughly equal between the
renderers, so DXVK is ~12 % faster than D3D9 when GMod renders
single-threaded. DXVK won every single-threaded comparison in every session
pair (16/16 and 8/8). This matches the MMD scenes (8 characters, ~12 % better
p50 on DXVK), where the addon forces queue mode 0.

## Noise and pitfalls

- **Slow sessions:** three sessions after ~01:10 ran 15-30 % slower than the
  same renderer's other sessions in some or all scenes: two D3D9 sessions and
  one DXVK session. No background process showed up when checked, so the cause
  is unknown (possibly other use of the PC). Direct FPS comparisons therefore use the sessions that agreed. The
  single-threaded estimate uses the within-session ratio.
- **CCD placement on the 9950X3D:**
  - The V-Cache CCD is logical processors 0-15. Pinning a DXVK session there
    made construct_props 2.58 ms instead of 2.81-2.85 ms on the other CCD
    (~9 %).
  - Windows may place a Vulkan-rendered GMod differently from a D3D9 one; the
    unpinned DXVK construct timings resembled the frequency CCD's. This was
    not verified.
  - `--affinity 0xffff` pins a run, but pinned runs drifted more here (all work
    on 8 cores).
- **Custom content:** the citizen paths resolve to replacement models, and
  `models/player/kleiner.mdl` is replaced too. flatgrass_combine is the stock
  character scene.

## Reproduce

```
powershell -File <scratch>/dx_session.ps1 -Renderer d3d9|dxvk     # isolated session; dxvk copies DXVK 3.1.1's d3d9.dll into bin/win64
python scripts/measure-vanilla.py <label> [--queue-modes 2,0] [--affinity 0xffff] [--shots <dir>]
python scripts/measure-vanilla.py --compare labelA,labelB,...
```

The session helper also sets `DXVK_CONFIG_FILE` to the no-descriptor-heap
config. Afterwards restore the original natives, remove `bin/win64/d3d9.dll`
and `%LOCALAPPDATA%\dxvk`.
