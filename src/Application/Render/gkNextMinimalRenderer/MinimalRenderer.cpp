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
    constexpr std::string_view minimalSceneName = "CornellBox.proc";

    void BuildMinimalScene(Assets::EnvironmentSetting& environment,
                           std::vector<std::shared_ptr<Assets::Node>>& nodes,
                           std::vector<Assets::Model>& models,
                           std::vector<Assets::FMaterial>& materials,
                           std::vector<Assets::LightObject>& lights,
                           std::vector<Assets::AnimationTrack>&)
    {
        const uint32_t firstMaterial = static_cast<uint32_t>(materials.size());

        Assets::Camera camera;
        camera.name = "Cam";
        camera.ModelView = glm::lookAt(glm::vec3(0.0f, 2.78f, 10.78f),
                                       glm::vec3(0.0f, 2.78f, 0.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
        camera.FieldOfView = 40.0f;
        camera.Aperture = 0.0f;
        camera.FocalDistance = 10.0f;
        environment.cameras.push_back(camera);
        environment.ControlSpeed = 200.0f;
        environment.GammaCorrection = true;
        environment.HasSky = false;
        environment.HasSun = false;

        const uint32_t cboxModel = static_cast<uint32_t>(
            Assets::FProcModel::CreateCornellBox(5.55f, models, materials, lights));
        nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
            "CornellBox", glm::vec3(0.0f), glm::vec3(1.0f), static_cast<uint32_t>(nodes.size()), cboxModel,
            std::array<uint32_t, 16>{firstMaterial, firstMaterial + 1, firstMaterial + 2, firstMaterial + 3}));

        const glm::vec3 spherePosition(1.30f, 1.01f, 0.80f);
        const glm::vec3 boxPosition(-1.30f, 0.0f, -0.80f);

        materials.push_back({Assets::Material::Lambertian(glm::vec3(0.73f)), "cbox_white"});
        materials.push_back({Assets::Material::Mixture(glm::vec3(0.73f), 0.01f), "cball_white"});
        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.80f, 0.0f, -0.80f),
                                                       glm::vec3(0.80f, 1.60f, 0.80f)));
        models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 1.0f));

        nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
            "Sphere1", spherePosition, glm::vec3(1.0f), static_cast<uint32_t>(nodes.size()), cboxModel + 2,
            firstMaterial + 5, true, glm::quat(glm::vec3(0.0f, 0.5f, 0.0f))));
        nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
            "Box", boxPosition, glm::vec3(1.0f, 2.0f, 1.0f), static_cast<uint32_t>(nodes.size()), cboxModel + 1,
            firstMaterial + 4, true, glm::quat(glm::vec3(0.0f, 0.25f, 0.0f))));
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
    ConfigureWindow(config, options, "gkNext Minimal Renderer", 1920, 1080, false);
}

void FMinimalRenderer::OnInit()
{
    GetEngine().RequestLoadScene({.filename = sceneName_});
}
