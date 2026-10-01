# MMD actors preview — implementation and validation

Branch: `claude/mmd-importer-performance-87a36d`. Package: `2.1.0-actors-preview.9`. Module version: `2.1.0-actors-preview`. Rig schema 3 / generator 30, sharing protocol 2, appearance/dupe state 4. No Blender or studiomdl dependency. Raw model/texture caches remain usable.

Preview.9 supports cached-model restoration before native physics creation,
generic copying and Ragdoll To NPC conversion, and corrects stacked-surface
flashlight lighting. [Persistence and lighting validation](PERSISTENCE_LIGHTING_VALIDATION.md).

Preview.8 fixes actor origin, NPC weapons, player eyes and catalog deletion,
adds reset-all F8, and replaces parameter conditioning with optional
contact-aware stretch projection. [Current QoL validation](QOL_ACTOR_PHYSICS_VALIDATION.md).

Preview.7 corrects the remaining bind/IK rotation mismatch exposed by Furina and adds complete three-segment torso mapping. [Direct compiled-model comparison](FURINA_BIND_VALIDATION.md).

Preview.6 corrects actor proportion subtraction, uses forward tool reference poses and animation-only NPC includes, and fixes single-player living-actor exclusions. See [actor compatibility fixes and real EEER/VMD checks](ACTOR_COMPATIBILITY_FIXES.md).

Preview.5 fixes the single-player renderer compatibility/performance regression from the updated game libraries. See [measurements, visual checks and remaining actor observations](SINGLEPLAYER_RENDER_VALIDATION.md). This renderer fix does not certify every animation/viewmodel combination.

**Installed development preview: dedicated-server functional checks passed with two real clients. Physics performance is addressed/accepted for this rollout by the user on 2026-09-22.** Earlier benchmark results below remain unchanged; acceptance does not relabel a missed numerical target as a measured pass. Broad addon compatibility coverage is documented separately.

## Usage and defaults

In **Q → MMD → Library**, choose Ragdoll, Friendly Citizen, Hostile Combine, or Use as player model. Player profiles offer female (default) and male. Generated actors register with the standard NPC menu and player_manager. Standard model selectors can choose the registered player carrier; MMD appearance attaches without changing gamemode movement, collision hulls, view offsets or loadouts.

**Preview / edit first-person arms** extracts arms, fingers and attached sleeves by bone ownership/weights. Each material supports automatic extraction, inclusion of its complete geometry, or exclusion. Save/rebuild, then apply the player model again. The native hands model follows weapon/viewmodel animations without a second MMD physics world.

**Physics & Performance** contains global client-local backend, collision, workers, solver, adaptive controls and the reset-all shortcut. First-person body defaults off; its menu control is hidden. The console experiment `mmdhl_first_person_body 0` enables Auto and `2` disables it. Camera/body addons can suppress it with:

```lua
hook.Add('MMDHLSuppressFirstPersonBody', 'MyBodyAddon', function(player, isAuto)
    if MyAddonIsDrawingThisBody(player) then return true end
end)
```

This integration hook does not prove compatibility with every camera addon. The first-person color view excludes head/near-camera geometry and duplicate arms; other views and shadow/depth passes retain the full model. Clothing, model proportions and weapon viewmodels still affect the result.

## Alpha-zero materials

| Authored material | Bodygroup | State 0 | State 1 |
| --- | --- | --- | --- |
| Alpha above zero | Hide material | Authored appearance | Hidden |
| Alpha zero | Show material | Authored hidden appearance | Visible, MMD alpha forced to 1 |

Forced alpha applies after material morph evaluation. Texture transparency, entity color and explicit material-editor overrides remain effective. Native/overflow groups, context menus, preview, duplication, replication and renderer snapshots share the interpretation. Reset restores authored appearance. Per-slot revisions ensure server edits/resets replace stale client previews. Physics is unchanged when hiding/showing a material.

## Adaptive physics

Source AI, player movement and ragdoll physics continue normally. Each client independently selects secondary quality:

