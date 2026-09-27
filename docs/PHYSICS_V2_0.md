# Physics v2.0: asynchronous secondary simulation

Version 2.0.0 adds the `cpu_mt_v2` secondary backend and a faster frame path
for every backend. The v1 solvers (`reference`, `cpu_mt`, `gpu_opencl`) stay
selectable. Shared attachment and surface corrections also apply to these
backends. Experimental stretch correction defaults off and only runs with
model-only collisions because it conflicts with Source surface recovery.

The problem v2.0 solves is structural, not arithmetic. In v1 the render thread
blocks inside `PrepareFrame` until the slowest character's serial chain of
physics steps has finished, then deforms, uploads and draws every character on
the same thread. Total physics CPU for eight characters is only about two
cores, but it is serialised onto the frame, so the frame time grows with the
character count while the CPU stays mostly idle.

## Selecting it

- Default backend: `cpu_mt_v2`, shown as **Claude CPU v2**, in the MMD settings
  UI or the `mmdhl_secondary_backend` convar. Saved explicit choices are retained.
- **Physics & Performance** in the importer exposes global tuning and a restore
  defaults button. See [Physics tuning](PHYSICS_TUNING.md) for controls and the
  focused stretch-correction validation.
- `mmdhl_secondary_sleep` (default 1): resting secondary bodies may sleep;
  `mmdhl_secondary_sleep_linear` (0.5 units/s), `mmdhl_secondary_sleep_angular`
  (0.35 rad/s) and `mmdhl_secondary_sleep_seconds` (1.5) tune the thresholds;
  `mmdhl_secondary_wake_drift` (0.02 units) is how far a resting follower may
  drift before it wakes its chain. Raise it above an animation addon's
  breathing amplitude if animated idle characters should be allowed to sleep.
- `mmdhl_secondary_wait_ms` (default 0): milliseconds a frame waits for the
  tick it just submitted before presenting the previous one. Zero presents the
  last finished tick without waiting.
- Harness: `python scripts/test-performance.py <label> --count 8 --backend
  cpu_mt_v2 --motion standing --contacts` (also `--wait <ms>`, `--layout
  clustered`, `--motion moving`, `--broadphase auto|dbvt|dbvt-fast|sap`,
  `--midphase 0` for the A/B of the exact narrowphase gate). Reports now
  include `workerMsPerTick`: worker milliseconds per simulated 60 Hz tick,
  summed over the characters (wall time of the step, of the whole tick job,
  of the guards, and thread CPU time), which is the number that decides
  whether a scene keeps up. Pinned models resolve by source hash, so a
  re-imported model no longer fails the eight-character gate.
- Broadphase default `auto`: sweep-and-prune for `cpu_mt_v2` worlds, the
  order-preserving `dbvt-fast` tree for `reference`, `cpu_mt` and
  `gpu_opencl`. `mmdhl_secondary_broadphase`-style explicit names still apply
  to every backend.

## Design

### Asynchronous worlds

`Secondary::submitAsync` receives an `Input` from `submitPresentationPose`
every frame: timestamp, elapsed time, the driven MMD pose, morph weights, a
shared snapshot of the manual bone poses, the character bounds for the scene
mirror, and reset flags. Inputs queue on the world; at most one background job
per world runs at a time (`Pool::enqueueBackground`, jobs.cpp). The worker loop
prefers frame-critical foreground work, so deform chunks are never queued
behind physics. A backlog of eight inputs coalesces: the dropped sample's
elapsed time is added to the next input, so the simulation clock never loses
time.

`tickAsync` runs the same code as the synchronous `cpu_mt` step: scene sync,
impulse morphs, sleep policy, then `advance()` with the 60 Hz accumulator
(`MaxStepsPerInput` 4, `MaxAsyncJobMs` 30). The job runs inside an
`InlinePhysicsScope`, so the nested `parallelForSecondary` lanes collapse to
one and the eight worlds overlap instead of fighting for the pool.

