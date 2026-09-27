#pragma once
#include "compute_rows.hpp"
#include <nlohmann/json.hpp>
namespace mmd {
// Vulkan compute for the secondary constraint iterations. It owns a
// standalone VkDevice on the best GPU and neither needs nor touches the
// game's renderer, so it runs the same under D3D9 and under DXVK.
// Startup is first checked in a disposable worker process (probeVulkanWorker).
nlohmann::json vulkanCapabilities(bool isolated=true);
nlohmann::json probeVulkanWorker();
// Solves every island of the batch in one dispatch and writes the solved
// velocities and impulses back into it. Throws on any device failure.
nlohmann::json runVulkanSolver(Buffers&);
// Live Vulkan worlds; a batch closes early once every one of them has joined.
void countVulkanWorld(int delta);
// Colored levels solve independent rows together; ordered levels keep
// Bullet's row sweep order exactly (slower, for fidelity comparison).
void setVulkanColoring(bool colored);
bool vulkanColoring();
// Call on the owning thread, after destroying worlds and before unloading DLLs.
void shutdownVulkan();
}
