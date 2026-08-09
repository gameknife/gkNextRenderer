#include "Engine/Common/CoreMinimal.hpp"

#include "DiagnosticWidgets.hpp"

#include "Engine/Utilities/Localization.hpp"

namespace Runtime::DevToolsUI
{
    const char* GetBoolStatusLabel(const bool value)
    {
        return value ? LOCTEXT("On") : LOCTEXT("Off");
    }

    void DrawBadge(const char* text, const ImVec4& background, const ImVec4& foreground)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        constexpr ImVec2 padding(7.0f, 3.0f);
        const ImVec2 size(textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);

        drawList->AddRectFilled(
            pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(background), 8.0f);
        drawList->AddText(
            ImVec2(pos.x + padding.x, pos.y + padding.y), ImGui::GetColorU32(foreground), text);
        ImGui::Dummy(size);
    }

    void DrawSectionHeader(const char* title)
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.96f, 0.82f, 0.42f, 1.0f), "%s", title);
        ImGui::Separator();
    }

    void DrawValueRow(const char* label, const char* value, const ImVec4& valueColor)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.82f, 1.0f), "%s", label);
        ImGui::SameLine(136.0f);
        ImGui::TextColored(valueColor, "%s", value);
    }

    void DrawValueRow(const char* label, const std::string& value, const ImVec4& valueColor)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.82f, 1.0f), "%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(valueColor, "%s", value.c_str());
    }

    void DrawBooleanRow(const char* label, const bool enabled)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.82f, 1.0f), "%s", label);
        ImGui::SameLine(136.0f);
        DrawBadge(
            GetBoolStatusLabel(enabled),
            enabled ? ImVec4(0.14f, 0.36f, 0.20f, 0.92f) : ImVec4(0.22f, 0.24f, 0.28f, 0.88f),
            enabled ? ImVec4(0.86f, 1.0f, 0.90f, 1.0f) : ImVec4(0.85f, 0.88f, 0.92f, 0.96f));
    }

    void DrawShortcutRow(const char* shortcut, const char* label, const bool active)
    {
        DrawBadge(
            shortcut,
            active ? ImVec4(0.22f, 0.32f, 0.52f, 0.95f) : ImVec4(0.20f, 0.22f, 0.27f, 0.90f),
            active ? ImVec4(0.92f, 0.97f, 1.0f, 1.0f) : ImVec4(0.82f, 0.85f, 0.90f, 1.0f));
        ImGui::SameLine();
        ImGui::TextColored(
            active ? ImVec4(0.95f, 0.97f, 1.0f, 1.0f) : ImVec4(0.72f, 0.76f, 0.82f, 1.0f),
            "%s",
            label);
    }
}
