#pragma once
#include "Common/CoreMinimal.hpp"
#include <imgui.h>

namespace MagicaLego::Style
{
    // Lego Colors
    inline constexpr ImVec4 LegoYellow(1.0f, 0.8f, 0.0f, 1.0f); // Typical Lego Yellow
    inline constexpr ImVec4 LegoRed(0.8f, 0.1f, 0.1f, 1.0f);
    inline constexpr ImVec4 LegoBlue(0.0f, 0.3f, 0.8f, 1.0f);
    inline constexpr ImVec4 TireGray(0.15f, 0.15f, 0.15f, 1.0f); // Dark gray for background

    // Glassmorphism
    inline constexpr ImVec4 GlassBackground(0.1f, 0.1f, 0.1f, 0.6f); // Semi-transparent black
    inline constexpr float WindowRounding = 12.0f;
    inline constexpr float FrameRounding = 6.0f;
    inline constexpr float PopupRounding = 8.0f;
    
    // Application function
    void ApplyStyle();
}
