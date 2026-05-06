#pragma once

#include "Common/CoreMinimal.hpp"
#include "Voyage3DPort.hpp"
#include "Voyage3DShip.hpp"

#include <random>

namespace Voyage3D
{
    void RefreshPortPrices(FPortRuntime& port, const std::vector<FGoodsDef>& goods, std::mt19937& rng);
    bool BuyGood(FPortRuntime& port, FShipRuntime& ship, int& playerGold, const std::string& goodId, int qty);
    bool SellGood(FPortRuntime& port, FShipRuntime& ship, int& playerGold, const std::string& goodId, int qty);
}
