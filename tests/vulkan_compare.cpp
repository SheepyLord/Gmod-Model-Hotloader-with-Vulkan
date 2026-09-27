// Lockstep comparison of two secondary backends on one character: the same
// rigid whole-body motion as mmdhl_profile, sleep off, identical tuning.
// Reports body divergence per tick and each backend's step time.
// Usage: mmdhl_vulkan_compare (--pmx <file> | --cache <root> --asset <id>) [--steps N] [--iterations N]
//        [--a cpu_mt_v2] [--b gpu_vulkan] [--order colored|ordered] [--json <file>]
//        [--dxvk <d3d9.dll>]  (load a patched DXVK and create a D3D9 device first, so the
//                              Vulkan solver runs on DXVK's device and shared compute queues)
#include "runtime.hpp"
#include "secondary.hpp"
#include "rig.hpp"
#include "jobs.hpp"
#include "vulkan_solver.hpp"
#include "compute_solver.hpp"
#include "crash_report.hpp"
#include <Windows.h>
#include <d3d9.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <vector>
using namespace mmd;
namespace {
// A D3D9Ex device from the given (patched DXVK) d3d9.dll on a hidden window.
struct D3D9Host {
 HMODULE library=nullptr;HWND window=nullptr;IDirect3D9Ex* d3d=nullptr;IDirect3DDevice9Ex* device=nullptr;
 explicit D3D9Host(const fs::path& path){
  library=LoadLibraryW(path.c_str());if(!library)throw std::runtime_error("Cannot load "+utf8(path.wstring()));
  auto create=reinterpret_cast<HRESULT(WINAPI*)(UINT,IDirect3D9Ex**)>(GetProcAddress(library,"Direct3DCreate9Ex"));
  if(!create||FAILED(create(D3D_SDK_VERSION,&d3d)))throw std::runtime_error("Direct3DCreate9Ex failed");
  window=CreateWindowExW(0,L"STATIC",L"mmdhl-vulkan-shared",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,nullptr,nullptr);
  D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.hDeviceWindow=window;
  if(FAILED(d3d->CreateDeviceEx(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,nullptr,&device)))throw std::runtime_error("CreateDeviceEx failed");
 }
 ~D3D9Host(){if(device)device->Release();if(d3d)d3d->Release();if(window)DestroyWindow(window);}
};
struct Series {
 std::vector<double> v;void add(double x){v.push_back(x);}
 double q(double p)const{if(v.empty())return 0;auto s=v;std::sort(s.begin(),s.end());return s[std::min(s.size()-1,size_t(p*s.size()))];}
 double mean()const{return v.empty()?0:std::accumulate(v.begin(),v.end(),0.)/v.size();}
};
}
int wmain(int argc,wchar_t** argv){
 test::installCrashReport();
 struct Cleanup{~Cleanup(){shutdownCompute();shutdownJobs();}}cleanup;
 try{
  std::vector<std::wstring> args(argv+1,argv+argc);fs::path cache,pmx,jsonPath,dxvk;std::string asset,a="cpu_mt_v2",b="gpu_vulkan";int steps=600,iterations=10;
  for(size_t i=0;i<args.size();i++){auto& k=args[i];auto next=[&]{if(i+1>=args.size())throw std::runtime_error("Missing argument value");return args[++i];};
   if(k==L"--cache")cache=next();else if(k==L"--asset")asset=utf8(next());else if(k==L"--pmx")pmx=next();else if(k==L"--steps")steps=std::stoi(next());
   else if(k==L"--iterations")iterations=std::stoi(next());else if(k==L"--a")a=utf8(next());else if(k==L"--b")b=utf8(next());else if(k==L"--json")jsonPath=next();else if(k==L"--dxvk")dxvk=next();
   else if(k==L"--order"){auto order=utf8(next());if(order!="colored"&&order!="ordered")throw std::runtime_error("--order must be colored or ordered");setVulkanColoring(order=="colored");}
   else throw std::runtime_error("Unknown argument: "+utf8(k));}
  if(steps<=0||iterations<1||iterations>100)throw std::runtime_error("Invalid options");
  std::shared_ptr<Model> model;if(!pmx.empty())model=parse(readFile(pmx));else if(!cache.empty()&&!asset.empty())model=loadAsset(cache,asset);else throw std::runtime_error("Pass --pmx or --cache and --asset");
  // Declared before the worlds so it outlives them (the solver also holds its own reference).
  std::unique_ptr<D3D9Host> d3d9;if(!dxvk.empty())d3d9=std::make_unique<D3D9Host>(dxvk);
  Secondary::setSleepPolicy(false,.5f,.35f,1.5f);Secondary::setTuning(iterations,1,1,false,1);
  auto options=[&](const std::string& backend){return Json{{"backend","source"},{"presentationDriven",true},{"secondaryCollision",0},{"secondaryBroadphase","auto"},{"secondaryBackend",backend}};};
  World hostA,hostB;auto& pa=hostA.get(hostA.create(model,options(a)));auto& pb=hostB.get(hostB.create(model,options(b)));
  std::cout<<"model "<<model->name<<" bodies="<<model->bodies.size()<<" joints="<<model->joints.size()<<" iterations="<<iterations<<" a="<<pa.secondary->effectiveBackend<<" b="<<pb.secondary->effectiveBackend;
  if(!pb.secondary->backendFallback.empty())std::cout<<" (fallback: "<<pb.secondary->backendFallback<<")";
  {auto caps=vulkanCapabilities(false);std::cout<<" vulkan="<<caps.value("context",std::string("?"));
   if(auto occupancy=caps.find("occupancyPriority");occupancy!=caps.end()&&occupancy->is_number())std::cout<<" occupancy="<<occupancy->get<double>();}
  std::cout<<" ordering="<<(vulkanColoring()?"colored":"ordered")<<"\n";
  double height=0;for(int k=0;k<3;k++)height=std::max(height,double(model->maximum[k]-model->minimum[k]));
  // Rigid whole-body motion about the pelvis, as in mmdhl_profile.
  auto submit=[&](Instance& p,int frame){
   auto& rig=*p.sourceRig;double t=frame/60.;btVector3 pivot=rig.bones[0].rest.getOrigin();
   btVector3 offset(float(std::sin(t*.7)*24),float(std::sin(t*.9)*16),float(std::sin(t*1.1)*14));
   btQuaternion q;q.setEulerZYX(float(std::sin(t*.6)*18*SIMD_RADS_PER_DEG),float(std::sin(t)*12*SIMD_RADS_PER_DEG),float(std::sin(t*.8)*10*SIMD_RADS_PER_DEG));
   btTransform drive=btTransform(btQuaternion::getIdentity(),pivot+offset)*btTransform(q,btVector3(0,0,0))*btTransform(btQuaternion::getIdentity(),-pivot);
   std::vector<btTransform> palette;palette.reserve(rig.bones.size());for(auto& bone:rig.bones)palette.push_back(drive*bone.rest);
   p.submitPresentationPose(palette,t,uint64_t(frame)+1);p.secondary->waitAsyncIdle();p.stepSource();
  };
  Series stepA,stepB,worst;double sumSquares=0;size_t samples=0;int firstDiverged=-1;double maxPosition=0,maxRotation=0;Json perTick=Json::array();
  for(int f=0;f<steps;f++){
   submit(pa,f);submit(pb,f);
   stepA.add(pa.secondary->frameStats().physicsMs);stepB.add(pb.secondary->frameStats().physicsMs);
   auto wa=pa.secondary->dynamics(),wb=pb.secondary->dynamics();double tickMax=0,tickRot=0;
   int n=std::min(wa->getNumCollisionObjects(),wb->getNumCollisionObjects());
   for(int i=0;i<n;i++){
    auto oa=wa->getCollisionObjectArray()[i],ob=wb->getCollisionObjectArray()[i];if(oa->isStaticOrKinematicObject())continue;
    const auto& ta=oa->getWorldTransform();const auto& tb=ob->getWorldTransform();
    double d=(ta.getOrigin()-tb.getOrigin()).length();double r=ta.getRotation().angleShortestPath(tb.getRotation());
    if(!std::isfinite(d)||!std::isfinite(r))throw std::runtime_error("Non-finite body transform");
    tickMax=std::max(tickMax,d);tickRot=std::max(tickRot,r);sumSquares+=d*d;samples++;
   }
   if(firstDiverged<0&&(tickMax>0||tickRot>0))firstDiverged=f;
   maxPosition=std::max(maxPosition,tickMax);maxRotation=std::max(maxRotation,tickRot);worst.add(tickMax);
   if(f%60==59)perTick.push_back({{"tick",f+1},{"maxPosition",tickMax},{"maxRotation",tickRot}});
  }
  double rms=samples?std::sqrt(sumSquares/samples):0;
  auto compute=pb.secondary->diagnostics(false).value("compute",Json::object());
  Json result={{"model",model->name},{"bodies",model->bodies.size()},{"iterations",iterations},{"a",pa.secondary->effectiveBackend},{"b",pb.secondary->effectiveBackend},{"ordering",vulkanColoring()?"colored":"ordered"},
   {"modelExtent",height},{"maxPosition",maxPosition},{"maxRotation",maxRotation},{"rmsPosition",rms},{"p95TickMaxPosition",worst.q(.95)},{"firstDivergedTick",firstDiverged},
   {"stepMsA",{{"mean",stepA.mean()},{"p50",stepA.q(.5)},{"p95",stepA.q(.95)}}},{"stepMsB",{{"mean",stepB.mean()},{"p50",stepB.q(.5)},{"p95",stepB.q(.95)}}},{"computeB",compute},{"perSecond",perTick}};
  std::cout<<"divergence: max position "<<maxPosition<<" (model extent "<<height<<"), max rotation "<<maxRotation<<" rad, rms "<<rms<<", first diverged tick "<<firstDiverged<<"\n";
  std::cout<<"step ms "<<a<<": mean "<<stepA.mean()<<" p50 "<<stepA.q(.5)<<" p95 "<<stepA.q(.95)<<" | "<<b<<": mean "<<stepB.mean()<<" p50 "<<stepB.q(.5)<<" p95 "<<stepB.q(.95)<<"\n";
  std::cout<<"compute "<<compute.dump()<<"\n";
  if(!jsonPath.empty())std::ofstream(jsonPath)<<result.dump(2);
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
