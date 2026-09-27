#pragma once
// C API of the shared compute queues in the patched DXVK d3d9.dll
// (patches/dxvk/0001-shared-compute-queues.patch, src/d3d9/d3d9_shared_compute.h).
// DXVK creates queues on a dedicated compute family of the VkDevice that
// renders the game and never uses them itself; work submitted there overlaps
// rendering inside one GPU context.
#include <cstdint>
#include <Windows.h>
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#define DXVK_SHARED_COMPUTE_QUEUE_VERSION 1u

typedef struct DXVK_SHARED_COMPUTE_QUEUE {
  uint32_t                  Version;          // in: DXVK_SHARED_COMPUTE_QUEUE_VERSION
  VkInstance                Instance;
  VkPhysicalDevice          PhysicalDevice;
  VkDevice                  Device;
  PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
  uint32_t                  QueueFamilyIndex;
  uint32_t                  FirstQueueIndex;
  uint32_t                  QueueCount;
  void*                     Owner;            // for Lock/Unlock/Release
} DXVK_SHARED_COMPUTE_QUEUE;

// Acquire keeps the VkDevice and VkInstance alive until Release, even after the
// D3D9 device is destroyed; Owner is an opaque token. Hold Lock/Unlock around vkQueueSubmit and vkQueueWaitIdle on the shared
// queues (it excludes DXVK's vkDeviceWaitIdle). Never destroy the device or
// instance, never call vkDeviceWaitIdle on it, and destroy every object
// created on the device before Release.
typedef HRESULT (__stdcall *PFN_DXVK_AcquireSharedComputeQueue)(DXVK_SHARED_COMPUTE_QUEUE* pQueue);
typedef void    (__stdcall *PFN_DXVK_SharedComputeQueueOwner)(void* pOwner);
