"""Exercise the real installer in disposable package/game fixtures."""
from pathlib import Path
import hashlib, json, shutil, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
PWSH=shutil.which('pwsh') or str(Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe')
parent=(ROOT/'test-output').resolve();parent.mkdir(exist_ok=True)
scratch=Path(tempfile.mkdtemp(prefix='installation-',dir=parent)).resolve()
assert scratch.parent==parent
try:
    package=scratch/'package';game=scratch/'game'
    (package/'scripts').mkdir(parents=True)
    shutil.copyfile(ROOT/'scripts/install.ps1',package/'scripts/install.ps1')
    binary=package/'build/bin/Release';binary.mkdir(parents=True)
    release=json.loads((ROOT/'build/bin/Release/native-release.json').read_text())
    for item in release['files'].values():shutil.copyfile(ROOT/'build/bin/Release'/item['name'],binary/item['name'])
    shutil.copyfile(ROOT/'build/bin/Release/native-release.json',binary/'native-release.json')
    addon=package/'addon';(addon/'lua/autorun').mkdir(parents=True);(addon/'shaders/fxc').mkdir(parents=True)
    (addon/'addon.json').write_text('{"title":"Model Hotloader"}')
    (addon/'lua/autorun/mmdhl.lua').write_text('mmdhl = {}')
    (game/'bin/win64').mkdir(parents=True);(game/'bin/win64/gmod.exe').write_bytes(b'fixture')
    vanilla=game/'bin/win64/engine.dll';vanilla.write_bytes(b'vanilla-before')
    legacy=game/'garrysmod/addons/mmd_hotloader';shutil.copytree(addon,legacy)
    (legacy/'addon.json').write_text('{"title":"MMD Hot Loader"}') # Installed before the rename.
    (legacy/'local-note.txt').write_text('preserve me')
    def install(*args,ok=True):
        result=subprocess.run([PWSH,'-NoProfile','-File',str(package/'scripts/install.ps1'),'-GameRoot',str(game),*args],capture_output=True,text=True)
        assert (result.returncode==0)==ok,result.stdout+result.stderr
        return result.stdout+result.stderr
    install()
    assert not legacy.exists() and vanilla.read_bytes()==b'vanilla-before'
    backups=list((game/'mmdhl-install-backups').glob('loose-addon-*'))
    assert len(backups)==1 and (backups[0]/'local-note.txt').read_text()=='preserve me'
    installed=game/'garrysmod/lua/bin'
    for item in release['files'].values():assert hashlib.sha256((installed/item['name']).read_bytes()).hexdigest()==item['sha256']
    vanilla.write_bytes(b'vanilla-after-steam-update')
    for item in release['files'].values():assert hashlib.sha256((installed/item['name']).read_bytes()).hexdigest()==item['sha256']
    install('-InstallAddon');assert (legacy/'lua/autorun/mmdhl.lua').exists()
    conflict=game/'garrysmod/addons/duplicate/lua/autorun';conflict.mkdir(parents=True);(conflict/'mmdhl.lua').write_text('duplicate')
    assert 'Conflicting loose importer' in install(ok=False)
    (conflict/'mmdhl.lua').unlink()
    worker=binary/'mmdhl_worker.exe';worker.write_bytes(b'broken package')
    assert 'checksum mismatch' in install(ok=False)
    assert legacy.exists() # Reject bad packages before migrating an installed addon.
    print('PASS: binary-only install, destination hashes, migration backup, development opt-in, duplicate rejection, corruption rejection and vanilla update survival')
finally:
    assert scratch.resolve().parent==parent and scratch.name.startswith('installation-')
    shutil.rmtree(scratch)