Every tick publishes a `Solved` state: body transforms (current and previous),
root transforms, clocks, timings, sleeping count and scene statistics.
`presentAsync` (render thread, inside `stepSource`) keeps the last five
distinct published states of the current reset epoch and interpolates between
the pair that brackets the display time, relative to each chain's nearest
joint-connected kinematic attachment, just as the synchronous path does. It
writes the `display` transforms that `feedback()` reads. The display time is
the render-side input clock (the presented state's input clock plus the game
time since that input was submitted) minus one tick minus a margin. The margin
follows the largest worker lag of the last 32 frames (a hitch frame raises it
only until it leaves the window; a step job that outlasts frames recurs every
tick), floors at half a tick, is capped at two ticks and moves gradually
(at most half the frame's game time up, 5 % down). Until 2026-09-23 the interpolation
factor was the worker's own accumulator: while a step job ran longer than a
frame (a 599-body rig steps in about 5 ms; at 350 FPS a frame is 2.8 ms) the
display froze for the frames the job spanned and then jumped when it
published, a 60 Hz judder of hair against a smooth body. Live attachment
transforms compensate skeleton motion without applying pelvis motion to
unrelated chains. World-anchored and free components retain simulation-space
positions. `presentationDelayMs` in the diagnostics reports one tick plus the
margin; `mmdhl_async_tests` checks continuity with a 6 ms artificial step
delay against 240 Hz frames.

State ownership rule: while a world is asynchronous, the clock and timing
members (`accumulator`, `inputTime`, `simulationTime`, `ticks`, `resets`,
`physicsMs`, ...) belong to the tick job. The render thread reads them through
`Secondary::frameStats()`, which returns the presented tick's values. The first
in-game run of the async backend violated this rule and corrupted seven of the
eight worlds (frozen input clock, one step per frame); the fix is the rule.

### Stepped skeletons

Bone poses that reach the client over the network (an NPC posed by an
animation addon through bone manipulation) change only at the server tick
rate and hold between packets. `Instance::smoothPresentation` keeps the last
four distinct submitted poses with their timestamps, measures the update and
frame intervals, and while the skeleton updates less often than the frames
(hysteresis at 1.3-1.6 frames, intervals up to 60 ms) it drives the instance
with the pose interpolated 1.2 update intervals behind. The delay moves at
most a quarter frame per frame, so the displayed time never runs backwards;
a root jump over 128 units or a clock rewind clears the history. Ragdolls and
players, whose bones change every frame, pass through unchanged.
`mmdhl_smooth_stepped_poses` (default on); diagnostics report
`presentationSmoothingMs` and `presentationUpdateIntervalMs`.

### Tick-local pose evaluation

`evaluatePose()` (model.cpp) is the bone/inherit/IK evaluation as a free
function over explicit arrays. The tick job evaluates the driven pose into its
own `PoseArrays` from the input's manual poses, while the render thread keeps
evaluating into the instance for deformation.

### Sleep and wake

nanoem sets Bullet's sleeping thresholds to zero, so bodies never rest, and
it marks the kinematic follower bodies `DISABLE_DEACTIVATION`. The second
point matters more than the first: Bullet's island builder wakes every body
that touches an *active* kinematic object on every step, so a skirt resting
against a leg collider could never sleep whatever the thresholds. v2 worlds
therefore let followers sleep as well (tiny thresholds, `ACTIVE_TAG` instead
of `DISABLE_DEACTIVATION`), and `follow()` wakes a follower and every body
linked to it through the joint graph as soon as its target has drifted more
than 0.02 units (a sleeping kinematic body keeps its transform, so it must
wake before it moves). Dynamic bodies use linear 0.5 units/s, angular 0.35
rad/s and 1.5 s of deactivation time; contact jitter of resting chains stays
below that, a decaying swing crosses it only when its amplitude is already
tiny. Convars: `mmdhl_secondary_sleep`, `mmdhl_secondary_sleep_linear`,
`mmdhl_secondary_sleep_angular`, `mmdhl_secondary_sleep_seconds`. The
sleep policy can be disabled independently. Attachment presentation and the
optional stretch correction mean this is not an exact nanoem trajectory mode.

### Kinematic pair filter

Pairs whose two sides are both kinematic followers never produce contacts;
the broadphase filter rejects them before the narrowphase.

### Deformation

`Instance::publish` (model.cpp) skins into the verified Source vertex layout
directly: `DrawVertex` is the 64-byte common vertex (position, normal, colour,
UV, four-float tangent, padding). A per-model `SkinLayout` groups vertices by
skinning type and bone set, padded to eight-lane blocks, in structure-of-arrays
form. Each publish builds one affine matrix per bone that folds the MMD-to-
Source basis, the model scale, the inch conversion and the placement, then an
AVX2 kernel skins position, normal and tangent eight vertices at a time with
the group's fixed bone set. SDEF/QDEF vertices use the scalar reference path,
as does every vertex on a processor without AVX2 and FMA3, or whose operating
system does not save the AVX register state (OSXSAVE with XCR0 bits 1 and 2). Morph deltas are sparse: only the vertices a morph
touched are restored and re-applied. Static attributes (UVs, edge, tangent
sign) are written once per buffer and again only when a UV morph changes them;
materials are copied only while a material morph is active. The timing test
compares the kernel with the scalar definitions for every skinning type.

Idle reuse: a publish first builds the palette and compares it with the
previous one. If no bone moved and no morph, static attribute or soft body
changed, the instance keeps its current snapshot: no skinning, no upload
(the sequence did not change) and no engine shadow re-render (Lua marks the
shadow dirty only when the bounds sequence changed). A character whose
physics has gone to sleep therefore costs only the pose evaluation and the
draw calls.

### Rendering

- `findMaterial` caches engine materials by name with a held reference; the
  colour pass and shadow pass no longer do two string lookups per draw.
- Shadow pass: opaque parts share one `ShadowBuild` material per cull mode
  (native_render.lua), and `shadowDraw` groups a character's parts by material
  and alpha, binds once per group and merges adjacent index ranges into one
  draw. Alpha-tested parts keep their per-material cutout.
- `drawInstanceNativeParts` draws any list of parts with a single bind; the
  colour pass still binds per material because every part has its own
  textures, but adjacent ranges merge.
- The per-part colour/depth loop runs in the module: Lua registers the
  engine material names once per asset (`SetInstanceMaterials`) and issues
  one `DrawInstance` call per entity and pass, passing material overrides
  only when the entity has any. Default parts belong to the opaque pass, so
  the translucent pass is skipped entirely for entities without overrides;
  override materials go to the pass their flags select, exactly as the Lua
  loop did.
- Vertex upload: each publish records the sequence at which every palette
  bone last moved and at which every vertex last changed (morphs, statics,
  soft bodies). The renderer keeps, per triple-buffer slot and per
  8192-vertex span, the sequence it last uploaded, and knows which bones
  influence each span. Only dirty spans are locked (one partial lock per run
  of dirty spans) and streamed by the worker pool with 64-byte streaming
  stores, each worker fencing its own stores before the barrier.

### Telemetry

`PrepareFrame` reports `asyncWorlds`, `asyncLagMs`, `sleepingBodies`,
`changedBones` (palette rows that moved this frame, summed over instances)
and `fullChanges` (instances whose every vertex changed); `RenderFrameStats`
reports `drawBinds`, `drawCalls`, `spansTotal` and `spansDirty` per frame
beside the existing pass timings. Detailed diagnostics list `speed`, `spin`
and the Bullet `activation` state per body. The performance HUD (`mmdhl_debug_overlay 1`) shows the
async line and a native render line. The harness records every profile key as
a percentile histogram and gates `asyncKeepsUp` (every 60 Hz tick of the
measured interval exists and presentation lag p95 is at most two frames).

## v2.1: cheaper steps (2026-09-23)

The v2.0 frame path left every tick's cost where nanoem put it. Profiling one
tick on a single lane (what an asynchronous job actually costs; the earlier
30-lane profiles hid the narrowphase behind parallel dispatch) showed, for
March 7th (287 bodies, 329 joints, 1604 rows): narrowphase dispatch 35 %,
DBVT update 23 %, solver 37 %. Four changes, all validated by tests:

- **Exact mid-phase gate** (`native/ordered_dispatcher.hpp`, multicore
  dispatchers only). Bullet keeps a broadphase pair alive while fat volumes
  overlap and runs the narrowphase for every live pair every step; a dense rig
  carries 1200-1700 live pairs for 130-360 contacts. Before dispatching a
  pair the gate compares the bodies' tight AABBs padded by the pair
  manifold's contact breaking threshold, and for pairs that Bullet would send
  through GJK or box SAT (at least one box) it also bounds the gap by the
  distance between conservative swept capsules (a box lies inside the
  capsule around its longest axis whose radius covers the other two half
  extents, margins included). No algorithm adds a point whose surface gap
  exceeds the threshold, so a gated pair only refreshes the points it already
  has, exactly what the skipped algorithm would have done: the step is
  bit-identical (`tests/midphase_tests.cpp` compares every transform of every
  step with the gate on and off, on the synthetic fixture and on the cached
  March 7th rig, model-only and with scene contacts; `mmdhl_profile` prints a
  trajectory digest for the same comparison on any asset). The gate removes
  32-44 % of live pairs on the tested rigs.
- **Sweep-and-prune for v2 worlds** (`auto` broadphase). The incremental
  endpoint sort costs far less than DBVT leaf updates when a few hundred
  bodies all move every tick (0.10 ms against 0.23 ms per tick on March 7th).
  SAP changes the pair order, hence the solver row order, so the replay-exact
  backends keep `dbvt-fast`; v2 already relaxed that order (sleep, pair
  filter). A fatter DBVT margin for v2 worlds is available to native callers
  through `setSecondaryDbvtMargin` (`mmdhl_profile --margin`) for experiments:
  a 0.25 margin was 4 % faster than 0.05, SAP 13 % faster.
- **Empty manifolds skip the compute solver's dependency scan**
  (`native/compute_solver.cpp`): a manifold without points creates no rows and
  therefore no kinematic write-back dependency.
- **Tick bookkeeping**: `readSolved` reads Bullet transforms directly instead
  of round-tripping every body through a 16-float OpenGL matrix.
- **Stretch correction against the contacts it can reach.** With
  `mmdhl_secondary_stretch 1` (the setting found enabled on the test machine)
  and world/prop collisions, every corrected body used to be swept (convex
  cast) against every mirrored obstacle, up to three times per correction,
  four passes per step, plus one broadphase AABB update per correction; in
  the clustered scene (about 130 mirrored hulls per character) that was
  2-32 ms per tick job and the simulation could not keep up. Now:
  - once per step the pass reads the broadphase pair cache: which obstacles
    each body is paired with, and the contact normals of touching pairs;
  - a touching pair clamps the correction against its contact planes (what
    a sweep from a touching start returns, without the GJK query); a paired
    obstacle without points is swept only once the motion exceeds its
    contact breaking threshold; every obstacle is swept only when the motion
    exceeds the broadphase padding;
  - corrections no longer refresh the broadphase: nothing reads body AABBs
    before the next step's `updateAabbs` (the surface guard's refresh was
    dropped for the same reason);
  - with world/prop collisions the pass runs two alternating sweeps instead
    of four (model-only worlds keep four). This is a deliberate trade: the
    contact solver already bounds the chains there, and a crushed cluster
    violates most joints at once.
  The ground-recovery regressions (`mmdhl_stretch_ground_tests` on both
  chain fixtures), the stretch and projection tests all pass. Standalone
  cost on March 7th with scene contacts: 0.29 ms per step for 163
  corrections.

