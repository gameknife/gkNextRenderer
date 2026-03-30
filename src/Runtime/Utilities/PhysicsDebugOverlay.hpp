#pragma once

#include "Common/CoreMinimal.hpp"

namespace Assets
{
    class Scene;
    struct Camera;
}

namespace Runtime
{
    void DrawPhysicsDebugOverlay(const Assets::Scene& scene, const Assets::Camera& camera);
}
