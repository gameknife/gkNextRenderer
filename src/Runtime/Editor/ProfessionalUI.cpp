#include "Common/CoreMinimal.hpp"

#include "Runtime/Editor/ProfessionalUI.hpp"
#include "Runtime/Engine.hpp"

#include <algorithm>
#include <imgui_internal.h>
#include <fmt/format.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

namespace Runtime::UiTheme
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
        colors[ImGuiCol_WindowBg] = Color(EColor::Surface);
        colors[ImGuiCol_ChildBg] = Color(EColor::Background, 0.72f);
        colors[ImGuiCol_PopupBg] = Color(EColor::SurfaceElevated, 0.98f);
        colors[ImGuiCol_Border] = Color(EColor::Border);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_FrameBg] = Color(EColor::Background);
        colors[ImGuiCol_FrameBgHovered] = Color(EColor::SurfaceHover);
        colors[ImGuiCol_FrameBgActive] = Color(EColor::SurfaceHover);
        colors[ImGuiCol_TitleBg] = Color(EColor::Background);
        colors[ImGuiCol_TitleBgActive] = Color(EColor::Background);
        colors[ImGuiCol_TitleBgCollapsed] = Color(EColor::Background);
        colors[ImGuiCol_MenuBarBg] = Color(EColor::Background);
        colors[ImGuiCol_ScrollbarBg] = Color(EColor::Background, 0.40f);
        colors[ImGuiCol_ScrollbarGrab] = Color(EColor::BorderStrong);
        colors[ImGuiCol_ScrollbarGrabHovered] = Color(EColor::TextDim);
        colors[ImGuiCol_ScrollbarGrabActive] = Color(EColor::TextMuted);
        colors[ImGuiCol_CheckMark] = Color(EColor::AccentHover);
        colors[ImGuiCol_SliderGrab] = Color(EColor::Blue);
        colors[ImGuiCol_SliderGrabActive] = Color(EColor::AccentHover);
        colors[ImGuiCol_Button] = Color(EColor::SurfaceElevated);
        colors[ImGuiCol_ButtonHovered] = Color(EColor::SurfaceHover);
        colors[ImGuiCol_ButtonActive] = Color(EColor::Accent);
        colors[ImGuiCol_Header] = Color(EColor::SurfaceElevated);
        colors[ImGuiCol_HeaderHovered] = Color(EColor::SurfaceHover);
        colors[ImGuiCol_HeaderActive] = Color(EColor::Accent, 0.85f);
        colors[ImGuiCol_Separator] = Color(EColor::Border);
        colors[ImGuiCol_SeparatorHovered] = Color(EColor::AccentHover);
        colors[ImGuiCol_SeparatorActive] = Color(EColor::AccentHover);
        colors[ImGuiCol_ResizeGrip] = Color(EColor::BorderStrong, 0.55f);
        colors[ImGuiCol_ResizeGripHovered] = Color(EColor::AccentHover);
        colors[ImGuiCol_ResizeGripActive] = Color(EColor::AccentHover);
        colors[ImGuiCol_Tab] = Color(EColor::Surface);
        colors[ImGuiCol_TabHovered] = Color(EColor::SurfaceHover);
        colors[ImGuiCol_TabActive] = Color(EColor::SurfaceElevated);
        colors[ImGuiCol_TabUnfocused] = Color(EColor::Surface, 0.82f);
        colors[ImGuiCol_TabUnfocusedActive] = Color(EColor::SurfaceElevated, 0.88f);
        colors[ImGuiCol_DockingPreview] = Color(EColor::Accent, 0.55f);
        colors[ImGuiCol_DockingEmptyBg] = Color(EColor::Background);
        colors[ImGuiCol_PlotHistogram] = Color(EColor::Success);
        colors[ImGuiCol_PlotHistogramHovered] = Color(EColor::AccentHover);
        colors[ImGuiCol_TextSelectedBg] = Color(EColor::Accent, 0.45f);
        colors[ImGuiCol_NavHighlight] = Color(EColor::AccentHover);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.62f);

        style.WindowPadding = ImVec2(10.0f, 8.0f);
        style.FramePadding = ImVec2(8.0f, 5.0f);
        style.CellPadding = ImVec2(8.0f, 5.0f);
        style.ItemSpacing = ImVec2(7.0f, 6.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
        style.IndentSpacing = 14.0f;
        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 12.0f;
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.TabBorderSize = 0.0f;
        style.WindowRounding = 6.0f;
        style.ChildRounding = 5.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 6.0f;
        style.ScrollbarRounding = 7.0f;
        style.GrabRounding = 6.0f;
        style.TabRounding = 4.0f;
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

        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + brandWidth, viewport->Pos.y));
        ImGui::SetNextWindowSize(ImVec2(menuWidth, config.Height));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(config.MenuWindowId, nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoDocking);
        ImGui::PopStyleVar();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 11.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(16.0f, 0.0f));
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

        const float dragLeftReserved =
            std::max(brandWidth + 12.0f, menuRight - viewport->Pos.x + config.MenuHitPadding);
        engine.ConfigureCustomTitleBarDrag(true, config.Height, dragLeftReserved, rightWidth);
    }

    void DrawTooltip(const char* text)
    {
        if (!ImGui::IsItemHovered() || text == nullptr || text[0] == '\0')
        {
            return;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(text);
        ImGui::EndTooltip();
        ImGui::PopStyleVar();
    }

    bool IconButton(const char* label, const char* tooltip, bool active, ImVec2 size)
    {
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Color(EColor::Accent, 0.82f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::AccentHover));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::AccentHover));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Color(EColor::SurfaceElevated, 0.86f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::Accent, 0.72f));
        }

        const bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(3);
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

    bool BeginSection(const char* icon, const char* label, bool defaultOpen)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, Color(EColor::Background, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Color(EColor::SurfaceHover));

        const std::string header = fmt::format("{} {}", icon ? icon : "", label ? label : "");
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;
        if (defaultOpen)
        {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        const bool open = ImGui::CollapsingHeader(header.c_str(), flags);

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        if (open)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7.0f, 6.0f));
            ImGui::Indent(4.0f);
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
        ImGui::TextUnformatted(label);
    }

    void DrawBadge(const char* label, ImVec4 background, ImVec4 foreground)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 padding(8.0f, 3.0f);
        const ImVec2 size(textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pos, pos + size, ImGui::GetColorU32(background), 4.0f);
        drawList->AddRect(pos, pos + size, ColorU32(EColor::Border, 0.75f), 4.0f);
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
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
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
} // namespace Runtime::UiTheme
