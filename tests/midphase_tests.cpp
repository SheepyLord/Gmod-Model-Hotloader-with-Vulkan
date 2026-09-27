// Mid-phase pair gate and broadphase selection: the gate must leave every
// multicore step bit-identical (it only skips narrowphase work that could not
// have produced a contact), and `auto` must resolve per backend.
#include "runtime.hpp"
#include "secondary.hpp"
#include "rig.hpp"
#include "jobs.hpp"
#include "scene.hpp"
#include "broadphase.hpp"
#include "ordered_dispatcher.hpp"
#include "contact_fixture.hpp"
#include <cstring>
#include <iostream>
#include <vector>
using namespace mmd;
namespace {
std::vector<btTransform> palette(const Rig& rig,double t){std::vector<btTransform> out;btTransform drive(btQuaternion(btVector3(0,0,1),float(std::sin(t*1.3)*.2)),btVector3(float(std::sin(t*1.7)*12),float(std::cos(t*.9)*6)-2,float(std::sin(t*2)*4)));out.reserve(rig.bones.size());for(auto& b:rig.bones)out.push_back(drive*b.rest);return out;}
struct Run {std::vector<float> trajectory;Json diagnostics;unsigned contacts=0;};
// Every collision object transform after every frame, as raw floats, so the
// comparison is exact rather than tolerance based.
Run simulate(const std::shared_ptr<Model>& model,const char* backend,int mode,const char* broadphase,bool gate,int frames){
 setMidphaseGate(gate);Run run;
 World host;auto handle=host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",mode},{"secondaryBroadphase",broadphase},{"secondaryBackend",backend}});auto& p=host.get(handle);
 p.secondary->setPairCounting(true);auto world=p.secondary->dynamics();
 for(int frame=0;frame<=frames;frame++){double t=frame/60.;if(mode)publishScene(test::contactScene(t,mode));
  p.submitPresentationPose(palette(*p.sourceRig,t),t,uint64_t(frame)+1);p.secondary->waitAsyncIdle();p.stepSource();
  if(!p.sourceError.empty())throw std::runtime_error(p.sourceError);
  for(int i=0;i<world->getNumCollisionObjects();i++){const auto& tr=world->getCollisionObjectArray()[i]->getWorldTransform();const auto& o=tr.getOrigin();const auto& b=tr.getBasis();
   for(int k=0;k<3;k++){run.trajectory.push_back(o[k]);run.trajectory.push_back(b[k].x());run.trajectory.push_back(b[k].y());run.trajectory.push_back(b[k].z());}}
  if(mode){auto d=p.secondary->diagnostics(false);run.contacts=std::max(run.contacts,d["externalContacts"].get<unsigned>());}
 }
 run.diagnostics=p.secondary->diagnostics(false);publishScene(nullptr);setMidphaseGate(true);return run;
}
bool identical(const Run& a,const Run& b){return a.trajectory.size()==b.trajectory.size()&&!a.trajectory.empty()&&std::memcmp(a.trajectory.data(),b.trajectory.data(),a.trajectory.size()*sizeof(float))==0;}
}
int main(int argc,char** argv){try{
 if(argc!=2&&argc!=4)throw std::runtime_error("Pass the rigid chain fixture [asset cache root, asset id]");auto model=parse(readFile(argv[1]));
 int checks=0;auto check=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);++checks;std::cout<<"PASS "<<name<<"\n";};
 Secondary::setSleepPolicy(false,.15f,.2f,1.f);Secondary::setAsyncWaitBudget(0);
 check(resolveBroadphase("auto",true)=="sap"&&resolveBroadphase("auto",false)=="dbvt-fast"&&resolveBroadphase("dbvt",true)=="dbvt"&&resolveBroadphase("sap",false)=="sap","auto resolves to sweep-and-prune for relaxed-order worlds and the ordered DBVT otherwise");
 bool rejected=false;try{resolveBroadphase("grid",true);}catch(const std::exception&){rejected=true;}check(rejected,"unknown broadphase names are rejected");
 check(secondaryBroadphaseDefault()=="auto","the default broadphase is auto");
 for(int mode:{0,2}){
  auto gated=simulate(model,"cpu_mt",mode,"dbvt-fast",true,240),plain=simulate(model,"cpu_mt",mode,"dbvt-fast",false,240);
  std::cout<<"mode "<<mode<<": gated per step dispatched="<<gated.diagnostics["midphase"]["dispatched"]<<" gated="<<gated.diagnostics["midphase"]["gated"]<<"; ungated dispatched="<<plain.diagnostics["midphase"]["dispatched"]<<" contacts="<<gated.contacts<<"\n";
  check(identical(gated,plain),mode?"the gate leaves scene-contact steps (mesh floor and convex plate) bit-identical":"the gate leaves model-only steps bit-identical");
  check(gated.diagnostics["midphase"]["dispatched"].get<uint64_t>()+gated.diagnostics["midphase"]["gated"].get<uint64_t>()==plain.diagnostics["midphase"]["dispatched"].get<uint64_t>(),"every live pair is either gated or dispatched");
  check(!mode||gated.contacts>0,"scene contacts still occur with the gate");
 }
 {
  auto v2=simulate(model,"cpu_mt_v2",0,"auto",true,60),v1=simulate(model,"cpu_mt",0,"auto",true,60);
  check(v2.diagnostics["broadphase"]=="sap"&&v2.diagnostics["broadphaseRequested"]=="auto"&&v2.diagnostics["broadphaseFallback"]=="","a v2 world on auto runs sweep-and-prune without fallback");
  check(v1.diagnostics["broadphase"]=="dbvt-fast"&&v1.diagnostics["broadphaseRequested"]=="auto","a cpu_mt world on auto keeps the ordered DBVT");
  auto explicitSap=simulate(model,"cpu_mt_v2",0,"sap",true,60);
  check(identical(v2,explicitSap),"auto and explicit sap produce the same v2 trajectory");
 }
 {
  setSecondaryDbvtMargin(.25f);auto fat=simulate(model,"cpu_mt_v2",0,"dbvt-fast",true,60);setSecondaryDbvtMargin(.05f);
  auto thin=simulate(model,"cpu_mt",0,"dbvt-fast",true,60);
  check(fat.diagnostics["broadphase"]=="dbvt-fast"&&thin.diagnostics["broadphase"]=="dbvt-fast","the v2 DBVT margin setting keeps the dbvt-fast tree");
  bool caught=false;try{setSecondaryDbvtMargin(-1);}catch(const std::exception&){caught=true;}check(caught,"invalid DBVT margins are rejected");
 }
 if(argc==4){
  // A dense production rig (hundreds of cross-group pairs) exercises the gate for real.
  auto dense=loadAsset(argv[2],argv[3]);std::cout<<"dense rig "<<dense->name<<": "<<dense->bodies.size()<<" bodies, "<<dense->joints.size()<<" joints\n";
  for(int mode:{0,2}){
   auto gated=simulate(dense,"cpu_mt",mode,"dbvt-fast",true,180),plain=simulate(dense,"cpu_mt",mode,"dbvt-fast",false,180);
   double dispatched=gated.diagnostics["midphase"]["dispatched"].get<double>(),skipped=gated.diagnostics["midphase"]["gated"].get<double>();
   std::cout<<"dense mode "<<mode<<": per step dispatched="<<dispatched/181<<" gated="<<skipped/181<<" ("<<100*skipped/std::max(1.,dispatched+skipped)<<" % of live pairs), contacts="<<gated.contacts<<"\n";
   check(identical(gated,plain),mode?"the gate leaves a dense rig's scene-contact steps bit-identical":"the gate leaves a dense rig's model-only steps bit-identical");
   check(skipped>0,"a dense rig gates separated pairs");
  }
 }
 shutdownJobs();std::cout<<checks<<" mid-phase and broadphase checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";shutdownJobs();return 1;}}
