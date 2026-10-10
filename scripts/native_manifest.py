"""Produce the Workshop policy and installer manifest from final native artifacts."""
import argparse, hashlib, json, re, subprocess, sys
from pathlib import Path
from compatibility_profiles import lua_policy, write_policy

ROOT = Path(__file__).resolve().parents[1]
# The recorded files of each platform (installation.lua's platformFiles). win64: Windows
# x64; linux64: Linux, the x86-64 branch; linux: Linux, the default (32-bit) branch, for
# which CoACD has no build.
PLATFORM_FILES = {
    'win64': {'client':'gmcl_mmdhl_win64.dll', 'server':'gmsv_mmdhl_win64.dll',
              'runtime':'mmdhl_runtime_win64.dll', 'worker':'mmdhl_worker.exe', 'coacd':'lib_coacd.dll'},
    'linux64': {'client':'gmcl_mmdhl_linux64.dll', 'server':'gmsv_mmdhl_linux64.dll',
                'runtime':'libmmdhl_runtime_linux64.so', 'worker':'mmdhl_worker_linux64', 'coacd':'lib_coacd.so'},
    'linux': {'client':'gmcl_mmdhl_linux.dll', 'server':'gmsv_mmdhl_linux.dll',
              'runtime':'libmmdhl_runtime_linux.so', 'worker':'mmdhl_worker_linux'},
}
FILES = PLATFORM_FILES['win64']

def platform_of(bin_dir):
    """The platform whose worker is in this build folder."""
    found = [name for name, files in PLATFORM_FILES.items() if (bin_dir/files['worker']).is_file()]
    if len(found) != 1:
        raise FileNotFoundError(f'Expected the worker of exactly one platform in {bin_dir}, found: {", ".join(found) or "none"}')
    return found[0]

def renderer_record(path):
    """The bundled DXVK d3d9.dll (bin/win64 in the Vulkan drop-in package)."""
    data = path.read_bytes()
    tag = re.search(r"\$tag='([^']+)'", (ROOT/'scripts/build-dxvk.ps1').read_text(encoding='utf8'))
    patches = sorted(p.name for p in (ROOT/'patches/dxvk').glob('*.patch'))
    return dict(name='d3d9.dll', size=len(data), sha256=hashlib.sha256(data).hexdigest(), kind='dxvk',
                dxvk=tag.group(1) if tag else 'unknown', patches=patches)

def release_record(identity, files, recorded, renderer=None):
    """The release record for these binaries. The renderer is built separately
    (scripts/build-dxvk.ps1): without one, the record keeps the renderer already
    recorded for this release instead of dropping it."""
    release = dict(identity, files=files)
    if renderer:
        release['renderer'] = renderer_record(renderer)
    elif recorded.get('renderer'):
        release['renderer'] = recorded['renderer']
    # Players download releases from the public repository's releases page.
    release['url'] = 'https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/releases'
    return release

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--bin', type=Path, default=ROOT/'build/bin/Release')
    p.add_argument('--output', type=Path, default=ROOT/'addon/lua/mmdhl/native_policy.lua')
    p.add_argument('--renderer', type=Path, help='The patched DXVK d3d9.dll shipped with this release (build-dxvk/src/d3d9/d3d9.dll)')
    p.add_argument('--check', action='store_true')
    args = p.parse_args()
    platform = platform_of(args.bin)
    identity = json.loads(subprocess.check_output([str(args.bin/PLATFORM_FILES[platform]['worker']), '--version'], text=True))
    if identity.get('platform', 'win64') != platform:
        raise ValueError(f"The worker reports platform {identity.get('platform')}, not {platform}")
    files = {}
    for key, name in PLATFORM_FILES[platform].items():
        data = (args.bin/name).read_bytes()
        files[key] = dict(name=name, size=len(data), sha256=hashlib.sha256(data).hexdigest())
    value = lua_policy(args.output) if args.output.exists() else dict(schema=1, approved=[], releases={})
    entry = dict(value.get('releases', {}).get(identity['release']) or {})
    # A release's Windows record is the entry itself (as in 2.x); the Linux records sit in
    # its platforms table, so a release can have any of them.
    if platform == 'win64':
        recorded = {key: item for key, item in entry.items() if key != 'platforms'}
    else:
        recorded = dict((entry.get('platforms') or {}).get(platform) or {})
        if args.renderer:
            raise ValueError('The DXVK renderer belongs to the Windows packages only')
    renderer = args.renderer
    # A check compares a recorded renderer with the local DXVK build when there is one.
    if not renderer and args.check and recorded.get('renderer') and (ROOT/'build-dxvk/src/d3d9/d3d9.dll').is_file():
        renderer = ROOT/'build-dxvk/src/d3d9/d3d9.dll'
    release = release_record(identity, files, recorded, renderer)
    if args.check:
        if recorded != release:
            raise ValueError('Workshop native policy does not match the completed binaries')
        checked = ' and the renderer' if renderer else ' (renderer recorded but not built here; not compared)' if recorded.get('renderer') else ''
        print(f'Native policy matches all {len(files)} {platform} binaries' + checked); return
    value['recommended'] = identity['release']
    value['approved'] = list(dict.fromkeys(value.get('approved', []) + [identity['release']]))
    if platform == 'win64':
        value['releases'][identity['release']] = dict(release, platforms=entry['platforms']) if entry.get('platforms') else release
    else:
        entry.setdefault('platforms', {})[platform] = release
        value['releases'][identity['release']] = entry
    write_policy(args.output, value)
    (args.bin/'native-release.json').write_text(json.dumps(release, indent=2), encoding='utf8')
    kept = '' if renderer or 'renderer' not in release else ' (kept)'
    print(f'Generated native policy for {identity["release"]} {platform} ({identity["build"]})' + (f', renderer {release["renderer"]["sha256"][:12]}{kept}' if 'renderer' in release else ''))

if __name__ == '__main__':
    main()
