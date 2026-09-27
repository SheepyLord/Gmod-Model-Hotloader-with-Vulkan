# Actor and physics QoL — preview.8

Implemented on `claude/mmd-importer-performance-87a36d`, 2026-09-22.
Release `2.1.0-actors-preview.8`; rig generator 30. Raw assets are reused;
respawn NPCs and reapply player models to obtain the new carrier.

## Changes

- Citizen, Combine and player variants have a fixed +2.4 Source-unit origin,
  equivalent to the SCMI origin correction. Skeleton, inverse binds, collision,
  animation proportions and MMD retargeting use the same transform. Ragdolls
  and first-person arms keep their existing origin and scale.
- Library NPC spawning respects Sandbox's `gmod_npcweapon`: empty selects
  the profile default, `none` is unarmed, and a valid NPC weapon is used as
  selected. The gamemode spawn hook receives that same weapon.
- F8 resets all client-local MMD secondary worlds. Physics & Performance has
  an editable shortcut and reset button. Console: `mmdhl_reset_all_physics`;
  Lua: `mmdhl.ResetAllPhysics()`. It preserves native limb freezes and poses.
  The shortcut ignores typing, menus and unfocused windows.
- Single-player deletion no longer resurrects an empty row from the server
  catalog. It removes publication and NPC/player registration as well as the
  cache. Deleting an active player model restores the previous stock model.
  Windows-held mounted archives remain queued for deletion on next startup.
  Multiplayer clients still see their server's independently approved library.
- Player/NPC eye targets use world coordinates; ragdolls retain attachment-local
  targets, as documented by [SetEyeTarget](https://wiki.facepunch.com/gmod/Entity:SetEyeTarget).
  A narrow runtime adapter fixes EEER's player-target conversion without editing
  its addon files. Target-driven PMX eye rotations are limited to 20 degrees yaw
  and 15 pitch, keeping shallow iris geometry inside the face. Explicit native
  eye-bone poses (including zero) still take precedence in the same update.
- First-person body defaults off, including a one-time reset of the former
  Auto-enabled development setting. Its menu control is removed. The hidden
  console override and normal third-person body/hands remain available.
- Removed parameter conditioning entirely: no near-unity damping caps, mass
  heuristics or strengthened translation ERP. Optional experimental stretch
  correction now works with external collisions. It remains off by default.

## Stretch correction

Surface recovery runs before positional correction so buried-strand detection
still sees violated joints. Four alternating projection sweeps correct excess
translation outside authored limits plus the configured allowance. Followers,
world anchors, free axes and angular springs remain untouched.

Each proposed correction sweeps the actual convex body against the current
external collision geometry, then slides along up to three contact planes.
World/prop contact takes priority when contact and attachment constraints are
incompatible; some temporary separation is preferable to clipping underground.
Only achieved correction removes separating velocity. No correction is divided
by timestep to add velocity, and no impulse is sent to Source objects.

The safety projection uses equal mobility for dynamic endpoints to prevent a
heavy accessory concentrating all error in a light link. Bullet still uses
authored masses, damping and spring parameters in its physical solve.

## Validation

- Native CTest: **16/16 passed**. Tests cover floor/wall sliding, slope contacts,
  contact escape, compression/lift, a reset below the floor, collision-mode
  switching, near-unity damping, mass ratios, parameter preservation, carrier
  animation encoding, existing carry/attachment behavior and async scheduling.
- Five executable Python/GLua suites passed: QoL syntax/state, NPC/catalog
  behavior, actor profiles, renderer readiness and sharing transport.
- Owned single-player `gm_construct`, visible **2560×1440**, enabled folder and
  Workshop addons. Reviewed screenshots from motion probes and player eye views.
- Furina: all 58 bind pivots moved +2.4 units (maximum residual 0.0000016);
  fitted native physics remains 18 bodies. Source Reference-pose probes:
  Citizen max position error 0.04894 units / rotation 0.345 degrees; player
  0.00101 units / 0.082 degrees. Live mesh/bone alignment was below 0.00014 units.
- Player eyes visible during EEER tracking. Forward world-space target produced
  under 0.008 degrees residual; explicit zero bone manipulation won over a
  subsequent target in the same tick. First-person body reported Off.
- Citizen with Sandbox weapon `none` spawned unarmed; Combine with
  `weapon_shotgun` spawned with the shotgun. Defaults/invalid selection also
  covered by Lua regression.
- Reset shortcut dispatch fired once while held and reset all four live MMD
  instances. Physics panel exposed the F8 binder and omitted retired controls.
- A uniquely generated disposable model was imported, published, spawned and
  deleted. Its entity, manifest, library row, published rigs and actor
  registrations disappeared; library refresh reported zero ghost rows.
  Player-model removal separately restored the previous model and matching hands.

Both full-rig motion measurements lasted **10 seconds after warm-up**, at
60 Hz / 10 solver iterations, with correction enabled and external collisions.
No authored bodies/joints were removed, no automatic reset or dropped simulation
time occurred, and every measured frame had focus.

| Probe | Bodies / joints | Final joint error, PMX units | Below floor after lift | Frame p95 |
|---|---:|---:|---:|---:|
| March 7th, rapid sleeve motion and 240 units/s carry | 287 / 329 | 0.0684 | 0 | 7.12 ms |
| Sandrone, ground compression and lift | 571 / 873 | 0.1376 | 0 | 18.99 ms |

Sandrone recorded 60 simultaneous world contacts and 2,727 contact-limited
projection moves. Maximum sampled projection cost was 0.452 ms; March 7th was
0.056 ms. These are functional contact/motion probes, not a new comparative
performance claim; the Sandrone run exceeds 16.7 ms total frame time. Prior
user acceptance of performance is unchanged. Multiplayer and unrelated camera
addons were not revalidated in this update.

Private evidence: `validation/qol8-ctest.log`,
`validation/qol8-march-sleeve.json`, `validation/qol8-sandrone-ground.json`,
and token-scoped RPC/captures. Test-only cameras, hooks and settings are restored
when the owned test session stops.
