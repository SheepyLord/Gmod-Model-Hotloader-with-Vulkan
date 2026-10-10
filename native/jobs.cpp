#include "jobs.hpp"
#ifdef _WIN32
#include <Windows.h>
#else
#include <ctime>
#include <sched.h>
#endif
#include <algorithm>
#include <atomic>
#include <immintrin.h>
#include <semaphore>
#include <chrono>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <LinearMath/btThreads.h>
namespace mmd { namespace {
unsigned defaultCount(){
 return automaticWorkerCount(availableThreadCount());
}
thread_local bool inlinePhysics=false;
// Bullet hands out thread indices in first-come order and reserves index 0 for
// the thread that installs its task scheduler; btResetThreadIndexCounter (used
// when the pool is rebuilt) makes the next claimant index 1. The owning thread
// therefore claims its index before any worker exists and before any reset,
// otherwise a later btSetTaskScheduler silently refuses and the multicore
// dispatcher dereferences a missing scheduler.
void claimOwnerThreadIndex(){(void)btGetCurrentThreadIndex();}
// One parallel-for. Participants claim chunk indices with an atomic counter, so
// handing out work never takes a lock; the last chunk to finish wakes the owner
// through the group's own counter instead of a pool-wide broadcast. Workers
// hold the group by shared_ptr, so a late claim or wake never touches freed
// memory after the owner has returned.
struct Group {
 const std::function<void(size_t,size_t)>* body=nullptr;size_t count=0,grain=1,chunks=0;
 std::atomic<size_t> next{0},done{0};std::exception_ptr error;std::mutex errorMutex;
 bool available()const{return next.load(std::memory_order_relaxed)<chunks;}
 bool complete()const{return done.load(std::memory_order_acquire)>=chunks;}
 void work(){
  for(;;){
   size_t i=next.fetch_add(1,std::memory_order_relaxed);if(i>=chunks)return;
   size_t begin=i*grain;
   try{(*body)(begin,std::min(count,begin+grain));}catch(...){std::lock_guard guard(errorMutex);if(!error)error=std::current_exception();}
   if(done.fetch_add(1,std::memory_order_acq_rel)+1==chunks)done.notify_all();
  }
 }
};
// Brief spin before sleeping: frame work arrives in bursts (pose evaluation,
// skinning, vertex fill), and a sleeping thread takes tens of microseconds to wake.
constexpr auto SpinBudget=std::chrono::microseconds(25);
struct Pool {
 std::mutex mutex;std::vector<std::shared_ptr<Group>> groups;std::deque<std::function<void()>> background;
 std::counting_semaphore<> signal{0};std::atomic<uint64_t> published{0};std::atomic<bool> stopping{false};
 std::vector<std::thread> threads;
 explicit Pool(unsigned n){claimOwnerThreadIndex();for(unsigned i=1;i<n;i++)threads.emplace_back([this]{loop();});}
 ~Pool(){stopping=true;published.fetch_add(1);signal.release(std::ptrdiff_t(threads.size()));for(auto& t:threads)t.join();
  // Background jobs left in the queue still own live world state; run them here
  // so their owners observe completion instead of an abandoned job.
  std::deque<std::function<void()>> remaining;{std::lock_guard lock(mutex);remaining.swap(background);}for(auto& job:remaining)job();}
 std::shared_ptr<Group> takeGroup(){
  std::lock_guard lock(mutex);
  for(auto it=groups.begin();it!=groups.end();){if((*it)->available())return *it;it=groups.erase(it);}
  return nullptr;
 }
 bool takeBackground(std::function<void()>& job){std::lock_guard lock(mutex);if(background.empty())return false;job=std::move(background.front());background.pop_front();return true;}
 // Spin until new work is published or the budget ends; true if something arrived.
 bool spin(uint64_t seen,const Group* waiting=nullptr){
  auto until=std::chrono::steady_clock::now()+SpinBudget;
  for(unsigned k=1;;k++){
   if(published.load(std::memory_order_acquire)!=seen||(waiting&&waiting->complete()))return true;
   _mm_pause();
   if(k%64==0&&std::chrono::steady_clock::now()>=until)return false;
  }
 }
 void loop(){
  for(;;){
   uint64_t seen=published.load(std::memory_order_acquire);
   if(auto group=takeGroup()){group->work();continue;}
   // Frame groups always win; background jobs (asynchronous physics ticks) run only when none are waiting.
   std::function<void()> job;if(takeBackground(job)){job();continue;}
   if(stopping.load())return;
   if(spin(seen))continue;
   signal.acquire();
  }
 }
 void run(size_t count,size_t grain,const std::function<void(size_t,size_t)>& f){
  if(!count)return;if(threads.empty()||count<=grain){f(0,count);return;}
  auto group=std::make_shared<Group>();group->body=&f;group->count=count;group->grain=grain;group->chunks=(count+grain-1)/grain;
  {std::lock_guard lock(mutex);groups.push_back(group);}
  published.fetch_add(1,std::memory_order_release);
  // The owner takes chunks too; wake only as many sleepers as there is other work.
  signal.release(std::ptrdiff_t(std::min<size_t>(group->chunks-1,threads.size())));
  group->work();
  // While chunks claimed by others finish, help with other frame groups (nested
  // skinning chunks); never with background jobs, whose length is unbounded.
  while(!group->complete()){
   uint64_t seen=published.load(std::memory_order_acquire);
   if(auto other=takeGroup()){other->work();continue;}
   if(spin(seen,group.get()))continue;
   size_t observed=group->done.load(std::memory_order_acquire);if(observed>=group->chunks)break;
   group->done.wait(observed,std::memory_order_acquire);
  }
  if(group->error)std::rethrow_exception(group->error);
 }
 void enqueue(std::function<void()> job){if(threads.empty()){job();return;}{std::lock_guard lock(mutex);background.push_back(std::move(job));}published.fetch_add(1,std::memory_order_release);signal.release(1);}
};
std::unique_ptr<Pool>& storage(){static std::unique_ptr<Pool> value;return value;}
std::atomic<unsigned> secondaryWorlds{1};
Pool& pool(){auto& value=storage();if(!value)value=std::make_unique<Pool>(defaultCount());return *value;}
class BulletScheduler final:public btITaskScheduler {
public:
 BulletScheduler():btITaskScheduler("Model Hotloader shared workers"){}
 int getMaxNumThreads()const override{return int(maximumWorkerCount());}
 int getNumThreads()const override{return int(workerCount());}
 void setNumThreads(int n)override{setWorkerCount(unsigned(std::max(1,n)));}
 void parallelFor(int first,int last,int grain,const btIParallelForBody& body)override{
  mmd::parallelFor(size_t(std::max(0,last-first)),size_t(std::max(1,grain)),[&](size_t a,size_t b){body.forLoop(first+int(a),first+int(b));});
 }
 btScalar parallelSum(int first,int last,int grain,const btIParallelSumBody& body)override{
  grain=std::max(1,grain);int count=std::max(0,last-first),chunks=(count+grain-1)/grain;
  std::vector<btScalar> values(chunks);
  mmd::parallelFor(chunks,1,[&](size_t a,size_t b){for(size_t i=a;i<b;i++)values[i]=body.sumLoop(first+int(i)*grain,std::min(last,first+(int(i)+1)*grain));});
  btScalar sum=0;for(auto v:values)sum+=v;return sum;
 }
};
}
void parallelFor(size_t count,size_t grain,const std::function<void(size_t,size_t)>& f){pool().run(count,std::max(size_t(1),grain),f);}
unsigned workerCount(){return unsigned(pool().threads.size()+1);}
unsigned maximumWorkerCount(){return std::min<unsigned>(BT_MAX_THREAD_COUNT-1,availableThreadCount());}
unsigned availableThreadCount(){
#ifdef _WIN32
 DWORD_PTR processMask=0,systemMask=0;
 if(GetProcessAffinityMask(GetCurrentProcess(),&processMask,&systemMask)&&processMask){
  unsigned count=0;while(processMask){count+=unsigned(processMask&1);processMask>>=1;}return count;
 }
#else
 // The CPUs this process may run on (taskset, cgroups, containers).
 cpu_set_t set;CPU_ZERO(&set);
 if(sched_getaffinity(0,sizeof(set),&set)==0){unsigned count=unsigned(CPU_COUNT(&set));if(count)return count;}
#endif
 return std::max(1u,unsigned(std::thread::hardware_concurrency()));
}
unsigned automaticWorkerCount(unsigned logicalThreads){return std::clamp(logicalThreads<=4?logicalThreads:logicalThreads-2,1u,maximumWorkerCount());}
void setWorkerCount(unsigned count){count=count?std::clamp(count,1u,maximumWorkerCount()):defaultCount();if(count!=workerCount()){claimOwnerThreadIndex();storage().reset();btResetThreadIndexCounter();storage()=std::make_unique<Pool>(count);}}
void shutdownJobs(){claimOwnerThreadIndex();storage().reset();btResetThreadIndexCounter();}
void initializeBulletScheduler(){
 static BulletScheduler scheduler;static bool installed=false;if(installed)return;
 (void)pool();btSetTaskScheduler(&scheduler);
 // Bullet silently refuses a scheduler unless the calling thread holds thread
 // index 0; a multicore world would later dereference the missing scheduler.
 if(btGetTaskScheduler()!=&scheduler)throw std::runtime_error("Bullet task scheduler was not installed (thread index "+std::to_string(btGetCurrentThreadIndex())+")");
 installed=true;
}
void setSecondaryWorkload(unsigned worlds){secondaryWorlds=std::max(1u,worlds);}
void parallelForSecondary(size_t count,size_t grain,const std::function<void(size_t,size_t)>& f){
 if(!count)return;
 unsigned lanes=inlinePhysics?1u:std::max(1u,workerCount()/secondaryWorlds.load());
 if(lanes==1){f(0,count);return;}
 parallelFor(count,std::max(grain,(count+lanes-1)/lanes),f);
}
void enqueueBackground(std::function<void()> job){pool().enqueue(std::move(job));}
#ifdef _WIN32
double threadCpuMs(){FILETIME creation,exit,kernel,user;if(!GetThreadTimes(GetCurrentThread(),&creation,&exit,&kernel,&user))return 0;auto ticks=[](const FILETIME& t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};return double(ticks(kernel)+ticks(user))*1e-4;}
#else
double threadCpuMs(){timespec t{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)!=0)return 0;return double(t.tv_sec)*1000.+double(t.tv_nsec)/1e6;}
#endif
InlinePhysicsScope::InlinePhysicsScope():previous(inlinePhysics){inlinePhysics=true;}
InlinePhysicsScope::~InlinePhysicsScope(){inlinePhysics=previous;}
}
