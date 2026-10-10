#define VK_NO_PROTOTYPES
#include "vulkan_solver.hpp"
#include <vulkan/vulkan.h>
#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
#include <cstdint>
#include "vulkan_solver_spv.h"
#include "vulkan_solver_profile_spv.h"
#include "dxvk_shared_compute.h"
#include <LinearMath/btScalar.h>
#include <LinearMath/btCpuFeatureUtility.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
namespace mmd {namespace {
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
void check(VkResult result,const char* operation){if(result!=VK_SUCCESS)throw std::runtime_error(std::string(operation)+" failed (Vulkan "+std::to_string(int(result))+")");}

#define MMDHL_VK_INSTANCE_FUNCTIONS(X) X(vkDestroyInstance) X(vkEnumerateDeviceExtensionProperties) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr)
#define MMDHL_VK_DEVICE_FUNCTIONS(X) X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkInvalidateMappedMemoryRanges) \
 X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateComputePipelines) X(vkDestroyPipeline) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
 X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDispatch) X(vkCmdPushConstants) X(vkCmdPipelineBarrier) X(vkCmdCopyBuffer) X(vkCmdResetQueryPool) X(vkCmdWriteTimestamp) \
 X(vkCreateQueryPool) X(vkDestroyQueryPool) X(vkGetQueryPoolResults) X(vkCreateFence) X(vkDestroyFence) X(vkResetFences) X(vkWaitForFences) X(vkQueueSubmit) X(vkQueueWaitIdle)
#define MMDHL_VK_STATISTICS_FUNCTIONS(X) X(vkGetPipelineExecutablePropertiesKHR) X(vkGetPipelineExecutableStatisticsKHR) X(vkGetPipelineExecutableInternalRepresentationsKHR)
struct Api {
#ifdef _WIN32
 HMODULE library=nullptr;
#else
 void* library=nullptr;
#endif
 PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr=nullptr;PFN_vkCreateInstance vkCreateInstance=nullptr;PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion=nullptr;
#define MMDHL_VK_DECLARE(name) PFN_##name name=nullptr;
 MMDHL_VK_INSTANCE_FUNCTIONS(MMDHL_VK_DECLARE) MMDHL_VK_DEVICE_FUNCTIONS(MMDHL_VK_DECLARE) MMDHL_VK_STATISTICS_FUNCTIONS(MMDHL_VK_DECLARE)
#undef MMDHL_VK_DECLARE
};
// Staged arrays, in the order of the shader bindings 0-5 (6 is the profiling clock buffer).
constexpr int Arrays=6;
// Asynchronous worlds tick concurrently, each holding a slot while it waits.
constexpr int Slots=16;
constexpr uint32_t MaxQueues=8;
struct Allocation {VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;VkDeviceSize capacity=0;void* mapped=nullptr;bool coherent=true;};
// One in-flight solve: its own command buffer, fence, descriptors and buffers.
// Concurrent worlds take different slots and, where possible, different queues.
struct Slot {
 VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer commands=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;VkQueryPool queries=VK_NULL_HANDLE;VkDescriptorSet set=VK_NULL_HANDLE;
 std::array<Allocation,Arrays> work;Allocation staging,readback,clocks;bool busy=false,dirty=true,lost=false;size_t queue=0;
};
struct Device {
 std::mutex mutex;std::condition_variable freed;Api api;
 VkInstance instance=VK_NULL_HANDLE;VkPhysicalDevice physical=VK_NULL_HANDLE;VkDevice device=VK_NULL_HANDLE;
 std::vector<VkQueue> queues;std::vector<std::unique_ptr<std::mutex>> queueLocks;uint32_t family=0;bool asyncCompute=false;
 VkPhysicalDeviceProperties properties{};VkPhysicalDeviceMemoryProperties memory{};
 VkShaderModule shader=VK_NULL_HANDLE;VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;std::array<VkPipeline,2> pipelines{};VkDescriptorPool descriptors=VK_NULL_HANDLE;
 std::vector<std::unique_ptr<Slot>> slots;
 bool initialized=false,available=false,timestamps=false;std::string name,driver,error,version;uint32_t workgroup=256,sharedBodies=1024,sharedBytes=0;bool fusedRows=false,profileClock=false,statistics=false;
 // Shared mode: the renderer's own VkDevice (patched DXVK), see attachShared().
 bool shared=false;DXVK_SHARED_COMPUTE_QUEUE sharedQueue{};PFN_DXVK_SharedComputeQueueOwner sharedLock=nullptr,sharedUnlock=nullptr,sharedRelease=nullptr;
 // On the shared device the solver's dispatches ask for a low SM occupancy
 // priority where DXVK enabled VK_NV_compute_occupancy_priority
 // (MMDHL_VULKAN_OCCUPANCY 0-1 or "off", MMDHL_VULKAN_THROTTLING 0-1). No
 // measurable effect on frame times on an RTX 5090 (driver 610.88).
 PFN_vkCmdSetComputeOccupancyPriorityNV setOccupancy=nullptr;float occupancyPriority=VK_COMPUTE_OCCUPANCY_PRIORITY_LOW_NV,occupancyThrottling=0.f;
 // Iterations per dispatch (MMDHL_VULKAN_CHUNK); 0 solves in one dispatch.
 // On the renderer's device a multi-millisecond dispatch holds up the game's
 // frames (p95 +10 ms with 8 characters at 100 iterations); dispatches of 3
 // iterations keep that under 1 ms at about 13 % more kernel time.
 int chunkIterations=0;nlohmann::json pipelineStatistics=nlohmann::json::array();double timestampPeriod=0;unsigned growths=0;
 // A timed-out or lost device may still write host-visible memory. Its slot
 // and every Vulkan object stay alive until process exit, never reused.
 bool quarantined=false;
 ~Device(){shutdown();}

