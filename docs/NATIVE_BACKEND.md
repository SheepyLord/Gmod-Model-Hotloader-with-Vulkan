# Native Source backend architecture and API

The native carrier is the default (`mmdhl_native_carrier 1`); `backend='source'` also selects it explicitly in `mmdhl.Spawn`. Completed acceptance checks are recorded in `NATIVE_VALIDATION.md`. The old custom-entity path is retained for explicit rollback with `mmdhl_native_carrier 0`; its older APIs and historical behavior are documented in `API.md` and `IMPLEMENTATION.md`.

## Ownership and cache

`mmdhl.Spawn(player, assetHash, options, callback)` returns through the callback after loading the asset. Its result is a genuine `prop_ragdoll`. `options` includes `scaleMultiplier` (default 1), `mass`, `position`, `angles`, `frozen`, `secondaryCollision` (0/1/2), and optional `collisionOverrides` keyed by English body name. Use the spawn factory for undo, cleanup and creator registration. Direct native instance creation does not create an engine entity.

A rig manifest identifies 56 essential/finger bones plus up to two available PMX eye bones, 18 physics bodies, primary MMD bindings, rest/inverse transforms, fit confidence, 17 joint profiles, morph controller assignments and material slots. Rig format version 3 / generator 18 writes immutable content-addressed files under `data/mmd_hotloader/rigs/<key>/`. A GMA mounts `models/mmd/<16-hex-rig-id>/<readable-model-name>.mdl` and its matching VVD/VTX/PHY. Asset and texture caches retain their original layout. A new generator or fit produces a new key.

The deterministic fitter excludes authored secondary chains from primary body samples, folds finger/toe/twist weights into primary bodies, estimates bounded robust dimensions, and creates convex hulls. A bundled atlas contains normalized convex references and small linear support-plane predictors, calibrated on 159 registered source/reference pairs. Character art, corpus paths, and private audit reports are not bundled. Corrections are validated before writing `fit_overrides/<asset>.json` and creating a corrected copy.

Source owns primary transforms, traces, physics-bone indices, angular constraints, freezing, world contacts and impulses. Before rendering, the client refreshes and transfers its complete carrier studio bone palette in one bulk call. Secondary simulation and deformation consume this same interpolated pose, including native finger manipulations. The server does not advance the secondary world. Bind offsets retarget them to original MMD indices. Source-driven bones cannot be overwritten by IK or Bullet feedback.

Each character owns an independent nanoem world. Bodies bound to primary Source bones become kinematic followers; all authored secondary bodies and valid joints remain represented. Invalid endpoint indices and self-joints produce named warnings; index -1 is a world anchor with an explicitly initialized transform. Constraints between two followers need no active solver constraint. The default has no external contacts. Optional modes mirror static Source map geometry or additionally moving Source physics objects into that character's world. Its own 18 native objects are excluded. These kinematic proxies cannot apply impulses back into Source. Other MMD characters are represented by their Source carriers, not their secondary bodies. Fixed 60 Hz steps permit at most four catch-up steps, report dropped time, and reset after initial pose/teleport. Physics worlds run in parallel and join before engine calls.

## Entity helpers

All model/bone/morph indices stored in native metadata are zero-based. Lua arrays are one-based.

