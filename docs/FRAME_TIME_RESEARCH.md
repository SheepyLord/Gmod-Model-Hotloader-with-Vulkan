# Frame time with many MMD models in view (2026-09-23)

Question: where does the frame go when several imported models are on screen,
and what would raise the frame rate?

Scene: the user's save `gm_construct 2026-9-23 18-26-10.gms`: ten copies of
Furina R18 (64,031 vertices, 533 bones, 356 bodies, 54 materials each;
640,310 vertices in total), ragdolls lying in a row, 4-7 facial morphs active,
`cpu_mt_v2`, collisions on, `mmdhl_secondary_stretch 1`. Measured in an owned
session on the Steam install (2560x1440, `-NoWorkshop`; folder addons such as
the expression-response addon still load), camera locked to the saved player
view, 8-second windows after 3 seconds of warm-up. The user's own HUD reading
(46.6 FPS, p50 20.9 / p95 26.9 ms) matched the owned baseline.

Reproduce:

```
python scripts/measure-scene.py <label> --save "<save>.gms" [--variants a,b,...] [--sample]
```

`tests/game/scene-measure.lua` records frame times, the MMD stage timings and
the per-frame cost of every Lua hook (all addons). `--sample` also runs a
sampling profiler of the main thread (`native/thread_sampler.hpp`,
`mmdhl.native.StartMainThreadSampling`): about 1 kHz, attribution by module,
by call chain, and by function inside our DLLs. `mmdhl_skinning_analysis
<cache> <asset>` reports the GPU-skinning feasibility numbers below.

## What the frame costs

| Variant | Frame p50 / p95 (ms) | GPU 3D busy | Meaning |
|---|---|---|---|
| baseline | 20.2 / 26.0 | 9 % | as played |
| HUD overlay on | 20.2 / 25.0 | 10 % | the overlay costs about 0.3 ms |
| `r_shadows 0` | 18.8 / 23.8 | 9 % | engine shadow pass minus the upload it carried |
| secondary physics off | 15.2 / 18.8 | 13 % | not an option (no hair motion); shows interplay |
| poses frozen ("still") | 5.4 / 7.0 | 26 % | every draw kept, no capture, skinning or upload |
| MMD work suspended | 1.34 / 3.45 | 42 % | engine floor with the scene present |
| suspended, `mat_queue_mode 2` | 0.84 / 2.61 | 65 % | what forcing mode 0 costs the engine here |
| 16 / 8 / 4 workers | 19.1 / 25.2 / 26.2 | | fewer lanes do not help |

The frame is CPU-bound on the main thread; the GPU is about 10 % busy. Of the
20 ms, about 19 ms is MMD work, and about 14 ms of that is the per-frame
vertex pipeline (pose capture, skinning, upload), not drawing.

Main-thread attribution of the baseline (sampling, 3,800 samples):

| Where | Share | About |
|---|---|---|
| Waiting in our worker pool (`Pool::run`) for skinning/fill tasks | 14 % | 3.0 ms |
| Lua (all addons; our pose capture, morph and material sync dominate the extra) | 17 % | 3.6 ms (1.2 ms when frozen) |
| NVIDIA driver under D3D9 (static-buffer modification) | 11 % | 2.2 ms (0.2 ms when frozen) |
| Skinning executed by the main thread while it waits | 6 % | 1.2 ms |
| Overlap-guard draw ranges rebuilt per draw (`drawRange`) | 5 % | 1.0 ms |
| Vertex-buffer lock (`CMeshBuilder::BeginModify`) | 3.6 % | 0.75 ms |
| Server simulation (listen server: server.dll, vphysics, server Lua) | 10 % | 2 ms |
| Heap allocations in the draw path | 2.6 % | 0.5 ms |

The worker waits are the pool's own scheduling, not physics: they stay at
17 % with physics off. One shared mutex, `notify_all` wakes of 30 threads and
nested groups make hand-offs slow; with 16 workers the waits fall to 5 %, but
the main thread then does more of the skinning itself, so the frame is the
same.

Per-frame upload: 41 MB (640,310 vertices x 64 bytes) every frame, because
every palette row changes (the expression addon moves resting ragdolls by
0.001-0.1 units per frame) and active morphs mark every buffer fully dirty.

## Fixed now

