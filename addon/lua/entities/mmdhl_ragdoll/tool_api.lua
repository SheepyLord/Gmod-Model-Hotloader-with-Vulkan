-- Vanilla stools call these through the class-scoped Entity dispatch adapter.
local function vector(t) return Vector(unpack(t or {0,0,0})) end
local function normalName(s)
 for i,v in ipairs({'０','１','２','３','４'}) do s=string.Replace(s,v,tostring(i-1)) end
 return string.lower(s):gsub('[%s_.%-]','')
end
function ENT:MMDToolInfo()
 local id=self:GetAsset()
 if self.MMDInfoAsset==id and self.MMDInfo then return self.MMDInfo end
 if not mmdhl or not mmdhl.native then return end
 local native=mmdhl.native
 native.RequestAsset(id)
 local info=mmdhl.Decode(native.AssetInfo(id)) if not info then return end
 self.MMDInfoAsset=id self.MMDInfo=info self.MMDBoneLookup={} self.MMDFingerAxes={} self.MMDFlexMap={} self.FingerIndex=nil
 local lookup=self.MMDBoneLookup
 for i,b in ipairs(info.boneList) do lookup[normalName(b.name)]=i-1 if b.english~='' then lookup[normalName(b.english)]=i-1 end end
 local function find(names) for _,name in ipairs(names) do local b=lookup[normalName(name)] if b then return b end end end
 local function alias(name,names) local i=find(names) if i then lookup[normalName(name)]=i end return i end
 self.MMDEyes={find({'左目','eye_l','left eye'}),find({'右目','eye_r','right eye'})}
 self.MMDHead=find({'頭','head','J_Bip_C_Head'})
 for _,side in ipairs({{'L','左','left'},{'R','右','right'}}) do
  local s,jp,en=unpack(side)
  local hand=alias('ValveBiped.Bip01_'..s..'_Hand',{jp..'手首','wrist_'..s,en..' wrist','J_Bip_'..s..'_Hand'})
  local fingerNames={'親指','人指','中指','薬指','小指'} local english={'thumb','index','middle','ring','little'}
  local index=find({jp..'人指1',jp..'人差指1','J_Bip_'..s..'_Index1'}) local pinky=find({jp..'小指1','J_Bip_'..s..'_Little1'})
  local palm=Vector(0,-1,0)
  if hand and index and pinky then
   local h=vector(info.boneList[hand+1].position)
   local n=(vector(info.boneList[index+1].position)-h):Cross(vector(info.boneList[pinky+1].position)-h)
   if n:LengthSqr()>0.00001 then palm=n:GetNormalized() if palm.y>0 then palm=-palm end end
  end
  local thumbZero=find({jp..'親指0'})~=nil
  for f=0,4 do for part=0,2 do
   local number=part+((f==0 and thumbZero) and 0 or 1)
   local bone=alias('ValveBiped.Bip01_'..s..'_Finger'..f..(part==0 and '' or part),{jp..fingerNames[f+1]..number,jp..(f==1 and '人差指' or fingerNames[f+1])..number,en..' '..english[f+1]..(part+1),english[f+1]..(part+1)..'_'..s,'J_Bip_'..s..'_'..english[f+1]..(part+1)})
   if bone then
    local b=info.boneList[bone+1] local direction
    for _,child in ipairs(info.boneList) do if child.parent==bone then direction=vector(child.position)-vector(b.position) break end end
    if not direction or direction:LengthSqr()<0.00001 then direction=vector(b.position)-vector(info.boneList[(b.parent or -1)+1] and info.boneList[b.parent+1].position) end
    local curl=direction:Cross(palm):GetNormalized() if curl:LengthSqr()<.1 then curl=Vector(0,0,s=='L' and -1 or 1) end
    self.MMDFingerAxes[bone]={curl=curl,spread=palm,thumb=f==0}
   end
  end end
 end
 -- Face Poser has 96 controls. Prefer facial panels, then other safe morphs.
 for _,category in ipairs({1,2,3,4}) do for i,m in ipairs(info.morphInfo or {}) do
  if m.category==category and m.type~=10 and #self.MMDFlexMap<96 then self.MMDFlexMap[#self.MMDFlexMap+1]=i-1 end
 end end
 return info
end
function ENT:GetFlexNum() self:MMDToolInfo() return #(self.MMDFlexMap or {}) end
function ENT:GetFlexName(i) local info=self:MMDToolInfo() local id=self.MMDFlexMap and self.MMDFlexMap[i+1] return info and id and info.morphs[id+1] or '' end
function ENT:GetFlexType(i)
 local info=self:MMDToolInfo() local id=self.MMDFlexMap and self.MMDFlexMap[i+1]
 return info and id and ({'Eyebrows','Eyes','Mouth','Other'})[info.morphInfo[id+1].category] or 'Other'
end
function ENT:GetFlexBounds() return 0,1 end
function ENT:HasFlexManipulatior() return true end -- spelling used by vanilla Face Poser
function ENT:GetFlexScale() return self:GetNW2Float('MMDFlexScale',1) end
function ENT:GetFlexWeight(i)
 self:MMDToolInfo() local id=self.MMDFlexMap and self.MMDFlexMap[i+1] if not id then return 0 end
 local weights=mmdhl.Decode(mmdhl.native.GetMorphWeights(self:GetInstance())) or {}
 local scale=self:GetFlexScale() return scale~=0 and (weights[id+1] or 0)/scale or 0
