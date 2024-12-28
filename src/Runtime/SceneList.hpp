#pragma once

#include <memory>
#include <string>
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
}

class SceneList final
{
public:
    static void ScanScenes();
    static int32_t AddExternalScene(std::string absPath);
    static std::vector<std::string> AllScenes;

	static bool LoadScene(std::string filename, Assets::EnvironmentSetting& camera, std::vector< std::shared_ptr<Assets::Node> >& nodes, std::vector<Assets::Model>& models,
                     std::vector<Assets::FMaterial>& materials,
                     std::vector<Assets::LightObject>& lights,
                     std::vector<Assets::AnimationTrack>& tracks);
};