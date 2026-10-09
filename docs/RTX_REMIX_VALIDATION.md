# RTX Remix compatibility — preview.13

## Scope and implementation

Tested the supplied installation at `H:\GModRTX-Fresh-Test\GarrysMod-RTX`:
GMod `2026.09.15`, garrys-mod-rtx-remixed Nightly `ecf4c95` with the client's
existing Advanced Material Editor integration, dxvk-remix-gmod Nightly
`12759b3`, and the installed SourceRTXTweaks patches. No RTX runtime, shader,
material-editor module or ray-tracing quality settings were replaced.

The first failure happened before any mesh submission. The renderer rejected
the patched `engine.dll`, `materialsystem.dll` and `shaderapidx9.dll`, and
required `stdshader_dx9.dll`, which this RTX build deliberately does not load.
It uses a custom `stdshader_dx6.dll` and `VertexLitGeneric_DX7` instead.

The supported fingerprints are explicitly pinned in `render_binaries.hpp`.
The existing offline ABI suite confirmed that the model lighting/shadow and
entity interfaces are unchanged. Compared with this client's Vanilla base,
engine/materialsystem/shaderapidx9 differ by 10/25/11 instruction bytes;
client.dll is identical. Unknown binaries are still rejected. Exact diff and
ABI reports are retained privately in `validation/rtx-binary-diff.json` and
`validation/rtx-render-abi.json`.

Once rendering was allowed, full material overlays still disappeared. The
normal Source fix changes projection depth for overlapping materials. Disabling
that correction restored the missing hair and body. Projection changes are
incompatible with the tested Remix capture path.

For the validated fixed-function renderer, known overlapping materials now get
a tiny normal separation in their **render buffers**: 0.005 Source units per
layer, reduced when necessary so the total is below 0.05 units. Camera matrices
are unchanged. Upload buffers are per material because different layers may
share original PMX vertex indices. Buffers remain cached and triple-buffered;
non-layered parts retain the fast upload path. Cache keys distinguish layered
and unlayered data when the overlap option or material overrides change.

The original PMX, skinning matrices, UVs, morphs, physics and source collision
geometry are untouched. Same-material duplicate-face selection remains active.
Alpha-test holes, material hiding and explicit blended/ignore-Z overrides keep
their semantics. Normal GMod continues using the projection-depth correction
and shared vertex buffers. `RenderStats` reports `fixedFunctionRemix` and
`overlapLayerMode` for diagnosis.

This uses the client's normal Source-to-Remix capture path; it does not submit
a second character through the Remix mesh API or generate replacement PBR
assets. Captured texture hashes and downstream PBR conversion remain owned by
the installed Remix integration. See its [source](https://github.com/Xenthio/garrys-mod-rtx-remixed)
and the [Remix SDK](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/documentation/RemixSDK.md).

## Validation

- Reproduced the unsupported-renderer error in the unmodified addon.
- Reproduced missing overlapping hair/body regions after enabling the patched
  interfaces; directly inspected corrected standing and ground-pose captures.
- Verified Remix's texture-hash API reports the face, body, hair and glove
  materials. Representative overlay hashes were `0xCB9D2123019AC708` (髮+)
  and `0x83BDC99ECDA2A61C` (手套).
- Hid/restored the hair and glove overlays, exposing the underlying surfaces.
  Applied/reset an overflow submaterial override and a face morph. Three native
  ragdoll creation/removal cycles retained the 18-body rig and left no render
  errors.
- A focused 10-second movement measurement after warm-up at 2560×1440 collected
  764 frames: median **12.95 ms**, p95 **14.42 ms**, maximum **19.15 ms**.
  Mean native mesh rendering was **2.09 ms**. All 356 MMD bodies, 475 joints and
  18 Source bodies were retained at 10 iterations; there were **zero new
  resets and zero dropped simulation time** during measurement.
- All 17 native CTest cases passed, including binary rejection and overlap
  separation/order/bounds checks. This is one-model RTX compatibility evidence,
  not an eight-character RTX performance certification or a fresh multiplayer
  certification.

The test used the client's folder addons, including Remix and Advanced Material
Editor, with Workshop fetching disabled. The owned harness now has `-RTX` to
apply the launcher's DX90 / no-D3D9Ex arguments. Temporary input/video settings
and bootstrap are restored after testing. Evidence is under
`validation/rtx-evidence`; models and captures are excluded from release ZIPs.

Install the package normally into the **RTX game directory**, not the launcher
directory. Native DLL changes require restarting the game. The support is
automatic for this validated build; no material conversion or PMX reimport is
required. Updated Remix/Source binaries require renewed ABI validation.

## Materials switched by a UV morph (2.3.0)

Some models switch what an alpha-tested part shows with a texture (UV) morph
(issue #7; background in [DXVK_PACKAGE.md](DXVK_PACKAGE.md)). Under Remix such a
part is cut per instance at its current UVs, and `RenderStats().remixCutout`
reports what is drawn, so the check needs no screenshots. With Ruan Mei
(`阮·梅.pmx`) spawned as the only character in the RTX copy, run on the server
(morphs counted from 0, as in PMX Editor):

```
python scripts/gamectl.py server "local e=ents.FindByClass('mmdhl_ragdoll')[1] mmdhl.SetMorphWeight(e,154,1) mmdhl.SetMorphWeight(e,157,.5)"
```

and about a second later on the client:

```
python scripts/gamectl.py client "local s=mmdhl.Decode(mmdhl.native.RenderStats()) assert(s.fixedFunctionRemix) local c=s.remixCutout for _,i in ipairs(c.instances) do for _,p in ipairs(i.parts) do print(i.instance,p.part,p.kept,p.triangles) end end print(c.recomputes,c.indexBuilds)"
```

Expected per weight of morph 157 (0, 0.25, 0.5, 0.75, 1): part 10 (`黑絲襪`,
18,501 triangles) keeps 8,167, 18,377, 11,348, 9,104 and 6,142; part 11
(`襠部絲襪`) keeps 1,904 up to 0.5 and 0 from 0.75. With morph 158 at 1 as well
(white row), part 10 keeps 0, 0, 11,348, 9,104 and 6,142 and part 11 0, 0,
1,904, 0 and 0. Repeating the client call a few seconds later must print the
same `recomputes` and `indexBuilds` while the weights are held. Then sweep 157
from 0 to 1 over two seconds (a server timer setting it every tick): the counts
must end at the weight-1 row, and `recomputes` grows by at most about a quarter
of the frames rendered meanwhile. Look at the legs 8 s and 40 s after each change
(opacity micromaps), compare with the raster game at the same weights, and repeat
with the instance frozen, with `mmdhl_vertex_cache 0` and in first person. The
Furina, Linlong and Vodyanitsa check of DXVK_PACKAGE.md must stay unchanged.
