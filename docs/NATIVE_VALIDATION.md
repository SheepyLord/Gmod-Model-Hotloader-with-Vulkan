# Native carrier acceptance — 2026-09-20

The native carrier and reusable-buffer renderer are enabled by default after the checks below. This report separates observed results from implementation coverage. Private model paths, full captures, corpus geometry, logs and dumps remain in ignored `validation/` and the game's debug cache; they are not distributed. The native regression suite passed 86 checks with zero failures.

## Installation and measurement conditions

- Windows x64, single-player Sandbox; GMod `2026.09.15`, branch `x86-64`.
- AMD Ryzen 9 9950X3D, NVIDIA GeForce RTX 5090, approximately 64 GB RAM. NVIDIA driver `32.0.16.1088`.
- Visible 2560×1440 game, normal folder and Workshop addons enabled. The initial engine inventory reported 742 Workshop entries, 741 with `mounted=true`; the final renderer session reported 741 entries, 740 mounted, and 29 folder addons. The Workshop inventory changed externally during testing; the harness did not disable addons. Inventories are recorded with each run; unrelated addon warnings remain in the console.
- 8× MSAA, 16× anisotropic filtering, `mat_picmip -1`, HDR level 2, DX level 95, VSync off, `fps_max 300`; shadows and flashlight depth enabled. Native drawing temporarily uses `mat_queue_mode 0`, restored after the last carrier is removed.
- Primary gameplay validation session: `ebda3b1a6b1749e7b8b9cc2bbe31cd72`; tint and initial map-change checks: `80ebf202efd140dca211fe93ca0c1b7a`; reusable-buffer renderer checks: `98780c28820d418a9a627cc12d49208e`. Engine DLL hashes and private interface contracts are in [ABI.md](ABI.md).
- Frame times are actual consecutive PostRender timestamps after eight seconds of warm-up. Percentiles use bounded 0.01 ms histograms. Capturing a PNG produces occasional large outliers; those remain in the measured distribution. The current harness retains every observed frame and separately records sustained foreground frames, excluding the first second after focus returns. Performance acceptance requires at least 1,000 foreground samples and 20 measured foreground seconds. All three final character benchmarks below had zero unfocused frames. Focus loss does not invalidate runtime stability observations.

## Carrier and native tools

All three acceptance assets generated and mounted their carriers without Blender or `studiomdl`. In-game probes observed `prop_ragdoll`, exactly 18 valid physics objects, 18 successful collision traces, reversible physics-to-bone mappings, 56 native bones and working weld/rope constraints. Fifty bare-carrier create/remove cycles passed. The generated PHY contains 17 anatomical constraints with SCMI frames, mass biases and damping.

Forced elbow/knee hinge probes measured forearms −120.33…10.00° against limits −120…10°, and calves −10.84…126.36° against −10…125°. Small solver overshoot is expected. These are measured hinge-axis checks, not a claim of exhaustive three-axis limit verification. Bind-pose, finger-direction and full-ragdoll motion captures were also inspected.

The actual Q → MMD library row placed each imported character. The three gameplay suites passed 14/13/13 checks for Xin/Cyrene/Sandrone. Real player commands exercised Physics Gun pickup, lift, rotation, drop and individual-body freezing. Stock Face/Finger/Eye Poser handlers, native constraints, Sandbox duplication and Remover passed; removal released runtime instances.

Nine extended checks passed for Xin's 37 overflow sliders, complete-face copy/reset/paste/save/load, stock Weld, Color and Material tools, a Source cubemap material, and undo. Ten interaction checks passed for hand/foot/torso grabs, Super Gravity Gun pickup/launch, stock Rope, terrain/prop contacts, damage impulses, the collision editor's actual save button and migration of a legacy duplicate.

Normal Gravity Gun rejection of flesh ragdolls matches stock GMod/Source behavior; the same rejection was observed on a stock ragdoll. The Super Gravity Gun was enabled temporarily for its pickup/launch test and then restored. No addon-specific pickup bypass is installed.

Evidence: `carrier-gate.json`, `native-tools-{Xin,Cyrene,Sandrone}.json`, `native-extras.json`, `native-interactions.json` under local `validation/`.

## Secondary physics and reference fidelity

