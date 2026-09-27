# Hidden controllers and stacked face lighting — preview.12

## Changes

Coincident same-material faces now use the same deformed-surface selection in
the opaque/depth-writing pass and additive flashlight passes. Previously only
the flashlight pass omitted the redundant triangle. Tiny skinning differences
could make the other copy win the depth test, leaving either a bright strip or
dark triangular patches. The existing position/UV checks, opposite-winding
exclusion, alpha cutouts and cross-material layer ordering remain intact.
Surfaces that separate during animation/morphing render independently. This
does not delete PMX faces or change authored materials, normals or physics.

Model metadata is distinct from presentation. Invisible animation controllers
can still use the native skeleton, sequences and entity APIs, but no longer
create an MMD render proxy, fallback model, shadow or secondary simulation.
The server's `SetNoDraw` intent is retained separately from the client flag.
Local no-draw/effects and network dormancy are also respected. Hiding releases
the client instance; showing/retransmitting binds a fresh current-pose instance.
No physics objects are created or destroyed on the controller's behalf.

The local player's body shadow is disabled in first person, including Source
shadows and projected-light depth passes. Third-person shadows resume normally.
First-person secondary physics stays suspended, as in preview.11.

## Evidence

- Loaded the supplied `gm_construct 2026-9-22 22-26-40.gms`, retaining the
  saved Furina appearance and the user's corrected viewing placement.
- Directly inspected guard-off, preview.11 guard-on and corrected captures.
  The guard-off capture showed a bright band across the cheeks; preview.11
  left dark triangular gaps. The corrected image has continuous face shading.
  The user also reported that the visual issue was fixed.
- Configured the actual EEER headshot controller and a generic `prop_dynamic`
  helper. Hidden helpers had no client world or proxy. A shown generic copy
  acquired a renderer; hiding it again released the world. Server skeleton
  access and zero Source physics objects were preserved.
- Fired a bullet at a fresh imported ragdoll's head. EEER created
  `rpe_death_stiff_controller` and `rpe_headshot_sequence_controller`, both
  hidden with zero Source physics objects. Repeated client observations found
  no simulated or visible hidden helper.
- Verified first person → third person → first person: native shadow alpha
  `0 → 1 → 0`, projected-depth suppression in first person, and local physics
  suspension. Reviewed the resulting in-game capture.
- 17 CTest cases and nine executable Python/Lua regression suites passed.
  Added coverage for sub-tolerance coincident faces and hidden presentation
  policy, including a client addon clearing its local no-draw flag.

## Performance and scope

A focused 10-second run after warm-up at 2560×1440 collected 1,376 frames:
median **7.20 ms**, p95 **8.44 ms**, maximum **17.02 ms**. Mean native rendering
was **2.19 ms**; maximum measured overlap-mask cost was **0.116 ms**.
This is a smoke check of this scene, not a new eight-rig performance claim.

Steam's subscription fetch hung before entering the map. The owned test used
`-noworkshop`, with the installed folder addons (including EEER and Advanced
Material Editor) active. An attempted cached-GMA staging did not mount under
that flag and was removed. These results therefore do **not** certify the full
Workshop addon set or resolve Steam's subscription service itself. The harness
has an explicit `-NoWorkshop` option; normal launches remain unchanged.

Private captures and structured results are under
`validation/preview12-evidence`. The source save, Workshop files, subscriptions
and model caches are unchanged. Test configuration and bootstrap are restored
after the run. No PMX reimport or carrier regeneration is required.

Reference behavior: [SetNoDraw](https://wiki.facepunch.com/gmod/Entity:SetNoDraw)
can stop transmission as well as drawing; [RenderFlashlights](https://wiki.facepunch.com/gmod/render.RenderFlashlights)
adds separate lighting passes. The fix handles both ownership boundaries.
