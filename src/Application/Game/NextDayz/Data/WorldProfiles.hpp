#pragma once

#include "Engine/Common/CoreMinimal.hpp"

namespace NextDayz
{
    enum class ELootProfile : uint8_t
    {
        Residential,
        Medical,
        Military,
        Industrial,
        Wilderness,
    };

    enum class ELootCategory : uint8_t
    {
        FoodWater,
        Medical,
        Ammo,
        Weapon,
        Clothing,
        Misc,
    };

    struct FLootRespawnTuning
    {
        double foodWaterSeconds = 8.0 * 60.0;
        double medicalSeconds = 12.0 * 60.0;
        double ammoSeconds = 15.0 * 60.0;
        double weaponSeconds = 30.0 * 60.0;
        double clothingSeconds = 18.0 * 60.0;
        double miscSeconds = 12.0 * 60.0;
        float minimumPlayerDistance = 60.0f;
        double minimumOffscreenSeconds = 20.0;
    };

    inline double RespawnCooldown(ELootCategory category, const FLootRespawnTuning& tuning)
    {
        switch (category)
        {
        case ELootCategory::FoodWater: return tuning.foodWaterSeconds;
        case ELootCategory::Medical: return tuning.medicalSeconds;
        case ELootCategory::Ammo: return tuning.ammoSeconds;
        case ELootCategory::Weapon: return tuning.weaponSeconds;
        case ELootCategory::Clothing: return tuning.clothingSeconds;
        case ELootCategory::Misc:
        default: return tuning.miscSeconds;
        }
    }
}
