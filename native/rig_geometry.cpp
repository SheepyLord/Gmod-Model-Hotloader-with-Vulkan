#include "rig_geometry.hpp"
#include "rig_animation.hpp"
#include <numeric>
#include <set>
namespace mmd {
namespace {
std::vector<int> owners(const Model& m,const Rig& r){
 std::vector<int> out(m.bones.size(),-1);for(size_t i=0;i<r.bones.size();i++){auto& b=r.bones[i];if(b.mmd>=0)out[b.mmd]=int(i);for(int a:b.aliases)out[a]=int(i);}
 for(size_t i=0;i<out.size();i++)if(out[i]<0){int p=m.bones[i].parent;size_t depth=0;while(p>=0&&depth++<m.bones.size()){if(out[p]>=0){out[i]=out[p];break;}p=m.bones[p].parent;}}
 return out;
}
bool arm(const std::string& name){return name.find("UpperArm")!=name.npos||name.find("Forearm")!=name.npos||name.find("Hand")!=name.npos||name.find("Finger")!=name.npos;}
bool viewArm(const std::string& name){return name.find("Forearm")!=name.npos||name.find("Hand")!=name.npos||name.find("Finger")!=name.npos;}
bool head(const std::string& name){return name.find("Head")!=name.npos||name.find("Neck")!=name.npos||name.find("Eye")!=name.npos;}
btTransform from(const Json& j){auto p=j.at("position"),q=j.at("rotation");return btTransform(btQuaternion(q[0],q[1],q[2],q[3]),btVector3(p[0],p[1],p[2]));}
struct Skin {std::array<int,3> bones{};std::array<float,3> weights{};int count=0;};
Skin weights(const Vertex& v,const std::vector<int>& map){std::map<int,float> sum;for(int i=0;i<4;i++)if(v.bones[i]>=0&&v.weights[i]>0&&map[v.bones[i]]>=0)sum[map[v.bones[i]]]+=v.weights[i];std::vector<std::pair<int,float>> sorted(sum.begin(),sum.end());std::sort(sorted.begin(),sorted.end(),[](auto a,auto b){return a.second>b.second;});Skin out;out.count=int(std::min<size_t>(3,sorted.size()));float total=0;for(int i=0;i<out.count;i++){out.bones[i]=sorted[i].first;out.weights[i]=sorted[i].second;total+=out.weights[i];}if(total==0){out.count=1;out.bones[0]=0;out.weights[0]=1;}else for(auto& w:out.weights)w/=total;return out;}
}
std::vector<uint8_t> firstPersonTriangles(const Model& m,const Rig& r,bool arms){
 auto map=owners(m,r);std::vector<float> ownership(m.vertices.size(),0);
 for(size_t i=0;i<m.vertices.size();i++){auto& v=m.vertices[i];for(int j=0;j<4;j++)if(v.bones[j]>=0&&map[v.bones[j]]>=0){auto& name=r.bones[map[v.bones[j]]].name;if(arms?viewArm(name):(arm(name)||head(name)))ownership[i]+=v.weights[j];}}
 // A Lua round trip writes an empty choice set as []; any non-object means none.
 auto parts=r.manifest.value("armsParts",Json::object());if(!parts.is_object())parts=Json::object();std::vector<uint8_t> mask(m.indices.size()/3,0);
 for(size_t p=0;p<m.materials.size();p++){auto& mat=m.materials[p];int mode=parts.value(std::to_string(p),0);for(unsigned i=mat.first;i<mat.first+mat.count;i+=3){float amount=0;for(int j=0;j<3;j++)amount+=ownership[m.indices[i+j]];bool include=arms?(amount>=1.5f):(amount<.15f);if(arms&&r.manifest["materials"][p].value("defaultHidden",false))include=false;if(arms&&mode!=0)include=mode>0;mask[i/3]=include;}}
 return mask;
}
void writeArmsGeometry(StudioWriter& w,StudioWriter& vvd,StudioWriter& vtx,const Rig& r,const Model& m){
 auto mask=firstPersonTriangles(m,r,true);auto map=owners(m,r);auto& reference=r.manifest.at("animation").at("reference");
 std::vector<btTransform> bind(r.bones.size()),delta(r.bones.size());
 int bones;std::memcpy(&bones,w.b.data()+160,4);
 for(size_t i=0;i<bind.size();i++){auto local=from(reference[i]);auto parent=r.bones[i].parent;bind[i]=parent<0?local:bind[parent]*local;delta[i]=bind[i]*r.bones[i].rest.inverse();auto p=bones+i*216;auto q=local.getRotation();w.vec(p+32,local.getOrigin());for(int j=0;j<4;j++)w.f(p+44+j*4,q[j]);float z,y,x;local.getBasis().getEulerZYX(z,y,x);w.vec(p+60,{x,y,z});w.matrix(p+96,bind[i].inverse());}
 // Bone merge uses the stock c_arms bind. Bake the fitted mesh into that bind;
 // no Lua bone copying or second secondary-physics world is needed.
 auto facing=rigMeshBind(r);
 struct Mesh{unsigned material;std::vector<unsigned> vertices;std::vector<uint16_t> indices;};std::vector<Mesh> meshes;
 for(unsigned p=0;p<m.materials.size();p++){Mesh mesh{p};std::map<unsigned,uint16_t> remap;auto flush=[&](){if(!mesh.indices.empty()){meshes.push_back(std::move(mesh));mesh=Mesh{p};remap.clear();}};
  auto& mat=m.materials[p];for(unsigned i=mat.first;i<mat.first+mat.count;i+=3){if(!mask[i/3])continue;if(mesh.vertices.size()+3>60000)flush();for(unsigned j=0;j<3;j++){// PMX -> Source changes handedness.
   auto index=m.indices[i+2-j];auto [it,added]=remap.emplace(index,uint16_t(mesh.vertices.size()));if(added)mesh.vertices.push_back(index);mesh.indices.push_back(it->second);
  }}flush();}
 if(meshes.empty())throw std::runtime_error("No first-person arm geometry; choose included materials in the arms editor");
 std::vector<unsigned> slots;for(auto& mesh:meshes)if(std::find(slots.begin(),slots.end(),mesh.material)==slots.end())slots.push_back(mesh.material);
 if(slots.size()>128)throw std::runtime_error("First-person arms exceed Source's 128 material slots; narrow the selected arm parts");
 auto textures=w.alloc(slots.size()*64),skins=w.alloc(slots.size()*2);w.i(204,int(slots.size()));w.i(208,int(textures));w.i(220,int(slots.size()));w.i(224,1);w.i(228,int(skins));for(size_t i=0;i<slots.size();i++){w.relstr(textures+i*64,textures+i*64,r.manifest["materials"][slots[i]]["path"].get<std::string>());w.put<uint16_t>(skins+i*2,uint16_t(i));}
 auto bp=w.alloc(16),model=w.alloc(148),meshBase=w.alloc(meshes.size()*116);w.i(232,1);w.i(236,int(bp));w.relstr(bp,bp,"arms");w.i(bp+4,1);w.i(bp+8,1);w.i(bp+12,int(model-bp));w.fixed(model,64,"MMD c_arms");w.f(model+68,100);w.i(model+72,int(meshes.size()));w.i(model+76,int(meshBase-model));
 size_t total=0;for(auto& mesh:meshes)total+=mesh.vertices.size();w.i(model+80,int(total));
 vvd.b.resize(64);vvd.i(16,int(total));auto vertices=vvd.alloc(total*48),tangents=vvd.alloc(total*16);vvd.i(52,64);vvd.i(56,int(vertices));vvd.i(60,int(tangents));
 vtx.b.resize(44);vtx.i(28,1);auto vb=vtx.alloc(8),vm=vtx.alloc(8),lod=vtx.alloc(12),vmeshes=vtx.alloc(meshes.size()*9);vtx.i(32,int(vb));vtx.i(vb,1);vtx.i(vb+4,int(vm-vb));vtx.i(vm,1);vtx.i(vm+4,int(lod-vm));vtx.i(lod,int(meshes.size()));vtx.i(lod+4,int(vmeshes-lod));
 size_t cursor=0;btVector3 boundMin(BT_LARGE_FLOAT,BT_LARGE_FLOAT,BT_LARGE_FLOAT),boundMax=-boundMin;
 for(size_t i=0;i<meshes.size();i++){
  auto& mesh=meshes[i];auto md=meshBase+i*116;auto slot=std::find(slots.begin(),slots.end(),mesh.material)-slots.begin();w.i(md,int(slot));w.i(md+4,int(model-md));w.i(md+8,int(mesh.vertices.size()));w.i(md+12,int(cursor));w.i(md+32,int(i));for(int lod=0;lod<8;lod++)w.i(md+52+lod*4,int(mesh.vertices.size()));
  auto mh=vmeshes+i*9,group=vtx.alloc(25),verts=vtx.alloc(mesh.vertices.size()*9),indices=vtx.alloc(mesh.indices.size()*2),strip=vtx.alloc(27);vtx.i(mh,1);vtx.i(mh+4,int(group-mh));vtx.i(group,int(mesh.vertices.size()));vtx.i(group+4,int(verts-group));vtx.i(group+8,int(mesh.indices.size()));vtx.i(group+12,int(indices-group));vtx.i(group+16,1);vtx.i(group+20,int(strip-group));vtx.b[group+24]=0; // Native software skinning: stable across Source DX9 hardware palettes.
  int maxWeights=1;
  for(size_t j=0;j<mesh.vertices.size();j++){
   auto vi=mesh.vertices[j];auto& vertex=m.vertices[vi];auto skin=weights(vertex,map);auto p=vertices+(cursor+j)*48,t=tangents+(cursor+j)*16,v=verts+j*9;
   btVector3 position(0,0,0),normal(0,0,0),tangent(0,0,0);auto raw=facing*(toSource(vertex.position)*r.scale),n=facing.getBasis()*toSource(vertex.normal);auto tn=facing.getBasis()*toSource(vi<m.tangents.size()?m.tangents[vi]:btVector3(1,0,0));
   for(int k=0;k<skin.count;k++){int bone=skin.bones[k];vvd.f(p+k*4,skin.weights[k]);vvd.b[p+12+k]=uint8_t(bone);vtx.b[v+k]=uint8_t(k);vtx.b[v+6+k]=uint8_t(bone);position+=delta[bone]*raw*skin.weights[k];normal+=delta[bone].getBasis()*n*skin.weights[k];tangent+=delta[bone].getBasis()*tn*skin.weights[k];}
   maxWeights=std::max(maxWeights,skin.count);boundMin.setMin(position);boundMax.setMax(position);
   vvd.b[p+15]=uint8_t(skin.count);vvd.vec(p+16,position);vvd.vec(p+28,normal.normalized());vvd.f(p+40,vertex.uv[0]);vvd.f(p+44,vertex.uv[1]);vvd.vec(t,tangent.safeNormalize());vvd.f(t+12,vi<m.tangentSigns.size()?-m.tangentSigns[vi]:1);vtx.b[v+3]=uint8_t(skin.count);vtx.put<uint16_t>(v+4,uint16_t(j));
  }
  for(size_t j=0;j<mesh.indices.size();j++)vtx.put<uint16_t>(indices+j*2,mesh.indices[j]);vtx.i(strip,int(mesh.indices.size()));vtx.i(strip+8,int(mesh.vertices.size()));vtx.put<int16_t>(strip+16,int16_t(maxWeights));vtx.b[strip+18]=1;vtx.i(strip+19,0);cursor+=mesh.vertices.size();
 }
 // Hands are driven by native viewmodel bone merge. Its bind is already stock.
 for(int p:{104,128})w.vec(p,boundMin);for(int p:{116,140})w.vec(p,boundMax);w.i(152,0x80);
 for(int i=1;i<8;i++)vvd.i(16+i*4,int(total));
 Rig arms=r;for(size_t i=0;i<bind.size();i++)arms.bones[i].rest=bind[i];writeAnimations(w,arms,bones,boundMin,boundMax);w.i(188,3);w.i(180,3);w.i(336,0);w.i(340,0);w.i(76,int(w.b.size()));
}
}
