#include "KongLie3DStyle.hpp"

namespace
{
    ImVec4 WithAlpha(const ImVec4& color, float alpha)
    {
        return ImVec4(color.x, color.y, color.z, alpha);
    }

    ImVec4 MixColor(const ImVec4& lhs, const ImVec4& rhs, float t)
    {
        return ImVec4(lhs.x + (rhs.x - lhs.x) * t,
                      lhs.y + (rhs.y - lhs.y) * t,
                      lhs.z + (rhs.z - lhs.z) * t,
                      lhs.w + (rhs.w - lhs.w) * t);
    }
}

namespace KongLie3D::KongLieFonts
{
    ImFont* Body = nullptr;
    ImFont* Title = nullptr;
    ImFont* Display = nullptr;
}

namespace KongLie3D
{
    void ApplyKongLieImGuiStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 8.0f;
        style.ChildRounding = 8.0f;
        style.FrameRounding = 6.0f;
        style.GrabRounding = 6.0f;
        style.PopupRounding = 8.0f;
        style.ScrollbarRounding = 8.0f;
        style.TabRounding = 6.0f;
        style.WindowPadding = ImVec2(12.0f, 10.0f);
        style.FramePadding = ImVec2(10.0f, 6.0f);
        style.ItemSpacing = ImVec2(8.0f, 6.0f);
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;

        style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.97f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_TextDisabled] = Style::TextDim;
        style.Colors[ImGuiCol_WindowBg] = Style::Surface;
        style.Colors[ImGuiCol_PopupBg] = Style::Surface;
        style.Colors[ImGuiCol_ChildBg] = WithAlpha(Style::SurfaceAlt, 0.45f);
        style.Colors[ImGuiCol_Border] = Style::Border;
        style.Colors[ImGuiCol_TitleBg] = Style::SurfaceAlt;
        style.Colors[ImGuiCol_TitleBgActive] = MixColor(Style::SurfaceAlt, Style::Accent, 0.28f);
        style.Colors[ImGuiCol_TitleBgCollapsed] = Style::SurfaceAlt;
        style.Colors[ImGuiCol_Button] = MixColor(Style::SurfaceAlt, Style::Accent, 0.18f);
        style.Colors[ImGuiCol_ButtonHovered] = MixColor(Style::SurfaceAlt, Style::Accent, 0.45f);
        style.Colors[ImGuiCol_ButtonActive] = MixColor(Style::SurfaceAlt, Style::Accent, 0.62f);
        style.Colors[ImGuiCol_Header] = MixColor(Style::SurfaceAlt, Style::Accent, 0.26f);
        style.Colors[ImGuiCol_HeaderHovered] = MixColor(Style::SurfaceAlt, Style::Accent, 0.52f);
        style.Colors[ImGuiCol_HeaderActive] = MixColor(Style::SurfaceAlt, Style::Accent, 0.64f);
        style.Colors[ImGuiCol_Tab] = MixColor(Style::SurfaceAlt, Style::Accent, 0.18f);
        style.Colors[ImGuiCol_TabHovered] = MixColor(Style::SurfaceAlt, Style::Accent, 0.48f);
        style.Colors[ImGuiCol_TabActive] = MixColor(Style::SurfaceAlt, Style::Accent, 0.42f);
        style.Colors[ImGuiCol_Separator] = Style::Border;
        style.Colors[ImGuiCol_FrameBg] = WithAlpha(Style::SurfaceAlt, 0.90f);
        style.Colors[ImGuiCol_FrameBgHovered] = MixColor(Style::SurfaceAlt, Style::Accent, 0.34f);
        style.Colors[ImGuiCol_FrameBgActive] = MixColor(Style::SurfaceAlt, Style::Accent, 0.48f);
        style.Colors[ImGuiCol_TableRowBg] = WithAlpha(Style::SurfaceAlt, 0.22f);
        style.Colors[ImGuiCol_TableRowBgAlt] = WithAlpha(Style::SurfaceAlt, 0.38f);
        style.Colors[ImGuiCol_CheckMark] = Style::Highlight;
        style.Colors[ImGuiCol_SliderGrab] = Style::Accent;
        style.Colors[ImGuiCol_SliderGrabActive] = Style::Highlight;
        style.Colors[ImGuiCol_ScrollbarBg] = WithAlpha(Style::SurfaceAlt, 0.35f);
        style.Colors[ImGuiCol_ScrollbarGrab] = WithAlpha(Style::TextDim, 0.45f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = WithAlpha(Style::Accent, 0.70f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = Style::Accent;
    }

    void PushPanelStyle()
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Style::Surface);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, WithAlpha(Style::SurfaceAlt, 0.40f));
        ImGui::PushStyleColor(ImGuiCol_Border, Style::Border);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    }

    void PopPanelStyle()
    {
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
    }
}
