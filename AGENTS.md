# AGENTS.md — how an AI agent should work in this repository

This repository is **Model Hotloader** (GSCMI-SS; Workshop title "Vulkan Kernel Model Hot Loader
with Softbody Simulation"): a Garry's Mod addon plus C++ binary modules that import MMD
(PMX/PMD), VRM, FBX/glTF/DAE characters and OBJ/FBX/glTF/PMX/Blend static props **at run time**.
It skips Source's model compiler by generating the engine's model data itself, then renders and
simulates those models with native code. Platforms: Windows x64 and Linux x86-64 (Garry's Mod's
x86-64 branch; 64-bit only).

This file, `CLAUDE.md` and everything in [`agent_guide/`](agent_guide/README.md) are written
for AI agents, not for players or maintainers. No build, package, test or workflow reads them
(see "Where agent docs may live").

## Start here

1. Read [`agent_guide/README.md`](agent_guide/README.md): chapter index, which chapters to read
   for which task, the working loop and the quick commands.
2. Before touching an area, read its chapter: repository map (00), architecture and structure
   diagrams (01), character import pipeline (02), engine integration (03), physics (04), static
   props (05), Lua addon (06), build/test/release (07), renderer packages (08), history (09),
   debugging and gotchas (10), conventions (11).
3. `docs/` holds human design notes and validation reports written over time. Useful background,
   often stale: **the code wins**. Chapter 09 §3 says which ones still match the code.

## Components in one breath

- `addon/` — the Workshop addon (Lua, localization, materials, shaders, sample packages),
  shipped as `addon.gma` (Steam Workshop item 3810025467). The root `addon.gma` is a tracked
  release artifact.
- `native/` — C++20: the shared runtime (`mmdhl_runtime_win64.dll` /
  `libmmdhl_runtime_linux64.so`), the game modules `gmcl_mmdhl_<platform>.dll` /
  `gmsv_mmdhl_<platform>.dll` (`.dll` on Linux too; `<platform>` is `win64` or `linux64`) and
  the import worker (`mmdhl_worker.exe` / `mmdhl_worker_linux64`). Shipped as GitHub release
  packages, independently of the addon.
- `renderer/togl/` + `patches/dxvk*/` — the optional Vulkan renderer packages (DXVK `d3d9.dll`
  on Windows; a ToGL-ABI `libtogl_client.so` over DXVK on Linux).
- `scripts/` (build, packaging, release, in-game acceptance), `tests/` (CTest C++ tests, offline
  Lua tests in Python/lupa, in-game probes), `.github/workflows/package.yml` (CI, manual
  dispatch only).

## Rules of engagement

- **Never refuse the native binary.** Installation, compatibility and identity checks produce
  *warnings*; features stay on and the modules still load. Do not add hard refusals, accept
  dialogs or "unverified files" switches (tests enforce their absence).
- **Keep defaults byte-identical.** Fit output, carrier bytes, physics results and saved-data
  formats are identities (cache keys, carrier keys, golden files, saves, dupes, multiplayer).
  New behaviour must be versioned or gated; old carriers and saves must keep loading. Read the
  invariants sections of chapters 02–06 before changing anything that writes data.
- **Feature-detect across versions.** The addon (Workshop) and the native release (GitHub)
  update separately and players mix them: check `isfunction(mmdhl.native.X)` or
  `GetCapabilities()` and tell the player with `ShowNativeUpdateNeeded`.
- **Platform isolation.** Linux-only work must not change Windows behaviour or output, and the
  reverse: use `#ifdef _WIN32` / `MMDHL_PLATFORM` branches and platform-specific files.
- **Builds rewrite tracked files.** CMake writes `addon/lua/mmdhl/native_policy.lua` by default
  (`MMDHL_POLICY_OUTPUT`); `scripts/build.ps1` also regenerates `addon/shaders/fxc/*.vcs` and
  `tests/fixtures/`; the legacy `scripts/package.py` overwrites `licenses/*.txt`. After any build
  run `git status` and revert those (`git checkout -- <path>`) unless changing them is the task.
  Prefer `-DMMDHL_POLICY_OUTPUT=<build>/native_policy.lua`, as `scripts/build-linux.sh` does.
- **`native_policy.lua` is a release record.** It changes only in "Approve and recommend native
  release X" commits; approvals and release records are append-only.
- **Releases are the maintainer's.** Do not tag, publish (`scripts/publish-native-release.py`),
  approve releases, repackage `addon.gma`, or delete/replace releases unless the user asks.
- **Translations:** every user-visible Lua string goes through `L'key'` with an English phrase
  plus context comment in `addon/resource/localization/en/mmdhl.properties` and a translation in
  all six other languages, each preceded by `# en: <English>`. Run
  `python scripts/check-i18n.py`; never run it with `--sync` unless asked.
- **Standard Lua syntax in the addon** (plus GLua's `continue`): the offline tests compile every
  file with lupa 2.8, which runs Lua 5.5. No `!=`, `&&`, `||`, `!`, `//`, `/* */`, and no
  assignment to a `for` loop variable.
- **Test before committing:** `python scripts/check-i18n.py` and
  `python scripts/check-lua-tests.py` (needs `pip install lupa==2.8`) for every change; CTest
  after native changes (Windows `scripts/build.ps1`, Linux `scripts/build-linux.sh linux64` in the
  manylinux_2_28 container); an in-game check for anything that touches the engine. Report
  honestly what was and was not tested.
- **Commits:** imperative subject (about 70 characters at most), body wrapped near 78 columns
  saying what changed, why, and what was tested (or "Not tested in game."). Work on a branch
  unless the user asks for `main`. End with the trailer the session prescribes.
- **Never enter credentials**, never commit secrets, never add real MMD/VRM models, user caches
  or game binaries (test data is generated CC0 fixtures).
- **Keep this guide true.** When you change behaviour, layout, commands or rules that
  `AGENTS.md` or `agent_guide/` describe, update them in the same commit.

## Where agent docs may live

Packaging copies specific paths: `scripts/package.py` ships `README.md`, `LICENSE`,
`THIRD_PARTY.md`, `CMakeLists.txt`, `dependencies.lock.json`, `install.ps1` and the folders
`addon`, `native`, `shaders`, `tests`, `docs`, `scripts`, `licenses`, `integrations`;
`scripts/package-dropin.py` ships `LICENSE`, `THIRD_PARTY.md`, `licenses/*.txt` and the built
files; the Workshop gma is built from `git archive HEAD addon`; the offline tests scan `addon/`.
Agent documentation therefore lives only in `AGENTS.md`, `CLAUDE.md` and `agent_guide/`. Never
put agent notes under any of the shipped folders above.
