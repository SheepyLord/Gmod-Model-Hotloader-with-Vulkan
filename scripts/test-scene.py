"""Load the locally imported test asset and set a repeatable in-game camera."""
import time,json
from gamectl import execute,ROOT
asset=json.loads((ROOT/'validation/test-asset.json').read_text())['id']
def run(realm,code):
    r=execute(realm,code)
    if not r['ok']:raise RuntimeError(r)
    return r.get('value')
for realm in ['server','client']:
    run(realm,f"return mmdhl.native.RequestAsset('{asset}')")
    for i in range(200):
        if run(realm,f"local info=mmdhl.Decode(mmdhl.native.AssetInfo('{asset}')) if info then mmdhl.testInfo=info return true end return false"):break
        time.sleep(.05)
    else:raise TimeoutError('Asset load')
created=run('server',f"local id,err=mmdhl.native.CreateInstance('{asset}',util.TableToJSON({{position={{64,-384,-12260}},frozen=true}})) mmdhl.testInstance=id return {{id=id,err=err}}")
if created.get('err'):raise RuntimeError(created['err'])
handle=created['id'];print(created)
print(run('client',f"""
RunConsoleCommand('pp_motionblur','0')
RunConsoleCommand('mat_motion_blur_enabled','0')
hook.Add('CalcView','MMDHL.DebugView',function() return {{origin=Vector(-86,-384,-12215),angles=Angle(0,0,0),fov=60,drawviewer=true}} end)
hook.Add('PostDrawTranslucentRenderables','MMDHL.DebugDraw',function(depth,sky)
 if sky or depth then return end
 mmdhl.DrawInstance({handle},'{asset}',mmdhl.testInfo)
end)
return true
"""))
time.sleep(.3)
print(run('client',"RunConsoleCommand('mmdhl_debug_capture','native-model') return mmdhl.native.RenderStatus()"))
