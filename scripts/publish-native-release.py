"""Publish a native release from a finished "Build drop-in package" run, with the
Workshop policy that records and recommends it (the addon runs native files it
does not know too, with a warning; the policy decides which run without one).

  python scripts/publish-native-release.py publish --run <id> --notes whats-new.md [--dry-run]
  python scripts/publish-native-release.py supersede <older-label> --note note.md

publish:
 1. The run must have succeeded on a commit that is on origin/main. origin must
    fetch from and push to this repository only, and the local main must equal
    origin/main, with no uncommitted changes and no untracked files in addon/.
    (Main may also be one unpushed publication commit of this release ahead: its
    push failed, and it is pushed.)
 2. Its packages are downloaded: Windows -win64-vulkan and -win64-opengl-remix, and
    Linux -linux64-vulkan and -linux64-opengl when the run built them. A platform's
    two packages must carry the same release record (mmdhl-native-release.json,
    on Linux mmdhl-native-release-linux64.json) of the same release and the run's
    commit, every native file must match that record, the Vulkan package's renderer
    (d3d9.dll; on Linux libtogl_client.so and libdxvk_d3d9.so) its renderer record,
    and the other package must have none. Each package is zipped with fixed file
    times and modes (the Linux programs and libraries executable): a rerun makes
    the same bytes.
 3. The record is appended to addon/lua/mmdhl/native_policy.lua with the release
    link and the recommended release's alternative link, approved and made the
    recommended release; the Linux record becomes its platforms.linux64, with the
    same links. Earlier records and approvals stay. The policy is evaluated for
    every approved release on every platform it records (on client and server its
    files must raise no issue, not even a warning), check-i18n.py and
    check-lua-tests.py run, and addon.gma is built with gmad from the committed
    addon folder (files git does not track never reach it) and checked against it.
    If any of this fails, GitHub and the repository stay as they were.
 4. The GitHub release <label> is created at the run's commit with all the zips.
    --notes holds the "What's new" list; the rest of the notes (requirements,
    packages, install, checksums) is standard. A release or tag is never replaced or
    deleted: an existing tag must name the run's commit and an existing release may
    hold only these zips, byte for byte. A release an interrupted publish left as a
    draft, or without a zip, gets the missing zip and is published.
 5. native_policy.lua and addon.gma (when its files changed) are committed and
    pushed to main. Should anything fail before the commit, both files are put back:
    a rerun then finds the release it published and completes the publication.

--dry-run does steps 1 to 3 without changing the repository (addon.gma is built in
the work folder), prints the notes and the policy change, and leaves GitHub alone.

supersede prepends a note (Markdown, shown as a quote) to an older release's
notes, once; its files stay as they are.

Uploading addon.gma to the Workshop and the packages to the mirror stays manual.
"""
import argparse, hashlib, io, json, pathlib, re, shutil, struct, subprocess, sys, tarfile, tempfile, zipfile, zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO = 'SheepyLord/Gmod-Model-Hotloader-with-Vulkan'
REPO_URL = 'https://github.com/' + REPO
WORKFLOW = 'Build drop-in package'
WORKSHOP = 'https://steamcommunity.com/sharedfiles/filedetails/?id=3810025467'
POLICY = ROOT / 'addon/lua/mmdhl/native_policy.lua'
GMA = ROOT / 'addon.gma'
# The packages of a run (Model-Hotloader-<release>-<commit>-<platform>-<variant>): Windows
# x64 always, Linux (the x86-64 branch) when the run built it. The first variant has the
# renderer.
PACKAGES = {'win64': ('vulkan', 'opengl-remix'), 'linux64': ('vulkan', 'opengl')}
# Each package's release record, beside the native files in garrysmod/lua/bin.
RECORD_FILES = {'win64': 'mmdhl-native-release.json', 'linux64': 'mmdhl-native-release-linux64.json'}
LUA_BIN = 'GarrysMod/garrysmod/lua/bin'
SUBJECT = 'Approve and recommend native release '
NOTES = '''Native files for **Model Hotloader**: {about}.

**What's new in {label}**
{whats_new}

Use it with the current Workshop version of the addon.

**Requirements**
- The Lua addon comes from the Steam Workshop: **[Model Hotloader]({workshop})**. Subscribe to it, then install one of the packages below.
{requirements}
{mirror}
## Which package

| Package | For |
|---|---|
{packages}

## Troubleshoot

{troubleshoot}

## Install

{install}

`INSTALL.txt` in each archive has the details (the Windows one's step 1 still names only the x86-64 beta; either 64-bit branch works). The licence notices are installed to {notices}; the game doesn't load them.

## Checksums

Build: {repo}/actions/runs/{run} ({builds})

SHA-256:
```
{checksums}
```
'''
# Each platform's parts of the notes; packages follow PACKAGES.
PLATFORM_NOTES = {
    'win64': dict(
        name='Windows',
        about="Windows x64 (the 64-bit Garry's Mod: the default branch's `gmod_win64.exe` or the x86-64 beta)",
        requirement='- Windows: the Microsoft Visual C++ x64 Redistributable.',
        packages=("**Windows, most PCs.** Also installs DXVK as `bin\\win64\\d3d9.dll`, so Garry's Mod renders through Vulkan. Needs a graphics driver with Vulkan 1.3.",
                  "**Windows with RTX Remix, ReShade or another `d3d9.dll`**, or PCs where DXVK doesn't work. Keeps the game's own Direct3D 9 renderer."),
        troubleshoot="- **Windows:** if the game crashes or draws wrongly with the Vulkan package (likely with little video memory or an older GPU), delete `bin\\win64\\d3d9.dll` in your Garry's Mod folder, for example `C:\\Program Files (x86)\\Steam\\steamapps\\common\\GarrysMod\\bin\\win64\\d3d9.dll`. The game then renders with its own Direct3D 9 again.",
        install='''**Windows**
1. Close Garry's Mod. Run it as 64-bit: the default branch's `gmod_win64.exe`, or the x86-64 beta (Steam > Garry's Mod > Properties > Betas).
2. Copy the `GarrysMod` folder from the archive onto your Garry's Mod folder, for example `C:\\Program Files (x86)\\Steam\\steamapps\\common\\GarrysMod`, and replace existing files.
3. Start the game and open **Q > External Models**. It confirms the installation or tells you what is missing.''',
        notices='`bin\\win64\\LICENSES` (Windows)'),
    'linux64': dict(
        name='Linux',
        about='Linux (the x86-64 branch)',
        requirement="- Linux: Garry's Mod's x86-64 branch. The default branch is 32-bit on Linux, which Model Hotloader does not support.",
        packages=("**Linux, most PCs.** Also replaces the game's `bin/linux64/libtogl_client.so` and adds DXVK's `libdxvk_d3d9.so`, so Garry's Mod renders through Vulkan. Needs a graphics driver with Vulkan 1.3.",
                  "**Linux, PCs where DXVK doesn't work.** Keeps the game's own OpenGL renderer."),
        troubleshoot="- **Linux:** if the game crashes or draws wrongly with the Vulkan package, verify the game files in Steam (Garry's Mod > Properties > Installed Files > Verify integrity of game files). That restores the game's own `libtogl_client.so`, and the game renders with OpenGL again. A Garry's Mod update that replaces that file does the same; copy the package again afterwards.",
        install='''**Linux**
1. Close Garry's Mod and switch it to the x86-64 branch (Steam > Garry's Mod > Properties > Betas > x86-64).
2. Copy the `GarrysMod` folder from the archive onto your Garry's Mod folder (Steam > Garry's Mod > Manage > Browse local files; usually `~/.local/share/Steam/steamapps/common/GarrysMod`), merging it with the folders there and replacing existing files.
3. Start the game and open **Q > External Models**. It confirms the installation or tells you what is missing.''',
        notices='`garrysmod/lua/bin/LICENSES` (Linux)'),
}
# The game each platform's records are evaluated in, and the folder (besides lua/bin) the
# game loads the runtime from; on Linux the modules load it from beside them.
GAMES = {'win64': (dict(windows=True, arch='x64'), 'BASE_PATH/bin/win64/'), 'linux64': (dict(linux=True, arch='x64'), None)}


