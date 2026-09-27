# Compatibility and reliability validation — 0.5.0

Tested 20 September 2026 local time (21 September UTC captures), Windows x64 single-player Sandbox, rig schema 3 / generator 16 and duplication schema 3. The affected functional checks pass. The 16.7 ms single-character p95 performance target remains unmet for four of the five acceptance models in the final default-mode measurements.

## Implementation and functional results

- **残虹 / Zankou:** imports, previews, spawns and simulates with all 718 rigid bodies and 929 valid joints. Joint 373, `Bn_flowerB_R01補`, is skipped with a named identical-body-reference warning. Separate fixtures cover invalid indices and world-anchor endpoints; preview and spawning share validation.
- **March 7th:** collision generation passes after complete convex topology repair. Weld/rebuild and bounded closed fallback regressions pass. No open hull is made by deleting an isolated triangle. All five characters spawn with exactly 18 native objects at 0.1, 0.5, 1 and 2 size multipliers: 20 in-game checks.
- **Native regression suite:** 121 passed, zero failed. This includes self/invalid/world joints, near-duplicate vertices, collapsed hull fallback, secondary external contacts, own-carrier exclusion proxy removal and safe rejection of truncated generated texture headers.
- **Independent nanoem replay:** all five complete 600 frames with zero body-position difference from the separately driven reference world. Full rigid-body counts are preserved: Xin 686, Cyrene 447, Sandrone 571, Zankou 718 and March 7th 269. Valid joint counts are 947, 639, 873, 929 and 389 respectively.
- **Eyes:** the appended native pivots agree with the visible MMD pose within 0.02 Source units, including zero/half/full EERP eye-position scales. The actual EERP function and stock Eye Poser handlers were exercised. Explicit zero bone input wins in either call order; target-only rotation is bounded and resets. A compiled SCMI Cyrene reference agrees in head-local eye displacement within 0.00098 Source units under identical EERP inputs. That controlled comparison temporarily suspends automatic player-target selection, invokes the real addon function, and restores its settings; the normal compatibility runs leave tracking active.
- **Morphs:** actual Face Poser controls show all 96 native plus 37 overflow Xin controls with stable machine names/controller IDs; three uncertain labels use their authored names. March 7th has 71 native controls, nine with authored fallback labels. Original names are available to search and tooltips.
- **Materials:** all 64 Cyrene slots have readable VertexLitGeneric VMTs and VTF base textures. The actual AME preview/save/reset paths pass for slots 0, 31, 40 and 63, including shader parameter edits, UV geometry, posed picking and duplication. A 130-material fixture reaches slot 129 in Lua and AME while the underlying native enumeration remains 128. Ordinary Source entities retain AME's 32-slot limit.
- **Visibility:** all 64 material bodygroups default visible. Native and overflow hiding, context-menu dispatch, editor picking and duplication pass. Captures of all-hidden versus visible geometry were reviewed with a shadowed projected texture; hidden geometry and its shadow disappear while all 18 native physics objects remain.
- **Import/UI:** the real worker shows filename/stage/progress, Cancel, a persistent HUD after closing the library, parse errors and completion. Cached repeat imports succeed. The final library uses a light background; Cancel remains inside its banner. AME frames the actual visible mesh rather than the carrier bounds. Menu placement preserves scale, frozen state and collision mode across reopening; its server request now forwards the collision choice.
- **Gameplay:** 13 current Sandbox checks pass: Q-menu placement, 18 objects, native flex count, actual Physics Gun pickup/lift/rotation/drop, individual freezing, face/finger/eye handlers, weld/rope, duplication, Remover and runtime release. The finger-copy check applies the real Finger Poser in the same tick as copy so another enabled addon's live finger animation cannot change the input between probes.
- **Capacity/migration:** six live native MMD characters are accepted. The five-character restriction was removed from native and legacy spawn paths. Genuine older generator-9 duplicates migrate with scale, physical poses, freezing and fingers preserved; maximum checked body-position error remains below 0.01 Source units. Current duplicates also retain material overrides, hidden parts and secondary collision mode.

## One-way secondary contacts

Tests on `gm_construct` use actual Source map geometry, wall/slope props, a moving prop and a separate native ragdoll. The character's own 18 objects are identified and excluded. Maximum observed contacts per short scenario were:

| Scenario | Contacts | Capture / synchronization at final sample |
|---|---:|---:|
| Map floor | 60 | 0.018 / 0.004 ms |
| Wall prop | 56 | 0.017 / 0.005 ms |
| Sloped prop | 84 | 0.031 / 0.002 ms |
| Moving prop | 17 | 0.052 / 0.007 ms |
| Other ragdoll | 62 | 0.021 / 0.011 ms |

