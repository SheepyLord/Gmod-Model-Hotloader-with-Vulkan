# Native installation and game compatibility

The first release of the checker requires **2.1.0-native.1**. Windows x64 GMod
is the only supported platform. Workshop supplies Lua; the GitHub package
supplies the native files. Normal installation no longer installs a loose addon
that could override subsequent Workshop updates. Developers can opt into that
layout with `install.ps1 -InstallAddon`.

## What is checked

Before `require`, Lua reads physical `MOD` / `BASE_PATH` files and compares their
sizes and SHA-256 hashes with `native_policy.lua`. An installation must match
one approved release, including its realm module and executable-side runtime.
Clients additionally check the worker, worker-side runtime and CoACD. Dedicated
servers use the runtime beside `srcds_win64.exe` and do not require client-side
import components. The previous unversioned installation is recorded as obsolete.

After loading, the native module reports its compiled identity, loaded paths,
expected paths and hashes. Both 64-bit branches load the client runtime from
`bin\win64`: the x86-64 branch starts `bin\win64\gmod.exe`, and the main branch
(since 2026-09-17) starts `gmod_win64.exe` in the game folder. Native builds up
to 2.1.0-native.5 expect the runtime beside the executable, so Lua also accepts
the runtime file it checked under the loaded module's game folder. A contained worker process checks startup, runtime
identity and optional CoACD exports. It runs asynchronously with a 10-second
deadline. Invalid CoACD is never loaded by the self-test; requested detailed
collision falls back to a simple hull. A failed worker disables imports while
cached models remain usable. A core failure prevents native initialization.

External Models and `mmdhl_open` remain available without the DLL. A dismissible
notice appears once per Lua session; the persistent banner provides Download,
Copy diagnostics and Recheck. Server failures are explicitly attributed to the
server administrator. The minimal status protocol is registered before native
loading, including safe handling of client initialization requests.

`mmdhl_check_installation` reads the files again but never reloads DLLs. Repairs
that change an unavailable component require a full game restart. Native and
game-library checks are cached, including failures; render/simulation hooks do
not hash files. Diagnostics remain local unless the user copies them.

## Using unverified native files anyway

Hash and release mismatches no longer block the addon permanently. Files that
do not match an approved release (another or newer version, a custom build,
modified or damaged files, a mixed installation, or a known release that is no
longer approved) are reported as *unverified*. The banner then offers
**Use anyway…**, which explains the risk (crashes, damaged saves, unpredictable
behaviour) and asks for confirmation. After the next map load or game restart
the native module loads and every feature works. The accepted problems leave the
External Models banner and the pop-up notice; the installation window
(Utilities → User → Character Models → **Model Hotloader — installation**, or
`mmdhl_installation`) still lists them marked "(warning accepted)", and its
**Stop using unverified files** withdraws the acceptance.

A loaded module or runtime that differs from the checked files, or was loaded
from another path, is unverified in the same way: **Use anyway…** accepts the
checked files together with the size and SHA-256 of the loaded module and runtime.

The acceptance is stored in `data/mmd_hotloader/unverified_native.json` as a
per-realm fingerprint of the exact size and SHA-256 of every checked file, so any
changed file asks again. In single player and on a listen server the host
accepts the client and the server files together. A dedicated server
administrator uses `mmdhl_accept_unverified_native` (or
`mmdhl_revoke_unverified_native`) in the server console, then changes the map.

Accepting never overrides missing or unreadable files, unsupported platforms,
releases without installation verification (`installApi` 0), a module whose
native API differs from this Lua, or game compatibility (ABI) checks. After
loading, accepted files are identified
by their bytes on disk instead of by release: the loaded module and runtime must
still equal the files that were checked, and the import worker must match its
own runtime.

## Game updates

The installer writes custom `mmdhl` module/runtime/worker names and CoACD in the
Lua module directory; it does not replace vanilla DLLs. Normal Steam updates
should retain these separate files. File survival and ABI compatibility are
independent: retaining an importer DLL does not prove it can use a changed engine.

Our own files are verified by hash (above). Game libraries change with every
Garry's Mod update. `compatibility_policy.lua` describes the builds audited for
the compiled `source-win64-v1` ABI family: executable and read-only PE sections
(DIR64 image-base relocations normalized), with the RVAs of the existing guards.
A library that matches a profile is reported as `tested` or `abi-evidence`; any
other build is `unverified`.

An unverified build keeps model rendering (client) or physics (server) off, with
the message "This Garry's Mod build is not supported yet", which **Dismiss** can
hide. Imports, the library and every other feature keep working. 2.1.0-native.6
itself would run unverified builds behind runtime checks (interface versions,
slot ownership, RTTI class names, guards learned from their first observation),
but the default branch's 64-bit build of 2026-09-17 showed those checks are not
enough: its `IAppSystem` has four fewer methods than the x86-64 branch's, so the
unchanged `VMaterialSystem080` and `VPhysics031` interfaces have every later
method four slots lower. The compiled `IMaterialSystem::GetRenderContext` call
landed on a function that waits for a render job, and the model preview hung the
game. The Lua therefore disables those features for unverified builds; supporting
another build needs its profile and, when its layout differs, native code built
for it. Native releases up to 2.1.0-native.5 reject unverified builds themselves.

