# Q-menu clipping and death handoff — preview.15

## Menu fix

The dropdowns were opening, but the character preview left an explicit render
scissor override enabled. It incorrectly used `surface.GetScissorRect()` as a
saved render override; that function reports VGUI's current panel clip. Later
popups were clipped to the preview rectangle.

Preview drawing now relinquishes its render scissor override after ending the
3D pass. VGUI continues managing its own panel clipping. The camera/render-target
stacks are balanced on both success and drawing errors. The NPCs, Server and
Drawing menus were opened with External Models selected and visually inspected
in the actual Q menu.

## Death handoff

Independent Think hooks previously released the hidden former actor, attached
the corpse and created its visual at different times. The corpse could have a
valid native pose while still having no render proxy. Transmission loss also
destroyed the former actor's world before a corpse in the same network update
could inherit it.

Client attachment, source-pose capture, native preparation and visual-bound
updates now run in order at the render boundary. Transfers are attempted before
hidden-source cleanup. A compatible transfer moves the existing proxy and native
world; removing the former actor cannot delete the new owner's proxy. Generation,
asset and rig checks prevent borrowing an unrelated or respawned actor's state.

Source forbids entity creation during rendering. New proxies are still created
in Think. A ready model without a proxy is drawn directly for the interim frame;
once a proxy exists, only the regular path draws it. Integrations which omit the
opaque post-draw hook get the pending opaque surface in the translucent hook,
before its overrides. Hidden helpers remain suppressed. Proxy deletion and
fallback creation are deferred out of the render callback.

EEER's independent-player-corpse path could already be bound when our callback
ran, causing the old code to skip recording the source-player link. The link is
now recorded without overwriting EEER's existing death expression. Clientside
corpses also keep their direct source reference for the handoff retry.

Pending metadata/assets retry after 50 ms rather than the hard-error 500 ms
delay. Missing assets still must become available; this change cannot eliminate
network download time. No duplicate corpse or artificial physical body is made.

## Validation

Test client: `H:\GModRTX-Fresh-Test\GarrysMod-RTX`, gm_construct,
**3840×2160**, **721 Workshop addons mounted**, foreground during recordings.
Launch used RTX Launcher and waited for Workshop mounting before starting the
map. A token-scoped temporary menu-to-map trigger was staged for the owned
session and restored afterward.

The baseline warm NPC trace showed two frames without the affected character
being drawn (about 89 ms from first missing frame to corpse rendering). Native
allocation itself took roughly 5–6 ms. A cold trial also encountered a much
longer engine stall; it is not used as a steady-state performance comparison.

Final frame-by-frame draw traces:

| Transition | Recorded frames | Frames with neither actor nor corpse drawn |
| --- | ---: | ---: |
| NPC → server ragdoll | 248 | 0 |
| Player → EEER independent ragdoll | 259 | 0 |
| NPC → clientside ragdoll | 253 | 0 |

Each final transition retained the same native instance handle and visual proxy
across the actor/corpse boundary. The interim player test exposed the EEER link
race; the final linked test eliminated its remaining missing frame. Hidden
helper suppression and cleanup boundaries remain covered by regression tests.

All 13 executable Python/Lua regression suites passed, including new tests for
preview clipping on failure/success and ordered, compatible proxy/world handoff.
No native physics or renderer binary changed from preview.14. This is functional
transition validation, not a new whole-game frame-time target or multiplayer
certification. No PMX reimport is required.

Private traces and screenshots are under `validation/preview15-evidence`,
excluded from packages. Test settings, launcher settings and staged menu/debug
files are restored at completion.
