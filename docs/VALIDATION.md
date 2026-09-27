> Historical documentation for the legacy custom-entity backend (0.2.0 and earlier). See [native carrier architecture/API](NATIVE_BACKEND.md) and [current validation](NATIVE_VALIDATION.md) for 0.3.0.

# Validation record â€” 2026-09-19

Tested on Windows x64, standard Garry's Mod `2026.09.15` (`260915`, branch
`x86-64`), single-player Sandbox. Exact engine fingerprints are in `ABI.md`.
The original short tests used isolated `gm_flatgrass`. Version 0.1.1 adds long
`gm_construct` runs with installed folder addons enabled and water views active;
Workshop remains disabled. This is not exhaustive addon compatibility coverage.

## 0.2.0 anatomy and vanilla tool integration

Final Release build: **84 native checks passed**. New checks apply sustained
positive/negative torque to elbows and knees, verify angular stops, locked
sideways axes, pivot closure, natural flexion direction, a held rig lifting,
cleanup, authored-primary limits and frozen branch posing without resetting
unrelated bodies.

With installed folder addons enabled on `gm_flatgrass`, all **24 existing
integration checks passed**, including two-way Source collision, soft bodies,
reload, duplication, one/five-model simulation, custom grabber, undo and GPU
cache cleanup (`validation/game-report-0.2.0.json`). Median physics step time
was 2.06 ms for one Flandre and 12.16 ms for five; these exclude deformation and
render time.

All **18 vanilla integration checks passed on each of three humanoids**:

| Model | Vertices | Head lift with engine physgun | Post-release maximum joint error | Maximum pivot error |
| --- | ---: | ---: | ---: | ---: |
| Flandre PMX | 42,775 | 57.72 Source units | 4.18 degrees | 0.250 Source units |
| Stock Miku PMD | 9,036 | 59.00 Source units | 0.66 degrees | 0.056 Source units |
| Dense corpus PMX (初雪) | 358,333 | 58.53 Source units | 0.61 degrees | 0.055 Source units |

Reports: `validation/vanilla-report-{flandre,miku,dense}-0.2.0.json`.
These tests click the actual Q-menu library and drive real player input to the
engine physgun; pickup callbacks are observed, not called to simulate pickup.
They verify lifting, right-click freeze, reload unfreeze, standard Face/Finger/Eye
Poser selection and state changes, client facial controls, preservation of the
frozen primary body pose, Sandbox duplication and the vanilla Remover. All three
models mapped all 30 finger joints. Normal Source entity bone/flex APIs and
exclusion of the invisible handle from collision feedback are checked too.

The dense moving model also passed a **61.9-second** run with every-frame mesh
extent monitoring and finite-body checks. Maximum extent was 72.51 Source units,
median physics step 1.78 ms, and process private memory ranged from 6,820.9 to
6,838.8 MiB in the warmed multi-model test session
(`validation/soak-dense-0.2.0.json`). It remained costly to deform/render; this is
stability evidence, not a claim of 60 FPS on dense models.

A separate real E + mouse physgun rotation scenario on Flandre turned the Source
handle by 9.98 degrees and the native head followed within 0.03 degrees of yaw
at the end of the hold (`validation/physgun-rotation-observed.json`).

The visible MMD creation tab and a rendered humanoid were inspected in captured
game screenshots. Local debug reports/screenshots contain user assets and are
excluded from release archives.

Compatibility is limited to the documented tools. There is one Source handle,
not an engine ragdoll physics-bone array: per-bone Source constraints, Inflator,
and Source material/color replacement are not supported. Physgun freeze applies
to the whole MMD ragdoll. Face Poser has 96 controls; the MMD editor retains all
morphs. Joint profiles infer anatomy from standard names/bind positions and may
need tuning for unusual rigs. Workshop addons were disabled in these tests.

## 0.1.1 crash and performance correction

The original release was reproduced crashing after roughly 30 seconds with a
frozen corpus humanoid. A Windows crash dump showed Source physics on the main
thread, preceded by the addon's non-finite-vector error. Short fixture tests had
missed this failure.

The first-step follow-body transform is now initialized at the spawn position,
preventing a false velocity measured from the world origin. Both generated
fixtures test this at map-scale coordinates.

The fix also removes frozen bodies from Bullet's dynamic lists, disables their
constraints, and skips their repeated bone evaluation. Unfreezing restores the
body registration and constraints. Feedback handles singular/locked inertia
axes without matrix inversion and validates finiteness before calling Source.

Frozen rendering now caches indexed GPU meshes and reuses its shadow map until
the snapshot changes. Moving models use indexed streams, and vertex skinning
runs for a rendered frame instead of each server tick. The latter prevents a
dense model from consuming the server's tick budget merely to regenerate an
unseen intermediate mesh.

Recorded `gm_construct` runs include a 180-second frozen physics regression,
90-second cached frozen runs for 42,775- and 358,333-vertex corpus models, and a
90-second final moving run for the larger model and a 120-second final moving
run for the 42,775-vertex humanoid. No crash or simulation error occurred in
these runs. Cache allocation/build counts stayed constant for idle
frozen models; active models released the frozen GPU caches.

