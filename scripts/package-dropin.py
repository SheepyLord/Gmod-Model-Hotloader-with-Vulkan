"""Stage the drop-in GarrysMod folders with the native files of one build.

With --renderer (the patched DXVK d3d9.dll recorded in native-release.json) two
packages are made: <name>-vulkan also installs DXVK as bin/win64/d3d9.dll, so
Garry's Mod renders through Vulkan and the Vulkan physics processor can share
its device; <name>-opengl-remix leaves Source's Direct3D 9 in place for PCs
where DXVK does not work or another d3d9.dll (RTX Remix, ReShade) must stay.
Without --renderer, one package named <name> holds the native files only.

A Linux build (native-release.json's platform linux64 or linux) makes one package
named <name> (default Model-Hotloader-<release>-<platform>) with every file in
garrysmod/lua/bin: Garry's Mod on Linux draws with OpenGL, so there is no renderer
to replace, and the runtime is found beside the modules ($ORIGIN).

Every package also carries the project's license and the notices of everything
built into the native files in GarrysMod/bin/win64/LICENSES (DXVK's in
DXVK-LICENSES beside it); packaging stops when they are incomplete. The source
the files are built from is published in the public repository (PUBLIC_REPOSITORY).

The Lua addon is not included: Workshop is its only distribution channel, and a
loose copy in garrysmod/addons would override the Workshop addon and its update
notices. The Workshop Lua runs these binaries whether it knows them or not: until
their native-release.json record (release, build, sizes and SHA-256) is added to
native_policy.lua, External Models shows a warning that it does not know them.
"""
import argparse, hashlib, json, os, pathlib, re, shutil, zipfile
from native_manifest import PLATFORM_FILES

ROOT = pathlib.Path(__file__).resolve().parents[1]
NATIVE = ('gmcl_mmdhl_win64.dll', 'gmsv_mmdhl_win64.dll', 'mmdhl_runtime_win64.dll', 'mmdhl_worker.exe', 'lib_coacd.dll')
RUNTIME = 'mmdhl_runtime_win64.dll'
# DXVK's own license and those of the subprojects linked into d3d9.dll.
DXVK_LICENSES = {'dxvk.txt': 'LICENSE', 'dxbc-spirv.txt': 'subprojects/dxbc-spirv/LICENSE', 'libdisplay-info.txt': 'subprojects/libdisplay-info/LICENSE'}
# Project notices beside the dependency notices in licenses/ (THIRD_PARTY.md lists them).
PROJECT_NOTICES = {'Model-Hotloader-LICENSE.txt': 'LICENSE', 'THIRD_PARTY.md': 'THIRD_PARTY.md'}
# Where players download releases and read the source (the MPL-2.0 files of
# THIRD_PARTY.md included), and the Workshop addon these files belong to.
PUBLIC_REPOSITORY = 'https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan'
WORKSHOP = 'https://steamcommunity.com/sharedfiles/filedetails/?id=3810025467'
HEADER = '''Model Hotloader native files {release} (build {build}), Windows x64{variant}

These are the native files only. The addon itself comes from the Steam Workshop:
{workshop}
Keep it subscribed, and do not copy any addon folder into garrysmod\\addons,
which would override the Workshop version.

Newer releases: {repository}/releases
Source code: {repository}
'''
STEPS = '''
1. Close Garry's Mod. It must be on the 64-bit branch (Steam > Garry's Mod >
   Properties > Betas > x86-64).
2. Copy the GarrysMod folder in this archive onto your Garry's Mod folder, for
   example C:\\Program Files (x86)\\Steam\\steamapps\\common\\GarrysMod, and
   replace existing files. Its contents go into the game folder itself (the one
   that contains bin and garrysmod), NOT into the garrysmod folder inside it.
   It adds:
   - garrysmod\\lua\\bin: native modules, import worker, runtime and CoACD
   - garrysmod\\shaders\\fxc: character model shaders
   - bin\\win64\\{runtime}: runtime loaded by the game{renderer_line}
   - bin\\win64\\LICENSES{dxvk_licenses}: license notices of Model Hotloader
     and the libraries built into these files
   Check: <Garry's Mod folder>\\bin\\win64\\{runtime} must exist afterwards.
3. Start Garry's Mod and open Q > External Models. Missing or mismatched files
   are reported there with repair instructions.{renderer_check}
   Files the Workshop addon does not know yet (a test build, a release newer
   than the addon) still run, with a warning there.

The Microsoft Visual C++ x64 Redistributable is required.
'''
VULKAN = '''
This package switches Garry's Mod to DXVK, which runs the game's Direct3D 9
renderer on Vulkan (every game session, not only Model Hotloader). It lowers
the GPU load and lets the Vulkan physics processor share the renderer's device.
It needs a graphics driver with Vulkan 1.3.

Use the -opengl-remix package instead if bin\\win64\\d3d9.dll already exists in your
Garry's Mod folder (RTX Remix, ReShade or another tool installed it): this
package would replace that file. To return to Direct3D 9 later, delete
bin\\win64\\d3d9.dll. Do the same if the game no longer starts or draws wrongly.

DXVK {dxvk} (github.com/doitsujin/dxvk; zlib license, its dxbc-spirv and
libdisplay-info parts MIT; see bin\\win64\\DXVK-LICENSES)
with Model Hotloader's patches:
{patches}
'''
NO_VULKAN = '''
This package leaves Garry's Mod on its own Direct3D 9 renderer. Use it when the
-vulkan package's DXVK renderer does not work on your PC, or when you keep
another d3d9.dll such as RTX Remix. If you installed the -vulkan package
before, delete bin\\win64\\d3d9.dll to return to Direct3D 9 (only when Q >
External Models shows "Renderer: DXVK (Vulkan)").
'''

