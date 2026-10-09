# File access for other addons (2.3.0)

Garry's Mod Lua can only read the game's own folders. Model Hotloader lets other
client addons ask to read a file or a folder anywhere on the player's computer
(a preset exported from another program, a folder of images, a JSON settings
file). The player decides every time in a Windows dialog, and the addon only
gets what the player chose or allowed.

This page is for players (what the dialogs mean) and for addon authors (the Lua
API and its limits).

## For players

* An addon asks in one of two ways. It opens a **file picker** titled with the
  addon's name ("“My HUD” asks you to choose a file to read"), or it names an
  exact file or folder and a **request window** asks **Allow once**, **Always
  allow this addon here** or **Deny**. Deny is the default button, and the
  Allow buttons wake up a moment after the window appears, so a key or click
  meant for the game cannot answer it. The picker's **Allow reading** button
  does the same, so a stray Enter cannot choose the folder the picker opens
  in. The picker starts in Documents and remembers its own last folder, apart
  from the model import picker.
* The windows are Windows dialogs shown by Model Hotloader's worker, not game
  menus. In a full-screen game they open behind it: a notice in the top right
  corner says so; press **Alt+Tab** to answer.
* **Allow once** lasts until the map changes. **Always allow this addon here**
  remembers the folder (and its subfolders) for that addon name. It is not
  offered for whole drives, your user folder, Desktop, Documents, Downloads,
  Pictures, Music, Videos, Saved Games, AppData (all of it: Roaming, Local and
  LocalLow), OneDrive, Public or Program Files, nor for a folder that holds one
  of them.
* An addon that you refuse three times in a request window (**Deny**, or
  closing the window) is refused for the rest of the map without asking;
  allowing one of its requests clears its count. Closing a file picker does not
  count against the addon, but after ten refusals and closed pickers in one map
  no addon can ask until the map changes.
* If Model Hotloader cannot save a choice (for example, its folder in
  `%LOCALAPPDATA%` is read-only or held by antivirus software), a notice says
  so; the choice still applies until the map changes.
* **Utilities → User → Character Models → File access for other addons…**
  lists the remembered folders (your user folder is shown as `~`), revokes them
  one by one or all at once, and has the switch **Let addons ask to read
  files**. Turning it off takes effect at once, also for reads an addon
  already started (they deliver nothing), and revoking a folder ends the
  reads of files in it the same way (also of files allowed through a smaller
  folder it replaced); turning it on asks for confirmation in a Windows
  dialog. Two copies of the game running at once (another install,
  `-multirun`) share the remembered folders and the switch: what you change
  in one applies in the other too, also to the reads running there, and
  neither brings back what the other revoked or turned off.
* Only in **single player** and on a **server you host**. On anyone else's
  server every client script comes from that server, so Model Hotloader refuses
  all requests there and does not even show your remembered folders.
* Model imports follow the same rule there: a script from another server can
  import, reimport or read the readmes beside only models you chose in Model
  Hotloader's own file picker, so it cannot use the importer to learn whether
  other files exist or what they are.
* Whatever an addon reads, it can use as it likes, including sending it to a
  server or a website. Allow only addons you trust, and only what they need.

## What Model Hotloader guarantees, and what it does not

The decision is made in the native module, where Lua cannot reach it:

* The dialogs run in a separate process (`mmdhl_worker.exe`), and only while
  the installation check allows that worker, as for model imports (a worker
  from another release, or one that failed its self-test, shows nothing, and
  requests wait while the self-test still runs; turning file access off, and
  a request a remembered folder answers, need no dialog). Lua can
  click Derma buttons and override hooks, but not windows of another process.
  Their sentences are compiled into the module in the player's language; only
  the addon's name, its reported script, its stated purpose and a short title
  come from Lua. They are shown in quotes or marked as reported and not verified,
  cleaned of control and text-direction characters, cut to length, and their
  own quotation marks (`"`, `“ ”`, `« »`, `「 」` and look-alikes) become
  apostrophes, so the text cannot close the quotes and add sentences that look
  like Model Hotloader's.
* Requests and answers travel through a folder the module creates in the
  user's temporary folder with a random name, and remembered folders are stored
  in `%LOCALAPPDATA%\ModelHotloader\file-access.json`. Lua can write anything
  under `garrysmod/data`, so nothing there is trusted. A damaged store means
  "nothing remembered".
* A request is refused unless this process also runs the server (single player
  or listen host); the module counts the realms that loaded it.
