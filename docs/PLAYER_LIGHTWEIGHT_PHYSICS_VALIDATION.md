# Player presentation, ledge lighting and lightweight motion — preview.11

Branch: `claude/mmd-importer-performance-87a36d`. Reuses existing asset/rig caches; no model reimport.

## Fixes

The supplied `gm_construct 2026-9-22 19-28-21.gms` was loaded in an owned addons-enabled game. The Furina ragdoll's world bounding-box center was inside the solid ledge (`CONTENTS_SOLID`, sampled light `[0,0,0]`). Its pelvis and several other bones remained in open map space. Lighting now keeps the normal center when valid and otherwise uses an exposed primary bone. The fallback follows that bone while it stays valid, avoiding repeated switches between limbs. It never borrows the player's light or selects points by brightness. The four-light map fix and overlapping-surface flashlight fix remain intact.

**Physics Gun arms:** Workshop 3102962003 is *Invisible Physgun*, which replaces `c_superphyscannon.mdl`. The generated hand bones matched that viewmodel, but the importer had also included upper-arm sleeve geometry at the camera. Automatic first-person extraction now follows SCMI’s forearm-and-below cut. This removes the obstruction with Invisible Physgun enabled; a visible pistol still renders the imported hand normally. Explicit material inclusion overrides remain available. Only the derived arms variant receives `armsGeometryVersion: 2`; reapply **Use as Player Model** once to regenerate older arms. The Workshop addon is unchanged.

The model-only persistence adapter had attached a complete PMX renderer and secondary world to `gmod_hands`. Hands and viewmodels now use their native studio geometry without a full-character visual instance. Their renderer is excluded from the actor/physics list, including old tagged hands. Player color rendering is hidden whenever the actual camera is inside the first-person range, even when a body addon requests `ShouldDrawLocalPlayer`. Third-person, reflection and shadow views retain the character.

A second origin-ghost path existed before the first asynchronous physics publication: suspended players could display secondary geometry in its construction pose at world origin. Unpublished worlds now follow the latest animated skeleton. Color/shadow output waits for a valid Source palette; the first visible simulation step initializes at the current pose. Local first-person physics is suspended independently of the adaptive-quality setting. Reset/backend changes invalidate the quality cache so they cannot accidentally reactivate hidden-player simulation. Single-player scene capture also stops when no visible full-physics character needs it.

## Physics accuracy and shortcuts

| Value | Behavior |
|---|---|
| 1–100 | Existing full secondary physics; default 10, fixed 60 Hz, authored bodies/joints/masks retained. |
| 0 | Lightweight inertia springs on physics bones. Rotation only, exact parent pivots/bone lengths, bounded swing and speed. No Bullet stepping, gravity, world/body collisions, or external-scene synchronization. |
| −1 | Secondary motion disabled; bones follow the animated pose. Native Source ragdolls, NPC AI and player movement remain active. |

A cached hierarchy lookup traces each physics bone to its nearest mapped ValveBiped ancestor. The inward child→ancestor vector is compared to **character translation direction**, not spring velocity/displacement. Swing is multiplied by `max(0, cos(theta / 1.33))`: front-mounted parts cannot trail backwards into the body while moving forward; rear-mounted parts may trail outwards. A stationary character (below one Source unit/s) or missing/coincident ancestor uses weight 1. This is a cheap directional heuristic, not collision detection.

Lightweight targets come from the animated skeleton independently of parent jiggle. This prevents small motions from accumulating into curls along long hair chains. Damped analytic stepping supports variable render rates; teleports and long stalls reinitialize safely. Returning to full physics drains old jobs and rebuilds contacts/warm starts at the current pose instead of replaying suspended time. Soft-body deformation is not simulated in the lightweight/disabled modes.

`mmdhl_toggle_physics` switches to −1 and restores the previous non-disabled accuracy, including 0. **Physics & Performance** contains a toggle button and key pickers for toggle/reset. Both shortcuts default to unassigned to avoid claiming another game/addon's keys. An old F8 reset assignment is cleared; the reset handler ignores F8. Press/hold is edge-triggered, and shortcuts do not fire while unfocused, typing or using menus. Existing manual reset commands remain available.

## Validation

- Saved ledge case reproduced and reviewed directly before/after. The corrected sample is outside the solid brush, approximately `[0.55,0.58,0.56]`; ordinary valid lighting points are unchanged.
- In-game player test (Linlong, 599 bodies / 809 valid joints): first-person body hidden, hands instance handle 0, only the actual player in the presentation list after removing the test ragdoll. Bullet ticks remained 0; physics and scene synchronization cost 0. Reset did not wake first-person physics. Single-player scene diagnostics reported inactive, zero objects/capture cost.
- Third-person and first-person captures reviewed, including a crowbar view; no full-character hand proxy or world-origin ghost. Dedicated first-person arms remain native and editable through the existing arms options.
- Native suite: **17/17 passed on the final build**, including the arms extraction regression. The extended quality test covers lightweight motion at 30/60/240 Hz, finite output, exact parent pivots, cosine front/back/side weighting, facing invariance, settling to the authored pose, disabled-pose agreement, mode switching and initial suspension away from world origin. Its final settling revision passed again.
- Python regressions passed: camera/shortcut rules, solid-lighting fallback, QoL Lua, controls, actor profiles, renderer readiness, sharing transport and virtual folders.
- Visible, focused 2560×1440, addons enabled. Each measurement is 10 seconds after warm-up; two full-detail characters (Linlong + Furina, 955 bodies / 1,284 valid joints), with the frozen Source ragdoll driven through a repeatable forward/backward motion. The native ragdoll retains 18 objects in every mode. Lightweight/disabled runs report no Bullet work or external-scene synchronization. Rendering and animation still cost time.

The final focused comparison (before the geometry-only arm cut) recorded:

| Accuracy | Focused frames | Median frame | p95 frame | Mean reported Bullet work |
|---|---:|---:|---:|---:|
| 10 | 1,042 / 1,042 | 8.91 ms | 15.13 ms | 2.80 ms |
| 0 | 1,317 / 1,317 | 7.05 ms | 12.54 ms | 0 ms |
| −1 | 1,253 / 1,253 | 7.42 ms | 13.30 ms | 0 ms |

The directional lookup found 547 Linlong and 293 Furina bones with ValveBiped ancestors. Sampled lightweight pose/jiggle work at the end of the run totaled about 0.31 ms for both characters; this includes animation evaluation. These are short gameplay samples, not a claim that off mode must always beat jiggle in total frame time. Engine/addon/rendering costs remain. An unfocused run was discarded.

Measurements and final visual captures are recorded privately under `validation/preview11-evidence/`; model assets and saves are not packaged. The early lightweight gravity experiment was rejected after visual review and is not shipped. This is a single-player validation, not a new multiplayer/addon compatibility certification. Temporary camera/motion hooks and game settings are restored after testing.