For the 42,775-vertex model, median native draw time across water views fell from
9.94 to 1.53 ms per frame while frozen. The 358,333-vertex frozen model used about
46 MiB of mesh cache and 1.16 ms per frame of draw time. Its final moving run
cost about 1.71 ms per physics update and 27.13 ms per frame for native draws,
plus skinning and engine work. Such extremely dense moving models remain heavy;
this release does not promise 60 FPS for them.

Detailed local reports are `validation/soak-*.json`; source paths and models are
not included in the release. The reusable long-run test is `scripts/test-soak.py`.

The final 0.1.1 build also passes **all 24 in-game integration checks** with
installed folder addons on `gm_flatgrass` (`validation/game-report-0.1.1.json`).
This includes the actual grabber, reload, pose/morph/soft duplication, Sandbox
undo, frozen mesh reuse, and release of entity/preview GPU buffers after removal.
The moving indexed render path and frozen cached render path were visually
reviewed in game. The moving 42,775-vertex test measured a median 4.60 ms of
native draw time per frame across water views; its memory use stabilized after
loading (about a 5 MiB range across the remaining samples).

## Native checks

The Release build passes **55 checks, zero failures**. Generated PMX 2.1
fixtures exercise BDEF1/2/4, SDEF/QDEF under translation and rotation, IK,
morphs, all three rigid modes, springs, cloth/rope, pins and distinct anchor
body/vertex indices. Runtime checks cover finite soft simulation, bounded
fixed-step catch-up, impulse conservation, frozen manual poses, grab picking,
removal, BC1 DDS decoding, cache integrity rejection, frozen body/constraint
registration, singular inertia, and deferred once-per-render skinning.

These fixtures caught and now guard a nanoem soft-anchor index bug. They are
generated from scratch; no corpus models are in the release.

## Original 0.1 game integration (historical)

`scripts/test-game.py` passed all 22 checks for both the stock Miku PMD and a
42,775-vertex PMX from the local corpus. The PMX has 287 bones and 204 authored
rigid bodies. Local detailed reports are retained in ignored
`validation/game-report-pmd.json` and `validation/game-report-pmx.json`.

The suite verifies:

- Native rendering, with separately reviewed model and importer screenshots.
- Map floor collision and contact feedback into a moving Source prop.
- Source impulse calibration: an impulse of 200 on a 2 kg prop produces
  100 Source units/s; a torque impulse of `I.x * 10` produces 10 degrees/s.
- A grab constraint lifting the model, and the actual supplied grabber weapon
  acquiring a movable body.
- Isolated worker reload preserving the model's center, and failed import
  leaving an existing ragdoll intact.
- Manual bone posing, morph/rigid pose duplication and soft-node duplication.
- In-game PMX 2.1 cloth import and simulation.
- A separate native library preview, Sandbox undo, and native instance cleanup.

The final PMD run also exercises the final alpha-blending and debug sequence
changes. Screenshots establish visible geometry/textures, not pixel equivalence
to the MMD client. Hair, face, dress, alpha, outline, depth and shadow behavior
were inspected during renderer development.

## Original 0.1 simulation cost (historical)

These are the original 0.1 measurements, before the fixes above. Each sample is
one server physics-and-deformation update, not total frame time.
There are 120 samples per workload on this workstation; rendering, Source and
other addon costs are additional. Physics uses fixed 120 Hz substeps, with
bounded catch-up inside the server update.

| Model | Copies | Median update | 95th percentile |
|---|---:|---:|---:|
| Stock Miku PMD | 1 | 0.88 ms | 0.99 ms |
| Stock Miku PMD | 5 | 4.20 ms | 4.57 ms |
| 42,775-vertex PMX | 1 | 3.84 ms | 4.20 ms |
| 42,775-vertex PMX | 5 | 21.57 ms | 24.24 ms |

Five models is a placement limit, not a performance guarantee. Detailed models
benefit from a one- or two-instance budget. Native dynamic meshes require
`mat_queue_mode 0` on this engine: the five-model workload exhausted Source's
queued vertex staging arena before the immediate-rendering fix. Matching
minidumps and PDBs were used to identify that failure.

## Corpus parsing

All **706** discovered PMX files were inspected in bounded worker processes.
**696 passed; 10 were rejected**: six cyclic bone hierarchies, two invalid
coordinate cases, and two invalid skin-weight cases. The local detailed report
is `validation/corpus.json` and contains private source paths, so it is omitted
from the release. All inspected files were PMX 2.0 with UTF-16 text; QDEF and
PMX 2.1 coverage comes from the generated fixtures.

Passing inspection means the parser accepted the structure and references.
It does not mean every texture, pose, morph or physics configuration in those
696 files was rendered or simulated in game.

## Remaining limits

- Windows x64 and the guarded standard GMod build only; single-player only.
- Custom Bullet entities use the supplied grabber/editor. Source ragdoll tools,
  NPCs, player models, multiplayer and VMD playback are outside this release.
- Generated primary rigs are approximate. Nonstandard bones and unusual joint
  or soft-body settings may require model-specific mapping/tuning.
- MME effects are not executed. Alpha uses conventional depth/blending, not
  order-independent transparency. Rendering is not an exact MMD reproduction.
- BC1 decoding is fixture-tested; BC2/BC3 support uses the same WIC path but
  does not have separate regression images in this release.
- Large-scale endurance, every material/joint permutation, other maps and
  interaction with unrelated addons have not been exhaustively validated.
