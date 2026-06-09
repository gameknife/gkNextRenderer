#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Vulkan
{
    class VulkanBaseRenderer;
}

namespace Runtime::Remote
{
    class FSignalingServer;
    class FVideoPipeline;

    class RemoteServer final
    {
    public:
        struct FConfig
        {
            bool enabled = false;
            std::string bindAddress = "0.0.0.0";
            uint32_t httpPort = 8088;
            uint32_t signalingPort = 8089;
            uint32_t bitrateKbps = 4000;
            uint32_t fps = 30;
            uint32_t width = 0;
            uint32_t height = 0;
        };

        explicit RemoteServer(FConfig config);
        ~RemoteServer();

        bool Start();
        void Stop();

        // Render thread: records the per-frame video capture into the frame command buffer.
        void RecordVideoFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                              Vulkan::VulkanBaseRenderer& renderer);

        // Render thread: the renderer is about to destroy its swapchain.
        void OnRendererDeleteSwapChain();

        bool IsRunning() const { return running_; }
        const FConfig& Config() const { return config_; }

    private:
        FConfig config_;
        bool running_ = false;
        std::unique_ptr<FVideoPipeline> videoPipeline_;
        std::unique_ptr<FSignalingServer> signalingServer_;
    };
}
