# Secondary-physics scene capture on busy maps (2026-09-26)

Branch `scene-capture` of the test repository (on top of `queued-rendering`).

## What changed

Every server tick the `MMDHL.SecondaryScene` hook captures Source's VPhysics
objects for the characters' secondary physics (hair and skirts touching the map
and props). It used to read and classify everything on the map.

- **Now:** it captures only what a consumer can reach, and classifies only the
  entities near those consumers.
- **Result:** with 48 animated citizens the hook fell from 5.56 to about
  0.65 ms per tick, and frames from 39.4 to about 10 ms (p50). That is within
  a millisecond of the same scene with contacts off.

## Where the time went

Scene: gm_flatgrass, 8 characters with contact plates (collision mode 2) and 48
animated citizens, 128 ticks.

| Part | Cost per tick | Why |
|---|---|---|
| Native capture | 4.35 ms | Read all 1016 VPhysics objects: 846 citizen `phys_bone_follower`s, the world, the characters' bodies and the plates. Each object also got an ABI lookup, and the list was rebuilt as a `std::set`. |
| Lua | 1.21 ms | Walked all 2177 entities (1180 `logic_collision_pair`s, 846 bone followers, ...) twice: once to classify living actors, once in `mmdhl.Entities()`. |

Single-player runs the server's tick hooks on the main thread, so the cost repeats
128 times a second. The frame rate collapses as that approaches the frame budget:
at 39 ms per frame each frame runs five ticks.

Previously each character's diagnostics showed "about 4 ms of scene capture".
That was this one shared capture reported by every world, not a per-character
cost. The per-world sync was about 0.02 ms.

## Design

- **Interest regions (`native/scene.cpp`).** Consumers register the box they
  need, and the capture keeps only objects that can reach one. A region lapses
  after 2 s without a refresh; until any consumer registers, everything is
  captured. The consumers:
  - **Each secondary world** registers its character's collision box (+48, the
    same margin its own cull uses) on every sync. The box leads a moving
    character by twice its last displacement, capped at 512 units per axis. The
    capture reads the box a sync or two late.
  - **Each remote subscriber** registers the sphere it is sent (`ExportSecondaryScene`
    now takes the subscriber's entity index). Since the second Codex review
    (2026-09-26) the server caps that sphere at the subscriber's farthest
    character (distance + bounding radius + 512, clamped to 512-16384 units, the
    client's own formula) and exports nothing when there is none or the
    subscriber's own collision setting is off. A NaN or infinite radius
    unsubscribes. Shape chunks: one pending request per subscriber, answered at
    most every 20 ms. Clients drop frames over 32 MB of JSON or 20000 objects
    (`tests/test_scene_sharing.py`).
- **Cull (`native/bridge.cpp`).** An object's bounds are grown by a quarter
  second of its own velocity and tested against the regions. That is a superset
  of each world's own cull (bounds + 48, velocity × 0.1). Every object stays
  tracked, so its geometry is still read only once.
- **Ownership across realms.** In single player the client's secondary worlds
  mirror the frames the server module captures, so they can hold them after
  that module is unloaded; quitting unloads it before the client closes. A
  `std::make_shared` in the server module left the `shared_ptr` control blocks'
  code there, and dropping the last reference then crashed the game on quit
  (2.1.0-native.9 and earlier). Since 2.1.0-native.10 frames and geometry come
  from the runtime (`newSceneFrame`, `newSceneGeometry` in `native/scene.cpp`),
  which outlives both realm modules.
- **Cheaper per-object work.** The two ABI guards (object vtable, position
  function) are resolved to addresses once per environment. Objects are walked
  in list order, and a generation sweep replaces the `std::set`.
- **Classification (`addon/lua/mmdhl/secondary_collision.lua`).** Living actors
  are excluded before the native bridge reads them.
  - The hook now classifies only the entities that overlap a region (+64,
    `ents.FindInBox`), plus every player, since players can outrun the margin
    (noclip, vehicles, falls).
  - Far actors are no longer counted in `excludedLivingEntities`. Their objects
    are outside every region, so they are not captured at all.
- **Character list (`addon/lua/mmdhl/carrier.lua`).** `mmdhl.Entities()` keeps
  its candidates incrementally instead of scanning every entity each tick or
  frame.
  - `OnEntityCreated` adds entities by class; other new entities are checked
    once, when their model is set. `EntityRemoved` drops them.
  - A full scan runs every 5 s, and immediately after a client full update.
  - Editor previews register through `mmdhl.NoteCarrier`.

## Results

Same session, stock D3D9 at 2560x1440, `cpu_mt_v2`, 10 iterations, multicore
rendering. The before runs are from the `queued-rendering` build.

| Scene | Build | Frame p50 / p95 / p99 ms | Scene hook ms/tick | Capture ms | Objects captured |
|---|---|---|---|---|---|
| 8 characters + 48 citizens, contacts, standing | before | 39.38 / 46.92 / 52.39 | 5.25 | 4.17 | 1016 of 1016 |
| 8 characters + 48 citizens, contacts, standing | after | 9.34-10.52 p50 (5 runs) | 0.57-0.75 | 0.15-0.34 | 153 of 1018 |
| 8 characters + 48 citizens, contacts off (reference) | before | 9.11-9.46 p50 | 0 | 0 | - |
| 8 characters + 48 citizens, contacts, moving | after | 11.01-11.95 p50 | 0.61-0.66 | 0.19-0.28 | 153 of 1018 |
| 8 characters, contacts, standing | after | 4.31-5.97 p50 (7 runs) | 0.34-0.42 | 0.07 | 153 of 172 |

- **Contacts:** the contact gate passed in every after run but one (the plate
  flake below). World and object contacts were present, and the one-way plates
  kept velocity error 0. Each world holds
  the same external objects as before (2 per character, 11-12 for the one next
  to the other plates).
- **Targeted checks:**
  - Living-actor exclusion with the QoL test's layout: a player about 160 units
    behind the model and an NPC in between give `excludedLivingEntities` 2.
  - The compatibility collision checks pass on the client: 77 one-way contacts,
    the prop does not move, and mode 0 / 1 / 2 keep 0 / 1 / 1 mirrors.
  - A remote-subscriber sphere around a prop 1500 units away adds it to the
    capture on the next tick and drops it 2 s after the last refresh.

## Known gaps

- **Contact plate flake.** The plate check (`maxPropVelocityError`) failed once
  in 17 contact runs of this branch (1.14). That run also had the only long frame
  hitch (103 ms).
  - The other 16 passed with 0, including the same citizens-then-plain sequence.
    Four of them ran with a `PhysicsCollide` probe on the plates, which recorded
    no Source collisions.
  - The check also failed once (3.12) on `vulkan-compute`, before any of this
    work, so it predates this branch.
- **No multiplayer run.** The dedicated-server + two-client harness
  (`scripts/test-multiplayer.py contacts`) needs prepared game roots under
  `validation/multiplayer`, which this repository does not have. The remote path
  was exercised in-process instead (see above).
- **Stale tests.** Parts of `test-qol.py` and `test-compatibility.py collisions`
  predate the current UI and client-simulated physics. Examples:
  `p.secondaryCollision`, the server-side contact diagnostics, and the AdvMat
  preamble.

## Reproduce

```
python scripts/test-performance.py <label> --count 8 --backend cpu_mt_v2 --contacts --motion standing --iterations 10 --force-immediate 0 --citizens 48
mmdhl.sceneDiagnostics      -- server Lua: objects, sourceObjects, outsideInterest, interestRegions, captureMs
mmdhl.native.SceneInterest() -- server Lua: registered regions, 6 numbers each (minimum, maximum)
```
