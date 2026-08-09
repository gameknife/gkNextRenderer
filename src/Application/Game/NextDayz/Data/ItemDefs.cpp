#include "Engine/Common/CoreMinimal.hpp"

#include "ItemDefs.hpp"

namespace NextDayz
{
    namespace
    {
        constexpr std::array<FItemDef, 25> itemDefs = {{
            {"ak", "AK-74", EItemKind::Weapon, 10, 1, EEquipSlot::PrimaryWeapon, 0, 0, 0, 0, "ak"},
            {"svd", "SVD", EItemKind::Weapon, 10, 1, EEquipSlot::PrimaryWeapon, 0, 0, 0, 0, "svd"},
            {"mosin", "Mosin", EItemKind::Weapon, 10, 1, EEquipSlot::PrimaryWeapon, 0, 0, 0, 0, "mosin"},
            {"shotgun", "Shotgun", EItemKind::Weapon, 10, 1, EEquipSlot::PrimaryWeapon, 0, 0, 0, 0, "shotgun"},
            {"pistol", "Makarov", EItemKind::Weapon, 4, 1, EEquipSlot::SecondaryWeapon, 0, 0, 0, 0, "pistol"},
            {"crowbar", "Crowbar", EItemKind::Melee, 5, 1, EEquipSlot::Hands},
            {"ammo_545", "5.45x39", EItemKind::Ammo, 2, 30},
            {"ammo_762", "7.62x54", EItemKind::Ammo, 2, 20},
            {"ammo_12g", "12 Gauge", EItemKind::Ammo, 2, 12},
            {"ammo_9mm", "9x18", EItemKind::Ammo, 1, 24},
            {"helmet", "Helmet", EItemKind::Clothing, 3, 1, EEquipSlot::Head},
            {"jacket", "Field Jacket", EItemKind::Clothing, 3, 1, EEquipSlot::Torso, 12},
            {"work_pants", "Work Pants", EItemKind::Clothing, 2, 1, EEquipSlot::Legs, 10},
            {"backpack_small", "Small Backpack", EItemKind::Clothing, 3, 1, EEquipSlot::Back, 16},
            {"backpack", "Backpack", EItemKind::Clothing, 4, 1, EEquipSlot::Back, 28},
            {"food_can", "Canned Food", EItemKind::Consumable, 2, 4, EEquipSlot::None, 0, 32.0f},
            {"water_bottle", "Water Bottle", EItemKind::Consumable, 2, 1, EEquipSlot::None, 0, 0, 42.0f},
            {"water_bottle_empty", "Empty Bottle", EItemKind::Misc, 2, 1},
            {"bandage", "Bandage", EItemKind::Consumable, 1, 4, EEquipSlot::None, 0, 0, 0, 12.0f},
            {"medkit", "Medkit", EItemKind::Consumable, 3, 1, EEquipSlot::None, 0, 0, 0, 45.0f},
            {"fuel", "Fuel", EItemKind::Misc, 4, 1},
            {"radio", "Radio", EItemKind::Misc, 2, 1},
            {"bedroll", "Bedroll", EItemKind::Misc, 4, 1},
            {"lantern", "Lantern", EItemKind::Misc, 2, 1},
            {"weapon_sling", "Weapon Sling", EItemKind::Clothing, 1, 1, EEquipSlot::Torso, 2},
        }};
    }

    const FItemDef* FindItemDef(std::string_view id)
    {
        const auto it = std::find_if(itemDefs.begin(), itemDefs.end(),
            [id](const FItemDef& definition) { return definition.id == id; });
        return it == itemDefs.end() ? nullptr : &*it;
    }

    const FItemDef& ResolveItemDef(std::string_view id, std::string_view displayName, EItemKind fallbackKind)
    {
        if (const FItemDef* definition = FindItemDef(id))
        {
            return *definition;
        }
        static thread_local std::string fallbackId;
        static thread_local std::string fallbackDisplay;
        static thread_local FItemDef fallback;
        fallbackId = id;
        fallbackDisplay = displayName;
        fallback = FItemDef{fallbackId, fallbackDisplay, fallbackKind};
        return fallback;
    }
}
