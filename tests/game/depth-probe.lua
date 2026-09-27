local e=mmdhl.Entities()[1] local id=mmdhl.GetAsset(e) local mats={}
for i,p in ipairs(mmdhl.assets[id].materials) do
 local mat=CreateMaterial('mmdhl_phong_depth_probe_'..id..'_'..i,'VertexLitGeneric',{['$basetexture']='models/debug/debugwhite',['$model']='1',['$vertexcolor']='1',['$vertexalpha']='0',['$nocull']=p.twoSided and '1' or '0',['$translucent']=(p.alpha<.999 or p.alphaTexture) and '1' or '0',['$alphatest']=p.alphaTexture and '1' or '0',['$alphatestreference']='.005',['$halflambert']='1',['$phong']='1',['$phongboost']='.15',['$phongexponent']='24'})
 if p.base and p.base~='' then mat:SetTexture('$basetexture',Material('../data/mmd_hotloader/textures/'..p.base..'.png','smooth'):GetTexture('$basetexture')) end
 mats[i]=mat
end
mmdhl.sourceMaterials[id]=mats
hook.Remove('PostDrawOpaqueRenderables','MMDHL.CompareShader') include('mmdhl/carrier.lua')
timer.Simple(1,function() RunConsoleCommand('mmdhl_debug_capture','xin-phong-depth-probe') end)
return true