- **Overlap-guard draw ranges are cached** (`native/renderer.cpp`,
  `LightMask::ranges`). The flashlight-overlap mask was cached per snapshot,
  but the index ranges derived from it were rebuilt from every triangle on
  every draw of every pass. They are now kept while the mask content is
  unchanged (compared byte for byte), so draws are identical. Measured: draw
  time 1.95 -> 0.83 ms per frame, frozen-pose frame 5.4 -> 4.6 ms, baseline
  about 20 -> 19.2 ms. This also confirmed that the sampler's attribution
  turns into real frame time.

## Ranked plan

| # | Change | Measured cost it attacks | Expected gain | Effort / risk |
|---|---|---|---|---|
| 1 | Worker pool: per-parallel-for atomic chunk index, per-group completion event instead of global `notify_all`, brief spin before sleeping | 3.0 ms waiting | 1.5-2.5 ms | medium / low (timing and async suites cover nesting, exceptions, resizing) |
| 2 | Trim per-frame Lua: native pose capture (bone-to-world matrices read in the module instead of `SetupBones` + `GetBoneMatrix` + three userdata per bone), cache material-state and morph encodings (today both are rebuilt and JSON-encoded every frame per model), return bounds as numbers instead of JSON (decoded three times per model per frame), skip the frame-profile JSON when the overlay is off | about 2.4 ms of extra Lua plus GC pressure | 1.5-2 ms | medium / low |
| 3 | Slimmer vertex upload: drop attributes the bound materials do not read (tangent userdata, colour) from the shared layout, 64 -> 36-48 bytes | upload 4.3 ms + driver 2 ms | about 2 ms | medium / medium (per-model layout, flashlight/depth/shadow materials must agree) |
| 4 | GPU skinning (below) | skinning 12 ms of worker time, upload, driver copy, most of the pool waits | frame toward 8-9 ms (about 2.3x FPS in this scene) | large / medium |
| 5 | Update-rate LOD: skin and upload distant or unfocused models at 30 or 20 Hz, staggered across frames | per-model pipeline | scales with distance spread; none for this compact row | medium / low |

Not levers here: the GPU (10 % busy), `mat_queue_mode` (0.5 ms in this scene;
making native draws queue-safe is a large rewrite), the HUD overlay, and the
physics ticks themselves (asynchronous; their CPU is off the main thread).

Items 1-3 together are estimated at 5-6 ms: about 20 -> 14 ms (70 FPS) for
this scene without changing how models look.

## Items 1-3 implemented (results)

What changed:

1. **Worker pool** (`native/jobs.cpp`). Each parallel-for is a group whose
   chunks are claimed with an atomic index; the last chunk wakes the owner
   through the group's own counter (`std::atomic::wait`), workers sleep on a
   counting semaphore released only for the chunks available, and every thread
   spins 25 us before sleeping. Priority is unchanged: frame groups before
   background physics ticks, and an owner waiting on its group helps only other
   groups.
2. **Per-frame Lua.** Pose capture passes the bone matrices straight to
   `SubmitPresentationMatrixBatch` (decomposed natively). Networked asset/rig
   strings and the entity list are cached per frame. Material state is re-synced
   every 8 frames or at once when `SetSubMaterial`/`SetBodygroup` changes it.
   Morph tables are double-buffered. `GetBoundsValues` returns numbers only
   when the sequence changed. `PrepareFrame(false)` skips the frame-profile
   JSON unless the overlay, a settings panel or a harness asks for it
   (`mmdhl.RequestFrameProfile`).
3. **Compact vertices** (`native/vertex_upload.hpp`, `native/renderer.cpp`).
   Shared buffers use Source's own compressed vertex (`VERTEX_FORMAT_COMPRESSED`,
   `COMBINEDTANGENTS_UBYTE4`: normal and tangent with binormal sign in one
   UBYTE4), 64 -> 32 bytes, streamed with two 16-byte stores. No attribute is
   dropped. The encoder rounds where mathlib's truncates: 1.9 degrees worst case
   instead of 3.7, decoded by the unchanged shader. It is used only when the
   engine reports the expected layout and every material drawn from the
   instance declares `VERTEX_FORMAT_COMPRESSED` (or reads positions only), as
   StudioRender requires. Otherwise the instance keeps the 64-byte layout.
   Toggle: `mmdhl_compact_vertices` / "Compact vertex upload".

