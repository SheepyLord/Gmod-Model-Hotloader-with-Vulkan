"""Owned-game QoL checks. Deletion uses only a disposable fixture imported here."""
import json, pathlib, runpy, time
from gamectl import ROOT, read

c=runpy.run_path(str(ROOT/'scripts/test-compatibility.py'))
lua,wait=c['lua'],c['wait']
session=read(ROOT/'validation/session.json');cache=pathlib.Path(session['cache'])
report={'session':session}
def check(name,value):
    assert value,name
    report[name]=value
    (ROOT/'validation/qol-game.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print('PASS',name,flush=True)
def import_model(source):
    lua('client','assert(mmdhl.library.StartImport(mmdhl.native.BeginImport('+json.dumps(str(source),ensure_ascii=False)+",'{}'))) return true")
    for _ in range(1200):
        status=lua('client','local l=mmdhl.library return {active=l.job~=nil,status=l.status,asset=l.lastImported,progress=l.progress}')
        if not status['active']:break
        time.sleep(.2)
    assert not status['active'] and status['progress']==1,status
    return status['asset']

lua('server','for _,e in ipairs(mmdhl.Entities()) do e:Remove() end return true')
# Use a unique authored name so no previously imported fixture is deleted.
fixtures=runpy.run_path(str(ROOT/'scripts/fixtures.py'))
raw=fixtures['make'](humanoid=True,texture=True,material_count=513)
old='MMDHL regression cloth'.encode('utf8')
assert old in raw
new=('MMDHL QoL test '+session['token']).encode('utf8')
import struct
raw=raw.replace(struct.pack('<i',len(old))+old,struct.pack('<i',len(new))+new,1)
source=ROOT/'test-output/qol-fixture.pmx';source.parent.mkdir(exist_ok=True);source.write_bytes(raw)
(source.parent/'checker.dds').write_bytes((ROOT/'tests/fixtures/checker.dds').read_bytes())
asset=import_model(source)
info=read(cache/'assets'/asset/'manifest.json')
check('budget warning keeps all 513 materials',len(info['materials'])==513 and any('513 materials' in w for w in info['warnings']))
check('library has no deleted tab',lua('client',f"mmdhl.Open() local p=mmdhl.window.Library p:SelectAsset('{asset}') return p.Tabs.trash==nil and p.secondaryCollision==2 and p.Warnings:IsVisible()"))
lua('client',"RunConsoleCommand('mmdhl_debug_capture','qol-warning-library','ui') return true")
check('selection opens a preview',wait('client',f"local p=mmdhl.window.Library return p.previewHandle and p.previewAsset=='{asset}' and not p.previewError"))
check('refresh preserves placement and selection',lua('client',f"local p=mmdhl.window.Library mmdhl.library.Refresh() p:Refresh() return p.selected=='{asset}' and p.Spawn:IsEnabled()"))
check('search and favorite controls work',lua('client',f"local p=mmdhl.window.Library p.Search:SetText('{session['token']}') p.Search:OnChange() assert(#p.Models:GetLines()==1) p.Favorite:DoClick() return mmdhl.library.entries['{asset}'].settings.favorite==true"))
lua('server',"local p=player.GetHumans()[1] p:SetMoveType(MOVETYPE_NOCLIP) local ground=util.TraceLine({start=Vector(0,0,2048),endpos=Vector(0,0,-30000),mask=MASK_SOLID_BRUSHONLY}).HitPos p:SetPos(ground+Vector(-120,0,12)) p:SetEyeAngles(Angle(25,0,0)) return true")
lua('client',"mmdhl.lastSpawnStatus=nil mmdhl.window.Library.Spawn:DoClick() return true")
placed=wait('client',"return mmdhl.lastSpawnStatus and mmdhl.lastSpawnStatus.state~='loading' and mmdhl.lastSpawnStatus")
check('actual Place button acknowledges success',placed['state']=='ready')
ent=placed['entity'];wait('client',f'return mmdhl.GetInstance(Entity({ent}))>0')
check('successful placement releases the menu preview',wait('client','return not IsValid(mmdhl.previewOwner)'))
rig=lua('server',f'return mmdhl.GetRig(Entity({ent}))')
check('short paths and complete material inventory',lua('client',f"local e=Entity({ent}) local m=e:GetMaterials() return #m==513 and #m[1]<70 and m[1]:find('/core_1',1,true) and AdvMat.GetMaterialLimit(m[1])==513 and AdvMat.GetMaterialLimit(e:GetModel())==513 and not Material(m[1]):IsError()"))
check('default scene collision mode',lua('server',f'return mmdhl.GetSecondaryCollisionMode(Entity({ent}))==2'))
check('18 native bodies preserved',lua('server',f'return Entity({ent}):GetPhysicsObjectCount()==18'))
# The public API and bindable command must recover both server and client stop flags.
lua('server',f"local e=Entity({ent}) e.MMDStopped='test stop' MMDHL_QOL_POSES={{}} for i=0,17 do MMDHL_QOL_POSES[i]=e:GetPhysicsObjectNum(i):GetPos() end return true")
lua('client',f"Entity({ent}).MMDPresentationStopped='test stop' RunConsoleCommand('mmdhl_reset_physics','all') return true")
wait('client',f'return Entity({ent}).MMDPresentationStopped==nil')
check('bindable physics reset preserves native pose and appearance',lua('server',f"local e=Entity({ent}) assert(not e.MMDStopped) for i=0,17 do assert(e:GetPhysicsObjectNum(i):GetPos():Distance(MMDHL_QOL_POSES[i])<.001 and not e:GetPhysicsObjectNum(i):IsMotionEnabled()) end local d=mmdhl.GetDiagnostics(e) return d.bodies==3 and d.sourceError=='' and d.collisionMode==2"))
check('context reset available',lua('client',f'return properties.List.mmdhl_reset_physics:Filter(Entity({ent}),LocalPlayer())'))
# Spawn a live NPC with a native physics object; the owning-thread capture must exclude it.
lua('server',"local n=ents.Create('npc_citizen') n:SetPos(player.GetHumans()[1]:GetPos()+Vector(100,0,0)) n:Spawn() n:PhysicsInit(SOLID_VPHYSICS) n:SetMoveType(MOVETYPE_NONE) n:SetHealth(100) MMDHL_QOL_NPC=n return true")
time.sleep(.3)
check('living actors excluded from default contacts',lua('server',"local d=mmdhl.sceneDiagnostics assert(d.excludedLivingEntities>=2) return {entities=d.excludedLivingEntities,physicsObjects=d.excludedLivingObjects,sceneObjects=d.objects,feedback=d.feedbackApplied}"))
lua('server','if IsValid(MMDHL_QOL_NPC) then MMDHL_QOL_NPC:Remove() end return true')
# Keep another texture reference to prove deletion does not remove shared data.
other=import_model(ROOT/'tests/fixtures/textured21.pmx')
shared=info['textures'][0]['base']
lua('client',f"mmdhl.Open() local p=mmdhl.window.Library p:SelectAsset('{asset}') p.Delete:DoClick() return true")
lua('client',"local function find(p) if p.GetText and p:GetText()=='Delete' and p.DoClick and mmdhl.PanelVisible(p) then return p end for _,c in ipairs(p:GetChildren()) do local r=find(c) if r then return r end end end assert(find(vgui.GetWorldPanel()),'Delete confirmation missing'):DoClick() return true")
wait('client',f"return not mmdhl.library.deleting and not mmdhl.library.entries['{asset}']")
result={'ok':True,'message':lua('client','return mmdhl.window.Library.Status:GetText()')}
check('actual Delete confirmation completes',result['message'].startswith('Deleted imported models'))
check('deletion removes model and its live ragdoll',not (cache/'assets'/asset/'manifest.json').exists() and lua('server',f'return not IsValid(Entity({ent}))'))
check('deletion clears source registry and fit files',asset not in read(cache/'sources.local.json') and not list((cache/'fits').glob('*'+asset+'.json')))
check('deletion retains source and shared texture',source.exists() and (cache/'textures'/(shared+'.png')).exists() and (cache/'assets'/other/'manifest.json').exists())
report['deletion']=result
pending=read(cache/'cleanup.json') if (cache/'cleanup.json').exists() else []
report['deferredFiles']=pending
# Reimport while a former archive may still be mounted: the new live cache must
# not remain on a delayed-deletion queue.
again=import_model(source);assert again==asset
pending=read(cache/'cleanup.json') if (cache/'cleanup.json').exists() else []
check('reimport cancels deletion of reused files',not any(x.startswith('assets/'+asset+'/') for x in pending))
lua('client',f"MMDHL_QOL_DELETE=nil mmdhl.library.Delete({{'{asset}'}},function(ok,message) MMDHL_QOL_DELETE={{ok=ok,message=message}} end) return true")
assert wait('client','return MMDHL_QOL_DELETE')['ok']
lua('client','mmdhl.CloseLibrary() return true')
(ROOT/'validation/qol-game.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
