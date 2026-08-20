#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <imgui.h>

namespace NextUI::Foundation
{
    enum class EColor
    {
        Text,
        TextMuted,
        TextDim,
        Background,
        Surface,
        SurfaceElevated,
        SurfaceHover,
        Border,
        BorderStrong,
        Accent,
        AccentHover,
        Brand,
        Success,
        Warning,
        Danger,
        Blue,
    };

    ImVec4 Color(EColor color, float alpha = 1.0f);
    ImU32 ColorU32(EColor color, float alpha = 1.0f);

    // Installs the product-neutral desktop theme into the current ImGui context.
    // The function deliberately has no Engine or DevTools dependency.
    void ApplyTheme();
}
