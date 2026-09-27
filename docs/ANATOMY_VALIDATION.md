# Anatomy and SCMI shading correction validation — 0.3.2

Tested 2026-09-20 in the owned Windows x64 single-player session on GMod build 2026.09.15, with a visible 2560×1440 window, all 740 mounted Workshop addons and the user's folder addons present (29 directories). The graphics settings include 8× MSAA, 16× anisotropic filtering, HDR 2 and mat_picmip -1. Full authored rigs are retained: Xin 686 bodies/947 joints, Cyrene 447/639, Sandrone 571/873.

## Diagnosed and corrected

- Carrier primary positions were already taken from the imported PMX landmarks. The carrier exposes a ValveBiped subset and synthetic intermediate bones, while retaining the complete PMX skeleton internally. It is not the unscaled stock human skeleton.
- SCMI SMD reference orientations were used without converting their horizontal coordinate axes to the renderer's convention. This put the knee's main local-Z hinge along the model's facing direction. Generator 9 applies the missing -90° vertical rotation, tracks local X to explicit limb successors, and preserves reference roll, using one fitted bind pose for skin offsets, inverse binds, pivots and collision.
- Calibration enlarged some limb hulls beyond their endpoints. Long-bone hull lengths and centers are now bounded by the imported joint pivots.
- The PHY wrote `selfcollisions 1`. Source treats the presence of this key as disabling self-contact. The corrected PHY explicitly enables all 136 nonadjacent pairs, with 17 adjacent joint pairs excluded. Reference: [Valve's ragdoll parser](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/ragdoll_shared.cpp).
- Runtime materials omitted the author's shared normal, toon lightwarp, exponent texture and rim response. They now use the legacy SCMI corpus VMT recipe and those three generic VTFs under private addon paths. PMX base textures are retained. Fractional texture alpha blends; cutouts write depth; morph/entity fades have a separate blend material.

## Checks executed

The native build passes 89 checks, including new assertions for PMX-derived pivots, knee bend direction and bounded limb hulls. `scripts/test-native-anatomy.py` exercises all three supplied characters with their full secondary rigs.

| Model | Maximum PMX/physics pivot error (Source units) | Serialized hull center error | Native enabled pairs | Isolated hand/torso contacts |
|---|---:|---:|---:|---:|
| Sandrone | 0.00001527 | 0.00000382 | 136 | 1 |
| Cyrene | 0.00000788 | 0.00000477 | 136 | 1 |
| Xin | 0.00000792 | 0.00000383 | 136 | 1 |

For each character, both elbows and both knees were driven against both hinge stops (eight cases). All eight came within 3° of the authored SCMI limits: elbows -120…10°, knees -10…125°. Both calf hinge axes point laterally, with lateral magnitude >0.98 and forward magnitude <0.05. This tests the anatomical direction in addition to the existence of numerical limits.

`native.ProbeCarrierCollisions` reads the engine's parsed pair table. The contact test then breaks the joints on a disposable carrier, freezes the torso and fires its own detached hand toward it. Each hand produces a real self-contact and rebounds without crossing the torso center. Joint forces cannot account for that result.

All three also pass 18-body creation, native bone mapping/traces, weld/rope, per-body freeze and 50 bare-carrier create/remove cycles via `scripts/test-carrier.py`.

## Visual review and materials

Thirty repeatable captures in gm_construct cover all three characters: full body, collision overlay, neutral/posed face and hand, flashlight, cubemap override, entity color/submaterial and zero alpha. Captures were inspected directly for representative collision alignment, face/hair shading, knee bend direction and flashlight response. Separate side captures show Xin and Sandrone with a calf bent backward by 90°. Sandrone also has a dark indoor lighting / flashlight comparison. Shader introspection verifies VertexLitGeneric, the three non-error textures, Phong boost 24, albedo tint and rimlight on all 47 Xin material slots. Cutout and blended parts coexist (1 cutout and 10 fractional-alpha textures for Xin).

The collision inspector explicitly refreshes the invisible carrier's bone cache and displays the selected joint's allowed arcs plus its original MMD bone name/index. Fit corrections now record the generator: older-frame corrections are preserved but not automatically reapplied to a new coordinate frame.

## Stability and measured performance

The shortened stress run measures 60 seconds after 8 seconds of warm-up, driving three ragdolls simultaneously. It passes with no runtime errors, non-finite transforms, mesh explosions, crashes or freezes. Tail private memory ranges from 7592.7 to 7605.3 MiB, ending below the earlier sample. This short interval cannot establish long-term absence of a leak. All authored secondary bodies and joints remain present.

This is a stability pass, not a 60 FPS claim. The three-character foreground p95 was 57.54 ms. A separate full-detail Xin run has 1,808 sustained foreground samples with median 13.67 ms, p95 17.58 ms and p99 28.85 ms. The single-character 16.7 ms p95 performance target therefore remains unmet in this gm_construct run with the current addons/settings. Physics detail was not reduced.

Raw evidence is retained locally in `validation/native-anatomy.json`, `validation/carrier-gate.json`, `validation/native-material-profile.json`, `validation/native-stress-stability.json`, `validation/native-xin-stability.json`, and the session captures under `data/mmd_hotloader/debug/60e08bd6064244489414c1963e8d9d65/`. These contain private model metadata and are not packaged.

## Updating

Restart GMod and spawn fresh ragdolls. Generator 9 produces new immutable carrier paths automatically, reusing original PMX/texture caches. Already spawned or saved old carriers retain their old compiled frames; this update does not rewrite those files in place. Generic self-collision remains a convex-body approximation; clothing uses the separate authored nanoem world and does not collide with Source entities.