 template<class T> T instanceFunction(const char* name){auto f=reinterpret_cast<T>(api.vkGetInstanceProcAddr(instance,name));if(!f)throw std::runtime_error(std::string("Vulkan function missing: ")+name);return f;}
 template<class T> T deviceFunction(const char* name){auto f=reinterpret_cast<T>(api.vkGetDeviceProcAddr(device,name));if(!f)throw std::runtime_error(std::string("Vulkan function missing: ")+name);return f;}
 uint32_t findMemory(uint32_t bits,VkMemoryPropertyFlags required,VkMemoryPropertyFlags preferred){
  for(auto flags:{required|preferred,required})for(uint32_t i=0;i<memory.memoryTypeCount;i++)if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags)return i;
  throw std::runtime_error("No suitable Vulkan memory type");
 }
 void release(Allocation& a){
  if(a.buffer)api.vkDestroyBuffer(device,a.buffer,nullptr);if(a.memory)api.vkFreeMemory(device,a.memory,nullptr);a=Allocation{};
 }
 // Grows by half again; the slot is idle (its fence has signalled) when this runs.
 bool ensure(Allocation& a,VkDeviceSize bytes,VkBufferUsageFlags usage,VkMemoryPropertyFlags required,VkMemoryPropertyFlags preferred,bool map){
  bytes=std::max<VkDeviceSize>(256,(bytes+255)&~VkDeviceSize(255));if(bytes<=a.capacity)return false;
  VkDeviceSize grown=std::max(bytes,a.capacity+a.capacity/2);release(a);
  VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=grown;info.usage=usage;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
  check(api.vkCreateBuffer(device,&info,nullptr,&a.buffer),"Create solver buffer");
  VkMemoryRequirements need{};api.vkGetBufferMemoryRequirements(device,a.buffer,&need);
  VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.allocationSize=need.size;allocate.memoryTypeIndex=findMemory(need.memoryTypeBits,required,preferred);
  a.coherent=(memory.memoryTypes[allocate.memoryTypeIndex].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
  check(api.vkAllocateMemory(device,&allocate,nullptr,&a.memory),"Allocate solver memory");
  check(api.vkBindBufferMemory(device,a.buffer,a.memory,0),"Bind solver memory");
  if(map)check(api.vkMapMemory(device,a.memory,0,VK_WHOLE_SIZE,0,&a.mapped),"Map solver memory");
  a.capacity=grown;growths++;return true;
 }

 void loadLibrary(){
  if(api.library)return;
  // The game ships an application-local loader beside gmod.exe; either loader works.
#ifdef _WIN32
  api.library=LoadLibraryW(L"vulkan-1.dll");if(!api.library)throw std::runtime_error("The Vulkan runtime (vulkan-1.dll) is not installed");
  api.vkGetInstanceProcAddr=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(api.library,"vkGetInstanceProcAddr"));
#else
  // The system's (or the Steam runtime's) loader; Linux has no application-local copy.
  api.library=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);if(!api.library)throw std::runtime_error("The Vulkan runtime (libvulkan.so.1) is not installed");
  api.vkGetInstanceProcAddr=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(api.library,"vkGetInstanceProcAddr"));
#endif
  if(!api.vkGetInstanceProcAddr)throw std::runtime_error("vulkan-1.dll exports no vkGetInstanceProcAddr");
  api.vkCreateInstance=reinterpret_cast<PFN_vkCreateInstance>(api.vkGetInstanceProcAddr(nullptr,"vkCreateInstance"));
  api.vkEnumerateInstanceVersion=reinterpret_cast<PFN_vkEnumerateInstanceVersion>(api.vkGetInstanceProcAddr(nullptr,"vkEnumerateInstanceVersion"));
  if(!api.vkCreateInstance)throw std::runtime_error("vulkan-1.dll exports no vkCreateInstance");
 }
 void createInstance(){
  uint32_t loader=VK_API_VERSION_1_0;if(api.vkEnumerateInstanceVersion)api.vkEnumerateInstanceVersion(&loader);
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Model Hotloader";app.pEngineName="Model Hotloader physics";app.apiVersion=loader>=VK_API_VERSION_1_1?VK_API_VERSION_1_1:VK_API_VERSION_1_0;
  VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;
  check(api.vkCreateInstance(&info,nullptr,&instance),"Create Vulkan instance");
#define MMDHL_VK_LOAD(name) api.name=instanceFunction<PFN_##name>(#name);
  MMDHL_VK_INSTANCE_FUNCTIONS(MMDHL_VK_LOAD)
#undef MMDHL_VK_LOAD
 }
 // Discrete GPUs first, then the largest device-local heap. MMDHL_VULKAN_DEVICE
 // selects an index for benchmarking.
 void choosePhysical(){
  uint32_t count=0;check(api.vkEnumeratePhysicalDevices(instance,&count,nullptr),"Enumerate Vulkan devices");if(!count)throw std::runtime_error("No Vulkan device is available");
  std::vector<VkPhysicalDevice> devices(count);check(api.vkEnumeratePhysicalDevices(instance,&count,devices.data()),"Read Vulkan devices");
  long long best=-1;int forced=-1;if(auto setting=std::getenv("MMDHL_VULKAN_DEVICE"))forced=std::atoi(setting);
  for(uint32_t i=0;i<count;i++){
   VkPhysicalDeviceProperties p{};api.vkGetPhysicalDeviceProperties(devices[i],&p);VkPhysicalDeviceMemoryProperties m{};api.vkGetPhysicalDeviceMemoryProperties(devices[i],&m);
   VkDeviceSize local=0;for(uint32_t h=0;h<m.memoryHeapCount;h++)if(m.memoryHeaps[h].flags&VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)local=std::max(local,m.memoryHeaps[h].size);
   long long score=(p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?1ll<<50:p.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU?1ll<<49:0)+(long long)(local>>20);
   if(forced>=0)score=int(i)==forced?1:-1;
   if(score>best){best=score;physical=devices[i];}
  }
  if(!physical)throw std::runtime_error("The requested Vulkan device does not exist");
  describePhysical();
 }
 void describePhysical(){
  api.vkGetPhysicalDeviceProperties(physical,&properties);api.vkGetPhysicalDeviceMemoryProperties(physical,&memory);name=properties.deviceName;
  uint32_t v=properties.driverVersion;
  // NVIDIA packs 10.8.8.6 bits; others use the Vulkan version encoding.
  driver=properties.vendorID==0x10de?std::to_string(v>>22)+"."+std::to_string((v>>14)&0xff):std::to_string(VK_API_VERSION_MAJOR(v))+"."+std::to_string(VK_API_VERSION_MINOR(v))+"."+std::to_string(VK_API_VERSION_PATCH(v));
  version=std::to_string(VK_API_VERSION_MAJOR(properties.apiVersion))+"."+std::to_string(VK_API_VERSION_MINOR(properties.apiVersion));
 }
 // A compute-only family runs beside the game's graphics queue (async compute).
 void createDevice(){
  uint32_t count=0;api.vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);api.vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
  int chosen=-1;
  for(uint32_t i=0;i<count&&chosen<0;i++)if((families[i].queueFlags&VK_QUEUE_COMPUTE_BIT)&&!(families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT))chosen=int(i);
  asyncCompute=chosen>=0;
  for(uint32_t i=0;i<count&&chosen<0;i++)if(families[i].queueFlags&VK_QUEUE_COMPUTE_BIT)chosen=int(i);
  if(chosen<0)throw std::runtime_error("The Vulkan device has no compute queue");
  family=uint32_t(chosen);uint32_t queueCount=std::min<uint32_t>(families[family].queueCount,MaxQueues);
  timestamps=families[family].timestampValidBits>0&&properties.limits.timestampPeriod>0;timestampPeriod=properties.limits.timestampPeriod;
  std::vector<float> priorities(queueCount,1.f);
  VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueFamilyIndex=family;queue.queueCount=queueCount;queue.pQueuePriorities=priorities.data();
  VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};info.queueCreateInfoCount=1;info.pQueueCreateInfos=&queue;
  // Developer profiling: per-level SM clock cycles (VK_KHR_shader_clock).
  VkPhysicalDeviceShaderClockFeaturesKHR clock{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR};const char* clockExtension=VK_KHR_SHADER_CLOCK_EXTENSION_NAME;
  if(std::getenv("MMDHL_VULKAN_CLOCK")){
   uint32_t n=0;api.vkEnumerateDeviceExtensionProperties(physical,nullptr,&n,nullptr);std::vector<VkExtensionProperties> extensions(n);api.vkEnumerateDeviceExtensionProperties(physical,nullptr,&n,extensions.data());
   for(auto& e:extensions)if(std::strcmp(e.extensionName,clockExtension)==0)profileClock=true;
   if(profileClock){clock.shaderSubgroupClock=VK_TRUE;info.pNext=&clock;info.enabledExtensionCount=1;info.ppEnabledExtensionNames=&clockExtension;}
  }
  // Developer profiling: the driver's compile statistics (registers, spills).
  VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR executables{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};std::vector<const char*> enabled;
  if(profileClock)enabled.push_back(clockExtension);
  if(std::getenv("MMDHL_VULKAN_CLOCK")){
   uint32_t n=0;api.vkEnumerateDeviceExtensionProperties(physical,nullptr,&n,nullptr);std::vector<VkExtensionProperties> extensions(n);api.vkEnumerateDeviceExtensionProperties(physical,nullptr,&n,extensions.data());
   for(auto& e:extensions)if(std::strcmp(e.extensionName,VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME)==0)statistics=true;
   if(statistics){executables.pipelineExecutableInfo=VK_TRUE;executables.pNext=const_cast<void*>(info.pNext);info.pNext=&executables;enabled.push_back(VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME);}
  }
  info.enabledExtensionCount=uint32_t(enabled.size());info.ppEnabledExtensionNames=enabled.data();
  check(api.vkCreateDevice(physical,&info,nullptr,&device),"Create Vulkan device");
