// PMX chains authored with very heavy links (MMD wings use masses near 1e8 that
// fall x4 per link) must stay on their anchors. Newer Bullet drops joint rows
// whose inverse-mass sum is below FLT_EPSILON; MikuMikuDance's Bullet 2.75
// solved them. Holds the rest pose and checks that no dynamic body moves.
// Usage: mmdhl_heavy_chain_tests <pmx> [max drift in PMX units, default .05]
#include "runtime.hpp"
#include "rig.hpp"
#include "secondary.hpp"
#include "scene.hpp"
#include "jobs.hpp"
#include <iostream>
#include <string>
#include "wide_main.hpp"
using namespace mmd;
int wmain(int argc,wchar_t** argv){try{
 if(argc<2)throw std::runtime_error("Pass native-heavy-chain.pmx (or any PMX) and an optional drift limit");
 double limit=argc>2?std::stod(argv[2]):.05;
 auto model=parse(readFile(fs::path(argv[1])));bool passed=true;
 for(auto backend:{"reference","cpu_mt","cpu_mt_v2"}){
  World host;auto id=host.create(model,{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",0},{"secondaryBackend",backend}});auto& p=host.get(id);
  std::vector<btTransform> rest;for(auto& b:p.sourceRig->bones)rest.push_back(b.rest);
  Json first;
  for(int f=0;f<=300;f++){p.submitPresentationPose(rest,f/60.,f+1);p.secondary->waitAsyncIdle();p.stepSource();if(f==1)first=p.secondary->diagnostics(true)["bodyList"];}
  auto last=p.secondary->diagnostics(true)["bodyList"];double drift=0;int worst=-1;
  for(size_t i=0;i<last.size();i++){if(last[i]["follower"].get<bool>())continue;
   btVector3 a(first[i]["position"][0],first[i]["position"][1],first[i]["position"][2]),b(last[i]["position"][0],last[i]["position"][1],last[i]["position"][2]);
   if((b-a).length()>drift){drift=(b-a).length();worst=int(i);}
  }
  std::cout<<backend<<": largest dynamic-body drift over 5 s "<<drift<<" PMX units (body "<<worst<<")"<<std::endl;
  passed&=drift<limit;
 }
 shutdownJobs();std::cout<<(passed?"PASS":"FAIL")<<": heavy chains stay on their anchors"<<std::endl;return passed?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;shutdownJobs();return 2;}}
