// NextEngine scene loading pipeline: load requests, async parse task and
// GPU upload. Split from Engine.cpp; same class, separate TU.
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/GaussianSplat.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/RemoteProtocol.hpp"
#include "Engine/Runtime/DebugUiProvider.hpp"
#include "Engine/Runtime/UiOverlay.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Command/DeleteNodesCommand.hpp"
#include "Engine/Runtime/Command/DuplicateNodesCommand.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Runtime/Subsystems/NextAudio.h"
#include "Engine/Runtime/Subsystems/NextPhysics.h"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.h"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Engine/Utilities/Localization.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <fmt/format.h>
#include <spdlog/stopwatch.h>

namespace
{
    bool ShouldLogStartupProfile()
    {
        return GOption != nullptr && GOption->AgentValidation;
    }

    struct SceneTaskContext
    {
        bool success;
        float elapsed;
        std::array<char, 256> outputInfo;
    };
}

void NextEngine::RequestLoadScene(FSceneLoadRequest request)
{
    AddTickedTask(
        [this, request = std::move(request)](double deltaSeconds) -> bool
        {
            if (status_ != NextRenderer::EApplicationStatus::Running)
            {
                return false;
            }

            LoadScene(request);
            return true;
        });
}

void NextEngine::LaunchLoadSceneTask(std::string sceneFileName, std::function<void(SceneLoadContext&)> onGpuLoad)
{
    // wait all task finish
    Tasks::TaskCoordinator::GetInstance()->CancelAllParralledTasks();
    Tasks::TaskCoordinator::GetInstance()->WaitForAllParralledTask();

    status_ = NextRenderer::EApplicationStatus::Loading;

    SceneLoadContext ctx;
    ctx.models = std::make_shared<std::vector<Assets::Model>>();
    ctx.nodes = std::make_shared<std::vector<std::shared_ptr<Assets::Node>>>();
    ctx.materials = std::make_shared<std::vector<Assets::FMaterial>>();
    ctx.lights = std::make_shared<std::vector<Assets::LightObject>>();
    ctx.tracks = std::make_shared<std::vector<Assets::AnimationTrack>>();
    ctx.skeletons = std::make_shared<std::vector<Assets::Skeleton>>();
    ctx.splats = std::make_shared<std::vector<Assets::FGaussianSplatData>>();
    ctx.cameraState = std::make_shared<Assets::EnvironmentSetting>();

    // dispatch in thread task and reset in main thread
    Tasks::TaskCoordinator::GetInstance()->AddTask(
        [ctx, sceneFileName](Tasks::ResTask& task)
        {
            SceneTaskContext taskContext{};
            const auto timer = std::chrono::high_resolution_clock::now();

            taskContext.success = Runtime::Scene::SceneList::LoadScene(sceneFileName, *ctx.cameraState, *ctx.nodes, *ctx.models,
                                                       *ctx.materials, *ctx.lights, *ctx.tracks, *ctx.skeletons,
                                                       ctx.splats.get());

            taskContext.elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
                                      std::chrono::high_resolution_clock::now() - timer)
                                      .count();

            std::string info =
                fmt::format("parsed scene [{}] on cpu in {:.2f}ms",
                            std::filesystem::path(sceneFileName).filename().string(), taskContext.elapsed * 1000.f);
            std::copy(info.begin(), info.end(), taskContext.outputInfo.data());
            task.SetContext(taskContext);
        },
        [this, ctx, sceneFileName, onGpuLoad](Tasks::ResTask& task) mutable
        {
            spdlog::stopwatch profileTimer;
            auto logProfile = [&profileTimer](const char* label)
            {
                if (ShouldLogStartupProfile())
                {
                    SPDLOG_INFO("[StartupProfile]   SceneLoad main {:<36} {}", label, profileTimer.elapsed_ms());
                }
            };

            SceneTaskContext taskContext{};
            task.GetContext(taskContext);
            logProfile("task context fetched");
            if (taskContext.success)
            {
                SPDLOG_INFO("{}", taskContext.outputInfo.data());

                renderer_->Device().WaitIdle();
                logProfile("device idle before reload");
                renderer_->DeleteSwapChain();
                logProfile("old swapchain deleted");

                // Execute the specific GPU load logic
                onGpuLoad(ctx);
                logProfile("scene gpu load callback");

                frameState_.totalFrames = 0;
                renderer_->OnPostLoadScene();
                logProfile("renderer post-load scene");
                renderer_->CreateSwapChain();
                logProfile("new swapchain created");
            }
            else
            {
                SPDLOG_ERROR("failed to load scene [{}]", std::filesystem::path(sceneFileName).filename().string());
            }

            status_ = NextRenderer::EApplicationStatus::Running;
        },
        1);
}

