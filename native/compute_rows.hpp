#pragma once
// Device-side layout of one solver batch, shared by the OpenCL and Vulkan
// backends. Every struct mirrors a std430 / OpenCL struct of float4 / int4.
#include <vector>
namespace mmd {
struct alignas(16) F4 {float x=0,y=0,z=0,w=0;};
struct I2 {int x=0,y=0;};
struct alignas(16) I4 {int x=0,y=0,z=0,w=0;};
struct F2 {float x=0,y=0;};
// invMass.w is 1 when the solver may write the body's velocities.
struct GBody {F4 linear,angular,push,turn,invMass,linearFactor,angularFactor;};
// params: rhs, cfm, jacDiagABInv, friction. limits: lower, upper, rhsPenetration,
// override iterations. ids: body A, body B, friction's normal row (or -1), pool.
struct GRow {F4 normalA,normalB,relA,relB,angularA,angularB,params,limits;I4 ids;};
// rows: joint, contact, end, first body. levels: joint, contact, friction, end.
// other: iterations, split impulse, body count, contact iterations.
struct GIsland {I4 rows,levels,other;F4 settings;};
static_assert(sizeof(GBody)==112&&sizeof(GRow)==144&&sizeof(GIsland)==64);
// A group is a row range one thread solves in order; a level is a range of
// groups that share no writable body and may run concurrently.
struct Buffers {
 std::vector<GBody> bodies;std::vector<GRow> rows;std::vector<F2> impulses;std::vector<I2> groups,levels;std::vector<GIsland> islands;
 // Two per row, Vulkan only: normalA*invMassA and normalB*invMassB, w = writable.
 std::vector<F4> terms;
 void clear(){bodies.clear();rows.clear();impulses.clear();groups.clear();levels.clear();islands.clear();terms.clear();}
};
}
