"""Produce the Workshop policy and installer manifest from final native artifacts."""
import argparse, hashlib, json, re, subprocess, sys
from pathlib import Path
from compatibility_profiles import lua_policy, write_policy

ROOT = Path(__file__).resolve().parents[1]
FILES = {'client':'gmcl_mmdhl_win64.dll', 'server':'gmsv_mmdhl_win64.dll',
         'runtime':'mmdhl_runtime_win64.dll', 'worker':'mmdhl_worker.exe', 'coacd':'lib_coacd.dll'}

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
    identity = json.loads(subprocess.check_output([str(args.bin/'mmdhl_worker.exe'), '--version'], text=True))
    files = {}
    for key, name in FILES.items():
        data = (args.bin/name).read_bytes()
        files[key] = dict(name=name, size=len(data), sha256=hashlib.sha256(data).hexdigest())
    value = lua_policy(args.output) if args.output.exists() else dict(schema=1, approved=[], releases={})
    recorded = dict(value.get('releases', {}).get(identity['release']) or {})
    renderer = args.renderer
    # A check compares a recorded renderer with the local DXVK build when there is one.
    if not renderer and args.check and recorded.get('renderer') and (ROOT/'build-dxvk/src/d3d9/d3d9.dll').is_file():
        renderer = ROOT/'build-dxvk/src/d3d9/d3d9.dll'
    release = release_record(identity, files, recorded, renderer)
    if args.check:
        if recorded != release:
            raise ValueError('Workshop native policy does not match the completed binaries')
        checked = ' and the renderer' if renderer else ' (renderer recorded but not built here; not compared)' if recorded.get('renderer') else ''
        print('Native policy matches all five binaries' + checked); return
    value['recommended'] = identity['release']
    value['approved'] = list(dict.fromkeys(value.get('approved', []) + [identity['release']]))
    value['releases'][identity['release']] = release
    write_policy(args.output, value)
    (args.bin/'native-release.json').write_text(json.dumps(release, indent=2), encoding='utf8')
    kept = '' if renderer or 'renderer' not in release else ' (kept)'
    print(f'Generated native policy for {identity["release"]} ({identity["build"]})' + (f', renderer {release["renderer"]["sha256"][:12]}{kept}' if 'renderer' in release else ''))

if __name__ == '__main__':
    main()
