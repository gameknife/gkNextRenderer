#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Data/Skeleton.hpp"

namespace Assets
{
    class FSceneLoader
    {
    public:
        static Camera AutoFocusCamera(Assets::EnvironmentSetting& cameraInit, std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Model>& models);
        
        static bool LoadGLTFScene(const std::string& filename, Assets::EnvironmentSetting& cameraInit, std::vector< std::shared_ptr<Assets::Node> >& nodes,
                                  std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks, std::vector<Assets::Skeleton>& skeletons);

        // Load only animation tracks from a glTF/GLB file (discards mesh/material/node data)
        static bool LoadAnimationTracks(const std::string& filename,
                                        std::vector<Assets::AnimationTrack>& outTracks);

        static void GenerateMikkTSpace(Assets::Model *m);

        static std::string currSceneName;
    };

}
