#pragma once

#include "Engine/Common/CoreMinimal.hpp"

class NextEngine;
class VulkanGpuTimer;
struct Statistics;

namespace Runtime
{
    void DrawProfileDebugOverlay(NextEngine& engine, const Statistics& statistics, VulkanGpuTimer* gpuTimer,
                                 float topOffset);
}
