#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Brotato3DDataLoader.hpp"
#include "Brotato3DWeapon.hpp"

#include <random>

namespace Brotato3D
{
    class FShop
    {
    public:
        void SetItems(std::vector<FShopItemDef> items);
        void SetPassiveItems(std::vector<FItemDef> items);
        void SetWeapons(std::map<std::string, FWeaponDef> weapons);
        void SetWaveIndex(int waveIndex) { waveIndex_ = waveIndex; }
        void Roll(int count,
                  const std::vector<std::string>& ownedItemIds,
                  const std::vector<FWeaponRuntime>& equippedWeapons,
                  std::vector<FShopItemDef>& outOffer);
        bool Reroll(int& materials,
                    const std::vector<std::string>& ownedItemIds,
                    const std::vector<FWeaponRuntime>& equippedWeapons,
                    std::vector<FShopItemDef>& outOffer);
        int GetRerollCost() const { return 2 + waveIndex_; }

    private:
        FShopItemDef MakeOfferFromItem(const FItemDef& item) const;
        FShopItemDef MakeOfferFromWeapon(const std::string& weaponId, const FWeaponDef& weapon) const;
        bool CanOfferWeapon(const std::string& weaponId, const std::vector<FWeaponRuntime>& equippedWeapons) const;
        void RollStatOffer(std::vector<FShopItemDef>& remainingStats, std::vector<FShopItemDef>& outOffer);
        void RollItemOffer(std::vector<FItemDef>& remainingItems, std::vector<FShopItemDef>& outOffer);
        void RollWeaponOffer(std::vector<std::pair<std::string, FWeaponDef>>& remainingWeapons, std::vector<FShopItemDef>& outOffer);

        std::vector<FShopItemDef> items_;
        std::vector<FItemDef> passiveItems_;
        std::map<std::string, FWeaponDef> weapons_;
        int waveIndex_ = 0;
        std::mt19937 rng_{std::random_device{}()};
    };
}