Every build also passes the runtime checks before private calls or hooks: named
factories and interface versions, and the ownership of each used vtable slot by
the expected library (a slot another module replaced is refused). Physics
environment checks remain deferred until that environment exists; missing game
libraries are retried after map load.

## Release workflow

1. Set `MMDHL_RELEASE` when configuring a new immutable native release. CMake
   gives all components one build ID; `GetCapabilities()` also exposes it.
2. Build the Release configuration. The `mmdhl_native_manifest` target generates
   `native_policy.lua` and `build/bin/Release/native-release.json` from the actual
   outputs. Existing release records are retained. `approved` and `recommended`
   in the Lua policy control Workshop acceptance; removing an ID from `approved`
   revokes it. Do not remove old records needed to diagnose obsolete installs.
3. Run native, policy, installer and owned-game validation. Run
   `python scripts/native_manifest.py --check` before packaging. For the drop-in
   packages, build DXVK with `scripts/build-dxvk.ps1` and record it with
   `python scripts/native_manifest.py --renderer build-dxvk/src/d3d9/d3d9.dll`
   (the release record's optional `renderer` entry; see `DXVK_PACKAGE.md`).
4. Package with `python scripts/package.py --version 2.1.0-native.1`. The label
   must match the compiled identity. Existing release archive labels cannot be
   overwritten. Installer checks both source and destination hashes and refuses
   running games/workers. Recognized loose addons are backed up outside mounted
   addons; conflicting or ambiguous folders are left intact with instructions.
5. Publish the matching immutable GitHub release first, then publish Workshop
   Lua and its policy. Lua-only releases retain native approvals. Publication is
   a separate maintainer action; these scripts do not upload anything.

For an engine-only update, `validate-render-abi.py --emit-profile` can emit a
profile when the renderer audit and the additional VPhysics/material/shader
code/data checks are unchanged. Its baseline/candidate directories must contain
all the named libraries. If evidence changed, inspect the affected ABI and run
in-game tests before using `compatibility_profiles.py --audited` with reviewed
guard RVAs. `--append` preserves other supported profiles. Never use `--audited`
merely to bypass a failing game. Publish this Lua profile without changing the
native release if its compiled ABI remains valid; changed layouts require native
implementation and a new binary release. New policy takes effect after restart.

## Interfaces and validation

- Native `GetInstallationInfo()` returns module/runtime identities and paths.
- Native `ConfigureCompatibility(policyJson)` accepts schema 1, once per process.
  Changing an active policy returns a restart-required error.
- Native `CheckCompatibility()` returns readiness, pending libraries and issues.
- Client `StartInstallationProbe(coacd)` / `PollInstallationProbe()` provide the
  asynchronous worker result. Native results use the existing JSON-string or
  `nil, error` convention.
- Lua `GetInstallationStatus()`, `FeatureAvailable(feature)` and
  `ServerFeatureAvailable(feature)` expose diagnostics and availability.

Tests: CTest `installation_and_abi_evidence`, `tests/test_installation.py`,
`tests/test_native_installer.py`, and the owned-game scripts
`test-installation-game.py` / `test-installation-failures.py`. Fault injection
only modifies disposable game roots and restores their files. Generated reports
and screenshots live under `validation` and are excluded from release archives.

### Validation on 2026-09-24

- All 22 CTest cases pass, including worker startup/dependency failure, explicit
  child failure and the 10-second timeout. Python and C++ produce identical PE
  evidence for the actual engine DLL.
- 29 policy scenarios, five loaded-identity checks and the installer fixture suite pass. All addon Lua files
  compile after normalizing GLua `continue` for the offline Lua runner.
- Owned vanilla and RTX sessions pass worker self-test, character/prop import,
  18-body ragdoll creation, detailed prop collision, rendering and shadow-status
  checks. Manual recheck preserves the loaded native module.
- Actual-game fault injection passes: a changed engine timestamp/hash retains
  compatibility; damaged CoACD uses simple hulls; missing client DLL retains the
  repair UI; missing server DLL is reported to clients; failed engine evidence
  disables rendering without changing render queue mode.
- A Windows x64 dedicated server and two real clients report clean installation
  status. Existing admin publication, non-admin denial, verified model download
  and download hash checks pass.
- Five pre-existing offline tests fail identically on untouched `7a2c995`:
  library folders, presentation handoff, preview clipping, QoL Lua and Remix
  preview. Their stubs/syntax handling are stale; they are not regressions from
  this change. The other existing offline tests pass.
