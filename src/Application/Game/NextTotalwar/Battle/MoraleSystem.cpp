#include "Battle/MoraleSystem.h"

#include "Battle/CombatModel.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>

namespace NextTotalwar
{
    void FMoraleSystem::Reset()
    {
        routedCount_ = 0;
        ralliedCount_ = 0;
    }

    void FMoraleSystem::Tick(float deltaSeconds, std::vector<FRegiment>& regiments,
                             FBattleState& battleState, const FMoraleTuning& tuning)
    {
        const size_t incomingEventCount = battleState.events.size();
        std::vector<int> casualties(regiments.size(), 0);
        for (size_t index = 0; index < incomingEventCount; ++index)
        {
            const FCombatEvent& event = battleState.events[index];
            if (event.type == ECombatEventType::Death && event.regiment >= 0 &&
                static_cast<size_t>(event.regiment) < casualties.size())
            {
                ++casualties[static_cast<size_t>(event.regiment)];
            }
        }

        for (size_t index = 0; index < regiments.size(); ++index)
        {
            FRegiment& regiment = regiments[index];
            if (regiment.strength <= 0 || regiment.state == ERegimentState::Destroyed)
            {
                regiment.morale = 0.0f;
                regiment.moraleState = EMoraleState::Eliminated;
                continue;
            }

            regiment.moraleStateTimer += deltaSeconds;
            regiment.recentCasualtyPressure = std::max(
                0.0f, regiment.recentCasualtyPressure - deltaSeconds * 0.45f);
            regiment.recentCasualtyPressure +=
                static_cast<float>(casualties[index]) * tuning.casualtyShock;
            regiment.suppression = std::max(0.0f, regiment.suppression - deltaSeconds * 0.65f);

            float hostileStrength = 0.0f;
            float friendlyStrength = static_cast<float>(regiment.strength);
            bool threatenedFlank = false;
            int nearbyRoutingFriends = 0;
            for (const FRegiment& other : regiments)
            {
                if (other.id == regiment.id || other.strength <= 0 ||
                    other.state == ERegimentState::Destroyed)
                {
                    continue;
                }
                const glm::vec3 toOther = other.anchor - regiment.anchor;
                const float distance = glm::length(glm::vec2(toOther.x, toOther.z));
                if (distance > 42.0f) continue;
                if (other.faction == regiment.faction)
                {
                    friendlyStrength += static_cast<float>(other.strength) * 0.45f;
                    if (other.moraleState == EMoraleState::Routing) ++nearbyRoutingFriends;
                }
                else
                {
                    hostileStrength += static_cast<float>(other.strength);
                    if (distance < 24.0f &&
                        CombatModel::ClassifyAttackArc(regiment.facing, toOther) !=
                            CombatModel::EAttackArc::Front)
                    {
                        threatenedFlank = true;
                    }
                }
            }

            float drain = regiment.recentCasualtyPressure +
                          regiment.suppression * tuning.suppressionDrain;
            if (threatenedFlank) drain += tuning.flankDrain;
            if (hostileStrength > friendlyStrength * 1.35f)
                drain += tuning.localDisadvantageDrain;
            drain += static_cast<float>(nearbyRoutingFriends) * tuning.friendlyRoutDrain;
            const bool calm = hostileStrength <= 0.0f && casualties[index] == 0 &&
                              regiment.suppression < 0.1f;
            const float moraleCeiling = regiment.def ? regiment.def->baseMorale : 100.0f;

            if (regiment.moraleState == EMoraleState::Routing)
            {
                regiment.morale = glm::clamp(
                    regiment.morale + (calm ? tuning.calmRecovery * 1.6f : -drain * 0.15f) * deltaSeconds,
                    0.0f, moraleCeiling);
                const float direction = regiment.faction == 0 ? -1.0f : 1.0f;
                regiment.anchor.x += direction * tuning.routSpeed * deltaSeconds;
                regiment.orderTarget = regiment.anchor;
                regiment.path.clear();
                regiment.pathCursor = 0;
                if (std::abs(regiment.anchor.x) >= tuning.leaveBattleExtent)
                {
                    regiment.strength = 0;
                    regiment.state = ERegimentState::Destroyed;
                    regiment.moraleState = EMoraleState::Eliminated;
                    battleState.events.push_back({ECombatEventType::RegimentDestroyed,
                                                  static_cast<int16_t>(regiment.id), -1,
                                                  regiment.anchor});
                }
                else if (calm && regiment.moraleStateTimer >= tuning.routMinimumSeconds &&
                         regiment.morale >= tuning.rallyThreshold)
                {
                    regiment.moraleState = EMoraleState::Rallying;
                    regiment.moraleStateTimer = 0.0f;
                    regiment.state = ERegimentState::Reforming;
                    regiment.disengaging = false;
                    ++ralliedCount_;
                    battleState.events.push_back({ECombatEventType::Rally,
                                                  static_cast<int16_t>(regiment.id), -1,
                                                  regiment.anchor});
                }
                continue;
            }

            regiment.morale = glm::clamp(
                regiment.morale + (calm ? tuning.calmRecovery : -drain) * deltaSeconds,
                0.0f, moraleCeiling);
            if (regiment.morale <= tuning.routThreshold)
            {
                regiment.moraleState = EMoraleState::Routing;
                regiment.moraleStateTimer = 0.0f;
                regiment.state = ERegimentState::Routing;
                regiment.selected = false;
                regiment.engagedWith.clear();
                regiment.disengaging = true;
                ++routedCount_;
                battleState.events.push_back({ECombatEventType::Rout,
                                              static_cast<int16_t>(regiment.id), -1,
                                              regiment.anchor});
            }
            else if (regiment.morale < tuning.steadyThreshold)
            {
                regiment.moraleState = EMoraleState::Wavering;
            }
            else if (regiment.moraleState == EMoraleState::Rallying &&
                     regiment.moraleStateTimer < 3.0f)
            {
                // Keep a visible rallying state until the unit reforms.
            }
            else
            {
                regiment.moraleState = EMoraleState::Steady;
            }
        }
    }
}