end
function ENT:SetFlexWeight(i,value)
 if CLIENT then return end
 self:MMDToolInfo() local id=self.MMDFlexMap and self.MMDFlexMap[i+1] if not id then return end
 self.MMDFlexWeights=self.MMDFlexWeights or {} self.MMDFlexWeights[i]=value
 mmdhl.native.SetMorph(self:GetInstance(),id,value*self:GetFlexScale())
end
function ENT:SetFlexScale(value)
 if CLIENT or value==self:GetFlexScale() then return end
 self:SetNW2Float('MMDFlexScale',value)
 for i,weight in pairs(self.MMDFlexWeights or {}) do self:SetFlexWeight(i,weight) end
end
function ENT:LookupBone(name) self:MMDToolInfo() return self.MMDBoneLookup and self.MMDBoneLookup[normalName(name)] end
function ENT:GetBoneCount() local info=self:MMDToolInfo() return info and #info.boneList or 0 end
function ENT:GetBoneName(i) local info=self:MMDToolInfo() return info and info.boneList[i+1] and info.boneList[i+1].name or '__INVALIDBONE__' end
function ENT:GetBoneParent(i) local info=self:MMDToolInfo() return info and info.boneList[i+1] and info.boneList[i+1].parent or -1 end
function ENT:GetBoneMatrix(i)
 if not isnumber(i) or i<0 or not mmdhl then return end
 local pose=mmdhl.Decode(mmdhl.native.GetBoneTransform(self:GetInstance(),i)) if not pose then return end
 local matrix=Matrix() matrix:SetTranslation(vector(pose.position)) matrix:SetAngles(Angle(unpack(pose.angles))) return matrix
end
function ENT:GetBonePosition(i) local matrix=self:GetBoneMatrix(i) if matrix then return matrix:GetTranslation(),matrix:GetAngles() end end
function ENT:GetManipulateBoneAngles(i) return self.MMDAngles and self.MMDAngles[i] or angle_zero end
local function multiply(a,b)
 return {a[4]*b[1]+a[1]*b[4]+a[2]*b[3]-a[3]*b[2],a[4]*b[2]-a[1]*b[3]+a[2]*b[4]+a[3]*b[1],a[4]*b[3]+a[1]*b[2]-a[2]*b[1]+a[3]*b[4],a[4]*b[4]-a[1]*b[1]-a[2]*b[2]-a[3]*b[3]}
end
local function axis(axis,degrees) local h=math.rad(degrees)*.5 local s=math.sin(h) return {axis.x*s,axis.y*s,axis.z*s,math.cos(h)} end
function ENT:ManipulateBoneAngles(i,angle)
 if CLIENT then return end
 self:MMDToolInfo() if i<0 or i>=self:GetBoneCount() then return end
 local old=self:GetManipulateBoneAngles(i) if old==angle then return end
 self.MMDAngles=self.MMDAngles or {} self.MMDAngles[i]=Angle(angle.p,angle.y,angle.r)
 local axes=self.MMDFingerAxes[i] local rotation
 if axes then rotation=multiply(axis(axes.curl,axes.thumb and angle.y or angle.p),axis(axes.spread,axes.thumb and angle.p or angle.y))
 else rotation=multiply(multiply(axis(Vector(1,0,0),angle.p),axis(Vector(0,1,0),angle.y)),axis(Vector(0,0,1),angle.r)) end
 mmdhl.native.SetBonePose(self:GetInstance(),i,util.TableToJSON({rotation=rotation}))
end
function ENT:LookupAttachment(name)
 self:MMDToolInfo()
 return name=='eyes' and self.MMDEyes and self.MMDEyes[1] and self.MMDEyes[2] and 1 or 0
end
function ENT:GetAttachment(i)
 if i~=1 or self:LookupAttachment('eyes')~=1 then return end
 local left,right=self:GetBoneMatrix(self.MMDEyes[1]),self:GetBoneMatrix(self.MMDEyes[2])
 local head=self.MMDHead and self:GetBoneMatrix(self.MMDHead)
 if not left or not right then return end
 local a=head and head:GetAngles() or self:GetAngles() a:RotateAroundAxis(a:Up(),180)
 return {Pos=(left:GetTranslation()+right:GetTranslation())*.5,Ang=a}
end
function ENT:SetEyeTarget(target)
 if CLIENT or (self.MMDEyeTarget and self.MMDEyeTarget==target) then return end
 local attachment=self:GetAttachment(1) if not attachment then return end
 self.MMDEyeTarget=Vector(target.x,target.y,target.z)
 for _,bone in ipairs(self.MMDEyes) do
  local localEye=WorldToLocal(self:GetBoneMatrix(bone):GetTranslation(),angle_zero,attachment.Pos,attachment.Ang)
  local a=(target-localEye):Angle() local pitch,yaw=math.Clamp(math.NormalizeAngle(a.p),-45,45),math.Clamp(math.NormalizeAngle(a.y),-45,45)
  if target==vector_origin then pitch=0 yaw=0 end
  -- MMD eyes face -Z; the synthetic Source attachment faces local +X.
  local x,y=-math.rad(pitch)*.5,-math.rad(yaw)*.5
  local rotation={math.sin(x)*math.cos(y),math.cos(x)*math.sin(y),-math.sin(x)*math.sin(y),math.cos(x)*math.cos(y)}
  mmdhl.native.SetBonePose(self:GetInstance(),bone,util.TableToJSON({rotation=rotation}))
 end
end
function ENT:TestCollision(start,delta,isbox)
 if not mmdhl or self:GetInstance()<1 then return false end
 local hit=mmdhl.Decode(mmdhl.native.Raycast(start,start+delta,self:GetInstance(),true))
 if not hit then return false end
 return {HitPos=vector(hit.position),Normal=vector(hit.normal),Fraction=hit.fraction}
end
