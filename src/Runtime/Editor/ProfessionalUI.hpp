#pragma once

#include "Common/CoreMinimal.hpp"

#include <functional>
#include <imgui.h>

class NextEngine;

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

    struct FAppTitleBarConfig
    {
        const char* BrandWindowId = "AppTitleBarBrand";
        const char* MenuWindowId = "AppTitleBarMenu";
        const char* RightWindowId = "AppTitleBarRight";
        const char* AppName = "";
        float Height = 44.0f;
        float RightContentWidth = 0.0f;
        float BrandHorizontalPadding = 14.0f;
        float BrandIconSize = 22.0f;
        float BrandTextSpacing = 10.0f;
        float MenuHitPadding = 28.0f;
        float MenuTrailingPadding = 8.0f;
        ImFont* TitleFont = nullptr;
        bool IsMaximized = false;
        std::function<float()> DrawMenuBar;
        std::function<void()> DrawRightContent;
        std::function<void()> OnMinimize;
        std::function<void()> OnToggleMaximize;
        std::function<void()> OnClose;
    };

    void ApplyProfessionalTheme();
    void DrawBrandMark(ImDrawList* drawList, ImVec2 min, float size);
    void DrawAppTitleBar(NextEngine& engine, const FAppTitleBarConfig& config);
    void DrawTooltip(const char* text);
    bool IconButton(const char* label, const char* tooltip, bool active = false, ImVec2 size = ImVec2(30.0f, 30.0f));
    bool ToolbarButton(const char* label, const char* tooltip, bool active = false, ImVec2 size = ImVec2(34.0f, 30.0f));
    bool ModeRailButton(const char* icon, const char* tooltip, bool active, float buttonSize);
    bool BeginSection(const char* icon, const char* label, bool defaultOpen = true);
    void EndSection();

    // Floating panel matching the new design language: rounded surface, single-line title with optional close X.
    // pOpen may be null. Returns true when the panel body is visible (matches ImGui::Begin semantics).
    bool BeginFloatingPanel(const char* id, const char* icon, const char* title, bool* pOpen,
                             ImVec2 position, ImVec2 size, ImVec2 pivot = ImVec2(0.0f, 0.0f));
    void EndFloatingPanel();

    // Collapsible section inside a floating panel: chevron + title row, no border background.
    bool BeginPanelSection(const char* label, bool defaultOpen = true);
    void EndPanelSection();

    // Renders "Label" small caption above the next control. Use right before a Combo / Slider / etc.
    void LabelOver(const char* label);

    // Inline sparkline. width<=0 fills available width.
    void Sparkline(const float* values, int count, ImVec2 size, ImVec4 color,
                   float scaleMin = FLT_MAX, float scaleMax = FLT_MAX);

    void DrawPanelHeader(const char* icon, const char* title, const char* subtitle = nullptr);
    void DrawLabelValue(const char* label, const char* value, ImVec4 valueColor = Color(EColor::Text));
    void DrawStatusDot(const char* label, bool active);
    void DrawBadge(const char* label, ImVec4 background, ImVec4 foreground);
    void DrawMetricCard(const char* label, const char* value, ImVec4 valueColor, float width);
    void DrawThinSeparator(float alpha = 1.0f);
    void DrawProgressBar(float fraction, ImVec4 color, ImVec2 size);
} // namespace Runtime::UiTheme
