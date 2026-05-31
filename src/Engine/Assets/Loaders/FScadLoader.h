#pragma once

// ============================================================================
// FScadLoader.h - Public entry point for loading OpenSCAD (.scad) scenes.
//
// Mirrors FLDrawLoader::LoadLDrawScene. Resolves the use/include closure,
// evaluates the program into a SCAD user-module scene graph, rebuilds the
// parent/child Node hierarchy, and merges each logical module node's direct
// geometry into as few render meshes as the material-slot limit allows.
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
