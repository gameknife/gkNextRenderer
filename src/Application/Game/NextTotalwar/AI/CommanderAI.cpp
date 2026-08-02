#include "AI/CommanderAI.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <limits>

namespace NextTotalwar
{
    void FCommanderAI::Reset(uint64_t seed, int faction)
    {
        faction_ = faction;
        decisionCooldown_ = 0.0f;
        phase_ = ECommanderPhase::Deploy;
        decisionCount_ = 0;
        rng_ = FDeterministicRng(seed ^ 0x43b0d7ULL ^ static_cast<uint64_t>(faction));
    }

    int FCommanderAI::FindClosestEnemy(const FRegiment& regiment,
                                       const std::vector<FRegiment>& regiments) const
    {
        float nearest = std::numeric_limits<float>::max();
        int result = -1;
        for (const FRegiment& candidate : regiments)
        {
            if (candidate.faction == faction_ || !IsRegimentSelectable(candidate)) continue;
            const float distance = glm::length(glm::vec2(candidate.anchor.x - regiment.anchor.x,
                                                          candidate.anchor.z - regiment.anchor.z));
            if (distance < nearest)
            {
                nearest = distance;
                result = candidate.id;
            }
        }
        return result;
    }

    std::vector<FBattleOrder> FCommanderAI::Tick(float deltaSeconds, uint64_t combatTick,
                                                  float battleSeconds,
                                                  const std::vector<FRegiment>& regiments)
    {
        decisionCooldown_ -= std::max(deltaSeconds, 0.0f);
        if (decisionCooldown_ > 0.0f) return {};
        decisionCooldown_ = 1.0f;
        ++decisionCount_;

        int friendlyStrength = 0;
        int enemyStrength = 0;
        bool anyEngaged = false;
        for (const FRegiment& regiment : regiments)
        {
            if (regiment.faction == faction_) friendlyStrength += regiment.strength;
            else enemyStrength += regiment.strength;
            anyEngaged |= regiment.faction == faction_ && regiment.state == ERegimentState::Engaged;
        }
        if (friendlyStrength < enemyStrength * 0.45f) phase_ = ECommanderPhase::Recover;
        else if (anyEngaged && friendlyStrength > enemyStrength * 1.15f) phase_ = ECommanderPhase::Press;
        else if (anyEngaged) phase_ = ECommanderPhase::Engage;
        else if (battleSeconds > 0.0f) phase_ = ECommanderPhase::Advance;
        else phase_ = ECommanderPhase::Deploy;

        std::vector<const FRegiment*> force;
        for (const FRegiment& regiment : regiments)
        {
            if (regiment.faction == faction_ && IsRegimentSelectable(regiment) &&
                regiment.moraleState != EMoraleState::Routing)
                force.push_back(&regiment);
        }
        std::sort(force.begin(), force.end(), [](const FRegiment* a, const FRegiment* b)
        {
            return a->id < b->id;
        });

        std::vector<FBattleOrder> orders;
        orders.reserve(force.size());
        for (size_t forceIndex = 0; forceIndex < force.size(); ++forceIndex)
        {
            const FRegiment& regiment = *force[forceIndex];
            const int targetId = FindClosestEnemy(regiment, regiments);
            if (targetId < 0) continue;
            const FRegiment& target = regiments[static_cast<size_t>(targetId)];
            FBattleOrder order;
            order.issuerFaction = faction_;
            order.regimentId = regiment.id;
            order.targetRegimentId = targetId;
            order.targetPosition = target.anchor;
            order.targetFacing = std::atan2(target.anchor.x - regiment.anchor.x,
                                             target.anchor.z - regiment.anchor.z);
            order.issuedTick = combatTick;

            const bool reserve = force.size() >= 6 && forceIndex + 2 >= force.size();
            const bool flank = forceIndex >= force.size() / 2 && forceIndex + 2 < force.size();
            if (phase_ == ECommanderPhase::Deploy ||
                (phase_ == ECommanderPhase::Advance && reserve && battleSeconds < 15.0f))
            {
                order.type = EBattleOrderType::Halt;
                order.targetRegimentId = -1;
                order.targetPosition = regiment.anchor;
            }
            else if (regiment.def && regiment.def->canRangedAttack)
            {
                const glm::vec3 away = regiment.anchor - target.anchor;
                const float distance = glm::length(glm::vec2(away.x, away.z));
                if (distance < std::max(18.0f, regiment.def->rangedMinRange + 5.0f))
                {
                    order.type = EBattleOrderType::Withdraw;
                    order.targetRegimentId = -1;
                    const glm::vec3 direction = distance > 0.1f
                                                    ? away / distance
                                                    : glm::vec3(faction_ == 0 ? -1.0f : 1.0f, 0.0f, 0.0f);
                    order.targetPosition = regiment.anchor + direction * 30.0f;
                }
                else
                {
                    order.type = EBattleOrderType::Attack;
                }
            }
            else if (phase_ == ECommanderPhase::Recover && forceIndex >= force.size() / 2)
            {
                order.type = EBattleOrderType::Withdraw;
                const float retreat = faction_ == 0 ? -42.0f : 42.0f;
                order.targetPosition = regiment.anchor + glm::vec3(retreat, 0.0f, 0.0f);
            }
            else
            {
                const float distance = glm::length(glm::vec2(target.anchor.x - regiment.anchor.x,
                                                              target.anchor.z - regiment.anchor.z));
                if (phase_ == ECommanderPhase::Advance && flank &&
                    regiment.currentOrder != EBattleOrderType::Move)
                {
                    order.type = EBattleOrderType::Move;
                    order.targetRegimentId = -1;
                    const float side = (forceIndex & 1U) == 0 ? -1.0f : 1.0f;
                    order.targetPosition.z += side * (18.0f + rng_.UnitFloat() * 8.0f);
                }
                else if (flank && regiment.currentOrder == EBattleOrderType::Move &&
                         regiment.state == ERegimentState::Marching)
                {
                    order.type = EBattleOrderType::Move;
                    order.targetRegimentId = -1;
                    order.targetPosition = regiment.orderTarget;
                }
                else
                {
                    order.type = (phase_ == ECommanderPhase::Press || distance < 30.0f ||
                                  regiment.currentOrder == EBattleOrderType::Move)
                                     ? EBattleOrderType::Charge
                                     : EBattleOrderType::Attack;
                }
            }
            const bool samePersistentOrder =
                regiment.currentOrder == order.type &&
                regiment.orderTargetRegiment == order.targetRegimentId &&
                (order.type == EBattleOrderType::Attack ||
                 order.type == EBattleOrderType::Charge ||
                 order.type == EBattleOrderType::Halt ||
                 (order.type == EBattleOrderType::Move &&
                  regiment.state == ERegimentState::Marching) ||
                 (order.type == EBattleOrderType::Withdraw &&
                  regiment.state == ERegimentState::Marching));
            if (!samePersistentOrder) orders.push_back(order);
        }
        return orders;
    }
}
