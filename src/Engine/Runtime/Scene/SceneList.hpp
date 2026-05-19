#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Assets
{
    class Node;
    class Model;
    class Texture;
    struct Material;
    struct FMaterial;
    struct LightObject;
	struct AnimationTrack;
    struct EnvironmentSetting;
    struct Skeleton;
}

class SceneList final
{
public:
    static void ScanScenes();
    static int32_t AddExternalScene(std::string absPath);
    static bool IsSupportedSceneExtension(std::string_view extension);
    static bool IsSupportedScenePath(const std::filesystem::path& path);
    static std::span<const std::string_view> SupportedSceneExtensions();
    static std::vector<std::string> AllScenes;

	static bool LoadScene(std::string filename, Assets::EnvironmentSetting& camera, std::vector< std::shared_ptr<Assets::Node> >& nodes, std::vector<Assets::Model>& models,
                     std::vector<Assets::FMaterial>& materials,
                     std::vector<Assets::LightObject>& lights,
                     std::vector<Assets::AnimationTrack>& tracks,
                     std::vector<Assets::Skeleton>& skeletons);
};
