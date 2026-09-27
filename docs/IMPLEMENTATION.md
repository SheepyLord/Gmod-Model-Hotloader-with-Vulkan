> Historical documentation for the legacy custom-entity backend (0.2.0 and earlier). See [native carrier architecture/API](NATIVE_BACKEND.md) and [current validation](NATIVE_VALIDATION.md) for 0.3.0.

# Implementation ledger

Target: Windows x64 standard GMod, single player, custom PMD/PMX ragdolls,
Bullet rigid and soft physics, native deformation, MMD shader rendering,
two-way Source prop bridge, transactional explicit reload.

Required gates (record evidence, never infer passing from compilation):

- [x] Pinned native dependencies, parser, deformation and 84 regression checks
- [x] Client/server modules loading a shared runtime in the actual game
- [x] Native mesh rendering, shader/depth verification and reviewed screenshots
- [x] Map/prop collision extraction, two-way impact and measured impulse units
- [x] Generated primary ragdoll, authored rigid physics and PMX 2.1 fixture physics
- [x] Worker, library/preview/editor, grabber, reload and duplication
- [x] Anatomical primary limits and class-scoped vanilla physgun/poser adapters
- [x] Dedicated Q-menu MMD library tab and real engine input regression harness
- [x] Token-scoped game integration, screenshots, one/five-model benchmarks, cleanup
- [x] Corpus parsing and release packaging with source, notices and separate symbols

Evidence and remaining compatibility limits are recorded in `VALIDATION.md`.
Completion of a gate does not imply exhaustive compatibility with every MMD model.

Existing reusable references: sibling !GModel_Hot_loader (MIT worker and UI);
!Github_SCMI (anatomical rig reference); !vphysics_optimization (exact-build ABI
probe); garrys-mod-rtx-remixed (Source material interface headers).

Never package local real-model test assets. Corpus has 706 UTF-16 PMX 2.0 files;
QDEF and PMX 2.1 need generated fixtures. Use 120 Hz fixed physics with bounded
catch-up. Shared runtime owns all data crossing the client/server module boundary.