| Condition, in priority order | Simulation |
| --- | --- |
| Local player, physgun-held, or fast primary-bone motion | Full configured iterations |
| Within 300 units, regardless of visibility | Full |
| Visible within 1000 units | Full |
| Invisible for 2 seconds, or beyond 4000 units | Suspended |
| Remaining within 2000 units | Half, rounded up |
| Remaining within 4000 units | Quarter, rounded up |

Fast-motion defaults are 120 Source units/s or 90 degrees/s, measured across primary bones. Full quality persists for 0.5 seconds after motion/pickup ends. Distance boundaries have 5% hysteresis when leaving a tier. Reduced tiers use at least two iterations, capped at the configured full count. Active worlds stay at 60 Hz with every authored body, valid joint and collision mask.

Visibility combines receiving-client dormancy/PVS, camera-frustum checks and staggered occlusion probes, bounded to six rays per frame. Uncertainty counts as visible. Shadow/depth passes do not establish player visibility.

Suspension drains pending jobs, stops Bullet stepping and per-world scene synchronization, and retains attachment-relative presentation. No simulation debt accumulates. Resume uses current anchors/contacts, clears stale velocities/warm starts, retains surface recovery, and blends presentation over 0.12 seconds.

Automatic workers use available logical threads: T for T ≤ 4, otherwise T − 2, bounded by process affinity and scheduler capacity. This machine reports 32 threads, 30 execution lanes and 29 background threads. The calling thread is one lane. Explicit settings remain available; pool resizing drains existing work. More lanes are not necessarily faster for every workload.

## Actors, animation and corpses

Cached variants cover ragdoll, Citizen, Combine, player and arms. They contain fitted Source bones, the 18-body collision rig, hitboxes, attachments, real animation descriptors, activities, pose parameters and included animation models. Profiles remain separate; player profiles follow SCMI's female/male conventions, with the player pack below. Installed animation packs/addons determine the complete sequence inventory.

**Player packs.** Player models include `f_anm` or `m_anm`, the only player pack the game ships, alone, as Valve's player models do. Until 2.1.0-native.12 they also included SCMI's `f_gst`, `f_pst`, `f_shd` and `f_ss` (`m_*`), which the game lacks and no installed addon provided. Source resolves a missing include to `models/error.mdl` and appends that model's one sequence, `idle`, once: in a stock game the female and male player carriers had that junk `idle` as their last sequence. An addon whose extended `f_anm` already has an `idle` hides it, so the result depended on the installed addons. Player models applied before keep their carriers; applying one again uses the new one.

**Ragdoll animations.** Since 2.1.0-native.11 a spawned ragdoll's carrier also includes the player pack and the Citizen packs (`humans/female_*`/`male_*`) of the Animation style chosen in the library, so animation tools can pose it with their sequences: 1055 with the female style, 1874 with the male one (stock packs). Since 2.1.0-native.12 the Citizen packs come first and the player pack last. Source numbers included sequences in include order, and addons commonly replace or extend `f_anm`/`m_anm`: a client whose player pack differs from the server's no longer shifts the Citizen sequences' indices, and only the player pack's own sequences can differ, as for any player model. Where both have a sequence name (`walk_all`, `head_rot_y`, `head_rot_z`, `body_rot_z` and some reloads), the Citizen sequence is found. It keeps the ragdoll bind (facing -X), spawn pose, physics and Reference. The player and Citizen packs of a style share one reference skeleton and IK chain order, so one proportion layer and chain list serve both, and every included pack ships with the game. The server builds a ragdoll's bodies and joints from its first sequence and applies no autoplay layer there, so that sequence places each physics bone on its bind directly (below its uncorrected non-physics parents) while clients pose the other bones through the proportion layer. The Citizen donor supplies attachments, IK chains and pose parameters. Existing ragdolls, saves and dupes keep their carriers; without a readable reference a ragdoll spawns without the packs, as before.

**Hostile NPC weapons.** Friendly NPCs use the Citizen packs, which animate pistols, SMG/AR2, shotguns, RPGs and melee weapons separately, exactly like vanilla citizens. The Combine Soldier pack has only a rifle stance (SMG, AR2, shotgun, crossbow) and holds any other weapon in its unarmed pose. A hostile NPC therefore picks its carrier from the weapon (`mmdhl.HostileActorRole`): rifle-type weapons, Lua SWEPs with hold type smg/ar2/shotgun/crossbow, and unarmed stay Combine soldiers with Combine AI; pistols, the .357, RPGs, melee and other weapons spawn a *hostile citizen* (`npc_citizen`, not commandable, squad `mmdhl_hostile`) that hates players and player-allied classes and is friends with Combine classes and other hostile imported NPCs, for NPCs created later and after save/dupe restoration too. A Combine entry from the Sandbox NPC tab spawned with such a weapon is replaced by the hostile citizen.

