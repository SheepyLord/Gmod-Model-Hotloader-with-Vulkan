# Actor animation, reference poses and death physics — preview.6

**Historical report:** preview.7 supersedes the bind-orientation approach in item 2. It preserves fitted frames instead of replacing them with generic animation-pack frames. The Furina comparison revealed the extra rotation introduced by Source's final IK pass. See [the corrected implementation and measurements](FURINA_BIND_VALIDATION.md).

Implemented on `claude/mmd-importer-performance-87a36d`; rig generator **28**, sharing protocol 2. Install `2.1.0-actors-preview.6`, then spawn fresh actors and reselect the player model in Q → MMD. Cached PMX/textures remain usable. Existing generator-27 actors/duplicates are not migrated.

## Fixes

1. **Use the animation skeleton for proportion subtraction.** The installed Citizen mesh's bind skeleton differs substantially from `humans/female_shared.mdl`; the Alyx player mesh likewise differs from `f_anm.mdl`. Subtracting mesh binds from included animation poses stretched the upper spine above the neck. Citizen now uses the matching shared animation skeleton, Combine uses `combine_soldier_anims.mdl`, and players use `f_anm` / `m_anm`. Actor attachments and IK metadata still come from the actor profile.
2. **Match SCMI's translation-only proportion layer.** Animated bones keep fitted PMX pivots and use the animation reference's rotations. Hulls and the eyes attachment are rebased to preserve their fitted positions when bone frames change. Unit checks verify that the autoplay correction contains no rotation and that hulls do not move. Ordinary ragdoll fitting and secondary solver parameters are unchanged.
3. **Provide forward tool references.** `Reference` and `Referencef` include the root compensation needed to face entity +X after autoplay. This does not rotate the included locomotion/combat sequences. Both actual engine poses produced eyes-forward dot entity-forward **0.99999988**.
4. **Include animation packs without the donor mesh/physics.** Including the complete Citizen donor caused the engine to expose that donor's `.phy` metadata for our carrier. The observed physics-body order then disagreed with the generated studio `physicsbone` indices. For example, index 1 identified a thigh in one direction and a spine in the reverse query. The corrected NPC retains its own 18 bodies, mass distribution, limits and collision pairs. Source builds ragdoll objects from solid names/indices and binds constraints to those objects; keeping these mappings consistent is necessary. [Source SDK ragdoll construction](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/ragdoll_shared.cpp#L166)
5. **Exclude all living actors from secondary contacts in single-player.** The previous optimization only collected single-player owners from the MMD entity list. Stock players/NPCs therefore had no exclusion bodies. Living players, NPCs and NextBots are now collected independently of MMD identity. The native capture also excludes sibling shadow bodies with the same VPhysics game-data owner, including the player's standing/crouched shapes. The filter changes only MMD secondary contacts.

## Validation

Owned visible 2560×1440 `gm_construct` session with folder and Workshop addons enabled, including Enhanced Entity Expression Response and the enhanced MMD animation importer. The primary regression model is March 7th (`bac307fdce…`), retaining all **287 bodies / 329 joints**, 60 Hz stepping, configured solver iterations and the existing stabilization.

- Reviewed Citizen weapon-idle and player Physgun poses: the stretched chest/neck geometry is gone. Combine sequences remain available through its animation pack.
- Reviewed forward Reference/Referencef poses. The animation importer's own `LookupRequiredReferenceSequenceInfo` accepted NPC/player references, with the expected arm-angle probe around 49°.
- Used the installed animation importer's real selection/build/play APIs to build and play `bad_apple_5fa2fcb4db` on the generated Combine model. Reviewed captures at 2 and 8 seconds: limbs, torso and secondary geometry remain bounded, with no native presentation error.
- Actual engine physics metadata now matches the generated carrier, and all **18** corpse `TranslatePhysBoneToBone` / `TranslateBoneToPhysBone` mappings agree with the manifest.
- With a living player and Combine, capture reported **2 excluded living entities / 3 excluded physics objects**, while retaining **54** owned corpse bodies and world/prop geometry. Source feedback remains zero.
- Exercised NPC death and the EEER isolated player-corpse path. The player corpse retains MMD identity, 18 native objects and client presentation; EEER stiffness and player-death-input controllers remain active. Death/respawn did not steal or duplicate the new player's native instance.

Ten-second death checks used a common explicit bullet impact: damage 10 against health 1, force `(25,0,0)` at the subject's world-space center. Early diagnostics with zero damage position/force produced very large synthesized launch forces and were rejected as gameplay comparisons. No release velocity clamp, controller disable or physics-detail reduction was introduced.

| Subject with EEER enabled | Peak linear units/s | Peak angular °/s | Final linear units/s | Maximum body span | Mapping errors |
| --- | ---: | ---: | ---: | ---: | ---: |
| Installed Citizen control | 1231.26 | 4132.72 | 30.89 | 34.82 | — |
| Imported Citizen | 1110.10 | 3685.68 | 33.48 | 38.56 | 0 |
| Imported player, isolated EEER corpse | 1733.52 | 2766.65 | 3.16 | 38.64 | 0 |

All sampled transforms remained finite. EEER's configured stiffness/twitch effects produce motion in both control and imported corpses; these tests establish comparable bounded behavior, not identical trajectories or motionless corpses. NPC and player gameplay use different controller paths, so their peak values are reported separately.

All **16 native tests** passed after the main changes. The actor/serialization test was rerun after the animation-only include correction. Lua profile-selection, living-exclusion, existing QoL, renderer-readiness and transfer regressions pass. Male/female profile selection is covered offline; the pictured gameplay uses female March 7th. Multiplayer transport is unchanged; this update does not claim a new two-client multiplayer validation or exhaustive compatibility with all animation/weapon addons.

Local evidence is recorded in `validation/actor-compatibility-preview6.json`, `validation/actor-compatibility-preview6/`, `validation/death-explicit-*.json`, and the owned-session RPC captures. Private model captures are excluded from the release archive. Temporary settings/debug hooks are restored and the owned test process is closed after testing. Previous release packages remain available.
