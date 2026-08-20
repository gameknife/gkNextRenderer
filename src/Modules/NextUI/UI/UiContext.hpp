#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <imgui.h>

namespace NextUI::Foundation
{
    enum class EUiSurfaceKind : uint8_t
    {
        MainWindow,
        RemoteView,
    };

    struct FUiMetrics
    {
        float scale = 1.0f;
        float spacing = 6.0f;
        float radius = 5.0f;
        float controlHeight = 27.0f;
        float toolbarHeight = 38.0f;
        float titleBarHeight = 48.0f;
        float footerHeight = 30.0f;

        static FUiMetrics FromScale(float scale);
    };

    struct FUiContext
    {
        ImGuiContext* imguiContext = nullptr;
        ImFont* defaultFont = nullptr;
        ImFont* titleFont = nullptr;
        FUiMetrics metrics{};
        EUiSurfaceKind surfaceKind = EUiSurfaceKind::MainWindow;
        bool allowWindowCommands = true;
    };
}
