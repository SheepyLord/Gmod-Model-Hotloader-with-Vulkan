#include "runtime.hpp"
#include "scene.hpp"
#include "secondary.hpp"
#include "fitter.hpp"
#include "rig.hpp"
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
    World w;w.setMirror(1,{{"shape","box"},{"half",{10,10,1}},{"position",{0,0,0}},{"mass",0}});w.step(1,btVector3(0,0,-9.8f));check(w.dropped>.9,"catchup bounded and reported");check(w.takeImpulses().empty(),"static environment produces no feedback");w.removeMirror(1);check(w.mirrors.empty(),"mirror removal");
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

    // One non-finite or runaway value per numeric section never reaches Bullet or the GPU.
    for(auto bad:{"uv","material","ik","morph_vertex","morph_bone","morph_material","morph_group","morph_impulse","body_orientation","body_mass","body_damping","joint_limit","joint_spring","soft","soft_iterations"}){
     bool rejected=false;try{parse(readFile(std::string("tests/fixtures/corrupt-")+bad+".pmx"));}catch(const std::runtime_error&){rejected=true;}
     check(rejected,(std::string("a corrupt ")+bad+" value is rejected at load").c_str());}
    try{auto repaired=parse(readFile("tests/fixtures/corrupt-zero_normal.pmx"));bool finite=true;
     for(size_t i=0;i<3;i++){auto n=repaired->vertices[i].normal;auto t=repaired->tangents[i];for(int k=0;k<3;k++)finite&=std::isfinite(n[k])&&std::isfinite(t[k]);finite&=std::fabs(n.length()-1)<1e-4f;}
     check(finite,"a zero normal is repaired to a unit normal with a finite tangent");}catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"a zero normal is repaired to a unit normal with a finite tangent");}
    {auto cache=fs::absolute("test-output/corrupt-cache");fs::remove_all(cache);bool rejected=false;try{importAsset(fs::absolute("tests/fixtures/corrupt-body_orientation.pmx"),cache,Json::object());}catch(...){rejected=true;}
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
     fs::remove_all(cache);
    }catch(const std::exception& e){std::cout<<e.what()<<"\n";check(false,"alpha-test coverage of imported textures");}
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
     check(instance.secondary->collisionMode==2,"secondary collision default includes the Source scene");
     instance.sourceError="synthetic solver failure";instance.pendingSourceDelta=200;auto pose=instance.sourcePose;instance.reset();
     check(instance.sourceError.empty()&&instance.pendingSourceDelta==0&&instance.secondary->collisionMode==2&&instance.sourcePose.size()==pose.size(),"physics reset recovers stopped secondary world without changing its primary pose or mode");
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
     bool cyclic=false;try{parse(readFile("tests/fixtures/cycle.pmx"));}catch(const std::exception& e){cyclic=std::string(e.what()).find("Cyclic")!=std::string::npos;}
     check(cyclic,"a cyclic bone hierarchy is still rejected");
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";check(false,"deep bone chain regression");}
    std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
