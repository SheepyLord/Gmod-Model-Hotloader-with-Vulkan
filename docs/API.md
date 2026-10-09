> Historical documentation for the legacy custom-entity backend (0.2.0 and earlier). See [native carrier architecture/API](NATIVE_BACKEND.md) and [current validation](NATIVE_VALIDATION.md) for 0.3.0.

# Native / Lua API

`require('mmdhl')` exposes `mmdhl_native`; the addon aliases it as `mmdhl.native`. Structured return values are JSON strings; `mmdhl.Decode(value, error)` converts them to Lua tables. Failed calls return `nil, error`. Most successful mutators have no return value. Handles and material/bone/morph indices are zero-based where stated; instance handles start at 1 and become invalid on removal.

## Assets and worker jobs

Client: `Browse() -> job`, `BeginImport(absoluteUTF8Path, optionsJSON) -> job`, `Reload(assetHash) -> job`, `PollJob(job) -> JSON`, `CancelJob(job)`.

Jobs progress through `running`, then `selected` (picker), `complete`, `failed` or `cancelled`. From 2.3.0, `BeginImport` also takes the character kinds `character_probe` and `character` for FBX, glTF and DAE files, and failed jobs carry `errorCode`, `errorDetails`, `context`, `stageCode`, `exitCode` and, after a crash, the end of the worker's crash log (see [Why an import failed](IMPORT_ERRORS.md#statusjson-of-a-failed-job)); see [Characters from FBX, glTF and DAE](CHARACTER_IMPORT.md#worker-jobs), which also documents `InspectBoneMap` and the `bonemap` action. A selected result carries `source`; a completed import carries `asset` and `info`. Only one worker runs at once. It has no memory limit of its own (running out of memory fails the import with code `memory`), and after five minutes `PollJob` adds a `warning` but keeps waiting; the player can cancel. Cancelling kills that job's process group. A job's scratch folder under `data/mmd_hotloader/jobs/` is removed once the job finishes or is cancelled. The module keeps the results of the 16 most recent finished jobs, and the client removes job folders more than a day old that an earlier session left behind. Registry source paths are local-only.

Both realms: `RequestAsset(hash)` asynchronously verifies and parses a cached model. Poll `AssetInfo(hash)` until it returns a table or an error. Cache paths are `data/mmd_hotloader/assets/<hash>/` and `textures/<hash>.png`. Models, manifests and textures are checked before use. The parser rejects a model with a non-finite or out-of-range number (vertices, bones and IK, materials, morphs, rigid bodies, joints, soft bodies; VRM spring parameters) before anything is cached. A zero-length normal is replaced by its adjacent faces' normal, or by up when those are degenerate too. The asset hash includes the source and texture manifest, so an explicit reload can create a new revision without invalidating an existing live instance.

Client preview: `CreatePreview(hash) -> handle`, `DrawPreview(handle, materialIndex, runtimeMaterialName, edgeShell)`, `ClearPreview()`. Preview physics lives in a separate frozen world and does not add server entities.

## Instances

Server: `CreateInstance(hash, optionsJSON) -> handle`. The normal UI preloads asynchronously; a cold cached duplication can load synchronously. Options:

```json
{"position":[0,0,64],"angles":[0,180,0],"height":72,"mass":70,"frozen":true,"ragdoll":true}
```

Position, optional `center`, and height are Source units. Angles are Source pitch/yaw/roll degrees. `center` aligns the model's initial bounds to a requested center and is used for reload/duplication. `mass` controls generated primary bodies; authored body masses are preserved. `coreBones` can override humanoid bone selection with a list of zero-based indices. `scale`, if provided, specifies metres per MMD model unit and overrides height.

`DestroyInstance`, `ResetPhysics`, `SetFrozen`, `SetMorph(handle, zeroBasedIndex, weight)`, `SetBonePose(handle, zeroBasedIndex, poseJSON)` are server mutators. A pose contains `translation:[x,y,z]` in MMD units and `rotation:[x,y,z,w]` as a quaternion. Morph and undriven finger/eye edits preserve the existing ragdoll pose and defer skinning until rendering. Frozen manual bone edits transform only that bone's descendants, including affected collision bodies and soft nodes. Dynamic bone drivers continue to own their transforms.

