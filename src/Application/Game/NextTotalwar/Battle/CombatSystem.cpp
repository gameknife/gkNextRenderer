#include "Battle/CombatSystem.h"

#include "Battle/CombatModel.h"
#include "Battle/FormationLayout.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace NextTotalwar
{
    namespace
    {
        float ApproachRadius(const FCombatTuning& tuning)
        {
            // Front-line soldiers may leave their slots to close the final gap.
            // Acquisition must therefore happen before weapon/search range, or
            // two intact formations will wait forever until their OBBs overlap.
            return tuning.searchRadius + tuning.maxBreakDistance;
        }

        float RegimentAcquisitionRadius(size_t regimentIndex,
                                        const std::vector<FRegiment>& regiments,
                                        const FCombatTuning& tuning)
        {
            const FRegiment& regiment = regiments[regimentIndex];
            if (!regiment.def) return ApproachRadius(tuning);
            const glm::vec2 ownExtent = Formation::FormationHalfExtent(
                regiment.startStrength, regiment.ranks,
                regiment.def->fileSpacing, regiment.def->rankSpacing);
            float radius = ApproachRadius(tuning);
            for (const int16_t enemyIndex : regiment.engagedWith)
            {
                if (enemyIndex < 0 || static_cast<size_t>(enemyIndex) >= regiments.size()) continue;
                const FRegiment& enemy = regiments[enemyIndex];
                if (!enemy.def) continue;
                const glm::vec2 enemyExtent = Formation::FormationHalfExtent(
                    enemy.startStrength, enemy.ranks,
                    enemy.def->fileSpacing, enemy.def->rankSpacing);
                const float anchorDistance = glm::distance(
                    glm::vec2(regiment.anchor.x, regiment.anchor.z),
                    glm::vec2(enemy.anchor.x, enemy.anchor.z));
                radius = std::max(
                    radius,
                    anchorDistance + glm::length(ownExtent) * 2.0f +
                        glm::length(enemyExtent) * 2.0f);
            }
            return radius;
        }

        bool IsAlive(const FSoldier& soldier)
        {
            return soldier.combatState != ESoldierState::Dying &&
                   soldier.combatState != ESoldierState::Dead &&
                   soldier.health > 0;
        }

        bool Contains(const std::vector<int16_t>& values, int value)
        {
            return std::find(values.begin(), values.end(), static_cast<int16_t>(value)) != values.end();
        }

        int8_t EngagementSlot(uint8_t occupiedSlots)
        {
            if (occupiedSlots == 0) return 0;
            const int ring = (static_cast<int>(occupiedSlots) + 1) / 2;
            return static_cast<int8_t>((occupiedSlots & 1u) != 0u ? -ring : ring);
        }
    }

    FCombatSystem::FCombatSystem(uint64_t seed, float worldHalfExtent)
        : grid_(2.0f, worldHalfExtent), rng_(seed)
    {
        queryResults_.reserve(64);
    }

    void FCombatSystem::Reset(uint64_t seed)
    {
        rng_ = FDeterministicRng(seed);
        regimentOffsets_.clear();
        attackerCounts_.clear();
    }

    size_t FCombatSystem::FlatIndex(size_t regiment, size_t soldier) const
    {
        return regimentOffsets_[regiment] + soldier;
    }

    void FCombatSystem::Tick(float deltaSeconds, std::vector<FRegiment>& regiments,
                             const FCombatTuning& tuning, FBattleState& battleState)
    {
        if (!tuning.enabled || regiments.empty()) return;
        UpdateEngagements(regiments, tuning);
        PairSoldiers(regiments, tuning);
        ResolveAttacks(deltaSeconds, regiments, tuning, battleState);
        for (FRegiment& regiment : regiments)
        {
            regiment.chargeTimer = std::max(0.0f, regiment.chargeTimer - deltaSeconds);
        }
        ++battleState.combatTicks;
    }

    void FCombatSystem::UpdateEngagements(std::vector<FRegiment>& regiments,
                                          const FCombatTuning& tuning)
    {
        for (FRegiment& regiment : regiments) regiment.engagedWith.clear();
        std::vector<bool> nearEnemy(regiments.size(), false);
        for (size_t firstIndex = 0; firstIndex < regiments.size(); ++firstIndex)
        {
            FRegiment& first = regiments[firstIndex];
            if (first.state == ERegimentState::Destroyed || first.state == ERegimentState::Routing) continue;
            for (size_t secondIndex = firstIndex + 1; secondIndex < regiments.size(); ++secondIndex)
            {
                FRegiment& second = regiments[secondIndex];
                if (first.faction == second.faction ||
                    second.state == ERegimentState::Destroyed ||
                    second.state == ERegimentState::Routing)
                {
                    continue;
                }
                const glm::vec2 delta(first.anchor.x - second.anchor.x,
                                      first.anchor.z - second.anchor.z);
                const glm::vec2 firstExtent =
                    Formation::FormationHalfExtent(first.strength, first.ranks,
                                                   first.def->fileSpacing, first.def->rankSpacing);
                const glm::vec2 secondExtent =
                    Formation::FormationHalfExtent(second.strength, second.ranks,
                                                   second.def->fileSpacing, second.def->rankSpacing);
                const float contactMargin =
                    std::max({tuning.engageMargin, ApproachRadius(tuning),
                              tuning.regimentEngageDistance});
                const float broadRadius = glm::length(firstExtent) + glm::length(secondExtent) +
                                          contactMargin;
                if (glm::dot(delta, delta) > broadRadius * broadRadius ||
                    !CombatModel::RegimentBoundsOverlap(
                        first, second, contactMargin))
                {
                    continue;
                }
                nearEnemy[firstIndex] = true;
                nearEnemy[secondIndex] = true;
                if (!first.disengaging)
                {
                    first.engagedWith.push_back(static_cast<int16_t>(secondIndex));
                }
                if (!second.disengaging)
                {
                    second.engagedWith.push_back(static_cast<int16_t>(firstIndex));
                }
            }
        }

        for (size_t regimentIndex = 0; regimentIndex < regiments.size(); ++regimentIndex)
        {
            FRegiment& regiment = regiments[regimentIndex];
            if (regiment.disengaging)
            {
                if (!nearEnemy[regimentIndex])
                {
                    regiment.disengaging = false;
                }
                continue;
            }
            if (!regiment.engagedWith.empty())
            {
                if (regiment.state == ERegimentState::Charging)
                {
                    regiment.chargeTimer = tuning.chargeWindow;
                }
                if (regiment.state != ERegimentState::Destroyed &&
                    regiment.state != ERegimentState::Routing)
                {
                    regiment.state = ERegimentState::Engaged;
                }
                regiment.outOfContact = 0.0f;
            }
            else if (regiment.state == ERegimentState::Engaged)
            {
                Formation::PrepareNearestReform(regiment);
                regiment.orderTarget = regiment.anchor;
                regiment.orderFacing = regiment.facing;
                regiment.path.clear();
                regiment.pathCursor = 0;
                regiment.state = ERegimentState::Reforming;
            }
        }
    }

    bool FCombatSystem::IsValidTarget(const std::vector<FRegiment>& regiments,
                                      size_t attackerRegiment, const FSoldier& attacker,
                                      float maxDistance) const
    {
        if (attacker.targetRegiment < 0 || attacker.targetSoldier < 0 ||
            static_cast<size_t>(attacker.targetRegiment) >= regiments.size())
        {
            return false;
        }
        const FRegiment& regiment = regiments[attackerRegiment];
        const FRegiment& targetRegiment = regiments[attacker.targetRegiment];
        if (!Contains(regiment.engagedWith, attacker.targetRegiment) ||
            attacker.targetSoldier >= static_cast<int>(targetRegiment.soldiers.size()))
        {
            return false;
        }
        const FSoldier& target = targetRegiment.soldiers[attacker.targetSoldier];
        if (!IsAlive(target)) return false;
        const glm::vec2 delta(target.position.x - attacker.position.x,
                              target.position.z - attacker.position.z);
        return glm::dot(delta, delta) <= maxDistance * maxDistance;
    }

    void FCombatSystem::PairSoldiers(std::vector<FRegiment>& regiments,
                                     const FCombatTuning& tuning)
    {
        regimentOffsets_.resize(regiments.size() + 1);
        size_t totalSoldiers = 0;
        for (size_t regiment = 0; regiment < regiments.size(); ++regiment)
        {
            regimentOffsets_[regiment] = totalSoldiers;
            totalSoldiers += regiments[regiment].soldiers.size();
        }
        regimentOffsets_.back() = totalSoldiers;
        attackerCounts_.assign(totalSoldiers, 0);

        const float acquisitionRadius = ApproachRadius(tuning);
        const float hysteresisRadius = acquisitionRadius * 1.5f;
        for (size_t regimentIndex = 0; regimentIndex < regiments.size(); ++regimentIndex)
        {
            FRegiment& regiment = regiments[regimentIndex];
            const float acquisitionRadius =
                RegimentAcquisitionRadius(regimentIndex, regiments, tuning);
            for (FSoldier& soldier : regiment.soldiers)
            {
                if (!IsAlive(soldier) ||
                    !IsValidTarget(regiments, regimentIndex, soldier,
                                   std::max(hysteresisRadius, acquisitionRadius * 1.25f)))
                {
                    soldier.targetRegiment = -1;
                    soldier.targetSoldier = -1;
                    soldier.engagementSlot = -1;
                    if (IsAlive(soldier)) soldier.combatState = ESoldierState::Formation;
                    continue;
                }
                const size_t targetIndex =
                    FlatIndex(static_cast<size_t>(soldier.targetRegiment),
                              static_cast<size_t>(soldier.targetSoldier));
                soldier.engagementSlot = EngagementSlot(attackerCounts_[targetIndex]);
                ++attackerCounts_[targetIndex];
                soldier.combatState = ESoldierState::Fighting;
            }
        }

        grid_.Build(regiments);
        for (size_t regimentIndex = 0; regimentIndex < regiments.size(); ++regimentIndex)
        {
            FRegiment& regiment = regiments[regimentIndex];
            if (regiment.engagedWith.empty()) continue;
            const float regimentAcquisitionRadius =
                RegimentAcquisitionRadius(regimentIndex, regiments, tuning);
            const float radiusSquared =
                regimentAcquisitionRadius * regimentAcquisitionRadius;
            for (FSoldier& soldier : regiment.soldiers)
            {
                if (!IsAlive(soldier) || soldier.targetRegiment >= 0) continue;
                grid_.Query(soldier.position, regimentAcquisitionRadius, queryResults_);
                float bestScore = std::numeric_limits<float>::max();
                FCombatGridEntry nearest;
                const glm::vec2 regimentRight(
                    std::cos(regiment.facing), -std::sin(regiment.facing));
                for (const FCombatGridEntry candidate : queryResults_)
                {
                    if (candidate.regiment < 0 ||
                        candidate.regiment == static_cast<int>(regimentIndex) ||
                        !Contains(regiment.engagedWith, candidate.regiment))
                    {
                        continue;
                    }
                    const size_t flatIndex = FlatIndex(candidate.regiment, candidate.soldier);
                    if (attackerCounts_[flatIndex] >= tuning.maxAttackersPerTarget) continue;
                    const FSoldier& target =
                        regiments[candidate.regiment].soldiers[candidate.soldier];
                    const glm::vec2 delta(target.position.x - soldier.position.x,
                                          target.position.z - soldier.position.z);
                    const float distanceSquared = glm::dot(delta, delta);
                    const float lateralDelta = glm::dot(delta, regimentRight);
                    const float score =
                        distanceSquared +
                        lateralDelta * lateralDelta * tuning.targetLateralPenalty;
                    if (distanceSquared <= radiusSquared && score < bestScore)
                    {
                        bestScore = score;
                        nearest = candidate;
                    }
                }
                if (nearest.regiment >= 0)
                {
                    soldier.targetRegiment = nearest.regiment;
                    soldier.targetSoldier = nearest.soldier;
                    soldier.combatState = ESoldierState::Fighting;
                    const size_t targetIndex =
                        FlatIndex(nearest.regiment, nearest.soldier);
                    soldier.engagementSlot = EngagementSlot(attackerCounts_[targetIndex]);
                    ++attackerCounts_[targetIndex];
                }
            }
        }
    }

    void FCombatSystem::ResolveAttacks(float deltaSeconds, std::vector<FRegiment>& regiments,
                                       const FCombatTuning& tuning, FBattleState& battleState)
    {
        for (size_t regimentIndex = 0; regimentIndex < regiments.size(); ++regimentIndex)
        {
            FRegiment& regiment = regiments[regimentIndex];
            if (!regiment.def) continue;
            const float targetKeepRadius =
                RegimentAcquisitionRadius(regimentIndex, regiments, tuning) * 1.25f;
            const FUnitCombatDef& attackerDef = CombatDef(regiment.def->type);
            for (size_t soldierIndex = 0; soldierIndex < regiment.soldiers.size(); ++soldierIndex)
            {
                FSoldier& soldier = regiment.soldiers[soldierIndex];
                if (soldier.combatState != ESoldierState::Fighting ||
                    !IsValidTarget(regiments, regimentIndex, soldier,
                                   targetKeepRadius))
                {
                    continue;
                }
                soldier.attackTimer -= deltaSeconds;
                if (soldier.attackTimer > 0.0f) continue;

                FRegiment& targetRegiment = regiments[soldier.targetRegiment];
                FSoldier& target = targetRegiment.soldiers[soldier.targetSoldier];
                const glm::vec3 delta = soldier.position - target.position;
                if (glm::length(glm::vec2(delta.x, delta.z)) > attackerDef.weaponReach + 0.25f)
                {
                    continue;
                }

                soldier.attackTimer =
                    attackerDef.attackInterval * (1.0f + rng_.Jitter(0.15f));
                const FUnitCombatDef& targetDef = CombatDef(targetRegiment.def->type);
                int attack = attackerDef.attack;
                attack += CombatModel::ArcAttackBonus(
                    CombatModel::ClassifyAttackArc(target.yaw, delta), tuning);
                if (regiment.chargeTimer > 0.0f) attack += attackerDef.chargeBonus;
                if (regiment.morale < 40.0f) attack -= tuning.waveringPenalty;
                if (!rng_.Chance(CombatModel::HitChance(attack, targetDef.defense, tuning))) continue;

                target.health = static_cast<int16_t>(target.health - attackerDef.damage);
                battleState.events.push_back({
                    ECombatEventType::Hit, soldier.targetRegiment, soldier.targetSoldier,
                    target.position, target.yaw});
                if (target.health > 0) continue;

                target.health = 0;
                target.combatState = ESoldierState::Dying;
                target.deathTimer = tuning.deathClipSeconds;
                target.targetRegiment = -1;
                target.targetSoldier = -1;
                target.engagementSlot = -1;
                targetRegiment.strength = std::max(0, targetRegiment.strength - 1);
                ++regiment.kills;
                battleState.events.push_back({
                    ECombatEventType::Death, soldier.targetRegiment, soldier.targetSoldier,
                    target.position, target.yaw + rng_.Jitter(glm::radians(25.0f))});
                if (targetRegiment.strength == 0)
                {
                    targetRegiment.state = ERegimentState::Destroyed;
                    targetRegiment.selected = false;
                    targetRegiment.engagedWith.clear();
                    battleState.events.push_back({
                        ECombatEventType::RegimentDestroyed, soldier.targetRegiment, -1,
                        targetRegiment.anchor, targetRegiment.facing});
                }
                soldier.targetRegiment = -1;
                soldier.targetSoldier = -1;
                soldier.engagementSlot = -1;
                soldier.combatState = ESoldierState::Formation;
            }
        }
    }
}
