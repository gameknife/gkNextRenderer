#include "Voyage3DTrade.hpp"

namespace
{
    bool Contains(const std::vector<std::string>& values, const std::string& id)
    {
        return std::find(values.begin(), values.end(), id) != values.end();
    }
}

namespace Voyage3D
{
    void RefreshPortPrices(FPortRuntime& port, const std::vector<FGoodsDef>& goods, std::mt19937& rng)
    {
        std::uniform_real_distribution<float> noiseDist(0.85f, 1.15f);
        port.currentPrices.clear();
        for (const FGoodsDef& good : goods)
        {
            const float supplyFactor = Contains(good.supplyPorts, port.def.id) ? -0.40f : 0.0f;
            const float demandFactor = Contains(good.demandPorts, port.def.id) ? 0.60f : 0.0f;
            float price = static_cast<float>(good.basePrice) * (1.0f + supplyFactor) * (1.0f + demandFactor) * noiseDist(rng);
            if (const auto discountIt = port.nextVisitDiscountFactor.find(good.id); discountIt != port.nextVisitDiscountFactor.end())
            {
                price *= std::max(0.25f, 1.0f + discountIt->second);
            }
            port.currentPrices[good.id] = std::max(1, static_cast<int>(std::lround(price)));
            if (!port.stock.contains(good.id))
            {
                port.stock[good.id] = 100;
            }
        }
        port.nextVisitDiscountFactor.clear();
    }

    bool BuyGood(FPortRuntime& port, FShipRuntime& ship, int& playerGold, const std::string& goodId, int qty)
    {
        if (qty <= 0 || !port.currentPrices.contains(goodId))
        {
            return false;
        }
        const int price = port.currentPrices.at(goodId) * qty;
        if (playerGold < price || ship.cargoUsed + qty > ship.def.cargoMax)
        {
            return false;
        }

        playerGold -= price;
        ship.cargo[goodId] += qty;
        ship.cargoUsed += qty;
        port.stock[goodId] = std::max(0, port.stock[goodId] - qty);
        return true;
    }

    bool SellGood(FPortRuntime& port, FShipRuntime& ship, int& playerGold, const std::string& goodId, int qty)
    {
        if (qty <= 0 || !port.currentPrices.contains(goodId))
        {
            return false;
        }
        const auto cargoIt = ship.cargo.find(goodId);
        if (cargoIt == ship.cargo.end() || cargoIt->second < qty)
        {
            return false;
        }

        playerGold += port.currentPrices.at(goodId) * qty;
        cargoIt->second -= qty;
        ship.cargoUsed = std::max(0, ship.cargoUsed - qty);
        port.stock[goodId] += qty;
        if (cargoIt->second <= 0)
        {
            ship.cargo.erase(cargoIt);
        }
        return true;
    }
}
