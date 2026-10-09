# Debugging and acceptance testing

Run commands from the repository root. `game-start.ps1 -Visible` installs the build and launches an owned single-player game at **2560×1440**, with the user's enabled folder and Workshop addons. `-Isolated` is an explicit diagnostic option that stages only this addon's files. Isolation results are not release acceptance results.

The launcher normally refuses to start alongside another GMod process. `-AllowOtherInstallation` permits a different installation, using separate network ports; it still refuses a second process from the target installation. Record that concurrent workload when interpreting performance. `-QueueMode 0` selects the renderer's required mode during startup; the launcher backs up the user's configuration. Its session token and PID are recorded in ignored `validation/session.json`. `game-stop.ps1` verifies the token before stopping that process tree, removes the temporary bootstrap/RPC specification, restores staged files when unchanged, and restores backed-up configuration files. It never terminates an unrelated game. Use it after every owned session, including failures.

The normal addon has no active debug RPC. The local file listener requires the temporary bootstrap, matching token and single player. Captures, environment inventories and heartbeat files are written to `garrysmod/data/mmd_hotloader/debug/<token>/`. Reports, dumps and corpus paths in `validation/` are private and excluded from packages.

## Build and iterate

```powershell
./scripts/game-stop.ps1
./scripts/build.ps1 -SkipBootstrap
./scripts/game-start.ps1 -Visible
# Bring the owned game forward through the computer-use tool.

# Lua-only edits; native changes always require restart.
python scripts/reload-lua.py
python scripts/gamectl.py server 'return mmdhl.Decode(mmdhl.native.GetDiagnostics())'
python scripts/gamectl.py client 'return {width=ScrW(),height=ScrH(),error=mmdhl.renderError}'
python scripts/gamectl.py server --file tests/game/native-limits.lua
python scripts/gamectl.py client 'RunConsoleCommand("mmdhl_debug_capture","scene") return true'
python scripts/gamectl.py client 'RunConsoleCommand("mmdhl_debug_capture","menu","ui") return true'
```

Use Lua files for complex commands and shader keys containing `$`; PowerShell interpolation can otherwise change them. Run one RPC request at a time in each realm. Keep the game focused for performance measurement. The probe preserves all observed frame times and separately records foreground samples, excluding the first second after focus returns. Benchmark acceptance requires at least 200 foreground frames and 80% of the requested measurement interval. Background-throttled samples never count toward the 16.7 ms target.

## Native carrier gates

```powershell
python scripts/prepare-acceptance.py
python scripts/test-carrier.py
python scripts/test-native-anatomy.py
python scripts/test-presentation.py
python scripts/test-portable-fit.py
python scripts/test-native-migration.py
python scripts/test-native-tools.py Xin
python scripts/test-native-tools.py Cyrene
python scripts/test-native-tools.py Sandrone
python scripts/test-native-extras.py
python scripts/test-native-interactions.py
python scripts/test-native-stability.py empty
python scripts/test-native-stability.py Xin
python scripts/test-native-stability.py Cyrene
python scripts/test-native-stability.py Sandrone
python scripts/test-native-stability.py stress
python scripts/test-native-stability.py cycles
```

`prepare-acceptance.py` imports the supplied models through the production worker. The tool suite selects the actual Q-menu library row, drives real player commands for Physics Gun interaction, and executes installed Sandbox tool handlers and panel callbacks. Carrier tests verify real physics-object counts, traces, bone indices, constraints and removal. The extra suite covers complete face presets, native weld, color/material tools and undo.

The stability probe moves Source pelvis bodies while the other native limbs and complete authored secondary rigs simulate. It records actual PostRender frame intervals, p50/p95/p99, deformation/render costs, uploads, bounds, finite transforms, native counts, process memory and dropped time. Bounded 0.01 ms histograms avoid retaining frame arrays during memory measurements. It records the actual resolution, engine build, graphics settings, Workshop mount flags and local addon folder inventory. At the user's request, measurements default to **10 seconds after 2 seconds of warm-up**; `stress` uses all three supplied characters, and `cycles` creates/removes 8 full rendered characters within a short run. Assets and textures should already be loaded before measurement. Use `--models validation/compatibility-models.json` for Zankou and March7th, and `--collision-mode 1` or `2` for optional contacts. A stale-heartbeat watchdog saves a failure report, attempts an owned-process minidump, and stops only that process even if the dump times out. Single-character acceptance requires p95 ≤16.7 ms after warm-up.

The editor's collision overlay shows fitted hulls, primary joint frames, selected physics bone, limits and confidence. Cyan geometry comes from the engine's actual `PhysObj:GetMeshConvexes()` result; yellow geometry shows pending corrections. Its optional secondary overlay shows nanoem bodies/anchors and current solver diagnostics. Disable these overlays for performance measurements. They intentionally collect more diagnostic data.

