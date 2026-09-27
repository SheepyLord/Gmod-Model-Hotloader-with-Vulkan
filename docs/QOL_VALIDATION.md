# QoL validation — 0.6.0

Validated 2026-09-20 in the existing Windows x64 single-player installation.
The native carrier remains an 18-object `prop_ragdoll`; scale and authored
secondary-physics detail are unchanged.

## Changes and results

| Change | Verification |
|---|---|
| Short, readable engine paths | Model/material/texture identities use 16 hex characters. Common material terms become `face`, `hair`, `skirt`, etc.; unfamiliar names are romanized. Authored names remain in metadata/editor labels. Reviewed the actual SubMaterial HUD. |
| Source-safe naming | Reproduced bytewise lowercasing corrupting UTF-8 model filenames, causing an AME ERROR preview. ASCII engine paths fix it. All 64 Cyrene VMTs can be read and parsed by AME; visible preview, textures and shader controls were reviewed directly. |
| Library controls | Selected-row preview, search, favorite, refresh, the actual Place button and the actual Delete confirmation all passed with a disposable model. |
| Complete deletion, no Deleted tab | Disposable 513-material import removed its live ragdoll, manifest/model, carriers, fits, settings, aliases and unused textures. Shared textures and original PMX survived. Windows-locked archives were queued; a game restart removed all four remaining paths and the queue. Reimport cancels queued deletion of reused files. |
| Advisory budgets | 136 native checks passed. A 513-material model warns and retains all materials. 大国主 imports and places successfully with its two 8192-pixel textures; each cached VTF is 357,914,020 bytes, beyond the former 256 MB cap. |
| Default external collisions | Mode 2 includes map geometry and physics objects, excluding the owning carrier and living players/NPCs/NextBots. The live-player/live-NPC probe excluded both actors' physics objects. Floor, wall, slope, moving-prop and another-ragdoll contacts passed, with zero secondary impulses applied back to Source. |
| Recoverable secondary physics | Public helper, bindable command and context entry recover stopped solver/presentation state. All 18 native body positions and frozen states survived reset; collision mode and full authored body count remained intact. |
| AME compatibility | Edited, saved and reset slots 0, 31, 40 and 63. Verified native/overflow overrides, posed UV geometry, visibility and duplication. Ordinary Source models retain AME's 32-slot limit. The distributed patch applies cleanly to its documented base and matches the installed integration files. |

The import worker no longer has the artificial 4 GB memory ceiling or a
five-minute forced cancellation. Large input budgets produce visible warnings;
Cancel remains available. Malformed-data checks, index validation and actual
decoder/file-format requirements remain in place.

## Final short measurement

Owned session `395dfad6ed944822ab825ccb774ffb26`, `gm_flatgrass`, GMod build
`2026.09.15`, `x86-64`, actual 2560×1440, 741 mounted entries reported by
`engine.GetAddons()`, with the user's folder/Workshop addons enabled.

大国主 retained 18 Source objects, 341 MMD bodies and 491 joints, with collision
mode 2. After warm-up, the foreground measurement lasted 10 seconds:

| Metric | Result |
|---|---:|
| Foreground frames | 1,083 |
| Median frame time | 8.41 ms |
| 95th percentile | 11.94 ms |
| 99th percentile | 22.00 ms |
| 16.7 ms p95 target | Pass |
| Non-finite/renderer/stopped-solver errors | None reported |

An earlier owned-session measurement before foreground testing resumed recorded
34.47 ms p95. The final result is specific to the measured foreground session;
it is not a long-duration stability or multi-character performance claim. No
authored bodies or joints were removed to achieve it.

## Reproduction and evidence

- `build/bin/Release/mmdhl_tests.exe tests/fixtures/cloth21.pmx tests/fixtures/rope21.pmx`
- `python scripts/test-qol.py` — disposable import/deletion/default/reset checks.
- `python scripts/test-compatibility.py materials` — AME and material/bodygroup checks.
- `python scripts/test-secondary-scenes.py` — one-way external collision scenes.
- `python scripts/test-native-stability.py Daikokuten --models validation/qol-models.json --seconds 10 --skip-focus` — first bring the owned game forward.

Local evidence lives under `validation/`: `native-qol-tests.log`,
`qol-game.json`, `qol-cleanup-after-restart.json`, `qol-daikokuten.json`,
`compatibility-materials.json`, `compatibility-contact-scenes.json`,
`native-daikokuten-collision2-stability.json`, `qol-environment.json`, and
`ame-patch-validation.json`. Reviewed screenshots include
`compatibility-materials.png`, `qol-warning-library.png`, and
`qol-readable-materials.png`. Local character assets and captures are not
included in the release package.

Only the owned test process is stopped. The harness restores its temporary
graphics/input settings and removes the debug bootstrap when stopped.

## Reset controls

Aim at an MMD ragdoll and run `mmdhl_reset_physics`, or use **Reset MMD physics**
in its context menu / **In this map**. `mmdhl_reset_physics all` resets every
MMD ragdoll. Optional keybinding: `bind "F6" "mmdhl_reset_physics"`.

Development cache/duplicate compatibility is not guaranteed. Original source
models are never deleted by library management. Mounted cache archives that
Windows locks are physically removed on the next game start, with that delay
reported in the deletion result.
