"""Compile Source VCS files with a checksum-pinned open-source compiler."""
import hashlib,pathlib,subprocess,urllib.request,shutil
ROOT=pathlib.Path(__file__).resolve().parents[1]
compiler=ROOT/'vendor/ShaderCompile.exe'
url='https://github.com/SCell555/ShaderCompile/releases/download/build_235_20231013.2/ShaderCompile.exe'
expected='c341f397483b70b4e2260211dcba772e7c3e0e82db0edb9c15b75abb8e12db70'
if not compiler.exists():compiler.parent.mkdir(exist_ok=True);compiler.write_bytes(urllib.request.urlopen(url,timeout=60).read())
if hashlib.sha256(compiler.read_bytes()).hexdigest()!=expected:raise RuntimeError('Shader compiler checksum mismatch')
for name,kind in [('mmdhl_vs20','vs'),('mmdhl_ps20b','ps')]:
 subprocess.run([str(compiler),'-force','-shaderpath',str(ROOT/'shaders'),'-ver','20b','-types',kind,f'{name}.hlsl'],check=True,cwd=ROOT)
out=ROOT/'addon/shaders/fxc';out.mkdir(parents=True,exist_ok=True)
for name in ('mmdhl_vs20','mmdhl_ps20b'):
 p=ROOT/'shaders/shaders/fxc'/f'{name}.vcs'
 shutil.copy2(p,out/p.name)