LINUX_HEADER = '''Model Hotloader native files {release} (build {build}), Linux, {branch}

These are the native files only. The addon itself comes from the Steam Workshop:
{workshop}
Keep it subscribed, and do not copy any addon folder into garrysmod/addons,
which would override the Workshop version.

Newer releases: {repository}/releases
Source code: {repository}
'''
LINUX_STEPS = '''
1. Close Garry's Mod. {branch_step}
2. Copy the GarrysMod folder in this archive onto your Garry's Mod folder (Steam >
   Garry's Mod > Manage > Browse local files; usually
   ~/.local/share/Steam/steamapps/common/GarrysMod) and merge it with the folders
   there, replacing existing files. Its contents go into the game folder itself
   (the one that contains bin and garrysmod), NOT into the garrysmod folder inside it.
   It adds:
   - garrysmod/lua/bin: native modules, import worker and runtime{coacd_line}
   - garrysmod/shaders/fxc: character model shaders
   - garrysmod/lua/bin/LICENSES: license notices of Model Hotloader and the
     libraries built into these files
   Check: <Garry's Mod folder>/garrysmod/lua/bin/{runtime} must exist afterwards.
3. Start Garry's Mod and open Q > External Models. Missing or mismatched files
   are reported there with repair instructions.
   Files the Workshop addon does not know yet (a test build, a release newer
   than the addon) still run, with a warning there.

Garry's Mod draws with OpenGL on Linux; this package does not change that.
Choosing files to import opens zenity (part of the Steam Linux Runtime and of
most desktops) or, without it, kdialog.
The files need glibc 2.28 or newer, which every Steam Linux Runtime provides.
'''
LINUX_BRANCHES = {
    'linux64': ('64-bit (x86-64 branch)', 'It must be on the 64-bit branch (Steam > Garry\'s Mod >\n   Properties > Betas > x86-64). On the default branch use the -linux package.'),
    'linux': ('32-bit (default branch)', 'This package is for the default branch, which is 32-bit on\n   Linux (Steam > Garry\'s Mod > Properties > Betas > None). On the x86-64 branch\n   use the -linux64 package. Detailed (multi-hull) collision for static props\n   needs the 64-bit branch: CoACD has no 32-bit build.'),
}
# Notices of what only the Linux files contain: ICU (built into the runtime) and, for
# linux64, the OpenMP runtime that CoACD's wheel bundles.
LINUX_NOTICES = {'linux64': ('icu.txt', 'libgomp-GPL-3.0-with-GCC-exception.txt'), 'linux': ('icu.txt',)}

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def openmp_files(native_dir):
    """CoACD's OpenMP runtime (libgomp-<hash>.so.1), which lib_coacd.so loads from beside it."""
    return sorted(p.name for p in native_dir.glob('libgomp*.so*'))

