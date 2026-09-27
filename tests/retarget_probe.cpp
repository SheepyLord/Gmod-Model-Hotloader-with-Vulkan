#include "rig.hpp"
#include <iostream>
using namespace mmd;
static Json xyz(btVector3 p){return {p.x(),p.y(),p.z()};}
int main(int argc,char** argv){try{
 if(argc!=5)throw std::runtime_error("retarget_probe cache asset rig.json report.json");
 auto m=loadAsset(argv[1],argv[2]);auto rig=rigFromManifest(readJson(argv[3]));
 std::vector<int> controlled(m->bones.size(),-1);for(size_t i=0;i<rig.bones.size();i++){auto& b=rig.bones[i];if(b.mmd>=0)controlled[b.mmd]=int(i);for(int a:b.aliases)controlled[a]=int(i);}
 std::vector<float> influence(m->bones.size());for(auto& v:m->vertices)for(int k=0;k<4;k++)if(v.bones[k]>=0)influence[v.bones[k]]+=v.weights[k];
 Json out=Json::array();
 for(int scenario=0;scenario<3;scenario++){
  btTransform motion(btQuaternion(btVector3(0,1,0),scenario==2?SIMD_PI:0.f),scenario?btVector3(30,2,20):btVector3(0,0,0));
  std::vector<btTransform> manual(m->bones.size(),btTransform::getIdentity()),goals,local,global,skin,effective;
  std::vector<float> morphs(m->morphs.size());for(auto& b:m->bones)goals.emplace_back(motion.getBasis(),motion*b.position);
  evaluatePose(*m,manual,morphs,&controlled,&goals,nullptr,local,global,skin,effective);
  Json bones=Json::array();for(size_t i=0;i<m->bones.size();i++)if(influence[i]>.01){auto& b=m->bones[i];float error=(global[i].getOrigin()-goals[i].getOrigin()).length();auto q=skin[i].getRotation();if(error>.001||btFabs(q.dot(motion.getRotation()))<.99999f)bones.push_back({{"i",i},{"name",b.name},{"parent",b.parent},{"inherit",b.inherit},{"coefficient",b.coefficient},{"localInherit",b.localInherit},{"inheritRotation",b.inheritRotation},{"inheritTranslation",b.inheritTranslation},{"fixedAxis",xyz(b.fixedAxis)},{"controlled",controlled[i]},{"weight",influence[i]},{"positionError",error},{"rotationAgreement",btFabs(q.dot(motion.getRotation()))}});}
  float maximum=0,total=0;size_t worst=0;for(size_t i=0;i<m->vertices.size();i++){auto& v=m->vertices[i];float e=(skinPosition(v,skin)-motion*v.position).length();total+=e;if(e>maximum){maximum=e;worst=i;}}
  out.push_back({{"scenario",scenario},{"maxVertexError",maximum},{"meanVertexError",total/m->vertices.size()},{"worstVertex",worst},{"bones",bones}});
 }
 writeJson(argv[4],out);for(auto& j:out)std::cout<<j["scenario"]<<" max vertex error "<<j["maxVertexError"]<<" mean "<<j["meanVertexError"]<<" bones "<<j["bones"].size()<<'\n';
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
