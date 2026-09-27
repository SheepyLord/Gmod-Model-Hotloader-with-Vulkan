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
// Altered: shared task pool; retain nanoem soft-rigid world ABI for rigid-only models.
#pragma once
#include <BulletSoftBody/btSoftRigidDynamicsWorld.h>
#include "jobs.hpp"
namespace mmd {
class SecondaryMtWorld final:public btSoftRigidDynamicsWorld {
public:
 SecondaryMtWorld(btDispatcher* dispatcher,btBroadphaseInterface* broadphase,btConstraintSolver* solver,btCollisionConfiguration* config)
  :btSoftRigidDynamicsWorld(dispatcher,broadphase,solver,config){}
 void predictUnconstraintMotion(btScalar dt)override{
  parallelForSecondary(m_nonStaticRigidBodies.size(),64,[&](size_t a,size_t b){for(size_t i=a;i<b;i++){auto body=m_nonStaticRigidBodies[int(i)];if(!body->isStaticOrKinematicObject()){body->applyDamping(dt);body->predictIntegratedTransform(dt,body->getInterpolationWorldTransform());}}});
 }
 void integrateTransforms(btScalar dt)override{
  parallelForSecondary(m_nonStaticRigidBodies.size(),64,[&](size_t a,size_t b){integrateTransformsInternal(&m_nonStaticRigidBodies[int(a)],int(b-a),dt);});
 }
};
}
