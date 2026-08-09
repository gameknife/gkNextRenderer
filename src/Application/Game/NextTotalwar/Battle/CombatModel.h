#pragma once

#include "NextTotalwarCombatConfig.hpp"

namespace NextTotalwar::CombatModel
{
    enum class EAttackArc : uint8_t
    {
        Front,
        Flank,
        Rear,
    };

    EAttackArc ClassifyAttackArc(float targetFacing, const glm::vec3& attackerDirection);
    int ArcAttackBonus(EAttackArc arc, const FCombatTuning& tuning);
    float HitChance(int attack, int defense, const FCombatTuning& tuning);
    bool RegimentBoundsOverlap(const FRegiment& first, const FRegiment& second, float margin);
}
