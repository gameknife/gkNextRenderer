#include "Battle/RangedCombatSystem.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace NextTotalwar
{
    void FRangedCombatSystem::Reset(uint64_t seed)
    {
        rng_ = FDeterministicRng(seed ^ 0xa76d34e5ULL);
        volleyCount_ = 0;
        casualtyCount_ = 0;
    }

    int FRangedCombatSystem::FindTarget(const FRegiment& attacker,
                                        const std::vector<FRegiment>& regiments) const
    {
        if (attacker.orderTargetRegiment >= 0 &&
            static_cast<size_t>(attacker.orderTargetRegiment) < regiments.size())
        {
            const FRegiment& ordered = regiments[static_cast<size_t>(attacker.orderTargetRegiment)];
            if (ordered.faction != attacker.faction && IsRegimentSelectable(ordered))
            {
                const float distance = glm::length(glm::vec2(ordered.anchor.x - attacker.anchor.x,
                                                              ordered.anchor.z - attacker.anchor.z));
                if (distance >= attacker.def->rangedMinRange &&
                    distance <= attacker.def->rangedRange)
                    return ordered.id;
            }
        }
        if (!attacker.fireAtWill) return -1;
        float nearest = std::numeric_limits<float>::max();
        int target = -1;
        for (const FRegiment& candidate : regiments)
        {
            if (candidate.faction == attacker.faction || !IsRegimentSelectable(candidate)) continue;
            const float distance = glm::length(glm::vec2(candidate.anchor.x - attacker.anchor.x,
                                                          candidate.anchor.z - attacker.anchor.z));
            if (distance >= attacker.def->rangedMinRange &&
                distance <= attacker.def->rangedRange && distance < nearest)
            {
                nearest = distance;
                target = candidate.id;
            }
        }
        return target;
    }

    int FRangedCombatSystem::PickAliveSoldier(FRegiment& regiment)
    {
        if (regiment.strength <= 0) return -1;
        const size_t start = static_cast<size_t>(rng_.NextU32()) % regiment.soldiers.size();
        for (size_t offset = 0; offset < regiment.soldiers.size(); ++offset)
        {
            const size_t index = (start + offset) % regiment.soldiers.size();
            const FSoldier& soldier = regiment.soldiers[index];
            if (soldier.health > 0 && soldier.combatState != ESoldierState::Dying &&
                soldier.combatState != ESoldierState::Dead)
                return static_cast<int>(index);
        }
        return -1;
    }

    bool FRangedCombatSystem::HasLineOfFire(const FRegiment& attacker, const FRegiment& target,
                                            const FBattleContext* context) const
    {
        if (!context || !context->sampleGround) return true;
        constexpr int samples = 7;
        for (int index = 1; index < samples; ++index)
        {
            const float t = static_cast<float>(index) / static_cast<float>(samples);
            const glm::vec3 point = glm::mix(attacker.anchor, target.anchor, t);
            const float arcHeight = 1.8f + std::sin(t * 3.14159265f) * 9.0f;
            const float projectileHeight = point.y + arcHeight;
            if (context->sampleGround(point.x, point.z) + 1.0f > projectileHeight) return false;
        }
        return true;
    }

    void FRangedCombatSystem::Tick(float deltaSeconds, std::vector<FRegiment>& regiments,
                                   FBattleState& battleState, const FRangedCombatTuning& tuning,
                                   const FBattleContext* context)
    {
        for (size_t attackerIndex = 0; attackerIndex < regiments.size(); ++attackerIndex)
        {
            FRegiment& attacker = regiments[attackerIndex];
            if (!attacker.def || !attacker.def->canRangedAttack || attacker.ammo <= 0 ||
                attacker.strength <= 0 || attacker.state == ERegimentState::Destroyed ||
                attacker.state == ERegimentState::Routing || attacker.state == ERegimentState::Engaged)
            {
                continue;
            }
            attacker.volleyTimer = std::max(0.0f, attacker.volleyTimer - deltaSeconds);
            if (attacker.volleyTimer > 0.0f) continue;
            const int targetId = FindTarget(attacker, regiments);
            if (targetId < 0 || static_cast<size_t>(targetId) >= regiments.size()) continue;
            FRegiment& target = regiments[static_cast<size_t>(targetId)];
            if (!HasLineOfFire(attacker, target, context)) continue;
            const float distance = glm::length(glm::vec2(target.anchor.x - attacker.anchor.x,
                                                          target.anchor.z - attacker.anchor.z));
            const float distanceRatio = glm::clamp(distance / std::max(attacker.def->rangedRange, 1.0f),
                                                   0.0f, 1.0f);
            const float hitChance = glm::clamp(attacker.def->rangedAccuracy -
                                                   distanceRatio * tuning.distanceAccuracyPenalty,
                                               0.05f, 0.85f);
            const int arrows = std::max(1, attacker.strength /
                std::max(1, tuning.arrowsPerVolleyDivisor));
            int hits = 0;
            int deaths = 0;
            for (int arrow = 0; arrow < arrows && target.strength > 0; ++arrow)
            {
                if (!rng_.Chance(hitChance)) continue;
                const int soldierIndex = PickAliveSoldier(target);
                if (soldierIndex < 0) break;
                FSoldier& soldier = target.soldiers[static_cast<size_t>(soldierIndex)];
                soldier.health -= static_cast<int16_t>(std::max(1.0f, attacker.def->rangedDamage));
                ++hits;
                FCombatEvent hit{ECombatEventType::Hit, static_cast<int16_t>(target.id),
                                 static_cast<int16_t>(soldierIndex), soldier.position, soldier.yaw,
                                 static_cast<int16_t>(attacker.id), -1};
                battleState.events.push_back(hit);
                if (soldier.health > 0) continue;
                soldier.health = 0;
                soldier.combatState = ESoldierState::Dying;
                soldier.deathTimer = tuning.deathClipSeconds;
                soldier.targetRegiment = -1;
                soldier.targetSoldier = -1;
                soldier.engagementSlot = -1;
                target.strength = std::max(0, target.strength - 1);
                ++attacker.kills;
                ++deaths;
                ++casualtyCount_;
                battleState.events.push_back({ECombatEventType::Death,
                                              static_cast<int16_t>(target.id),
                                              static_cast<int16_t>(soldierIndex),
                                              soldier.position, soldier.yaw,
                                              static_cast<int16_t>(attacker.id), -1});
            }
            target.suppression = glm::clamp(target.suppression +
                                                static_cast<float>(hits) * tuning.suppressionPerHit,
                                            0.0f, 1.0f);
            --attacker.ammo;
            attacker.volleyTimer = std::max(attacker.def->volleyInterval, 0.1f);
            ++volleyCount_;
            battleState.events.push_back({ECombatEventType::Volley,
                                          static_cast<int16_t>(target.id), -1,
                                          target.anchor, target.facing,
                                          static_cast<int16_t>(attacker.id),
                                          static_cast<int16_t>(deaths)});
            if (target.strength == 0)
            {
                target.state = ERegimentState::Destroyed;
                target.moraleState = EMoraleState::Eliminated;
                target.selected = false;
                battleState.events.push_back({ECombatEventType::RegimentDestroyed,
                                              static_cast<int16_t>(target.id), -1,
                                              target.anchor});
            }
        }
    }
}
