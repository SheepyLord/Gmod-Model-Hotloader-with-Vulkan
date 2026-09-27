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
// Altered: parallel row construction, retaining all original row/body indices.
#pragma once
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include "jobs.hpp"
#include <vector>
namespace mmd {
class OrderedJointSolver : public btSequentialImpulseConstraintSolver {
 struct JointRows {int first,a,b;};
 std::vector<JointRows> jointRows;
protected:
 void convertJoints(btTypedConstraint** joints,int count,const btContactSolverInfo& settings) override {
  // PMX's spring constraints do not write shared rigid bodies during setup.
  // Other constraint types use Bullet's original setup until separately audited.
  bool supported=count>=64;
  for(int i=0;i<count&&supported;i++)supported=joints[i]->getConstraintType()==D6_SPRING_CONSTRAINT_TYPE&&!joints[i]->getJointFeedback();
  if(!supported){btSequentialImpulseConstraintSolver::convertJoints(joints,count,settings);return;}
  m_tmpConstraintSizesPool.resizeNoInitialize(count);jointRows.resize(count);
  parallelForSecondary(count,32,[&](size_t first,size_t end){for(size_t i=first;i<end;i++){
   auto constraint=joints[i];constraint->buildJacobian();constraint->internalSetAppliedImpulse(0.f);
   auto& size=m_tmpConstraintSizesPool[int(i)];
   if(constraint->isEnabled())constraint->getInfo1(&size);else{size.m_numConstraintRows=0;size.nub=0;}
  }});
  int total=0;
  for(int i=0;i<count;i++){
   auto& entry=jointRows[i];int rows=m_tmpConstraintSizesPool[i].m_numConstraintRows;entry.first=total;
   if(rows){
    entry.a=getOrInitSolverBody(joints[i]->getRigidBodyA(),settings.m_timeStep);
    entry.b=getOrInitSolverBody(joints[i]->getRigidBodyB(),settings.m_timeStep);
    // Prevent the base convertJoint() maximum reduction from writing in workers.
    m_maxOverrideNumSolverIterations=btMax(m_maxOverrideNumSolverIterations,joints[i]->getOverrideNumSolverIterations()>0?joints[i]->getOverrideNumSolverIterations():settings.m_numIterations);
   }
   total+=rows;
  }
  m_tmpSolverNonContactConstraintPool.resizeNoInitialize(total);
  parallelForSecondary(count,32,[&](size_t first,size_t end){for(size_t i=first;i<end;i++){
   auto& entry=jointRows[i];const auto& size=m_tmpConstraintSizesPool[int(i)];
   if(size.m_numConstraintRows)convertJoint(&m_tmpSolverNonContactConstraintPool[entry.first],joints[i],size,entry.a,entry.b,settings);
  }});
 }
};
}