`test-presentation.py` exercises all three supplied models at multipliers 0.5, 1 and 2, comparing PMX landmarks, Source's client bone matrices, rendered mesh samples and the actual serialized collision. It also captures the installed Easy Entity Inspector's bone and collision modes. `test-native-migration.py` pastes a genuine 2.2 carrier (rig generator 30, from a cache 2.2.0 filled) with a version-1 duplicate state and checks that the paste keeps that carrier, its body pose, size, freezing and fingers. `test-portable-fit.py` copies only the worker, runtime and input PMX into a clean directory, then checks uncached fitting, topology and timing; it does not sandbox filesystem access. Runtime code must also remain free of corpus-path dependencies.

The performance environment file records settings at scenario setup. Since the addon switches to its nonqueued renderer when the first model attaches, `client.renderModes` additionally counts the actual `mat_queue_mode` used during measured frames. Use that field when checking render-mode assumptions.

`test-native-visuals.py` captures repeatable full-body, face, hand, collision, flashlight and override views. Run on both `gm_flatgrass` and `gm_construct`, including bright and dark areas. Inspect the PNGs directly. A successful API return, nonempty image or passing number does not prove visual correctness.

`python scripts/capture-native-motion.py Xin` saves twelve full-resolution frames with timestamped native driving poses and secondary diagnostics. Cyrene and Sandrone use the same repeatable camera/motion. `--origin` selects a location on the current map. The script cleans its motion hook and character afterward.

## Independent nanoem replay

```powershell
build/bin/Release/mmdhl_replay.exe 'C:\Models\character.pmx'
```

A separate nanoem C-API world receives the same 600-frame driving poses as the runtime. It compares body positions/orientations and independent emapp feedback equations, plus carrier metadata and bind-pose agreement. This tests the pinned backend and synchronization; it is not a claim that every MMD animation/IK feature matches the desktop application.

`calibrate-fitter.py` discovers corpus pairs, QC origins, proportion poses and the 18 primary reference hulls. `build-shape-atlas.py` further validates rigid registration, excluding the torso landmarks deliberately relocated by SCMI's proportion workflow. It emits normalized reference convexes and support-plane regression coefficients. The choice between tapered references and plane predictions uses only family-separated calibration folds. `evaluate-shape-atlas.py` measures exact convex intersection-over-union on held-out families; supplied characters and their variants never enter calibration. Both development scripts need NumPy/SciPy and the private correspondence audit. Neither dependency is needed at runtime. The older `export-fit-calibration.py` belongs to the previous aggregate-factor fitter.

## Native failures

```powershell
python scripts/capture-dump.py
```

This verifies the owned PID/token before capturing a minidump. Use matching PDBs from `build/bin/Release/`. Save capabilities, physics/render status, engine version and DLL fingerprints. Runtime pose/secondary failures also write a per-character diagnostic JSON under `data/mmd_hotloader/failures/`.

The renderer keeps nonqueued engine submission, bounded physics catch-up, finite-transform checks and ABI guards. Reusable buffers report their memory, draw/update counts and upload volume in `RenderStats()`. `mmdhl_vertex_cache 0` selects the earlier dynamic-stream fallback for diagnosis. Never bypass an ABI guard to load an unverified engine build.

The old `test-game.py`, `test-vanilla.py`, `test-soak.py` and native regression suite remain useful for the legacy backend. Their historical reports do not count as native-carrier acceptance. Current evidence and unresolved items are in `NATIVE_VALIDATION.md`.

## Compatibility regressions (0.5.0)

`test-compatibility.py` has `imports`, `materials`, `eyes`, `morphs`, `visibility`, `collisions`, `limit` and `overflow` sections. The private `compatibility-models.json` maps the five acceptance names to local cached assets. `overflow --asset <fixture asset>` tests the generated 130-material fixture. `test-secondary-scenes.py` exercises map ground, wall and slope props, a moving prop, another ragdoll, teleport, mode changes and map cleanup. It asserts finite transforms and no added Source prop velocity.

`test-spawn-settings.py` verifies the library's placement request and persisted options. `test-compiled-eyes.py` compares identical EERP inputs against the installed SCMI Cyrene reference, restoring its temporary targeting setting afterward.

`test-import-feedback.py` exercises the real worker and library banner, Cancel button, persistent HUD, parse errors and cache reuse. Native Face Poser regressions inspect actual controls and stable controller names, not only manifest labels. AME tests invoke the installed editor's preview/save/reset paths and stock context-menu bodygroup handler. The gameplay test reapplies the stock Finger Poser immediately before copying: enabled expression addons may animate the same fingers between separate RPC steps. This does not disable those addons.

