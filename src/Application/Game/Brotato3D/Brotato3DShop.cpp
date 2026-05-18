#include "Brotato3DShop.hpp"

namespace Brotato3D
{
    void FShop::SetItems(std::vector<FShopItemDef> items)
    {
        items_ = std::move(items);
    }

    void FShop::SetPassiveItems(std::vector<FItemDef> items)
    {
        passiveItems_ = std::move(items);
    }

    void FShop::SetWeapons(std::map<std::string, FWeaponDef> weapons)
    {
        weapons_ = std::move(weapons);
    }

    FShopItemDef FShop::MakeOfferFromItem(const FItemDef& item) const
    {
        FShopItemDef offer{};
        offer.id = item.id;
        offer.name = item.name;
        offer.cost = item.cost;
        offer.weight = item.weight;
        offer.isPassiveItem = true;
        offer.description = item.description;
        offer.rarity = item.rarity;
        offer.trigger = item.trigger;
        offer.effect = item.effect;
        offer.value = item.value;
        offer.threshold = item.threshold;
        offer.explosionRadius = item.explosionRadius;
        offer.explosionDamage = item.explosionDamage;
        return offer;
    }

    FShopItemDef FShop::MakeOfferFromWeapon(const std::string& weaponId, const FWeaponDef& weapon) const
    {
        FShopItemDef offer{};
        offer.id = fmt::format("weapon_{}", weaponId);
        offer.name = weapon.name;
        offer.isWeaponCard = true;
        offer.weaponId = weaponId;
        if (weaponId == "smg" || weaponId == "shotgun")
        {
            offer.cost = 8;
        }
        else if (weaponId == "sniper" || weaponId == "laser")
        {
            offer.cost = 14;
        }
        else
        {
            offer.cost = 18;
        }
        offer.weight = 1;
        return offer;
    }

    bool FShop::CanOfferWeapon(const std::string& weaponId, const std::vector<FWeaponRuntime>& equippedWeapons) const
    {
        if (equippedWeapons.size() < 6)
        {
            return true;
        }

        int sameTierOneCount = 0;
        for (const FWeaponRuntime& weapon : equippedWeapons)
        {
            if (weapon.weaponId == weaponId && weapon.tier == 1)
            {
                ++sameTierOneCount;
            }
        }
        return sameTierOneCount >= 1;
    }

    void FShop::RollStatOffer(std::vector<FShopItemDef>& remainingStats, std::vector<FShopItemDef>& outOffer)
    {
        if (remainingStats.empty())
        {
            return;
        }

        int totalWeight = 0;
        for (const FShopItemDef& item : remainingStats)
        {
            totalWeight += std::max(1, item.weight);
        }

        std::uniform_int_distribution<int> dist(1, totalWeight);
        int pick = dist(rng_);
        size_t chosenIndex = 0;
        for (size_t index = 0; index < remainingStats.size(); ++index)
        {
            pick -= std::max(1, remainingStats[index].weight);
            if (pick <= 0)
            {
                chosenIndex = index;
                break;
            }
        }

        outOffer.push_back(remainingStats[chosenIndex]);
        remainingStats.erase(remainingStats.begin() + static_cast<std::ptrdiff_t>(chosenIndex));
    }

    void FShop::RollItemOffer(std::vector<FItemDef>& remainingItems, std::vector<FShopItemDef>& outOffer)
    {
        if (remainingItems.empty())
        {
            return;
        }

        int totalWeight = 0;
        for (const FItemDef& item : remainingItems)
        {
            totalWeight += std::max(1, item.weight);
        }

        std::uniform_int_distribution<int> dist(1, totalWeight);
        int pick = dist(rng_);
        size_t chosenIndex = 0;
        for (size_t index = 0; index < remainingItems.size(); ++index)
        {
            pick -= std::max(1, remainingItems[index].weight);
            if (pick <= 0)
            {
                chosenIndex = index;
                break;
            }
        }

        outOffer.push_back(MakeOfferFromItem(remainingItems[chosenIndex]));
        remainingItems.erase(remainingItems.begin() + static_cast<std::ptrdiff_t>(chosenIndex));
    }

    void FShop::RollWeaponOffer(std::vector<std::pair<std::string, FWeaponDef>>& remainingWeapons, std::vector<FShopItemDef>& outOffer)
    {
        if (remainingWeapons.empty())
        {
            return;
        }

        std::uniform_int_distribution<size_t> dist(0, remainingWeapons.size() - 1);
        const size_t chosenIndex = dist(rng_);
        outOffer.push_back(MakeOfferFromWeapon(remainingWeapons[chosenIndex].first, remainingWeapons[chosenIndex].second));
        remainingWeapons.erase(remainingWeapons.begin() + static_cast<std::ptrdiff_t>(chosenIndex));
    }

    void FShop::Roll(int count,
                     const std::vector<std::string>& ownedItemIds,
                     const std::vector<FWeaponRuntime>& equippedWeapons,
                     std::vector<FShopItemDef>& outOffer)
    {
        outOffer.clear();
        std::vector<FShopItemDef> remainingStats = items_;
        std::vector<FItemDef> remainingItems;
        std::vector<std::pair<std::string, FWeaponDef>> remainingWeapons;
        for (const FItemDef& item : passiveItems_)
        {
            if (std::find(ownedItemIds.begin(), ownedItemIds.end(), item.id) == ownedItemIds.end())
            {
                remainingItems.push_back(item);
            }
        }
        for (const auto& [weaponId, weapon] : weapons_)
        {
            if (CanOfferWeapon(weaponId, equippedWeapons))
            {
                remainingWeapons.emplace_back(weaponId, weapon);
            }
        }

        std::uniform_real_distribution<float> typeDist(0.0f, 1.0f);
        while (static_cast<int>(outOffer.size()) < count &&
               (!remainingStats.empty() || !remainingItems.empty() || !remainingWeapons.empty()))
        {
            const float typeRoll = typeDist(rng_);
            if (typeRoll < 0.5f && !remainingStats.empty())
            {
                RollStatOffer(remainingStats, outOffer);
            }
            else if (typeRoll < 0.8f && !remainingItems.empty())
            {
                RollItemOffer(remainingItems, outOffer);
            }
            else if (!remainingWeapons.empty())
            {
                RollWeaponOffer(remainingWeapons, outOffer);
            }
            else if (!remainingStats.empty())
            {
                RollStatOffer(remainingStats, outOffer);
            }
            else if (!remainingItems.empty())
            {
                RollItemOffer(remainingItems, outOffer);
            }
            else
            {
                RollWeaponOffer(remainingWeapons, outOffer);
            }
        }
    }

    bool FShop::Reroll(int& materials,
                       const std::vector<std::string>& ownedItemIds,
                       const std::vector<FWeaponRuntime>& equippedWeapons,
                       std::vector<FShopItemDef>& outOffer)
    {
        const int cost = GetRerollCost();
        if (materials < cost)
        {
            return false;
        }
        materials -= cost;
        Roll(4, ownedItemIds, equippedWeapons, outOffer);
        return true;
    }
}
