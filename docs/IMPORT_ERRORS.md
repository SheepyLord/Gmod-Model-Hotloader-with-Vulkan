# Why an import failed

From 2.3.0, a character import that fails says **why**, not only at which step: which part of the file is at fault (bone 12 “左足”, material 7 “スカート”, morph 1 after “まばたき”, mesh “Body” primitive 2…), which value, where reading stopped, and what to do about it, in the player's language.

## The failure window

**Import failed: <file>** shows four things:

1. **While <step>: <cause>**. The step and the cause are translated. The step is the importer's current step (reading the file, reading the skeleton, materials and physics, converting the VRM avatar, preparing textures, saving to the cache, preparing Source materials, fitting the ragdoll…); the cause is one sentence chosen by the error's code (“Part of the model file is damaged and cannot be read.”, “This is an archive, not a model file.”, “Another program is using the file.”…).
2. **Where: …**, when the importer can say: the elements it names, outermost first (`mesh 0 “Body” › primitive 2 › accessor 7`), the element before a damaged one (`morph 1 › after morph 0 “まばたき”`), the value at fault (`soft body 0 “soft fabric” › velocity correction factor: NaN`) and the byte where reading stopped (`byte 3,210 of 6,377`). After a crash it shows the importer's last progress line (`Material 3 of 9 “Hair”: hair.png`).
3. **What to try: …**, chosen by the code: download or extract again for a cut-off file, re-save in PMX Editor for damaged sections, extract an archive first, rename a file whose content does not match its extension, close the program that holds the file, free disk space, and so on.
4. The details box (also **Copy details**): the importer's own English message, which names the element exactly, the error code, the context, the error data (JSON), the file, source, step and step code, the last progress line, the importer's exit code and its meaning, the importer's release and build, the time spent, the time, and the tail of the importer's crash log. Paste it into a bug report.

The window grows with its text instead of clipping long names and translations. It also opens when the importer cannot be started (missing or blocked `mmdhl_worker.exe`, the installation check) and when the game loses track of a running import. A busy importer only updates the status line.

### Files that are not models

The importer looks at the first bytes of a file it cannot read and says what it is: a ZIP, RAR or 7-Zip archive (“extract it first”), an MMD motion, pose or project (VMD, VPD, PMM), an image or texture, an FBX, glTF or COLLADA file whose name ends in another extension (“rename it to end in .fbx”), a static model without a skeleton (OBJ, Blender, DirectX .x, Metasequoia…), a text file, an empty file.

### Crashes

If the importer process ends without reporting (a crash inside a library, running out of memory, a stack overflow), the window keeps the step, file and progress line it had reached and explains the exit code: an access violation, a stack overflow, a fail-fast stop, heap corruption, running out of memory, an abort, an illegal instruction, a missing DLL… The importer writes one line per crash to `<job>/worker.log` (exception code, module and offset, step), which the details include.

## Characters imported without a ragdoll

A character whose skeleton lacks body parts the ragdoll needs still imports and can be previewed. Right after the import:

- with the bone window available, its rescue prompt lists the missing parts and offers **Assign bones…** (see [CHARACTER_IMPORT.md](CHARACTER_IMPORT.md));
- otherwise **Character imported without a ragdoll** lists the missing parts in plain words, with **Assign bones…** only when the bone window exists, the advice to give those bones the standard MMD names in PMX Editor when it does not, and **Keep for now**;
- when the fit failed for another reason (no height, invalid scale), the same window says why.

Spawning such a character fails with “This character cannot become a ragdoll: <the fitter's reason>”, translated around the fitter's English reason.

## Compatibility

- **Older natives (2.2.0, 2.1.0-native.12) with this Lua**: failed jobs have no `errorCode`, `stageCode` or `errorDetails`. The window shows the English message and the English step (lower-cased) as before, with hints matched on the English wording; that matching now checks spring-bone and nanoem PMX errors before the general “bone” and “truncated” phrases. No import-time fit block arrives, so no ragdoll window appears after an import.
- **This native with older Lua**: the English messages keep the words older hints look for (“belong in Static Props”, “exited”, “Invalid … PMX”), so their hints still match.
- Warning and note texts are unchanged: asset identities do not change on reimport.

## Technical reference

### Error codes

