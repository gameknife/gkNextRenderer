#pragma once

#include "Common/CoreMinimal.hpp"
#include "KongLie3DDataLoader.hpp"

namespace Assets
{
    class Node;
}

namespace KongLie3D
{
    struct FPieceRuntime
    {
        std::string pieceId;
        FPieceDef baseDef;
        FPieceDef def;
        int currentHp = 0;
        int currentMana = 0;
        int col = 0;
        int row = 0;
        int initialCol = 0;
        int initialRow = 0;
        bool alive = true;
        bool onBench = false;
        bool initialOnBench = false;
        std::shared_ptr<Assets::Node> node;
        uint32_t modelId = 0;
        uint32_t materialId = 0;
        uint32_t darkMaterialId = 0;
        glm::vec3 dimensions = glm::vec3(0.0f);
        float visualScale = 1.0f;
        float attackCooldownMs = 0.0f;
        float healCooldownMs = 0.0f;
        float moveCooldownMs = 0.0f;
        glm::vec3 prevWorldPos = glm::vec3(0.0f);
        glm::vec3 targetWorldPos = glm::vec3(0.0f);
        float moveDurationMs = 0.0f;
        float moveElapsedMs = 0.0f;
        float wCooldownMs = 0.0f;
        float ultimateCooldownMs = 0.0f;
        bool ultimateUsed = false;
        int shield = 0;
        float shieldTimerMs = 0.0f;
        float stunTimerMs = 0.0f;
        float furyTimerMs = 0.0f;
        float deathAnimationMs = 0.0f;
        glm::vec3 deathStartWorldPos = glm::vec3(0.0f);
        int statDmgAD = 0;
        int statDmgAP = 0;
        int statDmgTaken = 0;
        int statHeal = 0;

        int ApplyDamage(int damage)
        {
            const int incomingDamage = std::max(0, damage);
            int shieldAbsorbed = 0;
            if (shield > 0 && shieldTimerMs > 0.0f)
            {
                shieldAbsorbed = std::min(shield, incomingDamage);
                shield -= shieldAbsorbed;
                if (shield <= 0)
                {
                    shield = 0;
                    shieldTimerMs = 0.0f;
                }
            }

            const int hpDamage = incomingDamage - shieldAbsorbed;
            currentHp -= hpDamage;
            statDmgTaken += hpDamage;
            return hpDamage;
        }

        bool IsStunned() const { return stunTimerMs > 0.0f; }
        float GetAttackSpeedMultiplier() const { return furyTimerMs > 0.0f ? 3.0f : 1.0f; }
        float GetDamageMultiplier() const { return furyTimerMs > 0.0f ? 1.5f : 1.0f; }
    };
}
