#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/RemoteProtocol.hpp"
#include "Engine/Rendering/Preview/RenderViewServices.hpp"
#include "Modules/NextRemote/RemoteServer.hpp"

#include "Modules/NextRemote/SignalingServer.hpp"
#include "Modules/NextRemote/VideoPipeline.hpp"

#include <spdlog/spdlog.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>

#include <algorithm>

namespace Runtime::Remote
{
    namespace
    {
        constexpr auto remoteStatsLogInterval = std::chrono::seconds(10);
        constexpr auto remoteIdleFpsDelay = std::chrono::seconds(5);

        uint32_t IdleTargetFps(const uint32_t activeFps)
        {
            if (activeFps <= 15u)
            {
                return std::max(1u, activeFps);
            }
            return std::clamp(activeFps / 2u, 10u, 15u);
        }
    }

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

        NextEngine* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return false;
        }
        if (config_.multiView)
        {
            constexpr uint32_t maxRenderViewClients = Vulkan::OffscreenRenderViewController::kMaxSecondaryViews;
            const uint32_t requestedClients = std::min(std::max(1u, config_.maxClients), maxRenderViewClients);
            cloudVideoPipelines_.reserve(requestedClients);
            for (uint32_t viewIndex = 0; viewIndex < requestedClients; ++viewIndex)
            {
                RemoteServer::FConfig streamConfig = config_;
                streamConfig.encodeBindlessBase = 60000u + viewIndex * 16u;
                auto stream = std::make_unique<FVideoPipeline>(streamConfig);
                if (!stream->Initialize(engine->GetRenderer()))
                {
                    SPDLOG_WARN("RemotePlay: failed to initialize cloud video stream {}, limiting max clients to {}",
                                viewIndex, cloudVideoPipelines_.size());
                    break;
                }
                stream->Start();
                cloudVideoPipelines_.push_back(std::move(stream));
            }
            if (cloudVideoPipelines_.empty())
            {
                return false;
            }
            config_.maxClients = static_cast<uint32_t>(cloudVideoPipelines_.size());
            engine->GetRenderer().ViewServices().OffscreenViews().SetViewRenderedCallback(
                [this](uint32_t viewIndex, VkCommandBuffer commandBuffer, uint32_t imageIndex, Vulkan::RenderView& view)
                {
                    RecordCloudViewFrame(viewIndex, commandBuffer, imageIndex, view);
                });
        }
        else
        {
            videoPipeline_ = std::make_unique<FVideoPipeline>(config_);
            if (!videoPipeline_->Initialize(engine->GetRenderer()))
            {
                videoPipeline_.reset();
                return false;
            }
            videoPipeline_->Start();
        }

        FVideoPipeline* configPipeline =
            config_.multiView && !cloudVideoPipelines_.empty() ? cloudVideoPipelines_.front().get() : videoPipeline_.get();
        signalingServer_ = std::make_unique<FSignalingServer>(config_, configPipeline, this);
        if (!signalingServer_->Start())
        {
            signalingServer_.reset();
            if (videoPipeline_)
            {
                videoPipeline_->Stop();
                videoPipeline_.reset();
            }
            for (auto& stream : cloudVideoPipelines_)
            {
                stream->Stop();
            }
            cloudVideoPipelines_.clear();
            return false;
        }

        running_ = true;
        nextStatsLogTime_ = std::chrono::steady_clock::now() + remoteStatsLogInterval;
        if (config_.multiView)
        {
            SPDLOG_INFO(
                "RemotePlay: mode=multiview maxClients={} view={}x{} fps={} bitrate={}kbps requestedEncoder={} videoSource=per-session-renderview",
                config_.maxClients, config_.width, config_.height, config_.fps, config_.bitrateKbps,
                ToString(config_.encoderBackend));
        }
        else
        {
            SPDLOG_INFO("RemotePlay: mode=legacy source=swapchain fps={} bitrate={}kbps target={}x{} requestedEncoder={}",
                        config_.fps, config_.bitrateKbps, config_.width, config_.height,
                        ToString(config_.encoderBackend));
        }
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
        if (config_.multiView)
        {
            if (NextEngine* engine = NextEngine::GetInstance())
            {
                engine->GetRenderer().ViewServices().OffscreenViews().SetViewRenderedCallback({});
            }
        }
        for (auto& stream : cloudVideoPipelines_)
        {
            stream->Stop();
        }
        cloudVideoPipelines_.clear();
        if (running_)
        {
            SPDLOG_INFO("RemotePlay: server stopped");
        }
        {
            std::lock_guard lock(cloudViewsMutex_);
            cloudViews_.clear();
        }
        cloudInputRouter_.Clear();
        running_ = false;
        nextStatsLogTime_ = {};
    }

    void RemoteServer::Tick()
    {
        if (config_.multiView)
        {
            TickCloudViews();
        }
        LogStatsIfDue();
    }

    void RemoteServer::RecordFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                                   Vulkan::VulkanBaseRenderer& renderer)
    {
        if (!config_.multiView && videoPipeline_)
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
        for (auto& stream : cloudVideoPipelines_)
        {
            stream->ReleaseSwapChainResources();
        }
        if (config_.multiView)
        {
            std::lock_guard lock(cloudViewsMutex_);
            for (auto& [_, clientView] : cloudViews_)
            {
                clientView.initialized = false;
                clientView.lastButtonMask = 0;
            }
        }
        RequestAllKeyframes();
        SPDLOG_INFO("RemotePlay: renderer swapchain invalidated; remote streams will request keyframes");
    }

    void RemoteServer::OnRendererPostLoadScene()
    {
        if (config_.multiView)
        {
            std::lock_guard lock(cloudViewsMutex_);
            for (auto& [_, clientView] : cloudViews_)
            {
                clientView.initialized = false;
                clientView.lastButtonMask = 0;
                clientView.lastInputTime = std::chrono::steady_clock::now();
                clientView.targetFps = 0;
            }
        }
        RequestAllKeyframes();
        SPDLOG_INFO("RemotePlay: renderer scene changed; remote views reset and keyframes requested");
    }

    FVideoPipeline* RemoteServer::VideoPipelineForSession(const std::string& sessionId)
    {
        if (!config_.multiView)
        {
            return videoPipeline_.get();
        }
        std::lock_guard lock(cloudViewsMutex_);
        auto it = cloudViews_.find(sessionId);
        if (it == cloudViews_.end() || it->second.viewIndex >= cloudVideoPipelines_.size())
        {
            return nullptr;
        }
        return cloudVideoPipelines_[it->second.viewIndex].get();
    }

    bool RemoteServer::TryRegisterCloudSession(const std::string& sessionId)
    {
        if (!config_.multiView)
        {
            return true;
        }

        std::lock_guard lock(cloudViewsMutex_);
        if (cloudViews_.find(sessionId) != cloudViews_.end())
        {
            return true;
        }
        constexpr uint32_t maxRenderViewClients = Vulkan::OffscreenRenderViewController::kMaxSecondaryViews;
        const uint32_t effectiveMaxClients = std::min(std::max(1u, config_.maxClients), maxRenderViewClients);
        if (cloudViews_.size() >= effectiveMaxClients)
        {
            SPDLOG_WARN("RemotePlay: rejecting session {} because multiview is full ({}/{})", sessionId,
                        cloudViews_.size(), effectiveMaxClients);
            return false;
        }

        std::array<bool, maxRenderViewClients> usedSlots{};
        for (const auto& [_, clientView] : cloudViews_)
        {
            if (clientView.viewIndex < usedSlots.size())
            {
                usedSlots[clientView.viewIndex] = true;
            }
        }

        uint32_t viewIndex = 0;
        while (viewIndex < usedSlots.size() && usedSlots[viewIndex])
        {
            ++viewIndex;
        }
        if (viewIndex >= usedSlots.size())
        {
            return false;
        }

        FRemoteClientView clientView{};
        clientView.viewIndex = viewIndex;
        clientView.lastInputTime = std::chrono::steady_clock::now();
        clientView.targetFps = 0;
        cloudViews_.emplace(sessionId, std::move(clientView));
        SPDLOG_INFO("RemotePlay: session {} registered cloud view slot {} ({}/{})", sessionId, viewIndex,
                    cloudViews_.size(), effectiveMaxClients);
        return true;
    }

    void RemoteServer::UnregisterCloudSession(const std::string& sessionId)
    {
        if (!config_.multiView)
        {
            return;
        }

        {
            std::lock_guard lock(cloudViewsMutex_);
            auto it = cloudViews_.find(sessionId);
            if (it != cloudViews_.end())
            {
                pendingDisabledViewIndices_.push_back(it->second.viewIndex);
                cloudViews_.erase(it);
                SPDLOG_INFO("RemotePlay: session {} unregistered cloud view", sessionId);
            }
        }
        cloudInputRouter_.ClearSession(sessionId);
    }

    void RemoteServer::EnqueueCloudInputBinary(const std::string& sessionId,
                                               const std::vector<std::byte>& message)
    {
        if (config_.multiView)
        {
            cloudInputRouter_.EnqueueBinaryMessage(sessionId, message);
        }
    }

    void RemoteServer::EnqueueCloudInputText(const std::string& sessionId, const std::string& message)
    {
        if (config_.multiView)
        {
            cloudInputRouter_.EnqueueTextMessage(sessionId, message);
        }
    }

    void RemoteServer::TickCloudViews()
    {
        NextEngine* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return;
        }

        {
            std::vector<uint32_t> disabledViewIndices;
            {
                std::lock_guard lock(cloudViewsMutex_);
                disabledViewIndices.swap(pendingDisabledViewIndices_);
            }
            for (const uint32_t viewIndex : disabledViewIndices)
            {
                auto& offscreenViews = engine->GetRenderer().ViewServices().OffscreenViews();
                offscreenViews.SetEnabled(viewIndex, false);
                offscreenViews.ClearCameraOverride(viewIndex);
                SPDLOG_INFO("RemotePlay: disabled cloud RenderView slot {}", viewIndex);
            }
        }

        std::vector<std::string> sessionIds;
        {
            std::lock_guard lock(cloudViewsMutex_);
            sessionIds.reserve(cloudViews_.size());
            for (const auto& [sessionId, _] : cloudViews_)
            {
                sessionIds.push_back(sessionId);
            }
        }

        for (const std::string& sessionId : sessionIds)
        {
            std::vector<FCloudInputEvent> events = cloudInputRouter_.Drain(sessionId);
            const auto now = std::chrono::steady_clock::now();

            std::lock_guard lock(cloudViewsMutex_);
            auto it = cloudViews_.find(sessionId);
            if (it == cloudViews_.end())
            {
                continue;
            }

            FRemoteClientView& clientView = it->second;
            if (!clientView.initialized)
            {
                InitializeClientView(clientView);
                clientView.initialized = true;
            }
            if (!events.empty())
            {
                clientView.lastInputTime = now;
            }

            for (const FCloudInputEvent& event : events)
            {
                switch (event.type)
                {
                case FCloudInputEvent::EType::Key:
                    {
                        const SDL_Keymod mod = static_cast<SDL_Keymod>(event.mod);
                        const auto scancode = static_cast<SDL_Scancode>(event.scancode);
                        const SDL_Keycode key = SDL_GetKeyFromScancode(scancode, mod, true);
                        clientView.controller.SetKeyHeld(key, event.down);
                        if (event.down && !event.repeat && key == SDLK_SPACE)
                        {
                            if (NextGameInstanceBase* gameInstance = engine->GetGameInstance())
                            {
                                gameInstance->OnRemoteViewAction(BuildActionContext(sessionId, clientView), "space");
                            }
                        }
                        break;
                    }
                case FCloudInputEvent::EType::MouseMove:
                    {
                        const bool relative =
                            event.mode == static_cast<uint8_t>(ERemoteMouseMoveMode::Relative);
                        const double x = relative ? static_cast<double>(event.x)
                                                  : static_cast<double>(event.x) * static_cast<double>(config_.width);
                        const double y = relative ? static_cast<double>(event.y)
                                                  : static_cast<double>(event.y) * static_cast<double>(config_.height);
                        clientView.controller.ApplyMouseMove(x, y, relative);
                        break;
                    }
                case FCloudInputEvent::EType::MouseButton:
                    clientView.controller.ApplyMouseButton(event.button, event.down,
                                                           static_cast<double>(event.x) *
                                                               static_cast<double>(config_.width),
                                                           static_cast<double>(event.y) *
                                                               static_cast<double>(config_.height));
                    break;
                case FCloudInputEvent::EType::Wheel:
                    clientView.controller.ApplyWheel(event.x, event.y);
                    break;
                case FCloudInputEvent::EType::Gamepad:
                    clientView.controller.OnGamepadInput(event.axes[0], event.axes[1], event.axes[2], event.axes[3],
                                                         event.axes[4], event.axes[5]);
                    if ((event.buttonMask & (1u << SDL_GAMEPAD_BUTTON_SOUTH)) != 0u &&
                        (clientView.lastButtonMask & (1u << SDL_GAMEPAD_BUTTON_SOUTH)) == 0u)
                    {
                        if (NextGameInstanceBase* gameInstance = engine->GetGameInstance())
                        {
                            gameInstance->OnRemoteViewAction(BuildActionContext(sessionId, clientView), "space");
                        }
                    }
                    clientView.lastButtonMask = event.buttonMask;
                    break;
                }
            }

            const bool cameraUpdated = clientView.controller.UpdateCamera(8.0, engine->GetDeltaSeconds());
            if (cameraUpdated)
            {
                clientView.lastInputTime = now;
            }
            ApplyClientTargetFps(clientView, now);
            {
                auto& offscreenViews = engine->GetRenderer().ViewServices().OffscreenViews();
                offscreenViews.SetEnabled(clientView.viewIndex, true);
                offscreenViews.SetRenderExtent(clientView.viewIndex, VkExtent2D{config_.width, config_.height});
                offscreenViews.SetCameraOverride(clientView.viewIndex, BuildClientCamera(clientView));
            }
        }
    }

    void RemoteServer::ApplyClientTargetFps(FRemoteClientView& clientView,
                                            const std::chrono::steady_clock::time_point now)
    {
        if (clientView.viewIndex >= cloudVideoPipelines_.size() || !cloudVideoPipelines_[clientView.viewIndex])
        {
            return;
        }

        const uint32_t activeFps = std::max(1u, config_.fps);
        const uint32_t idleFps = IdleTargetFps(activeFps);
        const bool idle = clientView.lastInputTime.time_since_epoch().count() != 0 &&
            now - clientView.lastInputTime >= remoteIdleFpsDelay;
        const uint32_t desiredFps = idle ? idleFps : activeFps;
        if (clientView.targetFps == desiredFps)
        {
            return;
        }

        clientView.targetFps = desiredFps;
        cloudVideoPipelines_[clientView.viewIndex]->SetTargetFps(desiredFps);
        SPDLOG_INFO("RemotePlay: cloud view slot {} target fps -> {}", clientView.viewIndex, desiredFps);
    }

    void RemoteServer::InitializeClientView(FRemoteClientView& clientView)
    {
        NextEngine* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return;
        }

        Assets::Camera camera = engine->GetScene().GetRenderCamera();
        if (const NextGameInstanceBase* gameInstance = engine->GetGameInstance())
        {
            Assets::Camera overrideCamera = camera;
            if (gameInstance->OverrideRenderCamera(overrideCamera))
            {
                camera = overrideCamera;
            }
        }
        clientView.controller.Reset(camera);
        clientView.lastInputTime = std::chrono::steady_clock::now();
    }

    Assets::Camera RemoteServer::BuildClientCamera(const FRemoteClientView& clientView) const
    {
        NextEngine* engine = NextEngine::GetInstance();
        Assets::Camera camera = engine ? engine->GetScene().GetRenderCamera() : Assets::Camera{};
        camera.ModelView = clientView.controller.ModelView();
        camera.FieldOfView = clientView.controller.FieldOfView();
        return camera;
    }

    void RemoteServer::RecordCloudViewFrame(const uint32_t viewIndex, VkCommandBuffer commandBuffer,
                                            const uint32_t imageIndex, Vulkan::RenderView& view)
    {
        if (!config_.multiView || viewIndex >= cloudVideoPipelines_.size() || !cloudVideoPipelines_[viewIndex])
        {
            return;
        }
        cloudVideoPipelines_[viewIndex]->RecordFrameFromStorage(
            commandBuffer,
            imageIndex,
            NextEngine::GetInstance()->GetRenderer(),
            view.RtBankBase() + Assets::Bindless::RT_DENOISED,
            view.RenderExtent());
    }

    NextGameInstanceBase::FRemoteViewActionContext RemoteServer::BuildActionContext(
        const std::string& sessionId, const FRemoteClientView& clientView) const
    {
        NextGameInstanceBase::FRemoteViewActionContext context;
        context.sessionId = sessionId;
        context.position = clientView.controller.GetPosition();
        context.forward = clientView.controller.GetForward();
        context.right = clientView.controller.GetRight();
        context.up = clientView.controller.GetUp();
        return context;
    }

    void RemoteServer::RequestAllKeyframes()
    {
        if (videoPipeline_)
        {
            videoPipeline_->RequestKeyframe();
        }
        for (auto& stream : cloudVideoPipelines_)
        {
            if (stream)
            {
                stream->RequestKeyframe();
            }
        }
    }

    void RemoteServer::LogStatsIfDue()
    {
        if (!running_)
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (nextStatsLogTime_.time_since_epoch().count() != 0 && now < nextStatsLogTime_)
        {
            return;
        }
        nextStatsLogTime_ = now + remoteStatsLogInterval;

        if (!config_.multiView)
        {
            if (videoPipeline_)
            {
                const FVideoPipeline::FStats stats = videoPipeline_->Stats();
                SPDLOG_INFO("RemotePlay: legacy stats sinks={} fps={} bitrate={}kbps dropped={} encoder={}",
                            stats.sinkCount, stats.targetFps, stats.bitrateKbps, stats.droppedFrames,
                            stats.activeEncoder);
            }
            return;
        }

        size_t activeSessions = 0;
        {
            std::lock_guard lock(cloudViewsMutex_);
            activeSessions = cloudViews_.size();
        }

        SPDLOG_INFO("RemotePlay: multiview stats activeSessions={} streams={}", activeSessions,
                    cloudVideoPipelines_.size());
        for (size_t viewIndex = 0; viewIndex < cloudVideoPipelines_.size(); ++viewIndex)
        {
            if (!cloudVideoPipelines_[viewIndex])
            {
                continue;
            }

            const FVideoPipeline::FStats stats = cloudVideoPipelines_[viewIndex]->Stats();
            SPDLOG_INFO("RemotePlay: stream {} stats sinks={} fps={} bitrate={}kbps dropped={} encoder={}",
                        viewIndex, stats.sinkCount, stats.targetFps, stats.bitrateKbps, stats.droppedFrames,
                        stats.activeEncoder);
        }
    }
}
