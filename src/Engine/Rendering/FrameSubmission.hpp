#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Vulkan
{
    class VulkanBaseRenderer;

    class FrameSubmission final
    {
    public:
        static bool WaitAndAcquire(VulkanBaseRenderer& renderer, uint64_t timeout,
                                   VkSemaphore& imageAvailable, VkSemaphore& renderFinished);
        static void Submit(VulkanBaseRenderer& renderer, VkCommandBuffer commandBuffer,
                           VkSemaphore imageAvailable, VkSemaphore renderFinished);
        static bool Present(VulkanBaseRenderer& renderer, VkSemaphore renderFinished);
    };
}
