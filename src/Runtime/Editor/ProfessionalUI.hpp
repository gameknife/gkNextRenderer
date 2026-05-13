#pragma once

#include "Common/CoreMinimal.hpp"

#include <imgui.h>

namespace Runtime::UiTheme
{
    enum class EColor
    {
        Text,
        TextMuted,
        TextDim,
        Background,
        Surface,
        SurfaceElevated,
        SurfaceHover,
        Border,
        BorderStrong,
        Accent,
        AccentHover,
        Brand,
        Success,
        Warning,
        Danger,
        Blue,
    };

    ImVec4 Color(EColor color, float alpha = 1.0f);
    ImU32 ColorU32(EColor color, float alpha = 1.0f);

    void ApplyProfessionalTheme();
    void DrawBrandMark(ImDrawList* drawList, ImVec2 min, float size);
    void DrawTooltip(const char* text);
    bool IconButton(const char* label, const char* tooltip, bool active = false, ImVec2 size = ImVec2(30.0f, 30.0f));
    bool ToolbarButton(const char* label, const char* tooltip, bool active = false, ImVec2 size = ImVec2(34.0f, 30.0f));
    bool BeginSection(const char* icon, const char* label, bool defaultOpen = true);
    void EndSection();
    void DrawPanelHeader(const char* icon, const char* title, const char* subtitle = nullptr);
    void DrawLabelValue(const char* label, const char* value, ImVec4 valueColor = Color(EColor::Text));
    void DrawStatusDot(const char* label, bool active);
    void DrawBadge(const char* label, ImVec4 background, ImVec4 foreground);
    void DrawMetricCard(const char* label, const char* value, ImVec4 valueColor, float width);
    void DrawThinSeparator(float alpha = 1.0f);
    void DrawProgressBar(float fraction, ImVec4 color, ImVec2 size);
} // namespace Runtime::UiTheme