Measured on the same save and camera (`measure-scene.py --wrap`, p50 ms):

| | before (pre123) | after |
|---|---|---|
| frame | 18.37 / 17.87 | 15.60 / 14.78 (items 1+2 alone: 15.20 / 15.07) |
| pose capture | 2.13 | 0.93 |
| Lua draw | 1.51 | 0.95 |
| native render | 5.39 | 4.82 |
| upload (fill) | 4.18 (2.52) | 3.64 (1.82) |
| bytes uploaded per frame | 42.4 MB | 21.2 MB |

Lua per frame: `PrepareNativePresentation` 2.36 -> 1.15, `SyncMaterialState`
1.07 -> 0.14, `GetRig` 0.50 -> 0.09, `UpdateNativeVisuals` 0.38 -> 0.08,
`Decode` 0.26 -> 0.08, bounds 0.07 -> 0.007. About 3 ms in total (18.1 -> 15.0
ms, 55 -> 67 FPS). Halving the bytes cut the fill time but not the driver's
lock/unlock (about 1.8 ms), so upload fell by only about 0.5 ms; `PrepareFrame`
(1.6 ms) is unchanged.

Rendering: screenshots with compact vertices on and off differ by a mean of
0.11/255 (p99 2/255), the same as two captures with identical settings (0.08,
p99 2). The layout check and a 20,000-vector encode/decode test are in
`mmdhl_timing`.

Physics harness (flatgrass, 8 pinned characters, cpu_mt_v2, contacts):
- Standing: frame p50 11.7 / p95 14.9 ms (v2.1: about 14-15 / 17.6-18). All gates pass.
- Clustered moving: frame p50 23-26 ms (v2.1: 28-30). The keep-up gate
  failed in 3 of 3 runs. Each time the 607-body character dropped 0.25-0.5 s
  while tangled. Its tick is compute-bound (10.5-11 ms CPU per 10.5-11 ms tick),
  not waiting on the pool. v2.1's comparison run failed the same way
  (12.2 ms, 0.52 s), and only one v2.1 run passed. The limit is the single-lane
  step of one heavy rig under contact.

## GPU skinning feasibility

Source skins its own models on the GPU through the same materials the native
path already uses (VertexLitGeneric, depth, flashlight and shadow shaders):
static vertex buffers carrying bone indices and weights, `SetNumBoneWeights`
and up to 53 `LoadBoneMatrix` calls per draw. The per-frame work would become
bone matrices only; vertex data would be uploaded once, plus morph deltas on
frames where a morph weight changes (about 6 % of frames per model in this
scene, and only the vertices of the changed morphs).

`mmdhl_skinning_analysis` on five imported models:

| Model | Vertices | SDEF/QDEF | 4-weight vertices | Draws with <= 53 bones per batch | Vertices any vertex morph touches |
|---|---|---|---|---|---|
| Furina | 64,031 | 0 | 18 % | 67 for 54 materials (+24 %) | 47 % |
| Xin | 92,521 | 0 | 29 % | 154 for 47 materials (3.3x; one hair material spans 184 bones) | 6 % |
| Firefly | 154,682 | 0 | 19 % | 124 for 76 materials | 67 % |
| March 7th | 22,754 | 0 | 5 % | 41 for 31 materials | 17 % |
| Daniya | 294,632 | 0 | 13 % | 126 for 98 materials | 48 % |

The shaders support three weights per vertex. Dropping the smallest weight
and renormalising, over poses rotating every bone by up to 25 degrees:

| Model | Mean error | Worst vertex |
|---|---|---|
| Furina | 0.008 PMX | 0.15 PMX (about 0.5 Source units) |
| March 7th | 0.005 PMX | 0.10 PMX |
| Xin | 0.019 PMX | 1.08 PMX (about 3.5 Source units) |

So a GPU path needs a fallback: triangles touching vertices whose fourth
weight matters (for example error above 0.02 PMX under a test pose) stay on
the current CPU path in their own small dynamic buffer; everything else is
GPU skinned. Batches should be formed with bone-aware triangle ordering inside
opaque materials to keep the draw-call growth down (greedy authored order is
the worst case shown above). The RTX Remix fixed-function path cannot run
vertex shaders and keeps CPU skinning. Expected result for this scene: the
frozen-pose frame (4.6 ms) plus pose capture and the extra matrix loads and
draws, about 8-9 ms.

