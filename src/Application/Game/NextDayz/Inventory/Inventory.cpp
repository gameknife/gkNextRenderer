#include "Inventory.hpp"

#include <algorithm>

namespace NextDayz
{
    void Inventory::Clear()
    {
        items_.clear();
        equippedClothing_.clear();
    }

    void Inventory::Add(const std::string& id, const std::string& displayName, EItemKind kind, int count)
    {
        if (count <= 0)
        {
            return;
        }

        // Ammo and misc stack onto an existing entry of the same id.
        if (kind == EItemKind::Ammo || kind == EItemKind::Misc)
        {
            for (FItemStack& stack : items_)
            {
                if (stack.id == id)
                {
                    stack.count += count;
                    return;
                }
            }
        }

        if (items_.size() >= kMaxSlots)
        {
            return;
        }
        items_.push_back(FItemStack{id, displayName, kind, count});
    }

    int Inventory::CountOf(const std::string& id) const
    {
        int total = 0;
        for (const FItemStack& stack : items_)
        {
            if (stack.id == id)
            {
                total += stack.count;
            }
        }
        return total;
    }

    int Inventory::Consume(const std::string& id, int count)
    {
        int remaining = count;
        for (auto it = items_.begin(); it != items_.end() && remaining > 0;)
        {
            if (it->id == id)
            {
                const int take = std::min(remaining, it->count);
                it->count -= take;
                remaining -= take;
                if (it->count <= 0)
                {
                    it = items_.erase(it);
                    continue;
                }
            }
            ++it;
        }
        return count - remaining;
    }

    bool Inventory::IsClothingEquipped(const std::string& id) const
    {
        return std::find(equippedClothing_.begin(), equippedClothing_.end(), id) != equippedClothing_.end();
    }

    void Inventory::SetClothingEquipped(const std::string& id, bool equipped)
    {
        const bool already = IsClothingEquipped(id);
        if (equipped && !already)
        {
            equippedClothing_.push_back(id);
        }
        else if (!equipped && already)
        {
            equippedClothing_.erase(std::remove(equippedClothing_.begin(), equippedClothing_.end(), id),
                                    equippedClothing_.end());
        }
    }
}