**NPC health.** The library's **NPC health** slider, also on Utilities → Character Models (client convar `mmdhl_npc_health`, 0–10000, sent as userinfo like `gmod_npcweapon`), sets the health and maximum health of new friendly and hostile NPCs: those from the library buttons and from the External Models entries in the Sandbox NPC tab (`mmdhl.NPCHealth`). 0, the default, keeps the health the class sets in `Spawn` from `skill.cfg`: 40 for Citizens and hostile citizens, 50 for Combine Soldiers. Server code passes `options.npcHealth` instead. Pasted dupes and loaded saves keep the health they were saved with: Sandbox's NPC duplicator restores it after `PlayerSpawnedNPC`.

Reference/Referencef contain actual encoded poses. An autoplay proportion delta compensates the donor skeleton. Included IK chains preserve donor ordering and remap links by name; donor autoplay foot-lock positions are neutralized while sequence IK remains active. Source animation and addon bone manipulation remain authoritative and feed the existing MMD retargeting/deformation.

Accepted ground recovery and carry alignment remain enabled. Parameter conditioning has been removed. Optional positional stretch correction respects external contacts. Claude CPU v2 remains the default. Native handles are client-local; entity identity uses asset/rig/generation. Client and server runtimes are separate even in a listen-server process.

CreateEntityRagdoll and CreateClientsideRagdoll attach appearance to existing engine corpses. Existing client secondary worlds transfer when available. A captured life-generation check prevents a retained corpse from taking a respawned player's world when a client joins late. No extra corpse is created. A narrow EEER adapter recognizes its explicit independent-player-corpse/source markers. Client-only corpse shadows use their physics object's engine owner, without treating a Lua object pointer as an entity.

## Multiplayer

Matching Windows x64 native modules must be installed separately on each server/client. Admins publish imported assets; the server approves model data and generates carriers. Packages transfer model/carrier/material/texture data only, with SHA-256 verification, cached-file checks, staging, bounded chunks, progress and cancellation. Executable code, Lua and arbitrary paths are excluded.

Protocol 2 uses cached lossless compression, a bounded sliding window, and background verification. Clients reconstruct the large Source material archive from the supplied textures and check it against the server's hash; a mismatch falls back to downloading the verified archive. No shader, texture or mesh detail is reduced. All participating server/client installations need the updated module and addon. See [transfer validation](TRANSFER_VALIDATION.md) for measurements and recovery tests.

The server owns actor/model selection and appearance. Backend, workers, collision choice and LOD are client-local. Missing assets show a fallback until the approved package is mounted. Source geometry is captured on its owning engine thread; remote clients receive reusable geometry and timestamped snapshots, using interpolated native transforms for moving objects. Contacts remain one-way, excluding living actors and the character's own carrier. Single-player uses the existing in-process immutable scene snapshots.

**Dedicated server plus two real clients passed 34 functional checks.** Both clients ran at 2560×1440, with the enabled addon collection (743 entries reported), on the same Windows host over its LAN IPv4 interface. One client was admin and the other was not; neither was a bot. Approval, verified download, native actors/hands, replicated appearance, edit permissions, independent client physics settings, suspension/resume, remote contacts, death/respawn, and late joining/reconnection were exercised. This does not validate WAN behavior or multiple-account authentication on separate machines.

The initial six-retry connection failure was corrected in the owned harness: use `+clientport`, select unused distinct UDP ports instead of Steam Remote Play's port, and connect through the real local IPv4 address. Passwords have a text prefix so Source cannot interpret a leading `5e…` token numerically. No firewall rules were changed.