* Read only. Every read and listing opens the file again and checks where it
  really is: links and junctions are never followed, the final path (as Windows
  resolves it) must stay inside what the player allowed, and hidden or system
  files are read only when the addon asks for them.
* Paths from Lua must be full local paths (`C:\...`). Network paths
  (`\\computer\share`, `\\?\`, `\\.\`, URLs), network drive letters, relative
  and drive-relative paths, `..` parts, alternate data streams and device names
  (`CON`, `COM1`...) are refused before anything is opened, so a script can
  never make Windows sign in to another computer. A file on a mapped network
  drive can still be chosen in the picker.
* Some places are never readable, even with consent: the Windows folder,
  Windows credential and key stores, browser profiles (Chrome, Edge, Brave,
  Chromium, Vivaldi, Yandex, Opera, Firefox, Thunderbird), `.ssh`, `.gnupg`,
  `.aws`, `.azure`, `.kube`, `.docker` and `.config\gh` in the user folder,
  registry hives (`NTUSER.DAT`), Steam's `config` folder and `ssfn` files,
  Discord's local storage, Telegram's session (`tdata`), Signal, FileZilla's
  saved sites, the Exodus, Electrum, Ethereum and Bitcoin wallets,
  `.git-credentials`, `.npmrc`, `.netrc`, `.pypirc` and `wallet.dat` files
  anywhere, the game's `cfg` folder and the remembered-folders store itself.
  This list is a second line of defence, not a promise that everything secret
  on the computer is covered: allow only what an addon needs.
* An addon never receives full paths: items carry an opaque handle, the file
  name and a display path with the user folder shown as `~`. Failures carry
  fixed English sentences, never a system message that could name a folder.
* Whether a named file or folder exists, whether its drive letter exists or is
  a network drive, and whether it is on the never-readable list are only
  revealed after the player answered: such a request still opens the request
  window (which says what is wrong and offers only **Close**), and nothing is
  opened for a missing or network drive or a never-readable place. So an addon
  cannot probe the disk, or guess the Windows user name from the never-readable
  list, silently. Only path text that is malformed (relative, `..`, network
  forms, device names) is refused at once. Inside a remembered folder, requests
  are answered without a window.

Not guaranteed:

* **Addon names are not verified.** A script reports its own name and file;
  any client addon can claim another's name. Remembered folders are keyed by
  that name, so they are a convenience between your own addons, not a wall
  between them. In single player every client script is one of your own
  subscriptions.
* Any client addon can call the native functions directly
  (`mmdhl_native.FileAccess*`), skipping the Lua hooks below. That gives it
  nothing more: the native rules and dialogs still apply.
* `MMDHL.RequestUserFile` is an ordinary GMod hook: any client addon can add a
  hook of its own that returns `true` first and answers another addon's request
  with made-up items or data. Use `mmdhl.FileAccess` directly when it exists,
  and the hook only as the fallback (the example below does both).
* Once an addon has read something, Model Hotloader cannot control what it does
  with it.

## For addon authors

### Without depending on Model Hotloader: the hook

```lua
-- Ask the player for a JSON preset; works when Model Hotloader 2.3.0 is installed.
local opts = {
    addon = 'My HUD',                       -- your addon's name (required, up to 64 characters)
    purpose = 'Import a HUD layout',        -- shown in quotes (up to 120 characters)
    filters = {{'HUD layouts', '*.json'}},  -- up to 8, patterns like *.json;*.txt
}
local function chosen(ok, items, code)
    if not ok then print('No layout: ' .. items) return end  -- items is a localized reason here
    local file = items[1]
    file:Read({mode = 'text'}, function(read, text, info)
        file:Release()
        if not read then print('Cannot read: ' .. text) return end
        local layout = util.JSONToTable(text)
        if layout then MyHUD.Apply(layout) end
    end)
end
-- The API when it is there (no other addon can answer in between), the hook otherwise.
local handled = true
if mmdhl and mmdhl.FileAccess then mmdhl.FileAccess.Pick(opts, chosen)
else handled = hook.Run('MMDHL.RequestUserFile', opts, chosen) end
if not handled then
    -- Model Hotloader is not installed (or its Lua did not load): offer another way.
end
```

`hook.Run('MMDHL.RequestUserFile', opts, callback)` returns `true` when Model
Hotloader took the request. With `opts.path` it asks for that exact path
(`RequestPath`), otherwise it opens the picker (`Pick`). It still returns `true`
when file access is unavailable (old binary module, someone else's server,
turned off); the callback then explains why.

### The API: `mmdhl.FileAccess` (client only)

`mmdhl.FileAccess.Version` is `1`. Every callback runs exactly once, from a
`Think` poll a frame or more later, never during the call. Failures call back
with `(false, message, code)`: `message` is localized for the player, `code` is
stable (see below). A callback that raises an error is reported and does not
affect others.

| Function | |
|---|---|
| `IsAvailable()` | `true`, or `false, message, code` (`needs_update`, `unavailable_remote`, `disabled`, `worker_missing`, `worker_unavailable`). |
| `Pick(opts, cb)` | Opens the picker. `opts`: `addon` (required), `title`, `purpose`, `filters` (`{{label, '*.ext;*.ext'}, ...}`), `multiple`, `folder` (pick folders instead). `cb(true, items, refused)`; `refused` lists chosen names that cannot be read, with their codes. |
| `RequestPath(path, opts, cb)` | Asks for an exact absolute path. `opts`: `addon` (required), `folder` (the path is a folder), `purpose`. `cb(true, items)`. |
| `Read(item, opts, cb)` | `opts`: `mode` `'binary'` (default) or `'text'`, `offset`, `maxBytes` (default 1 MiB, at most 16 MiB), `relative` (a path inside a folder item, with `/` or `\`), `hidden`. `cb(true, data, info)`; `info` has `name`, `size`, `offset`, `read`, `eof`, `limited` (the file continues past 256 MiB) and, in text mode, `encoding`. |
| `List(item, opts, cb)` | One level of a folder item. `opts`: `relative`, `hidden`. `cb(true, entries, {truncated})`; each entry has `name`, `folder`, `size` (files) and `modified` (Unix time). At most 4000 entries; links are left out. |
| `Release(item)` | Gives the item back. Items also end when the map changes. |

Items are tables `{handle, name, size, folder, remembered, displayPath}` with
methods `item:Read(opts, cb)`, `item:List(opts, cb)` and `item:Release()`.
`Read` and `List` also accept the handle string.

Limits: binary reads return up to 16 MiB per call and can reach the first
256 MiB of a file (read larger files in chunks with `offset`). Text mode reads
the whole file, so the file must be no larger than `maxBytes`; it is decoded
from UTF-8, UTF-16, Shift-JIS, GBK, Big5 or UHC to UTF-8 (`info.encoding`),
with line ends as `\n`, control characters other than tab removed and surrounding
blank space trimmed. Use binary mode when the exact bytes matter. At most one
dialog is open at a time and four requests wait behind it (`busy`); at most
eight reads and listings run at once, and a result that nobody collects within
30 seconds is dropped (the Lua API collects every result on the next frame).

Codes: `denied` (the player refused, closed the window or chose nothing; also
every request whose window could only say what is wrong), `auto_denied` (this
addon was refused three times), `auto_denied_session` (ten refusals and closed
pickers in this map, from any addons), `busy`, `not_found`, `network`,
`remote_drive`, `invalid_path` (also `relative`, `parent`, `stream`,
`device`), `denied_location`, `link`, `outside`, `hidden`, `too_large`,
`offset_too_large`, `not_a_file`, `not_a_folder`, `released`, `unreadable`,
`dialog_failed`, `too_many_items`, `invalid_options`, `denied_by_hook`,
`needs_update`, `unavailable_remote`, `disabled` (also for a read or listing
the player turned file access off during), `worker_missing`,
`worker_unavailable` (the installation check does not allow the worker that
shows the dialogs; the message carries its reason. `RequestPath` still gets a
path inside a folder the player always allowed for the addon, which needs no
window. While the check still tests the worker, in the first seconds after
Model Hotloader loads, `IsAvailable()` says `true` and requests wait for its
verdict). A read or listing of a folder the player revoked meanwhile ends
with `released`.

### Hooks

* `MMDHL.RequestUserFile(opts, callback)`: the entry above. Another addon's
  hook could answer first (see "Not guaranteed").
* `MMDHLCanAccessUserFile(addonName, request)`: return `false` to refuse a
  request before any dialog opens (a privacy addon, a single player rule).
  `request` has `kind` (`'pick'` or `'path'`), `addon`, `script`, `path`,
  `folder`, `multiple`, `purpose` and `title`; it is a copy. Returning anything
  else lets the request continue to the player: this hook can never grant.
  Like any GMod hook, a hook that returns a value stops the others.
* `MMDHL.FileAccessReady(api)`: runs once after Model Hotloader's Lua loaded.
  `mmdhl.FileAccess` exists from then on (with an old binary module its
  `IsAvailable()` says so).
* `MMDHL.FileAccessChanged()`: a folder was remembered or revoked, the switch
  changed, or the installation check's verdict on the worker changed what
  `IsAvailable()` says (its self-test failed, or the player repaired the
  files): ask `IsAvailable()` again. After a request it runs from the `Think`
  poll after that request's callback; a listener that raises an error is
  reported and changes nothing else.

## Technical reference

* Native: `native/file_access.{hpp,cpp}` in the runtime (path rules, the
  never-readable list, the store, reads, listings, requests and the dialog
  queue), `native/file_access_dialog.cpp` in the worker (`--fa-pick` and
  `--fa-consent <private folder>`; the dialog text in 7 languages), and the
  client module bindings. The worker carries a manifest
  (`native/worker.manifest`) for Common Controls 6 (the request window is a
  `TaskDialogIndirect`) and DPI awareness. Network paths are recognised by one
  parser, `props::networkPath` (`native/props/network_path.hpp`).
* Client module functions (JSON strings in and out; a refusal returns `nil`,
  the English reason and its code): `FileAccessInfo()`,
  `FileAccessPick(json)`, `FileAccessRequest(json)` (both return an id;
  `"noDialog":true`, which the installation check's guard adds while it does
  not allow the worker, lets only a remembered folder answer and refuses
  anything else as `worker_unavailable`),
  `FileAccessPoll(id)` (`pending` with `dialog` and `position`, `granted` with
  `items`, `denied`, `failed`), `FileAccessRead(handle, json)` /
  `FileAccessPollRead(id)` (`false` while running, then `data, infoJson`),
  `FileAccessList(handle, json)` / `FileAccessPollList(id)`,
  `FileAccessRelease(handle)`, `FileAccessCancel(id)`, `FileAccessGrants()`,
  `FileAccessRevoke(id or "all")` (`false` when the change applies but could
  not be saved) and `FileAccessSetEnabled(bool, language)`
  (`{"enabled":false}`, `{"enabled":true}` or `{"request":id}` for the
  confirmation dialog). A `granted` poll result or a `SetEnabled` result with
  `"notSaved":true` is a choice that applies until the map changes but could
  not be saved. The server module has none of them.
* The same release closes older ways for Lua to touch other computers or read
  beside any path: `BeginImport`, `Reload`, `PropReload` and the worker's
  `--request` refuse network sources before anything opens them (mapped drive
  letters still work). The game passes its cache folder to the worker's
  `--request` on the command line: `request.json` sits in `data/`, where any
  script can rewrite it before the worker reads it, so its `cache` field is
  read only when a development tool runs the worker without that argument,
  and a network path there is refused. `InspectModelNotes` reads only beside an existing local
  model file (`.pmx`, `.pmd`, `.vrm`, `.fbx`, `.glb`, `.gltf`, `.dae`, `.obj`,
  `.blend`), and the Workshop package scan ignores network paths.
* Reads and listings run on threads of their own that hold what they need and
  a reference to the runtime library; turning file access off, revoking a
  folder or closing the module (every map change) tells them to stop and
  cancels the file operation they wait in, and nothing waits for them, so a
  network share that stopped answering cannot hold the game. The grants
  store is changed under a named mutex shared by every game process: each
  change is applied to the newest file (a change that could not be saved is
  applied again with the next one), and requests, reads, every poll and the
  management window read the file again, so what another process turned off
  or revoked applies here at once: from the addon's next poll on, nothing it
  ended is delivered and no dialog it ended stays open. An item a remembered
  folder answered lasts while a remembered folder of its addon covers it, so
  revoking the larger folder that replaced a smaller one ends both.
* The picker's arming is `native/file_access_picker.hpp`: the picker is never
  shown without it (it fails as `dialog_failed` instead).
* Tests: `tests/file_access_tests.cpp` (CTest `file_access`, also the picked
  files that imports on another server are limited to, shared by two games
  running at once, the private job folders, and the functions the module's
  `BeginImport`, `Reload`, `PropReload`, `Browse`, `PollJob` and
  `InspectModelNotes` go through (`native/picked_models.hpp`); with the test-only
  worker `tests/file_access_worker.cpp`, which answers from an environment
  variable and is never packaged; the shipped worker has no such path. It
  checks the picker's arming without a window; with `MMDHL_FA_UI_TESTS=1` it
  also opens the real folder picker on the desktop and checks that an OK sent
  at once chooses nothing), `tests/test_file_access.py` and
  `tests/test_file_access_worker_gate.py`.
