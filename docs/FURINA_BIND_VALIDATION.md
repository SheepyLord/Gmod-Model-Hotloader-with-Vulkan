# Furina actor bind correction — preview.7

Branch `claude/mmd-importer-performance-87a36d`, rig generator **29**.

## Reproduction and cause

Compared the cached hot-import asset `8f6507b0fad5769a24718bdb8dbcca19050b1e9730db313db649b8f89142881f` with `models/sheepylord/genshin_impact/furina_sheepylord.mdl` and the user's `E:/G/Upload/furina/4_export` / `5_propo` SMDs. The cached PMX and `1_PMX/芙宁娜_V1/芙宁娜 R18.pmx` have the identical SHA-256 `476fe45c75d582ec4052fd8916812505cfde4c6a21032fd0dee07257229be468`.

Preview.6 replaced fitted bone orientations with generic animation-pack bind orientations. The offline reference/proportion arithmetic passed, but Source's final IK pass realigned the arm bones to the fitted limb segments. Actual Reference joint positions were already almost exact, while arm orientations changed by about **19.7°**, with a further **3.6–4.0°** discrepancy in the legs. The MMD renderer interpreted that difference as articulation, producing the elbow and knee kinks. This is why the earlier arithmetic-only test did not catch this model's failure.

The fix preserves the fitted skin/physics bind frames. Reference and proportion subtraction use those same orientations, while the animation pack still supplies its translation reference. The autoplay correction remains translation-only; it no longer changes the bind to force that result. Source's IK-adjusted matrices and the renderer's bind now agree.

Furina also has `上半身`, `上半身2` and `上半身3`. The top segment was previously omitted from primary control. These now map to Spine1, Spine2 and Spine4 respectively. Models with only two upper-body segments retain the existing mapping. The physical rig still has exactly 18 bodies.

## In-game checks

Owned, visible, addons-enabled Windows x64 single-player `gm_construct`, 2560×1440. Both comparison entities used the same Reference or `Pistol_idle` sequence, fixed cycle and zeroed pose parameters, avoiding NPC AI/weapon overlays as a comparison variable. Screenshots were reviewed directly. An actual player model was also checked holding the Physgun and during a melee pose.

| Generated Reference variant | Largest primary position error | Largest primary rotation error |
| --- | ---: | ---: |
| Citizen, before fix | about 0.02 units | about 19.7° |
| Citizen, corrected | 0.0494 units | 0.349° |
| Player, corrected | 0.00105 units | 0.0645° |

Errors compare actual engine matrices after IK against the intended fitted Reference pose, rather than only comparing serialized animation curves. Small residuals include Source IK/quantization effects. The corrected screenshots show normal arms, elbows and knees in both Reference and idle, alongside the supplied compiled Furina.

The compiled intermediate skeleton also has approximately **1.028×** the raw importer scale and a **2.4-unit** compiled origin offset. Captures include the unadjusted default import and a separate comparison instance using those uniform adjustments. The importer's global **3.23656 Source units per PMX unit** convention and default size multiplier remain unchanged. These adjustments explain the remaining size/placement difference; they are not the deformation fix.

All **356 authored rigid bodies / 475 joints** remain present, using Claude CPU v2 and 10 configured iterations. A fresh ragdoll retained 18 native objects and correct physics-bone mappings. A 10-second functional observation found no native errors or non-finite bounds. It was not used as a performance benchmark or a new FPS-target claim.

## Regression coverage and rollout

- All 16 native tests passed after the implementation changes. The expanded actor test was rerun after adding the three-segment torso case.
- Actor serialization tests now require preservation of fitted bind rotations, in addition to translation-only proportion correction and hull preservation.
- A synthetic three-segment torso must expose all three primary spine drivers without changing the 18-body count.
- `tests/game/actor-reference-probe.lua` measures actual post-IK engine rotations and positions.
- `tests/retarget_probe.cpp` independently checks whole-model rigid transform invariance through FK/skinning. Furina's rigid-transform errors were below 0.00001 PMX units, ruling out a general skinning translation/rotation failure.
- Existing Lua actor-profile and QoL regressions passed. Prior rendering, living-actor exclusion and animation-only NPC include fixes remain in place.

Local evidence: `validation/furina-preview7.json`, `validation/furina-preview7/`, `validation/furina-fix-ctest.log`. Private model captures and intermediate skeleton data are excluded from release archives.

Install **2.1.0-actors-preview.7**, then respawn NPCs and reapply the player model to create generator-29 variants. No PMX reimport is required. Previous packages remain available. Temporary camera/debug hooks, render settings and the owned game process are restored/closed after testing. This check covers the supplied Furina and does not certify every model or addon combination.
