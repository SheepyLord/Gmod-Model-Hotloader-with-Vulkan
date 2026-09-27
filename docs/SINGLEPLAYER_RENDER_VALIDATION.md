# Single-player rendering regression — preview.5

Tested on 2026-09-22, branch `claude/mmd-importer-performance-87a36d`.

## Cause and fix

The installed game had updated `engine.dll` and `client.dll` fingerprints. The renderer's strict guards rejected the new libraries, preventing lighting, preview drawing, world drawing and shadow registration. Failed initialization did not retain its result, so successive frame hooks reread and hashed the DLLs. Ten rejected lighting calls cost 49.38 ms; shadow setup also repeatedly hashed the larger client DLL. The small UI status area displayed only the tail of the resulting traceback.

The apparent empty scene had restored March 7th as the local player model. That explains the overlay's one Source body: it was a player, not a malformed one-body ragdoll. First-person body display was off during the controlled comparison.

Preview.5 adds the two specifically validated fingerprints without weakening the unknown-build rejection. An offline RTTI/vtable and disassembly audit established that the renderer interfaces retain their ABI: complete lighting/shadow methods match, except three data relocations to identical data in shadow setup; client executable code and entity-list tables are unchanged. The rendering/material/VPhysics libraries are unchanged. See [ABI.md](ABI.md).

Successful and failed validations are now retained with `ValidationOnce`; unknown builds cannot trigger per-frame hashing. A renderer readiness check prevents allocating invisible client worlds or changing render queue mode when initialization is unavailable. Native failures appear as their actual message in the library, with full details in the tooltip; ordinary native failures no longer manufacture a Lua exception. Rendering errors also appear in the optional diagnostics.

## Measurements

Visible, foreground, addons-enabled single-player Sandbox, `gm_construct`, 2560×1440, 128 Hz server tick, `fps_max 300`. Each measurement lasts ten seconds after warm-up. March 7th asset `bac307fdcefad2b6331a95e3b77644ad1562a1aba9983c0830e260326f2ee4c6`; 22,754 vertices, 287 authored bodies, 329 joints. Claude CPU v2, 30 lanes, 60 Hz/10 iterations and full physics LOD remain unchanged.

| Scene | Average FPS | p50 ms | p95 ms |
| --- | ---: | ---: | ---: |
| Preview.4, restored player, first-person body off | 69.29 | 14.25 | 15.93 |
| Preview.5, same player/camera, first-person body off | 259.88 | 3.85 | 5.23 |
| Preview.5, one visible native ragdoll | 280.39 | 3.58 | 5.11 |
| Preview.5, empty map after removal | 298.63 | 3.34 | 4.49 |

The first two rows are the direct before/after comparison. The ragdoll row uses a dedicated inspection camera and a stock player; it is a separate scene, not an additional baseline comparison. Its Source bodies were frozen for inspection, while all authored secondary physics remained active. Physics was subsequently unfrozen and the fallen model remained rendered. No resolution, mesh, rig or solver-detail reductions were used to fix the regression. The empty scene's measured physics/pose/deformation work was zero, prepare overhead 0.00062 ms and pose-capture hook overhead 0.0243 ms. Native render buffers returned to zero; the user's previous render queue mode (2) was restored. One thousand warmed renderer readiness calls took 0.096 ms.

## Functional checks and limits

- Library preview visible with no preview error; menu placement succeeds.
- Spawned `prop_ragdoll`: **18 server physics objects**, 58 native bones, complete 287/329 secondary rig. Client-side `GetPhysicsObjectCount()` is not the authoritative server count.
- Normal rendering and registered Source shadows produce real draw calls, uploads and bounded mesh caches with no native lighting/shadow errors. The flashlight/unfreeze smoke check remains visible.
- Citizen actor renders and runs a native `Pistol_idle` sequence. **This is a rendering smoke check, not an animation-quality pass**: chest/clothing deformation was visible. Generated first-person arms also showed deformation in the selected viewmodel. Those actor/proportion issues are not corrected by this renderer patch.
- Removal clears client instances, rendered meshes and shadow hooks. Test sessions were stopped and temporary configuration restored.
- Three affected CTest cases passed: cached render validation (16 concurrent callers, 16,000 rejected calls but one validation), adaptive materials/physics, and animated-carrier/sharing format tests.
- Lua readiness/error-label tests, existing GLua QoL regressions and delayed transfer/cancellation tests passed. Older engine fingerprints remain supported. Multiplayer transport code was unchanged; two-client network checks were not repeated for this renderer-only fix.

Local evidence: `validation/render-abi-20260922.json`, `validation/singleplayer-render-preview5.json`, individual `sp-preview*.json` measurements and `validation/singleplayer-render-preview5/*.png`. These contain private local model captures and are intentionally excluded from the release archive. Reproduce the audit with `scripts/validate-render-abi.py`; measurement hook: `tests/game/render-regression-measure.lua` through the owned-session harness.

Package/install: `2.1.0-actors-preview.5`. Preview.4 archives are retained. Raw model/texture caches and generated rigs need no reimport for this fix.