Standalone, one character, single lane, 300 steps after 60 warm-up steps,
mode 0 unless noted (`mmdhl_profile --workers 1`):

| Rig | Before (cpu_mt, dbvt-fast, gate off) step / tick | After (cpu_mt_v2, auto) step / tick | Change |
|---|---|---|---|
| March 7th, 287 bodies | 1.01 / 1.09 ms | 0.72 / 0.74 ms | -29 % / -31 % |
| March 7th, scene contacts (mode 2) | 1.07 / 1.17 ms | 0.77 / 0.80 ms | -28 % / -31 % |
| Xin, 686 bodies, 946 joints | 2.05 / 2.22 ms | 1.72 / 1.77 ms | -16 % / -20 % |

Per phase on March 7th after the changes: solver 0.36 ms (49 %), narrowphase
0.21 ms (29 %), broadphase update 0.10 ms (14 %). The solver is untouched: 10
iterations over 1604 joint rows plus contacts is the fidelity contract.

In game (eight pinned characters, gm_flatgrass, 2560x1440, 30 workers,
collision mode 2 with contact props, `mmdhl_secondary_stretch 1`, 10-second
window; the session was started with `-NoWorkshop` because the Steam
workshop fetch hung at "Fetching subscriptions", so unlike the v2.0 table no
workshop addons were loaded). "Old" is this build with `--broadphase
dbvt-fast --midphase 0`, which isolates the broadphase and gate; the
stretch-pass changes are in both:

