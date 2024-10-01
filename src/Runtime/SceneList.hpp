#pragma once

#include "Utilities/Glm.hpp"
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>

namespace Assets
{
    class Node;
    class Model;
    class Texture;
    struct Material;
    struct LightObject;
    struct CameraInitialSate;

    typedef std::unordered_map<std::string, std::string> uo_string_string_t;

    const uo_string_string_t sceneNames =
    {
    	{"qx50.glb",            "Qx50"},
    	{"track.glb",           "LowpolyTrack"},
    	{"simple.glb",          "Simple"},
    	{"complex.glb",         "Complex"},
    	{"livingroom.glb",      "LivingRoom"},
    	{"kitchen.glb",         "Kitchen"},
    	{"luxball.glb",         "LuxBall"},
    	{"still.glb",			   "Still"}
    };
}


typedef std::pair<std::string, std::function<void (Assets::CameraInitialSate&,
                                                             std::vector<Assets::Node>& nodes,
                                                             std::vector<Assets::Model>&,
                                                             std::vector<Assets::Material>&,
                                                             std::vector<Assets::LightObject>&)>> scenes_pair;

class SceneList final
{
public:
    static void ScanScenes();
    static int32_t AddExternalScene(std::string absPath);
    
    static std::vector<scenes_pair> AllScenes;
};