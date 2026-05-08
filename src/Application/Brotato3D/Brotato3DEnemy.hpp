#pragma once

#include "Common/CoreMinimal.hpp"
#include "Brotato3DDataLoader.hpp"
#include "Runtime/Subsystems/NextPhysicsTypes.h"

namespace Assets
{
    class Node;
}

namespace Brotato3D
{
    struct FEnemyRuntime
    {
        const FEnemyDef* def = nullptr;
        glm::vec3 worldPos = glm::vec3(0.0f);
        float radius = 0.3f;
        int currentHp = 0;
        int maxHp = 0;
        bool alive = false;
        bool fading = false;
        float hitFlashRemainingMs = 0.0f;
        float deathFadeMs = 0.0f;
        float contactCooldownMs = 0.0f;
        float rangedFireCooldownMs = 0.0f;
        float chargeRampMs = 0.0f;
        float chargeCooldownMs = 0.0f;
        bool charging = false;
        float bombFuseMs = -1.0f;
        float healIntervalMs = 0.0f;
        bool bossPhase2Active = false;
        glm::vec3 lastHitDebrisDir = glm::vec3(0.0f, 0.0f, 1.0f);
        uint32_t modelId = 0;
        uint32_t materialId = 0;
        uint32_t darkMaterialId = 0;
        uint32_t hitFlashMaterialId = 0;
        uint32_t warningMaterialId = 0;
        uint32_t phase2MaterialId = 0;
        NextBodyID kinematicBodyId{};
        bool kinematicBodyActive = false;
        std::shared_ptr<Assets::Node> node;
    };
}