| Scene | Config | Frame p50 / p95 | Worker ms per tick (step / whole job / guards) | Lag p95 | Dropped | Keeps up |
|---|---|---|---|---|---|---|
| Clustered, moving | before the stretch fix | 53.2 / 78.3 | 30.7 / 81.2 / 49.3 | | 13.6 s | no |
| Clustered, moving | old | 27.8 / 40.6 | 36.9 / 55.4 / 17.3 | 347 ms | 0.52 s | no |
| Clustered, moving | new | 29.8 / 37.2 | 33.2 / 48.6 / 14.3 | 37.5 ms | 0 | **yes** |
| Clustered, moving, stretch off | old / new | 33.9 / 57.8 and 34.4 / 55.7 | 34.8 / 37.2 and 31.8 / 34.2 | | 0 / 0 | yes / yes |
| Standing | old | 14.9 / 17.7 | 24.6 / 27.7 / 2.1 | 17.8 ms | 0 | yes |
| Standing | new | 15.0 / 18.0 | 24.5 / 27.6 / 2.1 | 17.9 ms | 0 | yes |

Reading the table:

- The crowded scene is where the physics work decides the experience: the
  whole tick job fell from 81 to 49 worker ms per tick (-40 %), and the
  eight worlds now keep real time instead of dropping 13.6 s of simulation
  in a 10 s window. The broadphase and gate alone account for -10 % of the
  step and -12 % of the job there.