## Items 4-5 implemented: GPU skinning and update-rate LOD

### GPU skinning (`mmdhl_gpu_skinning`, default on)

Carrier models draw through Source's own hardware-skinning path, the one
studio models use: `SetNumBoneWeights(3)`, up to 53 `LoadBoneMatrix` calls
per draw, and static vertex buffers holding the rest vertex (morphed MMD
position, rest normal and tangent, UV), two stored weights (the shader
computes the third as 1 - w0 - w1) and three batch-local matrix slots.

- **Per model, once** (`Model::gpuSkin`, `native/model.cpp`). Each vertex's
  three largest weights are kept and renormalised. SDEF/QDEF vertices keep CPU
  skinning, and so do BDEF4 vertices whose position moves more than 0.05 PMX
  when the smallest weight is dropped. That is tested over 12 poses (every bone
  rotated up to 25, 45 and 90 degrees, four of each). Every triangle using such
  a vertex stays on the CPU path. The plan also stores a rest-space box per bone,
  widened by each vertex's morph reach, for bounds.
- **Per frame** (`Instance::publish`). The bone palette (rest MMD space to
  Source world, already computed) goes into the snapshot. Only the CPU subset
  is deformed: CPU triangles plus flashlight-overlap candidates. Boundary
  vertices use the same three weights as the shader, so shared edges match.
  Bounds come from the transformed bone boxes, which include each vertex-morph
  offset once; while a vertex morph weight exceeds 1 in magnitude (up to 2,
  more through group morphs) every box grows by the excess times its largest
  per-vertex morph reach. Morph weights that change bump a
  rest version, and only the vertices those morphs touch are rewritten.
- **Draw** (`native/gpu_topology.hpp`, `native/renderer.cpp`). Each material
  is split into batches of at most 53 bones, with triangles ordered by their
  dominant bone first. That gives Furina 64 draws for 54 materials and Xin 65
  for 47 (authored order: 67 and 154). A vertex used by two batches is
  duplicated. Buffers are written once and partially rewritten when rest data
  changes, one of three per rest version. Per batch, bone 0 is also loaded as
  the material-system model matrix, because the shader API copies the model
  matrix into bone slot 0 when it commits the skinning constants. The parts'
  CPU triangles then draw through the existing shared-buffer path.
- **Fallback.** A shader outside VertexLitGeneric/UnlitGeneric/DepthWrite/
  ShadowBuild, a material vertex format beyond the common one, the Remix
  fixed-function renderer, soft bodies and frozen instances keep full CPU
  skinning. The reason is listed in `RenderStats().gpuSkinning.blocked`. Lua
  position queries (`GetMaterialPositions`, the alignment probe) request one
  fully deformed publish.
- **ABI.** In the pinned materialsystem.dll, `IMatRenderContext` slot 102
  (`LoadBoneMatrix`) and slot 51 (`SetNumBoneWeights`) both forward to the
  shader API. Its `LoadBoneMatrix` stores the matrix and, for bone 0, loads the
  model matrix. `IMaterial` slot 49 is `GetShaderName`. All were checked by
  disassembly.

`mmdhl_gpu_skinning_tests` (52 checks) covers the fixture and three imported
models:

| Model | CPU-skinned triangles | Batches | Emulated shader vs CPU, up to 3 weights | Kept 4th weight, unseen 20-degree pose |
|---|---|---|---|---|
| Furina | 6,043 of 102,680 (5.9 %) | 64 / 54 materials | 1.5e-5 units | 0.10 units |
| Xin | 25,702 of 117,580 (22 %) | 65 / 47 | 2.3e-5 | 0.11 |
| Daniya | 19,856 of 452,226 (4.4 %) | 125 / 98 | 1.5e-5 | 0.11 |

The classification study in `mmdhl_skinning_analysis` shows why 12 poses are
used. Two poses (25 and 45 degrees) let kept vertices reach 0.16-0.40 PMX on
unseen 20/45/90-degree poses. With 12 poses the worst kept vertex is about
twice the limit: 0.10-0.12 PMX at 0.05.

### Update-rate LOD (`mmdhl_update_lod`, default 1)

