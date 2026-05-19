#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Runtime/Editor/ProfessionalUI.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"

#include <algorithm>
#include <imgui_internal.h>
#include <fmt/format.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

namespace NextUI::Theme
{
    namespace
    {
        constexpr float kTitleBarControlButtonWidth = 46.0f;
        constexpr float kTitleBarControlButtonCount = 3.0f;
        constexpr float kTitleBarControlsWidth = kTitleBarControlButtonWidth * kTitleBarControlButtonCount;

        ImVec4 WithAlpha(ImVec4 color, float alpha)
        {
            color.w *= alpha;
            return color;
        }

        float CalcFontTextWidth(ImFont* font, const char* text)
        {
            if (text == nullptr || text[0] == '\0')
            {
                return 0.0f;
            }

            ImFont* activeFont = font != nullptr ? font : ImGui::GetFont();
            return activeFont->CalcTextSizeA(activeFont->FontSize, FLT_MAX, 0.0f, text).x;
        }

        bool DrawWindowControlButton(const char* label, const char* tooltip, ImVec2 size, ImVec4 hoverColor,
                                     ImVec4 activeColor)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
            const bool pressed = ImGui::Button(label, size);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            DrawTooltip(tooltip);
            return pressed;
        }

        float CalcFormLabelWidth(float contentWidth, float ratio, float minLabelWidth, float maxLabelWidth)
        {
            return std::clamp(contentWidth * ratio, minLabelWidth, maxLabelWidth);
        }

