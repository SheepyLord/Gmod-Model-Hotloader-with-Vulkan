# 07 — Build, test, CI and release

Verified against `main` @ `9e02a27`. Names of files/targets: see 00. **Builds write into the repo** (`vendor/`, `build*/`,
`dist/`, `test-output/`, and tracked files listed in §1.4): check `git status` before committing.

## 1. Build

### 1.1 `CMakeLists.txt` (CMake ≥ 3.24, C++20)
- **Platform** (automatic, not an option): `MMDHL_PLATFORM` = `win64` or `linux64` (64-bit only, else `FATAL_ERROR`); selects `MMDHL_{WORKER,RUNTIME,COACD,CLIENT,SERVER}_FILE`.
- **Cache variables** (a cached value survives re-configure; changing the default in the file does not affect an existing build folder):
  `MMDHL_RELEASE` (`"2.3.0"`, bump with `project(… VERSION)`) · `MMDHL_POLICY_OUTPUT` (default `addon/lua/mmdhl/native_policy.lua`) · option `MMDHL_BULLET_PROFILE` (OFF; ON → `BT_ENABLE_PROFILE=1`, use a separate folder such as ignored `build-profile/`).
- **Identity**: `MMDHL_BUILD_ID` = `<git rev-parse --short=12 HEAD | "source">-<UTC %Y%m%dT%H%M%SZ>`, fixed at **configure** time (a dirty tree still carries HEAD), written with the label, install API 1, platform and file names into `<build>/generated/release.hpp` (from `native/release.hpp.in`); `mmdhl_worker --version` prints `{release,build,installApi,api,platform}`.
- **MSVC**: `MMDHL_WARNINGS=/W4 /utf-8`, `/Zi` + `/DEBUG:FULL`, `MultiThreaded[Debug]DLL` CRT (packages require the VC++ x64 redistributable).
- **GCC**: PIC, `-ffunction-sections -fdata-sections`; link `-static-libstdc++ -Wl,--exclude-libs,ALL -Wl,--disable-new-dtags -Wl,-z,origin -Wl,--gc-sections`, RPATH `$ORIGIN`; Threads + `libdl` on every target (glibc < 2.34); runtime `-Bsymbolic -z nodelete`; modules hidden visibility + `-Bsymbolic`, no tier0/vstdlib; libgcc_s stays shared (exceptions cross binaries).
- **Common**: `NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS BT_THREADSAFE=1`; `CMAKE_POLICY_VERSION_MINIMUM 3.5` (pinned Bullet under CMake 4).
- **Vendored** (`EXCLUDE_FROM_ALL`): Bullet (static, multithreaded), glslang (`glslang-standalone` → `<build>/generated/vulkan_solver_spv.h` and, with `-DMMDHL_PROFILE`, `vulkan_solver_profile_spv.h`; the DXVK builds reuse this binary), Assimp (OBJ/FBX/glTF/COLLADA import, bundled zlib), meshoptimizer. Linux runtime: static ICU from `vendor/icu-linux64` (else `FATAL_ERROR` naming `build-icu.py`).
- **Post-build**: worker copies CoACD (`vendor/coacd/*.dll` | `vendor/coacd/linux64/*.so*`) beside itself; Linux splits debug info of runtime/worker/client/server (`objcopy --only-keep-debug` → `<file>.debug`, then `--strip-debug --add-gnu-debuglink`); target `mmdhl_native_manifest` (ALL) runs `python scripts/native_manifest.py --bin <worker dir> --output ${MMDHL_POLICY_OUTPUT}`. Python3 is REQUIRED.
- **Outputs**: `build/bin/Release` (MSVC) or `build-linux64/bin`.

