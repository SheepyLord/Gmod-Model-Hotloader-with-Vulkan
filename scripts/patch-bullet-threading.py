"""Documented local patches: keep Bullet diagnostic counters and soft RNG isolated,
and keep joint rows of very heavy PMX bodies active as MikuMikuDance's Bullet 2.75
did. Iteration counts, constraint equations and collision algorithms are unchanged.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'vendor/bullet/src'
def patch(name,old,new):
    p=root/name;s=p.read_text(encoding='utf-8')
    if new in s:return
    if old in s:p.write_text(s.replace(old,new),encoding='utf-8')
    else:raise RuntimeError('Bullet patch context changed: '+name)
for name,counters in [
 ('BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.cpp',['gNumSplitImpulseRecoveries']),
 ('BulletDynamics/Dynamics/btDiscreteDynamicsWorld.cpp',['gNumClampedCcdMotions']),
 ('LinearMath/btAlignedAllocator.cpp',['gNumAlignedAllocs','gNumAlignedFree','gTotalBytesAlignedAllocs'])]:
    for c in counters:patch(name,'\nint '+c+' = 0;','\nthread_local int '+c+' = 0; // MMDHL: independent worker diagnostic')
patch('BulletSoftBody/btSoftBody.h','static unsigned long seed = 243703;','// MMDHL: repulsion random state belongs to each soft body.')
patch('BulletSoftBody/btSoftBody.h','\tvoid applyRepulsionForce(btScalar timeStep, bool applySpringForce)','\tunsigned long m_repulsionSeed = 243703; // MMDHL: deterministic across worker scheduling\n\tvoid applyRepulsionForce(btScalar timeStep, bool applySpringForce)')
patch('BulletSoftBody/btSoftBody.h','#define NEXTRAND (seed = (1664525L * seed + 1013904223L) & 0xffffffff)','#define NEXTRAND (m_repulsionSeed = (1664525L * m_repulsionSeed + 1013904223L) & 0xffffffff)')
# stepSimulation writes this debugger flag even when debug drawing is disabled.
# Worlds use their own fixed debug settings and may run on different workers.
for name in ['BulletDynamics/Dynamics/btRigidBody.h','BulletDynamics/Featherstone/btMultiBody.cpp']:
    patch(name,'extern bool gDisableDeactivation;','extern thread_local bool gDisableDeactivation; // MMDHL: worker-local debugger flag')
patch('BulletDynamics/Dynamics/btRigidBody.cpp','\nbool gDisableDeactivation = false;','\nthread_local bool gDisableDeactivation = false; // MMDHL: worker-local debugger flag')
# Bullet 2.75 (MikuMikuDance's physics) inverted every nonzero joint-row mass sum.
# Later Bullet disables rows whose sum is below FLT_EPSILON (1.2e-7), which
# silently detaches PMX chains authored with masses near 1e7 and above from their
# anchors. Only truly degenerate rows are dropped now.
patch('BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.cpp',
      '\t\t\tbtAssert(fsum > SIMD_EPSILON);\n\t\t\tbtScalar sorRelaxation = 1.f;  //todo: get from globalInfo?\n\t\t\tsolverConstraint.m_jacDiagABInv = fsum > SIMD_EPSILON ? sorRelaxation / sum : 0.f;',
      '\t\t\t// MMDHL: rows of heavy PMX bodies (sum below SIMD_EPSILON) stay active, as in Bullet 2.75.\n\t\t\tbtScalar sorRelaxation = 1.f;  //todo: get from globalInfo?\n\t\t\tsolverConstraint.m_jacDiagABInv = fsum > btScalar(1e-20) ? sorRelaxation / sum : 0.f;')
