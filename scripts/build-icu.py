"""Build ICU for Linux as static libraries holding only transliteration and normalization data.

native/naming.cpp turns model and material names into engine paths with ICU's
"Any-Latin; Latin-ASCII" transliterator, as Windows' own ICU does on Windows: those
paths are part of a carrier's identity, so Linux and Windows games must agree on them.
The Steam runtime has no ICU, so the Linux binaries carry their own (about 3 MB).

    python scripts/build-icu.py --platform linux64   (or linux: 32-bit, built with -m32)

The result lands in vendor/icu-<platform> (include/, lib/); it is reused while the pinned
version and this script are unchanged.
"""
import argparse, hashlib, io, json, os, pathlib, shutil, subprocess, sys, tarfile, urllib.request, zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
# What the transliterator needs: its rule bundles and the normalization forms they apply.
FILTER = {"strategy": "additive", "featureFilters": {"translit": "include", "normalization": "include"}}


def fetch(spec):
    request = urllib.request.Request(spec['url'], headers={'User-Agent': 'ModelHotloader-build'})
    with urllib.request.urlopen(request, timeout=300) as response:
        blob = response.read()
    if hashlib.sha256(blob).hexdigest() != spec['sha256']:
        raise RuntimeError('ICU download checksum mismatch: ' + spec['url'])
    return blob


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--platform', choices=['linux64', 'linux'], required=True)
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    args = parser.parse_args()
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text())['icu']
    identity = hashlib.sha256((json.dumps(lock, sort_keys=True) + (pathlib.Path(__file__).read_text())).encode()).hexdigest()
    out = ROOT / 'vendor' / ('icu-' + args.platform)
    marker = out / '.mmdhl-revision'
    if marker.is_file() and marker.read_text() == identity and (out / 'lib' / 'libicui18n.a').is_file():
        print('icu-' + args.platform + ': ready')
        return
    work = ROOT / 'vendor' / 'icu-build' / args.platform
    shutil.rmtree(work, ignore_errors=True)
    shutil.rmtree(out, ignore_errors=True)
    work.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(fetch(lock['source'])), mode='r:gz') as archive:
        archive.extractall(work, filter='data')
    # Filtering needs the data as source (the source archive carries it prebuilt).
    data = work / 'icu' / 'source' / 'data'
    shutil.rmtree(data)
    with zipfile.ZipFile(io.BytesIO(fetch(lock['data']))) as archive:
        archive.extractall(work / 'data-zip')
    shutil.move(str(work / 'data-zip' / 'data'), str(data))
    (work / 'filter.json').write_text(json.dumps(FILTER))
    flags = '-O2 -fPIC -fvisibility=hidden'
    if args.platform == 'linux':
        # As CMakeLists.txt builds the 32-bit files: SSE2 math, initial-exec TLS.
        flags += ' -m32 -msse2 -mfpmath=sse -ftls-model=initial-exec'
    env = dict(os.environ, ICU_DATA_FILTER_FILE=str(work / 'filter.json'), CFLAGS=flags, CXXFLAGS=flags + ' -std=c++17', LDFLAGS='-m32' if args.platform == 'linux' else '')
    source = work / 'icu' / 'source'
    configure = ['./runConfigureICU', 'Linux', '--enable-static', '--disable-shared', '--disable-tests', '--disable-samples',
                 '--disable-extras', '--disable-icuio', '--disable-layoutex', '--with-data-packaging=static', '--prefix=' + str(out)]
    # 32-bit: -m32 above, built as a native build (no --host), so ICU's own data tools are
    # 32-bit programs that the 64-bit build machine runs.
    subprocess.run(configure, cwd=source, env=env, check=True)
    subprocess.run(['make', '-j', str(args.jobs)], cwd=source, env=env, check=True)
    subprocess.run(['make', 'install'], cwd=source, env=env, check=True)
    shutil.rmtree(work, ignore_errors=True)
    marker.write_text(identity)
    print('icu-' + args.platform + ': built ' + lock['version'])


if __name__ == '__main__':
    sys.exit(main())
