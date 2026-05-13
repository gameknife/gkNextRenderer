#include "Common/CoreMinimal.hpp"

#include "Runtime/Editor/ProfessionalUI.hpp"

#include <algorithm>
#include <fmt/format.h>

namespace Runtime::UiTheme
{
    namespace
    {
        ImVec4 WithAlpha(ImVec4 color, float alpha)
        {
            color.w *= alpha;
            return color;
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
} // namespace Runtime::UiTheme