### 1.2 Build scripts
| Script | Behaviour |
|---|---|
| `bootstrap.py` | Lock entries → `codeload.github.com` tarballs in `vendor/<name>` + `.mmdhl-revision` (`.archive-sha256` recorded, not checked); **raises** on a folder at another revision (delete it). CoACD wheels SHA-256-checked (Linux: `vendor/coacd/linux64`, RPATH `$ORIGIN`, `libgomp-*.so*`). In-place patches: Bullet virtual `solveSoftBodiesConstraints`; nanoem anchor index, world-anchor init, rope, soft pins (fails on changed context). Network needed. |
| `patch-bullet-threading.py` | Idempotent Bullet edits: thread-local counters/`gDisableDeactivation`, per-body soft RNG, joint rows active above `1e-20` (heavy PMX chains). |
| `build-icu.py --platform linux64` | ICU 74.2 (SHA-256), translit + normalization data only, static, `vendor/icu-linux64`; cached by hash of lock entry + script. |
| `shaders.py` | Windows only: pinned `ShaderCompile.exe` (SHA-256) → compiles `shaders/mmdhl_{vs20,ps20b}.hlsl` → copies to tracked `addon/shaders/fxc/`. Linux packages ship the committed `.vcs`. |
| `fixtures.py` | Rewrites tracked CC0 fixtures in `tests/fixtures/`. |
| `build.ps1 [-Configuration Release\|Debug] [-SkipBootstrap]` | bootstrap → patch → VS 2022 CMake via vswhere (`-G "Visual Studio 17 2022" -A x64`, folder `build`) → `--parallel 12` → shaders.py → fixtures.py → standalone checks (§2.2). No CTest; default `MMDHL_POLICY_OUTPUT`. |
| `build-linux.sh linux64 [name]` | Container only (`/opt/python/cp312-cp312`). pip `ninja==1.13.0` → bootstrap → patch → build-icu → `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DMMDHL_POLICY_OUTPUT=$PWD/build-linux64/native_policy.lua` → build → `ctest -j$(nproc)` → `build-dxvk-linux.sh` → `native_manifest.py --bin build-linux64/bin --output build-linux64/native_policy.lua --renderer build-dxvk-linux64` → `package-dropin.py`. `linux` (32-bit) exits 2. |
| `build-dxvk.ps1 [-Clean]` | DXVK `v3.1.1` (must be `b1a1c99`) in `vendor/dxvk`; resets and re-applies all `patches/dxvk/*.patch` unless every one is applied; needs build.ps1's `glslangValidator.exe` and pip `meson==1.12.1 ninja`; → `build-dxvk/src/d3d9/d3d9.dll`. |
| `build-dxvk-linux.sh <glslang>` | Container only (`dnf install SDL2-devel libX11-devel`). DXVK `v3.1.1`/`b1a1c99` in `vendor/dxvk-native` + `patches/dxvk/*` + `patches/dxvk-linux/*`, Meson `-Dnative_sdl2=enabled` → `build-dxvk-linux64/src/d3d9/libdxvk_d3d9.so.0.30101`; then `renderer/togl` (RelWithDebInfo, `-DMMDHL_DXVK_SOURCE`) → `build-dxvk-linux64/togl/libtogl_client.so` + `ctest` (`togl_exports`). |
| `renderer/togl/CMakeLists.txt` | `togl_client`; needs DXVK headers; `-static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL -Wl,-z,defs --version-script=exports.map`; debug split; `togl_exports` = `check_exports.py` (the 131 mapped symbols; extra exports other than `_ZTI/_ZTS/_ZTV` fail). |

### 1.3 Commands
Windows (VS 2022 Build Tools + Windows SDK, Python 3.12):
```powershell
./scripts/build.ps1                                   # -SkipBootstrap after the first run
ctest --test-dir build -C Release --output-on-failure # any ctest >= 3.24 (CI uses the runner's)
python tests/test_native_installer.py                 # needs pwsh
python -m pip install meson==1.12.1 ninja; ./scripts/build-dxvk.ps1
python scripts/native_manifest.py --renderer build-dxvk/src/d3d9/d3d9.dll
python scripts/package-dropin.py --renderer build-dxvk/src/d3d9/d3d9.dll [--name <name>]
git checkout -- addon/lua/mmdhl/native_policy.lua     # drop the local policy rewrite (§5.3)
```
Alternative: configure once with the same generator (`-G "Visual Studio 17 2022" -A x64`) and `-DMMDHL_POLICY_OUTPUT=<abs>/build/native_policy.lua`; the cache keeps it for `build.ps1`; pass `--output build/native_policy.lua` to manual `native_manifest.py` runs.

