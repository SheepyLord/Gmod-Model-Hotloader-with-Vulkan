# Static props

**Q → External Models → Static Props** imports OBJ, FBX, GLB, GLTF, PMX and Blender (`.blend`) files as physics props. It sits beside **Character Models** in the same library panel and follows the same flow: **Import Static Props**, pick a file, watch the progress banner, preview, then **Spawn Prop** (or double-click) to place it where you aim, facing you. Everything runs inside GMod. The bundled worker parses the file, so no Blender, `studiomdl` or separate importer install is needed; `.blend` files are read directly.

The importer is ported from the GModel Hot Loader project (its "Build binary model loader" session, 1.0.6). The same parser, cache format, fast collider, material analysis and render plan are reused here. The character importer's infrastructure provides the rest: the worker process, file picker, progress banner, library UI, folders, sharing and placement.

## Using it

- **Import Static Props** opens the Windows file picker. The window may open behind a full-screen game; a notification says so. The import starts as soon as a file is chosen, using the **Import options** below the spawn buttons.
- **Blender files** list their meshes first. With more than one, a window asks which to import; the ticked meshes become one prop and keep their positions from Blender (see [Blender files](#blender-files)).
- **Character or static?** A PMX with a humanoid skeleton (head, arms and legs among its bones) imported here gets a prompt offering **Import as character**; a PMX imported under Character Models without that skeleton is offered **Import as static prop**. Rigged FBX/glTF/Blender characters import as a statue in their bind pose, with a note saying so.
- **Warnings and notes.** The bar above the preview turns orange only for warnings that may affect the result (missing textures, broken joints, very dense models, OBJ unit guesses). Messages that need no action are notes and keep it grey: joints that link a body to itself or join two world anchors (MMD ignores them too), large textures, and the notices that Source shading approximates glTF or PMX materials. Clicking the bar lists both groups.
- **When an import fails**, a window names the file and the step that failed, gives the error and what to try, and offers **Retry** (or **Import as character** for a PMD), **Copy details** and **Close**.
- The list supports search, folders, rename, favorites, multi-select and delete, like the character list. Delete removes the cached prop and its unused textures, and removes your props of that model from the map. Original files are never touched.
- The preview is studio-lit. Drag rotates, the wheel zooms, right-drag pans, and **Show collision** overlays the collision hulls.
- **Prop size** (0.05–20×) scales new placements uniformly, affecting rendering and collision together. **Apply size to aimed prop** resizes the prop under your crosshair and keeps its position, motion, mass and Weld/NoCollide constraints. **Freeze new props in place** spawns them frozen.
- **Import options** apply to the next import and to **Reimport selected with these options**:
  - Up axis: automatic, Y up or Z up.
  - Turn: 0, 90, 180 or −90 degrees.
  - Collision: fast or detailed.
  - An extra import scale.

  A reimport keeps the library name, folder and favorite. In single-player it also moves props already in the map onto the new import.
- Props are ordinary Sandbox props for gameplay purposes:
  - They obey `PlayerSpawnProp` and the prop limit, and have undo and cleanup entries.
  - The Physics Gun, tool gun and bullets hit their collision shape on both server and client.
  - Duplicator and saves record the model ID, size, mass, frozen state and surface material.
  - Spawned props are titled "Static prop" in the undo list.
- **Collides with** and **Gravity** set how new placements behave (see [Collision and gravity](#collision-and-gravity)). The default is **World only**.
- **Surface material** sets what new placements are made of: Default, Wood, Metal, Bouncy metal, Concrete, Glass, Plastic, Rubber, Flesh, Ice, Paper, Dirt, Gravel, Foliage, Cardboard, Porcelain, Carpet, and Garry's Mod's Frictionless ice, Very bouncy and Silent. It sets their impact sounds, bullet marks, friction and bounce. The Static Prop tool shares the choice. Resizing, reimport, attaching and detaching, the duplicator and saves keep a prop's material, including one set with Sandbox's Physical Properties tool.
- **Place with tool gun** equips the **Static Prop** tool with the selected prop (see [Tool gun](#tool-gun)).
- **Edit parts…** makes part presets: pieces of the model kept by material or by a 3D region (see [Parts presets](#parts-presets)).
- In multiplayer, an administrator's first **Spawn Prop** shares the prop with the server automatically; **Share prop with server (admin)** does it explicitly. Other players download shared props on demand through the existing model transfer (compressed, pipelined, SHA-256 checked, and decoded and validated before use). Their collision arrives first, so traces work while the rest downloads.

## Collision and gravity

Each placed prop has a collision level and a gravity setting. New placements take them from **Collides with** and **Gravity** in the Static Props panel. The Static Prop tool shares the same two settings. To change a prop already in the world, hold **C** and right-click it: **Collides with ▸** lists the levels (the current one is ticked), and **Gravity** toggles.

| Level | Collides with | Players and NPCs |
|---|---|---|
| Nothing (no gravity) | Nothing, not even the world. Gravity is off, and throws come to rest. | Pass through |
| World only (default) | The map | Pass through, and so do other props |
| Everything except players and NPCs | The map and other props | Pass through |
| Everything except players | The map, props and NPCs (NPCs walk around it) | Players pass through |
| Everything | Everything | Blocked |

The Physics Gun, tool gun and bullets still hit the prop at every level, so it can always be grabbed, shot or edited. At the levels players pass through, the prop a player aims at while holding primary or secondary attack is solid to that player: the engine traces of the Physics Gun, bullets and the gravity gun start at the player and would otherwise pass through as well. A Physics Gun that carries something passes through again, and players walking or aiming elsewhere are not affected. Switching from **Nothing** to another level restores the prop's gravity setting. Levels and gravity survive resizing, reimport, detaching, the duplicator and saves. Copies made before these settings existed keep colliding with everything. On static props, Sandbox's own Collision and Gravity context-menu toggles are replaced by these two entries.

## Tool gun

The **Static Prop** tool (Construction category) places and attaches imported props. **Place with tool gun** in the library selects the prop and equips the tool; the tool's own menu also lists the library with a search box.

| Input | Action |
|---|---|
| Left click | Place the prop where you aim, standing on the surface and turned toward you. A ghost shows where it will go |
| Right click | Attach it to the character, NPC, ragdoll or prop you aim at. A panel opens with the target's bones (nearest preselected, searchable), offset, rotation and size sliders, and a live ghost; **Attach** confirms. Right-clicking an attached prop reopens the panel to adjust it |
| Reload | On an attached prop: detach it (it becomes a normal physics prop). On any static prop: copy its model and size into the tool |

The menu sets size, turn, freeze, collision level, gravity, surface material and colour for new placements. Attached props follow the bone (`FollowBone`), have no physics of their own and do not collide. They are removed with their target, and the duplicator and saves copy them with the target and attach them again on paste. Attaching to another player's entity follows the tool permissions (`CanTool`); other players' characters need an administrator.

## Parts presets

**Edit parts…** opens the model with a list of its materials, each with its triangle count. Untick materials to drop them (**Show all**, **Invert**, **Hide all**), and optionally tick **Keep only the region inside the box** to cut the model with a box set by six sliders (X, Y and Z from/to, as a percentage of the bounds); triangles whose centres are inside are kept. The preview updates live, with the box drawn and clip planes applied.

**Save preset** stores the selection under a name as its own library entry ("model · preset"), with its own collision (fast or detailed) built from the kept triangles. The original import is never changed. Presets are placed, shared and duplicated like any prop. Opening a preset with **Edit parts…** shows its parent with the preset's selection; **Save changes** rebuilds it and **Delete this preset** removes it.

## Blender files

`.blend` files are read without Blender. The reader uses each file's own structure catalogue, so files from Blender 2.60 through 5.x load, uncompressed or compressed with gzip (Blender 2.x) or Zstandard (Blender 3.0+, the default in 5.x).

- **What is imported:** mesh objects with their object transforms (location, all Euler orders, quaternion and axis-angle rotation, delta transforms, parenting and mirrored scales), faces of any size (n-gons are triangulated), flat/smooth shading, the render UV map and material slots (object-linked slots win).
- **Materials:** the base colour, alpha and normal map of a Principled BSDF (or the colour of a Diffuse, Glossy, Emission or similar shader) feeding the material output, including an image texture reached through mix or colour nodes. Images packed into the file are used directly; external images are found relative to the `.blend` (`//` paths) or by name in the usual texture folders, never outside the model's folders ([the rule](CHARACTER_IMPORT.md)). Materials without nodes use their viewport colour.
- **Choosing objects:** the importer first lists the meshes. With more than one, a window shows each with its triangle count, materials, parent and state; objects hidden in Blender (viewport or render), excluded collections and objects outside the scene start unticked. **Select all**, **Visible in Blender** and **None** set the ticks. Reimport keeps the chosen objects.
- **Not evaluated:** modifiers (a warning lists the objects; apply them in Blender first), armature poses (the rest shape is imported), shape keys, curves, text, metaballs and other non-mesh objects (convert them with Object → Convert → Mesh), instanced collections, and data linked from other `.blend` files. Generated images that were never saved or packed have no pixels to read and are reported per material.

## Units and orientation

| Format | Units |
|---|---|
| FBX | Metadata unit scale and axes |
| GLB / GLTF | Metres → Source inches |
| PMX | Imported characters' scale (0.08 m per PMX unit at 40.457 units/m = 3.237 Source units per PMX unit), so stages and accessories fit characters |
| BLEND | Blender units (metres) → Source inches; Z up, like Source |
| OBJ | No units in the format. A model under 8 units across is treated as metres (×39.37) with a warning; larger files stay 1:1 |

Imported fronts (glTF +Z, PMX −Z, FBX front axis) end up on the prop's local −Y, and placement turns that side toward the player.

## Collision

- **Fast single hull (default)** builds one enclosing convex collider directly from 26 support planes. It needs no decomposition search and takes about 15 ms. It fills holes and concavities. Flat, needle-thin or near-degenerate models (a sword blade, a sheet) are welded and retried with growing tolerances, and fall back to a thin enclosing box, so the collision step does not fail on them.
- **Detailed** runs the pinned CoACD 1.0.14 on a welded, simplified copy (at most 18,000 triangles), producing up to 16 hulls of at most 64 points. Pieces CoACD returns degenerate are rebuilt from their points; if decomposition fails entirely, the fast hull is used and the import warns. It follows concave shapes such as arches and tables, but can take much longer. `lib_coacd.dll` ships beside `mmdhl_worker.exe` and needs the Microsoft x64 Visual C++ runtime (VCOMP140).

Mass is about 250 kg per cubic metre of the collision volume, clamped to 2–50,000 kg.

## Materials and lighting

- **Material values.** Base color and diffuse textures, normal maps, opacity, alpha masks and blending, two-sided flags, and specular color and shininess (Source Phong) all become `VertexLitGeneric` materials with bracket-form color values.
- **Alpha classification.** Alpha is classified from the texels the UVs actually cover:
  - Antialiased cutout edges use depth-writing masks.
  - Sustained fractional alpha blends.
  - Explicit glTF alpha modes win.
- **Special cases.** Phong without a normal map gets a valid flat normal. Emissive-only baked textures are self-illuminated.
- **Texture wrap.** Textures repeat, as glTF, FBX, OBJ, PMX and Blender expect. Models whose UVs lie outside 0–1 (common in game rips, e.g. a tank with every V between 1 and 2) render correctly instead of smearing the texture's edge row.
- **Glossiness from PBR.** glTF and Blender materials record roughness, metalness and reflectance. They become Source Phong: a rough surface (roughness above 0.85) gets no highlight, and smoother ones get a tighter highlight scaled like a normalized Blinn-Phong lobe (4% reflectance for non-metals, strong for metals). Props imported before this change are converted from their stored values, with no reimport needed. **Glossy highlights (all static props)** in the Static Props panel (`mmdhl_prop_specular`) turns Phong off for every prop at once, if a model still looks shiny or washed out.
- **Lighting.** Each prop samples map and dynamic lighting as a six-sided ambient cube at its own center. The player flashlight and projected lamps are added in extra passes.
- **Transparency.** Translucent geometry is split into spatial clusters that are sorted each frame.
- **Sidecar overrides.** A `<model file>.gmodel.json` sidecar can repair missing or wrong texture assignments. The format is the GModel loader's: `materials` and `meshes` sections with `base_texture`, `normal_texture`, `opacity_texture`, `color`, `alpha_mode`, `two_sided`, `specular`, `shininess` and `unlit`.

Limits:
- This is Source Phong, not PBR. Metallic/roughness, clearcoat, toon ramps and sphere maps are approximated or ignored; PMX warnings say so per material.
- Intersecting transparent layers can still show Source sorting artifacts.
- Props do not cast render-to-texture shadows. The prop entity draws hidden mesh proxies itself, and the engine's shadow pass does not call that draw.

## Cache and files

Everything is under `garrysmod/data/mmd_hotloader/static/`:

| Path | Contents |
|---|---|
| `assets/<sha256>.gmdl` | Content-addressed bundles; the ID is the bundle's SHA-256 |
| `textures/<sha256>.png` | Normalized textures, rewritten only when missing |
| `library/` | Names, folders, favorites and last size |
| `sources.local.json` | Private absolute source paths for Reimport; never shared |

Servers record approvals in `approved.json` under `props`. They limit uploads with these convars:
- `mmdhl_prop_max_package_mb` (256)
- `mmdhl_prop_max_expanded_mb` (1024)
- `mmdhl_prop_max_texture_dimension` (4096)
- `mmdhl_prop_max_materials` (128)

## Console

| Command / convar | Purpose |
|---|---|
| `mmdhl_open_props` | Open the library on Static Props |
| `mmdhl_prop_import_axis` | `auto`, `y_up` or `z_up` |
| `mmdhl_prop_import_collision` | `hull` or `balanced` |
| `mmdhl_prop_import_scale` | Extra import scale |
| `mmdhl_prop_import_yaw` | Turn at import, in degrees |
| `mmdhl_prop_spawn_frozen` | Freeze new props |
| `mmdhl_prop_specular` | Glossy (Phong) highlights on static props, 1 or 0 |
| `mmdhl_prop_collide` | Collision level for new placements: `none`, `world` (default), `noactors`, `noplayers` or `all` |
| `mmdhl_prop_gravity` | Gravity for new placements, 1 or 0 (always off with `none`) |
| `mmdhl_prop_stats` | Mesh preparation counters |
| `mmdhl_prop_asset`, `mmdhl_prop_scale`, `mmdhl_prop_yaw`, `mmdhl_prop_frozen`, `mmdhl_prop_physprop`, `mmdhl_prop_r/g/b` | Static Prop tool settings |

## Validation

See [STATIC_PROPS_VALIDATION.md](STATIC_PROPS_VALIDATION.md).
