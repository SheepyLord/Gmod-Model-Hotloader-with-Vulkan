# Model Hotloader 2.2.0

**Model Hotloader 2.2.0.** Native packages: `Model-Hotloader-2.2.0-<commit>-win64-vulkan.zip` (with the DXVK renderer) and `Model-Hotloader-2.2.0-<commit>-win64-opengl-remix.zip` (without it).

- **Models that failed to import now load.** A mock import of 998 models from testers' libraries failed 25 times; 22 of those now import, and the other three are Blender pose libraries with no objects in them. Instead of rejecting a model, the loader repairs broken numbers that MMD and PMX Editor tolerate: vertices without a valid position (their triangles are hidden, as in MMD), NaN normals (rebuilt from the faces), NaN texture coordinates, BDEF4 weights such as (1, 1, 1, −2) on one bone, a bone parented to itself and other bone loops, rigid-body masses above 10¹⁵ (anchors of up to 7.6 × 10²¹), joint springs stiffer than 10¹² and NaN morph offsets. Each kind of repair is listed once under the model's notes. Models that loaded before load exactly as before, and their cached identity is unchanged.
- **Textures are at most 4096 pixels on a side.** Larger ones, such as 8192 atlases, are scaled down on import with their aspect ratio kept; the game shows no more detail. The Source materials package is now written one texture at a time, so large characters need much less memory, and the test library imported in about 20 % less time. Static props scale such textures down too, instead of rejecting them.
- **Static props.** `.blend` files up to 3 GiB import (a 1.7 GB character file failed before), and FBX files whose meshes list the same bone twice no longer fail Assimp's validation.
- **"Hair and clothing collide with" is now a set of checkboxes**: World, Character, Objects, Living players and Living NPCs, in Physics & Performance, Utilities → Character Models and the character editor. Character and Objects are on by default, so the map is now off by default. A level chosen in an earlier release carries over once (character only, or character and map); the old default becomes the new one. Living players and NPCs collide as their simple collision boxes, and a player model never collides with its own player. In multiplayer each client receives only the kinds of objects it collides with.

**Languages.** The addon speaks English, Simplified and Traditional Chinese, Japanese, Korean, French and Russian. It follows the game's language automatically; the globe button beside **Refresh** in the library, or **Utilities → Character Models → Language**, picks another; open windows switch at once and the rest of the spawn menu when it next closes. Messages from the server appear in each player's own language. All text comes from `addon/resource/localization/<language>/mmdhl.properties`; the English file explains each phrase's context, and each translation records the English it was made from, so `python scripts/check-i18n.py` can report missing, outdated or mismatched phrases. `mmdhl_i18n_debug 1` marks every phrase in game to find hard-coded text and clipped buttons. Messages written by the native module stay English. [Languages and translating](docs/TRANSLATING.md).

**VRM characters.** The character importer accepts `.vrm` avatars (VRM 0.x and 1.0) beside PMX and PMD. They become ragdolls, NPCs and player models with Face Poser expressions (vowels and blinks under their MMD names). Their hair and clothing move with the avatar's own **VRM spring bones**, simulated natively as the VRM specification defines them rather than converted to MMD rigid bodies. Chains land on the floor instead of passing through it, and damping measured against the body keeps hair from trailing behind walking NPCs (**VRM: hair and clothing keep their shape while moving**, on by default). The library shows each avatar's VRM licence, and sharing a model that forbids redistribution asks first. [Design, measurements and limits](docs/VRM_IMPORT.md).

**Static props update.** Blender `.blend` files import directly (Blender 2.60–5.x, gzip or Zstandard compressed), with a window to choose which meshes to import. The **Static Prop** tool gun places props and attaches them to character, NPC, ragdoll or prop bones, and the duplicator keeps attachments. **Edit parts…** saves part presets cut by material or a 3D region, each with its own collision. Characters gain an **Edit bodygroups…** editor: presets of which parts (material slots) show, with a default applied to new spawns and player models. Imports detect characters imported as props (and the reverse) and offer to switch; failures open a window with the failing step, the error and what to try. Flat or degenerate models no longer fail collision. The library header puts the tabs beside **Refresh** and **Import**, and primary actions are coloured. [Guide](docs/STATIC_PROPS.md) · [validation](docs/STATIC_PROPS_VALIDATION.md).