| Character | Authored bodies | Authored joints | Native / overflow morphs |
|---|---:|---:|---:|
| 心 / Xin | 686 | 947 | 96 / 37 |
| 昔涟 / Cyrene | 447 | 639 | 84 / 0 |
| 桑多涅 / Sandrone | 571 | 873 | 64 / 0 |

All authored bodies and joints remain represented. Source-bound bodies become kinematic followers; joints between two followers retain metadata without an unnecessary active solver constraint. Secondary worlds have no ground, Source mirrors, inter-character contacts or feedback impulses. No physics-detail reduction was used for performance.

The separate nanoem C-API reference replay passed 600 identical driving frames for each model. Maximum rigid-body position error was zero; rotation matrix differences were below 8×10⁻⁷. Independent GLM evaluation of emapp bone feedback agreed within 9×10⁻⁶ MMD units and 4×10⁻⁷ in rotation matrix components. Bind-pose error was below 6×10⁻⁶ MMD units; dropped time was zero.

The supplied characters contain rigid-body cloth/hair chains, not PMX 2.1 soft bodies. Separate generated cloth and rope fixtures each exercised a soft body under native primary driving. Maximum independent-world differences were approximately 0.000065 and 0.00231 MMD units respectively, below the 0.01 MMD-unit tolerance; transforms remained finite. This does not establish every third-party soft-body configuration or the entire nanoem animation/IK application pipeline.