def stage_linux(output, name, release, native_dir):
    platform = release['platform']
    folder = output/name
    archive = output/(name + '.zip')
    if folder.exists():
        shutil.rmtree(folder)
    archive.unlink(missing_ok=True)
    game = folder/'GarrysMod'
    lua_bin = game/'garrysmod/lua/bin'
    lua_bin.mkdir(parents=True)
    files = [entry['name'] for entry in release['files'].values()]
    if 'coacd' in release['files']:
        files += openmp_files(native_dir)
    for native in files:
        shutil.copyfile(native_dir/native, lua_bin/native)
        if not native.endswith('.json'):
            (lua_bin/native).chmod(0o755)
    # Named for the platform: both Linux packages can share a game folder.
    shutil.copyfile(native_dir/'native-release.json', lua_bin/f'mmdhl-native-release-{platform}.json')
    shaders = game/'garrysmod/shaders/fxc'
    shaders.mkdir(parents=True)
    for shader in sorted((ROOT/'addon/shaders/fxc').glob('mmdhl_*.vcs')):
        shutil.copyfile(shader, shaders/shader.name)
    notices = lua_bin/'LICENSES'
    notices.mkdir()
    for notice, relative in PROJECT_NOTICES.items():
        shutil.copyfile(ROOT/relative, notices/notice)
    for notice in sorted((ROOT/'licenses').glob('*.txt')):
        shutil.copyfile(notice, notices/notice.name)
    for notice in LINUX_NOTICES[platform]:
        shutil.copyfile(ROOT/'licenses/linux'/notice, notices/notice)
    branch, step = LINUX_BRANCHES[platform]
    text = LINUX_HEADER.format(release=release['release'], build=release['build'], branch=branch, workshop=WORKSHOP, repository=PUBLIC_REPOSITORY)
    text += LINUX_STEPS.format(branch_step=step, runtime=release['files']['runtime']['name'],
                               coacd_line=', and CoACD with\n     its OpenMP runtime' if 'coacd' in release['files'] else '')
    (folder/'INSTALL.txt').write_text(text, encoding='utf8', newline='\n')
    staged = sorted(f for f in folder.rglob('*') if f.is_file())
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for f in staged:
            # Keep the programs and libraries executable when unpacked.
            info = zipfile.ZipInfo.from_file(f, f.relative_to(folder).as_posix())
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(f, 'rb') as source:
                z.writestr(info, source.read(), compresslevel=6)
    return dict(name=name, folder=str(folder), archive=str(archive), files=len(staged), sha256=sha256(archive))