Linux, release-equivalent (as CI; container writes as root):
```bash
docker run --rm -v "$PWD:/src" -w /src quay.io/pypa/manylinux_2_28_x86_64:2026.10.09-1 scripts/build-linux.sh linux64 [package-name]
sudo chown -R "$(id -u):$(id -g)" .
```
→ `build-linux64/bin` (+`.debug`, `native-release.json`), `build-linux64/native_policy.lua`, `build-dxvk-linux64/`, `dist/<name>-vulkan[.zip]`, `dist/<name>-opengl[.zip]` (default name `Model-Hotloader-<release>-linux64`).

Linux development (system GCC ≥ 11; binaries need the build machine's glibc):
```bash
python3 scripts/bootstrap.py && python3 scripts/patch-bullet-threading.py && python3 scripts/build-icu.py --platform linux64
cmake -S . -B build-linux64 -DCMAKE_BUILD_TYPE=Release -DMMDHL_POLICY_OUTPUT="$PWD/build-linux64/native_policy.lua"
cmake --build build-linux64 -j"$(nproc)" && ctest --test-dir build-linux64 --output-on-failure -j"$(nproc)"
```
`build-dxvk-linux.sh` does not run outside the container. `physics_profiles` pins carriers recorded with the container's GCC 14 (comment in `tests/physics_profile_tests.cpp`); other compilers may not match (unverified).

### 1.4 Tracked files that builds rewrite
`addon/lua/mmdhl/native_policy.lua` (CMake default output; `native_manifest.py` without `--output`), `addon/shaders/fxc/*.vcs` (`shaders.py`), `tests/fixtures/*` (`fixtures.py`), `licenses/*.txt` (`package.py`).

## 2. Tests

### 2.1 CTest — 36 `add_test` (35 on Windows, 34 on Linux)
| Test (props) | Executable / args | Covers |
|---|---|---|
| `render_binary_validation_cache` | `mmdhl_render_guard_tests` | `ValidationOnce` caching; pinned render-DLL hashes |
| `adaptive_physics_and_materials`, `adaptive_physics_nan_tails` | `mmdhl_quality_tests native-cloth21.pmx` / `native-nan-tail.pmx` | Quality suspend/resume, iteration tiers, worker defaults, jiggle, material alpha |
| `native_runtime` (WD=src) | `mmdhl_tests` | Skinning, parser, vtable lengths, `SetMirror` validation, impulses, joints, physgun, soft bodies (writes `test-output/`) |
| `presentation_timing`, `presentation_timing_control` | `mmdhl_timing native-cloth21.pmx [dbvt]` | Fixed-60 presentation, vertex layouts, batched topology |
| `secondary_multicore` | `mmdhl_compute_tests cpu_mt` | Multicore solver vs reference rows |
| `secondary_async` (RUN_SERIAL) | `mmdhl_async_tests native-chain.pmx` | Async `cpu_mt_v2` equivalence, lag, teardown (clock-sensitive) |
| `gpu_skinning`, `gpu_skinning_morph_bounds` (env `MMDHL_REQUIRE_GPU_MORPH=1`), `gpu_skinning_control_root`, `gpu_skinning_control_root_pmd` | `mmdhl_gpu_skinning_tests` + `native-cloth21`/`native-far-morph`/`native-control-root.pmx`/`.pmd` | Hardware-skinning plan/topology vs CPU (no GPU) |
| `secondary_midphase` | `mmdhl_midphase_tests native-chain.pmx` | Mid-phase gate bit-identical; `auto` broadphase |
| `secondary_motion` | `mmdhl_motion_tests native-chain.pmx` | Attachments, thin map surfaces |
| `flashlight_overlaps` | `mmdhl_light_overlap_tests native-chain.pmx` | Overlap masks, Remix layers |
| `secondary_projection`, `_unaffected`, `_mass_ratio` | `mmdhl_projection_tests` + `native-stretch-prone`/`chain`/`mass-ratio` | Stabilisation keeps authored parameters |
| `secondary_heavy_chain` | `mmdhl_heavy_chain_tests native-heavy-chain.pmx` | Heavy links stay anchored (Bullet patch) |
| `secondary_stretch` / `secondary_stretch_ground`, `secondary_projection_ground` | `mmdhl_stretch_tests` / `mmdhl_stretch_ground_tests` | Sleeve constraints; contact-aware projection, floor reset |
| `secondary_broadphase` | `mmdhl_broadphase_tests` | dbvt-fast order, SAP→DBVT migration |
| `vrm_import_and_springs` | `mmdhl_vrm_tests` | VRM → PMX, spring solver, colliders |
| `static_props` | `mmdhl_props_tests` | Prop cache, collision, materials, `.blend`/zstd, textures |
| `animated_carriers_and_sharing` | `mmdhl_actor_tests native-cloth21.pmx` | Actor animation/IK, arms/first-person geometry, sharing hashes |
| `torso` (WD=src) | `mmdhl_torso_tests` | Torso resolver, fit pins, `bonemap/proposal.json` |
| `installation_and_abi_evidence` | `mmdhl_installation_tests` (**Windows**) | Worker probe, PE evidence, vtable checks |
| `windows_packed_models` | `mmdhl_xpress_tests addon/data_static/mmdhl/files` (**Linux**) | XPRESS decoding of Windows-packed packages |
| `workshop_packages`, `model_terms_of_use` | `mmdhl_package_tests`, `mmdhl_model_notes_tests` | Exported GMA read back; readme encodings |
| `import_errors` (WD=src) | `mmdhl_import_error_tests <worker>` | Error codes/places, worker `status.json`, crash log |
| `physics_profiles` (WD=src) | `mmdhl_physics_profile_tests` | Overrides canonicalisation, `.phy`; golden `physics/golden.json` (Win) / `golden-linux64.json`; `canonical_cases.json` |
| `file_access` (WD=src) | `mmdhl_file_access_tests <test worker> <worker>` (**Windows**) | Path policy, links, grants, dialogs via `MMDHL_FA_TEST_ANSWER` |
| `character_import`, `character_import_worker` (WD=src) | `mmdhl_character_import_tests`; `python scripts/test-character-import.py <worker> <char tests> <vrm tests>` | Naming, synthetic glTF/DAE → fit, `rules.json`/`slots.json`; worker `status.json` contract |

Plus `togl_exports` in the renderer build. CI: Windows CTest serial, Linux `-j$(nproc)` (hence `secondary_async` RUN_SERIAL, `1df48da`).

### 2.2 Other native checks
`build.ps1` also runs `mmdhl_tests cloth21.pmx rope21.pmx`, `mmdhl_timing … dbvt` and `dbvt-fast`, `mmdhl_compute_tests cpu_mt`, `mmdhl_broadphase_tests`, `mmdhl_midphase_tests native-chain.pmx [<MMDHL_ASSET_CACHE> bac307fd…]` (private dense asset when cached; default cache under `H:/SteamLibrary/…`), `mmdhl_async_tests`, `mmdhl_props_tests`. Unregistered tools: `mmdhl_replay`, `mmdhl_profile`, `mmdhl_parameters`, `mmdhl_retarget_probe`, `mmdhl_skinning_analysis`, `mmdhl_vulkan_compare` (Win). Env: `MMDHL_DISABLE_OPENCL`, `MMDHL_DISABLE_VULKAN`, `MMDHL_TEST_WORKER_TIMEOUT`.

### 2.3 Offline Python/lupa tests
```bash
python -m pip install lupa==2.8
python scripts/check-lua-tests.py          # every tests/test_*.py except NEEDS_BUILD
python scripts/check-lua-tests.py names    # names containing "names"
```
Runs `python -B tests/<file>` from the root; exit 1 on any failure or no match. `NEEDS_BUILD = {'test_native_installer.py'}` (needs `build/bin/Release` + pwsh). `test_release_evidence.py` calls `pwsh` too (fails without PowerShell 7). Five tests (`test_installation.py`, `test_native_update_session.py`, …) read the working-tree `native_policy.lua`: run them on the committed one.

### 2.4 Translations
`python scripts/check-i18n.py` exits 1 on errors (Lua key missing from English, bad key, empty English, placeholder mismatch, malformed/duplicate lines, uppercase language folder); warnings (unused key, missing context comment, outdated `# en:`) pass. Untranslated phrases fall back to English and are only counted. `--csv`/`--template` leave catalogues alone. **Never run `--sync` unless the user asks** (it rewrites `<lang>/mmdhl.properties`), although `docs/TRANSLATING.md` tells developers to.

### 2.5 In-game acceptance (Windows only)
Needs a Windows GMod (default `H:\SteamLibrary\steamapps\common\GarrysMod`; `-GameRoot`/`--game`), a Release build and private models (`~/Desktop/test_models/{心,星穹铁道—昔涟5,桑多涅}.pmx` for `prepare-acceptance.py`, `validation/compatibility-models.json`, `tests/v2-assets.json` assets).
```powershell
./scripts/game-start.ps1 -Visible    # runs install.ps1 -InstallAddon, owned session, validation/session.json
python scripts/prepare-acceptance.py
python scripts/test-carrier.py       # full sequence: README "Build and verify", docs/DEBUGGING.md
python scripts/gamectl.py server --file tests/game/native-limits.lua
python scripts/reload-lua.py         # Lua only; native changes need a restart
./scripts/game-stop.ps1              # always, also after failures
```
`game-start.ps1` defaults: `-Map gm_flatgrass`, `-Width 3840 -Height 2160`, `-TickRate 128`; also `-Isolated`, `-NoWorkshop`, `-RTX`, `-QueueMode`, `-SkipInstall`, `-AllowOtherInstallation`. Bridge: token-gated `addon/lua/mmdhl/debug.lua`. No Linux launcher exists in the repo.

## 3. CI — `.github/workflows/package.yml` ("Build drop-in package")
`workflow_dispatch` only (any branch); concurrency `${{ github.workflow }}-${{ github.ref }}` cancel-in-progress; `contents: read`. Two independent jobs.

**`package` (Windows, `windows-2022`, 150 min)**: checkout@v6 → setup-python@v6 3.12 → `check-i18n.py` → `pip install lupa==2.8` + `check-lua-tests.py` (committed policy, before the build) → cache@v5 `vendor` (`!vendor/dxvk`, noted as ineffective) key `vendor-<hash(lock, bootstrap.py, patch-bullet-threading.py, shaders.py)>` → `build.ps1` (`MMDHL_ASSET_CACHE=${{ runner.temp }}/mmd_hotloader`) → `ctest --test-dir build -C Release --output-on-failure` → `tests/test_native_installer.py` → `pip install meson==1.12.1 ninja`, `build-dxvk.ps1`, `native_manifest.py --renderer build-dxvk/src/d3d9/d3d9.dll` → `package-dropin.py --name Model-Hotloader-<release>-<sha8>-win64 --renderer …` (release read from `native-release.json`) → upload-artifact@v6 `<name>-vulkan`, `<name>-opengl-remix` (folders; `if-no-files-found: error`).

**`linux` (`ubuntu-24.04`, 120 min)**: checkout → cache `vendor` key `vendor-linux64-<hash(… + build-icu.py)>` (holds `dxvk-native`, ICU) → release via `sed` on `set(MMDHL_RELEASE "…"` → `docker run … -e GITHUB_STEP_SUMMARY=/src/linux-summary.md <manylinux image> scripts/build-linux.sh linux64 "$name"` → `sudo chown -R` → append `linux-summary.md` to the summary → upload `<name>-vulkan`, `<name>-opengl`. No i18n/Lua tests.

**Job summary**: `package-dropin.py` appends (when `GITHUB_STEP_SUMMARY` is set) the package names and the release record JSON, saying to add it under `releases` (win64) or as `releases["<release>"].platforms.linux64`. The policy file CI writes is discarded.

## 4. Release data
**Release record** (`<bin>/native-release.json`; in packages `garrysmod/lua/bin/mmdhl-native-release.json` or `…-linux64.json`): worker identity `{release, build, installApi:1, api:1, platform}` + `files.{client,server,runtime,worker,coacd}={name,size,sha256}` + optional `renderer` + `url`. Renderer win64 `{name:"d3d9.dll", size, sha256, kind:"dxvk", dxvk:"v3.1.1", patches}`; linux64 `{name:"libtogl_client.so", …, patches incl. "dxvk-linux/…", library:{name:"libdxvk_d3d9.so", size, sha256}}` (tag regex-read from the DXVK scripts, patches from `patches/`).

**`native_policy.lua`**: `-- Generated release evidence. Do not edit hashes by hand.` + `return util.JSONToTable([==[…]==])`. JSON `{schema:1, approved:[labels], releases:{key: win64 record + url (tag page) + altUrl (mirror) + platforms:{linux64: record + url + altUrl}}, recommended:label, revoked?:{label: i18n key}}`. Keys may differ from `record.release` (`2.1.0-native.5+d4a199ab`); `legacy-actors-preview` has `installApi:0`; `revoked` is read by `installation.lua` (advisory, still runs) and written by no script. `installation.lua` `recordFor` uses the entry for win64, `platforms[platform]` otherwise.

**Packages** (`package-dropin.py --bin <bin> [--renderer <d3d9.dll|build-dxvk-linux64>] [--name] [--output dist] [--dxvk-source]`; refuses files that differ from the record or missing notices; never contains the Lua addon):

| Package | `GarrysMod/` contents |
|---|---|
| `…-win64-vulkan` | `garrysmod/lua/bin/` 5 natives + `mmdhl-native-release.json`; `bin/win64/mmdhl_runtime_win64.dll`; `garrysmod/shaders/fxc/mmdhl_*.vcs`; `bin/win64/LICENSES/`; `bin/win64/d3d9.dll` + `DXVK-LICENSES/`; `INSTALL.txt` (CRLF) |
| `…-win64-opengl-remix` | same without DXVK |
| `…-linux64-vulkan` | `garrysmod/lua/bin/` 5 natives + `libgomp-*.so*` + `mmdhl-native-release-linux64.json` + `LICENSES/` (+icu, libgomp); shaders; `bin/linux64/{libtogl_client.so, libdxvk_d3d9.so, DXVK-LICENSES/ (+togl.txt)}`; mode 755; `INSTALL.txt` (LF) |
| `…-linux64-opengl` | same without `bin/linux64` |

Without `--renderer`: one natives-only package `<name>`. Each folder is zipped beside it.

## 5. Release process
### 5.1 Scripts
- **`native_manifest.py [--bin build/bin/Release] [--output addon/lua/mmdhl/native_policy.lua] [--renderer …] [--check]`**: platform = the one worker in `--bin`; hashes files; loads the policy (or a new one). Without `--check` it **makes the label recommended and approved and replaces `releases[label]`** (win64 entry keeping `platforms`, or `platforms.linux64`) with `url` = generic releases page and no `altUrl`; writes `<bin>/native-release.json`; keeps a recorded renderer if `--renderer` is absent. `--check` compares (renderer too when a local build exists).
- **`publish-native-release.py publish --run <id> --notes whats-new.md [--game-root <GMod>] [--commit-note f] [--trailer "…"]… [--dry-run]`** (needs logged-in `gh`, `lupa`, `gmad.exe`; work folder `build/release-run-<id>/`):
  1. origin must be exactly `SheepyLord/Gmod-Model-Hotloader-with-Vulkan`; on `main` == `origin/main` (or one unpushed publication commit of this label); no uncommitted changes; no untracked files in `addon/`. Run = "Build drop-in package", success, commit on `origin/main`.
  2. Downloads the artifacts (only `-win64-{vulkan,opengl-remix}`, `-linux64-{vulkan,opengl}`; Windows required) and checks them: one record per platform, `installApi 1`, files and renderer match, renderer only in `-vulkan`, same OpenMP lib, build-ID commit = run commit, one label. Re-zips with the build timestamp and fixed modes (reproducible).
  3. Adds the record (`url` = `…/releases/tag/<label>`, `altUrl` of the current recommended release, Linux as `platforms.linux64`), approves, recommends; refuses a different existing record or a policy not in `json.dumps(indent=2)` layout. `evaluate()` runs `installation.lua` (lupa) for every approved release × platform × client/server and allows no issue. Runs `check-i18n.py`, `check-lua-tests.py`; builds `addon.gma`.
  4. `gh release create <label> --target <run commit>` with the zips and composed notes; never replaces tags/releases, completes an interrupted one.
  5. Commits `native_policy.lua` + `addon.gma` ("Approve and recommend native release <label>" template + note + trailers), pushes `main`; earlier failures restore both files, reruns resume.
  `--dry-run`: steps 1–2, policy diff, `evaluate()`, `addon.gma` in the work folder, notes printed; it skips `check-i18n`/`check-lua-tests` (code; the docstring says steps 1–3). `supersede <label> --note f` prepends a note to an older release. Workshop and mirror uploads stay manual.
- **`addon.gma`** (`package_addon`): `git archive HEAD addon` → new policy written into that tree → `<game-root>/bin/win64/gmad.exe` (or `bin/gmad.exe`) `create -folder … -out …` → header must equal the committed GMA and every entry its file; unchanged content keeps the old bytes. Only `gmad.exe` is searched, so publishing needs a Windows game folder (default `H:\SteamLibrary\steamapps\common\GarrysMod`). Linux `gmad` (Addon Creator 1.1) cannot replace it: it rejects `shaders/fxc/*.vcs` as "Not allowed by whitelist" (checked 2026-10-16); the repo itself does not document this.
- **Legacy** `update-native-policy.ps1 -RunUrl -ReleaseUrl [-AltReleaseUrl] [-Artifact] [-Replace]`: PowerShell 7, win64 records only.
- **Legacy** `package.py --version <label>` (Windows; `native_manifest.py --check` must pass on the default policy path; refuses an existing label; overwrites `licenses/*.txt` from `vendor/`) → `dist/model-hotloader-<v>-win64.zip` with root files `README.md LICENSE THIRD_PARTY.md CMakeLists.txt dependencies.lock.json install.ps1`, folders `addon native shaders (top-level files only) tests docs scripts licenses integrations`, client/server/runtime + 16 exes + `native-release.json` + `lib_coacd.dll`; plus `-symbols.zip` (PDBs) and `-SHA256.json`. Omits `patches/`, `renderer/`, `shaders/vulkan/solver.comp` (cannot rebuild the tree), `ReleaseNote.md`, `addon.gma`, `.github/`.

### 5.2 The maintainer's sequence (2.3.0; 2.2.0 = `c9385a0` likewise)
1. Prep commit bumps `project VERSION` + `MMDHL_RELEASE` (+`RigGenerator`) (`46ed406`); feature branches → `release-2.3.0` → PR #11; Linux PR #12; follow-ups on `main` (`609ce83`, `1df48da`).
2. `ReleaseNote.md`/`README.md` commits (`bbe958a`, `d63eaad`).
3. "Repackage addon.gma with …" commits after Lua changes (`63b581c`, `800fd20`, `0e7d5d4`, `3313640`), built like `package_addon`.
4. Run "Build drop-in package" on `main` (run 38215722189 at `1df48da`); adapt/dry-run the publisher if needed (`0db09ee`).
5. `publish` → tag/release `2.3.0` at `1df48da` with four zips → commit `9e02a27` pushed.
6. Manual Workshop upload of `addon.gma`; mirror upload.

### 5.3 Local-build policy rule
`native_policy.lua` changes only in "Approve and recommend native release …" commits. A local build at the default output (or `native_manifest.py` without `--output`) overwrites the current label's record with local hashes, recommends it and drops its `altUrl`: never commit it. Use `-DMMDHL_POLICY_OUTPUT=<build>/native_policy.lua` (as `build-linux.sh`) or `git checkout -- addon/lua/mmdhl/native_policy.lua`.

### 5.4 `ReleaseNote.md`
Newest release on top as `# Model Hotloader X.Y.Z`; bold first paragraph naming the package zips; bullets with a bold lead and issue links; previous release demoted to `##`; trailing shared sections `## Install and use`, `## Implementation`, `## Limits`, `## Build and verify` (partly outdated). GitHub notes come from `--notes` plus the script template.

## 6. Build/test invariants
| Invariant | Enforced / checked by |
|---|---|
| Windows output unchanged by Linux work; MSVC keeps `/W4 /utf-8` | `if(MSVC)`/`if(WIN32)` split in `CMakeLists.txt` (`767effc`: without `/utf-8` `humanoid_map.cpp` fails C2015); Windows job untouched by `609ce83`; review |
| Linux binaries need only glibc 2.28 | Built in `manylinux_2_28_x86_64:2026.10.09-1` (workflow, `build-linux.sh`, README); no automated symbol-version check |
| Linux link model (static libstdc++, shared libgcc_s, hidden/`-Bsymbolic`, RPATH `$ORIGIN`) | `CMakeLists.txt`; shim exports: `togl_exports` |
| Pinned versions | `dependencies.lock.json` (SHA-256 for CoACD/ICU), DXVK tag+commit checks, ShaderCompile SHA-256, container tag, pip pins (`lupa`, `meson`, Linux `ninja`); CI cache keys hash lock + scripts |
| Local patches applied on every path | `bootstrap.py` (context check), `patch-bullet-threading.py` in both build scripts (`cbbd560`), CTest `secondary_heavy_chain`; DXVK scripts re-apply from the tag |
| Same native file names everywhere | Six places (00 §9); partly covered by `test_release_evidence.py`, `test_installation.py` |
| Packages = recorded bytes | `package-dropin.py`, `publish` `check_packages`, `test_release_evidence.py` |
| Policy append-only, approved releases keep loading | `publish` (`add_release`, `evaluate`); `test_native_update_session.py`, `test_installation*.py` |
| Releases/tags immutable | `publish` `ensure_release`; `update-native-policy.ps1` needs `-Replace`; `package.py` refuses existing labels |
| `addon.gma` = committed `addon/` | `package_addon` compare; untracked `addon/` files block publish |
| Unedited carriers byte-stable per platform | `physics_profiles` golden files; `RigGenerator`/`RigGeneratorMinLoadable` |
| C++ ⇄ Lua catalogues agree | `slots.json` (`character_import`, `test_bone_mapper.py`); `canonical_cases.json` (`physics_profiles`, `test_physics_editor.py`) |
| Translations consistent | `check-i18n.py` (CI, `publish`) |
| Windows-packed data decodes on Linux | `windows_packed_models` |
| Code-page tables = Python codecs | `codepage-tables.py --check` (manual only) |
| `MMDHL_RELEASE` line format | Linux job `sed 's/^set(MMDHL_RELEASE "\([^"]*\)".*/\1/p'` |

## 7. Stale or contradictory docs (code wins)
- `README.md` lists `build-dxvk-linux.sh` for system-compiler builds; it needs the container.
- `ReleaseNote.md` ("Native package from GitHub Actions"), `docs/DXVK_PACKAGE.md`, `docs/INSTALLATION_VALIDATION.md` describe the `update-native-policy.ps1`/`package.py` flow; releases since 2.2.0 use `publish-native-release.py` (`992e9ef`), Linux included (`0db09ee`).
- `docs/DEBUGGING.md` and `ReleaseNote.md`: launcher default 2560×1440; `game-start.ps1` defaults to 3840×2160.
- `native/release.hpp.in` comment lists platform `linux` (removed in `609ce83`).
- `publish-native-release.py` docstring: `--dry-run` "does steps 1 to 3"; the code skips the i18n/Lua test runs.
- `docs/NATIVE_VALIDATION.md` "Release scope" (Windows single-player only) and older CTest counts are historical; `docs/API.md` is labelled historical but documents 2.3.0 functions.
