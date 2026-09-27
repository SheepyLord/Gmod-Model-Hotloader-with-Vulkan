# Physics and performance tuning

Open **Q → MMD → Physics & Performance** in the importer/library panel.
Settings are global, saved between sessions, and apply to existing and newly
placed models. Spawn freezing affects new placements only. Solver tuning is
applied at a complete step boundary; worker changes drain pending jobs first.
No reimport is needed.

## Defaults

| Control | Default |
| --- | --- |
| Backend | Claude CPU v2 (`cpu_mt_v2`) |
| Broadphase | `auto`: sweep-and-prune for Claude CPU v2, order-preserving DBVT (`dbvt-fast`) for the replay-exact backends |
| Secondary collisions | Everything except living players/NPCs |
| Spawn frozen | Off |
| Solver iterations | 10 |
| Gravity / authored damping multipliers | 1 / 1 |
| Stretch correction / allowance multiplier | Off / 1 (experimental, model-only collisions) |
| Worker lanes | 0: automatic (physical cores minus two, minimum one) |
| Async frame wait | 0 ms |
| Deformed vertex cache | On |
| Resting-body sleep | On |
| Sleep linear / angular thresholds | 0.5 PMX units/s / 0.35 radians/s |
| Time before sleep | 1.5 s |
| Follower wake distance | 0.02 PMX units |
| Performance overlay / console reports | Off / off |

Existing explicit backend preferences remain saved. **Restore current defaults**
selects Claude CPU v2 and resets every control above. The fixed simulation clock
remains 60 Hz and all valid authored bodies and constraints remain present.
The tab also offers diagnostic reports and a reset of secondary physics for all
models. Source ragdoll gravity and frozen limbs are unaffected by secondary
gravity and reset controls.

## Sleeve and hair stretching

The parameter-conditioning audit that earlier builds ran at world creation
(capped linear damping, chain mass-ratio flags, a stronger stop ERP, the
`mmdhl_secondary_conditioning` convar and the `parameterConditioning`
diagnostics) was removed in commit f9d6673; [STRETCH_CONDITIONING.md](STRETCH_CONDITIONING.md)
is historical. The current build changes follower timing instead: each tick
solves from the previous follower pose with start-to-end velocities and then
publishes followers and dynamics at the same tick boundary, so attached chains
no longer run one tick ahead of the Source bones that carry them. This is a
deliberate departure from nanoem's tick alignment; `mmdhl_replay` therefore no
longer reports strict agreement with the nanoem reference (the first-frame
difference is the removed one-tick lead, about 0.19 PMX units at the replay's
carry speed).

The experimental post-step projection below remains available and stays off by
default. When it is enabled together with world/prop collisions it slides
corrections along contacts. Since 2026-09-23 a touching pair clamps the
correction against its contact planes, other obstacles are swept only when a
correction can reach them, corrections no longer refresh the broadphase, and
the pass runs two alternating sweeps instead of four while external
collisions are on (model-only worlds keep four). In the eight-character
clustered scene this cut the per-tick guard work from about 49 to 14 worker
milliseconds and let the simulation keep real time. See PHYSICS_V2_0.md,
section "v2.1".

## Historical post-step projection (experimental)

March 7th's sleeve chains include locked translation joints. Fast movement can
leave the iterative solver outside those limits even though the model does not
authorize such stretching. The experimental optional correction performs four alternating
passes after each fixed step. It only corrects translation outside the authored
6-DOF ranges, with a small length-dependent allowance. Free axes and angular
limits/springs remain intact. Kinematic followers and world anchors never move.

Corrections split between dynamic bodies by inverse mass and remove separating
velocity along the violated direction. They do not convert positional correction
into additional velocity. Other PMX joint types are excluded because they use
their limit fields differently.

**Ground-contact regression:** this projection is not contact-aware. Moving
bodies after the solver can cross a contact plane, and reducing joint separation
before surface recovery hides the signal used to recover buried chains. The
correction now defaults **off** in both Lua and native code. It is also suppressed
per character whenever world/prop collisions are enabled, even if a saved setting
requests it. The accepted surface-recovery and carry code is unchanged. This is
a restriction of the experimental correction, not a claim that it now handles
stacked contacts. No reimport is needed; restart after installing the native fix.

Lower **Stretch allowance multiplier** for tighter attachments; raise it or
disable correction for deliberately compliant models. This is a stabilization
pass, not a rewrite of the PMX and not a guarantee of zero joint error. More
solver iterations improve convergence at additional CPU cost.

## Original model-only validation

Built in Release on `claude/mmd-importer-performance-87a36d`. Compared correction
off/on at 60 Hz, 10 iterations, full rigs, sleep disabled. Each replay carries the
model at 240 Source units/s and swings the left arm. Statistics cover seconds
2–6. No new in-game visual result is claimed. The previously accepted ground
and carry issues were not rerun. These free-space results did not validate
ground-contact compatibility; the subsequent user report exposed that regression.

| Replay | Bodies / joints | Peak error, off → on (PMX) | Mean error, off → on (PMX) | Mean correction cost |
| --- | --- | --- | --- | --- |
| March 7th | 287 / 329 | 0.41324 → 0.23401 | 0.03633 → 0.02743 | 0.1710 ms |
| Synthetic chain | 9 / 8 | 0.27143 → 0.16414 | 0.04979 → 0.05687 | 0.0017 ms |

March 7th peak violation improved 43%; mean violation improved 24%. The synthetic
chain's peak improved but its mean increased, reflecting redistributed error
between links. All measured positions/velocities remained finite. Live solver
iteration and gravity changes also passed. These are offline correction costs,
not total in-game frame times or a fresh performance claim.

March 7th asset: `bac307fdcefad2b6331a95e3b77644ad1562a1aba9983c0830e260326f2ee4c6`.
Reports: `validation/march-stretch.json`, `validation/chain-stretch.json`.
Regression: `mmdhl_stretch_tests <PMX-or-cached-model> [JSON-report]`, also
registered as CTest `secondary_stretch` using the synthetic chain fixture.

## Ground-regression coverage

`mmdhl_stretch_ground_tests` / CTest `secondary_stretch_ground` replays a chain
compressed against a thin floor and lifted again with collision modes 1 and 2.
It also resets the chain while buried, then lifts it. It compares complete
position/velocity trajectories with stretch requested off and on, checks finite
transforms and recovered joint error, and verifies that
the stretch pass runs only after switching to model-only collisions and stops
again when external collisions return. It also checks the native default is off.

Release build and both focused regressions passed. In both external collision
modes, off/on requests produced identical position/velocity samples (maximum
difference 0); the compression phase stayed at least 0.02053 PMX above the floor.
Eight buried bodies were recovered after reset, with a final lifted minimum
height of 6.60614 PMX and peak settled joint error of 0.07734 PMX. Live collision
mode transitions passed. Lua loading, the off default and Restore Defaults were
also checked. Report: `validation/stretch-ground-regression.json`. These are
offline regression results, not a new in-game visual or performance claim.
