#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <vulkan/vulkan.h>

// NVIDIA Streamline (DLSS / DLSS-RR) integration. All sl::* usage lives in
// StreamlineIntegration.cpp behind WITH_STREAMLINE; on other platforms these
// entry points compile to no-ops reporting no DLSS support.
namespace StreamlineWrapper
{
    bool ShouldInitialize();
    void Initialize();
    void LazyInit(VkDevice device, VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t computeQueueIdx,
                  uint32_t computeQueueFamily, uint32_t graphicsQueueIdx, uint32_t graphicsQueueFamily,
                  bool& outSupportDLSS, bool& outSupportDLSSRR);
    void Shutdown();
}