The download test transferred 15 files totaling 141,192,134 bytes and verified every SHA-256. Long Windows cache paths now use extended filesystem paths; Source mounts the verified GMA through a short, same-cache alias when needed. Aliases are included in model deletion. A carrier arriving after its network entity triggers a client-side model rebind once its files are mounted. Dedicated-server Citizen donors in Source v44 format are read with the same structural validation as the existing supported versions.

## APIs

```lua
-- Server; same completion/error pattern as the existing ragdoll factory.
mmdhl.Spawn(player, assetId, options, done, progress)
mmdhl.SpawnNPC(player, assetId, 'citizen', options, done, progress)
mmdhl.SpawnNPC(player, assetId, 'combine', options, done, progress)
-- options.npcHealth: the new NPC's health and maximum health (0 keeps its class's);
-- without it, the player's NPC health setting.
mmdhl.SetPlayerModel(player, assetId, options, done, progress)

-- Entity metadata/appearance and client quality diagnostics.
mmdhl.GetMaterials(entity)
mmdhl.IsMaterialVisible(entity, zeroBasedSlot)
mmdhl.SetMaterialVisible(entity, zeroBasedSlot, visible)
mmdhl.GetPhysicsLOD(entity)
mmdhl.GetSecondaryCollisionMode(entity)
mmdhl.GetDiagnostics(entity, detailed)
```

Permissions use asset approval, normal spawn/property hooks, MMDHLCanEdit and MMDHLCanSetPlayerModel. Gamemodes retain control over player model selection. Faction addons can change native NPC relationships, as with other Citizens/Combine.

## Validation

Local raw evidence is in ignored `validation/`; user assets/screenshots are excluded from release packages.

- Native CTest: **15/15 passed**, covering adaptive state/worker selection, actor binary animation/IK/reference streams, arms geometry, sharing/hash checks, long-path atomic replacement and mount-alias cleanup, motion, ground recovery and stretch conditioning.
- `tests/test_qol_lua.py`: syntax, LOD boundary/priority/hysteresis rules, 140 material slots, show/default/reset/duplication behavior, server/client appearance precedence, NULL-physics-object actor capture and corpse/player life-generation ownership.
- Seven eight-character LOD scenes passed full-rig counts, tier selection, finite/no-reset/no-dropped-time checks. Invisible/cutoff worlds stopped stepping. The same suspended instances resumed at full quality when held. Evidence: `validation/adaptive-physics.json`.
- Native Citizen: 687 sequences; Combine: 163; female player: 471; male player: 472 with this addon installation. Both NPC profiles passed the enhanced animation importer's required Reference selection. Citizen locomotion and Combine locomotion/combat were exercised.
- March player applied through the actual client request/ack path. Native pistol hands displayed. The arms editor saved an excluded material and generated a different model path; original selection was restored afterward.
- Standard-selector attachment and death/respawn passed. EEER corpse retained secondary handle 10 and 18 server physics bodies; respawn received handle 13 with the same hands. Three simultaneous client-only NPC corpses and partial removal preserved independent shadows.
- Cyrene native alpha-zero slot 2 and overflow slot 49 showed at alpha 1 and reset to alpha 0. Ordinary slot 0 hid normally. Server reset replaced client material overrides. Captures were inspected.
- Two real clients passed the approved March 7th package/actor tests, with 287 authored secondary bodies and 329 valid joints. Multiplayer appearance checks covered a normal override, alpha-zero Show, color and morphs, plus an explicitly denied edit. Overflow appearance was covered separately in the single-player/Lua tests.
- One remote client suspended a distant world while the other simulated the same entity at full quality with a different backend and collision mode. Suspended ticks remained constant, and resuming kept the same instance.
- A ten-second remote-contact measurement observed both terrain and moving-prop contacts with current geometry snapshots. Both props retained their prescribed kinematic state. This run measured 64.08 ms p95 while two clients ran concurrently; it is a functional contact check, not a comparable single-client performance benchmark.
- Native client NPC corpses and the installed EEER server player corpse attached on both clients. After respawn and client reconnection, the live player (generation 3, handle 5) and older corpse (generation 2, handle 4) remained independent. The close-up capture was reviewed directly: textured player, weapon and corpse rendered without missing-model placeholders or mesh explosions. Evidence: `validation/actors-multiplayer.json` and owned-session captures.