- The standing scene is unchanged within noise. Resting chains barely move,
  so the DBVT's containment early-out already made its update cheap and few
  pairs cross the gate; in-game steps also cost 2-4x the standalone single-
  lane numbers because the ticks share the machine with 30 deform/upload
  lanes. The standing frame is render-bound (native render 5 ms, deform
  work 9-10 ms across workers, prepare wall 2 ms): the 16.7 ms p95 gate is
  still open and physics is no longer the lever for it.
- Thread CPU time per tick is sampled with `GetThreadTimes`, whose 15.6 ms
  quantum makes it noisy per run; use the wall columns for comparisons.

Tooling fixed on the way, all of which had hidden the numbers above:

- `mmdhl_profile` could not measure an asynchronous world (it read tick-thread
  members from the frame and reported zero steps and dropped time); it now
  waits for the queued tick, reports `frameStats()`, accumulates Bullet's
  phase tree across steps (Bullet resets it every `stepSimulation`, and the
  tree must be read inside the runtime DLL because test executables link
  their own LinearMath copy), prints the trajectory digest and the gate
  counters, and symbolizes native crashes.
- `mmdhl_profile --workers N` crashed on every multicore backend: Bullet's
  thread counter is post-incremented, so `btResetThreadIndexCounter` hands
  index 1 to the next claimant; `setWorkerCount` reset the counter before the
  owning thread had ever claimed its index, the later `btSetTaskScheduler`
  silently refused (it demands index 0) and `btCollisionDispatcherMt`
  dereferenced the missing scheduler. `jobs.cpp` claims the owner index before
  building a pool and before every reset, and `initializeBulletScheduler`
  throws instead of continuing without a scheduler.
