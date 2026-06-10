#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Vulkan
{
    class VulkanBaseRenderer;
}

namespace Runtime
{
    // Injection point for streaming rendered frames out of the engine
    // (e.g. WebRTC remote play). The implementation lives in
    // Modules/NextRemote; the application entry assembles it via
    // NextEngine::SetFrameStreamer when remote mode is requested.
    class IFrameStreamer
    {
    public:
        virtual ~IFrameStreamer() = default;

        virtual bool Start() = 0;

        // Render thread: records the per-frame video capture into the frame command buffer.
        virtual void RecordVideoFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                                      Vulkan::VulkanBaseRenderer& renderer) = 0;

        // Render thread: the renderer is about to destroy its swapchain.
        virtual void OnRendererDeleteSwapChain() = 0;
    };
}
