#include "Battle/CombatModel.h"

#include "Battle/FormationLayout.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <array>
#include <algorithm>
#include <cmath>

namespace NextTotalwar::CombatModel
{
    EAttackArc ClassifyAttackArc(float targetFacing, const glm::vec3& attackerDirection)
    {
        const glm::vec2 direction(attackerDirection.x, attackerDirection.z);
        const float length = glm::length(direction);
        if (length <= 0.0001f) return EAttackArc::Front;
        const glm::vec2 forward(std::sin(targetFacing), std::cos(targetFacing));
        const float cosine = glm::clamp(glm::dot(forward, direction / length), -1.0f, 1.0f);
        if (cosine < -0.5f) return EAttackArc::Rear;
        if (cosine < 0.5f) return EAttackArc::Flank;
        return EAttackArc::Front;
    }

    int ArcAttackBonus(EAttackArc arc, const FCombatTuning& tuning)
    {
        if (arc == EAttackArc::Rear) return tuning.rearBonus;
        if (arc == EAttackArc::Flank) return tuning.flankBonus;
        return 0;
    }

    float HitChance(int attack, int defense, const FCombatTuning& tuning)
    {
        return glm::clamp(tuning.hitBase +
                              static_cast<float>(attack - defense) * tuning.hitScale,
                          tuning.minHitChance, tuning.maxHitChance);
    }

    bool RegimentBoundsOverlap(const FRegiment& first, const FRegiment& second, float margin)
    {
        if (!first.def || !second.def || first.strength <= 0 || second.strength <= 0) return false;

        const glm::vec2 firstFormationExtent =
            Formation::FormationHalfExtent(first.strength, first.ranks,
                                           first.def->fileSpacing, first.def->rankSpacing);
        const glm::vec2 secondFormationExtent =
            Formation::FormationHalfExtent(second.strength, second.ranks,
                                           second.def->fileSpacing, second.def->rankSpacing);
        // engageMargin is the total tolerated gap between both formations.
        // Split it between the two OBBs so contact never freezes two front
        // lines farther apart than the soldier search radius.
        const glm::vec2 halfMargin(std::max(margin, 0.0f) * 0.5f);
        const glm::vec2 firstExtent = firstFormationExtent + halfMargin;
        const glm::vec2 secondExtent = secondFormationExtent + halfMargin;
        const glm::vec3 firstCenterWorld =
            Formation::SlotWorld(first.anchor, first.facing, {0.0f, -firstFormationExtent.y});
        const glm::vec3 secondCenterWorld =
            Formation::SlotWorld(second.anchor, second.facing, {0.0f, -secondFormationExtent.y});
        const glm::vec2 centerDelta(secondCenterWorld.x - firstCenterWorld.x,
                                    secondCenterWorld.z - firstCenterWorld.z);

        const std::array<glm::vec2, 2> firstAxes = {{
            {std::cos(first.facing), -std::sin(first.facing)},
            {std::sin(first.facing), std::cos(first.facing)},
        }};
        const std::array<glm::vec2, 2> secondAxes = {{
            {std::cos(second.facing), -std::sin(second.facing)},
            {std::sin(second.facing), std::cos(second.facing)},
        }};

        const auto separated = [&](const glm::vec2& axis)
        {
            const float distance = std::abs(glm::dot(centerDelta, axis));
            const float firstRadius =
                firstExtent.x * std::abs(glm::dot(firstAxes[0], axis)) +
                firstExtent.y * std::abs(glm::dot(firstAxes[1], axis));
            const float secondRadius =
                secondExtent.x * std::abs(glm::dot(secondAxes[0], axis)) +
                secondExtent.y * std::abs(glm::dot(secondAxes[1], axis));
            return distance > firstRadius + secondRadius;
        };

        return !separated(firstAxes[0]) && !separated(firstAxes[1]) &&
               !separated(secondAxes[0]) && !separated(secondAxes[1]);
    }
}
