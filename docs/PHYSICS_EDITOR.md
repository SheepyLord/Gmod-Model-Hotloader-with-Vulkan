# Ragdoll physics editor (2.3.0)

The ragdoll physics editor changes how a Model Hotloader ragdoll moves: how stiff
and how far its joints bend, how it falls, what it weighs, what it sounds like,
which parts collide with each other and the shape of each collision part. It
works on the 18-part ragdoll ("carrier") that Model Hotloader builds for every
model. A change rebuilds that ragdoll's physics file (`.phy`); the model, its
look and its pose stay the same.

Native details (the `physicsOverrides` option, the manifest and
`PreviewCarrierFit`) are in [NATIVE_BACKEND.md](NATIVE_BACKEND.md#carrier-physics-profiles-230).

## Opening the editor

* Right-click a Model Hotloader ragdoll in the context menu (hold C) and choose
  **Ragdoll physics…**.
* Or open **Edit character** on the ragdoll and press **Ragdoll physics…**
  (NPCs and player models show the button disabled: spawn the character as a
  ragdoll to edit it; physics saved for new spawns also apply to them).
* Or look at the ragdoll and run `mmdhl_physics_editor_open` in the console
  (`mmdhl_physics_editor` is the server setting below).

The window docks on the right. The ragdoll's collision parts are drawn in the
world; click a part to select it. Hold the right mouse button to look around
the ragdoll, **F** focuses the selected part and **Home** resets the camera.
**Ctrl+Z** / **Ctrl+Y** undo and redo, **Ctrl+Enter** applies, **Ctrl+Tab**
moves to the next tab and **Esc** closes (it asks first when there are unapplied
changes).

## How changes are applied

Nothing in the world changes while you edit: you edit a draft, and the preview
and the **Checks** strip update as you go. When the collision has to be fitted
to the model again (after changing *Fit to model parts*, or on a ragdoll with
parts left out of the fit), the editor shows *Re-fitting collision to the
model…* for a few seconds; overlap checks, **Apply** and **Test copy** wait for
it.

* **Apply** builds a new ragdoll with the draft's physics and swaps it in for the
  old one. Pose, velocity, frozen state, skin, colour, material, bodygroups,
  welds and ropes, owner, undo and cleanup entries carry over. Welds or ropes
  that cannot be re-created are reported.
* **Test copy** spawns a copy with the draft's physics next to the ragdoll,
  labelled TEST COPY, so you can throw it around first. Each player has at most
  one; it is removed when you close the editor or leave the server.
* **Previous version** goes back one step (up to 10 steps per ragdoll).
* **Reset** rebuilds the ragdoll with the automatic (factory) settings, or with
  the model's saved settings when there are some.
* After Previous version or Reset the editor shows what the ragdoll has now;
  unapplied changes are dropped. After Apply it keeps your draft, including
  edits made while the ragdoll was building.
* **Discard changes** throws the draft away.

Each distinct set of physics is its own carrier (about 350 KB, cached): equal
settings share one carrier, so presets and Previous version are quick. The
footer shows the carrier key before and after (`Carrier abc → def`). An edit
that changes nothing physical reuses the current carrier. Carriers are never
deleted automatically, because saves and dupes may refer to them.

## Tabs

**Feel** is for everyone:

* *Starting points*: Default, Less floppy, Posing doll, Relaxed, Extra floppy,
  Statue, and *From another model* (see below).
* *Stiffness* (Floppy … Stiff), *Range of motion* (Locked … Very loose) and
  *Floatiness* (Normal, Slow fall, Floaty).
* *Weight* (total kg) and *Material*, which sets the impact sounds and grip
  (skin, rubber, wood, metal, ice and the other surface materials the game has).

**Parts** shows the ragdoll as a diagram; select a part there or in the world.

* *Shape*: Shorter, Longer, Thinner, Thicker and Reset shape. With
  *Same change on the other side* (on by default) the mirrored part changes too.
* *Joint*: the bend range and stiffness for this joint only (or *Same as whole
  body*), *Hinge only* (elbows and knees: bends one way, no twist) and *Lock this
  joint*. The hips are the root and have no joint.

**Collisions**:

* *Body parts and each other*: bump into each other (default), pass through
  each other (fewer jitters, but limbs can cross), or custom pairs.
* *Collision thickness* for all parts (±5 % per click). Fatter shapes stop the
  skin from clipping into floors.
* *Overlaps* lists parts that overlap at spawn; **Fix overlaps** shrinks them
  until they no longer do.
* *Fit to model parts*: untick big clothing such as skirts, capes or wings so
  the collision follows the body.

Tick **Advanced** for the two tabs below. They use the same names as studiomdl's
QC commands.

**Numbers** (the selected part):

* Centre and half size in Source units, and the style: *Fitted to model*
  (default), *Box* or *Capsule*.
* The joint's three axes (X twist, Y tilt, Z bend), each Limit, Locked or Free,
  with minimum and maximum degrees in the part's own frame (0° is the imported
  pose) and friction in QC units (the `.phy` stores one fifth of it). Axes
  marked ≈ have not been verified in game yet: their arc in the world may be
  mirrored. A joint preset menu sets all three at once.