The engine's IK-adjusted Reference calf differed from the encoded reference by about 0.6 Source units on the sampled donor; encoded reference reconstruction was below 0.003 units. NPC Factions' NPCRelation hook overwrites Citizen relationships each tick in this installation. MMD does not fight its faction preference with a competing hook.

Outstanding coverage includes every weapon hold type/reload/crouch/vehicle combination, broad camera/body-addon compatibility, full VMD motion playback, and separate flashlight/depth visual review of every alpha-zero/morph combination. Native interfaces and shared render-state implementation do not by themselves validate all these cases. The installed EEER client file exceeds GMod's compressed Lua transfer limit and emitted an include error; its server corpse path was tested, but this is not a claim that all its client features work remotely. Existing enhanced-blood and gravity-addon errors were also recorded without modifying those addons.

## Performance and rollout

Pinned eight full rigs; visible 2560×1440; enabled folder/Workshop addons; eight seconds warm-up then ten seconds measurement. No mesh, rig or timestep reduction.

| Scene | p95 frame time (ms) |
| --- | ---: |
| Visible standing, adaptive disabled | 24.30 |
| Visible near, full | 24.80 |
| Visible middle, half iterations | 27.88 |
| Visible distant, quarter iterations | 30.61 |
| Invisible, suspended | 15.39 |
| Visible past cutoff, suspended | 27.92 |
| Held past cutoff/out of view, full | 17.43 |

Individual runs do not establish a general speedup. Invisible suspension saves simulation work. Visible distant scenes still have substantial engine/render cost. The 16.7 ms target is not generally met.

The demanding moving clustered world/prop scene measured 55.14 ms p95 on the preserved baseline's old automatic 14 lanes, and 72.05 ms with 30 lanes. The candidate after entity/owner caching measured 72.12 ms at 30 lanes and 62.49 ms at 14 lanes. Both 30-lane runs dropped simulation time, so that gate failed despite finite transforms, complete rigs and valid one-way contacts. The candidate's 14-lane run retained the simulation clock but still missed the frame-time target and was slower than the baseline's single 14-lane run. Earlier candidate failures (83–156 ms p95) remain in the raw evidence; they are not discarded as passes. Older raw reports called the finite/contact checks `functionalPassed`; the harness now also requires the simulation-clock checks for that field.

The user accepted the performance issue as addressed and authorized installation for hands-on testing on 2026-09-22. The source and immutable `2.1.0-actors-preview.4` package are retained alongside earlier releases; the previous installation has a separate restore manifest. Historical performance results remain as measured. Owned test sessions are stopped and temporary hooks removed. Package labels and checksum filenames are version-specific and never overwrite an existing release.

## Reproduction

```powershell
./scripts/build.ps1
python tests/test_qol_lua.py
./scripts/stage-candidate.ps1
./scripts/install.ps1
./scripts/game-start.ps1 -Visible -WithAddons -Map gm_construct
python scripts/test-adaptive-physics.py
python scripts/test-performance.py actors-ground-motion --count 8 --backend cpu_mt_v2 --layout clustered --contacts --motion moving
./scripts/game-stop.ps1
./scripts/stage-candidate.ps1 -Restore
python scripts/package.py --version 2.1.0-actors-preview.4
```

For the dedicated-server gate, prepare independent roots with `scripts/prepare-multiplayer.py` and start `scripts/multiplayer-session.ps1 -Peer server`, then `-Peer client1 -Visible` and `-Peer client2 -Visible`. Run `scripts/test-multiplayer.py` phases `transport`, `actors`, `appearance`, `settings`, `contacts`, and `death`. Stop/restart client2, then run `latejoin`. Each root has its own cache and native runtime. The launcher chooses unused client UDP ports and accepts `-ConnectAddress` if automatic LAN address selection is unsuitable. `-Stop` affects only the matching owned peer. The functional harness caps each client at 60 FPS and is not the eight-character performance harness.

API references: [player_manager/hands](https://wiki.facepunch.com/gmod/player_manager), [server ragdolls](https://wiki.facepunch.com/gmod/GM:CreateEntityRagdoll), [client ragdolls](https://wiki.facepunch.com/gmod/GM:CreateClientsideRagdoll).
