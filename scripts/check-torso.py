"""Read-only torso check of the fitter (issue #9) over a model folder.

Runs mmdhl_worker --fit (the carrier manifest) and --inspect (the PMX bone
hierarchy) on every model and counts:
  inverted       Spine2 sits above Spine4 (the chest lies on the ground in a ragdoll)
  order          any break of Spine1 <= Spine2 < Spine4 < Neck1 (heights, Source Z)
  double-mapped  a PMX bone driven by two carrier bones (mmd or mmdAliases)
  chest          the Spine4 bone (or a bone moving with it) does not hold the neck and both shoulders
  aliased        a torso bone moves with a synthesized Spine2/Spine4 (torso repairs "band"/"coincident")
Models the fitter rejects for missing landmarks are counted as not humanoid.
Nothing is written next to the models; --json writes the per-model report where you say.

  python scripts/check-torso.py <model or folder>... [--worker exe] [--glob PATTERN] [--sample N] [--jobs N] [--json out.json] [--list]

Exit code 1 when a fitted model is inverted, double-mapped or has such a chest.
"""
import argparse, collections, concurrent.futures, json, pathlib, subprocess, sys, time
ROOT = pathlib.Path(__file__).resolve().parents[1]
VB = 'ValveBiped.Bip01_'
CHAIN = ['Spine1', 'Spine2', 'Spine4', 'Neck1']
EXTENSIONS = {'.pmx', '.pmd', '.vrm'}


def run(worker, flag, path):
    r = subprocess.run([str(worker), flag, str(path)], capture_output=True, timeout=300, encoding='utf-8', errors='replace')
    text = r.stdout if r.returncode == 0 else r.stderr
    try:
        return r.returncode == 0, json.loads(text.strip().splitlines()[-1] if text.strip() else '{}')
    except ValueError:
        return False, {'error': text.strip()[-400:] or 'exit code %d' % r.returncode}


def check(worker, path):
    started = time.perf_counter()
    out = {'path': str(path)}
    try:
        ok, fit = run(worker, '--fit', path)
        if not ok:
            out['skip' if fit.get('errorCode') == 'fit.landmarks' else 'error'] = fit.get('error', 'fit failed')
            return out
        ok, info = run(worker, '--inspect', path)
        if not ok:
            out['error'] = info.get('error', 'inspect failed')
            return out
        pmx = info['boneList']
        bones = {b['name']: b for b in fit['bones']}
        name = lambda i: pmx[i]['name'] if 0 <= i < len(pmx) else '-'
        def ancestors(i):
            seen, p = set(), pmx[i]['parent'] if 0 <= i < len(pmx) else -1
            while 0 <= p < len(pmx) and p not in seen:
                seen.add(p); p = pmx[p]['parent']
            return seen
        z = {k: bones[VB + k]['position'][2] for k in ['Pelvis'] + CHAIN}
        tolerance = 1e-3
        out['inverted'] = z['Spine2'] > z['Spine4'] + tolerance
        # Spine2 may share Spine1's height; Spine4 and Neck1 must be strictly higher.
        out['order'] = [f'{a}>={b}' for a, b in zip(CHAIN, CHAIN[1:]) if (z[b] < z[a] - tolerance if a == 'Spine1' else z[b] <= z[a])]
        out['pelvisAboveSpine1'] = z['Pelvis'] > z['Spine1'] + tolerance
        owners = collections.defaultdict(list)
        for b in fit['bones']:
            for i in ([b['mmd']] if b['mmd'] >= 0 else []) + b.get('mmdAliases', []):
                owners[i].append(b['name'])
        out['double'] = {name(i): o for i, o in owners.items() if len(o) > 1}
        chest = bones[VB + 'Spine4']
        holders = ([chest['mmd']] if chest['mmd'] >= 0 else []) + chest.get('mmdAliases', [])
        anchors = []
        for k, fallback in (('Neck1', 'Head1'), ('L_Clavicle', 'L_UpperArm'), ('R_Clavicle', 'R_UpperArm')):
            i = bones[VB + k]['mmd'] if bones[VB + k]['mmd'] >= 0 else bones[VB + fallback]['mmd']
            if i >= 0: anchors.append(i)
        out['chestSynthesized'] = not holders
        out['chestHolds'] = (not holders) or all(any(h in ancestors(a) for h in holders) for a in anchors)
        torso = fit.get('torso', {})
        out['method'] = torso.get('method', '')
        out['repairs'] = [r.get('code', '') if isinstance(r, dict) else str(r) for r in torso.get('repairs', [])]
        out['repairText'] = [r.get('text', '') if isinstance(r, dict) else str(r) for r in torso.get('repairs', [])]
        out['aliased'] = any(c in ('band', 'coincident') for c in out['repairs']) if torso else bool(bones[VB + 'Spine2'].get('mmdAliases') or chest.get('mmdAliases'))
        def mapped(k):
            b = bones[VB + k]
            text = name(b['mmd']) if b['mmd'] >= 0 else '(synth)'
            return text + (' +' + '+'.join(name(a) for a in b['mmdAliases']) if b.get('mmdAliases') else '')
        out['mapping'] = {k: mapped(k) for k in ['Pelvis'] + CHAIN}
        out['z'] = {k: round(v, 2) for k, v in z.items()}
        out['all'] = {b['name']: [b['mmd']] + b.get('mmdAliases', []) for b in fit['bones']}
    except Exception as e:  # noqa: BLE001 - one broken file must not end the scan
        out['error'] = f'{type(e).__name__}: {e}'
    finally:
        out['seconds'] = round(time.perf_counter() - started, 2)
    return out


