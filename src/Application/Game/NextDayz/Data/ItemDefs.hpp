#pragma once

#include "Engine/Common/CoreMinimal.hpp"

namespace NextDayz
{
    enum class EItemKind : uint8_t
    {
        Weapon,
        Ammo,
        Clothing,
        Consumable,
        Melee,
        Misc,
    };

    enum class EEquipSlot : uint8_t
    {
        None,
        Head,
        Torso,
        Legs,
        Back,
        PrimaryWeapon,
        SecondaryWeapon,
        Hands,
    };

    struct FItemDef
    {
        std::string_view id;
        std::string_view displayName;
        EItemKind kind = EItemKind::Misc;
        int volumePerStack = 1;
        int maxStack = 1;
        EEquipSlot equipSlot = EEquipSlot::None;
        int containerCapacity = 0;
        float hungerDelta = 0.0f;
        float hydrationDelta = 0.0f;
        float healthDelta = 0.0f;
        std::string_view weaponDefId;
    };

    const FItemDef* FindItemDef(std::string_view id);
    const FItemDef& ResolveItemDef(std::string_view id, std::string_view displayName, EItemKind fallbackKind);
}
