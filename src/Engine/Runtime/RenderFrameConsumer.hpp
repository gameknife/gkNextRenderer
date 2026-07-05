#pragma once

#include <vulkan/vulkan.h>

namespace Vulkan
{
    class VulkanBaseRenderer;
}

namespace Runtime
{
    // Unified injection point for consuming fully rendered frames after UI submission.
    // Implementations may stream, terminal-blit, record, or otherwise present frames.
    class IRenderFrameConsumer
    {
    public:
        virtual ~IRenderFrameConsumer() = default;

        virtual const char* Name() const = 0;
        virtual bool Start() = 0;
        virtual void Tick() {}
        virtual void RecordFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                                 Vulkan::VulkanBaseRenderer& renderer) = 0;
        virtual void OnRendererDeleteSwapChain() = 0;
        virtual void OnRendererPostLoadScene() {}
    };
}
