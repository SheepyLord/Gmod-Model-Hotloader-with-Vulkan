"""Publish a native release from a finished "Build drop-in package" run, with the
Workshop policy that accepts it.

  python scripts/publish-native-release.py publish --run <id> --notes whats-new.md [--dry-run]
  python scripts/publish-native-release.py supersede <older-label> --note note.md

publish:
 1. The run must have succeeded on a commit that is on origin/main. origin must
    fetch from and push to this repository only, and the local main must equal
    origin/main, with no uncommitted changes and no untracked files in addon/.
    (Main may also be one unpushed publication commit of this release ahead: its
    push failed, and it is pushed.)
 2. Its two packages (-vulkan, -opengl-remix) are downloaded. Both must carry the
    same mmdhl-native-release.json, every native file must match that record, the
    Vulkan package's d3d9.dll its renderer record, and the other must have none.
    Each package is zipped with fixed file times and modes: a rerun makes the same
    bytes.
 3. The record is appended to addon/lua/mmdhl/native_policy.lua with the release
    link and the recommended release's alternative link, approved and made the
    recommended release. Earlier records and approvals stay. The policy is evaluated
    for every approved release (client and server must load without issues),
    check-i18n.py and check-lua-tests.py run, and addon.gma is built with gmad from
    the committed addon folder (files git does not track never reach it) and checked
    against it. If any of this fails, GitHub and the repository stay as they were.
 4. The GitHub release <label> is created at the run's commit with both zips.
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
VARIANTS = ('vulkan', 'opengl-remix')
SUBJECT = 'Approve and recommend native release '
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


def zip_package(folder, out, build):
    target = out / (folder.name + '.zip')
    # The build's time and one mode for every file: the same package makes the same
    # bytes, so a rerun can tell its own zips on a release from other files.
    stamp = re.search(r'-(\d{4})(\d\d)(\d\d)T(\d\d)(\d\d)(\d\d)Z$', build)
    when = tuple(int(x) for x in stamp.groups()) if stamp else (1980, 1, 1, 0, 0, 0)
    files = sorted((p.relative_to(folder).as_posix(), p) for p in folder.rglob('*') if p.is_file())
    with zipfile.ZipFile(target, 'w') as z:
        for name, p in files:
            info = zipfile.ZipInfo(name, when)
            info.compress_type, info.create_system, info.external_attr = zipfile.ZIP_DEFLATED, 3, 0o100644 << 16
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
        run('gh', 'release', 'create', label, '--repo', REPO, '--target', commit, '--title', f'Model Hotloader native {label}', '--notes-file', notes, *zips)
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
        raise SystemExit(f'Release {label} is not published at {commit} with both zips: draft {info and info["isDraft"]}, tag {tag}, assets {held}')


def push():
    run('git', 'push', 'origin', 'main')
    print('pushed', run('git', 'log', '--oneline', '-1').stdout.strip(), '| addon.gma sha256', sha256(GMA))


def publish(args):
    run_id, work = str(args.run), ROOT / 'build' / f'release-run-{args.run}'
    pending = check_repository(args.dry_run)
    commit = check_run(run_id)
    record, packages = download(run_id, work / 'packages')
    label, build = record['release'], record['build']
    if not commit.startswith(build.split('-')[0]): raise SystemExit(f'The build {build} is not the run commit {commit}')
    if pending and pending != label: raise SystemExit(f'main is the unpushed publication commit of {pending}, not {label}')
    url = f'{REPO_URL}/releases/tag/{label}'
    zips = [zip_package(packages[v], work, build) for v in VARIANTS]
    policy, layout = read_policy()
    alt = policy['releases'].get(policy.get('recommended'), {}).get('altUrl')
    notes = NOTES.format(label=label, whats_new=args.notes.read_text(encoding='utf-8').strip(), workshop=WORKSHOP, repo=REPO_URL, run=run_id, build=build,
                         mirror=f'\n中国大陆请访问 **[alternative download]({alt})**.\n' if alt else '',
                         zip_vulkan=zips[0].name, zip_remix=zips[1].name, sha_vulkan=sha256(zips[0]), sha_remix=sha256(zips[1]))
    (work / 'notes.md').write_text(notes, encoding='utf-8')
    updated, added = add_release(policy, record, url)
    print(f'{label} (build {build}, commit {commit[:12]}): packages verified; notes in {work / "notes.md"}')
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
    message = (f'{SUBJECT}{label}\n\nRecord build {build} (Actions run {run_id},\nrelease {url})\n'
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
