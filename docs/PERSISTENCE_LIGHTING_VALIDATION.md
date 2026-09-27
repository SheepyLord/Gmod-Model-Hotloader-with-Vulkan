# Persistence, addon compatibility and stacked surfaces — preview.9

Branch: `claude/mmd-importer-performance-87a36d`. Rig generator remains 30.
The release reuses existing PMX, texture and carrier caches. Native modules
require a game restart. No source PMX or supplied Blend file was modified.

## Duplicator, saves and replacement entities

- Cached model packages mount before Sandbox creates their physics. This fixes
  loading a saved ragdoll in a fresh process where its carrier was not mounted.
- Model-only entities bind their MMD metadata and client presentation through
  the existing engine class. Standard factories, constraints, traces, physics
  objects, cleanup and bone indices stay authoritative.
- `duplicator.DoGeneric` supports copying MMD appearance even when an addon
  does not call `ApplyEntityModifiers`. Citizen/Combine conversions select an
  animated carrier before the caller spawns the NPC.
- Dupe state version 4 captures asset/rig identity, morphs, manual poses,
  complete material/bodygroup state and eye state. Transient native handles
  and rendering data are excluded. Re-capture clears the old modifier first,
  so reset overrides cannot reappear through table merging.
- Material-slot keys work as strings in memory and as numbers after
  `util.JSONToTable` loads a disk save.
- Fixed a native lifetime error: iterating `json(...).items()` referenced a
  destroyed temporary, causing restored submaterials to fail rendering.
- The public generated VMTs are now the actual rendering materials, so edits
  through `Material(entity:GetMaterials()[slot])` affect the visible mesh.
- Preserved existing copy callbacks and moved the physgun-held LOD hooks into
  the reachable server initialization path.

Entity-oriented helpers: `mmdhl.EnsureModel(path)`,
`mmdhl.BindEntity(entity, optionalState)`,
`mmdhl.CaptureNativeState(entity)` and `mmdhl.StoreNativeState(entity)`.
These do not grant access to unapproved multiplayer assets.

## Flashlight overlap correction

The provided `Selection.blend` was inspected read-only in background Blender
with embedded script auto-execution disabled. Its selected region contained
135 vertices and **64 same-material triangle pairs** with coincident positions
and winding. Vertex weights differ, so permanently deleting copies is unsafe.
The full PMX also contains overlapping material layers, including `手套` and
`髮+`, whose different alpha masks must remain independent.

GMod's [RenderFlashlights](https://wiki.facepunch.com/gmod/render.RenderFlashlights)
draws additive light passes. Equal-depth surfaces can therefore add a light more
than once even though the base opaque pass displays only the top surface.

The fix has two parts:

1. Same-material triangle candidates are identified once at model load, then
   checked against the current deformed positions and UVs. Only redundant
   flashlight primitives are skipped. A morph or bone pose that separates
   the surfaces restores both. Winding, cutouts and first-person masks are
   respected. The base, shadow and source model geometry is retained.
2. Known overlapping materials receive a consistent, small projection-depth
   ordering in the base and flashlight passes. The depth buffer chooses the
   visible layer per pixel/sample, including alpha-test holes. The allowance
   tapers with distance and is bounded to a small world-equivalent amount,
   with a floating-point precision floor. World vertices, normals, UVs,
   skeletons and physics are not moved. Projection state is restored after
   each draw; the implementation does not clear or reserve other addons'
   stencil buffers.

Original toon, Phong and rim settings are retained. The earlier diagnostic
specular reduction was discarded after the stacked-geometry cause was
confirmed. Frozen mesh caches also distinguish vertex formats, preventing a
depth-only layout from being reused for a different shader.

The guard defaults on and is available in Physics & Performance, or through
`mmdhl_flashlight_overlap_fix 1`. Native diagnostics expose
`flashlightDuplicates` and `overlapCheckMs`. Setting the convar to 0 allows
controlled comparisons. Intentional translucent/additive materials retain
their normal rendering behavior.

## Validation

Owned Windows x64 single-player sessions in `gm_construct`, visible
2560×1440, with the enabled folder and Workshop addons.

- Warm duplication preserved 18 native bodies, finger manipulation, per-limb
  freezing, morph weight/scale, and material/bodygroup slots above 31.
- Reset appearance did not resurrect stale modifiers.
- A fresh-process paste began with the carrier absent from the GAME search
  path, then restored its model, all 18 bodies, overflow appearance and weld.
- Actual `gmsave.SaveMap` / `gmsave.LoadMap` restored a ragdoll and Citizen.
  Both obtained client instances; rendering with restored slot-35 overrides
  completed without the former JSON error.
- Tested the actual `other_ragdoll_respawn_tool` SWEP from Workshop
  **2292308929 (Ragdoll To NPC)**. It removed the original and created an MMD
  Citizen with the normal 687-sequence animation inventory.
- Compared the supplied compiled Furina, imported Furina, and guard on/off
  captures. Reviewed face, hair, gloves, legs and alpha-layer visibility.
  Hiding `手套` / `髮+` revealed the underlying surface correctly. Submaterial
  editing remained functional.
- Native tests: **17/17 passed**, including animation/UV separation, winding,
  visibility, material-layer ordering, prior physics regressions and actor
  metadata. Five Python/GLua regression suites passed.

Two **10-second measurements after warm-up**, identical camera and projected
light, full **64,031 vertices / 102,680 triangles / 356 bodies / 475 joints**:

| Guard | Frame p50 | Frame p95 | Focused frames |
|---|---:|---:|---:|
| Off | 3.23 ms | 4.94 ms | 2,803 / 2,803 |
| On | 3.82 ms | 5.31 ms | 2,632 / 2,632 |

The selected face retained one contribution for each of its 64 duplicate
pairs. In fixed image regions, fully white pixels fell from 5.84% to 0.35%
on the face and from 54.13% to 3.72% on the legs. These are image diagnostics,
not a photometric fidelity metric. The largest sampled overlap-check cost
was 0.10 ms.

Private evidence is under `validation/`: `selection-report.json`,
`overlap-comparison.json`, `overlap-performance-summary.json`, token-scoped
RPC records and captures. Temporary camera/debug hooks and test settings
are restored when the owned session stops.

## Compatibility boundaries

EEER's enabled `sv_rpe_wound_grab_clear_constraints_enable` setting deliberately
calls `constraint.RemoveAll` shortly after spawning. This was observed on
ordinary compiled ragdolls too. The importer preserves pasted constraints;
it does not override that addon policy. Draconic Base also reset a test
client's NoDraw flag; the renderer honors the current owner flag rather than
fighting another addon's explicit change.

This update was validated in single-player. Multiplayer and arbitrary
third-party entity classes/shaders are not universally certified. The overlap
guard targets coincident copied triangles/material layers; it is not a repair
for intersecting, noncoplanar geometry or arbitrary transparent compositing.
