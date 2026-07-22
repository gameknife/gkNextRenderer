#pragma once

// ============================================================================
// Inventory.hpp - Minimal item list + equipment state for the NextDayz MVP.
// Ammo/misc stack by id; weapons/clothing are appended. No weight/volume caps
// (a large slot guard prevents runaway growth). Equipment is tracked here;
// WeaponSystem owns the actual weapon runtime state.
// ============================================================================

#include <string>
#include <vector>

namespace NextDayz
{
    enum class EItemKind
    {
        Weapon,
        Ammo,
        Clothing,
        Misc
    };

    struct FItemStack
    {
        std::string id;
        std::string displayName;
        EItemKind kind = EItemKind::Misc;
        int count = 1;
    };

    class Inventory
    {
    public:
        static constexpr size_t kMaxSlots = 256;

        void Clear();

        // Stacks ammo/misc by id; weapons/clothing append as discrete entries.
        void Add(const std::string& id, const std::string& displayName, EItemKind kind, int count);

        const std::vector<FItemStack>& Items() const { return items_; }

        int CountOf(const std::string& id) const;
        // Removes up to `count`; returns the amount actually consumed.
        int Consume(const std::string& id, int count);

        // Clothing (helmet/backpack/...) equip state.
        bool IsClothingEquipped(const std::string& id) const;
        void SetClothingEquipped(const std::string& id, bool equipped);
        const std::vector<std::string>& EquippedClothing() const { return equippedClothing_; }

    private:
        std::vector<FItemStack> items_;
        std::vector<std::string> equippedClothing_;
    };
}
