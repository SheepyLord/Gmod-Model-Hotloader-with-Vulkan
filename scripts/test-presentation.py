"""Measure the rendered snapshot against actual client/EEI bone queries.

The sample runs after rendering, rather than comparing two server-side inputs.
Includes moving palettes, scale ratios, serialized engine hulls and EEI captures.
"""
import json,time,subprocess,sys
from gamectl import execute,ROOT
sys.path.insert(0,str(ROOT/'build/python-deps'))
import numpy as np
from scipy.spatial import ConvexHull
def lua(realm,code):
    r=execute(realm,code,45)
    if not r['ok']:raise RuntimeError(r.get('error'))
    return r.get('value')
def wait(realm,code):
    for _ in range(120):
        value=lua(realm,code)
        if value:return value
        time.sleep(.2)
    raise TimeoutError(code)
models=json.loads((ROOT/'validation/acceptance-models.json').read_text(encoding='utf-8'))
# Bring the owned game forward with the computer-use tool before this probe.
report={}
try:
    lua('client',"for _,n in ipairs({'MMDHL.TestCamera','MMDHL.VisualCamera','MMDHL.StressCamera','MMDHL.MotionCamera'}) do hook.Remove('CalcView',n) end return true")
    origin=lua('server',"local at=game.GetMap()=='gm_construct' and Vector(822,-800,0) or vector_origin local tr=util.TraceLine({start=at+Vector(0,0,2048),endpos=at-Vector(0,0,30000),mask=MASK_SOLID_BRUSHONLY}) return {(tr.HitPos+Vector(0,0,12)):Unpack()}")
    for name,model in models.items():
        entries=[]
        for ratio in (.5,1,2):
            print(name,ratio,'checking alignment',flush=True)
            lua('server',f"""for _,e in ipairs(mmdhl.Entities()) do e:Remove() end
local p=player.GetHumans()[1] p:SetMoveType(MOVETYPE_NOCLIP) p:SetPos(Vector({origin[0]-275},{origin[1]},{origin[2]+60})) p:SetEyeAngles(Angle(0,0,0)) MMDHL_READY=nil
mmdhl.Spawn(p,'{model['asset']}',{{backend='source',scaleMultiplier={ratio},position=util.JSONToTable('{json.dumps(origin)}'),frozen=true}},function(e,err) assert(IsValid(e),err) mmdhl.testEnt=e MMDHL_READY=e:EntIndex() end) return true""")
            entity=wait('server','return MMDHL_READY')
            time.sleep(.4)
            lua('client',f"MMDHL_VISUAL=Entity({entity}) gui.HideGameUI() if IsValid(g_SpawnMenu) then g_SpawnMenu:Close() end return true")
            lua('client',"""local e=MMDHL_VISUAL e:SetupBones() local center=e:GetBoneMatrix(4):GetTranslation() local offset=Vector(-155,15,5)*"""+str(ratio)+"""
hook.Add('CalcView','MMDHL.AlignmentCamera',function() return {origin=center+offset,angles=(-offset):Angle(),fov=42,drawviewer=false} end)
MMDHL_ALIGNMENT={frames=0,neutralFrames=0,morphFrames=0,bones=0,vertices=0,eei=0,eeiMatrix=0,errors={}}
hook.Add('PostRender','MMDHL.AlignmentProbe',function()
 local e=MMDHL_VISUAL if not IsValid(e) then return end local s=MMDHL_ALIGNMENT
 local probe=mmdhl.Decode(mmdhl.native.GetAlignmentProbe(mmdhl.GetInstance(e))) if not probe then return end
 s.frames=s.frames+1
 for _,b in ipairs(probe.bones or {}) do
  s.bones=math.max(s.bones,b.error or 1e6) local matrix=e:GetBoneMatrix(b.source) local pos=e:GetBonePosition(b.source)
  if pos==e:GetPos() and matrix then pos=matrix:GetTranslation() end
  s.eei=math.max(s.eei,pos:Distance(Vector(unpack(b.meshBone)))) s.eeiMatrix=math.max(s.eeiMatrix,matrix:GetTranslation():Distance(Vector(unpack(b.meshBone))))
 end
 -- This vertex oracle uses the authored neutral positions. Enabled expression
 -- addons may animate those vertices; bone alignment is still checked above.
 local neutral=true for _,weight in ipairs(mmdhl.Decode(mmdhl.native.GetMorphWeights(mmdhl.GetInstance(e))) or {}) do
  if math.abs(weight)>0.00001 then neutral=false break end
 end
 if neutral then s.neutralFrames=s.neutralFrames+1
  for _,v in ipairs(probe.vertices or {}) do if v.error>s.vertices then s.vertices=v.error s.worstVertex=v end end
 else s.morphFrames=s.morphFrames+1 end
 local d=mmdhl.GetDiagnostics(e) if d.sourceError and d.sourceError~='' then s.errors[#s.errors+1]=d.sourceError end
end) return true""")
            time.sleep(1)
            static=wait('client','if MMDHL_ALIGNMENT.neutralFrames>10 then return MMDHL_ALIGNMENT end')
            assert static['neutralFrames']>10 and max(static['bones'],static['vertices'],static['eei'])<.05,static
            lua('server',"""local e=mmdhl.testEnt local start=CurTime() local poses={} local origin=e:GetPos()
for i=0,17 do local p=e:GetPhysicsObjectNum(i) poses[i]={pos=p:GetPos()-origin,ang=p:GetAngles()} end
hook.Add('Tick','MMDHL.AlignmentMotion',function() if not IsValid(e) then return end local t=CurTime()-start
 local angle=Angle(0,math.sin(t*3)*25,0) local offset=origin+Vector(0,0,math.sin(t*4)*3)
 for i=0,17 do local p=e:GetPhysicsObjectNum(i) local pos,ang=LocalToWorld(poses[i].pos,poses[i].ang,offset,angle) p:SetPos(pos) p:SetAngles(ang) end
end) return true""")
            lua('client','MMDHL_ALIGNMENT={frames=0,neutralFrames=0,morphFrames=0,bones=0,vertices=0,eei=0,eeiMatrix=0,errors={}} return true')
            time.sleep(2)
            moving=wait('client','if MMDHL_ALIGNMENT.neutralFrames>10 then return MMDHL_ALIGNMENT end')
            assert moving['neutralFrames']>10 and max(moving['bones'],moving['vertices'],moving['eeiMatrix'])<.1 and not moving['errors'],moving
            lua('server',"hook.Remove('Tick','MMDHL.AlignmentMotion') return true")
            lua('client',"hook.Remove('PostRender','MMDHL.AlignmentProbe') mmdhl.RequestCollisionMesh(MMDHL_VISUAL) return true")
            wait('client','return MMDHL_VISUAL.MMDHLActualCollision~=nil')
            hulls=lua('client',"local e=MMDHL_VISUAL local r=mmdhl.GetRig(e) local out={} for i,h in ipairs(e.MMDHLActualCollision) do out[i]={actual=h.vertices,expected=r.bodies[i].hull} end return out")
            # VPhysics merges near-coincident vertices. Compare convex surfaces,
            # not vertex indices/counts or distances to a different triangulation.
            errors=[]
            for h in hulls:
                a,b=np.array(h['actual']),np.array(h['expected'])
                ea,eb=ConvexHull(a).equations,ConvexHull(b).equations
                errors.append(float(max(0,(a@eb[:,:3].T+eb[:,3]).max(),(b@ea[:,:3].T+ea[:,3]).max())))
            assert len(hulls)==18 and max(errors)<.02*ratio,errors
            if ratio==1:
                # Installed EEI's real bone and server-physics views.
                lua('server',"local p=player.GetHumans()[1] p:Give('gmod_tool') p:SelectWeapon('gmod_tool') local t=p:GetWeapon('gmod_tool'):GetToolObject('rb655_easy_inspector') assert(t,'EEI is not mounted') t:SetSelectedEntity(mmdhl.testEnt) p:GetWeapon('gmod_tool'):SetNWInt('rb655_inspector_func',2) return true")
                lua('client',"hook.Add('HUDPaint','MMDHL.EEIReview',function() local t=LocalPlayer():GetTool('rb655_easy_inspector') if t then t:DrawHUD(true) end end) return true")
                time.sleep(.3);lua('client',f"RunConsoleCommand('mmdhl_debug_capture','{name.lower()}-eei-bones') return true");time.sleep(.3)
                lua('server',"player.GetHumans()[1]:GetWeapon('gmod_tool'):SetNWInt('rb655_inspector_func',3) return true")
                time.sleep(.3);lua('client',f"RunConsoleCommand('mmdhl_debug_capture','{name.lower()}-eei-collision') return true");time.sleep(.3)
                lua('client',"hook.Remove('HUDPaint','MMDHL.EEIReview') return true")
            entries.append({'ratio':ratio,'static':static,'moving':moving,'serializedHullMaxError':max(errors)})
        report[name]=entries
        (ROOT/'validation/presentation-alignment.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        print(name,'PASS: 0.5/1/2 scale, rendered bones/vertices, moving client palette, EEI, 18 serialized hulls',flush=True)
finally:
    lua('server',"hook.Remove('Tick','MMDHL.AlignmentMotion') return true")
    lua('client',"hook.Remove('PostRender','MMDHL.AlignmentProbe') hook.Remove('HUDPaint','MMDHL.EEIReview') hook.Remove('CalcView','MMDHL.AlignmentCamera') return true")