**Static props.** **Q → External Models → Static Props** imports OBJ, FBX, GLB, GLTF and PMX files as physics props, next to **Character Models** in the same library. **Import Static Props** starts the import; the file is parsed inside the bundled worker, with no Blender or external importer. Then preview it and **Spawn Prop** where you aim. A fast single convex hull is generated in about 15 ms by default; a detailed CoACD collision (up to 16 hulls) is optional. Materials become engine-lit `VertexLitGeneric` surfaces with texture-alpha analysis, normal maps and Phong, lit by the map's lighting at each prop plus flashlights. Props obey Sandbox limits, undo and the duplicator, can be resized in place, and are shared with multiplayer servers through the existing model transfer. The importer is ported from the GModel Hot Loader. `lib_coacd.dll` now ships beside `mmdhl_worker.exe`. [Guide](docs/STATIC_PROPS.md) · [validation](docs/STATIC_PROPS_VALIDATION.md).

**2.1.0-actors-preview.16** limits lightweight jiggle to the first simulated bone of each skeletal chain; descendants inherit its motion without their own springs. The accuracy slider now uses levels **1–7 → 1, 2, 5, 10, 20, 50, 100 iterations**, with **0 Jiggle** and **−1 Off** retained. The default remains 10 iterations (level 4), and saved custom iteration counts are preserved. [Offline validation](docs/JIGGLE_ROOT_ACCURACY.md).

**2.1.0-actors-preview.15** restores the NPC/Server/Drawing dropdowns while the External Models tab is open and removes the renderer handoff gap on death. Corpse attachment, pose preparation and visual updates now run in order; compatible NPC/player corpses inherit the existing render proxy and secondary world, including EEER replacements. [Validation](docs/MENU_DEATH_HANDOFF_VALIDATION.md).

**2.1.0-actors-preview.14** fixes RTX menu previews and the folder destination submenu. Local MMD ragdolls are tracked independently of network entities, pause secondary simulation while their Source bodies sleep, and can be removed with **Clean up local MMD ragdolls** or `mmdhl_cleanup_client_ragdolls`. Debugging now defaults to **3840×2160 with Workshop addons enabled**. [Validation and client APIs](docs/RTX_PREVIEW_CLIENT_RAGDOLLS.md).

**2.1.0-actors-preview.13** supports the validated Windows x64 GMod RTX Remix client. Its fixed-function shader library is recognized, and overlapping materials use a small render-surface separation instead of projection-depth changes that made Remix lose hair, gloves and body layers. Normal GMod retains its existing rendering path. No model reimport is needed. [RTX compatibility and validation](docs/RTX_REMIX_VALIDATION.md).

**2.1.0-actors-preview.11** fixes black lighting on ledges and unwanted player/hand renderers. Your own body is hidden and its secondary physics suspended in first person. Physics accuracy **0** selects lightweight jiggle motion; **−1** disables secondary motion. Toggle/reset shortcuts are configurable and unassigned by default; F8 no longer resets physics. Full physics still defaults to 10. [Changes and validation](docs/PLAYER_LIGHTWEIGHT_PHYSICS_VALIDATION.md).

**2.1.0-actors-preview.10** restores the full Source map-light list, makes library spawns face the player, and adds **Q → External Models → Character Models** with saved folders and four direct spawn actions. Physics choices now use plain names, including **Multicore CPU Processor**; existing settings and backend IDs are preserved. No model reimport is needed. [Changes and validation](docs/MAP_LIGHTING_LIBRARY_VALIDATION.md).

**2.1.0-actors-preview.9** fixes cold duplicator/save restoration, full appearance state and Ragdoll To NPC integration. Stacked face triangles and full material overlays now receive correct flashlight lighting while retaining their geometry, morphs and alpha masks. The guard defaults on; no model reimport is needed. [Implementation and measured validation](docs/PERSISTENCE_LIGHTING_VALIDATION.md).