#define MMDHL_VK_LOAD(name) api.name=deviceFunction<PFN_##name>(#name);
  MMDHL_VK_DEVICE_FUNCTIONS(MMDHL_VK_LOAD)
  if(statistics){MMDHL_VK_STATISTICS_FUNCTIONS(MMDHL_VK_LOAD)}
#undef MMDHL_VK_LOAD
  for(uint32_t i=0;i<queueCount;i++){VkQueue q=VK_NULL_HANDLE;api.vkGetDeviceQueue(device,family,i,&q);queues.push_back(q);queueLocks.push_back(std::make_unique<std::mutex>());}
 }
 void createPipeline(){
  // Developer benchmark override; this never changes simulation settings.
  if(auto setting=std::getenv("MMDHL_VULKAN_WORKGROUP")){int n=std::atoi(setting);if(n==32||n==64||n==128||n==256||n==512||n==1024)workgroup=uint32_t(n);}
  workgroup=std::min({workgroup,properties.limits.maxComputeWorkGroupSize[0],properties.limits.maxComputeWorkGroupInvocations});
  sharedBytes=std::min<uint32_t>(properties.limits.maxComputeSharedMemorySize,65536);sharedBodies=sharedBytes/36;
  VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};module.codeSize=profileClock?sizeof(mmdhl_vulkan_solver_profile_spv):sizeof(mmdhl_vulkan_solver_spv);module.pCode=profileClock?mmdhl_vulkan_solver_profile_spv:mmdhl_vulkan_solver_spv;
  check(api.vkCreateShaderModule(device,&module,nullptr,&shader),"Create solver shader");
  // Binding Arrays (6) is the profiling clock buffer.
  std::array<VkDescriptorSetLayoutBinding,Arrays+1> bindings{};
  for(uint32_t i=0;i<bindings.size();i++){bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
  VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};setInfo.bindingCount=uint32_t(bindings.size());setInfo.pBindings=bindings.data();
  check(api.vkCreateDescriptorSetLayout(device,&setInfo,nullptr,&setLayout),"Create solver descriptor layout");
  chunkIterations=shared?3:0;if(auto setting=std::getenv("MMDHL_VULKAN_CHUNK");setting&&*setting)chunkIterations=std::max(0,std::atoi(setting));
  // Push constants: the dispatch's iteration range (Chunk in the kernel).
  VkPushConstantRange chunkRange{VK_SHADER_STAGE_COMPUTE_BIT,0,2*sizeof(int32_t)};
  VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layoutInfo.setLayoutCount=1;layoutInfo.pSetLayouts=&setLayout;layoutInfo.pushConstantRangeCount=1;layoutInfo.pPushConstantRanges=&chunkRange;
  check(api.vkCreatePipelineLayout(device,&layoutInfo,nullptr,&layout),"Create solver pipeline layout");
  // Bullet picks its row solver at run time: FMA3 + SSE4.1 rows when the CPU
  // has both, SSE2 rows otherwise. The kernel follows the same choice.
  fusedRows=false;
#ifdef BT_ALLOW_SSE4
  {int features=btCpuFeatureUtility::getCpuFeatures();fusedRows=(features&btCpuFeatureUtility::CPU_FEATURE_FMA3)&&(features&btCpuFeatureUtility::CPU_FEATURE_SSE4_1);}
