#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/glm.hpp>

class Voyage3DGameInstance;

namespace Assets
{
    class Node;
}

namespace Voyage3D
{
    struct FProjectileRuntime
    {
        glm::vec3 worldPos = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        float lifetimeMs = 0.0f;
        bool fromPlayer = true;
        int damage = 8;
        bool active = false;
        std::shared_ptr<Assets::Node> node;
    };

    void UpdateCombat(Voyage3DGameInstance& gameInstance, double deltaSec);
}
