#pragma once

#include "Common/CoreMinimal.hpp"
#include "Brotato3DDataLoader.hpp"

#include <random>

namespace Brotato3D
{
    class FShop
    {
    public:
        void SetItems(std::vector<FShopItemDef> items);
        void SetWaveIndex(int waveIndex) { waveIndex_ = waveIndex; }
        void Roll(int count, std::vector<FShopItemDef>& outOffer);
        bool Reroll(int& materials, std::vector<FShopItemDef>& outOffer);
        int GetRerollCost() const { return 2 + waveIndex_; }

    private:
        std::vector<FShopItemDef> items_;
        int waveIndex_ = 0;
        std::mt19937 rng_{std::random_device{}()};
    };
}