* Weight share (`$jointmassbias`), linear damping (`$jointdamping`), rotational
  damping (`$jointrotdamping`), resistance to spinning (`$jointinertia`), air
  drag (`$drag`, approximate) and the part's surface material.
* **Copy to other side** and **Reset part**.

**Model**:

* Total weight, or *Compute from shape volume* (`$automass`) with a density,
  and the distribution: by weight shares (the Model Hotloader default) or by
  shape volume × weight share, like studiomdl.
* The material for the whole model, the self-collision pair grid (joined parts
  never collide) and *Animated friction*, which stiffens NPC and player death
  ragdolls and then relaxes them (ragdolls you spawn ignore it).
* **Copy as QC**, **Paste QC…** and **View .phy text** (the exact text the
  server will write; it needs the 2.3.0 module on your computer).
* Whether the model has saved physics on this server, and **Forget saved
  physics…**.

## Checks

The Checks strip lists problems with the draft. Errors (a value out of range, a
minimum above its maximum, an unknown surface material, a severe overlap with
collision on) block Apply and Test copy. Warnings (very light parts, large
weight ratios between neighbours, a limb whose imported pose is outside its
range, unlimited axes, very high friction or damping, thin shapes) do not. Most
come with a one-click fix such as Clamp, Swap, Include the starting pose, Limit
to natural range, Even out weights or Make it thicker; **Go to** selects the
part.

## Copying physics from another model

*From another model* (Feel tab) or **Copy physics from another model…** reads
the physics of an installed model: player models, Half-Life 2 characters, a
ragdoll picked in the world (click it; **Esc**, the right mouse button or a
click beside it cancels), or a model path. It reads the model's `.phy` file
from the game's mounted content (or the text section the game reports for it),
maps its parts to the 18 carrier parts by bone name and says what it copied. A
model whose spine has fewer parts gives one part's range to two carrier parts
(split between them). Choose what to copy: joint ranges, joint friction,
damping/inertia/material, weight shares, total weight, self-collision rules and
NPC corpse stiffening. Directions are approximate when the other model's bones
are oriented differently, so check the arcs after copying.

## QC

**Copy as QC** puts a `$collisionjoints` block on the clipboard with
`$mass` or `$automass`, `$rootbone`, `$surfaceprop`, `$jointsurfaceprop`,
`$jointconstrain`, `$jointmassbias`, `$jointdamping`, `$jointrotdamping`,
`$jointinertia`, `$noselfcollisions` or `$jointcollide`, and
`$animatedfriction`. Per-part air drag cannot be written in QC; the block says
so in a comment.

**Paste QC…** accepts a `$collisionjoints` block, a whole `.qc` file or single
`$joint…` lines, starting either from studiomdl's defaults (what studiomdl
would compile) or from the current settings. Every line is listed as used,
ignored (with the reason: not one of the 18 parts, not supported with one convex
shape per part, studiomdl uses the first line for an axis, …) or an error.
Collision meshes, `$collisiontext`, `$concave`, `$jointmerge`, `$jointskip` and
similar commands are reported and ignored; bone names without the `ValveBiped.`
prefix are accepted.

## Saved physics for new spawns

**Save for new spawns…** stores the physics the ragdoll has *now* (not unapplied
changes; *Apply and save* applies first) as that model's default on the server.
New ragdolls, NPCs and player models of that model then spawn with it; ragdolls
already in the world do not change. The save goes to
`data/mmd_hotloader/fit_overrides/<asset>.json` (version 3, generator 18) next to
the collision corrections from the collision editor, with the fields `physics`,
`mass` and `editor` (who saved it and when). It is written to a temporary
`<asset>.new.txt` first, so a failed write keeps the previous default. The collision editor's *Save fit*
keeps these fields. Workshop packages carry the file without the saver's name
and SteamID; on dedicated servers it is installed from the package when there is
no local one.

The same file holds the bone window's pins (`boneMap`, `boneMapVersion`,
`boneMapSavedAt`, see [CHARACTER_IMPORT.md](CHARACTER_IMPORT.md)). Saving and
**Forget** change only the physics fields: the pins stay, and a file with only
pins counts as no saved physics. Every build is fitted with the saved pins, and
so is the editor's preview. Shapes were made for the bones a ragdoll was fitted
with: after the pins of its 18 body parts changed, **Test copy**, **Apply** and
**Save for new spawns** are refused (*use Reset to rebuild it*), and a version
made with the old pins is not offered as **Previous version**; **Reset** and
**Restore saved** rebuild it with the current pins. Other pins (fingers, toes,
neck, middle spine) leave the shapes valid, as they leave the saved corrections
(`mmdhl.boneMapper.SamePhysicalPins`).

The collision editor's *Save fit and spawn corrected copy* is a build under the
same rules as a test copy: `mmdhl_physics_editor`, prop protection and
`MMDHLCanEditPhysics` (operation `test`), the value checks, the ragdoll limit,
the cooldown and the budget. Like every editor build it is a ragdoll, also from
an NPC's corpse.