`GetState` / `SetState(handle, stateJSON, newCenterVector)` save and restore rigid transforms, manual poses, morph weights, frozen state and soft nodes. They clear velocities. The model/rig must match. `GetBounds` supplies current Source-space bounds; `GetDiagnostics(handle)` reports bodies, mappings, warnings and `jointLimits` (bone/parent, lower/upper/current angles in degrees and pivot separation in Source units). With no handle, diagnostics report world counts, timing, dropped catch-up time and tick number.

`mmdhl.Spawn(player, hash, options, callback)` wraps creation in the custom entity, undo, creator, and cleanup registration. Prefer it to creating bare native instances when building addon tools.

## Physics bridge and manipulation

Server `CapturePhysics()` applies the previous tick's feedback, removes disappeared Source objects, and captures collider geometry/transforms. Then `Step(seconds, SourceGravityVector)` advances Bullet at 120 Hz with at most eight catch-up steps. The addon owns this Tick loop; do not add a second loop.

`Raycast(startVector, endVector, optionalInstance, coreOnly=false)` returns the nearest grabbable MMD rigid body, respecting Source mirror occlusion. Authored kinematic follow colliders and soft nodes are not selected. `BeginGrab(instance, bodyIndex, point)`, `UpdateGrab(point)`, `EndGrab()` control a single grab constraint. Unfreeze before grabbing. Supplying an instance restricts the ray to its bodies; Source occlusion is then handled by the engine trace. `coreOnly=true` selects humanoid bodies beneath clothing.

`BeginPhysgun(instance, bodyIndex, SourceHandlePosition, Vector(pitch,yaw,roll))` unfreezes the instance and starts a bounded point constraint plus angular drive. `UpdatePhysgun(position, anglesVector)` updates that handle, and `EndGrab()` releases it. Translation impulse is sized for the complete rigid payload, not just the selected hand/head. No body is teleported through its anatomical constraints.

Both realms can read `GetBoneTransform(instance,index)` (Source position and pitch/yaw/roll) and `GetMorphWeights(instance)`. `AssetInfo` now includes bone rest positions and `morphInfo` category/type fields. The entity adapters provide the flex/bone/eye APIs used by the installed Sandbox stools; class-scoped Entity dispatch is necessary because engine methods take precedence over SENT methods. Other entity classes retain their original API behavior.

The entity owns one non-colliding Source physics handle (`SOLID_OBB` plus `TestCollision` against Bullet). It is automatically excluded from `CapturePhysics`, so it cannot collide with its own Bullet rig. This does not expose a Source ragdoll physics-bone array. Right-click freeze currently freezes the whole MMD instance.

`SetMirror`, `RemoveMirror`, `TakeImpulses`, `ProbePhysics` and `Clear` are developer-level functions. Mirror geometry uses packed native float XYZ values in Source units. See `docs/ABI.md` for units and ownership. Never feed returned impulses back twice.

## Rendering

Client `Draw(handle, materialIndex, '!runtimeMaterialName', edgeShell)` streams already-deformed native vertices to a Source mesh. Native draws run on Source's render thread when the material system is queued (`mat_queue_mode 2`) and inline otherwise ([MULTICORE_RENDERING.md](MULTICORE_RENDERING.md)). `mmdhl.DrawInstance` prepares shader parameters from Lua and renders complete passes, so it still selects immediate rendering; a direct caller that sets material parameters from Lua around a draw must arrange `mat_queue_mode 0` first. No vertex tables cross Lua. `RenderStatus()` identifies the active renderer or last native buffer status.

Native DLLs are not hot-unloaded. Restart the game after changing native code or compiled shaders; use `scripts/reload-lua.py` for Lua-only changes.

Frozen instances use native indexed GPU meshes, shared between surface and shadow
passes, with a separate outline mesh. Pose/morph snapshot changes invalidate the
cache. `RenderStats()` returns cached byte/buffer counts and cumulative build/draw
counts; `PruneRenderCache(false)` releases expired snapshots and is called once per
frame by the addon. `PruneRenderCache(true)` releases every cached mesh. Unfrozen
instances continue to stream indexed deformed geometry. `Step` advances physics
and marks the pose dirty; it no longer skins vertices on every server tick.
Client `PrepareFrame()` publishes dirty snapshots once before rendering and
returns deformation time in milliseconds. `Draw` also ensures a fresh snapshot
for direct API callers. `GetState` refreshes its snapshot before saving. Bounds
are from the most recently published snapshot, normally the previous rendered
frame when read by the server.
