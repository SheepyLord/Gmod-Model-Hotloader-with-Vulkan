# Character models with Source's multicore rendering (2026-09-26)

Branch `queued-rendering` of the test repository (on top of `vulkan-compute`;
the commits here do not depend on the Vulkan work).

## What changed

The addon used to switch Source to single-threaded rendering
(`mat_queue_mode 0`) whenever a character model existed.

- **Now:** character models draw with the game's own setting. With multicore
  rendering (`mat_queue_mode 2`, `gmod_mcore_test 1`), each native draw runs on
  Source's render thread.
- **Result:** frames are 1.75-2x faster in the scenes below, with the same draw
  work and identical images.

Still switched to single-threaded rendering:

- **While in use:** the library and bodygroup previews, the editor previews and
  the export icon. These set material parameters from Lua around each native
  draw.
- **Whenever present:** legacy `mmdhl_ragdoll` entities (fallback spawn path),
  for the same reason.
- **Opt-in:** `mmdhl_force_immediate_rendering 1` brings back the old behaviour
  for every character model.

## Why mode 0 was forced

In queued mode the main thread's render context (`CMatQueuedRenderContext`)
records calls for the render thread. Vertex data written through it is copied
into a per-frame arena of bounded size. The addon uploads a lot of vertex data:
whole models when their buffers are created, and every deformed vertex under
CPU skinning. Detailed multi-model scenes exhausted the arena.

## Design (`native/renderer.cpp`)

1. **Prepare on the main thread.** `drawPrepared` builds a `DrawJob` holding
   only shared or copied data:
   - the model and the immutable snapshot (`shared_ptr`);
   - the GPU-skinning rest data (`GpuRest`: shared, written and uploaded under
     its mutex);
   - the first-person triangle mask (shared, built once);
   - copied instance state: centre, scale, flags, and the material binds with
     their parts.

   Visibility filtering, material lookup and the hardware-skinning eligibility
   checks happen here too. So does returning an instance to CPU skinning.
   `DrawInstance` submits one job per entity and pass.
2. **Submit.** `submit()` classifies the calling thread's render context by
   vtable (pinned ABI guards):
   - **hardware context** (mode 0, or the render thread): run the job now;
   - **queued context:** append it to Source's call list for the render thread,
     in order with the calls recorded before it (lighting, flashlight state,
     clip planes, render targets);
   - **anything else:** throw.
3. **Execute where the hardware context is current.** The drawing code is the
   same as before, reading the job instead of the instance: static buffers are
   created, locked, filled (on the worker pool) and drawn directly. The buffer
   caches, pruning (itself queued) and the draw counters belong to that thread
   and are guarded by `renderMutex`.
4. **Report back.**
   - Errors of render-thread draws surface through the next `DrawInstance`
     call.
   - Hardware-path failures return the instance to CPU skinning at its next
     draw.
   - Draw counters are published by a marker call queued at each
     `PrepareFrame`. In queued mode `RenderFrameStats` therefore reports the
     last frame the render thread finished, plus `nativePrepareMs` (main
     thread) and `queuedDraws`.
5. **Lifetime.**
   - A queued call deletes itself after running.
   - `drainRenderQueue()` leaves no call of this module queued. It runs before
     the worker pool is rebuilt (`SetWorkers`) and at module close, both before
     and after the final prune. `IMaterialSystem::Lock` waits for the render
     thread to finish the calls handed to it (the previous frame) and makes the
     hardware context the caller's, but it does not run the frame the main
     thread is recording. So, while locked, the drain unlinks this module's
     calls from that call list (recognized by their vtable; Source's own calls
     stay queued) and runs them in order on the main thread. Draws are dropped;
     the prune and the counter marker run.
   - Before 2.1.0-native.9 the drain relied on `Lock` alone. At "Exit to main
     menu" the final prune and the frame's draws stayed queued. Source ran them
     when disconnecting took it out of multicore mode (`materialsystem.dll`
     `0x2FFDD` on the default branch), after the client Lua state had closed and
     unloaded the module, and the game crashed.
   - Should a call still be outstanding after the close-time drain,
     `closeRenderQueue()` pins the module until the process exits and the call
     skips its work.
   - A recycled snapshot is fenced against a render-thread reader that just
     released it.

## GMod ABI facts (pinned `materialsystem.dll` b933ff.. and RTX daee78..)

These were established from RTTI and disassembly; see `scripts/compatibility_profiles.py`.

- **Context vtables.** `CMatQueuedRenderContext` is at RVA `0xB6080` and
  `CMatRenderContext` (hardware) at `0xBB020`, both builds. The old
  `queuedContext` guard (`0xF11C0`) is not a vtable in either build, so the old
  "requires mat_queue_mode 0" check could never fire. It is now `0xB6080`, and a
  new `hardwareContext` guard is `0xBB020`.
