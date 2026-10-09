# DXVK in the drop-in package, solver choices and low-core tests (2026-09-26)

The default drop-in package (`-vulkan`) now installs DXVK, so Garry's Mod renders
through Vulkan. The CPU solver stays the default physics processor. An
`-opengl-remix` package keeps Source's own Direct3D 9 renderer, for PCs where DXVK
does not work and for RTX Remix, which brings its own `d3d9.dll`. This page covers:

- what ships and how the native policy knows about it;
- how the addon offers the two solvers;
- what the tests of low-core CPUs, first person, the stream path and an RTX
  Remix install showed.

The merge itself is described in [VULKAN_MERGE.md](VULKAN_MERGE.md).

## Packages

The "Build drop-in package" workflow (`.github/workflows/package.yml`) runs in this order:

1. It builds the native modules and runs the checks as before.
2. It builds DXVK 3.1.1's `d3d9.dll` with `scripts/build-dxvk.ps1`, using MSVC, Meson 1.12.1, Ninja and the
   glslang from the native build. Two patches are applied:

   | Patch | Change |
   |---|---|
   | `patches/dxvk/0001-shared-compute-queues.patch` | Compute queues shared with the Vulkan solver ([VULKAN_COMPUTE.md](VULKAN_COMPUTE.md)). |
   | `patches/dxvk/0002-gmod-app-profile.patch` | A built-in profile for `gmod.exe` and `gmod_win64.exe` that turns descriptor heaps off. |

