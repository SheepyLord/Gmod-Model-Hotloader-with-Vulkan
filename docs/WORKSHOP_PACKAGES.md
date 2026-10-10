# Workshop model packages

Imported character models and static props can be exported from the library as
a `.gma` package, published on the Steam Workshop, and installed automatically
for every player who subscribes to it and has this addon. The package needs
nothing but this addon as a dependency.

## Exporting

Select one or more models in **External Models → Character Models** or
**Static Props** (Ctrl/Shift for several) and choose **Export as Workshop
package…** (button under the list, or right-click). The export window lists
every imported character and prop, with the selection ticked, and asks for:

- **Package title**: the GMA title, which `gmpublish` uses as the Workshop title.
- **Author** and **Workshop description** (credits and licence terms belong here).
- **Workshop tags**: at most two of Workshop's tags for the `model` type.
- **Install as the player's own models (demo pack)**: the flag for shipping demo
  models, with an optional folder for them (see below).

**Export package** writes `garrysmod/data/mmd_hotloader/exports/<title>.gma`
(the file name is the title reduced to safe ASCII). When a character preview is
open in the library, a 512×512 `<title>.jpg` Workshop preview image is rendered
from that preview; otherwise supply your own. A `<title>.txt` beside them
repeats the paths and the publish command. **Show file** opens Explorer on the
package and **Copy publish command** copies:

```
"<GarrysMod>\bin\gmpublish.exe" create -addon "<…>\exports\<title>.gma" -icon "<…>\exports\<title>.jpg"
```

Run it in a command prompt while Steam is running. To update a published item,
export again and run `gmpublish update -addon "<new .gma>" -id <Workshop item id>`.
Only publish models whose licence allows redistribution. The export window
shows how many selected models mention restrictions, and **Export package**
first lists each model's recorded terms and asks the player to confirm that
they may share them ([Model terms of use](MODEL_TERMS.md)).

`mmdhl_export` opens the export window with nothing ticked.

The package manifest (names, settings, fits and terms of every model) may be at
most 4 MiB, the size installers accept. A selection whose manifest would be
larger is refused, before the models are packed and without touching an
earlier package of the same name; export fewer models per package. The same
goes for the files installers accept: a model's manifest up to 64 MiB, its
model data up to 1 GiB, a texture or prop bundle up to 256 MiB, and 16 GiB per
package. A file that compresses more than 256 times (installers refuse such a
decompression bomb above 1 MiB) is stored uncompressed instead.

## What a package contains

A package is an ordinary GMA whose members are all on GMod's addon whitelist:

| Member | Content |
| --- | --- |
| `data_static/mmdhl/packages/<id>.json` | Package manifest (below). `<id>`: 32 random hex digits per export. |
| `data_static/mmdhl/files/<sha256>.dat` | One cache file, compressed with the lossless block format of multiplayer model transfers (`MMDPACK2`). Identical files, such as shared textures, are stored once. |

For a character the files are its imported cache data (`assets/<id>/manifest.json`,
`assets/<id>/model.bin` and its `textures/<sha256>.png`); for a static prop its
bundle (`static/assets/<id>.gmdl`). Generated archives (Source materials,
carrier models) are never shipped: every game rebuilds them locally, exactly as
after a multiplayer transfer. The manifest also carries each model's display
name and settings: spawn size and bodygroup presets, first-person arm parts and
collision-fit corrections for characters, spawn size for props. It also
carries each model's `terms`: its embedded comment, VRM licence and readme-named
files, which become the installed model's terms-of-use record
([Model terms of use](MODEL_TERMS.md)).

```json
{"format":"mmdhl-package","version":1,"id":"<32 hex>","title":"…","author":"…","description":"…",
 "created":1790000000,"install":"workshop","folder":"…","generator":{"release":"…","build":"…"},
 "items":[{"kind":"character","asset":"<64 hex>","name":"…","files":["assets/…/manifest.json","assets/…/model.bin","textures/….png"],
           "settings":{"spawn":{"scaleMultiplier":1},"bodygroups":{"presets":{},"default":null}},"arms":{},"fit":{},
           "terms":{"file":"….pmx","embedded":{"comment":"…"},"readmes":[{"name":"readme.txt","text":"…","matched":true}]}},
          {"kind":"static","asset":"<64 hex>","name":"…","files":["static/assets/….gmdl"],"settings":{"spawn":{"scale":1}}}],
 "files":{"<cache path>":{"size":0,"sha256":"<64 hex>","packed":0}}}
```

## Installing