def stage(output, name, release, native_dir, renderer=None, dxvk=ROOT/'vendor/dxvk'):
    folder = output/name
    archive = output/(name + '.zip')
    if folder.exists():
        shutil.rmtree(folder)
    archive.unlink(missing_ok=True)
    game = folder/'GarrysMod'
    lua_bin = game/'garrysmod/lua/bin'
    lua_bin.mkdir(parents=True)
    for native in NATIVE:
        shutil.copyfile(native_dir/native, lua_bin/native)
    shutil.copyfile(native_dir/'native-release.json', lua_bin/'mmdhl-native-release.json')
    # The game loads the runtime from bin/win64: beside bin/win64/gmod.exe on the
    # x86-64 branch, and from the engine folder for the main branch's gmod_win64.exe.
    # Dedicated servers' srcds_win64.exe searches bin/win64 after its own folder,
    # and the installation check accepts either place.
    (game/'bin/win64').mkdir(parents=True)
    shutil.copyfile(native_dir/RUNTIME, game/'bin/win64'/RUNTIME)
    shaders = game/'garrysmod/shaders/fxc'
    shaders.mkdir(parents=True)
    for shader in sorted((ROOT/'addon/shaders/fxc').glob('mmdhl_*.vcs')):
        shutil.copyfile(shader, shaders/shader.name)
    record = release.get('renderer') or {}
    if renderer:
        shutil.copyfile(renderer, game/'bin/win64/d3d9.dll')
        (game/'bin/win64/DXVK-LICENSES').mkdir()
        for notice, relative in DXVK_LICENSES.items():
            shutil.copyfile(dxvk/relative, game/'bin/win64/DXVK-LICENSES'/notice)
    # Notices ride with the files they cover; the game loads nothing from them.
    notices = game/'bin/win64/LICENSES'
    notices.mkdir()
    for notice, relative in PROJECT_NOTICES.items():
        shutil.copyfile(ROOT/relative, notices/notice)
    for notice in sorted((ROOT/'licenses').glob('*.txt')):
        shutil.copyfile(notice, notices/notice.name)
    text = HEADER.format(release=release['release'], build=release['build'], variant=', DXVK (Vulkan) renderer' if renderer else ', without the DXVK renderer',
                         workshop=WORKSHOP, repository=PUBLIC_REPOSITORY)
    patches = '\n'.join('  - ' + patch for patch in record.get('patches', []))
    text += (VULKAN.format(dxvk=record.get('dxvk', '?'), patches=patches) if renderer else NO_VULKAN if record else '')
    text += STEPS.format(runtime=RUNTIME, renderer_line='\n   - bin\\win64\\d3d9.dll: DXVK renderer' if renderer else '',
                         renderer_check='\n   Its first line also names the renderer: DXVK (Vulkan) or Direct3D 9.' if record else '',
                         dxvk_licenses=' and DXVK-LICENSES' if renderer else '')
    (folder/'INSTALL.txt').write_text(text, encoding='utf8', newline='\r\n')
    staged = sorted(f for f in folder.rglob('*') if f.is_file())
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for f in staged:
            z.write(f, f.relative_to(folder).as_posix())
    return dict(name=name, folder=str(folder), archive=str(archive), files=len(staged), sha256=sha256(archive))

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--bin', type=pathlib.Path, default=ROOT/'build/bin/Release', help='Folder with the built native files and native-release.json')
    p.add_argument('--renderer', type=pathlib.Path, help='The patched DXVK d3d9.dll recorded in native-release.json; makes <name>-vulkan with it and <name>-opengl-remix without it')
    p.add_argument('--dxvk-source', type=pathlib.Path, default=ROOT/'vendor/dxvk', help='The DXVK checkout the renderer was built from (license files)')
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'dist')
    p.add_argument('--name', help='Folder and archive name (default Model-Hotloader-<release>-win64); --renderer adds -vulkan and -opengl-remix')
    args = p.parse_args()
    release = json.loads((args.bin/'native-release.json').read_text(encoding='utf8'))
    platform = release.get('platform', 'win64')
    if platform not in PLATFORM_FILES:
        raise ValueError(f'Unknown platform in native-release.json: {platform}')
    expected = NATIVE if platform == 'win64' else tuple(PLATFORM_FILES[platform].values())
    files = {entry['name']: entry for entry in release['files'].values()}
    if sorted(files) != sorted(expected):
        raise ValueError('native-release.json must list exactly: ' + ', '.join(expected))
    if platform != 'win64' and args.renderer:
        p.error('The DXVK renderer belongs to the Windows packages only')
    if 'coacd' in release['files'] and platform != 'win64' and not openmp_files(args.bin):
        raise FileNotFoundError("CoACD's OpenMP runtime (libgomp*.so*) is missing beside lib_coacd.so; run scripts/bootstrap.py")
    for name, entry in files.items():
        path = args.bin/name
        if path.stat().st_size != entry['size'] or sha256(path) != entry['sha256']:
            raise ValueError(f'{name} does not match native-release.json; rebuild before packaging')
    if args.renderer:
        record = release.get('renderer')
        if not record or record.get('name') != 'd3d9.dll' or args.renderer.stat().st_size != record['size'] or sha256(args.renderer) != record['sha256']:
            raise ValueError('The renderer does not match native-release.json; run scripts/native_manifest.py --renderer first')
        missing = [relative for relative in DXVK_LICENSES.values() if not (args.dxvk_source/relative).is_file()]
        if missing:
            raise FileNotFoundError('DXVK license files are missing (' + ', '.join(missing) + '); build the renderer with scripts/build-dxvk.ps1')
    name = args.name or f"Model-Hotloader-{release['release']}-{platform}"
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,109}', name):
        p.error('Invalid package name')
    if not any((ROOT/'licenses').glob('*.txt')) or not all((ROOT/relative).is_file() for relative in PROJECT_NOTICES.values()):
        raise FileNotFoundError('The license notices (LICENSE, THIRD_PARTY.md, licenses/) are missing')
    if platform != 'win64' and not all((ROOT/'licenses/linux'/notice).is_file() for notice in LINUX_NOTICES[platform]):
        raise FileNotFoundError('The Linux license notices (licenses/linux) are missing')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if platform != 'win64':
        packages = [stage_linux(output, name, release, args.bin)]
    elif args.renderer:
        packages = [stage(output, name + '-vulkan', release, args.bin, args.renderer, args.dxvk_source),
                    stage(output, name + '-opengl-remix', release, args.bin)]
    else:
        packages = [stage(output, name, release, args.bin)]
    # The record to add to native_policy.lua's releases, verbatim, in the Actions job summary.
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf8') as summary:
            where = ("under `releases` in `addon/lua/mmdhl/native_policy.lua` (set `url` to the GitHub release), or run scripts/update-native-policy.ps1"
                     if platform == 'win64' else f"in `addon/lua/mmdhl/native_policy.lua` as `releases[\"{release['release']}\"].platforms.{platform}` (set `url` to the GitHub release)")
            summary.write(f"### {name}\n\nPackages: {', '.join(item['name'] for item in packages)}. Add this record {where}:\n\n"
                          f"```json\n{json.dumps(release, indent=2)}\n```\n")
    print(json.dumps({'release': release['release'], 'build': release['build'], 'renderer': bool(args.renderer), 'packages': packages}, indent=2))

if __name__ == '__main__':
    main()
