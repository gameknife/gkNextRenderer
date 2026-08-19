#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Runtime/Editor/UI/UiTheme.hpp"

namespace NextUI::Foundation
{
    namespace
    {
        ImVec4 WithAlpha(ImVec4 color, const float alpha)
        {
            color.w *= alpha;
            return color;
        }
    }

    ImVec4 Color(const EColor color, const float alpha)
    {
        ImVec4 result;
        switch (color)
        {
        case EColor::Text: result = ImVec4(0.88f, 0.90f, 0.93f, 1.0f); break;
        case EColor::TextMuted: result = ImVec4(0.63f, 0.67f, 0.73f, 1.0f); break;
        case EColor::TextDim: result = ImVec4(0.43f, 0.47f, 0.53f, 1.0f); break;
        case EColor::Background: result = ImVec4(0.055f, 0.058f, 0.064f, 1.0f); break;
        case EColor::Surface: result = ImVec4(0.105f, 0.112f, 0.122f, 1.0f); break;
        case EColor::SurfaceElevated: result = ImVec4(0.145f, 0.153f, 0.166f, 1.0f); break;
        case EColor::SurfaceHover: result = ImVec4(0.185f, 0.198f, 0.218f, 1.0f); break;
        case EColor::Border: result = ImVec4(0.22f, 0.235f, 0.255f, 1.0f); break;
        case EColor::BorderStrong: result = ImVec4(0.30f, 0.325f, 0.36f, 1.0f); break;
        case EColor::Accent: result = ImVec4(0.18f, 0.43f, 0.78f, 1.0f); break;
        case EColor::AccentHover: result = ImVec4(0.26f, 0.53f, 0.90f, 1.0f); break;
        case EColor::Brand: result = ImVec4(0.95f, 0.58f, 0.14f, 1.0f); break;
        case EColor::Success: result = ImVec4(0.22f, 0.78f, 0.38f, 1.0f); break;
        case EColor::Warning: result = ImVec4(0.95f, 0.70f, 0.24f, 1.0f); break;
        case EColor::Danger: result = ImVec4(0.92f, 0.25f, 0.28f, 1.0f); break;
        case EColor::Blue:
        default: result = ImVec4(0.38f, 0.62f, 0.94f, 1.0f); break;
        }
        return WithAlpha(result, alpha);
    }

    ImU32 ColorU32(const EColor color, const float alpha)
    {
        return ImGui::GetColorU32(Color(color, alpha));
    }

    void ApplyTheme()
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
        style.DisabledAlpha = 0.42f;
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
}