The moving prop retained its commanded Source velocity with zero measured deviation; a separate initially stationary prop also remained stationary. `feedbackApplied` stayed zero. Mode changes, teleport, object removal and map cleanup completed without invalid transforms or stale proxies. Contacts are optional and can stretch constrained clothing when the user puts frozen primary bones through obstacles; they do not provide bidirectional Source physics.

## Final 10-second measurements

Actual resolution **2560×1440**, standard GMod build **2026.09.15 / x86-64**, `gm_flatgrass`, visible foreground game. All enabled addons were loaded: **741 mounted Workshop entries out of 742 listed**, with 30 folder addons present. Graphics included 8× MSAA, 16× anisotropic filtering, texture setting -1, HDR 2, DX95, shadows/flashlight depth enabled, VSync off and fps_max 300. Character rendering uses `mat_queue_mode 0`; the empty scene restores mode 2.

Each scenario measures 10 seconds after 2 seconds of warm-up, with cached assets loaded beforehand. The probe records frame percentiles, deformation/render work, upload volume, physics timing, dropped simulation time, finite bounds and process memory. Its two diagnostic PNG captures remain in the timing interval, so individual maxima include capture overhead. No physics detail, texture quality or enabled addons were reduced.

| Scenario | p50 | p95 | p99 | Foreground frames | 16.7 ms single-character target |
|---|---:|---:|---:|---:|---|
| Empty scene | 3.57 ms | 15.20 ms | 17.44 ms | 1,316 | Baseline |
| Xin | 16.34 ms | 30.19 ms | 49.27 ms | 539 | Miss |
| Cyrene | 12.38 ms | 20.07 ms | 34.90 ms | 738 | Miss |
| Sandrone | 12.75 ms | 18.64 ms | 34.36 ms | 771 | Miss |
| 残虹 / Zankou | 15.13 ms | 21.82 ms | 45.85 ms | 624 | Miss |
| March 7th | 6.25 ms | 9.04 ms | 13.58 ms | 1,527 | Pass |
| Three supplied characters | 41.96 ms | 76.67 ms | 133.35 ms | 211 | Stability scenario |
| Cyrene, world + objects | 12.18 ms | 16.43 ms | 28.11 ms | 782 | Pass in this run |

The optional-contact run's lower p95 is not evidence that contacts improve performance: these are separate short measurements with variable background and engine work. Earlier measurements in this task also varied. The previous 0.4.0 report measured Xin/Cyrene/Sandrone at 20.59/17.52/17.48 ms p95 over longer intervals; the current default-mode figures are worse and do not establish a performance improvement. The current empty scene alone has a 15.20 ms p95, so these runs do not isolate the addon's cost.

All five single-character runs, the three-character run and eight spawn/remove cycles completed without crashes, freezes, stopped instances, non-finite transforms or exploded mesh bounds. Bounded catch-up reported 0.15–0.22 seconds of dropped simulation time in single-character runs and 0.85 seconds per world in the three-character run; these are not zero-drop results. The cycle run's private memory decreased from 8,567 to 8,546 MiB. Process private usage after warming all five models was approximately 8,300–8,650 MiB; this includes the entire game and addon set. After cleanup the native instance, editor-preview and cached vertex-buffer counts were zero. Shared material/topology/asset caches remain resident by design. These short checks do not prove long-duration leak freedom.

## Visual review and evidence

Reviewed full-resolution captures directly: import banner/HUD, AME model/texture/shader controls, Cyrene full body, neutral/posed face and hands, collision overlay, flashlight, cubemap/color/submaterial overrides, full hiding and a 12-frame driven motion sequence. Captures cover bright `gm_construct` and `gm_flatgrass` views. No missing-texture checkerboards, detached eye meshes or geometry explosions were observed. This update does not establish pixel-identical MMD/MME shading or exhaustive coverage of every external addon/shader.

Private evidence stays in the development `validation/` directory and game cache; character images/models are excluded from the release ZIP:

- `native-compatibility-tests.txt`, `compatibility-replays.json`, `compatibility-imports.json`.
- `compatibility-materials.json`, `compatibility-overflow.json`, `compatibility-eyes.json`, `compatibility-compiled-eyes.json`, `compatibility-morphs.json`.
- `compatibility-visibility.json`, `compatibility-contact-scenes.json`, `compatibility-import-feedback.json`, `compatibility-spawn-settings.json`.
- `compatibility-performance.json` and complete `native-*-stability.json` files.
- `native-tools-Cyrene.json`, `legacy-native-duplicates.json`, `ame-patch-validation.json`.
- Captures under `data/mmd_hotloader/debug/53a93dd64d6f4d1cb2a1305dc711a3d3/` (construct/UI) and `6ef6b60b530340feaa9f10b5cb73db83/` (final performance/motion).

The test watchdog targets only the owned process. Temporary configuration and eye-tracking settings are restored, and the owned test session is stopped after validation. AME's unrelated working-tree edits are preserved; its compatibility diff is packaged separately and checked on a clean base and the current installation.
