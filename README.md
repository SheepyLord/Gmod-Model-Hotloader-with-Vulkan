# Garry's Mod Static and Character Model Hotload Importer and Softbody Simulation with Vulkan (GSCMI-SS)

This is a binary module based addon for Garry's Mod. With this installed, 3D assets stored in common formats can be hotloaded directly into *Garry's Mod*. This plugin is implemented entirely within GMod, no external executable like [GSCMI](https://github.com/SheepyLord/Gmod-Simple-Character-Model-Importer) is required. Gmod must be ran using the x64 version. Also, it replaces the rendering API of Garry's Mod from OpenGL to Vulkan for the Vulkan ridigbody physics resolver. The performance of the game at complex scenes should increase while halves the GPU use than Vanilla. 

Preview video: 
[Gmod Model Hotloader and Softbody Simulation - Addon Preview](https://www.youtube.com/watch?v=IvpritG_aKQ)

Unlike most modern sandbox games, in *Garry's Mod* you must first convert models or characters to the Source Engine format before you can actually use them. While being able to provide a Source engine native asset, this process is usually tedious and time consuming. We're pleased to introduce the GSCMI-SS which bypasses all limitations of the Source Engine compiler (include bone, material, physics rigidbody, shapekey count) by writing runtime model information directly into the game memory and the rendering pipeline, thereby completely resolving this issue by hotloading models into the game. The imported model is algorithmically identical to a normally spawned model in game, at least at the engine level, making the model porting process obsolete with moderate performance tradeoff when multiple models are processed in the player's view. For models with PBR textures, [Advanced Material Editor](https://steamcommunity.com/sharedfiles/filedetails/?id=3793132326) could allow further finetune of the rendering of the model in game. 

If any issue appears when using Vulkan version (likely due to limited memory or old devices), please delete this file in your Gmod installation: Drive:\SteamLibrary\steamapps\common\GarrysMod\bin\win64\d3d9.dll

<img width="3840" height="2160" alt="20260924000541_1" src="https://github.com/user-attachments/assets/7d783459-ea74-42f5-a42b-1914ca347b0e" />

## Install and use
1. Subscribe to the [Model Hotloader Workshop addon](https://steamcommunity.com/sharedfiles/filedetails/?id=3810025467).
2. Download the most recent binary module package from [Releases](https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/releases). 在中国大陆请使用[替代链接](https://pan.baidu.com/s/1eUaJAUhnnnGpNSnvFojmwQ?pwd=lord).
3. Close Garry's Mod and copy the package's `GarrysMod` folder onto `steamapps\common\GarrysMod`, replacing files: its contents belong in the game folder itself (the one with `bin` and `garrysmod`), not in `garrysmod`. Afterwards `steamapps\common\GarrysMod\bin\win64\mmdhl_runtime_win64.dll` exists. A package copied into `garrysmod` (or `addons`) is detected: a window and External Models say where the files are and where they belong.

There are two packages:

- `…-win64-vulkan.zip` (default): the native modules plus DXVK (`bin\win64\d3d9.dll`), which runs Garry's Mod's Direct3D 9 renderer on Vulkan. It lowers GPU load and lets the Vulkan physics processor share the renderer's device.
- `…-win64-opengl-remix.zip`: the native modules only; the game keeps its own Direct3D 9 renderer. Use it if DXVK does not work on your PC, or if you use RTX Remix, ReShade or another `d3d9.dll`, which the `-vulkan` package would replace. Deleting `bin\win64\d3d9.dll` also returns the game to Direct3D 9.

The installation banner (Q > External Models) shows which renderer is active. When the Workshop addon recommends a newer binary module than the one installed, a window offers the download (it can be skipped for that version or turned off for good); older binary modules keep working, and features that need the update say so. A binary module the Workshop addon does not know yet (a build from GitHub Actions or a local build, a newer release, modified files) runs too, with a warning in External Models that **Dismiss** hides until the files change; so does a game build whose interfaces the binary's checks reject (the binary still refuses each engine call it cannot make safely). The Multicore CPU Processor is the default physics processor; the Vulkan GPU Processor is an experimental alternative that needs the DXVK renderer. 

If cloned with locally built binary modules, run:

```powershell
./scripts/install.ps1 -GameRoot 'H:\SteamLibrary\steamapps\common\GarrysMod'
```

## Characters, physics and other formats

- **Import Character Models** reads PMX, PMD and VRM, and FBX, glTF/GLB and DAE characters with a skeleton. For those, and for any character whose bones the importer cannot match, the bone assignment window shows what was found and lets you assign the rest. [Guide](docs/CHARACTER_IMPORT.md).
- **Ragdoll physics…** (right-click a character ragdoll) edits its Source physics model: presets, collision shapes, joint limits, masses, damping, self-collision and every other `$collisionjoints` value, or copies them from an installed model. [Guide](docs/PHYSICS_EDITOR.md).
- Addon authors can let players hand files to their addon through `hook.Run('MMDHL.RequestUserFile', …)`, with the player's consent each time. [File access for other addons](docs/FILE_ACCESS.md).

## Model terms of use

Many MMD and VRM models are released only for making videos. Their terms often forbid using them in games or other software, sharing or re-uploading them, or showing them in sexual or violent content. The addon cannot check or enforce a model's terms, and importing a model gives you no right to use it: **you are responsible for following each model's terms, its author's wishes and the law.**

## Share models on the Workshop

Select imported models in the library and choose **Export as Workshop package…** to write a `.gma` (and a preview image) to `garrysmod/data/mmd_hotloader/exports`, then publish it with the `gmpublish` command the export window copies. Players who subscribe and have this addon get the models in the library's **Workshop** folder, labelled with the Workshop item they come from; deleted ones move to **Deleted Workshop models** until restored. A package can instead install as the player's own models (demo packs). Exporting needs a native release that includes package export; installing works with the current one. [How packages work](docs/WORKSHOP_PACKAGES.md).

## Limitation

This project currently only fit Windows x64 GMod (with linux build being worked on). Character models draw with GMod's multicore rendering; previews and legacy `mmdhl_ragdoll` entities switch to `mat_queue_mode 0` while they draw (`mmdhl_force_immediate_rendering 1` keeps the old single-threaded behaviour for every model). Physics performance was optimized but might still be slow on slow computers, thus there is not a frame-rate guarantee.

## Build and verify

Requirements: Python 3, VS 2022 C++ Build Tools, Windows SDK/CMake, and the supported GMod installation. See Action for details.

```powershell
./scripts/build.ps1
./scripts/game-start.ps1 -Visible
python scripts/prepare-acceptance.py
python scripts/test-carrier.py
python scripts/test-native-anatomy.py
python scripts/test-native-tools.py Xin
python scripts/test-library-ui.py
python scripts/test-native-extras.py
python scripts/test-compatibility.py imports
python scripts/test-compatibility.py materials
python scripts/test-compatibility.py eyes
python scripts/test-compatibility.py morphs
python scripts/test-secondary-scenes.py
python scripts/test-import-feedback.py
python scripts/test-native-stability.py Xin
python scripts/test-native-stability.py stress
python scripts/test-native-stability.py cycles
./scripts/game-stop.ps1
```


Project code is MIT except the MPL-2.0 synchronization/reference files identified in [THIRD_PARTY.md](THIRD_PARTY.md). The binary module packages carry the applicable license notices in `GarrysMod/bin/win64/LICENSES`; their source is this repository. No user models, corpus textures, MMD executable or engine DLLs are distributed.
