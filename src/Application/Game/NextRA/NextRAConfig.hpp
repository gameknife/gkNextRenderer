#pragma once

#include "Sim/WMath.h"

#include <array>
#include <cstdint>

namespace NextRA
{
    inline constexpr uint16_t infantryTypeId = 1;
    inline constexpr uint16_t tankTypeId = 2;
    inline constexpr uint16_t barracksTypeId = 3;
    inline constexpr uint16_t baseTypeId = 4;
    inline constexpr uint16_t rocketeerTypeId = 5;
    inline constexpr uint16_t turretTypeId = 6;
    inline constexpr uint16_t wallTypeId = 7;
    inline constexpr uint32_t simHz = 20;
    inline constexpr double simStepSeconds = 1.0 / static_cast<double>(simHz);
    inline constexpr int maxCatchupTicksPerFrame = 5;

    enum class EArmorType : uint8_t
    {
        Flesh = 0,
        Light = 1,
        Heavy = 2,
        Building = 3,
    };

    enum class EWeaponType : uint8_t
    {
        Bullet = 0,
        Shell = 1,
        Rocket = 2,
        Flame = 3,
    };

    inline constexpr std::array<std::array<int32_t, 4>, 4> kDamageMultiplier = {{
        {{100, 75, 25, 20}},
        {{60, 100, 150, 80}},
        {{90, 120, 130, 140}},
        {{140, 60, 40, 60}},
    }};

    struct FUnitDef
    {
        uint16_t typeId = 0;
        const char* name = "";
        int32_t maxHp = 1;
        Sim::FFixed speedPerTick = Sim::FFixed::FromInt(0);
        Sim::WDist attackRange = Sim::WDist{};
        Sim::WDist acquireRange = Sim::WDist{};
        int32_t damage = 0;
        int32_t cooldownTicks = 12;
        int32_t productionBuildTicks = 0;
        EArmorType armor = EArmorType::Flesh;
        EWeaponType weapon = EWeaponType::Bullet;
        Sim::WAngle bodyTurnSpeed = Sim::WAngle::FromRaw(64);
        bool hasTurret = false;
        Sim::WAngle turretTurnSpeed = Sim::WAngle::FromRaw(96);
        Sim::CPos footprint = Sim::CPos{1, 1};
        bool mobile = true;
        bool building = false;
        bool production = false;
        bool base = false;
    };

