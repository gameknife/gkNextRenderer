#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Editor/UI/UiContext.hpp"

#include <functional>
#include <imgui.h>

namespace NextUI::Foundation
{
    enum class EAppChromeAction : uint8_t
    {
        None,
        Minimize,
        ToggleMaximize,
        Close,
    };

    struct FAppTitleBarOptions
    {
        const char* windowId = "AppTitleBar";
        const char* menuWindowId = "AppTitleBarMenu";
        const char* rightWindowId = "AppTitleBarRight";
        const char* appName = "";
        float height = 48.0f;
        float rightContentWidth = 0.0f;
        float brandHorizontalPadding = 14.0f;
        float brandIconSize = 48.0f;
        float brandTextSpacing = 10.0f;
        float menuTrailingPadding = 8.0f;
        float menuHitPadding = 14.0f;
        bool isMaximized = false;
        std::function<void(ImDrawList*, ImVec2, float)> drawBrandIcon;
        std::function<float()> drawMenuBar;
        std::function<void()> drawRightContent;
    };

    struct FAppChromeResult
    {
        EAppChromeAction action = EAppChromeAction::None;
        float dragHeight = 0.0f;
        float dragLeftReservedWidth = 0.0f;
        float dragRightReservedWidth = 0.0f;
    };

    struct FBottomBarOptions
    {
        const char* windowId = "AppBottomBar";
        float height = 30.0f;
        float horizontalPadding = 10.0f;
        float verticalPadding = 4.0f;
        float centerWidth = 0.0f;
        float rightWidth = 0.0f;
        std::function<void()> drawLeftContent;
        std::function<void()> drawCenterContent;
        std::function<void()> drawRightContent;
    };

    FAppChromeResult DrawAppTitleBar(const FUiContext& context, const FAppTitleBarOptions& options);
    void DrawBottomBar(const FBottomBarOptions& options);
}
