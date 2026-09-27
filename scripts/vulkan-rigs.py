"""Vulkan solver full-rig survey: every pinned eight-character model, lockstep
against cpu_mt_v2 (mmdhl_vulkan_compare), at the given iteration counts.

  python scripts/vulkan-rigs.py [--iterations 10 100] [--order ordered] [--steps 600] [--out validation/vulkan/rigs.json]
                                [--dxvk build-dxvk/src/d3d9/d3d9.dll]  (solve on the patched DXVK's shared queues)
"""
import argparse, json, os, pathlib, re, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parents[1]
CACHE = pathlib.Path(os.environ.get('MMDHL_ASSET_CACHE', r'H:/SteamLibrary/steamapps/common/GarrysMod/garrysmod/data/mmd_hotloader'))

def pinned():
    byHash = {}
    for f in (CACHE / 'assets').glob('*/manifest.json'):
        try: m = json.loads(f.read_text(encoding='utf-8-sig'))
        except (OSError, ValueError): continue
        byHash.setdefault(m.get('sourceHash'), []).append(f.parent.name)
    out = []
    for m in json.loads((ROOT / 'tests/v2-assets.json').read_text(encoding='utf-8')):
        ids = [m['asset']] if (CACHE / 'assets' / m['asset'] / 'manifest.json').is_file() else byHash.get(m['sourceHash'], [])
        if not ids: raise SystemExit(f"{m['label']} is not in the asset cache")
        out.append((m['label'], ids[0]))
    return out

def main():
    p = argparse.ArgumentParser(); p.add_argument('--iterations', type=int, nargs='+', default=[10, 100]); p.add_argument('--order', default='ordered')
    p.add_argument('--steps', type=int, default=600); p.add_argument('--out', default='validation/vulkan/rigs.json'); p.add_argument('--only', nargs='*'); p.add_argument('--dxvk')
    a = p.parse_args(); exe = ROOT / 'build/bin/Release/mmdhl_vulkan_compare.exe'; results = []
    tmp = ROOT / 'validation/vulkan'; tmp.mkdir(parents=True, exist_ok=True)
    for label, asset in pinned():
        if a.only and label not in a.only: continue
        for it in a.iterations:
            report = tmp / f'{label}-{it}-{a.order}.json'
            r = subprocess.run([str(exe), '--cache', str(CACHE), '--asset', asset, '--steps', str(a.steps), '--iterations', str(it), '--order', a.order, '--json', str(report)] + (['--dxvk', str(pathlib.Path(a.dxvk).resolve())] if a.dxvk else []), capture_output=True, text=True, encoding='utf-8', errors='replace')
            if r.returncode: print(label, it, 'FAILED', r.stderr.strip()[-300:], flush=True); results.append({'label': label, 'iterations': it, 'error': r.stderr.strip()[-300:]}); continue
            j = json.loads(report.read_text(encoding='utf-8')); c = j['computeB']
            context = re.search(r'vulkan=(\S+)', r.stdout)
            row = {'label': label, 'context': context.group(1) if context else None, 'bodies': j['bodies'], 'iterations': it, 'maxPosition': j['maxPosition'], 'maxRotation': j['maxRotation'], 'firstDivergedTick': j['firstDivergedTick'],
                   'cpuStepMs': j['stepMsA']['mean'], 'gpuStepMs': j['stepMsB']['mean'], 'kernelMs': c.get('kernelMs'), 'gpuWallMs': c.get('gpuWallMs'), 'setupMs': c.get('setupMs'), 'kernel': c.get('kernel'), 'solver': c.get('solver'), 'failure': c.get('failure')}
            results.append(row)
            print(f"{label:10s} it={it:3d} bodies={j['bodies']:4d} maxPos={j['maxPosition']:.3g} cpu={row['cpuStepMs']:.2f} gpu={row['gpuStepMs']:.2f} kernel={row['kernelMs'] or 0:.2f} {row['kernel']} {row['solver']} {row['context']} {row['failure'] or ''}", flush=True)
    (ROOT / a.out).write_text(json.dumps(results, indent=2), encoding='utf-8')

if __name__ == '__main__': main()
