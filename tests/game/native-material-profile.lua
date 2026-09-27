local e=assert(MMDHL_VISUAL)
local info=mmdhl.assets[mmdhl.GetAsset(e)]
local result={materials={},resolution={ScrW(),ScrH()},error=mmdhl.renderError}
assert(not result.error,result.error)
for i,p in ipairs(info.materials) do
 local mat=mmdhl.sourceMaterials[mmdhl.GetAsset(e)][i]
 assert(mat:GetShader()=='VertexLitGeneric')
 assert(mat:GetFloat('$phongboost')==24)
 assert(mat:GetInt('$phongalbedotint')==1 and mat:GetInt('$rimlight')==1)
 local maps={}
 for _,name in ipairs({'$bumpmap','$lightwarptexture','$phongexponenttexture'}) do
  local texture=mat:GetTexture(name) assert(texture and not texture:IsError(),name..' is missing') maps[name]=texture:GetName()
 end
 result.materials[i]={name=p.name,alpha=p.alpha,cutout=p.alphaTexture and not p.translucentTexture,blended=p.translucentTexture,maps=maps}
end
return result