def run(*cmd, check=True):
    result = subprocess.run([str(c) for c in cmd], cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace')
    if check and result.returncode:
        raise SystemExit(f'{" ".join(map(str, cmd))} failed ({result.returncode}):\n{result.stdout}{result.stderr}')
    return result


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def origin_repositories():
    """The GitHub repositories origin fetches from and pushes to (pushurl and
    insteadOf/pushInsteadOf rewrites applied)."""
    urls = run('git', 'remote', 'get-url', '--all', 'origin').stdout.split() + run('git', 'remote', 'get-url', '--push', '--all', 'origin').stdout.split()
    def repository(url):
        match = re.fullmatch(r'(?:https://github\.com/|git@github\.com:|ssh://git@github\.com/)([\w.-]+/[\w.-]+?)(?:\.git)?/?', url, re.I)
        return match.group(1) if match else url
    return sorted({repository(url) for url in urls})


def check_repository(dry_run):
    """The label of the unpushed publication commit main is, if it is one."""
    run('git', 'fetch', '-q', 'origin')
    origins = origin_repositories()
    branch = run('git', 'branch', '--show-current').stdout.strip()
    # gmad packages whatever the addon folder holds: untracked files there would
    # reach the Workshop without being in any commit.
    entries = iter(run('git', 'status', '--porcelain', '-z', '--untracked-files=all').stdout.split('\0'))
    dirty, untracked = [], []
    for entry in entries:
        if not entry: continue
        code, path = entry[:2], entry[3:]
        if code[0] in 'RC': next(entries, None)  # a rename's or copy's original path follows
        if code != '??': dirty.append(path)
        elif path.startswith('addon/'): untracked.append(path)
    head, remote = (run('git', 'rev-parse', ref).stdout.strip() for ref in ('HEAD', 'origin/main'))
    pending = None
    if head != remote and run('git', 'rev-parse', 'HEAD^', check=False).stdout.strip() == remote:
        # One publication commit on origin/main whose push failed may be pushed again.
        subject = run('git', 'log', '-1', '--format=%s').stdout.strip()
        changed = set(run('git', 'diff', '--name-only', 'HEAD^', 'HEAD').stdout.split())
        if subject.startswith(SUBJECT) and changed <= {POLICY.relative_to(ROOT).as_posix(), GMA.name}:
            pending = subject[len(SUBJECT):]
    problems = [p for p, bad in ((f'origin is {", ".join(origins)}, not {REPO}', [o.lower() for o in origins] != [REPO.lower()]),
                                 (f'on branch {branch}, not main', branch != 'main'), ('uncommitted changes: ' + ', '.join(dirty), dirty),
                                 ('untracked files in addon/: ' + ', '.join(untracked), untracked),
                                 ('main differs from origin/main', head != remote and not pending)) if bad]
    if problems and not dry_run:
        raise SystemExit('The repository must be main, clean and equal to origin/main: ' + '; '.join(problems))
    for p in problems: print('note:', p)
    if pending: print(f'note: main is the unpushed publication commit of {pending}')
    return pending


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
    """The release record of each platform the run built ({platform: record}) and its
    packages ({platform: {variant: folder}}), checked against that record."""
    shutil.rmtree(work, ignore_errors=True); work.mkdir(parents=True)
    run('gh', 'run', 'download', run_id, '--repo', REPO, '--dir', work)
    found = {}
    for d in sorted(p for p in work.iterdir() if p.is_dir()):
        names = [(platform, v) for platform, variants in PACKAGES.items() for v in variants if d.name.endswith(f'-{platform}-{v}')]
        if len(names) != 1: raise SystemExit(f'run {run_id} has an artifact that is no known package: {d.name}')
        found.setdefault(names[0][0], {})[names[0][1]] = d
    for platform, variants in PACKAGES.items():
        if (platform == 'win64' or platform in found) and sorted(found.get(platform, {})) != sorted(variants):
            raise SystemExit(f'run {run_id} lacks a {platform} package: found {sorted(p.name for p in work.iterdir())}')
    packages = {platform: {v: found[platform][v] for v in variants} for platform, variants in PACKAGES.items() if platform in found}
    return {platform: check_packages(platform, items) for platform, items in packages.items()}, packages


def check_packages(platform, packages):
    """A platform's two packages carry one record and every file it lists; only the
    -vulkan one has the renderer."""
    vulkan, plain = PACKAGES[platform]
    records = {v: json.loads((d / LUA_BIN / RECORD_FILES[platform]).read_text(encoding='utf-8')) for v, d in packages.items()}
    record = records[vulkan]
    if records[plain] != record: raise SystemExit(f'The {platform} packages carry different release records')
    if record.get('installApi') != 1: raise SystemExit(f'Unsupported installApi {record.get("installApi")}')
    if record.get('platform') != platform: raise SystemExit(f'The {platform} packages carry a record of platform {record.get("platform")}')
    if not record.get('renderer'): raise SystemExit(f'The {platform} record names no renderer for the -{vulkan} package')
    for v, d in packages.items():
        for key, f in record['files'].items(): check_file(d / LUA_BIN / f['name'], f, f'{platform}-{v} {key}')
    if platform == 'win64':
        for v, d in packages.items():
            runtime = record['files']['runtime']
            check_file(d / 'GarrysMod/bin/win64' / runtime['name'], runtime, f'{v} bin/win64 runtime')
        check_file(packages[vulkan] / 'GarrysMod/bin/win64/d3d9.dll', record['renderer'], 'vulkan d3d9.dll')
        if (packages[plain] / 'GarrysMod/bin/win64/d3d9.dll').exists(): raise SystemExit('The opengl-remix package carries a d3d9.dll')
        return record
    # lib_coacd.so loads the OpenMP runtime its wheel bundles from beside it; the record
    # does not list it, so both packages must carry the same one.
    openmp = {v: [(p.name, p.read_bytes()) for p in sorted((d / LUA_BIN).glob('libgomp*.so*'))] for v, d in packages.items()}
    if not openmp[vulkan] or openmp[plain] != openmp[vulkan]: raise SystemExit(f"The {platform} packages lack CoACD's OpenMP runtime or carry different ones")
    # The game loads ToGL from bin/linux64; the Vulkan package's replacement loads DXVK from beside it.
    if not record['renderer'].get('library'): raise SystemExit(f"The {platform} renderer record names no DXVK library")
    check_file(packages[vulkan] / 'GarrysMod/bin/linux64/libtogl_client.so', record['renderer'], f'{platform}-vulkan libtogl_client.so')
    check_file(packages[vulkan] / 'GarrysMod/bin/linux64/libdxvk_d3d9.so', record['renderer']['library'], f'{platform}-vulkan libdxvk_d3d9.so')
    if (packages[plain] / 'GarrysMod/bin/linux64').exists(): raise SystemExit(f'The {platform}-{plain} package carries a renderer')
    return record


def executable(name):
    """The programs and libraries of a Linux package, which package-dropin.py makes
    executable (artifact zips lose the mode): the native files in garrysmod/lua/bin and
    the renderer's libraries in bin/linux64."""
    folder, _, file = name.rpartition('/')
    return (folder == LUA_BIN and not file.endswith('.json')) or (folder == 'GarrysMod/bin/linux64' and '.so' in file)


def zip_package(folder, out, build, linux=False):
    target = out / (folder.name + '.zip')
    # The build's time and fixed modes: the same package makes the same bytes, so a
    # rerun can tell its own zips on a release from other files.
    stamp = re.search(r'-(\d{4})(\d\d)(\d\d)T(\d\d)(\d\d)(\d\d)Z$', build)
    when = tuple(int(x) for x in stamp.groups()) if stamp else (1980, 1, 1, 0, 0, 0)
    files = sorted((p.relative_to(folder).as_posix(), p) for p in folder.rglob('*') if p.is_file())
    with zipfile.ZipFile(target, 'w') as z:
        for name, p in files:
            info = zipfile.ZipInfo(name, when)
            mode = 0o100755 if linux and executable(name) else 0o100644
            info.compress_type, info.create_system, info.external_attr = zipfile.ZIP_DEFLATED, 3, mode << 16
            z.writestr(info, p.read_bytes(), compresslevel=9)
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


def policy_bytes(policy, layout):
    head, tail, crlf = layout
    text = head + '[==[\n' + json.dumps(policy, indent=2, ensure_ascii=False) + '\n]==]' + tail
    return (text.replace('\n', '\r\n') if crlf else text).encode('utf-8')


def add_release(policy, record, url, platforms=None):
    """The policy with the release of this Windows record approved and recommended, the
    records of other platforms ({platform: record}) as its platforms table, each with the
    release link and the recommended release's alternative link; unchanged when it holds
    this release already."""
    key = record['release']
    alt = policy['releases'].get(policy.get('recommended'), {}).get('altUrl')
    links = dict(url=url, **({'altUrl': alt} if alt else {}))
    entry = dict(record, **links)
    if platforms: entry['platforms'] = {platform: dict(other, **links) for platform, other in platforms.items()}
    if key in policy['releases']:
        if policy['releases'][key] != entry: raise SystemExit(f'native_policy.lua already has a different record for {key}')
        return policy, False
    updated = json.loads(json.dumps(policy))
    updated['releases'][key] = entry
    updated['approved'] = list(policy['approved']) + ([key] if key not in policy['approved'] else [])
    updated['recommended'] = key
    return updated, True


def evaluate(policy, key):
    """Every approved release must raise no issue, not even a warning, on client and server of
    every platform it records; the new one is the download."""
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
    evaluated = set()
    for release in policy['approved']:
        entry = policy['releases'][release]
        # A release's Windows record is the entry itself; other platforms' are in its platforms table.
        for platform, record in [('win64', entry)] * ('files' in entry) + list((entry.get('platforms') or {}).items()):
            if platform not in GAMES: raise SystemExit(f'{release} records platform {platform}, which this script cannot evaluate')
            game, runtime_folder = GAMES[platform]
            files = {'MOD/lua/bin/' + f['name']: dict(size=f['size'], sha256=f['sha256']) for f in record['files'].values()}
            runtime = record['files']['runtime']
            if runtime_folder: files[runtime_folder + runtime['name']] = dict(size=runtime['size'], sha256=runtime['sha256'])
            for server in (False, True):
                reader = lambda path, search, *_: (table(files[search + '/' + path]), None) if search + '/' + path in files else (None, 'missing')
                status = lua.globals().mmdhl.EvaluateInstallation(lua_policy, reader, table(dict(game, server=server, dedicated=False)))
                issues = [issue.code for issue in status.issues.values()]
                if not status.features.core or issues: raise SystemExit(f'{release} ({platform} {"server" if server else "client"}) fails the policy: {issues}')
                if release == key and status.download != record['url']: raise SystemExit(f'The {platform} download link is {status.download}')
            evaluated.add(platform)
    print(f'policy: every approved release loads ({", ".join(sorted(evaluated))});', key, 'is recommended')


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


def package_addon(game_root, work, policy):
    """addon.gma of the committed addon folder with this policy (files git does not
    track never reach it), or None when it holds the committed addon.gma's files: then
    those bytes stay, with their timestamp."""
    tree = work / 'addon'
    shutil.rmtree(tree, ignore_errors=True)
    archive = subprocess.run(['git', 'archive', '--format=tar', 'HEAD', 'addon'], cwd=ROOT, capture_output=True, check=True).stdout
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar: tar.extractall(work, filter='data')
    (tree / POLICY.relative_to(ROOT / 'addon')).write_bytes(policy)
    out = work / 'addon.gma'
    out.unlink(missing_ok=True)
    run(game_tool(game_root, 'gmad.exe'), 'create', '-folder', tree, '-out', out)
    old, new = parse_gma(GMA), parse_gma(out)
    if old['header'] != new['header']: raise SystemExit('addon.gma header (title, description, author) differs from the committed one')
    for path_, data in new['files'].items():
        if (tree / path_).read_bytes() != data: raise SystemExit(f'addon.gma: {path_} differs from the addon folder')
    changed = sorted(p for p in new['files'] if old['files'].get(p) != new['files'][p])
    removed = sorted(set(old['files']) - set(new['files']))
    print(f'addon.gma: {len(new["files"])} files; changed {changed}; removed {removed}')
    return out if changed or removed else None


def remote_tag(label):
    """The commit the repository's tag <label> names, or None."""
    result = run('gh', 'api', f'repos/{REPO}/git/ref/tags/{label}', check=False)
    if result.returncode:
        if 'HTTP 404' in result.stderr: return None
        raise SystemExit(f'Reading tag {label} failed:\n{result.stdout}{result.stderr}')
    target = json.loads(result.stdout)['object']
    if target['type'] == 'tag': target = json.loads(run('gh', 'api', f'repos/{REPO}/git/tags/{target["sha"]}').stdout)['object']
    return target['sha']


def view_release(label):
    result = run('gh', 'release', 'view', label, '--repo', REPO, '--json', 'isDraft,targetCommitish,assets', check=False)
    if result.returncode == 0: return json.loads(result.stdout)
    if 'release not found' in (result.stdout + result.stderr).lower(): return None
    raise SystemExit(f'Reading release {label} failed:\n{result.stdout}{result.stderr}')


def ensure_release(label, commit, zips, notes):
    """The published release <label> at the run commit with exactly these zips: created,
    or completed where an interrupted publish left it a draft or without a zip."""
    expected = {z.name: 'sha256:' + sha256(z) for z in zips}
    tag = remote_tag(label)
    # gh release create --target only applies to a new tag.
    if tag and tag != commit: raise SystemExit(f'Tag {label} names {tag}, not the run commit {commit}; it is left as it is')
    info = view_release(label)
    if info is None:
        run('gh', 'release', 'create', label, '--repo', REPO, '--target', commit, '--title', f'Model Hotloader {label}', '--notes-file', notes, *zips)
        print('release created:', f'{REPO_URL}/releases/tag/{label}')
    else:
        assets = {a['name']: a.get('digest') for a in info['assets']}
        other = sorted(name for name, digest in assets.items() if expected.get(name) != digest)
        if other or (not tag and info['targetCommitish'] != commit):
            raise SystemExit(f'Release {label} exists already for other files or another commit ({", ".join(other) or info["targetCommitish"]}); it is left as it is')
        missing = [z for z in zips if z.name not in assets]
        if missing: run('gh', 'release', 'upload', label, '--repo', REPO, *missing)
        if info['isDraft']: run('gh', 'release', 'edit', label, '--repo', REPO, '--draft=false')
        print(f'release {label}:', 'completed and published' if missing or info['isDraft'] else 'exists for this build; kept as it is')
    info, tag = view_release(label), remote_tag(label)
    held = {a['name']: a.get('digest') for a in info['assets']} if info else {}
    if not info or info['isDraft'] or tag != commit or held != expected:
        raise SystemExit(f'Release {label} is not published at {commit} with all its zips: draft {info and info["isDraft"]}, tag {tag}, assets {held}')


def push():
    run('git', 'push', 'origin', 'main')
    print('pushed', run('git', 'log', '--oneline', '-1').stdout.strip(), '| addon.gma sha256', sha256(GMA))


def compose_notes(label, whats_new, run_id, records, zips, alt):
    """The release notes: the "What's new" list, then the standard parts for the platforms
    the run built (records: {platform: record}; zips: {(platform, variant): zip})."""
    parts = [PLATFORM_NOTES[platform] for platform in records]
    return NOTES.format(label=label, whats_new=whats_new, workshop=WORKSHOP, repo=REPO_URL, run=run_id,
                        about=' and '.join(p['about'] for p in parts), requirements='\n'.join(p['requirement'] for p in parts),
                        mirror=f'\n中国大陆请访问 **[alternative download]({alt})**.\n' if alt else '',
                        packages='\n'.join(f'| `{z.name}` | {PLATFORM_NOTES[p]["packages"][PACKAGES[p].index(v)]} |' for (p, v), z in zips.items()),
                        troubleshoot='\n'.join(p['troubleshoot'] for p in parts), install='\n\n'.join(p['install'] for p in parts),
                        notices=' and '.join(p['notices'] for p in parts),
                        builds=', '.join(f'{PLATFORM_NOTES[p]["name"]} `{r["build"]}`' for p, r in records.items()),
                        checksums='\n'.join(f'{sha256(z)}  {z.name}' for z in zips.values()))


def publish(args):
    run_id, work = str(args.run), ROOT / 'build' / f'release-run-{args.run}'
    pending = check_repository(args.dry_run)
    commit = check_run(run_id)
    records, packages = download(run_id, work / 'packages')
    record = records['win64']
    label, build = record['release'], record['build']
    for platform, other in records.items():
        if other['release'] != label: raise SystemExit(f'The {platform} packages are release {other["release"]}, not {label}')
        if not commit.startswith(other['build'].split('-')[0]): raise SystemExit(f'The {platform} build {other["build"]} is not the run commit {commit}')
    if pending and pending != label: raise SystemExit(f'main is the unpushed publication commit of {pending}, not {label}')
    url = f'{REPO_URL}/releases/tag/{label}'
    zips = {(platform, v): zip_package(packages[platform][v], work, records[platform]['build'], platform != 'win64') for platform in packages for v in PACKAGES[platform]}
    policy, layout = read_policy()
    alt = policy['releases'].get(policy.get('recommended'), {}).get('altUrl')
    notes = compose_notes(label, args.notes.read_text(encoding='utf-8').strip(), run_id, records, zips, alt)
    (work / 'notes.md').write_text(notes, encoding='utf-8')
    zips = list(zips.values())
    updated, added = add_release(policy, record, url, {platform: other for platform, other in records.items() if platform != 'win64'})
    builds = ', '.join(f'{platform} {other["build"]}' for platform, other in records.items())
    print(f'{label} (builds {builds}, commit {commit[:12]}): packages verified; notes in {work / "notes.md"}')
    if args.dry_run:
        print(notes)
        print('policy:', 'unchanged' if not added else f'approved {updated["approved"]}, recommended {updated["recommended"]}')
        evaluate(updated, label)
        package_addon(args.game_root, work, policy_bytes(updated, layout))
        return
    if pending:
        # The publication commit exists; only its push failed.
        if added: raise SystemExit(f'The unpushed publication commit lacks the record of {label}')
        evaluate(policy, label)
        if package_addon(args.game_root, work, POLICY.read_bytes()): raise SystemExit('The unpushed addon.gma differs from its addon folder')
        ensure_release(label, commit, zips, work / 'notes.md')
        push()
        return
    # Everything local first: a failing check or a missing gmad.exe must not leave a
    # release without its policy. Until the commit is made, a failure puts both files
    # back, so a rerun finds a clean main and the release it may have published.
    others = ''.join(f'\nand its {PLATFORM_NOTES[platform]["name"]} build {other["build"]} (platforms.{platform})' for platform, other in records.items() if platform != 'win64')
    message = (f'{SUBJECT}{label}\n\nRecord build {build}{others}\n(Actions run {run_id}, release {url})\n'
               'in native_policy.lua with the existing alternative download link, approve it\nand make it the recommended release; earlier approvals stay. Repackage\naddon.gma.\n')
    if args.commit_note: message += '\n' + args.commit_note.read_text(encoding='utf-8').strip() + '\n'
    if args.trailer: message += '\n' + '\n'.join(args.trailer) + '\n'
    (work / 'commit.txt').write_text(message, encoding='utf-8')
    original = POLICY.read_bytes(), GMA.read_bytes()
    try:
        if added: POLICY.write_bytes(policy_bytes(updated, layout))
        evaluate(read_policy()[0], label)
        for script in ('check-i18n.py', 'check-lua-tests.py'):
            result = run(sys.executable, ROOT / 'scripts' / script, check=False)
            print(result.stdout.strip().splitlines()[-1] if result.stdout.strip() else script)
            if result.returncode: raise SystemExit(f'{script} failed:\n{result.stdout}{result.stderr}')
        gma = package_addon(args.game_root, work, POLICY.read_bytes())
        if gma: shutil.copyfile(gma, GMA)
        ensure_release(label, commit, zips, work / 'notes.md')
        run('git', 'add', POLICY, GMA)
        if not run('git', 'diff', '--cached', '--name-only').stdout.strip():
            print('nothing to commit'); return
        run('git', 'commit', '-q', '-F', work / 'commit.txt')
    except BaseException:
        run('git', 'reset', '-q', '--', POLICY, GMA, check=False)
        POLICY.write_bytes(original[0]); GMA.write_bytes(original[1])
        raise
    push()


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
