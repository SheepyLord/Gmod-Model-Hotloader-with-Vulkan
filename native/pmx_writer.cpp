#include "pmx_writer.hpp"
namespace mmd {
using Vec3=glm::vec3;using Vec4=glm::vec4;
Bytes writePmx(const PmxData& d){
 PmxWriter w;w.b.insert(w.b.end(),{'P','M','X',' '});w.f32(2.f);w.u8(8);for(uint8_t v:{uint8_t(1),uint8_t(0),uint8_t(4),uint8_t(4),uint8_t(4),uint8_t(4),uint8_t(4),uint8_t(4)})w.u8(v);
 w.text(d.name);w.text(d.name);w.text(d.comment);w.text(d.comment);
 w.i32(int32_t(d.vertices.size()));
 for(auto& v:d.vertices){w.v3(v.p);w.v3(v.n);w.v2(v.uv);int n=0;for(int k=0;k<4;k++)n+=v.bone[k]>=0;
  if(n<=1){w.u8(0);w.i32(v.bone[0]);}
  else if(n==2){w.u8(1);w.i32(v.bone[0]);w.i32(v.bone[1]);w.f32(v.weight[0]);}
  else{w.u8(2);for(int k=0;k<4;k++)w.i32(v.bone[k]);for(int k=0;k<4;k++)w.f32(v.bone[k]>=0?v.weight[k]:0);}
  w.f32(1);}
 size_t indexCount=0;for(auto* mat:d.materials)indexCount+=mat->indices.size();w.i32(int32_t(indexCount));for(auto* mat:d.materials)for(auto i:mat->indices)w.i32(int32_t(i));
 w.i32(int32_t(d.textures.size()));for(auto& p:d.textures)w.text(p);
 w.i32(int32_t(d.materials.size()));
 for(auto* mat:d.materials){w.text(mat->name);w.text(mat->name);w.v4(mat->diffuse);w.v3(Vec3(0));w.f32(5);w.v3(Vec3(mat->diffuse)*.5f);
  w.u8(uint8_t((mat->twoSided?0x01:0)|0x02|0x04|0x08|(mat->edge?0x10:0)));w.v4(mat->edgeColor);w.f32(mat->edgeSize);
  w.i32(mat->texture);w.i32(mat->sphere);w.u8(uint8_t(mat->sphereMode));w.u8(0);w.i32(-1);w.text(mat->memo);w.i32(int32_t(mat->indices.size()));}
 w.i32(int32_t(d.bones.size()));
 for(auto& b:d.bones){w.text(b.name);w.text(b.english);w.v3(b.position);w.i32(b.parent);w.i32(0);
  uint16_t flags=0x0002|0x0008|0x0010;if(b.tail>=0)flags|=0x0001;if(b.parent<0)flags|=0x0004;if(b.inherit>=0)flags|=0x0100;if(b.fixed)flags|=0x0400;w.u16(flags);
  if(b.tail>=0)w.i32(b.tail);else w.v3(b.tailOffset);
  if(b.inherit>=0){w.i32(b.inherit);w.f32(b.inheritWeight);}
  if(b.fixed)w.v3(b.fixedAxis);}
 w.i32(int32_t(d.morphs.size()));
 for(auto& mo:d.morphs){w.text(mo.name);w.text(mo.english);w.u8(uint8_t(mo.panel));w.u8(uint8_t(mo.type));
  if(mo.type==0){w.i32(int32_t(mo.group.size()));for(auto& [i,wt]:mo.group){w.i32(i);w.f32(wt);}}
  else if(mo.type==1){w.i32(int32_t(mo.vertex.size()));for(auto& [i,dv]:mo.vertex){w.i32(int32_t(i));w.v3(dv);}}
  else if(mo.type==3){w.i32(int32_t(mo.uv.size()));for(auto& [i,dv]:mo.uv){w.i32(int32_t(i));w.v4(dv);}}
  else{w.i32(int32_t(mo.material.size()));for(auto& o:mo.material){w.i32(o.material);w.u8(1);w.v4(o.diffuse);w.v3(Vec3(0));w.f32(0);w.v3(Vec3(0));w.v4(Vec4(0));w.f32(0);w.v4(Vec4(0));w.v4(Vec4(0));w.v4(Vec4(0));}}}
 // Display frames: root, expressions, then every other bone.
 w.i32(3);
 w.text("Root");w.text("Root");w.u8(1);w.i32(1);w.u8(0);w.i32(0);
 w.text("表情");w.text("Exp");w.u8(1);w.i32(int32_t(d.morphs.size()));for(size_t i=0;i<d.morphs.size();i++){w.u8(1);w.i32(int32_t(i));}
 w.text("Bones");w.text("Bones");w.u8(0);w.i32(int32_t(d.bones.size()>0?d.bones.size()-1:0));for(size_t i=1;i<d.bones.size();i++){w.u8(0);w.i32(int32_t(i));}
 w.i32(0);w.i32(0); // no rigid bodies or joints: the converters' physics is simulated natively
 return std::move(w.b);
}
}
