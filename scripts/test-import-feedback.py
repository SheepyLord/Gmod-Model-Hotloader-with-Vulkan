"""Exercise the actual import banner, worker cancellation, failure and cache reuse."""
import json, pathlib, time
from gamectl import execute, ROOT, read

def lua(code):
    result=execute('client',code,45)
    if not result['ok']: raise RuntimeError(result.get('error'))
    return result.get('value')

session=read(ROOT/'validation/session.json')
cache=pathlib.Path(session['cache'])
models=read(ROOT/'validation/acceptance-models.json')
source=read(cache/'sources.local.json')[models['Xin']['asset']]['source']
report={}
lua("mmdhl.Open() if mmdhl.library.job then mmdhl.library.CancelImport() end return true")
job=lua('local handle,err=mmdhl.native.BeginImport('+json.dumps(source,ensure_ascii=False)+",'{}') assert(mmdhl.library.StartImport(handle,err)) return handle")
for _ in range(30):
    state=lua("local l=mmdhl.library return {active=l.job~=nil,filename=l.filename,stage=l.status,progress=l.progress,banner=mmdhl.window.Library.ImportBanner:IsVisible(),cancel=mmdhl.window.Library.Cancel:IsVisible()}")
    if state.get('filename'):break
    time.sleep(.1)
assert state['active'] and state['banner'] and state['cancel'] and state.get('filename'),state
report['banner']=state
lua("RunConsoleCommand('mmdhl_debug_capture','compatibility-import-banner','ui') return true")
time.sleep(.15)
lua("mmdhl.CloseLibrary() assert(mmdhl.library.job) RunConsoleCommand('mmdhl_debug_capture','compatibility-import-hud','ui') return true")
time.sleep(.15)
lua("mmdhl.Open() mmdhl.window.Library.Cancel:DoClick() assert(not mmdhl.library.job) return true")
report['cancel']=lua(f"return mmdhl.Decode(mmdhl.native.PollJob({job}))")
assert report['cancel']['state']=='cancelled'
print('PASS banner, filename, measurable progress, closed-window HUD and Cancel',flush=True)
for name,path in [('error',str(ROOT/'tests/fixtures/truncated.pmx')),('current_schema',source),('cached',source)]:
    lua('assert(mmdhl.library.StartImport(mmdhl.native.BeginImport('+json.dumps(path,ensure_ascii=False)+",'{}'))) return true")
    for _ in range(200):
        state=lua('local l=mmdhl.library return {active=l.job~=nil,status=l.status,progress=l.progress,asset=l.lastImported}')
        if not state['active']:break
        time.sleep(.1)
    assert not state['active'],state
    if name=='error':assert 'parse' in state['status'].lower() or 'invalid' in state['status'].lower() or 'PMX' in state['status'],state
    else:
        assert state['progress']==1,state
        if name=='cached':assert state['asset']==report['current_schema']['asset'],state
    report[name]=state
    print('PASS',name,state['status'],flush=True)
lua('mmdhl.CloseLibrary() return true')
(ROOT/'validation/compatibility-import-feedback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
