#pragma once

#include "Common/CoreMinimal.hpp"

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
        Material,
    };

    struct FPickupRuntime
    {
        EPickupKind kind = EPickupKind::XP;
        glm::vec3 worldPos = glm::vec3(0.0f);
        int value = 0;
        bool active = false;
        bool magnetized = false;
        std::shared_ptr<Assets::Node> node;
    };
}