After map load, and whenever mounted content changes (subscriptions, content
downloaded for a server), `workshop.lua` looks for `data_static/mmdhl/packages/*.json`
in the `GAME` search path. Nothing in a package is trusted: the manifest is
size-limited and validated (paths, hashes, sizes, packet bounds; characters may
only list their own manifest and model plus hash-named textures, props only
their own bundle). Every subscriber writes what a package declares, so each
file is capped by kind (the limits above), may inflate at most 256 times its
packed size (or to 1 MiB), and a package may hold at most 16 GiB. Missing files are installed with the verified shared-file
transfer (`BeginSharedFile` … `CommitSharedFile`), which checks each file's
SHA-256 and validates prop bundles; the model loader then checks that the
manifest and model match the model's identity. Models already in the cache are
not touched, and a model's existing name and settings are never overwritten.
A model counts as installed only with every file its package lists: an install
that stopped part way (a texture that failed, a game closed mid-install) has no
completion record, so the next scan or start transfers just the missing files.
Files whose hashes all match can still make up another model than the one the
package names (a manifest of another identity, a model that does not parse).
With natives after 2.3.0 the assembled model is loaded off the main thread as
using it would load it (`StartAssetCheck`): only when that passes is it recorded
as installed, and a server approves it only then. A model that fails is reported
like a failed file, its manifest (a prop: its bundle) is removed so that the
library does not list it, and shared textures stay. Older natives install as
before.

Installed models repair themselves. Every scan compares the sizes of their
files with the package; a missing or resized file clears the completion record
and is installed again. Once per session, files whose modification time differs
from the one recorded when they were installed or last checked are hashed, a
few milliseconds per frame while nothing installs; one that no longer matches
is installed again from the package. An unchanged cache costs no reads. A
player's own import of a model that a package also provides is only checked
for existence.

Installation runs a few milliseconds per frame with a progress notice, so
installing works with the current native release; only exporting needs a
native release that includes `StartPackageExport`.

Each package is traced to the addon it came from: first through the addon whose
title equals the package title (a GMA mounts under its own title), and for a
Workshop item renamed after publishing through the native module, which reads
the mounted archives' indexes off the main thread.

## In the library

- **Workshop** in the folder tree lists every model installed from Workshop
  packages, with one sub-folder per Workshop item.
- Every row carries a label: **Workshop**, **Own** (imported by the player,
  including demo models), **Server** (shared by a multiplayer server) or
  **Deleted**.
- Above the preview, a strip names the Workshop item and author, with a
  **Workshop page** button (Steam overlay) for Workshop items.
- **Deleting** a model that a mounted package provides removes its cache files
  and moves it to **Deleted Workshop models**, so it is not installed again.
  **Restore** (button, strip or right-click) installs it again from the package.
- **Unsubscribing** (or disabling the addon) removes its Workshop models from
  the cache at the next scan, except models that are in use on the map.

Installation records live in `data/mmd_hotloader/workshop/state.json`.

## Demo packs

With **Install as the player's own models**, subscribers get the models once as
if they had imported them: no Workshop label, placed in the chosen folder (or
Unfiled), and they stay after unsubscribing. Deleting one still records the
choice, so it is not installed again, and it can be restored while the package
is mounted. To ship demo models inside this addon itself, export a demo pack and
copy its `data_static` folder into the addon before publishing the addon; any
mounted content is scanned, not only Workshop items.

## Multiplayer

- A **dedicated server** installs the packages it mounts into its own cache,
  repairs it the same way, and approves those models for its players
  (`approvedBy = "workshop"` in `approved.json`) once their assembled files pass
  the check above (one model at a time; a model approved before needs none); the
  approval is withdrawn when
  the package is gone, and connected clients drop the withdrawn models and props
  from their libraries (`mmdhl_catalog_forget`).
- A **listen server** shares its host's cache: the host's game installs the
  models and the server then approves them.
- **Clients** that have the package mounted (for example through the server's
  `resource.AddWorkshop`) install it locally; the others download approved
  models from the server as before.

## Validation (2026-09-25)

- `mmdhl_package_tests` (CTest `workshop_packages`): the GMA read back
  independently (members, CRCs, gmad header, Workshop description), whitelist
  compliance of every member, deduplication, lossless packets within the
  transfer bound, the addon-index scan, rejected exports leave no files, file
  name sanitizing.
- `tests/test_workshop_packages.py`: workshop.lua against a simulated game:
  validation of hostile manifests, settings sanitizing, installation through
  the transfer (ordered chunks, sizes), title and archive-index origins, demo
  adoption, existing settings kept, delete/rescan/restore, unsubscribing,
  failed files, dedicated-server installation, approval and withdrawal.
- In game (Workshop enabled, 748 mounted addons): export through the window
  (79 MB package of two characters and a prop, preview image rendered), the
  package extracts with Facepunch's `gmad.exe`, packages in mounted addons are
  installed with the right origin, Workshop label, names and settings, the demo
  pack installs as own models in its folder, delete → Deleted Workshop models
  → restore, deleted models stay deleted after rescans, and unmounted packages
  remove their Workshop models. A rescan with packages present takes 0.14 s.
- Not exercised: an actual Workshop upload, and dedicated or listen servers in
  game (covered by the simulated test).
- 2026-09-26 (second Codex review): `workshop_packages` also covers the
  per-kind path limits and a file past the expansion bound stored within it;
  `tests/test_workshop_packages.py` covers the caps, the expansion bound, size
  repair at every scan, hashing of modified files only (none for an unchanged
  cache in a new session) and dedicated-server repair.
