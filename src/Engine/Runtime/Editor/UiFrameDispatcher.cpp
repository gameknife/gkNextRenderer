#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Runtime/Editor/UiFrameDispatcher.hpp"

#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/DebugUiProvider.hpp"

namespace NextUI
{
    void FUiFrameDispatcher::DrawDeveloperLayers(NextEngine& engine,
                                                  const Statistics& statistics,
                                                  Runtime::FrameProfiler* profiler,
                                                  const EUiDeveloperLayer layers,
                                                  const bool suppressStatisticsOverlay)
    {
        if (layers == EUiDeveloperLayer::None)
        {
            return;
        }
        if (Runtime::IDebugUiProvider* provider = engine.GetDebugUiProvider())
        {
            provider->DrawUiPanels(engine, statistics, profiler, layers, suppressStatisticsOverlay);
        }
    }
}
