#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextRemote/RemoteServer.hpp"

#include "Modules/NextRemote/SignalingServer.hpp"
#include "Modules/NextRemote/VideoPipeline.hpp"

#include <spdlog/spdlog.h>

namespace Runtime::Remote
{
    RemoteServer::RemoteServer(FConfig config)
        : config_(std::move(config))
    {
    }

    RemoteServer::~RemoteServer()
    {
        Stop();
    }

    bool RemoteServer::Start()
    {
        if (running_)
        {
            return true;
        }
        if (!config_.enabled)
        {
            return false;
        }

        videoPipeline_ = std::make_unique<FVideoPipeline>(config_);
        videoPipeline_->Start();

        signalingServer_ = std::make_unique<FSignalingServer>(config_, videoPipeline_.get());
        if (!signalingServer_->Start())
        {
            signalingServer_.reset();
            videoPipeline_.reset();
            return false;
        }

        running_ = true;
        SPDLOG_INFO("RemotePlay: server started fps={} bitrate={}kbps target={}x{}", config_.fps,
                    config_.bitrateKbps, config_.width, config_.height);
        return true;
    }

    void RemoteServer::Stop()
    {
        // Sessions unregister their packet sinks on destruction, so they must go before the pipeline.
        if (signalingServer_)
        {
            signalingServer_->Stop();
            signalingServer_.reset();
        }
        if (videoPipeline_)
        {
            videoPipeline_->Stop();
            videoPipeline_.reset();
        }
        if (running_)
        {
            SPDLOG_INFO("RemotePlay: server stopped");
        }
        running_ = false;
    }

    void RemoteServer::RecordFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                                   Vulkan::VulkanBaseRenderer& renderer)
    {
        if (videoPipeline_)
        {
            videoPipeline_->RecordFrame(commandBuffer, imageIndex, renderer);
        }
    }

    void RemoteServer::OnRendererDeleteSwapChain()
    {
        if (videoPipeline_)
        {
            videoPipeline_->ReleaseSwapChainResources();
        }
    }
}
