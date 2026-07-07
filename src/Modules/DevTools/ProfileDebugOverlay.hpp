#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"

namespace Runtime
{
    void DrawProfileDebugOverlay(NextEngine& engine, const NextUI::Statistics& statistics, Runtime::FrameProfiler* profiler,
                                 float topOffset);
}
