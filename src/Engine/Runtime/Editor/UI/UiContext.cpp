#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Runtime/Editor/UI/UiContext.hpp"

#include <algorithm>

namespace NextUI::Foundation
{
    FUiMetrics FUiMetrics::FromScale(const float requestedScale)
    {
        const float safeScale = std::clamp(requestedScale, 0.5f, 4.0f);
        return {
            .scale = safeScale,
            .spacing = 6.0f * safeScale,
            .radius = 5.0f * safeScale,
            .controlHeight = 27.0f * safeScale,
            .toolbarHeight = 38.0f * safeScale,
            .titleBarHeight = 48.0f * safeScale,
            .footerHeight = 30.0f * safeScale,
        };
    }
}
