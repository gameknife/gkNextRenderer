#pragma once

#include "Sim/WMath.h"

namespace NextRA
{
    inline constexpr uint16_t infantryTypeId = 1;
    inline constexpr uint16_t tankTypeId = 2;
    inline constexpr uint16_t barracksTypeId = 3;
    inline constexpr uint16_t baseTypeId = 4;
    inline constexpr uint32_t simHz = 20;
    inline constexpr double simStepSeconds = 1.0 / static_cast<double>(simHz);
    inline constexpr int maxCatchupTicksPerFrame = 5;

    inline constexpr Sim::FFixed InfantrySpeedPerTick()
    {
        return Sim::FFixed::FromInt(192);
    }

    inline constexpr Sim::FFixed TankSpeedPerTick()
    {
        return Sim::FFixed::FromInt(144);
    }

    inline constexpr Sim::FFixed UnitSpeedPerTick(uint16_t typeId)
    {
        return typeId == tankTypeId ? TankSpeedPerTick() : InfantrySpeedPerTick();
    }

    inline constexpr int32_t UnitMaxHp(uint16_t typeId)
    {
        return typeId == tankTypeId ? 180 : 100;
    }

    inline constexpr int32_t UnitDamage(uint16_t typeId)
    {
        return typeId == tankTypeId ? 34 : 20;
    }

    inline constexpr Sim::WDist UnitAttackRange(uint16_t typeId)
    {
        return typeId == tankTypeId ? Sim::CellDistance(3) : Sim::CellDistance(2);
    }

    inline constexpr int32_t ProductionBuildTicks(uint16_t typeId)
    {
        return typeId == tankTypeId ? 90 : 55;
    }

    inline constexpr int32_t BarracksMaxHp()
    {
        return 260;
    }

    inline constexpr int32_t BaseMaxHp()
    {
        return 520;
    }
}
