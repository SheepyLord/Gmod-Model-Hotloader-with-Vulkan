# RTX previews, folders and client ragdolls — preview.14

## Changes

The RTX fixed-function world material path did not produce a complete menu
preview. A plain-material probe confirmed that the full mesh was present;
preview materials using the same base textures restored the missing surfaces.
The RTX preview now uses separate UnlitGeneric materials with authored tint,
alpha and cutouts, rendered into a private target and displayed in the panel.
It is a texture/geometry preview, not a ray-traced lighting preview. Normal
GMod retains its existing lit preview.

The materials use the `vgui/` namespace, which the installed Remix integration
excludes from scene material categorization and PBR conversion. Public world
VMTs are not modified. Render targets use bounded power-of-two sizes, reused
across panels; camera and scissor state are restored after drawing.

The right-click folder action now owns a real Derma submenu. Previously its
callback opened a separate menu, which the original menu's mouse-release
handler immediately closed. Both the submenu and standalone move button use
the same destination list and captured multi-selection.

Client MMD ragdolls have a weak registry because they have no network entity
index and are not reliably enumerated by `ents.GetAll()`. Engine-created
corpses and direct `ClientsideRagdoll` copies of cached MMD models register
there. They have distinct local IDs in the map list, can be removed through
that list, and release their native instances and render proxies on removal.
Hidden client objects retain their no-draw behavior.

When every Source physics object in a client ragdoll is asleep, its secondary
world is suspended at the existing safe worker boundary. Physics and external
scene synchronization stop without accumulating simulation debt. Source motion
is not changed. On wake, normal quality rules resume with the existing pose
blend/recovery path. Server ragdolls do not receive this sleep policy. Missing
or incomplete physics is conservatively treated as awake.

## Controls and APIs

- **Physics & Performance → Clean up local MMD ragdolls**
- Client console: `mmdhl_cleanup_client_ragdolls` (also usable with `bind`).
- Client Lua: `mmdhl.CleanupClientRagdolls()` returns the removed count.
- `mmdhl.RemoveClientRagdoll(entity)` safely removes one eligible local ragdoll.
- `mmdhl.IsClientRagdoll(entity)`, `mmdhl.ClientRagdollAsleep(entity)` and
  `mmdhl.RegisterClientRagdoll(entity)` expose the scoped behavior to addons.

Cleanup affects MMD client ragdolls only. It does not delete library assets,
server entities, players, NPCs or editor previews.

## Validation

The supplied RTX Launcher successfully mounted **721 Workshop addons** after
direct startup hung at subscription fetching. The computer-use tool timed out,
so the supported launcher `--skip-launcher` entry point was used with the saved
settings, and the user loaded the map. Temporary launcher options were restored.
All gameplay/UI checks ran at **3840×2160**, with Workshop loading enabled.

- Reproduced the hands/feet-only Furina preview. Inspected the corrected full
  model and face/rotated views directly, with the authored textures intact.
- Opened the actual context submenu, verified `Unfiled` and the user's
  `Genshin Impact` folder, selected the destination, checked persistence and
  restored the original folder afterward.
- Tested real 18-object `class C_ClientRagdoll` objects. Sleeping held the
  secondary tick count at 225 with zero physics cost; lifting/waking resumed
  full 10-iteration simulation and advanced to tick 254, without a Source error.
- Tested direct model-only client ragdoll creation. Cleanup removed two local
  corpses, expired both native handles and emptied the registry. The server
  ragdoll remained valid with 18 physics objects.
- During a further 10-second check on `gm_construct_rtx`, all 355 frames were
  focused at 4K. The sleeping client's tick count remained 7; physics and scene
  synchronization costs remained **0 ms** throughout. Whole-game median/p95
  were 24.65/47.42 ms. This proves the sleep policy, not a whole-game performance
  target; these full-Workshop RTX results are not comparable to the earlier
  reduced-addon 1440p run.
- Eleven executable Python/Lua regression suites passed, including new client
  cleanup/sleep tests, preview material isolation/cache reuse and folder-menu
  multi-selection coverage. No native simulation or renderer binaries changed
  from preview.13.

Private evidence is in `validation/preview14-evidence`, excluded from packages.
No model reimport is needed. Test configuration, launcher settings and the debug
bootstrap are restored at completion. Future owned debug launches default to
3840×2160 and keep Workshop enabled unless explicitly overridden.
