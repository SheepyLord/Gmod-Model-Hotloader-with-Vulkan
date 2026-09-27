#include "runtime.hpp"
#include <fstream>
#include <iostream>
using namespace mmd;
static Json vector(const float* p){return {p[0],p[1],p[2]};}
int main(int argc,char** argv){try{
 if(argc!=3)throw std::runtime_error("Usage: mmdhl_parameters model.pmx report.json");
 auto model=parse(readFile(argv[1]));Json out={{"bodies",Json::array()},{"joints",Json::array()}};
 for(size_t i=0;i<model->bodies.size();i++){auto b=model->bodies[i];out["bodies"].push_back({{"id",i},{"name",model->text(nanoemModelRigidBodyGetName(b,NANOEM_LANGUAGE_TYPE_JAPANESE))},{"bone",boneIndex(nanoemModelRigidBodyGetBoneObject(b))},{"mode",nanoemModelRigidBodyGetTransformType(b)},{"mass",nanoemModelRigidBodyGetMass(b)},{"linearDamping",nanoemModelRigidBodyGetLinearDamping(b)},{"angularDamping",nanoemModelRigidBodyGetAngularDamping(b)},{"position",vector(nanoemModelRigidBodyGetOrigin(b))},{"size",vector(nanoemModelRigidBodyGetShapeSize(b))}});}
 for(size_t i=0;i<model->joints.size();i++){auto j=model->joints[i];auto ref=model->jointReferences[i];out["joints"].push_back({{"id",i},{"name",model->text(nanoemModelJointGetName(j,NANOEM_LANGUAGE_TYPE_JAPANESE))},{"a",ref.a},{"b",ref.b},{"valid",ref.valid},{"type",nanoemModelJointGetType(j)},{"position",vector(nanoemModelJointGetOrigin(j))},{"lower",vector(nanoemModelJointGetLinearLowerLimit(j))},{"upper",vector(nanoemModelJointGetLinearUpperLimit(j))},{"spring",vector(nanoemModelJointGetLinearStiffness(j))},{"angularLower",vector(nanoemModelJointGetAngularLowerLimit(j))},{"angularUpper",vector(nanoemModelJointGetAngularUpperLimit(j))},{"angularSpring",vector(nanoemModelJointGetAngularStiffness(j))}});}
 writeJson(argv[2],out);std::cout<<model->bodies.size()<<" bodies, "<<model->joints.size()<<" joints\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