Evidence: `replay-*.json`. Runtime and reference use the pinned backend from [nanoem](https://github.com/hkrn/nanoem/blob/30acffaa29f5d2eb9e997d69418f2e4b97b5894f/nanoem/ext/physics_bullet.cc), separate worlds, identical poses/settings and independent feedback equations.

## Fitting calibration

Of 267 corpus candidates, 207 pairings passed QC collision-input, proportion-pose and limb-proportion checks. Sixty were rejected. The split contained 160 calibration, 42 family-separated evaluation and five acceptance-family holdouts. The broad family grouping and ambiguous-variant rejection intentionally sacrifice some data.

Median absolute log dimension error on evaluation bodies improved from 0.25052 to 0.11238. Acceptance-family holdouts improved from 0.24416 to 0.11472. The corresponding post-calibration p95 errors were 0.53990 and 0.64461. These are sorted primary-hull dimension errors, not full surface-overlap accuracy. QC translations do not change those dimensions; proportion poses and local bone frames are checked before extracting only the 18 primary bodies.

Aggregate priors improve automatic fitting, but outliers remain. Low-confidence fits use bounded defaults and the collision editor can save per-model corrections. No neural network or corpus geometry is shipped. Corpus materials were not reused without verified model/material correspondences; the default materials derive from PMX textures and properties.

The five family holdouts have no exact PMX file-hash match with the three supplied acceptance models. Consequently, no exact compiled-counterpart visual comparison or corpus-material correspondence is claimed for those models.

Evidence: `fitting-corpus.json`, `fitter-evaluation.json`, `acceptance-corpus-correspondence.json`.

## Visual review

Final `gm_flatgrass` captures were inspected directly: all three meshes, alpha clothing, detailed sunlight silhouettes, flashlight illumination/shadows, close-up faces/hands, finger bending, collision alignment and material/color overrides. Twelve timestamped movement frames per model show secondary hair/clothing following moving native limbs without mesh explosions. Source render-to-texture shadows use the deformed MMD geometry; a typed shadow material reference preserves texture alpha and avoids the engine warning/performance failure found in an earlier candidate.

`gm_construct` captures were reviewed in bright outdoor and dark indoor areas, including flashlight illumination and shadows. Visual review found that native IMesh draws needed explicit entity tint/opacity; the corrected draw path visibly produces the selected blue tint, wireframe submaterial and transparency. At alpha zero both mesh and sunlight shadow disappear. All 31 Sandrone material color/alpha states returned to their original values after drawing. The secondary-body overlay was inspected against the clothing/hair geometry.

A same-process change from `gm_construct` to `gm_flatgrass` with an active 18-body MMD ragdoll completed safely: no remaining runtime instances, no shadow error, and `mat_queue_mode` restored to 2. New native ragdolls rendered after the map change. Evidence: `native-final-render-state.json` and corresponding captures.

The final reusable-buffer path also passed the reverse `gm_flatgrass` → `gm_construct` change with an active character. Live buffers and instances returned to zero, the cache was enabled again on the new map, and Xin's dark-area/flashlight captures were reviewed. Source shading and sunlight shadows were directly compared between cached and immediate-stream rendering at the same camera. See `native-cache-map-change.json`, `cache-review-xin-*`, `stream-review-xin-full.png` and `final-cache-construct-xin-*`.

Source shading intentionally differs from MMD/MME. VertexLitGeneric receives native ambient/local-light/cubemap state and deformed normals/tangents; conventional transparency can still show sorting limitations. Diffuse color/alpha and geometry/UV morphs work. Uncommon MMD sphere/toon/edge shader effects and all material morph semantics are not reproduced by Source's standard shader. No pixel-equivalence claim is made.

## Performance and sustained stability

| Full-detail character, 2560×1440 | p50 | p95 | p99 |
|---|---:|---:|---:|
| Xin | 13.10 ms | **15.16 ms** | 26.79 ms |
| Cyrene | 3.47 ms | **12.54 ms** | 16.47 ms |
| Sandrone | 3.32 ms | **12.99 ms** | 17.10 ms |

Each passed the single-character p95 ≤16.7 ms target with the reusable-buffer and sunlight-shadow paths: 2,334 / 6,199 / 6,318 samples respectively. The earlier immediate-stream renderer showed variable Xin p95 results of 16.91–25.57 ms in follow-up checks, prompting replacement with three reusable vertex buffers per material/format. The final path reuses immutable indices and updates vertices only for a changed deformation snapshot. No physics detail, resolution or graphics quality was reduced. Xin's live buffer allocation was approximately 34.1 MB, and all live buffers were released after removal.

The empty-scene baseline after map change was p50 3.34 ms / p95 4.59 ms with the user's queued mode restored to 2 (5,281 samples). An earlier warmed addon session measured p50 3.42 ms / p95 14.59 ms. Both remain recorded; render modes and session conditions differ, so subtracting those percentiles from character percentiles would not estimate importer cost.

The user shortened the three-character stress requirement to one minute and requested that the active run end as a pass. It was stopped and recorded as passed at its actual elapsed time of 673.96 seconds, with 21,733 sampled frames, no runtime errors and no dropped simulation time. Its 1,611 unfocused frames make its frame-time percentiles unsuitable as a performance benchmark. Future stress runs default to 60 measured seconds after eight seconds of warm-up.

The final reusable-buffer path passed the revised one-minute stress gate (eight-second warm-up; 69.93 seconds total until its status heartbeat completed), with all 1,704 authored bodies and 2,459 joints represented, zero dropped simulation time and zero runtime errors. Private memory after warm-up was 7,499.32–7,506.46 MB, median 7,501.26 MB and final 7,503.14 MB. The test now stops collecting frames exactly at its configured duration, independently of heartbeat polling.

Fifty full rendered spawn/remove cycles passed again with the final buffer path in 73.99 seconds including completion polling. Private memory after warm-up was 7,406.44–7,440.00 MB, median 7,434.84 MB and final 7,411.12 MB. The buffer count and allocated buffer bytes returned to zero after removal. Earlier stream-path cycles also passed; the longer user-ended observation's successive 150-second memory-window medians were 7,408.30, 7,410.26, 7,411.32 and 7,411.45 MB. These observations support bounded cache behavior over the tested intervals, not a guarantee of indefinite memory stability.

No crashes, freezes, non-finite transforms or mesh explosions were reported during the runtime gates. Some earlier game launches stalled before any MMDHL module loaded; owned-process dumps were saved and those launches restarted. Those startup failures are not counted as successful runtime tests. The three-character run is a stability requirement; the 60 FPS criterion applies to one character.

Evidence: `native-*-stability.json`, matching per-run environment/heartbeat JSON, PNG captures and process-memory samples. The user-ended observation is preserved separately as `native-stress-extended-observation.json`. Earlier renderer runs are historical evidence, not substituted for the final-buffer checks.

## Release scope

Windows x64 single-player on the guarded engine build only. Native carriers expose 56 Source bones and at most 96 stable flex controllers; the complete MMD inventory remains available through metadata and overflow helpers. Nonstandard humanoid landmarks can require naming/mapping work. MME, multiplayer, NPC/player animation, VMD playback, secondary collision with the Source world and inter-character secondary collisions are outside this release.

Owned-session debug hooks, settings and RPC are removed/restored at test completion. The normal addon does not enable the RPC listener. The legacy backend and immediate-stream renderer remain available for explicit rollback.
