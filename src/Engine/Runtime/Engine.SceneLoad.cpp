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
#include "Engine/Runtime/Interface/DebugUiProvider.hpp"
#include "Engine/Runtime/Interface/UiOverlay.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.h"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.h"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Runtime/Profiling/FrameProfiler.hpp"
#include "Engine/Utilities/Localization.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <fmt/format.h>

namespace
{
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

void NextEngine::PrepareRendererForSceneMutation()
{
    renderer_->OnPreLoadScene();
}

void NextEngine::CommitSceneToRenderer(const SceneRendererSyncOptions& options)
{
    if (options.rebuildMeshBuffer)
    {
        scene_->RebuildMeshBuffer(renderer_->CommandPool(), renderer_->SupportsRayTracing());
    }

    if (options.setRendererScene)
    {
        renderer_->SetScene(scene_);
    }

    if (options.resetFrameCounter)
    {
        frameState_.totalFrames = 0;
    }

    if (options.postLoadRenderer)
    {
        renderer_->OnPostLoadScene();
        OnRendererPostLoadScene();
    }

    if (options.refreshSwapChainResources)
    {
        if (renderer_->HasSwapChain())
        {
            renderer_->RefreshSceneSwapChainResources();
        }
        else if (options.createSwapChainIfMissing)
        {
            renderer_->CreateSwapChain();
        }
    }
}

void NextEngine::RequestSceneGpuRefresh()
{
    AddTickedTask(
        [this](double /*deltaSeconds*/) -> bool
        {
            if (status_ != NextRenderer::EApplicationStatus::Running)
            {
                return false;
            }

            renderer_->Device().WaitIdle();
            PrepareRendererForSceneMutation();
            CommitSceneToRenderer({.createSwapChainIfMissing = false});
            return true;
        });
}

void NextEngine::RequestAddSceneReference(std::string assetPath, glm::vec3 translation)
{
    AddTickedTask(
        [this, assetPath = std::move(assetPath), translation](double /*deltaSeconds*/) -> bool
        {
            if (status_ != NextRenderer::EApplicationStatus::Running)
            {
                return false;
            }

            status_ = NextRenderer::EApplicationStatus::Loading;
            const auto timer = std::chrono::high_resolution_clock::now();
            const bool canRefreshExistingSwapChain = renderer_->HasSwapChain();

            renderer_->Device().WaitIdle();
            PrepareRendererForSceneMutation();

            auto proxy = Runtime::Scene::SceneList::AddSceneReferenceToScene(*scene_, assetPath, translation);
            if (proxy)
            {
                scene_->SetSelectedId(proxy->GetInstanceId());
                CommitSceneToRenderer({.createSwapChainIfMissing = !canRefreshExistingSwapChain});

                const float elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
                                          std::chrono::high_resolution_clock::now() - timer)
                                          .count();
                SPDLOG_INFO("added scene reference [{}] to gpu in {:.2f}ms",
                            std::filesystem::path(assetPath).filename().string(), elapsed * 1000.f);
            }

            status_ = NextRenderer::EApplicationStatus::Running;
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
            SceneTaskContext taskContext{};
            task.GetContext(taskContext);
            if (taskContext.success)
            {
                SPDLOG_INFO("{}", taskContext.outputInfo.data());

                const bool canRefreshExistingSwapChain = renderer_->HasSwapChain();
                renderer_->Device().WaitIdle();
                if (!canRefreshExistingSwapChain)
                {
                    renderer_->DeleteSwapChain();
                }

                // Execute the specific GPU load logic
                onGpuLoad(ctx);

                CommitSceneToRenderer({.rebuildMeshBuffer = false,
                                       .setRendererScene = false,
                                       .createSwapChainIfMissing = !canRefreshExistingSwapChain});
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

            PrepareRendererForSceneMutation();
            gameInstance_->BeforeSceneRebuild(*ctx.nodes, *ctx.models, *ctx.materials, *ctx.lights, *ctx.tracks);

            if (!request.append)
            {
                gameInstance_->OnSceneUnloaded();
                services_.physics->OnSceneStarted();

                scene_->Reload(*ctx.nodes, *ctx.models, *ctx.materials, *ctx.lights, *ctx.tracks);
                scene_->SetEnvSettings(*ctx.cameraState);
                scene_->SetGaussianSplats(std::move(*ctx.splats));
                scene_->PostLoad(*ctx.skeletons);
                CommitSceneToRenderer({.resetFrameCounter = false,
                                       .postLoadRenderer = false,
                                       .refreshSwapChainResources = false});

                config_.userSettings.CameraIdx = 0;
                assert(!scene_->GetEnvSettings().cameras.empty());
                scene_->SetRenderCamera(scene_->GetEnvSettings().cameras[0]);
                gameInstance_->OnSceneLoaded();
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
                CommitSceneToRenderer({.resetFrameCounter = false,
                                       .postLoadRenderer = false,
                                       .refreshSwapChainResources = false});
            }

            const float elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
                                      std::chrono::high_resolution_clock::now() - timer)
                                      .count();
            SPDLOG_INFO("uploaded scene [{}] to gpu in {:.2f}ms",
                        std::filesystem::path(request.filename).filename().string(), elapsed * 1000.f);
        });
}

void NextEngine::InitPhysics() {}