def models(args):
    found = []
    for item in args.paths:
        p = pathlib.Path(item)
        if p.is_file(): found.append(p)
        else: found += sorted(q for q in p.glob(args.glob) if q.is_file() and q.suffix.lower() in EXTENSIONS)
    if args.sample and len(found) > args.sample:
        step = len(found) / args.sample
        found = [found[int(k * step)] for k in range(args.sample)]
    return found


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('paths', nargs='+')
    p.add_argument('--worker', type=pathlib.Path, default=ROOT / 'build/bin/Release/mmdhl_worker.exe')
    p.add_argument('--glob', default='**/*', help="pattern inside each folder, e.g. '*/1_PMX/**/*.pmx'")
    p.add_argument('--sample', type=int, default=0, help='check at most N models, evenly spread')
    p.add_argument('--jobs', type=int, default=6)
    p.add_argument('--json', type=pathlib.Path)
    p.add_argument('--list', action='store_true', help='print the torso mapping of every model')
    args = p.parse_args()
    paths = models(args)
    results, started = [], time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for r in pool.map(lambda path: check(args.worker, path), paths):
            results.append(r)
            if len(results) % 50 == 0: print(len(results), '/', len(paths), flush=True, file=sys.stderr)
    fitted = [r for r in results if 'mapping' in r]
    count = lambda key: sum(bool(r.get(key)) for r in fitted)
    summary = {'models': len(results), 'fitted': len(fitted), 'notHumanoid': sum('skip' in r for r in results), 'errors': sum('error' in r for r in results),
               'inverted': count('inverted'), 'order': count('order'), 'doubleMapped': count('double'),
               'spine2EqualsSpine4': sum(r['all'][VB + 'Spine2'][0] >= 0 and r['all'][VB + 'Spine2'][0] == r['all'][VB + 'Spine4'][0] for r in fitted),
               'chestMisplaced': sum(not r['chestHolds'] for r in fitted), 'chestSynthesized': count('chestSynthesized'), 'aliased': count('aliased'),
               'pelvisAboveSpine1': count('pelvisAboveSpine1'),
               'methods': dict(collections.Counter(r['method'] or '(none)' for r in fitted)),
               'repairs': dict(collections.Counter(c for r in fitted for c in r['repairs'])), 'seconds': round(time.time() - started, 1)}
    for r in results:
        problems = [k for k in ('inverted', 'order', 'double') if r.get(k)] + ([] if r.get('chestHolds', True) else ['chest'])
        if args.list or problems or 'error' in r:
            line = r['path'] + ('  ' + ','.join(problems) if problems else '')
            if 'error' in r: line += '  ERROR ' + r['error']
            elif 'mapping' in r: line += '\n    ' + ' | '.join(f'{k}={v}' for k, v in r['mapping'].items()) + (f"\n    double: {r['double']}" if r['double'] else '') + ''.join('\n    repair: ' + t for t in r['repairText'])
            print(line)
    print(json.dumps(summary, ensure_ascii=False))
    if args.json: args.json.write_text(json.dumps({'summary': summary, 'models': results}, ensure_ascii=False, indent=1), encoding='utf-8')
    return 1 if summary['inverted'] or summary['doubleMapped'] or summary['chestMisplaced'] else 0


if __name__ == '__main__':
    sys.exit(main())
