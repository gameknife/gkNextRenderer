#include "Render/CombatFx.h"

#include "Engine/Assets/Core/Node.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>

namespace NextTotalwar
{
    void FCombatFx::Tick(float deltaSeconds, std::vector<FRegiment>& regiments,
                         std::vector<std::vector<FSoldierVisual>>& visuals,
                         const std::vector<FCombatEvent>& events)
    {
        for (const FCombatEvent& event : events)
        {
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
            soldier.position = event.worldPos;
            soldier.yaw = event.yaw;
            visual.animator.Play("die", 0.05f);
            if (visual.worldNode)
            {
                visual.worldNode->Translation() = soldier.position;
                visual.worldNode->Rotation() =
                    glm::angleAxis(soldier.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            }
        }

        corpseCount_ = 0;
        for (size_t regimentIndex = 0; regimentIndex < regiments.size(); ++regimentIndex)
        {
            FRegiment& regiment = regiments[regimentIndex];
            for (size_t soldierIndex = 0; soldierIndex < regiment.soldiers.size(); ++soldierIndex)
            {
                FSoldier& soldier = regiment.soldiers[soldierIndex];
                if (soldier.combatState == ESoldierState::Dead)
                {
                    ++corpseCount_;
                    continue;
                }
                if (soldier.combatState != ESoldierState::Dying) continue;

                FSoldierVisual& visual = visuals[regimentIndex][soldierIndex];
                soldier.deathTimer = std::max(0.0f, soldier.deathTimer - deltaSeconds);
                visual.animator.Play("die", 0.05f);
                visual.animator.Update(deltaSeconds);
                if (soldier.deathTimer <= 0.0f)
                {
                    soldier.combatState = ESoldierState::Dead;
                    ++corpseCount_;
                }
            }
        }
    }
}
