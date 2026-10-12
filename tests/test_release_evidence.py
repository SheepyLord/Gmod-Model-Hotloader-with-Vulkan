"""Release evidence: the renderer entry survives native-only manifest runs, the
drop-in packager reports the packages it staged (Windows and the Linux -vulkan and
-opengl ones, whose renderer is a build folder), and the policy updater accepts
only artifacts that ship every byte their record lists."""
from pathlib import Path
import hashlib, json, os, shutil, subprocess, sys, tempfile, zipfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import native_manifest
PWSH=shutil.which('pwsh') or str(Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe')
NATIVE={'client':'gmcl_mmdhl_win64.dll','server':'gmsv_mmdhl_win64.dll','runtime':'mmdhl_runtime_win64.dll','worker':'mmdhl_worker.exe','coacd':'lib_coacd.dll'}
NATIVE_LINUX_RUNTIME=native_manifest.PLATFORM_FILES['linux64']['runtime']
parent=(ROOT/'test-output').resolve();parent.mkdir(exist_ok=True)
scratch=Path(tempfile.mkdtemp(prefix='release-',dir=parent)).resolve()
def entry(path):data=path.read_bytes();return dict(name=path.name,size=len(data),sha256=hashlib.sha256(data).hexdigest())
try:
    # Native files, a renderer and DXVK's license files of one fixture build.
    build=scratch/'build';build.mkdir()
    for key,name in NATIVE.items():(build/name).write_bytes(os.urandom(4096+len(key)))
    (build/'d3d9.dll').write_bytes(os.urandom(8192))
    dxvk=scratch/'dxvk'
    for relative in ('LICENSE','subprojects/dxbc-spirv/LICENSE','subprojects/libdisplay-info/LICENSE'):
        (dxvk/relative).parent.mkdir(parents=True,exist_ok=True);(dxvk/relative).write_text('license '+relative)
    identity=dict(api=1,build='fixture-build',installApi=1,platform='win64',release='9.9.9-test.1')
    files={key:entry(build/name) for key,name in NATIVE.items()}

    # A native-only run keeps the recorded renderer; --renderer replaces it.
    with_renderer=native_manifest.release_record(identity,files,{},build/'d3d9.dll')
    assert with_renderer['renderer']['sha256']==entry(build/'d3d9.dll')['sha256'] and with_renderer['renderer']['kind']=='dxvk'
    assert with_renderer['renderer']['patches']==sorted(p.name for p in (ROOT/'patches/dxvk').glob('*.patch'))
    kept=native_manifest.release_record(identity,files,with_renderer)
    assert kept['renderer']==with_renderer['renderer'],'A native-only manifest run dropped the renderer'
    assert 'renderer' not in native_manifest.release_record(identity,files,{})
    (build/'native-release.json').write_text(json.dumps(with_renderer,indent=2),encoding='utf8')

    # The packager reports the packages it staged (a license loop used to overwrite the name).
    dist=scratch/'dist'
    out=subprocess.run([sys.executable,str(ROOT/'scripts/package-dropin.py'),'--bin',str(build),'--renderer',str(build/'d3d9.dll'),'--dxvk-source',str(dxvk),'--output',str(dist),'--name','Review-Package'],capture_output=True,text=True,check=True).stdout
    packages=json.loads(out)['packages']
    assert [p['name'] for p in packages]==['Review-Package-vulkan','Review-Package-opengl-remix'],packages
    for p in packages:assert Path(p['archive']).name==p['name']+'.zip' and Path(p['folder']).name==p['name']
    names=[]
    for p in packages:
        with zipfile.ZipFile(p['archive']) as z:names.append(z.namelist())
    assert 'GarrysMod/bin/win64/d3d9.dll' in names[0]
    assert not any('d3d9.dll' in n or 'DXVK-LICENSES' in n for n in names[1])
    # Both carry the project license and every dependency notice beside the files they
    # cover (GarrysMod/bin/win64/LICENSES), point to the public repository, and ship no
    # source archive (the source is the public repository).
    notices=['GarrysMod/bin/win64/LICENSES/Model-Hotloader-LICENSE.txt','GarrysMod/bin/win64/LICENSES/THIRD_PARTY.md']+['GarrysMod/bin/win64/LICENSES/'+p.name for p in sorted((ROOT/'licenses').glob('*.txt'))]
    assert len(notices)>20
    for p,listed in zip(packages,names):
        assert not [n for n in notices if n not in listed],p['name']
        assert not [n for n in listed if n.endswith('-source.zip') or n.startswith(('LICENSES/','DXVK-LICENSES/'))],p['name']
        with zipfile.ZipFile(p['archive']) as archive:
            assert archive.read('GarrysMod/bin/win64/LICENSES/Model-Hotloader-LICENSE.txt')==(ROOT/'LICENSE').read_bytes()
        install=(Path(p['folder'])/'INSTALL.txt').read_text(encoding='utf8')
        assert 'https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/releases' in install and 'filedetails/?id=3810025467' in install,p['name']
    assert 'GarrysMod/bin/win64/DXVK-LICENSES/dxvk.txt' in names[0] and not any('DXVK-LICENSES' in n for n in names[1])

    # Linux: the renderer is its build folder (libtogl_client.so and DXVK's libdxvk_d3d9.so, beside
    # Meson's object folder), recorded with both files; -vulkan installs them in bin/linux64.
    linux=scratch/'linux';linux.mkdir()
    for key,name in native_manifest.PLATFORM_FILES['linux64'].items():(linux/name).write_bytes(os.urandom(4096+len(key)))
    (linux/'libgomp-a34b3233.so.1.0.0').write_bytes(os.urandom(1024))
    renderer=scratch/'build-dxvk-linux64';(renderer/'togl').mkdir(parents=True);(renderer/'src/d3d9/libdxvk_d3d9.so.0.30101.p').mkdir(parents=True)
    (renderer/'togl/libtogl_client.so').write_bytes(os.urandom(6144));(renderer/'src/d3d9/libdxvk_d3d9.so.0.30101').write_bytes(os.urandom(9216))
    linux_identity=dict(identity,platform='linux64')
    linux_files={key:entry(linux/name) for key,name in native_manifest.PLATFORM_FILES['linux64'].items()}
    linux_record=native_manifest.release_record(linux_identity,linux_files,{},renderer)
    togl=linux_record['renderer']
    assert togl['name']=='libtogl_client.so' and togl['kind']=='dxvk' and togl['sha256']==entry(renderer/'togl/libtogl_client.so')['sha256']
    assert togl['library']==dict(entry(renderer/'src/d3d9/libdxvk_d3d9.so.0.30101'),name='libdxvk_d3d9.so')
    assert togl['patches']==sorted(p.name for p in (ROOT/'patches/dxvk').glob('*.patch'))+sorted('dxvk-linux/'+p.name for p in (ROOT/'patches/dxvk-linux').glob('*.patch'))
    assert native_manifest.release_record(linux_identity,linux_files,linux_record)['renderer']==togl,'A native-only manifest run dropped the Linux renderer'
    (linux/'native-release.json').write_text(json.dumps(linux_record,indent=2),encoding='utf8')
    linux_dist=scratch/'linux-dist'
    def package_linux(*extra):
        return subprocess.run([sys.executable,str(ROOT/'scripts/package-dropin.py'),'--bin',str(linux),'--dxvk-source',str(dxvk),'--output',str(linux_dist),'--name','Review-Linux',*extra],capture_output=True,text=True)
    result=package_linux('--renderer',str(renderer));assert result.returncode==0,result.stderr
    packages=json.loads(result.stdout)['packages']
    assert [p['name'] for p in packages]==['Review-Linux-vulkan','Review-Linux-opengl'],packages
    names=[]
    for p in packages:
        with zipfile.ZipFile(p['archive']) as z:names.append(z.namelist())
    engine=['GarrysMod/bin/linux64/libtogl_client.so','GarrysMod/bin/linux64/libdxvk_d3d9.so']+['GarrysMod/bin/linux64/DXVK-LICENSES/'+n for n in ('dxvk.txt','dxbc-spirv.txt','libdisplay-info.txt','togl.txt')]
    assert not [n for n in engine if n not in names[0]] and not any('bin/linux64' in n for n in names[1])
    with zipfile.ZipFile(packages[0]['archive']) as archive:
        assert archive.read('GarrysMod/bin/linux64/libdxvk_d3d9.so')==(renderer/'src/d3d9/libdxvk_d3d9.so.0.30101').read_bytes()
        assert archive.read('GarrysMod/bin/linux64/DXVK-LICENSES/togl.txt')==(ROOT/'licenses/linux/togl.txt').read_bytes()
    for p,listed in zip(packages,names):
        assert 'GarrysMod/garrysmod/lua/bin/'+NATIVE_LINUX_RUNTIME in listed and 'GarrysMod/garrysmod/lua/bin/LICENSES/icu.txt' in listed,p['name']
    vulkan_install=(Path(packages[0]['folder'])/'INSTALL.txt').read_text(encoding='utf8')
    opengl_install=(Path(packages[1]['folder'])/'INSTALL.txt').read_text(encoding='utf8')
    assert 'Verify integrity of game files' in vulkan_install and 'dxvk-linux/' in vulkan_install and 'libtogl_client.so' in opengl_install
    # A DXVK library other than the recorded one is refused, and so is d3d9.dll as a Linux renderer.
    (renderer/'src/d3d9/libdxvk_d3d9.so.0.30101').write_bytes(os.urandom(9216))
    result=package_linux('--renderer',str(renderer));assert result.returncode!=0 and 'does not match' in result.stderr,result.stderr
    result=package_linux('--renderer',str(build/'d3d9.dll'));assert result.returncode!=0 and 'build folder' in result.stderr,result.stderr

    def update(artifact):
        tree=scratch/('policy-'+os.urandom(4).hex());(tree/'scripts').mkdir(parents=True);(tree/'addon/lua/mmdhl').mkdir(parents=True)
        shutil.copyfile(ROOT/'scripts/update-native-policy.ps1',tree/'scripts/update-native-policy.ps1')
        shutil.copyfile(ROOT/'addon/lua/mmdhl/native_policy.lua',tree/'addon/lua/mmdhl/native_policy.lua')
        result=subprocess.run([PWSH,'-NoProfile','-File',str(tree/'scripts/update-native-policy.ps1'),'-RunUrl','https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/actions/runs/1',
                               '-ReleaseUrl','https://github.com/SheepyLord/Gmod-Model-Hotloader-with-Vulkan/releases','-AltReleaseUrl','https://example.com/9.9.9-test.1','-Artifact',str(artifact)],capture_output=True,text=True)
        return result.returncode,result.stdout+result.stderr,tree/'addon/lua/mmdhl/native_policy.lua'
    def variant(name,change):
        target=scratch/name;shutil.copytree(dist,target,ignore=shutil.ignore_patterns('*.zip'));change(target);return target

    code,log,policy=update(dist)
    assert code==0,log
    recorded=json.loads(policy.read_text(encoding='utf8').split('[==[',1)[1].split(']==]',1)[0])['releases']['9.9.9-test.1']
    assert recorded['renderer']['sha256']==with_renderer['renderer']['sha256'] and recorded['files']==with_renderer['files']
    # Only a record: nothing it lists ships.
    manifest_only=scratch/'manifest-only/GarrysMod/garrysmod/lua/bin';manifest_only.mkdir(parents=True)
    shutil.copyfile(build/'native-release.json',manifest_only/'mmdhl-native-release.json')
    code,log,_=update(scratch/'manifest-only');assert code!=0 and 'does not contain' in log,log
    # A changed native file, a missing game runtime, a changed renderer, no renderer at all.
    code,log,_=update(variant('tampered',lambda t:(t/'Review-Package-opengl-remix/GarrysMod/garrysmod/lua/bin/mmdhl_worker.exe').write_bytes(b'changed')));assert code!=0 and 'worker' in log,log
    code,log,_=update(variant('no-runtime',lambda t:(t/'Review-Package-vulkan/GarrysMod/bin/win64/mmdhl_runtime_win64.dll').unlink()));assert code!=0 and 'game runtime' in log,log
    code,log,_=update(variant('bad-renderer',lambda t:(t/'Review-Package-vulkan/GarrysMod/bin/win64/d3d9.dll').write_bytes(b'changed')));assert code!=0 and 'renderer' in log,log
    code,log,_=update(variant('no-renderer',lambda t:shutil.rmtree(t/'Review-Package-vulkan')));assert code!=0 and 'no package ships d3d9.dll' in log,log
    print('PASS: renderer kept by native-only manifest runs (Windows and Linux), packager reports staged names and ships the Linux renderer only in -vulkan, policy update rejects manifest-only, tampered, runtime-less and renderer-less artifacts')
finally:
    assert scratch.resolve().parent==parent and scratch.name.startswith('release-')
    shutil.rmtree(scratch)
