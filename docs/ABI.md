# Supported binary interface

Windows x64 Garry's Mod (both 64-bit branches), including the validated RTX variant. Workshop ABI profiles with normalized code/data evidence identify audited builds. Any other build is *unverified* and keeps rendering and physics off: the default branch's 64-bit build of 2026-09-17 keeps the interface version strings but has four fewer `IAppSystem` methods, which shifts every later `IMaterialSystem`/`IPhysics` slot (see [installation and compatibility validation](INSTALLATION_VALIDATION.md#game-updates)). Both successful and failed checks are cached. See [installation and compatibility validation](INSTALLATION_VALIDATION.md) for the current contract; the fingerprints below record historical audit baselines.

| Library | SHA-256 |
|---|---|
| engine.dll | `4c0b6d27a25215256862de2d10976e53dc59331fa253b204c51e8dda2ce0b745` |
| engine.dll (2026-09-22 update) | `7c21e827722fa7ac9ba4539dc652d3b79f58e240da49d28e75a1a9a1a88c4173` |
| client.dll | `11da950b136e4820815c106da8de65306828a27afa25f928ec6c4f86b10c3864` |
| client.dll (2026-09-22 update) | `8ad1a66160097b5a5bd4c5eefd8c8bca328b69e3fea15170e7939ce303ec6226` |
| vphysics.dll | `4ebd6149f885dfc518a44dd32dda64cbd6ebb3f938d43bdead70e80771b7e414` |
| materialsystem.dll | `b933ffacf596411773eaf8b7b6847cdda46e8d70191fad25e493be1f7e4639e5` |
| shaderapidx9.dll | `4a07b56a2f6f03d5c06bc3d3bddb67d26d9a12eb1c4d01eaf35eb52623ae7326` |
| stdshader_dx9.dll | `c9814355c1bdae82d1156e655795c1c0bc2086756cccc9c641fb2f2fde9bf546` |

`VPhysics031`, `VPhysicsCollision007`, and `VMaterialSystem080` are obtained through their factories. The physics adapter additionally checks the expected environment/object vtables and selected function RVAs. See `native/bridge.cpp` for the complete slot contract; `ProbePhysics()` emits the observed tables. The renderer identifies the queued and hardware render contexts by their RTTI class. Its reusable buffers use `CreateStaticMesh`, `CMeshBuilder::BeginModify` / `IMesh::ModifyBeginEx`, `ModifyEnd`, `Draw` and `DestroyStaticMesh` under the rendering DLL guards. Three buffers per material/format avoid rewriting the immediately preceding submissions; indices remain immutable. The previous dynamic-mesh stream remains available. Runtime Lua-created materials must be found with the `!` prefix.

`VEngineModel016` slot 21 (`IVModelRender::SetupLighting`) supplies the native Source model lighting state. Slots 12/13 (`DrawModelShadowSetup`/`DrawModelShadow`) are temporarily intercepted for explicitly registered MMD carriers, allowing their deformed geometry to participate in classic render-to-texture sunlight shadows. Unregistered entities retain the original callbacks. Installation rejects slots already replaced by another module; removal restores only slots still owned by this module. The last carrier's removal uninstalls the callbacks. `VClientEntityList003` resolves the carrier's client renderable under the separate client DLL guard. `ShadowBuild` requires a typed material reference for texture alpha, set through `IMaterialVar::SetMaterialValue`.

The generated carrier uses `VPhysicsCollision007` convex creation and serialization. See the guarded implementations for the exact ABI contracts.

`scripts/validate-render-abi.py` compares RTTI-derived interface tables and the complete contiguous unwind ranges for lighting/shadow methods against a previously pinned build. The September 22 engine retains the same method slots and instructions, with three relocated data references to identical data in shadow setup. The client executable section and entity-list tables are unchanged. This audit plus actual preview/ragdoll/shadow rendering supports the two additional fingerprints. See [single-player regression results](SINGLEPLAYER_RENDER_VALIDATION.md).

## Native carrier coordinates

Source simulates the 18 primary bodies in Source units. Each independent nanoem secondary world simulates in original MMD units with the pinned backend gravity and 60 Hz stepping. Source poses are converted through explicit bind offsets; secondary worlds have no Source mirrors or impulse feedback.

## Legacy bridge coordinate and impulse contract

- Model coordinates map to Source with `(x,y,z)MMD -> (z,-x,y)Source`. The reflected basis also changes rotation/axial-vector signs.
- Bullet stores metres, kilograms, radians. One Source unit is `0.0254` metre. Model scale is chosen from the requested Source-unit height.
- Source `GetInertia` supplies kg m². Its angular velocity is local degrees/s; the adapter rotates it into world radians/s.
- Solver momentum changes are captured around both rigid and soft solver phases. Linear impulse is returned as `J / 0.0254`; angular impulse as `L * 180/pi`. There is no extra `dt` factor.
- Source objects are updated once per server tick. Source gravity and Source-to-Source contacts remain owned by Source. Bullet mirrors never collide with other mirrors. Feedback is consumed once on the following capture, after removed pointers are discarded.

The in-game unit test applies 200 units of impulse to a 2 kg prop and measures 100 Source units/s. Applying `inertia.x * 10` along its local X axis measures 10 degrees/s. The impact test verifies actual feedback to a moving prop, in addition to the offline conservation check.

Physics handles are realm-local. Client and server DLLs share the runtime library but own separate worlds, including in a listen-server process. The installer places `mmdhl_runtime_win64.dll` beside the executable and beside the worker. Multiplayer replicates asset/rig identity rather than native handles; each client owns its own presentation and secondary simulation.
