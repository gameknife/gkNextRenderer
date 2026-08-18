#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Editor/UiFrame.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"

class NextEngine;

namespace NextUI
{
    struct Statistics;

    class FUiFrameDispatcher final
    {
    public:
        static void DrawDeveloperLayers(NextEngine& engine,
                                        const Statistics& statistics,
                                        EUiDeveloperLayer layers,
                                        bool suppressStatisticsOverlay);
    };
}
