#pragma once
#include <cstddef>
// Vtable slots of the engine methods the modules call by number, as the compiler of each
// platform lays them out. A virtual destructor takes one slot under MSVC (Windows) and
// two under the Itanium C++ ABI (GCC on Linux), so an interface that declares one before
// a method has that method one slot later on Linux; MSVC also lists a group of overloads
// in reverse order, which moves no later method. Every number below is the Windows slot
// the 2.x releases call plus Dtor where the interface has a destructor first. The Linux
// numbers match the SDK headers as GCC compiles them (virtualSlot) and the game's
// binaries (both branches: GetCallQueue and the vtable lengths were read from them).
namespace mmd::abi {
#ifdef _WIN32
constexpr size_t Dtor=0;
#else
constexpr size_t Dtor=1;
#endif
// IVModelRender (VEngineModel016): DrawModelShadowSetup, DrawModelShadow, SetupLighting.
constexpr size_t ModelRenderShadowSetup=12,ModelRenderShadow=13,ModelRenderSetupLighting=21;
// IEngineTool (VENGINETOOL003, after IBaseInterface's destructor): GetLightingConditions.
constexpr size_t EngineToolLightingConditions=77+Dtor;
// IClientEntityList (VClientEntityList003): GetClientEntity, GetClientEntityFromHandle.
constexpr size_t EntityListClientEntity=3,EntityListClientEntityFromHandle=4;
// IPhysics (VPhysics031) before the IAppSystem shift: GetActiveEnvironmentByIndex, FindCollisionSet.
constexpr size_t PhysicsActiveEnvironment=11,PhysicsFindCollisionSet=15;
// IPhysicsCollisionSet::ShouldCollide.
constexpr size_t CollisionSetShouldCollide=2;
// IPhysicsEnvironment::GetObjectList.
constexpr size_t EnvironmentObjectList=47+Dtor;
// IPhysicsCollision (VPhysicsCollision007) at the x86-64 layout.
constexpr size_t CollisionConvexFromPolyhedron=8+Dtor,CollisionConvertConvex=14+Dtor,CollisionDestroy=16+Dtor,CollisionSize=17+Dtor,CollisionWrite=18+Dtor,
 CollisionDebugMesh=41+Dtor,CollisionDestroyDebugMesh=42+Dtor,CollisionQueryModel=43+Dtor,CollisionDestroyQueryModel=44+Dtor;
// The older layout's missing VPhysicsKeyParserCreate(vcollide_t*): MSVC lists the overload
// pair in reverse (it is slot 38), GCC in declaration order (slot 40 after the const char* one).
#ifdef _WIN32
constexpr size_t CollisionMissingInOlder=38;
#else
constexpr size_t CollisionMissingInOlder=40;
#endif
// ICollisionQuery: ConvexCount, TriangleCount, GetTriangleVerts.
constexpr size_t QueryConvexCount=1+Dtor,QueryTriangleCount=2+Dtor,QueryTriangleVerts=4+Dtor;
// IPhysicsObject at the x86-64 layout; the older one lacks SetSphereRadius.
constexpr size_t ObjectIsStatic=1+Dtor,ObjectIsTrigger=3+Dtor,ObjectIsFluid=4+Dtor,ObjectIsCollisionEnabled=6+Dtor,ObjectIsMoveable=10+Dtor,
 ObjectGameData=17+Dtor,ObjectMass=29+Dtor,ObjectInertia=31+Dtor,ObjectSphereRadius=42+Dtor,ObjectMissingInOlder=43+Dtor,ObjectMassCenter=45+Dtor,
 ObjectPosition=48+Dtor,ObjectPositionMatrix=49+Dtor,ObjectVelocity=52+Dtor,ObjectApplyForce=60+Dtor,ObjectApplyTorque=62+Dtor,ObjectCollide=74+Dtor;
// IMatRenderContext::GetCallQueue on the queued context returns its concrete call list at
// this offset (the hardware context returns null).
#if defined(_WIN32)
constexpr std::ptrdiff_t CallListOffset=0x2B0;
#elif defined(__x86_64__)
constexpr std::ptrdiff_t CallListOffset=0x2B0;
#else
constexpr std::ptrdiff_t CallListOffset=0x1DC;
#endif
// Vtable lengths of the x86-64 layout, for telling the older (default branch) layout
// apart: CMaterialSystem (VMaterialSystem080), the VPhysics031 object and CPhysicsCollision.
// Windows counts entries up to the first that is not code of the library; Linux counts the
// same way, and its vtables also hold the second destructor entry and no import thunks.
#ifdef _WIN32
constexpr size_t MaterialSystemVtableLength=151,PhysicsVtableLength=17,CollisionVtableLength=59;
#else
constexpr size_t MaterialSystemVtableLength=179,PhysicsVtableLength=17,CollisionVtableLength=60;
#endif
}
