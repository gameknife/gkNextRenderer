#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"
#include "Modules/ScadLoader/FScadLoader.h"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"

namespace Modules::Scad
{
    void Register()
    {
        Assets::FLoaderRegistry::Get().RegisterSceneLoader(
            {".scad"},
            [](const std::string& filename, Assets::EnvironmentSetting& camera,
               std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
               std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights,
               std::vector<Assets::AnimationTrack>& tracks, std::vector<Assets::Skeleton>& skeletons)
            {
                Assets::ScadLoadOptions options;
                if (NextEngine* engine = NextEngine::GetInstance())
                {
                    options.scadToWorldScale = engine->GetUserSettings().ScadToWorldScale;
                }
                return Assets::FScadLoader::LoadScadScene(
                    filename, camera, nodes, models, materials, lights, tracks, skeletons, options);
            });
    }
}
