"""Package only project code, pinned notices, generated fixtures and release outputs.
Never scans or packages local model caches, corpus paths, validation dumps or vendor trees.
"""
import argparse,hashlib,json,pathlib,re,zipfile,shutil,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--version',required=True,help='Immutable release label, e.g. 2.1.0-actors-preview.1')
args=parser.parse_args()
if not re.fullmatch(r'[0-9][A-Za-z0-9.-]{0,79}',args.version):parser.error('Invalid version label')
subprocess.run([sys.executable,str(ROOT/'scripts/native_manifest.py'),'--check'],check=True)
native_release=json.loads((ROOT/'build/bin/Release/native-release.json').read_text(encoding='utf8'))
if args.version!=native_release['release']:parser.error('Package version must match the compiled immutable native release ID')

dist=ROOT/'dist';dist.mkdir(exist_ok=True)
prefix='model-hotloader-'+args.version+'-win64'
release=dist/(prefix+'.zip')
symbols=dist/(prefix+'-symbols.zip')
checksumFile=dist/(prefix+'-SHA256.json')
if any(f.exists() for f in (release,symbols,checksumFile)):raise FileExistsError('This release label already exists; choose a new version')
notices={'nanoem/LICENSE.MIT':'nanoem-MIT.txt','bullet/LICENSE.txt':'Bullet-zlib.txt','garrysmod_common/license.txt':'garrysmod_common.txt','sourcesdk/LICENSE':'Source-SDK.txt','sourcesdk/thirdpartylegalnotices.txt':'Source-SDK-third-party.txt','json/LICENSE.MIT':'json-MIT.txt','stb/LICENSE':'stb.txt'}
lic=ROOT/'licenses';lic.mkdir(exist_ok=True)
notices['glm/copying.txt']='glm.txt'
notices['nanoem/LICENSE.MPL']='nanoem-MPL-2.0.txt'
notices.update({'assimp/LICENSE':'assimp.txt','meshoptimizer/LICENSE.md':'meshoptimizer.txt','coacd/LICENSE':'coacd.txt'})
for source,name in notices.items():shutil.copyfile(ROOT/'vendor'/source,lic/name)
files=[]
for name in ('README.md','LICENSE','THIRD_PARTY.md','CMakeLists.txt','dependencies.lock.json','install.ps1'):
 files.append(ROOT/name)
for folder in ('addon','native','shaders','tests','docs','scripts','licenses','integrations'):
 for f in (ROOT/folder).rglob('*'):
  if not f.is_file() or '__pycache__' in f.parts:continue
  if folder=='shaders' and f.parent!=ROOT/'shaders':continue
  files.append(f)
executables=['mmdhl_worker','mmdhl_tests','mmdhl_replay','mmdhl_timing','mmdhl_profile','mmdhl_broadphase_tests','mmdhl_compute_tests','mmdhl_async_tests','mmdhl_motion_tests','mmdhl_stretch_tests','mmdhl_stretch_ground_tests','mmdhl_projection_tests','mmdhl_light_overlap_tests','mmdhl_actor_tests','mmdhl_quality_tests','mmdhl_props_tests']
names=['gmcl_mmdhl_win64','gmsv_mmdhl_win64','mmdhl_runtime_win64']+executables
for name in names:files.append(ROOT/'build/bin/Release'/(name+('.exe' if name in executables else '.dll')))
files.append(ROOT/'build/bin/Release/native-release.json')
files.append(ROOT/'build/bin/Release/lib_coacd.dll') # pinned CoACD wheel binary, not rebuilt here
files=sorted(set(files));manifest={str(f.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}
# User-facing native components must always ship matching symbols. Some optional
# diagnostic executables are built without /DEBUG; include their symbols when available.
for name in ['gmcl_mmdhl_win64','gmsv_mmdhl_win64','mmdhl_runtime_win64','mmdhl_worker']:
 if not (ROOT/'build/bin/Release'/(name+'.pdb')).is_file():raise FileNotFoundError('Build debug symbols before packaging: '+name)
symbolNames=[name for name in names if (ROOT/'build/bin/Release'/(name+'.pdb')).is_file()]
with zipfile.ZipFile(release,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for f in files:z.write(f,f.relative_to(ROOT).as_posix())
 z.writestr('SHA256.json',json.dumps(manifest,indent=2))
with zipfile.ZipFile(symbols,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for name in symbolNames:z.write(ROOT/'build/bin/Release'/(name+'.pdb'),name+'.pdb')
 z.writestr('BINARY_SHA256.json',json.dumps({k:v for k,v in manifest.items() if k.startswith('build/bin/')},indent=2))
checksums={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in (release,symbols)}
checksumFile.write_text(json.dumps(checksums,indent=2),encoding='utf-8')
print(json.dumps({'release':str(release),'symbols':str(symbols),'files':len(files),'checksums':checksums},indent=2))
