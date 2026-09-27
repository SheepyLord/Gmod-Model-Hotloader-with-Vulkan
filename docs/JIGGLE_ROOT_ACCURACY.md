# Jiggle chain roots and accuracy levels — preview.16

Quality 0 now gives a spring only to the first physics-driven bone below an animated attachment. Detection follows the PMX bone hierarchy, carries across helper bones and branches, and starts again after a Source-controlled bone. Descendants preserve their animated local transforms and follow their root as a unit; they are not pinned in world space. The existing movement-direction filter, damping, swing limit, suspension and reset behavior remain on the root. Full Bullet simulation is unchanged.

The menu uses these positions:

| Position | Behavior / iterations |
|---|---|
| −1 | Off |
| 0 | Lightweight jiggle |
| 1 | 1 |
| 2 | 2 |
| 3 | 5 |
| 4 | 10 (default) |
| 5 | 20 |
| 6 | 50 |
| 7 | 100 |

`mmdhl_secondary_iterations` still stores actual iterations. Opening the menu does not round or overwrite a saved custom value; the thumb uses the nearest logarithmic step and the label displays the actual count. Dragging the slider selects a listed count. Console changes, Restore Defaults and physics toggle restore remain compatible.

## Validation

- Release build succeeded.
- All 17 native CTest cases passed, including ground recovery, stretch, motion, suspension and full-physics resume.
- The jiggle regression checks root motion, unchanged descendant local transforms, inherited child motion, finite output and fixed pivot lengths at 30, 60 and 240 Hz, plus stationary settling and no Bullet ticks in jiggle/off modes.
- All 14 Lua regression scripts passed. New slider checks cover all nine positions, rounding, bounds, default level, custom saved counts and external convar changes.
- No in-game tests were run, as requested. Visual behavior of this release is not claimed as game-validated.

No model reimport is required. Restart the game to load the updated native module.
