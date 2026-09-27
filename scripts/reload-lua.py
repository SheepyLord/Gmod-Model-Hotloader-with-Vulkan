"""Reload addon Lua in an owned test game; native/shader changes require restart."""
import hashlib,shutil,subprocess
from gamectl import ROOT,read,atomic,execute
session=read(ROOT/'validation/session.json')
if not session:raise SystemExit('Start an owned test game first.')
# Single player can pause server Think while the owned window is unfocused.
subprocess.run(['powershell','-NoProfile','-File',str(ROOT/'scripts/game-focus.ps1')],check=True)
from pathlib import Path
game=Path(session['gameRoot'])/'garrysmod'
staged={str(Path(e['target'])):e for e in session.get('staged',[])}
for source in (ROOT/'addon/lua').rglob('*.lua'):
 relative=source.relative_to(ROOT/'addon');target=game/'addons/mmd_hotloader'/relative
 target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
 base=game/relative
 if str(base) in staged:
  shutil.copy2(source,base);staged[str(base)]['sha256']=hashlib.sha256(base.read_bytes()).hexdigest().upper()
atomic(ROOT/'validation/session.json',session)
for realm in ('server','client'):
 result=execute(realm,"""include('autorun/mmdhl.lua')
 local old=ENT local stored=scripted_ents.GetStored('mmdhl_ragdoll')
 if stored then
  ENT=stored.t include('entities/mmdhl_ragdoll/'..(SERVER and 'init.lua' or 'cl_init.lua'))
  scripted_ents.Register(ENT,'mmdhl_ragdoll')
  for _,e in ipairs(ents.FindByClass('mmdhl_ragdoll')) do
   for k,v in pairs(ENT) do if isfunction(v) then e[k]=v end end
   e.MMDInfo=nil e.FingerIndex=nil
  end
 end
 ENT=old return true""")
 if not result['ok']:raise RuntimeError(result)
print('Lua reloaded; native instances preserved.')
