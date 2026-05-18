#pragma once

#include "Common/CoreMinimal.hpp"

#include <imgui.h>

namespace KongLie3D::Style
{
    inline constexpr float UiScale = 1.5f;
    inline constexpr ImVec4 Accent(0.32f, 0.62f, 0.95f, 1.0f);
    inline constexpr ImVec4 Hostile(0.85f, 0.32f, 0.32f, 1.0f);
    inline constexpr ImVec4 Highlight(0.97f, 0.83f, 0.33f, 1.0f);
    inline constexpr ImVec4 Surface(0.06f, 0.08f, 0.12f, 0.92f);
    inline constexpr ImVec4 SurfaceAlt(0.10f, 0.13f, 0.18f, 0.95f);
    inline constexpr ImVec4 Border(1.0f, 1.0f, 1.0f, 0.10f);
    inline constexpr ImVec4 TextDim(0.74f, 0.78f, 0.86f, 1.0f);
}

namespace KongLie3D
{
    inline constexpr float ScaleUi(float value)
    {
        return value * Style::UiScale;
    }

    inline ImVec2 ScaleUi(float x, float y)
    {
        return ImVec2(ScaleUi(x), ScaleUi(y));
    }
}

namespace KongLie3D::KongLieFonts
{
    extern ImFont* Body;
    extern ImFont* Title;
    extern ImFont* Display;
}

namespace KongLie3D
{
    void ApplyKongLieImGuiStyle();
    void PushPanelStyle();
    void PopPanelStyle();
}
