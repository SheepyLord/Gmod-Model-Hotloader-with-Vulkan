# Native installation and game compatibility

The first release of the checker required **2.1.0-native.1**. Windows x64 GMod
is the only supported platform. Workshop supplies Lua; the GitHub package
supplies the native files. Normal installation no longer installs a loose addon
that could override subsequent Workshop updates. Developers can opt into that
layout with `install.ps1 -InstallAddon`.

## What is checked

Model Hotloader always tries its native files, and their identity never turns a
feature off. Before `require`, Lua reads the physical `MOD` / `BASE_PATH` files
and compares their sizes and SHA-256 hashes with `native_policy.lua`: the realm
module and the executable-side runtime, and on clients also the worker, the
worker-side runtime and CoACD. The release whose realm module matches is the
installed one, and the other files are compared with it (with the recommended
release when the module matches none). A file that matches another release or
none (a test build from GitHub Actions or a local build, a newer release, a
modified or damaged file, a mixed installation) is a warning that disables
nothing; see [Native files this addon does not
know](#native-files-this-addon-does-not-know). So is a missing or broken
`native_policy.lua`: the files are then read but not compared. Dedicated servers
use the runtime beside `srcds_win64.exe`, or in `bin\win64` when there is none
beside it: the native package puts it there, and `srcds_win64.exe` searches
`bin\win64` after its own folder, as the main branch's `gmod_win64.exe` does.
They do not need the client-side import components. The previous unversioned
installation is recorded as obsolete (`installApi` 0; see Update reminders).

Loading is always tried. Only three things keep the native module from loading,
and with it every feature: no module file for this realm
(`garrysmod/lua/bin/gmcl_mmdhl_win64.dll` or `gmsv_mmdhl_win64.dll`), a game that
is not Windows x64 (no binary exists for it, so nothing is read), and a `require`
that fails (Windows cannot load the module or a DLL it needs: a missing or
damaged runtime, a missing Microsoft Visual C++ x64 Redistributable). The
warnings that explain why (missing or unreadable files, files from different
builds, the platform) then become its problems, beside the loader's own error
when `require` failed (`loader_failed`).

Every Model Hotloader binary carries the ID of the build that made it
(`<commit>-YYYYMMDDTHHMMSSZ`, the `build` of a release record), and Lua reads it
from the realm module and the runtime before loading them. The two share C++
types without a handshake, so from different builds they may fail to load or
crash Garry's Mod. They are still loaded, with the warning `mixed_builds` ("… is
from build …, but … is from build …"). Every warning is printed to the console
before `require`, so a crash while loading leaves its reason in the last lines
Model Hotloader printed to the console (`console.log` with `-condebug`). Files
this addon does not know raise no `mixed_builds` while their builds agree.

After loading, the native module reports its compiled identity, loaded paths,
expected paths and hashes. Both 64-bit branches load the client runtime from
`bin\win64`: the x86-64 branch starts `bin\win64\gmod.exe`, and the main branch
(since 2026-09-17) starts `gmod_win64.exe` in the game folder. Native builds up
to 2.1.0-native.5 expect the runtime beside the executable, so Lua also accepts
the runtime file it checked under the loaded module's game folder. A loaded
module or runtime that is not the file Lua checked (other bytes, another path or
build) only adds a warning (`loaded_mismatch`): Model Hotloader uses what Garry's
Mod loaded.

A contained worker process checks startup, runtime identity and CoACD's exports.
It runs asynchronously with a 10-second deadline, and imports wait for it. The
self-test loads the installed `lib_coacd.dll` in the worker process, never in
the game, whether this addon knows the file or not: one that does not load or
lacks the exports fails as `dependency_failed`, and requested detailed collision
falls back to a simple hull. A worker that does not start or answer
(`worker_failed`) disables imports while cached models remain usable. A worker
and runtime copy from different builds, or not from the installed release, only
add a warning. A module from before the self-test runs imports unchecked.

External Models and `mmdhl_open` remain available without the DLL. A dismissible
notice appears once per Lua session, for warnings too; the persistent banner
provides Download, Copy diagnostics, Recheck and Dismiss. Server failures are
explicitly attributed to the server administrator. The minimal status protocol
is registered before native loading, including safe handling of client
initialization requests.

Clients never refuse a request because of the status a server reported: the
server checks its own installation when it acts. A spawn the server cannot make
fails with the server's problem ("Server: Native runtime: …"), also from a
server whose native module did not load, instead of waiting for the client's
timeout.

`mmdhl_check_installation` (**Recheck** in the banner) reads the files again but
never reloads DLLs: its warnings describe the files on disk now, while the
worker self-test's results and an incompatible game build carry over (a
self-test failure that a missing or unreadable file explained gives way to the
restart line once that file is back). Repairs that change an unavailable
component require a full game restart. Native and game-library checks are
cached, including failures; render/simulation hooks do not hash files.
Diagnostics remain local unless the user copies them.

