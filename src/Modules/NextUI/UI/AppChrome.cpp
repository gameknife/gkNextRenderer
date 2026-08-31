#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/UI/AppChrome.hpp"

#include "Modules/NextUI/UI/UiScopes.hpp"
#include "Modules/NextUI/UI/UiTheme.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

namespace NextUI::Foundation
{
    namespace
    {
        constexpr float controlWidth = 46.0f;

        void DrawBrandMark(ImDrawList* drawList, const ImVec2 min, const float size)
        {
            const ImU32 accent = ColorU32(EColor::Accent);
            const ImU32 brand = ColorU32(EColor::Brand);
            const ImVec2 center(min.x + size * 0.5f, min.y + size * 0.5f);
            drawList->AddCircleFilled(center, size * 0.34f, ColorU32(EColor::SurfaceElevated), 16);
            drawList->AddTriangleFilled(
                ImVec2(center.x - size * 0.18f, center.y + size * 0.16f),
                ImVec2(center.x, center.y - size * 0.20f),
                ImVec2(center.x + size * 0.18f, center.y + size * 0.16f), brand);
            drawList->AddCircleFilled(center, size * 0.07f, accent, 10);
        }

        float CalcFontTextWidth(ImFont* font, const char* text)
        {
            if (text == nullptr || text[0] == '\0')
            {
                return 0.0f;
            }
            ImFont* activeFont = font != nullptr ? font : ImGui::GetFont();
            return activeFont->CalcTextSizeA(activeFont->LegacySize, FLT_MAX, 0.0f, text).x;
        }

