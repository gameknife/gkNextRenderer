#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/ext.hpp>

namespace Assets
{
    class Node;
}

namespace Brotato3D
{
    struct FPlayerStats
    {
        float maxHpFlat = 50.0f;
        float damagePct = 0.0f;
        float damageFlat = 0.0f;
        float atkSpeedPct = 0.0f;
        float rangePct = 0.0f;
        float moveSpeedPct = 0.0f;
        float pickupRadiusPct = 0.0f;
        float critChancePct = 0.05f;
        float critMultiplier = 2.0f;
        int maxHpFlatBonus = 0;
        int dashChargeBonus = 0;
    };

    struct FPlayerRuntime
    {
        glm::vec3 worldPos = glm::vec3(0.0f, 0.5f, 0.0f);
        glm::vec3 facingDir = glm::vec3(0.0f, 0.0f, -1.0f);
        float radius = 0.4f;
        int currentHp = 50;
        int maxHp = 50;
        int currentXp = 0;
        int materials = 0;
        int level = 1;
        int pendingLevelUps = 0;
        float dashCooldownMs = 0.0f;
        float dashRemainingMs = 0.0f;
        float dashDurationMs = 0.0f;
        int dashCharges = 3;
        glm::vec3 dashDir = glm::vec3(0.0f);
        FPlayerStats stats;
        std::vector<std::string> ownedItemIds;
        std::shared_ptr<::Assets::Node> bodyNode;
        std::shared_ptr<::Assets::Node> facingNode;
        std::shared_ptr<::Assets::Node> smgWeaponNode;
        std::shared_ptr<::Assets::Node> shotgunWeaponNode;
    };
}
