#include "Engine/Common/CoreMinimal.hpp"
#include "MinimalRenderer.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"

namespace
{
    constexpr std::string_view minimalSceneName = "Minimal.proc";

    void BuildMinimalScene(Assets::EnvironmentSetting& environment,
                           std::vector<std::shared_ptr<Assets::Node>>& nodes,
                           std::vector<Assets::Model>& models,
                           std::vector<Assets::FMaterial>& materials,
                           std::vector<Assets::LightObject>& lights,
                           std::vector<Assets::AnimationTrack>&)
    {
        Assets::Camera camera;
        camera.name = "MainCamera";
        camera.ModelView = glm::lookAt(glm::vec3(0.0f, 2.78f, 10.78f),
                                       glm::vec3(0.0f, 2.78f, 0.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
        camera.FieldOfView = 40.0f;
        environment.cameras.push_back(camera);
        environment.GammaCorrection = true;
        environment.HasSky = false;
        environment.HasSun = false;

        const uint32_t firstMaterial = static_cast<uint32_t>(materials.size());
        const uint32_t modelIndex = static_cast<uint32_t>(
            Assets::FProcModel::CreateCornellBox(5.55f, models, materials, lights));
        nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
            "CornellBox", glm::vec3(0.0f), glm::vec3(1.0f), 0, modelIndex,
            std::array<uint32_t, 16>{firstMaterial, firstMaterial + 1, firstMaterial + 2, firstMaterial + 3}));
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                         Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    Assets::FLoaderRegistry::Get().RegisterProcScene(minimalSceneName, BuildMinimalScene);
    return std::make_unique<FMinimalRenderer>(config, options, engine);
}

FMinimalRenderer::FMinimalRenderer(Vulkan::WindowConfig& config,
                                   Runtime::Config::Options& options,
                                   NextEngine* engine)
    : NextGameInstanceBase(config, options, engine),
      sceneName_(options.SceneName.empty() ? minimalSceneName : options.SceneName)
{
    ConfigureWindow(config, options, "gkNext Minimal Renderer", 1280, 720, false);
}

void FMinimalRenderer::OnInit()
{
    GetEngine().RequestLoadScene({.filename = sceneName_});
}
