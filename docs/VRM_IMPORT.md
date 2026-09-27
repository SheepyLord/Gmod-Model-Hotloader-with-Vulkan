# VRM characters

**Q → External Models → Character Models → Import Character Models** now accepts `.vrm` files (VRM 0.x and VRM 1.0) next to PMX and PMD. A VRM avatar becomes the same kind of character as an MMD model: a posable ragdoll, a friendly or hostile NPC, or a player model, with Face Poser expressions, bodygroups, the collision editor and multiplayer sharing. Its hair, skirts and accessories move with the avatar's own **VRM spring bones**, simulated natively as the VRM specification defines them, instead of being approximated by MMD rigid bodies.

## Using it

- Pick a `.vrm` in the character importer (the picker lists PMX, PMD and VRM). The import takes 1–7 s for the avatars tested, including textures, the fitted ragdoll and Source materials.
- The library row shows the file, the VRM version and its licence, for example `Mafuyu_VRM.vrm · VRM 0.x · Redistribution prohibited`. The tooltip adds the author, the licence URL and the spring-bone counts.
- VRM expressions are ordinary morphs. The vowels use the MMD names, so they get the usual flex controllers: `あ い う え お` → `mouth_a`…`mouth_o`, `まばたき` → `blink`, and `ウィンク２`/`ウィンク２右` → `eye_blink_left`/`eye_blink_right`. Emotions keep their VRM 1.0 names (`happy`, `angry`, `sad`, `relaxed`, `surprised`), and the avatar's raw blend shapes follow. Expressions come first in Face Poser.
- A `.vrm` chosen in **Static Props** is refused, with an **Import as character** button.
- **Physics & Performance → VRM: hair and clothing keep their shape while moving** (`mmdhl_vrm_relative_damping`, default on) chooses how spring damping treats locomotion (see [Moving characters](#moving-characters)). **Hair and clothing collide with** applies to VRM spring bones as it does to MMD physics. The accuracy slider's **Off** stops spring bones. Every other level, including Jiggle, simulates them fully, because they are already a lightweight solver.
- In multiplayer, **Share model with server (admin)** asks first when the VRM licence forbids redistribution, or doesn't say.

## How spring bones are handled

VRM and MMD describe secondary motion differently. MMD authors place rigid bodies (spheres, boxes, capsules) with masses and 6-DOF spring joints, and a physics engine solves them. VRM authors instead set, per chain, a **stiffness** (pull back toward the rest shape), a **drag** (fraction of velocity lost per step), a **gravity power** and direction, and a **hit radius**, plus sphere and capsule colliders on body bones. Every VRM runtime (UniVRM, three-vrm, VRoid Hub, VSeeFace) moves the chain tails with the same Verlet step, keeps each bone at its length, pushes tails out of colliders and rotates the bones toward their tails. Authors tune those numbers by looking at that exact step.

Two ways to bring this into the addon were considered.

1. **Convert spring bones to MMD rigid bodies and joints** (what VRM→PMX converters do). This reuses the Bullet world as it is. But the VRM parameters have no physical meaning Bullet can reproduce:
   - Most VRoid and UniVRM hair has **gravity power 0**. Bullet's gravity is global, so converted hair sags unless its springs are overtuned.
   - VRM stiffness pulls toward the *animated* rest direction by a fixed distance per second. Bullet 6-DOF springs are torques that depend on invented masses and inertias.
   - VRM hair chains have 5–10 short segments, and some models have 350 joints. As Bullet bodies these need hand tuning to avoid jitter.
   - VRM colliders only push chain tails, and chains never collide with each other. Bullet bodies collide unless every pair is masked.

   The result would differ visibly from what the author saw, and would cost far more than the original.
2. **Simulate the VRM spring step natively** (chosen). It reproduces the authored motion by construction, and is small and cheap:
   - 112 joints cost 0.13–0.18 ms per 60 Hz tick including world contacts; 263 joints cost 0.37 ms.
   - It cannot explode, because each bone keeps its length.
   - It fits the existing secondary pipeline: it runs inside the same fixed 60 Hz ticks as the Bullet world, on the same worker thread, with the same asynchronous presentation, adaptive quality, reset and diagnostics.

`native/spring_bones.cpp` implements the VRMC_springBone 1.0 step:
- inertia × (1 − drag), stiffness toward the parent-relative rest direction, gravity;
- the length constraint;
- sphere and capsule colliders (push out, then restore the length), in order;
- bones rotated to their tails parent-first;
- `center` spaces.

VRM 0.x bone groups use UniVRM's rules: every node under each root is a joint, its first child is the tail, and a leaf gets a virtual tail 7 cm along its parent-to-node line. VRM 1.0 springs pair each joint with the next one; the last joint is only a tail. A native test runs an independent implementation of the published step beside the solver on a swaying, turning chain: they agree within 5 × 10⁻⁶ PMX units, with and without colliders.

Between ticks the frame blends each spring bone's rotation *relative to its parent*, so hair stays attached to the head at any frame rate. The pose of the Source ragdoll, NPC or player drives the chains, like MMD physics.

### Moving characters

VRM damping acts on world-space velocity. That is harmless for an avatar standing in front of a camera, but a character that simply walks drags every chain behind it: the tail keeps only (1 − drag) of the body's motion each step. With common VRoid hair settings (stiffness 0.4–0.8, drag 0.4) the root segment trails 45–63° at 2 m/s. VRoid exports set `center` to the avatar root to cancel this in Unity, where that root moves with the character. In GMod no bone plays that role: the root of a ragdoll does not move, and an NPC's motion lives in its hips. UniVRM exports usually have no centre at all.

The addon therefore measures damping against the character's hips: inertia = hips step + (tail step − hips step) × (1 − drag).
- A standing avatar behaves exactly as authored, because the hips step is zero.
- At steady speed, walking or a thrown ragdoll no longer bends the chains.
- Starting, stopping, turning, impacts and every animated body motion still swing them, because the tails keep their own velocity.
- A spring whose centre is an animated bone (hips, chest, head) keeps the VRM centre-space behaviour.

Turning the setting off restores plain VRM damping.

Measured on the VRoid avatar as a citizen NPC walking at 80 units/s (about 2 m/s), as hair-tip displacement from the standing shape in the head's frame:

| Damping | Mean | Worst |
|---|---|---|
| Relative to the character (default) | 0.30 units | 1.55 units |
| Plain VRM (setting off) | 4.61 units | 15.35 units |

With plain VRM damping the long hair streams out horizontally behind the walking NPC. With the default it hangs down her back and sways with the walk. The native test gives the same result for a synthetic chain at 2 m/s: 38.7° of trail with world damping, 0° relative to the hips, and 0° with a hips centre.

### World contacts

A ragdoll lies on the floor constantly, and hair falling through it is the most visible failure VRM runtimes never meet. In the collision modes that mirror the map (and objects), each joint tail is kept out of that geometry as a sphere of its hit radius, at least 2 cm.

The map is a hollow triangle mesh, so a sphere already below the floor would be pushed further down. To avoid that:
- The side counted as outside is taken from the joint's nearest bone driven by the Source skeleton (head, chest, hips). That bone lies inside a ragdoll hull, so it is always in open space.
- A strand whose root has sunk slightly into the floor still rises, because the ragdoll's hulls are a little smaller than the mesh.
- The line from that bone to the tail may not cross a surface.
- A tail that touches or crosses a surface slides along it at the full bone length.

VRM colliders still act first, exactly as authored. Contacts are one-way: hair does not push props.

Measured with a dropped ragdoll lying on the pavement, counting spring tails below the floor:

| Avatar | Collision on | Model only | Switched back on |
|---|---|---|---|
| VRoid (76 tails) | 0; lowest tail 0.8 units above | 28, down to 8.6 units | 0 |
| Mafuyu (169 tails) | 0 | 42, down to 10.7 units | 0 |

## Conversion

The worker converts a VRM in memory into a PMX 2.0 model, and the rest of the character pipeline takes it unchanged: fitting, carrier, rendering, morphs, bodygroups and sharing (`native/vrm.cpp`). `model.bin` holds the PMX, textures come from the embedded images, and the spring-bone data, humanoid map and licence travel in the asset manifest, so they are covered by its identity hash and shared with it.

- **Axes and size.** glTF is right-handed. VRM 1.0 avatars face +Z with their left side at +X; VRM 0.x avatars were exported turned around (facing −Z). Both are mirrored into MMD's left-handed frame (facing −Z, left at +X), and triangle winding is reversed to match. VRM 0.x collider offsets and gravity directions are Unity axes and are converted as UniVRM does. Metres become 8 cm PMX units (× 12.5), so an avatar keeps its real height in Source.
- **Skeleton.** Every node that shapes the skeleton becomes a bone; mesh-only leaf nodes do not. The rest pose is baked from the skins (joint × inverse bind matrix), so non-normalised VRM 1.0 files also work. Humanoid bones get the MMD names the ragdoll fitter matches, for example `下半身`, `上半身`, `首`, `頭`, `左腕`, `左ひじ`, `左手首`, `左足`, `左ひざ`, `左足首`, the fingers and `左目`. Other names that collide get a suffix. The fitter treats spring-bone chains as secondary, so long hair does not widen the head's collision body.
- **Meshes.** Only the vertices each primitive draws are kept: VRoid shares one vertex buffer across all submeshes, which multiplied vertices up to tenfold otherwise. Up to eight glTF skin weights are merged into PMX BDEF1/2/4. Unskinned meshes follow their nearest bone. Morph offsets under 1 µm are dropped, because exporters leave float noise in every vertex.
- **Expressions.** VRM 0.x blend-shape groups and VRM 1.0 expressions become group morphs over the raw targets. Same-named targets in several meshes merge when every expression binds them alike. Colour binds become material morphs and texture-offset binds become UV morphs.
- **Materials.** MToon and glTF materials become PMX materials in render-queue order, carrying base colour, alpha, double-sidedness, the matcap as an additive sphere map and the outline as edge settings. The base texture carries glTF's alpha meaning, because the character renderer infers cutout or blending from texture alpha: OPAQUE materials get an opaque copy, and MASK materials get alpha cut at `alphaCutoff`. KHR_texture_transform is baked into the UVs.
- **Node constraints (VRM 1.0).** Rotation and roll constraints become PMX inherited rotation (roll adds a fixed axis). Aim constraints are reported as a warning.

## Limits

- **MToon shading is approximated.** The Source material draws the base texture with the addon's lighting. Shade colour, toon ramps, rim light, outlines, emission and UV animation are not drawn; each import lists this as a note.
- **VRM 1.0** is implemented from the specification and tested with generated 1.0 files. All twelve avatars available here are VRM 0.x (UniVRM 0.94–0.129, VRoid Studio 1.5–2.1.3, the Blender VRM add-on 2.16/2.31).
- Extended colliders (VRMC_springBone_extended_collider planes and inside shapes) use their standard fallback shapes. Expressions marked binary blend smoothly.
- Eye movement uses the eye bones. Avatars whose look-at is expression-driven keep their look expressions as ordinary morphs. First-person mesh annotations are not used: the addon already hides your own body in first person.
- VRM avatars are exported in a T-pose. Source animations drive the fitted carrier as they do for T-posed MMD models, and the NPC walk looks natural. VMD motions played through animation addons were not tested with VRM avatars. Motions made for A-pose MMD models may hold the arms higher on a T-posed avatar.

## Validation

`mmdhl_vrm_tests` (ctest `vrm_import_and_springs`, 71 checks) builds a small humanoid as VRM 0.x and as VRM 1.0 GLBs and checks the conversion:
- facing, left side and scale;
- winding against every normal, shared vertex buffers, skin weights;
- morph mirroring, and expression, material and UV morphs;
- alpha baking;
- spring and collider data, licence and humanoid map.

It also checks the solver:
- rest stability, gravity and the independent reference step;
- sphere colliders;
- floor contacts, including strands rooted under the floor;
- world, relative and centred damping under steady motion.

Finally it drives a spring-only model through the secondary pipeline, synchronous and asynchronous. Checks there: rest, lag and settling on a quarter turn; Jiggle keeping springs; Off holding the animated pose. The full native suite passes (21/21).

All twelve VRM files provided import through the worker, and all twelve fit an 18-body ragdoll:

| File | Import | Vertices | Triangles | Bones | Morphs | Materials | Spring joints | Colliders |
|---|---|---|---|---|---|---|---|---|
| MANUKA | 6.7 s | 73,322 | 112,444 | 247 | 550 | 5 | 159 | 25 |
| Eku | 3.7 s | 88,574 | 141,265 | 235 | 55 | 5 | 137 | 3 |
| Mafuyu / Mafuyu Another | 3.4–3.9 s | 60,469 | 94,897 | 350 | 297 | 6 | 263 | 27 |
| Tlipoca (maid) | 4.1 s | 29,285 | 41,333 | 164 | 70 | 18 | 90 | 28 |
| Tlipoca (school) | 2.6 s | 21,449 | 31,235 | 116 | 70 | 14 | 60 | 22 |
| Furina | 0.9 s | 25,422 | 31,726 | 515 | 96 | 6 | 352 | 0 |
| Karin | 5.2 s | 33,746 | 46,414 | 208 | 292 | 6 | 0 | 0 |
| Rusk | 2.3 s | 21,237 | 29,841 | 118 | 241 | 6 | 0 | 0 |
| AvatarSample_M (VRoid) | 5.3 s | 31,268 | 45,405 | 192 | 70 | 19 | 112 | 28 |
| ayame | 6.4 s | 34,779 | 47,520 | 199 | 70 | 38 | 136 | 28 |
| Lamy | 5.5 s | 43,789 | 60,742 | 166 | 69 | 22 | 98 | 28 |

MANUKA keeps one bone whose tail sits on the bone itself; it is reported and stays still.

In game (gm_construct, single-player, the asynchronous Multicore CPU backend):
- Mafuyu, the VRoid avatar, Furina and Karin imported through the library and spawned.
- They faced the player with correct textures, transparency and faces.
- Expressions and Face Poser names worked.
- The dropped-ragdoll, collision-mode and walking-NPC measurements above were taken there.
- The static-prop refusal and its **Import as character** button, the library licence line and the new setting were checked.

Multiplayer sharing of a VRM was not run.
