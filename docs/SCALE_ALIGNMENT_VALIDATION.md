# Scale, presentation and collision validation — 0.4.0

Tested on 20 September 2026 with rig schema 2 / generator 14. This update corrects scale, the visible skin's native-bone alignment, and loss of fitted collision detail during VPhysics conversion. It also replaces the box-like fitter with portable corpus-derived convex priors. It does **not** meet every proposed acceptance target: the held-out collision accuracy target and the final single-character 16.7 ms p95 targets remain unmet.

## Changes and causes

The default conversion is `raw PMX × 0.08 × 40.457`, or **3.23656 Source units per PMX unit**, followed by the user's size multiplier. The 0.08 term is the PMX import unit conversion in the SCMI workflow; 40.457 is its bodygroup scale factor. Characters retain their relative heights. Hidden accessory geometry no longer changes the default size. Explicit legacy `height` and `scale` API options remain available; conflicting options are rejected.

The 56-bone carrier uses original mapped PMX positions and SCMI English names. It is an essential/finger projection of the MMD skeleton, not a fixed-size stock skeleton or a copy of every internal MMD bone. Missing intermediate spine/neck/attachment landmarks are synthesized and identified in metadata. Its SCMI reference orientations are converted into the renderer's coordinate frame, tracking the actual limb direction while preserving roll. The ragdoll bind pose already contains the proportions, so no animated proportions delta is required.

The old mesh path could consume a different pose from the client skeleton shown by native tools. Rendering now reads Source's current client bone matrices once per frame and uses that same palette to drive primary MMD bones, secondary physics and deformation. The server no longer advances a redundant secondary pose. Original MMD names, D-bone aliases and indices remain internal metadata.

VPhysics's point-cloud and plane constructors simplified small fitted hulls aggressively. Supplying explicit `CPolyhedron` topology preserves the tapered convex surfaces. Source's clockwise winding and directed-edge endpoint convention are validated, followed by a two-way surface comparison against the actual engine convex before caching the PHY. No Blender, `studiomdl`, engine DLL redistribution or runtime corpus lookup is introduced.

## Portable fitting

Of 267 discovered candidates, the earlier correspondence audit produced 207 usable pairings; rigid registration retained 206. Calibration uses 159 pairs, with 47 held out by character family, including the supplied characters and their variants. Registration accounts for QC transforms and excludes the torso landmarks intentionally moved by SCMI's proportion workflow.

The fitter combines summed bone weights, anatomical landmarks, connected-component filtering and robust surface quantiles. Finger, toe and twist contributions fold into primary bodies; authored secondary chains, invisible regions and explicitly excluded materials do not enlarge limb collision. Small support-plane regressions estimate dimensions and choose tapered convex references. Selection between reference shapes and plane predictions uses calibration-family folds only. Every body has at most 64 hull vertices. Bounded overlap adjustments never move the bone pivots.

Only normalized convex references and fitted coefficients are shipped. The three acceptance models fitted in a clean worker/runtime/PMX directory in **0.14–0.88 seconds** each. These are parse/fit measurements, not a promise that texture conversion and a complete first import finish in that interval. This portability check does not sandbox filesystem access; the runtime also contains no SCMI/corpus path dependency.

Held-out exact convex intersection-over-union over **846 bodies**:

| Measure | Result | Proposed target |
|---|---:|---:|
| Median IoU | 0.6704 | 0.75 |
| 10th percentile IoU | 0.3718 | 0.55 |
| Head median | 0.7860 | — |
| Thigh medians | 0.781 / 0.782 | — |
| Calf medians | 0.746 / 0.750 | — |
| Clavicle medians | 0.407 / 0.410 | — |
| Hand medians | 0.544 / 0.568 | — |

The preceding plane-only candidate scored 0.6039 median and 0.3804 at the 10th percentile. Tapered references improved the median, not the tail. Low-confidence regions remain marked for review; confidence is not a guaranteed IoU score. The collision editor displays actual engine geometry in cyan and proposed edits in yellow, supports material-region exclusion, and saves corrections per asset. Automatic fitting is still weaker around clavicles, hands and some torso shapes than a carefully authored SCMI collision model.

## Engine and gameplay checks

| Check | Observed result |
|---|---|
| Native regression suite | 105 passed, 0 failed |
| Carrier identity and traces | All three models: genuine `prop_ragdoll`, 18 physics objects, 18 successful body traces |
| Joint frames and stops | All three: PMX pivots, lateral knee axes, eight exercised hinge stops per model |
| Self-contact | 136 nonadjacent collision pairs enabled; hand blocked by torso on all three |
| Presentation at 0.5× / 1× / 2× | All three passed static and moving comparisons |
| Largest rendered-bone discrepancy | 0.000070 Source units |
| Largest sampled rigid-vertex discrepancy | 0.000127 Source units |
| Largest engine/manifest convex discrepancy | 0.00000190 Source units |
| Library | 33 checks: preview, search, favorites, rename, placement acknowledgement, deletion/restore, scene removal and import |
| Native tools | All three passed physgun, freezing, face/finger/eye posing, constraints, duplication and removal; Xin includes overflow morphs |
| Additional interactions | Gravity gun, terrain/prop collisions, damage, fit editor, color/material overrides, complete-face presets and undo passed |
| Older native duplicates | Generator-9 examples for all three migrated with saved scale, physical pose, freezing and finger edits retained |
| Removal cycles | 50 bare carriers and 50 fully rendered spawn/remove cycles passed |

