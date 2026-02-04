#pragma once

namespace MagicaLego
{
    // Block grid unit sizes (world space)
    namespace Grid
    {
        constexpr float UnitX = 0.08f;
        constexpr float UnitY = 0.095f;
        constexpr float UnitZ = 0.08f;
        constexpr float HeightOffset = 0.0475f;
    }

    // UI layout constants
    namespace UI
    {
        constexpr float TitleBarHeight = 40.0f;
        constexpr float TitleBarControlWidth = TitleBarHeight * 3;
        constexpr float IconSize = 64.0f;
        constexpr float PaletteSize = 46.0f;
        constexpr float ButtonSize = 36.0f;
        constexpr float BuildBarWidth = 240.0f;
        constexpr float SideBarWidth = 300.0f;
        constexpr float Padding = 20.0f;
    }

    // Animation timing presets
    namespace Anim
    {
        constexpr float Fast = 0.2f;
        constexpr float Normal = 0.3f;
        constexpr float Slow = 0.4f;
    }

    // AI service limits
    namespace AI
    {
        constexpr int MaxBlocksToList = 100;
        constexpr int MaxOutputTokens = 819200;
        constexpr long RequestTimeoutSeconds = 120L;
    }
}