#endif
  uint32_t debugMode=0;if(auto setting=std::getenv("MMDHL_VULKAN_DEBUG_MODE"))debugMode=uint32_t(std::atoi(setting));
  // Two variants: [0] also handles islands too large for shared memory,
  // [1] is the faster shared-memory-only kernel, used when every island fits.
  for(uint32_t globalPath:{1u,0u}){
   std::array<uint32_t,5> constants={workgroup,sharedBytes/4,fusedRows?1u:0u,debugMode,globalPath};std::array<VkSpecializationMapEntry,5> entries={{{0,0,4},{1,4,4},{2,8,4},{3,12,4},{4,16,4}}};
   VkSpecializationInfo specialization{uint32_t(entries.size()),entries.data(),sizeof(constants),constants.data()};
   VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pipelineInfo.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pipelineInfo.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipelineInfo.stage.module=shader;pipelineInfo.stage.pName="main";pipelineInfo.stage.pSpecializationInfo=&specialization;pipelineInfo.layout=layout;
   if(statistics)pipelineInfo.flags|=VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR|VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR;
   check(api.vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pipelineInfo,nullptr,&pipelines[globalPath?0:1]),"Create solver pipeline");
   if(statistics){
    VkPipelineInfoKHR which{VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR};which.pipeline=pipelines[globalPath?0:1];uint32_t count=0;api.vkGetPipelineExecutablePropertiesKHR(device,&which,&count,nullptr);
    for(uint32_t e=0;e<count;e++){VkPipelineExecutableInfoKHR executable{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};executable.pipeline=which.pipeline;executable.executableIndex=e;uint32_t n=0;api.vkGetPipelineExecutableStatisticsKHR(device,&executable,&n,nullptr);
     std::vector<VkPipelineExecutableStatisticKHR> stats(n,{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR});api.vkGetPipelineExecutableStatisticsKHR(device,&executable,&n,stats.data());
     nlohmann::json entry={{"variant",globalPath?"shared+global":"shared"}};
     for(auto& st:stats){nlohmann::json v;switch(st.format){case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:v=bool(st.value.b32);break;case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:v=st.value.i64;break;case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:v=st.value.u64;break;default:v=st.value.f64;}entry[st.name]=v;}
     pipelineStatistics.push_back(entry);
     // MMDHL_VULKAN_IR=<directory>: the driver's internal representations of each variant.
     if(auto directory=std::getenv("MMDHL_VULKAN_IR")){
      uint32_t count=0;api.vkGetPipelineExecutableInternalRepresentationsKHR(device,&executable,&count,nullptr);
      std::vector<VkPipelineExecutableInternalRepresentationKHR> irs(count,{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR});api.vkGetPipelineExecutableInternalRepresentationsKHR(device,&executable,&count,irs.data());
      std::vector<std::vector<char>> storage(count);for(uint32_t i=0;i<count;i++){storage[i].resize(irs[i].dataSize);irs[i].pData=storage[i].data();}
      api.vkGetPipelineExecutableInternalRepresentationsKHR(device,&executable,&count,irs.data());
      for(uint32_t i=0;i<count;i++){std::string path=std::string(directory)+"/"+(globalPath?"global":"shared")+"-"+std::to_string(i)+"-"+irs[i].name+(irs[i].isText?".txt":".bin");for(auto& ch:path)if(ch==' '||ch==':')ch='_';
       if(FILE* f=std::fopen(path.c_str(),"wb")){std::fwrite(storage[i].data(),1,irs[i].isText&&storage[i].size()?storage[i].size()-1:storage[i].size(),f);std::fclose(f);}}
     }}
   }
  }
  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,uint32_t((Arrays+1)*Slots)};
  VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};poolInfo.maxSets=Slots;poolInfo.poolSizeCount=1;poolInfo.pPoolSizes=&size;
  check(api.vkCreateDescriptorPool(device,&poolInfo,nullptr,&descriptors),"Create solver descriptor pool");
  for(int i=0;i<Slots;i++){
   auto slot=std::make_unique<Slot>();slot->queue=size_t(i)%queues.size();
   VkCommandPoolCreateInfo poolCreate{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};poolCreate.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;poolCreate.queueFamilyIndex=family;
   check(api.vkCreateCommandPool(device,&poolCreate,nullptr,&slot->pool),"Create solver command pool");
   VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};commandInfo.commandPool=slot->pool;commandInfo.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commandInfo.commandBufferCount=1;
   check(api.vkAllocateCommandBuffers(device,&commandInfo,&slot->commands),"Allocate solver commands");
   VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(api.vkCreateFence(device,&fenceInfo,nullptr,&slot->fence),"Create solver fence");
   if(timestamps){VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};queryInfo.queryType=VK_QUERY_TYPE_TIMESTAMP;queryInfo.queryCount=2;check(api.vkCreateQueryPool(device,&queryInfo,nullptr,&slot->queries),"Create solver timestamps");}
   VkDescriptorSetAllocateInfo setAllocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};setAllocate.descriptorPool=descriptors;setAllocate.descriptorSetCount=1;setAllocate.pSetLayouts=&setLayout;
   check(api.vkAllocateDescriptorSets(device,&setAllocate,&slot->set),"Allocate solver descriptors");
   slots.push_back(std::move(slot));
  }
 }
 // Inside a game rendering through the patched DXVK (patches/dxvk), the
 // solver takes queues of a dedicated compute family on the renderer's own
 // VkDevice, which DXVK creates and never uses. Physics then overlaps
 // rendering inside one GPU context instead of being time-sliced against a
 // second device. MMDHL_VULKAN_STANDALONE=1 keeps a separate device.
 bool attachShared(){
  if(std::getenv("MMDHL_VULKAN_STANDALONE"))return false;
#ifndef _WIN32
  // Linux games render through OpenGL (ToGL): there is no DXVK device to share.
  return false;
#else
  HMODULE d3d9=GetModuleHandleW(L"d3d9.dll");if(!d3d9)return false;
  auto acquireShared=reinterpret_cast<PFN_DXVK_AcquireSharedComputeQueue>(GetProcAddress(d3d9,"DXVK_AcquireSharedComputeQueue"));
  sharedLock=reinterpret_cast<PFN_DXVK_SharedComputeQueueOwner>(GetProcAddress(d3d9,"DXVK_LockSharedComputeQueue"));
  sharedUnlock=reinterpret_cast<PFN_DXVK_SharedComputeQueueOwner>(GetProcAddress(d3d9,"DXVK_UnlockSharedComputeQueue"));
  sharedRelease=reinterpret_cast<PFN_DXVK_SharedComputeQueueOwner>(GetProcAddress(d3d9,"DXVK_ReleaseSharedComputeQueue"));
  if(!acquireShared||!sharedLock||!sharedUnlock||!sharedRelease)return false;
  DXVK_SHARED_COMPUTE_QUEUE q{};q.Version=DXVK_SHARED_COMPUTE_QUEUE_VERSION;
  if(FAILED(acquireShared(&q))||!q.Device||!q.GetInstanceProcAddr||!q.QueueCount)return false;
  sharedQueue=q;shared=true;
  api.vkGetInstanceProcAddr=q.GetInstanceProcAddr;instance=q.Instance;
#define MMDHL_VK_LOAD(name) api.name=instanceFunction<PFN_##name>(#name);
  MMDHL_VK_INSTANCE_FUNCTIONS(MMDHL_VK_LOAD)
#undef MMDHL_VK_LOAD
  physical=q.PhysicalDevice;describePhysical();device=q.Device;
#define MMDHL_VK_LOAD(name) api.name=deviceFunction<PFN_##name>(#name);
  MMDHL_VK_DEVICE_FUNCTIONS(MMDHL_VK_LOAD)
#undef MMDHL_VK_LOAD
  uint32_t count=0;api.vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);api.vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
  if(q.QueueFamilyIndex>=count)throw std::runtime_error("DXVK reported an invalid shared compute family");
  family=q.QueueFamilyIndex;asyncCompute=!(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT);
  timestamps=families[family].timestampValidBits>0&&properties.limits.timestampPeriod>0;timestampPeriod=properties.limits.timestampPeriod;
  for(uint32_t i=0;i<std::min<uint32_t>(q.QueueCount,MaxQueues);i++){VkQueue queue=VK_NULL_HANDLE;api.vkGetDeviceQueue(device,family,q.FirstQueueIndex+i,&queue);queues.push_back(queue);queueLocks.push_back(std::make_unique<std::mutex>());}
  // Null unless DXVK enabled the extension on its device.
  setOccupancy=reinterpret_cast<PFN_vkCmdSetComputeOccupancyPriorityNV>(api.vkGetDeviceProcAddr(device,"vkCmdSetComputeOccupancyPriorityNV"));
  if(auto setting=std::getenv("MMDHL_VULKAN_OCCUPANCY");setting&&*setting){if(std::string(setting)=="off")setOccupancy=nullptr;else occupancyPriority=std::clamp(float(std::atof(setting)),0.f,1.f);}
  if(auto setting=std::getenv("MMDHL_VULKAN_THROTTLING");setting&&*setting)occupancyThrottling=std::clamp(float(std::atof(setting)),0.f,1.f);
  return true;
