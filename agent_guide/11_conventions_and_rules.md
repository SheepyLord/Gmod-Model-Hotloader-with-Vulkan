# 11 — Conventions and rules for editing this repository

Write code that looks like the code around it. The rules below are what the history and the
tests enforce; most were learned from regressions (see [09_history_and_lineage.md](09_history_and_lineage.md)).

## Code style

| Language | Conventions |
|---|---|
| C++ (`native/`, `renderer/togl/`) | C++20, namespace `mmd` (`mmd::props` for props, `togl`/`dxbk` in the renderer). Dense: several statements per line are normal (over 1,000 native lines exceed 200 columns); match the surrounding density instead of reformatting. `Json` = `nlohmann::json`. Errors are C++ exceptions inside the runtime; Lua bindings catch them and return `nil, message` (or error tables) — a binding must never throw into the game. Platform code under `#ifdef _WIN32` with POSIX counterparts in `native/posix.cpp`, `dialogs_posix.cpp`, `codepages.cpp`, `xpress_huffman.cpp`, `dds.cpp`. No new third-party code without `dependencies.lock.json`, `THIRD_PARTY.md` and a notice in `licenses/`. |
| Lua (`addon/lua/`) | Garry's Mod LuaJIT. One-space indentation, dense lines, `local` everything, public API on the `mmdhl` table (`mmdhl.native` = the binary module, `mmdhl.L` = translation). **Standard Lua syntax only, except `continue`**: the offline tests compile every addon file with lupa 2.8 — whose default runtime is **Lua 5.5** — after replacing `continue` by `break`. So GLua-only operators (`!=`, `&&`, `||`, `!`, `//`, `/* */`) and assigning to a `for` loop variable (a const in 5.5, legal in LuaJIT) break the test suite. Tests also locate code by exact text anchors (e.g. `function mmdhl.Spawn(`, `if not mmdhl.CheckInstallation()`); keep them or update the tests ([06](06_lua_addon.md) §11). User-visible strings only through `L'key'` / `L('key',{placeholder=…})`. |
| Python (`scripts/`, `tests/`) | Standard library only (plus `lupa==2.8` for tests, Meson/Ninja for DXVK). Compact one-file scripts with a module docstring stating usage. Must run on Windows and Linux (`pathlib`, no shell-specific behaviour) unless the script is Windows-only by nature. |
| PowerShell (`scripts/*.ps1`, `install.ps1`) | Windows build, install, in-game acceptance sessions. `$ErrorActionPreference='Stop'`; check `$LASTEXITCODE` after native tools. |
| Shell (`scripts/build-linux.sh`, `build-dxvk-linux.sh`) | `set -euo pipefail`; designed to run inside `quay.io/pypa/manylinux_2_28_x86_64:2026.10.09-1`. |

Comments: full sentences that explain **why** (a constraint, an engine fact, a measured result),
not what the next line does. Adapted third-party code keeps its notice: `native/secondary.cpp` and
`tests/nanoem_replay.cpp` carry MPL-2.0 portions of nanoem emapp (SPDX header); Bullet-derived
code (`secondary_mt.hpp`, `ordered_joint_setup.hpp`, `opencl_solver_kernel.hpp`, `dbvt_leaf.hpp`)
keeps the zlib notice with alterations marked. Keep it that way when you edit them.

## Data, versions and compatibility

- Every persisted or networked format is a contract between releases: the addon (Workshop) and
  the native modules (GitHub releases) update independently, players mix versions, servers and
  clients mix versions. Add optional fields; never change the meaning of an existing one; gate
  new behaviour by feature detection: `isfunction(mmdhl.native.X)` or `GetCapabilities()` fields
  (`api`, `rigVersion`, `rigGenerator`, `rigGeneratorMin`, `physicsEditor`, `fullTextures`, `version`), and
  tell the player with `ShowNativeUpdateNeeded(feature, '<release>')`, or without a release for a
  feature of the next, unlabelled release (`install.update.needed_newer`). `mmdhl.NativeReleaseAtLeast` exists
  (its ordering is tested) but the addon does not call it.
- Content identities: carriers, fits, prop bundles and textures are named by hashes of their
  bytes or inputs. A change to what is written changes identities; old ones must keep loading
  (saves, dupes, approvals, Workshop packages). Bump the relevant version
  (`RigGenerator`, `RigVersion`, `material_processing_version`, physics schema…) instead of
  silently changing output. Chapters 02–05 list the identities per area.
