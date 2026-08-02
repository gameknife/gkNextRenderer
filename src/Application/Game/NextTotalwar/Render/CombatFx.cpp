#include "Render/CombatFx.h"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>

namespace
{
    uint32_t MixBits(uint32_t value)
    {
        value ^= value >> 16U;
        value *= 0x7feb352dU;
        value ^= value >> 15U;
        value *= 0x846ca68bU;
        return value ^ (value >> 16U);
    }

    float UnitFloat(uint32_t value)
    {
        return static_cast<float>(MixBits(value) >> 8U) * (1.0f / 16777216.0f);
    }
}

namespace NextTotalwar
{
    void FCombatFx::Initialize(uint32_t flashMaterialId,
                               std::vector<std::shared_ptr<Assets::Node>> bloodPool)
    {
        Reset();
        flashMaterialId_ = flashMaterialId;
        bloodPool_ = std::move(bloodPool);
        for (const std::shared_ptr<Assets::Node>& node : bloodPool_)
        {
            Assets::NodeUtils::SetVisible(node, false);
        }
    }

    void FCombatFx::Reset()
    {
        for (const std::shared_ptr<Assets::Node>& node : bloodPool_)
        {
            Assets::NodeUtils::SetVisible(node, false);
        }
        bloodPool_.clear();
        nextBloodSlot_ = 0;
        bloodSpawnCount_ = 0;
        corpseCount_ = 0;
        flashingCount_ = 0;
        bloodStainCount_ = 0;
    }

    void FCombatFx::ResetBattle()
    {
        for (const std::shared_ptr<Assets::Node>& node : bloodPool_)
        {
            Assets::NodeUtils::SetVisible(node, false);
        }
        nextBloodSlot_ = 0;
        bloodSpawnCount_ = 0;
        corpseCount_ = 0;
        flashingCount_ = 0;
        bloodStainCount_ = 0;
    }

    void FCombatFx::SetFlashing(FSoldierVisual& visual, bool flashing) const
    {
        if (visual.flashApplied == flashing) return;

        for (size_t index = 0; index < visual.renderNodes.size(); ++index)
        {
            Assets::Node* node = visual.renderNodes[index];
            if (!node) continue;
            Runtime::RenderComponent* render =
                node->GetComponentPtr<Runtime::RenderComponent>();
            if (!render) continue;

            if (flashing)
            {
                std::array<uint32_t, 16> flashMaterials{};
                flashMaterials.fill(flashMaterialId_);
                render->SetMaterials(flashMaterials);
            }
            else if (index < visual.baseMaterials.size())
            {
                render->SetMaterials(visual.baseMaterials[index]);
            }
        }
        visual.flashApplied = flashing;
    }

    void FCombatFx::SpawnBloodStain(const FCombatEvent& event)
    {
        if (bloodPool_.empty()) return;

        const size_t slotIndex = nextBloodSlot_;
        nextBloodSlot_ = (nextBloodSlot_ + 1) % bloodPool_.size();
        std::shared_ptr<Assets::Node>& node = bloodPool_[slotIndex];
        if (!node) return;

        const uint32_t seed =
            bloodSpawnCount_++ ^
            (static_cast<uint32_t>(static_cast<uint16_t>(event.regiment)) << 16U) ^
            static_cast<uint32_t>(static_cast<uint16_t>(event.soldier));
        const float yaw = UnitFloat(seed) * glm::two_pi<float>();
        const float scale = 0.8f + UnitFloat(seed ^ 0x9e3779b9U) * 0.6f;
        node->Translation() = event.worldPos + glm::vec3(0.0f, 0.02f, 0.0f);
        node->Rotation() = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        node->Scale() = glm::vec3(scale, 1.0f, scale);
        node->RecalcTransform(true);
        Assets::NodeUtils::SetVisible(node, true);
        bloodStainCount_ =
            std::min(static_cast<int>(bloodPool_.size()), bloodStainCount_ + 1);
    }