3. It records the DXVK build with `scripts/native_manifest.py --renderer`.
4. `scripts/package-dropin.py --renderer` stages two artifacts:

   | Artifact | Contents |
   |---|---|
   | `Model-Hotloader-<release>-<commit>-win64-vulkan` (default) | The native files, `bin/win64/d3d9.dll` (DXVK) and its `bin/win64/DXVK-LICENSES` folder. The licenses are DXVK (zlib) and its linked dxbc-spirv and libdisplay-info (MIT). |
   | `Model-Hotloader-<release>-<commit>-win64-opengl-remix` | The native files only; the game keeps its own Direct3D 9 renderer (or RTX Remix's). |

   Both also carry `GarrysMod/bin/win64/LICENSES` (Model Hotloader's license, `THIRD_PARTY.md` and the notices of every library built into the native files); the `-vulkan` package puts DXVK's notices in `DXVK-LICENSES` beside it. The source is the public repository, which `INSTALL.txt` links with its releases page and the Workshop addon. The packager stops if the notices are incomplete, and `tests/test_release_evidence.py` checks both packages.

   Each `INSTALL.txt` explains its package. The `-vulkan` one says:
   - it switches every game session to DXVK;
   - it needs a Vulkan 1.3 driver;
   - owners of RTX Remix, ReShade or another `d3d9.dll` should take the `-opengl-remix` package;
   - deleting `bin\win64\d3d9.dll` returns the game to Direct3D 9.

**The `gmod.exe` profile.**
- **The crash:** DXVK 3.1.1 turns descriptor heaps on for NVIDIA drivers from 595.84. With them, Garry's Mod crashes
  right after device creation on 610.88 ([upstream-DXVK test](DXVK_VANILLA.md)).
- **The fix:** the profile turns descriptor heaps off for `\gmod.exe` (the x86-64 branch's `bin\win64\gmod.exe`) and
  `\gmod_win64.exe` (the main branch's 64-bit executable since 2026-09-17), which needs no `dxvk.conf` in the game's
  working directory and no environment variable. A user `dxvk.conf` can still override it. Renderers built before
  `gmod_win64.exe` was added (up to 2.1.0-native.5) leave descriptor heaps on in the main branch.
- **In-game check:** a session with neither logged `Found built-in config: dxvk.enableDescriptorHeap = False` and ran
  normally.

## Native policy

**The release record.**
- `native-release.json` and each policy record may carry a `renderer` entry: name `d3d9.dll`, size, SHA-256, kind
  `dxvk`, DXVK tag and patch list.
- A `native_manifest.py` run without `--renderer`, such as the CMake build's own run, keeps the renderer already
  recorded for the release instead of dropping it.
- `native_manifest.py --check` compares a recorded renderer with `--renderer`, or else with
  `build-dxvk/src/d3d9/d3d9.dll` when that build exists; otherwise it says the renderer was not compared.

**`scripts/update-native-policy.ps1`:**
- It reads every artifact of the run and requires their records to be identical.
- A record is evidence only for the bytes shipped with it. Each package must contain the five native files its record
  lists (in `garrysmod/lua/bin`) and the runtime in `bin/win64`, with the recorded sizes and SHA-256.
- It checks every shipped `d3d9.dll` against the record's renderer. A record with a renderer must ship at least one.
- It treats a different renderer as different binaries when a release label already exists.

**`scripts/package-dropin.py`** copies the DXVK license notices from `--dxvk-source` (default `vendor/dxvk`), the
checkout the renderer was built from. With `--renderer` the packages are `<name>-vulkan` and `<name>-opengl-remix`
(without it, a single `<name>` package with the native files only).
`tests/test_release_evidence.py` covers the packager and the updater.

**The addon (`installation.lua`).**
- On the client it reads `bin/win64/d3d9.dll` and reports one of:
  - DXVK of this release;
  - DXVK of another release;
  - Direct3D 9 (no file);
  - another `d3d9.dll` (RTX Remix, ReShade);
  - an unreadable file.
- The installation banner shows it as "Renderer: …".
- The renderer never disables a feature and is never an unverified file. The engine loads it, not the addon, and
  other tools legitimately own that file.
- `tests/test_installation.py` covers every case and checks that the server realm never reads the file.

## Solvers in the addon

- **Order:** the processor list starts with the two current solvers.
  1. Multicore CPU Processor (default).
  2. Vulkan GPU Processor (Experimental).
  3. MMD Compatibility, Legacy Multicore and OpenCL GPU Processor (Experimental) follow.
- **Without the bundled DXVK:** the Vulkan solver runs on its own Vulkan device. The character editor then shows that
  physics can fall behind, and the tooltip explains when the Vulkan processor helps.
- **Defaults:** the CPU solver, with the renderer from the default package (DXVK).

## Low-core CPUs

**Setup:**
- **Scene:** the same as [VULKAN_MERGE.md](VULKAN_MERGE.md): 8 characters standing with contact plates, flatgrass at
  2560x1440, RTX 5090.
- **Simulated CPUs:** process affinity set at launch to 8 logical processors (0xFF: 4 cores / 8 threads of the
  9950X3D's V-Cache die) or 12 (0xFFF: 6 cores / 12 threads).
- **Workers:** the worker pool follows the mask, so the game runs 6 or 10 physics workers like such a PC.
- **Sessions:** four, about 8 minutes of runs in total.

Frame p50 / p95 ms; "behind" means the physics did not keep real time.

| CPU | Scene | D3D9, CPU solver | DXVK, CPU solver | DXVK, Vulkan solver |
|---|---|---|---|---|
| 4C/8T | 8 characters, 10 it | 3.71 / 5.02 | 3.62 / 5.00 | 4.34 / 6.85 |
| 4C/8T | 8 characters, 100 it | 5.72 / 7.39 | 5.69 / 7.49 | 3.75 / 7.87, behind: 222 ms lag, 6.7 s dropped |
| 4C/8T | + 48 citizens, 100 it | 10.47 / 14.52 | 10.67 / 15.17 | 8.62 / 17.66, behind: 245 ms lag, 5.9 s dropped |
| 6C/12T | 8 characters, 10 it | 3.05 / 4.15 | 3.14 / 4.22 | 3.62 / 5.26 |
| 6C/12T | 8 characters, 100 it | 3.64 / 5.52 | 3.80 / 5.53 | 4.59 / 6.65 (real time, 10 ms lag) |

GPU 3D load with the CPU solver: D3D9 39-53 %, DXVK 19-28 %.

1. **The CPU solver stays the better choice with fewer cores.** It keeps real time in every case.
2. **The Vulkan solver falls behind with few workers.**
   - **Cause:** each character's physics tick waits for its GPU solve on a worker thread. At 100 iterations 8
     characters need more waiting time per 16.7 ms tick than 6 workers have. The faster frames in those rows only mean
     the physics stopped keeping up.
   - **With 10 workers** it keeps real time, but frames are still 0.5-0.8 ms slower than with the CPU solver.
   - **Possible fix:** free the worker while the GPU solves (split the tick around the fence), or submit all characters
     at once whatever the worker count.
3. **DXVK frame times are within ±4 % of Direct3D 9** (slightly behind at 12 threads), with about half the GPU load.
   - On this GPU every scene is CPU-bound, so the lower load does not become frame rate here. On a weaker GPU it
     could.
   - It also enables DXVK-based modding.

**Caveat:** the V-Cache cores and the RTX 5090 are faster than a typical 4-6 core PC. The comparisons carry over, the
absolute numbers do not. On a weaker GPU the Vulkan solver also competes harder with rendering.

## Paths now tested with multicore rendering

**First person** (DXVK and D3D9 sessions, `mat_queue_mode 2`):
- **Setup:** the local player gets an MMD model and a physgun.
- **First person:**
  - The body is not drawn (its draw age never advances).
  - The viewmodel uses the model's arms.
- **Third person:** the body draws every frame through the queued path.
- **Retired first-person body path:** the clip plane with the first-person triangle mask, forced on, also draws every
  frame without errors. The view from inside the head stays clean.

**No vertex cache** (`mmdhl_vertex_cache 0`, the stream path):
- **Results:**
  - The model draws correctly (screenshot).
  - About 720,000 stream draws per run.
  - Physics keeps real time and contacts pass.
- **Speed:** it is slow, but faster than before:

  | Rendering | Frame p50 / p95 ms |
  |---|---|
  | Multicore (DXVK) | 19.8 / 23.2 |
  | Multicore (D3D9) | 20.2 / 24.1 |
  | Old single-threaded, same session | 27.9 / 33.0 |

**RTX Remix install** (the test install `GarrysMod-RTX`: dxvk-remix-gmod 12759b3, path tracing on, its own
`mat_queue_mode 2`):
- **Rendering:**
  - The addon's Remix fixed-function path is active.
  - Every measured frame ran in queue mode 2.
  - All three models were drawn from the render thread every frame and were path-traced (screenshot).
- **Results:** no errors, frames 10.2 / 11.9 ms p50 / p95.
- **Installation banner:** it reports "Renderer: other d3d9.dll".
- **Package:** Remix users need the `-opengl-remix` package, because the `-vulkan` one would replace Remix's `d3d9.dll`.

**Dark MMD layers under RTX Remix (fixed 2026-09-27).** Hair and stockings of some models rendered black under Remix
for the first seconds to minutes after a model appeared, and some stayed blocky (Linlong's hair) afterwards.
- **Cause:** MMD models layer alpha-textured overlays over hair and legs (highlight and shadow shells, stockings) that
  the materials' 0.5 alpha test removes entirely or mostly in raster. As the tests read, Remix traced those
  alpha-tested draws as opaque for lighting until its opacity micromaps were baked, over tens of seconds and at coarse
  resolution, so the parts beneath stayed unlit. With `rtx.opacityMicromap.enable = False` they stayed black. Alpha to coverage, snapshot
  rebuilds and material refreshes changed nothing; drawing the overlay blended fixed it at once.
- **Fix (`native/renderer.cpp`, Remix fixed-function path only):** `loadAsset` samples each alpha-textured triangle
  where it maps (corners, edge midpoints and centre). A triangle with no passing sample is left out of a part's own
  generated material draw (raster shows nothing of it), and a part left with none is not drawn. A part whose remaining
  triangles pass the test on under half their area blends, which Remix renders without shadows and which matches the
  test for the binary alpha of such layers. Mostly opaque parts keep alpha testing, and overrides are untouched.
- **Checked:** Furina, Linlong and Vodyanitsa spawned fresh render correctly 8 s after spawn and stay correct at 40 s,
  with Remix's default settings (micromaps on); before, their hair was black at 8 s.
- **Near-empty shells and atlas padding (fixed in 2.1.0-native.5):** native.3 measured the passing share of a part
  over its whole texture and blended or skipped whole parts. Furina's `手套` is a copy of her body whose texture is
  opaque only at the gloves (4 % passes); drawn blended over the body, Remix showed no legs beyond a few hundred units.
  Her socks use 7 % of an 8192² atlas (99 % where they map) and were blended too, with blocky thighs up close. Now the
  shell keeps only its glove triangles and the socks keep alpha testing. The raster light-overlap mask, which drops
  coincident duplicate triangles for Source's flashlight passes, is also not applied under Remix: with it, a frozen
  Furina lost her legs up close. Coplanar layers there are separated along their normals instead.
  `tests/fixtures/native-cutout-atlas.pmx` covers the sampling and the per-triangle cut.
- **Materials switched by a UV morph (fixed in 2.3.0, issue #7):** some models change what an alpha-tested part
  shows with a texture (UV) morph. Ruan Mei's stockings slide across an atlas of five styles (`[絲襪切換]`, morph 157
  counted from 0: 0.25 bodystocking, 0.5 pantyhose, 0.75 thigh-highs, 1 ankle socks). The cut above was measured once,
  at rest UVs, where her legs map to transparent texels, so under Remix the pantyhose kept only 1,136 of their 11,348
  visible triangles and the thigh-highs and socks none. Raster GMod was not affected.
  - **Fix:** `loadAsset` leaves the triangles a texture morph moves in the static cut and keeps their texture's
    pass mask (one bit per texel). The renderer cuts them again by the same rule (`native/cutout.hpp`) at each
    instance's current UVs, on the main thread and only when that instance's UV-morph weights change. The part
    then draws from index lists of that instance's own, rebuilt (with its buffers) only when the cut changes, so it
    stays one draw call per chunk and two instances of one model may wear different styles. Drawing the shared
    lists in index ranges was rejected: at rest the stockings split into 379 ranges, each a separate Remix
    geometry. Such a part is skipped only while nothing of it is left. Whether it blends is still decided once, by
    its coverage at rest UVs as before, because the engine material is shared by every instance; a part hidden at
    rest (Cantarella's emotes, Luo Tianyi's lenses: skipped before) keeps alpha testing. A held UV morph also no
    longer rewrites every vertex's UVs on each publish, on any renderer.
  - **Diagnostics:** `mmdhl.Decode(mmdhl.native.RenderStats()).remixCutout` lists, per instance with such a part,
    the triangles drawn now (`parts[k].kept`) of all the part has, the UV state they belong to, and the number and
    total time of recomputes.
  - **Checked offline** (Ruan Mei, cache at 4096: triangles of `黑絲襪` drawn at morph 157 = 0, 0.25, 0.5, 0.75 and 1):
    8,167, 18,377, 11,348, 9,104 and 6,142, against 8,167, 8,165, 1,136, 0 and 0 before. One recompute takes
    0.3-0.6 ms for her 151,174 triangles. Every other alpha-tested part is cut exactly as before.
    `tests/fixtures/native-cutout-uvmorph.pmx` covers the static and per-instance cuts and the publishing.

## Reproduce

```
powershell -File scripts/build.ps1
powershell -File scripts/build-dxvk.ps1
python scripts/native_manifest.py --renderer build-dxvk/src/d3d9/d3d9.dll   # rewrites native_policy.lua: do not commit that
python scripts/package-dropin.py --renderer build-dxvk/src/d3d9/d3d9.dll --output <folder>
pwsh -File scripts/update-native-policy.ps1 -RunUrl <run> -ReleaseUrl <release> -Artifact <folder with both packages>
python tests/test_installation.py
```

**In-game runs:**
- **Harness:** use `scripts/test-performance.py` in isolated sessions, as in [VULKAN_MERGE.md](VULKAN_MERGE.md).
- **Low-core:** set the game process's `ProcessorAffinity` right after launch, before the module loads, so the worker
  pool sizes itself from the mask.
- **DXVK:** DXVK runs need no `dxvk.conf` or `DXVK_CONFIG_FILE` any more.
