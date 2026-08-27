#pragma once

#include "Modules/NextViture/VitureModule.hpp"

#include <functional>

namespace DevTools
{
    struct FVitureDebugPanelData
    {
        const Modules::Viture::IHeadPoseTracker* tracker = nullptr;
        const Modules::Viture::FHeadPose* pose = nullptr;
        const glm::vec3* cameraEulerDegrees = nullptr;
        bool sixDof = true;
        float worldUnitsPerMeter = 1.0f;
        float predictionMs = 20.0f;
        float pollHz = 25.0f;
        float smoothingHz = 0.0f;
        std::function<bool()> recenter;
        std::function<bool()> restart;
    };

    void DrawVitureDebugPanel(bool& visible, const FVitureDebugPanelData& data, float topOffset = 0.0f);
}
