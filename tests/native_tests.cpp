#include "runtime.hpp"
#include "scene.hpp"
#include "secondary.hpp"
#include "fitter.hpp"
#include "rig.hpp"
#include "rig_writer.hpp"
#include "spring_bones.hpp"
#include "cutout.hpp"
#include "compatibility.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>
using namespace mmd;
int main(int argc,char** argv){int failed=0,passed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<"\n";(ok?passed:failed)++;};
    for(int type=0;type<5;type++){Vertex v;v.type=type;v.position=btVector3(2,3,4);v.normal=btVector3(0,1,0);v.bones={0,1,2,3};v.weights={.25f,.25f,.25f,.25f};if(type==0)v.weights={1,0,0,0};if(type==1||type==3)v.weights={.3f,.7f,0,0};v.c=btVector3(1,2,3);v.r0=btVector3(0,1,0);v.r1=btVector3(1,0,0);
        std::vector<btTransform> transforms(4,btTransform::getIdentity());check((skinPosition(v,transforms)-v.position).length()<1e-4,"bind pose survives skinning");for(auto& t:transforms)t.setOrigin(btVector3(4,5,6));check((skinPosition(v,transforms)-(v.position+btVector3(4,5,6))).length()<1e-4,"all skinning modes preserve rigid translation");}
    for(int type:{0,1,2,3,4}){Vertex v;v.type=type;v.position={2,3,4};v.bones={0,1,2,3};v.weights={.4f,.6f,0,0};v.c={1,2,0};v.r0={2,1,0};v.r1={0,2,1};btTransform rigid(btQuaternion(btVector3(1,2,3).normalized(),.7f),btVector3(1,-2,5));std::vector<btTransform> t(4,rigid);check((skinPosition(v,t)-rigid*v.position).length()<1e-4,"all skinning modes preserve rigid rotation about an offset pivot");}
    check((fromSource(toSource(btVector3(1,2,3)))-btVector3(1,2,3)).length()<1e-6,"coordinate round trip");
    for(int type=0;type<5;type++){
        bool agrees=true;
        for(int sample=0;sample<32;sample++){
            Vertex v;v.type=type;v.position={2,-3,4};v.normal=btVector3(.3f,.7f,-.2f).normalized();v.bones={0,1,2,3};
            v.weights={.1f,.2f,.3f,.4f};if(type==0)v.weights={1,0,0,0};if(type==1||type==3)v.weights={.3f,.7f,0,0};
            if(sample==31&&type!=3)v.weights={0,0,0,0};v.c={1,-2,3};v.r0={0,1,2};v.r1={2,0,1};
            std::vector<btTransform> transforms;for(int i=0;i<4;i++)transforms.emplace_back(btQuaternion(btVector3(float(i+1),2,-1).normalized(),sample*.13f+i*.6f),btVector3(float(i),-2,3));
            auto tangent=btVector3(.7f,-.3f,.1f).normalized();auto out=skinFrame(v,transforms,tangent);auto tv=v;tv.normal=tangent;
            agrees&=(out.position-skinPosition(v,transforms)).length()<1e-5f&&(out.normal-skinNormal(v,transforms)).length()<1e-5f&&(out.tangent-skinNormal(tv,transforms)).length()<1e-5f;
        }
        check(agrees,"shared skinning frame matches independent position and direction paths across mixed rotations");
    }
    try{parse(Bytes{1,2,3});check(false,"truncated input rejected");}catch(...){check(true,"truncated input rejected");}
    // IAppSystem layouts by vtable length: the compiled one (or longer, methods appended) and the default
    // branch's, four shorter. Any other length is a layout this code does not know: refused, not guessed.
    {bool known=knownAppSystemShift(147,151)==size_t(4)&&knownAppSystemShift(151,151)==size_t(0)&&knownAppSystemShift(152,151)==size_t(0)&&knownAppSystemShift(400,151)==size_t(0);
     for(size_t length:{0,1,13,146,148,149,150})known&=!knownAppSystemShift(length,151);
     check(known&&knownAppSystemShift(13,17)==size_t(4)&&!knownAppSystemShift(14,17),"vtable layouts: 147 and 151 entries (or more) are known, every other length is refused");}
    World w;w.setMirror(1,{{"shape","box"},{"half",{10,10,1}},{"position",{0,0,0}},{"mass",0}});w.step(1,btVector3(0,0,-9.8f));check(w.dropped>.9,"catchup bounded and reported");check(w.takeImpulses().empty(),"static environment produces no feedback");w.removeMirror(1);check(w.mirrors.empty(),"mirror removal");
    // A malformed mirror (SetMirror is a Lua API) throws before Bullet sees any of it and leaves
    // the mirrors as they were: a hull count whose three-fold product wraps on x64, a zero
    // rotation, negative extents, NaN or infinite vertices and numbers beyond float's range.
    {World test;test.setMirror(1,{{"shape","sphere"},{"radius",1}});const int objects=test.dynamics().getNumCollisionObjects();
     auto rejected=[&](const Json& spec,std::vector<float> geometry){
      try{test.setMirror(2,spec,geometry);}catch(const std::exception&){return !test.mirrors.contains(2)&&test.dynamics().getNumCollisionObjects()==objects;}return false;};
     std::vector<float> hull={0,0,0,1,0,0,0,1,0,0,0,1},nanHull=hull,infHull=hull;nanHull[4]=std::nanf("");infHull[5]=INFINITY;
     check(rejected({{"shape","compound"},{"hulls",Json::array({6148914691236517206ull})}},{0,0,0}),"a hull count whose three-fold product wraps is rejected at once");
     check(rejected({{"shape","compound"},{"hulls",Json::array({Json(-4)})}},hull)&&rejected({{"shape","compound"},{"hulls",Json::array({4.5})}},hull)&&rejected({{"shape","compound"},{"hulls",Json::array()}},{}),"negative, fractional and missing hull counts are rejected");
     check(rejected({{"shape","compound"},{"hulls",Json::array({4})},{"rotation",{0,0,0,0}}},hull),"a zero rotation quaternion is rejected");
     check(rejected({{"shape","box"},{"half",{-1,1,1}}},{})&&rejected({{"shape","sphere"},{"radius",-1}},{}),"negative extents are rejected");
     check(rejected({{"shape","compound"},{"hulls",Json::array({4})}},nanHull)&&rejected({{"shape","compound"},{"hulls",Json::array({4})}},infHull),"NaN and infinite vertices are rejected");
     check(rejected({{"shape","sphere"},{"radius",1},{"mass",1},{"position",{1e39,0,0}}},{})&&rejected({{"shape","sphere"},{"mass",1e39}},{}),"numbers beyond float's range are rejected");
     test.setMirror(2,{{"shape","compound"},{"hulls",Json::array({4.0})},{"mass",1}},hull);check(test.mirrors.contains(2)&&test.dynamics().getNumCollisionObjects()==objects+1,"a valid compound mirror, its count written as 4.0, is added");
     auto before=test.mirrors.at(2)->body->getWorldTransform().getOrigin();bool threw=false;
     try{test.setMirror(2,{{"position",{100,0,0}},{"rotation",{0,0,0,0}}});}catch(const std::exception&){threw=true;}
     check(threw&&test.mirrors.at(2)->body->getWorldTransform().getOrigin()==before,"a rejected update leaves the mirror where it was");}
    {World test;test.setMirror(1,{{"shape","box"},{"half",{1,1,1}},{"mass",1},{"inertia",{0,1,1}}});test.captureBefore();test.mirrors.at(1)->body->setAngularVelocity({0,1,0});test.captureAfter();auto out=test.takeImpulses();check(out.size()==1&&std::isfinite(out[0]["angular"][0].get<float>())&&btFabs(out[0]["angular"][1].get<float>()-SIMD_DEGS_PER_RAD)<.001f,"locked mirror inertia exports finite torque");}
    {World test;auto m=std::make_shared<Model>();Bone bone;bone.name="root";m->bones.push_back(bone);m->order={0};m->minimum={-1,0,-1};m->maximum={1,2,1};auto id=test.create(m,{{"frozen",true}});test.setMirror(9,{{"shape","sphere"},{"radius",1},{"position",{-5,0,2.36}},{"velocity",{200,0,0}},{"mass",2}});
     auto before=test.mirrors[9]->body->getLinearVelocity();btVector3 exported(0,0,0);for(int k=0;k<10;k++){test.step(1./120,{0,0,0});for(auto& e:test.takeImpulses())exported+=btVector3(e["linear"][0],e["linear"][1],e["linear"][2]);}auto actual=(test.mirrors[9]->body->getLinearVelocity()-before)*2/Inch;
     check(exported.length()>1,"rigid solver produces mirror feedback");check((exported-actual).length()<.01f,"exported linear impulse equals mass times velocity change in Source units");check(test.takeImpulses().empty(),"feedback consumed once");test.remove(id);check(test.raycast({-1,0,0},{1,0,0}).is_null(),"removed ragdoll cannot be raycast");}
    for(const auto& jointName:{"knee_l","elbow_l"})for(int sign:{-1,1}){
        World test;auto m=std::make_shared<Model>();Bone root;root.name="lower body";root.position={0,10,0};m->bones.push_back(root);Bone child;child.name=jointName;child.parent=0;child.position={0,5,0};m->bones.push_back(child);m->order={0,1};m->minimum={-1,0,-1};m->maximum={1,12,1};
        auto id=test.create(m,{{"scale",.1f}});auto& instance=test.get(id);auto& joint=instance.anatomicalJoints.at(0);auto& body=joint.constraint->getRigidBodyB();
        for(int k=0;k<600;k++){joint.constraint->calculateTransforms();body.applyTorque(joint.constraint->getCalculatedTransformA().getBasis().getColumn(0)*(sign*2.f));body.activate(true);test.step(1./120,{0,0,0},false);}
        joint.constraint->calculateTransforms();float angle=joint.constraint->getAngle(0);std::cout<<jointName<<" torque "<<sign<<" angle "<<angle*SIMD_DEGS_PER_RAD<<"\n";
        check(angle>=joint.lower.x()-.05f&&angle<=joint.upper.x()+.05f,"anatomical hinge stops sustained overextension torque");
        check(btFabs(joint.constraint->getAngle(1))<.02f&&btFabs(joint.constraint->getAngle(2))<.02f,"elbow and knee reject sideways rotation");
        check((joint.constraint->getCalculatedTransformA().getOrigin()-joint.constraint->getCalculatedTransformB().getOrigin()).length()<.01f,"anatomical pivot stays joined under torque");
        if(sign<0){check(angle>2.f,"hinge permits natural flexion through more than 115 degrees");
            auto parentDelta=joint.constraint->getRigidBodyA().getWorldTransform().getBasis()*instance.bodies[instance.drivers[0]]->initial.getBasis().transpose();
            auto childDelta=body.getWorldTransform().getBasis()*instance.bodies[instance.drivers[1]]->initial.getBasis().transpose();
            auto limb=parentDelta.transpose()*childDelta*btVector3(0,0,-1);
            check(limb.x()*(std::string(jointName)=="knee_l"?1.f:-1.f)>.2f,"knees bend backward and elbows bend forward");}
        auto center=body.getWorldTransform();test.beginPhysgun(id,instance.drivers[1],center);auto goal=center;goal.getOrigin()+={0,0,1};test.updatePhysgun(goal);auto before=body.getWorldTransform().getOrigin();for(int k=0;k<120;k++)test.step(1./120,{0,0,-9.8f},false);
        check(body.getWorldTransform().getOrigin().z()>before.z()+.5f,"bounded physics gun drive lifts constrained rig");instance.freeze(true);auto parentPose=joint.constraint->getRigidBodyA().getWorldTransform();auto childPose=body.getWorldTransform();instance.setBonePose(1,btTransform(btQuaternion(btVector3(1,0,0),.3f),btVector3(0,0,0)));
        check((parentPose.getOrigin()-joint.constraint->getRigidBodyA().getWorldTransform().getOrigin()).length()<1e-6f&&childPose.getRotation().angleShortestPath(body.getOrientation())>.2f,"posing frozen driven bone preserves the rest of the ragdoll");
        test.remove(id);test.step(1./120,{0,0,-9.8f});check(test.instances.empty(),"removing held rig releases physics gun drive");
    }
    if(argc>1){auto m=parse(readFile(wide(argv[1])));m->bones[1].name="lower body";m->bones[2].name="knee_l";World test;auto id=test.create(m,Json::object());auto& p=test.get(id);check(p.anatomicalJoints.size()==1&&!p.bodies[p.drivers[1]]->generated&&!p.bodies[p.drivers[2]]->generated,"authored primary body pair receives anatomical limits");}
    for(int i=1;i<argc;i++)try{auto m=parse(readFile(wide(argv[i])));auto id=w.create(m,{{"position",{512,-1024,-12260}},{"ragdoll",true}});auto& instance=w.get(id);
      if(!instance.softBodies.empty()){auto& soft=instance.softBodies[0];check(soft.vertexIndices.front()==3,"soft material uses global vertex indices");check(soft.body->m_anchors.size()==1&&soft.body->m_anchors[0].m_node==&soft.body->m_nodes[0],"anchor resolves vertex index independently of rigid body index");check(soft.pins.size()==2&&soft.body->m_nodes[soft.pins[1]].m_im==0,"soft pins retain zero inverse mass");auto p=instance.bodies[0]->rigid->getWorldTransform().getOrigin();auto hit=w.raycast(p-btVector3(1,0,0),p+btVector3(1,0,0));check(!hit.is_null()&&instance.bodies[hit["body"].get<size_t>()]->mode!=0,"grab ray skips authored kinematic follow colliders");}
      w.step(1./120,{0,0,0});bool noKick=true;for(auto& b:instance.bodies)if(b->mode==0)noKick=noKick&&b->rigid->getLinearVelocity().length()<.1f&&b->rigid->getAngularVelocity().length()<.1f;check(noKick,"spawned follow bodies have no teleport velocity");
      for(int t=0;t<240;t++)w.step(1./120,btVector3(0,0,-9.8f));auto s=instance.snapshot;bool finite=true;for(auto& v:s->vertices)finite=finite&&std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);check(finite,"fixture simulates with finite vertices");
      auto revision=s->sequence;for(int k=0;k<8;k++)w.step(1./120,{0,0,-9.8f},false);check(instance.snapshot->sequence==revision&&instance.poseDirty,"physics ticks defer vertex skinning");instance.ensureSnapshot();auto fresh=instance.snapshot->sequence;instance.ensureSnapshot();check(fresh==revision+1&&instance.snapshot->sequence==fresh,"render snapshot skins once after multiple ticks");
      instance.freeze(true);bool inactive=true;for(auto& j:instance.joints)inactive=inactive&&!j->isEnabled();for(auto& b:instance.bodies)if(b->mode!=0)inactive=inactive&&w.dynamics().getNonStaticRigidBodies().findLinearSearch(b->rigid.get())==w.dynamics().getNonStaticRigidBodies().size();check(inactive,"frozen bodies and joints leave active simulation");
      auto sequence=instance.snapshot->sequence;auto position=instance.bodies.back()->rigid->getWorldTransform().getOrigin();for(int k=0;k<600;k++)w.step(1./120,{0,0,-9.8f});check((position-instance.bodies.back()->rigid->getWorldTransform().getOrigin()).length()<1e-5,"frozen rigid body remains stationary");check(sequence==instance.snapshot->sequence,"frozen snapshot is reused");
      instance.freeze(false);bool enabled=true;for(auto& j:instance.joints)enabled=enabled&&j->isEnabled();check(enabled,"unfreeze restores joint simulation");instance.freeze(true);
      instance.manual[0].setOrigin({2,0,0});instance.applyPose();auto pose=instance.bodies.back()->rigid->getWorldTransform();w.step(1./120,{0,0,-9.8f});check((pose.getOrigin()-instance.bodies.back()->rigid->getWorldTransform().getOrigin()).length()<1e-5,"manual pose persists into frozen simulation tick");
      w.remove(id);check(w.instances.empty()&&w.dynamics().getNumCollisionObjects()==0,"instance bodies constraints and soft bodies removed");}catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"fixture loads and simulates");}
    if(fs::exists("tests/fixtures/textured21.pmx"))try{auto cache=fs::absolute("test-output/native-cache");auto result=importAsset(fs::absolute("tests/fixtures/textured21.pmx"),cache,Json::object());std::string id=result.at("asset");auto model=loadAsset(cache,id);check(!model->materials[0].base.empty(),"DDS BC1 decoded and cached as verified PNG");auto file=cache/"assets"/id/"manifest.json";auto manifest=readJson(file),tampered=manifest;tampered["name"]="tampered";writeJson(file,tampered);try{loadAsset(cache,id);check(false,"manifest tampering rejected");}catch(...){check(true,"manifest tampering rejected");}writeJson(file,manifest);
      auto derivative=cache/"textures"/(model->materials[0].base+".vtf");auto original=readFile(derivative);fs::remove(cache/"assets"/id/"materials-v5.gma");writeAtomic(derivative,Bytes{0});bool rejected=false;
      try{prepareSourceMaterials(cache,id);}catch(...){rejected=true;}
      writeAtomic(derivative,original);prepareSourceMaterials(cache,id);check(rejected,"truncated Source texture derivatives fail safely before reading their header");
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"texture cache import");}
    if(fs::exists("tests/fixtures/native-cloth21.pmx"))try{
      auto m=parse(readFile("tests/fixtures/native-cloth21.pmx"));
      m->morphNames[0]="duplicate_2";m->morphNames[1]="duplicate";m->morphNames[2]="duplicate";
      auto rig=fitRig(*m,Json::object());std::set<std::string> names;
      for(auto& morph:rig.morphs)names.insert(morph.at("name").get<std::string>());
      check(names.size()==rig.morphs.size(),"authored suffixes cannot alias duplicate flex names or presets");
      check(rig.manifest==fitRig(*m,Json::object()).manifest,"native controller names and IDs remain stable across fitting");
      bool pivots=true,axes=true,bounds=true;
      for(auto& body:rig.bodies){auto& bone=rig.bones[body.bone];if(bone.mmd>=0)pivots&=(bone.rest.getOrigin()-toSource(m->bones[bone.mmd].position)*rig.scale).length()<1e-4f;
        if(bone.name.find("Calf")!=std::string::npos){auto hinge=bone.rest.getBasis().getColumn(2);axes&=btFabs(hinge.y())>.98f&&btFabs(hinge.x())<.05f;auto direction=bone.rest.getBasis().getColumn(0);auto bent=quatRotate(btQuaternion(hinge,SIMD_HALF_PI),direction);axes&=bent.x()>.9f;}
        if(bone.name.find("Thigh")!=std::string::npos||bone.name.find("Calf")!=std::string::npos||bone.name.find("UpperArm")!=std::string::npos||bone.name.find("Forearm")!=std::string::npos){int child=-1;for(auto& candidate:rig.bones)if(candidate.parent==body.bone&&candidate.physics>=0){child=candidate.physics;break;}if(child>=0){float length=(rig.bones[rig.bodies[child].bone].rest.getOrigin()-bone.rest.getOrigin()).length();for(auto p:body.hull)bounds&=p.x()>=-.35f*length-1e-3f&&p.x()<=1.2f*length+1e-3f;}}
      }
      check(std::abs(rig.scale-3.23656f)<1e-5f,"default size is SCMI 0.08 times 40.457 without height normalization");
      auto half=fitRig(*m,{{"scaleMultiplier",.5f}}),twice=fitRig(*m,{{"scaleMultiplier",2.f}});
      bool scaled=true,topology=true;for(size_t i=0;i<rig.bones.size();i++)scaled&=(half.bones[i].rest.getOrigin()*2-rig.bones[i].rest.getOrigin()).length()<1e-3f&&(twice.bones[i].rest.getOrigin()*.5f-rig.bones[i].rest.getOrigin()).length()<1e-3f;
      for(auto& body:rig.manifest["bodies"]){topology&=body["hull"].size()>=4&&body["hull"].size()<=64&&body["faces"].size()>=4;for(auto& face:body["faces"])for(auto index:face)topology&=index.get<size_t>()<body["hull"].size();}
      check(scaled,"half and double size scale every Source bone consistently");check(topology,"all 18 collision shapes have bounded real convex topology");
      auto oldMax=m->maximum;m->maximum.setY(oldMax.y()+100);auto padded=fitRig(*m,Json::object());m->maximum=oldMax;check(std::abs(padded.scale-rig.scale)<1e-6f,"hidden remote geometry cannot change import scale");
      try{resolveSourceScale({{"scaleMultiplier",1},{"height",72}},20);check(false,"ambiguous scale options rejected");}catch(...){check(true,"ambiguous scale options rejected");}
      try{resolveSourceScale({{"scaleMultiplier",-1}},20);check(false,"negative scale rejected");}catch(...){check(true,"negative scale rejected");}
      auto restored=rigFromManifest(rig.manifest);check(restored.key==rig.key,"rig manifest round trip preserves identity");
      {World host;auto id=host.create(m,{{"backend","source"},{"rigManifest",rig.manifest}});check(host.get(id).sourceRig->key==rig.key,"a valid shared rig manifest creates an instance");}
      // Shared or saved manifests are untrusted: every model-relative index is checked before use.
      int modelBones=int(m->bones.size());
      const std::vector<std::pair<const char*,std::function<void(Json&)>>> mutations={
       {"model bone past the model",[&](Json& j){j["bones"][5]["mmd"]=modelBones;}},
       {"negative model bone",[&](Json& j){j["bones"][5]["mmd"]=-2;}},
       {"model bone alias past the model",[&](Json& j){j["bones"][5]["mmdAliases"]=Json::array({modelBones+7});}},
       {"rig parent after its child",[&](Json& j){j["bones"][5]["parent"]=40;}},
       {"rig parent below -1",[&](Json& j){j["bones"][5]["parent"]=-3;}},
       {"bone body past the 18 bodies",[&](Json& j){j["bones"][5]["physics"]=18;}},
       {"body bone past the rig",[&](Json& j){j["bodies"][3]["bone"]=99;}},
       {"body parent past the 18 bodies",[&](Json& j){j["bodies"][3]["parent"]=18;}},
       {"root without a model bone",[&](Json& j){j["bones"][0]["mmd"]=-1;}},
       {"non-finite bone position",[&](Json& j){j["bones"][5]["position"]=Json::array({1e39,0,0});}},
       {"non-finite mesh bind",[&](Json& j){j["meshYaw"]=1e39;}}};
      for(auto& [label,mutate]:mutations){auto manifest=rig.manifest;mutate(manifest);World host;bool rejected=false;
       try{host.create(m,{{"backend","source"},{"rigManifest",manifest}});}catch(const std::exception&){rejected=true;}
       check(rejected&&host.instances.empty()&&host.dynamics().getNumCollisionObjects()==0,(std::string("rig manifest rejected without leftovers: ")+label).c_str());}
      m->fittedRig=std::make_shared<Rig>(rig);auto cachedHalf=fitRig(*m,{{"scaleMultiplier",.5f}});check(std::abs(cachedHalf.scale-rig.scale*.5f)<1e-5f,"cached fitting obeys explicit scale ratios");
      World presentation;auto id=presentation.create(m,{{"backend","source"},{"presentationDriven",true}});auto& instance=presentation.get(id);
      btTransform placed(btQuaternion(btVector3(0,0,1),.7f),btVector3(150,-20,40));std::vector<btTransform> palette;for(auto& bone:rig.bones)palette.push_back(placed*bone.rest);
      instance.submitPresentationPose(palette,1,1);instance.stepSource();instance.ensureSnapshot();bool aligned=true;for(auto& bone:rig.bones)if(bone.mmd>=0){auto live=instance.placement*convert(instance.global[bone.mmd],instance.scale);aligned&=(live.getOrigin()/Inch-placed*bone.rest.getOrigin()).length()<.001f;}
      check(aligned,"client presentation palette drives the MMD primary bones in the same world frame");auto sequence=instance.snapshot->sequence;instance.submitPresentationPose(palette,1,1);instance.stepSource();instance.ensureSnapshot();check(instance.snapshot->sequence==sequence,"multiple render passes reuse one presentation frame");
      for(auto& t:palette)t.getOrigin()+=btVector3(2,0,0);instance.submitPresentationPose(palette,1,2);instance.stepSource();instance.ensureSnapshot();check(instance.snapshot->sequence>sequence,"new client palette updates even when simulation timestamp is unchanged");
      check(pivots,"Source primary pivots are the scaled original PMX landmarks");
      check(axes,"native knee flexion is backward around the character lateral axis");
      check(bounds,"limb hulls stay inside bounded joint-overlap envelopes");
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"native flex naming regression");}
    for(auto name:{"self","invalid","world"})try{
      auto model=parse(readFile(std::string("tests/fixtures/native-joint-")+name+".pmx"));bool anchored=std::string(name)=="world";
      check(model->jointReferences.size()==1&&model->jointReferences[0].valid==anchored,"joint validation distinguishes world anchors from malformed references");
      check(anchored?model->warnings.empty():!model->warnings.empty(),"invalid authored joints have actionable warnings");
      World host;auto id=host.create(model,{{"backend","source"}});auto& instance=host.get(id);
      for(int i=0;i<30;i++)instance.secondary->step(1./60);
      auto diagnostics=instance.secondary->diagnostics();check(diagnostics["joints"]==(anchored?1:0)&&diagnostics["bodies"]==3,"secondary solver retains every body and only valid joints");
      if(anchored){
       auto geometry=std::make_shared<SceneGeometry>();geometry->kind=SceneGeometry::Convexes;geometry->minimum={-200,-200,-3};geometry->maximum={200,200,0};
       for(int x:{-1,1})for(int y:{-1,1})for(int z:{0,1})geometry->vertices.emplace_back(float(x*200),float(y*200),float(-z*3));geometry->hullCounts={8};
       auto frame=std::make_shared<SceneFrame>();SceneObject object;object.id=1;object.isStatic=true;object.geometry=geometry;frame->objects.push_back(object);publishScene(frame);instance.secondary->setCollisionMode(1);
       bool contact=false;for(int i=0;i<120;i++){instance.secondary->step(1./60);contact|=instance.secondary->diagnostics()["externalContacts"].get<int>()>0;}
       check(contact,"nanoem authored bodies contact one-way Source scene geometry");
       check(host.takeImpulses().empty(),"secondary scene cannot export impulses into Source");
       frame=std::make_shared<SceneFrame>();object.owner=id;frame->objects.push_back(object);publishScene(frame);instance.secondary->step(1./60);check(instance.secondary->diagnostics()["sourceMirrors"]==0,"own carrier is excluded from secondary contacts");
       // Each collision checkbox admits its own kind of scene object.
       {auto kinds=std::make_shared<SceneFrame>();SceneObject o=object;o.owner=0;o.id=11;o.isStatic=true;kinds->objects.push_back(o);o.id=12;o.isStatic=false;kinds->objects.push_back(o);
        o.id=13;o.actor=SceneObject::LivingPlayer;kinds->objects.push_back(o);o.id=14;o.actor=SceneObject::LivingNpc;kinds->objects.push_back(o);publishScene(kinds);
        auto mirrors=[&](unsigned flags){instance.secondary->setCollisionFlags(flags);instance.secondary->step(1./60);return instance.secondary->diagnostics()["sourceMirrors"].get<int>();};
        check(mirrors(Collide::Character|Collide::World)==1&&mirrors(Collide::Objects)==1&&mirrors(Collide::Players)==1&&mirrors(Collide::Npcs)==1&&mirrors(Collide::All)==4&&mirrors(Collide::Default)==1,"each collision checkbox admits only its kind of scene object");}
       instance.secondary->setCollisionMode(0);publishScene(nullptr);check(instance.secondary->diagnostics()["sourceMirrors"]==0,"disabling scene contacts releases all proxies");
      }
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"joint/scene regression");}
    // nanoem soft bodies of the secondary world: one node per vertex of the soft
    // material, pins on the authored model vertices (3 and 7 here, which are not
    // nodes 3 and 7), pinned nodes on their animated vertices, and a rope that
    // ignores vertices outside its material.
    try{
     std::vector<std::vector<btVector3>> ropes;
     for(std::string name:{"native-cloth21.pmx","native-rope21.pmx","native-rope21-extra.pmx"}){
      auto model=parse(readFile("tests/fixtures/"+name));World host;auto id=host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",0}});auto& instance=host.get(id);
      auto source=model->softBodies.at(0);auto& part=model->materials.at(size_t(nanoemModelObjectGetIndex(nanoemModelMaterialGetModelObject(nanoemModelSoftBodyGetMaterialObject(source)))));
      std::set<int> materialVertices;for(unsigned k=part.first;k<part.first+part.count;k++)materialVertices.insert(int(model->indices[k]));
      nanoem_rsize_t count=0;auto pins=nanoemModelSoftBodyGetAllPinnedVertexIndices(source,&count);std::set<int> pinned(pins,pins+count);
      auto& softs=instance.secondary->dynamics()->getSoftBodyArray();if(softs.size()!=1)throw std::runtime_error("expected one soft body in "+name);
      auto& nodes=softs[0]->m_nodes;auto vertexOf=[&](int n){return vertexIndex(static_cast<const nanoem_model_vertex_t*>(nodes[n].m_tag));};
      std::set<int> tagged,massless;for(int n=0;n<nodes.size();n++){tagged.insert(vertexOf(n));if(nodes[n].m_im==0)massless.insert(vertexOf(n));}
      check(size_t(nodes.size())==materialVertices.size()&&tagged==materialVertices,"a nanoem soft body has one node per vertex of its material");
      check(!pinned.empty()&&massless==pinned,"nanoem soft-body pins hold the authored model vertices");
      auto& rig=*instance.sourceRig;double error=0;
      for(int frame=0;frame<30;frame++){double t=frame/60.;btTransform drive(btQuaternion(btVector3(0,0,1),float(t)),btVector3(float(t*30),0,float(std::sin(t*9)*3)));
       std::vector<btTransform> palette;for(auto& b:rig.bones)palette.push_back(drive*b.rest);
       instance.submitPresentationPose(palette,t,uint64_t(frame)+1);instance.stepSource();instance.evaluate(false);
       for(int n=0;n<nodes.size();n++)if(pinned.contains(vertexOf(n)))error=std::max(error,double((nodes[n].m_x-skinPosition(model->vertices[vertexOf(n)],instance.skin)).length()));}
      check(error<1e-4,"pinned soft-body nodes follow their animated model vertices");
      if(name.starts_with("native-rope")){ropes.emplace_back();for(int n=0;n<nodes.size();n++)ropes.back().push_back(nodes[n].m_x);}
     }
     bool same=ropes.size()==2&&ropes[0].size()==ropes[1].size();for(size_t i=0;same&&i<ropes[0].size();i++)same=(ropes[0][i]-ropes[1][i]).length()<1e-4f;
     check(same,"vertices outside the rope's material change neither its nodes nor its motion");
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"nanoem soft-body regression");}
    {ConvexFit fit;fit.vertices={{-1,-1,-1},{1,-1,-1},{-1,1,-1},{1,1,-1},{-1,-1,1},{1,-1,1},{-1,1,1},{1,1,1},{1,.99999f,1}};convexTopology(fit);check(fit.repaired&&fit.vertices.size()==8,"near duplicate collision vertices are welded into a closed hull");}
    { ConvexFit thin;thin.vertices={{0,0,0},{1,0,0},{2,0,0},{3,0,0}};convexTopology(thin);check(thin.fallback&&thin.faces.size()>=4&&thin.extent.y()>0&&thin.extent.z()>0,"collapsed collision hull becomes a bounded closed anatomical fallback"); }

    // One non-finite or runaway value per numeric section never reaches Bullet or the GPU:
    // the loader repairs it (with a "Repaired ..." note) or, for soft bodies, rejects the model.
    for(auto bad:{"uv","material","ik","morph_vertex","morph_bone","morph_material","morph_group","morph_impulse","body_orientation","body_mass","body_damping","joint_limit","joint_spring",
                  "vertex_position","normal","weights","self_parent","body_heavy","joint_stiff"}){
     bool noted=false;try{auto m=parse(readFile(std::string("tests/fixtures/corrupt-")+bad+".pmx"));for(auto& w:m->warnings)noted|=w.starts_with("Repaired ");}catch(const std::exception& e){std::cerr<<bad<<": "<<e.what()<<"\n";}
     check(noted,(std::string("a corrupt ")+bad+" value is repaired at load, with a note").c_str());}
    for(auto bad:{"soft","soft_iterations"}){
     bool rejected=false;try{parse(readFile(std::string("tests/fixtures/corrupt-")+bad+".pmx"));}catch(const std::runtime_error&){rejected=true;}
     check(rejected,(std::string("a corrupt ")+bad+" value is rejected at load").c_str());}
    try{
     auto hidden=parse(readFile("tests/fixtures/corrupt-vertex_position.pmx"));
     check(hidden->indices[0]==hidden->indices[1]&&hidden->indices[1]==hidden->indices[2]&&std::isfinite(hidden->vertices[0].position.x()),"a vertex without a position is parked and its triangles are hidden");
     auto weights=parse(readFile("tests/fixtures/corrupt-weights.pmx"));const auto& v=weights->vertices[2];
     check(v.bones[0]==1&&std::fabs(v.weights[0]-1)<1e-6f&&v.weights[1]==0&&v.weights[2]==0&&v.weights[3]==0,"BDEF4 weights of (1, 1, 1, -2) on one bone become that bone's whole weight");
     auto normal=parse(readFile("tests/fixtures/corrupt-normal.pmx"));
     check(std::fabs(normal->vertices[1].normal.length()-1)<1e-4f,"a NaN normal is rebuilt from the faces");
     auto root=parse(readFile("tests/fixtures/corrupt-self_parent.pmx"));
     check(root->bones[0].parent==-1&&!nanoemModelBoneGetParentBoneObject(root->bones[0].source)&&root->order.size()==root->bones.size(),"a bone parented to itself becomes a root bone");
     auto heavy=parse(readFile("tests/fixtures/corrupt-body_heavy.pmx"));
     check(nanoemModelRigidBodyGetMass(heavy->bodies[1])==1e15f,"a 1e21 kg anchor body is limited to 1e15 kg");
     auto stiff=parse(readFile("tests/fixtures/corrupt-joint_stiff.pmx"));
     check(nanoemModelJointGetAngularStiffness(stiff->joints[0])[0]==1e12f,"a 1e14 joint spring is limited to 1e12");
     for(auto name:{"native-cloth21.pmx","native-chain.pmx","native-heavy-chain.pmx","native-nan-tail.pmx","corrupt-zero_normal.pmx"}){
      auto m=parse(readFile(std::string("tests/fixtures/")+name));bool quiet=true;for(auto& w:m->warnings)quiet&=!w.starts_with("Repaired ");
      check(quiet,(std::string(name)+" loads without repairs (its cached identity is unchanged)").c_str());}
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"model repair regression");}
    try{auto repaired=parse(readFile("tests/fixtures/corrupt-zero_normal.pmx"));bool finite=true;
     for(size_t i=0;i<3;i++){auto n=repaired->vertices[i].normal;auto t=repaired->tangents[i];for(int k=0;k<3;k++)finite&=std::isfinite(n[k])&&std::isfinite(t[k]);finite&=std::fabs(n.length()-1)<1e-4f;}
     check(finite,"a zero normal is repaired to a unit normal with a finite tangent");}catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"a zero normal is repaired to a unit normal with a finite tangent");}
    {auto cache=fs::absolute("test-output/corrupt-cache");fs::remove_all(cache);bool rejected=false;try{importAsset(fs::absolute("tests/fixtures/corrupt-soft.pmx"),cache,Json::object());}catch(...){rejected=true;}
     check(rejected&&(!fs::exists(cache/"assets")||fs::is_empty(cache/"assets")),"a corrupt model is rejected before its cache entry exists");fs::remove_all(cache);}
    // The share of a part's surface whose texels pass the 0.5 alpha test, sampled at its
    // triangle's UVs (the RTX Remix renderer skips or blends by it). The Core triangle maps
    // to u 0-0.5 on row 0: two of its seven samples fall in the quarter texture's opaque
    // column, and all of them in the padded atlas's opaque texels.
    try{auto cache=fs::absolute("test-output/coverage-cache");fs::remove_all(cache);auto load=[&](const char* file){auto id=importAsset(fs::absolute(file),cache,Json::object()).at("asset").get<std::string>();return loadAsset(cache,id);};
     auto coverage=[&](const std::shared_ptr<Model>& model){return std::make_pair(model->materials[0].alphaTexture,model->materials[0].alphaCoverage);};
     auto quarterModel=load("tests/fixtures/native-cutout-quarter.pmx"),noneModel=load("tests/fixtures/native-cutout-none.pmx"),opaqueModel=load("tests/fixtures/textured21.pmx"),atlasModel=load("tests/fixtures/native-cutout-atlas.pmx");
     auto quarter=coverage(quarterModel),none=coverage(noneModel),opaque=coverage(opaqueModel),atlas=coverage(atlasModel);
     check(quarter.first&&std::abs(quarter.second-2.f/7)<1e-6f&&none.first&&none.second==0&&!opaque.first&&opaque.second==1,"alpha-test coverage: sampled at the part's UVs, none below the reference, and one for opaque textures");
     check(atlas.first&&atlas.second==1,"alpha-test coverage ignores the transparent padding of an atlas");
     // Per triangle: the atlas's Fabric grid keeps only its triangles next to the opaque row.
     auto kept=[&](const Model& m,size_t part){size_t n=0;for(size_t t=m.materials[part].first/3;t<(m.materials[part].first+m.materials[part].count)/3;t++)n+=m.cutoutTriangles.at(t)!=0;return n;};
     size_t fabric=atlasModel->materials[1].count/3,fabricKept=kept(*atlasModel,1);
     check(atlasModel->cutoutTriangles.size()==atlasModel->indices.size()/3&&kept(*atlasModel,0)==1&&fabricKept>0&&fabricKept<fabric&&atlasModel->materials[1].alphaCoverage>0,"alpha-test cutout keeps the triangles that sample an opaque texel and drops the rest");
     check(kept(*noneModel,0)+kept(*noneModel,1)==0&&opaqueModel->cutoutTriangles.empty(),"alpha-test cutout drops every triangle of an all-cut texture and measures nothing without texture alpha");
     // Without texture morphs nothing is cut per instance: no part is dynamic and no pass mask is kept.
     bool still=true;for(auto* m:{&quarterModel,&noneModel,&atlasModel,&opaqueModel}){still&=(*m)->uvCutoutTriangles.empty()&&(*m)->cutoutMasks.empty();for(auto& part:(*m)->materials)still&=!part.dynamicCutout&&part.uvMorphCoverage==0;}
     check(still,"models without texture morphs keep no pass masks and no per-instance Remix cut");
     auto hidden=[](const std::shared_ptr<Model>& m){return remixPartHidden(m->materials[0],nullptr,0);};auto blends=[](const std::shared_ptr<Model>& m){return remixPartBlends(m->materials[0]);};
     check(hidden(noneModel)&&!hidden(quarterModel)&&blends(quarterModel)&&!hidden(atlasModel)&&!blends(atlasModel)&&!hidden(opaqueModel)&&!blends(opaqueModel),"RTX Remix skips a part with no coverage left and blends one below half, as before");
     fs::remove_all(cache);
    }catch(const std::exception& e){std::cout<<e.what()<<"\n";check(false,"alpha-test coverage of imported textures");}
    // A texture (UV) morph switches what an alpha-tested atlas shows (Ruan Mei's stockings).
    // Its triangles stay in the static cut and are cut again at an instance's current UVs:
    // the Fabric grid is transparent at rest, shows its last column at 0.5 and all at 1.
    try{auto cache=fs::absolute("test-output/uvmorph-cache");fs::remove_all(cache);
     auto id=importAsset(fs::absolute("tests/fixtures/native-cutout-uvmorph.pmx"),cache,Json::object()).at("asset").get<std::string>();auto model=loadAsset(cache,id);
     const auto& core=model->materials[0];const auto& fabric=model->materials[1];const unsigned first=fabric.first/3,count=fabric.count/3;
     bool listed=count==32&&model->uvCutoutTriangles.size()==count;for(unsigned k=0;listed&&k<count;k++)listed=model->uvCutoutTriangles[k]==first+k&&model->cutoutTriangles.at(first+k)==1;
     check(listed&&fabric.dynamicCutout&&!core.dynamicCutout&&model->cutoutMasks.size()==model->materials.size()&&model->cutoutMasks[1]&&!model->cutoutMasks[0],"triangles a UV morph moves stay in the static cut and keep their texture's pass mask");
     check(fabric.alphaCoverage==0&&std::abs(core.alphaCoverage-1.f/7)<1e-6f&&model->cutoutTriangles.at(0)==1,"rest coverage is measured as before, over every triangle at rest UVs (the Fabric's is 0)");
     // Blending is decided once per model: the Fabric is opaque at full weight, so it keeps the test.
     check(fabric.uvMorphCoverage==1&&core.uvMorphCoverage==0&&!remixPartBlends(fabric)&&remixPartBlends(core),"the styles a texture morph shows decide whether its part blends under RTX Remix (the Fabric never does)");
     {auto blends=[](float rest,float morphed){Material m;m.alphaTexture=true;m.dynamicCutout=true;m.alphaCoverage=rest;m.uvMorphCoverage=morphed;return remixPartBlends(m);};
      check(blends(.17f,.3f)&&blends(0,.3f)&&!blends(.17f,.9f)&&!blends(.6f,.2f)&&!blends(.95f,0)&&!blends(0,0),"a part a UV morph moves blends only when every style it shows is sparse, and never when none shows anything");}
     int uv=-1;for(size_t i=0;i<model->morphs.size();i++)if(nanoemModelMorphGetType(model->morphs[i])==NANOEM_MODEL_MORPH_TYPE_TEXTURE)uv=int(i);
     if(uv<0)throw std::runtime_error("the UV morph fixture has no texture morph");
     {World host;auto& p=host.get(host.create(model,{{"backend","source"},{"secondaryCollision",0}}));p.secondary.reset();bool cuts=true,drawn=true;
      for(auto [w,expected]:std::initializer_list<std::pair<float,unsigned>>{{0.f,0u},{.25f,0u},{.5f,8u},{.75f,24u},{1.f,32u},{0.f,0u}}){
       p.morphWeights[size_t(uv)]=w;p.updatePose();p.ensureSnapshot();auto cut=cutRemixTriangles(*model,p.snapshot->vertices);
       std::cout<<"UV morph "<<w<<": the Remix cut keeps "<<cut.kept.at(1)<<" of "<<count<<" Fabric triangles\n";
       cuts&=cut.keep.size()==model->cutoutTriangles.size()&&cut.kept.at(1)==expected&&cut.keptTriangles==expected&&cut.droppedTriangles==count-expected&&cut.kept.at(0)==1&&cut.keep.at(0)==1;
       drawn&=remixPartHidden(fabric,&cut,1)==(expected==0)&&!remixPartHidden(core,&cut,0);}
      check(cuts,"the Remix cut follows an instance's UV morph: nothing at rest, the last column at 0.5, everything at 1");
      check(drawn,"RTX Remix skips the Fabric (no coverage at rest) only while its current UVs leave nothing of it");
      // A snapshot that does not match the model keeps the static cut and hides nothing.
      std::span<const DrawVertex> vertices(p.snapshot->vertices);bool fallback=true;
      for(auto span:{vertices.first(vertices.size()-1),std::span<const DrawVertex>{}}){auto stale=cutRemixTriangles(*model,span);
       fallback&=stale.keep==model->cutoutTriangles&&stale.kept.at(1)==count&&stale.kept.at(0)==1&&stale.keptTriangles+stale.droppedTriangles==0&&!remixPartHidden(fabric,&stale,1);}
      RemixCutout empty;check(fallback&&!remixPartHidden(fabric,&empty,1)&&!remixPartHidden(fabric,nullptr,1),"a UV cut of a mismatched snapshot draws the static cut instead of hiding the part");}
     // Recomputing the cut: at once after a quiet spell, at most every remixCutFrames frames
     // while the UVs change on every frame (each new cut rebuilds index lists and buffers),
     // and once more within remixCutFrames frames after they settle.
     {bool made=false,settled=false,prompt=false;uint64_t version=0,madeFrame=0,uvVersion=0;unsigned moving=0;
      for(uint64_t frame=1;frame<=40;frame++){if(frame<=20)uvVersion=frame;if(frame==30)uvVersion=99;
       if(remixCutDue(made,version,uvVersion,frame,madeFrame)){made=true;version=uvVersion;madeFrame=frame;moving+=frame<=20;}
       if(frame==20+remixCutFrames)settled=version==20;if(frame==30)prompt=version==99;}
      check(moving<=20/remixCutFrames+1&&settled&&prompt&&!remixCutDue(true,99,99,1000,30),"the Remix cut is recomputed at most every few frames while UVs keep changing, settles, and follows a lone change at once");}
     // Publishing: the UV state changes only with the UV morphs' weights. Held, they cost no
     // statics refill and an unchanged pose publishes nothing; both snapshot buffers and the
     // hardware-skinning rest data still receive every change.
     for(bool gpu:{false,true}){
      World host;host.gpuSkinning=gpu;auto& p=host.get(host.create(model,{{"backend","source"},{"secondaryCollision",0}}));p.secondary.reset();
      auto republish=[&]{p.poseDirty=true;p.ensureSnapshot();};
      p.ensureSnapshot();republish();const float rest=model->vertices[3].uv[0];const auto uvVersion=p.snapshot->uvVersion;auto restVersion=p.snapshot->restVersion;
      p.morphWeights[size_t(uv)]=.5f;p.updatePose();p.ensureSnapshot();
      bool applied=p.snapshot->gpu==gpu&&p.snapshot->uvVersion==uvVersion+1&&p.snapshot->vertices[3].u==rest+.25f;
      if(gpu)applied&=p.snapshot->restVersion>restVersion&&p.gpuRest->changed.at(3)==p.snapshot->restVersion;
      republish();auto sequence=p.snapshot->sequence;auto statics=p.staticsVersion;restVersion=p.snapshot->restVersion;republish();republish();
      bool held=p.snapshot->sequence==sequence&&p.staticsVersion==statics&&p.uvVersion==uvVersion+1&&p.snapshot->restVersion==restVersion;
      std::set<const Snapshot*> buffers;bool current=true;
      for(int k=1;k<=4;k++){p.placement.setOrigin(btVector3(float(k),0,0));republish();buffers.insert(p.snapshot.get());current&=p.snapshot->vertices[3].u==rest+.25f;}
      p.morphWeights[size_t(uv)]=0;p.updatePose();p.ensureSnapshot();bool cleared=p.snapshot->uvVersion==uvVersion+2&&p.snapshot->vertices[3].u==rest;
      for(int k=1;k<=2;k++){p.placement.setOrigin(btVector3(0,float(k),0));republish();cleared&=p.snapshot->vertices[3].u==rest&&p.snapshot->uvVersion==uvVersion+2;}
      check(applied,gpu?"a UV morph change reaches the snapshot and the hardware-skinning rest data":"a UV morph change reaches the snapshot UVs");
      check(held,gpu?"a held UV morph neither refills statics nor restamps rest data on the hardware path":"a held UV morph neither refills statics nor republishes an unchanged pose");
      check(buffers.size()==2&&current&&cleared,gpu?"both reused snapshot buffers carry the current UVs (hardware path)":"both reused snapshot buffers carry the current UVs");
     }
     fs::remove_all(cache);
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"UV morph cutout and publishing");}
    // Nested group morphs: 13 links of coefficient 1000 end at an impulse morph (4), and a
    // lattice names each child twice for 40 levels before the vertex morph (0).
    try{
     auto chain=parse(readFile("tests/fixtures/native-group-chain.pmx"));World host;auto& p=host.get(host.create(chain,Json::object()));
     p.morphWeights[5]=1;p.updatePose();bool finite=true;for(float v:p.expandedMorphs())finite&=std::isfinite(v)&&std::fabs(v)<=1e3f;
     check(chain->morphs.size()==18&&finite&&p.expandedMorphs()[4]==1e3f,"a chain of group morphs keeps every derived weight finite and bounded");
     for(int k=0;k<4;k++)host.step(1./120,{0,0,0});
     bool bodies=true;for(auto& b:p.bodies){auto l=b->rigid->getLinearVelocity(),a=b->rigid->getAngularVelocity(),o=b->rigid->getWorldTransform().getOrigin();for(int k=0;k<3;k++)bodies&=std::isfinite(l[k])&&std::isfinite(a[k])&&std::isfinite(o[k]);}
     check(bodies,"an impulse morph at the end of the chain leaves every rigid body finite");
     auto lattice=parse(readFile("tests/fixtures/native-group-lattice.pmx"));World other;auto& q=other.get(other.create(lattice,Json::object()));
     auto started=std::chrono::steady_clock::now();q.morphWeights[5]=1;q.updatePose();const auto& weights=q.expandedMorphs();
     auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
     finite=true;for(float v:weights)finite&=std::isfinite(v)&&std::fabs(v)<=1e3f;
     check(seconds<5&&finite&&weights[0]!=0,"a lattice of group morphs expands within a bounded budget");
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"nested group morph regressions");}
    try{
     auto model=parse(readFile("tests/fixtures/native-large-materials.pmx"));
     check(model->materials.size()==513&&!model->warnings.empty(),"advisory material budget warns and retains every material");
     auto a=materialPath(model->id,0,"脸 / Face"),b=materialPath(model->id,1,"脸 / Face");
     check(a!=b&&a.find("face_face")!=std::string::npos&&a.size()<64,"short material paths retain authored meaning and distinguish duplicate names");
     auto slug=readableName("星穹铁道—昔涟");
     check(!slug.empty()&&std::all_of(slug.begin(),slug.end(),[](unsigned char c){return c<128;}),"engine paths avoid bytewise UTF-8 lowercase corruption");
     check(readableName("髪髪髪髪髪",7).size()<=7&&readableName("Hair / Front+")=="hair_front","path shortening preserves ASCII names and bounds");
     check(materialPath(model->id,0,"顏2+").ends_with("face_2_plus_1"),"common material terms have meaningful English paths");
     auto cache=fs::absolute("test-output/delete-cache");auto first=importAsset(fs::absolute("tests/fixtures/textured21.pmx"),cache,Json::object());
     std::string id=first.at("asset");auto manifest=first.at("info");auto texture=manifest["textures"][0]["base"].get<std::string>();
     // Peers derive the materials package and compare its hash: the streamed package and the
     // in-place mip chain keep the bytes of the earlier in-memory builds.
     {auto vtf=readFile(cache/"textures"/(texture+".vtf"));uint16_t w0=0,h0=0;std::memcpy(&w0,vtf.data()+16,2);std::memcpy(&h0,vtf.data()+18,2);
      std::vector<Bytes> mips;int w=w0,h=h0;mips.emplace_back(vtf.end()-std::ptrdiff_t(size_t(w)*h*4),vtf.end());
      while(w>1||h>1){int nw=std::max(1,w/2),nh=std::max(1,h/2);Bytes next(size_t(nw)*nh*4);auto& prior=mips.back();for(int y=0;y<nh;y++)for(int x=0;x<nw;x++)for(int c=0;c<4;c++){unsigned sum=0;for(int yy=0;yy<2;yy++)for(int xx=0;xx<2;xx++)sum+=prior[(size_t(std::min(h-1,y*2+yy))*w+std::min(w-1,x*2+xx))*4+c];next[(size_t(y)*nw+x)*4+c]=uint8_t(sum/4);}mips.push_back(std::move(next));w=nw;h=nh;}
      Bytes expected(vtf.begin(),vtf.begin()+80);for(auto it=mips.rbegin();it!=mips.rend();++it)expected.insert(expected.end(),it->begin(),it->end());
      check(w0>1&&vtf==expected&&vtf[56]==mips.size(),"the in-place mip chain matches the per-level VTF build");
      Bytes large(9u<<20);for(size_t i=0;i<large.size();i++)large[i]=uint8_t(i*31+(i>>11));auto source=cache/"stream-source.bin";writeAtomic(source,large);
      std::map<std::string,GmaEntry> streamed;streamed["materials/a.vmt"].data={'v','m','t'};streamed["materials/b.vtf"].file=source;streamed["materials/c.vtf"].file=cache/"textures"/(texture+".vtf");
      writeGma(cache/"streamed.gma",streamed,"Model Hotloader materials test");
      std::map<std::string,Bytes> memory={{"materials/a.vmt",{'v','m','t'}},{"materials/b.vtf",large},{"materials/c.vtf",vtf}};
      check(readFile(cache/"streamed.gma")==makeGma(memory,"Model Hotloader materials test"),"the streamed package is byte-identical to the in-memory one");
      // makeGma as released before 2.2 (bitwise CRC-32, one buffer).
      auto released=[](const std::map<std::string,Bytes>& files,const std::string& title){
       auto crc=[](const Bytes& b){uint32_t c=~0u;for(auto v:b){c^=v;for(int i=0;i<8;i++)c=(c>>1)^((0u-(c&1))&0xedb88320u);}return ~c;};
       StudioWriter g;g.b={'G','M','A','D',3};g.b.resize(21);g.str("");g.str(title);g.str("{\"type\":\"model\",\"tags\":[]}");g.str("Model Hotloader");size_t p=g.b.size();g.b.resize(p+4);g.i(p,1);int id=0;for(auto& [name,data]:files){p=g.b.size();g.b.resize(p+4);g.i(p,++id);g.str(name);p=g.b.size();g.b.resize(p+12);g.put<uint64_t>(p,data.size());g.put<uint32_t>(p+8,crc(data));}p=g.b.size();g.b.resize(p+4);for(auto& [name,data]:files)g.b.insert(g.b.end(),data.begin(),data.end());p=g.b.size();g.b.resize(p+4);g.put<uint32_t>(p,0);
       return g.b;};
      check(makeGma(memory,"Model Hotloader materials test")==released(memory,"Model Hotloader materials test"),"packages keep the bytes of earlier releases");
      fs::remove(source);fs::remove(cache/"streamed.gma");}
     std::string other(64,'a');writeJson(cache/"assets"/other/"manifest.json",manifest);
     model->id=id;auto rig=fitRig(*model,Json::object());packageCarrier(cache,rig,{});
     writeJson(cache/"fits"/("g18-"+id+".json"),{{"fit",rig.manifest}});
     writeJson(cache/"fit_overrides"/(id+".json"),{{"version",1}});
     auto deleted=deleteAssets(cache,{id});bool fitsGone=true;
     for(auto& e:fs::directory_iterator(cache/"fits"))fitsGone&=e.path().filename().string().find(id)==std::string::npos;
     check(deleted["pendingFiles"]==0&&!fs::exists(cache/"assets"/id)&&!fs::exists(cache/"rigs"/rig.key)&&!fs::exists(cache/"fit_overrides"/(id+".json"))&&fitsGone,"deletion removes asset, carrier, fitted geometry and corrections");
     check(fs::exists(cache/"textures"/(texture+".png")),"deletion retains a texture referenced by another model");
     deleteAssets(cache,{other});check(!fs::exists(cache/"textures"/(texture+".png"))&&!fs::exists(cache/"textures"/(texture+".vtf")),"last reference deletion removes shared texture and derivative");
     bool rejected=false;try{deleteAssets(cache,{"../outside"});}catch(...){rejected=true;}check(rejected,"deletion rejects path traversal before removing data");
     {std::string third(64,'c'),broken(64,'d');writeJson(cache/"assets"/third/"manifest.json",manifest);fs::create_directories(cache/"assets"/broken);{std::ofstream(cache/"assets"/broken/"manifest.json")<<"{\"textures\":[";}
      Json result;try{result=deleteAssets(cache,{third});}catch(const std::exception& e){std::cerr<<e.what()<<"\n";}
      check(!result.is_null()&&!fs::exists(cache/"assets"/third)&&result.value("unreadableManifests",0)==1&&fs::exists(cache/"assets"/broken),"an unrelated unreadable manifest neither blocks deletion nor is removed");fs::remove_all(cache/"assets"/broken);}
     check(simdDeformSupported(true,true,true,7)&&!simdDeformSupported(true,false,true,7)&&!simdDeformSupported(false,true,true,7)&&!simdDeformSupported(true,true,false,7)&&!simdDeformSupported(true,true,true,3),"SIMD skinning needs AVX2, FMA3 and OS-saved YMM state");
     {auto jobs=cache/"jobs";fs::create_directories(jobs/"old");fs::create_directories(jobs/"new");writeJson(jobs/"old"/"status.json",{{"state","failed"}});writeJson(jobs/"new"/"status.json",{{"state","running"}});
      fs::last_write_time(jobs/"old",fs::file_time_type::clock::now()-std::chrono::hours(48));auto removed=sweepJobFolders(cache,std::chrono::hours(24));
      check(removed==1&&!fs::exists(jobs/"old")&&fs::exists(jobs/"new"),"job folders untouched for a day are swept, recent ones stay");fs::remove_all(jobs);}
     auto source=fs::absolute("tests/fixtures/textured21.pmx");check(fs::exists(source),"library deletion keeps the original model source");
     registerShortName(cache,"assets",id);auto collision=id;collision[20]=collision[20]=='a'?'b':'a';rejected=false;try{registerShortName(cache,"assets",collision);}catch(...){rejected=true;}check(rejected,"short identifier collision cannot overwrite another asset");
     writeJson(cache/"cleanup.json",{"assets/"+id+"/materials-v5.gma","textures/"+texture+".vtf"});
     retainCacheFiles(cache,{fs::path("assets")/id});auto pending=readJson(cache/"cleanup.json");check(pending.size()==1&&pending[0]=="textures/"+texture+".vtf","reimport cancels deferred deletion of reused paths");fs::remove(cache/"cleanup.json");
     World host;auto handle=host.create(model,{{"backend","source"}});auto& instance=host.get(handle);
     check(instance.secondary->collisionFlags==Collide::Default&&(Collide::Default&Collide::Scene),"secondary collision defaults to the character and objects, from the Source scene");
     instance.sourceError="synthetic solver failure";instance.pendingSourceDelta=200;auto pose=instance.sourcePose;instance.reset();
     check(instance.sourceError.empty()&&instance.pendingSourceDelta==0&&instance.secondary->collisionFlags==Collide::Default&&instance.sourcePose.size()==pose.size(),"physics reset recovers stopped secondary world without changing its primary pose or mode");
     instance.secondary->step(1./60);check(instance.secondary->diagnostics()["bodies"]==model->bodies.size(),"reset retains every authored physics body");
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"QoL naming, deletion and reset regressions");}
    // Snapshots with an unchanged pose: a material-only change (a material morph on
    // the CPU path, forced opacity on the hardware path) still publishes.
    try{
     auto model=parse(readFile("tests/fixtures/native-chain.pmx"));model->materials[1].alpha=.2f;
     int tint=-1;for(size_t i=0;i<model->morphs.size();i++)if(nanoemModelMorphGetType(model->morphs[i])==NANOEM_MODEL_MORPH_TYPE_MATERIAL)tint=int(i);
     if(tint<0)throw std::runtime_error("the chain fixture has no material morph");
     for(bool gpu:{false,true}){
      World host;host.gpuSkinning=gpu;auto& p=host.get(host.create(model,{{"backend","source"},{"secondaryCollision",0}}));p.secondary.reset();
      // Two publishes allocate both snapshot buffers; the next one reuses the first.
      p.ensureSnapshot();p.poseDirty=true;p.ensureSnapshot();auto red=p.snapshot->materials[1].diffuse.x();
      p.morphWeights[size_t(tint)]=1;p.updatePose();p.ensureSnapshot();
      check(p.snapshot->gpu==gpu&&std::abs(p.snapshot->materials[1].diffuse.x()-(red+.1f))<1e-5f,gpu?"a material morph publishes on the hardware path with the pose unchanged":"a material morph publishes on the CPU path with the pose unchanged");
      p.setMaterialState(std::vector<bool>(model->materials.size(),true),{false,true});p.ensureSnapshot();
      check(p.snapshot->materials[1].alpha==1,gpu?"forced opacity publishes on the hardware path with the pose unchanged":"forced opacity publishes on the CPU path with the pose unchanged");
      p.morphWeights[size_t(tint)]=0;p.updatePose();p.ensureSnapshot();
      check(std::abs(p.snapshot->materials[1].diffuse.x()-red)<1e-5f,"clearing a material morph publishes with the pose unchanged");
     }
     // Frozen meshes are cached per snapshot buffer, and the two buffers are reused
     // for later poses: a cache entry is current only for the same geometry revision.
     World host;auto& p=host.get(host.create(model,{{"frozen",true}}));
     std::map<const Snapshot*,std::pair<uint64_t,std::vector<DrawVertex>>> cache;unsigned hits=0,builds=0;bool stale=false;std::set<const Snapshot*> buffers;
     auto draw=[&]{p.ensureSnapshot();const auto& s=*p.snapshot;buffers.insert(&s);auto it=cache.find(&s);
      if(it!=cache.end()&&it->second.first==s.geometry){hits++;stale|=it->second.second.size()!=s.vertices.size()||std::memcmp(it->second.second.data(),s.vertices.data(),s.vertices.size()*sizeof(DrawVertex))!=0;}
      else{builds++;cache[&s]={s.geometry,s.vertices};}};
     draw();
     for(int k=1;k<=6;k++){p.setBonePose(1,btTransform(btQuaternion(btVector3(1,0,0),.15f*float(k)),btVector3(0,0,0)));draw();draw();}
     auto geometry=p.snapshot->geometry,sequence=p.snapshot->sequence;p.morphWeights[size_t(tint)]=1;p.updatePose();draw();
     check(p.frozen&&buffers.size()==2&&builds>=7&&hits>=6,"frozen poses alternate between two reused snapshot buffers");
     check(!stale,"a reused snapshot buffer never matches a cached mesh of an earlier pose");
     check(p.snapshot->sequence==sequence+1&&p.snapshot->geometry==geometry,"an appearance-only publish keeps the geometry revision");
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"snapshot publication regressions");}
    // A bone chain stored child before parent is as deep as the model; ordering it
    // recursively overflowed the stack at 100,000 bones (0xC00000FD).
    try{
     Bytes b;auto raw=[&](const void* p,size_t n){auto q=static_cast<const uint8_t*>(p);b.insert(b.end(),q,q+n);};
     auto i32=[&](int32_t v){raw(&v,4);};auto f32=[&](float v){raw(&v,4);};auto text=[&](const std::string& s){i32(int32_t(s.size()));raw(s.data(),s.size());};
     raw("PMX ",4);f32(2.f);b.push_back(8);for(uint8_t v:{1,0,4,4,4,4,4,4})b.push_back(v);
     text("deep");text("deep");text("");text("");
     i32(3);for(int v=0;v<3;v++){for(float x:{float(v==1),float(v==2),0.f,0.f,0.f,-1.f,0.f,0.f})f32(x);b.push_back(0);i32(0);f32(1);}
     i32(3);for(int32_t v:{0,1,2})i32(v);i32(0);
     i32(1);text("m");text("");for(float v:{1.f,1.f,1.f,1.f,0.f,0.f,0.f,1.f,.2f,.2f,.2f})f32(v);b.push_back(0);for(float v:{0.f,0.f,0.f,1.f,1.f})f32(v);i32(-1);i32(-1);b.push_back(0);b.push_back(1);b.push_back(0);text("");i32(3);
     const int bones=100000;i32(bones);
     for(int k=0;k<bones;k++){text("b");text("b");f32(0);f32(float(k)*.001f);f32(0);i32(k+1<bones?k+1:-1);i32(0);uint16_t flags=0x1e;raw(&flags,2);f32(0);f32(.001f);f32(0);}
     for(int k=0;k<4;k++)i32(0);
     auto deep=parse(b);std::vector<int> position(deep->bones.size(),-1);for(size_t k=0;k<deep->order.size();k++)position[size_t(deep->order[k])]=int(k);
     bool parentsFirst=deep->order.size()==size_t(bones);for(int k=0;k<bones&&parentsFirst;k++){int parent=deep->bones[size_t(k)].parent;parentsFirst=parent<0||position[size_t(parent)]<position[size_t(k)];}
     check(parentsFirst,"a 100,000-bone chain stored child before parent is ordered parents first");
     // A loop loses the link that closes it (MMD shows such a bone unparented).
     auto cycle=parse(readFile("tests/fixtures/cycle.pmx"));std::vector<int> at(cycle->bones.size(),-1);for(size_t k=0;k<cycle->order.size();k++)at[size_t(cycle->order[k])]=int(k);
     bool ordered=cycle->order.size()==cycle->bones.size(),noted=false;for(size_t k=0;k<cycle->bones.size()&&ordered;k++){int parent=cycle->bones[k].parent;ordered=parent<0||at[size_t(parent)]<at[k];}
     for(auto& w:cycle->warnings)noted|=w.starts_with("Repaired bone ");
     check(ordered&&noted,"a cyclic bone hierarchy loads with the loop broken, parents first");
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"deep bone chain regression");}
    // Collide::Character: the model's simulated bodies (hair) against its bone-following
    // ones (head). Reference worlds keep Bullet's pair order unless a filter is needed.
    for(auto backend:{"reference","cpu_mt_v2"})try{
     auto model=parse(readFile("tests/fixtures/native-chain.pmx"));World host;auto& p=host.get(host.create(model,{{"backend","source"},{"secondaryBackend",backend},{"collisionFlags",Collide::Default}}));auto& s=*p.secondary;
     // A bone-following body and a simulated one whose authored groups collide.
     btCollisionObject* follower=nullptr;btCollisionObject* simulated=nullptr;auto& objects=s.dynamics()->getCollisionObjectArray();
     for(int i=0;i<objects.size();i++)for(int j=0;j<objects.size()&&!follower;j++){auto a=objects[i],b=objects[j];auto pa=a->getBroadphaseHandle(),pb=b->getBroadphaseHandle();
      if(a->getUserIndex2()!=ExternalCollisionTag&&b->getUserIndex2()!=ExternalCollisionTag&&a->isStaticOrKinematicObject()&&!b->isStaticOrKinematicObject()&&(pa->m_collisionFilterGroup&pb->m_collisionFilterMask)&&(pb->m_collisionFilterGroup&pa->m_collisionFilterMask)){follower=a;simulated=b;}}
     auto cache=static_cast<btHashedOverlappingPairCache*>(s.dynamics()->getPairCache()); // SecondaryBroadphase (no RTTI in Bullet)
     // Refreshing proxies replaces the broadphase handles: read them after each change.
     auto contacts=[&](unsigned flags){s.setCollisionFlags(flags);s.step(1./60);return cache->needsBroadphaseCollision(follower->getBroadphaseHandle(),simulated->getBroadphaseHandle());};
     bool ok=cache&&follower&&simulated&&contacts(Collide::Default)&&!contacts(Collide::Objects)&&contacts(Collide::Default)&&!contacts(0)&&contacts(Collide::Character)&&!contacts(Collide::World);
     check(ok&&s.diagnostics()["bodies"]==model->bodies.size(),(std::string("the character checkbox turns the body's contacts with hair and clothing off and on (")+backend+")").c_str());
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"character collision flag regression");}
    // A secondary world that fails to rebuild (Reset physics, a backend switch) leaves the working one
    // in place: the same world, backend, collision flags and pose, and its next tick runs.
    try{
     auto model=parse(readFile("tests/fixtures/native-chain.pmx"));World host;auto& p=host.get(host.create(model,{{"backend","source"},{"secondaryBackend","reference"},{"collisionFlags",Collide::Default}}));
     p.secondary->setCollisionFlags(Collide::Character);auto* before=p.secondary.get();auto skin=p.skin;
     auto samePose=[&]{if(p.skin.size()!=skin.size())return false;for(size_t i=0;i<skin.size();i++)if((p.skin[i].getOrigin()-skin[i].getOrigin()).length()>1e-5f)return false;return true;};
     // The world reads the backend name first while it is built: a name it does not know fails there.
     bool threw=false;p.secondaryBackend="no such backend";try{p.reset();}catch(const std::exception&){threw=true;}p.secondaryBackend="reference";
     bool kept=threw&&p.secondary.get()==before&&p.secondary->collisionFlags==Collide::Character&&samePose();
     threw=false;try{p.setSecondaryBackend("bogus");}catch(const std::exception&){threw=true;}
     kept=kept&&threw&&p.secondary.get()==before&&p.secondaryBackend=="reference";
     p.secondary->step(1./60);auto resets=p.secondary->resets;
     p.setSecondaryBackend("cpu_mt_v2");
     check(kept&&p.secondary.get()!=before&&p.secondaryBackend=="cpu_mt_v2"&&p.secondary->collisionFlags==Collide::Character&&p.secondary->resets==resets+1&&p.secondary->resetReason=="backend_change","a failed secondary rebuild keeps the working world, backend, flags and pose; a valid switch still replaces it");
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"secondary rebuild failure regression");}
    // Issue #6: Source drives the pelvis and limbs; nothing drives the MMD control roots above
    // the pelvis (全ての親 > センター > グルーブ), an unrelated root (操作中心) or the leg IK goals
    // under 全ての親. They ride with the Source pelvis, and IK goals with their chain's driven
    // bone, so a body far from the world origin keeps every vertex, follower body and IK goal
    // with it: on both skinning paths, frame-synchronous and asynchronous. nanoem keeps PMX IK on
    // its bones, which evaluatePose does not solve: the PMD below covers solved chains.
    try{
     auto m=parse(readFile("tests/fixtures/native-control-root.pmx"));
     auto bone=[&](const char* name){for(size_t i=0;i<m->bones.size();i++)if(m->bones[i].name==name)return i;throw std::runtime_error(std::string("no bone ")+name);};
     const size_t groove=bone("グルーブ"),pelvis=bone("lower body"),ankle=bone("left ankle"),toe=bone("left toe"),legGoal=bone("左足ＩＫ"),toeGoal=bone("左つま先ＩＫ"),heelGoal=bone("left heel IK");
     const size_t control=m->vertices.size()-5; // BDEF2, BDEF4 and BDEF1 on グルーブ, BDEF1 on 操作中心, BDEF2 on 全ての親 and センター
     check(m->gpuSkin()->cpuVertex[control+1]&&!m->gpuSkin()->cpuVertex[control+2],"a small fourth control-root weight keeps CPU skinning, a whole one hardware skinning");
     // A bone's pose relative to another: the rest offset between them, unrotated (MMD units).
     auto atRest=[&](const btTransform& relative,const btVector3& offset){const auto& r=relative.getBasis();return (relative.getOrigin()-offset).length()<2e-3f&&(r.getColumn(0)-btVector3(1,0,0)).length()<1e-4f&&(r.getColumn(1)-btVector3(0,1,0)).length()<1e-4f;};
     const btTransform placed(btQuaternion(btVector3(0,0,1),.7f),btVector3(4000,-3000,500));
     for(auto [gpu,backend]:{std::pair{false,"reference"},std::pair{true,"cpu_mt_v2"}}){
      World host;host.gpuSkinning=gpu;host.poseSmoothing=false;auto& p=host.get(host.create(m,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",0},{"secondaryBackend",backend}}));
      const auto& rig=*p.sourceRig;std::vector<btTransform> palette;for(auto& b:rig.bones)palette.push_back(placed*b.rest);
      auto present=[&](double t,uint64_t frame){p.submitPresentationPose(palette,t,frame);p.secondary->waitAsyncIdle();p.stepSource();p.ensureSnapshot();};
      auto toWorld=[&](const btVector3& mmd){return placed*(rigMeshBind(rig)*(toSource(mmd)*rig.scale));};
      std::string mode=std::string(gpu?" (hardware skinning, ":" (CPU skinning, ")+backend+")";
      present(1,1);
      bool same=atRest(p.skin[pelvis].inverse()*p.skin[groove],{0,0,0});if(gpu){const auto& s=*p.snapshot;same=same&&s.gpu;for(int k=0;k<12&&same;k++)same=std::fabs(s.palette[groove*12+k]-s.palette[pelvis*12+k])<(k%4==3?.01f:1e-5f);}
      check(same,("the groove's skinning matrix is the pelvis's under a rigid pose"+mode+(gpu?", in the hardware palette too":"")).c_str());
      // What GetAlignmentProbe reads: every vertex of the drawn parts where it is drawn; a
      // hardware-skinned snapshot does not hold most of them, the palette places them.
      {std::vector<uint8_t> referenced(m->vertices.size(),0);for(auto i:m->indices)referenced[i]=1;size_t expected=0,visited=0;for(auto r:referenced)expected+=r;double drawnError=0;
       p.drawnVertices([&](size_t i,const btVector3& v){visited++;drawnError=std::max(drawnError,double((v-toWorld(m->vertices[i].position)).length()));});
       check(p.snapshot->gpu==gpu&&visited==expected&&drawnError<.02,("drawn vertices are read where the active skinning path draws them, with the far body"+mode).c_str());}
      p.requireCpuVertices();double error=0,radius=0;
      for(size_t i=0;i<m->vertices.size();i++){auto& d=p.snapshot->vertices[i];auto expected=toWorld(m->vertices[i].position);error=std::max(error,double((btVector3(d.x,d.y,d.z)-expected).length()));radius=std::max(radius,double((expected-placed.getOrigin()).length()));}
      check(error<.02,("vertices weighted to MMD control roots follow the Source pelvis far from the world origin"+mode).c_str());
      check((p.snapshot->minimum-placed.getOrigin()).length()<=2*radius+1&&(p.snapshot->maximum-placed.getOrigin()).length()<=2*radius+1,("snapshot bounds stay around the far body"+mode).c_str());
      check(atRest(p.effectiveScratch[pelvis],{0,0,0}),("a Source-driven bone below the control roots keeps a true local pose"+mode).c_str());
      auto bodies=p.secondary->diagnostics()["bodyList"];auto w=bodies.at(3)["worldPosition"];
      check(bodies.at(3)["follower"].get<bool>()&&(btVector3(w[0],w[1],w[2])-toWorld({0,8,0})).length()<.02f,("a follower body on センター follows the body"+mode).c_str());
      // 全ての親, センター, グルーブ, 操作中心 and 左足IK親 ride with the pelvis; the leg and toe IK
      // goals on the foot, with the heel IK goal hung below them.
      auto diagnostics=p.diagnostics();
      check(diagnostics["floatingRoots"]==Json{{"roots",2},{"bones",5},{"vertices",5},{"bodies",1}}&&diagnostics["anchoredGoals"]==Json{{"goals",2},{"bones",3},{"vertices",0},{"bodies",0}},("diagnostics count the bones Source does not reach, apart by what carries them, and what they carry"+mode).c_str());
      // Bend the left hip: the leg and toe IK goals ride on the Source-driven ankle and toe, and
      // the heel IK goal hung below the toe IK goal follows the foot. Goal placement only: PMX IK
      // is not solved.
      auto ankleBefore=p.global[ankle].getOrigin();int thigh=-1;for(size_t i=0;i<rig.bones.size();i++)if(rig.bones[i].name=="ValveBiped.Bip01_L_Thigh")thigh=int(i);
      auto hip=rig.bones[thigh].rest.getOrigin();btTransform bend=btTransform(btQuaternion::getIdentity(),hip)*btTransform(btQuaternion(btVector3(0,1,0),.9f),btVector3(0,0,0))*btTransform(btQuaternion::getIdentity(),-hip);
      std::vector<bool> below(rig.bones.size(),false);for(size_t i=0;i<rig.bones.size();i++)below[i]=int(i)==thigh||(rig.bones[i].parent>=0&&below[size_t(rig.bones[i].parent)]);
      for(size_t i=0;i<rig.bones.size();i++)palette[i]=placed*(below[i]?bend:btTransform::getIdentity())*rig.bones[i].rest;
      present(1+1./60,2);const auto& g=p.global;auto offset=[&](size_t a,size_t b){return m->bones[b].position-m->bones[a].position;};
      check((g[ankle].getOrigin()-ankleBefore).length()>2&&atRest(g[ankle].inverse()*g[legGoal],offset(ankle,legGoal))&&atRest(g[toe].inverse()*g[toeGoal],offset(toe,toeGoal)),("leg and toe IK goals ride on the Source-driven ankle and toe"+mode).c_str());
      check(atRest(g[toe].inverse()*g[heelGoal],offset(toe,heelGoal)),("an IK goal hung below the toe IK goal follows the bent foot"+mode).c_str());
      // A hidden part is not drawn: the probe skips its vertices (the control vertices are Core only).
      p.setMaterialState({false,true},{false,false});p.ensureSnapshot();bool core=false,fabric=false;p.drawnVertices([&](size_t i,const btVector3&){core|=i>=control;fabric|=i==3;});
      check(!core&&fabric,("drawn vertices skip hidden parts"+mode).c_str());
     }
     // A VRM spring joint on a root bone (操作中心) rests relative to the pelvis the root rides
     // with: under a turned far pose its tail stays where the turned body puts it.
     {World host;host.poseSmoothing=false;auto& p=host.get(host.create(m,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",0}}));p.secondary.reset();const auto& rig=*p.sourceRig;
      std::vector<btTransform> palette;for(auto& b:rig.bones)palette.push_back(placed*b.rest);p.submitPresentationPose(palette,1,1);p.evaluate(false);
      const size_t n=m->bones.size(),root=bone("操作中心");auto setup=std::make_shared<SpringSetup>();setup->springs.push_back({"root",-1,{}});
      SpringSetup::Joint joint;joint.spring=0;joint.bone=int(root);joint.axis=btVector3(0,0,-1);joint.length=1;joint.stiffness=4;setup->joints.push_back(joint);
      setup->jointOfBone.assign(n,-1);setup->jointOfBone[root]=0;setup->isAffected.assign(n,0);setup->isAffected[root]=1;setup->affected={int(root)};
      std::vector<uint8_t> driven(n,0);for(size_t i=0;i<n;i++)driven[i]=p.sourceControl[i]>=0;
      SpringSystem springs(*m,setup,driven,rig.bones[0].mmd);springs.reset(p.skin);for(int k=0;k<30;k++)springs.step(1.f/60,p.skin,1,1,nullptr);
      auto tail=p.skin[root]*(m->bones[root].position+joint.axis*joint.length);
      check(driven[root]==0&&(springs.tails(p.skin)[0]-tail).length()<1e-3f,"a VRM spring joint on a root bone rests relative to the pelvis it rides with");}
     // Server instances are placed at their spawn point and follow the physical Source pose.
     {World host;auto& p=host.get(host.create(m,{{"backend","source"},{"position",{300,-200,40}},{"secondaryCollision",0}}));const auto& rig=*p.sourceRig;
      std::vector<btTransform> physical(18),manipulation(rig.bones.size(),btTransform::getIdentity());for(auto& b:rig.bones)if(b.physics>=0)physical[size_t(b.physics)]=placed*b.rest;
      p.submitSourcePose(physical,manipulation,1);p.evaluate(false);p.poseDirty=true;p.ensureSnapshot();double error=0;
      for(size_t i=0;i<m->vertices.size();i++){auto& d=p.snapshot->vertices[i];error=std::max(error,double((btVector3(d.x,d.y,d.z)-placed*(rigMeshBind(rig)*(toSource(m->vertices[i].position)*rig.scale))).length()));}
      check(error<.02,"vertices weighted to MMD control roots follow the physical Source pose of a server instance placed at its spawn point");}
     // A PMD, whose IK evaluatePose solves: each heel IK turns a heel Source does not drive (below
     // the driven ankle) toward its goal, hung from the toe IK goal (left) or from 全ての親 itself
     // (right). With both hips bent far from the world origin the goals keep their rest offset
     // from the feet, so the heels and their vertices stay at rest on the bent feet.
     {auto d=parse(readFile("tests/fixtures/native-control-root.pmd"));
      auto at=[&](const char* name){for(size_t i=0;i<d->bones.size();i++)if(d->bones[i].name==name)return i;throw std::runtime_error(std::string("no PMD bone ")+name);};
      World host;host.poseSmoothing=false;auto& p=host.get(host.create(d,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",0}}));p.secondary.reset();const auto& rig=*p.sourceRig;
      struct Side{size_t ankle,heel,goal;btTransform bend;};Side sides[2]={{at("left ankle"),at("left heel"),at("left heel IK")},{at("right ankle"),at("right heel"),at("right heel IK")}};
      std::vector<int> bent(rig.bones.size(),-1);
      for(int s=0;s<2;s++){int thigh=-1;for(size_t i=0;i<rig.bones.size();i++)if(rig.bones[i].name==(s?"ValveBiped.Bip01_R_Thigh":"ValveBiped.Bip01_L_Thigh"))thigh=int(i);if(thigh<0)throw std::runtime_error("no thigh in the PMD rig");
       auto hip=rig.bones[size_t(thigh)].rest.getOrigin();sides[s].bend=btTransform(btQuaternion::getIdentity(),hip)*btTransform(btQuaternion(btVector3(0,1,0),s?-.6f:.9f),btVector3(0,0,0))*btTransform(btQuaternion::getIdentity(),-hip);
       for(size_t i=0;i<rig.bones.size();i++)if(int(i)==thigh||(rig.bones[i].parent>=0&&bent[size_t(rig.bones[i].parent)]==s))bent[i]=s;}
      bool chains=true;for(auto& s:sides)chains=chains&&p.sourceControl[s.ankle]>=0&&p.sourceControl[s.heel]<0&&p.sourceControl[s.goal]<0;
      check(chains,"PMD heel IK chains hang below Source-driven ankles and are not Source-driven themselves");
      auto anchor=ikAnchors(*d,p.sourceControl);
      check(anchor.size()==d->bones.size()&&anchor[at("左足ＩＫ")]==int(sides[0].ankle)&&anchor[at("左つま先ＩＫ")]==int(at("left toe"))&&anchor[sides[0].goal]<0&&anchor[sides[1].goal]==int(sides[1].ankle)&&anchor[at("センター")]<0,
            "IK goals ride on the driven effector, a goal hung from 全ての親 on its effector's nearest driven ancestor, a goal below an anchored goal on that goal");
      std::vector<btTransform> palette;for(size_t i=0;i<rig.bones.size();i++)palette.push_back(placed*(bent[i]>=0?sides[bent[i]].bend:btTransform::getIdentity())*rig.bones[i].rest);
      p.submitPresentationPose(palette,1,1);p.evaluate(false);p.poseDirty=true;p.ensureSnapshot();const auto& g=p.global;
      auto offset=[&](size_t a,size_t b){return d->bones[b].position-d->bones[a].position;};
      check(atRest(g[sides[0].ankle].inverse()*g[sides[0].heel],offset(sides[0].ankle,sides[0].heel))&&atRest(g[sides[1].ankle].inverse()*g[sides[1].heel],offset(sides[1].ankle,sides[1].heel)),
            "solved PMD heel IK keeps each heel at rest on its bent foot, the goal below the toe IK goal and the goal under 全ての親 alike");
      double error=0;for(size_t i=0;i<d->vertices.size();i++){const auto& v=d->vertices[i];int side=v.bones[0]==int(sides[0].heel)||v.bones[0]==int(at("left heel tip"))?0:v.bones[0]==int(sides[1].heel)||v.bones[0]==int(at("right heel tip"))?1:-1;
       auto expected=placed*(side>=0?sides[side].bend:btTransform::getIdentity())*(rigMeshBind(rig)*(toSource(v.position)*rig.scale));auto& s=p.snapshot->vertices[i];error=std::max(error,double((btVector3(s.x,s.y,s.z)-expected).length()));}
      check(error<.02,"PMD vertices on the heels follow the bent feet and those on グルーブ the pelvis, far from the world origin");
      // Before 2.3.0 (no Source root): the goals stayed at the world origin and the solver turned the heels toward it.
      std::vector<btTransform> local,global,skin,effective;std::vector<float> none(d->morphs.size(),0.f);evaluatePose(*d,p.manual,none,&p.sourceControl,&p.sourcePose,nullptr,local,global,skin,effective);
      check(!atRest(global[sides[0].ankle].inverse()*global[sides[0].heel],offset(sides[0].ankle,sides[0].heel))&&!atRest(global[sides[1].ankle].inverse()*global[sides[1].heel],offset(sides[1].ankle,sides[1].heel)),
            "the PMD heel IK chains are solved: with goals left at the world origin they turn the heels");
      // Physics feedback gets the frame spring joints are measured in (SpringSystem): the pelvis
      // carrier for a root bone, the parent for any other bone, an anchored goal too.
      std::map<size_t,btTransform> frames;PoseHooks hooks;hooks.physics=[&](size_t i,btTransform&,btTransform&,const btVector3&,const btTransform* parent){if(parent)frames[i]=*parent;};
      int root=rig.bones[0].mmd;evaluatePose(*d,p.manual,none,&p.sourceControl,&p.sourcePose,&hooks,local,global,skin,effective,root);
      auto rides=[&](size_t bone,const btTransform& frame){return frames.count(bone)&&atRest(frame.inverse()*frames[bone],{0,0,0});};
      check(root>=0&&rides(at("全ての親"),skin[size_t(root)])&&rides(at("左足ＩＫ"),global[at("左足IK親")])&&rides(sides[1].goal,global[at("全ての親")])&&rides(at("センター"),global[at("全ての親")]),
            "physics feedback receives the pelvis carrier for a root bone and the parent for any other");}
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"control roots above the pelvis (issue #6)");}
    std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