**2.1.0-actors-preview.8** adds the +2.4-unit NPC/player origin, Sandbox NPC weapon selection, configurable F8 reset-all, complete library/catalog deletion and corrected third-person eye targeting. First-person body is now off and hidden from the menu. Parameter-based anti-stretch has been removed; optional stretch projection now respects ground and prop contacts. [Changes and validation](docs/QOL_ACTOR_PHYSICS_VALIDATION.md).

**2.1.0-actors-preview.7** fixes the remaining Furina-style actor distortion by retaining fitted bind rotations through Source IK and mapping three-segment MMD torsos completely. Verified against the supplied compiled Furina in Reference and idle poses. Respawn actors/reapply player models; PMX caches are reused. [Furina comparison and regression results](docs/FURINA_BIND_VALIDATION.md).

**2.1.0-actors-preview.6** corrects NPC/player proportions and forward reference poses, preserves each NPC's fitted death-ragdoll physics, and excludes living players/NPCs from secondary contacts in single-player. Spawn fresh actors and reapply player models to generate the corrected carriers; no PMX reimport is needed. [Compatibility fixes and validation](docs/ACTOR_COMPATIBILITY_FIXES.md).

**2.1.0-actors-preview.5** restores previews and world rendering after the September 22 game-library update. Failed compatibility checks are cached instead of hashing engine DLLs every frame, and the UI retains the actual error message. [Single-player fix and measurements](docs/SINGLEPLAYER_RENDER_VALIDATION.md).

Import PMD/PMX models directly into Windows x64 Garry's Mod. The native Source backend generates cached carriers without Blender or `studiomdl`. Ragdolls retain 18 native collision bodies and 17 anatomical joints, with authored MMD secondary physics. This preview adds Citizen/Combine NPCs, player models, hands, alpha-zero Show bodygroups and adaptive physics. Two real Windows clients have passed the dedicated-server functional checks. Physics performance is accepted/addressed for this rollout; see the [validation and compatibility coverage](docs/ACTORS_IMPLEMENTATION.md). Shared-model transfers now use compression, pipelining and verified local material generation: [transfer results](docs/TRANSFER_VALIDATION.md).

The native carrier has been the default since the checks in [the original native validation report](docs/NATIVE_VALIDATION.md). Version 0.3.1 migrated the prototype's saved backend setting once. An explicit `options.backend='legacy'` remains available to diagnostic callers. Existing raw model and texture data is reused. Historical results are recorded in [the 0.5.0 compatibility validation report](docs/COMPATIBILITY_VALIDATION.md); current preview results are in [the actors report](docs/ACTORS_IMPLEMENTATION.md).


Version 0.3.2 corrected the SCMI-to-renderer joint frames, bounds fitted limbs to their MMD joint lengths, enables native self-collision, and uses the corpus toon/Phong/rim-light material recipe. Restart GMod and place fresh ragdolls to generate corrected carriers; cached PMX and textures are reused. See [anatomy and material validation](docs/ANATOMY_VALIDATION.md).

Version 0.4.0 uses SCMI's import ratio instead of forcing every character to 72 units. The raw PMX scale is `0.08 × 40.457 = 3.23656` Source units per PMX unit, with an adjustable multiplier. The visible mesh follows the same client bone palette used by Source tools. A bundled anatomical atlas fits real convex shapes without needing the development corpus. See [scale, alignment and collision validation](docs/SCALE_ALIGNMENT_VALIDATION.md) for measured results and remaining accuracy limits.

Version 0.5.0 repairs invalid authored joints and thin collision hulls, adds native eye pivots, complete material and visibility interfaces, original morph labels, import progress/cancellation, and optional one-way contacts between secondary physics and Source geometry. The addon no longer limits placement to five characters.

Version 0.6.0 uses short, readable material names in Source tools, permanently deletes model caches, replaces advisory input budgets with warnings, and adds recoverable secondary-physics reset. See [QoL validation](docs/QOL_VALIDATION.md). Development cache and duplicate compatibility is not a release requirement.