- **`GetCallQueue`.** `IMatRenderContext::GetCallQueue` (slot 145) returns
  `this+0x2B0` on the queued context and null on the hardware one. What it
  returns is **not** tier1's virtual `ICallQueue`: it is the context's concrete
  call list, with this layout:

  | Offset | Field |
  |---|---|
  | `+0x00` | `head` |
  | `+0x08` | `tail` |
  | `+0x10` | bump-allocator next pointer |
  | `+0x18` | 16 MB element buffer |

  Each element is `{next, CFunctor*}`. `studiorender.dll` appends to it inline
  the same way (e.g. at `0x60200`); `appendCall` mirrors that and checks the
  layout first.
- **Execution.** The render thread runs the list at `materialsystem.dll`
  `0x31070`: `(*functor)()` through vtable slot 3, then it resets the list. It
  never destroys or releases the functors.
- **Functor layout.** The vtables are AddRef, Release, deleting destructor,
  `operator()`, as in the SDK.

## Results

Setup:

- **Machine and settings:** stock D3D9 at 2560x1440, the user's settings
  (multicore on, `fps_max 0`), Ryzen 9 9950X3D, RTX 5090.
- **Scene:** gm_flatgrass, the 8 pinned characters standing, `cpu_mt_v2`,
  collision mode 0.
- **Citizens:** 48 animated citizens behind them (`--citizens 48`, the
  install's replacement models).
- **Procedure:** new vs old alternated in one session (`--force-immediate 0|1`).

| Scene | Mode | Frame p50 / p95 / p99 ms | FPS | Game CPU cores | GPU 3D |
|---|---|---|---|---|---|
| + 48 citizens, 10 it | multicore (new) | 9.11 / 10.86 / 12.31 | 110 | 4.48 | 40 % |
| + 48 citizens, 10 it | forced mode 0 (old) | 16.18 / 17.60 / 18.98 | 62 | 2.94 | 26 % |
| + 48 citizens, 10 it (repeat) | new / old | 9.46 / 16.36 p50 | 106 / 61 | 4.72 / 3.13 | 40 / 27 % |
| + 48 citizens, 100 it | new / old | 9.32 / 16.60 p50 | 107 / 60 | 6.03 / 4.57 | 39 / 26 % |
| characters only, 10 it | new / old | 3.56 / 7.00 p50 | 281 / 143 | 5.53 / 3.60 | 50 / 30 % |
| + 48 citizens, CPU skinning | new | 11.46 / 15.47 / 20.49 | 87 | 6.26 | 31 % |

- **Same work:** 1,212 draw calls, about 2.5 ms of native draw work (now on the
  render thread) and the same upload and shadow time in both modes. Memory is
  unchanged.
- **Physics:** keeps real time in every run; no render errors.
- **Images:** screenshots with the flashlight on are identical except for
  animation phase.
- **Robustness:**
  - `SetWorkers` changes while rendering and a `changelevel` with models
    present both survive.
  - Switching `mat_queue_mode` 0/2 mid-session works.
  - About 440k queued calls were executed with none left outstanding.

The extra CPU cores are Source's render thread, which now carries the frame's
D3D9 work in parallel with the main thread.

## Known costs and gaps

- **Scene capture with many animated entities (render-mode independent; fixed
  on branch `scene-capture`, see [SCENE_CAPTURE.md](SCENE_CAPTURE.md)).** The
  secondary-physics scene capture used to read every VPhysics object and
  classify every entity each server tick (the `MMDHL.SecondaryScene` Tick hook).
  - **Cost:** with 48 animated citizens in collision mode 2 it took 5.25 ms per
    server tick. At the harness's 128 ticks that is 670 ms per second, on the
    main thread in single-player.
  - **Per character:** each character's diagnostics showed the same shared
    capture time (about 4 ms); it was not an extra per-character cost.
  - **Effect:** frames dropped to 39 ms (new) or 71 ms (old), so the table above
    uses collision mode 0. On `scene-capture` the same scene runs at about
    10 ms with contacts on.
- **Not exercised in queued mode by this test:** the first-person view (clip
  plane plus triangle mask), the RTX Remix fixed-function path and the
  `mmdhl_vertex_cache 0` stream path.

## Reproduce

```
python scripts/test-performance.py <label> --count 8 --backend cpu_mt_v2 --motion standing --iterations 10 --mode 0 --citizens 48 --force-immediate 0|1
mmdhl.Decode(mmdhl.native.RenderStats()).renderQueue       -- context classification, queued call totals (client Lua)
```
