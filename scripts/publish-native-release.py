"""Publish a native release from a finished "Build drop-in package" run, with the
Workshop policy that accepts it.

  python scripts/publish-native-release.py publish --run <id> --notes whats-new.md [--dry-run]
  python scripts/publish-native-release.py supersede <older-label> --note note.md

publish:
 1. The run must have succeeded on a commit that is on origin/main, and the local
    main must equal origin/main with a clean working tree.
 2. Its two packages (-vulkan, -opengl-remix) are downloaded. Both must carry the
    same mmdhl-native-release.json, every native file must match that record, the
    Vulkan package's d3d9.dll its renderer record, and the other must have none.
 3. Each package is zipped and the GitHub release <label> is created at the run's
    commit with both zips. --notes holds the "What's new" list; the rest of the
    notes (requirements, packages, install, checksums) is standard. An existing tag
    is never replaced or deleted: publishing stops, unless that release targets the
    same commit with both packages (an interrupted publish), which is kept as it is.
 4. The record is appended to addon/lua/mmdhl/native_policy.lua with the release
    link and the recommended release's alternative link, approved and made the
    recommended release. Earlier records and approvals stay.
 5. The policy is evaluated for every approved release (client and server must
    load without issues), check-i18n.py and check-lua-tests.py run, and addon.gma
    is rebuilt with gmad and checked against the addon folder.
 6. native_policy.lua and addon.gma are committed and pushed to main.

--dry-run does steps 1 and 2, prints the notes and the policy change, and leaves
GitHub and the repository alone.

supersede prepends a note (Markdown, shown as a quote) to an older release's
notes, once; its files stay as they are.

Uploading addon.gma to the Workshop and the packages to the mirror stays manual.
"""
import argparse, hashlib, json, pathlib, shutil, struct, subprocess, sys, tempfile, zipfile, zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO = 'SheepyLord/Gmod-Model-Hotloader-with-Vulkan'
REPO_URL = 'https://github.com/' + REPO
WORKFLOW = 'Build drop-in package'
WORKSHOP = 'https://steamcommunity.com/sharedfiles/filedetails/?id=3808939802'
POLICY = ROOT / 'addon/lua/mmdhl/native_policy.lua'
GMA = ROOT / 'addon.gma'
VARIANTS = ('vulkan', 'opengl-remix')
NOTES = '''Native files for **Model Hotloader**, Windows x64 (the 64-bit Garry's Mod: the default branch's `gmod_win64.exe` or the x86-64 beta).

**What's new in {label}**
{whats_new}

Use it with the current Workshop version of the addon.

**Requirements**
- The Lua addon comes from the Steam Workshop: **[Model Hotloader]({workshop})**. Subscribe to it, then install one of the packages below.
- The Microsoft Visual C++ x64 Redistributable.
{mirror}
## Which package

| Package | For |
|---|---|
| `{zip_vulkan}` | **Most PCs.** Also installs DXVK as `bin\\win64\\d3d9.dll`, so Garry's Mod renders through Vulkan. Needs a graphics driver with Vulkan 1.3. |
| `{zip_remix}` | **RTX Remix, ReShade or another `d3d9.dll`**, or PCs where DXVK doesn't work. Keeps the game's own Direct3D 9 renderer. |

## Install

1. Close Garry's Mod. Run it as 64-bit: the default branch's `gmod_win64.exe`, or the x86-64 beta (Steam > Garry's Mod > Properties > Betas).
2. Copy the `GarrysMod` folder from the archive onto your Garry's Mod folder, for example `C:\\Program Files (x86)\\Steam\\steamapps\\common\\GarrysMod`, and replace existing files.
3. Start the game and open **Q > External Models**. It confirms the installation or tells you what is missing.

`INSTALL.txt` in each archive has the details (its step 1 still names only the x86-64 beta; either 64-bit branch works). The licence notices are installed to `bin\\win64\\LICENSES`; the game doesn't load them.

## Checksums

Build: {repo}/actions/runs/{run} (`{build}`)

SHA-256:
```
{sha_vulkan}  {zip_vulkan}
{sha_remix}  {zip_remix}
```
'''


