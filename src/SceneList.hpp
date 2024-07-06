#pragma once

#include "Utilities/Glm.hpp"
#include <functional>
#include <string>
#include <vector>

namespace Assets
{
    class Node;
    class Model;
    class Texture;
    struct Material;
    struct LightObject;
    struct CameraInitialSate;
}


typedef std::pair<std::string, std::function<void (Assets::CameraInitialSate&,
                                                             std::vector<Assets::Node>& nodes,
                                                             std::vector<Assets::Model>&, std::vector<Assets::Texture>&,
                                                             std::vector<Assets::Material>&,
                                                             std::vector<Assets::LightObject>&)>> scenes_pair;

class SceneList final
{
public:
    static void ScanScenes();
    static int32_t AddExternalScene(std::string absPath);
    
    static std::vector<scenes_pair> AllScenes;
};