#endif
 }
 void init(){if(initialized)return;initialized=true;try{
#ifdef _WIN32
  if(GetEnvironmentVariableW(L"MMDHL_DISABLE_VULKAN",nullptr,0))throw std::runtime_error("Vulkan disabled by MMDHL_DISABLE_VULKAN");
#else
  if(std::getenv("MMDHL_DISABLE_VULKAN"))throw std::runtime_error("Vulkan disabled by MMDHL_DISABLE_VULKAN");
#endif
  if(!attachShared()){loadLibrary();createInstance();choosePhysical();createDevice();}
  createPipeline();available=true;if(!std::getenv("MMDHL_VULKAN_DEBUG_MODE"))selfTest();
 }catch(const std::exception& e){error=e.what();available=false;}}
 // Queue access is externally synchronized: our per-queue lock, and on the
 // shared device DXVK's lock, which excludes its vkDeviceWaitIdle.
 VkResult queueCall(size_t index,const std::function<VkResult(VkQueue)>& call){
  std::lock_guard lock(*queueLocks[index]);if(shared)sharedLock(sharedQueue.Owner);
  VkResult result=call(queues[index]);
  if(shared)sharedUnlock(sharedQueue.Owner);
  return result;
 }
 void shutdown(){
  // A quarantined shared device keeps DXVK's reference: the GPU may still own its memory.
  if(quarantined||!device){if(instance&&!quarantined&&!shared){api.vkDestroyInstance(instance,nullptr);instance=VK_NULL_HANDLE;}initialized=available=false;return;}
  // The shared device belongs to DXVK: wait on our own queues only.
  if(shared)for(size_t i=0;i<queues.size();i++)queueCall(i,[&](VkQueue queue){return api.vkQueueWaitIdle(queue);});
  else api.vkDeviceWaitIdle(device);
  for(auto& slot:slots){for(auto& a:slot->work)release(a);release(slot->staging);release(slot->readback);release(slot->clocks);if(slot->queries)api.vkDestroyQueryPool(device,slot->queries,nullptr);if(slot->fence)api.vkDestroyFence(device,slot->fence,nullptr);if(slot->pool)api.vkDestroyCommandPool(device,slot->pool,nullptr);}
  slots.clear();
  if(descriptors)api.vkDestroyDescriptorPool(device,descriptors,nullptr);for(auto& p:pipelines)if(p){api.vkDestroyPipeline(device,p,nullptr);p=VK_NULL_HANDLE;}if(layout)api.vkDestroyPipelineLayout(device,layout,nullptr);if(setLayout)api.vkDestroyDescriptorSetLayout(device,setLayout,nullptr);if(shader)api.vkDestroyShaderModule(device,shader,nullptr);
  descriptors=VK_NULL_HANDLE;layout=VK_NULL_HANDLE;setLayout=VK_NULL_HANDLE;shader=VK_NULL_HANDLE;
  if(shared){sharedRelease(sharedQueue.Owner);sharedQueue={};shared=false;setOccupancy=nullptr;instance=VK_NULL_HANDLE;}
  else api.vkDestroyDevice(device,nullptr);
  device=VK_NULL_HANDLE;queues.clear();queueLocks.clear();
  if(instance){api.vkDestroyInstance(instance,nullptr);instance=VK_NULL_HANDLE;}
  initialized=available=false;
 }
 Slot* acquire(){
  std::unique_lock lock(mutex);init();if(!available)throw std::runtime_error(error);
  for(;;){
   for(auto& slot:slots)if(!slot->busy&&!slot->lost){slot->busy=true;return slot.get();}
   if(freed.wait_for(lock,std::chrono::seconds(5))==std::cv_status::timeout)throw std::runtime_error("Every Vulkan solver slot stayed busy for 5 seconds");
   if(!available)throw std::runtime_error(error);
  }
 }
 void releaseSlot(Slot* slot){{std::lock_guard lock(mutex);slot->busy=false;}freed.notify_one();}
 // ---- The kernel's batch: compact rows in level order ----
 // Row: normal (w rhs), relA (w cfm), relB (w jacDiagABInv), angularA (w lower
 // limit), angularB (w upper limit), extra (friction, rhsPenetration, override
 // iterations), ids (body A, body B, the friction row's normal row or -1,
 // flags). Flags: bits 0-1 the pool (joint, contact, friction, rolling
 // friction); 4: the B normal is the negated A normal, otherwise equal to it
 // (Bullet's rows are always one or the other); 8/16: body A/B writable.
 struct VRow {F4 normal,relA,relB,angularA,angularB,extra;I4 ids;};
 struct VBody {F4 linear,angular,push,turn,invMass;};
 static_assert(sizeof(VRow)==112&&sizeof(VBody)==80);
 // Level: first, end, kind, pool. Kind 0: groups [first, end), one thread per
 // group with all of its rows fetched at once; kind 1: a serial run of rows
 // [first, end) on one subgroup, one row per lane. Rows are stored in solve
 // order, so a level's (and a run's) rows are contiguous.
 struct Batch {std::vector<VBody> bodies;std::vector<VRow> rows;std::vector<F2> impulses;std::vector<I2> groups;std::vector<I4> levels;std::vector<GIsland> islands;std::vector<int> order;int serialLevels=0,serialRows=0,mergedLevels=0;};
 static constexpr int GroupRows=6; // the kernel's per-thread row registers
 static bool sameBits(float a,float b){return std::memcmp(&a,&b,sizeof(float))==0;}
 // Reorders rows into level order (groups of one level share no writable
 // body, so this is Bullet's sweep up to independent swaps) and merges runs of
 // consecutive tiny levels into serial runs, which cost one level instead of many.
 static void convert(const Buffers& in,Batch& out){
  out=Batch{};out.bodies.reserve(in.bodies.size());for(auto& b:in.bodies)out.bodies.push_back({b.linear,b.angular,b.push,b.turn,b.invMass});
  out.rows.reserve(in.rows.size());out.impulses.reserve(in.rows.size());out.order.reserve(in.rows.size());
  std::vector<int> renamed(in.rows.size(),-1);
  auto emit=[&](int old){
   const GRow& r=in.rows[old];renamed[old]=int(out.rows.size());out.order.push_back(old);out.impulses.push_back(in.impulses[old]);
   bool negated=sameBits(r.normalB.x,-r.normalA.x)&&sameBits(r.normalB.y,-r.normalA.y)&&sameBits(r.normalB.z,-r.normalA.z);
   bool equal=sameBits(r.normalB.x,r.normalA.x)&&sameBits(r.normalB.y,r.normalA.y)&&sameBits(r.normalB.z,r.normalA.z);
   if(!negated&&!equal)throw std::runtime_error("Vulkan solver: a row's B normal is neither its A normal nor the negation");
   int friction=-1;if(r.ids.z>=0){friction=renamed[r.ids.z];if(friction<0)throw std::runtime_error("Vulkan solver: a friction row precedes its contact row");}
   int flags=(r.ids.w&3)|(equal?0:4)|(in.bodies[r.ids.x].invMass.w!=0?8:0)|(in.bodies[r.ids.y].invMass.w!=0?16:0);
   out.rows.push_back({{r.normalA.x,r.normalA.y,r.normalA.z,r.params.x},{r.relA.x,r.relA.y,r.relA.z,r.params.y},{r.relB.x,r.relB.y,r.relB.z,r.params.z},
    {r.angularA.x,r.angularA.y,r.angularA.z,r.limits.x},{r.angularB.x,r.angularB.y,r.angularB.z,r.limits.y},{r.params.w,r.limits.z,r.limits.w,0},{r.ids.x,r.ids.y,friction,flags}});
  };
  auto rowsOf=[&](int level){int n=0;for(int g=in.levels[level].x;g<in.levels[level].y;g++)n+=in.groups[g].y-in.groups[g].x;return n;};
  auto longest=[&](int level){int n=0;for(int g=in.levels[level].x;g<in.levels[level].y;g++)n=std::max(n,in.groups[g].y-in.groups[g].x);return n;};
  auto tiny=[&](int level){return in.levels[level].y-in.levels[level].x<=2&&rowsOf(level)<=2*GroupRows;};
  auto serial=[&](int l0,int l1,int pool){
   int first=int(out.rows.size());for(int k=l0;k<l1;k++)for(int g=in.levels[k].x;g<in.levels[k].y;g++)for(int row=in.groups[g].x;row<in.groups[g].y;row++)emit(row);
   out.levels.push_back({first,int(out.rows.size()),1,pool});out.serialLevels++;out.serialRows+=int(out.rows.size())-first;out.mergedLevels+=l1-l0;
  };
  auto pool=[&](int l0,int l1,int kind){
   for(int l=l0;l<l1;){
    int m=l;while(m<l1&&tiny(m))m++;
    if(m-l>=2){serial(l,m,kind);l=m;continue;}
    // A group longer than the kernel's row registers runs its level serially.
    if(longest(l)>GroupRows){serial(l,l+1,kind);l++;continue;}
    // The kernel holds a group's body pair in registers: every row of a group
    // must act on the same two bodies (one constraint, or one contact row).
    bool samePair=true;
    for(int g=in.levels[l].x;g<in.levels[l].y;g++)for(int row=in.groups[g].x;row<in.groups[g].y;row++)samePair&=in.rows[row].ids.x==in.rows[in.groups[g].x].ids.x&&in.rows[row].ids.y==in.rows[in.groups[g].x].ids.y;
    if(!samePair){serial(l,l+1,kind);l++;continue;}
    int first=int(out.groups.size());
    for(int g=in.levels[l].x;g<in.levels[l].y;g++){int begin=int(out.rows.size());for(int row=in.groups[g].x;row<in.groups[g].y;row++)emit(row);out.groups.push_back({begin,int(out.rows.size())});}
    out.levels.push_back({first,int(out.groups.size()),0,kind});l++;
   }
  };
  for(auto& island:in.islands){
   GIsland o=island;o.rows.x=int(out.rows.size());o.levels.x=int(out.levels.size());
   pool(island.levels.x,island.levels.y,0);o.levels.y=int(out.levels.size());o.rows.y=int(out.rows.size());
   pool(island.levels.y,island.levels.z,1);o.levels.z=int(out.levels.size());
   pool(island.levels.z,island.levels.w,2);o.levels.w=int(out.levels.size());o.rows.z=int(out.rows.size());
   if(o.rows.z-o.rows.x!=island.rows.z-island.rows.x)throw std::runtime_error("Vulkan solver: island rows lost in level ordering");
   out.islands.push_back(o);
  }
 }
 // Appends a converted batch, shifting its body, row, group and level indices.
 static void merge(Batch& into,const Batch& from){
  int bodies=int(into.bodies.size()),rows=int(into.rows.size()),groups=int(into.groups.size()),levels=int(into.levels.size());
  into.bodies.insert(into.bodies.end(),from.bodies.begin(),from.bodies.end());into.impulses.insert(into.impulses.end(),from.impulses.begin(),from.impulses.end());
  for(auto r:from.rows){r.ids.x+=bodies;r.ids.y+=bodies;if(r.ids.z>=0)r.ids.z+=rows;into.rows.push_back(r);}
  for(auto g:from.groups)into.groups.push_back({g.x+rows,g.y+rows});
  for(auto l:from.levels){int shift=l.z==0?groups:rows;into.levels.push_back({l.x+shift,l.y+shift,l.z,l.w});}
  for(auto i:from.islands){i.rows.x+=rows;i.rows.y+=rows;i.rows.z+=rows;i.rows.w+=bodies;i.levels.x+=levels;i.levels.y+=levels;i.levels.z+=levels;i.levels.w+=levels;into.islands.push_back(i);}
  into.serialLevels+=from.serialLevels;into.serialRows+=from.serialRows;into.mergedLevels+=from.mergedLevels;
 }
 // Worlds whose solves start within `window` share one submission: the first
 // to arrive leads, waits for the others (or until every live Vulkan world has
 // joined) and runs a single dispatch over all of their islands. Separate
 // submissions each wait their turn against the game's graphics context.
 struct Gathering {std::vector<Buffers*> parts;bool closed=false,finished=false;nlohmann::json result;std::exception_ptr failure;};
 std::mutex gatherMutex;std::condition_variable gatherWake;std::shared_ptr<Gathering> open;std::atomic<int> worlds{0};
 std::chrono::microseconds window{std::getenv("MMDHL_VULKAN_BATCH_US")?std::atoi(std::getenv("MMDHL_VULKAN_BATCH_US")):500};
 nlohmann::json run(Buffers& data){
  auto start=Clock::now();if(data.islands.empty())return nlohmann::json::object();
  std::shared_ptr<Gathering> mine;bool leader=false;
  {std::lock_guard lock(gatherMutex);
   if(open&&!open->closed){mine=open;mine->parts.push_back(&data);}else{mine=std::make_shared<Gathering>();mine->parts.push_back(&data);open=mine;leader=true;}
  }
  gatherWake.notify_all();
  if(leader){
   {std::unique_lock lock(gatherMutex);
    gatherWake.wait_for(lock,window,[&]{return int(mine->parts.size())>=std::max(1,worlds.load());});
    mine->closed=true;if(open==mine)open.reset();}
   double gatherMs=elapsed(start);Slot* slot=nullptr;
   try{
    slot=acquire();double waitMs=elapsed(start)-gatherMs;
    mine->result=execute(slot,mine->parts);releaseSlot(slot);mine->result["gatherMs"]=gatherMs;mine->result["waitMs"]=waitMs;
   }catch(const std::exception& e){
    // A lost slot is never released: the device may still own its memory.
    if(slot&&slot->lost){std::lock_guard lock(mutex);quarantined=true;available=false;error=e.what();}else if(slot)releaseSlot(slot);
    mine->failure=std::current_exception();
   }
   {std::lock_guard lock(gatherMutex);mine->finished=true;}
   gatherWake.notify_all();
  }else{std::unique_lock lock(gatherMutex);gatherWake.wait(lock,[&]{return mine->finished;});}
  if(mine->failure)std::rethrow_exception(mine->failure);
  auto result=mine->result;result["batchWorlds"]=mine->parts.size();result["wallMs"]=elapsed(start);return result;
 }
 // Runs on an acquired slot without taking the device mutex (init's self-test holds it).
 nlohmann::json execute(Slot* slot,const std::vector<Buffers*>& parts){
  auto prepare=Clock::now();
  Batch batch;std::vector<Batch> pieces(parts.size());std::vector<std::array<size_t,2>> partOffsets;
  for(size_t i=0;i<parts.size();i++){convert(*parts[i],pieces[i]);partOffsets.push_back({batch.bodies.size(),batch.rows.size()});merge(batch,pieces[i]);}
  double convertMs=elapsed(prepare);
  std::array<VkDeviceSize,Arrays> sizes={batch.bodies.size()*sizeof(VBody),batch.rows.size()*sizeof(VRow),batch.impulses.size()*sizeof(F2),batch.groups.size()*sizeof(I2),batch.levels.size()*sizeof(I4),batch.islands.size()*sizeof(GIsland)};
  std::array<const void*,Arrays> sources={batch.bodies.data(),batch.rows.data(),batch.impulses.data(),batch.groups.data(),batch.levels.data(),batch.islands.data()};
  std::array<VkDeviceSize,Arrays> offsets{};VkDeviceSize total=0;for(int i=0;i<Arrays;i++){offsets[i]=total;total+=(sizes[i]+255)&~VkDeviceSize(255);}
  constexpr auto storage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  for(int i=0;i<Arrays;i++)slot->dirty|=ensure(slot->work[i],sizes[i],storage,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0,false);
  ensure(slot->staging,total,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0,true);
  ensure(slot->readback,sizes[0]+sizes[2],VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,VK_MEMORY_PROPERTY_HOST_CACHED_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
  // The layout always has the clock binding; it points at the clock buffer when
  // profiling and at the (unused) island buffer otherwise.
  if(profileClock){slot->dirty|=ensure(slot->clocks,(batch.levels.size()+8)*sizeof(uint32_t),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0,true);std::memset(slot->clocks.mapped,0,(batch.levels.size()+8)*sizeof(uint32_t));}
  for(int i=0;i<Arrays;i++)if(sizes[i])std::memcpy(static_cast<char*>(slot->staging.mapped)+offsets[i],sources[i],size_t(sizes[i]));
  if(slot->dirty){
   std::array<VkDescriptorBufferInfo,Arrays+1> infos{};std::array<VkWriteDescriptorSet,Arrays+1> writes{};
   for(int i=0;i<=Arrays;i++){auto& a=i<Arrays?slot->work[i]:profileClock?slot->clocks:slot->work[Arrays-1];infos[i]={a.buffer,0,VK_WHOLE_SIZE};writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=slot->set;writes[i].dstBinding=uint32_t(i);writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&infos[i];}
   api.vkUpdateDescriptorSets(device,uint32_t(writes.size()),writes.data(),0,nullptr);slot->dirty=false;
  }
  auto cmd=slot->commands;check(api.vkResetCommandBuffer(cmd,0),"Reset solver commands");
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(api.vkBeginCommandBuffer(cmd,&begin),"Record solver commands");
  if(timestamps)api.vkCmdResetQueryPool(cmd,slot->queries,0,2);
  for(int i=0;i<Arrays;i++)if(sizes[i]){VkBufferCopy region{offsets[i],0,sizes[i]};api.vkCmdCopyBuffer(cmd,slot->staging.buffer,slot->work[i].buffer,1,&region);}
  auto barrier=[&](VkPipelineStageFlags from,VkAccessFlags fromAccess,VkPipelineStageFlags to,VkAccessFlags toAccess){VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=fromAccess;b.dstAccessMask=toAccess;api.vkCmdPipelineBarrier(cmd,from,to,0,1,&b,0,nullptr,0,nullptr);};
  barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  if(timestamps)api.vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,slot->queries,0);
  // Same pooling test as the kernel: nine words per body plus one per row.
  bool pooled=true;for(auto& island:batch.islands)pooled&=9ll*island.other.z+(island.rows.z-island.rows.x)<=(long long)(sharedBytes/4);
  api.vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[pooled?1:0]);api.vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&slot->set,0,nullptr);
  if(setOccupancy){VkComputeOccupancyPriorityParametersNV parameters{VK_STRUCTURE_TYPE_COMPUTE_OCCUPANCY_PRIORITY_PARAMETERS_NV};parameters.occupancyPriority=occupancyPriority;parameters.occupancyThrottling=occupancyThrottling;setOccupancy(cmd,&parameters);}
  // Each island runs its split-impulse iterations, then its main iterations;
  // with chunking every dispatch covers the next chunkIterations of them.
  int span=1;for(auto& island:batch.islands)span=std::max(span,(island.other.y!=0?island.other.w:0)+island.other.x);
  int step=chunkIterations>0?chunkIterations:span,dispatches=0;
  for(int first=0;first<span;first+=step,dispatches++){
   if(first)barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
   std::array<int32_t,2> range={first,first+step};
   api.vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(range),range.data());
   api.vkCmdDispatch(cmd,uint32_t(batch.islands.size()),1,1);
  }
  if(timestamps)api.vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,slot->queries,1);
  barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  VkBufferCopy bodiesBack{0,0,sizes[0]},impulsesBack{0,sizes[0],sizes[2]};
  api.vkCmdCopyBuffer(cmd,slot->work[0].buffer,slot->readback.buffer,1,&bodiesBack);if(sizes[2])api.vkCmdCopyBuffer(cmd,slot->work[2].buffer,slot->readback.buffer,1,&impulsesBack);
  barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
  check(api.vkEndCommandBuffer(cmd),"Finish solver commands");
  check(api.vkResetFences(device,1,&slot->fence),"Reset solver fence");
  double prepareMs=elapsed(prepare);auto submitted=Clock::now();
  {VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;check(queueCall(slot->queue,[&](VkQueue queue){return api.vkQueueSubmit(queue,1,&submit,slot->fence);}),"Submit constraint solver");}
  auto waited=api.vkWaitForFences(device,1,&slot->fence,VK_TRUE,3'000'000'000ull);
  if(waited!=VK_SUCCESS){slot->lost=true;throw std::runtime_error(waited==VK_TIMEOUT?std::string("Vulkan solver timed out"):"Vulkan solver failed (Vulkan "+std::to_string(int(waited))+")");}
  double gpuWallMs=elapsed(submitted);auto finish=Clock::now();
  double kernelMs=0;if(timestamps){std::array<uint64_t,2> stamps{};if(api.vkGetQueryPoolResults(device,slot->queries,0,2,sizeof(stamps),stamps.data(),sizeof(uint64_t),VK_QUERY_RESULT_64_BIT)==VK_SUCCESS)kernelMs=double(stamps[1]-stamps[0])*timestampPeriod*1e-6;}
  if(!slot->readback.coherent){VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=slot->readback.memory;range.size=VK_WHOLE_SIZE;check(api.vkInvalidateMappedMemoryRanges(device,1,&range),"Read solver results");}
  auto solvedBodies=static_cast<const VBody*>(slot->readback.mapped);auto solvedImpulses=reinterpret_cast<const F2*>(static_cast<const char*>(slot->readback.mapped)+sizes[0]);
  for(size_t part=0;part<parts.size();part++){
   auto& data=*parts[part];auto [bodyOffset,rowOffset]=partOffsets[part];
   for(size_t i=0;i<data.bodies.size();i++){auto& b=solvedBodies[bodyOffset+i];for(auto v:{b.linear,b.angular,b.push,b.turn})if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw std::runtime_error("Non-finite Vulkan velocity");
    data.bodies[i].linear=b.linear;data.bodies[i].angular=b.angular;data.bodies[i].push=b.push;data.bodies[i].turn=b.turn;}
   auto& order=pieces[part].order;
   for(size_t i=0;i<order.size();i++){auto impulse=solvedImpulses[rowOffset+i];if(!std::isfinite(impulse.x)||!std::isfinite(impulse.y))throw std::runtime_error("Non-finite Vulkan impulse");data.impulses[order[i]]=impulse;}
  }
  nlohmann::json clockReport;
  if(profileClock){
   // The island with the most cycles in its second iteration, level by level.
   // Words 0-3: row phase sums (load, math, store, rows); level cycles from word 8.
   auto phases=static_cast<const uint32_t*>(slot->clocks.mapped);auto cycles=phases+8;size_t worst=0;double worstTotal=-1;
   for(size_t i=0;i<batch.islands.size();i++){double sum=0;for(int l=batch.islands[i].levels.x;l<batch.islands[i].levels.w;l++)sum+=cycles[l];if(sum>worstTotal){worstTotal=sum;worst=i;}}
   auto& island=batch.islands[worst];nlohmann::json levels=nlohmann::json::array();
   for(int l=island.levels.x;l<island.levels.w;l++){auto level=batch.levels[l];int longestGroup=0,groupCount=0;long long rowCount=0;
    if(level.z==0)for(int g=level.x;g<level.y;g++){longestGroup=std::max(longestGroup,batch.groups[g].y-batch.groups[g].x);rowCount+=batch.groups[g].y-batch.groups[g].x;groupCount++;}else rowCount=level.y-level.x;
    levels.push_back({{"cycles",cycles[l]},{"kind",level.z?"serial":"groups"},{"groups",groupCount},{"longest",longestGroup},{"rows",rowCount},{"pool",level.w==0?"joint":level.w==1?"contact":"friction"}});}
   double solved=std::max(1u,phases[3]);
   clockReport={{"island",worst},{"cycles",worstTotal},{"bodies",island.other.z},{"rows",island.rows.z-island.rows.x},{"levels",levels},
    {"rowPhases",{{"rows",phases[3]},{"loadCycles",phases[0]/solved},{"mathCycles",phases[1]/solved},{"storeCycles",phases[2]/solved}}},{"sharedDependent16",phases[4]},{"sharedIndependent16",phases[5]},{"rowSolveCycles",phases[6]}};
  }
  return {{"device",name},{"kernelMs",kernelMs},{"gpuWallMs",gpuWallMs},{"prepareMs",prepareMs},{"convertMs",convertMs},{"readMs",elapsed(finish)},{"uploadBytes",total},{"downloadBytes",sizes[0]+sizes[2]},{"bufferGrowths",growths},{"batchIslands",batch.islands.size()},
   {"kernel",pooled?"shared":"shared+global"},{"levels",batch.levels.size()},{"serialRuns",batch.serialLevels},{"serialRows",batch.serialRows},{"mergedLevels",batch.mergedLevels},{"groups",batch.groups.size()},{"workgroupSize",workgroup},{"queue",slot->queue},{"dispatches",dispatches},{"clock",clockReport}};
 }
 // One joint row against a fixed body, checked against the scalar result.
 void selfTest(){
  Buffers b;GBody moving;moving.invMass={1,1,1,1};moving.linearFactor={1,1,1,0};moving.angularFactor={1,1,1,0};b.bodies={moving,GBody{}};
  GRow row;row.normalA={1,0,0,0};row.normalB={-1,-0.f,-0.f,0};row.params={1,0,.5f,0};row.limits={-1e30f,1e30f,0,2};row.ids={0,1,-1,0};b.rows={row};b.impulses={F2{}};
  b.groups={{0,1}};b.levels={{0,1}};GIsland island;island.rows={0,1,1,0};island.levels={0,1,1,1};island.other={2,0,2,2};b.islands={island};
  auto slot=slots.front().get();
  try{execute(slot,{&b});}catch(...){if(slot->lost)quarantined=true;throw;}
  // Iteration 1: delta = rhs = 1. Iteration 2: delta = rhs - (n.v)*jacDiagABInv
  // = 1 - 1*0.5. Linear x and the applied impulse both reach 1.5.
  if(std::fabs(b.bodies[0].linear.x-1.5f)>1e-6f||std::fabs(b.impulses[0].x-1.5f)>1e-6f||b.bodies[1].linear.x!=0)
   throw std::runtime_error("Vulkan solver self-test returned wrong values (velocity "+std::to_string(b.bodies[0].linear.x)+", impulse "+std::to_string(b.impulses[0].x)+")");
 }
 nlohmann::json describe(){
  return {{"available",available},{"device",name},{"driver",driver},{"error",error},{"api","Vulkan "+version},{"context",shared?"dxvk-shared":"standalone"},{"occupancyPriority",setOccupancy?nlohmann::json(occupancyPriority):nlohmann::json()},{"chunkIterations",chunkIterations},{"asyncCompute",asyncCompute},{"queues",queues.size()},{"workgroupSize",workgroup},{"sharedBodies",sharedBodies},{"sharedMemoryBytes",sharedBytes},{"timestamps",timestamps},{"coloring",vulkanColoring()},{"pipelineStatistics",pipelineStatistics},{"rowArithmetic",fusedRows?"sse4.1+fma3":"sse2"},
   {"scope","constraint iterations; CPU collision detection, row setup and integration"},{"experimental",true},{"validated",false}};
 }
};
Device& device(){static Device value;return value;}
// Ordered by default: it reproduces the CPU solver exactly.
std::atomic<bool> colored{false};
}
nlohmann::json runVulkanSolver(Buffers& data){return device().run(data);}
void countVulkanWorld(int delta){device().worlds+=delta;}
void setVulkanColoring(bool value){colored=value;}
bool vulkanColoring(){return colored;}
void shutdownVulkan(){auto& d=device();std::lock_guard lock(d.mutex);d.shutdown();}
nlohmann::json vulkanCapabilities(bool isolated){
 static const nlohmann::json preflight=isolated?probeVulkanWorker():nlohmann::json{{"available",true}};
 if(isolated&&!preflight.value("available",false))return preflight;
 auto& d=device();std::lock_guard lock(d.mutex);d.init();return d.describe();
}
}
