// Source world-space vertex contract. CPU deformation is shared with Bullet.
float4x4 ViewProjection : register(c8);
struct Input { float4 position:POSITION; float3 normal:NORMAL; float4 color:COLOR0; float2 uv:TEXCOORD0; float4 extra:TEXCOORD1; };
struct Output {float4 position:POSITION;float2 uv:TEXCOORD0;float3 normal:TEXCOORD1;float3 world:TEXCOORD2;float4 extra:TEXCOORD3;float2 depth:TEXCOORD4;};
Output main(Input v){Output o;o.position=mul(float4(v.position.xyz,1),ViewProjection);o.depth=o.position.zw;o.uv=v.uv;o.normal=v.normal;o.world=v.position.xyz;o.extra=v.extra;return o;}
