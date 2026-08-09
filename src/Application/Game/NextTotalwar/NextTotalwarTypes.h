#pragma once

#include <glm/vec3.hpp>
#include <cstdint>
#include <vector>

namespace NextTotalwar
{
    enum class EUnitType : uint8_t
    {
        Spearman,
        Swordsman,
        Archer,
    };

    enum class ERegimentState : uint8_t
    {
        Idle,
        Marching,
        Reforming,
        Engaged,
        Charging,
        Routing,
        Destroyed,
    };

    enum class ESoldierState : uint8_t
    {
        Formation,
        Fighting,
        Dying,
        Dead,
    };

    enum class EBattleOrderType : uint8_t
    {
        Move,
        Attack,
        Charge,
        Halt,
        Withdraw,
        SetFormation,
    };

    enum class EMoraleState : uint8_t
    {
        Steady,
        Wavering,
        Routing,
        Rallying,
        Eliminated,
    };

    struct FUnitDef
    {
        EUnitType type = EUnitType::Spearman;
        const char* id = "spearman";
        const char* displayName = "Spearmen";
        float marchSpeed = 8.0f;
        float catchUpFactor = 1.45f;
        int defaultRanks = 10;
        float fileSpacing = 1.15f;
        float rankSpacing = 1.35f;
        bool canRangedAttack = false;
        float rangedRange = 0.0f;
        float rangedMinRange = 0.0f;
        float volleyInterval = 0.0f;
        int startingAmmo = 0;
        float rangedAccuracy = 0.0f;
        float rangedDamage = 0.0f;
        float baseMorale = 70.0f;
    };

    struct FSoldier
    {
        glm::vec3 position{};
        float yaw = 0.0f;
        int slotIndex = -1;
        float phaseOffset = 0.0f;
        ESoldierState combatState = ESoldierState::Formation;
        int16_t health = 0;
        int16_t targetRegiment = -1;
        int16_t targetSoldier = -1;
        int8_t engagementSlot = -1;
        float attackTimer = 0.0f;
        float flashTimer = 0.0f;
        float deathTimer = 0.0f;
    };

    struct FRegiment
    {
        int id = -1;
        int faction = 0;
        const FUnitDef* def = nullptr;
        glm::vec3 anchor{};
        float facing = 0.0f;
        int ranks = 4;
        bool selected = false;
        ERegimentState state = ERegimentState::Idle;
        glm::vec3 orderTarget{};
        float orderFacing = 0.0f;
        std::vector<glm::vec3> path;
        size_t pathCursor = 0;
        std::vector<FSoldier> soldiers;
        int strength = 0;
        int startStrength = 0;
        int kills = 0;
        float morale = 70.0f;
        float chargeTimer = 0.0f;
        float outOfContact = 0.0f;
        float orderLock = 0.0f;
        bool disengaging = false;
        std::vector<int16_t> engagedWith;
        EMoraleState moraleState = EMoraleState::Steady;
        float moraleStateTimer = 0.0f;
        float recentCasualtyPressure = 0.0f;
        float suppression = 0.0f;
        int ammo = 0;
        float volleyTimer = 0.0f;
        bool fireAtWill = true;
        EBattleOrderType currentOrder = EBattleOrderType::Halt;
        int orderTargetRegiment = -1;
        uint64_t lastOrderSequence = 0;
    };

    inline bool IsRegimentSelectable(const FRegiment& regiment)
    {
        return regiment.strength > 0 &&
               regiment.state != ERegimentState::Destroyed;
    }
}
