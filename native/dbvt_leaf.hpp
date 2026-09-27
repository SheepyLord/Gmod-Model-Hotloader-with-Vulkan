// Altered from Bullet btDbvtBroadphase.cpp (Nathanael Presson).
// Copyright (c) 2003-2009 Erwin Coumans. See licenses/Bullet-zlib.txt.
/*
This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it freely,
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/
// The tree updates and pair visitation order are unchanged. An AABB update
// always queries a leaf against a tree, so avoid the general tree/tree walker.
#pragma once
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
extern btScalar gDbvtMargin;
namespace mmd {
class LeafDbvt final:public btDbvtBroadphase {
 btAlignedObjectArray<const btDbvtNode*> stack;
 static void remove(btDbvtProxy* item,btDbvtProxy*& head){if(item->links[0])item->links[0]->links[1]=item->links[1];else head=item->links[1];if(item->links[1])item->links[1]->links[0]=item->links[0];}
 static void append(btDbvtProxy* item,btDbvtProxy*& head){item->links[0]=nullptr;item->links[1]=head;if(head)head->links[0]=item;head=item;}
 void collideLeaf(const btDbvtNode* root,const btDbvtNode* leaf){
  if(!root)return;int depth=1;stack[0]=root;
  do {
   auto n=stack[--depth];if(n==leaf||!Intersect(n->volume,leaf->volume))continue;
   if(n->isinternal()){
    if(depth+2>stack.size())stack.resize(stack.size()*2);
    // Same LIFO visitation as collideTTpersistentStack(root, leaf).
    stack[depth++]=n->childs[0];stack[depth++]=n->childs[1];
   }else{auto a=static_cast<btDbvtProxy*>(n->data),b=static_cast<btDbvtProxy*>(leaf->data);
#if DBVT_BP_SORTPAIRS
    if(a->m_uniqueId>b->m_uniqueId)btSwap(a,b);
#endif
    m_paircache->addOverlappingPair(a,b);++m_newpairs;
   }
  }while(depth);
 }
public:
 btScalar margin;
 explicit LeafDbvt(btOverlappingPairCache* pairs,btScalar fatMargin=gDbvtMargin):btDbvtBroadphase(pairs),margin(fatMargin){stack.resize(64);}
 void setAabb(btBroadphaseProxy* abs,const btVector3& minimum,const btVector3& maximum,btDispatcher*)override{
  auto proxy=static_cast<btDbvtProxy*>(abs);auto aabb=btDbvtVolume::FromMM(minimum,maximum);
#if DBVT_BP_PREVENTFALSEUPDATE
  if(!NotEqual(aabb,proxy->leaf->volume))return;
#endif
  bool collide=false;
  if(proxy->stage==STAGECOUNT){m_sets[1].remove(proxy->leaf);proxy->leaf=m_sets[0].insert(aabb,proxy);collide=true;}
  else {
   ++m_updates_call;
   // The original update returns immediately when the fat leaf already
   // contains this AABB. Avoid constructing unused velocity/margin operands.
   if(proxy->leaf->volume.Contain(aabb)){}
   else if(Intersect(proxy->leaf->volume,aabb)){
    const auto delta=minimum-proxy->m_aabbMin;btVector3 velocity(((proxy->m_aabbMax-proxy->m_aabbMin)/2)*m_prediction);
    if(delta[0]<0)velocity[0]=-velocity[0];if(delta[1]<0)velocity[1]=-velocity[1];if(delta[2]<0)velocity[2]=-velocity[2];
    if(m_sets[0].update(proxy->leaf,aabb,velocity,margin)){++m_updates_done;collide=true;}
   }else{m_sets[0].update(proxy->leaf,aabb);++m_updates_done;collide=true;}
  }
  remove(proxy,m_stageRoots[proxy->stage]);proxy->m_aabbMin=minimum;proxy->m_aabbMax=maximum;proxy->stage=m_stageCurrent;append(proxy,m_stageRoots[m_stageCurrent]);
  if(collide){m_needcleanup=true;if(!m_deferedcollide){collideLeaf(m_sets[1].m_root,proxy->leaf);collideLeaf(m_sets[0].m_root,proxy->leaf);}}
 }
};
}