Paths use 16-character identifiers and meaningful names such as `mmd/<id>/face_1` and `hair_19`. Common material terms are translated; other names are romanized for Source compatibility. Editors retain the original authored names. This Windows build uses the system ICU library available on Windows 10 1903 or newer.

Version 0.7.0 separates the 60 Hz simulation clock from rendering, interpolates secondary poses between ticks, and shares a persistent worker pool across characters and deformation. Color, flashlight, depth and shadow passes reuse material ranges in the same vertex buffers. Generated Source materials use alpha testing with alpha-to-coverage; explicit material overrides retain their own shader settings. See [motion and performance validation](docs/MOTION_PERFORMANCE_VALIDATION.md) for measured results and remaining limits.

The 0.8.0 QoL update makes collision mode, secondary backend and spawn freezing global across models. Spawn freezing defaults off and affects new placements only; changing collision/backend also updates existing MMD ragdolls without changing native limb freezes. Model size remains individual. Q → MMD and Utilities → MMD models expose the shared controls. Enable the optional performance overlay there, or use `mmdhl_debug_toggle` (for example, `bind F7 mmdhl_debug_toggle`). `mmdhl_debug_print 1` prints a summary every five seconds; `mmdhl_debug_report` saves a JSON snapshot under `data/mmd_hotloader/diagnostics`. See [global settings and telemetry validation](docs/GLOBAL_SETTINGS_VALIDATION.md).

Version 0.8.0 adds a selectable **CPU multicore** secondary-physics backend in the importer and ragdoll editor. It retains the complete rigs, fixed 60 Hz stepping and 10 solver iterations. Ten full character replays match the independent nanoem reference with zero body-position difference. The existing CPU backend remains the default. PMX soft bodies automatically use that reference backend.

An **experimental OpenCL constraint solver** is also available for development. It is slower on the tested RTX 5090 and fails both contact and full-rig motion fidelity checks; dense rigs can move substantially differently. It is not a validated full GPU dynamics backend. Collision detection, row generation and integration still run on CPU. See [backend implementation and measured results](docs/COMPUTE_BACKENDS.md) before selecting it.

Version 0.7.1 specializes Bullet's DBVT broadphase traversal without changing overlap-pair order, the 60 Hz timestep, solver iterations or authored rigs. It preserves nanoem replay results and improves most tested frame-time comparisons. The proposed sweep-and-prune backend failed fidelity validation and remains a diagnostic experiment. See [broadphase validation](docs/BROADPHASE_VALIDATION.md) for the controlled, addons-enabled measurements and remaining performance misses.

## Install and use

Subscribe to the current Workshop addon, then install the matching GitHub native
package below. **2.1.0-native.1** introduces automatic installation verification:
missing, damaged, mixed or obsolete binaries appear in External Models, with
repair instructions. Working features remain available when an optional component
fails. See [installation checks and release workflow](docs/INSTALLATION_VALIDATION.md).
For a local development checkout without Workshop, add `-InstallAddon`.

**Native package from GitHub Actions.** Actions → Build drop-in package → **Run
workflow** (on any branch) builds and tests the native modules and uploads
`Model-Hotloader-<release>-<commit>-win64-vulkan` (with the DXVK renderer) and
`Model-Hotloader-<release>-<commit>-win64-opengl-remix` (without it) under the
run's Artifacts. Each holds one `GarrysMod` folder with only the native files
(`garrysmod/lua/bin`, `garrysmod/shaders/fxc`, `bin/win64`); the Lua addon
comes only from Workshop.
To release it: set a new `MMDHL_RELEASE` in `CMakeLists.txt`, run the workflow,
publish the artifact as a GitHub release, then run
`./scripts/update-native-policy.ps1 -RunUrl <run link> -ReleaseUrl <release link> -AltReleaseUrl <mirror link>`
(PowerShell 7; downloads the artifact with a logged-in `gh`, `GITHUB_TOKEN`, or
`-Artifact <downloaded zip>`). It writes the build's record to
`addon/lua/mmdhl/native_policy.lua` as the approved, recommended release. Players who cannot reach GitHub
see the mirror link and an **Alternative download** button in the installation
banner. Then publish the Workshop update.

