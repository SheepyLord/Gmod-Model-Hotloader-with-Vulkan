"""Owned-session regressions for import, native eyes, materials, and scene contacts."""
import argparse, json, time
from gamectl import execute, ROOT

def lua(realm, code):
    result = execute(realm, code, 45)
    if not result['ok']:
        raise RuntimeError(result.get('error'))
    return result.get('value')

def wait(realm, code):
    for _ in range(150):
        value = lua(realm, code)
        if value:
            return value
        time.sleep(.1)
    raise TimeoutError(code)

def spawn(asset, scale=1):
    lua('server', f"""MMDHL_COMPAT_RESULT=nil
local p=player.GetHumans()[1] local at=game.GetMap()=='gm_construct' and Vector(822,-800,0) or vector_origin local tr=util.TraceLine({{start=at+Vector(0,0,2048),endpos=at-Vector(0,0,30000),mask=MASK_SOLID_BRUSHONLY}}) local pos=tr.HitPos+Vector(0,0,12) mmdhl.Spawn(p,{json.dumps(asset)},{{backend='source',scaleMultiplier={scale},position={{pos:Unpack()}},angles={{0,0,0}},frozen=true}},function(e,err)
 MMDHL_COMPAT_RESULT={{entity=IsValid(e) and e:EntIndex(),error=err}} if IsValid(e) then mmdhl.testEnt=e end end) return true""")
    result=wait('server','return MMDHL_COMPAT_RESULT')
    if not result.get('entity'):
        raise RuntimeError(result)
    entity=result['entity']
    wait('client',f'return mmdhl.IsMMD(Entity({entity})) and mmdhl.GetInstance(Entity({entity}))>0')
    return entity

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('section',choices=['imports','materials','eyes','collisions','limit','overflow','morphs','visibility'])
    parser.add_argument('--models',default='validation/compatibility-models.json')
    parser.add_argument('--asset')
    args=parser.parse_args()
    models=json.loads((ROOT/args.models).read_text(encoding='utf8'))
    report={'checks':[]}
    destination=ROOT/f'validation/compatibility-{args.section}.json'
    def check(label, realm, code):
        value=lua(realm,code)
        report['checks'].append({'name':label,'value':value,'passed':True})
        destination.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
        print('PASS',label,flush=True)
        return value
    lua('client',"if IsValid(AdvMat.Editor) then AdvMat.Editor:Close() end if IsValid(g_SpawnMenu) then g_SpawnMenu:Close() end gui.HideGameUI() return true")
    lua('server','for _,e in ipairs(mmdhl.Entities()) do e:Remove() end return true')
    if args.section=='imports':
        for name, model in models.items():
            for scale in [.1,.5,1,2]:
                entity=spawn(model['asset'],scale)
                check(f'{name} at {scale}x retains native anatomy','server',f"""local e=Entity({entity}) local r=mmdhl.GetRig(e) local d=mmdhl.GetDiagnostics(e)
assert(e:GetPhysicsObjectCount()==18) assert(e:GetBoneCount()==#r.bones) assert(#e:GetMaterials()==#r.materials)
for i=0,17 do local p=e:GetPhysicsObjectNum(i) assert(IsValid(p) and p:GetPos():Length()<100000) assert(e:TranslatePhysBoneToBone(i)==r.bodies[i+1].bone) end
local out={{scale=r.scale,bodies=d.bodies,joints=d.joints,skipped=d.skippedJoints,warnings=d.warnings,bones=#r.bones,materials=#r.materials}}
e:Remove() return out""")
        return
    entity=spawn(args.asset or models['Cyrene']['asset'])
    lua('client',f'MMDHL_COMPAT_ENTITY=Entity({entity}) return true')
    if args.section=='overflow':
        check('Complete inventory exceeds the native 128-material limit','server',"""local e=mmdhl.testEnt assert(#e:GetMaterials()==130) assert(e:GetNumBodyGroups()==131)
assert(#mmdhl.materialMethods.GetMaterials(e)==128) e:SetSubMaterial(129,'models/wireframe') e:SetBodygroup(130,1) return true""")
        wait('client',"local e=MMDHL_COMPAT_ENTITY return e:GetSubMaterial(129)=='models/wireframe' and e:GetBodygroup(130)==1")
        check('AME lists all 130 slots and accepts the last slot','client',"""local e=MMDHL_COMPAT_ENTITY AdvMat.OpenVanillaEditor(e) local f=AdvMat.Editor
assert(#f.Materials==130 and #f.MatList:GetLines()==130) assert(AdvMat.GetMaterialLimit(e:GetModel())==130) assert(AdvMat.GetMaterialLimit(e:GetMaterials()[130])==130) return true""")
    elif args.section=='materials':
        check('Every material has a real shader and readable texture','client',"""local e=MMDHL_COMPAT_ENTITY local materials=e:GetMaterials() assert(#materials==64)
for i,path in ipairs(materials) do local m=Material(path) assert(not m:IsError(),path) assert(m:GetShader()=='VertexLitGeneric') assert(m:GetTexture('$basetexture') and not m:GetTexture('$basetexture'):IsError()) end
AdvMat.OpenVanillaEditor(e) local f=AdvMat.Editor assert(#f.Materials==64 and #f.MatList:GetLines()==64)
f.CurrentScope=function() return AdvMat.SCOPE.ENTITY end return {slots=#materials,preview=f.ModelPanel.Entity.MMDHLEditorPreview}""")
        wait('client',"local f=AdvMat.Editor return IsValid(f) and IsValid(f.ModelPanel.Entity) and f.ModelPanel.Entity.MMDHLEditorPreview~=nil")
        check('AME edits, previews, and saves below and above slot 31','client',"""local f=AdvMat.Editor
for _,i in ipairs({0,31,40,63}) do f.Working[i]={mat=f.Materials[i+1],shader='VertexLitGeneric',params={['$phongboost']='7'},gen={}} end
f:PushPreview() f:SaveAll() return true""")
        wait('server',"local set=AdvMat.Store.EntityRules[mmdhl.testEnt:EntIndex()] return set and set[63] and mmdhl.testEnt:GetSubMaterial(63)~='' ")
        check('Server/client overrides and UV geometry include overflow','client',"""local e=MMDHL_COMPAT_ENTITY local result={}
for _,i in ipairs({0,31,40,63}) do local path=e:GetSubMaterial(i) assert(path~='') assert(math.abs(Material(path):GetFloat('$phongboost')-7)<.01)
 local group=AdvMat.Mesh.Group(e:GetModel(),e:GetMaterials()[i+1],i,0) assert(group and #group.pos>0)
 local positions,posed=AdvMat.Mesh.WorldPositions(e,group,e:GetModel()) assert(posed and #positions==#group.pos) result[#result+1]={slot=i,vertices=#positions,override=path} end return result""")
        check('Native and overflow bodygroups default visible and can hide','server',"""local e=mmdhl.testEnt assert(e:GetNumBodyGroups()==65)
for i=1,64 do assert(e:GetBodygroupCount(i)==2 and e:GetBodygroup(i)==0) end
e:SetBodygroup(1,1) e:SetBodygroup(41,1) e:SetBodygroup(64,1) mmdhl.SetSecondaryCollisionMode(e,2)
local copied=duplicator.Copy(e) local pasted=duplicator.Paste(player.GetHumans()[1],copied.Entities,copied.Constraints) local c=pasted[e:EntIndex()] assert(IsValid(c) and mmdhl.IsMMD(c))
for _,i in ipairs({0,31,40,63}) do assert(c:GetSubMaterial(i)==e:GetSubMaterial(i)) end
assert(c:GetBodygroup(1)==1 and c:GetBodygroup(41)==1 and c:GetBodygroup(64)==1) assert(mmdhl.GetSecondaryCollisionMode(c)==2)
assert(c:GetPhysicsObjectCount()==18) c:Remove() return true""")
        wait('client',"return MMDHL_COMPAT_ENTITY:GetBodygroup(64)==1")
        check('Hidden material is absent from editor picking geometry','client',"""local e=MMDHL_COMPAT_ENTITY local mask=AdvMat.Mesh.BodyMask(e)
assert(not mmdhl.IsMaterialVisible(e,63)) assert(#mmdhl.GetModelMaterialMeshes(e:GetModel(),63,mask)==0)
local f=AdvMat.Editor assert(not f.ModelPanel.Entity.MMDHLEditorError) f:SetSize(1200,1040) f:Center() RunConsoleCommand('mmdhl_debug_capture','compatibility-materials','ui') return {bodygroup=e:GetBodygroupName(64),mask=mask}""")
        check('AME reset removes saved rules and overflow overrides','client',"""local f=AdvMat.Editor for _,i in ipairs({0,31,40,63}) do f.Selected=i f:DeleteSelected() end return true""")
        wait('server',"local e=mmdhl.testEnt return e:GetSubMaterial(0)=='' and e:GetSubMaterial(40)=='' and e:GetSubMaterial(63)==''")
        check('Ordinary Source entities keep the original material limit','server',"assert(AdvMat.GetMaterialLimit(player.GetHumans()[1])==32) return true")
    elif args.section=='eyes':
        check('Native eye pivots match the PMX skin','client',"""local e=MMDHL_COMPAT_ENTITY local r=mmdhl.GetRig(e) local probe=mmdhl.Decode(mmdhl.native.GetAlignmentProbe(mmdhl.GetInstance(e))) local out={}
for _,b in ipairs(probe.bones) do if b.source>=56 then assert(b.error<.02) out[#out+1]=b end end assert(#out==2) return out""")
        for order in ['target_first','bones_first']:
            check('Explicit zero bone controls win: '+order,'server',"""local e=mmdhl.testEnt local b=e:LookupBone('Eye_L')
"""+("e:SetEyeTarget(Vector(100,60,20)) e:ManipulateBonePosition(b,vector_origin)" if order=='target_first' else "e:ManipulateBonePosition(b,vector_origin) e:SetEyeTarget(Vector(100,60,20))")+" assert(e:GetManipulateBoneAngles(b)==angle_zero) assert(e.MMDHLEyeDriver=='bones') return true")
        saved=lua('server',"local out={} for _,n in ipairs({'sv_rpe_eye_track_bone_pos_ud','sv_rpe_eye_track_bone_pos_lr','sv_rpe_eye_track_use_bone','sv_rpe_eye_track_use_flex'}) do local c=GetConVar(n) if c then out[n]=c:GetString() end end return out")
        try:
            for scale in [0,.5,1]:
                check(f'Enhanced Expression Response eye-position scale {scale}','server',f"""local e=mmdhl.testEnt assert(isfunction(apply_eye_tracking),'Expression addon missing')
GetConVar('sv_rpe_eye_track_bone_pos_ud'):SetFloat({scale}) GetConVar('sv_rpe_eye_track_bone_pos_lr'):SetFloat({scale}) GetConVar('sv_rpe_eye_track_use_bone'):SetBool(true) GetConVar('sv_rpe_eye_track_use_flex'):SetBool(false)
local st={{overrideControlActive=true,overrideControlU=.4,overrideControlL=.5,overrideControlB=0,curU=.4,curL=.5,curB=0,last=CurTime()}}
apply_eye_tracking(e,nil,st,CurTime()) local b=e:LookupBone('Eye_L') local v=e:GetManipulateBonePosition(b)
assert(math.abs(v.x-.4*{scale})<.01 and math.abs(v.z+.5*{scale})<.01) assert(e:GetManipulateBoneAngles(b)==angle_zero)
return {{position={{v:Unpack()}},driver=e.MMDHLEyeDriver}}""")
                time.sleep(.3)
                check(f'Visible eye and native queries agree at scale {scale}','client',"""local e=MMDHL_COMPAT_ENTITY local p=mmdhl.Decode(mmdhl.native.GetAlignmentProbe(mmdhl.GetInstance(e))) local out={} for _,b in ipairs(p.bones) do if b.source>=56 then assert(b.error<.02) out[#out+1]=b end end return out""")
        finally:
            lua('server','for k,v in pairs(util.JSONToTable('+json.dumps(json.dumps(saved))+')) do GetConVar(k):SetString(v) end return true')
        time.sleep(.1)
        check('Target-only Eye Poser produces bounded rotation','server',"""local e=mmdhl.testEnt local b=e:LookupBone('Eye_L') e:SetEyeTarget(Vector(.001,100,100)) local a=e:GetManipulateBoneAngles(b)
assert(a:Forward().x==a:Forward().x and math.abs(a.p)<90 and math.abs(a.y)<90 and math.abs(a.r)<90) assert(a~=angle_zero) e:SetEyeTarget(vector_origin) assert(e:GetManipulateBoneAngles(b)==angle_zero) return {bounded={a:Unpack()},reset=true}""")
    elif args.section=='collisions':
        check('Scene snapshots identify all 18 owned Source objects','server',"mmdhl.SetSecondaryCollisionMode(mmdhl.testEnt,2) return true")
        wait('server','return mmdhl.sceneDiagnostics and mmdhl.sceneDiagnostics.ownedObjects==18')
        check('Moving prop receives no secondary impulses','server',"""local e=mmdhl.testEnt local prop=ents.Create('prop_physics') prop:SetModel('models/hunter/plates/plate1x1.mdl') prop:SetPos(e:GetPos()+Vector(0,0,-6)) prop:Spawn() local p=prop:GetPhysicsObject() p:EnableGravity(false) p:EnableDrag(false) p:SetVelocity(vector_origin)
for i=0,17 do constraint.NoCollide(e,prop,i,0) end mmdhl.compatProp=prop mmdhl.compatContacts={max=0,velocity=0,samples=0}
timer.Create('MMDHL.CompatibilityContacts',.05,80,function() if not IsValid(prop) then return end local out=mmdhl.compatContacts local d=mmdhl.GetDiagnostics(e) out.max=math.max(out.max,d.externalContacts) out.velocity=math.max(out.velocity,p:GetVelocity():Length()) out.samples=out.samples+1 out.capture=d.sceneCaptureMs out.sync=d.sceneSyncMs end) return true""")
        wait('server','return mmdhl.compatContacts.samples>=80')
        check('Authored secondary bodies make one-way contacts','server',"local s=mmdhl.compatContacts assert(s.max>0,'No external contacts') assert(s.velocity<.01,'Unexpected prop motion') return s")
        check('Mode changes and removal discard external proxies','server',"""mmdhl.compatProp:Remove() mmdhl.SetSecondaryCollisionMode(mmdhl.testEnt,0) assert(mmdhl.GetDiagnostics(mmdhl.testEnt).sourceMirrors==0) mmdhl.SetSecondaryCollisionMode(mmdhl.testEnt,1) return true""")
        time.sleep(.2)
        check('World-only mode retains static geometry','server',"local d=mmdhl.GetDiagnostics(mmdhl.testEnt) assert(d.sourceMirrors>0 and d.feedbackApplied==0) d.bodyList=nil return d")
    elif args.section=='morphs':
        for name in ['Xin','March7th']:
            lua('server','for _,e in ipairs(mmdhl.Entities()) do e:Remove() end return true')
            entity=spawn(models[name]['asset'])
            check(name+' Face Poser uses authored fallback labels with stable controllers','client',f"""local e=Entity({entity}) local p=controlpanel.Get('faceposer') p:ClearControls() weapons.GetStored('gmod_tool').Tool.faceposer.BuildCPanel(p,e)
local count,original,overflow=0,0,0 for _,m in ipairs(mmdhl.GetMorphs(e)) do local c=m.native>=0 and p.MMDNativeControls[m.native] or p.MMDOverflowControls[m.mmd]
assert(IsValid(c),m.name) assert(c.Label:GetText()==m.displayName,c.Label:GetText()..' != '..m.displayName)
if m.namingSource=='authored' then assert(m.displayName==m.original) original=original+1 end
if m.native>=0 then assert(e:GetFlexName(m.native)==m.name) count=count+1 else overflow=overflow+1 end end
assert(count==math.min(96,#mmdhl.GetMorphs(e)) and original>0) return {{native=count,overflow=overflow,originalNames=original}}""")
    elif args.section=='visibility':
        check('Context-menu bodygroup route hides an overflow material','client',"local e=MMDHL_COMPAT_ENTITY properties.List.bodygroups:SetBodyGroup(e,64,1) return true")
        wait('server','return mmdhl.testEnt:GetBodygroup(64)==1')
        check('Every material can hide without removing native physics','server',"local e=mmdhl.testEnt for i=1,#e:GetMaterials() do e:SetBodygroup(i,1) end assert(e:GetPhysicsObjectCount()==18) return true")
        wait('client','return MMDHL_COMPAT_ENTITY:GetBodygroup(1)==1 and MMDHL_COMPAT_ENTITY:GetBodygroup(64)==1')
        check('Hidden materials also disappear from editor geometry','client',"local e=MMDHL_COMPAT_ENTITY local mask=mmdhl.GetMaterialVisibilityMask(e) assert(#mmdhl.GetModelMaterialMeshes(e:GetModel(),nil,mask)==0) return mask")
        lua('client',"local e=MMDHL_COMPAT_ENTITY local c=e:WorldSpaceCenter() hook.Add('CalcView','MMDHL.VisibilityCamera',function() local offset=Vector(-210,-140,100) return {origin=c+offset,angles=(-offset):Angle(),fov=50,drawviewer=false} end) MMDHL_VISIBILITY_LAMP=ProjectedTexture() local l=MMDHL_VISIBILITY_LAMP l:SetPos(c+Vector(-100,70,100)) l:SetAngles((c-l:GetPos()):Angle()) l:SetFOV(65) l:SetNearZ(4) l:SetFarZ(600) l:SetBrightness(1) l:SetEnableShadows(true) l:SetTexture('effects/flashlight001') l:Update() return true")
        time.sleep(.5)
        lua('client',"RunConsoleCommand('mmdhl_debug_capture','compatibility-hidden-all') return true")
        time.sleep(.3)
        lua('server','for i=1,#mmdhl.testEnt:GetMaterials() do mmdhl.testEnt:SetBodygroup(i,0) end return true')
        time.sleep(.5)
        lua('client',"RunConsoleCommand('mmdhl_debug_capture','compatibility-visible-all') return true")
        time.sleep(.3)
        lua('client',"MMDHL_VISIBILITY_LAMP:Remove() hook.Remove('CalcView','MMDHL.VisibilityCamera') return true")
    elif args.section=='limit':
        for _ in range(5):
            spawn(models['March7th']['asset'])
        check('More than five live MMD ragdolls are accepted','server','assert(#mmdhl.Entities()==6) return #mmdhl.Entities()')
    lua('client',"if IsValid(AdvMat.Editor) then AdvMat.Editor:Close() end return true")
    lua('server','for _,e in ipairs(mmdhl.Entities()) do e:Remove() end return true')

if __name__=='__main__':
    main()