Easy Entity Inspector's installed bone and physics views were captured directly. The numerical alignment check compares the rendered snapshot with Source's current client palette and independently transformed rigid-weighted vertices; it does not merely compare two copies of the manifest. As documented by Source's Lua API, a last-tick `GetBonePosition` may lag a current `GetBoneMatrix` during motion.

An independent nanoem world replayed 600 identical driving frames for each character, plus the generated PMX 2.1 cloth and rope fixtures. All five passed; maximum rigid-body position difference was zero in these replays. This validates the pinned solver/synchronization path, not every feature of the desktop MMD application.

The tool harness now obtains a valid map surface instead of using Flatgrass's height on Construct, waits for the server's actual placement acknowledgement, and measures physgun rotation about all axes. A scenario with invalid aim fails with the placement error rather than reporting success merely because a button handler ran.

## Environment, performance and stability

Visible **2560×1440**, standard x64 GMod build **2026.09.15**, `gm_flatgrass` and `gm_construct`. The inventory recorded 741 mounted Workshop entries out of 742 enumerated, plus 30 folder addons present. Graphics included 8× AA, 16× anisotropic filtering, picmip -1, HDR 2, DX95, shadows and flashlight depth enabled, VSync off and `fps_max 300`. MMD rendering used `mat_queue_mode 0`; final probes recorded it during measured frames, not just at scenario setup.

A separate RTX test installation was running concurrently and was left untouched. These are measurements of the observed machine workload, not an isolated hardware benchmark. Only sustained foreground frames count toward the performance target. Screenshot capture stalls remain in the recorded distributions.

Final single-character runs, each with eight seconds of warm-up and 32 measured seconds:

| Character | Median frame time | p95 frame time | Foreground frames | 16.7 ms target |
|---|---:|---:|---:|---|
| Xin | 13.20 ms | 20.59 ms | 2,148 | Failed |
| Cyrene | 11.32 ms | 17.52 ms | 2,201 | Failed |
| Sandrone | 12.25 ms | 17.48 ms | 2,600 | Failed |

An earlier run measured 19.14 / 13.96 / 13.67 ms respectively, with a 4.73 ms p95 empty scene. The later empty-scene retry lost focus and supplied only 10.21 measured foreground seconds, so it was rejected as an acceptance benchmark. Results vary with the concurrent workload; the earlier passes for Cyrene and Sandrone are not treated as proof of the final target. Individual recorded stalls reached 2.85 s for Xin and 5.75 s for Cyrene. The harness did not diagnose their cause, and they remain part of the evidence.

Sharing skinning transforms reduced Xin's measured deformation-plus-secondary median from 4.95 to 4.42 ms, but did not establish the whole-frame target. No physics bodies, joints, mesh vertices or material passes were removed for this optimization.

The requested **one-minute three-character stress interval**, after eight seconds of warm-up, passed with all **1,704 authored rigid bodies and 2,459 authored joints** represented. Primary followers retain authored connections; follower-to-follower constraints need no active solver constraint. The run reported no non-finite transforms, mesh-bound explosions, stopped instances or render errors. Its p95 was 59.50 ms; this is a stability pass, not a 60 FPS result for three characters.

The 50-cycle run returned below its later-cycle private-memory readings after cleanup; no continuing increase was observed in that short test. These bounded checks do not establish long-duration leak freedom. Earlier development sessions did crash in `shaderapidx9.dll`; the cause of those render failures was not conclusively isolated. The final build completed the reported sessions, map changes and checks without a crash. Keep the ABI guards, diagnostic dumps and owned-process watchdog.

Visual captures were inspected directly: native skeleton/hull overlays, the enlarged library preview, faces, posed fingers, lighting and moving clothing. Bright projected light can wash out the high-Phong SCMI preset. The renderer still does not reproduce MME effects or guarantee pixel-identical appearance to every compiled counterpart.

## Evidence and rollout

Private evidence is kept under ignored `validation/`: `presentation-alignment.json`, `native-anatomy.json`, `carrier-gate.json`, `portable-fit.json`, `shape-quality-holdout.json`, `library-ui-final.json`, `legacy-native-duplicates.json`, `native-tools-*.json`, `replay-final-*.json` and `native-*-stability.json`. Full-resolution PNGs and environment records are in the owned session directories under the game's `data/mmd_hotloader/debug/`. User character artwork, those private reports and the corpus are excluded from release packages.

Restart GMod after installing the DLLs and spawn fresh ragdolls to use generator 14. Existing carriers remain immutable. Raw model/texture caches are reused; older duplicates migrate through the factory. Temporary test hooks and configuration changes are removed/restored when the owned session ends. This is a tested correction to the existing native backend, with the accuracy and performance limits above; it is not a claim that all original release gates now pass.
