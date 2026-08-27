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

    // World-space AABB of everything drawable. Returns false when the scene has
    // no drawable model, leaving the outputs untouched.
    bool SceneWorldBounds(const std::vector<std::shared_ptr<Node>>& nodes,
                          const std::vector<Model>& models,
                          glm::vec3& outMin,
                          glm::vec3& outMax);

    // Pushes an authored camera's far plane out far enough to contain the
    // scene. Only ever extends it: a camera that already reaches past the
    // bounds keeps whatever the author asked for.
    void ExtendCameraFarPlane(Camera& camera, const glm::vec3& boundsMin, const glm::vec3& boundsMax);
}
