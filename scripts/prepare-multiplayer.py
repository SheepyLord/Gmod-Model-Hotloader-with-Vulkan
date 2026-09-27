"""Create disposable client roots. Link immutable engine/content, copy settings.
No user cache, save, addon or config is modified by this preparation step.
"""
import json,os,pathlib,shutil
ROOT=pathlib.Path(__file__).resolve().parents[1]
SOURCE=pathlib.Path(r'H:\SteamLibrary\steamapps\common\GarrysMod')
TARGET=ROOT/'validation/multiplayer'
def tree(source,target):
 for base,dirs,files in os.walk(source):
  out=target/pathlib.Path(base).relative_to(source);out.mkdir(parents=True,exist_ok=True)
  for name in files:
   if 'mmdhl' in name.lower() or name.lower()=='lib_coacd.dll' or name.lower().endswith(('.log','.dmp','.mdmp')):continue
   src=pathlib.Path(base)/name;dst=out/name
   if dst.exists():continue
   if src.suffix in ('.cfg','.txt','.vdf','.json','.db','.conf','.ini'):shutil.copy2(src,dst)
   else:os.link(src,dst)
for client in ('client1','client2'):
 target=TARGET/(client+'-isolated');target.mkdir(parents=True,exist_ok=True)
 for name in ('bin','platform','sourceengine'):tree(SOURCE/name,target/name)
 for name in ('bin','cfg','gamemodes','html','lua','maps','materials','models','scripts','resource','shaders','particles','sound'):
  source=SOURCE/'garrysmod'/name
  if source.exists():tree(source,target/'garrysmod'/name)
 for source in (SOURCE/'garrysmod').glob('*'):
  if source.is_file() and source.suffix in ('.vpk','.txt','.inf'):
   dest=target/'garrysmod'/source.name
   if not dest.exists():shutil.copy2(source,dest) if source.suffix!='.vpk' else os.link(source,dest)
 for name in ('gmod.exe','steam_appid.txt'):
  if not (target/name).exists():shutil.copy2(SOURCE/name,target/name)
 print('Prepared',target)
# Seed only the first client's raw March 7th import, to exercise approval/upload
# followed by a genuine empty-cache download on client two.
asset='bac307fdcefad2b6331a95e3b77644ad1562a1aba9983c0830e260326f2ee4c6'
cache=SOURCE/'garrysmod/data/mmd_hotloader';dest=TARGET/'client1-isolated/garrysmod/data/mmd_hotloader'
manifest=json.loads((cache/'assets'/asset/'manifest.json').read_text('utf-8'))
paths=[f'assets/{asset}/manifest.json',f'assets/{asset}/model.bin']
for mat in manifest['textures']:
 for key in ('base','sphere','toon'):
  if mat.get(key):paths.append(f'textures/{mat[key]}.png')
for name in set(paths):
 out=dest/name;out.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(cache/name,out)
print('Seeded raw import in client one; client two starts empty.')
