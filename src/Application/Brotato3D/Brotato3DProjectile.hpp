#pragma once

#include "Common/CoreMinimal.hpp"

#include <glm/ext.hpp>

namespace Assets
{
    class Node;
}

namespace Brotato3D
{
    struct FProjectileRuntime
    {
        std::string weaponId;
        glm::vec3 worldPos = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        float remainingLifetimeMs = 0.0f;
        int damage = 0;
        float radius = 0.12f;
        uint32_t modelId = 0;
        uint32_t materialId = 0;
        std::shared_ptr<Assets::Node> node;
        bool active = false;
    };

    struct FImpactDebrisRuntime
    {
        glm::vec3 worldPos = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        float lifeMs = 400.0f;
        float remainingMs = 0.0f;
        std::shared_ptr<Assets::Node> node;
        bool active = false;
    };
}
