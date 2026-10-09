# Characters from FBX, glTF and DAE, and the bone assignment window

From 2.3.0, **Q → External Models → Character Models → Import Character Models** also accepts **FBX**, **glTF/GLB** and **DAE (COLLADA)** files with a skeleton, next to PMX, PMD and VRM. The importer reads the skeleton first and shows the **bone assignment window**: a picture of the body with every part the ragdoll needs, the model's own skeleton over its mesh, and an inspector that says what to do next. Parts found automatically are already filled in. When everything needed is found, one click imports the character.

The same window also rescues PMX, PMD and VRM characters the fitter cannot map (for example a model whose knee bones have unusual names): their choices are saved as fitter pins next to the collision corrections, and new spawns use them.

## Using it

### Importing an FBX, glTF or DAE character

1. **Import Character Models** and pick the file (the picker title is "Import character"; its filter lists PMX, PMD, VRM, FBX, glTF and DAE). The terms-of-use question, if the file has a readme, comes first as before.
2. The library shows **Reading the skeleton…**: the worker reads only the skeleton, the mesh points and the bone weights (no textures).
3. The window opens:
   - **Left**: the body chart. Each body part is a dot: a green tick is set, an amber "!" means check (a guess or a warning), a red ✕ is missing, a dashed ring is created automatically (optional parts without a bone). **Hands** shows the 30 finger joints.
   - **Middle**: the model, front or side, with its mesh points coloured by side (blue is the character's left, orange its right). Wheel zooms, right-drag moves, a double click fits. Suggested bones carry numbers 1–3.
   - **Right**: the inspector. With nothing selected it summarises the state ("Everything needed was found", "Almost there: please check 2 body parts", "3 body parts need your help") with one button that starts the work. With a body part selected it explains the part (with typical bone names), shows the current bone, three ranked suggestions with their reasons, a searchable hierarchy, and **Keep / Leave empty / Use the automatic choice / Done**.
   - **Footer**: what is still missing, **Cancel** and **Import character**.
4. Click a body part (or press **Start**), then click its bone in the picture, a suggestion or the hierarchy; the next missing part is selected by itself. A bone another part used moves, and the status line offers **Undo**.
5. **Import character**. The file is converted (MMD bone names, Japanese visemes and blinks for Face Poser, materials and textures) and imported like a VRM. The library selects the new character.

Tools: **Undo** (Ctrl+Z, 50 steps), **Swap left and right**, **More ▾** (assign again keeping your choices, assign everything again, copy one side to the other through mirrored bone names, clear fingers or everything optional, redo), **List** (an expert table with a bone picker per part). Keyboard: Tab/Shift+Tab move between parts, Enter takes the best suggestion (or imports when nothing is selected), Delete clears the selected part, F fits the view, 1 and 2 switch front and side, Ctrl+F searches. Esc is not bound (the game opens its menu).

The footer checkbox **Next time, skip this window when every body part is found** (`mmdhl_bonemap_autoskip`) imports at once when the automatic map has nothing to check.

**Jiggle bones (optional)**: hair, skirts, tails, chest and accessories found by name or position are grouped by type. Ticked groups swing as VRM spring bones (the same native solver as VRM avatars), with automatic colliders on the head, neck, chest, hips, upper arms, thighs and calves. Each group has **Less / Normal / More**, **Collide with the body**, its parts, and four sliders under **Details**. **Add a part that swings** takes the next clicked bone as a new swinging part; body bones, and bones that hold one, are refused.

The window remembers:
- the work of a cancelled window, for the game session (pick the same file again);
- the last import of the same file (`data/mmd_hotloader/bone_maps/sources/<sha256>.json`) and of any file with the same skeleton (`bone_maps/skeletons/<signature>.json`, at most 256). A notice says so, with **Use automatic instead**.

**Reload** (Edit character) converts the same way without the window: the import request is kept in `sources.local.json`. When the file changed and a named bone is gone, the library reads the skeleton again and reopens the window with "The file changed since its bones were assigned". **Import again with the bone window…** (library right-click, converted characters) goes through the window again, for example to change swinging parts.

### Rescuing a character the fitter cannot map

When a PMX, PMD or VRM character looks like a character (at least 6 of the head, arm and leg landmarks and 15 bones) but the fitter misses required parts, the library:
- marks it **Needs bones** and shows **Assign bones… (n missing)** above the spawn buttons;
- shows a prompt after the import (or after a failed spawn) with a small figure and the missing parts: **Assign bones…** or **Later**.

The window then works on the cached model: the fitter's own choice is "found automatically", parts the engine can guess are amber, the rest red. **Save bones** sends only the differences from the fitter's choice (pins) to the server, which checks them with its own fitter and writes `data/mmd_hotloader/fit_overrides/<asset>.json`. Placed characters keep their carriers; new spawns use the pins, and so do duplications and saves made from them. **Assign bones…** in the library's right-click menu edits any character the same way; **More ▾ → Reset to automatic** and saving removes the pins.

When collision corrections exist and a physics body part changes, the footer says that the corrections were made for the old bones and are removed on save.

### Multiplayer

- Converting is client-local and needs no new network traffic: the result is an ordinary asset (a PMX with MMD names plus a `conversion` block in its manifest), shared, approved and exported like any other.
- Saving pins changes how the server fits a model: single player, the listen host and admins may save (other addons can refuse with the server hook `MMDHLCanEditBoneMap(ply, assetId)`). Requests are limited to one per second and 16 KiB. Other players see **Only an admin can change how models are fitted on this server.**
- The pins travel with Workshop packages (the whole `fit_overrides` file is packed), but not with model sharing, like collision corrections.

### What is converted

- Up axis and units from FBX metadata (or glTF/DAE conventions), with the facing worked out from the legs, arms and toes; pivots are not preserved; animations are ignored.
- One armature: the one that moves most of the mesh. Meshes bound to another armature are rebound by bone name when at least 90 % of their bones match, otherwise left out (a note says which).
- Unusual sizes: a model outside 0.3–3 m is resized to 1.6 m (a notice in the window, a warning in the library).
- Bones keep their names where they are not body parts; body parts get MMD names (`下半身`, `上半身`, `上半身2`, `上半身3`, `首`, `頭`, `左腕`…), so every existing tool (Face Poser, fitter, collision editor) works unchanged. `conversion.sourceNames` records the original names.
- Expressions: viseme and blink blend shapes become `あ い う え お`, `まばたき`, `ウィンク２`, `ウィンク２右` group morphs.
- Materials: up to 128 (more are merged with a warning); embedded and external textures (external references never follow network paths).

Limits: 4,096 bones, 4,000,000 triangles, at least 15 usable bones. Not supported: animations, cameras and lights, blend files (export FBX or glTF from Blender), OBJ (no skeleton).

### When something goes wrong

The import failure dialog explains the error by its code: no skeleton or too few bones (**Import as a static prop** replaces Retry), a file the importer cannot read, too many bones or triangles, an orientation it cannot work out, or an assignment the converter rejected. While the window's work exists, **Back to bone assignment** reopens it. When the converter rejects the assignment (`character.bone_map`, `character.jiggle`), the window reopens by itself on the part at fault with the converter's sentence.

A rigged humanoid imported in **Static Props** offers **Import as character** (the prop is deleted, the file goes through the window).

## Compatibility

- **Older native files with this Lua**: no FBX in the picker, no bone window, no menu entries. The rescue prompt says to update the native files.
- **This native with older Lua**: the picker lists FBX/glTF/DAE; the old Lua sends no assignment, so the worker maps automatically and imports when every required part is certain, otherwise fails with "This model needs its bones assigned. Update Model Hotloader's addon files to get the bone window."
- **Fit mode** needs the fitter pins (`native.GetBoneMapProposal` and the `boneMap` fit option, the torso feature of 2.3.0) and `native.InspectBoneMap`. Without them, conversion works and fit mode is hidden.
- Existing PMX, PMD and VRM assets keep their ids; the VRM writer is byte-identical (golden test).
- A converted asset loaded by an older binary still loads (manifest version 2); it fits by its MMD names and loses only its springs and explicit torso choices.

## Technical reference

### Native files

| File | Content |
|---|---|
| `native/humanoid_slots.hpp` | The 54 body parts (52 assignable plus the two eyes, used only by conversion): ValveBiped key, group, side, segment, required/physical/recommended flags, anchors, mirror partner, MMD names. `tests/fixtures/bonemap/slots.json` is generated from it and checked against the Lua catalogue. |
| `native/humanoid_map.{hpp,cpp}` (mmd_runtime) | Name sanitising (UTF-8, Shift-JIS, GBK), name parsing (namespaces such as `mixamorig:`, Biped and Bip001 prefixes, side rules, tokens), bone classification, the automatic assignment, the structural rules (`checkBoneMap`), the skeleton signature and `inspectBoneMap`. |
| `native/pmx_writer.{hpp,cpp}` | The PMX writer shared by VRM and the converter. |
| `native/character_import.{hpp,cpp}` (worker only, links Assimp) | `probeCharacter` and `convertCharacter`. |
| `native/assets.cpp` | `importConverted`: imports a conversion like a VRM, adds `conversion` (with `sourceSha256`) to the manifest; `loadAsset` reloads its springs and validates its bone map. |

### Capabilities

`native.GetCapabilities().characterImport = {version=1, probeVersion=1, requestVersion=1, formats={"fbx","glb","gltf","dae"}}`.

### Worker jobs

`native.BeginImport(path, optionsJSON)`:

- `{"kind":"character_probe"}`: for FBX/glTF/DAE, completes with `{"state":"complete","kind":"bone_map","probe":{…},"source":…,"filename":…}`. A `.glb` that is really a VRM, and PMX/PMD/VRM files, import normally. OBJ and BLEND fail with `character.format` / `character.blend`. The probe holds `version`, `format`, `generator`, `sourceSha256`, `units`, `height` (`meters`, `normalized`), `skeleton` (`signature`, `maxDepth`, `armatures`, `bones` with name, english, parent, position, weighted, flags, meaning, side, segment, mirror, nameIssue; `points`: at most 3,000 flat `x,y,z,bone`), `auto` (`humanoid`, `slots`, `eyes`), `meshes`, `materials`, `textures`, `vertices`, `triangles`, `morphs` and `warnings`; it is kept under 2 MB.
- `{"kind":"character","requestVersion":1,"boneMap":{"<ValveBiped key>":"<bone name or empty>"},"eyes":{"L":…,"R":…},"jiggle":{"version":1,"groups":[{kind, enabled, swing, custom, collide, values:{stiffness, dragForce, gravityPower, hitRadius}, chains:[{root, enabled}]}]}}`: converts and imports. The request is stored as the asset's options in `sources.local.json`, so Reload repeats it. `{}` (older Lua) maps automatically.
- Running statuses carry `stageCode` `probe` or `convert_character`; failures carry `errorCode` and `errorDetails` (`character.bone_map`: `slot`, `bone`, `reason` = missing, required, duplicate, order, leg_on_spine, unknown_slot, locked or size; `character.jiggle`: `root`, `bone`, `slot`, `reason` = missing, body, too_many or range).

Developer flags of `mmdhl_worker.exe`: `--probe-character <file>`, `--convert-character <file> <outdir> [request.json]`, `--inspect-bone-map <model> [options.json]`.

### Manifest `conversion` block (identity-hashed)

`version`, `generator`, `format`, `sourceGenerator`, `units`, `boneMap` (key → PMX bone index), `eyes`, `sourceNames`, `morphRenames`, `meshes`, `jiggle` (the request's groups), `springBone` (VRM 1.0 style joints, colliders and groups for the native spring solver) and `sourceSha256`.

### `native.InspectBoneMap(assetId, optionsJSON)` (both realms)

The cached model's skeleton and automatic map for the window, and the structural rules on an assignment. The asset must be loaded (`RequestAsset`, then `AssetInfo`), otherwise it fails with "Load the asset before requesting a bone map". Options: `include` (`["skeleton"]` adds the skeleton in the probe's form) and `values` (`{"<key>": <bone index or -1>}`). The result has `version`, `asset`, `auto`, optionally `skeleton`, and `issues` (`code`, `severity`, `slot`, `bone`, `text`); values out of range or not whole numbers are `range` errors. Malformed options fail with "Invalid bone map options".

### Saving pins

Client → server: `mmdhl.Action('bonemap', assetId, nil, {version=1, boneMap={[key]=index}, dropCollision=bool})`. The server (`mmdhl.boneMapper.HandleSave`) checks the permission, rate, size, keys and values, loads the asset, asks `GetBoneMapProposal` with the pins (no missing parts, no error issues) and `InspectBoneMap` with the fitter's resulting map, then merges `boneMap`, `boneMapVersion=1` and `boneMapSavedAt` into `fit_overrides/<asset>.json` (`version` 3 and `generator` 18 stay, so older Lua keeps reading the collision corrections). An empty map removes the pins; `dropCollision` removes `bodies`, `scale` and `collisionOverrideScale`. The reply is the net message `mmdhl_bonemap_result`: `WriteString(assetId) WriteBool(ok) WriteString(token)`.

`mmdhl.Spawn` reads the file once: collision corrections as before, and `options.boneMap` from the pins when the caller gave none. Saving collision corrections keeps the pins.

### Lua (`mmdhl.boneMapper`)

- `bone_mapper_rules.lua` (shared, no panels): the catalogue, state, validation, suggestions, swinging parts, memory, request and pins. `tests/test_bone_mapper.py` runs it offline against the native rule vectors (`tests/fixtures/bonemap/rules.json`, `scripts/make-bonemap-fixtures.py`).
- `bone_mapper.lua` (shared): part labels and the server save handler.
- `bone_mapper_ui.lua` (client): the window, the library flow and the rescue prompt.

Entry points for other features: `mmdhl.OpenBoneMapper({asset=id, fit=status.fit})` (fit mode; `fit.ok==false` opens it as a rescue) or `mmdhl.OpenBoneMapper({probe=<probe status>})` (convert mode). Hooks: `MMDHL.BoneMapOpened(state)` and `MMDHL.BoneMapClosed(state, outcome)` with `imported`, `saved` or `cancelled`. `library.SetFitStatus(id, fit)` sets or clears the "Needs bones" badge.

## Tests

- `character_import` (CTest, no GPU, no game, no user models): name parsing and conventions (Mixamo, UE4/UE5, Unity, VRoid, Rigify, Biped, Daz, ValveBiped, MMD Japanese and English, numbered rigs), the shared rule vectors, and generated GLB characters through probe, conversion, import, reload, `InspectBoneMap` and the fitter.
- `vrm` (CTest): golden hashes keep the VRM writer byte-identical.
- `tests/test_bone_mapper.py` (lupa): the rules, the library flow (probe, window, rejections, file changes, skip option), routing in `library.lua`, the rescue checks, the server save handler, the spawn's pins, the action receiver, and a smoke run of every panel of the window with recording Derma stubs.

Developer corpus check (read-only, not in CI): 14 humanoid FBX characters from games and tools (Daz, Unity-chan, VRChat, Wuthering Waves, Genshin Impact, Honkai: Star Rail, Blue Archive, Rigify, Advanced Skeleton, Auto-Rig Pro) found all 15 required parts automatically; 12 converted and fitted 18 bodies with no help. The other two (a Rigify and an Auto-Rig Pro rig with low-confidence torso parts) stopped with `character.needs_mapping` when imported without the window, as intended. Files without a skeleton, with too few bones, an old-format FBX and a DAE with an unresolved library reference failed with their codes.
