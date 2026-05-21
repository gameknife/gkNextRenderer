#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/ext.hpp>
#include <unordered_set>

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
        glm::vec3 lastWorldPos = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::vec3 color = glm::vec3(1.0f);
        float remainingLifetimeMs = 0.0f;
        float elapsedMs = 0.0f;
        int damage = 0;
        float radius = 0.12f;
        int pierceRemaining = 0;
        float explosionRadius = 0.0f;
        int explosionDamage = 0;
        float knockbackMeters = 0.0f;
        bool isCrit = false;
        std::unordered_set<size_t> hitEnemyIndices;
        uint32_t modelId = 0;
        uint32_t materialId = 0;
        std::shared_ptr<::Assets::Node> node;
        bool active = false;
    };

    struct FExpandingRing
    {
        glm::vec3 worldPos = glm::vec3(0.0f);
        glm::vec4 color = glm::vec4(1.0f);
        float durationMs = 300.0f;
        float remainingMs = 0.0f;
        float maxRadius = 1.0f;
    };

    struct FLaserBeam
    {
        glm::vec3 from = glm::vec3(0.0f);
        glm::vec3 to = glm::vec3(0.0f);
        glm::vec4 color = glm::vec4(0.3f, 0.95f, 1.0f, 1.0f);
        float durationMs = 100.0f;
        float remainingMs = 0.0f;
        float width = 0.18f;
    };

    enum class EGroundIndicatorShape : uint8_t
    {
        Circle,
        Strip,
    };

    struct FGroundIndicator
    {
        EGroundIndicatorShape shape = EGroundIndicatorShape::Circle;
        glm::vec3 worldPos = glm::vec3(0.0f);
        glm::vec3 endPos = glm::vec3(0.0f);
        float radius = 0.0f;
        float width = 0.0f;
        glm::vec4 color = glm::vec4(1.0f, 0.18f, 0.10f, 1.0f);
        float totalMs = 0.0f;
        float remainingMs = 0.0f;
        uint32_t enemyTag = 0;
    };

    struct FEnemyProjectileRuntime
    {
        glm::vec3 worldPos = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        float remainingLifetimeMs = 0.0f;
        int damage = 0;
        float radius = 0.18f;
        std::shared_ptr<::Assets::Node> node;
        bool active = false;
    };

}
