#pragma once

#include "Engine/Assets/AssetsFwd.hpp"

#include <filesystem>
#include <glm/vec3.hpp>

namespace Runtime::Scene
{
    class ISceneContentService
    {
    public:
        virtual ~ISceneContentService() = default;
        virtual bool IsSupportedScenePath(const std::filesystem::path& path) const = 0;
        virtual bool LoadScene(
            std::string filename,
            Assets::EnvironmentSetting& environment,
            std::vector<std::shared_ptr<Assets::Node>>& nodes,
            std::vector<Assets::Model>& models,
            std::vector<Assets::FMaterial>& materials,
            std::vector<Assets::LightObject>& lights,
            std::vector<Assets::AnimationTrack>& tracks,
            std::vector<Assets::Skeleton>& skeletons) = 0;
        virtual std::shared_ptr<Assets::Node> AddSceneReference(
            Assets::Scene& scene,
            const std::string& assetPath,
            const glm::vec3& translation) = 0;
    };
}