The debug listener tracks completed sequences across Lua reloads. A reload must not mark a pending request as already consumed. `reload-lua.py` can therefore be used repeatedly without losing the first RPC after reload. All temporary debug hooks are removed when the owned game is stopped.

A functional pass is separate from `performanceTargetPassed`; do not label a frame-time miss as an overall performance pass. Ten seconds is a short regression check, not evidence of long-duration leak freedom. Current results are in `COMPATIBILITY_VALIDATION.md`.

## Motion/performance probes (0.7.0)

`python scripts/test-performance.py <label> --count 0|1|3|10` runs a fixed ten-character manifest (distinct source hashes) at default collision mode 2, with eight seconds of warm-up and **ten seconds of measurement**. Focus the owned game using the computer-use tool first. Do not run another scenario until it finishes; the runner locks out concurrent invocations. `--mode 0` isolates external collisions diagnostically and `--workers N` compares pool sizes. Neither setting is a fidelity reduction in the default acceptance run.

Reports contain per-stage p50/p95/p99, real foreground duration, minimum characters drawn, private/working-set memory, and per-character tick/reset/dropped-time deltas across measurement. A successful finite-transform test is separate from keeping up with 60 Hz and meeting 16.7 ms frame time. Worker stage totals sum character job wall times (including nested task waits); they are not additional serial frame costs. `prepareWallMs` is the actual main-thread frame barrier. Shadow time is included in native rendering and overlaps the upload/draw timings; do not add overlapping fields.

`mmdhl.native.SetWorkers(0)` restores automatic physical-core sizing, leaving two cores for the engine/OS. It changes the persistent pool only between completed frame barriers. No worker touches engine interfaces or Lua.

The collision overlay includes tick count, interpolation fraction, catch-up debt, reset reason and worker costs. The native regression `mmdhl_timing.exe tests/fixtures/native-cloth21.pmx` covers 30/60/120/144/240 FPS, irregular cadence, repeated/rewound timestamps, teleports, intermediate presentation, nested workers, scalar skinning equivalence, and shared material/index topology.

`tests/game/profile-hooks.lua` is an optional owned-session diagnostic. Restore wrappers with `MMDHL_HOOK_PROFILE.finish()` in each realm before collecting final performance numbers. Frame captures and Lua profiling are intentionally separate from performance acceptance, since they introduce work of their own.

## Broadphase comparisons (0.7.1)

`test-performance.py` accepts `--broadphase dbvt|dbvt-fast|sap`, `--layout spaced|clustered`, and `--contacts`. `dbvt-fast` is the release default; `dbvt` is the pinned control. Each comparison explicitly selects its backend before spawning, checks the effective per-character backend and full authored inventory, and restores the previous default afterward. `--contacts` requires collision mode 2, creates moving plates with native collision against the characters disabled, and requires both map and object secondary contacts while checking that Source velocities are unchanged. Failures in correctness, backend selection, visibility or foreground measurement return a nonzero exit code. A frame-time target miss remains a separate result.

```powershell
python scripts/test-performance.py control --count 3 --broadphase dbvt --layout clustered --contacts
python scripts/test-performance.py candidate --count 3 --broadphase dbvt-fast --layout clustered --contacts
build/bin/Release/mmdhl_replay.exe 'C:\Models\character.pmx' --broadphase dbvt-fast --scene contacts
build/bin/Release/mmdhl_profile.exe --pmx 'C:\Models\character.pmx' --broadphase dbvt-fast --workers 1 --mode 2 --scene contacts --no-profile --json validation/profile.json
build/bin/Release/mmdhl_broadphase_tests.exe
```

The standalone harness uses 600 measured steps after 120 warm-up steps by default. Collision modes 1/2 require real scene geometry (`--scene contacts`); an absent scene is an error. `--workers` also applies to deformation. `--no-profile` disables both timing hooks and the phase dump. For phase instrumentation configure a separate build directory with `-DMMDHL_BULLET_PROFILE=ON`; use the ordinary OFF build for game comparisons. Never compare an instrumented candidate against an uninstrumented control.

The reference replay always constructs an independent pinned DBVT nanoem world, even when the runtime default is changed. Scene replays also construct their own reference colliders. `sap` is experimental: it failed the existing fidelity gate on the acceptance characters and must not be treated as an accepted replacement. Its bounds/capacity fallback rebuilds the broadphase and contact cache, records the reason, and retains bodies, constraints, velocities and filters. No global experimental switch can change the reference world's construction.

`python scripts/test-motion-controls.py` verifies alpha-test/coverage flags, an explicitly translucent override through actual draw calls, and one manual reset preserving all 18 Source objects and appearance state. It briefly disables automatic expressions for the fixed-state reset assertion and restores that setting afterward. `test-presentation.py` keeps addons enabled: it checks bones every frame and compares its neutral-vertex oracle only when morph weights are neutral, recording the number of eligible and morphed frames.