    inline constexpr std::array<FUnitDef, 7> kUnitDefs = {{
        FUnitDef{
            infantryTypeId,
            "Infantry",
            100,
            Sim::FFixed::FromInt(192),
            Sim::CellDistance(2),
            Sim::CellDistance(5),
            20,
            12,
            55,
            EArmorType::Flesh,
            EWeaponType::Bullet,
            Sim::WAngle::FromRaw(96),
            false,
            Sim::WAngle::FromRaw(0),
            Sim::CPos{1, 1},
            true,
            false,
            false,
            false,
        },
        FUnitDef{
            tankTypeId,
            "Tank",
            180,
            Sim::FFixed::FromInt(144),
            Sim::CellDistance(3),
            Sim::CellDistance(5),
            34,
            12,
            90,
            EArmorType::Heavy,
            EWeaponType::Shell,
            Sim::WAngle::FromRaw(48),
            true,
            Sim::WAngle::FromRaw(96),
            Sim::CPos{1, 1},
            true,
            false,
            false,
            false,
        },
        FUnitDef{
            barracksTypeId,
            "Barracks",
            260,
            Sim::FFixed::FromInt(0),
            Sim::WDist{},
            Sim::WDist{},
            0,
            12,
            0,
            EArmorType::Building,
            EWeaponType::Bullet,
            Sim::WAngle::FromRaw(0),
            false,
            Sim::WAngle::FromRaw(0),
            Sim::CPos{2, 2},
            false,
            true,
            true,
            false,
        },
        FUnitDef{
            baseTypeId,
            "Base",
            520,
            Sim::FFixed::FromInt(0),
            Sim::WDist{},
            Sim::WDist{},
            0,
            12,
            0,
            EArmorType::Building,
            EWeaponType::Bullet,
            Sim::WAngle::FromRaw(0),
            false,
            Sim::WAngle::FromRaw(0),
            Sim::CPos{3, 3},
            false,
            true,
            false,
            true,
        },
        FUnitDef{
            rocketeerTypeId,
            "Rocketeer",
            85,
            Sim::FFixed::FromInt(170),
            Sim::CellDistance(3),
            Sim::CellDistance(6),
            24,
            18,
            70,
            EArmorType::Flesh,
            EWeaponType::Rocket,
            Sim::WAngle::FromRaw(88),
            false,
            Sim::WAngle::FromRaw(0),
            Sim::CPos{1, 1},
            true,
            false,
            false,
            false,
        },
        FUnitDef{
            turretTypeId,
            "Turret",
            300,
            Sim::FFixed::FromInt(0),
            Sim::CellDistance(5),
            Sim::CellDistance(6),
            30,
            14,
            0,
            EArmorType::Building,
            EWeaponType::Shell,
            Sim::WAngle::FromRaw(0),
            true,
            Sim::WAngle::FromRaw(128),
            Sim::CPos{1, 1},
            false,
            true,
            false,
            false,
        },
        FUnitDef{
            wallTypeId,
            "Wall",
            420,
            Sim::FFixed::FromInt(0),
            Sim::WDist{},
            Sim::WDist{},
            0,
            12,
            0,
            EArmorType::Building,
            EWeaponType::Bullet,
            Sim::WAngle::FromRaw(0),
            false,
            Sim::WAngle::FromRaw(0),
            Sim::CPos{1, 1},
            false,
            true,
            false,
            false,
        },
    }};

    inline constexpr const FUnitDef& UnitDef(uint16_t typeId)
    {
        for (const FUnitDef& def : kUnitDefs)
        {
            if (def.typeId == typeId)
            {
                return def;
            }
        }
        return kUnitDefs[0];
    }

    inline constexpr bool IsKnownUnitType(uint16_t typeId)
    {
        for (const FUnitDef& def : kUnitDefs)
        {
            if (def.typeId == typeId)
            {
                return true;
            }
        }
        return false;
    }

    inline constexpr int32_t DamageMultiplier(EWeaponType weapon, EArmorType armor)
    {
        return kDamageMultiplier[static_cast<size_t>(weapon)][static_cast<size_t>(armor)];
    }

    inline constexpr int32_t ApplyDamageMultiplier(int32_t damage, EWeaponType weapon, EArmorType armor)
    {
        return damage * DamageMultiplier(weapon, armor) / 100;
    }

    inline constexpr Sim::FFixed InfantrySpeedPerTick()
    {
        return UnitDef(infantryTypeId).speedPerTick;
    }

    inline constexpr Sim::FFixed TankSpeedPerTick()
    {
        return UnitDef(tankTypeId).speedPerTick;
    }

    inline constexpr Sim::FFixed UnitSpeedPerTick(uint16_t typeId)
    {
        return UnitDef(typeId).speedPerTick;
    }

    inline constexpr int32_t UnitMaxHp(uint16_t typeId)
    {
        return UnitDef(typeId).maxHp;
    }

    inline constexpr int32_t UnitDamage(uint16_t typeId)
    {
        return UnitDef(typeId).damage;
    }

    inline constexpr Sim::WDist UnitAttackRange(uint16_t typeId)
    {
        return UnitDef(typeId).attackRange;
    }

    inline constexpr int32_t ProductionBuildTicks(uint16_t typeId)
    {
        return UnitDef(typeId).productionBuildTicks;
    }

    inline constexpr int32_t BarracksMaxHp()
    {
        return UnitDef(barracksTypeId).maxHp;
    }

    inline constexpr int32_t BaseMaxHp()
    {
        return UnitDef(baseTypeId).maxHp;
    }
}