def run(*cmd, check=True):
    result = subprocess.run([str(c) for c in cmd], cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace')
    if check and result.returncode:
        raise SystemExit(f'{" ".join(map(str, cmd))} failed ({result.returncode}):\n{result.stdout}{result.stderr}')
    return result


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def check_repository(dry_run):
    run('git', 'fetch', '-q', 'origin')
    branch = run('git', 'branch', '--show-current').stdout.strip()
    dirty = [line for line in run('git', 'status', '--porcelain').stdout.splitlines() if not line.startswith('??')]
    head, remote = (run('git', 'rev-parse', ref).stdout.strip() for ref in ('HEAD', 'origin/main'))
    problems = [p for p, bad in ((f'on branch {branch}, not main', branch != 'main'), ('uncommitted changes: ' + ', '.join(dirty), dirty),
                                 ('main differs from origin/main', head != remote)) if bad]
    if problems and not dry_run:
        raise SystemExit('The repository must be main, clean and equal to origin/main: ' + '; '.join(problems))
    for p in problems: print('note:', p)


def check_run(run_id):
    info = json.loads(run('gh', 'run', 'view', run_id, '--repo', REPO, '--json', 'status,conclusion,headSha,workflowName').stdout)
    if info['workflowName'] != WORKFLOW: raise SystemExit(f'run {run_id} is "{info["workflowName"]}", not "{WORKFLOW}"')
    if (info['status'], info['conclusion']) != ('completed', 'success'): raise SystemExit(f'run {run_id} is {info["status"]}/{info["conclusion"]}')
    if run('git', 'merge-base', '--is-ancestor', info['headSha'], 'origin/main', check=False).returncode:
        raise SystemExit(f'run {run_id} built {info["headSha"]}, which is not on origin/main')
    return info['headSha']


def check_file(path, expected, what):
    if not path.is_file(): raise SystemExit(f'{what} is missing: {path}')
    data = path.read_bytes()
    if ('size' in expected and len(data) != expected['size']) or hashlib.sha256(data).hexdigest() != expected['sha256']:
        raise SystemExit(f'{what} does not match the release record: {path}')


def download(run_id, work):
    shutil.rmtree(work, ignore_errors=True); work.mkdir(parents=True)
    run('gh', 'run', 'download', run_id, '--repo', REPO, '--dir', work)
    packages = {v: d for d in work.iterdir() if d.is_dir() for v in VARIANTS if d.name.endswith('-win64-' + v)}
    if sorted(packages) != sorted(VARIANTS): raise SystemExit(f'run {run_id} lacks a package: found {sorted(p.name for p in work.iterdir())}')
    records = {v: json.loads((d / 'GarrysMod/garrysmod/lua/bin/mmdhl-native-release.json').read_text(encoding='utf-8')) for v, d in packages.items()}
    record = records['vulkan']
    if records['opengl-remix'] != record: raise SystemExit('The packages carry different release records')
    if record.get('installApi') != 1: raise SystemExit(f'Unsupported installApi {record.get("installApi")}')
    for v, d in packages.items():
        for key, f in record['files'].items(): check_file(d / 'GarrysMod/garrysmod/lua/bin' / f['name'], f, f'{v} {key}')
        runtime = record['files']['runtime']
        check_file(d / 'GarrysMod/bin/win64' / runtime['name'], runtime, f'{v} bin/win64 runtime')
    check_file(packages['vulkan'] / 'GarrysMod/bin/win64/d3d9.dll', record['renderer'], 'vulkan d3d9.dll')
    if (packages['opengl-remix'] / 'GarrysMod/bin/win64/d3d9.dll').exists(): raise SystemExit('The opengl-remix package carries a d3d9.dll')
    return record, packages


def zip_package(folder, out):
    target = out / (folder.name + '.zip')
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(folder.rglob('*')):
            if p.is_file(): z.write(p, p.relative_to(folder).as_posix())
    with zipfile.ZipFile(target) as z:
        if z.testzip() is not None: raise SystemExit(f'{target} is damaged')
    return target


def read_policy():
    raw = POLICY.read_bytes(); crlf = b'\r\n' in raw
    text = raw.decode('utf-8').replace('\r\n', '\n')
    head, rest = text.split('[==[\n', 1); body, tail = rest.split('\n]==]', 1)
    policy = json.loads(body)
    if json.dumps(policy, indent=2, ensure_ascii=False) != body: raise SystemExit('native_policy.lua is not in its generated layout')
    return policy, (head, tail, crlf)


def write_policy(policy, layout):
    head, tail, crlf = layout
    text = head + '[==[\n' + json.dumps(policy, indent=2, ensure_ascii=False) + '\n]==]' + tail
    POLICY.write_bytes((text.replace('\n', '\r\n') if crlf else text).encode('utf-8'))


def add_release(policy, record, url):
    key = record['release']
    alt = policy['releases'].get(policy.get('recommended'), {}).get('altUrl')
    entry = dict(record, url=url, **({'altUrl': alt} if alt else {}))
    if key in policy['releases']:
        if policy['releases'][key] != entry: raise SystemExit(f'native_policy.lua already has a different record for {key}')
        return policy, False
    updated = json.loads(json.dumps(policy))
    updated['releases'][key] = entry
    updated['approved'] = list(policy['approved']) + ([key] if key not in policy['approved'] else [])
    updated['recommended'] = key
    return updated, True


def evaluate(policy, key):
    """Every approved release must load on client and server without issues; the new one is the download."""
    sys.path.insert(0, str(ROOT / 'tests'))
    from lupa import LuaRuntime
    from lua_i18n import attach
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute('unpack=table.unpack; mmdhl={}; util={}; function include() return {} end')
    attach(lua); lua.execute('L=mmdhl.L')
    source = (ROOT / 'addon/lua/mmdhl/installation.lua').read_text(encoding='utf-8')
    lua.execute(source[:source.index('local policy=include')])
    def table(x):
        if isinstance(x, dict): return lua.table_from({k: table(v) for k, v in x.items()})
        if isinstance(x, list): return lua.table_from([table(v) for v in x])
        return x
    lua_policy = table(policy)
    for release in policy['approved']:
        record = policy['releases'][release]
        files = {'MOD/lua/bin/' + f['name']: dict(size=f['size'], sha256=f['sha256']) for f in record['files'].values()}
        runtime = record['files']['runtime']
        files['BASE_PATH/bin/win64/' + runtime['name']] = dict(size=runtime['size'], sha256=runtime['sha256'])
        for server in (False, True):
            reader = lambda path, search, *_: (table(files[search + '/' + path]), None) if search + '/' + path in files else (None, 'missing')
            status = lua.globals().mmdhl.EvaluateInstallation(lua_policy, reader, table(dict(server=server, dedicated=False, windows=True, arch='x64')))
            issues = [issue.code for issue in status.issues.values()]
            if not status.features.core or issues: raise SystemExit(f'{release} ({"server" if server else "client"}) fails the policy: {issues}')
            if release == key and status.download != policy['releases'][key]['url']: raise SystemExit(f'The download link is {status.download}')
    print('policy: every approved release loads;', key, 'is recommended')


def parse_gma(path):
    b = pathlib.Path(path).read_bytes()
    if b[:4] != b'GMAD': raise SystemExit(f'{path} is not a GMA')
    def text(o):
        e = b.index(b'\0', o); return b[o:e].decode('utf-8', 'replace'), e + 1
    version = b[4]; o = 5; steam_id, _ = struct.unpack_from('<QQ', b, o); o += 16
    required = []
    if version > 1:
        while True:
            s, o = text(o)
            if not s: break
            required.append(s)
    name, o = text(o); description, o = text(o); author, o = text(o)
    addon_version, = struct.unpack_from('<i', b, o); o += 4
    entries = []
    while True:
        n, = struct.unpack_from('<I', b, o); o += 4
        if n == 0: break
        path_, o = text(o); size, crc = struct.unpack_from('<qI', b, o); o += 12
        entries.append((path_, size, crc))
    files = {}
    for path_, size, crc in entries:
        data = b[o:o + size]; o += size
        if zlib.crc32(data) & 0xffffffff != crc: raise SystemExit(f'{path}: CRC mismatch in {path_}')
        files[path_] = data
    return dict(header=(version, steam_id, required, name, description, author, addon_version), files=files)


def game_tool(game_root, name):
    for candidate in (game_root / 'bin/win64' / name, game_root / 'bin' / name):
        if candidate.is_file(): return candidate
    raise SystemExit(f'{name} not found under {game_root}; pass --game-root')


def package_addon(game_root, work):
    out = work / 'addon.gma'
    out.unlink(missing_ok=True)
    run(game_tool(game_root, 'gmad.exe'), 'create', '-folder', ROOT / 'addon', '-out', out)
    old, new = parse_gma(GMA), parse_gma(out)
    if old['header'] != new['header']: raise SystemExit('addon.gma header (title, description, author) differs from the committed one')
    for path_, data in new['files'].items():
        if (ROOT / 'addon' / path_).read_bytes() != data: raise SystemExit(f'addon.gma: {path_} differs from the addon folder')
    changed = sorted(p for p in new['files'] if old['files'].get(p) != new['files'][p])
    removed = sorted(set(old['files']) - set(new['files']))
    print(f'addon.gma: {len(new["files"])} files; changed {changed}; removed {removed}')
    shutil.copyfile(out, GMA)


def publish(args):
    run_id, work = str(args.run), ROOT / 'build' / f'release-run-{args.run}'
    check_repository(args.dry_run)
    commit = check_run(run_id)
    record, packages = download(run_id, work / 'packages')
    label, build = record['release'], record['build']
    if not commit.startswith(build.split('-')[0]): raise SystemExit(f'The build {build} is not the run commit {commit}')
    url = f'{REPO_URL}/releases/tag/{label}'
    zips = {v: zip_package(d, work) for v, d in packages.items()}
    policy, layout = read_policy()
    alt = policy['releases'].get(policy.get('recommended'), {}).get('altUrl')
    notes = NOTES.format(label=label, whats_new=args.notes.read_text(encoding='utf-8').strip(), workshop=WORKSHOP, repo=REPO_URL, run=run_id, build=build,
                         mirror=f'\n中国大陆请访问 **[alternative download]({alt})**.\n' if alt else '',
                         zip_vulkan=zips['vulkan'].name, zip_remix=zips['opengl-remix'].name, sha_vulkan=sha256(zips['vulkan']), sha_remix=sha256(zips['opengl-remix']))
    (work / 'notes.md').write_text(notes, encoding='utf-8')
    updated, added = add_release(policy, record, url)
    print(f'{label} (build {build}, commit {commit[:12]}): packages verified; notes in {work / "notes.md"}')
    if args.dry_run:
        print(notes)
        print('policy:', 'unchanged' if not added else f'approved {updated["approved"]}, recommended {updated["recommended"]}')
        evaluate(updated, label)
        return
    existing = run('gh', 'release', 'view', label, '--repo', REPO, '--json', 'targetCommitish,assets', check=False)
    if existing.returncode == 0:
        info = json.loads(existing.stdout)
        if info['targetCommitish'] != commit or sorted(a['name'] for a in info['assets']) != sorted(z.name for z in zips.values()):
            raise SystemExit(f'Release {label} exists already for other files; it is left as it is')
        print(f'release {label} exists for this build; kept as it is')
    else:
        run('gh', 'release', 'create', label, '--repo', REPO, '--target', commit, '--title', f'Model Hotloader native {label}', '--notes-file', work / 'notes.md', *zips.values())
        assets = {a['name']: a['digest'] for a in json.loads(run('gh', 'release', 'view', label, '--repo', REPO, '--json', 'assets').stdout)['assets']}
        for z in zips.values():
            if assets.get(z.name) != 'sha256:' + sha256(z): raise SystemExit(f'{z.name} on the release does not match the local file')
        print('release created:', url)
    if added: write_policy(updated, layout)
    evaluate(read_policy()[0], label)
    for script in ('check-i18n.py', 'check-lua-tests.py'):
        result = run(sys.executable, ROOT / 'scripts' / script, check=False)
        print(result.stdout.strip().splitlines()[-1] if result.stdout.strip() else script)
        if result.returncode: raise SystemExit(f'{script} failed:\n{result.stdout}{result.stderr}')
    package_addon(args.game_root, work)
    run('git', 'add', POLICY, GMA)
    if not run('git', 'diff', '--cached', '--name-only').stdout.strip():
        print('nothing to commit'); return
    message = (f'Approve and recommend native release {label}\n\nRecord build {build} (Actions run {run_id},\nrelease {url})\n'
               'in native_policy.lua with the existing alternative download link, approve it\nand make it the recommended release; earlier approvals stay. Repackage\naddon.gma.\n')
    if args.commit_note: message += '\n' + args.commit_note.read_text(encoding='utf-8').strip() + '\n'
    if args.trailer: message += '\n' + '\n'.join(args.trailer) + '\n'
    (work / 'commit.txt').write_text(message, encoding='utf-8')
    run('git', 'commit', '-q', '-F', work / 'commit.txt')
    run('git', 'push', 'origin', 'main')
    print('pushed', run('git', 'log', '--oneline', '-1').stdout.strip(), '| addon.gma sha256', sha256(GMA))


def supersede(args):
    body = json.loads(run('gh', 'release', 'view', args.label, '--repo', REPO, '--json', 'body').stdout)['body']
    note = '\n'.join(('> ' + line) if line else '>' for line in args.note.read_text(encoding='utf-8').strip().splitlines())
    if body.startswith(note):
        print(args.label, 'already has this note'); return
    with tempfile.TemporaryDirectory() as folder:
        notes = pathlib.Path(folder) / 'notes.md'
        notes.write_text(note + '\n\n' + body, encoding='utf-8')
        run('gh', 'release', 'edit', args.label, '--repo', REPO, '--notes-file', notes)
    if not json.loads(run('gh', 'release', 'view', args.label, '--repo', REPO, '--json', 'body').stdout)['body'].startswith(note):
        raise SystemExit(f'The note did not reach {args.label}')
    print(args.label, 'noted')


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')  # the notes hold the mirror's Chinese line
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    p = commands.add_parser('publish')
    p.add_argument('--run', required=True, type=int, help='the "Build drop-in package" run to publish')
    p.add_argument('--notes', required=True, type=pathlib.Path, help="Markdown list of what's new")
    p.add_argument('--game-root', type=pathlib.Path, default=pathlib.Path(r'H:\SteamLibrary\steamapps\common\GarrysMod'), help='for gmad.exe')
    p.add_argument('--commit-note', type=pathlib.Path, help='paragraph added to the policy commit message')
    p.add_argument('--trailer', action='append', help='commit message trailer line')
    p.add_argument('--dry-run', action='store_true')
    s = commands.add_parser('supersede')
    s.add_argument('label'); s.add_argument('--note', required=True, type=pathlib.Path)
    args = parser.parse_args()
    (publish if args.command == 'publish' else supersede)(args)


if __name__ == '__main__':
    main()
