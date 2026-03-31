#pragma once

#include "Common/CoreMinimal.hpp"

namespace Assets
{
    class Scene;
    struct Camera;
}

class NextCharacterController;

namespace Runtime
{
    void DrawPhysicsDebugOverlay(const Assets::Scene& scene, const Assets::Camera& camera);
    void DrawCharacterControllerDebugOverlay(const NextCharacterController& controller, const Assets::Camera& camera);
}