void NextEngine::LoadScene(const FSceneLoadRequest& request)
{
    if (!request.append)
    {
        scene_->CleanUp();
        services_.physics->OnSceneDestroyed();
        Assets::GlobalTexturePool::GetInstance()->FreeTransientTextures();
    }

    LaunchLoadSceneTask(
        request.filename,
        [this, request](SceneLoadContext& ctx)
        {
            const auto timer = std::chrono::high_resolution_clock::now();
            spdlog::stopwatch profileTimer;
            auto logProfile = [&profileTimer](const char* label)
            {
                if (ShouldLogStartupProfile())
                {
                    SPDLOG_INFO("[StartupProfile]   SceneLoad gpu {:<37} {}", label, profileTimer.elapsed_ms());
                }
            };

            renderer_->OnPreLoadScene();
            logProfile("renderer pre-load scene");
            gameInstance_->BeforeSceneRebuild(*ctx.nodes, *ctx.models, *ctx.materials, *ctx.lights, *ctx.tracks);
            logProfile("game before rebuild");

            if (!request.append)
            {
                scene_->GetEnvSettings().Reset();
                scene_->SetEnvSettings(*ctx.cameraState);
                gameInstance_->OnSceneUnloaded();
                services_.physics->OnSceneStarted();
                logProfile("scene services reset");

                scene_->Reload(*ctx.nodes, *ctx.models, *ctx.materials, *ctx.lights, *ctx.tracks);
                scene_->SetGaussianSplats(std::move(*ctx.splats));
                scene_->PostLoad(*ctx.skeletons);
                logProfile("scene cpu structures rebuilt");
                scene_->RebuildMeshBuffer(renderer_->CommandPool(), renderer_->SupportsRayTracing());
                logProfile("mesh gpu buffers rebuilt");
                renderer_->SetScene(scene_);
                logProfile("renderer scene set");

                config_.userSettings.CameraIdx = 0;
                assert(!scene_->GetEnvSettings().cameras.empty());
                scene_->SetRenderCamera(scene_->GetEnvSettings().cameras[0]);
                gameInstance_->OnSceneLoaded();
                logProfile("game scene loaded");
            }
            else
            {
                std::string name = std::filesystem::path(request.filename).stem().string();
                std::shared_ptr<Assets::Node> rootNode =
                    scene_->Append(name, *ctx.nodes, *ctx.models, *ctx.materials, *ctx.lights, *ctx.tracks,
                                   *ctx.skeletons);
                if (request.placeOnHit && rootNode)
                {
                    rootNode->SetTranslation(request.hitPosition);
                }
                scene_->RebuildMeshBuffer(renderer_->CommandPool(), renderer_->SupportsRayTracing());
                logProfile("append mesh gpu buffers rebuilt");
                renderer_->SetScene(scene_);
                logProfile("append renderer scene set");
            }

            const float elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
                                      std::chrono::high_resolution_clock::now() - timer)
                                      .count();
            SPDLOG_INFO("uploaded scene [{}] to gpu in {:.2f}ms",
                        std::filesystem::path(request.filename).filename().string(), elapsed * 1000.f);
        });
}

void NextEngine::InitPhysics() {}
