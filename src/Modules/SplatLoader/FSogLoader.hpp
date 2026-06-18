#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Core/GaussianSplat.hpp"

namespace Assets
{
    class FSogLoader final
    {
    public:
        static bool Load(const std::string& filename, EnvironmentSetting& camera,
                         std::vector<std::shared_ptr<Node>>& nodes, std::vector<Model>& models,
                         std::vector<FMaterial>& materials, std::vector<LightObject>& lights,
                         std::vector<AnimationTrack>& tracks, std::vector<Skeleton>& skeletons,
                         std::vector<FGaussianSplatData>& splats);
    };
}
