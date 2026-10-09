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
  meant for the game cannot answer it. The picker starts in Documents and
  remembers its own last folder, apart from the model import picker.
* The windows are Windows dialogs shown by Model Hotloader's worker, not game
  menus. In a full-screen game they open behind it: a notice in the top right
  corner says so; press **Alt+Tab** to answer.
* **Allow once** lasts until the map changes. **Always allow this addon here**
  remembers the folder (and its subfolders) for that addon name. It is not
  offered for whole drives, your user folder, Desktop, Documents, Downloads,
  AppData or Program Files.
* An addon that you refuse three times is refused for the rest of the map
  without asking; after ten refusals in one map nobody can ask.
* **Utilities → User → Character Models → File access for other addons…**
  lists the remembered folders (your user folder is shown as `~`), revokes them
  one by one or all at once, and has the switch **Let addons ask to read
  files**. Turning it off takes effect at once; turning it on asks for
  confirmation in a Windows dialog.
* Only in **single player** and on a **server you host**. On anyone else's
  server every client script comes from that server, so Model Hotloader refuses
  all requests there and does not even show your remembered folders.
* Whatever an addon reads, it can use as it likes, including sending it to a
  server or a website. Allow only addons you trust, and only what they need.

## What Model Hotloader guarantees, and what it does not

The decision is made in the native module, where Lua cannot reach it:

* The dialogs run in a separate process (`mmdhl_worker.exe`). Lua can click
  Derma buttons and override hooks, but not windows of another process. Their
  sentences are compiled into the module in the player's language; only the
  addon's name, its reported script, its stated purpose and a short title come
  from Lua, shown in quotes, cleaned of control and text-direction characters
  and cut to length.
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
  `.aws`, `.azure`, `.kube` and `.docker` in the user folder, registry hives
  (`NTUSER.DAT`), Steam's `config` folder and `ssfn` files, Discord's local
  storage, the game's `cfg` folder and the remembered-folders store itself.
* An addon never receives full paths: items carry an opaque handle, the file
  name and a display path with the user folder shown as `~`.
* Whether a named path exists is only revealed after the player answered: a
  missing file still opens the request window (which says so), so an addon
  cannot probe the disk silently. Inside a remembered folder, requests are
  answered without a window.

Not guaranteed:

* **Addon names are not verified.** A script reports its own name and file;
  any client addon can claim another's name. Remembered folders are keyed by
  that name, so they are a convenience between your own addons, not a wall
  between them. In single player every client script is one of your own
  subscriptions.
* Any client addon can call the native functions directly
  (`mmdhl_native.FileAccess*`), skipping the Lua hooks below. That gives it
  nothing more: the native rules and dialogs still apply.
* Once an addon has read something, Model Hotloader cannot control what it does
  with it.

## For addon authors

### Without depending on Model Hotloader: the hook

```lua
-- Ask the player for a JSON preset; works when Model Hotloader 2.3.0 is installed.
local handled = hook.Run('MMDHL.RequestUserFile', {
    addon = 'My HUD',                       -- your addon's name (required, up to 64 characters)
    purpose = 'Import a HUD layout',        -- shown in quotes (up to 120 characters)
    filters = {{'HUD layouts', '*.json'}},  -- up to 8, patterns like *.json;*.txt
}, function(ok, items, code)
    if not ok then print('No layout: ' .. items) return end  -- items is a localized reason here
    local file = items[1]
    file:Read({mode = 'text'}, function(read, text, info)
        file:Release()
        if not read then print('Cannot read: ' .. text) return end
        local layout = util.JSONToTable(text)
        if layout then MyHUD.Apply(layout) end
    end)
end)
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
| `IsAvailable()` | `true`, or `false, message, code` (`needs_update`, `unavailable_remote`, `disabled`, `worker_missing`). |
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
eight reads and listings run at once.

Codes: `denied` (the player refused, closed the window or chose nothing),
`auto_denied`, `busy`, `not_found`, `network`, `remote_drive`, `invalid_path`
(also `relative`, `parent`, `stream`, `device`), `denied_location`, `link`,
`outside`, `hidden`, `too_large`, `offset_too_large`, `not_a_file`,
`not_a_folder`, `released`, `unreadable`, `dialog_failed`, `too_many_items`,
`invalid_options`, `denied_by_hook`, `needs_update`, `unavailable_remote`,
`disabled`, `worker_missing`.

### Hooks

* `MMDHL.RequestUserFile(opts, callback)`: the entry above.
* `MMDHLCanAccessUserFile(addonName, request)`: return `false` to refuse a
  request before any dialog opens (a privacy addon, a single player rule).
  `request` has `kind` (`'pick'` or `'path'`), `addon`, `script`, `path`,
  `folder`, `multiple`, `purpose` and `title`; it is a copy. Returning anything
  else lets the request continue to the player: this hook can never grant.
  Like any GMod hook, a hook that returns a value stops the others.
* `MMDHL.FileAccessReady(api)`: runs once after Model Hotloader's Lua loaded.
  `mmdhl.FileAccess` exists from then on (with an old binary module its
  `IsAvailable()` says so).
* `MMDHL.FileAccessChanged()`: a folder was remembered or revoked, or the
  switch changed.

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
  `FileAccessPick(json)`, `FileAccessRequest(json)` (both return an id),
  `FileAccessPoll(id)` (`pending` with `dialog` and `position`, `granted` with
  `items`, `denied`, `failed`), `FileAccessRead(handle, json)` /
  `FileAccessPollRead(id)` (`false` while running, then `data, infoJson`),
  `FileAccessList(handle, json)` / `FileAccessPollList(id)`,
  `FileAccessRelease(handle)`, `FileAccessCancel(id)`, `FileAccessGrants()`,
  `FileAccessRevoke(id or "all")` and `FileAccessSetEnabled(bool, language)`
  (`{"enabled":false}`, `{"enabled":true}` or `{"request":id}` for the
  confirmation dialog). The server module has none of them.
* The same release closes older ways for Lua to touch other computers or read
  beside any path: `BeginImport`, `Reload`, `PropReload` and the worker's
  `--request` refuse network sources before anything opens them (mapped drive
  letters still work), `InspectModelNotes` reads only beside an existing local
  model file (`.pmx`, `.pmd`, `.vrm`, `.fbx`, `.glb`, `.gltf`, `.dae`, `.obj`,
  `.blend`), and the Workshop package scan ignores network paths.
* Tests: `tests/file_access_tests.cpp` (CTest `file_access`, with the test-only
  worker `tests/file_access_worker.cpp`, which answers from an environment
  variable and is never packaged; the shipped worker has no such path) and
  `tests/test_file_access.py`.
