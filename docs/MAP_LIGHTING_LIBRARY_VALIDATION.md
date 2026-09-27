# Map lighting and character library — preview.10

Branch: `claude/mmd-importer-performance-87a36d`.

## Changes

- The native renderer now supplies up to four Source model lights. `IVModelRender::SetupLighting` on the pinned game build stops after two; this excluded the third, bright ceiling spotlight in the reported `gm_construct` scene. `IEngineTool::GetLightingConditions` provides the complete light list, with the engine's existing color, light styles, attenuation and cone parameters. The original ambient cube and cubemap setup remains intact. Materials are not brightened or made self-lit.
- The added engine interface is guarded by the existing engine fingerprints and its validated x64 method address. `scripts/validate-render-abi.py` compares the new query across the supported builds. Diagnostics report the last complete light query without calling shader-state methods outside a material draw.
- The accepted overlapping-surface flashlight handling, material transparency, geometry and physics settings are unchanged.
- Library ragdolls, Citizens and Combine face the player at placement. The ragdoll and animated carrier have different forward axes; placement handles both. Explicit API/duplicator angles and player orientation remain authoritative.
- **External Models → Character Models** replaces the previous MMD library labels. Four buttons directly spawn a ragdoll, friendly NPC, hostile NPC, or apply the player model. NPC weapon selection still follows Sandbox. Player animation style and first-person arm customization are in a collapsed options section.
- Local folders support nested folders, rename, multi-model moves, and removal. Removing a folder moves its models to Unfiled. Folder operations never relocate/delete model assets. Library deletion retains the existing full asset cleanup behavior.
- The physics selector uses **Multicore CPU Processor**, **MMD Compatibility Processor**, **Legacy Multicore CPU Processor**, and **GPU Processor (Experimental)**. Backend IDs, saved selections and defaults are unchanged. Detailed distance/sleep tuning is collapsed; basic accuracy, collisions and recovery remain easy to find.

## Validation

Owned Windows x64 single-player, `gm_construct`, enabled folder/Workshop addons, 2560×1440. Camera origin `(1327.741089, -722.169128, -79.968750)`, angles `(10.226360, 120.087006, 0)` match the report.

Classic March 7th asset `e2c25f9ea0b736c55fe2fd26660f94f85fb5c0f1952fa4562ea3b3df39658eb7`, compared to `models/sheepylord/honkai_star_rail/march_7th.mdl`:

- Before: imported model remained dark even at the compiled model's exact location. The simple helper forwarded only two warm lights. The compiled renderer also supplied the bright white spotlight at `(1344, -672, 19.0067)`.
- After: all three relevant map lights reach the importer. Same-camera captures inside and at the edge of the spotlight show the expected light response. Directly under the bright light, **both** compiled and imported models saturate on pale hair; the update intentionally preserves Source's intensity.
- All 18 server-side ragdoll physics objects remain present. Client `GetPhysicsObjectCount` is not used as a server-body validation; the probe receives the authoritative count.
- Focused 10-second measurement after warm-up: **2,481 / 2,481 focused frames**, median **4.06 ms**, p95 **5.83 ms**, mean native render work **0.26 ms**. This is one full-detail character, not an eight-character benchmark. An earlier unfocused 20 FPS run was discarded.
- Final fresh-start verification with the shipped lighting diagnostics also passed: **2,545 / 2,545 focused frames**, median **3.94 ms**, p95 **5.35 ms**, mean native render work **0.24 ms**. Both focused measurements are retained; neither changes simulation detail.
- All four actual library buttons completed the normal network request/acknowledgement path. Native classes were `prop_ragdoll`, `npc_citizen`, `npc_combine_s` and the existing player. Facing checked against the rendered shoulder/head/pelvis axes: ragdoll dot 1.000, Citizen 0.999, Combine 0.978 (animated posture); NPC entity forward directions both dot 1.000. Ragdoll entity angles reset to zero internally, so they are not a valid facing measurement.
- In-game folder creation, Unicode nested names, multi-move, rename, removal and filtering passed. Test library preferences/folders were restored afterward. Offline folder tests also verify preservation of favorite/scale settings and reload behavior.
- Reviewed the actual Q menu, separate library window, preview and physics panel captures. Four actions are visible, the redundant physics navigation button is gone, and advanced sections are collapsed.
- Native CTest: **17/17 passed**. Python suites: QoL Lua, QoL controls, actor profiles, renderer readiness, sharing transport and library folders passed. No physics fidelity/detail reduction was made.

A development-only shader diagnostic caused one owned-client exit during investigation and was removed. The final diagnostic reads our own captured lighting inputs. No shader vtable probing is shipped.

New repeatable probes: `tests/game/map-lighting-probe.lua`, `library-folders-probe.lua`, `library-actions-probe.lua`. Run only in an owned test session. Private captures and measured JSON are under `validation/preview10-evidence/`; they are not included in the release archive. Previous immutable releases are retained. No new multiplayer compatibility claim is made by this single-player validation.
