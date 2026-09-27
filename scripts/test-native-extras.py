"""Additional native constraints, complete facial poses, render tools and undo."""
import json,time,subprocess
from gamectl import execute,ROOT
def lua(realm,code):
 r=execute(realm,code,45)
 if not r['ok']:raise RuntimeError(r.get('error'))
 return r.get('value')
report=[]
def check(value,name):
 report.append({'name':name,'value':value,'passed':bool(value)})
 (ROOT/'validation/native-extras.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 print(('PASS ' if value else 'FAIL ')+name,flush=True)
 assert value,name
asset=json.loads((ROOT/'validation/acceptance-models.json').read_text(encoding='utf-8'))['Xin']['asset']
subprocess.run(['powershell','-NoProfile','-File',str(ROOT/'scripts/game-focus.ps1')],check=True)
e=lua('server',f"for _,e in ipairs(mmdhl.Entities()) do e:Remove() end local p=player.GetHumans()[1] p:Give('gmod_tool') p:SelectWeapon('gmod_tool') mmdhl.testEnt=mmdhl.SpawnNative(p,'{asset}',{{scaleMultiplier=1,position={{0,0,-12275}},frozen=true}}) return mmdhl.testEnt:EntIndex()")
time.sleep(1)
lua('server',"player.GetHumans()[1]:GetWeapon('gmod_tool'):GetToolObject('faceposer'):RightClick({Entity=mmdhl.testEnt}) return true")
panel=lua('client',f"local e=Entity({e}) LocalPlayer():GetWeapon('gmod_tool'):GetToolObject('faceposer'):RebuildControlPanel(e) MMDHL_PANEL=controlpanel.Get('faceposer') return MMDHL_PANEL.MMDOverflowCount")
check(panel==37,'Xin Face Poser contains all 37 overflow sliders')
lua('client',"function MMDHL_CLICK(label) local function visit(p) if p.GetText and p:GetText()==label and p.DoClick then p:DoClick() return true end for _,c in ipairs(p:GetChildren()) do if visit(c) then return true end end end return visit(MMDHL_PANEL) end return true")
over=lua('server','for _,m in ipairs(mmdhl.GetMorphs(mmdhl.testEnt)) do if m.native<0 then return m.mmd end end')
lua('client',f"mmdhl.SetMorphWeight(Entity({e}),{over},.45) return true");time.sleep(.3)
check(lua('client',"return MMDHL_CLICK('Copy complete MMD face')"),'Complete face copy button')
lua('client',"MMDHL_CLICK('Reset complete MMD face') return true");time.sleep(.3)
check(lua('server',f'return mmdhl.GetMorphWeight(mmdhl.testEnt,{over})==0'),'Complete reset includes overflow')
lua('client',"MMDHL_CLICK('Paste complete MMD face') return true");time.sleep(.3)
check(lua('server',f'return math.abs(mmdhl.GetMorphWeight(mmdhl.testEnt,{over})-.45)<.001'),'Complete paste restores overflow')
lua('client',"local function visit(p) if p.GetPlaceholderText and p:GetPlaceholderText()=='MMD preset name' then p:SetValue('owned_acceptance') end for _,c in ipairs(p:GetChildren()) do visit(c) end end visit(MMDHL_PANEL) MMDHL_CLICK('Save complete MMD face') MMDHL_CLICK('Reset complete MMD face') return true");time.sleep(.3)
lua('client',"MMDHL_CLICK('Load complete MMD face') return true");time.sleep(.3)
check(lua('server',f'return math.abs(mmdhl.GetMorphWeight(mmdhl.testEnt,{over})-.45)<.001'),'Named face preset restores overflow')
check(lua('server',"local e=mmdhl.testEnt local p=ents.Create('prop_physics') p:SetModel('models/props_c17/oildrum001.mdl') p:SetPos(e:GetPos()+Vector(100,0,0)) p:Spawn() local t=player.GetHumans()[1]:GetWeapon('gmod_tool'):GetToolObject('weld') t:ClearObjects() t:SetOperation(0) local a=t:LeftClick({Entity=e,PhysicsBone=7,HitPos=e:GetPhysicsObjectNum(7):GetPos(),HitNormal=Vector(0,0,1)}) local b=t:LeftClick({Entity=p,PhysicsBone=0,HitPos=p:GetPos(),HitNormal=Vector(0,0,1)}) local ok=a and b and constraint.HasConstraints(e) p:Remove() return ok"),'Stock Weld tool creates an actual limb constraint')
saved=lua('client',"local v={} for _,n in ipairs({'colour_r','colour_g','colour_b','colour_a','material_override'}) do v[n]=GetConVar(n):GetString() end RunConsoleCommand('colour_r','80') RunConsoleCommand('colour_g','170') RunConsoleCommand('colour_b','240') RunConsoleCommand('colour_a','255') RunConsoleCommand('material_override','debug/env_cubemap_model') return v")
time.sleep(.4)
check(lua('server',"local e=mmdhl.testEnt local w=player.GetHumans()[1]:GetWeapon('gmod_tool') return w:GetToolObject('colour'):LeftClick({Entity=e}) and w:GetToolObject('material'):LeftClick({Entity=e}) and e:GetColor().r==80 and e:GetMaterial()=='debug/env_cubemap_model'"),'Stock Color and Material tools accept the native entity')
time.sleep(.5)
check(lua('client','return not mmdhl.renderError'),'Source cubemap override renders without errors')
lua('client','for k,v in pairs(util.JSONToTable('+json.dumps(json.dumps(saved))+')) do RunConsoleCommand(k,v) end return true')
lua('server',"local e=mmdhl.testEnt local w=player.GetHumans()[1]:GetWeapon('gmod_tool') w:GetToolObject('colour'):Reload({Entity=e}) w:GetToolObject('material'):Reload({Entity=e}) e:Remove() return true")
copy=lua('server',f"local p=player.GetHumans()[1] mmdhl.undoTest=mmdhl.SpawnNative(p,'{asset}',{{scaleMultiplier=1,position={{0,0,-12275}},frozen=true}}) return mmdhl.undoTest:EntIndex()")
lua('client',"RunConsoleCommand('gmod_undo') return true");time.sleep(.5)
check(lua('server','return not IsValid(mmdhl.undoTest)'),'Sandbox undo removes native carrier and attached state')
lua('client',"file.Delete('mmd_hotloader/face_presets/owned_acceptance.json') return true")