| Helper | Meaning |
|---|---|
| `mmdhl.IsMMD(ent)` | Identifies a tagged carrier or legacy MMD entity |
| `mmdhl.GetAsset(ent)` / `GetInstance(ent)` | Cached asset hash / transient runtime handle |
| `mmdhl.Entities()` | Live MMD entities |
| `mmdhl.GetRig(ent)` | Versioned native carrier manifest |
| `mmdhl.GetMetadata(ent)` | Asset, rig and full original model metadata |
| `mmdhl.GetBones(ent)` | Full original MMD bone inventory, including internal bones |
| `mmdhl.GetMorphs(ent)` | Full stable list with `mmd`, `native`, machine `name`, `original`, `displayName` and `namingSource` |
| `mmdhl.GetMorphWeight(ent, index)` | Weight by original MMD morph index |
| `mmdhl.SetMorphWeight(ent, index, value)` | Sets one native or overflow morph; client calls network to server |
| `mmdhl.SetMorphWeights(ent, values)` | Sets the complete ordered morph array in bulk |
| `mmdhl.GetDiagnostics(ent)` | Counts, secondary transforms, timing, dropped time, joint errors and external-contact diagnostics |
| `mmdhl.GetMaterials(ent)` | Ordered material metadata; zero-based `slot`, authored `name`, actual VMT `path` |
| `mmdhl.IsMaterialVisible(ent, index)` / `SetMaterialVisible(ent, index, visible)` | Visibility by zero-based PMX material slot |
| `mmdhl.CaptureMaterialState(ent)` / `ApplyMaterialState(ent, state)` | Complete overrides and hidden slots |
| `mmdhl.GetCollisionFlags(ent)` / `SetCollisionFlags(ent, flags)` | What hair and clothing collide with, a sum of `mmdhl.Collide` flags: world 1, character (the model's own body) 2, objects 4, living players 8, living NPCs 16. Default 6 (character and objects). |

Native `LookupBone`, physics-object/bone conversion, flex and color functions keep their normal engine behavior. Material/bodygroup adapters are scoped to tagged MMD entities and editor previews; other entities keep their original methods. The carrier exposes at most 96 stable flex controllers, prioritizing common expressions and left/right pairs. Overflow controls never reassign those IDs. Original names and internal bones remain metadata; Source does not impersonate the full MMD skeleton.

Finger manipulations use native carrier bone axes and retarget to MMD. Eye posing has a narrow tagged-entity adapter because MMD eyes are skeletal. Face Poser chains the installed panel's existing behavior and appends the MMD section only for tagged ragdolls.

Native duplication uses the engine's physical bone poses, plus `MMDHLNative` modifier version 3 for the original rig manifest, asset identity, fit options, morphs, manual MMD poses, eye driver/target, material overrides, visibility and collision mode. Versions 1 and 2 remain readable. Legacy duplicates route through the new factory when enabled. Use Sandbox duplication for native ragdoll state; legacy `GetState/SetState` alone do not save Source physics objects. Duplications are local cache references.

## Render ownership

A client render entity submits the deformed MMD geometry in normal, translucent and flashlight depth views. The native carrier stays invisible. Geometry is deformed once per frame; topology and allocations are reused. Source `IVModelRender::SetupLighting` supplies ambient, local-light and cubemap state. Material color/alpha are restored after every override draw so unrelated entities are unaffected. Alpha and entity/submaterial overrides are resolved per pass.

Classic render-to-texture sunlight shadows bypass Lua model drawing in Source. A guarded native adapter submits the same deformed geometry through `ShadowBuild` for registered MMD carriers. It uses typed material references for texture alpha, leaves other renderables on their original callbacks, and uninstalls when the last carrier is removed. Flashlight depth views use the client render entity and `DepthWrite`.

The Source shader path does not execute MME. Diffuse/alpha, vertex and UV morphs are supported; uncommon sphere/toon/edge effects and every MMD material morph semantic are not reproduced by VertexLitGeneric. PMX 2.1 cloth and rope fixtures have separate reference-replay coverage. See the validation report for observed limits.

Native rendering uses three reusable vertex buffers per instance/material/vertex-format combination. Indices are uploaded once; vertices update only for a new deformation snapshot. Compatible passes reuse the same data. Buffers are released on entity removal, stale format changes and module shutdown. `mmdhl_vertex_cache 0` selects the earlier immediate-stream fallback. Both paths require the guarded nonqueued render context, owning-thread graphics calls and finite-transform checks.

## Failures

The client library shares one panel between Q → MMD and `mmdhl_open`. Preferences
live in `data/mmd_hotloader/library/<asset>.json`: optional display name and
favorite. Delete removes the cached asset, matching live ragdolls, generated
carriers, fits, preferences and textures no other asset uses. There is no
recoverable Deleted tab. Original source files stay untouched. Mounted archives
locked by Windows are queued for physical deletion on the next game start.

`mmdhl.RequestSpawn(asset, options, callback)` is the client placement entry point.
It returns `true` when sent, or `false, error`. Its callback receives
`state, message, entityIndex`, where state is `loading`, `ready`, or `error`.
`mmdhl_spawn_status` correlates replies by a stable request ID; the client keeps
the menu open on failure and bounds waiting at 75 seconds. The server validates
the eye trace, reports every load failure, and rejects overlapping
requests from the same player. `mmdhl.Spawn` preserves its existing server API;
callbacks now also run on early rejection.

`mmdhl_backend_version 1` migrates the prototype's archived carrier-off setting
once. UI placement explicitly selects Source. One client hook owns the temporary
render-mode change for all carriers, legacy entities, and visible previews;
hidden preview panels release their frozen world. Preview uses the same Source
materials with studio lighting, restoring alpha/color afterward. Native DLLs
are unchanged in the 0.3.1 library update.

Pose and simulation errors stop only the affected MMD secondary instance and write `data/mmd_hotloader/failures/` diagnostics. Source physics stays authoritative. Do not bypass ABI guards. Restart GMod after rebuilding native DLLs; Lua-only reload is available during development. Debug hooks and RPC are restricted to the owned single-player test session.

## Coordinate and shading corrections (0.3.2)

Primary carrier positions come from the original mapped PMX bone positions, at the fitted model scale. The carrier is a 56-bone ValveBiped projection, not a copy of all internal PMX bones. Missing intermediate spine/neck landmarks are synthesized; original PMX indices and D-bone aliases remain in the manifest. Its reference orientations are SCMI conventions converted by -90 degrees about vertical before tracking each actual limb, with roll retained. Bind pose, inverse binds, collision vertices and limits use this same frame. Unlike compiled animated models, this ragdoll-only carrier does not need an autoplay proportions delta.

Generator 9 omits the `selfcollisions` key from the PHY: in Source its presence disables all self-contact irrespective of the supplied value. The 136 nonadjacent pairs are explicitly enabled; the 17 joint neighbours remain excluded. `native.ProbeCarrierCollisions(modelIndex)` reads the actual VPhysics pair table for diagnostics. See Valve’s [ragdoll parser](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/ragdoll_shared.cpp).

Fit corrections use schema 3 / generator 14 so offsets authored in older bone frames are not silently applied. Old correction files are preserved, with a notice when a fresh fit is used. Existing placed carriers keep their immutable model; respawn them after updating.

Source materials use the user corpus’s legacy VertexLitGeneric recipe: shared flat normal, lightwarp, Phong exponent map, boost 24, albedo tint, Fresnel [0 0 1], and rim exponent/boost 2. The three generic maps live under mmdhl/scmi to avoid addon name collisions. Base textures remain the imported model’s own verified images. Texture alpha is classified once when loading: cutouts write depth; significant fractional alpha blends. Separate blend materials also handle entity fades and material morphs without mutating the opaque shader. Local lighting, flashlights, shadow/depth passes, and entity overrides remain supported.


## Scale and portable fitting (0.4.0)

`scaleMultiplier=1` maps raw PMX coordinates to Source by `0.08 * 40.457`. Explicit legacy `height` and `scale` remain supported, but mixing scale options is rejected. Legacy `scale` is metres per PMX unit, not the new multiplier. Hidden geometry cannot alter the default ratio. Legacy height fitting retains its previous anatomical-height behavior.

Import workers cache default fits under `fits/g16-<atlasHash>-<assetHash>.json` with a content digest. Variants are fitted off the engine thread; only VPhysics serialization/mounting happens on the engine thread. Cached fits include actual convex topology, region participation, exclusions, confidence, original PMX bindings, and the atlas hash. At most 64 hull vertices per body are serialized. Low confidence requires visual inspection; confidence is not a guaranteed reference overlap score.

The VPhysics bridge supplies explicit `CPolyhedron` topology. This avoids the installed interface's point-cloud simplifier collapsing fitted surfaces into a few vertices. It validates Source's clockwise polygon traversal, outward normals and endpoint references, then queries the resulting engine convex before serialization. A conversion with more than 0.01 Source units of surface error at the default ratio is rejected. The engine fingerprint guard remains mandatory.

Deformation shares each vertex's skinning transform across position, normal and tangent, including SDEF and QDEF. Regression checks compare all five vertex modes with the independent position/direction paths under mixed rotations. This does not change the authored secondary solver, mesh detail or material passes.

The editor requests actual `PhysObj:GetMeshConvexes()` from Source for its cyan overlay. Proposed corrections use separate yellow geometry. Material exclusions affect primary collision fitting only. Default invisible material regions and authored secondary chains are excluded automatically.

Older native duplicates retain their saved scale and physics poses. When the generated carrier changes, duplication recreates its physics and transfers each body's pose through the old/new bind frames before attaching the renderer. Missing original rig metadata produces an explicit error instead of attaching an incompatible skin.

`native.GetAlignmentProbe(instance)` reports positions from the rendered snapshot, corresponding Source client bones, and independently transformed rigid-weighted vertex samples. `presentationFrame` and `sourceError` appear in entity diagnostics. GetBonePosition can retain last-tick positions; GetBoneMatrix provides the current client palette ([Facepunch documentation](https://wiki.facepunch.com/gmod/Entity:GetBonePosition)).

## Eyes, materials and secondary contacts (0.5.0)

Generator 16 keeps the original 56 Source indices and all 18 physics-object indices. It appends `Eye_L` and `Eye_R` where those PMX bones exist, using their authored pivots and SCMI-compatible axes. `SetEyeTarget` is interpreted in the [ragdoll eyes-attachment coordinate space](https://wiki.facepunch.com/gmod/Entity:SetEyeTarget), reset by zero, and limited to 35 degrees vertically / 40 horizontally. Explicit external bone manipulation, including zero, wins over target rotation in the same update. The resulting native palette drives the MMD eye mesh; target and bone control are not accumulated twice.

Each asset mounts `materials-v4.gma`. Every material has a stable `mmd/<16-hex-asset-id>/<readable-material-name>_<slot+1>` VMT; full-mip RGBA VTF derivatives are shared by normalized texture hash. Shader defaults use the SCMI recipe. The renderer and editors share these VTF textures. Color/alpha and material morphs remain per-draw state. Native `GetMaterials` cannot enumerate [more than 128 entries](https://wiki.facepunch.com/gmod/Entity:GetMaterials), and [native submaterial setters use slots 0–31](https://wiki.facepunch.com/gmod/Entity:SetSubMaterial). MMD wrappers preserve slot order and use replicated overflow state while retaining native overrides where supported. AME's provider reads all slots without raising ordinary Source limits.

Bodygroup 0 is the empty carrier. Bodygroup `materialIndex + 1` has state 0 (visible, default) or 1 (hidden). The first 31 use native metadata; additional slots use the MMD adapter. Hiding applies to normal drawing, flashlight depth and sunlight shadow callbacks, with physics unchanged. The context property routes MMD overflow through a wider network index. Duplicates preserve complete material state.

Scene capture runs on the Source owning thread. Static triangle meshes and dynamic convex geometry are cached; immutable timestamped frames carry object transforms, velocities and owner identity. Each opted-in Bullet world culls proxies against swept character bounds and reuses shapes. An explicit external-contact filter bypasses authored masks only for dynamic-secondary versus external contacts; authored internal masks stay intact. Object deletion, teleports, mode changes and map cleanup discard stale proxies. Diagnostics include `collisionMode`, `sourceMirrors`, `externalContacts`, `sceneCaptureMs`, `sceneSyncMs`, `sceneSequence` and `feedbackApplied` (always zero).

Morph `name`, MMD index and native controller assignment remain preset identities. `displayName` uses confident SCMI mappings, otherwise the original authored name; `namingSource` distinguishes `scmi` from `authored`. Face Poser labels, overflow search and the MMD editor use display metadata. Derived carriers regenerate on demand. Development cache and duplication compatibility is not guaranteed; reimport old assets when necessary.

Collision repair welds near-duplicate vertices with scale-relative tolerances, rebuilds complete convex topology and validates winding, closed edges, finite coordinates and positive volume. Failed repair creates a bounded closed anatomical fallback with zero confidence/review metadata. Scaling and user corrections also rebuild topology, so a valid default hull cannot become an open or collapsed serialized hull at a smaller size.


## Development QoL update (0.6.0)

Engine path components are ASCII-safe: common Japanese/Chinese material terms become readable English, other names use Windows ICU romanization, and path punctuation is sanitized. Original authored UTF-8 names remain in metadata and editor labels. This avoids Source/third-party bytewise lowercasing corrupting UTF-8 paths (reproduced with 星 and 涟). A slot suffix distinguishes duplicate material names. The `names` registry maps short engine paths to internal content identities and rejects collisions before replacing cached data. AME resolves material and model paths through this registry.

`mmdhl.ResetPhysics(entity)` rebuilds the nanoem world, clears solver/contact history, pending catch-up and stopped-error flags, and resumes from Source's current primary pose. The server notifies the client to clear its presentation stop flag. It preserves the independent native ragdoll, its frozen limbs, morph/material state and collision mode.

`mmdhl.ResetAllPhysics()` is the client-local equivalent for all loaded MMD
actors and ragdolls; it returns the reset count. F8 is the configurable default
shortcut (`mmdhl_reset_all_key`); `mmdhl_reset_all_physics` can also be bound
through the console. Native `SetSecondaryTuning(iterations, gravity, damping,
stretch, tolerance)` no longer accepts or applies parameter conditioning.
Optional stretch projection respects external geometry; diagnostics include
`stretchContactClamps` and `stretchGuardMs`.

`mmdhl.library.Delete(ids, callback)` releases previews and removes matching live ragdolls before deleting generated cache data. It checks all paths against the cache root, refuses reparse points, and retains textures still referenced by other manifests. `cleanup.json` records only pending physical deletion; it is not a recoverable trash library. Reimport cancels pending deletion for reused paths. Original model/texture sources are never deletion targets.

Hair and clothing collide with the character and objects by default; the physics settings' checkboxes (client convar `mmdhl_collide_with`, from 2.2) add the world, living players and living NPCs, or remove the character's own body. Before scene capture, Lua classifies live players (`Alive()`), NPCs and NextBots (`Health() > 0`) on the engine thread. The native capture tags their VPhysics objects (and the owner's other shadows) as living players or NPCs, and each secondary world keeps only the kinds its flags ask for; a native module before 2.2 cannot tag them, so Lua excludes them before any native geometry reads. Ordinary/dead ragdolls are objects, and the owning carrier is still excluded per secondary world. Without the character flag, the pair filter drops contacts between the model's simulated bodies and its bone-following ones (and VRM spring colliders are ignored). In multiplayer each client names the kinds it needs and the server exports only those. Diagnostics report living actor/object counts as well as contacts and capture costs.

## Torso and bone pins (2.3.0, generator 31)

The chest (`Spine4`) and middle spine (`Spine2`) come from the PMX hierarchy instead of their names (`native/rig_torso.hpp`, issue #9): the chest is the bone holding the neck and both shoulders, `Spine1 < Spine2 < Spine4 < Neck1` runs up the body, and a bone that cannot be a pivot moves with a synthesized one as an alias, so every mapped carrier origin stays on its PMX bone. Loose name matching compares ASCII names only. The rig manifest records `torso` (`method`, `repairs`). The fit option `boneMap` pins carrier bones over a converted character's own map; it joins the fitted-rig cache key, and the fit of the pins alone is made once per model and pin set and then rescaled like the cached fit. `GetBoneMapProposal(assetId, optionsJSON)` (both realms) returns the fitter's choice without bodies, and missing landmarks throw `fit.landmarks` with every missing part and the names searched. `RigGenerator` is 31: 2.2 fits are refitted once, and the manifest's `torso` gives every carrier a new rig key and MDL header (`tests/fixtures/physics/golden.json` was re-recorded; the `.phy` text is unchanged). Details, the JSON contracts and `scripts/check-torso.py` are in [TORSO_FIT.md](TORSO_FIT.md).

## Carrier physics profiles (2.3.0)

The ragdoll physics editor (`docs/PHYSICS_EDITOR.md`) changes a carrier's `.phy` through two spawn options. Both are part of the fitted-rig cache key (`carrierFitKey`, `native/physics_profile.cpp`), and every value they change is written into the rig manifest, so it is hashed into the rig key: equal settings share one carrier, and an unedited model keeps the key, MDL and `.phy` bytes it had in 2.2 (pinned by `tests/fixtures/physics/golden.json`). `RigGenerator` did not change.

* `collisionOverrides[<ValveBiped name>]` keeps its `center` and `extent` and gains an optional `style`: `fitted` (default), `box` or `capsule`. A box or capsule replaces the fitted hull inside the same centre ± half size; the full refit's overlap shrink leaves styled bodies at their chosen size. Older modules ignore `style` and apply the same box to the fitted hull. A styled body records `style` in its manifest body.
* `physicsOverrides` is schema 1: a flat diff of effective values against the defaults, `{"schema":1, "surfaceprop", "massMode":"bias"|"volume", "automass":{"density"}, "collisions":{"mode":"none"} | {"mode":"custom","pairs":[[a,b],…]}, "animatedFriction":{"min","max","timeIn","timeOut","timeHold"}, "bodies":{<ValveBiped name>:{"limits":{"x":[min,max,friction],…}, "massBias", "damping", "rotdamping", "inertia", "drag", "surfaceprop"}}}`. Angles are degrees in the child bone frame (`[-360,360,f]` is a free axis, `[0,0,0]` a fixed one); friction is in `.phy` units (QC friction ÷ 5). Native canonicalises it strictly before the cache key: unknown fields, other schemas and out-of-range values are refused (`Invalid physics settings: <code> <path> <detail>`), numbers are quantised (angles 0.1, factors 0.001, density 0.1, times 0.01), values equal to the defaults are dropped, pairs are sorted. `{}`, `[]`, `null` and a profile that only restates the defaults are all "unedited". The `arms` role ignores it.

An edited manifest stores `physicsOverrides` (the canonical object), `physicsWriter` (1; bumped with any change to the text writer or the mass rule), the resolved `mass` and `massBiasTotal`, and per body `lower`, `upper`, `friction`, `massBias`, `rotationDamping`, `damping`, `inertia`, `surfaceprop`, `mass`, `volume` and `drag` when set. Bone surface properties in the MDL follow the body (or its nearest physical ancestor). Masses use the 2.2 weight shares, with VPhysics' 0.1 kg floor once edited, or studiomdl's volume × weight share with its 1 kg floor; `automass` sets the total from the hull volumes. `validateRig` checks the new body fields but never rejects a profile it cannot read: clients only render.

`GetCapabilities().physicsEditor` is 1 on modules that read these options. `PreviewCarrierFit(assetId, optionsJson)` exists in both realms and never writes files: it returns `nil, "Asset not loaded"`, `{"status":"error","errors":[{code,path,detail}]}`, `{"status":"pending"}` while a refit (height or excluded materials) runs off-thread (poll again; at most one refit runs per model and two in all, so a newer draft waits for the running one, and the last four finished results are cached; finished refits never block deleting the model), or `{"status":"ready","key","scale","m","unit","mass","canonical","bodies":[18 × {index,name,parent,bone,center,extent,hull,faces,style,volume,mass,massBias,damping,rotdamping,inertia,drag,surfaceprop,lower,upper,friction,confidence,needsReview}],"pairs":{mode,count},"penetrations":[{a,b,depth}],"phyText"}`. `phyText` is the exact text section a server build with the same binaries writes; `unit` is the shape unit (stature / 60).
