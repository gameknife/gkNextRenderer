#include "Modules/SceneContent/SceneContentModule.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/SceneContent.hpp"
#include "Modules/SceneContent/SceneList.hpp"

namespace
{
    class FSceneContentService final : public Runtime::Scene::ISceneContentService
    {
    public:
        bool IsSupportedScenePath(const std::filesystem::path& path) const override
        {
            return Runtime::Scene::SceneList::IsSupportedScenePath(path);
        }

        bool LoadScene(
            std::string filename,
            Assets::EnvironmentSetting& environment,
            std::vector<std::shared_ptr<Assets::Node>>& nodes,
            std::vector<Assets::Model>& models,
            std::vector<Assets::FMaterial>& materials,
            std::vector<Assets::LightObject>& lights,
            std::vector<Assets::AnimationTrack>& tracks,
            std::vector<Assets::Skeleton>& skeletons) override
        {
            return Runtime::Scene::SceneList::LoadScene(
                std::move(filename), environment, nodes, models, materials, lights, tracks, skeletons);
        }

        std::shared_ptr<Assets::Node> AddSceneReference(
            Assets::Scene& scene,
            const std::string& assetPath,
            const glm::vec3& translation) override
        {
            return Runtime::Scene::SceneList::AddSceneReferenceToScene(scene, assetPath, translation);
        }
    };
}

namespace Modules::SceneContent
{
    void Install(NextEngine& engine)
    {
        Runtime::Scene::SceneList::ScanScenes();
        engine.SetSceneContentService(std::make_unique<FSceneContentService>());
    }
}
