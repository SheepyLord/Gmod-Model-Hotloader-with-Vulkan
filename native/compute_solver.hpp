#pragma once
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
namespace mmd {
enum class ComputeBackend {Cpu,OpenCl,Vulkan};
// Deferred island setup keeps the reference island and row order. Independent
// islands can then run together on the shared CPU pool or one GPU dispatch.
class ComputeSolver final:public btSequentialImpulseConstraintSolver {
 struct Impl;std::unique_ptr<Impl> impl;
public:
 explicit ComputeSolver(ComputeBackend backend);
 ~ComputeSolver()override;
 static constexpr int Type=0x4d4d;
 btConstraintSolverType getSolverType()const override{return static_cast<btConstraintSolverType>(Type);}
 void prepareSolve(int,int)override;
 btScalar solveGroup(btCollisionObject**,int,btPersistentManifold**,int,btTypedConstraint**,int,const btContactSolverInfo&,btIDebugDraw*,btDispatcher*)override;
 void allSolved(const btContactSolverInfo&,btIDebugDraw*)override;
 void reset()override;
 nlohmann::json diagnostics()const;
};
nlohmann::json openclCapabilities(bool isolated=true);
nlohmann::json probeOpenClWorker();
void setComputeBatchSize(unsigned);
// Validation-only comparison of identical input rows against Bullet's solver.
void setComputeValidation(bool);
// Call on the owning thread, after destroying worlds and before unloading DLLs.
void shutdownCompute();
}
