#pragma once

// ============================================================================
// FScadLoader.h - Public entry point for loading OpenSCAD (.scad) scenes.
//
// Mirrors FLDrawLoader::LoadLDrawScene. Resolves the use/include closure,
// evaluates the program, groups geometry by color, and emits one Model + one
// single-material Node per color.
// ============================================================================

#include <memory>
#include <string>
#include <vector>

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Data/Skeleton.hpp"
#include "Engine/Assets/Loaders/FScadTypes.h"

namespace Assets
{
    class FScadLoader
    {
    public:
        static bool LoadScadScene(
            const std::string& filename,
            EnvironmentSetting& cameraInit,
            std::vector<std::shared_ptr<Node>>& nodes,
            std::vector<Model>& models,
            std::vector<FMaterial>& materials,
            std::vector<LightObject>& lights,
            std::vector<AnimationTrack>& tracks,
            std::vector<Skeleton>& skeletons,
            const ScadLoadOptions& options = {});
    };
} // namespace Assets
