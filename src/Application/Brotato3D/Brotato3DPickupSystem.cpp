#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include <spdlog/spdlog.h>

#include "Assets/Core/Node.h"
#include "Brotato3DAudio.hpp"
#include "Runtime/Components/RenderComponent.h"

using namespace Brotato3DUtil;

void Brotato3DGameInstance::SpawnPickup(int value, Brotato3D::EPickupKind kind, const glm::vec3& worldPos)
{
    if (kind != Brotato3D::EPickupKind::XP)
    {
        spdlog::warn("[Brotato3D] Material pickup requested through XP pool; ignored");
        return;
    }
    if (value <= 0 || pickupPool_.empty())
    {
        return;
    }

    Brotato3D::FPickupRuntime* slot = nullptr;
    for (auto& pickup : pickupPool_)
    {
        if (!pickup.active && pickup.kind == kind)
        {
            slot = &pickup;
            break;
        }
    }
    if (!slot)
    {
        slot = &*std::max_element(pickupPool_.begin(), pickupPool_.end(),
                                  [this](const Brotato3D::FPickupRuntime& lhs,
                                         const Brotato3D::FPickupRuntime& rhs)
                                  {
                                      return DistanceXZ(lhs.worldPos, player_.worldPos) <
                                             DistanceXZ(rhs.worldPos, player_.worldPos);
                                  });
        NodeUtils::SetVisible(slot->node, false);
    }

    slot->kind = kind;
    slot->worldPos = glm::vec3(worldPos.x, 0.15f, worldPos.z);
    slot->value = value;
    slot->active = true;
    slot->magnetized = false;
    if (slot->node)
    {
        if (auto render = slot->node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetModelId(pickupXpModelId_);
        }
        NodeUtils::SetPrimaryMaterial(slot->node, pickupXpMaterialId_);
        slot->node->SetTranslation(slot->worldPos);
        NodeUtils::SetVisible(slot->node, true);
    }
    std::uniform_real_distribution<float> horizontalDist(-1.5f, 1.5f);
    slot->bouncePhysicsMs = 300.0f;
    slot->bounceVelocity = glm::vec3(horizontalDist(rng_), 4.0f, horizontalDist(rng_));
}

void Brotato3DGameInstance::UpdatePickups(double deltaSeconds)
{
    const Brotato3D::FPlayerStats effectiveStats = GetEffectiveStats();
    for (auto& pickup : pickupPool_)
    {
        if (!pickup.active)
        {
            continue;
        }
        if (pickup.bouncePhysicsMs > 0.0f)
        {
            pickup.bounceVelocity.y -= 12.0f * static_cast<float>(deltaSeconds);
            pickup.worldPos += pickup.bounceVelocity * static_cast<float>(deltaSeconds);
            pickup.worldPos.y = std::max(0.15f, pickup.worldPos.y);
            pickup.bouncePhysicsMs -= static_cast<float>(deltaSeconds * 1000.0);
            pickup.node->SetTranslation(pickup.worldPos);
            if (pickup.bouncePhysicsMs <= 0.0f)
            {
                pickup.bouncePhysicsMs = 0.0f;
                pickup.bounceVelocity = glm::vec3(0.0f);
            }
            continue;
        }
        const float pickupRadius = PickupBaseRadius * (1.0f + effectiveStats.pickupRadiusPct);
        if (DistanceXZ(pickup.worldPos, player_.worldPos) < pickupRadius)
        {
            pickup.magnetized = true;
        }
        if (pickup.magnetized)
        {
            pickup.worldPos = glm::mix(pickup.worldPos, player_.worldPos, std::min(1.0f, 8.0f * static_cast<float>(deltaSeconds)));
            pickup.worldPos.y = 0.15f;
            pickup.node->SetTranslation(pickup.worldPos);
        }
        if (DistanceXZ(pickup.worldPos, player_.worldPos) < 0.4f)
        {
            Brotato3D::PlayPickupXpSfx();
            player_.currentXp += pickup.value;
            PushFloatingText(player_.worldPos + glm::vec3(0.0f, 0.6f, 0.0f), fmt::format("+{} XP", pickup.value), glm::vec4(0.2f, 1.0f, 0.35f, 1.0f),
                             500.0f);
            while (player_.currentXp >= GetXpToNextLevel())
            {
                player_.currentXp -= GetXpToNextLevel();
                ++player_.level;
                ++player_.pendingLevelUps;
            }
            if (player_.pendingLevelUps > 0)
            {
                BeginLevelUp();
            }
            pickup.active = false;
            pickup.magnetized = false;
            pickup.bouncePhysicsMs = 0.0f;
            pickup.bounceVelocity = glm::vec3(0.0f);
            NodeUtils::SetVisible(pickup.node, false);
        }
    }
}


