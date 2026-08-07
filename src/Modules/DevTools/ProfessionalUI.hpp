#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"

#include <functional>
#include <imgui.h>

namespace NextUI::Theme
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
        float Height = 48.0f;
        float RightContentWidth = 0.0f;
        float BrandHorizontalPadding = 14.0f;
        float BrandIconSize = 48.0f;
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

    struct FBottomBarConfig
    {
        const char* WindowId = "AppBottomBar";
        float Height = 30.0f;
        float HorizontalPadding = 10.0f;
        float VerticalPadding = 4.0f;
        float CenterWidth = 0.0f;
        float RightWidth = 0.0f;
        std::function<void()> DrawLeftContent;
        std::function<void()> DrawCenterContent;
        std::function<void()> DrawRightContent;
    };

    struct FOverlayPanelConfig
    {
        const char* WindowId = "OverlayPanel";
        ImVec2 Position = ImVec2(0.0f, 0.0f);
        ImVec2 Size = ImVec2(0.0f, 0.0f);
        ImVec2 Padding = ImVec2(10.0f, 5.0f);
        ImVec2 ItemSpacing = ImVec2(6.0f, 0.0f);
        float Rounding = 8.0f;
        float BorderAlpha = 0.74f;
        float BackgroundAlpha = 0.82f;
        EColor BackgroundColor = EColor::Background;
        ImGuiWindowFlags ExtraFlags = 0;
    };

    void ApplyProfessionalTheme();
    void DrawBrandMark(ImDrawList* drawList, ImVec2 min, float size);
    void DrawAppTitleBar(NextEngine& engine, const FAppTitleBarConfig& config);
    void DrawBottomBar(const FBottomBarConfig& config);
    void DrawStandardBottomBar(NextEngine& engine, const char* windowId = "AppBottomBar", float height = 30.0f,
                               std::function<void()> onMemoryClicked = {}, bool memoryActive = false,
                               std::function<void()> onCppReloadClicked = {}, bool cppLiveCodingAvailable = false,
                               std::function<void()> onCaptureClicked = {});
    void DrawTooltip(const char* text);
    bool IconButton(const char* label, const char* tooltip, bool active = false, ImVec2 size = ImVec2(30.0f, 30.0f));
    bool ToolbarButton(const char* label, const char* tooltip, bool active = false, ImVec2 size = ImVec2(34.0f, 30.0f));
    bool ModeRailButton(const char* icon, const char* tooltip, bool active, float buttonSize);
    bool BeginSection(const char* icon, const char* label, bool defaultOpen = true);
    void EndSection();
    void BeginFormRow(const char* label, float ratio = 0.40f, float minLabelWidth = 96.0f, float maxLabelWidth = 140.0f);
    bool BeginInsetPanel(const char* id, ImVec2 size = ImVec2(0.0f, 0.0f), bool border = true,
                         ImGuiWindowFlags flags = 0, ImVec2 padding = ImVec2(10.0f, 10.0f),
                         float backgroundAlpha = 0.30f);
    void EndInsetPanel();
    bool BeginOverlayPanel(const FOverlayPanelConfig& config);
    void EndOverlayPanel();

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
    // When scaleMin/scaleMax are left at FLT_MAX the range auto-fits the dataset.
    // Set baselineAtZero=true (in auto-fit mode) to pin scaleMin=0 so timing-style
    // data (frame time, pass ms) doesn't visually amplify small noise — the line
    // wiggles far less than the default min..max fit.
    void Sparkline(const float* values, int count, ImVec2 size, ImVec4 color,
                   float scaleMin = FLT_MAX, float scaleMax = FLT_MAX,
                   bool baselineAtZero = false);

    void DrawPanelHeader(const char* icon, const char* title, const char* subtitle = nullptr);
    void DrawLabelValue(const char* label, const char* value, ImVec4 valueColor = Color(EColor::Text));
    void DrawStatusDot(const char* label, bool active);
    void DrawBadge(const char* label, ImVec4 background, ImVec4 foreground);
    void DrawMetricCard(const char* label, const char* value, ImVec4 valueColor, float width);
    void DrawThinSeparator(float alpha = 1.0f);
    void DrawVerticalSeparator(float height = 18.0f, float spacing = 12.0f, float alpha = 0.9f);
    void DrawProgressBar(float fraction, ImVec4 color, ImVec2 size);
} // namespace NextUI::Theme
