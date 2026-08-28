// NextEngine scene loading pipeline: load requests, async parse task and
// GPU upload. Split from Engine.cpp; same class, separate TU.
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextRig.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/RemoteProtocol.hpp"
#include "Engine/Runtime/Interface/DebugUiProvider.hpp"
#include "Engine/Runtime/Interface/UiOverlay.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Interface/UserInterface.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Utilities/LogFormatting.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Runtime/Profiling/ProfilerMacros.hpp"
#include "Engine/Runtime/Profiling/TracyIntegration.hpp"
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

    bool LoadRegisteredScene(
        const std::string& filename,
        Assets::EnvironmentSetting& environment,
        std::vector<std::shared_ptr<Assets::Node>>& nodes,
        std::vector<Assets::Model>& models,
        std::vector<Assets::FMaterial>& materials,
        std::vector<Assets::LightObject>& lights,
        std::vector<Assets::AnimationTrack>& tracks,
        std::vector<Assets::Skeleton>& skeletons)
    {
        materials.push_back({Assets::Material::Lambertian(glm::vec3(0.73f)), "root_default"});
        std::string extension = std::filesystem::path(filename).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });

        Assets::FLoaderRegistry& registry = Assets::FLoaderRegistry::Get();
        if (extension == ".proc")
        {
            if (const Assets::FProcSceneFn* build = registry.FindProcScene(filename))
            {
                (*build)(environment, nodes, models, materials, lights, tracks);
                return true;
            }
        }
        else if (const Assets::FSceneLoaderFn* load = registry.FindSceneLoader(extension))
        {
            return (*load)(filename, environment, nodes, models, materials, lights, tracks, skeletons);
        }

        SPDLOG_ERROR("No registered scene loader for '{}'; install SceneContent for catalog/reference support",
                     filename);
        return false;
    }
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

            if (!sceneContent_)
            {
                SPDLOG_ERROR("Scene content service is not installed");
                status_ = NextRenderer::EApplicationStatus::Running;
                return true;
            }
            auto proxy = sceneContent_->AddSceneReference(*scene_, assetPath, translation);
            if (proxy)
            {
                scene_->SetSelectedId(proxy->GetInstanceId());
                CommitSceneToRenderer({.createSwapChainIfMissing = !canRefreshExistingSwapChain});

                const float elapsedMs =
                    std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - timer)
                        .count();
                const Assets::SceneRebuildProfile& rebuild = scene_->LastRebuildProfile();
                const float outsideRebuildMs = std::max(0.0f, elapsedMs - rebuild.totalMs);
                SPDLOG_INFO(
                    "committed scene reference [{}] in {:.2f}ms (GPU resource build {:.2f}ms; "
                    "physics {:.2f}ms; mesh/scene CPU {:.2f}ms; reference/renderer other {:.2f}ms)",
                    std::filesystem::path(assetPath).filename().string(), elapsedMs,
                    rebuild.gpuResourceBuildMs,
                    rebuild.physicsShapeCookingMs + rebuild.physicsBodyCreationMs,
                    rebuild.cpuPreparationMs, outsideRebuildMs);
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
    ctx.cameraState = std::make_shared<Assets::EnvironmentSetting>();

    // dispatch in thread task and reset in main thread
    Tasks::TaskCoordinator::GetInstance()->AddTask(
        [ctx, sceneFileName, sceneContent = sceneContent_.get()](Tasks::ResTask& task)
        {
            SceneTaskContext taskContext{};
            const auto timer = std::chrono::high_resolution_clock::now();

            taskContext.success = sceneContent
                ? sceneContent->LoadScene(
                      sceneFileName, *ctx.cameraState, *ctx.nodes, *ctx.models,
                      *ctx.materials, *ctx.lights, *ctx.tracks, *ctx.skeletons)
                : LoadRegisteredScene(
                      sceneFileName, *ctx.cameraState, *ctx.nodes, *ctx.models,
                      *ctx.materials, *ctx.lights, *ctx.tracks, *ctx.skeletons);

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
        1,
        "Scene load");
}