| Mode | Pose update rate |
|---|---|
| 0 | every frame |
| 1 (default) | every frame, except a model the renderer has not drawn in any pass (view, reflection, flashlight or shadow) for more than 3 frames: 10 Hz |
| 2 | as 1, and by on-screen size (bounding radius over half the view height): 0.35 or more every frame, 0.15 to 0.35 at 30 Hz, below 0.15 at 20 Hz |

A skipped model keeps its last snapshot, so pose capture, the module's pose
and physics hand-off and any CPU skinning all follow the reduced rate. Models
take turns by entity index, so the work per frame stays even, and a rate
change never delays a model more than two intervals. "Drawn" is the native
renderer's own record (`GetDrawAge`), so a model whose shadow is on screen
keeps full rate.

The first release made mode 2 the default. It stepped visible motion: watching
a dance from where the model is about a third of the screen height, the hair
updated 30 times a second at 240 FPS (every 8th frame), and a row of distant
models moved at 20-30 Hz. Visible models now always update every frame; mode 2
remains for scenes where frame rate matters more than distant motion.

### Measured in game

Same save and camera as above (`measure-scene.py gpu3`, one session, p50 ms):

| Variant | Frame p50 / p95 | Upload per frame | Meaning |
|---|---|---|---|
| GPU skinning (default) | 12.0 / 15.8 and 11.6 / 15.8 | 1.7 MB | CPU remainder only; skinned buffers rewritten on 0.1-0.2 model-frames per frame |
| `mmdhl_gpu_skinning 0` | 15.6 / 18.5 | 21.2 MB | items 1-3 path |
| poses frozen | 3.5 / 5.6 | 0 | draws only |

The frame dropped 18.1 -> about 11.8 ms from the start of this work (55 -> 85
FPS). Worker deformation fell from
10.8 to 4.2 ms per frame and the main thread's upload from 3.7 to 1.0 ms. Each
frame draws about 1,260 skinned batches with about 10,000 bone matrix loads
(8 per batch on average). The CPU remainder is streamed in the 64-byte layout:
packing its normals on the render thread cost 1.3 ms for about 50,000
vertices, streaming costs 0.3 ms. Screenshots with GPU skinning on and off
differ by a mean of 0.4/255 (p99 5), with the flashlight on 1.4/255 (p99 17),
matching the difference between two captures with identical settings (0.9 and
1.6). Nothing fell back: VertexLitGeneric, DepthWrite (flashlight shadow depth)
and ShadowBuild_DX9 all take the hardware path.

Update-rate LOD, same save, frame p50 / p95 (`scripts/measure-lod.py`, measured
in mode 2 vs off; mode 1 matches "off" for the two views with models in sight
and "on" for facing away):

| View | LOD on | LOD off | Poses skipped per frame |
|---|---|---|---|
| saved camera (all models large on screen) | 10.6 / 13.5 | 10.3 / 13.3 | 0 of 10 |
| pulled back 650 units | 7.7 / 9.8 | 11.5 / 14.6 | 8.4 of 10 (about 20 Hz) |
| facing away | 1.9 / 4.6 | 5.8 / 7.7 | 9.6 of 10 (10 Hz) |

The view position comes from the `RenderScene` hook: `EyePos()` during
`PreRender` still reports the player's eye, not a CalcView camera.

Physics harness (flatgrass, 8 pinned characters, cpu_mt_v2, contacts). The
harness now pins `mmdhl_update_lod 0` like physics LOD (`--update-lod`
measures with it); with it on, its presentation-lag gate reports the LOD
interval (49 ms p50 at 20 Hz) instead of physics lag.

- Standing: frame p50 9.75 / p95 12.5 ms, all gates pass (items 1-3: 11.7 /
  14.9; v2.1: 14-15 / 17.6-18). With update-rate LOD on: 6.0 / 8.3.
- Clustered moving: frame p50 23.2 ms; the same 607-body character drops
  0.27 s while tangled, the single-lane physics limit described above.

### Fixes after the first release (2026-09-23)

