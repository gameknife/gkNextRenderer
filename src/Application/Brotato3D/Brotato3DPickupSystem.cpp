#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include <spdlog/spdlog.h>

#include "Assets/Core/Node.h"
#include "Brotato3DAudio.hpp"
#include "Runtime/Components/RenderComponent.h"

using namespace Brotato3DUtil;

namespace
{
    constexpr float PickupPhysicsSettleMs = 600.0f;

    void HidePickupBody(NextPhysics* physics, Brotato3D::FPickupRuntime& pickup)
    {
        if (physics && !pickup.bodyId.IsInvalid())
        {
            physics->SetBodyActive(pickup.bodyId, false);
            physics->SetBodyTransform(pickup.bodyId, HiddenPosition, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
            physics->SetBodyVelocity(pickup.bodyId, glm::vec3(0.0f), glm::vec3(0.0f));
        }
    }
}

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

    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    HidePickupBody(physics, *slot);

    slot->kind = kind;
    slot->worldPos = glm::vec3(worldPos.x, std::max(PickupXpRadius + 0.05f, worldPos.y + 0.25f), worldPos.z);
    slot->value = value;
    slot->active = true;
    slot->magnetized = false;
    slot->settleTimerMs = PickupPhysicsSettleMs;
    if (slot->node)
    {
        if (auto render = slot->node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetModelId(pickupXpModelId_);
        }
        NodeUtils::SetOutlineFlags(slot->node, Runtime::RenderOutlineFlags::hovered);
        NodeUtils::SetPrimaryMaterial(slot->node, pickupXpMaterialId_);
        slot->node->SetTranslation(slot->worldPos);
        slot->node->SetRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        NodeUtils::SetVisible(slot->node, true);
    }
    if (physics && !slot->bodyId.IsInvalid())
    {
        std::uniform_real_distribution<float> horizontalDist(-1.0f, 1.0f);
        std::uniform_real_distribution<float> speedDist(3.0f, 5.0f);
        std::uniform_real_distribution<float> angularDist(-1.0f, 1.0f);
        glm::vec3 impulseDir(horizontalDist(rng_), 0.8f, horizontalDist(rng_));
        if (glm::length(impulseDir) < 0.001f)
        {
            impulseDir = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        impulseDir = glm::normalize(impulseDir);
        glm::vec3 angularVelocity(angularDist(rng_), angularDist(rng_), angularDist(rng_));
        if (glm::length(angularVelocity) < 0.001f)
        {
            angularVelocity = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        angularVelocity = glm::normalize(angularVelocity) * 10.0f;

        physics->SetBodyTransform(slot->bodyId, slot->worldPos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
        physics->SetBodyVelocity(slot->bodyId, impulseDir * speedDist(rng_), angularVelocity);
        physics->SetBodyActive(slot->bodyId, true);
    }
}

void Brotato3DGameInstance::UpdatePickups(double deltaSeconds)
{
    const Brotato3D::FPlayerStats effectiveStats = GetEffectiveStats();
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    for (auto& pickup : pickupPool_)
    {
        if (!pickup.active)
        {
            continue;
        }

        glm::vec3 currentPos = pickup.worldPos;
        glm::quat currentRot(1.0f, 0.0f, 0.0f, 0.0f);
        if (physics && !pickup.bodyId.IsInvalid())
        {
            if (const FNextPhysicsBody* body = physics->GetBody(pickup.bodyId))
            {
                currentPos = body->position;
                currentRot = body->rotation;
            }
        }
        pickup.worldPos = currentPos;

        if (!pickup.magnetized)
        {
            pickup.settleTimerMs = std::max(0.0f, pickup.settleTimerMs - deltaMs);
            if (pickup.node)
            {
                pickup.node->SetTranslation(pickup.worldPos);
                pickup.node->SetRotation(currentRot);
            }
        }

        const float pickupRadius = PickupBaseRadius * (1.0f + effectiveStats.pickupRadiusPct);
        if (!pickup.magnetized && DistanceXZ(pickup.worldPos, player_.worldPos) < pickupRadius)
        {
            pickup.magnetized = true;
            if (physics && !pickup.bodyId.IsInvalid())
            {
                physics->SetBodyVelocity(pickup.bodyId, glm::vec3(0.0f), glm::vec3(0.0f));
                physics->SetBodyActive(pickup.bodyId, false);
            }
        }

        if (pickup.magnetized)
        {
            pickup.worldPos = glm::mix(pickup.worldPos, player_.worldPos, std::min(1.0f, 8.0f * static_cast<float>(deltaSeconds)));
            pickup.worldPos.y = PickupXpRadius + 0.03f;
            if (physics && !pickup.bodyId.IsInvalid())
            {
                physics->SetBodyTransform(pickup.bodyId, pickup.worldPos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
            }
            if (pickup.node)
            {
                pickup.node->SetTranslation(pickup.worldPos);
            }
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
            pickup.settleTimerMs = 0.0f;
            HidePickupBody(physics, pickup);
            NodeUtils::SetVisible(pickup.node, false);
        }
    }
}


