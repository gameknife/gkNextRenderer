#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/AssetsFwd.hpp"

class NextCharacterController;

namespace NextGameplay
{
    void DrawCharacterControllerDebugOverlay(const NextCharacterController& controller, const Assets::Camera& camera);
}
