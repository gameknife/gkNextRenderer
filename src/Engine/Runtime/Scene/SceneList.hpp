#pragma once

#include "Engine/Assets/AssetsFwd.hpp"

#include <filesystem>
#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Runtime::Scene
{

class SceneList final
{
public:
    static void ScanScenes();
    static int32_t AddExternalScene(std::string absPath);
    static bool IsSupportedSceneExtension(std::string_view extension);
    static bool IsSupportedScenePath(const std::filesystem::path& path);
    static std::vector<std::string> SupportedSceneExtensions();
    static std::vector<std::string> AllScenes;

	static bool LoadScene(std::string filename, Assets::EnvironmentSetting& camera, std::vector< std::shared_ptr<Assets::Node> >& nodes, std::vector<Assets::Model>& models,
                     std::vector<Assets::FMaterial>& materials,
                     std::vector<Assets::LightObject>& lights,
                     std::vector<Assets::AnimationTrack>& tracks,
                     std::vector<Assets::Skeleton>& skeletons,
                     std::vector<Assets::FGaussianSplatData>* splats = nullptr);
    static std::shared_ptr<Assets::Node> AddSceneReferenceToScene(
        Assets::Scene& scene, const std::string& assetPath,
        const glm::vec3& translation = glm::vec3(0.0f));
};

}
