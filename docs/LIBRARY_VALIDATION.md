# Library and placement regression — 0.3.1

The revised Q → MMD interface was exercised in visible, owned single-player games
with the user's enabled folder addons and 740 mounted Workshop addons, including
GUI Scaler. Actual render resolutions were 2560×1440 and 3840×2160. The installed
native DLLs are unchanged; this update changes Lua and the development harness.

## Reproduced failures and corrections

- A normal launch preserved the prototype's archived `mmdhl_native_carrier 0`.
  The old Place button spawned a legacy one-body entity. Carrier-only render
  restoration then undid the legacy renderer's request for immediate rendering.
  The request had no result message, so the invisible entity looked like a failed
  button. A one-time settings migration, explicit native menu placement, shared
  render-mode ownership, and request/result messages correct this path.
- Source previews now use textured Source materials with studio lighting. Their
  framing follows head/foot landmarks rather than hidden geometry bounds. Preview
  worlds are released when their containing menu closes.
- GUI Scaler applied another scale factor to already scaled panels, collapsing
  the 4K preview. The library marks only its own controls as already scaled. Other
  addon interfaces retain the user's GUI Scaler configuration.

## Observed results

The initial 1440p run passed 26 interface checks. The expanded 4K run passed 33:
selection, preview, refresh/reopen retention, render-mode restoration, search,
favorite, rename, single and bulk delete/restore, invalid-aim reporting, the
five-model budget, and input-handler clicks on Place for Xin, Cyrene, and Sandrone.
Each character produced a visible native `prop_ragdoll` with exactly 18 frozen
physics objects. Map removal preserved its library entry. Permanent cache deletion
was tested on a generated fixture, rejected for live characters, and left the
other cached models intact. Background importing completed with the menu closed
and selected the result when reopened. Temporary test preferences and fixture
files were restored.

Captures of all three previews, a close-up face, placement errors, and the final
4K layout were reviewed directly. In the final 4K layout the preview measured
1314×1176 pixels and the Place button was 80 pixels high; the double-scaled
layout had made the same button 160 pixels high. Source material tint and alpha
are restored after preview drawing.

The three-character movement stress check passed for 60 measured seconds after
8 seconds of warm-up, with no probe errors, crashes, non-finite transforms, or
mesh explosions. The final memory samples ranged from 7582.5 to 7588.9 MiB of
private memory. This is a short stability check, not a new FPS acceptance result:
only 422 foreground frames were collected, below the performance harness's
minimum. It does not supersede the earlier single-character performance results.

Private evidence: `validation/library-ui.json`, `validation/library-ui-4k.json`,
`validation/native-stress-stability.json`, and captures under the corresponding
owned-session directories. Reproduce with `scripts/test-library-ui.py` and
`scripts/test-native-stability.py stress`. The launcher now also backs up and
restores `client.vdf` and `server.vdf`, which hold addon settings.