- `mmdhl_replay` no longer reports strict nanoem agreement for any backend:
  the follower timing change of the actor work (solve from the previous
  follower pose, publish followers and dynamics at the same boundary) removes
  the one-tick lead by design, and the replay measures exactly that
  difference from frame 1 (0.19 PMX units at its carry speed). The replay's
  gate needs a decision, not this change; see PHYSICS_TUNING.md.

## Fidelity

With sleep disabled, the asynchronous world reproduces the synchronous `cpu_mt`
trajectory step for step (tests/async_tests.cpp checks the maximum body
position difference over 180 frames). Sleep, the pair filter and the shared
opaque shadow material are the v2-only differences; `reference` and `cpu_mt`
keep their bit-identical replay.

## Results

Eight pinned full-detail characters (tests/v2-assets.json), standing motion,
collision mode 2 with contact props, gm_flatgrass, 2560x1440, 14 workers.
Frame percentiles are foreground frame times in milliseconds.

| Build | p50 | p95 | prepare wall | native render | upload |
|---|---|---|---|---|---|
| v1 `cpu_mt` | 29.8 | 36.8 | 10.0 | 6.3 | 3.2 |
| v2 async only (state race still present) | 26.5 | 32.7 | 6.3 | 6.4 | 3.2 |
| + tick-state ownership fix + SIMD skinning | 20.3 | 26.0 | 1.6 | 6.6 | 3.5 |
| + shadow grouping, material cache, parallel fill | 16.0 | 22.2 | 1.6 | 4.1 | 2.9 |
| + native draw loop, follower sleep, partial uploads (final) | 14.3 | 19.7 | 1.5 | 4.1 | 2.8 |
| engine floor (native work suspended) | 2.7 | 4.9 | | | |

PHYSICS_V2_0_RESULTS.md carries the moving-scene tables, the frame budget
breakdown, the gate status and the known issues.

## Verification

- `scripts/build.ps1` builds every target and runs the native, timing
  (`dbvt`, `dbvt-fast`), compute, broadphase and asynchronous suites.
- `build/bin/Release/mmdhl_async_tests.exe tests/fixtures/native-chain.pmx`
  (24 checks: equivalence, cadence, lag, wait budget, resets, collision mode
  changes, backlog, worker resize, teardown, single-thread inline, sleep/wake).
- `build/bin/Release/mmdhl_timing.exe tests/fixtures/native-cloth21.pmx dbvt`
  includes the SIMD-versus-scalar skinning comparison and the streamed vertex
  layout check.
- In game: the harness command above for `cpu_mt` and `cpu_mt_v2`.

## Known limits

- The worker normally runs one frame behind the input; `mmdhl_secondary_wait_ms`
  trades frame time for zero lag. The displayed physics state runs one tick
  plus the adaptive margin (at least half a tick) behind the animation, so a
  hair swing answers a head turn about 25 ms later than the skeleton moves.
- Soft-body models fall back to the reference world (as in v1) and are not
  asynchronous.
- The GPU backend is unchanged and still experimental; its level-serial
  kernel cannot be tuned into a win, and the asynchronous CPU path makes it
  unnecessary at this character count.
