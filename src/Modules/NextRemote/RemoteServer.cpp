#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/RemoteProtocol.hpp"
#include "Engine/Rendering/Preview/RenderViewServices.hpp"
#include "Modules/NextRemote/RemoteServer.hpp"

#include "Modules/NextRemote/SignalingServer.hpp"
#include "Modules/NextRemote/VideoPipeline.hpp"

#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"

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
        constexpr VkFormat remoteCompositeFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        constexpr uint32_t remoteCompositeBindlessBase = 64500u;

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
                ReleaseClientViewResources(clientView);
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
                ReleaseClientViewResources(clientView);
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
                pendingRemovedSessionIds_.push_back(sessionId);
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
            std::vector<std::string> removedSessionIds;
            {
                std::lock_guard lock(cloudViewsMutex_);
                disabledViewIndices.swap(pendingDisabledViewIndices_);
                removedSessionIds.swap(pendingRemovedSessionIds_);
            }
            for (const uint32_t viewIndex : disabledViewIndices)
            {
                auto& offscreenViews = engine->GetRenderer().ViewServices().OffscreenViews();
                offscreenViews.SetEnabled(viewIndex, false);
                offscreenViews.ClearCameraOverride(viewIndex);
                SPDLOG_INFO("RemotePlay: disabled cloud RenderView slot {}", viewIndex);
            }
            if (!removedSessionIds.empty())
            {
                std::lock_guard lock(cloudViewsMutex_);
                for (const std::string& sessionId : removedSessionIds)
                {
                    auto it = cloudViews_.find(sessionId);
                    if (it == cloudViews_.end())
                    {
                        continue;
                    }
                    ReleaseClientViewResources(it->second);
                    if (engine->GetGameInstance())
                    {
                        engine->GetGameInstance()->OnRemoteUiSessionClosed(sessionId);
                    }
                    cloudViews_.erase(it);
                }
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
            if (!clientView.uiSession)
            {
                clientView.uiSession = std::make_shared<FRemoteImGuiSession>(*engine, sessionId);
            }
            clientView.uiSession->HandleInputEvents(events, VkExtent2D{config_.width, config_.height});
            const bool uiCapturesKeyboard = clientView.uiSession->WantsCaptureKeyboard();
            const bool uiCapturesMouse = clientView.uiSession->WantsCaptureMouse();

            for (const FCloudInputEvent& event : events)
            {
                switch (event.type)
                {
                case FCloudInputEvent::EType::Key:
                    {
                        if (uiCapturesKeyboard)
                        {
                            break;
                        }
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
                        if (uiCapturesMouse && !relative)
                        {
                            break;
                        }
                        const double x = relative ? static_cast<double>(event.x)
                                                  : static_cast<double>(event.x) * static_cast<double>(config_.width);
                        const double y = relative ? static_cast<double>(event.y)
                                                  : static_cast<double>(event.y) * static_cast<double>(config_.height);
                        clientView.controller.ApplyMouseMove(x, y, relative);
                        break;
                    }
                case FCloudInputEvent::EType::MouseButton:
                    if (uiCapturesMouse)
                    {
                        break;
                    }
                    clientView.controller.ApplyMouseButton(event.button, event.down,
                                                           static_cast<double>(event.x) *
                                                               static_cast<double>(config_.width),
                                                           static_cast<double>(event.y) *
                                                               static_cast<double>(config_.height));
                    break;
                case FCloudInputEvent::EType::Wheel:
                    if (uiCapturesMouse)
                    {
                        break;
                    }
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
                case FCloudInputEvent::EType::TextUtf8:
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

    void RemoteServer::ReleaseClientViewResources(FRemoteClientView& clientView)
    {
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            if (NextUI::UserInterface* userInterface = engine->GetUserInterface())
            {
                userInterface->DestroyViewportPipeline(clientView.compositeUiPipeline);
            }
        }
        clientView.compositeUiPipeline = VK_NULL_HANDLE;
        clientView.uiRenderBuffers.clear();
        clientView.compositeFramebuffer.reset();
        clientView.compositeRenderPass.reset();
        clientView.compositeImage.reset();
        clientView.compositeExtent = {};
        clientView.compositeLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        clientView.compositeBindlessSlot = 0;
    }

    bool RemoteServer::EnsureClientCompositeTarget(FRemoteClientView& clientView,
                                                   Vulkan::VulkanBaseRenderer& renderer,
                                                   VkExtent2D extent)
    {
        extent.width = std::max(1u, extent.width);
        extent.height = std::max(1u, extent.height);
        if (clientView.compositeImage &&
            clientView.compositeExtent.width == extent.width &&
            clientView.compositeExtent.height == extent.height &&
            clientView.compositeUiPipeline != VK_NULL_HANDLE)
        {
            return true;
        }

        ReleaseClientViewResources(clientView);
        clientView.compositeExtent = extent;
        clientView.compositeBindlessSlot = remoteCompositeBindlessBase + clientView.viewIndex;
        clientView.compositeImage = std::make_unique<Vulkan::RenderImage>(
            renderer.Device(),
            extent,
            remoteCompositeFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            false,
            fmt::format("Remote Composite View {}", clientView.viewIndex).c_str());

        if (Assets::GlobalTexturePool* texturePool = Assets::GlobalTexturePool::GetInstance())
        {
            texturePool->BindStorageTexture(clientView.compositeBindlessSlot,
                                            clientView.compositeImage->GetImageView());
            texturePool->BindSampleTexture(clientView.compositeBindlessSlot,
                                           clientView.compositeImage->GetImageView(),
                                           clientView.compositeImage->Sampler());
        }

        clientView.compositeRenderPass = std::make_unique<Vulkan::RenderPass>(
            renderer.SwapChain(),
            remoteCompositeFormat,
            renderer.DepthBuffer(),
            VK_ATTACHMENT_LOAD_OP_LOAD,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        clientView.compositeRenderPass->SetDebugName(fmt::format("Remote Composite UI RenderPass {}", clientView.viewIndex));
        clientView.compositeFramebuffer = std::make_unique<Vulkan::FrameBuffer>(
            extent,
            clientView.compositeImage->GetImageView(),
            *clientView.compositeRenderPass,
            false);

        if (NextEngine* engine = NextEngine::GetInstance())
        {
            if (NextUI::UserInterface* userInterface = engine->GetUserInterface())
            {
                clientView.compositeUiPipeline =
                    userInterface->CreateViewportPipeline(clientView.compositeRenderPass->Handle());
            }
        }
        clientView.uiRenderBuffers.resize(renderer.SwapChain().Images().size());
        clientView.compositeLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        return clientView.compositeUiPipeline != VK_NULL_HANDLE;
    }

    void RemoteServer::CopyViewToComposite(FRemoteClientView& clientView,
                                           VkCommandBuffer commandBuffer,
                                           Vulkan::VulkanBaseRenderer& renderer,
                                           Vulkan::RenderView& view)
    {
        const Vulkan::RenderImage* src = renderer.GetStorageImage(view.RtBankBase() + Assets::Bindless::RT_DENOISED);
        if (!src || !clientView.compositeImage)
        {
            return;
        }

        const VkExtent2D extent = view.RenderExtent();
        src->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        const VkAccessFlags oldAccess =
            clientView.compositeLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0u : VK_ACCESS_SHADER_READ_BIT;
        clientView.compositeImage->InsertBarrier(commandBuffer, oldAccess, VK_ACCESS_TRANSFER_WRITE_BIT,
                                                 clientView.compositeLayout,
                                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        clientView.compositeLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        VkImageCopy copyRegion{};
        copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.extent = {extent.width, extent.height, 1};
        vkCmdCopyImage(commandBuffer,
                       src->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       clientView.compositeImage->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &copyRegion);

        clientView.compositeImage->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT,
                                                 VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        clientView.compositeLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        src->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    void RemoteServer::RenderClientUiToComposite(FRemoteClientView& clientView,
                                                 VkCommandBuffer commandBuffer,
                                                 uint32_t imageIndex,
                                                 Vulkan::VulkanBaseRenderer& renderer,
                                                 const Assets::Camera& camera)
    {
        if (!clientView.uiSession || !clientView.compositeImage || !clientView.compositeFramebuffer ||
            clientView.compositeUiPipeline == VK_NULL_HANDLE || clientView.uiRenderBuffers.empty())
        {
            return;
        }

        ImDrawData* drawData = clientView.uiSession->BuildDrawData(clientView.compositeExtent, camera);
        if (drawData == nullptr || drawData->CmdListsCount <= 0)
        {
            clientView.compositeImage->InsertBarrier(commandBuffer, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                     VK_ACCESS_SHADER_READ_BIT,
                                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                     VK_IMAGE_LAYOUT_GENERAL);
            clientView.compositeLayout = VK_IMAGE_LAYOUT_GENERAL;
            return;
        }

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = clientView.compositeRenderPass->Handle();
        renderPassInfo.framebuffer = clientView.compositeFramebuffer->Handle();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = clientView.compositeExtent;
        renderPassInfo.clearValueCount = 0;
        renderPassInfo.pClearValues = nullptr;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            if (NextUI::UserInterface* userInterface = engine->GetUserInterface())
            {
                const uint32_t bufferIndex = imageIndex % static_cast<uint32_t>(clientView.uiRenderBuffers.size());
                userInterface->RenderViewportDrawData(drawData,
                                                      commandBuffer,
                                                      clientView.uiRenderBuffers[bufferIndex],
                                                      clientView.compositeExtent,
                                                      renderer.SwapChain().HDROutputMode(),
                                                      clientView.compositeUiPipeline);
            }
        }
        vkCmdEndRenderPass(commandBuffer);
        clientView.compositeLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        clientView.compositeImage->InsertBarrier(commandBuffer, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                 VK_ACCESS_SHADER_READ_BIT,
                                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                 VK_IMAGE_LAYOUT_GENERAL);
        clientView.compositeLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    void RemoteServer::RecordCloudViewFrame(const uint32_t viewIndex, VkCommandBuffer commandBuffer,
                                            const uint32_t imageIndex, Vulkan::RenderView& view)
    {
        if (!config_.multiView || viewIndex >= cloudVideoPipelines_.size() || !cloudVideoPipelines_[viewIndex])
        {
            return;
        }

        NextEngine* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return;
        }

        Assets::Camera camera;
        FRemoteClientView* clientView = nullptr;
        {
            std::lock_guard lock(cloudViewsMutex_);
            for (auto& [_, candidate] : cloudViews_)
            {
                if (candidate.viewIndex == viewIndex)
                {
                    clientView = &candidate;
                    camera = BuildClientCamera(candidate);
                    break;
                }
            }
            if (clientView == nullptr)
            {
                return;
            }
            if (!EnsureClientCompositeTarget(*clientView, engine->GetRenderer(), view.RenderExtent()))
            {
                return;
            }
            CopyViewToComposite(*clientView, commandBuffer, engine->GetRenderer(), view);
            RenderClientUiToComposite(*clientView, commandBuffer, imageIndex, engine->GetRenderer(), camera);
            if (clientView->compositeImage)
            {
                cloudVideoPipelines_[viewIndex]->RecordFrameFromStorageImage(
                    commandBuffer,
                    imageIndex,
                    engine->GetRenderer(),
                    *clientView->compositeImage,
                    clientView->compositeBindlessSlot,
                    clientView->compositeExtent,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_SHADER_READ_BIT);
            }
            return;
        }
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