- **Far away, the game crawled** (save `20-28-51`, ten models about 450 units
  away). The Draconic weapon base's aggressive culling
  (`cl_drc_perf_aggressiveculling_misc 1`) calls `SetNoDraw(true)` on distant
  entities every Think, and the network state clears it again before the next
  frame. Our `SetNoDraw` wrapper copied that local call into the networked
  `MMDHLNoDraw` flag, which counted as "hidden by the server", so the client
  destroyed each model's world and rebuilt it moments later (11 rebuilds in 6
  s; half the models never finished attaching). Only the server, or the client
  of a client-only entity, now records that flag. A local hide only skips
  drawing, and releases the world after lasting 5 s
  (`mmdhl.PresentationReleased`). A hidden entity still never gets a world. The
  far view: 10-16 ms -> 8.1 ms, no rebuilds in 20 s, all ten models attached
  within 6 s.
- **Hair unsmooth during a dance** (save `20-32-11`, `ai_disabled 1`, motion
  花月成双 through MMD VMD NPC Animation): the size-based update rate above.
  From a watching distance the hair now updates every frame (157-185
  updates/s, roughness 1.0 as with LOD off, against 30 updates/s and 2.0 in
  mode 2).
- **Hair judder during a dance at high frame rates** (same save; the user
  found it predates the update LOD and shows at any distance). Per-frame
  traces of a hair bone relative to its anchor bone showed the presented
  offset freezing for one or two frames and then jumping, once per 60 Hz
  step. `presentAsync` interpolated with the worker's own accumulator, so the
  display only advanced when the worker published; the 599-body rig's step
  job takes 3.5-5 ms, longer than a 2.8 ms frame at 350 FPS. At 180 FPS in
  the harness the pattern needed 40 solver iterations to appear (zero-motion
  frames followed by 2-4x jumps); at 10 iterations the hair speed still
  fluctuated with the tick phase, because the presented state was this frame's
  or the previous frame's job at random. The render thread now keeps a short
  history of solved states and interpolates at its own input clock minus one
  tick and an adaptive margin (docs/PHYSICS_V2_0.md, "Asynchronous worlds").
  Measured in game time (the clock the animation follows), the hair offset now
  moves at a constant speed within each tick, as linear interpolation should:
  speed roughness (mean frame-to-frame speed change over mean speed) 0.48 ->
  0.09 at 10 iterations and 0.49 -> 0.05 at 40, frames more than 50 % off the
  local speed 18 % -> 3 %, at 240-280 FPS in the harness. The display runs 29-33
  ms behind the skeleton (one tick plus the margin). Against wall time the
  game's frame pacing alternates (Source advances game time by the previous
  frame's duration), so wall-clock speed measurements of any animated part
  are noisy.
- **Hair still juddered with the animation player, never with a dragged
  ragdoll.** Tracing the bone pipeline showed why: the MMD VMD NPC player's
  client-side playback never writes this NPC's bones. The pose arrives from
  the server as networked bone manipulation, changes only at the server tick
  rate (128 Hz in the harness, 66 Hz in the user's game) and holds between
  packets, while a ragdoll's bones move every frame. A 66 Hz staircase into
  the 60 Hz physics makes the follower velocity alternate between one and two
  steps per tick (a 6 Hz beat that the hair chain shows as wobble), and the
  hair's live anchor steps as well. Single-frame pops of the head matrix (up
  to 4°, invisible on the body but 1-2 units at the hair tips) came from the
  addon's spine-pelvis correction on the server (`mmd_vmd_npc_disable_spine_pelvis_correction 1`
  removes them). The module now interpolates skeletons that update less often
  than the frames one update behind (`mmdhl_smooth_stepped_poses`, default
  on; `Instance::smoothPresentation`), so body, anchors and the physics input
  move every frame; skeletons that change every frame pass through unchanged.
  At 66 tick and 270-290 FPS: the head anchor held still in 76 % of frames
  before and 0 % after (measured update interval 14.9 ms, smoothing delay
  17.9 ms); hair-offset frames more than 50 % off the local speed 7.1 % ->
  2.5 %, tick-level second/first difference 0.33 -> 0.23.
- The animated skeleton of a Source sequence changes only at the server tick
  rate (128 Hz in the test sessions), so at 250 FPS some frames repeat the
  same pose. That predates these changes and affects GPU and CPU skinning alike.
- `game-start.ps1 -MenuFirst` starts Workshop sessions in the main menu, waits
  for mounting to finish and loads the map with `-hijack`: loading it from the
  command line raced the Workshop fetch and crashed.
