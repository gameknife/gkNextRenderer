#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"
#include "Modules/SplatLoader/FSogLoader.hpp"
#include "Modules/SplatLoader/SplatModule.hpp"

namespace Modules::Splat
{
    void Register()
    {
        Assets::FLoaderRegistry::Get().RegisterSceneLoader(
            {".sog"},
            [](const std::string& filename, Assets::EnvironmentSetting& camera,
               std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
               std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights,
               std::vector<Assets::AnimationTrack>& tracks, std::vector<Assets::Skeleton>& skeletons,
               std::vector<Assets::FGaussianSplatData>& splats)
            {
                return Assets::FSogLoader::Load(
                    filename, camera, nodes, models, materials, lights, tracks, skeletons, splats);
            });
    }
}