        float CalcBadgeWidth(const char* label)
        {
            const float textWidth = ImGui::CalcTextSize(label != nullptr ? label : "").x;
            return textWidth + 14.0f;
        }
    } // namespace

    ImVec4 Color(EColor color, float alpha)
    {
        ImVec4 result;
        switch (color)
        {
        case EColor::Text:
            result = ImVec4(0.88f, 0.90f, 0.93f, 1.0f);
            break;
        case EColor::TextMuted:
            result = ImVec4(0.63f, 0.67f, 0.73f, 1.0f);
            break;
        case EColor::TextDim:
            result = ImVec4(0.43f, 0.47f, 0.53f, 1.0f);
            break;
        case EColor::Background:
            result = ImVec4(0.055f, 0.058f, 0.064f, 1.0f);
            break;
        case EColor::Surface:
            result = ImVec4(0.105f, 0.112f, 0.122f, 1.0f);
            break;
        case EColor::SurfaceElevated:
            result = ImVec4(0.145f, 0.153f, 0.166f, 1.0f);
            break;
        case EColor::SurfaceHover:
            result = ImVec4(0.185f, 0.198f, 0.218f, 1.0f);
            break;
        case EColor::Border:
            result = ImVec4(0.22f, 0.235f, 0.255f, 1.0f);
            break;
        case EColor::BorderStrong:
            result = ImVec4(0.30f, 0.325f, 0.36f, 1.0f);
            break;
        case EColor::Accent:
            result = ImVec4(0.18f, 0.43f, 0.78f, 1.0f);
            break;
        case EColor::AccentHover:
            result = ImVec4(0.26f, 0.53f, 0.90f, 1.0f);
            break;
        case EColor::Brand:
            result = ImVec4(0.95f, 0.58f, 0.14f, 1.0f);
            break;
        case EColor::Success:
            result = ImVec4(0.22f, 0.78f, 0.38f, 1.0f);
            break;
        case EColor::Warning:
            result = ImVec4(0.95f, 0.70f, 0.24f, 1.0f);
            break;
        case EColor::Danger:
            result = ImVec4(0.92f, 0.25f, 0.28f, 1.0f);
            break;
        case EColor::Blue:
        default:
            result = ImVec4(0.38f, 0.62f, 0.94f, 1.0f);
            break;
        }
        return WithAlpha(result, alpha);
    }

    ImU32 ColorU32(EColor color, float alpha)
    {
        return ImGui::GetColorU32(Color(color, alpha));
    }

    void ApplyProfessionalTheme()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::StyleColorsDark(&style);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = Color(EColor::Text);
        colors[ImGuiCol_TextDisabled] = Color(EColor::TextDim);
        colors[ImGuiCol_WindowBg] = Color(EColor::Surface, 0.98f);
        colors[ImGuiCol_ChildBg] = Color(EColor::Background, 0.62f);
        colors[ImGuiCol_PopupBg] = Color(EColor::SurfaceElevated, 0.98f);
        colors[ImGuiCol_Border] = Color(EColor::Border);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_FrameBg] = Color(EColor::Background, 0.96f);
        colors[ImGuiCol_FrameBgHovered] = Color(EColor::SurfaceHover, 0.96f);
        colors[ImGuiCol_FrameBgActive] = Color(EColor::SurfaceHover);
        colors[ImGuiCol_TitleBg] = Color(EColor::Background);
        colors[ImGuiCol_TitleBgActive] = Color(EColor::Background);
        colors[ImGuiCol_TitleBgCollapsed] = Color(EColor::Background);
        colors[ImGuiCol_MenuBarBg] = Color(EColor::Background);
        colors[ImGuiCol_ScrollbarBg] = Color(EColor::Background, 0.26f);
        colors[ImGuiCol_ScrollbarGrab] = Color(EColor::BorderStrong, 0.92f);
        colors[ImGuiCol_ScrollbarGrabHovered] = Color(EColor::TextDim);
        colors[ImGuiCol_ScrollbarGrabActive] = Color(EColor::TextMuted);
        colors[ImGuiCol_CheckMark] = Color(EColor::AccentHover);
        colors[ImGuiCol_SliderGrab] = Color(EColor::Blue);
        colors[ImGuiCol_SliderGrabActive] = Color(EColor::AccentHover);
        colors[ImGuiCol_Button] = Color(EColor::SurfaceElevated, 0.94f);
        colors[ImGuiCol_ButtonHovered] = Color(EColor::SurfaceHover);
        colors[ImGuiCol_ButtonActive] = Color(EColor::Accent);
        colors[ImGuiCol_Header] = Color(EColor::SurfaceElevated, 0.90f);
        colors[ImGuiCol_HeaderHovered] = Color(EColor::SurfaceHover, 0.96f);
        colors[ImGuiCol_HeaderActive] = Color(EColor::Accent, 0.82f);
        colors[ImGuiCol_Separator] = Color(EColor::Border);
        colors[ImGuiCol_SeparatorHovered] = Color(EColor::AccentHover);
        colors[ImGuiCol_SeparatorActive] = Color(EColor::AccentHover);
        colors[ImGuiCol_ResizeGrip] = Color(EColor::BorderStrong, 0.55f);
        colors[ImGuiCol_ResizeGripHovered] = Color(EColor::AccentHover);
        colors[ImGuiCol_ResizeGripActive] = Color(EColor::AccentHover);
        colors[ImGuiCol_Tab] = Color(EColor::Surface, 0.96f);
        colors[ImGuiCol_TabHovered] = Color(EColor::SurfaceHover);
        colors[ImGuiCol_TabActive] = Color(EColor::SurfaceElevated, 0.98f);
        colors[ImGuiCol_TabUnfocused] = Color(EColor::Surface, 0.82f);
        colors[ImGuiCol_TabUnfocusedActive] = Color(EColor::SurfaceElevated, 0.88f);
        colors[ImGuiCol_DockingPreview] = Color(EColor::Accent, 0.55f);
        colors[ImGuiCol_DockingEmptyBg] = Color(EColor::Background);
        colors[ImGuiCol_TableHeaderBg] = Color(EColor::SurfaceElevated, 0.92f);
        colors[ImGuiCol_TableBorderStrong] = Color(EColor::BorderStrong, 0.92f);
        colors[ImGuiCol_TableBorderLight] = Color(EColor::Border, 0.76f);
        colors[ImGuiCol_TableRowBg] = Color(EColor::Background, 0.10f);
        colors[ImGuiCol_TableRowBgAlt] = Color(EColor::Surface, 0.18f);
        colors[ImGuiCol_PlotHistogram] = Color(EColor::Success);
        colors[ImGuiCol_PlotHistogramHovered] = Color(EColor::AccentHover);
        colors[ImGuiCol_PlotLines] = Color(EColor::Blue);
        colors[ImGuiCol_PlotLinesHovered] = Color(EColor::AccentHover);
        colors[ImGuiCol_TextSelectedBg] = Color(EColor::Accent, 0.45f);
        colors[ImGuiCol_NavHighlight] = Color(EColor::AccentHover);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.62f);

        style.WindowPadding = ImVec2(8.0f, 7.0f);
        style.FramePadding = ImVec2(7.0f, 4.0f);
        style.CellPadding = ImVec2(7.0f, 4.0f);
        style.ItemSpacing = ImVec2(6.0f, 5.0f);
        style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
        style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
        style.IndentSpacing = 12.0f;
        style.ScrollbarSize = 10.0f;
        style.GrabMinSize = 10.0f;
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.TabBorderSize = 0.0f;
        style.WindowRounding = 7.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 5.0f;
        style.PopupRounding = 7.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = 7.0f;
        style.TabRounding = 5.0f;
        style.WindowMenuButtonPosition = ImGuiDir_None;
        style.SeparatorTextBorderSize = 1.0f;
    }

    void DrawBrandMark(ImDrawList* drawList, ImVec2 min, float size)
    {
        if (drawList == nullptr)
        {
            return;
        }

        const ImVec2 max(min.x + size, min.y + size);
        const float rounding = std::max(3.0f, size * 0.18f);
        drawList->AddRectFilled(min, max, ColorU32(EColor::Brand, 0.16f), rounding);
        drawList->AddRect(min, max, ColorU32(EColor::Brand, 0.92f), rounding, 0, 1.5f);

        const float pad = size * 0.24f;
        const float stroke = std::max(2.0f, size * 0.10f);
        const ImU32 lineColor = ColorU32(EColor::Brand);
        drawList->AddLine(ImVec2(min.x + pad, min.y + size - pad), ImVec2(min.x + pad, min.y + pad), lineColor, stroke);
        drawList->AddLine(ImVec2(min.x + pad, min.y + pad), ImVec2(min.x + size * 0.52f, min.y + pad), lineColor, stroke);
        drawList->AddLine(ImVec2(min.x + size * 0.52f, min.y + pad), ImVec2(min.x + size * 0.52f, min.y + size - pad), lineColor, stroke);
        drawList->AddLine(ImVec2(min.x + size * 0.52f, min.y + size - pad), ImVec2(min.x + size - pad, min.y + size - pad), lineColor, stroke);
        drawList->AddLine(ImVec2(min.x + size - pad, min.y + size - pad), ImVec2(min.x + size - pad, min.y + pad), lineColor, stroke);
    }

    void DrawAppTitleBar(NextEngine& engine, const FAppTitleBarConfig& config)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return;
        }

        ImDrawList* background = ImGui::GetBackgroundDrawList();
        const ImVec2 titleMin = viewport->Pos;
        const ImVec2 titleMax = viewport->Pos + ImVec2(viewport->Size.x, config.Height);
        background->AddRectFilled(titleMin, titleMax, ColorU32(EColor::Background), 0.0f);

        ImFont* titleFont = config.TitleFont != nullptr ? config.TitleFont : ImGui::GetFont();
        const float brandTextWidth = CalcFontTextWidth(titleFont, config.AppName);
        const float brandWidth =
            config.BrandHorizontalPadding * 2.0f + config.BrandIconSize + config.BrandTextSpacing + brandTextWidth;
        const float rightWidth = config.RightContentWidth + kTitleBarControlsWidth;
        const float menuWidth =
            std::max(0.0f, viewport->Size.x - brandWidth - rightWidth - config.MenuTrailingPadding);
        float menuRight = viewport->Pos.x + brandWidth;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(ImVec2(brandWidth, config.Height));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        
        ImGui::Begin(config.BrandWindowId, nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoDocking);
        ImGui::SetCursorPos(
            ImVec2(config.BrandHorizontalPadding, std::floor((config.Height - config.BrandIconSize) * 0.5f)));
        DrawBrandMark(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), config.BrandIconSize);
        ImGui::Dummy(ImVec2(config.BrandIconSize, config.BrandIconSize));
        ImGui::SameLine(0.0f, config.BrandTextSpacing);
        if (titleFont != nullptr)
        {
            ImGui::PushFont(titleFont);
        }
        ImGui::SetCursorPosY(std::floor((config.Height - ImGui::GetTextLineHeight()) * 0.5f) - 1.0f);
        ImGui::TextUnformatted(config.AppName);
        if (titleFont != nullptr)
        {
            ImGui::PopFont();
        }
        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + brandWidth, viewport->Pos.y + (config.Height - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::SetNextWindowSize(ImVec2(menuWidth, config.Height));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        
        ImGui::Begin(config.MenuWindowId, nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoDocking);
        ImGui::PopStyleVar(3);
        
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        if (ImGui::BeginMenuBar())
        {
            if (config.DrawMenuBar)
            {
                const float menuBarRightEdge = config.DrawMenuBar();
                menuRight = std::max(menuRight, menuBarRightEdge);
            }
            ImGui::EndMenuBar();
        }
        ImGui::PopStyleVar(2);
        ImGui::End();

        ImGui::SetNextWindowPos(viewport->Pos + ImVec2(viewport->Size.x - rightWidth, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(rightWidth, config.Height));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(config.RightWindowId, nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoDocking);
        if (config.DrawRightContent)
        {
            config.DrawRightContent();
        }
        ImGui::SetCursorPos(ImVec2(config.RightContentWidth, 0.0f));

        if (DrawWindowControlButton(ICON_FA_WINDOW_MINIMIZE, "Minimize",
                                    ImVec2(kTitleBarControlButtonWidth, config.Height), Color(EColor::SurfaceHover),
                                    Color(EColor::SurfaceHover, 0.92f)))
        {
            if (config.OnMinimize)
            {
                config.OnMinimize();
            }
        }
        ImGui::SameLine(0.0f, 0.0f);
        if (DrawWindowControlButton(config.IsMaximized ? ICON_FA_WINDOW_RESTORE : ICON_FA_WINDOW_MAXIMIZE, "Maximize",
                                    ImVec2(kTitleBarControlButtonWidth, config.Height), Color(EColor::SurfaceHover),
                                    Color(EColor::SurfaceHover, 0.92f)))
        {
            if (config.OnToggleMaximize)
            {
                config.OnToggleMaximize();
            }
        }
        ImGui::SameLine(0.0f, 0.0f);
        if (DrawWindowControlButton(ICON_FA_XMARK, "Close", ImVec2(kTitleBarControlButtonWidth, config.Height),
                                    Color(EColor::Danger, 0.90f), Color(EColor::Danger)))
        {
            if (config.OnClose)
            {
                config.OnClose();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        const float dragLeftReserved =
            std::max(brandWidth + 12.0f, menuRight - viewport->Pos.x + config.MenuHitPadding);
        engine.ConfigureCustomTitleBarDrag(true, config.Height, dragLeftReserved, rightWidth);
    }

    void DrawBottomBar(const FBottomBarConfig& config)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return;
        }

        const float barHeight = config.Height;
        ImGui::SetNextWindowPos(viewport->Pos + ImVec2(0.0f, viewport->Size.y - barHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, barHeight), ImGuiCond_Always);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(1.0f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(config.HorizontalPadding, config.VerticalPadding));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(EColor::Background, 0.98f));

        if (ImGui::Begin(config.WindowId, nullptr, flags))
        {
            ImGui::GetWindowDrawList()->AddLine(
                viewport->Pos + ImVec2(0.0f, viewport->Size.y - barHeight),
                viewport->Pos + ImVec2(viewport->Size.x, viewport->Size.y - barHeight),
                ColorU32(EColor::Border), 1.0f);

            if (config.DrawLeftContent)
            {
                config.DrawLeftContent();
            }

            if (config.DrawCenterContent)
            {
                const float centerWidth = std::max(0.0f, config.CenterWidth);
                const float centerStart = (viewport->Size.x - centerWidth) * 0.5f;
                if (ImGui::GetCursorPosX() < centerStart)
                {
                    ImGui::SameLine(centerStart);
                }
                config.DrawCenterContent();
            }

            if (config.DrawRightContent)
            {
                const float rightWidth = std::max(0.0f, config.RightWidth);
                const float rightStart = viewport->Size.x - rightWidth;
                if (ImGui::GetCursorPosX() < rightStart)
                {
                    ImGui::SameLine(rightStart);
                }
                config.DrawRightContent();
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);
    }

    void DrawStandardBottomBar(NextEngine& engine, const char* windowId, float height)
    {
        NextUI::UserInterface* ui = engine.GetUserInterface();
        const NextEngine::FHotReloadStatus hotReloadStatus = engine.GetHotReloadStatus();
        const auto memoryStats = engine.GetRenderer().Device().CaptureMemoryStats();
        const float memoryFraction =
            memoryStats.deviceLocalBudgetBytes > 0
                ? static_cast<float>(static_cast<double>(memoryStats.deviceLocalUsageBytes) /
                                     static_cast<double>(memoryStats.deviceLocalBudgetBytes))
                : 0.0f;

        const bool shaderLive = hotReloadStatus.shaderHotReloadEnabled && hotReloadStatus.shaderInitialized;
        const char* shaderLabel = shaderLive ? "Shader Live"
            : (hotReloadStatus.shaderHotReloadEnabled ? "Shader Init" : "Shader Off");
        const ImVec4 shaderBackground = shaderLive
            ? Color(EColor::Success, 0.14f)
            : (hotReloadStatus.shaderHotReloadEnabled ? Color(EColor::Warning, 0.14f)
                                                      : Color(EColor::Background, 0.78f));
        const ImVec4 shaderForeground = shaderLive
            ? Color(EColor::Success)
            : (hotReloadStatus.shaderHotReloadEnabled ? Color(EColor::Warning) : Color(EColor::TextMuted));

        const std::string fpsText = fmt::format("FPS {:.0f}", engine.GetFrameRate());
        const std::string memoryText = fmt::format("VRAM {} / {}",
                                                   fmt::format("{:.2f} {}",
                                                               memoryStats.deviceLocalUsageBytes >= (1024ull * 1024ull * 1024ull)
                                                                   ? static_cast<double>(memoryStats.deviceLocalUsageBytes) /
                                                                         static_cast<double>(1024ull * 1024ull * 1024ull)
                                                                   : static_cast<double>(memoryStats.deviceLocalUsageBytes) /
                                                                         static_cast<double>(1024ull * 1024ull),
                                                               memoryStats.deviceLocalUsageBytes >= (1024ull * 1024ull * 1024ull)
                                                                   ? "GB"
                                                                   : "MB"),
                                                   fmt::format("{:.2f} {}",
                                                               memoryStats.deviceLocalBudgetBytes >= (1024ull * 1024ull * 1024ull)
                                                                   ? static_cast<double>(memoryStats.deviceLocalBudgetBytes) /
                                                                         static_cast<double>(1024ull * 1024ull * 1024ull)
                                                                   : static_cast<double>(memoryStats.deviceLocalBudgetBytes) /
                                                                         static_cast<double>(1024ull * 1024ull),
                                                               memoryStats.deviceLocalBudgetBytes >= (1024ull * 1024ull * 1024ull)
                                                                   ? "GB"
                                                                   : "MB"));
        (void)memoryFraction;

        constexpr float kConsoleButtonWidth = 74.0f;
        constexpr float kStatsButtonWidth = 58.0f;
        constexpr float kCaptureButtonWidth = 72.0f;
        constexpr float kButtonHeight = 22.0f;
        constexpr float kSeparatorWidth = 25.0f;
        constexpr float kGapWidth = 8.0f;

        const float rightWidth = kConsoleButtonWidth + kGapWidth + kStatsButtonWidth + kGapWidth + kCaptureButtonWidth +
            kSeparatorWidth + CalcBadgeWidth(shaderLabel) + kSeparatorWidth + ImGui::CalcTextSize(fpsText.c_str()).x +
            kSeparatorWidth + ImGui::CalcTextSize(memoryText.c_str()).x + 18.0f;

        FBottomBarConfig config{};
        config.WindowId = windowId;
        config.Height = height;
        config.RightWidth = rightWidth;
        config.DrawLeftContent = []()
        {
            DrawStatusDot("Ready", true);
        };
        config.DrawRightContent = [&]()
        {
            if (ui != nullptr)
            {
                if (ToolbarButton("Console", "Toggle Console", ui->IsConsoleOpen(), ImVec2(kConsoleButtonWidth, kButtonHeight)))
                {
                    ui->ToggleConsole();
                }
            }
            else
            {
                ToolbarButton("Console", "Console Unavailable", false, ImVec2(kConsoleButtonWidth, kButtonHeight));
            }

            ImGui::SameLine();
            if (ToolbarButton("Stats", "Toggle Stats Overlay", engine.GetUserSettings().ShowOverlay,
                              ImVec2(kStatsButtonWidth, kButtonHeight)))
            {
                engine.GetUserSettings().ShowOverlay = !engine.GetUserSettings().ShowOverlay;
            }

            ImGui::SameLine();
            if (ToolbarButton("Capture", "Take Screenshot", false, ImVec2(kCaptureButtonWidth, kButtonHeight)))
            {
                engine.RequestScreenShot({});
            }

            DrawVerticalSeparator(14.0f, 10.0f, 0.72f);
            DrawBadge(shaderLabel, shaderBackground, shaderForeground);
            DrawVerticalSeparator(14.0f, 10.0f, 0.72f);
            ImGui::TextColored(Color(EColor::TextMuted), "%s", fpsText.c_str());
            DrawVerticalSeparator(14.0f, 10.0f, 0.72f);
            ImGui::TextColored(Color(EColor::TextMuted), "%s", memoryText.c_str());
        };
        DrawBottomBar(config);
    }

    void DrawTooltip(const char* text)
    {
        if (!ImGui::IsItemHovered() || text == nullptr || text[0] == '\0')
        {
            return;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Color(EColor::SurfaceElevated, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::BorderStrong, 0.82f));
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(text);
        ImGui::EndTooltip();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    bool IconButton(const char* label, const char* tooltip, bool active, ImVec2 size)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Color(EColor::Accent, 0.82f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::AccentHover));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::AccentHover));
            ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::AccentHover, 0.88f));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Color(EColor::SurfaceElevated, 0.82f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.96f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::Accent, 0.72f));
            ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.86f));
        }

        const bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
        DrawTooltip(tooltip);
        return pressed;
    }

    bool ToolbarButton(const char* label, const char* tooltip, bool active, ImVec2 size)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        const bool pressed = IconButton(label, tooltip, active, size);
        ImGui::PopStyleVar();
        return pressed;
    }

    void BeginFormRow(const char* label, float ratio, float minLabelWidth, float maxLabelWidth)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
        ImGui::TextUnformatted(label != nullptr ? label : "");
        ImGui::PopStyleColor();

        const float labelWidth = CalcFormLabelWidth(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX(),
                                                    ratio, minLabelWidth, maxLabelWidth);
        ImGui::SameLine(labelWidth);
    }

    bool BeginInsetPanel(const char* id, ImVec2 size, bool border, ImGuiWindowFlags flags, ImVec2 padding,
                         float backgroundAlpha)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Color(EColor::Background, backgroundAlpha));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.82f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
        return ImGui::BeginChild(id, size, border, flags);
    }

    void EndInsetPanel()
    {
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    bool BeginOverlayPanel(const FOverlayPanelConfig& config)
    {
        ImGui::SetNextWindowPos(config.Position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(config.Size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(config.BackgroundAlpha);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, config.Padding);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, config.ItemSpacing);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, config.Rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(config.BackgroundColor, config.BackgroundAlpha));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::BorderStrong, config.BorderAlpha));

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | config.ExtraFlags;
        return ImGui::Begin(config.WindowId, nullptr, flags);
    }

    void EndOverlayPanel()
    {
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
    }

    bool BeginSection(const char* icon, const char* label, bool defaultOpen)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Header, Color(EColor::Background, 0.86f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover, 0.94f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Color(EColor::Accent, 0.48f));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.84f));

        const std::string header = fmt::format("{} {}", icon ? icon : "", label ? label : "");
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;
        if (defaultOpen)
        {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        const bool open = ImGui::CollapsingHeader(header.c_str(), flags);

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);

        if (open)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));
            ImGui::Indent(5.0f);
        }
        return open;
    }

    void EndSection()
    {
        ImGui::Unindent(4.0f);
        ImGui::PopStyleVar();
        ImGui::Spacing();
    }

    void DrawPanelHeader(const char* icon, const char* title, const char* subtitle)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::Text));
        ImGui::Text("%s %s", icon ? icon : "", title ? title : "");
        ImGui::PopStyleColor();

        if (subtitle != nullptr && subtitle[0] != '\0')
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
            ImGui::TextUnformatted(subtitle);
            ImGui::PopStyleColor();
        }

        DrawThinSeparator(0.9f);
    }

    void DrawLabelValue(const char* label, const char* value, ImVec4 valueColor)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, valueColor);
        ImGui::TextUnformatted(value);
        ImGui::PopStyleColor();
    }

    void DrawStatusDot(const char* label, bool active)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float radius = 3.5f;
        const ImU32 dotColor = active ? ColorU32(EColor::Success) : ColorU32(EColor::Danger);
        drawList->AddCircleFilled(ImVec2(pos.x + radius, pos.y + ImGui::GetTextLineHeight() * 0.5f), radius, dotColor);
        ImGui::Dummy(ImVec2(radius * 2.0f + 4.0f, ImGui::GetTextLineHeight()));
        ImGui::SameLine(0.0f, 3.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
    }

    void DrawBadge(const char* label, ImVec4 background, ImVec4 foreground)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 padding(7.0f, 2.0f);
        const ImVec2 size(textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pos, pos + size, ImGui::GetColorU32(background), 4.0f);
        drawList->AddRect(pos, pos + size, ColorU32(EColor::Border, 0.62f), 4.0f);
        drawList->AddText(pos + padding, ImGui::GetColorU32(foreground), label);
        ImGui::Dummy(size);
    }

    void DrawMetricCard(const char* label, const char* value, ImVec4 valueColor, float width)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetTextLineHeight() * 2.35f;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pos, pos + ImVec2(width, height), ColorU32(EColor::Background, 0.72f), 5.0f);
        drawList->AddRect(pos, pos + ImVec2(width, height), ColorU32(EColor::Border, 0.9f), 5.0f);
        drawList->AddText(pos + ImVec2(8.0f, 5.0f), ColorU32(EColor::TextMuted), label);
        drawList->AddText(pos + ImVec2(8.0f, 5.0f + ImGui::GetTextLineHeight()), ImGui::GetColorU32(valueColor), value);
        ImGui::Dummy(ImVec2(width, height));
    }

    void DrawThinSeparator(float alpha)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddLine(pos, ImVec2(pos.x + ImGui::GetContentRegionAvail().x, pos.y), ColorU32(EColor::Border, alpha), 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    void DrawVerticalSeparator(float height, float spacing, float alpha)
    {
        ImGui::SameLine(0.0f, spacing);
        const ImVec2 separatorMin = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(separatorMin.x, separatorMin.y + 1.0f),
            ImVec2(separatorMin.x, separatorMin.y + height - 1.0f),
            ColorU32(EColor::Border, alpha));
        ImGui::Dummy(ImVec2(1.0f, height));
        ImGui::SameLine(0.0f, spacing);
    }

    void DrawProgressBar(float fraction, ImVec4 color, ImVec2 size)
    {
        fraction = std::clamp(fraction, 0.0f, 1.0f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pos, pos + size, ColorU32(EColor::Background, 0.86f), 4.0f);
        drawList->AddRect(pos, pos + size, ColorU32(EColor::BorderStrong, 0.88f), 4.0f);
        if (fraction > 0.0f)
        {
            const float fillWidth = std::max(2.0f, size.x * fraction);
            drawList->AddRectFilled(pos + ImVec2(1.0f, 1.0f), pos + ImVec2(fillWidth - 1.0f, size.y - 1.0f),
                                    ImGui::GetColorU32(color), 3.0f);
        }
        ImGui::Dummy(size);
    }

    bool ModeRailButton(const char* icon, const char* tooltip, bool active, float buttonSize)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Color(EColor::Accent, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::Accent, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::Accent, 0.45f));
            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::AccentHover));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.65f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::SurfaceHover, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
        }
        const bool pressed = ImGui::Button(icon ? icon : "?", ImVec2(buttonSize, buttonSize));
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);

        if (active)
        {
            // 4px accent strip on the left edge, like the mockup.
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            const float stripWidth = 3.0f;
            const float stripPad = 6.0f;
            drawList->AddRectFilled(
                ImVec2(itemMin.x - 4.0f, itemMin.y + stripPad),
                ImVec2(itemMin.x - 4.0f + stripWidth, itemMax.y - stripPad),
                ColorU32(EColor::AccentHover), stripWidth * 0.5f);
        }

        DrawTooltip(tooltip);
        return pressed;
    }

    bool BeginFloatingPanel(const char* id, const char* icon, const char* title, bool* pOpen,
                             ImVec2 position, ImVec2 size, ImVec2 pivot)
    {
        ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.94f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(EColor::Surface, 0.96f));

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavInputs;

        const bool visible = ImGui::Begin(id, nullptr, flags);
        if (!visible)
        {
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            return false;
        }

        // Header strip
        constexpr float headerHeight = 38.0f;
        const ImVec2 winPos = ImGui::GetWindowPos();
        const ImVec2 winSize = ImGui::GetWindowSize();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            winPos, ImVec2(winPos.x + winSize.x, winPos.y + headerHeight),
            ColorU32(EColor::SurfaceElevated, 0.88f), 8.0f, ImDrawFlags_RoundCornersTop);
        drawList->AddLine(
            ImVec2(winPos.x, winPos.y + headerHeight),
            ImVec2(winPos.x + winSize.x, winPos.y + headerHeight),
            ColorU32(EColor::Border, 0.85f));

        // Title text
        const float textY = winPos.y + (headerHeight - ImGui::GetTextLineHeight()) * 0.5f;
        const float textX = winPos.x + 14.0f;
        const ImU32 iconCol = ColorU32(EColor::AccentHover);
        const ImU32 titleCol = ColorU32(EColor::Text);
        if (icon != nullptr && icon[0] != '\0')
        {
            drawList->AddText(ImVec2(textX, textY), iconCol, icon);
            const float iconWidth = ImGui::CalcTextSize(icon).x;
            drawList->AddText(ImVec2(textX + iconWidth + 8.0f, textY), titleCol, title ? title : "");
        }
        else
        {
            drawList->AddText(ImVec2(textX, textY), titleCol, title ? title : "");
        }

        // Optional close X
        if (pOpen != nullptr)
        {
            const float closeSize = 20.0f;
            ImGui::SetCursorScreenPos(ImVec2(winPos.x + winSize.x - closeSize - 10.0f,
                                             winPos.y + (headerHeight - closeSize) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::SurfaceHover));
            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            if (ImGui::Button(ICON_FA_XMARK, ImVec2(closeSize, closeSize)))
            {
                *pOpen = false;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
        }

        // Move cursor below header & start a child for the body so padding works as expected.
        ImGui::SetCursorScreenPos(ImVec2(winPos.x, winPos.y + headerHeight));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##FloatingPanelBody", ImVec2(0, 0), false, ImGuiWindowFlags_NoBackground);
        return true;
    }

    void EndFloatingPanel()
    {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    bool BeginPanelSection(const char* label, bool defaultOpen)
    {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Color(EColor::SurfaceHover, 0.65f));
        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::Text, 0.92f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 4.0f));

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
        if (defaultOpen)
        {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        const bool open = ImGui::CollapsingHeader(label ? label : "", flags);

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);

        if (open)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));
            ImGui::Dummy(ImVec2(0.0f, 1.0f));
        }
        return open;
    }

    void EndPanelSection()
    {
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    void LabelOver(const char* label)
    {
        if (label == nullptr || label[0] == '\0')
        {
            return;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
    }

    void Sparkline(const float* values, int count, ImVec2 size, ImVec4 color,
                   float scaleMin, float scaleMax)
    {
        if (values == nullptr || count <= 1)
        {
            ImGui::Dummy(size);
            return;
        }

        if (size.x <= 0.0f)
        {
            size.x = ImGui::GetContentRegionAvail().x;
        }
        if (size.y <= 0.0f)
        {
            size.y = ImGui::GetTextLineHeight() * 1.6f;
        }

        if (scaleMin == FLT_MAX || scaleMax == FLT_MAX)
        {
            float lo = values[0];
            float hi = values[0];
            for (int i = 1; i < count; ++i)
            {
                lo = std::min(lo, values[i]);
                hi = std::max(hi, values[i]);
            }
            scaleMin = lo;
            scaleMax = hi;
        }
        const float range = std::max(0.0001f, scaleMax - scaleMin);

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, origin + size, ColorU32(EColor::Background, 0.55f), 4.0f);

        const float stepX = size.x / static_cast<float>(count - 1);
        ImU32 lineCol = ImGui::GetColorU32(color);
        ImU32 fillCol = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, color.w * 0.18f));

        // Build polyline
        std::vector<ImVec2> pts;
        pts.reserve(count);
        for (int i = 0; i < count; ++i)
        {
            const float t = (values[i] - scaleMin) / range;
            const float x = origin.x + stepX * static_cast<float>(i);
            const float y = origin.y + size.y - 2.0f - t * (size.y - 4.0f);
            pts.emplace_back(x, y);
        }

        // Fill underneath
        const ImVec2 baseRight(pts.back().x, origin.y + size.y);
        const ImVec2 baseLeft(pts.front().x, origin.y + size.y);
        for (int i = 0; i + 1 < count; ++i)
        {
            ImVec2 quad[4] = {pts[i], pts[i + 1],
                              ImVec2(pts[i + 1].x, origin.y + size.y),
                              ImVec2(pts[i].x, origin.y + size.y)};
            drawList->AddConvexPolyFilled(quad, 4, fillCol);
        }
        (void)baseRight; (void)baseLeft;

        drawList->AddPolyline(pts.data(), count, lineCol, ImDrawFlags_None, 1.5f);
        ImGui::Dummy(size);
    }
} // namespace NextUI::Theme
