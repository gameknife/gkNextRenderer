#pragma once

#include <imgui.h>

namespace NextUI::Scaling
{
    float GetViewportUiScale(const ImGuiViewport* viewport, float baseWidth = 1280.0f, float baseHeight = 720.0f);
}
