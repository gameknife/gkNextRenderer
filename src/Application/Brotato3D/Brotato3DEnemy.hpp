#pragma once

#include "Common/CoreMinimal.hpp"
#include "Brotato3DDataLoader.hpp"

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
        uint32_t modelId = 0;
        uint32_t materialId = 0;
        uint32_t darkMaterialId = 0;
        uint32_t hitFlashMaterialId = 0;
        std::shared_ptr<Assets::Node> node;
    };
}
