# Stretch-prone parameter conditioning

Implemented on `claude/mmd-importer-performance-87a36d`, 2026-09-22.
Enabled by default through `mmdhl_secondary_conditioning`; see
[Physics & Performance](PHYSICS_TUNING.md) for the control and exact thresholds.

## Diagnosis and method

March 7th's sleeve translation axes are already locked; tightening their
authored limits would not address the cause. Many dynamic sleeve/accessory
bodies have near-total linear damping, which brakes the translational velocity
needed to follow their attachments. Some connected bodies also have large
mass ratios, which slow convergence at a fixed iteration count.

The runtime identifies affected dynamic chains once, caps extreme linear
damping and strengthens the response to violated translation limits inside
Bullet. Followers do not bridge independent chains. It retains all authored
bodies, valid joints, masses, travel ranges, angular damping and spring values.
The old post-step projection stays disabled with external collisions.
The accepted carry compensation and surface recovery implementations are unchanged.

The audit is available in detailed `parameterConditioning` diagnostics, with
original names and indices. March 7th identifies 124 damping bodies, four
mass-ratio warnings and 321 affected joints. PMX and cached assets are not edited.
The global toggle reversibly restores authored solver parameters.

## Offline regression results

Release build; all **13 CTest regressions passed**. New coverage includes an
extreme-damping chain, a 32:1 alternating-mass chain, exact trajectory agreement
for unaffected parameters, live restoration of authored values, and floor
compression/lift/reset in both world-contact modes.

The March 7th replay carries at 240 Source units/s and swings the left arm.
It uses 60 Hz, 10 iterations, sleep disabled, 287 bodies and 329 joints.
Statistics cover seconds 2–6 with post-step projection off.

| Metric | Authored parameters | Conditioned |
| --- | ---: | ---: |
| Maximum translation-limit error (PMX) | 0.413240 | 0.151938 |
| Mean translation-limit error (PMX) | 0.036326 | 0.008776 |
| Mean physics step on this run (ms) | 1.025 | 1.079 |
| Post-step stretch corrections | 0 | 0 |

Peak error fell 63%; mean error fell 76%. The small timing difference is a
single-run observation, not a statistically established performance change.
Positions and velocities stayed finite, and live disable restored damping and
ERP without changing mass, angular damping or authored joint travel.

Reports: `validation/march-conditioning.json`,
`validation/conditioning-all-tests.log`.
The inspection executable `mmdhl_parameters <PMX/cache> <JSON>` lists authored
body and joint settings for diagnosing other models.

## In-game observations and acceptance

Owned, addons-enabled `gm_construct`, actual 2560×1440, Claude CPU v2.
Three 10-second captures after warm-up compared arm swing plus 240-unit/s
carrying with conditioning off/on, then floor compression and lift with it on.
The initial runs used the user's saved **5 iterations**. Full 287/329 rigs
were present, all frames were focused and drawn, and no additional simulation
time was dropped or physics resets triggered during measurement.

| Initial run | Mean sampled worst-joint error (PMX) | Frame p95 (ms) |
| --- | ---: | ---: |
| Arm/carry, off | 0.81877 | 5.980 |
| Arm/carry, on | 0.20045 | 5.763 |
| Compression/lift, on | 0.65419 | 5.627 |

These sampled worst-joint values differ from the per-joint offline mean.
The ground run recorded 26 simultaneous world contacts at peak and no bodies
below the floor after lifting. Post-step stretch corrections remained zero.
Captured arm-motion and lifted poses were reviewed directly; no elongated
ground tether was visible.

The original harness flagged these runs because it expected 10 iterations.
It now explicitly sets and restores tuning and verifies the conditioning state.
Subsequent 10-iteration attempts lost focus (one also lost clock/visibility
checks), so they are **excluded from acceptance/performance comparisons**.
The user then confirmed the issue was resolved and requested no further tests.
Gameplay testing stopped on that acceptance; no clean 10-iteration gameplay
gate is claimed.

Evidence: `validation/march-conditioning-{off,on,ground}.json` and matching
`-3.8.jpg`, `-7.jpg`, `-9.8.jpg` captures. Rejected reruns retain the
`march-conditioning10-` prefix. The owned client was stopped and temporary
settings restored before installing the final build.

## Limits

This is a targeted heuristic, not a universal guarantee against stretching.
Intentional authored translation ranges remain available. Extreme compression
can still create temporary constraint error; contact handling and the existing
surface-recovery path remain responsible for preventing persistent ground
trapping. No rig detail, timestep or default iteration count was reduced.
