#pragma once

#include "Engine/Runtime/Interface/RenderFrameConsumer.hpp"
#include "Engine/Runtime/Camera/ModelViewController.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Editor/MultiViewportBackend.hpp"
#include "Modules/NextRemote/CloudInputRouter.hpp"
#include "Modules/NextRemote/RemoteImGuiSession.hpp"
#include "Modules/NextRemote/VideoEncoder.hpp"

#include <vulkan/vulkan.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vulkan
{
    class FrameBuffer;
    class RenderImage;
    class RenderPass;
    class RenderView;
    class VulkanBaseRenderer;
}

namespace Runtime::Remote
{
    class FSignalingServer;
    class FVideoPipeline;

    class RemoteServer final : public Runtime::IRenderFrameConsumer
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
            bool multiView = false;
            uint32_t maxClients = 2;
            uint32_t encodeBindlessBase = 60;
            EVideoEncoderBackend encoderBackend = EVideoEncoderBackend::Auto;
        };

        const char* Name() const override { return "RemoteServer"; }
        explicit RemoteServer(FConfig config);
        ~RemoteServer() override;

        bool Start() override;
        void Stop();
        void Tick() override;

        // Render thread: records the per-frame video capture into the frame command buffer.
        void RecordFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                         Vulkan::VulkanBaseRenderer& renderer) override;

        // Render thread: the renderer is about to destroy its swapchain.
        void OnRendererDeleteSwapChain() override;
        void OnRendererPostLoadScene() override;

        bool IsRunning() const { return running_; }
        const FConfig& Config() const { return config_; }
        bool TryRegisterCloudSession(const std::string& sessionId);
        void UnregisterCloudSession(const std::string& sessionId);
        void EnqueueCloudInputBinary(const std::string& sessionId, const std::vector<std::byte>& message);
        void EnqueueCloudInputText(const std::string& sessionId, const std::string& message);
        FVideoPipeline* VideoPipelineForSession(const std::string& sessionId);

    private:
        struct FRemoteClientView
        {
            Runtime::Camera::ModelViewController controller;
            bool initialized = false;
            uint32_t viewIndex = 0;
            uint32_t lastButtonMask = 0;
            std::chrono::steady_clock::time_point lastInputTime{};
            uint32_t targetFps = 0;
            std::shared_ptr<FRemoteImGuiSession> uiSession;
            std::unique_ptr<Vulkan::RenderImage> compositeImage;
            std::unique_ptr<Vulkan::FrameBuffer> compositeFramebuffer;
            std::unique_ptr<Vulkan::RenderPass> compositeRenderPass;
            std::vector<NextUI::UiRenderBuffer> uiRenderBuffers;
            VkPipeline compositeUiPipeline = VK_NULL_HANDLE;
            VkExtent2D compositeExtent{0, 0};
            VkImageLayout compositeLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            uint32_t compositeBindlessSlot = 0;
        };

        void TickCloudViews();
        void InitializeClientView(FRemoteClientView& clientView);
        void ApplyClientTargetFps(FRemoteClientView& clientView, std::chrono::steady_clock::time_point now);
        Assets::Camera BuildClientCamera(const FRemoteClientView& clientView) const;
        void ReleaseClientViewResources(FRemoteClientView& clientView);
        bool EnsureClientCompositeTarget(FRemoteClientView& clientView, Vulkan::VulkanBaseRenderer& renderer,
                                         VkExtent2D extent);
        void CopyViewToComposite(FRemoteClientView& clientView, VkCommandBuffer commandBuffer,
                                 Vulkan::VulkanBaseRenderer& renderer, Vulkan::RenderView& view);
        void RenderClientUiToComposite(FRemoteClientView& clientView, VkCommandBuffer commandBuffer,
                                       uint32_t imageIndex, Vulkan::VulkanBaseRenderer& renderer,
                                       const Assets::Camera& camera);
        void RecordCloudViewFrame(uint32_t viewIndex, VkCommandBuffer commandBuffer, uint32_t imageIndex,
                                  Vulkan::RenderView& view);
        NextGameInstanceBase::FRemoteViewActionContext BuildActionContext(const std::string& sessionId,
                                                                          const FRemoteClientView& clientView) const;
        void RequestAllKeyframes();
        void LogStatsIfDue();

        FConfig config_;
        bool running_ = false;
        std::unique_ptr<FVideoPipeline> videoPipeline_;
        std::vector<std::unique_ptr<FVideoPipeline>> cloudVideoPipelines_;
        std::unique_ptr<FSignalingServer> signalingServer_;
        FCloudInputRouter cloudInputRouter_;
        std::mutex cloudViewsMutex_;
        std::unordered_map<std::string, FRemoteClientView> cloudViews_;
        std::vector<uint32_t> pendingDisabledViewIndices_;
        std::vector<std::string> pendingRemovedSessionIds_;
        std::chrono::steady_clock::time_point nextStatsLogTime_{};
    };
}
