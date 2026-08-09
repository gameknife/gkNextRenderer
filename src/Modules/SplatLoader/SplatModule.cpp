#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"
#include "Engine/Rendering/ExternalPassRegistry.hpp"
#include "Modules/SplatLoader/FSogLoader.hpp"
#include "Modules/SplatLoader/GaussianSplatPass.hpp"
#include "Modules/SplatLoader/SplatModule.hpp"
#include "Modules/SplatLoader/SplatSettings.hpp"
#include "Modules/SplatLoader/GaussianSplatComponent.h"

namespace Modules::Splat
{
    void Install(NextEngine& engine)
    {
        InstallSettings(engine);
        Runtime::GaussianSplatComponent::RegisterReflection();
        Register();
    }

    void Register()
    {
        // Splat rendering runs as an external content pass right after the primary
        // view (priority 0, before debug overlay passes). Register is called per
        // application entry and per test, so guard the process-wide factory list.
        static bool passRegistered = false;
        if (!passRegistered)
        {
            Vulkan::RegisterExternalPassFactory(
                /*priority*/ 0,
                [](Vulkan::VulkanBaseRenderer& renderer) -> std::unique_ptr<Vulkan::IExternalRenderPass>
                { return std::make_unique<Vulkan::GaussianSplat::GaussianSplatPass>(renderer); });
            passRegistered = true;
        }

        Assets::FLoaderRegistry::Get().RegisterSceneLoader(
            {".sog"},
            [](const std::string& filename, Assets::EnvironmentSetting& camera,
               std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
               std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights,
               std::vector<Assets::AnimationTrack>& tracks, std::vector<Assets::Skeleton>& skeletons)
            {
                return Assets::FSogLoader::Load(
                    filename, camera, nodes, models, materials, lights, tracks, skeletons);
            });
    }
}
