#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/LDrawLoader/LDrawModule.hpp"
#include "Modules/LDrawLoader/FLDrawLoader.h"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"

namespace Modules::LDraw
{
    void Register()
    {
        Assets::FLoaderRegistry::Get().RegisterSceneLoader(
            {".ldr", ".mpd"},
            [](const std::string& filename, Assets::EnvironmentSetting& camera,
               std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
               std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights,
               std::vector<Assets::AnimationTrack>& tracks, std::vector<Assets::Skeleton>& skeletons,
               std::vector<Assets::FGaussianSplatData>&)
            {
                Assets::LDrawLoadOptions options;
                if (NextEngine* engine = NextEngine::GetInstance())
                {
                    options.lduToWorldScale = engine->GetUserSettings().LDrawLduToWorldScale;
                }
                return Assets::FLDrawLoader::LoadLDrawScene(
                    filename, camera, nodes, models, materials, lights, tracks, skeletons, options);
            });
    }
}