void NextEngine::LoadScene(const FSceneLoadRequest& request)
{
    if (!request.append)
    {
        scene_->CleanUp();
        if (services_.physics)
        {
            services_.physics->OnSceneDestroyed();
        }
        Assets::GlobalTexturePool::GetInstance()->FreeTransientTextures();
    }

    LaunchLoadSceneTask(
        request.filename,
        [this, request](SceneLoadContext& ctx)
        {
            const auto timer = std::chrono::high_resolution_clock::now();
            
            Tasks::TaskCoordinator::GetInstance()->WaitForAllTasks();
            PrepareRendererForSceneMutation();

            // Before the game declares anything, not after: rig pools are declared from inside
            // BeforeSceneRebuild (that is where the models vector exists), and OnSceneUnloaded —
            // the intuitive place to drop them — runs *after* it and would throw the new
            // declarations away with the old world's.
            if (services_.rig && !request.append)
            {
                services_.rig->Clear();
            }

            gameInstance_->BeforeSceneRebuild(*ctx.nodes, *ctx.models, *ctx.materials, *ctx.lights, *ctx.tracks);

            if (!request.append)
            {
                gameInstance_->OnSceneUnloaded();
                if (services_.physics)
                {
                    services_.physics->OnSceneStarted();
                }

                scene_->Reload(*ctx.nodes, *ctx.models, *ctx.materials, *ctx.lights, *ctx.tracks);
                scene_->GetEnvSettings() = *ctx.cameraState;
                scene_->PostLoad(*ctx.skeletons);
                CommitSceneToRenderer({.resetFrameCounter = false,
                                       .postLoadRenderer = false,
                                       .refreshSwapChainResources = false});

                config_.userSettings.CameraIdx = 0;
                assert(!scene_->GetEnvSettings().cameras.empty());
                scene_->GetRenderCamera() = scene_->GetEnvSettings().cameras[0];

                // Instantiated before the game's own hook, so a game can acquire characters from
                // inside OnSceneLoaded — which is the only place it has node ids to work with.
                if (services_.rig)
                {
                    services_.rig->OnSceneLoaded(*scene_);
                }
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

            const float elapsedMs =
                std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - timer).count();
            const Assets::SceneRebuildProfile& rebuild = scene_->LastRebuildProfile();
            const float outsideRebuildMs = std::max(0.0f, elapsedMs - rebuild.totalMs);
            SPDLOG_INFO(
                "committed scene [{}] in {:.2f}ms (GPU resource build {:.2f}ms; physics {:.2f}ms: "
                "shape cooking {:.2f}ms, body creation {:.2f}ms; mesh/scene CPU {:.2f}ms; callbacks/other {:.2f}ms)",
                std::filesystem::path(request.filename).filename().string(), elapsedMs,
                rebuild.gpuResourceBuildMs,
                rebuild.physicsShapeCookingMs + rebuild.physicsBodyCreationMs,
                rebuild.physicsShapeCookingMs, rebuild.physicsBodyCreationMs,
                rebuild.cpuPreparationMs, outsideRebuildMs);
            GkProfiling::Message(fmt::format("committed scene [{}]",
                                             std::filesystem::path(request.filename).filename().string()));

            // Process-level startup cost, reported once: everything from PlatformInit to the
            // first scene being renderable. Later scene loads are covered by the line above.
            static bool startupReported = false;
            if (!startupReported)
            {
                startupReported = true;
                GK_LOG_STAGE("---- Startup complete in {:.2f}ms (first scene [{}] ready)",
                             NextRenderer::GetMillisecondsSinceProcessStart(),
                             std::filesystem::path(request.filename).filename().string());
            }
        });
}

void NextEngine::InitPhysics() {}
