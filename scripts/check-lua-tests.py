"""Run the offline regression scripts (tests/test_*.py): the addon's Lua against
simulated games through lupa (pip install lupa==2.8), plus the Python checks.

  python scripts/check-lua-tests.py            run every script
  python scripts/check-lua-tests.py names      run the scripts whose name contains "names"

tests/test_native_installer.py needs a built release (build/bin/Release) and runs
after the native build instead. Exit status is 1 when any script fails.
"""
import pathlib, subprocess, sys, time

ROOT = pathlib.Path(__file__).resolve().parents[1]
NEEDS_BUILD = {'test_native_installer.py'}


def main():
    wanted = sys.argv[1:]
    scripts = [p for p in sorted((ROOT / 'tests').glob('test_*.py')) if p.name not in NEEDS_BUILD and (not wanted or any(w in p.name for w in wanted))]
    failed = []
    for script in scripts:
        started = time.perf_counter()
        result = subprocess.run([sys.executable, '-B', str(script)], cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace')
        took = time.perf_counter() - started
        print(('ok    ' if result.returncode == 0 else 'FAIL  ') + f'{script.name} ({took:.1f} s)', flush=True)
        if result.returncode:
            failed.append(script.name)
            print(''.join(('    ' + line) for line in (result.stdout + result.stderr).splitlines(True)[-25:]), flush=True)
    print(f'{len(scripts) - len(failed)} of {len(scripts)} scripts passed')
    return 1 if failed or not scripts else 0


if __name__ == '__main__':
    sys.exit(main())