    void FCombatFx::Tick(float deltaSeconds, std::vector<FRegiment>& regiments,
                         std::vector<std::vector<FSoldierVisual>>& visuals,
                         const FCombatTuning& tuning,
                         const std::vector<FCombatEvent>& events)
    {
        for (FRegiment& regiment : regiments)
        {
            for (FSoldier& soldier : regiment.soldiers)
            {
                soldier.flashTimer = std::max(0.0f, soldier.flashTimer - deltaSeconds);
            }
        }

        auto flashSoldier = [&](int16_t regimentIndex, int16_t soldierIndex, float duration)
        {
            if (duration <= 0.0f || regimentIndex < 0 || soldierIndex < 0 ||
                static_cast<size_t>(regimentIndex) >= regiments.size() ||
                static_cast<size_t>(regimentIndex) >= visuals.size() ||
                static_cast<size_t>(soldierIndex) >= regiments[regimentIndex].soldiers.size() ||
                static_cast<size_t>(soldierIndex) >= visuals[regimentIndex].size())
            {
                return;
            }
            FSoldier& soldier = regiments[regimentIndex].soldiers[soldierIndex];
            if (soldier.combatState == ESoldierState::Dying ||
                soldier.combatState == ESoldierState::Dead)
            {
                return;
            }
            soldier.flashTimer = std::max(soldier.flashTimer, duration);
        };

        for (const FCombatEvent& event : events)
        {
            if (event.type == ECombatEventType::Hit)
            {
                flashSoldier(event.regiment, event.soldier, tuning.flashSeconds);
                flashSoldier(event.sourceRegiment, event.sourceSoldier, tuning.attackerFlash);
                continue;
            }

            if (event.type != ECombatEventType::Death ||
                event.regiment < 0 || event.soldier < 0 ||
                static_cast<size_t>(event.regiment) >= regiments.size() ||
                static_cast<size_t>(event.regiment) >= visuals.size() ||
                static_cast<size_t>(event.soldier) >= regiments[event.regiment].soldiers.size() ||
                static_cast<size_t>(event.soldier) >= visuals[event.regiment].size())
            {
                continue;
            }

            FSoldier& soldier = regiments[event.regiment].soldiers[event.soldier];
            FSoldierVisual& visual = visuals[event.regiment][event.soldier];
            soldier.flashTimer = 0.0f;
            SetFlashing(visual, false);
            soldier.position = event.worldPos;
            soldier.yaw = event.yaw;
            visual.animator.Play("die", 0.05f);
            if (visual.worldNode)
            {
                visual.worldNode->Translation() = soldier.position;
                visual.worldNode->Rotation() =
                    glm::angleAxis(soldier.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            }
            SpawnBloodStain(event);
        }

        corpseCount_ = 0;
        flashingCount_ = 0;
        for (size_t regimentIndex = 0; regimentIndex < regiments.size(); ++regimentIndex)
        {
            FRegiment& regiment = regiments[regimentIndex];
            for (size_t soldierIndex = 0; soldierIndex < regiment.soldiers.size(); ++soldierIndex)
            {
                FSoldier& soldier = regiment.soldiers[soldierIndex];
                FSoldierVisual& visual = visuals[regimentIndex][soldierIndex];
                if (soldier.combatState == ESoldierState::Dead)
                {
                    soldier.flashTimer = 0.0f;
                    SetFlashing(visual, false);
                    ++corpseCount_;
                    continue;
                }
                if (soldier.combatState == ESoldierState::Dying)
                {
                    soldier.flashTimer = 0.0f;
                    SetFlashing(visual, false);
                    soldier.deathTimer = std::max(0.0f, soldier.deathTimer - deltaSeconds);
                    visual.animator.Play("die", 0.05f);
                    visual.animator.Update(deltaSeconds);
                    if (soldier.deathTimer <= 0.0f)
                    {
                        soldier.combatState = ESoldierState::Dead;
                        ++corpseCount_;
                    }
                    continue;
                }

                const bool flashing = soldier.flashTimer > 0.0f;
                SetFlashing(visual, flashing);
                if (flashing) ++flashingCount_;
            }
        }
    }
}
