#pragma once

#include "NextTotalwarTypes.h"

#include <array>

namespace NextTotalwar
{
    struct FUnitCombatDef
    {
        int maxHealth = 30;
        int attack = 10;
        int defense = 10;
        int damage = 12;
        float attackInterval = 1.05f;
        float weaponReach = 1.20f;
        int chargeBonus = 8;
        float baseMorale = 70.0f;
    };

    inline constexpr std::array<FUnitCombatDef, 3> unitCombatDefs = {{
        {32, 9, 13, 12, 1.15f, 1.55f, 6, 72.0f},
        {30, 13, 10, 14, 0.95f, 1.15f, 10, 76.0f},
        {24, 6, 5, 8, 1.20f, 1.05f, 2, 55.0f},
    }};

    inline const FUnitCombatDef& CombatDef(EUnitType type)
    {
        return unitCombatDefs[static_cast<size_t>(type)];
    }

    struct FCombatTuning
    {
        bool enabled = true;
        float tickRate = 20.0f;
        float engageMargin = 1.6f;
        float regimentEngageDistance = 40.0f;
        float searchRadius = 2.4f;
        int maxAttackersPerTarget = 3;
        float targetLateralPenalty = 3.0f;
        float engagementArcDegrees = 140.0f;
        float separationRadius = 0.85f;
        float separationStrength = 1.4f;
        float maxBreakDistance = 2.5f;
        float hitBase = 0.45f;
        float hitScale = 0.03f;
        float minHitChance = 0.08f;
        float maxHitChance = 0.92f;
        float chargeWindow = 4.0f;
        int flankBonus = 3;
        int rearBonus = 6;
        int waveringPenalty = 3;
        float flashSeconds = 0.12f;
        float attackerFlash = 0.06f;
        float deathClipSeconds = 0.8f;
        int bloodPoolSize = 256;
    };
}
