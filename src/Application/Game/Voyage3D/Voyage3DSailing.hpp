#pragma once

#include "Common/CoreMinimal.hpp"
#include "Voyage3DCommon.hpp"
#include "Voyage3DDataLoader.hpp"
#include "Voyage3DPort.hpp"
#include "Voyage3DShip.hpp"

namespace Voyage3D
{
    bool IsInsideNavigableLand(const glm::vec3& worldPos,
                               const std::vector<FLandmassBlock>& landBlocks,
                               const std::vector<FPortRuntime>& ports);
    void UpdatePlayerShip(FShipRuntime& ship,
                          double deltaSec,
                          const FInputState& input,
                          const std::vector<FLandmassBlock>& landBlocks,
                          const std::vector<FPortRuntime>& ports);
}
