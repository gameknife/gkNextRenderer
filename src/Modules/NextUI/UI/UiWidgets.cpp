#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/UI/UiWidgets.hpp"

#include "Modules/NextUI/UI/UiScopes.hpp"
#include "Modules/NextUI/UI/UiTheme.hpp"

#include <imgui_internal.h>

namespace NextUI::Foundation
{
    void Tooltip(const char* text)
    {
        if (text == nullptr || text[0] == '\0' || !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            return;
        }
        ImGui::SetNextWindowSizeConstraints(ImVec2(), ImVec2(420.0f, FLT_MAX));
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    bool Button(const char* label, const FButtonOptions& options)
    {
        FScopedStyle style;
        const bool transparent = options.variant == EButtonVariant::Ghost || options.variant == EButtonVariant::Toolbar;
        const EColor activeColor = options.variant == EButtonVariant::Danger ? EColor::Danger
            : options.tone == EButtonTone::Success ? EColor::Success
            : options.tone == EButtonTone::Warning ? EColor::Warning
            : EColor::Accent;
        style.Add(ImGuiStyleVar_FrameRounding, options.variant == EButtonVariant::Toolbar ? 4.0f : 5.0f)
             .Add(ImGuiStyleVar_FrameBorderSize, options.variant == EButtonVariant::Ghost ? 0.0f
                                                                                         : ImGui::GetStyle().FrameBorderSize)
             .Add(ImGuiCol_Button, options.active ? Color(activeColor, 0.72f)
                                                  : transparent ? ImVec4(0, 0, 0, 0)
                                                                : Color(EColor::SurfaceElevated, 0.94f))
             .Add(ImGuiCol_ButtonHovered, Color(options.variant == EButtonVariant::Danger ? EColor::Danger
                                                                                          : EColor::SurfaceHover))
             .Add(ImGuiCol_ButtonActive, Color(activeColor, 0.86f));
        FScopedDisabled disabled(options.disabled);
        const bool pressed = ImGui::Button(label != nullptr ? label : "", options.size);
        Tooltip(options.tooltip);
        return pressed;
    }

    bool IconButton(const char* icon, const char* tooltip, const bool active, ImVec2 size,
                    const bool activeUnderline)
    {
        if (size.x <= 0.0f) size.x = ImGui::GetFrameHeight();
        if (size.y <= 0.0f) size.y = ImGui::GetFrameHeight();

        const char* label = icon != nullptr ? icon : "";
        const char* visibleEnd = strstr(label, "##");
        if (visibleEnd == nullptr)
        {
            visibleEnd = label + strlen(label);
        }

        const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        bool pressed = false;
        {
            FScopedStyle style;
            const bool showActiveFill = active && !activeUnderline;
            style.Add(ImGuiStyleVar_FrameRounding, 4.0f)
                .Add(ImGuiStyleVar_FrameBorderSize, 0.0f)
                .Add(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f))
                .Add(ImGuiCol_Text, ImVec4(textColor.x, textColor.y, textColor.z, 0.0f))
                .Add(ImGuiCol_Border, ImVec4(0, 0, 0, 0))
                .Add(ImGuiCol_Button, showActiveFill ? Color(EColor::Accent, 0.72f) : ImVec4(0, 0, 0, 0))
                .Add(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover))
                .Add(ImGuiCol_ButtonActive, activeUnderline ? Color(EColor::SurfaceHover)
                                                             : Color(EColor::Accent, 0.86f));

            pressed = ImGui::Button(label, size);
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            const ImVec2 itemCenter = (itemMin + itemMax) * 0.5f;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImU32 renderedTextColor = ImGui::GetColorU32(textColor);

            unsigned int codepoint = 0;
            const int codepointBytes = visibleEnd > label ? ImTextCharFromUtf8(&codepoint, label, visibleEnd) : 0;
            const bool singleGlyph = codepointBytes > 0 && label + codepointBytes == visibleEnd;
            const ImFontGlyph* glyph = singleGlyph
                ? ImGui::GetFontBaked()->FindGlyphNoFallback(static_cast<ImWchar>(codepoint))
                : nullptr;

            drawList->PushClipRect(itemMin, itemMax, true);
            if (glyph != nullptr && glyph->Visible)
            {
                const ImVec2 drawPosition(
                    itemCenter.x - (glyph->X0 + glyph->X1) * 0.5f,
                    itemCenter.y - (glyph->Y0 + glyph->Y1) * 0.5f);
                drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), drawPosition,
                                  renderedTextColor, label, visibleEnd);
            }
            else if (visibleEnd > label)
            {
                const ImVec2 textSize = ImGui::CalcTextSize(label, visibleEnd, false);
                const ImVec2 drawPosition = itemCenter - textSize * 0.5f;
                drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), drawPosition,
                                  renderedTextColor, label, visibleEnd);
            }
            if (active && activeUnderline)
            {
                drawList->AddRectFilled(
                    ImVec2(itemMin.x + 3.0f, itemMax.y - 2.0f),
                    ImVec2(itemMax.x - 3.0f, itemMax.y),
                    ColorU32(EColor::Accent, 0.95f), 1.0f);
            }
            drawList->PopClipRect();
        }

        Tooltip(tooltip);
        return pressed;
    }

    bool ComboOption(const char* label, const bool selected)
    {
        FScopedStyle style;
        style.Add(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
        return ImGui::Selectable(label, selected, ImGuiSelectableFlags_None, ImVec2(0.0f, ImGui::GetFrameHeight()));
    }
}