If a saved profile cannot be built (for example on an older server module), the
model spawns with automatic physics and the player is told. A saved profile from
a newer addon is ignored with a notice, never half applied.

## Servers

| Convar | Default | Meaning |
|---|---|---|
| `mmdhl_physics_editor` | 1 on dedicated servers, 2 otherwise | 0: nobody may change physics; 1: admins only; 2: anyone who may edit the ragdoll |
| `mmdhl_physics_editor_cooldown` | 3 | seconds between two builds of one player |
| `mmdhl_physics_editor_budget` | 20 | builds per player per 10 minutes (0: unlimited; superadmins are exempt) |

All three are replicated, so players see why the editor is read-only. Anyone may
open the editor and look; changing a ragdoll also needs the sandbox edit
permission (`CanProperty` with `mmdhl_physics`, so prop protection decides about
other players' ragdolls). Saving or forgetting a model's default is for single
player, the listen server host and admins.

| Action | Single player | Listen host | Admin | May edit the ragdoll (mode 2) | Others |
|---|---|---|---|---|---|
| Open, view, Copy as QC | yes | yes | yes | yes | yes (view only) |
| Test copy, Apply, Previous version, Reset | yes | yes | yes | yes | no |
| Save for new spawns, Forget | yes | yes | yes | only if a hook allows it | no |

Each build creates one carrier (about 350 KB), one mount and one approved rig;
the cooldown and the budget bound how fast that grows. Requests are compressed
JSON of at most 60 000 bytes on the `mmdhl_physics` message; values are checked
again on the server, which also refuses ragdolls that are held, busy, removed or
changed since the editor opened. Opening the editor is answered at most four
times a second per player. A test copy that finishes building after its editor
closed is removed at once.

Client convars (saved): `mmdhl_physics_editor_advanced` (show Numbers and
Model), `mmdhl_physics_editor_mirror` (same change on the other side),
`mmdhl_physics_editor_camera` (orbit camera), `mmdhl_physics_editor_actual` (also
draw the game's actual collision) and `mmdhl_physics_editor_coached` (the
first-use tip was shown).

## For addon authors

Hooks (server):

* `MMDHLCanEditPhysics(ply, ent, op)`: return `false` to deny an operation
  (`test`, `apply`, `previous`, `reset`, `restore_saved`, `save_default`,
  `clear_default`). The collision editor's corrected copy asks with `test`.
* `MMDHLCanSavePhysicsDefault(ply, asset)`: return `true` or `false` to override
  who may save and forget a model's default.
* `MMDHLPhysicsApplied(ply, oldEnt, newEnt)`: called after a rebuilt ragdoll
  replaced the old one (before the old one is removed).

Functions:

* `mmdhl.OpenPhysicsEditor(ent)` and `mmdhl.GetPhysicsEditor()` (client).
* `mmdhl.PhysicsRequest(op, ent, payload, callback)` (client) sends one
  operation and returns its request number. The callback receives
  `(state, message, entity, data, entityIndex)`: `building` while the server
  works, then once `ready` or `error` (`error` with *No answer from the server*
  after 45 seconds).
* `mmdhl.physics` (shared, no UI) is the profile model: defaults, presets,
  `Canonical` (the same canonical form as the native module, checked against
  the shared cases in `tests/fixtures/physics/canonical_cases.json`), `Resolve`,
  `Validate`, `Check`, `ParseQC`, `EmitQC`, `ParsePhyText` and `FromTemplate`.
* `mmdhl.SavePhysicsDefault(ply, ent)`, `mmdhl.ClearPhysicsDefault(ply, ent)`,
  `mmdhl.LoadSavedFit(asset)` and `mmdhl.ReplaceRagdoll(ply, old, new, op)`
  (server). `mmdhl.Spawn` and `SpawnNative` take an optional `flags` table;
  `flags.replace` skips the creator, undo and cleanup registration that
  `ReplaceRagdoll` carries over instead.

## Older versions

The editor never refuses to open:

* With an older module on **your computer**, the preview is approximate (no
  exact overlap check and no `.phy` text) and the editor says so.
* With an older module on the **server** (no `physicsEditor` capability), only
  collision shapes can be changed; the rest of the editor is read-only and the
  server owner is asked to update. Box and capsule styles then apply as the
  fitted shape in the same box.
* Ragdolls and dupes made without physics edits get exactly the carrier of a
  model nobody edited. (2.3.0 fits every model once more for its chest, see
  [TORSO_FIT.md](TORSO_FIT.md); the physics editor itself changes nothing
  there. Ragdolls, saves and dupes made with 2.2 keep their 2.2 carrier until
  they are spawned again; an edit applied to one builds a new 2.3.0 carrier.) A
  dupe whose physics cannot be built is restored without them.

## Limitations

* The ragdoll always has the 18 carrier parts and 17 joints, each part one
  convex shape. Parts cannot be added, removed, merged or re-parented, and QC
  collision meshes are not imported.
* Only elbow and knee bend directions are verified in game; other axes are
  marked ≈ until they are.
* Animated friction only affects NPC and player death ragdolls.
* Air drag and computed (`$automass`) weights use simplified shape volumes.
* View models (`c_arms`) ignore physics edits.
