#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"

class NextCharacterController;

namespace Runtime
{
    void DrawPhysicsDebugOverlay(const Assets::Scene& scene, const Assets::Camera& camera);
    void DrawCharacterControllerDebugOverlay(const NextCharacterController& controller, const Assets::Camera& camera);
}
