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

class SceneList final
{
public:
    static const std::vector<std::pair<std::string, std::function<void (Assets::CameraInitialSate&,
                                                                        std::vector<Assets::Node>& nodes,
                                                                        std::vector<Assets::Model>&,
                                                                        std::vector<Assets::Texture>&,
                                                                        std::vector<Assets::Material>&,
                                                                        std::vector<Assets::LightObject>&)>>> AllScenes;
};