Close GMod and run:

```powershell
./scripts/install.ps1 -GameRoot 'H:\SteamLibrary\steamapps\common\GarrysMod'
```

Native DLL changes require a full restart. Open **Q → MMD**, click **Import PMX / PMD**, and select a model. Its textured preview appears beside the library: drag to rotate, scroll to zoom, right-drag to pan, or choose **Face**. Set the size multiplier, frozen option and secondary collision mode, aim at nearby ground, then click **Place at aim point**. The menu shows loading/error messages and closes after successful placement. `mmdhl_open` opens the same library in a separate window.

- Search by name or source path; rename and favorite entries. Ctrl/Shift selects several models.
- **Delete from library** permanently removes the imported asset, matching ragdolls, generated carriers, fits, settings, and textures unused by other models. There is no Deleted tab. Original PMX/PMD files are kept. If Windows holds a mounted archive open, the result reports deferred files and cleanup finishes on the next game start.
- **In this map** lists spawned ragdolls, with editing, freezing and removal controls. Removing a ragdoll keeps its library entry. The detailed editor includes expressions and collision-fit controls.

See the [library and placement checks](docs/LIBRARY_VALIDATION.md) for the 1440p/4K regression results.

- Physics Gun operates on the native collision bodies, with per-limb freezing. Welds, ropes, collisions and damage impulses use Source physics. Gravity Gun behavior follows stock flesh ragdolls: normal pickup is rejected; the Super Gravity Gun can pick up and launch them.
- Face Poser exposes up to 96 stable controllers. Labels use recognized English names or the original authored name when a translation is uncertain. Its **MMD overflow** section adds the remaining morphs, with complete-face copy, reset, paste and saved presets. Original names appear in tooltips.
- Finger Poser, Eye Poser, Color, Material, submaterials, undo, cleanup, Remover and Duplicator use the native carrier with attached MMD state. Native `Eye_L`/`Eye_R` bones also support Enhanced Entity Expression Response, including zero eye-position scales.
- **C → Bodygroups** uses **Hide <material>** for ordinary parts and **Show <material>** for authored alpha-zero parts. State 0 preserves the authored appearance; state 1 hides an ordinary part or reveals an alpha-zero part. Physics is unchanged. Material slots include the complete PMX inventory. [The AME integration](integrations/README.md) adds all slots, readable shader parameters, textures, UV geometry and the visible MMD preview to Advanced Material Editor.
- The editor's collision inspector shows body hulls, joint frames, limits and fit confidence. Inspect the actual engine collision in cyan, exclude unsuitable material regions, or adjust a body in yellow, then **Save fit and spawn corrected copy**. Corrections apply to later imports of that asset; the original remains available for comparison.

Duplicates reference local cached assets; they do not distribute models or textures. Source handles the primary limbs; hair, clothing and accessories settle in an independent nanoem world. **Everything except living players / NPCs** is the default: map geometry, props, vehicles and other ragdolls affect the secondary simulation. Living players, NPCs and NextBots are excluded. **Model only** keeps authored internal collisions; **Model + map / static geometry** omits moving objects. These contacts affect only MMD secondary physics and never push Source objects; the character's own carrier is excluded. Freezing a limb does not freeze secondary motion.

Aim at an MMD ragdoll and run `mmdhl_reset_physics`, or use **Reset MMD physics** in its context menu / **In this map**. `mmdhl_reset_physics all` resets every MMD character. To assign a key, enter `bind "F6" "mmdhl_reset_physics"` in the console. No existing keybinding is overwritten by installation. Reset rebuilds only secondary simulation at the current pose, retaining native limb poses/freezing, scale, morphs, materials and collision mode.

Large files, textures and high model counts produce warnings and import at full detail. The 256 MB read cap, 4 GB worker memory ceiling and automatic five-minute cancellation have been removed. Cancel remains available. Malformed data, invalid references and actual file-format limitations still receive specific errors or warnings.

## Implementation