| Code | Meaning |
|---|---|
| `pmx.truncated` | The PMX/PMD file ends early (reading stopped less than 1 KB before the end). |
| `pmx.section_corrupt` | nanoem stopped in a section; `errorDetails.section`, `offset`, `size`, `status`, `where` (the element), `after` (the intact one before it). |
| `pmx.text`, `pmx.version` | Undecodable text; an unsupported PMX version (`errorDetails.version`). |
| `pmx.materials` | Material index counts that are not multiples of 3, run past the index list, or leave triangles uncovered (`first`, `count`, `indices`, `covered`). |
| `pmx.number`, `pmx.reference` | A non-finite or out-of-range value (`field`, `value`) or a reference outside the model (`value`, `count`), with the element in `where`. |
| `format.archive`, `format.motion`, `format.image`, `format.renamed`, `format.unknown` | Not a model (`errorDetails.format`, `family`; `extension` for a renamed file). `character.format` for a static model. |
| `vrm.truncated`, `vrm.data`, `vrm.container`, `vrm.external`, `vrm.image`, `vrm.humanoid` (`bone`), `vrm.no_skeleton`, `vrm.no_geometry` | VRM conversion; `where` holds the mesh, primitive, accessor, buffer view, node or skin. |
| `vrm.json` | The VRM's JSON lacks a key or has the wrong type, inside the VRM conversion (`json` elsewhere). |
| `spring.data` | Invalid spring-bone data; `where` holds the spring, spring joint, collider or collider group, `field` and `value` the setting. |
| `io.missing`, `io.locked`, `io.denied`, `io.device`, `io.read`, `io.empty`, `io.write`, `io.disk_full`, `io.filesystem` | File access, with `path` and the Windows error (`systemError`). |
| `texture.derivative` | The Source texture of a material could not be made (`where`: the material, `texture`). |
| `memory` | Out of memory (also an exit code that means it). |
| `worker.crash` | The importer ended without a final status (`exitCode`, `exitCodeHex`, `cause`). |
| `character.*` | Characters in other formats ([CHARACTER_IMPORT.md](CHARACTER_IMPORT.md)). |
| `unknown` | Any other exception; its message is kept. |

`where` entries are `{kind, index?, name?}` with kind one of vertex, triangle, material, texture, bone, ik, morph, display_frame, rigid_body, joint, soft_body, header, text, mesh, primitive, accessor, buffer_view, buffer, node, skin, image, humanoid_bone, spring, spring_joint, collider, collider_group. The Lua translates the kind (`library.element.<kind>`); names are the file's own.

### status.json of a failed job

`state` `failed`, `error` (English sentence), `errorCode`, `errorDetails`, `context` (the importer's steps, English), `exceptionType` (`import`, `json`, `filesystem`, `memory`, `std`, `crash`, `unknown`), `stage` and `stageCode` (`read`, `parse`, `convert_vrm`, `convert_character`, `probe`, `textures`, `cache`, `materials`, `fit`), `detail`, `current`, `total` (the last progress), `filename`, `source`, `kind` (`character` for character imports), `elapsed_ms`, `worker` (`release`, `build`). `PollJob` adds `exitCode` to every failed result and `log` (the end of `worker.log`) when there is one; a worker that ended without a final status becomes a `worker.crash` (or `memory`) failure with what it had reached.

Running character statuses carry `stageCode`; the texture step also `detail` (“Material 3 of 9 “Hair”: hair.png”), `current` and `total`.

A completed character import carries `fit` beside the manifest (outside the asset's identity): `{"ok":true}`, or `{"ok":false, "errorCode", "error", "missing", "errorDetails"}` where `errorDetails` holds at least `missing` (the ValveBiped keys the fitter found no bone for) and whatever else the fitter reports. The Lua reads `missing` or `errorDetails.missing`.

Native (`import_error.hpp`, in the runtime): `ImportError` (`code`, `details`, `context`), `importFail`, `ImportScope` (a step, a step with a domain such as `vrm` that turns JSON errors into `vrm.json`, or a place `ImportScope("mesh", 3, "Body")` that joins the `where` of every error inside), `describeException`, `place`/`placeText`, `sniffFormat`/`notCharacterFile`, `fileFailure`, `describeWorkerExit` and `finishedWorkerStatus`.

`mmdhl_worker.exe --crash-test <folder> access|abort|terminate` crashes on purpose to test the crash log and exit codes.

Lua: `mmdhl.ImportHint(err, code, details)`, `mmdhl.ImportCause(status)`, `mmdhl.ImportStage(status)`, `mmdhl.ImportWhere(status)`, `mmdhl.ImportFailureDetails(status, kind)`, `mmdhl.ShowImportFailure(status, kind)`, `mmdhl.ExplainFit(status)` and `mmdhl.ShowFitFailure(status)`.

## Tests

- `import_errors` (CTest, `tests/import_error_tests.cpp`, no GPU, game or user models): PMX files written in the test (a damaged vertex and morph mid-file, a cut-off file, material ranges, a bone parent outside the model that still loads), the corrupt fixtures (soft body field and value), VRM files without `byteLength`, with an accessor or humanoid bone missing, spring bones, `describeException` for JSON, filesystem, memory and other errors, the format sniffer, missing, locked and folder paths, worker exit codes, `finishedWorkerStatus`, and `mmdhl_worker --request` / `--crash-test` (status.json fields, `worker.log`, exit codes).
- `tests/test_import_failures.py` (lupa): hints by code, family and phrase; the cause, step and Where line; the window, its size and Copy details; older statuses; the window for an importer that cannot start and a lost job; the ragdoll window with and without the bone window; the spawn's translated fit error.
