#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Subsystems/NextPhysicsTypes.h"

#include <glm/ext.hpp>

namespace Assets
{
    class Node;
}

namespace Brotato3D
{
    enum class EPickupKind : uint8_t
    {
        XP,
    };

    struct FPickupRuntime
    {
        EPickupKind kind = EPickupKind::XP;
        NextBodyID bodyId{};
        glm::vec3 worldPos = glm::vec3(0.0f);
        int value = 0;
        bool active = false;
        bool magnetized = false;
        float settleTimerMs = 0.0f;
        std::shared_ptr<Assets::Node> node;
    };
}
