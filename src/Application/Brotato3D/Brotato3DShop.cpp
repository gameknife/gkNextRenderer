#include "Brotato3DShop.hpp"

namespace Brotato3D
{
    void FShop::SetItems(std::vector<FShopItemDef> items)
    {
        items_ = std::move(items);
    }

    void FShop::Roll(int count, std::vector<FShopItemDef>& outOffer)
    {
        outOffer.clear();
        std::vector<FShopItemDef> remaining = items_;
        while (!remaining.empty() && static_cast<int>(outOffer.size()) < count)
        {
            int totalWeight = 0;
            for (const FShopItemDef& item : remaining)
            {
                totalWeight += std::max(1, item.weight);
            }

            std::uniform_int_distribution<int> dist(1, totalWeight);
            int pick = dist(rng_);
            size_t chosenIndex = 0;
            for (size_t index = 0; index < remaining.size(); ++index)
            {
                pick -= std::max(1, remaining[index].weight);
                if (pick <= 0)
                {
                    chosenIndex = index;
                    break;
                }
            }

            outOffer.push_back(remaining[chosenIndex]);
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(chosenIndex));
        }
    }

    bool FShop::Reroll(int& materials, std::vector<FShopItemDef>& outOffer)
    {
        const int cost = GetRerollCost();
        if (materials < cost)
        {
            return false;
        }
        materials -= cost;
        Roll(4, outOffer);
        return true;
    }
}