## Installed into the wrong folder

Players unpack the package into the wrong folder ([issue #6](https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/issues/6)):
the contents of its `GarrysMod` folder belong in the game folder itself (the one
that contains `bin` and `garrysmod`), but many copy them into `garrysmod`, as most
mods are installed. When the realm module or the runtime is missing where the game
loads it, `EvaluateInstallation` looks for them (through `BASE_PATH`) in the places
this happens: `garrysmod/`, `garrysmod/GarrysMod/`, `GarrysMod/`,
`garrysmod/addons/`, `garrysmod/addons/GarrysMod/`, `garrysmod/lua/bin/GarrysMod/`,
and any `Model-Hotloader-*` folder (an unpacked release left where it was unpacked)
in the game folder, `garrysmod` or `garrysmod/addons`. A file found there adds the
first issue, `misplaced_package` (warning, cause, `detail` = the path found). When
nothing loads it becomes the problem every player reads first: in External Models,
in the banner (in place of the download instruction, because downloading again
would not help) and in the console ("Model Hotloader is installed in the wrong
folder: its files are at garrysmod\garrysmod\lua\bin\... Copy the contents of
the package's GarrysMod folder into the Garry's Mod folder itself ...").

A window says it too, once per game start, in place of the corner notice: where the
files were found, where they belong, the files that must exist afterwards
(`bin\win64\mmdhl_runtime_win64.dll`, `garrysmod\lua\bin\gmcl_mmdhl_win64.dll`),
**Installation guide** (the README's install steps), **Don't show this again** and
**Close**. Later maps of the same run get the corner notice (`install.notice_misplaced`),
which opens the window. **Don't show this again** stores the path found in
`data/mmd_hotloader/misplaced_dismissed.txt`, and then neither the window nor the
notice comes back for that copy (the banner and External Models still say it);
`mmdhl_installation_folder` opens the window by hand. A copy left in a wrong folder
beside a working installation says nothing.

## Update reminders

The addon never refuses a native release, old or new: the Workshop Lua loads
every native module it finds (What is checked). In `native_policy.lua`,
`recommended` is the release this Lua was built and tested with, `approved` the
releases it runs without a word, and `releases` every recorded release,
identified by the size and SHA-256 of its realm module.

- A recorded release **older** than `recommended` runs. Approved, it has no issue
  at all (`publish-native-release.py` requires that of every approved release);
  no longer approved, it gets one warning ("outdated and no longer approved …
  runs anyway") that disables nothing and never counts as pending: the reminder
  speaks for it. Either way the installation status carries `update`:
  `{installed, recommended, url, altUrl, approved, advisory}`, with the
  recommended release's links.
- Releases are ordered by the UTC time that ends their build ID
  (`<commit>-YYYYMMDDTHHMMSSZ`) when both records have one, else by label: the
  numbers first, a pre-release (`2.3.0-rc.1`, `2.1.0-native.12`) before its
  release, `+metadata` ignored.
- A recorded release **newer** than `recommended` that is not approved runs with a
  warning (`unapproved_release`), like files this addon does not know (below).
- Optional `"revoked": {"<label>": "install.advisory.<id>"}` marks a release with a
  known problem. It still runs; the update window and banner line add that phrase
  (or a general one when the player's catalogue lacks it), and the advisory
  reminds again even where that release was skipped. Keep the policy ASCII; the
  release scripts keep keys they do not know. A new phrase is an ordinary catalogue
  key: add `install.advisory.<id>` to all seven catalogues and list it in an
  `-- i18n-keys: install.advisory.<id>` comment in `installation_ui.lua`, or
  `check-i18n.py` warns that no Lua file uses it.
- Modules this Lua cannot check run too, with a warning that they may misbehave
  or crash: releases without installation verification (`installApi` 0) and a
  module without `GetInstallationInfo`/`ConfigureCompatibility`/`CheckCompatibility`
  ("too old for the addon to check"), and a loaded module with another interface
  (`api`, `installApi` or platform; the loaded-module warning). No Workshop
  profile checks them against game updates: they rely on their own guards (a
  module without `CheckCompatibility` keeps rendering or physics on, with the
  warning `game_unchecked`, and one without `StartInstallationProbe` imports
  without the worker self-test). Those older than `recommended`, or too old to say
  which release they are, get a **needed update** (`update.required`): the same
  window and links, titled "Model Hotloader — update needed", which cannot be
  skipped. A loaded module of the recommended release or newer with another
  interface keeps only its warning: there is nothing newer to download.

What players see:

- **The update window**, a few seconds after joining a game, once per game run
  (not again on the next map): the installed and the latest release, **Download
  from GitHub** (the repository's releases page only), **Alternative download**
  (the mirror, a plain https address only), **Remind me later** (closes; the next
  game run reminds again), **Skip this version** and **Don't remind me again**.
  Skip this version is stored in `data/mmd_hotloader/native_update.json`
  (`{"schema":1,"skipped":"<label>","reminded":<time>}`); a newer recommended
  release, or an advisory, reminds again. Don't remind me again sets the archived
  client ConVar `mmdhl_native_update_reminder` to 0, for good; the checkbox in the
  installation window and under Utilities > User > Character Models turns it back
  on. A needed update cannot be skipped. It opens once per game run in place of
  the notice (later maps of that run get the notice), and **Dismiss** in the
  banner silences both until the warnings change.
- It never opens beside the notice. While a problem the download fixes is pending
  (the module did not load because files are missing, unreadable or from
  different builds), the window stays closed: the notice already says to download
  (the banner and the installation window still show the update). Other problems
  and warnings, such as a game build no profile describes, get their notice when
  the window closes.
- **The banners** of External Models and the library window show one line, "Native
  update available: …", while the reminder is due. It is not a problem and counts
  as nothing pending. **Dismiss** there hides the problems and warnings first;
  with none left, it hides the line by skipping this version (its tooltip says
  so).
- **The installation window** always shows the update, "(update available)" after
  the recommended release, and the reminder checkbox. Administrators of a
  dedicated server, and a listen-server host whose server files differ from the
  game's, also see the server's update there; the server console prints one line.
- `mmdhl_native_update` opens the window (the installation window when there is no
  update).

For features of the addon: `mmdhl.NativeReleaseAtLeast(label)` (both realms) says
whether this realm's native module is that release or newer, in the order above:
the release its files match, else (files this addon does not know) the loaded
module's own label and build. `mmdhl.ShowNativeUpdateNeeded(feature, release)` (client)
shows a small dialog, one per feature at a time: the feature (an already localized
name) needs `release` or later, everything else keeps working, and **Download
update…** opens the update window (the installation window while no newer release
is known). Both exist even when the native module did not load. Every Lua use of a
native function added after the oldest release this Lua supports is feature
detected (`if mmdhl.native.NewFunction then … end`), with this dialog (or a
notification) when it is missing; additive native changes keep `ApiVersion` 1.

## Native files this addon does not know

A test build (from the "Build drop-in package" Action or a local build), a
release newer than this Workshop version records, modified or damaged files and
mixed installations all load. What sets them apart from a recorded, approved
release is a warning that disables nothing:

- `damaged_or_unrecognized`: a file matches no recorded release;
- `mixed_installation`: a file matches another release than the rest of the
  installation, or (found by the self-test) the worker and its runtime copy come
  from different builds, or not from the installed release;
- `unapproved_release`: the realm module is a recorded release that is neither
  approved nor older than `recommended`;
- `mixed_builds`: the realm module and the runtime carry different build IDs and
  may crash the game (What is checked);
- `loaded_mismatch`: Garry's Mod loaded another module or runtime than the file
  that was checked.

The banners of External Models and the library window show them as one line
above the download buttons ("The installed binary module is not a release
approved for this version of the addon … Model Hotloader runs it anyway"). The
banner's tooltip, Copy diagnostics and the console list each one. They raise the
notice once per Lua session like other warnings. **Dismiss** hides them until
the files change: it keeps each warning with what identifies it (the file's size
and SHA-256, the release, the build IDs or the loaded module's SHA-256), so
another build brings the warning back.

Such files have no release record: the installation status leaves `installed`
empty, and Lua identifies them by their bytes instead (the loaded module and
runtime are compared with the files it read, the worker's runtime with its copy
in `garrysmod/lua/bin`). The banner's version line shows the loaded module's own
label and build ("Native release: <label> (<build>)"), and
`mmdhl.NativeReleaseAtLeast` uses the same. When that label is older than
`recommended`, the ordinary update reminder offers the download; it can be
skipped, unlike a needed update.

A server's warnings of this kind are for its administrator: the server console
prints them, and the installation window (Utilities → User → Character Models →
**Model Hotloader — installation**, or `mmdhl_installation`) lists them in its
tooltip and in Copy diagnostics ("Server: …"). They never show in a player's
banner or notice and do not count for Dismiss.

There is nothing to accept any more: **Use anyway…**, **Stop using unverified
files**, `data/mmd_hotloader/unverified_native.json` and the server console
commands `mmdhl_accept_unverified_native` and `mmdhl_revoke_unverified_native`
are gone, and an `unverified_native.json` an earlier version wrote is ignored.
Features are turned off by failures, never by what the files are: no module
file, another platform or a module Windows cannot load (What is checked), a
worker or CoACD that fails the self-test, and a game build that fails its checks
(Game updates).

## Game updates

The installer writes custom `mmdhl` module/runtime/worker names and CoACD in the
Lua module directory; it does not replace vanilla DLLs. Normal Steam updates
should retain these separate files. File survival and ABI compatibility are
independent: retaining an importer DLL does not prove it can use a changed engine.

Our own files are compared by hash (above), and what they are never turns a
feature off. Game libraries change with every Garry's Mod update.
`compatibility_policy.lua` describes the builds audited for the compiled
`source-win64-v1` ABI family: executable and read-only PE sections (DIR64
image-base relocations normalized), with the RVAs of the existing guards. A
library that matches a profile is reported as `tested` or `abi-evidence`; any
other build is `unverified`.

An unverified build still runs: every feature stays on, and the player gets one
warning ("This Garry's Mod build has not been tested with this native release"),
which disables nothing and which **Dismiss** hides until the builds change. A real
failure is a warning too: a missing interface, a slot another module replaced or a
guard RVA that does not match (`game_incompatible`), a check that fails outright
(`game_check_failed`), or a game library still not loaded some seconds after the
map started (`game_not_ready`). Lua leaves rendering (clients) and physics
(servers) on and tries: the warning reads "Game check: ... Model Hotloader runs
anyway". This is safe because the binary does not rely on that verdict. Its
renderer and its VPhysics bridge run the same library, interface, slot and class
checks before every engine call they make, keep a failure for the session
(`ValidationOnce`) and refuse that call cleanly instead of calling a vtable slot
that holds another function (which hangs or crashes the game, below). The client
then falls back to Source's own rendering (`CheckRenderer` reports the failure), and
a failed scene capture on a server turns secondary contacts off once (it is not
retried every tick). Only a library the game has not loaded yet keeps the feature
off for the moment, because the binary would keep its absence for the session; the
map retries for some seconds, then tries anyway with `game_not_ready`.

A `compatibility_policy.lua` newer than the installed binary must not stop it
either. The binary validates the whole policy before it takes it, once per process,
and rejects a library it does not know, or a profile it cannot validate: one
without a guard that binary requires (or with an RVA outside the image), another
`sha256` or evidence format. Guard names it does not use are ignored. Lua therefore
offers the policy whole; when it is rejected, without one library at a time (in
policy order); then without libraries. The libraries left out run as unverified
game builds behind the binary's own interface, slot and class checks (releases
before 2.1.0-native.6 turn the affected engine features off instead), and the
player gets one warning naming them, which disables nothing. While an update is
known it is not counted as a problem either: the update reminder speaks for it. The
order is fixed, so every later map of that game finds the same policy. Another ABI
family is never forced on a binary: the module still loads, with a warning
(`policy_invalid`, component `compatibility`), and its own checks decide. Releases
from 2.1.0-native.5 accept no game library without a policy they took, so their
own checks then refuse every engine call (`game_incompatible`: "Compatibility policy
not configured", a warning). Keep new profiles additive anyway, so approved releases take
them whole.

Game updates can still change what the compiled modules call. The default branch's
64-bit build of 2026-09-17 keeps the `VMaterialSystem080` and `VPhysics031`
version strings but lacks the four newer `IAppSystem` methods of the x86-64 build
these modules are compiled against, so every later method of those two interfaces
sits four slots lower (`CMaterialSystem`'s vtable has 147 entries instead of 151,
the `VPhysics031` object's 13 instead of 17). 2.1.0-native.6 called the wrong
functions there: `GetRenderContext` landed on a method that waits for a render job,
and the model preview hung the game. From 2.1.0-native.7 the modules measure those
vtables at startup and call `IMaterialSystem`/`IPhysics` methods at the running
layout's slots: the compiled slot of each method, found by calling it on a probe
object, less the measured shift (reported as `layout` in Copy diagnostics). The same
build's `vphysics.dll` also lacks `IPhysicsCollision::VPhysicsKeyParserCreate(vcollide_t*)`
and six trailing methods (52 slots instead of 59) and `IPhysicsObject::SetSphereRadius`;
2.1.0-native.7 called `DestroyQueryModel` where it meant `CreateQueryModel`, and
spawning a model corrupted the heap. From 2.1.0-native.8 the physics bridge
recognizes that layout (13 and 52 slots) and calls collision methods after slot 38
and physics-object methods after slot 43 one slot lower. Any other change in the
middle of an interface cannot be recognized this way; it needs a native update.

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
   in the Lua policy decide which releases run without a warning and which one
   the update reminder offers; removing an ID from `approved` gives that release a
   warning (it still runs). Do not remove old records needed to diagnose obsolete
   installs.
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
   Lua and its policy. Lua-only releases retain native approvals. The scripts
   above do not upload anything. For a release built by the "Build drop-in
   package" Action, `python scripts/publish-native-release.py publish --run <id>
   --notes whats-new.md` checks the run's packages against their record, appends
   it to `native_policy.lua` as the recommended release, runs the policy and Lua
   checks and builds `addon.gma` from the committed addon folder; only then does
   it create the GitHub release (zips with fixed times, so a rerun makes the same
   bytes) and push main (`--dry-run` only checks, builds and prints). It never
   replaces or deletes an existing release, tag or record: an existing tag must
   name the run's commit, and a release an interrupted publish left (a draft or
   one without a zip) is completed only when it holds nothing else. A rerun after
   a failed push pushes the publication commit. `supersede <label> --note
   note.md` puts a note at the top of an older release. Uploading `addon.gma` to
   the Workshop stays manual.

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
- Lua `GetInstallationStatus()` and `FeatureAvailable(feature)` expose
  diagnostics and availability; `ServerIssue(feature,message)` words a server
  problem in a reply to a player.
- Each entry of the status's `issues` has `code`, `component`, `message` and
  `feature`. A problem keeps its feature off; an entry with `warning` disables
  nothing and is never the reason `FeatureAvailable` gives. `identity` marks a
  warning about files this addon does not know (one banner line), `detail` what
  tells it from an earlier one (for Dismiss), `cause` a warning that becomes the
  problem when loading (or, for its feature, the worker self-test) fails, and
  `probe` a result of the worker self-test, which Recheck carries over. The
  status sets `installed` only when the realm module matches a recorded release;
  `loaded` is what the loaded module reports about itself.
- Lua `NativeReleaseAtLeast(label)`, and on the client `ShowNativeUpdateNeeded(feature,
  release)`, `NativeUpdate()`, `NativeUpdateDue()`, `OpenNativeUpdate()`,
  `SkipNativeUpdate()` and `StopNativeUpdateReminders()` (Update reminders).

Tests: CTest `installation_and_abi_evidence`; `tests/test_installation.py` (the
check without a DLL: the evaluator's warnings, loading always tried, the
loaded-identity check, Recheck, `publish-native-release.py`'s evaluate(), and
that nothing of accepting unverified files is left);
`tests/test_installation_banner.py` (the banner, its notice and Dismiss);
`tests/test_installation_game_builds.py` (loading against a simulated game: game
builds and loaded files);
`tests/test_server_installation_requests.py` (requests to a server whose module
did not load); `tests/test_native_update_session.py` (older, newer and unknown
modules load, with the update they need; the compatibility fallback;
`publish-native-release.py` with a newer recommended release);
`tests/test_native_update_notice.py` (the reminder window, banner and dialog);
`tests/test_file_access_worker_gate.py` (file access and the worker self-test);
`tests/test_native_installer.py`; and the owned-game scripts
`test-installation-game.py` (a clean session: no problems, the same warnings after
Recheck) and `test-installation-failures.py` (a damaged CoACD, missing server and
client modules, a client module the policy does not know, a runtime from another
build, a game build no profile describes). Fault injection only modifies
disposable game roots and restores their files. Generated reports and
screenshots live under `validation` and are excluded from release archives.

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