- Determinism is tested: the golden files pin the fitted carriers' ragdoll physics text and
  model hashes per platform (`tests/fixtures/physics/golden.json` Windows, `golden-linux64.json`
  Linux; the arithmetic differs per compiler). Regenerate them with
  `mmdhl_physics_profile_tests --record` from the repository root on each platform's release
  toolchain (MSVC; the manylinux_2_28 container's GCC 14) only when a change is meant to alter
  carriers, and say so in the commit. Secondary-motion behaviour is covered by the
  `secondary_*` CTests instead (see [04_physics.md](04_physics.md)).
- Never refuse the native binary: installation and compatibility findings are warnings
  (`issues[*].warning`), features stay enabled, an unknown game build still runs (each engine call
  is still guarded). There is deliberately no "accept unverified files" path; tests check that
  none exists in `addon/`.

## Localization workflow

1. Use `L'area.name'` in Lua. Dynamic keys need a `-- i18n-keys: key1 key2` comment so
   `scripts/check-i18n.py` sees them.
2. Add the English phrase to `addon/resource/localization/en/mmdhl.properties` (`mmdhl.<key>=…`)
   with a context comment above it (where it shows, what each `{placeholder}` holds).
3. Add the translation to `fr`, `ja`, `ko`, `ru`, `zh-cn`, `zh-tw`, each preceded by
   `# en: <the English text>`; keep placeholders identical; keep the game's official terms
   (glossary at the top of each file).
4. Run `python scripts/check-i18n.py` (0 errors). Never run `--sync` unless the user asks; it
   rewrites translation files.
5. The server sends language-neutral tokens; each client translates.

## Documentation

| Where | For whom | When to touch |
|---|---|---|
| `AGENTS.md`, `CLAUDE.md`, `agent_guide/` | agents | when you change behaviour, layout, commands or rules they describe (same commit) |
| `README.md` | players (also shipped in `scripts/package.py`'s zip) | install/usage changes |
| `ReleaseNote.md` | players | user-visible changes of the next release |
| `docs/*.md` | maintainers/humans (shipped in `scripts/package.py`'s zip) | design notes and validation reports; add a dated one for a significant feature or measurement |
| `THIRD_PARTY.md`, `licenses/` | legal | any dependency, adapted code or shipped binary |
| `Translation Templates Write.txt` | the Workshop page (BBCode, several languages) | Workshop description changes |

## Tests and verification

- Offline, every change: `python scripts/check-i18n.py`, `python scripts/check-lua-tests.py`
  (lupa 2.8; tests that need a build are skipped by `NEEDS_BUILD`).
- Native changes: build and run CTest on the platform you touched; Linux via the container
  (`scripts/build-linux.sh linux64`). CI (`Build drop-in package`, manual dispatch) runs both.
- Engine-facing changes need an in-game check (Windows: `scripts/game-start.ps1` and the
  `scripts/test-*.py` sessions; the debug bridge in `addon/lua/mmdhl/debug.lua`). Say in the commit
  what was not tested in game.
- Test data: only generated CC0 fixtures (`scripts/fixtures.py` → `tests/fixtures/`). Never add
  real MMD/VRM models, game binaries or user caches to the repository; model terms of use are the
  user's responsibility (`addon/lua/mmdhl/terms.lua`, `docs/MODEL_TERMS.md`).

## Git

- Work on a branch (`feat/…`, `fix/…`) unless the user asks for `main`. Maintainer merges are
  either merge commits (PR #12) or fast-forwards. Release branches: `release-<version>`.
- Commit messages: subject in the imperative or a short noun phrase (median 56 characters),
  blank line, body wrapped at about 78 columns: what changed, why, how it was verified, what was
  not. End with the trailer the session prescribes (most commits carry
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`).
- Release-only commits: "Approve and recommend native release X" (policy + `addon.gma`) and
  "Repackage addon.gma …". Do not imitate them unless the user asked for a release.
- Never commit build outputs (`build*/`, `vendor/`, `dist/`, `test-output/`, `validation/` are
  ignored), a locally rewritten `addon/lua/mmdhl/native_policy.lua`, or `linux-summary.md`.
