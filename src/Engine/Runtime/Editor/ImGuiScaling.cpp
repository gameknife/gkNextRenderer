#include "Engine/Runtime/Editor/ImGuiScaling.hpp"

#include <algorithm>

namespace NextUI::Scaling
{
    float GetViewportUiScale(const ImGuiViewport* viewport, float baseWidth, float baseHeight)
    {
        if (!viewport || viewport->Size.x <= 0.0f || viewport->Size.y <= 0.0f || baseWidth <= 0.0f || baseHeight <= 0.0f)
        {
            return 1.0f;
        }

        return std::max(0.75f, std::min(viewport->Size.x / baseWidth, viewport->Size.y / baseHeight));
    }
}
