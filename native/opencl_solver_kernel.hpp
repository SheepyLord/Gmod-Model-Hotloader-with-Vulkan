/*
Bullet Continuous Collision Detection and Physics Library
Copyright (c) 2003-2006 Erwin Coumans  https://bulletphysics.org

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it freely,
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/
// Altered: OpenCL port of the Bullet scalar row equations.
#pragma once
namespace mmd {
inline constexpr const char* SolverKernel=R"CLC(
#pragma OPENCL FP_CONTRACT OFF
typedef struct {float4 linear,angular,push,turn,invMass,linearFactor,angularFactor;} Body;
typedef struct {float4 normalA,normalB,relA,relB,angularA,angularB,params,limits;int4 ids;} Row;
typedef struct {int4 rows;int4 levels;int4 other;float4 settings;} Island;
inline float dot3(float4 a,float4 b){return (a.x*b.x+a.y*b.y)+a.z*b.z;}
inline void solve_row(__global Body* bodies,__global const Row* rows,__global float2* impulses,int index,int split){
 Row r=rows[index];int ia=r.ids.x,ib=r.ids.y;float2 applied=impulses[index];
 float lower=r.limits.x,upper=r.limits.y;
 if(split&&r.limits.z==0)return;
 if(!split&&r.ids.z>=0){float normal=impulses[r.ids.z].x;if(normal<=0)return;float magnitude=r.params.w*normal;if(r.ids.w==3)magnitude=fmin(magnitude,r.params.w);lower=-magnitude;upper=magnitude;}
 float4 la=split?bodies[ia].push:bodies[ia].linear,aa=split?bodies[ia].turn:bodies[ia].angular;
 float4 lb=split?bodies[ib].push:bodies[ib].linear,ab=split?bodies[ib].turn:bodies[ib].angular;
 float previous=split?applied.y:applied.x;
 float delta=(split?r.limits.z:r.params.x)-previous*r.params.y;
 delta-=(dot3(r.normalA,la)+dot3(r.relA,aa))*r.params.z;
 delta-=(dot3(r.normalB,lb)+dot3(r.relB,ab))*r.params.z;
 float total=previous+delta;
 // Contacts use a unilateral lower-limit row; joints and friction have both bounds.
 float next=total;
 if(total<lower){next=lower;delta=lower-previous;}
 else if(!split&&r.ids.w!=1&&total>upper){next=upper;delta=upper-previous;}
 if(bodies[ia].invMass.w!=0){
  la+=(r.normalA*bodies[ia].invMass)*delta*bodies[ia].linearFactor;
  aa+=r.angularA*(delta*bodies[ia].angularFactor);
  if(split){bodies[ia].push=la;bodies[ia].turn=aa;}else{bodies[ia].linear=la;bodies[ia].angular=aa;}
 }
 if(bodies[ib].invMass.w!=0){
  lb+=(r.normalB*bodies[ib].invMass)*delta*bodies[ib].linearFactor;
  ab+=r.angularB*(delta*bodies[ib].angularFactor);
  if(split){bodies[ib].push=lb;bodies[ib].turn=ab;}else{bodies[ib].linear=lb;bodies[ib].angular=ab;}
 }
 if(split)applied.y=next;else applied.x=next;impulses[index]=applied;
}
inline void solve_row_local(__global Body* bodies,__global const Row* rows,__global float2* impulses,__local float4* linear,__local float4* angular,int offset,int index,int split){
 Row r=rows[index];int ia=r.ids.x,ib=r.ids.y;float2 applied=impulses[index];
 float lower=r.limits.x,upper=r.limits.y;
 if(split&&r.limits.z==0)return;
 if(!split&&r.ids.z>=0){float normal=impulses[r.ids.z].x;if(normal<=0)return;float magnitude=r.params.w*normal;if(r.ids.w==3)magnitude=fmin(magnitude,r.params.w);lower=-magnitude;upper=magnitude;}
 float4 la=linear[ia-offset],aa=angular[ia-offset];
 float4 lb=linear[ib-offset],ab=angular[ib-offset];
 float previous=split?applied.y:applied.x;
 float delta=(split?r.limits.z:r.params.x)-previous*r.params.y;
 delta-=(dot3(r.normalA,la)+dot3(r.relA,aa))*r.params.z;
 delta-=(dot3(r.normalB,lb)+dot3(r.relB,ab))*r.params.z;
 float total=previous+delta;
 // Contacts use a unilateral lower-limit row; joints and friction have both bounds.
 float next=total;
 if(total<lower){next=lower;delta=lower-previous;}
 else if(!split&&r.ids.w!=1&&total>upper){next=upper;delta=upper-previous;}
 if(bodies[ia].invMass.w!=0){
  la+=(r.normalA*bodies[ia].invMass)*delta*bodies[ia].linearFactor;
  aa+=r.angularA*(delta*bodies[ia].angularFactor);
  linear[ia-offset]=la;angular[ia-offset]=aa;
 }
 if(bodies[ib].invMass.w!=0){
  lb+=(r.normalB*bodies[ib].invMass)*delta*bodies[ib].linearFactor;
  ab+=r.angularB*(delta*bodies[ib].angularFactor);
  linear[ib-offset]=lb;angular[ib-offset]=ab;
 }
 if(split)applied.y=next;else applied.x=next;impulses[index]=applied;
}
__kernel void solve_islands(__global Body* bodies,__global const Row* rows,__global float2* impulses,__global const int2* groups,__global const int2* levels,__global const Island* islands){
 Island island=islands[get_group_id(0)];int tid=get_local_id(0),width=get_local_size(0);
 __local float4 linear[1024],angular[1024];
 int offset=island.rows.w,count=island.other.z,useLocal=count<=1024;
 if(useLocal)for(int i=tid;i<count;i+=width){linear[i]=bodies[offset+i].push;angular[i]=bodies[offset+i].turn;}
 barrier(CLK_LOCAL_MEM_FENCE);
 if(island.other.y)for(int iteration=0;iteration<island.other.w;iteration++){
  for(int level=island.levels.y;level<island.levels.z;level++){
   int2 range=levels[level];for(int group=range.x+tid;group<range.y;group+=width){int2 rr=groups[group];for(int row=rr.x;row<rr.y;row++){
    if(useLocal)solve_row_local(bodies,rows,impulses,linear,angular,offset,row,1);else solve_row(bodies,rows,impulses,row,1);
   }}barrier(useLocal?CLK_LOCAL_MEM_FENCE:CLK_GLOBAL_MEM_FENCE);
   if(level+1==island.levels.z)barrier(CLK_GLOBAL_MEM_FENCE);
  }
 }
 if(useLocal)for(int i=tid;i<count;i+=width){bodies[offset+i].push=linear[i];bodies[offset+i].turn=angular[i];linear[i]=bodies[offset+i].linear;angular[i]=bodies[offset+i].angular;}
 barrier(CLK_LOCAL_MEM_FENCE|CLK_GLOBAL_MEM_FENCE);
 for(int iteration=0;iteration<island.other.x;iteration++){
  for(int level=island.levels.x;level<island.levels.w;level++){
   int2 range=levels[level];for(int group=range.x+tid;group<range.y;group+=width){int2 rr=groups[group];for(int row=rr.x;row<rr.y;row++){
    if(iteration >= (rows[row].ids.w==0?(int)rows[row].limits.w:island.other.w))continue;
    if(useLocal)solve_row_local(bodies,rows,impulses,linear,angular,offset,row,0);else solve_row(bodies,rows,impulses,row,0);
   }}barrier(useLocal?CLK_LOCAL_MEM_FENCE:CLK_GLOBAL_MEM_FENCE);
   if(level+1==island.levels.z)barrier(CLK_GLOBAL_MEM_FENCE);
  }
 }
 if(useLocal)for(int i=tid;i<count;i+=width){bodies[offset+i].linear=linear[i];bodies[offset+i].angular=angular[i];}

}
__kernel void probe(__global float4* result){result[0]=(float4)(1.25f,2.5f,5.0f,10.0f);}
)CLC";
}
