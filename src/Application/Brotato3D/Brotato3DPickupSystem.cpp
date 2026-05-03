#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include <spdlog/spdlog.h>

#include "Brotato3DAudio.hpp"

using namespace Brotato3DUtil;

void Brotato3DGameInstance::SpawnPickup(int value, Brotato3D::EPickupKind kind, const glm::vec3& worldPos)
{
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
        HideNode(slot->node);
    }

    slot->kind = kind;
    slot->worldPos = glm::vec3(worldPos.x, 0.15f, worldPos.z);
    slot->value = value;
    slot->active = true;
    slot->magnetized = false;
    if (slot->node)
    {
        const uint32_t modelId = kind == Brotato3D::EPickupKind::XP ? pickupXpModelId_ : pickupMaterialModelId_;
        const uint32_t materialId =
            kind == Brotato3D::EPickupKind::XP ? pickupXpMaterialId_ : pickupMaterialMaterialId_;
        if (auto render = slot->node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetModelId(modelId);
            render->SetMaterial({materialId});
        }
        SetNodeTranslation(slot->node, slot->worldPos);
        ShowNode(slot->node);
    }
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
        const float pickupRadius = PickupBaseRadius * (1.0f + effectiveStats.pickupRadiusPct);
        if (DistanceXZ(pickup.worldPos, player_.worldPos) < pickupRadius)
        {
            pickup.magnetized = true;
        }
        if (pickup.magnetized)
        {
            pickup.worldPos = glm::mix(pickup.worldPos, player_.worldPos, std::min(1.0f, 8.0f * static_cast<float>(deltaSeconds)));
            pickup.worldPos.y = 0.15f;
            SetNodeTranslation(pickup.node, pickup.worldPos);
        }
        if (DistanceXZ(pickup.worldPos, player_.worldPos) < 0.4f)
        {
            if (pickup.kind == Brotato3D::EPickupKind::XP)
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
            }
            else
            {
                Brotato3D::PlayPickupMaterialSfx();
                player_.materials += pickup.value;
                totalMaterialsGained_ += pickup.value;
                PushFloatingText(player_.worldPos + glm::vec3(0.0f, 0.2f, 0.0f), fmt::format("+{} MAT", pickup.value), glm::vec4(1.0f, 0.85f, 0.15f, 1.0f),
                                 500.0f);
                spdlog::info("[Brotato3D] Materials {}", player_.materials);
            }
            pickup.active = false;
            pickup.magnetized = false;
            HideNode(pickup.node);
        }
    }
}