        bool ControlButton(const char* label, const char* tooltip, const ImVec2 size, const bool danger = false)
        {
            FScopedStyle style;
            style.Add(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f))
                .Add(ImGuiStyleVar_FrameRounding, 0.0f)
                .Add(ImGuiStyleVar_FrameBorderSize, 0.0f)
                .Add(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f))
                .Add(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f))
                .Add(ImGuiCol_ButtonHovered, danger ? Color(EColor::Danger, 0.90f) : Color(EColor::SurfaceHover))
                .Add(ImGuiCol_ButtonActive, danger ? Color(EColor::Danger) : Color(EColor::SurfaceHover, 0.92f));
            const bool pressed = ImGui::Button(label, size);
            if (tooltip != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("%s", tooltip);
            }
            return pressed;
        }
    }

    FAppChromeResult DrawAppTitleBar(const FUiContext& context, const FAppTitleBarOptions& options)
    {
        FAppChromeResult result;
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return result;
        }

        const float controlsWidth = context.allowWindowCommands ? controlWidth * 3.0f : 0.0f;
        ImFont* titleFont = context.titleFont != nullptr ? context.titleFont : context.defaultFont;
        if (titleFont == nullptr) titleFont = ImGui::GetFont();
        const float brandTextWidth = CalcFontTextWidth(titleFont, options.appName);
        const float brandWidth = options.brandHorizontalPadding * 2.0f + options.brandIconSize +
            options.brandTextSpacing + brandTextWidth;
        const float rightWidth = options.rightContentWidth + controlsWidth;
        const float menuWidth = std::max(
            0.0f, viewport->Size.x - brandWidth - rightWidth - options.menuTrailingPadding);
        float menuRight = viewport->Pos.x + brandWidth;
        const ImVec2 menuFramePadding = ImGui::GetStyle().FramePadding;

        ImGui::GetBackgroundDrawList()->AddRectFilled(
            viewport->Pos, viewport->Pos + ImVec2(viewport->Size.x, options.height),
            ColorU32(EColor::Background), 0.0f);

        constexpr ImGuiWindowFlags baseFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
        FScopedStyle chromeStyle;
        chromeStyle.Add(ImGuiStyleVar_WindowRounding, 0.0f)
            .Add(ImGuiStyleVar_WindowBorderSize, 0.0f)
            .Add(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(brandWidth, options.height), ImGuiCond_Always);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0.0f);
        {
            FScopedStyle brandStyle;
            brandStyle.Add(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            FScopedWindow brandWindow(options.windowId, nullptr, baseFlags);
            if (brandWindow)
            {
                ImGui::SetCursorPos(ImVec2(options.brandHorizontalPadding,
                    std::floor((options.height - options.brandIconSize) * 0.5f)));
                const ImVec2 iconMin = ImGui::GetCursorScreenPos();
                if (options.drawBrandIcon)
                {
                    options.drawBrandIcon(ImGui::GetWindowDrawList(), iconMin, options.brandIconSize);
                }
                else
                {
                    DrawBrandMark(ImGui::GetWindowDrawList(), iconMin, options.brandIconSize);
                }
                ImGui::Dummy(ImVec2(options.brandIconSize, options.brandIconSize));
                ImGui::SameLine(0.0f, options.brandTextSpacing);
                ImGui::PushFont(titleFont);
                ImGui::SetCursorPosY((options.height - ImGui::GetTextLineHeight()) * 0.5f);
                ImGui::TextUnformatted(options.appName != nullptr ? options.appName : "");
                ImGui::PopFont();
            }
        }

        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + brandWidth,
                   viewport->Pos.y + (options.height - ImGui::GetTextLineHeight() - ImGui::GetStyle().FramePadding.y * 2.0f) * 0.5f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(menuWidth, options.height), ImGuiCond_Always);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const bool menuWindowVisible = ImGui::Begin(
            options.menuWindowId, nullptr, baseFlags | ImGuiWindowFlags_MenuBar);
        ImGui::PopStyleVar();
        if (menuWindowVisible)
        {
            FScopedStyle menuBarStyle;
            menuBarStyle.Add(ImGuiStyleVar_FramePadding, menuFramePadding)
                .Add(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 6.0f));
            if (ImGui::BeginMenuBar())
            {
                if (options.drawMenuBar)
                {
                    menuRight = std::max(menuRight, options.drawMenuBar());
                }
                ImGui::EndMenuBar();
            }
        }
        ImGui::End();

        if (rightWidth > 0.0f)
        {
            ImGui::SetNextWindowPos(
                viewport->Pos + ImVec2(viewport->Size.x - rightWidth, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(rightWidth, options.height), ImGuiCond_Always);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::SetNextWindowBgAlpha(0.0f);
            {
                FScopedStyle rightStyle;
                rightStyle.Add(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
                FScopedWindow rightWindow(options.rightWindowId, nullptr, baseFlags);
                if (rightWindow)
                {
                    if (options.drawRightContent) options.drawRightContent();
                    ImGui::SetCursorPos(ImVec2(options.rightContentWidth, 0.0f));
                    if (context.allowWindowCommands)
                    {
                        const ImVec2 controlSize(controlWidth, options.height);
                        if (ControlButton(ICON_FA_WINDOW_MINIMIZE, "Minimize", controlSize))
                            result.action = EAppChromeAction::Minimize;
                        ImGui::SameLine(0.0f, 0.0f);
                        if (ControlButton(options.isMaximized ? ICON_FA_WINDOW_RESTORE : ICON_FA_WINDOW_MAXIMIZE,
                                          options.isMaximized ? "Restore" : "Maximize", controlSize))
                            result.action = EAppChromeAction::ToggleMaximize;
                        ImGui::SameLine(0.0f, 0.0f);
                        if (ControlButton(ICON_FA_XMARK, "Close", controlSize, true))
                            result.action = EAppChromeAction::Close;
                    }
                }
            }
        }

        result.dragHeight = options.height;
        result.dragLeftReservedWidth = std::max(
            brandWidth + 12.0f, menuRight - viewport->Pos.x + options.menuHitPadding);
        result.dragRightReservedWidth = rightWidth;
        return result;
    }

    void DrawBottomBar(const FBottomBarOptions& options)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return;
        }
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - options.height), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, options.height), ImGuiCond_Always);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(1.0f);
        FScopedStyle style;
        style.Add(ImGuiStyleVar_WindowPadding, ImVec2(options.horizontalPadding, options.verticalPadding))
            .Add(ImGuiStyleVar_WindowMinSize, ImVec2(0.0f, 0.0f))
            .Add(ImGuiStyleVar_WindowBorderSize, 0.0f)
            .Add(ImGuiStyleVar_WindowRounding, 0.0f)
            .Add(ImGuiCol_WindowBg, Color(EColor::Background, 0.98f));
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoDocking;
        FScopedWindow window(options.windowId, nullptr, flags);
        if (!window)
        {
            return;
        }
        if (options.drawLeftContent) options.drawLeftContent();
        if (options.drawCenterContent)
        {
            ImGui::SameLine();
            ImGui::SetCursorPosX((viewport->Size.x - options.centerWidth) * 0.5f);
            options.drawCenterContent();
        }
        if (options.drawRightContent)
        {
            ImGui::SameLine();
            ImGui::SetCursorPosX(viewport->Size.x - options.rightWidth - options.horizontalPadding);
            options.drawRightContent();
        }
    }
}