The carrier has SCMI's 56 essential/finger bones plus available PMX eye bones, attachments, material slots and flex metadata. VPhysics creates and serializes the fitted convex collision; the addon writes matching MDL/VVD/VTX metadata and mounts an immutable GMA. Deterministic fitting uses anatomical landmarks and robust weighted vertices, with portable normalized references and supporting-plane regression calibrated on 159 validated model pairs. No corpus path is accessed at runtime. The rig manifest preserves mappings to original MMD names and indices.

The update order is Source pose → primary MMD bones → nanoem secondary physics → deformation → Source rendering. The pinned nanoem backend runs at 60 Hz with bounded catch-up. Independent character worlds run in parallel; graphics calls remain on the render thread. All authored rigid bodies and valid joints are retained. Malformed constraints are skipped with named warnings; no detail is removed to meet frame-rate targets.

Rendering uses `VertexLitGeneric`, native model lighting, texture alpha, material morph color/alpha, engine material overrides, flashlight depth passes and deformed sunlight shadows. Reusable vertex buffers upload only changed poses and keep index data on the GPU. The render entity contains no collision; the `prop_ragdoll` remains the interactive entity. See [architecture and API](docs/NATIVE_BACKEND.md), [debugging](docs/DEBUGGING.md), and [validation](docs/NATIVE_VALIDATION.md).

## Limits

This targets Windows x64 GMod and the validated RTX configuration. Private VPhysics/render interfaces validate Workshop-delivered ABI profiles. A new game DLL hash is accepted when its checked code/data and interface contracts still match; other changes require ABI review and an updated Workshop profile or native release. The **2.1 actors preview** adds Citizen/Combine NPCs, player models, hands, adaptive physics, and approved asset sharing. See [implementation and validation status](docs/ACTORS_IMPLEMENTATION.md) before installing: the dedicated-server tests used two real clients on one Windows host over LAN; other network topologies and broad camera/weapon-addon compatibility are not validated. VMD playback remains the responsibility of animation addons.

MME effects are not executed. Generated materials use alpha testing and alpha-to-coverage (when MSAA is enabled), and Source shading is not pixel-identical to MMD; uncommon sphere/toon/edge material effects are not reproduced. Unusual rigs can need collision corrections. The full internal bone inventory is available through metadata, but only carrier bones are exposed through native Source bone functions. PMX soft-body coverage is documented separately from the supplied characters' rigid-body clothing rigs.

Native rendering temporarily requires `mat_queue_mode 0`; the previous value is restored when the last model/preview is closed or removed and at normal shutdown. The owned test launcher also backs up and restores configuration files. There is no addon character-count cap. Physics performance was accepted by the user for this rollout; the historical measurements remain in the actors report and are not universal frame-rate guarantees.

## Build and verify

Requirements: Python 3, VS 2022 C++ Build Tools, Windows SDK/CMake, and the supported GMod installation. Dependencies are pinned.

```powershell
./scripts/build.ps1
./scripts/game-start.ps1 -Visible
python scripts/prepare-acceptance.py
python scripts/test-carrier.py
python scripts/test-native-anatomy.py
python scripts/test-native-tools.py Xin
python scripts/test-library-ui.py
python scripts/test-native-extras.py
python scripts/test-compatibility.py imports
python scripts/test-compatibility.py materials
python scripts/test-compatibility.py eyes
python scripts/test-compatibility.py morphs
python scripts/test-secondary-scenes.py
python scripts/test-import-feedback.py
python scripts/test-native-stability.py Xin
python scripts/test-native-stability.py stress
python scripts/test-native-stability.py cycles
./scripts/game-stop.ps1
```

The visible launcher defaults to 2560×1440 with enabled folder and Workshop addons. `-Isolated` is diagnostic only. See the debugging guide for map changes, captures, independent nanoem replay, and owned-process crash dumps.

Project code is MIT except the MPL-2.0 synchronization/reference files identified in [THIRD_PARTY.md](THIRD_PARTY.md). Release packages include their source and applicable dependency notices. No user models, corpus textures, MMD executable or engine DLLs are distributed.
