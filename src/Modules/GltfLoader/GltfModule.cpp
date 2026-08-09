#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"
#include "Modules/GltfLoader/FSceneLoader.h"
#include "Modules/GltfLoader/GltfModule.hpp"

namespace Modules::Gltf
{
    void Register()
    {
        Assets::FLoaderRegistry::Get().RegisterSceneLoader(
            {".glb", ".gltf"},
            [](const std::string& filename, Assets::EnvironmentSetting& camera,
               std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
               std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights,
               std::vector<Assets::AnimationTrack>& tracks, std::vector<Assets::Skeleton>& skeletons)
            {
                return Assets::FSceneLoader::LoadGLTFScene(
                    filename, camera, nodes, models, materials, lights, tracks, skeletons);
            });
    }
}
