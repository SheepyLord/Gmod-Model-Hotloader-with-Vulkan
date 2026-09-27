# Static props validation — 2026-09-24

The static-prop importer from the GModel Hot Loader (1.0.6, commit `02341db`) was merged into this addon. Validation covered its native behavior after the merge, our worker and game modules, and real Garry's Mod sessions on Windows x64: single-player, then an owned dedicated server with two real clients. Other Workshop addons were disabled (`-NoWorkshop`); local legacy addons stayed installed.

## Native

| Suite | Result |
|---|---|
| `mmdhl_props_tests` (the loader's native suite, ported) | 203 checks passed: cache round trip and hashing, truncation and corruption, hull validation and tracing, fast hull, alpha classification, texture resolution, render plan and winding |
| Existing suites after the merge | `mmdhl_tests` 136, `mmdhl_async_tests` 29, `mmdhl_timing` 46, GPU skinning 13, broadphase 14, actors, quality, multicore compute and render-guard all pass |

## Worker import corpus

The same models the GModel loader validated were used: `F:\WSL_BKUP\ODBKUP\Desktop\organize\models`. Each was run through `mmdhl_worker.exe --request` with `kind=static`; 18 of them are shown below. **All of them imported.** A PMD file is rejected by design with the message "Choose OBJ, FBX, GLB, GLTF, or PMX" (now "Choose OBJ, FBX, GLB, GLTF, PMX or BLEND", and the failure window offers **Import as character**); the static importer reads PMX only, and the picker's default filter lists only the supported types.

| Model | Format | Triangles | Fast hull: total s | Detailed: total s | Detailed hulls |
|---|---|---:|---:|---:|---:|
| alchemical_potion | GLB | 119,984 | 0.76 | 16.25 | 6 |
| perfume_bottle | GLB | 141,602 | 0.13 | 4.03 | 7 |
| notebook | GLB | 1,386 | 2.02 | 4.63 | 5 |
| red_mailbox | GLB | 1,468 | 0.52 | 2.06 | 2 |
| steam_machine + controller | GLB | 160,927 | 0.62 | 3.64 | 5 |
| iPadPro | FBX / GLB / OBJ | 77.3–77.5 k | 0.79 / 0.77 / 0.87 | 2.9 / 3.7 / 3.0 | 2 |
| Banner | FBX | 12,612 | 0.51 | 2.93 | 6 |
| CH-47 Chinook | GLB | 15,386 | 0.53 | 6.76 | 16 |
| Challenger 2 | GLB | 169,424 | 0.71 | 9.07 | 16 |
| J-10S | GLB | 73,949 | 0.92 | 9.26 | 16 |
| F-16D | GLB | 61,543 | 0.75 | 18.88 | 16 |
| books | FBX (57 MB) | 16,956 | 5.28 | 9.45 | 15 |
| Afghani flag | FBX | 21,104 | 0.10 | 4.42 | 12 |
| mailbox scan | OBJ (39 MB) | 514,003 | 2.36 | 20.83 | 16 |
| PS5 | OBJ | 12,580 | 0.08 | 7.56 | 16 |
| SH-60B Seahawk | OBJ | 63,968 | 0.18 | 5.64 | 16 |
| cloth21 fixture | PMX | 33 | 0.05 | — | — |

The fast single hull took about 15 ms per model. The three iPad formats import at the same size (25.3 × 1.1 × 30.5 units). OBJ unit detection converted the metre-scale OBJ files (iPad, mailbox scan, PS5) and added a warning to each. The Chinook and tank GLBs are authored in centimetres, so they need **Import scale**.

## Game — single-player (`gm_construct`, 1920×1080)

The session ran through the real library job flow; only the Windows file dialog was bypassed.

| Check | Result |
|---|---|
| Import `alchemical_potion.glb` | Completed in 0.7 s. Library entry shows 119,984 triangles and 1 hull; status and notification shown |
| Spawn at aim (Prop size 4) | Server prop with one convex, 26.5 kg, resting on the ground. Client render ready: 46 opaque and 192 sorted translucent pieces |
| Materials and lighting | Glass, liquid, cork and metal render with world lighting. Brightness matches a stock `oildrum001` beside it |
| Dark-room check | With the flashlight off, the prop is as dark as the stock drum. With it on, both are lit by the flashlight |
| **Static Props** subtab | Heading and **Import Static Props** switch with the tab. Folders, list, studio-lit preview, collision overlay, Prop size, Spawn Prop and Apply size to aimed prop all work; import options expand |
| Five more imports queued | red_mailbox GLB 0.5 s, PS5 OBJ 0.1 s, iPadPro FBX 0.8 s, steam machine GLB 0.6 s, Evernight PMX from a Unicode path 5.3 s (286,329 triangles, character scale) |
| Apply size to aimed prop (4× → 8×) | Collision volume ×8, mass kept, position kept |
| Duplicator copy/paste | Asset, scale, mass and frozen state restored |
| Reimport with detailed collision | 2.1 s. Both mailbox props in the map moved onto the new two-hull import; the library entry kept its settings and the old entry is hidden |
| Delete from library | The prop left the map; its bundle and library entry were removed |
| Panel **Spawn Prop** | Placed at 3×, remembered the size and closed the menu |
| Character Models regression | 21 characters listed and the native preview renders; tab switching works |

## Game — owned dedicated server and two real clients

Run with `scripts/test-props-multiplayer.py` on `red_mailbox.glb`; all 9 checks passed.

1. The two real clients have separate privileges: client1 is superadmin, client2 is a player.
2. Client1 imports the file (1.31 s).
3. Client1's first **Spawn Prop** uploads the bundle; the server validates and approves it, broadcasts the catalog, and places the prop (18.98 s in total).
4. The server has the approval, the cached bundle and a prop with valid physics at 4×.
5. Client2 receives the collision first, then downloads, verifies and draws the prop (16.66 s). The prop appears in client2's library as shared.
6. Client2's traces hit the prop through the native hull test.
7. Non-admin client2 spawns the approved prop.

The 10.7 MB bundle moved at 0.57–0.59 MiB/s on the existing model transfer, with 48 KiB chunks and a 144 KiB window at the clients' `fps_max 60`.

## Update — Blender files, tool gun, part presets, bodygroups (2026-09-24)

### Native

`mmdhl_props_tests` now runs 340 checks. The new `blend_tests.hpp` adds:
- A Zstandard vector produced by libzstd at level 19 (two blocks, Huffman literals, FSE tables, repeat offsets), plus truncation, the output limit and 200 corrupted variants.
- Synthetic `.blend` files written by the test in both header layouts (`BLENDER-v305` and `BLENDER17-01v0500`) and wrapped in a Zstandard frame. They cover Euler and quaternion transforms, parenting, a mirrored object, a concave n-gon, a hidden object, material slots, and two meshes whose data blocks reuse the same addresses. A mutation that resolves addresses file-wide instead of per ID fails the test.

All 20 registered native suites pass.

### Blender corpus

Every mesh object was imported on its own through `mmdhl_worker.exe --request` and its bounds compared with Blender 5.2's own `matrix_world` bounds for that object.

| File | Contents | Result |
|---|---|---|
| `transforms.blend` (5.2, zstd) | Euler XYZ with non-uniform scale, Euler ZXY, quaternion with mirrored scale, a child of a rotated parent, a torus with a Subdivision modifier and delta transforms | All 5 objects within 1e-5 units; triangle counts equal; every closed mesh has positive volume (outward winding); the modifier is reported |
| `multi.blend` / `packed.blend` (5.2) | Textured crate (external / packed PNG), red sphere, translucent child cone, hidden ground | Within 1e-5 units; texture found both ways (same hash); alpha 0.4 blends; the ground is skipped by default |
| `big.blend` (5.2, 23.5 MB zstd) | 1,007,616-triangle mesh, 40 small meshes, packed 1024² texture | Listing 0.31 s, full import 1.61 s; all 41 objects within 1e-5 units |
| Blender test-suite files | 2.60 gzip 32-bit pointers (tessellated faces), 3.0 gzip, 3.0/3.2 zstd (positioned cubes, textured cubes, PBR and shader materials, Suzanne), 5.0 | All files with faces import; the 2.60 cube has exactly 8 m³. The 3.0 gzip file holds only an edges-only mesh and is refused with "no faces (only vertices or edges)". Missing external images in the test-suite files are reported per material |

400 randomly corrupted or truncated `.blend` files (uncompressed, gzip and zstd) all ended in a clean error or an import; none crashed.

### Game (`gm_construct`, single-player)

| Check | Result |
|---|---|
| `武器.pmx` (failed before with "Degenerate collision face") | Imports with one hull |
| `林珑.pmx` imported as a static prop | "Character model?" prompt (731 bones, 599 physics bodies) offering **Import as character** |
| Import of a broken FBX | Failure window with file, step ("Reading geometry"), error, hint, **Retry** and **Copy details** |
| `.blend` with four meshes | The object window opened with Ball, Crate and Hat ticked and the hidden Ground unticked; **Import 3 objects as one prop** imported and placed it with the checker texture and red sphere |
| `.blend` with an edges-only mesh | Failure window with the "no faces" error and Blender conversion hint |
| Library layout | Tabs beside **Refresh** and **Import**; coloured primary actions; no clipped text in the action panels or dialogs |
| **Edit parts…** | Material list with triangle counts and a live clipped preview; the "Linlong · Upper body" preset saved in 1.3 s as its own library entry |
| **Static Prop** tool | HUD and ghost; left click placed the preset with physics (73 kg) |
| Attach | Right click on an `npc_citizen` opened the bone panel; the prop followed `ValveBiped.Bip01_R_Hand` |
| Duplicator | Copying a barrel with an attached prop pasted both and attached the prop again |
| **Edit bodygroups…** | A preset hiding slot 13 set as default; a newly spawned ragdoll had that bodygroup hidden |

## Update — texture wrap, PBR glossiness, notes (2026-09-24)

`challenger_2_mk.3_main_battle_tank.glb` rendered as smeared stripes. Every V coordinate in the file is between 1.002 and 1.996 (the sampler repeats), and PNG textures loaded by `Material()` without `noclamp` clamp. All UVs read the texture's last row. Its materials (`KHR_materials_specular`, roughness 1.0) also became full-strength white Phong at exponent 1, a flat sheen over the whole hull.

| Check (`gm_construct`, single-player) | Result |
|---|---|
| New import of the tank (0.7 s), placed at 0.25× | Camouflage, markings, wheels and tracks textured correctly |
| Material conversion | Roughness 1.0 → no Phong on four materials; the 0.6-roughness material gets exponent 13.4, boost 1 |
| **Glossy highlights** off | Every prop rebuilds with matte materials (`$phong 0`) at once; on restores them |
| The user's earlier import of the same tank (no PBR fields) | Renders the same way, converted from its stored values; no reimport |
| Lailai (7 joints linking a body to itself) | Grey "7 note(s)" bar; the details window lists them under "Notes (no action needed)" |
| Classification | Missing textures, out-of-range joint bodies, large models, OBJ unit guesses and high-detail geometry stay warnings |

All eight characters in the library that had messages had only notes, so none shows the orange bar now.

## Update — collision levels and gravity (2026-09-24)

A 435-unit tank prop on `gm_construct` was switched through every level. At each level, an oil drum was dropped onto it, the player was sent across it in mid-air at 700 units/s, and it was raised 100 units and released.

| Level | Drum | Player | Falls when released | Physics collisions / group |
|---|---|---|---|---|
| Nothing | Falls through to the ground | Passes | No (0 units) | Off / `WORLD` |
| World only | Falls through | Passes | Yes (93 units) | On / `WORLD` |
| Except players and NPCs | Lands on top | Passes | Yes | On / custom rule |
| Except players | Lands on top | Passes | Yes | On / custom rule |
| Everything | Lands on top | Blocked at the near side | Yes | On / `NONE` |

NPCs: an `npc_citizen` sent across walked straight through under *except players and NPCs*, and walked around the prop under *except players* and *everything*. Dropped on the NPC, the prop passed through it under *except players and NPCs* (health unchanged) and hit it under the other two. The player's eye trace hit the prop at every level.

| Check | Result |
|---|---|
| Context menu **Collides with ▸** (client → server) | Mode applied; the client enabled the same custom rule (prediction agrees) |
| Context menu **Gravity** | Toggled the prop's gravity |
| Sandbox Collision / Gravity toggles on a static prop | Hidden |
| **Nothing**, then back to **World only** | Gravity off, then restored |
| Resize while **Nothing** | Collisions and gravity stay off |
| Duplicator copy/paste | Level and gravity kept (including *except players and NPCs* with gravity off) |
| Copy made before the update | Keeps colliding with everything |
| Spawn Prop with default settings | World only, gravity on; the server reads the tool's shared settings |
| Panel | **Collides with ▾** and **Gravity** fit on one row; choosing **Nothing** greys out and unticks Gravity without changing its saved value |

## Known limits

- Props do not cast render-to-texture shadows. The prop entity draws hidden per-piece mesh proxies, and the engine's shadow pass never calls that draw: instrumentation saw only normal render flags (9).
- Source Phong is not PBR; metallic/roughness and toon/sphere shading are approximations.
- OBJ has no units. The under-8-units-means-metres rule can misjudge very small inch-based models; **Import scale** corrects them.
- The detailed collision takes up to about 20 s on the corpus and has no progress percentage (CoACD gives none). The default fast hull is instant.
- `.blend` import does not evaluate modifiers, armature poses, shape keys, curves, text or instanced collections, and reads only the shader nodes that feed the base colour, alpha and normal map. Apply or convert in Blender, or export GLB for full fidelity.
