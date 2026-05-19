#pragma once

#include "Engine/Common/CoreMinimal.hpp"

class NextEngine;
class VulkanGpuTimer;

namespace NextUI
{
    struct Statistics;
}

namespace Runtime
{
    void DrawProfileDebugOverlay(NextEngine& engine, const NextUI::Statistics& statistics, VulkanGpuTimer* gpuTimer,
                                 float topOffset);
}
