#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Core/Model.hpp"

namespace Assets
{
    // Generates MikkTSpace tangents in place from the model's CPU vertices/indices.
    void GenerateMikkTSpace(Model* m);

    // Frames the scene bounds with a default camera; shared by scene loaders
    // that have no authored camera (glTF fallback, LDraw, SCAD).
    Camera AutoFocusCamera(EnvironmentSetting& cameraInit,
                           std::vector<std::shared_ptr<Node>>& nodes,
                           std::vector<Model>& models,
                           bool obliqueView = false);
}
