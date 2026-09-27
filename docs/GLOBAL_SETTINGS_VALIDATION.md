# Shared settings and optional telemetry — 0.8.0 QoL update

Collision mode, secondary backend and spawn freezing are archived client settings shared by every model. They appear in Q → MMD and Utilities → MMD models. The ragdoll editor changes the same global collision/backend settings. Changes to collision/backend also apply to existing MMD entities. Spawn freezing defaults **off** and only affects subsequent placements; existing per-limb freeze states and Source poses remain intact. Size multiplier remains per model. Old library collision/backend/frozen preferences are ignored.

The Lua spawn factories fill omitted options from the player's global settings. Explicit options and the per-entity backend/collision APIs remain available to duplication, integrations and test scenarios. Global controls intentionally replace those entity-specific choices when the user next changes the corresponding shared settings. Backend failures are reported rather than silently treated as successful.

## Diagnostics

- `mmdhl_debug_overlay 1` enables the overlay; `mmdhl_debug_toggle` toggles it, for example `bind F7 mmdhl_debug_toggle`.
- `mmdhl_debug_print 1` prints a summary every five seconds.
- `mmdhl_debug_report` prints a summary and saves JSON under `data/mmd_hotloader/diagnostics/performance-<timestamp>.json`. Enable telemetry first to collect frame timings.
- Both continuous options default off. Disabled telemetry does not collect frames or poll native diagnostics. A requested one-shot report still gathers current counts.

Frame statistics cover the most recent five seconds, sampled once per rendered frame with `SysTime`, bounded to 4096 samples. Percentiles and compact native diagnostics refresh twice per second. The display distinguishes frame/prepare wall time from summed worker CPU physics, pose, deformation and scene work; parallel worker totals must not be added to estimate frame duration. The HUD also includes backend/fallback information, actual server-replicated native body counts, MMD bodies/joints, external contacts/proxies, debt, dropped time, resets, mesh cache, upload rate and explicitly labelled Lua memory. It does not claim to measure GPU time or total process memory. Explicitly enabled diagnostics remain visible when a tool hides the ordinary game HUD.

## Validation

Owned single-player `gm_construct`, 2560×1440, enabled folder and Workshop addons. No solver changes: fixed 60 Hz, 10 iterations, complete Xin, Cyrene and Sandrone rigs.

`scripts/test-global-settings.py` checks:

- The default is unfrozen, reference backend, collision mode 2.
- Changing the menu choices applies to two existing ragdolls, each retaining exactly 18 native objects, unchanged frozen body transforms and full authored secondary rigs.
- Selecting different models with deliberately conflicting saved per-model values and reopening the library retain global choices.
- The next model inherits current defaults and all 18 limbs spawn unfrozen. Existing frozen limbs remain frozen. Explicit API options override defaults.
- Enabled telemetry reports 3 models, 54 native bodies, 1704 MMD bodies and 2459 valid joints, with nonzero frame samples. Turning it off stops polling. Console reports and screenshots are captured; test settings are restored.

The final **10-second measurement after eight seconds of warm-up**, with the overlay displayed, uses three clustered characters and world/moving-prop contacts. `validation/global-settings-overlay-stress.json` records a correctness pass and a valid foreground measurement: frame p50 **17.37 ms**, p95 **25.17 ms**, p99 **31.26 ms**. The 16.7 ms target remains unmet. This is a QoL verification, not a controlled claim of solver performance improvement or overlay overhead. The original snapshot with telemetry active but the harness hiding the HUD measured p95 24.00 ms; that is not an equivalent visible-overlay comparison.

The native-count check initially exposed client `GetPhysicsObjectCount()` returning zero for server-owned carriers. The overlay now consumes the count recorded by the server when attaching the carrier; the repeated check passed. The library and final overlay screenshots were reviewed directly. No new MMD Lua errors were observed. Existing unrelated Workshop startup warnings are outside this change.

Evidence is stored in `validation/global-settings.json`, `validation/global-settings-overlay-stress.json`, and the owned session's `global-settings-library.png` / `global-settings-overlay-stress.png` captures. The owned session is stopped and temporary configuration/debug hooks restored before the final installation/package.
